#pragma once

extern "C" {
#include "../C/lib/GUI/GUI_Paint.h"
}

#include <string>
#include <vector>

// Panel size. st7796.h defines the same values in the display build.
#ifndef ST7796_WIDTH
#define ST7796_WIDTH  320
#define ST7796_HEIGHT 480
#endif

#include "busStopInfo.h"
#include "departures.h"
#include "openmappull.h"
#include "../gfx/canvas.h"

// Shared UI state and display API
enum class DirectionChoice { None, IntoCity, OutOfCity };

extern DirectionChoice g_dirChoice;
extern GroupedStops g_groupedStops;
extern std::vector<BusStopInfo> g_dropdownStops;
extern bool g_showDirectionModal;
extern std::string g_selectedStopID;

// Push the frame to the LCD. The frame is drawn as the person sees it; the panel
// has its x axis reversed, so each row is flipped and byte-swapped here, and
// nowhere else. Only compiled into the display build (BUSTRACKER_HW); the
// snapshot-only build has a no-op stub.
void flushToLcd(const gfx::Canvas &frame);
void drawDirectionChoiceModal();

// Draw the main departures screen into the Paint buffer. Draws only; the
// caller decides when to push it to the LCD (flushToLcd) or write a PNG.
// The clock comes from board.fetchedAt, so snapshots are repeatable.
void drawMainScreen(UWORD *image, const StopBoard &board);
