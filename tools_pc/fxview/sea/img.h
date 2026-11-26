// Tiny image I/O for the PC viewer: read TGA (uncompressed or RLE, 24/32 bit) and PNG (8 bit gray/RGB/RGBA/gray+alpha,
// not interlaced; own inflate), write PNG (stored deflate blocks) and TGA. No dependency. Pixels are RGBA8, top row first.
#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <string>
#include <vector>

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

/* ---- inflate (RFC 1951), enough for PNG ---- */
struct Bits { const uint8_t* p; size_t n, pos = 0; uint32_t buf = 0; int cnt = 0;
    int get(int k) { while (cnt < k) { if (pos >= n) return -1; buf |= (uint32_t)p[pos++] << cnt; cnt += 8; } int v = buf & ((1u << k) - 1); buf >>= k; cnt -= k; return v; } };
struct Huff { uint16_t count[16], sym[288]; };
static inline void huffBuild(Huff& h, const uint8_t* len, int n) {
    memset(h.count, 0, sizeof h.count); for (int i = 0; i < n; i++) h.count[len[i]]++; h.count[0] = 0;
    uint16_t offs[16]; offs[1] = 0; for (int i = 1; i < 15; i++) offs[i + 1] = offs[i] + h.count[i];
    for (int i = 0; i < n; i++) if (len[i]) h.sym[offs[len[i]]++] = (uint16_t)i;
}
static inline int huffDecode(Bits& b, const Huff& h) {
    int code = 0, first = 0, index = 0;
    for (int l = 1; l < 16; l++) { int bit = b.get(1); if (bit < 0) return -1; code |= bit; int c = h.count[l];
        if (code - c < first) return h.sym[index + (code - first)]; index += c; first += c; first <<= 1; code <<= 1; }
    return -1;
}
static inline bool inflate(const uint8_t* src, size_t n, std::vector<uint8_t>& out) {
    static const uint16_t lb[] = { 3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258 };
    static const uint16_t le[] = { 0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0 };
    static const uint16_t db[] = { 1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577 };
    static const uint16_t de[] = { 0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13 };
    Bits b{ src, n }; int last;
    do {
        last = b.get(1); int type = b.get(2); if (last < 0 || type < 0) return false;
        if (type == 0) {
            b.buf = 0; b.cnt = 0; if (b.pos + 4 > n) return false;
            size_t len = src[b.pos] | src[b.pos + 1] << 8; b.pos += 4; if (b.pos + len > n) return false;
            out.insert(out.end(), src + b.pos, src + b.pos + len); b.pos += len;
        } else if (type == 1 || type == 2) {
            Huff lh, dh; uint8_t lens[320];
            if (type == 1) { int i = 0; for (; i < 144; i++) lens[i] = 8; for (; i < 256; i++) lens[i] = 9; for (; i < 280; i++) lens[i] = 7; for (; i < 288; i++) lens[i] = 8; huffBuild(lh, lens, 288); for (i = 0; i < 30; i++) lens[i] = 5; huffBuild(dh, lens, 30); }
            else {
                int nl = b.get(5) + 257, nd = b.get(5) + 1, nc = b.get(4) + 4; if (nl < 257 || nd < 1 || nc < 4) return false;
                static const uint8_t ord[19] = { 16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15 };
                uint8_t cl[19] = {}; for (int i = 0; i < nc; i++) { int v = b.get(3); if (v < 0) return false; cl[ord[i]] = (uint8_t)v; }
                Huff ch; huffBuild(ch, cl, 19); int i = 0;
                while (i < nl + nd) {
                    int s = huffDecode(b, ch); if (s < 0) return false;
                    if (s < 16) lens[i++] = (uint8_t)s;
                    else { int rep, v = 0; if (s == 16) { if (!i) return false; v = lens[i - 1]; rep = 3 + b.get(2); } else if (s == 17) rep = 3 + b.get(3); else rep = 11 + b.get(7);
                        if (i + rep > nl + nd) return false; while (rep--) lens[i++] = (uint8_t)v; }
                }
                huffBuild(lh, lens, nl); huffBuild(dh, lens + nl, nd);
            }
            for (;;) {
                int s = huffDecode(b, lh); if (s < 0) return false;
                if (s < 256) out.push_back((uint8_t)s); else if (s == 256) break;
                else { s -= 257; if (s >= 29) return false; int len = lb[s] + b.get(le[s]); int ds = huffDecode(b, dh); if (ds < 0 || ds >= 30) return false;
                    size_t dist = db[ds] + b.get(de[ds]); if (dist > out.size()) return false; for (int k = 0; k < len; k++) out.push_back(out[out.size() - dist]); }
            }
        } else return false;
    } while (!last);
    return true;
}

/* ---- PNG ---- */
static inline bool loadPng(const std::vector<uint8_t>& d, Image& im) {
    static const uint8_t sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    if (d.size() < 33 || memcmp(d.data(), sig, 8)) return false;
    size_t pos = 8; int w = 0, h = 0, depth = 0, ct = 0, il = 0; std::vector<uint8_t> idat;
    while (pos + 8 <= d.size()) {
        const uint32_t len = d[pos] << 24 | d[pos + 1] << 16 | d[pos + 2] << 8 | d[pos + 3]; const char* t = (const char*)&d[pos + 4]; const uint8_t* c = &d[pos + 8];
        if (pos + 12 + len > d.size()) return false;
        if (!memcmp(t, "IHDR", 4)) { w = c[0] << 24 | c[1] << 16 | c[2] << 8 | c[3]; h = c[4] << 24 | c[5] << 16 | c[6] << 8 | c[7]; depth = c[8]; ct = c[9]; il = c[12]; }
        else if (!memcmp(t, "IDAT", 4)) idat.insert(idat.end(), c, c + len);
        else if (!memcmp(t, "IEND", 4)) break;
        pos += 12 + len;
    }
    if (depth != 8 || il != 0 || w <= 0 || h <= 0 || idat.size() < 6) return false;
    const int ch = ct == 0 ? 1 : ct == 2 ? 3 : ct == 4 ? 2 : ct == 6 ? 4 : 0; if (!ch) return false;   /* palette: not supported */
    std::vector<uint8_t> raw; if (!inflate(idat.data() + 2, idat.size() - 2, raw)) return false;
    const size_t stride = (size_t)w * ch; if (raw.size() < (stride + 1) * h) return false;
    std::vector<uint8_t> pix((size_t)w * h * ch);
    for (int y = 0; y < h; y++) {
        const int f = raw[y * (stride + 1)]; const uint8_t* s = &raw[y * (stride + 1) + 1]; uint8_t* o = &pix[y * stride]; const uint8_t* up = y ? o - stride : nullptr;
        for (size_t x = 0; x < stride; x++) {
            const int a = x >= (size_t)ch ? o[x - ch] : 0, bb = up ? up[x] : 0, c = (up && x >= (size_t)ch) ? up[x - ch] : 0; int v = s[x];
            if (f == 1) v += a; else if (f == 2) v += bb; else if (f == 3) v += (a + bb) / 2;
            else if (f == 4) { int p = a + bb - c, pa = abs(p - a), pb = abs(p - bb), pc = abs(p - c); v += (pa <= pb && pa <= pc) ? a : (pb <= pc ? bb : c); }
            o[x] = (uint8_t)v;
        }
    }
    im.w = w; im.h = h; im.px.assign((size_t)w * h * 4, 255);
    for (size_t i = 0; i < (size_t)w * h; i++) {
        const uint8_t* s = &pix[i * ch]; uint8_t* o = &im.px[i * 4];
        if (ch <= 2) { o[0] = o[1] = o[2] = s[0]; if (ch == 2) o[3] = s[1]; } else { o[0] = s[0]; o[1] = s[1]; o[2] = s[2]; if (ch == 4) o[3] = s[3]; }
    }
    return true;
}

static inline uint32_t crc32(const uint8_t* p, size_t n, uint32_t c = 0) {
    static uint32_t tab[256]; static bool init = false;
    if (!init) { for (uint32_t i = 0; i < 256; i++) { uint32_t v = i; for (int k = 0; k < 8; k++) v = v & 1 ? 0xEDB88320u ^ (v >> 1) : v >> 1; tab[i] = v; } init = true; }
    c = ~c; for (size_t i = 0; i < n; i++) c = tab[(c ^ p[i]) & 255] ^ (c >> 8); return ~c;
}
static inline void put32(std::vector<uint8_t>& v, uint32_t x) { v.push_back(x >> 24); v.push_back(x >> 16); v.push_back(x >> 8); v.push_back(x); }
static inline void chunk(FILE* f, const char* type, const std::vector<uint8_t>& data) {
    std::vector<uint8_t> b; put32(b, (uint32_t)data.size()); b.insert(b.end(), type, type + 4); b.insert(b.end(), data.begin(), data.end());
    uint32_t c = crc32(b.data() + 4, b.size() - 4); put32(b, c); fwrite(b.data(), 1, b.size(), f);
}
/* RGB8 PNG (alpha dropped), stored (uncompressed) deflate: big but dependency-free */
static inline bool writePng(const std::string& p, int w, int h, const uint8_t* rgbaTop) {
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
