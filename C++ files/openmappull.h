#ifndef OPENMAPPULL_H
#define OPENMAPPULL_H

#include <string>
#include <vector>
#include "busStopInfo.h"

// Returns true and fills outStops with up to maxStops nearest stops for the address.
// Uses config values via config.h internally.
bool getNearestStops(const std::string &address, int maxStops, std::vector<BusStopInfo> &outStops);

// parseConfigFile moved to config_utils.h/.cpp for shared use

#endif // OPENMAPPULL_H