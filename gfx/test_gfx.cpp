// Self-check for the gfx layer. Build and run on the Pi:
//   g++ -std=c++17 -Wall -o test_gfx gfx/test_gfx.cpp gfx/canvas.cpp gfx/text.cpp && ./test_gfx
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

#include "canvas.h"
#include "text.h"
#include "theme.h"
#include "widgets.h"
#include "fonts/faces.h"
#include "fonts/faces.h"
#include "fonts/faces.h"

using namespace gfx;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

static const FontFace& R20 = fonts::kRegular17;
static const FontFace& B24 = fonts::kBold20;
static const FontFace& R48 = fonts::kBold26;

int main() {
    // Colour packing matches the 24-bit values quoted in theme.h (LOTHIAN-STYLE.md palette).
    CHECK(rgb(0xFF, 0xFF, 0xFF) == theme::BG);
    CHECK(rgb(0x1E, 0x27, 0x2C) == theme::TEXT);
    CHECK(rgb(0x6B, 0x77, 0x80) == theme::TEXT_MUTED);
    CHECK(rgb(0xE4, 0xE9, 0xEB) == theme::DIVIDER);
    CHECK(rgb(0x8C, 0x17, 0x13) == theme::ACCENT);
    CHECK(rgb(0xB2, 0x6A, 0x00) == theme::WARN);
    CHECK(rgb(0xC6, 0x28, 0x28) == theme::DANGER);
    CHECK(theme::LIVE == theme::ACCENT);
    CHECK(rgb(0x6B, 0x77, 0x80) == theme::BADGE_UNKNOWN);
    CHECK(rgb(0xFF, 0xFF, 0xFF) == theme::BADGE_TEXT_LIGHT);
    CHECK(theme::kStopHeader.h >= 48 && theme::kDirIntoZone.h >= 48 && theme::kDirOutZone.h >= 48);

    // mix endpoints and midpoint
    CHECK(mix(0x1234, 0xABCD, 0) == 0x1234);
    CHECK(mix(0x1234, 0xABCD, 255) == 0xABCD);
    CHECK(mix(rgb(0, 0, 0), rgb(255, 255, 255), 128) == rgb(128, 128, 128) ||
          mix(rgb(0, 0, 0), rgb(255, 255, 255), 128) == rgb(127, 127, 127));

    // readableOn: dark text on light fills, white on dark fills
    CHECK(readableOn(rgb(0xFD, 0xD8, 0x35)) == theme::BADGE_TEXT_DARK);  // Lothian yellow
    CHECK(readableOn(rgb(0x98, 0xC8, 0xED)) == theme::BADGE_TEXT_DARK);  // route 1 pale blue
    CHECK(readableOn(rgb(0x00, 0x2C, 0x4D)) == theme::BADGE_TEXT_LIGHT); // airport 100 navy
    CHECK(readableOn(rgb(0x00, 0x00, 0x00)) == theme::BADGE_TEXT_LIGHT); // X6 black

    // fillRect respects the clip and the buffer edges
    {
        const int W = 20, H = 20;
        std::vector<Color> buf(W * H, 0);
        Canvas c(buf.data(), W, H);
        c.setClip(Rect{5, 5, 10, 10});
        c.fillRect(Rect{0, 0, 100, 100}, 0x1111);
        CHECK(buf[5 * W + 5] == 0x1111);
        CHECK(buf[4 * W + 5] == 0);
        CHECK(buf[14 * W + 14] == 0x1111);
        CHECK(buf[15 * W + 14] == 0);
        c.clearClip();
        c.fillRoundRect(Rect{0, 0, 20, 20}, 6, 0x2222);
        CHECK(buf[0] == 0);                                          // corner pixel is outside the arc
        CHECK(buf[10 * W + 10] == 0x2222);                           // centre is solid
        CHECK(buf[10 * W + 0] == 0x2222);                            // edge midpoint is solid
    }

    // line: endpoints drawn, diagonal hits the diagonal pixels
    {
        const int W = 10, H = 10;
        std::vector<Color> buf(W * H, 0);
        Canvas c(buf.data(), W, H);
        c.line(0, 0, 9, 9, 0x7777);
        for (int i = 0; i < 10; ++i) CHECK(buf[i * W + i] == 0x7777);
    }

    // text metrics: monospaced digits, kern and width consistency
    {
        int d = R48.glyphs['1' - kFirstGlyph].advance;
        CHECK(R48.glyphs['0' - kFirstGlyph].advance == d);
        CHECK(R48.glyphs['8' - kFirstGlyph].advance == d);
        CHECK(textWidth(R48, "12") == 2 * d);
        CHECK(textWidth(R20, "") == 0);
        CHECK(textWidth(R20, "Ocean") > 0);
        CHECK(textWidth(R20, "Ocean") == textWidth(R20, "Ocean"));
        CHECK(R20.kern != nullptr);
    }

    // drawText draws something inside the expected box and returns the pen position
    {
        const int W = 120, H = 40;
        std::vector<Color> buf(W * H, 0);
        Canvas c(buf.data(), W, H);
        int end = drawText(c, B24, 4, 30, "22", 0xFFFF);
        CHECK(end == 4 + textWidth(B24, "22"));
        int lit = 0;
        for (Color p : buf) lit += (p != 0);
        CHECK(lit > 50);
        // nothing below the baseline descender zone, nothing left of the pen
        for (int y = 34; y < H; ++y) for (int x = 0; x < W; ++x) CHECK(buf[y * W + x] == 0);
        for (int y = 0; y < H; ++y) for (int x = 0; x < 3; ++x) CHECK(buf[y * W + x] == 0);
    }

    // fitText: never wider than the limit, ends with "..." when cut
    {
        std::string long_ = "Edinburgh Park Station Bus Interchange";
        std::string f = fitText(R20, long_, 100);
        CHECK(textWidth(R20, f) <= 100);
        CHECK(f.size() >= 3 && f.substr(f.size() - 3) == "...");
        CHECK(fitText(R20, "Ocean", 500) == "Ocean");
        CHECK(fitText(R20, "Ocean", 1) == "");
    }

    // wrapText: at most maxLines, each line fits, nothing silently lost except the cut
    {
        std::string s = "Edinburgh Park Station Bus Interchange Gyle Road Extended Terminus Name";
        auto lines = wrapText(R20, s, 128, 2);
        CHECK(lines.size() == 2);
        for (auto& l : lines) CHECK(textWidth(R20, l) <= 128);
        CHECK(lines[0].size() > 0 && textWidth(R20, lines[0]) <= 128);
        CHECK(lines[1].size() >= 3 && lines[1].substr(lines[1].size() - 3) == "...");

        auto short_ = wrapText(R20, "Cameron Toll", 128, 2);
        CHECK(short_.size() == 1 && short_[0] == "Cameron Toll");

        auto none = wrapText(R20, "", 128, 2);
        CHECK(none.empty());

        auto broken = wrapText(R20, "Supercalifragilisticexpialidocious", 80, 2);
        CHECK(!broken.empty());
        for (auto& l : broken) CHECK(textWidth(R20, l) <= 80);
    }

    // Status bar and stop header: the status bar rule at y=23, the maroon stop icon at
    // (12, 37), text inside the header band, and nothing drawn outside the bands
    {
        const int W = 320, H = 480;
        std::vector<Color> buf(W * H, theme::BG);
        Canvas c(buf.data(), W, H);
        drawStatusBar(c, StatusBarText{"16:02", "Live", theme::LIVE, true});
        CHECK(buf[23 * W + 100] == theme::DIVIDER);
        drawStopHeader(c, StopHeaderText{"Leith Street", "JB", 630});
        CHECK(buf[40 * W + 14] == theme::ACCENT);                       // inside the stop icon
        CHECK(buf[40 * W + 200] == theme::BG);                          // header background
        int ink = 0;
        for (int y = 24; y < 72; ++y) for (int x = 40; x < 260; ++x) ink += (buf[y * W + x] == theme::TEXT);
        CHECK(ink > 50);
        CHECK(buf[72 * W + 100] == theme::BG);                          // below the header
    }
    {
        // Route pill: fill is the route colour, the number dark or light by contrast
        const int W = 80, H = 30;
        std::vector<Color> buf(W * H, theme::BG);
        Canvas c(buf.data(), W, H);
        drawRoutePill(c, Rect{0, 0, 40, 18}, "1", rgb(0x98, 0xC8, 0xED), fonts::kBold14);
        CHECK(buf[2 * W + 2] == rgb(0x98, 0xC8, 0xED));
    }

    if (failures == 0) std::printf("gfx: all checks passed\n");
    else std::printf("gfx: %d check(s) failed\n", failures);
    return failures ? 1 : 0;
}
