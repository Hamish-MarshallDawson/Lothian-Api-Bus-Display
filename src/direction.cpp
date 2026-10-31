// direction.cpp: see direction.h

#include "direction.h"

#include <cmath>

#include "settings.h"

static const double DEG = M_PI / 180.0;
static const double MARGIN_M = 50.0;

double distanceM(LatLon a, LatLon b) {
    static const double R = 6371000.0;
    double dLat = (b.lat - a.lat) * DEG;
    double dLon = (b.lon - a.lon) * DEG;
    double h = std::sin(dLat / 2) * std::sin(dLat / 2)
             + std::cos(a.lat * DEG) * std::cos(b.lat * DEG) * std::sin(dLon / 2) * std::sin(dLon / 2);
    return R * 2 * std::atan2(std::sqrt(h), std::sqrt(1 - h));
}

int roundToTenMetres(double m) {
    if (!(m >= 0.0)) return -1;  // also catches NaN
    return static_cast<int>(std::floor(m / 10.0 + 0.5)) * 10;
}

double distToCentreM(LatLon p) {
    return distanceM(p, LatLon{CITY_CENTER_LAT(), CITY_CENTER_LON()});
}

DirectionVote voteFromTrips(const std::string &atco,
                            const std::vector<std::vector<std::string>> &trips,
                            const CoordLookup &coordOf) {
    DirectionVote v;
    LatLon here;
    if (trips.empty() || !coordOf(atco, here)) return v;
    const double dHere = distToCentreM(here);

    for (const auto &trip : trips) {
        for (size_t i = 0; i + 1 < trip.size(); ++i) {
            if (trip[i] != atco) continue;
            LatLon next;
            if (!coordOf(trip[i + 1], next)) continue;
            const double dNext = distToCentreM(next);
            if (dNext < dHere - MARGIN_M) v.into++;
            else if (dNext > dHere + MARGIN_M) v.out++;
        }
    }

    if (v.into > v.out) v.dir = Direction::Into;
    else if (v.out > v.into) v.dir = Direction::Out;
    return v;
}
