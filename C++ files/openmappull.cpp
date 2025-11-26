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
#include <cmath>
#include <map>

using json = nlohmann::json;

// Expanded features wanted from this file
// should return nearest bus stops to a given address as it currently does
// but also return the direction of each bus stop from the lothian api!
// this information will then be used in busTrack to display busses going in a certain direction in the sub menu
// the way this will work is that openmappull will return double the number of the stops requested
// but half will be for one direction and half for the other
// oppenmappull will sort them into two lists, one for into the city and one for out of the city
// clarifying what into the city is will work in the following way:
// the program will have a fixed location for what is the city centre in longitatude and latitude
// the users address will then be compared to this location to determine if they are north/south/east/west of the city centre
// then if the bus stop is heading towards the city centre it will be classed as into the city
// and if it is heading away from the city centre it will be classed as out of

// bus track will then be updated so that when the user selects the drop down menu they will be asked if they want to go into the city or out of the city
// then if they click into the city they will be shown the bus stops that are heading into the city
// and vice versa for out of the city


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

// Cached copy of the Lothian stops list to avoid fetching it repeatedly.
// Load once on demand and reuse for subsequent calls to getNearestStops()/grouping.
static json g_cachedStopsArray = json::array();

static bool ensureStopsCache() {
    if (!g_cachedStopsArray.empty()) return true;
    std::string url = API_CONFIG().baseURL + API_CONFIG().stopsEndpoint;
    std::string stopsBody = httpGetWithUA(url);
    if (stopsBody.empty()) return false;
    try {
        auto doc = json::parse(stopsBody);
        if (!(doc.contains("stops") && doc["stops"].is_array())) return false;
        g_cachedStopsArray = doc["stops"];
        return true;
    } catch (const std::exception &e) {
        std::cerr << "ensureStopsCache parse error: " << e.what() << std::endl;
        return false;
    }
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

    // Use cached stops array when available to avoid repeated HTTP fetches
    if (!ensureStopsCache()) return false;
    struct Tmp { std::string id; std::string name; double lat; double lon; double dist; };
    std::vector<Tmp> tmp;
    for(auto &el : g_cachedStopsArray) {
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
        // maxstops is duplicated to allow for return of busses that go into the city and out of the city
        int n = std::min((int)tmp.size(), maxStops*2);
        outStops.reserve(n);
        for(int i=0;i<n;++i) {
            BusStopInfo b;
            b.stopID = tmp[i].id;
            b.stopName = tmp[i].name;
            b.latitude = std::to_string(tmp[i].lat);
            b.longitude = std::to_string(tmp[i].lon);
            b.destination = tmp[i].name;
            b.services = "";
            // Attempt to capture any direction/towards/orientation info from the original JSON
            // Re-fetch the corresponding JSON element by stop_id to extract optional fields
            try {
                for(auto &el : g_cachedStopsArray) {
                    std::string sid;
                    if(el.contains("stop_id")) {
                        if(el["stop_id"].is_string()) sid = el["stop_id"].get<std::string>();
                        else sid = std::to_string(el.value("stop_id", 0));
                    }
                    if(sid == b.stopID) {
                        // look for common direction fields
                                if(el.contains("towards")) {
                                    if(el["towards"].is_string()) b.direction = el["towards"].get<std::string>();
                                    else if(el["towards"].is_object()) b.direction = el["towards"].value("description", "");
                                }
                                // orientation is numeric degrees if present
                                if(el.contains("orientation")) {
                                    if(el["orientation"].is_number()) {
                                        b.orientation_deg = el["orientation"].get<double>();
                                    } else if(el["orientation"].is_string()) {
                                        try { b.orientation_deg = std::stod(el["orientation"].get<std::string>()); } catch(...) {}
                                    }
                                    // If no textual direction was set, use compass direction if provided
                                    if(b.direction.empty() && el.contains("direction") && el["direction"].is_string()) {
                                        b.direction = el["direction"].get<std::string>();
                                    }
                                } else {
                                    if(b.direction.empty() && el.contains("orientation") && el["orientation"].is_string()) {
                                        if(el["orientation"].is_string()) b.direction = el["orientation"].get<std::string>();
                                    }
                                    if(b.direction.empty() && el.contains("direction")) {
                                        if(el["direction"].is_string()) b.direction = el["direction"].get<std::string>();
                                        else if(el["direction"].is_object()) b.direction = el["direction"].value("description", "");
                                    }
                                }
                        break;
                    }
                }
            } catch(...) {}
            outStops.push_back(b);
        }

        return true;
    }

// Convert degrees to radians
static double toRadians(double degrees) {
    return degrees * M_PI / 180.0;
}

// Convert radians to degrees
static double toDegrees(double radians) {
    return radians * 180.0 / M_PI;
}

static constexpr double DEG_TO_RAD = M_PI / 180.0;
static constexpr double RAD_TO_DEG = 180.0 / M_PI;

// Compute bearing from (lat1,lon1) to (lat2,lon2) in degrees [0,360)
double compute_bearing_deg(double lat1, double lon1, double lat2, double lon2) {
    double phi1 = lat1 * DEG_TO_RAD;
    double phi2 = lat2 * DEG_TO_RAD;
    double dLon = (lon2 - lon1) * DEG_TO_RAD;
    double y = sin(dLon) * cos(phi2);
    double x = cos(phi1)*sin(phi2) - sin(phi1)*cos(phi2)*cos(dLon);
    double theta = atan2(y, x);
    double bearing = fmod((theta * RAD_TO_DEG) + 360.0, 360.0);
    return bearing;
}

// Shortest absolute angular difference between a and b (degrees)
double angular_diff_deg(double a, double b) {
    double d = fmod(fabs(a - b), 360.0);
    if (d > 180.0) d = 360.0 - d;
    return d;
}

// Map cardinal direction strings (N,NE,E,SE,S,SW,W,NW) to degrees.
// Accepts uppercase or lowercase, and common variants like "NORTHEAST" if needed.
bool cardinal_to_deg(const std::string &s, double &out_deg) {
    static const std::map<std::string, double> m = {
        {"N", 0.0}, {"NNE", 22.5}, {"NE", 45.0}, {"ENE", 67.5},
        {"E", 90.0}, {"ESE", 112.5}, {"SE", 135.0}, {"SSE", 157.5},
        {"S", 180.0}, {"SSW", 202.5}, {"SW", 225.0}, {"WSW", 247.5},
        {"W", 270.0}, {"WNW", 292.5}, {"NW", 315.0}, {"NNW", 337.5}
    };

    std::string key;
    key.reserve(s.size());
    for (char c : s) {
        if (!isspace((unsigned char)c) && c != '-') key.push_back(toupper((unsigned char)c));
    }
    auto it = m.find(key);
    if (it != m.end()) {
        out_deg = it->second;
        return true;
    }
    return false;
}

// Note: CLI `main` moved to openmappull_main.cpp so the library functions here
// (getNearestStops, geocodeAddress, etc.) can be linked into other binaries
// such as `busTrack` without causing duplicate main() definitions.

#include "openmappull.h"
#include "config.h" // for CITY_CENTER_LAT/LON
#include <algorithm>

// Example: assumes getNearestStops(address, maxStops, vector<BusStopInfo>&) already exists.

bool getNearestStopsGrouped(const std::string &address, int perGroup, GroupedStops &out) {
    out.intoCity.clear();
    out.outOfCity.clear();
    // fetch bigger pool
    // Fetch a pool sized to the requested per-group count. The underlying
    // getNearestStops() already returns up to maxStops*2 entries, so asking
    // for `perGroup` should typically provide enough candidates to fill both
    // directions while avoiding an excessively large pool.
    int poolParam = perGroup;
    std::vector<BusStopInfo> pool;
    if (!getNearestStops(address, poolParam, pool)) {
        return false;
    }
    // Try to geocode the user's address to improve fallback classification
    double user_lat = 0.0, user_lon = 0.0;
    {
        std::string latStr, lonStr;
        if (geocodeAddress(address, latStr, lonStr)) {
            try { user_lat = std::stod(latStr); user_lon = std::stod(lonStr); }
            catch(...) { user_lat = user_lon = 0.0; }
        }
    }
    // compute bearing to centre and optional direction-deg
    struct WithMeta { BusStopInfo stop; double distToUser; double bearingToCentre; double dirDeg; bool hasDir; };
    std::vector<WithMeta> metas;
    for (const auto &s : pool) {
        WithMeta m;
        m.stop = s;
        // compute distance to user or to centre (you may already compute distance in getNearestStops)
        // For simplicity we'll compute distance to centre using haversine or reuse s.distance if present.
        double s_lat = 0.0, s_lon = 0.0;
        try {
            s_lat = std::stod(s.latitude);
            s_lon = std::stod(s.longitude);
        } catch(...) {
            // skip malformed coordinate
            continue;
        }
        m.bearingToCentre = compute_bearing_deg(s_lat, s_lon, CITY_CENTER_LAT(), CITY_CENTER_LON());
        double dirDeg = 0.0;
        m.hasDir = false;
        // Try to read s.direction (adjust to actual field name)
        if (!s.direction.empty()) {
            std::string dir = s.direction;
            // Try direct cardinal parse
            if (cardinal_to_deg(dir, dirDeg)) {
                m.dirDeg = dirDeg;
                m.hasDir = true;
            } else {
                // try to interpret words like 'north', 'south', 'centre' etc
                std::string lower;
                lower.reserve(dir.size());
                for(char c : dir) lower.push_back(std::tolower((unsigned char)c));
                if (lower.find("north") != std::string::npos) { m.dirDeg = 0.0; m.hasDir = true; }
                else if (lower.find("east") != std::string::npos) { m.dirDeg = 90.0; m.hasDir = true; }
                else if (lower.find("south") != std::string::npos) { m.dirDeg = 180.0; m.hasDir = true; }
                else if (lower.find("west") != std::string::npos) { m.dirDeg = 270.0; m.hasDir = true; }
                else if (lower.find("centre") != std::string::npos || lower.find("city") != std::string::npos) {
                    // assume this means towards city centre
                    m.dirDeg = m.bearingToCentre;
                    m.hasDir = true;
                }
            }
        }
        metas.push_back(m);
    }

    // classify
    // compute user's bearing to centre for fallback
    double user_bearingToCentre = 0.0;
    bool haveUserBearing = false;
    if (user_lat != 0.0 || user_lon != 0.0) {
        user_bearingToCentre = compute_bearing_deg(user_lat, user_lon, CITY_CENTER_LAT(), CITY_CENTER_LON());
        haveUserBearing = true;
    }

    for (const auto &m : metas) {
        // stop early once both groups have the requested number of stops
        if ((int)out.intoCity.size() >= perGroup && (int)out.outOfCity.size() >= perGroup) break;

        // Gather debug info
        std::string sid = m.stop.stopID;
        std::string name = m.stop.stopName;
        double orientation_field = m.stop.orientation_deg;
        std::string dir_text = m.stop.direction;
        bool assignedInto = false;

        if (m.hasDir) {
            double diff = angular_diff_deg(m.dirDeg, m.bearingToCentre);
            if (diff <= 90.0) {
                if ((int)out.intoCity.size() < perGroup) { out.intoCity.push_back(m.stop); assignedInto = true; }
            } else {
                if ((int)out.outOfCity.size() < perGroup) { out.outOfCity.push_back(m.stop); assignedInto = false; }
            }
            std::cerr << "classify(stop=" << sid << ", name='" << name << "', orientation_field=" << orientation_field
                      << ", dir_text='" << dir_text << "', dirDeg=" << m.dirDeg
                      << ", bearingToCentre=" << m.bearingToCentre << ", diff=" << diff
                      << ", assigned=" << (assignedInto?"Into":"Out") << ")\n";
        } else if (haveUserBearing) {
            double diff = angular_diff_deg(m.bearingToCentre, user_bearingToCentre);
            if (diff <= 90.0) {
                if ((int)out.intoCity.size() < perGroup) { out.intoCity.push_back(m.stop); assignedInto = true; }
            } else {
                if ((int)out.outOfCity.size() < perGroup) { out.outOfCity.push_back(m.stop); assignedInto = false; }
            }
            std::cerr << "classify(stop=" << sid << ", name='" << name << "', orientation_field=" << orientation_field
                      << ", dir_text='" << dir_text << "', bearingToCentre=" << m.bearingToCentre
                      << ", user_bearingToCentre=" << user_bearingToCentre << ", diff=" << diff
                      << ", assigned=" << (assignedInto?"Into":"Out") << ")\n";
        } else {
            // final fallback: distribute evenly but respect perGroup caps
            if ((int)out.intoCity.size() < perGroup) { out.intoCity.push_back(m.stop); assignedInto = true; }
            else if ((int)out.outOfCity.size() < perGroup) { out.outOfCity.push_back(m.stop); assignedInto = false; }
            std::cerr << "classify(stop=" << sid << ", name='" << name << "', orientation_field=" << orientation_field
                      << ", dir_text='" << dir_text << "', heuristic=\"even-distribute\", assigned=" << (assignedInto?"Into":"Out") << ")\n";
        }
    }

    // debug: print group sizes
    std::cerr << "getNearestStopsGrouped: intoCity=" << out.intoCity.size() << " outOfCity=" << out.outOfCity.size() << std::endl;

    // optional: sort each by distance if field available (e.g., m.stop.distance)
    // truncate
    if ((int)out.intoCity.size() > perGroup) out.intoCity.resize(perGroup);
    if ((int)out.outOfCity.size() > perGroup) out.outOfCity.resize(perGroup);
    return true;
}