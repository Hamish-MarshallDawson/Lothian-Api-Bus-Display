// Shared look: the Lothian app's light palette, layout and staleness thresholds.
// Values follow .agents/LOTHIAN-STYLE.md. Colours are RGB565; the 24-bit source is in each comment.
// Route colours are not here: they come from busColours.h, exactly as published.
#pragma once
#include "canvas.h"

namespace theme {

using gfx::Color;
using gfx::rgb;
using gfx::Rect;

// Palette
constexpr Color BG            = rgb(0xFF, 0xFF, 0xFF);  // screen
constexpr Color TEXT          = rgb(0x1E, 0x27, 0x2C);  // stop name, destinations, minutes
constexpr Color TEXT_MUTED    = rgb(0x6B, 0x77, 0x80);  // labels, scheduled times, footer
constexpr Color DIVIDER       = rgb(0xE4, 0xE9, 0xEB);  // row dividers, status-bar rule
constexpr Color ACCENT        = rgb(0x8C, 0x17, 0x13);  // Lothian maroon: stop icon, buttons, live icon, Due
constexpr Color ACCENT_SOFT   = rgb(0xF6, 0xE8, 0xE7);  // pressed or selected row tint
constexpr Color ALERT_BG      = rgb(0xE4, 0xEA, 0xFF);  // alert banner
constexpr Color ALERT_FG      = rgb(0x2F, 0x54, 0xEB);  // alert icon and chevron
constexpr Color WARN          = rgb(0xB2, 0x6A, 0x00);  // delayed
constexpr Color DANGER        = rgb(0xC6, 0x28, 0x28);  // cancelled, error titles, offline

// Names the flows and board code already use
constexpr Color HEADER_BG     = BG;
constexpr Color LIVE          = ACCENT;
constexpr Color ALERT         = DANGER;
constexpr Color ERROR         = DANGER;
constexpr Color NOTICE_BG     = ALERT_BG;
constexpr Color NOTICE_FG     = TEXT;
constexpr Color BADGE_UNKNOWN = rgb(0x6B, 0x77, 0x80);  // route not in busColours.h
constexpr Color BADGE_TEXT_DARK  = TEXT;
constexpr Color BADGE_TEXT_LIGHT = rgb(0xFF, 0xFF, 0xFF);
constexpr Color BUTTON_FILL      = ACCENT;               // selected or primary button
constexpr Color BUTTON_TEXT      = rgb(0xFF, 0xFF, 0xFF);
constexpr Color BUTTON_OUTLINE   = ACCENT;               // unselected button border and text
constexpr Color ROW_SELECTED     = ACCENT_SOFT;
constexpr Color ROW_SELECTED_TEXT = TEXT;

// Spacing, px
constexpr int SCREEN_W = 320;
constexpr int SCREEN_H = 480;
constexpr int MARGIN   = 12;
constexpr int STATUS_H = 24;
constexpr int HEADER_H = 72;  // status bar plus stop header: the top chrome of the board
constexpr int NOTICE_H = 28;  // alert banner height
constexpr int ROW_H    = 52;  // five rows fit below the alert banner

// Route pills (in rows) and chips (under the header)
constexpr int PILL_W = 44, PILL_H = 22, PILL_R = 3;
constexpr int CHIP_MIN_W = 40, CHIP_H = 32, CHIP_BAND_H = 40;  // chip band is the touch zone

// Legacy board metrics, used by the old board until it is rewritten
constexpr int FOOTER_Y = 432;
constexpr int BADGE_W  = 60;
constexpr int BADGE_H  = 44;
constexpr int BADGE_R  = 8;

// Named layout rects (LOTHIAN-STYLE.md). Tap zones are at least 48 px tall.
constexpr Rect kStatusBar    {0, 0, SCREEN_W, STATUS_H};        // clock, live state
constexpr Rect kStopHeader   {0, 24, SCREEN_W, 48};             // tap zone: opens the stop list
constexpr Rect kDirIntoZone  {0, 72, 160, 48};                  // tap zone: into city
constexpr Rect kDirOutZone   {160, 72, 160, 48};                // tap zone: out of city
constexpr Rect kDirInto      {12, 76, 144, 40};                 // drawn button: into city
constexpr Rect kDirOut       {165, 76, 144, 40};                // drawn button: out of city
constexpr int  kChipsTop     = 120;                             // chip band 120-159 (touch zones)
constexpr int  kSectionTop   = 160;                             // "Upcoming departures" band, 160-183
constexpr int  kAlertTop     = 184;                             // alert banner, 28 px when shown
constexpr int  kRowsTop      = 184;                             // first row without an alert banner
constexpr int  kRowsTopAlert = 212;                             // first row with an alert banner

// Touch zone and draw area of row i (0-based) when the rows start at rowsTop
constexpr Rect rowRect(int index, int rowsTop) { return Rect{0, rowsTop + index * ROW_H, SCREEN_W, ROW_H}; }

// Data age thresholds, seconds
constexpr int STALE_WARN_S = 90;
constexpr int STALE_DEAD_S = 300;

}  // namespace theme
