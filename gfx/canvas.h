// Canvas: drawing into the RGB565 frame buffer. Colours are native RGB565
// (not byte-swapped); the LCD flush is responsible for byte order.
#pragma once
#include <cstdint>

namespace gfx {

using Color = uint16_t;  // RGB565, native byte order

constexpr Color rgb(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<Color>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

// Per-channel blend in 8-bit space: t=0 gives a, t=255 gives b.
Color mix(Color a, Color b, uint8_t t);

// Black or white text, whichever has more contrast on bg (WCAG relative luminance).
Color readableOn(Color bg);

struct Rect { int x, y, w, h; };

class Canvas {
public:
    Canvas(Color* pixels, int width, int height);

    int width() const { return w_; }
    int height() const { return h_; }

    // All drawing is clipped to this rect (and to the buffer).
    void setClip(Rect r);
    void clearClip();

    void clear(Color c);
    void fillRect(Rect r, Color c);
    // Source-over blend of a whole rect with coverage alpha (255 = solid)
    void fillRectBlend(Rect r, Color c, uint8_t alpha);
    // Corners are anti-aliased (4x4 coverage). radius is clamped to half the size.
    void fillRoundRect(Rect r, int radius, Color c);
    void hline(int x0, int x1, int y, Color c);           // inclusive x range
    void line(int x0, int y0, int x1, int y1, Color c);   // 1 px Bresenham
    // Source-over blend of c with coverage alpha (255 = solid).
    void blendPixel(int x, int y, Color c, uint8_t alpha);

    // Frame buffer, row-major, native RGB565, as the person sees it (not flipped).
    const Color* data() const { return px_; }

private:
    Rect clipped(Rect r) const;

    Color* px_;
    int w_, h_;
    Rect clip_;
};

}  // namespace gfx
