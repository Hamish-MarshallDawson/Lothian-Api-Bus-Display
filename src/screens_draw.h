#ifndef SCREENS_DRAW_H
#define SCREENS_DRAW_H

// Drawing for the screens that are not the departures board, in the Lothian app's
// look (.agents/LOTHIAN-STYLE.md). Each function clears the canvas and paints the
// whole screen. Touch areas come from screens.h, so the buttons drawn here sit
// exactly on their hit areas.

#include <ctime>
#include <string>
#include <vector>

#include "../gfx/canvas.h"
#include "screens.h"

// Maroon full-screen splash with the app name.
void drawBootScreen(gfx::Canvas &c, const std::string &status);
void drawLoadingScreen(gfx::Canvas &c, const std::string &what, time_t now);
// names: the stops for the direction, with a second line each (distance, direction).
// Shows up to kMaxStopRows.
void drawStopListScreen(gfx::Canvas &c, const std::string &title,
                        const std::vector<std::string> &names,
                        const std::vector<std::string> &details, time_t now);
// staleSecs: age of the last good board, or -1. Shown under the message when under 10 minutes.
void drawErrorScreen(gfx::Canvas &c, ErrorReason r, int retryInSecs, long staleSecs, time_t now);
void drawEmptyScreen(gfx::Canvas &c, const std::string &stopName, time_t now);
void drawAsleepScreen(gfx::Canvas &c);
// The bus detail for one departure (null if that row has gone). A placeholder until
// lbd-ui-board draws the map and the live position.
void drawBusDetailScreen(gfx::Canvas &c, const Departure *d, time_t now);

#endif // SCREENS_DRAW_H
