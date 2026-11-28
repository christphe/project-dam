/*
 * fonts.c -- the game's fonts from the asset packs (port/include/fonts.h, docs/specs/fonts.md).
 * The first part is pure (tests/test_fonts.c includes this file with FONTS_PURE); the game side reads the stack.
 */
#include "fonts.h"

#include <stdlib.h>
#include <string.h>

#define NGLYPHS 94
#define KERN_N 169
#define PIX_START 0xB80u

/* ---- a small reader for the fonts.json we write: finds `"key":` and reads what follows ---- */

static const char *skipws(const char *p, const char *e)
{
    while (p < e && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) p++;
    return p;
}

/* The value after `"key":` in [p, e), or NULL. */
static const char *jkey(const char *p, const char *e, const char *key)
{
    char pat[48];
    size_t n = strlen(key);
    if (n + 3 > sizeof pat) return NULL;
    pat[0] = '"'; memcpy(pat + 1, key, n); pat[n + 1] = '"'; n += 2;
    for (; p + n <= e; p++) {
        if (*p == '"' && !memcmp(p, pat, n)) {
            const char *q = skipws(p + n, e);
            if (q < e && *q == ':') return skipws(q + 1, e);
        }
    }
    return NULL;
}

/* End of the object or array starting at p (one past its closing bracket), or NULL. */
static const char *jextent(const char *p, const char *e)
{
    int depth = 0, instr = 0;
    if (p >= e || (*p != '{' && *p != '[')) return NULL;
    for (; p < e; p++) {
        if (instr) { if (*p == '\\') p++; else if (*p == '"') instr = 0; continue; }
        if (*p == '"') instr = 1;
        else if (*p == '{' || *p == '[') depth++;
        else if (*p == '}' || *p == ']') { if (--depth == 0) return p + 1; }
    }
    return NULL;
}

static int jint(const char *p, const char *e, const char *key, int *v)
{
    const char *q = jkey(p, e, key);
    char *end;
    long n;
    if (!q || q >= e) return 0;
    n = strtol(q, &end, 10);
    if (end == q) return 0;
    *v = (int)n;
    return 1;
}

/* The object of font `key` inside "fonts". */
static int jfont(const char *json, unsigned long jlen, const char *key, const char **s, const char **e)
{
    const char *end = json + jlen, *f = jkey(json, end, "fonts"), *fe, *o, *oe;
    if (!f || !(fe = jextent(f, end))) return 0;
    o = jkey(f, fe, key);
    if (!o || !(oe = jextent(o, fe))) return 0;
    *s = o; *e = oe;
    return 1;
}

static void be32(unsigned char *p, int v)
{
    unsigned u = (unsigned)v;
    p[0] = (unsigned char)(u >> 24); p[1] = (unsigned char)(u >> 16); p[2] = (unsigned char)(u >> 8); p[3] = (unsigned char)u;
}

static unsigned rd32(const unsigned char *p)
{
    return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) | ((unsigned)p[2] << 8) | p[3];
}

/* ---- Latin ---- */

typedef struct { int x, y, w, h, top, cls; unsigned stride, off; } Glyph;

unsigned fontsBuildLatin(const char *json, unsigned long jlen, const char *key, const unsigned char *grey, int w, int h,
                         unsigned char *dst, unsigned cap)
{
    const char *s, *e, *p, *pe;
    Glyph g[NGLYPHS];
    unsigned char seen[NGLYPHS];
    unsigned pos = PIX_START;
    int i, nseen = 0;

    if (!grey || w < 1 || h < 1 || !jfont(json, jlen, key, &s, &e)) return 0;
    memset(seen, 0, sizeof seen);
    memset(g, 0, sizeof g);
    p = jkey(s, e, "glyphs");
    if (!p || !(pe = jextent(p, e))) return 0;
    for (i = 0; i < NGLYPHS; i++) {
        const char *o = p, *oe;
        int code, k;
        while (o < pe && *o != '{') o++;
        if (o >= pe || !(oe = jextent(o, pe))) return 0;
        p = oe;
        {
            Glyph t;
            if (!jint(o, oe, "code", &code) || !jint(o, oe, "x", &t.x) || !jint(o, oe, "y", &t.y) || !jint(o, oe, "w", &t.w) ||
                !jint(o, oe, "h", &t.h) || !jint(o, oe, "top", &t.top) || !jint(o, oe, "class", &t.cls)) return 0;
            k = code - 33;
            if (k < 0 || k >= NGLYPHS || seen[k]) return 0;
            if (t.w < 1 || t.w > 64 || t.h < 1 || t.h > 64 || t.cls < 0 || t.cls > 12 || t.x < 0 || t.y < 0 || t.x + t.w > w ||
                t.y + t.h > h) return 0;
            seen[k] = 1; nseen++;
            g[k] = t;
        }
    }
    if (nseen != NGLYPHS) return 0;
    for (i = 0; i < NGLYPHS; i++) {
        g[i].stride = ((unsigned)g[i].w + 7u) & ~7u;
        g[i].off = pos;
        pos += g[i].stride * (unsigned)g[i].h + 8u;   /* the bitmap, then the 8-byte gap */
    }
    if (pos > cap) return 0;
    memset(dst, 0, pos);
    /* kerning: all zeros when the font has none */
    p = jkey(s, e, "kerning");
    if (p && (pe = jextent(p, e)) != NULL) {
        int n = 0;
        while (p < pe && n < KERN_N) {
            if (*p == '-' || (*p >= '0' && *p <= '9')) {
                char *end;
                long v = strtol(p, &end, 10);
                be32(dst + 4 * n++, (int)v);
                p = end;
            } else p++;
        }
        if (n != KERN_N) memset(dst, 0, 4 * KERN_N);
    }
    for (i = 0; i < NGLYPHS; i++) {
        unsigned char *r = dst + 4 * KERN_N + 24 * i;
        be32(r, i); be32(r + 4, g[i].top); be32(r + 8, g[i].h); be32(r + 12, g[i].w); be32(r + 16, g[i].cls); be32(r + 20, (int)g[i].off);
        for (int y = 0; y < g[i].h; y++) {
            unsigned char *d = dst + g[i].off + (unsigned)y * g[i].stride;
            const unsigned char *src = grey + (size_t)(g[i].y + y) * (size_t)w + (size_t)g[i].x;
            memcpy(d, src, (size_t)g[i].w);
            memset(d + g[i].w, src[g[i].w - 1], g[i].stride - (unsigned)g[i].w);   /* the padding repeats the last column */
        }
        if (i < NGLYPHS - 1) dst[g[i].off + g[i].stride * (unsigned)g[i].h] = 0xB8;   /* the marker before the next bitmap */
    }
    dst[PIX_START - 8] = 0xB8;
    return pos;
}

/* The block of a font with no ink: all 94 glyphs have zero width and height (nothing drawn; every glyph still advances by
 * the one-pixel gap, a space by 5), the line height kept by giving the glyph of '|' (record 91) top = `line`. `size` bytes,
 * as the engine expects (the ROM's block size). Returns `size`, 0 when it is too small. */
unsigned fontsBuildEmptyLatin(unsigned char *dst, unsigned size, int line)
{
    int i;
    if (size < PIX_START + 8) return 0;
    memset(dst, 0, size);
    for (i = 0; i < NGLYPHS; i++) {
        unsigned char *r = dst + 4 * KERN_N + 24 * i;
        be32(r, i); be32(r + 20, (int)PIX_START);   /* the pixels point at zeros inside the block */
    }
    be32(dst + 4 * KERN_N + 24 * 91 + 4, line);
    return size;
}

void fontsCompareLatin(const unsigned char *a, const unsigned char *b, unsigned n, unsigned *compared, unsigned *diffs)
{
    unsigned c = 0, d = 0;
    int i;
    if (n < PIX_START) { if (compared) *compared = 0; if (diffs) *diffs = 1; return; }
    for (i = 0; i < KERN_N; i++) { c++; if (rd32(a + 4 * i) != rd32(b + 4 * i)) d++; }
    for (i = 0; i < NGLYPHS; i++) {
        const unsigned char *ra = a + 4 * KERN_N + 24 * i, *rb = b + 4 * KERN_N + 24 * i;
        unsigned h = rd32(ra + 8), wd = rd32(ra + 12), oa = rd32(ra + 20), ob = rd32(rb + 20), stride = (wd + 7u) & ~7u, bad = 0;
        c++;
        if (memcmp(ra + 4, rb + 4, 16)) bad = 1;   /* top, height, width, class */
        else if (h > 64 || wd > 64 || oa + stride * h > n || ob + stride * h > n || memcmp(a + oa, b + ob, stride * h)) bad = 1;
        d += bad;
    }
    if (compared) *compared = c;
    if (diffs) *diffs = d;
}

/* ---- Japanese ---- */

int fontsBuildJpnPair(const char *json, unsigned long jlen, const char *key, const unsigned char *grey, int w, int h,
                      unsigned pair, unsigned char *dst, unsigned *bytes)
{
    const char *s, *e, *c;
    int cw, ch, cols, slots, slot;
    char *end;

    if (!grey || !jfont(json, jlen, key, &s, &e)) return 0;
    c = jkey(s, e, "cell");
    if (!c || *c != '[') return 0;
    cw = (int)strtol(c + 1, &end, 10);
    ch = (int)strtol(end + 1, &end, 10);
    if (!jint(s, e, "columns", &cols) || !jint(s, e, "slots", &slots)) return 0;
    if (cw != 16 || (ch != 12 && ch != 16) || cols < 1 || (unsigned long)pair * 2 + 1 >= (unsigned long)slots) return 0;
    if ((long)cols * cw > w || (long)((slots + cols - 1) / cols) * ch > h) return 0;
    memset(dst, 0, (size_t)ch * 8u);
    for (slot = 0; slot < 2; slot++) {
        int n = (int)pair * 2 + slot, x0 = (n % cols) * cw, y0 = (n / cols) * ch, sh = slot ? 2 : 0;
        for (int r = 0; r < ch; r++)
            for (int px = 0; px < 16; px++) {
                int lv = (grey[(size_t)(y0 + r) * (size_t)w + (size_t)(x0 + px)] + 42) / 85;   /* level * 85 back to 0..3 */
                if (lv > 3) lv = 3;
                dst[r * 8 + px / 2] = (unsigned char)(dst[r * 8 + px / 2] | ((lv << sh) << ((px & 1) ? 0 : 4)));
            }
    }
    *bytes = (unsigned)ch * 8u;
    return 1;
}

#ifndef FONTS_PURE
/* ------------------------------------------------------------------------------------------------------------------ */
/* the game side                                                                                                      */
/* ------------------------------------------------------------------------------------------------------------------ */
#include "texpack.h"
#include "ge_pkg.h"
#include "ge_png.h"

static int envOn(const char *v)   /* the value of a getenv: set and not "0" */
{
    return v && v[0] && v[0] != '0';
}

int fontsRomOnly(void)
{
    static int v = -1;
    if (v < 0) v = envOn(getenv("GE_ROM_FONTS"));
    return v;
}

int fontsCheckEnabled(void)
{
    static int v = -1;
    if (v < 0) v = envOn(getenv("GE_FONTS_CHECK"));
    return v;
}

typedef struct {
    int tried;
    unsigned char *jbuf; unsigned long jlen;   /* fonts.json (pkg buffer) */
    unsigned char *grey; int w, h;
} Atlas;

static Atlas s_jp[2];

static void atlasFree(Atlas *a)
{
    if (a->jbuf) pkgFree(a->jbuf);
    free(a->grey);
    memset(a, 0, sizeof *a);
}

/* Loads fonts.json and the picture of font `key`. 1 on success. */
static int atlasLoad(Atlas *a, const char *key)
{
    unsigned char *pb = NULL;
    unsigned long pl = 0;
    const char *s, *e, *im;
    char path[160];
    int w, h, i;
    unsigned char *rgba = NULL;

    atlasFree(a);
    a->tried = 1;
    if (!assetRead("fonts.json", &a->jbuf, &a->jlen, NULL)) return 0;
    if (!jfont((const char *)a->jbuf, a->jlen, key, &s, &e)) goto fail;
    im = jkey(s, e, "image");
    if (!im || *im != '"') goto fail;
    im++;
    for (i = 0; im + i < e && im[i] != '"' && i < (int)sizeof path - 1; i++) path[i] = im[i];
    path[i] = 0;
    if (!assetRead(path, &pb, &pl, NULL)) goto fail;
    if (!gePngDecode(pb, pl, &w, &h, &rgba)) { pkgFree(pb); goto fail; }
    pkgFree(pb);
    a->grey = malloc((size_t)w * (size_t)h);
    if (!a->grey) { free(rgba); goto fail; }
    for (i = 0; i < w * h; i++) a->grey[i] = rgba[(size_t)i * 4];   /* grey: R = G = B */
    free(rgba);
    a->w = w; a->h = h;
    return 1;
fail:
    atlasFree(a);
    a->tried = 1;
    return 0;
}

unsigned fontsLatinBlock(int which, unsigned char *dst, unsigned cap)
{
    Atlas a;
    unsigned n = 0;
    atlasFree(&s_jp[0]); atlasFree(&s_jp[1]);   /* a new stage load: the packs may have changed */
    if (fontsRomOnly()) return 0;
    memset(&a, 0, sizeof a);
    if (atlasLoad(&a, which ? "large" : "small"))
        n = fontsBuildLatin((const char *)a.jbuf, a.jlen, which ? "large" : "small", a.grey, a.w, a.h, dst, cap);
    atlasFree(&a);
    return n;
}

int fontsJpnPair(int wide, unsigned pair, unsigned char *dst, unsigned *bytes)
{
    Atlas *a = &s_jp[wide ? 1 : 0];
    if (fontsRomOnly()) return 0;
    if (!a->tried) atlasLoad(a, wide ? "jp_wide" : "jp_narrow");
    if (!a->grey) return 0;
    return fontsBuildJpnPair((const char *)a->jbuf, a->jlen, wide ? "jp_wide" : "jp_narrow", a->grey, a->w, a->h, pair, dst, bytes);
}
#endif
