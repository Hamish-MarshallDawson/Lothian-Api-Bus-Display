// openmappull.cpp
// Nearest stops to BUS_ADDRESS, and the into/out-of-city picker lists.
//
// Caching, so that reopening the picker costs no network:
//   - geocoded addresses: kept for the life of the process
//   - the stop list (GetActiveStops): once per local day. If today's download
//     fails, yesterday's list is kept and retried after 10 minutes.
//   - grouped picker results: per address, count and day

#include <algorithm>
#include <cmath>
#include <ctime>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include "apiHttp.h"
#include "busStopInfo.h"
#include "openmappull.h"
#include "settings.h"

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

// Haversine distance in metres
static double haversine(double lat1, double lon1, double lat2, double lon2) {
    static const double R = 6371000.0;
    double dLat = (lat2 - lat1) * DEG_TO_RAD;
    double dLon = (lon2 - lon1) * DEG_TO_RAD;
    double a = std::sin(dLat / 2) * std::sin(dLat / 2)
             + std::cos(lat1 * DEG_TO_RAD) * std::cos(lat2 * DEG_TO_RAD) * std::sin(dLon / 2) * std::sin(dLon / 2);
    return R * 2 * std::atan2(std::sqrt(a), std::sqrt(1 - a));
}

// Bearing from (lat1,lon1) to (lat2,lon2) in degrees [0,360)
static double bearingDeg(double lat1, double lon1, double lat2, double lon2) {
    double phi1 = lat1 * DEG_TO_RAD, phi2 = lat2 * DEG_TO_RAD;
    double dLon = (lon2 - lon1) * DEG_TO_RAD;
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

struct LatLon { double lat, lon; };

static bool geocode(const std::string &address, LatLon &out) {
    static std::map<std::string, LatLon> cache;
    auto hit = cache.find(address);
    if (hit != cache.end()) { out = hit->second; return true; }

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
        cache[address] = ll;
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
    double lat, lon;
};

static std::vector<StopRec> g_stops;
static std::string g_stopsDate;
static time_t g_stopsRetryAt = 0;

static bool ensureStops() {
    const std::string today = todayLocal();
    if (!g_stops.empty() && g_stopsDate == today) return true;
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
                    StopRec s{jstr(el, "atcocode"), jstr(el, "NAPTAN"), jstr(el, "name"),
                              jnum(*coord, "latitude"), jnum(*coord, "longitude")};
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
    g_stops = fresh;
    g_stopsDate = today;
    g_stopsRetryAt = 0;
    std::cerr << "stop list: " << g_stops.size() << " stops for " << today << std::endl;
    return true;
}

// ---------------------------------------------------------------------------

bool getNearestStops(const std::string &address, int maxStops, std::vector<BusStopInfo> &outStops) {
    outStops.clear();
    LatLon here;
    if (!geocode(address, here)) return false;
    if (!ensureStops()) return false;

    struct Near { const StopRec *s; double dist; };
    std::vector<Near> near;
    near.reserve(g_stops.size());
    for (const auto &s : g_stops) near.push_back({&s, haversine(here.lat, here.lon, s.lat, s.lon)});
    std::sort(near.begin(), near.end(), [](const Near &a, const Near &b) { return a.dist < b.dist; });

    // Returns up to 2 * maxStops, as before: both directions are chosen from this
    size_t n = std::min(near.size(), static_cast<size_t>(std::max(maxStops, 0)) * 2);
    for (size_t i = 0; i < n; ++i) {
        const StopRec &s = *near[i].s;
        BusStopInfo b;
        b.stopID = s.atco;
        b.identifier = s.naptan;
        b.stopName = s.name;
        b.latitude = std::to_string(s.lat);
        b.longitude = std::to_string(s.lon);
        b.destination = s.name;
        outStops.push_back(b);
    }
    return !outStops.empty();
}

bool getNearestStopsGrouped(const std::string &address, int perGroup, GroupedStops &out) {
    static std::map<std::string, GroupedStops> cache;  // key: address|count|day
    const std::string key = address + "|" + std::to_string(perGroup) + "|" + todayLocal();
    auto hit = cache.find(key);
    if (hit != cache.end()) { out = hit->second; return true; }

    out.intoCity.clear();
    out.outOfCity.clear();
    LatLon here;
    if (!geocode(address, here)) return false;

    // Look at a wide pool so both groups can fill up
    std::vector<BusStopInfo> pool;
    if (!getNearestStops(address, perGroup * 4, pool)) return false;

    // Heuristic: the Novus stop data has no direction. A stop counts as "into
    // city" when it lies on the same side of the centre as the user does.
    const double userBearing = bearingDeg(here.lat, here.lon, CITY_CENTER_LAT(), CITY_CENTER_LON());
    for (const auto &s : pool) {
        if ((int)out.intoCity.size() >= perGroup && (int)out.outOfCity.size() >= perGroup) break;
        double stopLat = std::stod(s.latitude), stopLon = std::stod(s.longitude);
        double stopBearing = bearingDeg(stopLat, stopLon, CITY_CENTER_LAT(), CITY_CENTER_LON());
        bool into = angularDiffDeg(stopBearing, userBearing) <= 90.0;
        auto &group = into ? out.intoCity : out.outOfCity;
        if ((int)group.size() < perGroup) group.push_back(s);
    }

    cache[key] = out;
    return true;
}
