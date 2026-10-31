#ifndef TRACKING_H
#define TRACKING_H

// Where a departure's bus is: its trip, position, how far from my stop, and how
// late it is. Built from the departure, the trip's timetable and the live vehicle
// feed. Matching is by journey id only (vehicle.journeyid == departure.journeyId).

#include <ctime>
#include <string>
#include <utility>
#include <vector>

#include "departures.h"
#include "busStopInfo.h"

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

// One vehicle from GET /vehiclestatus/vehiclestatusextended
struct Vehicle {
    std::string journeyId;
    std::string fleetNo;
    std::string operatorCode;
    double lat = 0.0;
    double lon = 0.0;
    time_t lastUpdate = 0;            // 0 if unknown
    int delay = 0;                    // as the API reports it (often 0; not trusted)
    std::string status;               // as the API reports it (often "Unknown")
};

// Shared vehicle feed. Poll only while a detail screen is open, every 15-20 s,
// one fetch for the whole screen. Never from the board.
class VehicleFeed {
public:
    // Fetches if the last fetch is 15 s or more old. Returns true if a request was made.
    bool update(time_t now);
    const std::vector<Vehicle> &vehicles() const { return vehicles_; }
    bool lastFetchOk() const { return ok_; }
    time_t lastFetchAt() const { return lastAt_; }

    // Replace the list from a reply (used by update(), and by tests)
    bool setFromJson(const std::string &body, time_t now);

private:
    std::vector<Vehicle> vehicles_;
    time_t lastAt_ = 0;
    bool ok_ = false;
};

// The trip a departure belongs to, from GetServiceTimetable. True when found, with
// the trip's stops in order and their scheduled times. Stub until implemented.
bool findTripForDeparture(const Departure &dep, const BusStopInfo &myStop,
                          std::vector<TrackStop> &trip);

// Build the track for a departure at my stop. "trip" is the matched trip (may be
// empty, which gives Unavailable). Pure given its inputs.
BusTrack buildTrack(const Departure &dep, const BusStopInfo &myStop,
                    const std::vector<TrackStop> &trip, const VehicleFeed &feed, time_t now);

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
    std::vector<Pos> positions_;   // recent bus positions while watched
    time_t stuckSince_ = 0;
};

#endif // TRACKING_H
