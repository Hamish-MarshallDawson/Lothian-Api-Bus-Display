#ifndef BUSSTOP_H
#define BUSSTOP_H

#include <string>

struct BusStop {
    std::string stop_id;
    std::string name;
    double latitude = 0.0;
    double longitude = 0.0;
    double dist = 0.0; // meters from query point
};

#endif // BUSSTOP_H