// modelview: a dependency-free PNG writer (RGB8, stored deflate). Copied from seaview's img.h.
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string>
#include <vector>

namespace mvpng {

static inline uint32_t crc32(const uint8_t* p, size_t n) {
    static uint32_t tab[256]; static bool init = false;
    if (!init) { for (uint32_t i = 0; i < 256; i++) { uint32_t v = i; for (int k = 0; k < 8; k++) v = v & 1 ? 0xEDB88320u ^ (v >> 1) : v >> 1; tab[i] = v; } init = true; }
    uint32_t c = ~0u; for (size_t i = 0; i < n; i++) c = tab[(c ^ p[i]) & 255] ^ (c >> 8); return ~c;
}
static inline void put32(std::vector<uint8_t>& v, uint32_t x) { v.push_back(x >> 24); v.push_back(x >> 16); v.push_back(x >> 8); v.push_back(x); }
static inline void chunk(FILE* f, const char* type, const std::vector<uint8_t>& data) {
    std::vector<uint8_t> b; put32(b, (uint32_t)data.size()); b.insert(b.end(), type, type + 4); b.insert(b.end(), data.begin(), data.end());
    uint32_t c = crc32(b.data() + 4, b.size() - 4); put32(b, c); fwrite(b.data(), 1, b.size(), f);
}
// rgbaTop: w*h RGBA, first row on top
static inline bool write(const std::string& p, int w, int h, const uint8_t* rgbaTop) {
    FILE* f = fopen(p.c_str(), "wb"); if (!f) return false;
    static const uint8_t sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 }; fwrite(sig, 1, 8, f);
    std::vector<uint8_t> ih; put32(ih, w); put32(ih, h); ih.push_back(8); ih.push_back(2); ih.push_back(0); ih.push_back(0); ih.push_back(0); chunk(f, "IHDR", ih);
    std::vector<uint8_t> raw; raw.reserve((size_t)h * (w * 3 + 1));
    for (int y = 0; y < h; y++) { raw.push_back(0); for (int x = 0; x < w; x++) { const uint8_t* s = rgbaTop + ((size_t)y * w + x) * 4; raw.push_back(s[0]); raw.push_back(s[1]); raw.push_back(s[2]); } }
    std::vector<uint8_t> z; z.push_back(0x78); z.push_back(0x01);
    for (size_t o = 0; o < raw.size() || o == 0; o += 65535) {
        const size_t n = raw.size() - o < 65535 ? raw.size() - o : 65535; const bool last = o + n >= raw.size();
        z.push_back(last ? 1 : 0); z.push_back(n & 255); z.push_back(n >> 8); z.push_back(~n & 255); z.push_back((~n >> 8) & 255); z.insert(z.end(), raw.begin() + o, raw.begin() + o + n);
        if (last) break;
    }
    uint32_t a = 1, b2 = 0; for (uint8_t v : raw) { a = (a + v) % 65521; b2 = (b2 + a) % 65521; } put32(z, b2 << 16 | a);
    chunk(f, "IDAT", z); chunk(f, "IEND", {}); fclose(f); return true;
}

}  // namespace mvpng
