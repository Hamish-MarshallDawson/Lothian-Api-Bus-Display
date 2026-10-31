// openmappull.cpp
// Nearest stops to BUS_ADDRESS, and the into/out-of-city picker lists.
//
// Caching, so that reopening the picker costs no network:
//   - geocoded addresses: kept for the life of the process
//   - the stop list (GetActiveStops): once per local day. If today's download
//     fails, yesterday's list is kept and retried after 10 minutes.
//   - direction per stop (from timetables): once per local day. Stops that had
//     no timetable answer are retried after 10 minutes.
//   - grouped picker results: per address, count and day

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include "apiHttp.h"
#include "busStopInfo.h"
#include "direction.h"
#include "openmappull.h"
#include "settings.h"
#include "diskcache.h"
#include "timetables.h"

using json = nlohmann::json;

static const double DEG_TO_RAD = M_PI / 180.0;
static const double RAD_TO_DEG = 180.0 / M_PI;

static size_t plainWriteCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    ((std::string *)userp)->append((char *)contents, size * nmemb);
    return size * nmemb;
}

static std::string todayLocal() {
    time_t now = time(nullptr);
    struct tm tmv;
    localtime_r(&now, &tmv);
    char buf[16];
    strftime(buf, sizeof(buf), "%Y-%m-%d", &tmv);
    return std::string(buf);
}

static std::string jstr(const json &o, const char *key) {
    auto it = o.find(key);
    return (it != o.end() && it->is_string()) ? it->get<std::string>() : std::string();
}

static double jnum(const json &o, const char *key) {
    auto it = o.find(key);
    return (it != o.end() && it->is_number()) ? it->get<double>() : 0.0;
}

// Bearing from a to b in degrees [0,360)
static double bearingDeg(LatLon a, LatLon b) {
    double phi1 = a.lat * DEG_TO_RAD, phi2 = b.lat * DEG_TO_RAD;
    double dLon = (b.lon - a.lon) * DEG_TO_RAD;
    double y = std::sin(dLon) * std::cos(phi2);
    double x = std::cos(phi1) * std::sin(phi2) - std::sin(phi1) * std::cos(phi2) * std::cos(dLon);
    return std::fmod(std::atan2(y, x) * RAD_TO_DEG + 360.0, 360.0);
}

// Shortest difference between two bearings, in degrees [0,180]
static double angularDiffDeg(double a, double b) {
    double d = std::fmod(std::fabs(a - b), 360.0);
    return d > 180.0 ? 360.0 - d : d;
}

// ---------------------------------------------------------------------------
// Geocoding (Nominatim), cached per address

// Geocoded addresses persist in ~/.cache/bustracker/geocode.json. The file is
// keyed by a hash of the address, so the address text never reaches the disk.
static std::string addressKey(const std::string &address) {
    unsigned long long h = 1469598103934665603ULL;  // FNV-1a, 64-bit
    for (unsigned char c : address) { h ^= c; h *= 1099511628211ULL; }
    char buf[20];
    snprintf(buf, sizeof(buf), "%016llx", h);
    return buf;
}

static json &geocodeStore() {
    static json store;
    static bool loaded = false;
    if (!loaded) {
        loaded = true;
        if (!diskcache::load("geocode.json", store) || !store.is_object()) store = json::object();
    }
    return store;
}

static bool geocode(const std::string &address, LatLon &out) {
    json &store = geocodeStore();
    const std::string key = addressKey(address);
    auto hit = store.find(key);
    if (hit != store.end() && hit->is_object()) {
        out = LatLon{hit->value("lat", 0.0), hit->value("lon", 0.0)};
        return true;
    }

    CURL *curl = curl_easy_init();
    if (!curl) return false;
    char *enc = curl_easy_escape(curl, address.c_str(), static_cast<int>(address.size()));
    std::string url = "https://nominatim.openstreetmap.org/search?q=" + std::string(enc ? enc : "")
                    + "&format=json&limit=1";
    if (enc) curl_free(enc);

    std::string body;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, plainWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, USER_AGENT().c_str());
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");  // decode gzip replies
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    if (res != CURLE_OK) {
        std::cerr << "geocode: " << curl_easy_strerror(res) << std::endl;
        return false;
    }

    try {
        auto arr = json::parse(body);
        if (!arr.is_array() || arr.empty()) return false;
        LatLon ll{std::stod(jstr(arr[0], "lat")), std::stod(jstr(arr[0], "lon"))};
        store[key] = json{{"lat", ll.lat}, {"lon", ll.lon}};
        diskcache::save("geocode.json", store);
        out = ll;
        return true;
    } catch (const std::exception &e) {
        std::cerr << "geocode parse error: " << e.what() << std::endl;
        return false;
    }
}

// ---------------------------------------------------------------------------
// Stop list (GetActiveStops), cached per day

struct StopRec {
    std::string atco, naptan, name;
    LatLon at;
};

static std::vector<StopRec> g_stops;
static std::map<std::string, LatLon> g_stopAt;  // atco -> position, from g_stops
static std::string g_stopsDate;
static time_t g_stopsRetryAt = 0;

static void setStops(const std::vector<StopRec> &stops, const std::string &date) {
    g_stops = stops;
    g_stopAt.clear();
    for (const auto &s : g_stops) g_stopAt[s.atco] = s.at;
    g_stopsDate = date;
}

// Today's list from ~/.cache/bustracker/stops-<date>.json. Stored compactly as
// [atco, naptan, name, lat, lon] rows.
static bool loadStopsFromDisk(const std::string &today) {
    json j;
    if (!diskcache::load("stops-" + today + ".json", j) || !j.is_array()) return false;
    std::vector<StopRec> s;
    for (const auto &r : j) {
        if (!r.is_array() || r.size() < 5) continue;
        s.push_back(StopRec{r[0].get<std::string>(), r[1].get<std::string>(), r[2].get<std::string>(),
                            LatLon{r[3].get<double>(), r[4].get<double>()}});
    }
    if (s.empty()) return false;
    setStops(s, today);
    return true;
}

static bool ensureStops() {
    const std::string today = todayLocal();
    if (!g_stops.empty() && g_stopsDate == today) return true;
    if (g_stops.empty() && loadStopsFromDisk(today)) {
        std::cerr << "stop list: " << g_stops.size() << " stops from cache" << std::endl;
        return true;
    }
    if (!g_stops.empty() && time(nullptr) < g_stopsRetryAt) return true;  // stale list is fine for now

    std::string body;
    long code = 0;
    std::string url = API_BASE() + "/stops/GetActiveStops?Date=" + today + "&FullDetails=true";
    std::vector<StopRec> fresh;
    if (apiHttpGet(url, body, code)) {
        try {
            auto doc = json::parse(body);
            auto stops = doc.find("stops");
            if (stops != doc.end() && stops->is_array()) {
                for (const auto &el : *stops) {
                    auto coord = el.find("coordinate");
                    if (coord == el.end() || !coord->is_object()) continue;
                    // The live key is "NaPTAN"; the spec says "NAPTAN". Take either.
                    std::string naptan = jstr(el, "NaPTAN");
                    if (naptan.empty()) naptan = jstr(el, "NAPTAN");
                    StopRec s{jstr(el, "atcocode"), naptan, jstr(el, "name"),
                              LatLon{jnum(*coord, "latitude"), jnum(*coord, "longitude")}};
                    if (!s.atco.empty()) fresh.push_back(s);
                }
            }
        } catch (const json::exception &e) {
            std::cerr << "stop list parse error: " << e.what() << std::endl;
        }
    }

    if (fresh.empty()) {
        std::cerr << "stop list: download failed" << (g_stops.empty() ? "" : ", keeping the last list") << std::endl;
        g_stopsRetryAt = time(nullptr) + 600;
        return !g_stops.empty();
    }
    setStops(fresh, today);
    g_stopsRetryAt = 0;

    json rows = json::array();
    for (const auto &s : g_stops) {
        rows.push_back(json::array({s.atco, s.naptan, s.name, s.at.lat, s.at.lon}));
    }
    const std::string file = "stops-" + today + ".json";
    diskcache::save(file, rows);
    diskcache::pruneOthers("stops-", file);

    std::cerr << "stop list: " << g_stops.size() << " stops for " << today << std::endl;
    return true;
}

// ---------------------------------------------------------------------------
// Direction of travel, from timetables

struct StopDirection {
    Direction dir = Direction::Unknown;
    std::string how;   // "timetable: 3 of 4 trips" or why it is unknown
};

// Timetable direction for one stop, cached for the day. Looks at up to two
// services that call at the stop. Unknown means the caller should use the
// bearing guess.
// Today's per-stop results, in memory and in ~/.cache/bustracker/direction-<date>.json.
// Each entry: {"dir": 0|1|2 (unknown, into, out), "how", "final", "retryAt"}.
static json &directionStore(const std::string &today) {
    static json store;
    static std::string loadedDate;
    if (loadedDate != today) {
        loadedDate = today;
        const std::string file = "direction-" + today + ".json";
        if (!diskcache::load(file, store) || !store.is_object()) store = json::object();
        diskcache::pruneOthers("direction-", file);
    }
    return store;
}

static StopDirection timetableDirection(const std::string &atco) {
    const std::string today = todayLocal();
    const time_t now = time(nullptr);
    json &store = directionStore(today);
    auto hit = store.find(atco);
    if (hit != store.end() && hit->is_object()) {
        const bool final = hit->value("final", false);
        const time_t retryAt = static_cast<time_t>(hit->value("retryAt", 0LL));
        if (final || now < retryAt) {
            StopDirection cached;
            cached.dir = static_cast<Direction>(hit->value("dir", 0));
            cached.how = hit->value("how", std::string());
            return cached;
        }
    }

    StopDirection res;
    std::vector<ServiceRef> services;
    bool ok = stopServicesFor(atco, today, services);
    std::vector<std::vector<std::string>> trips;
    if (ok) {
        const size_t checkServices = std::min<size_t>(services.size(), 2);
        for (size_t i = 0; i < checkServices; ++i) {
            std::vector<std::vector<std::string>> t;
            if (tripsFor(services[i], today, t)) trips.insert(trips.end(), t.begin(), t.end());
            else ok = false;
        }
    }

    CoordLookup coordOf = [](const std::string &a, LatLon &out) {
        auto it = g_stopAt.find(a);
        if (it == g_stopAt.end()) return false;
        out = it->second;
        return true;
    };
    DirectionVote vote = voteFromTrips(atco, trips, coordOf);
    res.dir = vote.dir;
    res.how = "timetable: " + std::to_string(vote.into) + " into, " + std::to_string(vote.out) + " out";
    if (res.dir == Direction::Unknown) {
        res.how = ok ? "no usable trips" : "timetable request failed";
    }

    // A timetable answer holds for the day. A failure is retried after 10 minutes.
    const bool final = res.dir != Direction::Unknown || ok;
    store[atco] = json{{"dir", static_cast<int>(res.dir)}, {"how", res.how},
                       {"final", final}, {"retryAt", static_cast<long long>(now + 600)}};
    diskcache::save("direction-" + today + ".json", store);
    return res;
}

// ---------------------------------------------------------------------------

// Distance from the home address to a stop, rounded to 10 m; -1 if unknown.
// Uses the cached geocode and stop list, so it costs no network after the first
// stop list download. The home position is never printed.
int stopDistanceFromHomeM(const std::string &atco) {
    LatLon home;
    if (CONFIG_ADDRESS().empty() || !geocode(CONFIG_ADDRESS(), home)) return -1;
    if (!ensureStops()) return -1;
    auto it = g_stopAt.find(atco);
    if (it == g_stopAt.end()) return -1;
    return roundToTenMetres(distanceM(home, it->second));
}

bool getNearestStops(const std::string &address, int maxStops, std::vector<BusStopInfo> &outStops) {
    outStops.clear();
    LatLon here;
    if (!geocode(address, here)) return false;
    if (!ensureStops()) return false;

    struct Near { const StopRec *s; double dist; };
    std::vector<Near> near;
    near.reserve(g_stops.size());
    for (const auto &s : g_stops) near.push_back({&s, distanceM(here, s.at)});
    std::sort(near.begin(), near.end(), [](const Near &a, const Near &b) { return a.dist < b.dist; });

    // Returns up to 2 * maxStops, as before: both directions are chosen from this
    size_t n = std::min(near.size(), static_cast<size_t>(std::max(maxStops, 0)) * 2);
    for (size_t i = 0; i < n; ++i) {
        const StopRec &s = *near[i].s;
        BusStopInfo b;
        b.stopID = s.atco;
        b.identifier = s.naptan;
        b.stopName = s.name;
        b.latitude = std::to_string(s.at.lat);
        b.longitude = std::to_string(s.at.lon);
        b.destination = s.name;
        b.distance_m = roundToTenMetres(near[i].dist);
        outStops.push_back(b);
    }
    return !outStops.empty();
}

// Into/out-of-city for the picker. Uses timetable direction where the
// timetables say; otherwise the bearing guess (same side of the centre as the
// user). Every stop logs which method decided it.
bool getNearestStopsGrouped(const std::string &address, int perGroup, GroupedStops &out) {
    static std::map<std::string, GroupedStops> cache;  // key: address|count|day
    const std::string key = address + "|" + std::to_string(perGroup) + "|" + todayLocal();
    auto hit = cache.find(key);
    if (hit != cache.end()) { out = hit->second; return true; }

    out.intoCity.clear();
    out.outOfCity.clear();
    LatLon here;
    if (!geocode(address, here)) return false;

    // Nearest stops first; stop as soon as both groups are full
    std::vector<BusStopInfo> pool;
    if (!getNearestStops(address, perGroup * 4, pool)) return false;

    const double userBearing = bearingDeg(here, LatLon{CITY_CENTER_LAT(), CITY_CENTER_LON()});
    for (const auto &s : pool) {
        if ((int)out.intoCity.size() >= perGroup && (int)out.outOfCity.size() >= perGroup) break;

        LatLon at{std::stod(s.latitude), std::stod(s.longitude)};
        StopDirection d = timetableDirection(s.stopID);
        bool into;
        if (d.dir != Direction::Unknown) {
            into = d.dir == Direction::Into;
            // No stop code or name here: these stops are near home
            std::cerr << "direction: " << (into ? "into" : "out") << " by " << d.how << std::endl;
        } else {
            double stopBearing = bearingDeg(at, LatLon{CITY_CENTER_LAT(), CITY_CENTER_LON()});
            into = angularDiffDeg(stopBearing, userBearing) <= 90.0;
            std::cerr << "direction: " << (into ? "into" : "out") << " by bearing guess (" << d.how << ")" << std::endl;
        }

        auto &group = into ? out.intoCity : out.outOfCity;
        if ((int)group.size() < perGroup) group.push_back(s);
    }

    cache[key] = out;
    return true;
}

// Stub for the API commit. Implemented in M7.
bool pairedStop(const BusStopInfo &, Direction, BusStopInfo &) {
    return false;
}
