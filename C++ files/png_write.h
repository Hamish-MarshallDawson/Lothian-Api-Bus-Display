#ifndef PNG_WRITE_H
#define PNG_WRITE_H

#include <cstdint>
#include <string>

// Write a width x height RGB565 buffer (row-major, as the LCD sees it) to a PNG.
bool writePngRGB565(const std::string &path, const uint16_t *pixels, int width, int height);

#endif // PNG_WRITE_H
