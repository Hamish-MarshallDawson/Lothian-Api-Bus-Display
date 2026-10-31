#include "pairing.h"

#include <cmath>

double distanceM(double lat1, double lon1, double lat2, double lon2) {
    const double R = 6371000.0;
    const double p = M_PI / 180.0;
    const double dLat = (lat2 - lat1) * p;
    const double dLon = (lon2 - lon1) * p;
    const double a = std::sin(dLat / 2) * std::sin(dLat / 2) +
                     std::cos(lat1 * p) * std::cos(lat2 * p) * std::sin(dLon / 2) * std::sin(dLon / 2);
    return 2 * R * std::atan2(std::sqrt(a), std::sqrt(1 - a));
}

int pairedIndex(const std::vector<PairStop> &candidates, const PairStop &current, double maxM) {
    for (size_t i = 0; i < candidates.size(); ++i)
        if (candidates[i].id == current.id) return static_cast<int>(i);

    // Nearest within range; a candidate with the same name wins over a nearer one without
    int best = -1, bestSame = -1;
    double bestD = maxM + 1, bestSameD = maxM + 1;
    for (size_t i = 0; i < candidates.size(); ++i) {
        const double d = distanceM(current.lat, current.lon, candidates[i].lat, candidates[i].lon);
        if (d > maxM) continue;
        if (d < bestD) { bestD = d; best = static_cast<int>(i); }
        if (candidates[i].name == current.name && d < bestSameD) {
            bestSameD = d;
            bestSame = static_cast<int>(i);
        }
    }
    return bestSame >= 0 ? bestSame : best;
}
