#ifndef TRACKING_H
#define TRACKING_H

// Where a departure's bus is: its trip, position, how far from my stop, and how
// late it is. Built from the departure, the trip's timetable and the live vehicle.
// Matching is by journey id only (vehicle.journeyid == departure.journeyId).

#include <ctime>
#include <string>
#include <utility>
#include <vector>

#include "departures.h"
#include "busStopInfo.h"
#include "direction.h"
#include "timetables.h"

struct TrackStop {
    std::string atco;
    std::string name;
    double lat = 0.0;
    double lon = 0.0;
    std::string scheduled;   // HH:MM, the timetabled time at this stop
};

enum class TrackState {
    NotStarted,   // no live vehicle yet: show the origin and its start time
    Live,         // position reported within 2 min
    Stale,        // last position report older than 2 min
    Finished,     // the bus is past my stop
    Unavailable   // no timetable match, or the feed failed
};

struct BusTrack {
    TrackState state = TrackState::Unavailable;
    std::string service;
    std::string destination;
    std::string fleetNo;
    std::vector<TrackStop> stops;     // the trip's stops in order (origin .. terminus)
    int myStopIndex = -1;             // the stop the board is for
    int nextStopIndex = -1;           // the stop the bus is approaching
    double busLat = 0.0;              // valid when state is Live or Stale
    double busLon = 0.0;
    long lastUpdateAgeS = -1;         // seconds since the vehicle's last position report
    int lateMin = 0;                  // estimated: now minus the scheduled time at the bus's position
    int stopsAway = -1;               // myStopIndex - nextStopIndex
    int dueMin = -1;                  // the board's minutes for this departure
    bool stuck = false;               // position moved < 40 m over >= 3 min of observations
    long stuckForS = 0;
    std::vector<std::pair<time_t, int>> lateHistory;  // (time, lateMin) observed while watched
};

// One vehicle, from the feed list or a VehicleStatus reply
struct Vehicle {
    std::string id;                   // VehicleId, for VehicleStatus
    std::string journeyId;
    std::string fleetNo;
    std::string operatorCode;
    double lat = 0.0;
    double lon = 0.0;
    time_t lastUpdate = 0;            // 0 if unknown
    int delay = 0;                    // as the API reports it (often 0; not trusted)
    std::string status;               // as the API reports it (often "Unknown")
};

// Follows one journey's vehicle, on the bus detail screen only. The big list
// (vehiclestatusextended, ~170 KB) is read only to find the vehicle, and again
// only if the vehicle changes or disappears. While found, one small request
// (VehicleStatus?VehicleId=) every 20 s. Nothing is polled from the board.
class VehicleFeed {
public:
    static const time_t POLL_EVERY_S = 20;
    static const time_t LOOKUP_RETRY_S = 120;   // when the vehicle is not in the list yet

    // Start following a journey. Clears any vehicle; the next update() looks it up.
    void watch(const std::string &journeyId);
    // Stop following (detail screen closed)
    void stop();

    // Refreshes as the rules above say. Call every refresh tick while the detail
    // screen is open. Returns true if a request was made.
    bool update(time_t now);

    // The watched vehicle: 0 or 1 entries
    const std::vector<Vehicle> &vehicles() const { return vehicles_; }
    bool lastFetchOk() const { return ok_; }
    time_t lastFetchAt() const { return lastAt_; }
    const std::string &vehicleId() const { return vehicleId_; }

    // Replies, split out for tests. setFromList takes the big list and finds the
    // watched journey; setFromStatus takes one VehicleStatus object.
    bool setFromList(const std::string &body, time_t now);
    bool setFromStatus(const std::string &body, time_t now);

private:
    std::string journeyId_;
    std::string vehicleId_;
    std::vector<Vehicle> vehicles_;
    time_t lastAt_ = 0;          // last request of any kind
    time_t lastLookupAt_ = 0;    // last list request
    bool ok_ = false;
};

// Index of the trip a departure belongs to, or -1. Matches the journey's start
// time (journeyStart, local) to the trip's starttime and requires the trip to call
// at myAtco. Otherwise takes the trip that calls at myAtco at the departure's
// scheduled time. Pure.
int matchTripIndex(const std::vector<TripDetail> &details, const Departure &dep,
                   const std::string &myAtco);

// Nearest point of bus on the trip's polyline. seg is the segment (stop seg to
// seg+1), t its fraction 0..1. Returns false for trips with fewer than 2 stops.
bool projectOnTrip(const std::vector<TrackStop> &trip, LatLon bus, int &seg, double &t);

// The trip a departure belongs to, from GetServiceTimetable. True when found, with
// the trip's stops in order and their scheduled times.
bool findTripForDeparture(const Departure &dep, const BusStopInfo &myStop,
                          std::vector<TrackStop> &trip);

// Build the track for a departure at my stop. "trip" is the matched trip (may be
// empty, which gives Unavailable). Pure given its inputs.
BusTrack buildTrack(const Departure &dep, const BusStopInfo &myStop,
                    const std::vector<TrackStop> &trip, const VehicleFeed &feed, time_t now);

// Parse an API timestamp ("...T..:..:..+HH:MM", "...Z", or no offset = local). 0 if unreadable.
time_t parseApiTime(const std::string &iso);

// Keeps lateHistory and stuck state between refreshes while the detail screen
// is open. observe() records one BusTrack; current() is the latest with history.
class TrackWatch {
public:
    void observe(const BusTrack &t, time_t now);
    const BusTrack &current() const { return current_; }
    void reset();

private:
    struct Pos { time_t t; double lat; double lon; };
    BusTrack current_;
    std::vector<Pos> positions_;                        // last 6 min of positions while Live
    std::vector<std::pair<time_t, int>> history_;       // lateness, one point per 15 s, last 40
};

#endif // TRACKING_H
