/* conv_romcheck.c -- SHA-1 and the three retail GoldenEye 007 images. */
#include "conv_romcheck.h"

#define ROL(x, n) ((((x) << (n)) | ((x) >> (32 - (n)))) & 0xFFFFFFFFul)

static void sha1_block(unsigned long h[5], const unsigned char *p)
{
    unsigned long w[80], a, b, c, d, e, t, f, k;
    int i;

    for (i = 0; i < 16; i++)
        w[i] = ((unsigned long)p[i * 4] << 24) | ((unsigned long)p[i * 4 + 1] << 16) |
               ((unsigned long)p[i * 4 + 2] << 8) | (unsigned long)p[i * 4 + 3];
    for (i = 16; i < 80; i++)
        w[i] = ROL(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

    a = h[0]; b = h[1]; c = h[2]; d = h[3]; e = h[4];
    for (i = 0; i < 80; i++) {
        if (i < 20)      { f = (b & c) | (~b & d);          k = 0x5A827999ul; }
        else if (i < 40) { f = b ^ c ^ d;                   k = 0x6ED9EBA1ul; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDCul; }
        else             { f = b ^ c ^ d;                   k = 0xCA62C1D6ul; }
        t = (ROL(a, 5) + (f & 0xFFFFFFFFul) + e + k + w[i]) & 0xFFFFFFFFul;
        e = d; d = c; c = ROL(b, 30); b = a; a = t;
    }
    h[0] = (h[0] + a) & 0xFFFFFFFFul;
    h[1] = (h[1] + b) & 0xFFFFFFFFul;
    h[2] = (h[2] + c) & 0xFFFFFFFFul;
    h[3] = (h[3] + d) & 0xFFFFFFFFul;
    h[4] = (h[4] + e) & 0xFFFFFFFFul;
}

void cv_sha1(const unsigned char *data, unsigned long len, unsigned char out[20])
{
    unsigned long h[5] = { 0x67452301ul, 0xEFCDAB89ul, 0x98BADCFEul, 0x10325476ul, 0xC3D2E1F0ul };
    unsigned char tail[128];
    unsigned long full = len / 64, rem = len % 64, i, n;
    unsigned long long bits = (unsigned long long)len * 8u;

    for (i = 0; i < full; i++)
        sha1_block(h, data + i * 64);

    for (i = 0; i < rem; i++) tail[i] = data[full * 64 + i];
    tail[rem] = 0x80;
    n = rem < 56 ? 64 : 128;
    for (i = rem + 1; i < n; i++) tail[i] = 0;
    for (i = 0; i < 8; i++)
        tail[n - 1 - i] = (unsigned char)(bits >> (8 * i));
    sha1_block(h, tail);
    if (n == 128) sha1_block(h, tail + 64);

    for (i = 0; i < 5; i++) {
        out[i * 4]     = (unsigned char)(h[i] >> 24);
        out[i * 4 + 1] = (unsigned char)(h[i] >> 16);
        out[i * 4 + 2] = (unsigned char)(h[i] >> 8);
        out[i * 4 + 3] = (unsigned char)h[i];
    }
}

static const struct {
    const char *region;
    const char *sha1;   /* lower-case hex of the retail image */
} retail[3] = {
    { "ntsc-final", "abe01e4aeb033b6c0836819f549c791b26cfde83" },
    { "pal-final",  "167c3c433dec1f1eb921736f7d53fac8cb45ee31" },
    { "jpn-final",  "2a5dade32f7fad6c73c659d2026994632c1b3174" },
};

int cv_romcheck(const unsigned char *rom, unsigned long len, const char **region_out)
{
    static const char hex[] = "0123456789abcdef";
    unsigned char sum[20];
    char text[41];
    int r, i;

    if (len < 4) return CV_ROM_UNKNOWN;
    if (rom[0] == 0x37 && rom[1] == 0x80 && rom[2] == 0x40 && rom[3] == 0x12)
        return CV_ROM_BYTESWAPPED_V64;
    if (rom[0] == 0x40 && rom[1] == 0x12 && rom[2] == 0x37 && rom[3] == 0x80)
        return CV_ROM_BYTESWAPPED_N64;

    cv_sha1(rom, len, sum);
    for (i = 0; i < 20; i++) {
        text[i * 2]     = hex[sum[i] >> 4];
        text[i * 2 + 1] = hex[sum[i] & 15];
    }
    text[40] = 0;
    for (r = 0; r < 3; r++) {
        for (i = 0; i < 40 && text[i] == retail[r].sha1[i]; i++) {}
        if (i == 40) {
            if (region_out) *region_out = retail[r].region;
            return CV_ROM_OK;
        }
    }
    return CV_ROM_UNKNOWN;
}

const char *cv_romcheck_message(int code)
{
    switch (code) {
    case CV_ROM_OK:              return "ok";
    case CV_ROM_BYTESWAPPED_V64: return "byte-swapped .v64 dump: convert it to big-endian .z64 first";
    case CV_ROM_BYTESWAPPED_N64: return "byte-swapped .n64 dump: convert it to big-endian .z64 first";
    default:                     return "not an unmodified retail ROM (SHA-1 mismatch): modified, patched or bad dump";
    }
}
