/* test_export_tex.c -- PNG writer self-test + whole-bank texture decode (issue #150). */
#include "export.h"
#include "ge_png.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

static int fails;
#define CHECK(c, msg) do { if (!(c)) { printf("FAIL: %s\n", msg); fails++; } } while (0)

static uint32_t rd32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static void test_png(void)
{
    enum { W = 5, H = 3 };
    uint8_t img[W * H * 4], *png = NULL, *idat = NULL, *raw;
    size_t len = 0, pos = 8, idatlen = 0, i, y;
    int saw_ihdr = 0, saw_iend = 0;
    uLongf rawlen;
    for (i = 0; i < sizeof img; i++) img[i] = (uint8_t)(i * 37 + 11);
    CHECK(gePngEncode(W, H, img, 0, &png, &len) == 1, "png encode");
    if (!png) return;
    CHECK(len > 8 && memcmp(png, "\x89PNG\r\n\x1a\n", 8) == 0, "png signature");
    idat = (uint8_t *)malloc(len);
    while (pos + 12 <= len) {
        uint32_t cl = rd32(png + pos);
        const uint8_t *type = png + pos + 4;
        if (pos + 12 + cl > len) { CHECK(0, "chunk overruns"); break; }
        CHECK((uint32_t)crc32(0L, type, 4 + cl) == rd32(png + pos + 8 + cl), "chunk crc");
        if (!memcmp(type, "IHDR", 4)) {
            saw_ihdr = 1;
            CHECK(cl == 13 && rd32(png + pos + 8) == W && rd32(png + pos + 12) == H
                  && png[pos + 16] == 8 && png[pos + 17] == 6, "ihdr fields");
        } else if (!memcmp(type, "IDAT", 4)) {
            memcpy(idat + idatlen, png + pos + 8, cl); idatlen += cl;
        } else if (!memcmp(type, "IEND", 4)) saw_iend = 1;
        pos += 12 + cl;
    }
    CHECK(saw_ihdr && saw_iend && pos == len, "chunk structure");
    rawlen = (W * 4 + 1) * H;
    raw = (uint8_t *)malloc(rawlen);
    CHECK(uncompress(raw, &rawlen, idat, (uLong)idatlen) == Z_OK && rawlen == (W * 4 + 1) * H, "idat inflates");
    for (y = 0; y < H; y++) {
        CHECK(raw[y * (W * 4 + 1)] == 0, "filter 0");
        CHECK(memcmp(raw + y * (W * 4 + 1) + 1, img + y * W * 4, W * 4) == 0, "row data");
    }
    free(raw); free(idat); free(png);
    printf("png self-test %s\n", fails ? "FAILED" : "ok");
}

static uint64_t fnv(const uint8_t *p, int w, int h)
{
    uint64_t x = 1469598103934665603ULL;
    size_t i, n = (size_t)w * h * 4;
    for (i = 0; i < n; i++) x = (x ^ p[i]) * 1099511628211ULL;
    x = (x ^ (uint64_t)w) * 1099511628211ULL;
    x = (x ^ (uint64_t)h) * 1099511628211ULL;
    return x;
}

typedef struct { int fam, fmt, meth, count, fail; const char *firsterr; int firsttex; } Stat;

static Stat *stat_for(Stat *s, int *ns, int fam, int fmt, int meth)
{
    int i;
    for (i = 0; i < *ns; i++)
        if (s[i].fam == fam && s[i].fmt == fmt && s[i].meth == meth) return &s[i];
    s[*ns].fam = fam; s[*ns].fmt = fmt; s[*ns].meth = meth;
    s[*ns].count = s[*ns].fail = 0; s[*ns].firsterr = NULL;
    return &s[(*ns)++];
}

static int in_list(const char *list, int n)
{
    const char *p = list;
    while (p && *p) {
        char *e;
        long v = strtol(p, &e, 10);
        if (e == p) break;
        if (v == n) return 1;
        p = *e == ',' ? e + 1 : e;
        if (*e == 0) break;
    }
    return 0;
}

static void test_rom(const char *path, const char *region)
{
    FILE *f = fopen(path, "rb");
    uint8_t *rom;
    long sz;
    ExTexTable t;
    const char *err = NULL, *dump = getenv("GE_TEST_TEXDUMP"), *dir = getenv("GE_TEST_TEXDUMP_DIR");
    Stat *st = (Stat *)calloc(256, sizeof(Stat));
    int ns = 0, i, failed = 0;
    if (!f) { printf("FAIL: cannot open %s\n", path); fails++; return; }
    fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
    rom = (uint8_t *)malloc((size_t)sz);
    if (fread(rom, 1, (size_t)sz, f) != (size_t)sz) { printf("FAIL: short read\n"); fails++; fclose(f); return; }
    fclose(f);
    if (ex_tex_table_open(&t, rom, (size_t)sz, region, &err)) {
        printf("FAIL: table open: %s\n", err); fails++; free(rom); return;
    }
    printf("table: %u textures, images at 0x%X, total 0x%X\n", (unsigned)t.count, (unsigned)t.images_start, (unsigned)t.offset[t.count]);
    for (i = 0; i < (int)t.count; i++) {
        ExTexture tx;
        Stat *s;
        err = NULL;
        if (ex_tex_decode(&t, i, &tx, &err) == 0) {
            s = stat_for(st, &ns, tx.zlib, tx.format, tx.method);
            s->count++;
            if (dump && in_list(dump, i)) {
                printf("tex %d fmt %d zlib %d method %d %dx%d fnv %016llx\n", i, tx.format, tx.zlib, tx.method,
                       tx.width, tx.height, (unsigned long long)fnv(tx.rgba, tx.width, tx.height));
                if (dir) {
                    char name[512];
                    snprintf(name, sizeof name, "%s/%d.png", dir, i);
                    if (!gePngWrite(name, tx.width, tx.height, tx.rgba, 0)) printf("  could not write %s\n", name);
                }
            }
            ex_tex_free(&tx);
        } else {
            /* family/format/method are unknown on failure: re-read the header bytes for the key */
            const uint8_t *d = t.rom + t.images_start + t.offset[i];
            int fam = (d[0] >> 6) & 1, fmt = -1, meth = -1;
            if (!fam) { fmt = d[1] >> 4; meth = (d[3] & 15) ; }
            else fmt = d[1];
            s = stat_for(st, &ns, fam, fmt, meth);
            s->count++; s->fail++;
            if (!s->firsterr) { s->firsterr = err; s->firsttex = i; }
            failed++;
            if (dump && in_list(dump, i)) printf("tex %d FAILED: %s\n", i, err);
        }
    }
    for (i = 0; i < ns; i++)
        printf("%s fmt %2d method %2d: %4d textures, %d failed%s%s\n", st[i].fam ? "zlib  " : "packed", st[i].fmt, st[i].meth,
               st[i].count, st[i].fail, st[i].fail ? "  first: " : "", st[i].fail ? st[i].firsterr : "");
    printf("decoded %u textures, %d failures\n", (unsigned)t.count, failed);
    if (failed) fails++;
    ex_tex_table_close(&t);
    free(rom); free(st);
}

int main(void)
{
    const char *rom = getenv("GE_TEST_ROM");
    test_png();
    if (rom && *rom) {
        const char *reg = getenv("GE_TEST_ROM_REGION");
        test_rom(rom, reg && *reg ? reg : "ntsc-final");
    }
    else printf("skip (GE_TEST_ROM not set)\n");
    printf(fails ? "FAILED\n" : "PASS\n");
    return fails ? 1 : 0;
}
