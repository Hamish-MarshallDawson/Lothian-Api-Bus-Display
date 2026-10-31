// tracking.cpp: see tracking.h

#include "tracking.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <iostream>

#include "apiHttp.h"
#include "openmappull.h"
#include "settings.h"

using json = nlohmann::json;

static const double DEG = M_PI / 180.0;
static const double STALE_AFTER_S = 120.0;   // position report older than this = Stale
static const double STUCK_RADIUS_M = 40.0;
static const long STUCK_AFTER_S = 180;

// "YYYY-MM-DDTHH:MM:SS" with "Z", "+HH:MM" or "-HH:MM", or with no offset (local)
time_t parseApiTime(const std::string &s) {
    int Y = 0, Mo = 0, D = 0, h = 0, mi = 0, se = 0;
    if (std::sscanf(s.c_str(), "%d-%d-%dT%d:%d:%d", &Y, &Mo, &D, &h, &mi, &se) < 5) return 0;
    std::tm tm{};
    tm.tm_year = Y - 1900;
    tm.tm_mon = Mo - 1;
    tm.tm_mday = D;
    tm.tm_hour = h;
    tm.tm_min = mi;
    tm.tm_sec = se;
    const size_t off = s.find_first_of("Z+-", 10);
    if (off == std::string::npos) {
        tm.tm_isdst = -1;
        return mktime(&tm);
    }
    if (s[off] == 'Z') return timegm(&tm);
    int oh = 0, om = 0;
    std::sscanf(s.c_str() + off + 1, "%d:%d", &oh, &om);
    const long secs = (oh * 3600L + om * 60L) * (s[off] == '-' ? -1 : 1);
    return timegm(&tm) - secs;
}

static std::string localHHMM(time_t t) {
    std::tm tm{};
    localtime_r(&t, &tm);
    char buf[8];
    std::strftime(buf, sizeof(buf), "%H:%M", &tm);
    return buf;
}

// "HH:MM" to minutes since midnight; -1 if unreadable
static int hhmmToMin(const std::string &s) {
    int h = 0, m = 0;
    if (std::sscanf(s.c_str(), "%d:%d", &h, &m) != 2) return -1;
    return h * 60 + m;
}

static std::string todayLocal() {
    time_t now = time(nullptr);
    std::tm tm{};
    localtime_r(&now, &tm);
    char buf[16];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
    return buf;
}

// ---------------------------------------------------------------------------
// Matching a departure to its trip

int matchTripIndex(const std::vector<TripDetail> &details, const Departure &dep,
                   const std::string &myAtco) {
    auto callsAt = [&](const TripDetail &d) {
        for (const auto &e : d.events) if (e.atco == myAtco) return true;
        return false;
    };
    // 1. The journey's start time is the trip's starttime
    if (!dep.journeyStart.empty()) {
        const std::string start = localHHMM(parseApiTime(dep.journeyStart));
        for (size_t i = 0; i < details.size(); ++i) {
            if (details[i].starttime == start && callsAt(details[i])) return static_cast<int>(i);
        }
    }
    // 2. A trip that calls here at the departure's scheduled time
    for (size_t i = 0; i < details.size(); ++i) {
        for (const auto &e : details[i].events) {
            if (e.atco == myAtco && e.departure == dep.scheduled) return static_cast<int>(i);
        }
    }
    return -1;
}

bool findTripForDeparture(const Departure &dep, const BusStopInfo &myStop, std::vector<TrackStop> &trip) {
    trip.clear();
    ServiceRef svc{dep.service, dep.operatorCode.empty() ? std::string("LOTH") : dep.operatorCode};
    std::vector<TripDetail> details;
    // Whole day, once per service (see timetables.h: from/to is refused by the API)
    if (!tripDetailsFor(svc, todayLocal(), details)) return false;
    const int idx = matchTripIndex(details, dep, myStop.stopID);
    if (idx < 0) return false;

    for (const auto &e : details[idx].events) {
        TrackStop s;
        s.atco = e.atco;
        s.scheduled = e.departure;
        LatLon at{0.0, 0.0};
        if (!stopByCode(e.atco, s.name, at)) {
            trip.clear();
            return false;   // a stop we can't place: no usable track
        }
        s.lat = at.lat;
        s.lon = at.lon;
        trip.push_back(s);
    }
    return !trip.empty();
}

// ---------------------------------------------------------------------------
// Geometry

bool projectOnTrip(const std::vector<TrackStop> &trip, LatLon bus, int &seg, double &t) {
    if (trip.size() < 2) return false;
    const double lat0 = trip[0].lat;
    const double kx = std::cos(lat0 * DEG) * 111320.0;   // metres per degree of longitude
    const double ky = 110574.0;                          // metres per degree of latitude
    auto pt = [&](double lat, double lon) {
        return std::pair<double, double>((lon - trip[0].lon) * kx, (lat - lat0) * ky);
    };
    const auto b = pt(bus.lat, bus.lon);

    double best = 1e300;
    for (size_t i = 0; i + 1 < trip.size(); ++i) {
        const auto a = pt(trip[i].lat, trip[i].lon);
        const auto c = pt(trip[i + 1].lat, trip[i + 1].lon);
        const double vx = c.first - a.first, vy = c.second - a.second;
        const double len2 = vx * vx + vy * vy;
        double f = 0.0;
        if (len2 > 0) {
            f = ((b.first - a.first) * vx + (b.second - a.second) * vy) / len2;
            f = std::max(0.0, std::min(1.0, f));
        }
        const double qx = a.first + f * vx - b.first, qy = a.second + f * vy - b.second;
        const double d = std::sqrt(qx * qx + qy * qy);
        if (d < best) {
            best = d;
            seg = static_cast<int>(i);
            t = f;
        }
    }
    return true;
}

// Live minus scheduled, at a point (seg, t) on the trip, in whole minutes
static int lateAt(const std::vector<TrackStop> &trip, int seg, double t, time_t now) {
    const int s0 = hhmmToMin(trip[seg].scheduled);
    int s1 = hhmmToMin(trip[seg + 1].scheduled);
    if (s0 < 0 || s1 < 0) return 0;
    if (s1 < s0) s1 += 1440;                       // the segment crosses midnight
    const double schedAt = s0 + t * (s1 - s0);
    std::tm tm{};
    localtime_r(&now, &tm);
    double nowMin = tm.tm_hour * 60.0 + tm.tm_min + tm.tm_sec / 60.0;
    double late = nowMin - schedAt;
    if (late < -720) late += 1440;
    if (late > 720) late -= 1440;
    return static_cast<int>(std::lround(late));
}

// ---------------------------------------------------------------------------
// The track

BusTrack buildTrack(const Departure &dep, const BusStopInfo &myStop,
                    const std::vector<TrackStop> &trip, const VehicleFeed &feed, time_t now) {
    BusTrack t;
    t.service = dep.service;
    t.destination = dep.destination;
    t.dueMin = dep.minutes;
    t.stops = trip;
    t.state = TrackState::Unavailable;
    if (trip.size() < 2) return t;

    int my = -1;
    for (size_t i = 0; i < trip.size(); ++i) {
        if (trip[i].atco == myStop.stopID && trip[i].scheduled == dep.scheduled) { my = static_cast<int>(i); break; }
    }
    if (my < 0) {
        for (size_t i = 0; i < trip.size(); ++i) {
            if (trip[i].atco == myStop.stopID) { my = static_cast<int>(i); break; }
        }
    }
    if (my < 0) return t;
    t.myStopIndex = my;

    const Vehicle *veh = nullptr;
    for (const auto &v : feed.vehicles()) {
        if (!dep.journeyId.empty() && v.journeyId == dep.journeyId) { veh = &v; break; }
    }
    if (!veh) {
        // Not on the road yet (or no feed): show the origin and its start time
        t.state = TrackState::NotStarted;
        return t;
    }

    t.fleetNo = veh->fleetNo;
    t.busLat = veh->lat;
    t.busLon = veh->lon;
    t.lastUpdateAgeS = veh->lastUpdate ? static_cast<long>(now - veh->lastUpdate) : -1;

    int seg = 0;
    double frac = 0.0;
    projectOnTrip(trip, LatLon{veh->lat, veh->lon}, seg, frac);
    t.nextStopIndex = seg + 1;
    t.stopsAway = my - t.nextStopIndex;

    const bool stale = t.lastUpdateAgeS < 0 || t.lastUpdateAgeS > static_cast<long>(STALE_AFTER_S);
    t.state = stale ? TrackState::Stale : TrackState::Live;
    if (t.nextStopIndex > my) t.state = TrackState::Finished;   // past my stop

    t.lateMin = lateAt(trip, seg, frac, now);
    return t;
}

// ---------------------------------------------------------------------------
// Vehicle feed

static bool parseVehicle(const json &v, Vehicle &x) {
    if (!v.is_object()) return false;
    auto coord = v.find("coordinate");
    if (coord == v.end() || !coord->is_object()) return false;
    auto jid = v.find("journeyid");
    if (jid == v.end() || !jid->is_string()) return false;
    x.journeyId = jid->get<std::string>();
    auto id = v.find("id");
    if (id != v.end() && id->is_string()) x.id = id->get<std::string>();
    auto fn = v.find("fleetno");
    if (fn != v.end() && fn->is_string()) x.fleetNo = fn->get<std::string>();
    auto op = v.find("operator");
    if (op != v.end() && op->is_string()) x.operatorCode = op->get<std::string>();
    x.lat = coord->value("latitude", 0.0);
    x.lon = coord->value("longitude", 0.0);
    auto lu = v.find("lastupdate");
    x.lastUpdate = (lu != v.end() && lu->is_string()) ? parseApiTime(lu->get<std::string>()) : 0;
    x.delay = v.value("delay", 0);
    auto st = v.find("status");
    if (st != v.end() && st->is_string()) x.status = st->get<std::string>();
    return !(x.lat == 0.0 && x.lon == 0.0);
}

void VehicleFeed::watch(const std::string &journeyId) {
    journeyId_ = journeyId;
    vehicleId_.clear();
    vehicles_.clear();
    lastAt_ = 0;
    lastLookupAt_ = 0;
    ok_ = false;
}

void VehicleFeed::stop() {
    journeyId_.clear();
    vehicleId_.clear();
    vehicles_.clear();
}

bool VehicleFeed::setFromList(const std::string &body, time_t now) {
    lastAt_ = now;
    try {
        json doc = json::parse(body);
        if (!doc.is_array()) { ok_ = false; return false; }
        for (const auto &v : doc) {
            Vehicle x;
            if (!parseVehicle(v, x) || x.journeyId != journeyId_) continue;
            vehicles_ = {x};
            vehicleId_ = x.id;
            ok_ = true;
            return true;
        }
    } catch (const json::exception &) {
        std::cerr << "vehicle feed: list reply is not valid JSON" << std::endl;
        ok_ = false;
        return false;
    }
    // Not in the list: it has not started yet. Wait before looking again.
    vehicles_.clear();
    vehicleId_.clear();
    ok_ = true;
    return false;
}

bool VehicleFeed::setFromStatus(const std::string &body, time_t now) {
    lastAt_ = now;
    try {
        Vehicle x;
        if (!parseVehicle(json::parse(body), x)) { ok_ = false; return false; }
        if (x.journeyId != journeyId_) {
            // The vehicle now runs another journey: look it up again
            vehicles_.clear();
            vehicleId_.clear();
            lastLookupAt_ = 0;
            ok_ = true;
            return false;
        }
        vehicles_ = {x};
        ok_ = true;
        return true;
    } catch (const json::exception &) {
        std::cerr << "vehicle feed: status reply is not valid JSON" << std::endl;
        ok_ = false;
        return false;
    }
}

bool VehicleFeed::update(time_t now) {
    if (journeyId_.empty()) return false;

    // Not found yet: read the big list, at most every two minutes
    if (vehicleId_.empty()) {
        if (lastLookupAt_ != 0 && now - lastLookupAt_ < LOOKUP_RETRY_S) return false;
        lastLookupAt_ = now;
        std::string body;
        long code = 0;
        const std::string url = API_BASE() + "/vehiclestatus/vehiclestatusextended?operator=LOTH"
                              + "&onTripOnly=true&timeFormat=Local";
        lastAt_ = now;
        if (!apiHttpGet(url, body, code)) {
            ok_ = false;
            return true;
        }
        setFromList(body, now);
        return true;
    }

    // Found: one small request every 20 s
    if (lastAt_ != 0 && now - lastAt_ < POLL_EVERY_S) return false;
    std::string body;
    long code = 0;
    const std::string url = API_BASE() + "/vehiclestatus/VehicleStatus?VehicleId=" + vehicleId_
                          + "&timeFormat=Local";
    if (!apiHttpGet(url, body, code)) {
        lastAt_ = now;
        if (code == 404) {
            // The vehicle has gone: look it up again on the next tick
            vehicles_.clear();
            vehicleId_.clear();
            lastLookupAt_ = 0;
            ok_ = true;
        } else {
            ok_ = false;   // keep the last known position
        }
        return true;
    }
    setFromStatus(body, now);
    return true;
}

// ---------------------------------------------------------------------------
// Watch: lateness history, stuck detection

void TrackWatch::observe(const BusTrack &t, time_t now) {
    current_ = t;
    if (t.state != TrackState::Live) {
        // Stale, finished or not started: no stuck judgement, and the window restarts
        positions_.clear();
        current_.stuck = false;
        current_.stuckForS = 0;
        current_.lateHistory = history_;
        return;
    }

    positions_.push_back(Pos{now, t.busLat, t.busLon});
    while (!positions_.empty() && now - positions_.front().t > 360) positions_.erase(positions_.begin());

    // Walk back from the newest position while the bus stays within the radius
    const LatLon here{t.busLat, t.busLon};
    time_t earliest = now;
    for (auto it = positions_.rbegin(); it != positions_.rend(); ++it) {
        if (distanceM(here, LatLon{it->lat, it->lon}) < STUCK_RADIUS_M) earliest = it->t;
        else break;
    }
    const long dur = static_cast<long>(now - earliest);
    current_.stuck = dur >= STUCK_AFTER_S;
    current_.stuckForS = current_.stuck ? dur : 0;

    if (history_.empty() || now - history_.back().first >= 15) {
        history_.push_back({now, t.lateMin});
        if (history_.size() > 40) history_.erase(history_.begin());
    }
    current_.lateHistory = history_;
}

void TrackWatch::reset() {
    current_ = BusTrack();
    positions_.clear();
    history_.clear();
}
