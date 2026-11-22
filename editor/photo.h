// The "old photo" look baked into thumb.png by Capture (pure CPU, no dependency).
#pragma once

#include <cstdint>
#include <vector>

// Applies the look to top-down RGBA8 pixels in place: warm sepia, lifted blacks and softened
// contrast, about 1 px of blur, vignette, fine film grain, faint scratches and dust, and a thin
// off-white print border. intensity 0 leaves the pixels alone, 1 is the default look, up to 1.5.
// Deterministic: the noise is seeded from the pixels, so the same view gives the same file.
void applyOldPhoto(std::vector<uint8_t> &rgba, int w, int h, float intensity);
