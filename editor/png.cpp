// A minimal PNG writer (RGB8, per-row filter, zlib level 9) and reader: --screenshot and thumb.png.
#include "png.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include <zlib.h>

static uint32_t crcTable[256];
static void crcInit()
{
    if (crcTable[1]) return;
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++) c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1;
        crcTable[n] = c;
    }
}
static uint32_t crc(uint32_t c, const uint8_t *p, size_t n)
{
    for (size_t i = 0; i < n; i++) c = crcTable[(c ^ p[i]) & 0xff] ^ (c >> 8);
    return c;
}

static void put32(std::vector<uint8_t> &v, uint32_t x)
{
    v.push_back((uint8_t)(x >> 24)); v.push_back((uint8_t)(x >> 16)); v.push_back((uint8_t)(x >> 8)); v.push_back((uint8_t)x);
}

static void chunk(FILE *f, const char *type, const std::vector<uint8_t> &data)
{
    std::vector<uint8_t> hdr;
    put32(hdr, (uint32_t)data.size());
    fwrite(hdr.data(), 1, 4, f);
    uint32_t c = 0xffffffffu;
    c = crc(c, (const uint8_t *)type, 4);
    fwrite(type, 1, 4, f);
    if (!data.empty()) { c = crc(c, data.data(), data.size()); fwrite(data.data(), 1, data.size(), f); }
    std::vector<uint8_t> t; put32(t, c ^ 0xffffffffu);
    fwrite(t.data(), 1, 4, f);
}

bool writePng(const char *path, int w, int h, const uint8_t *rgba)
{
    crcInit();
    // RGB8 rows, each with the PNG filter that leaves the smallest sum of absolute bytes (the usual heuristic), then
    // zlib at level 9: a 320x240 thumb.png goes from ~230 KB (stored blocks) to a few tens of KB. The game's reader
    // (port/src/missions_thumb.c) unfilters all five types.
    const size_t stride = (size_t)w * 3;
    std::vector<uint8_t> rgb(stride * h);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            const uint8_t *q = rgba + ((size_t)y * w + x) * 4;
            uint8_t *d = &rgb[(size_t)y * stride + (size_t)x * 3];
            d[0] = q[0]; d[1] = q[1]; d[2] = q[2];
        }
    std::vector<uint8_t> raw;
    raw.reserve((stride + 1) * h);
    std::vector<uint8_t> cand[5];
    for (auto &c : cand) c.resize(stride);
    for (int y = 0; y < h; y++) {
        const uint8_t *row = &rgb[(size_t)y * stride], *up = y ? row - stride : nullptr;
        size_t best = 0, bestSum = (size_t)-1;
        for (int ft = 0; ft < 5; ft++) {
            size_t sum = 0;
            for (size_t x = 0; x < stride; x++) {
                const int a = x >= 3 ? row[x - 3] : 0, b = up ? up[x] : 0, c = (up && x >= 3) ? up[x - 3] : 0;
                int pr = 0;
                switch (ft) {
                case 1: pr = a; break;
                case 2: pr = b; break;
                case 3: pr = (a + b) / 2; break;
                case 4: { const int pp = a + b - c, pa = abs(pp - a), pb = abs(pp - b), pc = abs(pp - c);
                          pr = (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c); break; }
                default: break;
                }
                const uint8_t v = (uint8_t)(row[x] - pr);
                cand[ft][x] = v;
                sum += v < 128 ? v : 256 - v;
            }
            if (sum < bestSum) { bestSum = sum; best = (size_t)ft; }
        }
        raw.push_back((uint8_t)best);
        raw.insert(raw.end(), cand[best].begin(), cand[best].end());
    }
    uLongf zlen = compressBound((uLong)raw.size());
    std::vector<uint8_t> z(zlen);
    if (compress2(z.data(), &zlen, raw.data(), (uLong)raw.size(), 9) != Z_OK) return false;
    z.resize(zlen);

    FILE *f = fopen(path, "wb");
    if (!f) return false;
    static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a };
    fwrite(sig, 1, 8, f);
    std::vector<uint8_t> ihdr;
    put32(ihdr, (uint32_t)w); put32(ihdr, (uint32_t)h);
    ihdr.push_back(8); ihdr.push_back(2); ihdr.push_back(0); ihdr.push_back(0); ihdr.push_back(0);
    chunk(f, "IHDR", ihdr);
    chunk(f, "IDAT", z);
    chunk(f, "IEND", {});
    fclose(f);
    return true;
}


bool readPng(const char *path, int &w, int &h, std::vector<uint8_t> &rgba)
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    std::vector<uint8_t> file;
    uint8_t buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) file.insert(file.end(), buf, buf + n);
    fclose(f);
    static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a };
    if (file.size() < 33 || memcmp(file.data(), sig, 8) != 0) return false;
    size_t pos = 8;
    std::vector<uint8_t> idat, plte, trns;
    int depth = 0, ctype = 0, inter = 0;
    w = h = 0;
    while (pos + 12 <= file.size()) {
        uint32_t len = (uint32_t)file[pos] << 24 | (uint32_t)file[pos + 1] << 16 | (uint32_t)file[pos + 2] << 8 | file[pos + 3];
        const uint8_t *type = &file[pos + 4], *d = &file[pos + 8];
        if (pos + 12 + len > file.size()) return false;
        if (!memcmp(type, "IHDR", 4) && len >= 13) {
            w = (int)((uint32_t)d[0] << 24 | (uint32_t)d[1] << 16 | (uint32_t)d[2] << 8 | d[3]);
            h = (int)((uint32_t)d[4] << 24 | (uint32_t)d[5] << 16 | (uint32_t)d[6] << 8 | d[7]);
            depth = d[8]; ctype = d[9]; inter = d[12];
        } else if (!memcmp(type, "IDAT", 4)) idat.insert(idat.end(), d, d + len);
        else if (!memcmp(type, "PLTE", 4)) plte.assign(d, d + len);
        else if (!memcmp(type, "tRNS", 4)) trns.assign(d, d + len);
        else if (!memcmp(type, "IEND", 4)) break;
        pos += 12 + len;
    }
    // colour types: 0 grey, 2 RGB, 3 palette, 4 grey + alpha, 6 RGBA (8 bits per sample; an imported picture can be any of them)
    static const int kBpp[7] = { 1, 0, 3, 1, 2, 0, 4 };
    if (w <= 0 || h <= 0 || w > 8192 || h > 8192 || depth != 8 || inter != 0 || ctype < 0 || ctype > 6 || !kBpp[ctype]) return false;
    if (ctype == 3 && (plte.size() < 3 || plte.size() % 3)) return false;
    const int bpp = kBpp[ctype];
    const size_t stride = (size_t)w * bpp;
    std::vector<uint8_t> raw((stride + 1) * (size_t)h);
    uLongf outLen = (uLongf)raw.size();
    if (uncompress(raw.data(), &outLen, idat.data(), (uLong)idat.size()) != Z_OK || outLen != raw.size()) return false;
    std::vector<uint8_t> prev(stride, 0), cur(stride);
    rgba.assign((size_t)w * h * 4, 255);
    for (int y = 0; y < h; y++) {
        const uint8_t *row = &raw[(stride + 1) * (size_t)y];
        const int ft = row[0];
        for (size_t i = 0; i < stride; i++) {
            int a = i >= (size_t)bpp ? cur[i - bpp] : 0, b = prev[i], c = i >= (size_t)bpp ? prev[i - bpp] : 0, x = row[1 + i], v;
            switch (ft) {
            case 0: v = x; break;
            case 1: v = x + a; break;
            case 2: v = x + b; break;
            case 3: v = x + ((a + b) >> 1); break;
            case 4: {
                int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
                v = x + (pa <= pb && pa <= pc ? a : pb <= pc ? b : c);
                break;
            }
            default: return false;
            }
            cur[i] = (uint8_t)v;
        }
        for (int x = 0; x < w; x++) {
            uint8_t *o = &rgba[((size_t)y * w + x) * 4];
            const uint8_t *s = &cur[(size_t)x * bpp];
            switch (ctype) {
            case 0: o[0] = o[1] = o[2] = s[0]; break;
            case 4: o[0] = o[1] = o[2] = s[0]; o[3] = s[1]; break;
            case 3: {
                const size_t k = (size_t)s[0] * 3 < plte.size() ? s[0] : 0;
                o[0] = plte[k * 3]; o[1] = plte[k * 3 + 1]; o[2] = plte[k * 3 + 2];
                o[3] = k < trns.size() ? trns[k] : 255;
                break;
            }
            default: o[0] = s[0]; o[1] = s[1]; o[2] = s[2]; o[3] = ctype == 6 ? s[3] : 255; break;
            }
        }
        prev = cur;
    }
    return true;
}
