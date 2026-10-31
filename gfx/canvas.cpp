#include "canvas.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace gfx {

namespace {

inline uint8_t expand5(uint8_t v) { return static_cast<uint8_t>((v << 3) | (v >> 2)); }
inline uint8_t expand6(uint8_t v) { return static_cast<uint8_t>((v << 2) | (v >> 4)); }

double linear(double c8) {  // sRGB 0..255 to linear 0..1
    double c = c8 / 255.0;
    return c <= 0.03928 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

}  // namespace

Color mix(Color a, Color b, uint8_t t) {
    int ar = expand5(a >> 11), ag = expand6((a >> 5) & 63), ab = expand5(a & 31);
    int br = expand5(b >> 11), bg = expand6((b >> 5) & 63), bb = expand5(b & 31);
    auto ch = [t](int x, int y) { return (x * (255 - t) + y * t + 127) / 255; };
    return rgb(static_cast<uint8_t>(ch(ar, br)),
               static_cast<uint8_t>(ch(ag, bg)),
               static_cast<uint8_t>(ch(ab, bb)));
}

Color readableOn(Color bg) {
    double r = linear(expand5(bg >> 11));
    double g = linear(expand6((bg >> 5) & 63));
    double b = linear(expand5(bg & 31));
    double lum = 0.2126 * r + 0.7152 * g + 0.0722 * b;
    // Equal-contrast crossover between black and white text.
    constexpr Color kDark = rgb(0x1E, 0x27, 0x2C), kLight = rgb(0xFF, 0xFF, 0xFF);  // theme::TEXT / white
    return lum > 0.179 ? kDark : kLight;
}

Canvas::Canvas(Color* pixels, int width, int height)
    : px_(pixels), w_(width), h_(height), clip_{0, 0, width, height} {}

void Canvas::setClip(Rect r) { clip_ = r; }
void Canvas::clearClip() { clip_ = Rect{0, 0, w_, h_}; }

Rect Canvas::clipped(Rect r) const {
    int x0 = std::max({r.x, clip_.x, 0});
    int y0 = std::max({r.y, clip_.y, 0});
    int x1 = std::min({r.x + r.w, clip_.x + clip_.w, w_});
    int y1 = std::min({r.y + r.h, clip_.y + clip_.h, h_});
    if (x1 <= x0 || y1 <= y0) return Rect{0, 0, 0, 0};
    return Rect{x0, y0, x1 - x0, y1 - y0};
}

void Canvas::clear(Color c) {
    std::fill(px_, px_ + static_cast<size_t>(w_) * h_, c);
}

void Canvas::fillRect(Rect r, Color c) {
    Rect k = clipped(r);
    for (int y = k.y; y < k.y + k.h; ++y) {
        Color* row = px_ + static_cast<size_t>(y) * w_;
        std::fill(row + k.x, row + k.x + k.w, c);
    }
}

void Canvas::blendPixel(int x, int y, Color c, uint8_t alpha) {
    if (alpha == 0 || x < clip_.x || y < clip_.y || x >= clip_.x + clip_.w ||
        y >= clip_.y + clip_.h || x < 0 || y < 0 || x >= w_ || y >= h_) return;
    Color& p = px_[static_cast<size_t>(y) * w_ + x];
    p = (alpha == 255) ? c : mix(p, c, alpha);
}

void Canvas::fillRoundRect(Rect r, int radius, Color c) {
    radius = std::min({radius, r.w / 2, r.h / 2});
    if (radius <= 0) { fillRect(r, c); return; }
    Rect k = clipped(r);
    for (int y = k.y; y < k.y + k.h; ++y) {
        for (int x = k.x; x < k.x + k.w; ++x) {
            int lx = x - r.x, ly = y - r.y;
            int cx = -1, cy = -1;  // corner circle centre, if this pixel is in a corner square
            if (lx < radius && ly < radius) { cx = radius; cy = radius; }
            else if (lx >= r.w - radius && ly < radius) { cx = r.w - radius; cy = radius; }
            else if (lx < radius && ly >= r.h - radius) { cx = radius; cy = r.h - radius; }
            else if (lx >= r.w - radius && ly >= r.h - radius) { cx = r.w - radius; cy = r.h - radius; }
            if (cx < 0) { blendPixel(x, y, c, 255); continue; }
            // Coverage: 4x4 sub-samples, in 1/8 px units to keep everything integral.
            int covered = 0;
            int R8 = 8 * radius;
            for (int si = 0; si < 4; ++si)
                for (int sj = 0; sj < 4; ++sj) {
                    int sx = 8 * lx + 2 * si + 1 - 8 * cx;
                    int sy = 8 * ly + 2 * sj + 1 - 8 * cy;
                    if (sx * sx + sy * sy <= R8 * R8) ++covered;
                }
            if (covered) blendPixel(x, y, c, static_cast<uint8_t>(covered * 255 / 16));
        }
    }
}

void Canvas::hline(int x0, int x1, int y, Color c) {
    fillRect(Rect{std::min(x0, x1), y, std::abs(x1 - x0) + 1, 1}, c);
}

void Canvas::line(int x0, int y0, int x1, int y1, Color c) {
    int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        blendPixel(x0, y0, c, 255);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

}  // namespace gfx

namespace gfx {

void Canvas::fillRectBlend(Rect r, Color c, uint8_t alpha) {
    Rect k = clipped(r);
    for (int y = k.y; y < k.y + k.h; ++y)
        for (int x = k.x; x < k.x + k.w; ++x) blendPixel(x, y, c, alpha);
}

}  // namespace gfx
