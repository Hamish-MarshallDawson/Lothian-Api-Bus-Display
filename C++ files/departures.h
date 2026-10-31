#ifndef DEPARTURES_H
#define DEPARTURES_H

// Plain data the UI renders from. Nothing here is JSON.

#include <ctime>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

struct Departure {
    std::string service;      // route number, e.g. "10", "N22"
    std::string destination;  // as the API gives it
    std::string scheduled;    // timetabled departure "HH:MM" (local), "" if unknown
    bool hasMinutes = false;  // false when no usable time was supplied (not shown)
    int minutes = 0;          // whole minutes from the board's fetchedAt; <0 = already due
    bool live = false;        // true when a real-time prediction was used
    bool cancelled = false;
    bool delayed = false;
    int seatCount = -1;       // -1 when the API did not say
    std::string seatLevel;    // "None", "Low", "Medium", "High" or "" if unknown
};

enum class BoardStatus {
    Loading,        // not fetched yet
    Ok,             // data fetched and at least one departure
    NoDepartures,   // data fetched, nothing due in the lookahead window
    NetworkError,   // could not reach the API (or unreadable response)
    KeyRejected,    // 401/403, or no BUSTRACKER_KEY set
    RateLimited     // 429
};

struct StopBoard {
    std::string stopName;
    std::string indicator;    // e.g. "Stop F", "" if unknown
    std::string atcocode;
    std::vector<Departure> departures;  // sorted by minutes, earliest first
    std::vector<std::string> notices;   // stop-level notice texts
    time_t fetchedAt = 0;               // 0 = never fetched
    BoardStatus status = BoardStatus::Loading;
    std::string statusDetail;           // short human text for logs / error screens
};

// Build a board from one element of a StopEvents response array.
// "now" is the reference time for minutes-until (the fetch time).
StopBoard boardFromStopEvents(const nlohmann::json &stopObj, time_t now);

// Call StopEvents for one stop and map every failure to a board status.
StopBoard fetchStopBoard(const std::string &atcocode, time_t now);

// Name of a status for logs, e.g. "key rejected".
const char *boardStatusName(BoardStatus s);

#endif // DEPARTURES_H
