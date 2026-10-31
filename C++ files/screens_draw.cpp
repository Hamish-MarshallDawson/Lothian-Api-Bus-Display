#include "screens_draw.h"

#include <algorithm>

#include "../gfx/fonts/cond_bold_24.h"
#include "../gfx/fonts/cond_bold_28.h"
#include "../gfx/fonts/cond_regular_16.h"
#include "../gfx/fonts/cond_regular_20.h"
#include "../gfx/fonts/cond_regular_24.h"
#include "../gfx/text.h"
#include "../gfx/theme.h"

using gfx::Canvas;
using gfx::FontFace;
namespace F = gfx::fonts;
using GRect = gfx::Rect;  // screens.h's Rect is the touch-layout type; gfx::Rect draws

namespace {

GRect toG(const Rect &r) { return GRect{r.x, r.y, r.w, r.h}; }

// Baseline that puts a face's text visually centred in a box of this height.
int centreBaseline(int y, int h, const FontFace &f) { return y + h / 2 + f.size / 3; }

void background(Canvas &c) { c.clear(theme::BG); }

// Header band: optional back button on the left, title after it.
void header(Canvas &c, const std::string &title, bool back) {
    c.fillRect(toG(kHeaderBar), theme::HEADER_BG);
    c.hline(0, kScreenW - 1, kHeaderH - 1, theme::DIVIDER);
    int titleX = 16;
    if (back) {
        gfx::drawTextIn(c, F::kCondRegular20, GRect{16, 0, 64, kHeaderH},
                        centreBaseline(0, kHeaderH, F::kCondRegular20),
                        gfx::Align::Left, "< Back", theme::TEXT);
        titleX = kBackBtn.w + 8;
    }
    const int maxW = kScreenW - titleX - 16;
    gfx::drawTextIn(c, F::kCondBold24, GRect{titleX, 0, maxW, kHeaderH},
                    centreBaseline(0, kHeaderH, F::kCondBold24), gfx::Align::Left,
                    gfx::fitText(F::kCondBold24, title, maxW), theme::TEXT);
}

// Rounded button with a centred label. The rect is the same one hitTest uses.
void button(Canvas &c, const Rect &r, const std::string &label, const FontFace &f,
            gfx::Color fg, gfx::Color bg) {
    c.fillRoundRect(toG(r), theme::BADGE_R, bg);
    gfx::drawTextIn(c, f, toG(r), centreBaseline(r.y, r.h, f), gfx::Align::Center, label, fg);
}

void centredLine(Canvas &c, int y, const FontFace &f, const std::string &s, gfx::Color fg) {
    gfx::drawTextIn(c, f, GRect{0, y - f.ascent, kScreenW, f.ascent + f.descent}, y,
                    gfx::Align::Center, s, fg);
}

// Title, message and the two recovery buttons. Shared by errors and "no buses".
void messageScreen(Canvas &c, const std::string &title, gfx::Color titleColour,
                   const std::string &message, const std::string &note,
                   const std::string &footer) {
    background(c);
    gfx::drawTextIn(c, F::kCondBold28, GRect{16, 64, 288, 40}, 100, gfx::Align::Left,
                    title, titleColour);
    int y = 136;
    for (const auto &line : gfx::wrapText(F::kCondRegular20, message, 288, 4)) {
        gfx::drawText(c, F::kCondRegular20, 16, y, line, theme::TEXT);
        y += 26;
    }
    if (!note.empty()) {
        gfx::drawTextIn(c, F::kCondRegular16, GRect{16, 216, 288, 24}, 236, gfx::Align::Left,
                        note, theme::WARN);
    }
    button(c, kRetryBtn, "RETRY", F::kCondBold24, theme::TEXT, theme::HEADER_BG);
    button(c, kChooseBtn, "CHOOSE STOP", F::kCondBold24, theme::TEXT, theme::HEADER_BG);
    gfx::drawTextIn(c, F::kCondRegular16, GRect{16, 280, 288, 24}, 296, gfx::Align::Left,
                    footer, theme::TEXT_MUTED);
}

} // namespace

void drawBootScreen(Canvas &c, const std::string &status) {
    background(c);
    centredLine(c, 200, F::kCondBold28, "Bus Tracker", theme::TEXT);
    centredLine(c, 236, F::kCondRegular20, status, theme::TEXT_MUTED);
}

void drawLoadingScreen(Canvas &c, const std::string &what) {
    background(c);
    centredLine(c, 230, F::kCondRegular24, what, theme::TEXT);
}

void drawDirectionScreen(Canvas &c) {
    background(c);
    header(c, "Which way?", true);
    button(c, kDirInto, "INTO CITY", F::kCondBold28, theme::TEXT, theme::HEADER_BG);
    button(c, kDirOut, "OUT OF CITY", F::kCondBold28, theme::TEXT, theme::HEADER_BG);
    gfx::drawText(c, F::kCondRegular16, 16, 340, "Stops split by direction of travel",
                  theme::TEXT_MUTED);
}

void drawStopListScreen(Canvas &c, const std::string &title, const std::vector<std::string> &names) {
    background(c);
    header(c, title, true);
    if (names.empty()) {
        centredLine(c, 200, F::kCondRegular20, "No stops found", theme::ALERT);
        return;
    }
    const int rows = std::min(static_cast<int>(names.size()), kMaxStopRows);
    for (int i = 0; i < rows; ++i) {
        const Rect r = stopRow(i);
        c.hline(0, kScreenW - 1, r.y + r.h - 1, theme::DIVIDER);
        gfx::drawTextIn(c, F::kCondRegular20, GRect{16, r.y, 288, r.h},
                        centreBaseline(r.y, r.h, F::kCondRegular20), gfx::Align::Left,
                        gfx::fitText(F::kCondRegular20, names[i], 288), theme::TEXT);
    }
    gfx::drawTextIn(c, F::kCondRegular16, GRect{16, 456, 288, 24}, 474, gfx::Align::Left,
                    "Tap a stop to show its board", theme::TEXT_MUTED);
}

// "Last board 4 min ago", or "" when there is no board or it is 10 minutes old or more.
static std::string lastBoardNote(long staleSecs) {
    if (staleSecs < 0 || staleSecs >= 600) return "";
    if (staleSecs < 60) return "Last board " + std::to_string(staleSecs) + " s ago";
    return "Last board " + std::to_string(staleSecs / 60) + " min ago";
}

void drawErrorScreen(Canvas &c, ErrorReason r, int retryInSecs, long staleSecs) {
    std::string title = "NO WI-FI";
    std::string message = "Can't reach Lothian buses. Check the Wi-Fi.";
    switch (r) {
    case ErrorReason::KeyRejected:
        title = "KEY REJECTED";
        message = "The API key was refused. Tell the owner.";
        break;
    case ErrorReason::RateLimited:
        title = "SLOW DOWN";
        message = "Too many requests. Trying again shortly.";
        break;
    case ErrorReason::NoStops:
        title = "NO STOPS";
        message = "No stops found near the home address.";
        break;
    case ErrorReason::NoNetwork:
    case ErrorReason::None:
        break;
    }
    const std::string footer = retryInSecs > 0 ? "Retrying in " + std::to_string(retryInSecs) + " s" : "";
    messageScreen(c, title, theme::ALERT, message, lastBoardNote(staleSecs), footer);
}

void drawEmptyScreen(Canvas &c, const std::string &stopName) {
    messageScreen(c, "NO BUSES", theme::WARN, "No buses in the next hour.", "",
                  gfx::fitText(F::kCondRegular16, stopName, 288));
}

void drawAsleepScreen(Canvas &c) {
    c.clear(0);
}
