// modelview: see mv_tex.h.
#include "mv_tex.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static inline uint8_t e5(uint32_t x) { return (uint8_t)((x << 3) | (x >> 2)); }

void mv_tex_value_rgba(int format, uint32_t v, uint8_t o[4]) {
    switch (format) {
    case MVT_RGBA32: o[0] = (uint8_t)(v >> 24); o[1] = (uint8_t)(v >> 16); o[2] = (uint8_t)(v >> 8); o[3] = (uint8_t)v; break;
    case MVT_RGBA16: case MVT_CI8_RGBA16: case MVT_CI4_RGBA16: o[0] = e5((v >> 11) & 31); o[1] = e5((v >> 6) & 31); o[2] = e5((v >> 1) & 31); o[3] = (v & 1) ? 255 : 0; break;
    case MVT_RGB24: o[0] = (uint8_t)(v >> 16); o[1] = (uint8_t)(v >> 8); o[2] = (uint8_t)v; o[3] = 255; break;
    case MVT_RGB15: o[0] = e5((v >> 10) & 31); o[1] = e5((v >> 5) & 31); o[2] = e5(v & 31); o[3] = 255; break;
    case MVT_IA16: case MVT_CI8_IA16: case MVT_CI4_IA16: o[0] = o[1] = o[2] = (uint8_t)(v >> 8); o[3] = (uint8_t)v; break;
    case MVT_IA8: o[0] = o[1] = o[2] = (uint8_t)(((v >> 4) & 15) * 17); o[3] = (uint8_t)((v & 15) * 17); break;
    case MVT_IA4: o[0] = o[1] = o[2] = (uint8_t)(((v >> 1) & 7) * 255 / 7); o[3] = (v & 1) ? 255 : 0; break;
    case MVT_I8: o[0] = o[1] = o[2] = o[3] = (uint8_t)v; break;
    case MVT_I4: o[0] = o[1] = o[2] = o[3] = (uint8_t)((v & 15) * 17); break;
    default: o[0] = o[1] = o[2] = 255; o[3] = 255; break;
    }
}

static int bitsOf(int f) { static const int b[13] = { 32, 16, 24, 15, 16, 8, 4, 8, 4, 8, 4, 8, 4 }; return f >= 0 && f < 13 ? b[f] : 0; }

bool mv_tex_decode_packed(int fmt, const uint8_t* d, size_t len, int w, int h, const uint16_t* pal, int npal, std::vector<uint8_t>& rgba) {
    rgba.assign((size_t)w * h * 4, 0);
    const bool ci = fmt >= MVT_CI8_RGBA16;
    const int bits = bitsOf(fmt);
    if (!bits) return false;
    size_t pos = 0; int nib = 0;   // byte position, 0/1 = high/low nibble
    for (int y = 0; y < h; y++) {
        if (bits == 4 && nib) { nib = 0; pos++; }   // rows start on a byte
        for (int x = 0; x < w; x++) {
            uint32_t v = 0;
            if (bits == 4) { if (pos >= len) return false; v = nib ? (d[pos] & 15) : (d[pos] >> 4); if (nib) { nib = 0; pos++; } else nib = 1; }
            else { const int nb = (bits + 7) / 8; if (pos + nb > len) return false; for (int k = 0; k < nb; k++) v = (v << 8) | d[pos + k]; pos += nb; }
            uint8_t* o = &rgba[((size_t)y * w + x) * 4];
            if (ci) { const uint32_t pv = v < (uint32_t)npal ? pal[v] : 0; mv_tex_value_rgba(fmt, pv, o); }
            else mv_tex_value_rgba(fmt, v, o);
        }
    }
    return true;
}

// ---- the lookup codecs (texdecode.py, ported from the game's image.c)
namespace {
struct Bits {
    const uint8_t* d; size_t n, p; uint64_t acc = 0; int cnt = 0; bool bad = false;
    Bits(const uint8_t* dd, size_t nn, size_t pp) : d(dd), n(nn), p(pp) {}
    uint32_t read(int k) {
        while (cnt < k) { if (p >= n) { bad = true; acc <<= 8; } else acc = (acc << 8) | d[p]; p++; cnt += 8; }
        cnt -= k; return k ? (uint32_t)((acc >> cnt) & ((1ull << k) - 1)) : 0;
    }
};
bool huffman(Bits& bs, int numit, int chansize, std::vector<int>& out) {
    const int NB = 2048;
    std::vector<int> freq(NB, 0), n0(NB, -1), n1(NB, -1);
    for (int i = 0; i < chansize; i++) freq[(size_t)i] = (int)bs.read(8);
    auto two = [&](int& f1, int& i1, int& f2, int& i2) {
        f1 = f2 = 9999; i1 = i2 = -1;
        for (int i = 0; i < chansize; i++) {
            if (freq[(size_t)i] < f1) { if (f2 < f1) { f1 = freq[(size_t)i]; i1 = i; } else { f2 = freq[(size_t)i]; i2 = i; } }
            else if (freq[(size_t)i] < f2) { f2 = freq[(size_t)i]; i2 = i; }
        }
    };
    int f1, i1, f2, i2, root = 0; two(f1, i1, f2, i2);
    while (!(f1 == 9999 || f2 == 9999)) {
        const int sum = freq[(size_t)i1] + freq[(size_t)i2], s = sum ? sum : 1;
        freq[(size_t)i1] = 9999; freq[(size_t)i2] = 9999;
        const bool leaf1 = n0[(size_t)i1] < 0 && n1[(size_t)i1] < 0, leaf2 = n0[(size_t)i2] < 0 && n1[(size_t)i2] < 0;
        if (leaf1) { n0[(size_t)i1] = i1 + 10000; root = i1; freq[(size_t)i1] = s; n1[(size_t)i1] = leaf2 ? i2 + 10000 : i2; }
        else if (leaf2) { n0[(size_t)i2] = i2 + 10000; root = i2; freq[(size_t)i2] = s; n1[(size_t)i2] = leaf1 ? i1 + 10000 : i1; }
        else {
            root = 0;
            while (n0[(size_t)root] >= 0 || n1[(size_t)root] >= 0 || freq[(size_t)root] < 9999) { root++; if (root >= NB - 1) return false; }
            freq[(size_t)root] = s; n0[(size_t)root] = i1; n1[(size_t)root] = i2;
        }
        two(f1, i1, f2, i2);
    }
    out.clear();
    for (int i = 0; i < numit; i++) {
        int v = root, guard = 0;
        while (v < 10000) { v = bs.read(1) ? n1[(size_t)v] : n0[(size_t)v]; if (v < 0 || ++guard > 4096 || bs.bad) return false; }
        out.push_back(v - 10000);
    }
    return true;
}
bool rle(Bits& bs, int total, std::vector<int>& out) {
    const int bt = (int)bs.read(3), rl = (int)bs.read(3), blk = (int)bs.read(4);
    int cost = bt + rl + blk + 1, fudge = 0;
    while (cost > 0) { cost -= blk + 1; fudge++; }
    out.clear();
    long guard = (long)total * 4 + 64;
    while ((int)out.size() < total && guard-- > 0 && !bs.bad) {
        if (bs.read(1) == 0) out.push_back((int)bs.read(blk));
        else {
            const long start = (long)out.size() - (long)bs.read(bt) - 1; const int run = (int)bs.read(rl) + fudge;
            for (int k = 0; k < run; k++) out.push_back(start + k >= 0 && start + k < (long)out.size() ? out[(size_t)(start + k)] : 0);
            out.push_back((int)bs.read(blk));
        }
    }
    out.resize((size_t)total);
    return !bs.bad;
}
// image.c texBlur (compression method 8: Huffman coded channels, then a predictor per pixel)
void blur(std::vector<int>& px, int width, int height, int method, int chansize) {
    for (int y = 0; y < height; y++) for (int x = 0; x < width; x++) {
        const int cur = px[(size_t)(y * width + x)] + chansize * 2;
        const int left = x > 0 ? px[(size_t)(y * width + x - 1)] : 0;
        const int above = y > 0 ? px[(size_t)((y - 1) * width + x)] : 0;
        const int aboveleft = x > 0 && y > 0 ? px[(size_t)((y - 1) * width + x - 1)] : 0;
        int v = 0;
        switch (method) {
            case 0: v = cur + left; break;
            case 1: v = cur + above; break;
            case 2: v = cur + aboveleft; break;
            case 3: v = cur + (left + above - aboveleft); break;
            case 4: v = cur + ((above - aboveleft) / 2 + left); break;
            case 5: v = cur + ((left - aboveleft) / 2 + above); break;
            case 6: v = cur + ((left + above) / 2); break;
            default: continue;
        }
        px[(size_t)(y * width + x)] = v % chansize;
    }
}
std::vector<std::pair<int, int>> g_list;   // (offset, size) per texture number
}  // namespace

bool mv_tex_set_imagelist(const std::string& path) {
    FILE* f = fopen(path.c_str(), "r"); if (!f) return false;
    char line[512]; g_list.clear();
    while (fgets(line, sizeof line, f)) { int o = 0, s = 0; if (sscanf(line, "%d,%d", &o, &s) == 2) g_list.push_back({ o, s }); }
    fclose(f); return !g_list.empty();
}

bool mv_tex_load(const uint8_t* rom, size_t romSize, int tn, MvTexImage& out) {
    out = MvTexImage();
    if (tn < 0 || tn >= (int)g_list.size()) { out.note = "texture number outside the image list"; return false; }
    const size_t off = (size_t)g_list[(size_t)tn].first, size = (size_t)g_list[(size_t)tn].second;
    if (off + size > romSize || size < 5) { out.note = "bad image list entry"; return false; }
    const uint8_t* b = rom + off;
    const int ctrl = b[0]; const bool zlib = (ctrl >> 6) & 1;
    if (zlib) {   // texInflateZlib: format, ncol-1, palette (16 bit BE), then per image w, h and an RZ-style deflate stream
        const int fmt = b[1], ncol = b[2] + 1;
        if (fmt < MVT_CI8_RGBA16 || fmt > MVT_CI4_IA16) { out.note = "zlib texture with a non-paletted format"; return false; }
        std::vector<uint16_t> pal((size_t)ncol);
        size_t p = 3;
        for (int i = 0; i < ncol; i++, p += 2) pal[(size_t)i] = (uint16_t)((b[p] << 8) | b[p + 1]);
        const int w = b[p], h = b[p + 1]; p += 2;
        if (!w || !h || w * h > 4096) { out.note = "bad size"; return false; }
        std::vector<uint8_t> idx;
        if (!mv_inflate_raw(b + p + 2, size - p - 2, idx)) { out.note = "inflate failed"; return false; }
        if (!mv_tex_decode_packed(fmt, idx.data(), idx.size(), w, h, pal.data(), ncol, out.rgba)) { out.note = "short pixel data"; return false; }
        out.w = w; out.h = h; out.note = "zlib CI"; return true;
    }
    // texInflateNonZlib, first (largest) image: a 24 bit header (format, w, h, compmethod) then the codec
    Bits bs(b, size, 1);
    const int fmt = (int)bs.read(4), w = (int)bs.read(8), h = (int)bs.read(8), cm = (int)bs.read(4);
    if (!w || !h || w * h > 0x2000 || fmt > MVT_I4) { out.note = "bad header"; return false; }
    if (cm == 2 || cm == 8) {   // HUFFMAN / HUFFMANBLUR (image.c texInflateNonZlib): the channels of the format, Huffman coded, blurred for 8
        static const int nch[13] = { 4, 3, 3, 3, 2, 2, 1, 1, 1, 1, 1, 1, 1 }, csz[13] = { 0x100, 0x20, 0x100, 0x20, 0x100, 0x10, 8, 0x100, 0x10, 0x100, 0x10, 0x100, 0x10 };
        if (fmt >= MVT_CI8_RGBA16) { out.note = "paletted huffman texture"; return false; }
        const int stack = cm == 8 ? (int)bs.read(3) : 0, nc = nch[fmt], cs = csz[fmt];
        std::vector<int> px;
        if (!huffman(bs, nc * w * h, cs, px)) { out.note = "huffman failed"; return false; }
        if (cm == 8) blur(px, w, nc * h, stack, cs);
        std::vector<int> abit;
        if (fmt == MVT_RGBA16 || fmt == MVT_IA4) { for (int i = 0; i < w * h; i++) abit.push_back((int)bs.read(1)); }   // texReadAlphaBits
        const int m = w * h;
        out.rgba.assign((size_t)m * 4, 0);
        for (int i = 0; i < m; i++) {
            const int c0 = px[(size_t)i], c1 = nc > 1 ? px[(size_t)(m + i)] : 0, c2 = nc > 2 ? px[(size_t)(2 * m + i)] : 0, c3 = nc > 3 ? px[(size_t)(3 * m + i)] : 0;
            uint32_t v = 0;
            switch (fmt) {
                case MVT_RGBA32: v = ((uint32_t)c0 << 24) | ((uint32_t)c1 << 16) | ((uint32_t)c2 << 8) | (uint32_t)c3; break;
                case MVT_RGB24: v = ((uint32_t)c0 << 16) | ((uint32_t)c1 << 8) | (uint32_t)c2; break;
                case MVT_RGBA16: v = ((uint32_t)c0 << 11) | ((uint32_t)c1 << 6) | ((uint32_t)c2 << 1) | (uint32_t)abit[(size_t)i]; break;
                case MVT_RGB15: v = ((uint32_t)c0 << 10) | ((uint32_t)c1 << 5) | (uint32_t)c2; break;
                case MVT_IA16: v = ((uint32_t)c0 << 8) | (uint32_t)c1; break;
                case MVT_IA8: v = ((uint32_t)c0 << 4) | (uint32_t)c1; break;
                case MVT_IA4: v = ((uint32_t)c0 << 1) | (uint32_t)abit[(size_t)i]; break;
                default: v = (uint32_t)c0; break;   // I8, I4
            }
            mv_tex_value_rgba(fmt, v, &out.rgba[(size_t)i * 4]);
        }
        out.w = w; out.h = h; out.note = cm == 8 ? "huffman+blur" : "huffman"; return true;
    }
    if (cm < 5 || cm > 7) { char t[64]; snprintf(t, sizeof t, "compmethod %d not supported", cm); out.note = t; return false; }
    const int bpp = bitsOf(fmt), n = (int)bs.read(11);
    std::vector<uint32_t> lut((size_t)n);
    for (int i = 0; i < n; i++) lut[(size_t)i] = bpp <= 24 ? bs.read(bpp) : ((bs.read(24) << 8) | bs.read(bpp - 24));
    std::vector<int> idx;
    bool ok;
    if (cm == 6) ok = huffman(bs, w * h, n, idx);
    else if (cm == 7) ok = rle(bs, w * h, idx);
    else { int nb = 0; while ((1 << nb) < n) nb++; idx.resize((size_t)w * h); for (auto& v : idx) v = (int)bs.read(nb); ok = !bs.bad; }
    if (!ok) { out.note = "codec failed"; return false; }
    out.rgba.assign((size_t)w * h * 4, 0);
    for (int i = 0; i < w * h; i++) mv_tex_value_rgba(fmt, idx[(size_t)i] < n ? lut[(size_t)idx[(size_t)i]] : 0, &out.rgba[(size_t)i * 4]);
    out.w = w; out.h = h; out.note = "lookup"; return true;
}

// ---- self check: a 4x4 image per format, packed the N64 way, against hand-computed RGBA
static int g_fail = 0;
static void expect(const char* what, const std::vector<uint8_t>& got, const std::vector<uint8_t>& want) {
    if (got != want) {
        g_fail++; fprintf(stderr, "selftest FAIL %s\n  got  ", what);
        for (uint8_t v : got) fprintf(stderr, "%02x", v);
        fprintf(stderr, "\n  want ");
        for (uint8_t v : want) fprintf(stderr, "%02x", v);
        fprintf(stderr, "\n");
    }
}
static void push(std::vector<uint8_t>& v, int r, int g, int b, int a) { v.push_back((uint8_t)r); v.push_back((uint8_t)g); v.push_back((uint8_t)b); v.push_back((uint8_t)a); }

int mv_tex_selftest() {
    g_fail = 0;
    std::vector<uint8_t> rgba, d, want;
    // RGBA32: R,G,B,A bytes
    for (int i = 0; i < 16; i++) { const int p[4] = { i * 16, 255 - i * 16, i, 255 - i }; for (int k = 0; k < 4; k++) d.push_back((uint8_t)p[k]); push(want, p[0], p[1], p[2], p[3]); }
    mv_tex_decode_packed(MVT_RGBA32, d.data(), d.size(), 4, 4, 0, 0, rgba); expect("RGBA32", rgba, want);
    // RGBA16 5551: red, green, blue, white, transparent black, mid grey 0x8421 -> 132, ...
    { const uint16_t v[16] = { 0xF801, 0x07C1, 0x003F, 0xFFFF, 0x0000, 0x8421, 0xF800, 0x0001, 0x07C0, 0x003E, 0xFFFE, 0x0841, 0xF801, 0x07C1, 0x003F, 0xFFFF };
      const int w5[16][4] = { {255,0,0,255},{0,255,0,255},{0,0,255,255},{255,255,255,255},{0,0,0,0},{132,132,132,255},{255,0,0,0},{0,0,0,255},{0,255,0,0},{0,0,255,0},{255,255,255,0},{8,8,0,255},{255,0,0,255},{0,255,0,255},{0,0,255,255},{255,255,255,255} };
      d.clear(); want.clear();
      for (int i = 0; i < 16; i++) { d.push_back((uint8_t)(v[i] >> 8)); d.push_back((uint8_t)v[i]); push(want, w5[i][0], w5[i][1], w5[i][2], w5[i][3]); }
      mv_tex_decode_packed(MVT_RGBA16, d.data(), d.size(), 4, 4, 0, 0, rgba); expect("RGBA16", rgba, want); }
    // RGB24: R,G,B bytes, opaque
    d.clear(); want.clear();
    for (int i = 0; i < 16; i++) { const int p[3] = { i * 17, 255 - i * 17, i * 3 }; for (int k = 0; k < 3; k++) d.push_back((uint8_t)p[k]); push(want, p[0], p[1], p[2], 255); }
    mv_tex_decode_packed(MVT_RGB24, d.data(), d.size(), 4, 4, 0, 0, rgba); expect("RGB24", rgba, want);
    // RGB15: 5-5-5, opaque
    { const uint16_t v[6] = { 0x7C00, 0x03E0, 0x001F, 0x7FFF, 0x0000, 0x4210 };
      const int w5[6][4] = { {255,0,0,255},{0,255,0,255},{0,0,255,255},{255,255,255,255},{0,0,0,255},{132,132,132,255} };
      d.clear(); want.clear();
      for (int i = 0; i < 16; i++) { d.push_back((uint8_t)(v[i % 6] >> 8)); d.push_back((uint8_t)v[i % 6]); push(want, w5[i % 6][0], w5[i % 6][1], w5[i % 6][2], w5[i % 6][3]); }
      mv_tex_decode_packed(MVT_RGB15, d.data(), d.size(), 4, 4, 0, 0, rgba); expect("RGB15", rgba, want); }
    // IA16: intensity byte, alpha byte
    d.clear(); want.clear();
    for (int i = 0; i < 16; i++) { const int in = i * 16, a = 255 - i * 8; d.push_back((uint8_t)in); d.push_back((uint8_t)a); push(want, in, in, in, a); }
    mv_tex_decode_packed(MVT_IA16, d.data(), d.size(), 4, 4, 0, 0, rgba); expect("IA16", rgba, want);
    // IA8: high nibble intensity, low nibble alpha (each x17)
    d.clear(); want.clear();
    for (int i = 0; i < 16; i++) { d.push_back((uint8_t)((i << 4) | (15 - i))); push(want, i * 17, i * 17, i * 17, (15 - i) * 17); }
    mv_tex_decode_packed(MVT_IA8, d.data(), d.size(), 4, 4, 0, 0, rgba); expect("IA8", rgba, want);
    // IA4: 3 bit intensity + 1 bit alpha, two pixels per byte, high nibble first
    d.clear(); want.clear();
    for (int i = 0; i < 8; i++) {
        const int n0 = ((i & 7) << 1) | (i & 1), n1 = (((7 - i) & 7) << 1) | ((i + 1) & 1);
        d.push_back((uint8_t)((n0 << 4) | n1));
        for (int n : { n0, n1 }) { const int in = ((n >> 1) & 7) * 255 / 7; push(want, in, in, in, (n & 1) ? 255 : 0); }
    }
    mv_tex_decode_packed(MVT_IA4, d.data(), d.size(), 4, 4, 0, 0, rgba); expect("IA4", rgba, want);
    // I8, I4
    d.clear(); want.clear();
    for (int i = 0; i < 16; i++) { d.push_back((uint8_t)(i * 17)); push(want, i * 17, i * 17, i * 17, i * 17); }
    mv_tex_decode_packed(MVT_I8, d.data(), d.size(), 4, 4, 0, 0, rgba); expect("I8", rgba, want);
    d.clear(); want.clear();
    for (int i = 0; i < 8; i++) { d.push_back((uint8_t)(((2 * i) << 4) | (2 * i + 1))); for (int n : { 2 * i, 2 * i + 1 }) push(want, n * 17, n * 17, n * 17, n * 17); }
    mv_tex_decode_packed(MVT_I4, d.data(), d.size(), 4, 4, 0, 0, rgba); expect("I4", rgba, want);
    // CI8 with an RGBA16 TLUT and with an IA16 TLUT
    { const uint16_t pal[4] = { 0xF801, 0x07C1, 0x003F, 0x0000 }; const int w4[4][4] = { {255,0,0,255},{0,255,0,255},{0,0,255,255},{0,0,0,0} };
      d.clear(); want.clear();
      for (int i = 0; i < 16; i++) { d.push_back((uint8_t)(i % 4)); push(want, w4[i % 4][0], w4[i % 4][1], w4[i % 4][2], w4[i % 4][3]); }
      mv_tex_decode_packed(MVT_CI8_RGBA16, d.data(), d.size(), 4, 4, pal, 4, rgba); expect("CI8+RGBA16", rgba, want); }
    { const uint16_t pal[4] = { 0xFFFF, 0x80FF, 0x4080, 0x0000 }; const int w4[4][4] = { {255,255,255,255},{128,128,128,255},{64,64,64,128},{0,0,0,0} };
      d.clear(); want.clear();
      for (int i = 0; i < 16; i++) { d.push_back((uint8_t)(i % 4)); push(want, w4[i % 4][0], w4[i % 4][1], w4[i % 4][2], w4[i % 4][3]); }
      mv_tex_decode_packed(MVT_CI8_IA16, d.data(), d.size(), 4, 4, pal, 4, rgba); expect("CI8+IA16", rgba, want); }
    // CI4 with RGBA16 / IA16 TLUTs, two indices per byte, high nibble first
    { uint16_t pal[16] = {}; pal[0] = 0xF801; pal[1] = 0x07C1; pal[2] = 0x003F; pal[3] = 0xFFFF; pal[15] = 0x8421;
      const int w4[5][4] = { {255,0,0,255},{0,255,0,255},{0,0,255,255},{255,255,255,255},{132,132,132,255} };
      const int idx[16] = { 0, 1, 2, 3, 15, 0, 1, 2, 3, 15, 0, 1, 2, 3, 15, 0 }, map[16] = { 0, 1, 2, 3, 4, 0, 1, 2, 3, 4, 0, 1, 2, 3, 4, 0 };
      d.clear(); want.clear();
      for (int i = 0; i < 16; i += 2) d.push_back((uint8_t)((idx[i] << 4) | idx[i + 1]));
      for (int i = 0; i < 16; i++) push(want, w4[map[i]][0], w4[map[i]][1], w4[map[i]][2], w4[map[i]][3]);
      mv_tex_decode_packed(MVT_CI4_RGBA16, d.data(), d.size(), 4, 4, pal, 16, rgba); expect("CI4+RGBA16", rgba, want); }
    { uint16_t pal[16] = {}; pal[0] = 0xFFFF; pal[1] = 0x80FF; pal[2] = 0x4080; pal[15] = 0x2010;
      const int w4[4][4] = { {255,255,255,255},{128,128,128,255},{64,64,64,128},{32,32,32,16} };
      const int idx[16] = { 0, 1, 2, 15, 0, 1, 2, 15, 0, 1, 2, 15, 0, 1, 2, 15 }, map[16] = { 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3 };
      d.clear(); want.clear();
      for (int i = 0; i < 16; i += 2) d.push_back((uint8_t)((idx[i] << 4) | idx[i + 1]));
      for (int i = 0; i < 16; i++) push(want, w4[map[i]][0], w4[map[i]][1], w4[map[i]][2], w4[map[i]][3]);
      mv_tex_decode_packed(MVT_CI4_IA16, d.data(), d.size(), 4, 4, pal, 16, rgba); expect("CI4+IA16", rgba, want); }
    // an odd width: CI4 rows start on a byte (3 pixels = 2 bytes)
    { uint16_t pal[16] = {}; pal[1] = 0xF801; pal[2] = 0x07C1;
      const uint8_t dd[4] = { 0x12, 0x10, 0x21, 0x20 };   // row 0: 1 2 1 (pad), row 1: 2 1 2 (pad)
      want.clear(); push(want, 255, 0, 0, 255); push(want, 0, 255, 0, 255); push(want, 255, 0, 0, 255); push(want, 0, 255, 0, 255); push(want, 255, 0, 0, 255); push(want, 0, 255, 0, 255);
      mv_tex_decode_packed(MVT_CI4_RGBA16, dd, 4, 3, 2, pal, 16, rgba); expect("CI4 odd width", rgba, want); }
    fprintf(stderr, g_fail ? "selftest: %d failures\n" : "selftest: all texture formats ok\n", g_fail);
    return g_fail ? 1 : 0;
}
