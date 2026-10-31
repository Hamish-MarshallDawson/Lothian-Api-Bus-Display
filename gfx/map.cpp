#include "map.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "text.h"
#include "fonts/faces.h"
#include "theme.h"

#define STB_IMAGE_IMPLEMENTATION
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#define STBI_ONLY_PNG
#include "../third_party/stb_image.h"
#pragma GCC diagnostic pop

namespace gfx {

namespace {

constexpr double kPi = 3.14159265358979323846;

// Coverage in 0..1 of a disc or a thick line at distance d from its edge centreline
inline double edgeCoverage(double halfWidth, double d) {
    return std::clamp(halfWidth - d + 0.5, 0.0, 1.0);
}

// Anti-aliased filled disc
void fillDisc(Canvas &c, double cx, double cy, double r, Color col) {
    const int x0 = static_cast<int>(std::floor(cx - r - 1)), x1 = static_cast<int>(std::ceil(cx + r + 1));
    const int y0 = static_cast<int>(std::floor(cy - r - 1)), y1 = static_cast<int>(std::ceil(cy + r + 1));
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            double d = std::hypot(x + 0.5 - cx, y + 0.5 - cy);
            double cov = edgeCoverage(r, d);
            if (cov > 0) c.blendPixel(x, y, col, static_cast<uint8_t>(cov * 255));
        }
}

// Anti-aliased thick segment from (x0,y0) to (x1,y1), width w px
void strokeSegment(Canvas &c, double x0, double y0, double x1, double y1, double w, Color col) {
    const double half = w / 2.0;
    const int bx0 = static_cast<int>(std::floor(std::min(x0, x1) - half - 1));
    const int bx1 = static_cast<int>(std::ceil(std::max(x0, x1) + half + 1));
    const int by0 = static_cast<int>(std::floor(std::min(y0, y1) - half - 1));
    const int by1 = static_cast<int>(std::ceil(std::max(y0, y1) + half + 1));
    const double dx = x1 - x0, dy = y1 - y0;
    const double len2 = dx * dx + dy * dy;
    for (int y = by0; y <= by1; ++y)
        for (int x = bx0; x <= bx1; ++x) {
            double px = x + 0.5, py = y + 0.5;
            double t = len2 > 0 ? std::clamp(((px - x0) * dx + (py - y0) * dy) / len2, 0.0, 1.0) : 0.0;
            double d = std::hypot(px - (x0 + t * dx), py - (y0 + t * dy));
            double cov = edgeCoverage(half, d);
            if (cov > 0) c.blendPixel(x, y, col, static_cast<uint8_t>(cov * 255));
        }
}

// Canvas position of a world point, for a scene whose centre sits in the middle of its rect
struct Projector {
    WorldPoint centreWorld;
    Rect rect;
    int zoom;
    WorldPoint toCanvas(GeoPoint p) const {
        WorldPoint w = worldPixel(p, zoom);
        return WorldPoint{rect.x + rect.w / 2.0 + (w.x - centreWorld.x),
                          rect.y + rect.h / 2.0 + (w.y - centreWorld.y)};
    }
};

Color rgb8ToColor(const unsigned char *p) { return rgb(p[0], p[1], p[2]); }

}  // namespace

WorldPoint worldPixel(GeoPoint p, int zoom) {
    const double n = 256.0 * std::pow(2.0, zoom);
    const double latr = p.lat * kPi / 180.0;
    return WorldPoint{(p.lon + 180.0) / 360.0 * n,
                      (1.0 - std::log(std::tan(latr) + 1.0 / std::cos(latr)) / kPi) / 2.0 * n};
}

GeoPoint geoFromWorld(WorldPoint w, int zoom) {
    const double n = 256.0 * std::pow(2.0, zoom);
    const double lat = std::atan(std::sinh(kPi * (1.0 - 2.0 * w.y / n))) * 180.0 / kPi;
    return GeoPoint{lat, w.x / n * 360.0 - 180.0};
}

// ---------------------------------------------------------------------------
// TileStore

TileStore::TileStore(std::string dir) : dir_(std::move(dir)) {}

std::string TileStore::tilePath(const std::string &dir, int z, int x, int y) {
    // The backend's tile cache names: tiles/<z>-<x>-<y>.png
    return dir + "/tiles/" + std::to_string(z) + "-" + std::to_string(x) + "-" + std::to_string(y) + ".png";
}

static uint64_t tileKey(int z, int x, int y) {
    return (static_cast<uint64_t>(z) << 56) | (static_cast<uint64_t>(x) << 28) | static_cast<uint64_t>(y);
}

void TileStore::forget(int z, int x, int y) {
    tiles_.erase(tileKey(z, x, y));
}

const Tile *TileStore::get(int z, int x, int y) {
    if (dir_.empty()) return nullptr;  // no tile folder: never look at the root of the disk
    const uint64_t key = tileKey(z, x, y);
    auto it = tiles_.find(key);
    if (it != tiles_.end()) return it->second.get();

    std::unique_ptr<Tile> tile;
    const std::string path = tilePath(dir_, z, x, y);
    if (FILE *f = std::fopen(path.c_str(), "rb")) {
        int w = 0, h = 0, n = 0;
        unsigned char *img = stbi_load_from_file(f, &w, &h, &n, 3);
        std::fclose(f);
        if (img && w == Tile::kSize && h == Tile::kSize) {
            tile.reset(new Tile);
            tile->px.resize(Tile::kSize * Tile::kSize);
            for (int i = 0; i < Tile::kSize * Tile::kSize; ++i) tile->px[i] = rgb8ToColor(img + 3 * i);
        }
        stbi_image_free(img);
    }
    const Tile *out = tile.get();
    tiles_.emplace(key, std::move(tile));
    return out;
}

// ---------------------------------------------------------------------------
// Scene

void fitScene(MapScene &scene, const std::vector<GeoPoint> &points, int padPx, int minZoom, int maxZoom) {
    if (points.empty()) return;
    for (int z = maxZoom; z >= minZoom; --z) {
        double minX = 1e18, minY = 1e18, maxX = -1e18, maxY = -1e18;
        for (const auto &p : points) {
            WorldPoint w = worldPixel(p, z);
            minX = std::min(minX, w.x); maxX = std::max(maxX, w.x);
            minY = std::min(minY, w.y); maxY = std::max(maxY, w.y);
        }
        const bool fits = (maxX - minX) <= scene.rect.w - 2 * padPx && (maxY - minY) <= scene.rect.h - 2 * padPx;
        if (fits || z == minZoom) {
            scene.zoom = z;
            scene.centre = geoFromWorld(WorldPoint{(minX + maxX) / 2, (minY + maxY) / 2}, z);
            return;
        }
    }
}

std::vector<TileRef> sceneTiles(const MapScene &scene) {
    std::vector<TileRef> out;
    const Rect r = scene.rect;
    const WorldPoint centreWorld = worldPixel(scene.centre, scene.zoom);
    const double originX = centreWorld.x - r.w / 2.0;
    const double originY = centreWorld.y - r.h / 2.0;
    const int n = 1 << scene.zoom;
    const int tx0 = static_cast<int>(std::floor(originX / Tile::kSize));
    const int tx1 = static_cast<int>(std::floor((originX + r.w) / Tile::kSize));
    const int ty0 = static_cast<int>(std::floor(originY / Tile::kSize));
    const int ty1 = static_cast<int>(std::floor((originY + r.h) / Tile::kSize));
    for (int ty = std::max(ty0, 0); ty <= std::min(ty1, n - 1); ++ty)
        for (int tx = tx0; tx <= tx1; ++tx) {
            const int wrappedX = ((tx % n) + n) % n;
            out.push_back(TileRef{scene.zoom, wrappedX, ty});
        }
    return out;
}

void drawMap(Canvas &c, const MapScene &scene, TileStore &tiles) {
    const Rect r = scene.rect;
    c.setClip(r);
    c.fillRect(r, kMapFallback);

    // Tiles: for every tile that overlaps the panel and is on disk, copy its pixels in
    const WorldPoint centreWorld = worldPixel(scene.centre, scene.zoom);
    const double originX = centreWorld.x - r.w / 2.0;
    const double originY = centreWorld.y - r.h / 2.0;
    const int n = 1 << scene.zoom;
    const int tx0 = static_cast<int>(std::floor(originX / Tile::kSize));
    const int tx1 = static_cast<int>(std::floor((originX + r.w) / Tile::kSize));
    const int ty0 = static_cast<int>(std::floor(originY / Tile::kSize));
    const int ty1 = static_cast<int>(std::floor((originY + r.h) / Tile::kSize));
    bool drewTile = false;  // the credit is owed only when OSM tiles are on the screen
    for (int ty = std::max(ty0, 0); ty <= std::min(ty1, n - 1); ++ty)
        for (int tx = tx0; tx <= tx1; ++tx) {
            const int wrappedX = ((tx % n) + n) % n;  // the world wraps in longitude
            const Tile *tile = tiles.get(scene.zoom, wrappedX, ty);
            if (!tile) continue;
            drewTile = true;
            // Canvas pixels of this tile that fall inside the panel
            const int tileLeft = static_cast<int>(std::floor(tx * Tile::kSize - originX)) + r.x;
            const int tileTop = static_cast<int>(std::floor(ty * Tile::kSize - originY)) + r.y;
            for (int py = 0; py < Tile::kSize; ++py) {
                const int cy = tileTop + py;
                if (cy < r.y || cy >= r.y + r.h) continue;
                for (int px = 0; px < Tile::kSize; ++px) {
                    const int cx = tileLeft + px;
                    if (cx < r.x || cx >= r.x + r.w) continue;
                    c.blendPixel(cx, cy, tile->px[py * Tile::kSize + px], 255);
                }
            }
        }

    // Paths, then markers on top
    Projector proj{centreWorld, r, scene.zoom};
    for (const auto &path : scene.paths) {
        for (size_t i = 1; i < path.points.size(); ++i) {
            WorldPoint a = proj.toCanvas(path.points[i - 1]);
            WorldPoint b = proj.toCanvas(path.points[i]);
            strokeSegment(c, a.x, a.y, b.x, b.y, path.width, path.color);
        }
    }
    for (const auto &m : scene.markers) {
        WorldPoint p = proj.toCanvas(m.at);
        fillDisc(c, p.x, p.y, m.radius, m.ring);
        fillDisc(c, p.x, p.y, m.radius - 2.5, m.fill);
        if (!m.label.empty()) {
            const FontFace &f = fonts::kBold14;
            const int tw = textWidth(f, m.label);
            const int base = static_cast<int>(std::lround(p.y + f.size * 0.35));
            drawText(c, f, static_cast<int>(std::lround(p.x - tw / 2.0)), base, m.label,
                     readableOn(m.fill));
        }
    }
    c.clearClip();

    // Credit (OSM policy): bottom-right of the panel, on a translucent white backing; drawn last so
    // no marker or label covers it
    if (drewTile) {
    // The © is drawn as a ring with a small "c" (the baked faces are ASCII only)
    const FontFace &f = fonts::kRegular13;
    const std::string credit = "OpenStreetMap contributors";
    const int ringW = 12;
    const int w = ringW + 4 + textWidth(f, credit) + 8;
    const int x0 = r.x + r.w - w, y0 = r.y + r.h - 16;
    c.fillRectBlend(Rect{x0, y0, w, 16}, theme::BG, 200);  // white, about 80% opaque, so it reads over tiles
    fillDisc(c, x0 + 6, y0 + 8, 5.5, theme::TEXT_MUTED);
    fillDisc(c, x0 + 6, y0 + 8, 4.0, theme::BG);
    drawText(c, f, x0 + 4, y0 + 12, "c", theme::TEXT_MUTED);
    drawText(c, f, x0 + ringW + 4, y0 + 12, credit, theme::TEXT_MUTED);
    }
}

}  // namespace gfx
