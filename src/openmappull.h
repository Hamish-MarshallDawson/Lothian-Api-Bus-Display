#ifndef OPENMAPPULL_H
#define OPENMAPPULL_H

#include <string>
#include <vector>
#include "busStopInfo.h"
#include "direction.h"

struct GroupedStops {
    std::vector<BusStopInfo> intoCity;
    std::vector<BusStopInfo> outOfCity;
};

// Returns true and fills out with up to perGroup stops for each group.
// Classification is done by comparing the stop's reported direction (if present)
// mapped to degrees vs the bearing from the stop to CITY_CENTER.
// If a stop lacks direction info, fallback uses the stop->centre bearing to guess.
bool getNearestStops(const std::string &address, int maxStops, std::vector<BusStopInfo> &outStops);

// Straight-line distance in metres from the home address (BUS_ADDRESS) to a stop,
// rounded to 10 m. -1 when unknown (no address, geocode failed, stop not in the
// list). For StopBoard, call this with board.atcocode. Home coordinates are never printed.
int stopDistanceFromHomeM(const std::string &atco);

// New: grouped nearest stops (into/out of city)
bool getNearestStopsGrouped(const std::string &address, int perGroup, GroupedStops &out);

// The stop on the other side of the road, for Into/Out: among stops within ~300 m
// of "current" (cached stop list), classified by timetable direction, the nearest
// one in "want", preferring the same stop name. If "current" is already in "want",
// returns it. False when nothing is found (the caller falls back and says so).
bool pairedStop(const BusStopInfo &current, Direction want, BusStopInfo &out);

// Name and position of a stop by ATCO code, from the cached stop list. False if unknown.
bool stopByCode(const std::string &atco, std::string &name, LatLon &at);

#endif // OPENMAPPULL_H