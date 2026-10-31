// timetables.cpp: see timetables.h

#include "timetables.h"

#include <iostream>
#include <map>

#include <curl/curl.h>

#include "apiHttp.h"
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

// Drop cache entries from other days, so the map stays small
template <typename Map>
static void keepOnlyDay(Map &m, const std::string &date) {
    const std::string suffix = "|" + date;
    for (auto it = m.begin(); it != m.end();) {
        if (it->first.size() < suffix.size()
            || it->first.compare(it->first.size() - suffix.size(), suffix.size(), suffix) != 0) {
            it = m.erase(it);
        } else {
            ++it;
        }
    }
}

bool stopServicesFor(const std::string &atco, const std::string &date, std::vector<ServiceRef> &out) {
    static std::map<std::string, std::vector<ServiceRef>> cache;  // key: atco|date
    const std::string key = atco + "|" + date;
    auto hit = cache.find(key);
    if (hit != cache.end()) { out = hit->second; return true; }

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
    keepOnlyDay(cache, date);
    cache[key] = out;
    return true;
}

bool tripsFor(const ServiceRef &svc, const std::string &date, std::vector<std::vector<std::string>> &out) {
    static std::map<std::string, std::vector<std::vector<std::string>>> cache;  // key: name|op|date
    const std::string key = svc.publicservicename + "|" + svc.opCode + "|" + date;
    auto hit = cache.find(key);
    if (hit != cache.end()) { out = hit->second; return true; }

    std::string url = API_BASE() + "/timetables/GetServiceTimetable?publicservicename="
                    + urlEncode(svc.publicservicename) + "&operator=" + urlEncode(svc.opCode)
                    + "&date=" + date + "&timeFormat=Local";
    std::string body;
    long code = 0;
    if (!apiHttpGet(url, body, code)) return false;
    try {
        out = parseTripStops(json::parse(body));
    } catch (const json::exception &e) {
        std::cerr << "GetServiceTimetable parse error: " << e.what() << std::endl;
        return false;
    }
    keepOnlyDay(cache, date);
    cache[key] = out;
    return true;
}
