// Shared widgets: the header band used by the board and the other screens.
#pragma once
#include <string>

#include "canvas.h"

namespace gfx {

struct HeaderText {
    std::string title;   // stop name, bold, left
    std::string right;   // clock or screen-specific value, regular, right
    std::string kicker;  // small caps line under the title, left (e.g. "INTO CITY")
    std::string hint;    // small caps line, right (e.g. "CHANGE STOP >"); "" for none
};

// Full-width header band, 64 px tall at y=0 (theme::HEADER_H), with a 1 px divider
// on its bottom edge. The whole band is the touch target for "change stop" (48 px+).
void drawHeader(Canvas &c, const HeaderText &h);

}  // namespace gfx
