// Text drawing with baked, anti-aliased, proportional fonts (see tools/bake_font.cpp).
// Only printable ASCII (32..126) is supported. Other bytes draw as '?'.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "canvas.h"

namespace gfx {

constexpr int kFirstGlyph = 32;
constexpr int kGlyphCount = 95;

struct Glyph {
    uint8_t  w, h;        // bitmap size in px
    int8_t   xoff, yoff;  // bitmap top-left relative to pen x and baseline (y down)
    uint8_t  advance;     // pen advance in px (digits are equalised by the baker)
    uint32_t dataOffset;  // first alpha byte in FontFace::alpha, row-major
};

struct FontFace {
    const Glyph*   glyphs;  // kGlyphCount entries, index = codepoint - kFirstGlyph
    const uint8_t* alpha;
    const int8_t*  kern;    // kGlyphCount*kGlyphCount px adjustments, or nullptr
    uint8_t ascent;         // baseline to top of the line box, px
    uint8_t descent;        // baseline to bottom of the line box, px
    uint8_t size;           // nominal em size in px
};

int textWidth(const FontFace& f, const std::string& s);

// Draw with the pen at x on the given baseline. Returns the pen x after the text.
int drawText(Canvas& c, const FontFace& f, int x, int baselineY,
             const std::string& s, Color fg);

enum class Align { Left, Right, Center };

// Draw inside box, aligned horizontally in it. The caller makes sure it fits (fitText).
void drawTextIn(Canvas& c, const FontFace& f, Rect box, int baselineY,
                Align a, const std::string& s, Color fg);

// s shortened with "..." so it is at most maxWidth wide. Returns "" if even "..." does not fit.
std::string fitText(const FontFace& f, const std::string& s, int maxWidth);

// Greedy word wrap into at most maxLines lines of maxWidth. Words longer than a
// line are broken. The last line is cut with "..." if the text does not fit.
std::vector<std::string> wrapText(const FontFace& f, const std::string& s,
                                  int maxWidth, int maxLines);

}  // namespace gfx
