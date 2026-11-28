/* font_export.c -- static export of the game's fonts (docs/specs/fonts.md).
 *
 * Writes <out_dir>/fonts.json and <out_dir>/fonts/{small,large,jp_narrow,jp_wide}.png (8-bit greyscale, no alpha). The Latin
 * values are the ROM's 8-bit intensities, unchanged; the Japanese 2-bit levels are written as level * 85.
 *
 * CLEAN ROOM: written from docs/specs/fonts.md only (see export.h). The output is ROM data: it goes only to the directory given.
 */
#include "export_common.h"

#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#define LATIN_GLYPHS 94
#define KERN_N 169
#define KERN_BYTES (KERN_N * 4)
#define REC_BYTES 24
#define PIX_START 0xB80u
#define COLS_LATIN 16
#define ROWS_LATIN 6
#define COLS_JP 32

static const struct {
    const char *region;
    int limit;
} k_freg[] = { { "ntsc-final", 967 }, { "pal-final", 968 }, { "jpn-final", 968 } };

static const struct { const char *key, *kern, *chart, *image; } k_latin[2] = {
    { "small", "fontBankGothic_kerning", "fontBankGothic_fontchartable", "fonts/small.png" },
    { "large", "fontZurichBold_kerning", "fontZurichBold_fontchartable", "fonts/large.png" },
};

/* ---- 8-bit greyscale PNG (ge_assets' writer does RGB and RGBA only) ---- */

static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }

static int chunk(CvBuf *b, const char *type, const uint8_t *d, size_t n)
{
    uint8_t h[8], c[4];
    uLong crc;
    put32(h, (uint32_t)n);
    memcpy(h + 4, type, 4);
    crc = crc32(0, h + 4, 4);
    if (n) crc = crc32(crc, d, (uInt)n);
    put32(c, (uint32_t)crc);
    if (cv_buf_put(b, h, 8) || (n && cv_buf_put(b, d, n)) || cv_buf_put(b, c, 4)) return -1;
    return 0;
}

static int write_grey_png(const char *path, int w, int h, const uint8_t *g)
{
    static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', 13, 10, 26, 10 };
    CvBuf out = { 0 };
    uint8_t ihdr[13], *raw = NULL, *z = NULL;
    uLongf zn;
    size_t rawn = (size_t)h * ((size_t)w + 1);
    int rc = -1;

    put32(ihdr, (uint32_t)w); put32(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8; ihdr[9] = 0; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;   /* 8-bit grey, deflate, filter 0, no interlace */
    raw = malloc(rawn);
    zn = compressBound((uLong)rawn);
    z = malloc(zn);
    if (!raw || !z) goto done;
    for (int y = 0; y < h; y++) {
        raw[(size_t)y * (w + 1)] = 0;
        memcpy(raw + (size_t)y * (w + 1) + 1, g + (size_t)y * w, (size_t)w);
    }
    if (compress2(z, &zn, raw, (uLong)rawn, 9) != Z_OK) goto done;
    if (cv_buf_put(&out, sig, 8) || chunk(&out, "IHDR", ihdr, 13) || chunk(&out, "IDAT", z, zn) || chunk(&out, "IEND", NULL, 0)) goto done;
    rc = ex_write_file(path, out.p, out.len);
done:
    free(raw); free(z); cv_buf_free(&out);
    return rc;
}

/* ---- the Latin fonts ---- */

typedef struct { int top, height, width, cls; uint32_t off; } LGlyph;

static int export_latin(const uint8_t *rom, size_t rom_size, const CvRegionData *rd, int which, const char *dir, CvBuf *json,
                        ExLogFn log, void *user)
{
    const CvFileRow *kr = cv_find_row(rd, k_latin[which].kern), *cr = cv_find_row(rd, k_latin[which].chart);
    const uint8_t *blk;
    size_t size;
    LGlyph g[LATIN_GLYPHS];
    int maxw = 0, maxh = 0, cw, ch, aw, ah, line;
    uint8_t *img = NULL;
    char path[1400];

    if (!kr || !cr) { ex_logf(log, user, "fonts: %s: file table rows missing", k_latin[which].key); return 1; }
    if (kr->off + kr->size != cr->off || kr->size != KERN_BYTES) { ex_logf(log, user, "fonts: %s: the kerning and chart rows are not one block", k_latin[which].key); return 1; }
    size = (size_t)kr->size + cr->size;
    if ((size_t)kr->off + size > rom_size || size < PIX_START) { ex_logf(log, user, "fonts: %s: outside the ROM", k_latin[which].key); return 1; }
    blk = rom + kr->off;

    for (int i = 0; i < LATIN_GLYPHS; i++) {
        const uint8_t *r = blk + KERN_BYTES + (size_t)i * REC_BYTES;
        g[i].top = (int32_t)ex_be32(r + 4);
        g[i].height = (int32_t)ex_be32(r + 8);
        g[i].width = (int32_t)ex_be32(r + 12);
        g[i].cls = (int32_t)ex_be32(r + 16);
        g[i].off = ex_be32(r + 20);
        size_t stride = ((size_t)g[i].width + 7) & ~(size_t)7;
        if (g[i].width < 1 || g[i].width > 64 || g[i].height < 1 || g[i].height > 64 || g[i].cls < 0 || g[i].cls > 12 ||
            g[i].off < PIX_START || (size_t)g[i].off + stride * (size_t)g[i].height > size) {
            ex_logf(log, user, "fonts: %s: glyph record %d is out of range", k_latin[which].key, i);
            return 1;
        }
        if (g[i].width > maxw) maxw = g[i].width;
        if (g[i].height > maxh) maxh = g[i].height;
    }
    cw = maxw + 1; ch = maxh + 1;
    aw = cw * COLS_LATIN; ah = ch * ROWS_LATIN;
    img = calloc((size_t)aw * ah, 1);
    if (!img) return 1;
    for (int i = 0; i < LATIN_GLYPHS; i++) {
        size_t stride = ((size_t)g[i].width + 7) & ~(size_t)7;
        int x0 = (i % COLS_LATIN) * cw, y0 = (i / COLS_LATIN) * ch;
        for (int r = 0; r < g[i].height; r++)
            memcpy(img + (size_t)(y0 + r) * aw + x0, blk + g[i].off + stride * (size_t)r, (size_t)g[i].width);
    }
    snprintf(path, sizeof path, "%s/%s", dir, k_latin[which].image);
    if (write_grey_png(path, aw, ah, img)) { ex_logf(log, user, "fonts: cannot write %s", path); free(img); return 1; }
    free(img);

    line = g[91].top + g[91].height;   /* the glyph of '|' (code 0x7C): the line height */
    ex_jp(json, "    \"%s\": {\n      \"kind\": \"latin\",\n      \"image\": \"%s\",\n      \"line_height\": %d,\n      \"first_code\": 33,\n", k_latin[which].key, k_latin[which].image, line);
    ex_jp(json, "      \"kerning\": [\n");
    for (int a = 0; a < 13; a++) {
        ex_jp(json, "        [");
        for (int b = 0; b < 13; b++) ex_jp(json, "%s%d", b ? ", " : "", (int)(int32_t)ex_be32(blk + ((size_t)a * 13 + b) * 4));
        ex_jp(json, "]%s\n", a < 12 ? "," : "");
    }
    ex_jp(json, "      ],\n      \"glyphs\": [\n");
    for (int i = 0; i < LATIN_GLYPHS; i++)
        ex_jp(json, "        { \"code\": %d, \"x\": %d, \"y\": %d, \"w\": %d, \"h\": %d, \"top\": %d, \"class\": %d }%s\n", 33 + i,
              (i % COLS_LATIN) * cw, (i / COLS_LATIN) * ch, g[i].width, g[i].height, g[i].top, g[i].cls, i < LATIN_GLYPHS - 1 ? "," : "");
    ex_jp(json, "      ]\n    }");
    return 0;
}

/* ---- the Japanese fonts ---- */

static const CvFileRow *find_suffix(const CvRegionData *rd, const char *suffix)
{
    size_t sl = strlen(suffix);
    for (int i = 0; i < rd->nfiles; i++) {
        size_t bl = strlen(rd->files[i].base);
        if (bl >= sl && !strcmp(rd->files[i].base + bl - sl, suffix)) return &rd->files[i];
    }
    return NULL;
}

static int export_jp(const uint8_t *rom, size_t rom_size, const CvRegionData *rd, int wide, int limit, const char *dir, CvBuf *json,
                     ExLogFn log, void *user)
{
    const char *key = wide ? "jp_wide" : "jp_narrow";
    const CvFileRow *row = find_suffix(rd, wide ? "efont_chardata" : "jfont_chardata");
    const int cellw = 16, cellh = wide ? 16 : 12;
    const size_t block = wide ? 0x80 : 0x60;
    size_t slots, rows;
    int aw, ah;
    uint8_t *img;
    char path[1400];

    if (!row) { ex_logf(log, user, "fonts: %s: file table row missing", key); return 1; }
    if ((size_t)row->off + row->size > rom_size || row->size % block) { ex_logf(log, user, "fonts: %s: bad row", key); return 1; }
    slots = (size_t)row->size / block * 2;
    rows = (slots + COLS_JP - 1) / COLS_JP;
    aw = cellw * COLS_JP; ah = (int)rows * cellh;
    img = calloc((size_t)aw * ah, 1);
    if (!img) return 1;
    for (size_t n = 0; n < slots; n++) {
        const uint8_t *b = rom + row->off + (n >> 1) * block;
        int x0 = (int)(n % COLS_JP) * cellw, y0 = (int)(n / COLS_JP) * cellh, sh = (n & 1) ? 2 : 0;
        for (int r = 0; r < cellh; r++)
            for (int p = 0; p < 16; p++) {
                uint8_t v = b[r * 8 + p / 2];
                int nib = (p & 1) ? (v & 15) : (v >> 4);
                img[(size_t)(y0 + r) * aw + x0 + p] = (uint8_t)(((nib >> sh) & 3) * 85);
            }
    }
    snprintf(path, sizeof path, "%s/fonts/%s.png", dir, key);
    if (write_grey_png(path, aw, ah, img)) { ex_logf(log, user, "fonts: cannot write %s", path); free(img); return 1; }
    free(img);
    ex_jp(json, "    \"%s\": {\n      \"kind\": \"cells\",\n      \"image\": \"fonts/%s.png\",\n      \"cell\": [%d, %d],\n      \"columns\": %d,\n", key, key, cellw, cellh, COLS_JP);
    ex_jp(json, "      \"slots\": %d,\n      \"draw\": [%d, %d],\n      \"advance\": %d,\n      \"top\": 0,\n      \"limit\": %d,\n      \"fallback\": 2", (int)slots, wide ? 15 : 11, cellh, wide ? 16 : 12, limit);
    if (wide) ex_jp(json, ",\n      \"fallback_font\": \"jp_narrow\"");
    ex_jp(json, "\n    }");
    return 0;
}

int ex_export_fonts(const char *rom_path, const char *out_dir, ExLogFn log, void *user)
{
    uint8_t *rom = NULL;
    size_t rom_size = 0;
    const char *region = NULL;
    const CvRegionData *rd = NULL;
    CvBuf json = { 0 };
    char path[1400];
    int limit = -1, failed = 0;

    if (ex_open_rom(rom_path, log, user, &rom, &rom_size, &region, &rd)) return 1;
    for (size_t i = 0; i < sizeof k_freg / sizeof k_freg[0]; i++) if (!strcmp(region, k_freg[i].region)) limit = k_freg[i].limit;
    if (limit < 0) { ex_logf(log, user, "fonts: no constants for region %s", region); free(rom); return 1; }
    snprintf(path, sizeof path, "%s/fonts", out_dir);
    if (ex_mkdir_p(path)) { ex_logf(log, user, "cannot create %s", path); free(rom); return 1; }

    ex_jp(&json, "{\n  \"format\": \"ge007-fonts/1\",\n  \"region\": \"%s\",\n  \"space_advance\": 5,\n  \"jp_min_line_height\": 14,\n  \"fonts\": {\n", region);
    failed += export_latin(rom, rom_size, rd, 0, out_dir, &json, log, user);
    ex_jp(&json, ",\n");
    failed += export_latin(rom, rom_size, rd, 1, out_dir, &json, log, user);
    ex_jp(&json, ",\n");
    failed += export_jp(rom, rom_size, rd, 0, limit, out_dir, &json, log, user);
    ex_jp(&json, ",\n");
    failed += export_jp(rom, rom_size, rd, 1, limit, out_dir, &json, log, user);
    ex_jp(&json, "\n  }\n}\n");
    if (!failed) {
        snprintf(path, sizeof path, "%s/fonts.json", out_dir);
        if (ex_write_file(path, json.p, json.len)) { ex_logf(log, user, "fonts: cannot write %s", path); failed++; }
    }
    ex_logf(log, user, "fonts: 4 fonts, %d failure(s) -> %s", failed, out_dir);
    cv_buf_free(&json);
    free(rom);
    return failed;
}
