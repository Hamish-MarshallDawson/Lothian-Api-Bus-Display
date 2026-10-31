// Map panel: Web Mercator basemap from OpenStreetMap tiles on disk, with a route
// polyline and markers drawn on top. The screen never waits on the network: tiles
// that are not on disk are drawn as a plain background and the map redraws when
// they arrive (TileStore::get returns nullptr for them).
#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "canvas.h"

namespace gfx {

struct GeoPoint {
    double lat, lon;
};

// A point on the world map in pixels at a zoom level (256 px tiles, 2^zoom tiles a side)
struct WorldPoint {
    double x, y;
};
WorldPoint worldPixel(GeoPoint p, int zoom);
GeoPoint geoFromWorld(WorldPoint w, int zoom);

// A decoded 256x256 tile, native RGB565
struct Tile {
    static constexpr int kSize = 256;
    std::vector<Color> px;  // kSize * kSize, row-major
};

// Tiles on disk, laid out as <dir>/tiles/<z>-<x>-<y>.png: the layout the backend's tile
// cache writes (tiles.cpp), under ~/.cache/bustracker. dir is that cache folder.
class TileStore {
public:
    explicit TileStore(std::string dir);
    // The decoded tile, or nullptr when it is not on disk. Decodes once and keeps the result;
    // a missing or unreadable file is remembered too, so a frame never reads the disk twice.
    const Tile *get(int z, int x, int y);
    // Drop the remembered result for one tile, so the next get() reads the disk again
    // (call after the tile has been fetched).
    void forget(int z, int x, int y);
    static std::string tilePath(const std::string &dir, int z, int x, int y);

private:
    std::string dir_;
    std::map<uint64_t, std::unique_ptr<Tile>> tiles_;  // nullptr = not available
};

struct MapPath {
    std::vector<GeoPoint> points;
    Color color;
    int width;  // px, drawn thick and anti-aliased
};

struct MapMarker {
    GeoPoint at;
    Color fill;
    Color ring;  // outer ring colour; the marker is ring-radius wide, fill-radius inside
    int radius;  // px
    std::string label;  // optional text centred in the marker (e.g. the bus number)
};

struct MapScene {
    GeoPoint centre{0, 0};
    int zoom = 15;
    Rect rect{0, 0, 320, 200};  // where the map sits on the canvas
    std::vector<MapPath> paths;
    std::vector<MapMarker> markers;
    // A point off the panel: drawn as a pill at the panel edge, pointing towards it (if on, and outside)
    bool edgeOn = false;
    GeoPoint edgeTarget{0, 0};
    std::string edgeLabel;
    Color edgeFill = 0;
};

// A tile by its address, as TileStore wants it
struct TileRef {
    int z, x, y;
};

// The tiles that cover the scene's panel, the ones drawMap looks for, with x wrapped
// round the world. The caller fetches the missing ones, one at a time.
std::vector<TileRef> sceneTiles(const MapScene &scene);

// Pick the centre and the largest zoom (within minZoom..maxZoom) at which every point fits
// inside the rect with padPx spare on each side.
bool fitScene(MapScene &scene, const std::vector<GeoPoint> &points, int padPx,
              int minZoom = 10, int maxZoom = 16, const GeoPoint *focus = nullptr);

// Draw the basemap (background, then tiles on disk), the paths, the markers, and the
// "© OpenStreetMap contributors" credit in the bottom-right corner.
void drawMap(Canvas &c, const MapScene &scene, TileStore &tiles);

// Where a geographic point lands on the canvas for this scene (may be off the panel)
WorldPoint canvasPoint(const MapScene &scene, GeoPoint p);

// The plain background used where there is no tile
constexpr Color kMapFallback = rgb(0xF2, 0xF4, 0xF5);

}  // namespace gfx
