// png_write.cpp
// Writes an RGB565 frame buffer as an 8-bit RGB PNG. Uses zlib (already a
// dependency through libcurl's stack) for compression and crc32.

#include "png_write.h"

#include <cstdio>
#include <vector>
#include <zlib.h>

static void putBE32(std::vector<unsigned char> &out, uint32_t v) {
    out.push_back(v >> 24); out.push_back(v >> 16); out.push_back(v >> 8); out.push_back(v);
}

static void appendChunk(FILE *f, const char type[4], const std::vector<unsigned char> &data) {
    std::vector<unsigned char> head;
    putBE32(head, static_cast<uint32_t>(data.size()));
    head.insert(head.end(), type, type + 4);
    uLong crc = crc32(0L, head.data() + 4, 4);
    if (!data.empty()) crc = crc32(crc, data.data(), static_cast<uInt>(data.size()));
    fwrite(head.data(), 1, head.size(), f);
    if (!data.empty()) fwrite(data.data(), 1, data.size(), f);
    std::vector<unsigned char> tail;
    putBE32(tail, static_cast<uint32_t>(crc));
    fwrite(tail.data(), 1, tail.size(), f);
}

bool writePngRGB565(const std::string &path, const uint16_t *pixels, int width, int height) {
    // Raw scanlines: one filter byte (0 = none) then RGB888 per pixel.
    std::vector<unsigned char> raw;
    raw.reserve(static_cast<size_t>(height) * (1 + 3 * width));
    for (int y = 0; y < height; ++y) {
        raw.push_back(0);
        for (int x = 0; x < width; ++x) {
            uint16_t c = pixels[y * width + x];
            raw.push_back(((c >> 11) & 0x1F) * 255 / 31);
            raw.push_back(((c >> 5) & 0x3F) * 255 / 63);
            raw.push_back((c & 0x1F) * 255 / 31);
        }
    }

    uLongf zlen = compressBound(raw.size());
    std::vector<unsigned char> z(zlen);
    if (compress2(z.data(), &zlen, raw.data(), raw.size(), Z_BEST_COMPRESSION) != Z_OK) return false;
    z.resize(zlen);

    FILE *f = fopen(path.c_str(), "wb");
    if (!f) return false;
    static const unsigned char sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    fwrite(sig, 1, 8, f);

    std::vector<unsigned char> ihdr;
    putBE32(ihdr, static_cast<uint32_t>(width));
    putBE32(ihdr, static_cast<uint32_t>(height));
    // bit depth 8, colour type 2 (RGB), compression 0, filter 0, no interlace
    ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});
    appendChunk(f, "IHDR", ihdr);
    appendChunk(f, "IDAT", z);
    appendChunk(f, "IEND", {});
    return fclose(f) == 0;
}
