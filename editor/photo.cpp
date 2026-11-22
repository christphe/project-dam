#include "photo.h"

#include <algorithm>
#include <cmath>

namespace {

struct Rng {   // xorshift64*, seeded from the image
    uint64_t s;
    explicit Rng(uint64_t seed) : s(seed ? seed : 0x9e3779b97f4a7c15ull) {}
    uint64_t next()
    {
        s ^= s >> 12; s ^= s << 25; s ^= s >> 27;
        return s * 2685821657736338717ull;
    }
    float unit() { return (float)(next() >> 40) / (float)(1u << 24); }   // [0, 1)
    int range(int n) { return (int)(next() % (uint64_t)n); }
};

float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

// per-pixel hash noise in [-1, 1]
float grain(int x, int y, uint64_t seed)
{
    uint64_t h = (uint64_t)x * 0x9e3779b185ebca87ull ^ (uint64_t)y * 0xc2b2ae3d27d4eb4full ^ seed;
    h ^= h >> 33; h *= 0xff51afd7ed558ccdull; h ^= h >> 33; h *= 0xc4ceb9fe1a85ec53ull; h ^= h >> 33;
    return (float)(h & 0xffff) / 32767.5f - 1.0f;
}

} // namespace

void applyOldPhoto(std::vector<uint8_t> &rgba, int w, int h, float intensity)
{
    if (intensity <= 0 || w < 16 || h < 16 || rgba.size() < (size_t)w * h * 4) return;
    const float k = std::min(intensity, 1.5f);
    const size_t n = (size_t)w * h;

    uint64_t seed = 1469598103934665603ull;   // FNV over the pixels: same view, same file
    for (size_t i = 0; i < n * 4; i += 4) { seed ^= rgba[i] | (rgba[i + 1] << 8) | (rgba[i + 2] << 16); seed *= 1099511628211ull; }
    Rng rng(seed);

    std::vector<float> src(n * 3), tmp(n * 3), blur(n * 3);
    for (size_t i = 0; i < n; i++)
        for (int c = 0; c < 3; c++) src[i * 3 + c] = rgba[i * 4 + c] / 255.0f;

    // about 1 px of blur: [1 2 1] / 4, both ways
    auto at = [&](const std::vector<float> &v, int x, int y, int c) {
        x = std::max(0, std::min(w - 1, x)); y = std::max(0, std::min(h - 1, y));
        return v[((size_t)y * w + x) * 3 + c];
    };
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            for (int c = 0; c < 3; c++)
                tmp[((size_t)y * w + x) * 3 + c] = (at(src, x - 1, y, c) + 2 * at(src, x, y, c) + at(src, x + 1, y, c)) * 0.25f;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            for (int c = 0; c < 3; c++)
                blur[((size_t)y * w + x) * 3 + c] = (at(tmp, x, y - 1, c) + 2 * at(tmp, x, y, c) + at(tmp, x, y + 1, c)) * 0.25f;

    const float cx = (w - 1) * 0.5f, cy = (h - 1) * 0.5f;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const size_t i = (size_t)y * w + x;
            float rgb[3];
            for (int c = 0; c < 3; c++) rgb[c] = src[i * 3 + c] + (blur[i * 3 + c] - src[i * 3 + c]) * std::min(k, 1.0f);
            // warm sepia from the luminance, blended with the colour
            const float L = 0.299f * rgb[0] + 0.587f * rgb[1] + 0.114f * rgb[2];
            const float sep[3] = { L * 1.08f + 0.02f, L * 0.90f + 0.01f, L * 0.68f };
            const float m = std::min(0.92f * k, 0.97f);
            for (int c = 0; c < 3; c++) rgb[c] = rgb[c] + (sep[c] - rgb[c]) * m;
            // gentle tone curve: softer contrast, lifted blacks, a slightly rolled-off top
            for (int c = 0; c < 3; c++) {
                float v = clamp01(rgb[c]);
                const float soft = 0.5f + (v - 0.5f) * 0.86f;
                v = v + (0.05f + soft * 0.90f - v) * std::min(k, 1.0f);
                rgb[c] = v;
            }
            // vignette
            const float dx = (x - cx) / (w * 0.5f), dy = (y - cy) / (h * 0.5f);
            const float d2 = (dx * dx + dy * dy) * 0.5f;
            const float vig = 1.0f - 0.55f * k * d2 * d2 - 0.12f * k * d2;
            // film grain, monochrome
            const float g = grain(x, y, seed) * 0.045f * k;
            for (int c = 0; c < 3; c++) rgb[c] = clamp01(rgb[c] * vig + g);
            for (int c = 0; c < 3; c++) src[i * 3 + c] = rgb[c];   // src now holds the result
        }
    }

    // faint scratches: a few vertical hairlines, broken up
    const int scratches = 3 + rng.range(3);
    for (int s = 0; s < scratches; s++) {
        const int x = 8 + rng.range(w - 16), y0 = rng.range(h / 2), len = h / 4 + rng.range(h / 2);
        const float a = (0.10f + 0.10f * rng.unit()) * std::min(k, 1.0f);
        for (int y = y0; y < std::min(h, y0 + len); y++) {
            if (rng.range(7) == 0) continue;   // gaps
            const int xx = x + (int)lroundf(sinf(y * 0.05f + s) * 0.7f);
            for (int c = 0; c < 3; c++) {
                float &v = src[((size_t)y * w + std::max(0, std::min(w - 1, xx))) * 3 + c];
                v = clamp01(v + (1.0f - v) * a);
            }
        }
    }
    // dust specks: light and dark dots of one or two pixels
    const int specks = (int)(30 * k);
    for (int s = 0; s < specks; s++) {
        const int x = rng.range(w), y = rng.range(h), sz = 1 + rng.range(2);
        const bool light = rng.range(3) != 0;
        const float a = (0.15f + 0.25f * rng.unit()) * std::min(k, 1.0f);
        for (int yy = y; yy < std::min(h, y + sz); yy++)
            for (int xx = x; xx < std::min(w, x + sz); xx++)
                for (int c = 0; c < 3; c++) {
                    float &v = src[((size_t)yy * w + xx) * 3 + c];
                    v = light ? v + (1.0f - v) * a : v * (1.0f - a);
                }
    }

    // thin off-white print border with a slightly uneven inner edge
    const float bw = 7.0f * std::min(k, 1.0f) + (k > 1 ? 2.0f * (k - 1) : 0);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            const float edge = std::min(std::min((float)x, (float)(w - 1 - x)), std::min((float)y, (float)(h - 1 - y)));
            const float wob = (grain(x / 3, y / 3, seed ^ 0x5bd1e995ull)) * 0.6f * std::min(k, 1.0f);
            if (edge < bw + wob) {
                const size_t i = (size_t)y * w + x;
                const float paper[3] = { 0.93f, 0.90f, 0.80f };
                const float tex = grain(x, y, seed ^ 0x1234567ull) * 0.02f;
                for (int c = 0; c < 3; c++) src[i * 3 + c] = clamp01(paper[c] + tex);
            }
        }

    for (size_t i = 0; i < n; i++)
        for (int c = 0; c < 3; c++) rgba[i * 4 + c] = (uint8_t)lroundf(clamp01(src[i * 3 + c]) * 255.0f);
}
