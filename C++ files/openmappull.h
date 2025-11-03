#ifndef OPENMAPPULL_H
#define OPENMAPPULL_H

#include <string>
#include <vector>
#include "busStopInfo.h"

struct GroupedStops {
    std::vector<BusStopInfo> intoCity;
    std::vector<BusStopInfo> outOfCity;
};

// Returns true and fills out with up to perGroup stops for each group.
// Classification is done by comparing the stop's reported direction (if present)
// mapped to degrees vs the bearing from the stop to CITY_CENTER.
// If a stop lacks direction info, fallback uses the stop->centre bearing to guess.
bool getNearestStops(const std::string &address, int maxStops, std::vector<BusStopInfo> &outStops);

// New: grouped nearest stops (into/out of city)
bool getNearestStopsGrouped(const std::string &address, int perGroup, GroupedStops &out);

// parseConfigFile moved to config_utils.h/.cpp for shared use

#endif // OPENMAPPULL_H