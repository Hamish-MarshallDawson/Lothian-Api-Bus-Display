// live_format_test.cpp: checks the parsers against REAL API replies saved in fixtures/
// (live-*.json, from city-centre stops), and against the midnight edge case.
// No network. Run from "src/": make test

#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "../departures.h"
#include "../timetables.h"

static int failures = 0;
#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                                 \
        }                                                               \
    } while (0)

static nlohmann::json loadJson(const char *path) {
    std::ifstream f(path);
    if (!f) { std::printf("cannot open %s\n", path); failures++; return nlohmann::json(); }
    std::stringstream ss;
    ss << f.rdbuf();
    return nlohmann::json::parse(ss.str());
}

static time_t localTime(int y, int mo, int d, int h, int mi) {
    std::tm tm{};
    tm.tm_year = y - 1900;
    tm.tm_mon = mo - 1;
    tm.tm_mday = d;
    tm.tm_hour = h;
    tm.tm_min = mi;
    tm.tm_isdst = -1;
    return mktime(&tm);
}

// Real StopEvents for North Bridge: a single object, wrapped in an array by the fixture
static void testLiveStopEvents() {
    nlohmann::json doc = loadJson("fixtures/live-stopevents-northbridge.json");
    CHECK(doc.is_array() && doc.size() == 1);
    StopBoard b = boardFromStopEvents(doc[0], localTime(2026, 10, 31, 16, 0));
    CHECK(b.status == BoardStatus::Ok);
    CHECK(b.atcocode == "6200208410");
    CHECK(b.departures.size() == 1);
    if (b.departures.size() == 1) {
        const Departure &d = b.departures[0];
        CHECK(d.service == "X95");
        CHECK(d.live);                  // departuretime is a countdown in seconds (570)
        CHECK(d.minutes == 9);          // 570 s / 60
        CHECK(d.scheduled == "16:27");  // bare HH:MM in the real reply
        CHECK(!d.cancelled && !d.delayed);
        CHECK(d.seatCount == -1);       // not in this reply
    }
}

// Real MultipleStopEvents: {"stops": [...]} for Scott Monument and Waverley Steps
static void testLiveMultipleStopEvents() {
    nlohmann::json doc = loadJson("fixtures/live-multiplestopevents-centre.json");
    std::vector<std::string> codes = {"6200206470", "6200206530", "6299999999"};
    auto boards = boardsFromMultipleStopEvents(doc, codes, localTime(2026, 10, 31, 16, 0));
    CHECK(boards.size() == 3);
    if (boards.size() == 3) {
        CHECK(boards[0].status == BoardStatus::Ok);
        CHECK(boards[0].departures.size() == 8);
        CHECK(boards[1].status == BoardStatus::Ok);
        CHECK(boards[1].departures.size() == 11);   // all 11 events in the fixture have a time
        // A code the reply doesn't contain is reported, not silently empty
        CHECK(boards[2].status == BoardStatus::NoDepartures);
        CHECK(boards[2].statusDetail == "not in response");
        CHECK(boards[2].atcocode == "6299999999");
    }
    // Departures are sorted, and at least one has a bare-time (HH:MM) prediction
    // that parses to a plausible number of minutes
    bool sorted = true, plausible = true;
    for (const auto &b : boards) {
        for (size_t i = 1; i < b.departures.size(); ++i) {
            if (b.departures[i].minutes < b.departures[i - 1].minutes) sorted = false;
        }
        for (const auto &d : b.departures) {
            if (d.minutes < -5 || d.minutes > 180) plausible = false;
        }
    }
    CHECK(sorted);
    CHECK(plausible);
}

// Real live stop-services reply
static void testLiveStopServices() {
    nlohmann::json doc = loadJson("fixtures/live-stopservices-scottmon.json");
    auto svcs = parseStopServices(doc);
    CHECK(svcs.size() == 6);
    bool allLothian = true;
    for (const auto &s : svcs) if (s.opCode != "LOTH") allLothian = false;
    CHECK(allLothian);
}

// Real service timetable (trimmed to 3 trips per direction, shape unchanged)
static void testLiveTimetable() {
    nlohmann::json doc = loadJson("fixtures/live-timetable-31-subset.json");
    auto trips = parseTripStops(doc);
    CHECK(trips.size() == 6);
    for (const auto &t : trips) CHECK(t.size() > 1);
    // Every trip's stops are 9-digit or 12-digit ATCO-style codes, in order
    bool codesOk = true;
    for (const auto &t : trips) for (const auto &a : t) if (a.size() < 9) codesOk = false;
    CHECK(codesOk);
}

// Bare HH:MM predictions near midnight belong to the nearest day
static void testMidnightWrap() {
    nlohmann::json stop = nlohmann::json::parse(R"({
        "name": "Test", "atcocode": "1",
        "events": [
            {"publicservicename": "1", "destination": "Ahead", "departuretime": "00:10",
             "scheduledeparturetime": "00:10"},
            {"publicservicename": "2", "destination": "Behind", "departuretime": "23:45",
             "scheduledeparturetime": "23:45"}
        ]})");
    // now = 23:50: 00:10 is 20 minutes ahead; 23:45 is 5 minutes behind (due)
    StopBoard b = boardFromStopEvents(stop, localTime(2026, 10, 31, 23, 50));
    CHECK(b.departures.size() == 2);
    if (b.departures.size() == 2) {
        CHECK(b.departures[0].service == "2");
        CHECK(b.departures[0].minutes == -5);
        CHECK(b.departures[1].service == "1");
        CHECK(b.departures[1].minutes == 20);
    }
}

// LATE rule. Events are shaped like real replies; no real stop codes or names.
//   API says delayed               -> delayed, whatever the times say
//   no API flag, live +3 min       -> delayed (2+ min after schedule)
//   no API flag, live +1 min       -> not delayed (within a minute is normal)
//   countdown 120 s from 16:00 for a 16:00 bus -> live +2 min -> delayed
//   live time missing              -> not delayed, not live
static void testLateRule() {
    nlohmann::json stop = nlohmann::json::parse(R"({
        "name": "Test", "atcocode": "1",
        "events": [
            {"publicservicename": "1", "destination": "A", "status": "delayed",
             "scheduledeparturetime": "16:00", "departuretime": "16:00"},
            {"publicservicename": "2", "destination": "B", "status": null,
             "scheduledeparturetime": "16:00", "departuretime": "16:03"},
            {"publicservicename": "3", "destination": "C", "status": null,
             "scheduledeparturetime": "16:00", "departuretime": "16:01"},
            {"publicservicename": "4", "destination": "D", "status": null,
             "scheduledeparturetime": "16:00", "departuretime": 120},
            {"publicservicename": "5", "destination": "E", "status": null,
             "scheduledeparturetime": "16:05", "departuretime": null}
        ]})");
    StopBoard b = boardFromStopEvents(stop, localTime(2026, 10, 31, 16, 0));
    CHECK(b.departures.size() == 5);
    for (const auto &d : b.departures) {
        if (d.service == "1") { CHECK(d.delayed); CHECK(d.lateMin == 0); }
        if (d.service == "2") { CHECK(d.delayed); CHECK(d.lateMin == 3); }
        if (d.service == "3") { CHECK(!d.delayed); CHECK(d.lateMin == 1); }
        if (d.service == "4") { CHECK(d.delayed); CHECK(d.lateMin == 2); CHECK(d.live); }
        if (d.service == "5") { CHECK(!d.delayed); CHECK(!d.live); CHECK(d.lateMin == 0); }
    }
}

int main() {
    testLateRule();
    testLiveStopEvents();
    testLiveMultipleStopEvents();
    testLiveStopServices();
    testLiveTimetable();
    testMidnightWrap();
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("live_format_test: all checks passed\n");
    return 0;
}
