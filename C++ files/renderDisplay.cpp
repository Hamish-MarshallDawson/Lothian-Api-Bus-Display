/*
 * The main departures board, and the flush of any frame to the LCD. Everything
 * draws into a gfx::Canvas (see .agents/DESIGN.md).
 */

extern "C" {
#ifdef BUSTRACKER_HW
    #include "../C/lib/Config/DEV_Config.h"
    #include "../C/lib/lcd/st7796.h"
    void st7796_set_windows(uint16_t x_start, uint16_t y_start, uint16_t x_end, uint16_t y_end);
#endif
}

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

#include "busColours.h"     // route colours, exactly as Lothian publishes them
#include "departures.h"
#include "renderDisplay.h"
#include "../gfx/canvas.h"
#include "../gfx/fonts/cond_bold_12.h"
#include "../gfx/fonts/cond_bold_24.h"
#include "../gfx/fonts/cond_bold_28.h"
#include "../gfx/fonts/cond_regular_12.h"
#include "../gfx/fonts/cond_regular_14.h"
#include "../gfx/fonts/cond_regular_16.h"
#include "../gfx/fonts/cond_regular_20.h"
#include "../gfx/fonts/cond_regular_24.h"
#include "../gfx/fonts/cond_regular_48.h"
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
// Route colours

namespace {

using gfx::Color;
using gfx::FontFace;
using gfx::Rect;
using namespace theme;
namespace fonts = gfx::fonts;

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

// Toward the background: amount 0 keeps c, 255 gives BG.
Color fadeToBg(Color c, uint8_t amount) { return gfx::mix(c, BG, amount); }

}  // namespace

// ---------------------------------------------------------------------------
// Main board

namespace {

const int kRowsTopNoNotice = HEADER_H + 8;                 // 72
const int kRowsTopNotice = HEADER_H + NOTICE_H + 8;        // 96

// Centred block in the rows area: a headline and an optional line under it.
void drawCentredBlock(gfx::Canvas &c, int top, int bottom, const std::string &head, Color headInk,
                      const std::string &sub) {
    const int mid = (top + bottom) / 2;
    gfx::drawTextIn(c, fonts::kCondBold24, Rect{0, 0, SCREEN_W, 0}, mid - 4, gfx::Align::Center, head, headInk);
    if (!sub.empty())
        gfx::drawTextIn(c, fonts::kCondRegular14, Rect{0, 0, SCREEN_W, 0}, mid + 18, gfx::Align::Center, sub, TEXT_MUTED);
}

void drawNotice(gfx::Canvas &c, const StopBoard &board) {
    c.fillRect(Rect{0, HEADER_H, SCREEN_W, NOTICE_H}, NOTICE_BG);
    std::string t = board.notices.front();
    if (board.notices.size() > 1) t += "  +" + std::to_string(board.notices.size() - 1);
    gfx::drawText(c, fonts::kCondRegular14, MARGIN, HEADER_H + 16,
                  gfx::fitText(fonts::kCondRegular14, t, SCREEN_W - 2 * MARGIN), NOTICE_FG);
}

// Baseline that puts a face's cap height (px) centred on centreY
int capBaseline(int centreY, int capPx) { return centreY + capPx / 2; }

// One departure, 72 px tall, at row top r. See DESIGN.md section 2.2.
void drawRow(gfx::Canvas &c, int r, const Departure &d, bool stale) {
    const int cy = r + BADGE_H / 2 + 14;  // badge centre: the row's shared centre line (r+36)
    const bool cancelled = d.cancelled;
    const Color fill = routeColour(d.service);
    Color badge = cancelled ? fadeToBg(fill, 153) : fill;
    Color ink = cancelled ? TEXT_MUTED : TEXT;
    if (stale) { badge = fadeToBg(badge, 128); ink = fadeToBg(ink, 128); }

    // Badge: the number's text colour is the only thing computed; the fill is the route's.
    c.fillRoundRect(Rect{MARGIN, r + 14, BADGE_W, BADGE_H}, BADGE_R, badge);
    std::string label = d.service.substr(0, 3);
    Color labelInk = gfx::readableOn(fill);
    if (stale) labelInk = fadeToBg(labelInk, 128);
    gfx::drawTextIn(c, fonts::kCondBold24, Rect{MARGIN, r + 14, BADGE_W, BADGE_H}, capBaseline(cy, 17),
                    gfx::Align::Center, label, labelInk);

    // Destination, up to two lines of 128 px, centred on the badge's centre line
    const FontFace &dest = fonts::kCondRegular20;
    std::vector<std::string> lines = gfx::wrapText(dest, d.destination, 128, 2);
    int baselines[2] = {capBaseline(cy, 15), 0};  // one line
    if (lines.size() == 2) {                              // two lines as a block, 24 px apart
        baselines[0] = capBaseline(cy - 12, 15);
        baselines[1] = baselines[0] + 24;
    }
    for (size_t i = 0; i < lines.size(); ++i) {
        gfx::drawText(c, dest, 84, baselines[i], lines[i], ink);
        if (cancelled)  // strike-through at mid x-height
            c.hline(84, 84 + gfx::textWidth(dest, lines[i]) - 1, baselines[i] - 6, ink);
    }

    // Time column: the number (48 px) is centred on the badge; "min" shares its baseline.
    // DUE and CANC are centred on the same line, so the column reads level.
    const Rect right{0, 0, SCREEN_W - MARGIN, 0};
    if (cancelled) {
        gfx::drawTextIn(c, fonts::kCondBold24, right, capBaseline(cy, 17), gfx::Align::Right, "CANC",
                        stale ? fadeToBg(ALERT, 128) : ALERT);
    } else if (d.minutes <= 0) {
        Color due = d.live ? LIVE : TEXT_MUTED;
        if (stale) due = fadeToBg(due, 128);
        gfx::drawTextIn(c, fonts::kCondBold28, right, capBaseline(cy, 20), gfx::Align::Right, "DUE", due);
    } else {
        Color num = d.delayed ? WARN : (d.live ? TEXT : TEXT_MUTED);
        if (stale) num = fadeToBg(num, 128);
        const int numBase = capBaseline(cy, 35);  // 48 px digits, cap height 35
        gfx::drawTextIn(c, fonts::kCondRegular16, right, numBase, gfx::Align::Right, "min", num);
        std::string n = std::to_string(std::min(d.minutes, 99));
        gfx::drawTextIn(c, fonts::kCondRegular48, Rect{0, 0, 280, 0}, numBase, gfx::Align::Right, n, num);
        if (d.delayed) {
            Color tag = stale ? fadeToBg(WARN, 128) : WARN;
            gfx::drawTextIn(c, fonts::kCondBold12, right, r + 66, gfx::Align::Right, "LATE", tag);
        } else if (!d.live) {
            gfx::drawTextIn(c, fonts::kCondBold12, right, r + 66, gfx::Align::Right, "SCHED", TEXT_MUTED);
        }
    }

    c.hline(MARGIN, SCREEN_W - MARGIN - 1, r + ROW_H - 1, DIVIDER);
}

// Footer: status dot and text on the left, the tap hint on the right.
void drawFooter(gfx::Canvas &c, const StopBoard &board, time_t now) {
    const bool errorState = board.status == BoardStatus::NetworkError ||
                            board.status == BoardStatus::KeyRejected ||
                            board.status == BoardStatus::RateLimited;
    const bool hasData = board.fetchedAt != 0;
    const long age = hasData ? static_cast<long>(now - board.fetchedAt) : 0;

    bool anyLive = false;
    for (const auto &d : board.departures) anyLive = anyLive || (d.hasMinutes && d.live && !d.cancelled);

    std::string text;
    Color dot = TEXT_MUTED;
    char hhmm[8] = "";
    if (hasData) {
        std::tm *t = std::localtime(&board.fetchedAt);
        std::snprintf(hhmm, sizeof hhmm, "%02d:%02d", t->tm_hour, t->tm_min);
    }

    if (board.status == BoardStatus::Loading) {
        text = "Loading...";
    } else if (errorState) {
        dot = ALERT;
        text = board.status == BoardStatus::NetworkError ? "No connection"
             : board.status == BoardStatus::KeyRejected ? "Key rejected"
             : "Rate limited";
    } else if (age >= STALE_DEAD_S) {
        dot = ALERT;
        text = std::string("No live data since ") + hhmm;
    } else if (age >= STALE_WARN_S) {
        dot = WARN;
        text = "Updated " + std::to_string(age / 60) + " min ago";
    } else if (anyLive) {
        dot = LIVE;
        text = std::string("Live, updated ") + hhmm;
    } else {
        text = hasData ? std::string("Timetable times, ") + hhmm : "Timetable times";
    }

    const int hintW = gfx::textWidth(fonts::kCondRegular12, "Tap stop to change");
    c.fillRoundRect(Rect{MARGIN, 447, 6, 6}, 3, dot);
    gfx::drawText(c, fonts::kCondRegular14, MARGIN + 12, 456,
                  gfx::fitText(fonts::kCondRegular14, text, SCREEN_W - 2 * MARGIN - 12 - hintW - 12), TEXT_MUTED);
    gfx::drawTextIn(c, fonts::kCondRegular12, Rect{0, 0, SCREEN_W - MARGIN, 0}, 456, gfx::Align::Right,
                    "Tap stop to change", TEXT_MUTED);
}

}  // namespace

void drawMainScreen(gfx::Canvas &c, const StopBoard &board, time_t now) {
    c.clear(BG);

    // Clock and data age: the board's own fetch time, so a snapshot is repeatable
    const time_t clockT = board.fetchedAt ? board.fetchedAt : now;
    std::tm *t = std::localtime(&clockT);
    char clockText[8];
    std::snprintf(clockText, sizeof clockText, "%02d:%02d", t->tm_hour, t->tm_min);

    gfx::HeaderText header;
    header.title = board.stopName.empty() ? "Bus stop" : board.stopName;
    header.right = clockText;
    header.kicker = board.indicator;  // direction labels live on the flows' own screens
    gfx::drawHeader(c, header);

    if (!board.notices.empty()) drawNotice(c, board);
    const int rowsTop = board.notices.empty() ? kRowsTopNoNotice : kRowsTopNotice;
    const int rowsBottom = FOOTER_Y;

    const bool stale = board.fetchedAt && now - board.fetchedAt >= STALE_DEAD_S &&
                       (board.status == BoardStatus::Ok || board.status == BoardStatus::NoDepartures);

    std::vector<const Departure *> rows;
    for (const auto &d : board.departures)
        if (d.hasMinutes) rows.push_back(&d);

    if (rows.empty()) {
        if (board.status == BoardStatus::Loading) {
            drawCentredBlock(c, rowsTop, rowsBottom, "Loading...", TEXT_MUTED, "");
        } else if (board.status == BoardStatus::NetworkError) {
            drawCentredBlock(c, rowsTop, rowsBottom, "Can't reach Lothian", ERROR, "Check the connection");
        } else if (board.status == BoardStatus::KeyRejected) {
            drawCentredBlock(c, rowsTop, rowsBottom, "API key rejected", ERROR, "Check BUSTRACKER_KEY");
        } else if (board.status == BoardStatus::RateLimited) {
            drawCentredBlock(c, rowsTop, rowsBottom, "Too many requests", ERROR, "Trying again soon");
        } else {
            drawCentredBlock(c, rowsTop, rowsBottom, "No buses in the next hour", TEXT_MUTED, "");
        }
    } else {
        const int slots = (rowsBottom - rowsTop) / ROW_H;
        const int count = std::min(slots, static_cast<int>(rows.size()));
        for (int i = 0; i < count; ++i)
            drawRow(c, rowsTop + i * ROW_H, *rows[i], stale);
    }
    drawFooter(c, board, now);
}
