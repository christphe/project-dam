// PNG for the editor: --screenshot, thumb.png, imported pictures. Thin C++ calls on ge_assets' reader and writer (port/assets/ge_png.h).
#pragma once
#include <cstdint>
#include <cstdlib>
#include <vector>

extern "C" {
#include "ge_png.h"
}

// Writes top-down RGBA8 pixels as an RGB PNG (alpha dropped), smallest-sum row filters, zlib level 9.
inline bool writePng(const char *path, int w, int h, const uint8_t *rgba) { return gePngWrite(path, w, h, rgba, GE_PNG_RGB | GE_PNG_BEST) != 0; }

// Reads an 8-bit grey / grey+alpha / RGB / RGBA / palette, non-interlaced PNG as RGBA8.
inline bool readPng(const char *path, int &w, int &h, std::vector<uint8_t> &rgba)
{
    uint8_t *px = nullptr;
    if (!gePngRead(path, &w, &h, &px)) { w = h = 0; return false; }
    rgba.assign(px, px + (size_t)w * h * 4);
    free(px);
    return true;
}
