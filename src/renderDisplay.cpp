/*
 * The main departures board (in the Lothian app's style, see .agents/LOTHIAN-STYLE.md),
 * and the flush of any frame to the LCD. Everything draws into a gfx::Canvas.
 */

extern "C" {
#ifdef BUSTRACKER_HW
    #include "../third_party/waveshare/Config/DEV_Config.h"
    #include "../third_party/waveshare/lcd/st7796.h"
    void st7796_set_windows(uint16_t x_start, uint16_t y_start, uint16_t x_end, uint16_t y_end);
#endif
}

#include <algorithm>
#include <set>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

#include "busColours.h"     // route colours, exactly as Lothian publishes them
#include "departures.h"
#include "renderDisplay.h"
#include "../gfx/canvas.h"
#include "../gfx/fonts/faces.h"
#include "../gfx/text.h"
#include "../gfx/theme.h"
#include "../gfx/widgets.h"

// ---------------------------------------------------------------------------
// Flush to the LCD

#ifdef BUSTRACKER_HW
// Push the frame to the LCD, one row at a time. The panel's x axis runs the other
// way to the frame, so column x of the row goes to panel column W-1-x. Each pixel
// goes out high byte first, which is what the ST7796 expects.
void flushToLcd(const gfx::Canvas &frame) {
    st7796_set_windows(0, 0, ST7796_WIDTH - 1, ST7796_HEIGHT - 1);
    LCD_DC_1;
    static uint8_t line[ST7796_WIDTH * 2];
    for (int y = 0; y < ST7796_HEIGHT; y++) {
        const gfx::Color *row = frame.data() + y * ST7796_WIDTH;
        for (int x = 0; x < ST7796_WIDTH; x++) {
            gfx::Color v = row[ST7796_WIDTH - 1 - x];
            line[2 * x]     = static_cast<uint8_t>(v >> 8);
            line[2 * x + 1] = static_cast<uint8_t>(v & 0xFF);
        }
        DEV_SPI_Write_nByte(line, ST7796_WIDTH * 2);
    }
}
#else
// Snapshot-only build has no LCD. busTrack refuses to run the display loop here.
void flushToLcd(const gfx::Canvas &) {}
#endif

// ---------------------------------------------------------------------------
// Board

namespace {

using gfx::Color;
using gfx::FontFace;
using gfx::Rect;
using namespace theme;
namespace F = gfx::fonts;

// "#RRGGBB" to RGB565 with the same packing as gfx::rgb. Bad input gives the unknown-route grey.
Color hexToColor(const std::string &hex) {
    std::string h = hex;
    if (!h.empty() && h[0] == '#') h.erase(0, 1);
    unsigned r = 0, g = 0, b = 0;
    if (h.size() != 6 || std::sscanf(h.c_str(), "%2x%2x%2x", &r, &g, &b) != 3) return BADGE_UNKNOWN;
    return gfx::rgb(static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b));
}

// Route fill from busColours.h. Only routes missing from the map use the grey.
Color routeColour(const std::string &service) {
    auto it = busLineColours.find(service);
    return it != busLineColours.end() ? hexToColor(it->second) : BADGE_UNKNOWN;
}

// Toward white (the background): amount 0 keeps c, 255 gives BG.
Color fadeToBg(Color c, uint8_t amount) { return gfx::mix(c, BG, amount); }


// Header line of the board
void drawBoardStatus(gfx::Canvas &c, const StopBoard &board, time_t now) {
    const time_t clockT = board.fetchedAt ? board.fetchedAt : now;
    std::tm *t = std::localtime(&clockT);
    char clock[8];
    std::snprintf(clock, sizeof clock, "%02d:%02d", t->tm_hour, t->tm_min);

    const bool errorState = board.status == BoardStatus::NetworkError ||
                            board.status == BoardStatus::KeyRejected ||
                            board.status == BoardStatus::RateLimited;
    const long age = board.fetchedAt ? static_cast<long>(now - board.fetchedAt) : 0;
    bool anyLive = false;
    for (const auto &d : board.departures) anyLive = anyLive || (d.hasMinutes && d.live && !d.cancelled);

    gfx::StatusBarText s{clock, "", TEXT_MUTED, false};
    if (board.fetchedAt == 0) {
        s.state = "";
    } else if (errorState || age >= STALE_DEAD_S) {
        s.state = "Offline";
        s.stateInk = DANGER;
    } else if (age >= STALE_WARN_S) {
        s.state = "Updated " + std::to_string(age / 60) + " min ago";
    } else if (anyLive) {
        s.state = "Live";
        s.stateInk = LIVE;
        s.liveIcon = true;
    } else {
        s.state = "Scheduled";
    }
    gfx::drawStatusBar(c, s);
}

// Baseline that centres a face's cap height on cy
int capBaselineOf(int cy, const FontFace &f) { return cy + (f.size * 35 + 50) / 100; }

// One departure, 52 px row at top r: pill and destination on the left, minutes on the right
void drawRow(gfx::Canvas &c, int r, const Departure &d, bool stale) {
    const int cy = r + 26;                       // the row's centre line
    const bool cancelled = d.cancelled;
    const bool due = !cancelled && d.minutes <= 0;
    const Color fill = routeColour(d.service);

    // Route pill, top left
    Color pillFill = cancelled ? fadeToBg(fill, 128) : fill;
    if (stale) pillFill = fadeToBg(pillFill, 128);
    gfx::drawRoutePill(c, Rect{MARGIN, r + 6, PILL_W, PILL_H}, d.service.substr(0, 3), pillFill, F::kBold16);

    // Right side: the number (Bold 26) and a smaller "min" (SemiBold 16) on one baseline,
    // or "Due" (Bold 26), or "Cancelled" (SemiBold 15). Live icon to the left of it.
    const int rightEdge = SCREEN_W - MARGIN;
    const int numBase = capBaselineOf(cy, F::kBold26);
    const int gap = 5;
    int rightW = 0;
    Color rightInk = TEXT;
    std::string num, unit;
    if (cancelled) {
        rightW = gfx::textWidth(F::kSemiBold15, "Cancelled");
        rightInk = DANGER;
    } else if (due) {
        rightW = gfx::textWidth(F::kBold26, "Due");
        rightInk = ACCENT;
    } else {
        num = std::to_string(std::min(std::max(d.minutes, 0), 99));
        unit = "min";
        rightW = gfx::textWidth(F::kBold26, num) + gap + gfx::textWidth(F::kSemiBold16, unit);
        rightInk = d.delayed ? WARN : (d.live ? TEXT : TEXT_MUTED);
    }
    if (stale) rightInk = fadeToBg(rightInk, 128);
    const bool showIcon = d.live && !cancelled;
    const int rightLeft = rightEdge - rightW - (showIcon ? 18 : 0);
    if (cancelled) {
        gfx::drawText(c, F::kSemiBold15, rightEdge - rightW, capBaselineOf(cy, F::kSemiBold15), "Cancelled", rightInk);
    } else if (due) {
        gfx::drawText(c, F::kBold26, rightEdge - rightW, numBase, "Due", rightInk);
    } else {
        const int unitW = gfx::textWidth(F::kSemiBold16, unit);
        gfx::drawText(c, F::kSemiBold16, rightEdge - unitW, numBase, unit, rightInk);
        gfx::drawTextIn(c, F::kBold26, Rect{0, 0, rightEdge - unitW - gap + 1, 0}, numBase,
                        gfx::Align::Right, num, rightInk);
    }
    if (showIcon) gfx::drawLiveIcon(c, rightLeft, cy, stale ? fadeToBg(ACCENT, 128) : ACCENT);
    if (d.delayed && !cancelled) {
        Color late = stale ? fadeToBg(WARN, 128) : WARN;
        gfx::drawTextIn(c, F::kRegular12, Rect{0, 0, rightEdge + 1, 0}, r + 48, gfx::Align::Right, "Delayed", late);
    }

    // Destination, one line under the pill, clear of the minutes
    Color ink = cancelled ? TEXT_MUTED : TEXT;
    if (stale) ink = fadeToBg(ink, 128);
    const int destMax = rightLeft - 8 - MARGIN;
    const std::string dest = gfx::fitText(F::kRegular19, d.destination, destMax);
    const int baseline = r + 41;
    gfx::drawText(c, F::kRegular19, MARGIN, baseline, dest, ink);
    if (cancelled)  // strike through at mid x-height
        c.hline(MARGIN, MARGIN + gfx::textWidth(F::kRegular19, dest) - 1, baseline - 7, ink);

    c.hline(MARGIN, SCREEN_W - MARGIN - 1, r + ROW_H - 1, stale ? fadeToBg(DIVIDER, 128) : DIVIDER);
}

// Empty and error messages in the rows area: an optional icon, a title and a line under it.
void drawStatusBlock(gfx::Canvas &c, int top, int bottom, const std::string &title, Color titleInk,
                     const std::string &message, bool errorIcon) {
    const int mid = (top + bottom) / 2;
    int y = mid - 40;
    if (errorIcon) {
        const int cx = SCREEN_W / 2, cyc = mid - 52;
        c.fillRoundRect(Rect{cx - 28, cyc - 28, 56, 56}, 28, ACCENT);  // maroon circle
        c.fillRect(Rect{cx - 2, cyc - 12, 4, 15}, BG);                   // "!" glyph
        c.fillRoundRect(Rect{cx - 2, cyc + 7, 4, 4}, 2, BG);
        y = mid + 4;
    }
    gfx::drawTextIn(c, F::kBold20, Rect{0, 0, SCREEN_W, 0}, y, gfx::Align::Center, title, titleInk);
    if (!message.empty())
        gfx::drawTextIn(c, F::kRegular15, Rect{0, 0, SCREEN_W, 0}, y + 26, gfx::Align::Center, message, TEXT_MUTED);
}

// The alert banner: a count, the first notice's text, a chevron to open the detail
void drawAlertBanner(gfx::Canvas &c, const StopBoard &board) {
    c.fillRect(Rect{0, kAlertTop, SCREEN_W, NOTICE_H}, ALERT_BG);
    const int cy = kAlertTop + NOTICE_H / 2;
    c.fillRoundRect(Rect{MARGIN, cy - 7, 14, 14}, 7, ALERT_FG);        // info circle
    c.fillRect(Rect{MARGIN + 6, cy - 3, 2, 5}, BG);                     // "i" stem
    c.fillRect(Rect{MARGIN + 6, cy - 5, 2, 2}, BG);                     // "i" dot

    const size_t n = board.notices.size();
    std::string head = std::to_string(n) + (n == 1 ? " active alert" : " active alerts");
    const FontFace &f = F::kSemiBold13;
    const int base = gfx::centredBaseline(kAlertTop, NOTICE_H, f);
    gfx::drawText(c, f, MARGIN + 20, base, head, TEXT);
    const int x = MARGIN + 20 + gfx::textWidth(f, head) + 8;
    const int maxW = SCREEN_W - 28 - x;
    if (maxW > 40)
        gfx::drawText(c, F::kRegular13, x, base,
                      gfx::fitText(F::kRegular13, board.notices.front(), maxW), TEXT_MUTED);
    gfx::drawChevron(c, SCREEN_W - MARGIN - 4, cy, ALERT_FG, true);
}

// Route chips: each service at this stop, in the order they first appear. A chevron
// stands in for the rest when they do not fit.
void drawChips(gfx::Canvas &c, const StopBoard &board, const std::set<std::string> &hidden) {
    std::vector<std::string> services;
    for (const auto &d : board.departures)
        if (d.hasMinutes && std::find(services.begin(), services.end(), d.service) == services.end())
            services.push_back(d.service);

    bool overflow = false;
    const std::vector<Rect> zones = gfx::chipLayout(services, overflow);
    for (size_t i = 0; i < zones.size(); ++i) {
        const std::string &s = services[i];
        const bool isHidden = hidden.count(s) > 0;
        gfx::drawChip(c, zones[i], s, routeColour(s), isHidden);
    }
    if (overflow)
        gfx::drawChevron(c, SCREEN_W - MARGIN - 4, kChipsTop + CHIP_BAND_H / 2, TEXT_MUTED, true);
}

// "Upcoming departures" with a maroon underline and a full-width rule. With routes hidden
// the label says how many: "Upcoming departures - 2 routes hidden".
void drawSectionLabel(gfx::Canvas &c, int hiddenRoutes) {
    const FontFace &f = F::kRegular13;
    std::string label = "Upcoming departures";
    if (hiddenRoutes > 0)
        label += " - " + std::to_string(hiddenRoutes) + (hiddenRoutes == 1 ? " route hidden" : " routes hidden");
    const int base = gfx::centredBaseline(kSectionTop, 24, f);
    gfx::drawText(c, f, MARGIN, base, label, TEXT_MUTED);
    c.fillRect(Rect{MARGIN, kSectionTop + 21, gfx::textWidth(f, "Upcoming departures"), 2}, ACCENT);
    c.hline(0, SCREEN_W - 1, kSectionTop + 23, DIVIDER);
}

}  // namespace

void drawMainScreen(gfx::Canvas &c, const StopBoard &board, time_t now, StopDirection dir,
                    const std::set<std::string> &hidden) {
    c.clear(BG);

    drawBoardStatus(c, board, now);

    gfx::StopHeaderText header{board.stopName.empty() ? "Bus stop" : board.stopName,
                               board.indicator, board.distanceFromHomeM};
    gfx::drawStopHeader(c, header);

    // Direction buttons: the selected one filled, the other outlined. With no direction
    // (StopDirection::None) neither is filled.
    gfx::drawButton(c, theme::kDirInto, "Into city", F::kSemiBold15, dir == StopDirection::Into);
    gfx::drawButton(c, theme::kDirOut, "Out of city", F::kSemiBold15, dir == StopDirection::Out);

    // Hidden routes: counted in the label, left out of the rows (their chips stay, outlined)
    int hiddenRoutes = 0;
    {
        std::set<std::string> seen;
        for (const auto &d : board.departures)
            if (hidden.count(d.service) && seen.insert(d.service).second) ++hiddenRoutes;
    }
    drawChips(c, board, hidden);
    drawSectionLabel(c, hiddenRoutes);

    const bool hasAlert = !board.notices.empty();
    if (hasAlert) drawAlertBanner(c, board);
    const int rowsTop = hasAlert ? kRowsTopAlert : kRowsTop;

    const bool stale = board.fetchedAt && now - board.fetchedAt >= STALE_DEAD_S &&
                       (board.status == BoardStatus::Ok || board.status == BoardStatus::NoDepartures);

    std::vector<const Departure *> rows;
    for (const auto &d : board.departures)
        if (d.hasMinutes && !hidden.count(d.service)) rows.push_back(&d);

    if (rows.empty()) {
        const int bottom = SCREEN_H;
        if (board.status == BoardStatus::Loading) {
            drawStatusBlock(c, rowsTop, bottom, "Loading...", TEXT_MUTED, "", false);
        } else if (board.status == BoardStatus::NetworkError) {
            drawStatusBlock(c, rowsTop, bottom, "Can't reach Lothian", DANGER, "Check the connection", true);
        } else if (board.status == BoardStatus::KeyRejected) {
            drawStatusBlock(c, rowsTop, bottom, "API key rejected", DANGER, "Check the API key", true);
        } else if (board.status == BoardStatus::RateLimited) {
            drawStatusBlock(c, rowsTop, bottom, "Too many requests", DANGER, "Trying again soon", true);
        } else {
            drawStatusBlock(c, rowsTop, bottom, "No buses in the next hour", TEXT, "", false);
        }
    } else {
        const int slots = (SCREEN_H - rowsTop) / ROW_H;
        const int count = std::min(slots, static_cast<int>(rows.size()));
        for (int i = 0; i < count; ++i)
            drawRow(c, rowsTop + i * ROW_H, *rows[i], stale);
    }
}
