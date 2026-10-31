// tracking.cpp: stubs for the API commit. Real implementations follow in M7.

#include "tracking.h"

bool findTripForDeparture(const Departure &, const BusStopInfo &, std::vector<TrackStop> &trip) {
    trip.clear();
    return false;
}

bool VehicleFeed::update(time_t now) {
    (void)now;
    return false;
}

bool VehicleFeed::setFromJson(const std::string &, time_t) {
    return false;
}

BusTrack buildTrack(const Departure &dep, const BusStopInfo &myStop,
                    const std::vector<TrackStop> &trip, const VehicleFeed &, time_t) {
    (void)dep;
    (void)myStop;
    (void)trip;
    BusTrack t;
    t.state = TrackState::Unavailable;
    return t;
}

void TrackWatch::observe(const BusTrack &t, time_t) {
    current_ = t;
}

void TrackWatch::reset() {
    current_ = BusTrack();
    positions_.clear();
    stuckSince_ = 0;
}
