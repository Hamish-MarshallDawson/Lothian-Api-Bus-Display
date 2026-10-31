// picker_live.cpp: dev-only check of the picker's data path against the live API.
// Built by "make picker-live". Not part of "make test".
//
// For every stop the picker offers (both groups), fetches its board two ways
// (StopEvents, and MultipleStopEvents with one code) and prints only: group,
// index, status, statusDetail, departure count. No stop names, codes or
// coordinates are printed, because the stops are near home.

#include <cstdio>
#include <curl/curl.h>
#include <ctime>
#include <string>
#include <vector>

#include "../departures.h"
#include "../openmappull.h"
#include "../settings.h"

static void report(const char *group, size_t index, const char *how, const StopBoard &b) {
    std::printf("%s #%zu %-8s status=%-14s detail=%s departures=%zu\n", group, index, how,
                boardStatusName(b.status), b.statusDetail.c_str(), b.departures.size());
}

int main() {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    if (CONFIG_ADDRESS().empty()) {
        std::printf("BUS_ADDRESS is not set\n");
        return 2;
    }

    GroupedStops g;
    if (!getNearestStopsGrouped(CONFIG_ADDRESS(), CONFIG_NUM_STOPS(), g)) {
        std::printf("getNearestStopsGrouped failed\n");
        curl_global_cleanup();
        return 1;
    }
    std::printf("picker: into=%zu out=%zu\n", g.intoCity.size(), g.outOfCity.size());

    const time_t now = time(nullptr);
    int mismatches = 0;
    auto check = [&](const char *group, const std::vector<BusStopInfo> &stops) {
        for (size_t i = 0; i < stops.size(); ++i) {
            const std::string &atco = stops[i].stopID;
            StopBoard single = fetchStopBoard(atco, now);
            std::vector<StopBoard> multi = fetchStopBoards({atco}, now);
            report(group, i, "single", single);
            if (multi.size() == 1) report(group, i, "multi", multi[0]);
            // The two paths must agree on whether the stop has departures
            if (multi.size() == 1 && single.departures.empty() != multi[0].departures.empty()) {
                std::printf("  MISMATCH: single=%zu multi=%zu\n", single.departures.size(),
                            multi[0].departures.size());
                mismatches++;
            }
        }
    };
    check("into", g.intoCity);
    check("out", g.outOfCity);
    std::printf("mismatches=%d\n", mismatches);

    curl_global_cleanup();
    return mismatches ? 3 : 0;
}
