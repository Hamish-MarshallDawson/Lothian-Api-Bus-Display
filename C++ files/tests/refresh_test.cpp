// refresh_test.cpp: checks the refresh backoff policy and the StopEvents parser.
// Build and run on the Pi from "C++ files/":  make test
// No network: the fetcher is scripted.

#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "../departures.h"
#include "../refresh.h"

static int failures = 0;
#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
            failures++;                                                    \
        }                                                                  \
    } while (0)

// Scripted fetcher: each call returns the next status in the script.
struct Script {
    std::vector<BoardStatus> statuses;
    size_t next = 0;
    int calls = 0;
    BoardFetcher fetcher() {
        return [this](const std::vector<std::string> &atcos, time_t now) {
            calls++;
            BoardStatus s = next < statuses.size() ? statuses[next] : statuses.back();
            next++;
            std::vector<StopBoard> out;
            for (const auto &a : atcos) {
                StopBoard b;
                b.atcocode = a;
                b.fetchedAt = now;
                b.status = s;
                if (s == BoardStatus::Ok) {
                    Departure d;
                    d.service = "22";
                    d.destination = "Ocean Terminal";
                    d.minutes = 3;
                    d.hasMinutes = true;
                    b.departures.push_back(d);
                }
                out.push_back(b);
            }
            return out;
        };
    }
};

static void testBackoffSequences() {
    // Network errors: 30, 60, 120, 240, 300, 300
    {
        Script s;
        s.statuses = {BoardStatus::NetworkError};
        BoardRefresher r(RefreshPolicy(), s.fetcher());
        r.setStops({"1"});
        time_t t = 1000;
        CHECK(r.update(t));
        const int expect[] = {30, 60, 120, 240, 300, 300};
        for (int want : expect) {
            CHECK(!r.update(t + want - 1));     // not yet due
            t = t + want;
            CHECK(r.update(t));                 // due exactly then
        }
        CHECK(r.consecutiveFailures() == 7);
    }
    // Rate limited: 120, 240, 480, 900, 900
    {
        Script s;
        s.statuses = {BoardStatus::RateLimited};
        BoardRefresher r(RefreshPolicy(), s.fetcher());
        r.setStops({"1"});
        time_t t = 0;
        CHECK(r.update(t));
        const int expect[] = {120, 240, 480, 900, 900};
        for (int want : expect) {
            CHECK(r.nextFetchAt() == t + want);
            t = r.nextFetchAt();
            CHECK(r.update(t));
        }
    }
    // Key rejected: every 600 s
    {
        Script s;
        s.statuses = {BoardStatus::KeyRejected};
        BoardRefresher r(RefreshPolicy(), s.fetcher());
        r.setStops({"1"});
        CHECK(r.update(0));
        CHECK(r.nextFetchAt() == 600);
        CHECK(r.update(600));
        CHECK(r.nextFetchAt() == 1200);
    }
}

static void testSuccessResetsAndKeepsLastGood() {
    Script s;
    s.statuses = {BoardStatus::Ok, BoardStatus::NetworkError, BoardStatus::Ok};
    BoardRefresher r(RefreshPolicy(), s.fetcher());
    r.setStops({"7000000001"});

    CHECK(r.update(100));
    CHECK(r.nextFetchAt() == 160);                    // normal interval
    CHECK(r.board(0).departures.size() == 1);
    CHECK(r.board(0).status == BoardStatus::Ok);

    CHECK(r.update(160));                             // network error
    StopBoard b = r.board(0);
    CHECK(b.status == BoardStatus::NetworkError);
    CHECK(b.departures.size() == 1);                  // last good kept
    CHECK(r.ageSeconds(0, 200) == 100);               // data from t=100

    CHECK(r.update(190));                             // next try after 30 s
    CHECK(r.board(0).status == BoardStatus::Ok);
    CHECK(r.consecutiveFailures() == 0);
    CHECK(r.nextFetchAt() == 250);
}

static void testSetStopsForcesFetch() {
    Script s;
    s.statuses = {BoardStatus::Ok};
    BoardRefresher r(RefreshPolicy(), s.fetcher());
    r.setStops({"1"});
    CHECK(r.update(0));
    CHECK(!r.update(10));
    r.setStops({"2"});                                // new stop: fetch at once
    CHECK(r.update(11));
    CHECK(r.board(0).atcocode == "2");
    CHECK(r.ageSeconds(0, 11) == 0);
    CHECK(s.calls == 2);
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

static void testFixtureParse() {
    std::ifstream f("fixtures/stopevents-typical.json");
    CHECK(f.good());
    std::stringstream ss;
    ss << f.rdbuf();
    nlohmann::json doc = nlohmann::json::parse(ss.str());

    StopBoard b = boardFromStopEvents(doc[0], localTime(2026, 10, 31, 16, 0));
    CHECK(b.status == BoardStatus::Ok);
    // 5 (due, 0), 22 (live 3), 34 (delayed, 7), 10 (timetable only, 9), N22 (12), 16 (21)
    const int want[] = {0, 3, 7, 9, 12, 21};
    CHECK(b.departures.size() == 6);
    for (size_t i = 0; i < 6 && i < b.departures.size(); ++i) {
        CHECK(b.departures[i].minutes == want[i]);
    }
    CHECK(b.departures[2].delayed);
    CHECK(!b.departures[3].live);
}

int main() {
    testBackoffSequences();
    testSuccessResetsAndKeepsLastGood();
    testSetStopsForcesFetch();
    testFixtureParse();
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("refresh_test: all checks passed\n");
    return 0;
}
