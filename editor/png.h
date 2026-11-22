#pragma once
#include <cstdint>

// Writes top-down RGBA8 pixels as an RGB PNG (alpha dropped). Used by --screenshot.
bool writePng(const char *path, int w, int h, const uint8_t *rgba);

#include <vector>
// Reads an 8-bit RGB or RGBA, non-interlaced PNG (what writePng and common tools produce) as RGBA8.
bool readPng(const char *path, int &w, int &h, std::vector<uint8_t> &rgba);
