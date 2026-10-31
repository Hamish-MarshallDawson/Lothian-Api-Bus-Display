// track_live.cpp: dev-only check of bus tracking and tiles against the live API.
// Built by "make track-live", not part of "make test". Usage:
//   ./tests/track_live <city-centre atcocode>
// Prints numbers and states only: no stop names, codes or coordinates.

#include <chrono>
#include <cmath>
#include <cstdio>
#include <curl/curl.h>
#include <ctime>
#include <string>
#include <vector>

#include "../departures.h"
#include "../openmappull.h"
#include "../settings.h"
#include "../tiles.h"
#include "../tracking.h"

static const char *stateName(TrackState s) {
    switch (s) {
        case TrackState::NotStarted: return "not-started";
        case TrackState::Live: return "live";
        case TrackState::Stale: return "stale";
        case TrackState::Finished: return "finished";
        case TrackState::Unavailable: return "unavailable";
    }
    return "?";
}

static long long millis(std::chrono::steady_clock::time_point a) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - a).count();
}

int main(int argc, char **argv) {
    if (argc < 2) {
        std::printf("usage: track_live <atcocode>\n");
        return 2;
    }
    curl_global_init(CURL_GLOBAL_DEFAULT);
    const std::string stop = argv[1];
    const time_t now = time(nullptr);

    BusStopInfo my;
    my.stopID = stop;
    std::string name;
    LatLon at{0.0, 0.0};
    if (!stopByCode(stop, name, at)) {
        std::printf("stop not in the stop list\n");
        curl_global_cleanup();
        return 1;
    }
    my.stopName = name;   // used only for the match; never printed

    StopBoard board = fetchStopBoard(stop, now);
    std::printf("board status=%s departures=%zu\n", boardStatusName(board.status), board.departures.size());

    int shown = 0;
    for (const auto &dep : board.departures) {
        if (dep.journeyId.empty() || shown >= 3) continue;
        ++shown;

        std::vector<TrackStop> trip;
        const bool found = findTripForDeparture(dep, my, trip);
        std::printf("dep %d: service=%s due=%d sched=%s live=%d trip_found=%d trip_stops=%zu\n", shown,
                    dep.service.c_str(), dep.minutes, dep.scheduled.c_str(), dep.live, found, trip.size());
        if (!found) continue;

        VehicleFeed feed;
        feed.watch(dep.journeyId);
        const auto t0 = std::chrono::steady_clock::now();
        feed.update(now);   // one list read to find the vehicle
        std::printf("  lookup: %lld ms, vehicle_found=%zu\n", millis(t0), feed.vehicles().size());

        BusTrack t = buildTrack(dep, my, trip, feed, now);
        std::printf("  state=%s my_index=%d next_index=%d stops_away=%d late_min=%d age_s=%ld due_min=%d\n",
                    stateName(t.state), t.myStopIndex, t.nextStopIndex, t.stopsAway, t.lateMin,
                    t.lastUpdateAgeS, t.dueMin);

        if (t.state == TrackState::Live || t.state == TrackState::Stale) {
            // One small VehicleStatus poll, as the detail screen does every 20 s
            std::printf("  polled again immediately (should wait): %d\n", (int)feed.update(now + 1));
            std::printf("  poll after 21 s: %d\n", (int)feed.update(now + 21));
            break;
        }
    }

    // Tiles: one tile where the stop is, fetched twice. The second call should come
    // from the cache, which shows as much faster.
    TileId tile{16, 0, 0};
    const double n = 1 << 16;
    tile.x = static_cast<int>((at.lon + 180.0) / 360.0 * n);
    const double latR = at.lat * 3.14159265358979323846 / 180.0;
    tile.y = static_cast<int>((1.0 - std::log(std::tan(latR) + 1.0 / std::cos(latR)) / 3.14159265358979323846) / 2.0 * n);
    std::string png1, png2;
    auto t1 = std::chrono::steady_clock::now();
    bool ok1 = tileBytes(tile, png1, true);
    const long long ms1 = millis(t1);
    auto t2 = std::chrono::steady_clock::now();
    bool ok2 = tileBytes(tile, png2, true);
    const long long ms2 = millis(t2);
    std::printf("tile z16: first ok=%d bytes=%zu png=%d %lld ms; second ok=%d bytes=%zu %lld ms\n",
                ok1, png1.size(), isPng(png1), ms1, ok2, png2.size(), ms2);

    curl_global_cleanup();
    return 0;
}
