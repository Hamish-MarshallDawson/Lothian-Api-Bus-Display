#ifndef SCREENS_DRAW_H
#define SCREENS_DRAW_H

// Drawing for the screens that are not the departures board. Each function
// clears the canvas and paints the whole screen. Touch areas come from
// screens.h, so the buttons drawn here sit exactly on their hit areas.

#include <string>
#include <vector>

#include "../gfx/canvas.h"
#include "screens.h"

void drawBootScreen(gfx::Canvas &c, const std::string &status);
void drawLoadingScreen(gfx::Canvas &c, const std::string &what);
void drawDirectionScreen(gfx::Canvas &c);
// names: the stop list for the chosen direction. Shows up to kMaxStopRows.
void drawStopListScreen(gfx::Canvas &c, const std::string &title, const std::vector<std::string> &names);
void drawErrorScreen(gfx::Canvas &c, ErrorReason r, int retryInSecs);
void drawEmptyScreen(gfx::Canvas &c, const std::string &stopName);
void drawAsleepScreen(gfx::Canvas &c);

#endif // SCREENS_DRAW_H
