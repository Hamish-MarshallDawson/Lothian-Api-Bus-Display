#include "widgets.h"

#include <cmath>
#include <cstdio>

#include "fonts/faces.h"
#include "text.h"
#include "theme.h"

namespace gfx {

namespace {

using namespace theme;
namespace F = fonts;

// Chevron stroke: two 1 px lines, drawn twice for a 2 px weight
void chevronStroke(Canvas &c, int x0, int y0, int x1, int y1, Color ink) {
    c.line(x0, y0, x1, y1, ink);
    c.line(x0 + 1, y0, x1 + 1, y1, ink);
}

}  // namespace

int centredBaseline(int y, int h, const FontFace &f) {
    // Cap height is about 0.7 em; put its middle on the box's middle.
    return y + h / 2 + (f.size * 35 + 50) / 100;
}

void drawStatusBar(Canvas &c, const StatusBarText &s) {
    c.fillRect(kStatusBar, BG);
    const int base = centredBaseline(0, STATUS_H, F::kSemiBold14);
    drawText(c, F::kSemiBold14, MARGIN, base, s.clock, TEXT);

    int right = SCREEN_W - MARGIN;
    if (!s.state.empty()) {
        const FontFace &f = F::kRegular13;
        drawTextIn(c, f, Rect{0, 0, right + 1, 0}, base, Align::Right, s.state, s.stateInk);
        right -= textWidth(f, s.state) + 6;
    }
    if (s.liveIcon) drawLiveIcon(c, right - 12, STATUS_H / 2, LIVE);
    c.hline(0, SCREEN_W - 1, STATUS_H - 1, DIVIDER);
}

void drawStopIcon(Canvas &c, int x, int y) {
    c.fillRoundRect(Rect{x, y, 18, 22}, 3, ACCENT);
    c.fillRoundRect(Rect{x + 4, y + 4, 10, 9}, 2, BUTTON_TEXT);   // sign
    c.fillRect(Rect{x + 6, y + 6, 6, 3}, ACCENT);                  // window
    c.fillRect(Rect{x + 8, y + 13, 2, 6}, BUTTON_TEXT);            // pole
}

void drawStopHeader(Canvas &c, const StopHeaderText &h) {
    const Rect zone = kStopHeader;
    c.fillRect(zone, BG);
    drawStopIcon(c, MARGIN, zone.y + 13);

    // Right side first: distance, then the chevron hint
    int right = SCREEN_W - MARGIN;
    const int cy = zone.y + zone.h / 2;
    drawChevron(c, right - 3, cy, TEXT_MUTED, true);
    right -= 14;
    // The explicit affordance: the header opens the stop list
    const FontFace &hf = F::kSemiBold13;
    drawTextIn(c, hf, Rect{0, 0, right + 1, 0}, centredBaseline(zone.y, zone.h, hf), Align::Right,
               "Change stop", ACCENT);
    right -= textWidth(hf, "Change stop") + 10;
    if (h.distanceM >= 0) {
        std::string d = h.distanceM < 1000 ? std::to_string(h.distanceM) + "m"
                                           : std::to_string(h.distanceM / 1000) + "." +
                                                 std::to_string((h.distanceM % 1000) / 100) + "km";
        const FontFace &f = F::kRegular14;
        drawTextIn(c, f, Rect{0, 0, right + 1, 0}, centredBaseline(zone.y, zone.h, f), Align::Right, d, TEXT_MUTED);
        right -= textWidth(f, d) + 10;
    }

    std::string title = h.name;
    if (!h.indicator.empty()) title += " (" + h.indicator + ")";
    const int x = MARGIN + 18 + 10;
    const FontFace &f = F::kBold20;
    drawText(c, f, x, centredBaseline(zone.y, zone.h, f), fitText(f, title, right - x), TEXT);
}

void drawScreenHeader(Canvas &c, const std::string &title, bool back) {
    const Rect zone = kStopHeader;
    c.fillRect(zone, BG);
    int x = MARGIN;
    if (back) {
        drawChevron(c, 24 - 3, zone.y + zone.h / 2, ACCENT, false);
        x = 56;
    }
    const FontFace &f = F::kBold20;
    drawText(c, f, x, centredBaseline(zone.y, zone.h, f),
             fitText(f, title, SCREEN_W - MARGIN - x), TEXT);
}

void drawRoutePill(Canvas &c, Rect r, const std::string &label, Color fill, const FontFace &f) {
    c.fillRoundRect(r, PILL_R, fill);
    drawTextIn(c, f, r, centredBaseline(r.y, r.h, f), Align::Center, label, readableOn(fill));
}

void drawButton(Canvas &c, Rect r, const std::string &label, const FontFace &f, bool filled) {
    if (filled) {
        c.fillRoundRect(r, 4, BUTTON_FILL);
        drawTextIn(c, f, r, centredBaseline(r.y, r.h, f), Align::Center, label, BUTTON_TEXT);
    } else {
        c.fillRoundRect(r, 4, BUTTON_OUTLINE);                       // 1 px border...
        c.fillRoundRect(Rect{r.x + 1, r.y + 1, r.w - 2, r.h - 2}, 3, BG);  // ...and the white inside
        drawTextIn(c, f, r, centredBaseline(r.y, r.h, f), Align::Center, label, BUTTON_OUTLINE);
    }
}

void drawLiveIcon(Canvas &c, int x, int cy, Color ink) {
    // Dot at the left, arcs to the right of it
    c.fillRoundRect(Rect{x, cy - 2, 4, 4}, 2, ink);
    for (int radius : {5, 9}) {
        for (int deg = -55; deg <= 55; ++deg) {
            double a = deg * 3.14159265358979 / 180.0;
            int px = x + 2 + static_cast<int>(std::lround(radius * std::cos(a)));
            int py = cy + static_cast<int>(std::lround(radius * std::sin(a)));
            c.blendPixel(px, py, ink, 255);
        }
    }
}

void drawChevron(Canvas &c, int x, int cy, Color ink, bool right) {
    if (right) {
        chevronStroke(c, x, cy - 5, x + 5, cy, ink);
        chevronStroke(c, x + 5, cy, x, cy + 5, ink);
    } else {
        chevronStroke(c, x + 5, cy - 5, x, cy, ink);
        chevronStroke(c, x, cy, x + 5, cy + 5, ink);
    }
}

}  // namespace gfx
