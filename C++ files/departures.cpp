// departures.cpp
// Turns Novus StopEvents JSON into StopBoard, and fetches boards from the API.

#include "departures.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

#include "apiHttp.h"
#include "settings.h"

using json = nlohmann::json;

static std::string jstr(const json &o, const char *key) {
    auto it = o.find(key);
    return (it != o.end() && it->is_string()) ? it->get<std::string>() : std::string();
}

// Parse a local timestamp: "2026-08-24T17:42:00" or a bare "17:42[:00]"
// (the bare form takes today's date from "now"). Returns false if unreadable.
static bool parseLocalStamp(const std::string &s, time_t now, time_t &out) {
    std::tm tm{};
    localtime_r(&now, &tm);
    std::istringstream in(s);
    const char *fmt;
    if (s.find('T') != std::string::npos) {
        fmt = s.size() >= 19 ? "%Y-%m-%dT%H:%M:%S" : "%Y-%m-%dT%H:%M";
    } else {
        fmt = s.size() >= 8 ? "%H:%M:%S" : "%H:%M";
    }
    in >> std::get_time(&tm, fmt);
    if (in.fail()) return false;
    tm.tm_isdst = -1;
    out = mktime(&tm);
    if (out == static_cast<time_t>(-1)) return false;
    // A bare time is today's date. Near midnight the same clock time can mean the
    // previous or next day, so take the nearest of the three.
    if (s.find('T') == std::string::npos) {
        const long long diff = static_cast<long long>(out - now);
        if (diff < -12LL * 3600) out += 86400;
        else if (diff > 12LL * 3600) out -= 86400;
    }
    return true;
}

static std::string hhmm(time_t t) {
    std::tm tm{};
    localtime_r(&t, &tm);
    char buf[8];
    strftime(buf, sizeof(buf), "%H:%M", &tm);
    return buf;
}

StopBoard boardFromStopEvents(const json &stopObj, time_t now) {
    StopBoard b;
    b.stopName = jstr(stopObj, "name");
    b.atcocode = jstr(stopObj, "atcocode");
    b.fetchedAt = now;
    b.status = BoardStatus::NoDepartures;

    auto notices = stopObj.find("notices");
    if (notices != stopObj.end() && notices->is_array()) {
        for (const auto &n : *notices) {
            std::string text = jstr(n, "notice");
            if (!text.empty()) b.notices.push_back(text);
        }
    }

    auto events = stopObj.find("events");
    if (events == stopObj.end() || !events->is_array()) return b;

    for (const auto &ev : *events) {
        if (!ev.is_object()) continue;
        Departure d;
        d.service = jstr(ev, "publicservicename");
        d.destination = jstr(ev, "destination");
        const std::string status = jstr(ev, "status");
        d.cancelled = (status == "cancelled");
        d.delayed = (status == "delayed");
        if (b.indicator.empty()) b.indicator = jstr(ev, "indicator");

        auto seat = ev.find("seatavailability");
        if (seat != ev.end() && seat->is_object()) {
            auto count = seat->find("count");
            if (count != seat->end() && count->is_number_integer()) d.seatCount = count->get<int>();
            d.seatLevel = jstr(*seat, "level");
        }

        // The timetable time is always there (when the API has the stop).
        time_t schedT = 0;
        bool haveSched = parseLocalStamp(jstr(ev, "scheduledeparturetime"), now, schedT);
        if (haveSched) d.scheduled = hhmm(schedT);

        // Live prediction: an integer is seconds-to-go, a string is a date-time.
        auto dt = ev.find("departuretime");
        if (dt != ev.end()) {
            if (dt->is_number_integer()) {
                d.minutes = dt->get<int>() / 60;
                d.hasMinutes = d.live = true;
            } else if (dt->is_string()) {
                time_t t = 0;
                if (parseLocalStamp(dt->get<std::string>(), now, t)) {
                    d.minutes = static_cast<int>((t - now) / 60);
                    d.hasMinutes = d.live = true;
                }
            }
        }
        if (!d.hasMinutes && haveSched) {
            d.minutes = static_cast<int>((schedT - now) / 60);
            d.hasMinutes = true;
        }
        if (!d.hasMinutes) continue;  // nothing to show
        b.departures.push_back(d);
    }

    std::stable_sort(b.departures.begin(), b.departures.end(),
                     [](const Departure &a, const Departure &c) { return a.minutes < c.minutes; });
    if (!b.departures.empty()) b.status = BoardStatus::Ok;
    return b;
}

// GET a StopEvents-style URL and parse the array. On failure, sets the status
// and detail to report and returns false.
static bool getStopEventsJson(const std::string &url, json &doc, BoardStatus &failStatus,
                              std::string &detail) {
    if (API_KEY().empty()) {
        failStatus = BoardStatus::KeyRejected;
        detail = "BUSTRACKER_KEY is not set";
        return false;
    }
    std::string body;
    long code = 0;
    if (!apiHttpGet(url, body, code)) {
        if (code == 401 || code == 403) {
            failStatus = BoardStatus::KeyRejected;
            detail = "HTTP " + std::to_string(code);
        } else if (code == 429) {
            failStatus = BoardStatus::RateLimited;
            detail = "HTTP 429";
        } else {
            failStatus = BoardStatus::NetworkError;
            detail = code ? "HTTP " + std::to_string(code) : "no response";
        }
        return false;
    }
    try {
        doc = json::parse(body);
    } catch (const json::exception &e) {
        failStatus = BoardStatus::NetworkError;
        detail = std::string("bad JSON: ") + e.what();
        return false;
    }
    // Real replies: StopEvents is one stop object; MultipleStopEvents is {"stops": [...]}
    if (!doc.is_array() && !doc.is_object()) {
        failStatus = BoardStatus::NetworkError;
        detail = "response is neither an object nor an array";
        return false;
    }
    return true;
}

static std::string eventsQuery(const std::string &codesParam) {
    return "?" + codesParam
         + "&eventType=departure&ordering=RealDep&timeFormat=Local&doCountdown=true"
         + "&lookahead=" + std::to_string(CONFIG_LOOKAHEAD_MINS())
         + "&maxEvents=" + std::to_string(CONFIG_MAX_EVENTS());
}

StopBoard fetchStopBoard(const std::string &atcocode, time_t now) {
    StopBoard b;
    b.atcocode = atcocode;
    b.fetchedAt = now;

    json doc;
    if (!getStopEventsJson(API_BASE() + "/stops/StopEvents" + eventsQuery("atcoCode=" + atcocode),
                           doc, b.status, b.statusDetail)) {
        return b;
    }
    const json *stop = nullptr;
    if (doc.is_object() && doc.contains("events")) stop = &doc;
    else if (doc.is_array() && !doc.empty() && doc[0].is_object()) stop = &doc[0];
    if (!stop) {
        b.status = BoardStatus::NoDepartures;
        b.statusDetail = "empty response";
        return b;
    }
    StopBoard parsed = boardFromStopEvents(*stop, now);
    if (parsed.atcocode.empty()) parsed.atcocode = atcocode;
    return parsed;
}

std::vector<StopBoard> fetchStopBoards(const std::vector<std::string> &atcocodes, time_t now) {
    std::vector<StopBoard> out(atcocodes.size());
    for (size_t i = 0; i < atcocodes.size(); ++i) {
        out[i].atcocode = atcocodes[i];
        out[i].fetchedAt = now;
    }
    if (atcocodes.empty()) return out;

    std::string codes;
    for (size_t i = 0; i < atcocodes.size(); ++i) {
        if (i) codes += ",";
        codes += atcocodes[i];
    }

    json doc;
    BoardStatus failStatus = BoardStatus::NetworkError;
    std::string detail;
    if (!getStopEventsJson(API_BASE() + "/stops/MultipleStopEvents" + eventsQuery("atcoCodes=" + codes),
                           doc, failStatus, detail)) {
        for (auto &b : out) {
            b.status = failStatus;
            b.statusDetail = detail;
        }
        return out;
    }

    return boardsFromMultipleStopEvents(doc, atcocodes, now);
}

std::vector<StopBoard> boardsFromMultipleStopEvents(const json &doc,
                                                    const std::vector<std::string> &atcocodes,
                                                    time_t now) {
    std::vector<StopBoard> out(atcocodes.size());
    // Real reply: {"stops": [...]}. Also accept a bare array.
    const json &list = (doc.is_object() && doc.contains("stops")) ? doc["stops"] : doc;
    for (size_t i = 0; i < atcocodes.size(); ++i) {
        out[i].atcocode = atcocodes[i];
        out[i].fetchedAt = now;
        const json *found = nullptr;
        for (const auto &s : list) {
            if (s.is_object() && jstr(s, "atcocode") == atcocodes[i]) { found = &s; break; }
        }
        if (!found) {
            out[i].status = BoardStatus::NoDepartures;
            out[i].statusDetail = "not in response";
            continue;
        }
        StopBoard parsed = boardFromStopEvents(*found, now);
        if (parsed.atcocode.empty()) parsed.atcocode = atcocodes[i];
        out[i] = parsed;
    }
    return out;
}

const char *boardStatusName(BoardStatus s) {
    switch (s) {
        case BoardStatus::Loading:      return "loading";
        case BoardStatus::Ok:           return "ok";
        case BoardStatus::NoDepartures: return "no departures";
        case BoardStatus::NetworkError: return "network error";
        case BoardStatus::KeyRejected:  return "key rejected";
        case BoardStatus::RateLimited:  return "rate limited";
    }
    return "unknown";
}
