#ifndef DIRECTION_H
#define DIRECTION_H

// Works out whether buses at a stop head into or out of the city centre, from
// timetable trips. Pure logic: no network. The network side is in timetables.*
// and openmappull.cpp.

#include <functional>
#include <string>
#include <vector>

// No default member initialisers: the build is C++11, where they would stop
// LatLon from being an aggregate (LatLon{lat, lon} must work).
struct LatLon {
    double lat;
    double lon;
};

enum class Direction { Unknown, Into, Out };

// Great-circle distance in metres
double distanceM(LatLon a, LatLon b);

// Distance in metres from p to the city centre (settings.h)
double distToCentreM(LatLon p);

// Metres rounded to the nearest 10 m (e.g. 634.9 -> 630). Negative input -> -1.
int roundToTenMetres(double m);

// Coordinates of a stop by ATCO code. Returns false if unknown.
using CoordLookup = std::function<bool(const std::string &atco, LatLon &out)>;

struct DirectionVote {
    Direction dir = Direction::Unknown;
    int into = 0;   // trips where the next stop is clearly closer to the centre
    int out = 0;    // trips where the next stop is clearly further away
};

// Each trip is the ordered list of ATCO codes it calls at. For every trip that
// calls at atco (not as its last stop), compare the next stop's distance to the
// centre with atco's. Closer means Into, further means Out. Differences under
// 50 m don't count. The majority wins; a tie or no votes gives Unknown.
DirectionVote voteFromTrips(const std::string &atco,
                            const std::vector<std::vector<std::string>> &trips,
                            const CoordLookup &coordOf);

#endif // DIRECTION_H
