// Shared widgets in the Lothian app's style (LOTHIAN-STYLE.md). Used by the board
// and by every other screen, so the look lives in one place.
#pragma once
#include <string>

#include "canvas.h"
#include "text.h"

namespace gfx {

// Baseline that centres a face's cap height in a box (y, h).
int centredBaseline(int y, int h, const FontFace &f);

// Status bar, 0-23 px: clock on the left, state on the right, 1 px rule at y=23.
struct StatusBarText {
    std::string clock;     // "16:02"
    std::string state;     // "Live", "Updated 3 min ago", "Offline"
    Color stateInk;        // theme::LIVE, TEXT_MUTED or DANGER
    bool liveIcon;         // a live icon before the state text
};
void drawStatusBar(Canvas &c, const StatusBarText &s);

// Stop header band, 24-71 px (theme::kStopHeader is the tap zone): stop icon, name with
// indicator, walking distance from home (metres; < 0 hides it), and a chevron hint.
struct StopHeaderText {
    std::string name;       // "Leith Street"
    std::string indicator;  // "JB"; "" for none
    int distanceM;          // -1 = unknown
};
void drawStopHeader(Canvas &c, const StopHeaderText &h);

// Screen header band (24-71 px): optional maroon back chevron (48x48 zone at x 0), title Bold 20.
void drawScreenHeader(Canvas &c, const std::string &title, bool back);

// Maroon stop icon, 18x22 at (x, y): a bus-stop sign in white.
void drawStopIcon(Canvas &c, int x, int y);

// Route pill: rounded fill in the route colour, number centred, dark or white by contrast.
void drawRoutePill(Canvas &c, Rect r, const std::string &label, Color fill, const FontFace &f);

// Button, 4 px radius: filled (ACCENT, white text) or outlined (1 px ACCENT border, ACCENT text).
void drawButton(Canvas &c, Rect r, const std::string &label, const FontFace &f, bool filled);

// Live icon: a dot with two radio arcs, about 12 px wide, centred on cy.
void drawLiveIcon(Canvas &c, int x, int cy, Color ink);

// Chevron, 6x10, centred on (x, cy). right = true points right (">"), false points left ("<").
void drawChevron(Canvas &c, int x, int cy, Color ink, bool right = true);

// Shared main screen header used by the board until it is rewritten (see drawStopHeader).
struct HeaderText {
    std::string title;   // stop name, bold, left
    std::string right;   // clock or screen-specific value, regular, right
    std::string kicker;  // small caps line under the title, left
    std::string hint;    // small caps line, right; "" for none
};
void drawHeader(Canvas &c, const HeaderText &h);

}  // namespace gfx
