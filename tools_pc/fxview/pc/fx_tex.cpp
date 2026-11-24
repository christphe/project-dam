// fxview: the texture codecs modelview's mv_tex.cpp does not handle, ported from src/game/image.c
// (texInflateNonZlib compmethods 0-4, 8, 9: uncompressed, Huffman, Huffman per channel, RLE, Huffman / RLE + texBlur;
// and texInflateZlib images with a non-paletted format). The lookup codecs (5-7) and paletted zlib images stay
// mv_tex_load's. Only the first (largest) image of a texture is decoded.
#include "fx_tex.h"
#include <stdio.h>
#include <string.h>

namespace {
// the per-format tables of image.c (TEXFORMAT_* order)
const int kNumChannels[13] = { 4, 3, 3, 3, 2, 2, 1, 1, 1, 1, 1, 1, 1 };
const int kHas1BitAlpha[13] = { 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0 };
const int kChannelSize[13] = { 0x100, 0x20, 0x100, 0x20, 0x100, 0x10, 8, 0x100, 0x10, 0x100, 0x10, 0x100, 0x10 };

struct Bits {   // texReadBits: MSB first
    const uint8_t* d; size_t n, p; uint64_t acc = 0; int cnt = 0; bool bad = false;
    Bits(const uint8_t* dd, size_t nn, size_t pp) : d(dd), n(nn), p(pp) {}
    uint32_t read(int k) {
        while (cnt < k) { if (p >= n) { bad = true; acc <<= 8; } else acc = (acc << 8) | d[p]; p++; cnt += 8; }
        cnt -= k; return k ? (uint32_t)((acc >> cnt) & ((1ull << k) - 1)) : 0;
    }
};
// texInflateHuffman (same as mv_tex.cpp's, which is file-local there)
bool huffman(Bits& bs, int numit, int chansize, int* out) {
    const int NB = 2048;
    static int freq[NB], n0[NB], n1[NB];
    for (int i = 0; i < NB; i++) { freq[i] = 0; n0[i] = n1[i] = -1; }
    for (int i = 0; i < chansize; i++) freq[i] = (int)bs.read(8);
    auto two = [&](int& f1, int& i1, int& f2, int& i2) {
        f1 = f2 = 9999; i1 = i2 = -1;
        for (int i = 0; i < chansize; i++) {
            if (freq[i] < f1) { if (f2 < f1) { f1 = freq[i]; i1 = i; } else { f2 = freq[i]; i2 = i; } }
            else if (freq[i] < f2) { f2 = freq[i]; i2 = i; }
        }
    };
    int f1, i1, f2, i2, root = 0; two(f1, i1, f2, i2);
    while (!(f1 == 9999 || f2 == 9999)) {
        const int sum = freq[i1] + freq[i2], s = sum ? sum : 1;
        freq[i1] = 9999; freq[i2] = 9999;
        const bool leaf1 = n0[i1] < 0 && n1[i1] < 0, leaf2 = n0[i2] < 0 && n1[i2] < 0;
        if (leaf1) { n0[i1] = i1 + 10000; root = i1; freq[i1] = s; n1[i1] = leaf2 ? i2 + 10000 : i2; }
        else if (leaf2) { n0[i2] = i2 + 10000; root = i2; freq[i2] = s; n1[i2] = leaf1 ? i1 + 10000 : i1; }
        else {
            root = 0;
            while (n0[root] >= 0 || n1[root] >= 0 || freq[root] < 9999) { root++; if (root >= NB - 1) return false; }
            freq[root] = s; n0[root] = i1; n1[root] = i2;
        }
        two(f1, i1, f2, i2);
    }
    for (int i = 0; i < numit; i++) {
        int v = root, guard = 0;
        while (v < 10000) { v = bs.read(1) ? n1[v] : n0[v]; if (v < 0 || ++guard > 4096 || bs.bad) return false; }
        out[i] = v - 10000;
    }
    return true;
}
// texInflateRle
bool rle(Bits& bs, int total, int* out) {
    const int bt = (int)bs.read(3), rl = (int)bs.read(3), blk = (int)bs.read(4);
    int cost = bt + rl + blk + 1, fudge = 0;
    while (cost > 0) { cost -= blk + 1; fudge++; }
    int n = 0; long guard = (long)total * 4 + 64;
    while (n < total && guard-- > 0 && !bs.bad) {
        if (bs.read(1) == 0) out[n++] = (int)bs.read(blk);
        else {
            const long start = (long)n - (long)bs.read(bt) - 1; const int run = (int)bs.read(rl) + fudge;
            for (int k = 0; k < run && n < total; k++) { const int v = start + k >= 0 && start + k < n ? out[start + k] : 0; out[n++] = v; }
            if (n < total) out[n++] = (int)bs.read(blk);
        }
    }
    return !bs.bad && n >= total;
}
// texBlur: a predictor over the channel planes (height = channels * image height)
void blur(int* px, int width, int height, int method, int chansize) {
    for (int y = 0; y < height; y++) for (int x = 0; x < width; x++) {
        const int cur = px[y * width + x] + chansize * 2;
        const int left = x > 0 ? px[y * width + x - 1] : 0, above = y > 0 ? px[(y - 1) * width + x] : 0;
        const int al = x > 0 && y > 0 ? px[(y - 1) * width + x - 1] : 0;
        int v = cur;
        switch (method) {
        case 0: v = cur + left; break; case 1: v = cur + above; break; case 2: v = cur + al; break;
        case 3: v = cur + (left + above - al); break; case 4: v = cur + ((above - al) / 2 + left); break;
        case 5: v = cur + ((left - al) / 2 + above); break; case 6: v = cur + ((left + above) / 2); break;
        }
        px[y * width + x] = (v % chansize) & 0xFF;   // the game stores it in a u8
    }
}
// texChannelsToPixels: channel planes -> the format's pixel value (as mv_tex_value_rgba reads it)
uint32_t channelsToValue(int fmt, const int* s, int pos, int mult) {
    switch (fmt) {
    case 0: return ((uint32_t)s[pos] << 24) | ((uint32_t)s[pos + mult] << 16) | ((uint32_t)s[pos + mult * 2] << 8) | (uint32_t)s[pos + mult * 3];
    case 1: return ((uint32_t)s[pos] << 11) | ((uint32_t)s[pos + mult] << 6) | ((uint32_t)s[pos + mult * 2] << 1) | (uint32_t)(s[pos + mult * 3] & 1);
    case 2: return ((uint32_t)s[pos] << 16) | ((uint32_t)s[pos + mult] << 8) | (uint32_t)s[pos + mult * 2];
    case 3: return ((uint32_t)s[pos] << 10) | ((uint32_t)s[pos + mult] << 5) | (uint32_t)s[pos + mult * 2];   // RGB15 as 5-5-5
    case 4: return ((uint32_t)s[pos] << 8) | (uint32_t)s[pos + mult];
    case 5: return ((uint32_t)s[pos] << 4) | (uint32_t)s[pos + mult];
    case 6: return ((uint32_t)s[pos] << 1) | (uint32_t)(s[pos + mult * 3] & 1);   // IA4: 3 bit intensity, alpha bit plane at 3 * mult
    default: return (uint32_t)s[pos];   // I8, I4
    }
}
std::vector<std::pair<long, long>> g_list;
}  // namespace

bool fx_tex_set_imagelist(const std::string& path) {
    FILE* f = fopen(path.c_str(), "r"); if (!f) return false;
    char line[512]; g_list.clear();
    while (fgets(line, sizeof line, f)) { long o = 0, s = 0; if (sscanf(line, "%ld,%ld", &o, &s) == 2) g_list.push_back({ o, s }); }
    fclose(f); return !g_list.empty();
}

bool fx_tex_decode(const uint8_t* b, size_t size, MvTexImage& out) {
    out = MvTexImage();
    if (size < 5) { out.note = "too short"; return false; }
    if ((b[0] >> 6) & 1) {   // texInflateZlib with a direct-colour format: palette read and ignored, packed pixels
        const int fmt = b[1], ncol = b[2] + 1;
        if (fmt > MVT_I4) { out.note = "paletted zlib image (mv_tex_load's)"; return false; }
        size_t p = 3 + 2 * (size_t)ncol; if (p + 2 > size) { out.note = "bad header"; return false; }
        const int w = b[p], h = b[p + 1]; p += 2;
        if (!w || !h || w * h > 4096) { out.note = "bad size"; return false; }
        std::vector<uint8_t> raw;
        if (!mv_inflate_raw(b + p + 2, size - p - 2, raw)) { out.note = "inflate failed"; return false; }
        // texAlignIndices: rows padded; the inflated stream is tight rows
        if (!mv_tex_decode_packed(fmt, raw.data(), raw.size(), w, h, nullptr, 0, out.rgba)) { out.note = "short pixel data"; return false; }
        out.w = w; out.h = h; out.note = "zlib direct"; return true;
    }
    Bits bs(b, size, 1);
    const int fmt = (int)bs.read(4), w = (int)bs.read(8), h = (int)bs.read(8), cm = (int)bs.read(4);
    if (!w || !h || w * h > 0x2000 || fmt > MVT_I4) { out.note = "bad header"; return false; }
    const int n = w * h;
    std::vector<uint32_t> val((size_t)n);
    if (cm == 0 || cm == 1) {   // texReadUncompressed
        for (int y = 0; y < h; y++) {
            if (fmt == MVT_IA4 || fmt == MVT_I4) {
                for (int x = 0; x < w; x += 2) { const uint32_t v = bs.read(8); val[(size_t)(y * w + x)] = v >> 4; if (x + 1 < w) val[(size_t)(y * w + x + 1)] = v & 15; }
            } else {
                const int bits = fmt == MVT_RGBA32 ? 32 : fmt == MVT_RGB24 ? 24 : fmt == MVT_RGB15 ? 15 : (fmt == MVT_RGBA16 || fmt == MVT_IA16) ? 16 : 8;
                for (int x = 0; x < w; x++) val[(size_t)(y * w + x)] = bits == 32 ? (bs.read(16) << 16) | bs.read(16) : bs.read(bits);
            }
        }
    } else if (cm == 2 || cm == 3 || cm == 4 || cm == 8 || cm == 9) {
        const int nch = kNumChannels[fmt], cs = kChannelSize[fmt];
        std::vector<int> sc((size_t)n * 4, 0);
        int stack = 0;
        if (cm == 8 || cm == 9) stack = (int)bs.read(3);
        bool ok = true;
        if (cm == 3) { for (int j = 0; j < nch && ok; j++) ok = huffman(bs, n, cs, &sc[(size_t)n * j]); }
        else if (cm == 2 || cm == 8) ok = huffman(bs, nch * n, cs, sc.data());
        else ok = rle(bs, nch * n, sc.data());
        if (!ok) { out.note = "codec failed"; return false; }
        if (cm == 8 || cm == 9) blur(sc.data(), w, nch * h, stack, cs);
        if (kHas1BitAlpha[fmt]) for (int i = 0; i < n; i++) sc[(size_t)(n * 3 + i)] = (int)bs.read(1);
        for (int i = 0; i < n; i++) val[(size_t)i] = channelsToValue(fmt, sc.data(), i, n);
    } else { char t[48]; snprintf(t, sizeof t, "compmethod %d (mv_tex_load's)", cm); out.note = t; return false; }
    if (bs.bad) { out.note = "short bitstream"; return false; }
    out.rgba.assign((size_t)n * 4, 0);
    for (int i = 0; i < n; i++) mv_tex_value_rgba(fmt, val[(size_t)i], &out.rgba[(size_t)i * 4]);
    char t[48]; snprintf(t, sizeof t, "compmethod %d", cm); out.note = t;
    out.w = w; out.h = h; return true;
}

bool fx_tex_load(const uint8_t* rom, size_t romSize, int tn, MvTexImage& out) {
    if (mv_tex_load(rom, romSize, tn, out)) return true;   // lookup codecs and paletted zlib
    const std::string why = out.note;
    if (tn < 0 || tn >= (int)g_list.size()) { out.note = "texture number outside the image list"; return false; }
    const size_t off = (size_t)g_list[(size_t)tn].first, size = (size_t)g_list[(size_t)tn].second;
    if (off + size > romSize) { out.note = "bad image list entry"; return false; }
    if (fx_tex_decode(rom + off, size, out)) return true;
    out.note = why + "; " + out.note;
    return false;
}

// ---- self check: hand-made bitstreams for the new paths
namespace {
struct W { std::vector<uint8_t> b; int nb = 0; void put(uint32_t v, int k) { for (int i = k - 1; i >= 0; i--) { if (nb % 8 == 0) b.push_back(0); if ((v >> i) & 1) b.back() |= (uint8_t)(0x80 >> (nb % 8)); nb++; } } };
}
int fx_tex_selftest() {
    int fail = 0;
    {   // uncompressed IA8 2x2: header byte, format 5, w 2, h 2, compmethod 0, four bytes
        W w; w.put(0, 8); w.put(MVT_IA8, 4); w.put(2, 8); w.put(2, 8); w.put(0, 4);
        const uint8_t px[4] = { 0xF0, 0x0F, 0x88, 0xFF }; for (uint8_t p : px) w.put(p, 8);
        MvTexImage img;
        if (!fx_tex_decode(w.b.data(), w.b.size(), img) || img.w != 2 || img.rgba[0] != 255 || img.rgba[3] != 0 || img.rgba[7] != 255 || img.rgba[12] != 255 || img.rgba[15] != 255) { fprintf(stderr, "selftest FAIL fx uncompressed IA8 (%s)\n", img.note.c_str()); fail++; }
    }
    {   // RLE I8 2x2 (compmethod 4): bt 1, rl 1, blk 8; literals 10, 20, 30, 40
        W w; w.put(0, 8); w.put(MVT_I8, 4); w.put(2, 8); w.put(2, 8); w.put(4, 4);
        w.put(1, 3); w.put(1, 3); w.put(8, 4);
        for (int v : { 10, 20, 30, 40 }) { w.put(0, 1); w.put((uint32_t)v, 8); }
        MvTexImage img;
        if (!fx_tex_decode(w.b.data(), w.b.size(), img) || img.rgba[0] != 10 || img.rgba[4] != 20 || img.rgba[8] != 30 || img.rgba[12] != 40) { fprintf(stderr, "selftest FAIL fx RLE I8 (%s)\n", img.note.c_str()); fail++; }
    }
    {   // texBlur method 0 (left predictor), chansize 16: deltas 1,1,1 -> 1,2,3
        int p[3] = { 1, 1, 1 }; blur(p, 3, 1, 0, 16);
        if (p[0] != 1 || p[1] != 2 || p[2] != 3) { fprintf(stderr, "selftest FAIL fx blur\n"); fail++; }
    }
    if (!fail) fprintf(stderr, "selftest: fx codecs ok\n");
    return fail;
}
