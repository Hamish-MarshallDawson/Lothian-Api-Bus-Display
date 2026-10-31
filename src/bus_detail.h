// The bus detail screen: where one departure's bus is, how late, and the stops it has
// left. Drawing only. The view is plain values; busTrack maps the tracking data onto it.
#pragma once
#include <string>
#include <vector>

#include "tracking.h"
#include "../gfx/canvas.h"
#include "../gfx/map.h"
#include "../gfx/widgets.h"

enum class DetailState { NotStarted, Live, Stale, Finished, Unavailable };

struct DetailStop {
    std::string name;
    std::string scheduled;  // "HH:MM", local
    double lat = 0, lon = 0;
};

struct BusDetailView {
    std::string service;               // route number, for the pill and the bus marker
    gfx::Color routeFill = 0;          // from busColours.h (BADGE_UNKNOWN when missing)
    std::string destination;
    DetailState state = DetailState::Unavailable;
    std::vector<DetailStop> stops;     // the trip's stops, origin to terminus
    int myStop = -1;                   // the stop the board is for
    int nextStop = -1;                 // the stop the bus is approaching
    bool hasBus = false;               // a position is known (Live or Stale)
    double busLat = 0, busLon = 0;
    long lastUpdateAgeS = -1;          // seconds since the bus last reported
    int lateMin = 0;                   // minutes behind the timetable (negative = early)
    int stopsAway = -1;                // stops between the bus and my stop
    int dueMin = -1;                   // minutes until it reaches my stop, from the board
    bool stuck = false;
    long stuckForS = 0;
    std::string originName, originTime;  // NotStarted: where and when it leaves
    std::string lateTrend;             // "late 4 -> 6 min over 3 min", from the lateness history ("" if none)
};

// Draw the whole screen: status bar, header with back, map panel, info block, next stops.
// The map uses tiles when they are on disk and the plain background when they are not.
// The map for the detail: the trip's stops, the bus and the route, fitted to the panel. busTrack
// asks for its tiles; drawBusDetail draws the same scene.
gfx::MapScene detailMapScene(const BusDetailView &v);

void drawBusDetail(gfx::Canvas &c, const BusDetailView &v, const gfx::StatusBarText &status,
                   gfx::TileStore &tiles);

// The view for a tracked bus, from the tracking data (tracking.h). routeFill is the route
// colour from busColours.h.
BusDetailView detailFromTrack(const BusTrack &t, gfx::Color routeFill);
