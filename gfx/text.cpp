#include "text.h"

namespace gfx {

namespace {

int glyphIndex(unsigned char c) {
    if (c < kFirstGlyph || c >= kFirstGlyph + kGlyphCount) return '?' - kFirstGlyph;
    return c - kFirstGlyph;
}

}  // namespace

int textWidth(const FontFace& f, const std::string& s) {
    int pen = 0, prev = -1;
    for (unsigned char uc : s) {
        int gi = glyphIndex(uc);
        if (prev >= 0 && f.kern) pen += f.kern[prev * kGlyphCount + gi];
        pen += f.glyphs[gi].advance;
        prev = gi;
    }
    return pen;
}

int drawText(Canvas& c, const FontFace& f, int x, int baselineY,
             const std::string& s, Color fg) {
    int pen = x, prev = -1;
    for (unsigned char uc : s) {
        int gi = glyphIndex(uc);
        if (prev >= 0 && f.kern) pen += f.kern[prev * kGlyphCount + gi];
        const Glyph& g = f.glyphs[gi];
        const uint8_t* bitmap = f.alpha + g.dataOffset;
        for (int row = 0; row < g.h; ++row)
            for (int col = 0; col < g.w; ++col) {
                uint8_t a = bitmap[row * g.w + col];
                if (a) c.blendPixel(pen + g.xoff + col, baselineY + g.yoff + row, fg, a);
            }
        pen += g.advance;
        prev = gi;
    }
    return pen;
}

void drawTextIn(Canvas& c, const FontFace& f, Rect box, int baselineY,
                Align a, const std::string& s, Color fg) {
    int w = textWidth(f, s);
    int x = box.x;
    if (a == Align::Right) x = box.x + box.w - w;
    else if (a == Align::Center) x = box.x + (box.w - w) / 2;
    drawText(c, f, x, baselineY, s, fg);
}

std::string fitText(const FontFace& f, const std::string& s, int maxWidth) {
    if (textWidth(f, s) <= maxWidth) return s;
    const std::string dots = "...";
    for (size_t n = s.size(); n > 0; --n) {
        std::string cand = s.substr(0, n) + dots;
        if (textWidth(f, cand) <= maxWidth) return cand;
    }
    return textWidth(f, dots) <= maxWidth ? dots : std::string();
}

std::vector<std::string> wrapText(const FontFace& f, const std::string& s,
                                  int maxWidth, int maxLines) {
    std::vector<std::string> out;
    std::string rest = s;
    auto trimLeft = [](std::string& t) {
        size_t k = t.find_first_not_of(' ');
        t = (k == std::string::npos) ? std::string() : t.substr(k);
    };
    trimLeft(rest);
    while (!rest.empty()) {
        if (static_cast<int>(out.size()) + 1 == maxLines) {
            out.push_back(fitText(f, rest, maxWidth));
            return out;
        }
        // Longest prefix that fits.
        size_t n = 0;
        while (n < rest.size() && textWidth(f, rest.substr(0, n + 1)) <= maxWidth) ++n;
        if (n == rest.size()) { out.push_back(rest); break; }
        size_t cut;
        size_t sp = rest.rfind(' ', n);
        if (rest[n] == ' ') { cut = n; }
        else if (sp != std::string::npos && sp > 0) { cut = sp; }
        else { cut = n > 0 ? n : 1; }  // one word wider than the line: break it
        out.push_back(rest.substr(0, cut));
        rest = rest.substr(cut);
        trimLeft(rest);
    }
    return out;
}

}  // namespace gfx
