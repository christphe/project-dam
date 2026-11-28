/* The fonts reader (port/src/fonts.c, docs/specs/fonts.md): a synthetic Latin font and a synthetic cell font are built
 * as the exporter writes them, then rebuilt as the engine's blocks. Pure. */
#define FONTS_PURE 1
#include "ge_test.h"
#include "../port/src/fonts.c"

#include <stdio.h>

#define CW 6
#define CH 5

static unsigned be(const unsigned char *p) { return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) | ((unsigned)p[2] << 8) | p[3]; }

int main(void)
{
    static char json[40000];
    static unsigned char atlas[16 * CW * 6 * CH], blk[9000], blk2[9000];
    static unsigned char jatlas[32 * 16 * 3 * 12];
    int n = 0, i, w = 16 * CW, h = 6 * CH;
    unsigned size, c, d, bytes = 0;
    unsigned char pair[0x80];

    GE_TEST_BEGIN("fonts");

    /* Latin: glyph i is (i % 4 + 1) wide, (i % 3 + 2) high, class i % 13, top i % 5; pixel = i + col + row */
    n += snprintf(json + n, sizeof json - n, "{\"format\":\"ge007-fonts/1\",\"fonts\":{\"small\":{\"kind\":\"latin\",\"image\":\"fonts/small.png\",\"line_height\":13,\"first_code\":33,\"kerning\":[");
    for (i = 0; i < 13; i++) {
        int j;
        n += snprintf(json + n, sizeof json - n, "%s[", i ? "," : "");
        for (j = 0; j < 13; j++) n += snprintf(json + n, sizeof json - n, "%s%d", j ? "," : "", (i + j) % 3 - 1);
        n += snprintf(json + n, sizeof json - n, "]");
    }
    n += snprintf(json + n, sizeof json - n, "],\"glyphs\":[");
    for (i = 0; i < 94; i++) {
        int gw = i % 4 + 1, gh = i % 3 + 2, x = (i % 16) * CW, y = (i / 16) * CH, r, cc;
        n += snprintf(json + n, sizeof json - n, "%s{ \"code\": %d, \"x\": %d, \"y\": %d, \"w\": %d, \"h\": %d, \"top\": %d, \"class\": %d }", i ? "," : "", 33 + i, x, y, gw, gh, i % 5 - 1, i % 13);
        for (r = 0; r < gh; r++) for (cc = 0; cc < gw; cc++) atlas[(y + r) * w + x + cc] = (unsigned char)(i + cc * 3 + r * 7 + 1);
    }
    n += snprintf(json + n, sizeof json - n, "]},\"jp_narrow\":{\"kind\":\"cells\",\"image\":\"fonts/jp_narrow.png\",\"cell\":[16, 12],\"columns\":32,\"slots\":64,\"draw\":[11, 12],\"advance\":12,\"top\":0,\"limit\":967,\"fallback\":2}}}");

    size = fontsBuildLatin(json, (unsigned long)n, "small", atlas, w, h, blk, sizeof blk);
    GE_CHECK(size > 0xB80);
    GE_CHECK_EQ_INT((int)be(blk), 0 % 3 - 1);                 /* kerning [0][0] */
    GE_CHECK_EQ_INT((int)be(blk + 4 * (13 * 4 + 5)), (4 + 5) % 3 - 1);   /* [4][5] */
    {
        const unsigned char *r = blk + 676 + 24 * 6;           /* record 6: w 3, h 2, top 1, class 6 */
        GE_CHECK_EQ_INT((int)be(r + 4), 6 % 5 - 1);
        GE_CHECK_EQ_INT((int)be(r + 8), 2);
        GE_CHECK_EQ_INT((int)be(r + 12), 3);
        GE_CHECK_EQ_INT((int)be(r + 16), 6);
        GE_CHECK_EQ_INT((int)be(r + 20) % 8, 0);
        GE_CHECK_EQ_INT(blk[be(r + 20)], 6 + 1);                       /* pixel (0,0) */
        GE_CHECK_EQ_INT(blk[be(r + 20) + 2], 6 + 6 + 1);               /* pixel (2,0) */
        GE_CHECK_EQ_INT(blk[be(r + 20) + 3], blk[be(r + 20) + 2]);     /* the padding repeats the last column */
        GE_CHECK_EQ_INT(blk[be(r + 20) - 8], 0xB8);                    /* the marker before the bitmap */
    }
    GE_CHECK_EQ_INT(blk[0xB78], 0xB8);
    /* a too small buffer, a missing font, a missing glyph */
    GE_CHECK_EQ_INT((int)fontsBuildLatin(json, (unsigned long)n, "small", atlas, w, h, blk2, size - 1), 0);
    GE_CHECK_EQ_INT((int)fontsBuildLatin(json, (unsigned long)n, "large", atlas, w, h, blk2, sizeof blk2), 0);
    GE_CHECK_EQ_INT((int)fontsBuildLatin(json, (unsigned long)n, "small", atlas, 8, 8, blk2, sizeof blk2), 0);
    /* the comparison: equal blocks, then one pixel and one record field changed */
    memcpy(blk2, blk, size);
    fontsCompareLatin(blk, blk2, size, &c, &d);
    GE_CHECK_EQ_INT((int)c, 94 + 169);
    GE_CHECK_EQ_INT((int)d, 0);
    blk2[be(blk + 676 + 24 * 6 + 20) + 1] ^= 1;
    blk2[676 + 24 * 9 + 8 + 3] ^= 1;   /* record 9 height */
    blk2[4 * 20 + 3] ^= 1;             /* a kerning value */
    fontsCompareLatin(blk, blk2, size, &c, &d);
    GE_CHECK_EQ_INT((int)d, 3);

    /* a reload: another atlas rebuilt into the same buffer gives the same size and only the changed pixel differs */
    {
        unsigned size2, off6 = be(blk + 676 + 24 * 6 + 20);
        unsigned char keep = atlas[0 * w + 6 * CW];   /* glyph 6 is cell (6, 0): its first pixel */
        memcpy(blk2, blk, size);
        atlas[6 * CW] = (unsigned char)(keep ^ 0x55);
        size2 = fontsBuildLatin(json, (unsigned long)n, "small", atlas, w, h, blk, sizeof blk);
        GE_CHECK_EQ_INT((int)size2, (int)size);
        fontsCompareLatin(blk2, blk, size, &c, &d);
        GE_CHECK_EQ_INT((int)d, 1);
        GE_CHECK_EQ_INT(blk[off6], keep ^ 0x55);
        atlas[6 * CW] = keep;
    }

    /* Japanese: slot 2k level (k % 4), slot 2k+1 level 3 - (k % 4) at pixel (px) = row + px, clamped */
    for (i = 0; i < 64; i++) {
        int x0 = (i % 32) * 16, y0 = (i / 32) * 12, r, p;
        for (r = 0; r < 12; r++) for (p = 0; p < 16; p++) {
            int base = (i / 2 + r + p) % 4, lv = (i & 1) ? 3 - base : base;
            jatlas[(y0 + r) * 512 + x0 + p] = (unsigned char)(lv * 85);
        }
    }
    GE_CHECK(fontsBuildJpnPair(json, (unsigned long)n, "jp_narrow", jatlas, 512, 24, 5, pair, &bytes));
    GE_CHECK_EQ_INT((int)bytes, 0x60);
    for (i = 0; i < 12 * 16; i++) {
        int r = i / 16, p = i % 16, base = (5 + r + p) % 4, even = base, odd = 3 - base;
        unsigned char b = pair[r * 8 + p / 2];
        int nib = (p & 1) ? (b & 15) : (b >> 4);
        if ((nib & 3) != even || (nib >> 2) != odd) { GE_FAIL_("pair pixel %d", i); break; }
    }
    GE_CHECK(!fontsBuildJpnPair(json, (unsigned long)n, "jp_narrow", jatlas, 512, 24, 32, pair, &bytes));   /* slots 64 = pairs 0..31 */
    GE_CHECK(!fontsBuildJpnPair(json, (unsigned long)n, "jp_wide", jatlas, 512, 24, 0, pair, &bytes));
    return GE_TEST_END();
}
