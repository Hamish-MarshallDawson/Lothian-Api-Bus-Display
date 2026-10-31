#ifndef TIMETABLES_H
#define TIMETABLES_H

// Timetable data for working out direction of travel. Cached per day: a service
// timetable is fetched once per service per day, and a stop's service list once
// per stop per day.

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
bool tripsFor(const ServiceRef &svc, const std::string &date, std::vector<std::vector<std::string>> &out);

#endif // TIMETABLES_H
