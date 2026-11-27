/*
 * tex_decode.c -- GoldenEye texture bank decoder (issue #150).
 * Clean room: written from docs/specs/textures.md only. libc + zlib.
 */
#include "export.h"

#include <stdlib.h>
#include <string.h>
#include <zlib.h>

/* ---- the measured per-region constants (textures.md); the only per-region place ---- */
#define TEX_COUNT        2698u
#define TEX_TOTAL        0x2EEF18u
static const struct { const char *region; uint32_t cdata_rom, table_off, images_start; } REGIONS[] = {
    { "ntsc-final", 0x21990u, 0x28570u, 0x8F7DF0u },
    { "pal-final",  0x1F850u, 0x22C30u, 0x8F7840u },
    { "jpn-final",  0x219D0u, 0x28560u, 0x8F9050u },
};
#define CDATA_MAX_OUT    (8u * 1024u * 1024u)

/* ---- raw DEFLATE after 2 header bytes (any header value accepted) -------- */

/* Inflates in[0..n) (two header bytes skipped). Output capped at max_out.
 * 0 on success (*len may be < max_out when the stream ended), -1 on error. */
static int inflate_rz(const uint8_t *in, size_t n, size_t max_out, uint8_t **out, size_t *len)
{
    z_stream z;
    uint8_t *buf;
    size_t cap, got = 0;
    int r = Z_OK;
    if (n < 2) return -1;
    cap = max_out < 65536 ? (max_out ? max_out : 1) : 65536;
    buf = (uint8_t *)malloc(cap);
    if (!buf) return -1;
    memset(&z, 0, sizeof z);
    if (inflateInit2(&z, -15) != Z_OK) { free(buf); return -1; }
    z.next_in = (Bytef *)(in + 2);
    z.avail_in = (uInt)(n - 2);
    while (got < max_out) {
        if (got == cap) {
            size_t nc = cap * 2 > max_out ? max_out : cap * 2;
            uint8_t *nb = (uint8_t *)realloc(buf, nc);
            if (!nb) { inflateEnd(&z); free(buf); return -1; }
            buf = nb; cap = nc;
        }
        z.next_out = buf + got;
        z.avail_out = (uInt)(cap - got);
        r = inflate(&z, Z_NO_FLUSH);
        got = cap - z.avail_out;
        if (r == Z_STREAM_END) break;
        if (r == Z_BUF_ERROR && z.avail_in == 0) break;
        if (r != Z_OK && r != Z_BUF_ERROR) { inflateEnd(&z); free(buf); return -1; }
    }
    inflateEnd(&z);
    *out = buf; *len = got;
    return 0;
}

/* ---- table --------------------------------------------------------------- */

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

int ex_tex_table_open(ExTexTable *t, const uint8_t *rom, size_t rom_size, const char *region, const char **err)
{
    const char *dummy;
    uint8_t *cd = NULL;
    uint32_t cdrom, toff, istart;
    size_t cdlen = 0, i, cnt;
    uint64_t sum = 0;
    if (!err) err = &dummy;
    memset(t, 0, sizeof *t);
    for (i = 0; i < sizeof REGIONS / sizeof REGIONS[0]; i++)
        if (region && !strcmp(region, REGIONS[i].region)) break;
    if (i == sizeof REGIONS / sizeof REGIONS[0]) {
        *err = "textures unavailable: unknown region";
        return -1;
    }
    cdrom = REGIONS[i].cdata_rom; toff = REGIONS[i].table_off; istart = REGIONS[i].images_start;
    if (!rom || rom_size <= cdrom) { *err = "ROM too small"; return -1; }
    if (inflate_rz(rom + cdrom, rom_size - cdrom, CDATA_MAX_OUT, &cd, &cdlen)) {
        *err = "cdata segment does not inflate"; return -1;
    }
    if (cdlen < toff + 8) { free(cd); *err = "cdata too short for the size table"; return -1; }
    /* count entries up to the terminator */
    cnt = 0;
    for (i = toff; i + 8 <= cdlen; i += 8, cnt++)
        if ((be32(cd + i) & 0xFFFFFFu) == 0xFFFFu) break;
    if (i + 8 > cdlen) { free(cd); *err = "size table has no terminator"; return -1; }
    if (cnt != TEX_COUNT) { free(cd); *err = "size table entry count mismatch"; return -1; }
    t->offset = (uint32_t *)malloc((cnt + 1) * sizeof(uint32_t));
    t->word1 = (uint32_t *)malloc(cnt * sizeof(uint32_t));
    if (!t->offset || !t->word1) { free(cd); ex_tex_table_close(t); *err = "out of memory"; return -1; }
    for (i = 0; i < cnt; i++) {
        const uint8_t *e = cd + toff + i * 8;
        t->offset[i] = (uint32_t)sum;
        sum += be32(e) & 0xFFFFFFu;
        t->word1[i] = be32(e + 4);
    }
    t->offset[cnt] = (uint32_t)sum;
    free(cd);
    if (sum != TEX_TOTAL) { ex_tex_table_close(t); *err = "size table total mismatch"; return -1; }
    if ((uint64_t)istart + sum > rom_size) { ex_tex_table_close(t); *err = "images segment beyond ROM end"; return -1; }
    t->rom = rom; t->rom_size = rom_size;
    t->images_start = istart;
    t->count = (uint32_t)cnt;
    return 0;
}

void ex_tex_table_close(ExTexTable *t)
{
    free(t->offset);
    free(t->word1);
    memset(t, 0, sizeof *t);
}

/* ---- bit reader ---------------------------------------------------------- */

typedef struct { const uint8_t *p; size_t nbits, pos; int err; } Br;

static uint32_t br_get(Br *b, int k)
{
    uint32_t v = 0;
    int i;
    for (i = 0; i < k; i++) {
        v <<= 1;
        if (b->pos >= b->nbits) { b->err = 1; continue; }
        v |= (b->p[b->pos >> 3] >> (7 - (b->pos & 7))) & 1u;
        b->pos++;
    }
    return v;
}

/* ---- format tables (textures.md) ----------------------------------------- */

static const int F_CHANNELS[13] = { 4, 3, 3, 3, 2, 2, 1, 1, 1, 1, 1, 1, 1 };
static const int F_APLANE[13]   = { 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0 };
static const int F_RANGE[13]    = { 256, 32, 256, 32, 256, 16, 8, 256, 16, 256, 16, 256, 16 };

static uint8_t x5(uint32_t v) { return (uint8_t)(v * 255 / 31); }
static uint8_t x4(uint32_t v) { return (uint8_t)(v * 17); }
static uint8_t x3(uint32_t v) { return (uint8_t)(v * 255 / 7); }

/* One texel in the format's own layout (raw texels and lookup entries). */
static void read_texel(Br *b, int fmt, uint8_t o[4])
{
    uint32_t v;
    switch (fmt) {
    case 0: o[0] = (uint8_t)br_get(b, 8); o[1] = (uint8_t)br_get(b, 8);
            o[2] = (uint8_t)br_get(b, 8); o[3] = (uint8_t)br_get(b, 8); break;
    case 1: v = br_get(b, 16);
            o[0] = x5(v >> 11 & 31); o[1] = x5(v >> 6 & 31); o[2] = x5(v >> 1 & 31); o[3] = (v & 1) ? 255 : 0; break;
    case 2: o[0] = (uint8_t)br_get(b, 8); o[1] = (uint8_t)br_get(b, 8); o[2] = (uint8_t)br_get(b, 8); o[3] = 255; break;
    case 3: v = br_get(b, 15);
            o[0] = x5(v >> 10 & 31); o[1] = x5(v >> 5 & 31); o[2] = x5(v & 31); o[3] = 255; break;
    case 4: v = br_get(b, 16); o[0] = o[1] = o[2] = (uint8_t)(v >> 8); o[3] = (uint8_t)v; break;
    case 5: v = br_get(b, 8); o[0] = o[1] = o[2] = x4(v >> 4); o[3] = x4(v & 15); break;
    case 6: v = br_get(b, 4); o[0] = o[1] = o[2] = x3(v >> 1); o[3] = (v & 1) ? 255 : 0; break;
    case 7: v = br_get(b, 8); o[0] = o[1] = o[2] = o[3] = (uint8_t)v; break;
    default: v = br_get(b, 4); o[0] = o[1] = o[2] = o[3] = x4(v); break;
    }
}

/* Assemble one texel from channel values c[] and the 1-bit alpha plane bit. */
static void assemble(int fmt, const uint16_t *c, int abit, uint8_t o[4])
{
    switch (fmt) {
    case 0: o[0] = (uint8_t)c[0]; o[1] = (uint8_t)c[1]; o[2] = (uint8_t)c[2]; o[3] = (uint8_t)c[3]; break;
    case 1: o[0] = x5(c[0]); o[1] = x5(c[1]); o[2] = x5(c[2]); o[3] = abit ? 255 : 0; break;
    case 2: o[0] = (uint8_t)c[0]; o[1] = (uint8_t)c[1]; o[2] = (uint8_t)c[2]; o[3] = 255; break;
    case 3: o[0] = x5(c[0]); o[1] = x5(c[1]); o[2] = x5(c[2]); o[3] = 255; break;
    case 4: o[0] = o[1] = o[2] = (uint8_t)c[0]; o[3] = (uint8_t)c[1]; break;
    case 5: o[0] = o[1] = o[2] = x4(c[0]); o[3] = x4(c[1]); break;
    case 6: o[0] = o[1] = o[2] = x3(c[0]); o[3] = abit ? 255 : 0; break;
    case 7: o[0] = o[1] = o[2] = o[3] = (uint8_t)c[0]; break;
    default: o[0] = o[1] = o[2] = o[3] = x4(c[0]); break;
    }
}

/* ---- Huffman (the exact merge rules of textures.md) ---------------------- */

#define H_EMPTY 9999
#define H_LEAF  10000
typedef struct { int w[2048]; int ch[2048][2]; int root; int n; } Tree;

static int tree_build(Tree *t, Br *b, int n)
{
    int i;
    if (n < 1 || n > 2047) return -1;
    t->n = n; t->root = -1;
    for (i = 0; i < n; i++) { t->w[i] = (int)br_get(b, 8); t->ch[i][0] = t->ch[i][1] = -1; }
    if (b->err) return -1;
    for (;;) {
        int a = -1, bb = -1, wa = H_EMPTY, wb = H_EMPTY, sum, abare, bbare;
        for (i = 0; i < n; i++) {
            int x = t->w[i];
            if (x < wa) {
                if (wa > wb) { a = i; wa = x; } else { bb = i; wb = x; }
            } else if (x < wb) { bb = i; wb = x; }
        }
        if (a < 0 || bb < 0) break;
        sum = wa + wb;
        if (sum == 0) sum = 1;
        abare = t->ch[a][0] < 0 && t->ch[a][1] < 0;
        bbare = t->ch[bb][0] < 0 && t->ch[bb][1] < 0;
        t->w[a] = H_EMPTY; t->w[bb] = H_EMPTY;
        if (abare) {
            t->ch[a][0] = a + H_LEAF;
            t->ch[a][1] = bbare ? bb + H_LEAF : bb;
            t->w[a] = sum; t->root = a;
        } else if (bbare) {
            t->ch[bb][0] = bb + H_LEAF;
            t->ch[bb][1] = a;
            t->w[bb] = sum; t->root = bb;
        } else {
            int s;
            for (s = 0; s < n; s++)
                if (t->ch[s][0] < 0 && t->ch[s][1] < 0 && t->w[s] == H_EMPTY) break;
            if (s == n) return -1;
            t->ch[s][0] = a; t->ch[s][1] = bb; t->w[s] = sum; t->root = s;
        }
    }
    return 0;
}

static int tree_sym(const Tree *t, Br *b)
{
    int node = t->root, guard = 0;
    if (node < 0) return t->n == 1 ? 0 : -1;
    while (node < H_LEAF) {
        if (node < 0 || guard++ > 6000) return -1;
        node = t->ch[node][br_get(b, 1)];
        if (b->err) return -1;
    }
    return node - H_LEAF;
}

static int huff_values(Br *b, int nsym, uint16_t *out, size_t count)
{
    Tree *t = (Tree *)malloc(sizeof *t);
    size_t i;
    int r = 0;
    if (!t) return -1;
    if (tree_build(t, b, nsym)) { free(t); return -1; }
    for (i = 0; i < count; i++) {
        int s = tree_sym(t, b);
        if (s < 0) { r = -1; break; }
        out[i] = (uint16_t)s;
    }
    free(t);
    return r;
}

/* ---- run coding ---------------------------------------------------------- */

static int run_values(Br *b, uint16_t *out, size_t target)
{
    int back = (int)br_get(b, 3), len = (int)br_get(b, 3), vb = (int)br_get(b, 4);
    int extra = 0, s = back + len + vb + 1;
    size_t n = 0;
    while (s > 0) { s -= vb + 1; extra++; }
    while (n < target) {
        if (b->err) return -1;
        if (br_get(b, 1) == 0) {
            out[n++] = (uint16_t)br_get(b, vb);
        } else {
            size_t d = (size_t)br_get(b, back) + 1, l = (size_t)br_get(b, len) + (size_t)extra, k;
            uint16_t lit;
            if (d > n) return -1;
            for (k = 0; k < l && n < target; k++, n++) out[n] = out[n - d];
            lit = (uint16_t)br_get(b, vb);
            if (n < target) out[n++] = lit;
        }
    }
    return b->err ? -1 : 0;
}

/* ---- predictor ----------------------------------------------------------- */

static void unpredict(uint16_t *v, int w, int rows, int range, int id)
{
    int x, y;
    if (id == 7) return;
    for (y = 0; y < rows; y++)
        for (x = 0; x < w; x++) {
            int l = x > 0 ? v[y * w + x - 1] : 0;
            int a = y > 0 ? v[(y - 1) * w + x] : 0;
            int al = (x > 0 && y > 0) ? v[(y - 1) * w + x - 1] : 0;
            int p = 0;
            switch (id) {
            case 0: p = l; break;
            case 1: p = a; break;
            case 2: p = al; break;
            case 3: p = l + a - al; break;
            case 4: p = (a - al) / 2 + l; break;
            case 5: p = (l - al) / 2 + a; break;
            case 6: p = (l + a) / 2; break;
            }
            v[y * w + x] = (uint16_t)((v[y * w + x] + 2 * range + p) % range);
        }
}

static int bits_for(int c)
{
    int n = 0;
    while ((1 << n) < c) n++;
    return n;
}

/* ---- decode -------------------------------------------------------------- */

void ex_tex_free(ExTexture *tex)
{
    if (tex) { free(tex->rgba); tex->rgba = NULL; }
}

static int dec_zlib(const uint8_t *d, size_t size, ExTexture *o, const char **err)
{
    Br b;
    uint8_t pal[256][4];
    uint8_t *idx = NULL;
    size_t idxlen = 0, expect, bytepos, i;
    int fmt, pc, w, h, k;
    b.p = d + 1; b.nbits = (size - 1) * 8; b.pos = 0; b.err = 0;
    fmt = (int)br_get(&b, 8);
    pc = (int)br_get(&b, 8) + 1;
    if (fmt < 9 || fmt > 12) { *err = "zlib family: not a palette format"; return -1; }
    for (k = 0; k < pc; k++) {
        uint32_t v = br_get(&b, 16);
        if (fmt <= 10) {
            pal[k][0] = x5(v >> 11 & 31); pal[k][1] = x5(v >> 6 & 31); pal[k][2] = x5(v >> 1 & 31);
            pal[k][3] = (v & 1) ? 255 : 0;
        } else {
            pal[k][0] = pal[k][1] = pal[k][2] = (uint8_t)(v >> 8); pal[k][3] = (uint8_t)v;
        }
    }
    w = (int)br_get(&b, 8); h = (int)br_get(&b, 8);
    if (b.err) { *err = "zlib family: header truncated"; return -1; }
    if (w < 1 || h < 1) { *err = "zero texture size"; return -1; }
    bytepos = 1 + (b.pos + 7) / 8;
    if (bytepos + 2 > size) { *err = "zlib family: no pixel block"; return -1; }
    expect = (fmt == 9 || fmt == 11) ? (size_t)w * h : ((size_t)w * h + 1) / 2;
    if (inflate_rz(d + bytepos, size - bytepos, expect, &idx, &idxlen)) { *err = "zlib family: bad deflate block"; return -1; }
    if (idxlen < expect) { free(idx); *err = "zlib family: pixel block too short"; return -1; }
    o->width = w; o->height = h; o->format = fmt;
    o->rgba = (uint8_t *)malloc((size_t)w * h * 4);
    if (!o->rgba) { free(idx); *err = "out of memory"; return -1; }
    for (i = 0; i < (size_t)w * h; i++) {
        int ix = (fmt == 9 || fmt == 11) ? idx[i] : ((i & 1) ? (idx[i >> 1] & 15) : (idx[i >> 1] >> 4));
        if (ix >= pc) { free(idx); free(o->rgba); o->rgba = NULL; *err = "palette index out of range"; return -1; }
        memcpy(o->rgba + i * 4, pal[ix], 4);
    }
    free(idx);
    return 0;
}

static int dec_packed(const uint8_t *d, size_t size, ExTexture *o, const char **err)
{
    Br b;
    int fmt, w, h, method, ch, range, ap, pred = 0;
    size_t n, i, total;
    uint16_t *pl = NULL;
    uint8_t *px = NULL;
    uint8_t (*lut)[4] = NULL;
    uint8_t *abits = NULL;
    int rc = -1;
    b.p = d + 1; b.nbits = (size - 1) * 8; b.pos = 0; b.err = 0;
    fmt = (int)br_get(&b, 4);
    w = (int)br_get(&b, 8); h = (int)br_get(&b, 8);
    method = (int)br_get(&b, 4);
    if (b.err) { *err = "bit-packed: header truncated"; return -1; }
    if (fmt > 8) { *err = "bit-packed: palette format not specified"; return -1; }
    if (w < 1 || h < 1) { *err = "zero texture size"; return -1; }
    if (method > 9) { *err = "unknown method"; return -1; }
    ch = F_CHANNELS[fmt]; range = F_RANGE[fmt]; ap = F_APLANE[fmt];
    n = (size_t)w * h;
    o->width = w; o->height = h; o->format = fmt; o->method = method;
    px = (uint8_t *)malloc(n * 4);
    if (!px) { *err = "out of memory"; return -1; }

    if (method <= 1) {
        int x, y;
        for (y = 0; y < h; y++)
            for (x = 0; x < w; x++) {
                uint8_t *o4 = px + ((size_t)y * w + x) * 4;
                read_texel(&b, fmt, o4);
                if ((fmt == 6 || fmt == 8) && x == w - 1 && (w & 1)) br_get(&b, 4); /* pad nibble */
            }
        if (b.err) { *err = "raw texels truncated"; goto out; }
    } else if (method == 5 || method == 6 || method == 7) {
        uint16_t *ix = (uint16_t *)malloc(n * 2);
        int c = (int)br_get(&b, 11), k;
        if (!ix) { *err = "out of memory"; goto out; }
        lut = (uint8_t (*)[4])malloc((size_t)(c ? c : 1) * 4);
        if (!lut || c < 1) { free(ix); *err = c < 1 ? "empty lookup table" : "out of memory"; goto out; }
        for (k = 0; k < c; k++) read_texel(&b, fmt, lut[k]);
        if (b.err) { free(ix); *err = "lookup table truncated"; goto out; }
        if (method == 5) {
            int nb = bits_for(c);
            for (i = 0; i < n; i++) ix[i] = (uint16_t)br_get(&b, nb);
        } else if (method == 6) {
            if (huff_values(&b, c, ix, n)) { free(ix); *err = "huffman decode failed"; goto out; }
        } else {
            if (run_values(&b, ix, n)) { free(ix); *err = "run decode failed"; goto out; }
        }
        if (b.err) { free(ix); *err = "index stream truncated"; goto out; }
        for (i = 0; i < n; i++) {
            if (ix[i] >= c) { free(ix); *err = "lookup index out of range"; goto out; }
            memcpy(px + i * 4, lut[ix[i]], 4);
        }
        free(ix);
    } else {
        /* planes: methods 2, 3, 4, 8, 9 */
        int p;
        total = n * (size_t)ch;
        pl = (uint16_t *)malloc(total * 2);
        if (!pl) { *err = "out of memory"; goto out; }
        if (method == 8 || method == 9) pred = (int)br_get(&b, 3);
        if (method == 2 || method == 8) {
            if (huff_values(&b, range, pl, total)) { *err = "huffman decode failed"; goto out; }
        } else if (method == 3) {
            for (p = 0; p < ch; p++)
                if (huff_values(&b, range, pl + (size_t)p * n, n)) { *err = "huffman decode failed"; goto out; }
        } else {
            if (run_values(&b, pl, total)) { *err = "run decode failed"; goto out; }
        }
        for (i = 0; i < total; i++)
            if (pl[i] >= range) { *err = "channel value out of range"; goto out; }
        if (method >= 8) unpredict(pl, w, ch * h, range, pred);
        if (ap) {
            abits = (uint8_t *)malloc(n);
            if (!abits) { *err = "out of memory"; goto out; }
            for (i = 0; i < n; i++) abits[i] = (uint8_t)br_get(&b, 1);
        }
        if (b.err) { *err = "plane data truncated"; goto out; }
        for (i = 0; i < n; i++) {
            uint16_t c[4];
            for (p = 0; p < ch; p++) c[p] = pl[(size_t)p * n + i];
            assemble(fmt, c, abits ? abits[i] : 0, px + i * 4);
        }
    }
    o->rgba = px; px = NULL;
    rc = 0;
out:
    free(px); free(pl); free(lut); free(abits);
    return rc;
}

int ex_tex_decode(const ExTexTable *t, int texnum, ExTexture *out, const char **err)
{
    const char *dummy;
    const uint8_t *d;
    size_t size;
    uint8_t b0;
    int rc;
    if (!err) err = &dummy;
    memset(out, 0, sizeof *out);
    out->texnum = texnum; out->method = -1;
    if (!t || !t->offset || texnum < 0 || (uint32_t)texnum >= t->count) { *err = "texture number out of range"; return -1; }
    size = t->offset[texnum + 1] - t->offset[texnum];
    if ((uint64_t)t->images_start + t->offset[texnum] + size > t->rom_size) { *err = "texture beyond ROM end"; return -1; }
    if (size < 2) { *err = "texture too small"; return -1; }
    d = t->rom + t->images_start + t->offset[texnum];
    b0 = d[0];
    out->explicit_lods = b0 >> 7;
    out->zlib = (b0 >> 6) & 1;
    rc = out->zlib ? dec_zlib(d, size, out, err) : dec_packed(d, size, out, err);
    if (rc) { free(out->rgba); out->rgba = NULL; return -1; }
    if (out->zlib) out->method = -1;
    return 0;
}
