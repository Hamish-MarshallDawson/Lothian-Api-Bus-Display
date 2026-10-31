#include "screens_draw.h"

#include <algorithm>
#include <cstdio>

#include "../gfx/fonts/faces.h"
#include "../gfx/text.h"
#include "../gfx/theme.h"
#include "../gfx/widgets.h"

using gfx::Canvas;
using gfx::Color;
using gfx::FontFace;
namespace F = gfx::fonts;
using GRect = gfx::Rect;  // screens.h's Rect is the touch-layout type; gfx::Rect draws

namespace {

GRect toG(const Rect &r) { return GRect{r.x, r.y, r.w, r.h}; }

// Baseline that puts a face's text visually centred in a box of this height.
int centreBaseline(int y, int h, const FontFace &f) { return y + h / 2 + f.size / 3; }

void background(Canvas &c) { c.clear(theme::BG); }

// "16:02" for the status bar clock.
std::string clockText(time_t now) {
    std::tm *t = std::localtime(&now);
    char buf[8];
    std::snprintf(buf, sizeof buf, "%02d:%02d", t->tm_hour, t->tm_min);
    return buf;
}

// Status bar with a clock; state is shown on the right when given.
void statusBar(Canvas &c, time_t now, const std::string &state, Color ink) {
    gfx::drawStatusBar(c, gfx::StatusBarText{clockText(now), state, ink, false});
}

void centredLine(Canvas &c, int y, const FontFace &f, const std::string &s, Color fg) {
    gfx::drawTextIn(c, f, GRect{0, y - f.ascent, kScreenW, f.ascent + f.descent}, y,
                    gfx::Align::Center, s, fg);
}

// Maroon circle with a white glyph, centred at the top of an error or empty screen.
void iconCircle(Canvas &c, int centreY, const std::string &glyph) {
    GRect r{kScreenW / 2 - 28, centreY - 28, 56, 56};
    c.fillRoundRect(r, 28, theme::BUTTON_FILL);
    gfx::drawTextIn(c, F::kBold26, r, centreBaseline(r.y, r.h, F::kBold26),
                    gfx::Align::Center, glyph, theme::BUTTON_TEXT);
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
                   const std::string &state, Color stateInk, const std::string &footer) {
    background(c);
    statusBar(c, now, state, stateInk);
    iconCircle(c, 110, glyph);
    centredLine(c, 170, F::kBold22, title, titleColour);
    int y = 196;
    for (const auto &line : gfx::wrapText(F::kRegular15, message, 288, 3)) {
        centredLine(c, y, F::kRegular15, line, theme::TEXT_MUTED);
        y += 20;
    }
    if (!note.empty()) centredLine(c, 256, F::kSemiBold14, note, theme::WARN);
    if (!footer.empty()) centredLine(c, 296, F::kRegular13, footer, theme::TEXT_MUTED);
    gfx::drawButton(c, toG(kRetryBtn), "RETRY", F::kBold20, true);
    gfx::drawButton(c, toG(kChooseBtn), "CHOOSE STOP", F::kBold20, false);
}

} // namespace

void drawBootScreen(Canvas &c, const std::string &status) {
    c.clear(theme::BUTTON_FILL);   // maroon splash
    centredLine(c, 200, F::kBold26, "Bus Tracker", theme::BUTTON_TEXT);
    // "Finding stops..." at 70% white on maroon
    centredLine(c, 236, F::kRegular17, status, gfx::mix(theme::BUTTON_FILL, theme::BUTTON_TEXT, 178));
}

void drawLoadingScreen(Canvas &c, const std::string &what, time_t now) {
    background(c);
    statusBar(c, now, "", theme::TEXT);
    centredLine(c, 230, F::kRegular17, what, theme::TEXT_MUTED);
    // the backend's first lookup of the day can take around 12 s
    centredLine(c, 256, F::kRegular13, "This can take a few seconds", theme::TEXT_MUTED);
}

void drawStopListScreen(Canvas &c, const std::string &title, const std::vector<std::string> &names,
                        const std::vector<std::string> &details, time_t now) {
    background(c);
    statusBar(c, now, "", theme::TEXT);
    gfx::drawScreenHeader(c, title, true);
    c.hline(0, kScreenW - 1, kHeaderBar.y + kHeaderBar.h - 1, theme::DIVIDER);
    if (names.empty()) {
        centredLine(c, 200, F::kRegular17, "No stops found", theme::ALERT);
        return;
    }
    const int rows = std::min(static_cast<int>(names.size()), kMaxStopRows);
    for (int i = 0; i < rows; ++i) {
        const Rect r = stopRow(i);
        gfx::drawStopIcon(c, 12, r.y + (r.h - 22) / 2);
        gfx::drawTextIn(c, F::kSemiBold16, GRect{40, r.y, 240, r.h / 2 + 4},
                        r.y + 24, gfx::Align::Left,
                        gfx::fitText(F::kSemiBold16, names[i], 240), theme::TEXT);
        if (i < static_cast<int>(details.size()) && !details[i].empty()) {
            gfx::drawText(c, F::kRegular13, 40, r.y + 42, details[i], theme::TEXT_MUTED);
        }
        gfx::drawChevron(c, 304, r.y + r.h / 2, theme::TEXT_MUTED, true);
        c.hline(12, kScreenW - 13, r.y + r.h - 1, theme::DIVIDER);
    }
}

void drawErrorScreen(Canvas &c, ErrorReason r, int retryInSecs, long staleSecs, time_t now) {
    std::string title = "NO WI-FI";
    std::string message = "Can't reach Lothian buses. Check the Wi-Fi.";
    std::string state = "Offline";
    switch (r) {
    case ErrorReason::KeyRejected:
        title = "KEY REJECTED";
        message = "The API key was refused. Tell the owner.";
        state = "";
        break;
    case ErrorReason::RateLimited:
        title = "SLOW DOWN";
        message = "Too many requests. Trying again shortly.";
        state = "";
        break;
    case ErrorReason::NoStops:
        title = "NO STOPS";
        message = "No stops found near the home address.";
        state = "";
        break;
    case ErrorReason::NoNetwork:
    case ErrorReason::None:
        break;
    }
    const std::string footer = retryInSecs > 0 ? "Retrying in " + std::to_string(retryInSecs) + " s" : "";
    messageScreen(c, now, "!", title, theme::ALERT, message, lastBoardNote(staleSecs),
                  state, theme::ALERT, footer);
}

void drawEmptyScreen(Canvas &c, const std::string &stopName, time_t now) {
    messageScreen(c, now, "i", "NO BUSES", theme::TEXT, "No buses in the next hour.", "",
                  "", theme::TEXT, gfx::fitText(F::kRegular13, stopName, 288));
}

void drawAsleepScreen(Canvas &c) {
    c.clear(0);
}
