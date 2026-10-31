// direction_test.cpp: checks the timetable direction vote and the timetable parsers
// with hand-made fixtures. No network. Run from "src/": make test
//
// Test stops lie on one line running into the city centre (settings.h):
//   TESTW1  -3.24 lon,  about 3.2 km from the centre
//   TESTW2  -3.22 lon,  about 2.0 km
//   TESTC1  -3.20 lon,  about 0.7 km
// Every trip in fixtures/timetable-hand.json moves along that line.

#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "../direction.h"
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

static const std::map<std::string, LatLon> kStops = {
    {"TESTW1", {55.95, -3.24}},
    {"TESTW2", {55.95, -3.22}},
    {"TESTC1", {55.95, -3.20}},
};

static bool coordOf(const std::string &atco, LatLon &out) {
    auto it = kStops.find(atco);
    if (it == kStops.end()) return false;
    out = it->second;
    return true;
}

static void testDistances() {
    LatLon w1 = kStops.at("TESTW1"), c1 = kStops.at("TESTC1");
    CHECK(distToCentreM(w1) > distToCentreM(kStops.at("TESTW2")));
    CHECK(distToCentreM(kStops.at("TESTW2")) > distToCentreM(c1));
    CHECK(distToCentreM(c1) < 1000.0);
}

static void testVoteFromHandMadeTimetable() {
    nlohmann::json doc = loadJson("fixtures/timetable-hand.json");
    auto trips = parseTripStops(doc);
    CHECK(trips.size() == 3);                         // 2 outbound + 1 inbound
    CHECK(trips[0].size() == 3);

    // W2: two outbound trips head to C1 (closer), one inbound heads to W1 (further)
    DirectionVote w2 = voteFromTrips("TESTW2", trips, coordOf);
    CHECK(w2.into == 2 && w2.out == 1);
    CHECK(w2.dir == Direction::Into);

    // W1: the outbound trips go W1 -> W2, which is closer. The inbound ends there.
    DirectionVote w1 = voteFromTrips("TESTW1", trips, coordOf);
    CHECK(w1.dir == Direction::Into);

    // C1: the inbound trip leaves C1 for W2, which is further out
    DirectionVote c1 = voteFromTrips("TESTC1", trips, coordOf);
    CHECK(c1.dir == Direction::Out);
    CHECK(c1.out == 1 && c1.into == 0);
}

static void testUnknownAndTies() {
    nlohmann::json doc = loadJson("fixtures/timetable-hand.json");
    auto trips = parseTripStops(doc);

    // A stop no trip calls at: no votes, so Unknown (the caller falls back to bearing)
    DirectionVote none = voteFromTrips("NOPE", trips, coordOf);
    CHECK(none.dir == Direction::Unknown);
    CHECK(none.into == 0 && none.out == 0);

    // A stop whose next stop has no coordinates gives no vote
    std::vector<std::vector<std::string>> oneTrip = {{"TESTW1", "UNKNOWN"}};
    CHECK(voteFromTrips("TESTW1", oneTrip, coordOf).dir == Direction::Unknown);

    // A tie is Unknown
    std::vector<std::vector<std::string>> tie = {
        {"TESTW2", "TESTC1"},   // into
        {"TESTW2", "TESTW1"},   // out
    };
    CHECK(voteFromTrips("TESTW2", tie, coordOf).dir == Direction::Unknown);

    // Stops that are too close together (inside the 50 m margin) don't vote
    std::map<std::string, LatLon> same = {{"A", {55.95, -3.20}}, {"B", {55.95, -3.20001}}};
    CoordLookup sameCoords = [&same](const std::string &a, LatLon &o) {
        auto it = same.find(a);
        if (it == same.end()) return false;
        o = it->second;
        return true;
    };
    std::vector<std::vector<std::string>> near = {{"A", "B"}};
    CHECK(voteFromTrips("A", near, sameCoords).dir == Direction::Unknown);
}

static void testStopServicesParse() {
    nlohmann::json doc = loadJson("fixtures/stopservices-hand.json");
    auto svcs = parseStopServices(doc);
    CHECK(svcs.size() == 2);
    if (svcs.size() == 2) {
        CHECK(svcs[0].publicservicename == "22");
        CHECK(svcs[0].opCode == "LOTH");
        CHECK(svcs[1].publicservicename == "N22");
    }
    CHECK(parseStopServices(nlohmann::json::object()).empty());
}

static void testRoundToTenMetres() {
    CHECK(roundToTenMetres(634.9) == 630);
    CHECK(roundToTenMetres(635.0) == 640);   // halves round up
    CHECK(roundToTenMetres(0.4) == 0);
    CHECK(roundToTenMetres(4.9) == 0);
    CHECK(roundToTenMetres(-3.0) == -1);     // negative means unknown
    CHECK(roundToTenMetres(-1.0/0.0 * 0.0) == -1);  // NaN
}

int main() {
    testRoundToTenMetres();
    testDistances();
    testVoteFromHandMadeTimetable();
    testUnknownAndTies();
    testStopServicesParse();
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("direction_test: all checks passed\n");
    return 0;
}
