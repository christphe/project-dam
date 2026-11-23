/* mission_thumb.c: the pixel conversion of a mission's thumb.png (see mission_thumb.h). Pure C. */
#include "mission_thumb.h"

#include <string.h>

void mthumbGrey(const uint8_t *pix, int w, int h, int ch, int dw, int dh, uint8_t *out)
{
    if (!pix || w <= 0 || h <= 0 || ch < 1 || ch > 4) { memset(out, 0, (size_t)dw * (size_t)dh); return; }
    for (int oy = 0; oy < dh; oy++) {
        uint32_t y0 = (uint32_t)((uint64_t)oy * (uint32_t)h / (uint32_t)dh), y1 = (uint32_t)((uint64_t)(oy + 1) * (uint32_t)h / (uint32_t)dh);
        if (y1 <= y0) y1 = y0 + 1;
        for (int ox = 0; ox < dw; ox++) {
            uint32_t x0 = (uint32_t)((uint64_t)ox * (uint32_t)w / (uint32_t)dw), x1 = (uint32_t)((uint64_t)(ox + 1) * (uint32_t)w / (uint32_t)dw), n = 0;
            uint64_t sum = 0;
            if (x1 <= x0) x1 = x0 + 1;
            for (uint32_t y = y0; y < y1 && y < (uint32_t)h; y++)
                for (uint32_t x = x0; x < x1 && x < (uint32_t)w; x++, n++) {
                    const uint8_t *p = pix + ((size_t)y * (uint32_t)w + x) * (size_t)ch;
                    sum += (ch < 3) ? p[0] : (uint32_t)(p[0] * 77 + p[1] * 150 + p[2] * 29) >> 8;
                }
            out[oy * dw + ox] = n ? (uint8_t)(sum / n) : 0;
        }
    }
}

void mthumbWindow(const uint8_t *grey, int x0, int y0, int ww, int hh, int dw, int dh, uint8_t *out, int ostride)
{
    for (int oy = 0; oy < dh; oy++) {
        int ya = y0 + oy * hh / dh, yb = y0 + (oy + 1) * hh / dh;
        if (yb <= ya) yb = ya + 1;
        for (int ox = 0; ox < dw; ox++) {
            int xa = x0 + ox * ww / dw, xb = x0 + (ox + 1) * ww / dw, n = 0;
            unsigned sum = 0;
            if (xb <= xa) xb = xa + 1;
            for (int y = ya; y < yb && y < MTHUMB_H; y++)
                for (int x = xa; x < xb && x < MTHUMB_W; x++, n++) sum += grey[y * MTHUMB_W + x];
            out[oy * ostride + ox] = n ? (uint8_t)(sum / n) : 0;
        }
    }
}

void mthumbStrip(const uint8_t *grey, uint8_t *out, int line)
{
    /* the frame shows 68:44; take that crop of the 4:3 source (centred), not a squeezed copy */
    const int ch = MTHUMB_W * MTHUMB_STRIP_H / MTHUMB_STRIP_W;
    mthumbWindow(grey, 0, (MTHUMB_H - ch) / 2, MTHUMB_W, ch, MTHUMB_STRIP_W, MTHUMB_STRIP_H, out, line);
    for (int y = 0; y < MTHUMB_STRIP_H / 2; y++) {   /* the strip textures are stored bottom-up, like the polaroid's */
        for (int x = 0; x < MTHUMB_STRIP_W; x++) {
            uint8_t t = out[y * line + x];
            out[y * line + x] = out[(MTHUMB_STRIP_H - 1 - y) * line + x];
            out[(MTHUMB_STRIP_H - 1 - y) * line + x] = t;
        }
    }
}

void mthumbPolaroidImage(const uint8_t *grey, uint8_t *out128)
{
    uint8_t sq[124 * 124];
    mthumbWindow(grey, (MTHUMB_W - MTHUMB_H) / 2, 0, MTHUMB_H, MTHUMB_H, 124, 124, sq, 124);
    for (int y = 0; y < MTHUMB_POL_SIZE; y++)
        for (int x = 0; x < MTHUMB_POL_SIZE; x++)
            out128[y * MTHUMB_POL_SIZE + x] = (x < 2 || x > 125 || y < 2 || y > 125) ? 15u : (uint8_t)(sq[(y - 2) * 124 + (x - 2)] >> 4);
}

void mthumbPolaroidHalf(const uint8_t *image128, int half, uint8_t *out4096)
{
    for (int y = 0; y < 64; y++) {
        const int gy = half * 64 + (63 - y);   /* buffer row y shows composite row 63 - y of its half */
        for (int x = 0; x < 128; x += 2)
            out4096[y * 64 + x / 2] = (uint8_t)((image128[gy * MTHUMB_POL_SIZE + x] << 4) | (image128[gy * MTHUMB_POL_SIZE + x + 1] & 15));
    }
}

void mthumbPolaroidDecode(const uint8_t *half0, const uint8_t *half1, uint8_t *out128)
{
    for (int half = 0; half < 2; half++) {
        const uint8_t *src = half ? half1 : half0;
        for (int y = 0; y < 64; y++) {
            const int gy = half * 64 + (63 - y);
            for (int x = 0; x < 128; x += 2) {
                out128[gy * MTHUMB_POL_SIZE + x] = src[y * 64 + x / 2] >> 4;
                out128[gy * MTHUMB_POL_SIZE + x + 1] = src[y * 64 + x / 2] & 15;
            }
        }
    }
}

void mthumbAdjustRgba(uint8_t *rgba, int npix, float contrast, float brightness)
{
    for (int i = 0; i < npix; i++)
        for (int k = 0; k < 3; k++) {
            float v = ((float)rgba[i * 4 + k] - 128.0f) * contrast + 128.0f + brightness;
            rgba[i * 4 + k] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : (int)(v + 0.5f));
        }
}
