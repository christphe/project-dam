// Tiny image I/O for the PC viewer: read TGA (uncompressed or RLE, 24/32 bit) and PNG, write PNG and TGA. The PNG side is ge_assets'
// reader and writer (port/assets/ge_png.h). Pixels are RGBA8, top row first.
#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <string>
#include <vector>

extern "C" {
#include "ge_png.h"
}

namespace img {

struct Image { int w = 0, h = 0; std::vector<uint8_t> px; };   /* RGBA8, top row first */

static inline bool readAll(const std::string& p, std::vector<uint8_t>& out) {
    FILE* f = fopen(p.c_str(), "rb"); if (!f) return false;
    uint8_t b[8192]; size_t n; out.clear();
    while ((n = fread(b, 1, sizeof b, f))) out.insert(out.end(), b, b + n);
    fclose(f); return true;
}

/* ---- TGA ---- */
static inline bool loadTga(const std::vector<uint8_t>& d, Image& im) {
    if (d.size() < 18) return false;
    const int idl = d[0], type = d[2], w = d[12] | d[13] << 8, h = d[14] | d[15] << 8, bpp = d[16], desc = d[17];
    if ((type != 2 && type != 10) || (bpp != 24 && bpp != 32) || w <= 0 || h <= 0) return false;
    const int bytes = bpp / 8; size_t pos = 18 + idl;
    std::vector<uint8_t> raw((size_t)w * h * bytes);
    if (type == 2) { if (d.size() < pos + raw.size()) return false; memcpy(raw.data(), &d[pos], raw.size()); }
    else {
        size_t o = 0;
        while (o < raw.size() && pos < d.size()) {
            const int hd = d[pos++], n = (hd & 127) + 1;
            if (hd & 128) { if (pos + bytes > d.size()) return false; for (int i = 0; i < n && o < raw.size(); i++) { memcpy(&raw[o], &d[pos], bytes); o += bytes; } pos += bytes; }
            else { for (int i = 0; i < n && o < raw.size() && pos + bytes <= d.size(); i++) { memcpy(&raw[o], &d[pos], bytes); o += bytes; pos += bytes; } }
        }
    }
    im.w = w; im.h = h; im.px.assign((size_t)w * h * 4, 255);
    for (int y = 0; y < h; y++) {
        const int sy = (desc & 0x20) ? y : h - 1 - y;   /* bit 5: top-left origin */
        for (int x = 0; x < w; x++) {
            const uint8_t* s = &raw[((size_t)sy * w + x) * bytes]; uint8_t* o = &im.px[((size_t)y * w + x) * 4];
            o[0] = s[2]; o[1] = s[1]; o[2] = s[0]; o[3] = bytes == 4 ? s[3] : 255;
        }
    }
    return true;
}

static inline bool writeTga(const std::string& p, int w, int h, const uint8_t* rgbaTop) {
    FILE* f = fopen(p.c_str(), "wb"); if (!f) return false;
    uint8_t hd[18] = { 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, (uint8_t)(w & 255), (uint8_t)(w >> 8), (uint8_t)(h & 255), (uint8_t)(h >> 8), 32, 8 | 0x20 };
    fwrite(hd, 1, 18, f);
    std::vector<uint8_t> row((size_t)w * 4);
    for (int y = 0; y < h; y++) { for (int x = 0; x < w; x++) { const uint8_t* s = rgbaTop + ((size_t)y * w + x) * 4; row[x * 4] = s[2]; row[x * 4 + 1] = s[1]; row[x * 4 + 2] = s[0]; row[x * 4 + 3] = s[3]; } fwrite(row.data(), 1, row.size(), f); }
    fclose(f); return true;
}

/* ---- PNG: ge_assets' codec ---- */
static inline bool loadPng(const std::vector<uint8_t>& d, Image& im) {
    uint8_t* px = nullptr;
    if (!gePngDecode(d.data(), d.size(), &im.w, &im.h, &px)) return false;
    im.px.assign(px, px + (size_t)im.w * im.h * 4); free(px);
    return true;
}
/* RGB8 PNG (alpha dropped) */
static inline bool writePng(const std::string& p, int w, int h, const uint8_t* rgbaTop) { return gePngWrite(p.c_str(), w, h, rgbaTop, GE_PNG_RGB) != 0; }

static inline bool endsWithNoCase(const std::string& s, const char* ext) {
    const size_t n = strlen(ext); if (s.size() < n) return false;
    for (size_t i = 0; i < n; i++) if (tolower((unsigned char)s[s.size() - n + i]) != tolower((unsigned char)ext[i])) return false;
    return true;
}
static inline bool loadImage(const std::string& path, Image& im) {
    std::vector<uint8_t> d; if (!readAll(path, d)) return false;
    return endsWithNoCase(path, ".tga") ? loadTga(d, im) : loadPng(d, im);
}
static inline bool saveImage(const std::string& path, int w, int h, const uint8_t* rgbaTop) {
    return endsWithNoCase(path, ".tga") ? writeTga(path, w, h, rgbaTop) : writePng(path, w, h, rgbaTop);
}

}  // namespace img
