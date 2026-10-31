#ifndef TIMETABLES_H
#define TIMETABLES_H

// Timetable data for working out direction of travel. Cached per day: a service
// timetable is fetched once per service per day, and a stop's service list once
// per stop per day.

#include <ctime>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

struct ServiceRef {
    std::string publicservicename;  // e.g. "22"
    std::string opCode;             // operator code, as GetServiceTimetable wants it
};

// Parse GetStopServices: {"services":[{publicservicename, operator, serviceRef}]}
std::vector<ServiceRef> parseStopServices(const nlohmann::json &doc);

// Parse GetServiceTimetable: every trip in every direction, as ordered ATCO lists
std::vector<std::vector<std::string>> parseTripStops(const nlohmann::json &doc);

// Network, cached for the given date (CCYY-MM-DD). Return false only on a failed
// request (nothing is cached then). An empty result is a successful answer.
bool stopServicesFor(const std::string &atco, const std::string &date, std::vector<ServiceRef> &out);
// Trips for a service on a day, as ordered ATCO lists. Cached on disk per service
// and day. GetServiceTimetable accepts no from/to window (400), so this is always
// the whole day, ~0.4-0.9 MB per service: fetched once per service per day.
bool tripsFor(const ServiceRef &svc, const std::string &date, std::vector<std::vector<std::string>> &out);

// Trip detail for matching a departure to its trip: the trip's start time and
// every stop with its scheduled departure time (HH:MM, local).
struct TripEvent {
    std::string atco;
    std::string departure;   // HH:MM
};
struct TripDetail {
    std::string starttime;   // HH:MM, the trip's start at its origin
    std::vector<TripEvent> events;
};

// Parse GetServiceTimetable into trip details, in order
std::vector<TripDetail> parseTripDetails(const nlohmann::json &doc);

// Trip details for a service on a day (whole day, cached as above)
bool tripDetailsFor(const ServiceRef &svc, const std::string &date, std::vector<TripDetail> &out);

#endif // TIMETABLES_H
