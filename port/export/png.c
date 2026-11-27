/* png.c -- minimal RGBA8 PNG writer (issue #150). libc + zlib. */
#include "export.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}

/* Appends one chunk at p; returns the new end. */
static uint8_t *chunk(uint8_t *p, const char *type, const uint8_t *data, uint32_t len)
{
    uLong crc;
    put32(p, len);
    memcpy(p + 4, type, 4);
    if (len) memcpy(p + 8, data, len);
    crc = crc32(0L, (const Bytef *)(p + 4), 4 + len);
    put32(p + 8 + len, (uint32_t)crc);
    return p + 12 + len;
}

int ex_png_encode(int width, int height, const uint8_t *rgba, uint8_t **out, size_t *out_len)
{
    static const uint8_t sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    size_t stride, rawlen, y;
    uint8_t *raw, *z, *buf, *p;
    uint8_t ihdr[13];
    uLongf zlen;
    if (width < 1 || height < 1 || !rgba || !out) return -1;
    stride = (size_t)width * 4;
    rawlen = (stride + 1) * (size_t)height;
    raw = (uint8_t *)malloc(rawlen);
    if (!raw) return -1;
    for (y = 0; y < (size_t)height; y++) {
        raw[y * (stride + 1)] = 0;
        memcpy(raw + y * (stride + 1) + 1, rgba + y * stride, stride);
    }
    zlen = compressBound((uLong)rawlen);
    z = (uint8_t *)malloc(zlen);
    if (!z) { free(raw); return -1; }
    if (compress2(z, &zlen, raw, (uLong)rawlen, 6) != Z_OK) { free(raw); free(z); return -1; }
    free(raw);
    buf = (uint8_t *)malloc(8 + 12 + 13 + 12 + zlen + 12);
    if (!buf) { free(z); return -1; }
    memcpy(buf, sig, 8);
    put32(ihdr, (uint32_t)width); put32(ihdr + 4, (uint32_t)height);
    ihdr[8] = 8; ihdr[9] = 6; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    p = chunk(buf + 8, "IHDR", ihdr, 13);
    p = chunk(p, "IDAT", z, (uint32_t)zlen);
    p = chunk(p, "IEND", NULL, 0);
    free(z);
    *out = buf;
    if (out_len) *out_len = (size_t)(p - buf);
    return 0;
}

int ex_png_write(const char *path, int width, int height, const uint8_t *rgba)
{
    uint8_t *buf = NULL;
    size_t len = 0;
    FILE *f;
    int ok;
    if (ex_png_encode(width, height, rgba, &buf, &len)) return -1;
    f = fopen(path, "wb");
    if (!f) { free(buf); return -1; }
    ok = fwrite(buf, 1, len, f) == len;
    if (fclose(f)) ok = 0;
    free(buf);
    return ok ? 0 : -1;
}
