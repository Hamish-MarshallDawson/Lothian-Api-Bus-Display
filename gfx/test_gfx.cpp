// Self-check for the gfx layer. Build and run on the Pi:
//   g++ -std=c++17 -Wall -o test_gfx gfx/test_gfx.cpp gfx/canvas.cpp gfx/text.cpp && ./test_gfx
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

#include "canvas.h"
#include "text.h"
#include "theme.h"
#include "fonts/cond_regular_20.h"
#include "fonts/cond_bold_24.h"
#include "fonts/cond_regular_48.h"

using namespace gfx;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

static const FontFace& R20 = fonts::kCondRegular20;
static const FontFace& B24 = fonts::kCondBold24;
static const FontFace& R48 = fonts::kCondRegular48;

int main() {
    // Colour packing matches the 24-bit values quoted in theme.h.
    CHECK(rgb(0x0B, 0x0F, 0x14) == theme::BG);
    CHECK(rgb(0x14, 0x1B, 0x24) == theme::HEADER_BG);
    CHECK(rgb(0xF2, 0xF5, 0xF8) == theme::TEXT);
    CHECK(rgb(0x8A, 0x94, 0xA3) == theme::TEXT_MUTED);
    CHECK(rgb(0x24, 0x30, 0x40) == theme::DIVIDER);
    CHECK(rgb(0x3D, 0xDC, 0x84) == theme::LIVE);
    CHECK(rgb(0xFF, 0xB0, 0x20) == theme::WARN);
    CHECK(rgb(0xFF, 0x4D, 0x4D) == theme::ALERT);
    CHECK(rgb(0x5A, 0x63, 0x70) == theme::BADGE_UNKNOWN);
    CHECK(rgb(0xFF, 0xFF, 0xFF) == theme::BADGE_TEXT_LIGHT);

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
        std::string s = "Edinburgh Park Station Bus Interchange Gyle";
        auto lines = wrapText(R20, s, 128, 2);
        CHECK(lines.size() == 2);
        for (auto& l : lines) CHECK(textWidth(R20, l) <= 128);
        CHECK(lines[0] == "Edinburgh Park");
        CHECK(lines[1].size() >= 3 && lines[1].substr(lines[1].size() - 3) == "...");

        auto short_ = wrapText(R20, "Cameron Toll", 128, 2);
        CHECK(short_.size() == 1 && short_[0] == "Cameron Toll");

        auto none = wrapText(R20, "", 128, 2);
        CHECK(none.empty());

        auto broken = wrapText(R20, "Supercalifragilisticexpialidocious", 80, 2);
        CHECK(!broken.empty());
        for (auto& l : broken) CHECK(textWidth(R20, l) <= 80);
    }

    if (failures == 0) std::printf("gfx: all checks passed\n");
    else std::printf("gfx: %d check(s) failed\n", failures);
    return failures ? 1 : 0;
}
