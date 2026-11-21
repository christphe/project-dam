// The level-water colour bake, a copy of skyPlusWaterSource() in port/src/skyplus.c (keep the two in step; this one is
// the one to edit while tuning, then port the result): from the level's water texture (RGBA8, 2..128 wide)
//   - deep / light colour = the mean colour of the texture's darker / lighter half (by luma, split at the median);
//   - patch map = the luma enlarged to MW x MH (bilinear, wrapping), 3 box blurs of radius 6 (wrapping), stretched min..max.
// The map is RGBA8, R = 0 deep .. 1 light (water.frag reads .r).
#pragma once
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <vector>
#include <algorithm>

namespace waterbake {

static const int MW = 64, MH = 64;   /* SKYPLUS_WMAP_W/H */

static inline void blurPass(std::vector<float>& a, std::vector<float>& t, int R) {
    for (int y = 0; y < MH; y++) for (int x = 0; x < MW; x++) { float s = 0; for (int k = -R; k <= R; k++) s += a[y * MW + ((x + k) % MW + MW) % MW]; t[y * MW + x] = s / (2 * R + 1); }
    for (int y = 0; y < MH; y++) for (int x = 0; x < MW; x++) { float s = 0; for (int k = -R; k <= R; k++) s += t[(((y + k) % MH + MH) % MH) * MW + x]; a[y * MW + x] = s / (2 * R + 1); }
}

/* rgba: w x h (2..128). out map: MW*MH*4. deep/light: rgb 0..1. */
static inline bool bake(const uint8_t* rgba, int w, int h, uint8_t* outMap, float* deep, float* light, int blurR = 6) {
    if (w < 2 || h < 2 || w > 128 || h > 128) return false;
    const int n = w * h; std::vector<float> lum(n), sorted(n), map(MW * MH), tmp(MW * MH);
    for (int i = 0; i < n; i++) sorted[i] = lum[i] = (0.299f * rgba[i * 4] + 0.587f * rgba[i * 4 + 1] + 0.114f * rgba[i * 4 + 2]) / 255.0f;
    std::sort(sorted.begin(), sorted.end());
    const float med = sorted[n / 2]; float ds[3] = {}, ls[3] = {}; int nd = 0, nl = 0;
    for (int i = 0; i < n; i++) { float* d = lum[i] < med ? ds : ls; for (int j = 0; j < 3; j++) d[j] += rgba[i * 4 + j] / 255.0f; if (lum[i] < med) nd++; else nl++; }
    for (int i = 0; i < 3; i++) { deep[i] = nd ? ds[i] / nd : ls[i] / nl; light[i] = nl ? ls[i] / nl : ds[i] / nd; }
    for (int y = 0; y < MH; y++) for (int x = 0; x < MW; x++) {
        const float sx = (x + 0.5f) * w / MW - 0.5f, sy = (y + 0.5f) * h / MH - 0.5f; const int x0 = (int)floorf(sx), y0 = (int)floorf(sy); const float fx = sx - x0, fy = sy - y0;
        const int xa = ((x0 % w) + w) % w, xb = (((x0 + 1) % w) + w) % w, ya = ((y0 % h) + h) % h, yb = (((y0 + 1) % h) + h) % h;
        const float a = lum[ya * w + xa] + (lum[ya * w + xb] - lum[ya * w + xa]) * fx, b = lum[yb * w + xa] + (lum[yb * w + xb] - lum[yb * w + xa]) * fx;
        map[y * MW + x] = a + (b - a) * fy;
    }
    for (int i = 0; i < 3; i++) blurPass(map, tmp, blurR);
    float lo = 1e9f, hi = -1e9f; for (float v : map) { lo = std::min(lo, v); hi = std::max(hi, v); }
    for (int i = 0; i < MW * MH; i++) { const float t = hi - lo > 1e-4f ? (map[i] - lo) / (hi - lo) : 0.5f; outMap[i * 4] = outMap[i * 4 + 1] = outMap[i * 4 + 2] = (uint8_t)(t * 255.0f + 0.5f); outMap[i * 4 + 3] = 255; }
    return true;
}

/* A stand-in for a level's water texture until a real capture exists: 64x64, tiling, grey-blue noise */
static inline void placeholder(int seed, std::vector<uint8_t>& px) {
    const int W = 64; px.assign(W * W * 4, 255);
    auto hsh = [&](int a, int b, int px_, int o) { a = ((a % px_) + px_) % px_; b = ((b % px_) + px_) % px_; unsigned h = (unsigned)(a * 374761393 + b * 668265263 + (seed + o) * 2246822519u); h = (h ^ (h >> 13)) * 1274126177u; return (float)(h & 0xffff) / 65535.0f; };
    for (int y = 0; y < W; y++) for (int x = 0; x < W; x++) {
        float v = 0, amp = 0.5f, nrm = 0;
        for (int o = 0; o < 4; o++) {
            const int f = 4 << o; const float fx0 = x / (float)W * f, fy0 = y / (float)W * f; const int ix = (int)floorf(fx0), iy = (int)floorf(fy0); float fx = fx0 - ix, fy = fy0 - iy;
            fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy);
            const float a = hsh(ix, iy, f, o), b = hsh(ix + 1, iy, f, o), c = hsh(ix, iy + 1, f, o), d = hsh(ix + 1, iy + 1, f, o);
            v += amp * (a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy); nrm += amp; amp *= 0.5f;
        }
        v /= nrm; v = 0.25f + 0.6f * v;
        uint8_t* o = &px[(y * W + x) * 4]; o[0] = (uint8_t)(255 * v * 0.72f); o[1] = (uint8_t)(255 * v * 0.88f); o[2] = (uint8_t)(255 * v * 1.0f);
    }
}

}  // namespace waterbake
