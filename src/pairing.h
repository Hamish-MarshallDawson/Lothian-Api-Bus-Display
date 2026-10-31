#ifndef PAIRING_H
#define PAIRING_H

// Into/Out keeps the stop on screen: the stop across the road, the other side
// of the same stop. Pure logic; busTrack turns its stop lists into PairStops.

#include <string>
#include <vector>

struct PairStop {
    std::string id;     // ATCO code
    std::string name;
    double lat, lon;
};

// Great-circle distance in metres.
double distanceM(double lat1, double lon1, double lat2, double lon2);

// Index in candidates of the stop to show for "the other direction" from current:
//  - the same stop, if it is already in candidates;
//  - else the nearest candidate within maxM, preferring one with the same name;
//  - else -1 (no paired stop near enough).
int pairedIndex(const std::vector<PairStop> &candidates, const PairStop &current, double maxM = 300.0);

#endif // PAIRING_H
