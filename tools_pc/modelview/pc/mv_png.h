// modelview: PNG writing through ge_assets' writer (port/assets/ge_png.h): RGB8 (alpha dropped), the screenshots.
#pragma once
#include <stdint.h>
#include <string>

extern "C" {
#include "ge_png.h"
}

namespace mvpng {

// rgbaTop: w*h RGBA, first row on top
static inline bool write(const std::string& p, int w, int h, const uint8_t* rgbaTop) { return gePngWrite(p.c_str(), w, h, rgbaTop, GE_PNG_RGB) != 0; }

}  // namespace mvpng
