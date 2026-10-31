#include "screens_draw.h"

#include <algorithm>
#include <cstdio>

#include "../gfx/fonts/cond_bold_12.h"
#include "../gfx/fonts/cond_bold_24.h"
#include "../gfx/fonts/cond_bold_28.h"
#include "../gfx/fonts/cond_regular_12.h"
#include "../gfx/fonts/cond_regular_14.h"
#include "../gfx/fonts/cond_regular_16.h"
#include "../gfx/fonts/cond_regular_20.h"
#include "../gfx/fonts/cond_regular_24.h"
#include "../gfx/text.h"

using gfx::Canvas;
using gfx::Color;
using gfx::FontFace;
namespace F = gfx::fonts;
using GRect = gfx::Rect;  // screens.h's Rect is the touch-layout type; gfx::Rect draws

// Lothian look, from .agents/LOTHIAN-STYLE.md. Moves to theme.h when lbd-ui-board's
// STYLE commit lands, with the same names.
namespace style {
constexpr Color BG          = gfx::rgb(0xFF, 0xFF, 0xFF);
constexpr Color TEXT        = gfx::rgb(0x1E, 0x27, 0x2C);
constexpr Color TEXT_MUTED  = gfx::rgb(0x6B, 0x77, 0x80);
constexpr Color DIVIDER     = gfx::rgb(0xE4, 0xE9, 0xEB);
constexpr Color ACCENT      = gfx::rgb(0x8C, 0x17, 0x13);  // Lothian maroon
constexpr Color ACCENT_SOFT = gfx::rgb(0xF6, 0xE8, 0xE7);
constexpr Color WARN        = gfx::rgb(0xB2, 0x6A, 0x00);
constexpr Color DANGER      = gfx::rgb(0xC6, 0x28, 0x28);
constexpr Color WHITE       = gfx::rgb(0xFF, 0xFF, 0xFF);
}  // namespace style

namespace {

GRect toG(const Rect &r) { return GRect{r.x, r.y, r.w, r.h}; }

// Baseline that puts a face's text visually centred in a box of this height.
int centreBaseline(int y, int h, const FontFace &f) { return y + h / 2 + f.size / 3; }

void background(Canvas &c) { c.clear(style::BG); }

// Status bar at the top: clock on the left, an optional status on the right.
void statusBar(Canvas &c, time_t now, const std::string &right, Color rightColour) {
    std::tm *t = std::localtime(&now);
    char buf[8];
    std::snprintf(buf, sizeof buf, "%02d:%02d", t->tm_hour, t->tm_min);
    gfx::drawTextIn(c, F::kCondBold12, GRect{12, 0, 120, kStatusBarH},
                    centreBaseline(0, kStatusBarH, F::kCondBold12), gfx::Align::Left, buf, style::TEXT);
    if (!right.empty()) {
        gfx::drawTextIn(c, F::kCondRegular12, GRect{160, 0, 148, kStatusBarH},
                        centreBaseline(0, kStatusBarH, F::kCondRegular12), gfx::Align::Right,
                        right, rightColour);
    }
    c.hline(0, kScreenW - 1, kStatusBarH - 1, style::DIVIDER);
}

// Screen header: maroon "<" chevron (the back tap zone) and a bold title.
void header(Canvas &c, const std::string &title, bool back) {
    int titleX = 12;
    if (back) {
        gfx::drawTextIn(c, F::kCondBold24, GRect{12, kHeaderBar.y, 40, kHeaderBar.h},
                        centreBaseline(kHeaderBar.y, kHeaderBar.h, F::kCondBold24),
                        gfx::Align::Left, "<", style::ACCENT);
        titleX = 44;
    }
    const int maxW = kScreenW - titleX - 12;
    gfx::drawTextIn(c, F::kCondBold24, GRect{titleX, kHeaderBar.y, maxW, kHeaderBar.h},
                    centreBaseline(kHeaderBar.y, kHeaderBar.h, F::kCondBold24), gfx::Align::Left,
                    gfx::fitText(F::kCondBold24, title, maxW), style::TEXT);
    c.hline(0, kScreenW - 1, kHeaderBar.y + kHeaderBar.h - 1, style::DIVIDER);
}

// Filled button: maroon with white label.
void filledButton(Canvas &c, const Rect &r, const std::string &label, const FontFace &f) {
    c.fillRoundRect(toG(r), 4, style::ACCENT);
    gfx::drawTextIn(c, f, toG(r), centreBaseline(r.y, r.h, f), gfx::Align::Center, label, style::WHITE);
}

// Outlined button: white with a 1 px maroon border and maroon label.
void outlinedButton(Canvas &c, const Rect &r, const std::string &label, const FontFace &f) {
    c.fillRoundRect(toG(r), 4, style::ACCENT);
    c.fillRoundRect(GRect{r.x + 1, r.y + 1, r.w - 2, r.h - 2}, 3, style::BG);
    gfx::drawTextIn(c, f, toG(r), centreBaseline(r.y, r.h, f), gfx::Align::Center, label, style::ACCENT);
}

void centredLine(Canvas &c, int y, const FontFace &f, const std::string &s, Color fg) {
    gfx::drawTextIn(c, f, GRect{0, y - f.ascent, kScreenW, f.ascent + f.descent}, y,
                    gfx::Align::Center, s, fg);
}

// Maroon circle with a white glyph, centred at the top of an error or empty screen.
void iconCircle(Canvas &c, int centreY, const std::string &glyph) {
    GRect r{kScreenW / 2 - 28, centreY - 28, 56, 56};
    c.fillRoundRect(r, 28, style::ACCENT);
    gfx::drawTextIn(c, F::kCondBold28, r, centreBaseline(r.y, r.h, F::kCondBold28),
                    gfx::Align::Center, glyph, style::WHITE);
}

// "Last board 4 min ago", or "" when there is no board or it is 10 minutes old or more.
std::string lastBoardNote(long staleSecs) {
    if (staleSecs < 0 || staleSecs >= 600) return "";
    if (staleSecs < 60) return "Last board " + std::to_string(staleSecs) + " s ago";
    return "Last board " + std::to_string(staleSecs / 60) + " min ago";
}

// Icon, title, message and the two recovery buttons. Shared by errors and "no buses".
void messageScreen(Canvas &c, time_t now, const std::string &glyph, const std::string &title,
                   Color titleColour, const std::string &message, const std::string &note,
                   const std::string &statusRight, Color statusColour, const std::string &footer) {
    background(c);
    statusBar(c, now, statusRight, statusColour);
    iconCircle(c, 110, glyph);
    centredLine(c, 170, F::kCondBold24, title, titleColour);
    int y = 196;
    for (const auto &line : gfx::wrapText(F::kCondRegular16, message, 288, 3)) {
        centredLine(c, y, F::kCondRegular16, line, style::TEXT_MUTED);
        y += 20;
    }
    if (!note.empty()) centredLine(c, 256, F::kCondRegular14, note, style::WARN);
    if (!footer.empty()) centredLine(c, 296, F::kCondRegular14, footer, style::TEXT_MUTED);
    filledButton(c, kRetryBtn, "RETRY", F::kCondBold24);
    outlinedButton(c, kChooseBtn, "CHOOSE STOP", F::kCondBold24);
}

} // namespace

void drawBootScreen(Canvas &c, const std::string &status) {
    c.clear(style::ACCENT);
    centredLine(c, 200, F::kCondBold28, "Bus Tracker", style::WHITE);
    // "Finding stops..." at 70% white on maroon
    centredLine(c, 236, F::kCondRegular20, status, gfx::mix(style::ACCENT, style::WHITE, 178));
}

void drawLoadingScreen(Canvas &c, const std::string &what, time_t now) {
    background(c);
    statusBar(c, now, "", style::TEXT);
    centredLine(c, 230, F::kCondRegular20, what, style::TEXT_MUTED);
}

void drawStopListScreen(Canvas &c, const std::string &title, const std::vector<std::string> &names,
                        const std::vector<std::string> &details, time_t now) {
    background(c);
    statusBar(c, now, "", style::TEXT);
    header(c, title, true);
    if (names.empty()) {
        centredLine(c, 200, F::kCondRegular20, "No stops found", style::DANGER);
        return;
    }
    const int rows = std::min(static_cast<int>(names.size()), kMaxStopRows);
    for (int i = 0; i < rows; ++i) {
        const Rect r = stopRow(i);
        // stop icon: maroon tile with a white pole, as on the app's stop cards
        c.fillRoundRect(GRect{12, r.y + 16, 24, 24}, 4, style::ACCENT);
        c.fillRect(GRect{22, r.y + 21, 4, 14}, style::WHITE);
        gfx::drawText(c, F::kCondRegular16, 48, r.y + 25,
                      gfx::fitText(F::kCondRegular16, names[i], 232), style::TEXT);
        if (i < static_cast<int>(details.size()) && !details[i].empty()) {
            gfx::drawText(c, F::kCondRegular14, 48, r.y + 43, details[i], style::TEXT_MUTED);
        }
        gfx::drawText(c, F::kCondRegular20, 296, r.y + 33, ">", style::TEXT_MUTED);
        c.hline(12, kScreenW - 13, r.y + r.h - 1, style::DIVIDER);
    }
}

void drawErrorScreen(Canvas &c, ErrorReason r, int retryInSecs, long staleSecs, time_t now) {
    std::string title = "NO WI-FI";
    std::string message = "Can't reach Lothian buses. Check the Wi-Fi.";
    Color titleColour = style::DANGER;
    std::string statusRight = "Offline";
    switch (r) {
    case ErrorReason::KeyRejected:
        title = "KEY REJECTED";
        message = "The API key was refused. Tell the owner.";
        statusRight = "";
        break;
    case ErrorReason::RateLimited:
        title = "SLOW DOWN";
        message = "Too many requests. Trying again shortly.";
        statusRight = "";
        break;
    case ErrorReason::NoStops:
        title = "NO STOPS";
        message = "No stops found near the home address.";
        statusRight = "";
        break;
    case ErrorReason::NoNetwork:
    case ErrorReason::None:
        break;
    }
    const std::string footer = retryInSecs > 0 ? "Retrying in " + std::to_string(retryInSecs) + " s" : "";
    messageScreen(c, now, "!", title, titleColour, message, lastBoardNote(staleSecs),
                  statusRight, style::DANGER, footer);
}

void drawEmptyScreen(Canvas &c, const std::string &stopName, time_t now) {
    messageScreen(c, now, "i", "NO BUSES", style::TEXT, "No buses in the next hour.", "",
                  "", style::TEXT, gfx::fitText(F::kCondRegular14, stopName, 288));
}

void drawAsleepScreen(Canvas &c) {
    c.clear(0);
}
