#ifndef BUSSTOPINFO_H
#define BUSSTOPINFO_H

#include <string>
#include <vector>
#include <limits>

// Data shape used by busTrack for stops
struct BusStopInfo {
    std::string stopID;
    std::string stopName;
    std::string identifier;
    std::string direction;
    std::string latitude;
    std::string longitude;
    std::string destination;
    std::string services;
    // Optional numeric orientation/bearing reported by API (degrees 0..360). NaN if not provided.
    double orientation_deg = std::numeric_limits<double>::quiet_NaN();

    // Backwards-compatible constructor so existing brace-init lists in
    // `busStopInfo.cpp` continue to work. orientation_deg is optional.
    BusStopInfo(const std::string &sid = std::string(),
                const std::string &sname = std::string(),
                const std::string &iden = std::string(),
                const std::string &dir = std::string(),
                const std::string &lat = std::string(),
                const std::string &lon = std::string(),
                const std::string &dest = std::string(),
                const std::string &serv = std::string(),
                double orient = std::numeric_limits<double>::quiet_NaN())
        : stopID(sid), stopName(sname), identifier(iden), direction(dir),
          latitude(lat), longitude(lon), destination(dest), services(serv),
          orientation_deg(orient) {}
};

// Provide this API: returns vector of static fallback stops (implemented in busStopInfo.cpp)
std::vector<BusStopInfo> getAllBusStops();

#endif // BUSSTOPINFO_H
