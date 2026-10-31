// timetables.cpp: see timetables.h

#include "timetables.h"

#include <iostream>
#include <map>

#include <curl/curl.h>

#include "apiHttp.h"
#include "diskcache.h"
#include "settings.h"

using json = nlohmann::json;

static std::string jstr(const json &o, const char *key) {
    auto it = o.find(key);
    return (it != o.end() && it->is_string()) ? it->get<std::string>() : std::string();
}

static std::string urlEncode(const std::string &s) {
    CURL *c = curl_easy_init();
    if (!c) return s;
    char *e = curl_easy_escape(c, s.c_str(), static_cast<int>(s.size()));
    std::string out = e ? e : s;
    if (e) curl_free(e);
    curl_easy_cleanup(c);
    return out;
}

std::vector<ServiceRef> parseStopServices(const json &doc) {
    std::vector<ServiceRef> out;
    auto it = doc.find("services");
    if (it == doc.end() || !it->is_array()) return out;
    for (const auto &s : *it) {
        ServiceRef r{jstr(s, "publicservicename"), jstr(s, "operator")};
        if (!r.publicservicename.empty()) out.push_back(r);
    }
    return out;
}

std::vector<std::vector<std::string>> parseTripStops(const json &doc) {
    std::vector<std::vector<std::string>> out;
    auto dirs = doc.find("directions");
    if (dirs == doc.end() || !dirs->is_array()) return out;
    for (const auto &dir : *dirs) {
        auto trips = dir.find("trips");
        if (trips == dir.end() || !trips->is_array()) continue;
        for (const auto &trip : *trips) {
            std::vector<std::string> stops;
            auto events = trip.find("events");
            if (events == trip.end() || !events->is_array()) continue;
            for (const auto &ev : *events) {
                std::string a = jstr(ev, "atcocode");
                if (!a.empty()) stops.push_back(a);
            }
            if (!stops.empty()) out.push_back(stops);
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Day stores: one JSON object per kind per day, kept in memory and on disk
// (~/.cache/bustracker/<kind>-<date>.json). Yesterday's file is deleted when
// today's is first used.

static json &dayStore(const std::string &kind, const std::string &date) {
    static std::map<std::string, json> stores;  // key kind|date
    const std::string key = kind + "|" + date;
    auto it = stores.find(key);
    if (it != stores.end()) return it->second;

    // Drop the in-memory store for any other day of this kind
    for (auto i = stores.begin(); i != stores.end();) {
        if (i->first.compare(0, kind.size() + 1, kind + "|") == 0) i = stores.erase(i);
        else ++i;
    }

    const std::string file = kind + "-" + date + ".json";
    json j;
    if (!diskcache::load(file, j) || !j.is_object()) j = json::object();
    diskcache::pruneOthers(kind + "-", file);
    return stores[key] = j;
}

static void saveDayStore(const std::string &kind, const std::string &date) {
    diskcache::save(kind + "-" + date + ".json", dayStore(kind, date));
}

// ---------------------------------------------------------------------------

bool stopServicesFor(const std::string &atco, const std::string &date, std::vector<ServiceRef> &out) {
    json &store = dayStore("services", date);
    auto hit = store.find(atco);
    if (hit != store.end() && hit->is_array()) {
        out.clear();
        for (const auto &s : *hit) {
            out.push_back(ServiceRef{jstr(s, "publicservicename"), jstr(s, "operator")});
        }
        return true;
    }

    std::string url = API_BASE() + "/services/GetStopServices?atcoCode=" + urlEncode(atco)
                    + "&fromDate=" + date + "&toDate=" + date;
    std::string body;
    long code = 0;
    if (!apiHttpGet(url, body, code)) return false;
    try {
        out = parseStopServices(json::parse(body));
    } catch (const json::exception &e) {
        std::cerr << "GetStopServices parse error: " << e.what() << std::endl;
        return false;
    }
    json arr = json::array();
    for (const auto &s : out) arr.push_back({{"publicservicename", s.publicservicename}, {"operator", s.opCode}});
    store[atco] = arr;
    saveDayStore("services", date);
    return true;
}

bool tripsFor(const ServiceRef &svc, const std::string &date, std::vector<std::vector<std::string>> &out) {
    json &store = dayStore("trips", date);
    const std::string key = svc.publicservicename + "|" + svc.opCode;
    auto hit = store.find(key);
    if (hit != store.end() && hit->is_array()) {
        out.clear();
        for (const auto &t : *hit) out.push_back(t.get<std::vector<std::string>>());
        return true;
    }

    std::string url = API_BASE() + "/timetables/GetServiceTimetable?publicservicename="
                    + urlEncode(svc.publicservicename) + "&operator=" + urlEncode(svc.opCode)
                    + "&date=" + date;   // from/to and timeFormat are rejected (400): whole day only
    std::string body;
    long code = 0;
    if (!apiHttpGet(url, body, code)) return false;
    try {
        out = parseTripStops(json::parse(body));
    } catch (const json::exception &e) {
        std::cerr << "GetServiceTimetable parse error: " << e.what() << std::endl;
        return false;
    }
    store[key] = json(out);
    saveDayStore("trips", date);
    return true;
}

std::vector<TripDetail> parseTripDetails(const json &doc) {
    std::vector<TripDetail> out;
    auto dirs = doc.find("directions");
    if (dirs == doc.end() || !dirs->is_array()) return out;
    for (const auto &dir : *dirs) {
        auto trips = dir.find("trips");
        if (trips == dir.end() || !trips->is_array()) continue;
        for (const auto &trip : *trips) {
            TripDetail t;
            t.starttime = jstr(trip, "starttime");
            auto events = trip.find("events");
            if (events == trip.end() || !events->is_array()) continue;
            for (const auto &ev : *events) {
                TripEvent e{jstr(ev, "atcocode"), jstr(ev, "departure")};
                if (!e.atco.empty()) t.events.push_back(e);
            }
            if (!t.events.empty()) out.push_back(t);
        }
    }
    return out;
}

bool tripDetailsFor(const ServiceRef &svc, const std::string &date, std::vector<TripDetail> &out) {
    json &store = dayStore("details", date);
    const std::string key = svc.publicservicename + "|" + svc.opCode;
    auto hit = store.find(key);
    if (hit != store.end() && hit->is_array()) {
        out.clear();
        for (const auto &t : *hit) {
            TripDetail d;
            d.starttime = jstr(t, "starttime");
            // cache rows are [atco, departure]
            for (const auto &e : t.at("events")) {
                if (e.is_array() && e.size() == 2) {
                    d.events.push_back(TripEvent{e[0].get<std::string>(), e[1].get<std::string>()});
                }
            }
            out.push_back(d);
        }
        return true;
    }

    std::string url = API_BASE() + "/timetables/GetServiceTimetable?publicservicename="
                    + urlEncode(svc.publicservicename) + "&operator=" + urlEncode(svc.opCode)
                    + "&date=" + date;
    std::string body;
    long code = 0;
    if (!apiHttpGet(url, body, code)) return false;
    try {
        out = parseTripDetails(json::parse(body));
    } catch (const json::exception &e) {
        std::cerr << "GetServiceTimetable parse error: " << e.what() << std::endl;
        return false;
    }
    json arr = json::array();
    for (const auto &t : out) {
        json evs = json::array();
        for (const auto &e : t.events) evs.push_back(json::array({e.atco, e.departure}));
        arr.push_back({{"starttime", t.starttime}, {"events", evs}});
    }
    store[key] = arr;
    saveDayStore("details", date);
    return true;
}
