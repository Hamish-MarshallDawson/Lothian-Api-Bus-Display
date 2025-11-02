#ifndef BUSSTOPINFO_H
#define BUSSTOPINFO_H

#include <string>
#include <vector>

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
};

// Provide this API: returns vector of static fallback stops (implemented in busStopInfo.cpp)
std::vector<BusStopInfo> getAllBusStops();

#endif // BUSSTOPINFO_H
