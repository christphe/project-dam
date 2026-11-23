/* The thumb.png pixel conversion (port/mission/mission_thumb.c): grey master, film strip, polaroid, adjust. */
#include "ge_test.h"
#include "mission_thumb.h"

#include <stdlib.h>
#include <string.h>

static uint8_t g_grey[MTHUMB_W * MTHUMB_H];

int main(void)
{
    GE_TEST_BEGIN("mission_thumb");

    /* grey: RGB luma weights, grey and RGBA inputs, area average */
    {
        uint8_t rgb[2 * 2 * 3] = { 255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255 };
        uint8_t out[1];
        mthumbGrey(rgb, 2, 2, 3, 1, 1, out);
        GE_CHECK(out[0] > 100 && out[0] < 160);
        uint8_t g2[4] = { 10, 20, 30, 40 };
        mthumbGrey(g2, 2, 2, 1, 1, 1, out);
        GE_CHECK_EQ_INT(out[0], 25);
        uint8_t rgba[4] = { 255, 255, 255, 0 };
        mthumbGrey(rgba, 1, 1, 4, 1, 1, out);
        GE_CHECK_EQ_INT(out[0], 255);   /* the weights sum to 256 */
        mthumbGrey(NULL, 0, 0, 3, 1, 1, out);
        GE_CHECK_EQ_INT(out[0], 0);
    }

    /* a master whose left half is black and right half white, top 0..119 dark rows */
    for (int y = 0; y < MTHUMB_H; y++)
        for (int x = 0; x < MTHUMB_W; x++) g_grey[y * MTHUMB_W + x] = (uint8_t)(x < MTHUMB_W / 2 ? 0 : 255);

    /* strip: 68x44, left dark, right bright, stride honoured, rows written only inside 68 columns */
    {
        uint8_t strip[44 * 72];
        memset(strip, 0x77, sizeof strip);
        mthumbStrip(g_grey, strip, 72);
        GE_CHECK_EQ_INT(strip[0], 0);
        GE_CHECK_EQ_INT(strip[67], 255);
        GE_CHECK_EQ_INT(strip[43 * 72 + 0], 0);
        GE_CHECK_EQ_INT(strip[43 * 72 + 67], 255);
        GE_CHECK_EQ_INT(strip[68], 0x77);
        GE_CHECK_EQ_INT(strip[71], 0x77);
    }
    /* strip rows are stored bottom-up: a master that is bright on its top rows ends bright on the LAST strip rows */
    {
        static uint8_t g2[MTHUMB_W * MTHUMB_H];
        uint8_t strip[44 * 68];
        for (int y = 0; y < MTHUMB_H; y++) memset(g2 + y * MTHUMB_W, y < MTHUMB_H / 2 ? 255 : 0, MTHUMB_W);
        mthumbStrip(g2, strip, 68);
        GE_CHECK_EQ_INT(strip[0], 0);               /* first stored row = bottom of the picture = dark */
        GE_CHECK_EQ_INT(strip[43 * 68], 255);       /* last stored row = top of the picture = bright */
    }

    /* polaroid: white frame (15), the centred square of the master inside; left half of the square is dark */
    {
        static uint8_t img[MTHUMB_POL_SIZE * MTHUMB_POL_SIZE];
        mthumbPolaroidImage(g_grey, img);
        GE_CHECK_EQ_INT(img[0], 15);
        GE_CHECK_EQ_INT(img[127 * 128 + 127], 15);
        GE_CHECK_EQ_INT(img[1 * 128 + 64], 15);
        GE_CHECK_EQ_INT(img[2 * 128 + 2], 0);        /* square starts at x = 40 of the master: still the dark half */
        GE_CHECK_EQ_INT(img[2 * 128 + 125], 15);     /* 255 >> 4 */
        GE_CHECK_EQ_INT(img[64 * 128 + 60], 0);
        GE_CHECK_EQ_INT(img[64 * 128 + 66], 15);
        /* the two halves decode back to the picture */
        static uint8_t h0[MTHUMB_POL_HALF_BYTES], h1[MTHUMB_POL_HALF_BYTES], back[MTHUMB_POL_SIZE * MTHUMB_POL_SIZE];
        mthumbPolaroidHalf(img, 0, h0);
        mthumbPolaroidHalf(img, 1, h1);
        mthumbPolaroidDecode(h0, h1, back);
        GE_CHECK(memcmp(img, back, sizeof img) == 0);
        /* stored bottom-up: the buffer's first row of half 0 is composite row 63 */
        GE_CHECK_EQ_INT(h0[0] >> 4, img[63 * 128 + 0]);
        GE_CHECK_EQ_INT(h1[63 * 64] >> 4, img[64 * 128 + 0]);
        GE_CHECK_EQ_INT(h0[30] & 15, img[63 * 128 + 61]);
    }

    /* adjust: contrast about the middle grey, brightness added, clamped, alpha kept */
    {
        uint8_t px[8] = { 100, 128, 200, 77, 10, 250, 128, 255 };
        mthumbAdjustRgba(px, 2, 2.0f, 0.0f);
        GE_CHECK_EQ_INT(px[0], 72);
        GE_CHECK_EQ_INT(px[1], 128);
        GE_CHECK_EQ_INT(px[2], 255);
        GE_CHECK_EQ_INT(px[3], 77);
        GE_CHECK_EQ_INT(px[4], 0);
        GE_CHECK_EQ_INT(px[5], 255);
        mthumbAdjustRgba(px, 2, 1.0f, -40.0f);
        GE_CHECK_EQ_INT(px[0], 32);
        GE_CHECK_EQ_INT(px[1], 88);
        GE_CHECK_EQ_INT(px[7], 255);
    }
    return GE_TEST_END();
}
