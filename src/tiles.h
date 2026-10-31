#ifndef TILES_H
#define TILES_H

// OpenStreetMap tiles: the PNG bytes of one tile, cached on disk under
// ~/.cache/bustracker/tiles/ and revalidated against the server, per the tile usage
// policy (operations.osmfoundation.org/policies/tiles):
//   - URL from BUS_TILE_URL (default https://tile.openstreetmap.org/{z}/{x}/{y}.png)
//   - User-Agent from USER_AGENT(): names the app; never curl's default
//   - Cache-Control, Expires, ETag and Last-Modified from the reply are honoured
//   - no headers: the tile is kept at least 7 days
//   - expired tiles are revalidated with If-None-Match / If-Modified-Since; a 304
//     keeps the cached bytes
//   - never sends Cache-Control: no-cache or Pragma
//   - one request at a time, and only the tiles the caller asks for (the caller
//     fetches only the current viewport; nothing is prefetched)
//   - "© OpenStreetMap contributors" must be shown on any map

#include <ctime>
#include <string>

struct TileId {
    int z = 0;
    int x = 0;
    int y = 0;
};

// The URL for a tile, from BUS_TILE_URL
std::string tileUrl(const TileId &tile);

// Fills pngOut with the tile's PNG bytes and returns true if any copy is available.
// A fresh cached tile is served without the network. With allowNetwork false, a
// stale copy is still returned, and nothing is fetched. A failed request is not
// retried for a minute.
bool tileBytes(const TileId &tile, std::string &pngOut, bool allowNetwork);

// True if the bytes start with the PNG signature
bool isPng(const std::string &bytes);

// When a reply stops being fresh, from its caching headers. fetchedAt is when it
// arrived. Missing headers give fetchedAt + 7 days. noStore is set for
// "Cache-Control: no-store", which means the reply must not be kept at all.
// Pure, for tests. Header values are the raw strings ("" when absent).
time_t tileExpiry(const std::string &cacheControl, const std::string &expires,
                  time_t fetchedAt, bool &noStore);

#endif // TILES_H
