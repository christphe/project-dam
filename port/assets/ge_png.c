/* ge_png.c -- the one PNG reader and writer (ge_assets, issue #172). libc + zlib. */
#include "ge_png.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The codec under the PNG: zlib; or, with GE_PNG_NO_ZLIB (modelview's plain g++ build has no zlib), a small RFC 1951 inflate, a
 * table CRC and stored deflate blocks (a valid PNG, larger). zInflate: the zlib stream src -> exactly outn bytes. zDeflate: raw ->
 * a malloc'd zlib stream. zCrc: the PNG chunk CRC over a type and its data. */
#ifndef GE_PNG_NO_ZLIB
#include <zlib.h>

static int zInflate(const uint8_t *src, size_t n, uint8_t *out, size_t outn)
{
    z_stream z;
    int ok;
    memset(&z, 0, sizeof z);
    if (inflateInit(&z) != Z_OK) return 0;
    z.next_in = (Bytef *)src; z.avail_in = (uInt)n;
    z.next_out = out; z.avail_out = (uInt)outn;
    ok = inflate(&z, Z_FINISH) == Z_STREAM_END && z.avail_out == 0;
    inflateEnd(&z);
    return ok;
}

static uint8_t *zDeflate(const uint8_t *raw, size_t rawlen, int level, size_t *outlen)
{
    uLongf zlen = compressBound((uLong)rawlen);
    uint8_t *z = (uint8_t *)malloc(zlen);
    if (!z) return NULL;
    if (compress2(z, &zlen, raw, (uLong)rawlen, level) != Z_OK) { free(z); return NULL; }
    *outlen = (size_t)zlen;
    return z;
}

static uint32_t zCrc(const char *type, const uint8_t *data, uint32_t len)
{
    return (uint32_t)crc32(crc32(0L, (const Bytef *)type, 4), (const Bytef *)data, len);
}
#else
typedef struct { const uint8_t *p; size_t n, pos; uint32_t buf; int cnt; } Bits;
typedef struct { uint16_t count[16], sym[288]; } Huff;

static int bitsGet(Bits *b, int k)
{
    int v;
    while (b->cnt < k) {
        if (b->pos >= b->n) return -1;
        b->buf |= (uint32_t)b->p[b->pos++] << b->cnt;
        b->cnt += 8;
    }
    v = (int)(b->buf & ((1u << k) - 1));
    b->buf >>= k;
    b->cnt -= k;
    return v;
}

static void huffBuild(Huff *h, const uint8_t *len, int n)
{
    uint16_t offs[16];
    memset(h->count, 0, sizeof h->count);
    for (int i = 0; i < n; i++) h->count[len[i]]++;
    h->count[0] = 0;
    offs[1] = 0;
    for (int i = 1; i < 15; i++) offs[i + 1] = (uint16_t)(offs[i] + h->count[i]);
    for (int i = 0; i < n; i++) if (len[i]) h->sym[offs[len[i]]++] = (uint16_t)i;
}

static int huffDecode(Bits *b, const Huff *h)
{
    int code = 0, first = 0, index = 0;
    for (int l = 1; l < 16; l++) {
        const int bit = bitsGet(b, 1), c = h->count[l];
        if (bit < 0) return -1;
        code |= bit;
        if (code - c < first) return h->sym[index + (code - first)];
        index += c; first += c; first <<= 1; code <<= 1;
    }
    return -1;
}

static int zInflate(const uint8_t *src, size_t n, uint8_t *out, size_t outn)
{
    static const uint16_t lb[] = { 3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258 };
    static const uint16_t le[] = { 0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0 };
    static const uint16_t db[] = { 1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577 };
    static const uint16_t de[] = { 0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13 };
    Bits b;
    size_t o = 0;
    int last;
    if (n < 6) return 0;
    b.p = src + 2; b.n = n - 2; b.pos = 0; b.buf = 0; b.cnt = 0;   /* skip the zlib header */
    do {
        int type;
        last = bitsGet(&b, 1);
        type = bitsGet(&b, 2);
        if (last < 0 || type < 0) return 0;
        if (type == 0) {
            size_t len;
            b.buf = 0; b.cnt = 0;
            if (b.pos + 4 > b.n) return 0;
            len = (size_t)b.p[b.pos] | ((size_t)b.p[b.pos + 1] << 8);
            b.pos += 4;
            if (b.pos + len > b.n || o + len > outn) return 0;
            memcpy(out + o, b.p + b.pos, len);
            o += len; b.pos += len;
        } else if (type == 1 || type == 2) {
            Huff lh, dh;
            uint8_t lens[320];
            if (type == 1) {
                int i = 0;
                for (; i < 144; i++) lens[i] = 8;
                for (; i < 256; i++) lens[i] = 9;
                for (; i < 280; i++) lens[i] = 7;
                for (; i < 288; i++) lens[i] = 8;
                huffBuild(&lh, lens, 288);
                for (i = 0; i < 30; i++) lens[i] = 5;
                huffBuild(&dh, lens, 30);
            } else {
                static const uint8_t ord[19] = { 16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15 };
                uint8_t cl[19];
                Huff ch;
                const int nl = bitsGet(&b, 5) + 257, nd = bitsGet(&b, 5) + 1, nc = bitsGet(&b, 4) + 4;
                int i;
                if (nl < 257 || nd < 1 || nc < 4) return 0;
                memset(cl, 0, sizeof cl);
                for (i = 0; i < nc; i++) { const int v = bitsGet(&b, 3); if (v < 0) return 0; cl[ord[i]] = (uint8_t)v; }
                huffBuild(&ch, cl, 19);
                i = 0;
                while (i < nl + nd) {
                    const int sy = huffDecode(&b, &ch);
                    if (sy < 0) return 0;
                    if (sy < 16) lens[i++] = (uint8_t)sy;
                    else {
                        int rep, v = 0;
                        if (sy == 16) { if (!i) return 0; v = lens[i - 1]; rep = 3 + bitsGet(&b, 2); }
                        else if (sy == 17) rep = 3 + bitsGet(&b, 3);
                        else rep = 11 + bitsGet(&b, 7);
                        if (i + rep > nl + nd) return 0;
                        while (rep--) lens[i++] = (uint8_t)v;
                    }
                }
                huffBuild(&lh, lens, nl);
                huffBuild(&dh, lens + nl, nd);
            }
            for (;;) {
                int sy = huffDecode(&b, &lh);
                if (sy < 0) return 0;
                if (sy < 256) { if (o >= outn) return 0; out[o++] = (uint8_t)sy; }
                else if (sy == 256) break;
                else {
                    int len, ds;
                    size_t dist;
                    sy -= 257;
                    if (sy >= 29) return 0;
                    len = lb[sy] + bitsGet(&b, le[sy]);
                    ds = huffDecode(&b, &dh);
                    if (ds < 0 || ds >= 30) return 0;
                    dist = (size_t)db[ds] + (size_t)bitsGet(&b, de[ds]);
                    if (dist > o || o + (size_t)len > outn) return 0;
                    for (int k = 0; k < len; k++, o++) out[o] = out[o - dist];
                }
            }
        } else return 0;
    } while (!last);
    return o == outn;
}

static uint8_t *zDeflate(const uint8_t *raw, size_t rawlen, int level, size_t *outlen)
{
    const size_t blocks = rawlen / 65535 + 1;
    uint8_t *z = (uint8_t *)malloc(2 + rawlen + blocks * 5 + 4);
    size_t zl = 0, off = 0;
    uint32_t a = 1, b = 0;
    (void)level;
    if (!z) return NULL;
    z[zl++] = 0x78; z[zl++] = 0x01;
    do {
        const size_t n = rawlen - off < 65535 ? rawlen - off : 65535;
        z[zl++] = off + n >= rawlen ? 1 : 0;
        z[zl++] = (uint8_t)n; z[zl++] = (uint8_t)(n >> 8); z[zl++] = (uint8_t)~n; z[zl++] = (uint8_t)(~n >> 8);
        memcpy(z + zl, raw + off, n);
        zl += n; off += n;
    } while (off < rawlen);
    for (size_t i = 0; i < rawlen; i++) { a = (a + raw[i]) % 65521; b = (b + a) % 65521; }
    z[zl++] = (uint8_t)(b >> 8); z[zl++] = (uint8_t)b; z[zl++] = (uint8_t)(a >> 8); z[zl++] = (uint8_t)a;
    *outlen = zl;
    return z;
}

static uint32_t zCrc(const char *type, const uint8_t *data, uint32_t len)
{
    static uint32_t tab[256];
    uint32_t c = 0xFFFFFFFFu;
    if (!tab[1]) {
        for (uint32_t i = 0; i < 256; i++) { uint32_t v = i; for (int k = 0; k < 8; k++) v = (v & 1) ? 0xEDB88320u ^ (v >> 1) : v >> 1; tab[i] = v; }
    }
    for (int i = 0; i < 4; i++) c = tab[(c ^ (uint8_t)type[i]) & 255] ^ (c >> 8);
    for (uint32_t i = 0; i < len; i++) c = tab[(c ^ data[i]) & 255] ^ (c >> 8);
    return ~c;
}
#endif

static uint32_t be32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
static int iabs(int v) { return v < 0 ? -v : v; }

int gePngDecode(const uint8_t *file, size_t len, int *outW, int *outH, uint8_t **outRgba)
{
    uint8_t *idat = NULL, *raw = NULL, *pix = NULL, *rgba = NULL;
    uint8_t plte[256 * 3], trns[256];
    uint32_t w = 0, h = 0, pos = 8, idatn = 0, ch, stride, nplte = 0, ntrns = 0;
    int ctype = -1, ok = 0;
    *outRgba = NULL;
    *outW = *outH = 0;
    memset(trns, 255, sizeof trns);
    if (!file || len < 33 || len > 0x7FFFFFFF || memcmp(file, "\x89PNG\r\n\x1a\n", 8) != 0) return 0;
    idat = (uint8_t *)malloc(len);
    if (!idat) return 0;
    while ((size_t)pos + 12 <= len) {
        const uint32_t clen = be32(file + pos);
        const uint8_t *type = file + pos + 4, *d = file + pos + 8;
        if ((size_t)pos + 12 + clen > len) goto done;
        if (!memcmp(type, "IHDR", 4) && clen >= 13) {
            w = be32(d); h = be32(d + 4);
            if (d[8] != 8 || d[12] != 0) goto done;   /* 8-bit, not interlaced */
            ctype = d[9];
        } else if (!memcmp(type, "PLTE", 4) && clen <= sizeof plte) {
            memcpy(plte, d, clen); nplte = clen / 3;
        } else if (!memcmp(type, "tRNS", 4) && clen <= sizeof trns) {
            memcpy(trns, d, clen); ntrns = clen;
        } else if (!memcmp(type, "IDAT", 4)) {
            memcpy(idat + idatn, d, clen); idatn += clen;
        } else if (!memcmp(type, "IEND", 4)) {
            break;
        }
        pos += 12 + clen;
    }
    (void)ntrns;
    ch = ctype == 0 ? 1 : ctype == 2 ? 3 : ctype == 3 ? 1 : ctype == 4 ? 2 : ctype == 6 ? 4 : 0;
    if (!ch || !w || !h || w > GE_PNG_MAX_SIDE || h > GE_PNG_MAX_SIDE || !idatn || (ctype == 3 && !nplte)) goto done;
    stride = w * ch;
    raw = (uint8_t *)malloc((size_t)(stride + 1) * h);
    pix = (uint8_t *)malloc((size_t)stride * h);
    rgba = (uint8_t *)malloc((size_t)w * h * 4);
    if (!raw || !pix || !rgba) goto done;
    if (!zInflate(idat, idatn, raw, (size_t)(stride + 1) * h)) goto done;
    for (uint32_t y = 0; y < h; y++) {   /* unfilter */
        const uint8_t *src = raw + (size_t)y * (stride + 1) + 1;
        uint8_t *dst = pix + (size_t)y * stride, *up = y ? dst - stride : NULL;
        const int ft = raw[(size_t)y * (stride + 1)];
        for (uint32_t x = 0; x < stride; x++) {
            const int a = x >= ch ? dst[x - ch] : 0, b = up ? up[x] : 0, c = (up && x >= ch) ? up[x - ch] : 0;
            int v = src[x];
            switch (ft) {
            case 0: break;
            case 1: v += a; break;
            case 2: v += b; break;
            case 3: v += (a + b) / 2; break;
            case 4: { const int p = a + b - c, pa = iabs(p - a), pb = iabs(p - b), pc = iabs(p - c);
                      v += (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c); break; }
            default: goto done;
            }
            dst[x] = (uint8_t)v;
        }
    }
    for (size_t i = 0; i < (size_t)w * h; i++) {
        const uint8_t *s = pix + i * ch;
        uint8_t *o = rgba + i * 4;
        switch (ctype) {
        case 0: o[0] = o[1] = o[2] = s[0]; o[3] = 255; break;
        case 4: o[0] = o[1] = o[2] = s[0]; o[3] = s[1]; break;
        case 2: o[0] = s[0]; o[1] = s[1]; o[2] = s[2]; o[3] = 255; break;
        case 6: memcpy(o, s, 4); break;
        case 3:
            if (s[0] >= nplte) goto done;
            o[0] = plte[s[0] * 3]; o[1] = plte[s[0] * 3 + 1]; o[2] = plte[s[0] * 3 + 2]; o[3] = trns[s[0]];
            break;
        }
    }
    *outW = (int)w; *outH = (int)h; *outRgba = rgba; rgba = NULL;
    ok = 1;
done:
    free(idat); free(raw); free(pix); free(rgba);
    return ok;
}

int gePngRead(const char *path, int *w, int *h, uint8_t **rgba)
{
    FILE *f = fopen(path, "rb");
    uint8_t *buf = NULL;
    long n;
    int ok = 0;
    *rgba = NULL;
    *w = *h = 0;
    if (!f) return 0;
    if (fseek(f, 0, SEEK_END) == 0 && (n = ftell(f)) > 0 && n < 256l * 1024 * 1024 && fseek(f, 0, SEEK_SET) == 0) {
        buf = (uint8_t *)malloc((size_t)n);
        if (buf && fread(buf, 1, (size_t)n, f) == (size_t)n) ok = gePngDecode(buf, (size_t)n, w, h, rgba);
    }
    free(buf);
    fclose(f);
    return ok;
}

/* ---- writer --------------------------------------------------------------------------------------------------------- */

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}

/* Appends one chunk at p; returns the new end. */
static uint8_t *chunk(uint8_t *p, const char *type, const uint8_t *data, uint32_t len)
{
    put32(p, len);
    memcpy(p + 4, type, 4);
    if (len) memcpy(p + 8, data, len);
    put32(p + 8 + len, zCrc(type, p + 8, len));
    return p + 12 + len;
}

/* The scanline `row` (stride bytes, bpp bytes per pixel) under filter ft, given the row above (NULL on the first). */
static void filterRow(int ft, const uint8_t *row, const uint8_t *up, size_t stride, size_t bpp, uint8_t *out)
{
    for (size_t x = 0; x < stride; x++) {
        const int a = x >= bpp ? row[x - bpp] : 0, b = up ? up[x] : 0, c = (up && x >= bpp) ? up[x - bpp] : 0;
        int pr = 0;
        switch (ft) {
        case 1: pr = a; break;
        case 2: pr = b; break;
        case 3: pr = (a + b) / 2; break;
        case 4: { const int pp = a + b - c, pa = iabs(pp - a), pb = iabs(pp - b), pc = iabs(pp - c);
                  pr = (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c); break; }
        default: break;
        }
        out[x] = (uint8_t)(row[x] - pr);
    }
}

int gePngEncode(int width, int height, const uint8_t *rgba, unsigned flags, uint8_t **out, size_t *outLen)
{
    static const uint8_t sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    const size_t bpp = (flags & GE_PNG_RGB) ? 3 : 4;
    size_t stride, rawlen, y;
    uint8_t *pix, *raw, *z, *buf, *p;
    uint8_t ihdr[13];
    size_t zlen;
    if (width < 1 || height < 1 || !rgba || !out) return 0;
    stride = (size_t)width * bpp;
    rawlen = (stride + 1) * (size_t)height;
    pix = (uint8_t *)malloc(stride * (size_t)height);
    raw = (uint8_t *)malloc(rawlen);
    if (!pix || !raw) { free(pix); free(raw); return 0; }
    if (bpp == 4) memcpy(pix, rgba, stride * (size_t)height);
    else for (size_t i = 0; i < (size_t)width * (size_t)height; i++) memcpy(pix + i * 3, rgba + i * 4, 3);
    for (y = 0; y < (size_t)height; y++) {
        const uint8_t *row = pix + y * stride, *up = y ? row - stride : NULL;
        uint8_t *dst = raw + y * (stride + 1) + 1;
        int best = 0;
        if (flags & GE_PNG_BEST) {   /* the usual heuristic: the filter whose output has the smallest sum of absolute bytes */
            size_t bestSum = (size_t)-1;
            uint8_t *cand = (uint8_t *)malloc(stride);
            if (cand) {
                for (int ft = 0; ft < 5; ft++) {
                    size_t sum = 0;
                    filterRow(ft, row, up, stride, bpp, cand);
                    for (size_t x = 0; x < stride; x++) sum += cand[x] < 128 ? cand[x] : 256u - cand[x];
                    if (sum < bestSum) { bestSum = sum; best = ft; }
                }
                free(cand);
            }
        }
        raw[y * (stride + 1)] = (uint8_t)best;
        filterRow(best, row, up, stride, bpp, dst);
    }
    free(pix);
    z = zDeflate(raw, rawlen, (flags & GE_PNG_BEST) ? 9 : 6, &zlen);
    free(raw);
    if (!z) return 0;
    buf = (uint8_t *)malloc(8 + 12 + 13 + 12 + zlen + 12);
    if (!buf) { free(z); return 0; }
    memcpy(buf, sig, 8);
    put32(ihdr, (uint32_t)width); put32(ihdr + 4, (uint32_t)height);
    ihdr[8] = 8; ihdr[9] = (flags & GE_PNG_RGB) ? 2 : 6; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    p = chunk(buf + 8, "IHDR", ihdr, 13);
    p = chunk(p, "IDAT", z, (uint32_t)zlen);
    p = chunk(p, "IEND", NULL, 0);
    free(z);
    *out = buf;
    if (outLen) *outLen = (size_t)(p - buf);
    return 1;
}

int gePngWrite(const char *path, int width, int height, const uint8_t *rgba, unsigned flags)
{
    uint8_t *buf = NULL;
    size_t len = 0;
    FILE *f;
    int ok;
    if (!gePngEncode(width, height, rgba, flags, &buf, &len)) return 0;
    f = fopen(path, "wb");
    if (!f) { free(buf); return 0; }
    ok = fwrite(buf, 1, len, f) == len;
    if (fclose(f)) ok = 0;
    free(buf);
    return ok;
}
