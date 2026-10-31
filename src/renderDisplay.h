#pragma once

#include <ctime>

#include "departures.h"
#include "hidden_routes.h"  // RouteSet, the hidden routes
#include "screens.h"  // StopDirection
#include "../gfx/canvas.h"

// Panel size. st7796.h defines the same values in the display build.
#ifndef ST7796_WIDTH
#define ST7796_WIDTH  320
#define ST7796_HEIGHT 480
#endif

// Push the frame to the LCD. The frame is drawn as the person sees it; the panel
// has its x axis reversed, so each row is flipped and byte-swapped here, and
// nowhere else. Only compiled into the display build (BUSTRACKER_HW); the
// snapshot-only build has a no-op stub.
void flushToLcd(const gfx::Canvas &frame);

// Draw the main departures board into the frame. Draws only; the caller decides
// when to push it to the LCD (flushToLcd) or write a PNG. The clock and the data
// age come from board.fetchedAt, and `now` is the reference for "how old is it",
// so snapshots are repeatable when --now is given. `dir` selects the direction
// button that is filled.
void drawMainScreen(gfx::Canvas &frame, const StopBoard &board, time_t now, StopDirection dir,
                    const RouteSet &hidden);   // hidden: routes left out of the rows (chips stay)
