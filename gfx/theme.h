// Shared look: colours, spacing and staleness thresholds for the whole app.
// Values follow .agents/DESIGN.md; each colour is RGB565 (24-bit source in the comment).
#pragma once
#include "canvas.h"

namespace theme {

using gfx::Color;

constexpr Color BG            = 0x0862;  // #0B0F14 screen background
constexpr Color HEADER_BG     = 0x10C4;  // #141B24 header band
constexpr Color TEXT          = 0xF7BF;  // #F2F5F8 primary text
constexpr Color TEXT_MUTED    = 0x8CB4;  // #8A94A3 secondary text, scheduled numbers
constexpr Color DIVIDER       = 0x2188;  // #243040 hairlines
constexpr Color LIVE          = 0x3EF0;  // #3DDC84 DUE, live status
constexpr Color WARN          = 0xFD84;  // #FFB020 LATE, stale status, notice strip
constexpr Color ALERT         = 0xFA69;  // #FF4D4D CANC, no data
constexpr Color NOTICE_BG     = WARN;
constexpr Color NOTICE_FG     = BG;
constexpr Color BADGE_UNKNOWN = 0x5B0E;  // #5A6370 route not in busLineColours
constexpr Color BADGE_TEXT_DARK  = 0x0862;  // #0B0F14
constexpr Color BADGE_TEXT_LIGHT = 0xFFFF;  // #FFFFFF

// Spacing, px
constexpr int SCREEN_W = 320;
constexpr int SCREEN_H = 480;
constexpr int MARGIN   = 12;
constexpr int HEADER_H = 64;
constexpr int NOTICE_H = 24;
constexpr int ROW_H    = 72;
constexpr int FOOTER_Y = 432;
constexpr int BADGE_W  = 60;
constexpr int BADGE_H  = 44;
constexpr int BADGE_R  = 8;

// Data age thresholds, seconds
constexpr int STALE_WARN_S = 90;
constexpr int STALE_DEAD_S = 300;

}  // namespace theme
