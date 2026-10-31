#ifndef TILES_H
#define TILES_H

// OpenStreetMap tile bytes, from a disk cache (~/.cache/bustracker/tiles/, kept
// 7 days) or one HTTP request. The caller decodes the PNG (stb_image, in gfx/).
//
// Usage policy: a User-Agent that names the app, one request at a time, no
// prefetching, and "© OpenStreetMap contributors" shown on the map.

#include <string>

struct TileId {
    int z = 0;
    int x = 0;
    int y = 0;
};

// Fills pngOut with the tile's PNG bytes and returns true if available.
// Cached tiles return without the network. With allowNetwork false, never
// fetches. Never blocks the caller for long: a request has the usual timeouts.
bool tileBytes(const TileId &tile, std::string &pngOut, bool allowNetwork);

#endif // TILES_H
