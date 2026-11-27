/* The one PNG reader and writer (port/assets/ge_png.c, ge_assets): round trips in every writer mode, every colour type and row
 * filter the reader accepts, and what it refuses. Pure, no core. */
#include "ge_test.h"
#include "ge_png.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const uint8_t kSig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };

static void be(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }

static uint8_t *chunkAt(uint8_t *p, const char *type, const uint8_t *data, uint32_t n)
{
    be(p, n);
    memcpy(p + 4, type, 4);
    if (n) memcpy(p + 8, data, n);
    be(p + 8 + n, 0);   /* the reader does not check the chunk CRC */
    return p + 12 + n;
}

/* A PNG by hand: one IHDR (depth, ctype, interlace), optional PLTE / tRNS, one IDAT of `raw` (filter bytes included). */
static uint8_t *handmade(int w, int h, int depth, int ctype, int inter, const uint8_t *plte, int nplte, const uint8_t *trns, int ntrns,
                         const uint8_t *raw, size_t rawlen, size_t *outLen)
{
    uint8_t *buf = (uint8_t *)malloc(4096), *p = buf;
    uint8_t z[2048], ihdr[13];
    size_t zl = 0;
    uint32_t a = 1, b = 0;
    memcpy(p, kSig, 8);
    p += 8;
    be(ihdr, (uint32_t)w); be(ihdr + 4, (uint32_t)h);
    ihdr[8] = (uint8_t)depth; ihdr[9] = (uint8_t)ctype; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = (uint8_t)inter;
    p = chunkAt(p, "IHDR", ihdr, 13);
    if (plte) p = chunkAt(p, "PLTE", plte, (uint32_t)nplte);
    if (trns) p = chunkAt(p, "tRNS", trns, (uint32_t)ntrns);
    z[zl++] = 0x78; z[zl++] = 0x01;   /* zlib header, one stored deflate block, adler32 */
    z[zl++] = 1; z[zl++] = (uint8_t)rawlen; z[zl++] = (uint8_t)(rawlen >> 8); z[zl++] = (uint8_t)~rawlen; z[zl++] = (uint8_t)(~rawlen >> 8);
    memcpy(z + zl, raw, rawlen);
    zl += rawlen;
    for (size_t i = 0; i < rawlen; i++) { a = (a + raw[i]) % 65521; b = (b + a) % 65521; }
    be(z + zl, (b << 16) | a);
    zl += 4;
    p = chunkAt(p, "IDAT", z, (uint32_t)zl);
    p = chunkAt(p, "IEND", NULL, 0);
    *outLen = (size_t)(p - buf);
    return buf;
}

int main(void)
{
    enum { W = 7, H = 5 };
    uint8_t src[W * H * 4], *png = NULL, *back = NULL;
    size_t len = 0;
    int w = 0, h = 0;
    char path[600];
    const char *t = getenv("TEMP");

    GE_TEST_BEGIN("ge_png");
    for (int i = 0; i < W * H * 4; i++) src[i] = (uint8_t)(i * 37 + 11);

    /* the writer's modes, read back */
    for (unsigned flags = 0; flags < 4; flags++) {
        GE_CHECK(gePngEncode(W, H, src, flags, &png, &len) == 1);
        GE_CHECK(len > 8 && !memcmp(png, kSig, 8));
        GE_CHECK(gePngDecode(png, len, &w, &h, &back) == 1);
        GE_CHECK(w == W && h == H);
        if (back && w == W && h == H) {
            for (int i = 0; i < W * H; i++) {
                const int same3 = !memcmp(back + i * 4, src + i * 4, 3);
                const int a = (flags & GE_PNG_RGB) ? back[i * 4 + 3] == 255 : back[i * 4 + 3] == src[i * 4 + 3];
                if (!same3 || !a) { GE_FAIL_("pixel %d differs (flags %u)", i, flags); break; }
            }
            GE_CHECK((int)png[25] == ((flags & GE_PNG_RGB) ? 2 : 6));   /* IHDR colour type */
        }
        free(png); free(back); png = back = NULL;
    }
    GE_CHECK(gePngEncode(0, H, src, 0, &png, &len) == 0);
    GE_CHECK(gePngEncode(W, H, NULL, 0, &png, &len) == 0);

    /* a file */
    if (!t || !*t) t = getenv("TMPDIR");
    if (!t || !*t) t = "/tmp";
    snprintf(path, sizeof path, "%s/ge_png_test.png", t);
    GE_CHECK(gePngWrite(path, W, H, src, GE_PNG_BEST) == 1);
    GE_CHECK(gePngRead(path, &w, &h, &back) == 1 && w == W && h == H && back && !memcmp(back, src, sizeof src));
    free(back); back = NULL;
    remove(path);
    GE_CHECK(gePngRead(path, &w, &h, &back) == 0 && back == NULL);

    /* the reader: grey with filter 1 (sub), grey+alpha, RGB with filter 4 (paeth), palette with tRNS */
    {
        const uint8_t g[] = { 1, 10, 5, 5, 0, 7, 7, 7 };   /* 3x2 grey: row 0 sub (10, 15, 20), row 1 none (7 7 7) */
        const uint8_t ga[] = { 0, 9, 200, 8, 100 };        /* 2x1 grey+alpha */
        const uint8_t rgb[] = { 0, 10, 20, 30, 1, 2, 3,   4, 0, 0, 0, 0, 0, 0 };   /* 2x2 RGB, row 1 paeth (all deltas 0: each pixel takes the paeth predictor) */
        const uint8_t pal[] = { 0, 1, 0, 0 };              /* 3x1 palette: filter 0, indices 1 0 0 */
        const uint8_t plte[] = { 255, 0, 0, 0, 255, 0 }, trns[] = { 128 };
        uint8_t *f;
        f = handmade(3, 2, 8, 0, 0, NULL, 0, NULL, 0, g, sizeof g, &len);
        GE_CHECK(gePngDecode(f, len, &w, &h, &back) == 1 && w == 3 && h == 2);
        if (back) { GE_CHECK(back[0] == 10 && back[4] == 15 && back[8] == 20 && back[12] == 7 && back[3] == 255); }
        free(f); free(back); back = NULL;
        f = handmade(2, 1, 8, 4, 0, NULL, 0, NULL, 0, ga, sizeof ga, &len);
        GE_CHECK(gePngDecode(f, len, &w, &h, &back) == 1 && back && back[0] == 9 && back[1] == 9 && back[3] == 200 && back[7] == 100);
        free(f); free(back); back = NULL;
        f = handmade(2, 2, 8, 2, 0, NULL, 0, NULL, 0, rgb, sizeof rgb, &len);
        GE_CHECK(gePngDecode(f, len, &w, &h, &back) == 1 && back && back[0] == 10 && back[2] == 30 && back[4] == 1 && back[6] == 3 && back[8] == 10 && back[10] == 30 && back[12] == 1 && back[14] == 3);   /* paeth picks the row above */
        free(f); free(back); back = NULL;
        f = handmade(3, 1, 8, 3, 0, plte, sizeof plte, trns, sizeof trns, pal, sizeof pal, &len);
        GE_CHECK(gePngDecode(f, len, &w, &h, &back) == 1 && back && back[0] == 0 && back[1] == 255 && back[3] == 255 && back[4] == 255 && back[7] == 128);
        free(f); free(back); back = NULL;
        /* what it refuses: interlaced, 16-bit, an index past the palette, truncated, not a PNG */
        f = handmade(3, 2, 8, 0, 1, NULL, 0, NULL, 0, g, sizeof g, &len);
        GE_CHECK(gePngDecode(f, len, &w, &h, &back) == 0 && back == NULL);
        free(f);
        f = handmade(3, 2, 16, 0, 0, NULL, 0, NULL, 0, g, sizeof g, &len);
        GE_CHECK(gePngDecode(f, len, &w, &h, &back) == 0);
        free(f);
        {
            const uint8_t bad[] = { 0, 5, 0, 0 };
            f = handmade(3, 1, 8, 3, 0, plte, sizeof plte, NULL, 0, bad, sizeof bad, &len);
            GE_CHECK(gePngDecode(f, len, &w, &h, &back) == 0);
            free(f);
        }
        GE_CHECK(gePngEncode(W, H, src, 0, &png, &len) == 1);
        GE_CHECK(gePngDecode(png, len / 2, &w, &h, &back) == 0 && back == NULL);
        free(png); png = NULL;
        GE_CHECK(gePngDecode((const uint8_t *)"not a png at all, really not, nope....", 40, &w, &h, &back) == 0);
    }
    return GE_TEST_END();
}
