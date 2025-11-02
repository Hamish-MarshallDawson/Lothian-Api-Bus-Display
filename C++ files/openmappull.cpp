// openmappull.cpp
// Provides geocoding and nearest-stop lookup. Exposes getNearestStops() for other programs.

#include "config.h"
#include "config_utils.h"
#include "openmappull.h"
#include "busStopInfo.h"

#include <iostream>
#include "config.h"
#include "config_utils.h"
#include "openmappull.h"
#include "busStopInfo.h"

#include <iostream>
#include <string>
#include <vector>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <sstream>
#include <algorithm>
#include <iomanip>

using json = nlohmann::json;

static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

static std::string httpGetWithUA(const std::string &url) {
    CURL* curl = curl_easy_init();
    if(!curl) return std::string();
    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, USER_AGENT().c_str());
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    CURLcode res = curl_easy_perform(curl);
    if(res != CURLE_OK) {
        std::cerr << "httpGet error: " << curl_easy_strerror(res) << " for URL: " << url << std::endl;
        response.clear();
    }
    curl_easy_cleanup(curl);
    return response;
}

// Haversine distance in meters
double haversine(double lat1, double lon1, double lat2, double lon2) {
    static const double R = 6371000.0;
    double dLat = (lat2 - lat1) * M_PI / 180.0;
    double dLon = (lon2 - lon1) * M_PI / 180.0;
    double a = std::sin(dLat/2)*std::sin(dLat/2) + std::cos(lat1*M_PI/180.0)*std::cos(lat2*M_PI/180.0)*std::sin(dLon/2)*std::sin(dLon/2);
    double c = 2 * std::atan2(std::sqrt(a), std::sqrt(1-a));
    return R * c;
}

// Geocode address using Nominatim
bool geocodeAddress(const std::string &address, std::string &latOut, std::string &lonOut) {
    CURL* curl = curl_easy_init();
    if(!curl) return false;
    char* enc = curl_easy_escape(curl, address.c_str(), (int)address.size());
    if(!enc) { curl_easy_cleanup(curl); return false; }
    std::string url = std::string("https://nominatim.openstreetmap.org/search?q=") + std::string(enc) + "&format=json&limit=1";
    curl_free(enc);
    curl_easy_cleanup(curl);
    std::string body = httpGetWithUA(url);
    if(body.empty()) return false;
    try {
        auto arr = json::parse(body);
        if(!arr.is_array() || arr.empty()) return false;
        auto obj = arr[0];
        latOut = obj.value("lat", "");
        lonOut = obj.value("lon", "");
        return !(latOut.empty() || lonOut.empty());
    } catch(const std::exception &e) {
        std::cerr << "geocode parse error: " << e.what() << std::endl;
        return false;
    }
}

bool getNearestStops(const std::string &address, int maxStops, std::vector<BusStopInfo> &outStops) {
    outStops.clear();
    std::string latStr, lonStr;
    if(!geocodeAddress(address, latStr, lonStr)) return false;
    double lat=0.0, lon=0.0;
    try { lat = std::stod(latStr); lon = std::stod(lonStr); } catch(...) { return false; }

    std::string url = API_CONFIG().baseURL + API_CONFIG().stopsEndpoint;
    std::string stopsBody = httpGetWithUA(url);
    if(stopsBody.empty()) return false;

    try {
        auto doc = json::parse(stopsBody);
        if(!(doc.contains("stops") && doc["stops"].is_array())) return false;
        struct Tmp { std::string id; std::string name; double lat; double lon; double dist; };
        std::vector<Tmp> tmp;
        for(auto &el : doc["stops"]) {
            if(!el.contains("latitude") || !el.contains("longitude")) continue;
            Tmp s;
            if(el.contains("stop_id")) {
                if(el["stop_id"].is_string()) s.id = el["stop_id"].get<std::string>();
                else s.id = std::to_string(el.value("stop_id", 0));
            }
            s.name = el.value("name", std::string("(no name)"));
            s.lat = el.value("latitude", 0.0);
            s.lon = el.value("longitude", 0.0);
            s.dist = haversine(lat, lon, s.lat, s.lon);
            tmp.push_back(s);
        }
        if(tmp.empty()) return false;
        std::sort(tmp.begin(), tmp.end(), [](const Tmp &a, const Tmp &b){ return a.dist < b.dist; });
        int n = std::min((int)tmp.size(), maxStops);
        outStops.reserve(n);
        for(int i=0;i<n;++i) {
            BusStopInfo b;
            b.stopID = tmp[i].id;
            b.stopName = tmp[i].name;
            b.latitude = std::to_string(tmp[i].lat);
            b.longitude = std::to_string(tmp[i].lon);
            b.destination = tmp[i].name;
            b.services = "";
            outStops.push_back(b);
        }
        return true;
    } catch(const std::exception &e) {
        std::cerr << "getNearestStops parse error: " << e.what() << std::endl;
        return false;
    }
}

// Note: CLI `main` moved to openmappull_main.cpp so the library functions here
// (getNearestStops, geocodeAddress, etc.) can be linked into other binaries
// such as `busTrack` without causing duplicate main() definitions.