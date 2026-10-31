// tracking_test.cpp: bus tracking and the tile cache rules, offline.
// Real-data cases use fixtures/live-*.json (city-centre vehicles and the Scott
// Monument departures). Other cases use hand-made positions with exact answers.
// Run from "src/": make test

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "../departures.h"
#include "../direction.h"
#include "../tiles.h"
#include "../timetables.h"
#include "../tracking.h"

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

static std::string loadText(const char *path) {
    std::ifstream f(path);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static time_t localTime(int y, int mo, int d, int h, int mi, int s) {
    std::tm tm{};
    tm.tm_year = y - 1900;
    tm.tm_mon = mo - 1;
    tm.tm_mday = d;
    tm.tm_hour = h;
    tm.tm_min = mi;
    tm.tm_sec = s;
    tm.tm_isdst = -1;
    return mktime(&tm);
}

// ---------------------------------------------------------------------------
// Real data

static const char *SCOTT = "6200206470";

static void testParseApiTime() {
    std::tm expect{};
    expect.tm_year = 126; expect.tm_mon = 9; expect.tm_mday = 31;
    expect.tm_hour = 16; expect.tm_min = 20; expect.tm_sec = 0;
    const time_t utc = timegm(&expect);   // 2026-10-31 16:20:00 UTC
    CHECK(parseApiTime("2026-10-31T16:20:00+00:00") == utc);
    CHECK(parseApiTime("2026-10-31T16:20:00Z") == utc);
    CHECK(parseApiTime("2026-10-31T17:20:00+01:00") == utc);       // same instant
    CHECK(parseApiTime("2026-10-31T16:20:00") == localTime(2026, 10, 31, 16, 20, 0));
    CHECK(parseApiTime("nonsense") == 0);
}

// The real feed list: the watched journey is one of the matched vehicles
static void testRealFeedLookup() {
    const std::string body = loadText("fixtures/live-vehicles-centre.json");
    nlohmann::json arr = nlohmann::json::parse(body);
    const std::string journey = arr[0]["journeyid"].get<std::string>();
    const std::string id = arr[0]["id"].get<std::string>();

    VehicleFeed feed;
    feed.watch(journey);
    CHECK(feed.setFromList(body, 1000));
    CHECK(feed.vehicles().size() == 1);
    CHECK(feed.vehicleId() == id);
    if (feed.vehicles().size() == 1) {
        CHECK(feed.vehicles()[0].journeyId == journey);
        CHECK(feed.vehicles()[0].lat > 55.8 && feed.vehicles()[0].lat < 56.0);   // Edinburgh
        CHECK(feed.vehicles()[0].lastUpdate > 0);
    }

    // A journey not on the road yet: not found, and nothing kept
    VehicleFeed none;
    none.watch("0000_0000");
    CHECK(!none.setFromList(body, 1000));
    CHECK(none.vehicles().empty());
}

// The one-vehicle reply, for the 20 s poll
static void testRealStatusPoll() {
    const std::string body = loadText("fixtures/live-vehiclestatus-centre.json");
    nlohmann::json v = nlohmann::json::parse(body);
    const std::string journey = v["journeyid"].get<std::string>();

    VehicleFeed feed;
    feed.watch(journey);
    CHECK(feed.setFromStatus(body, 1000));
    CHECK(feed.vehicles().size() == 1);

    // The vehicle now runs another journey: no longer ours, and it must be looked up again
    feed.watch("0000_0000");
    feed.setFromStatus(body, 1000);
    CHECK(feed.vehicles().empty());
}

// Real departures at Scott Monument matched to real trips by journey start time
static void testRealTripMatch() {
    nlohmann::json tt = loadJson("fixtures/live-timetable-31-scottmon.json");
    std::vector<TripDetail> details = parseTripDetails(tt);
    CHECK(details.size() == 8);

    nlohmann::json se = loadJson("fixtures/live-stopevents-scottmon.json");
    StopBoard board = boardFromStopEvents(se, localTime(2026, 10, 31, 16, 0, 0));
    // Rule 1: a trip whose start is the journey's start (local) and that calls here.
    // Expected four at this stop; each must match exactly that trip.
    int startRule = 0, startRuleOk = 0;
    for (const auto &dep : board.departures) {
        if (dep.journeyStart.empty()) continue;
        std::tm tm{};
        time_t st = parseApiTime(dep.journeyStart);
        localtime_r(&st, &tm);
        char hhmm[8];
        std::strftime(hhmm, sizeof(hhmm), "%H:%M", &tm);

        int trip = -1;
        for (size_t i = 0; i < details.size() && trip < 0; ++i) {
            if (details[i].starttime != hhmm) continue;
            for (const auto &e : details[i].events) {
                if (e.atco == SCOTT) { trip = static_cast<int>(i); break; }
            }
        }
        if (trip < 0) continue;
        ++startRule;
        if (matchTripIndex(details, dep, SCOTT) == trip) ++startRuleOk;
    }
    CHECK(startRule == 4);
    CHECK(startRuleOk == startRule);

    // Rule 2 covers the rest: a departure with no start-time trip matches by its
    // scheduled time where the timetable has that trip. Five in total at this stop.
    int total = 0;
    for (const auto &dep : board.departures) {
        if (matchTripIndex(details, dep, SCOTT) >= 0) ++total;
    }
    CHECK(total == 5);
}

// ---------------------------------------------------------------------------
// Synthetic trip: A 16:00, B 16:05, C 16:10, D 16:15, on a line into the centre

static std::vector<TrackStop> syntheticTrip() {
    std::vector<TrackStop> t;
    const char *atco[] = {"TA", "TB", "TC", "TD"};
    const double lon[] = {-3.24, -3.22, -3.20, -3.18};
    const char *sched[] = {"16:00", "16:05", "16:10", "16:15"};
    for (int i = 0; i < 4; ++i) {
        TrackStop s;
        s.atco = atco[i];
        s.name = "";
        s.lat = 55.95;
        s.lon = lon[i];
        s.scheduled = sched[i];
        t.push_back(s);
    }
    return t;
}

static std::string vehicleJson(const std::string &journey, double lon, time_t lastUpdate) {
    char buf[512];
    std::tm tm{};
    gmtime_r(&lastUpdate, &tm);
    char ts[32];
    std::strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", &tm);
    std::snprintf(buf, sizeof(buf),
                  "[{\"id\":\"9001\",\"journeyid\":\"%s\",\"fleetno\":\"T1\",\"operator\":\"LOTH\","
                  "\"coordinate\":{\"latitude\":55.95,\"longitude\":%.6f},\"lastupdate\":\"%s\","
                  "\"delay\":0,\"status\":\"Unknown\"}]",
                  journey.c_str(), lon, ts);
    return buf;
}

static Departure synthDeparture(const std::string &journey) {
    Departure d;
    d.service = "T";
    d.destination = "Test";
    d.scheduled = "16:10";
    d.minutes = 0;
    d.hasMinutes = true;
    d.journeyId = journey;
    return d;
}

static void testBuildTrackStates() {
    const time_t now = localTime(2026, 10, 31, 16, 10, 0);
    const std::vector<TrackStop> trip = syntheticTrip();
    BusStopInfo my;
    my.stopID = "TC";                    // my stop is C

    // Between B and C, 30 s old: approaching C, which is my stop
    {
        VehicleFeed feed;
        feed.watch("J1");
        feed.setFromList(vehicleJson("J1", -3.21, now - 30), now);
        BusTrack t = buildTrack(synthDeparture("J1"), my, trip, feed, now);
        CHECK(t.state == TrackState::Live);
        CHECK(t.myStopIndex == 2);
        CHECK(t.nextStopIndex == 2);
        CHECK(t.stopsAway == 0);
        CHECK(t.lastUpdateAgeS == 30);
        // Scheduled 16:07:30 at that point; now 16:10: about 2-3 min late
        CHECK(t.lateMin == 2 || t.lateMin == 3);
    }
    // Between C and D: past my stop
    {
        VehicleFeed feed;
        feed.watch("J1");
        feed.setFromList(vehicleJson("J1", -3.19, now - 30), now);
        BusTrack t = buildTrack(synthDeparture("J1"), my, trip, feed, now);
        CHECK(t.state == TrackState::Finished);
        CHECK(t.nextStopIndex == 3);
    }
    // Between A and B, last reported 5 min ago: stale
    {
        VehicleFeed feed;
        feed.watch("J1");
        feed.setFromList(vehicleJson("J1", -3.23, now - 300), now);
        BusTrack t = buildTrack(synthDeparture("J1"), my, trip, feed, now);
        CHECK(t.state == TrackState::Stale);
        CHECK(t.stopsAway == 2 - 1);   // my C (2), next B (1)
    }
    // No vehicle for this journey yet: not started
    {
        VehicleFeed feed;
        feed.watch("J1");
        BusTrack t = buildTrack(synthDeparture("J1"), my, trip, feed, now);
        CHECK(t.state == TrackState::NotStarted);
        CHECK(t.stops.size() == 4);
    }
    // No timetable trip at all: unavailable
    {
        VehicleFeed feed;
        BusTrack t = buildTrack(synthDeparture("J1"), my, {}, feed, now);
        CHECK(t.state == TrackState::Unavailable);
    }
}

static void testMatchRule() {
    // Start time first; else the trip that calls here at the scheduled time
    std::vector<TripDetail> details = {
        TripDetail{"16:00", {TripEvent{"X", "15:55"}, TripEvent{"C", "16:10"}}},
        TripDetail{"15:30", {TripEvent{"C", "15:40"}}},
    };
    // No offset in the start: read as local time, so the result does not depend on the zone
    Departure dep;
    dep.journeyStart = "2026-10-31T15:30:00";
    dep.scheduled = "16:10";
    CHECK(matchTripIndex(details, dep, "C") == 1);      // start time 15:30 calls at C

    // Without a usable start, the trip calling here at 16:10 is the first one
    Departure bySched;
    bySched.scheduled = "16:10";
    CHECK(matchTripIndex(details, bySched, "C") == 0);

    Departure none;
    none.scheduled = "17:00";
    CHECK(matchTripIndex(details, none, "C") == -1);
}

static void testWatchStuckAndHistory() {
    const time_t t0 = localTime(2026, 10, 31, 16, 0, 0);
    BusTrack live;
    live.state = TrackState::Live;
    live.busLat = 55.95;
    live.busLon = -3.21;
    live.lateMin = 2;

    // Same place for 3 minutes: stuck
    TrackWatch still;
    for (int k = 0; k <= 180; k += 30) {
        live.lateMin = 2;
        still.observe(live, t0 + k);
    }
    CHECK(still.current().stuck);
    CHECK(still.current().stuckForS >= 180);

    // Moving about 150 m every 30 s: not stuck
    TrackWatch moving;
    for (int k = 0; k <= 180; k += 30) {
        BusTrack m = live;
        m.busLon = -3.21 + 0.003 * (k / 30);
        moving.observe(m, t0 + k);
    }
    CHECK(!moving.current().stuck);

    // Lateness history: one point per 15 s, not one per call
    TrackWatch hist;
    hist.observe(live, t0);
    hist.observe(live, t0 + 5);    // too soon: no new point
    hist.observe(live, t0 + 15);
    CHECK(hist.current().lateHistory.size() == 2);

    // Not live: no stuck judgement
    BusTrack stale = live;
    stale.state = TrackState::Stale;
    still.observe(stale, t0 + 400);
    CHECK(!still.current().stuck);
}

// ---------------------------------------------------------------------------
// Tile cache rules (no network)

static void testTileExpiry() {
    bool noStore = false;
    const time_t fetched = 1000000;
    CHECK(tileExpiry("max-age=3600", "", fetched, noStore) == fetched + 3600);
    CHECK(!noStore);
    CHECK(tileExpiry("", "", fetched, noStore) == fetched + 7 * 24 * 3600);    // no headers: 7 days
    std::tm future{};
    future.tm_year = 150; future.tm_mon = 0; future.tm_mday = 1;
    CHECK(tileExpiry("", "Sat, 01 Jan 2050 00:00:00 GMT", fetched, noStore) == timegm(&future));
    // An Expires date in the past is stale at once (fetched is a real 2026 time)
    std::tm now2026{};
    now2026.tm_year = 126; now2026.tm_mon = 9; now2026.tm_mday = 31; now2026.tm_hour = 12;
    const time_t fetched2026 = timegm(&now2026);
    CHECK(tileExpiry("", "Mon, 01 Jan 2001 00:00:00 GMT", fetched2026, noStore) == fetched2026);
    CHECK(tileExpiry("no-store", "", fetched, noStore) == fetched);
    CHECK(noStore);
    CHECK(tileExpiry("no-cache", "", fetched, noStore) == fetched);   // must revalidate every time
    CHECK(!noStore);
}

static void testTileUrlAndPng() {
    setenv("BUS_TILE_URL", "https://tiles.example.test/{z}/{x}/{y}.png", 1);
    TileId t{16, 32186, 20422};
    CHECK(tileUrl(t) == "https://tiles.example.test/16/32186/20422.png");
    const unsigned char sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    CHECK(isPng(std::string(reinterpret_cast<const char *>(sig), 8)));
    CHECK(!isPng("not a png"));
}

int main() {
    testParseApiTime();
    testRealFeedLookup();
    testRealStatusPoll();
    testRealTripMatch();
    testBuildTrackStates();
    testMatchRule();
    testWatchStuckAndHistory();
    testTileExpiry();
    testTileUrlAndPng();
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("tracking_test: all checks passed\n");
    return 0;
}
