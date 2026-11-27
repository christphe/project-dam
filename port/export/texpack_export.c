/* texpack_export.c -- the game's base texture pack, ge007.ptexpack (docs/specs/texture-packs.md).
 * CLEAN ROOM: written from docs/specs/ only. */
#include "export_common.h"
#include "ge_png.h"

#include <stdlib.h>
#ifdef _WIN32
#include <direct.h>
#else
#include <unistd.h>
#endif

#define PACK_TEXTURES 2698

static ExProgressFn g_progress;
static void *g_progress_user;
void ex_set_progress(ExProgressFn fn, void *user) { g_progress = fn; g_progress_user = user; }

static const char *const k_surfaces[13] = {
    "default", "stone", "wood", "metal", "glass", "water", "snow", "dirt", "mud", "tile", "metalobj", "chr", "glass_xlu"
};
static const char *const k_formats[13] = {
    "rgba32", "rgba16", "rgb24", "rgb15", "ia16", "ia8", "ia4", "i8", "i4", "ci8_rgba16", "ci4_rgba16", "ci8_ia16", "ci4_ia16"
};
/* Bits per texel for the line rule, by format number (texture-packs.md §tile_width): 32-bit texels are held as two
 * 16-bit halves, so they count as 16. */
static const int k_texel_bits[13] = { 16, 16, 16, 16, 16, 8, 4, 8, 4, 8, 4, 8, 4 };

static void put_class(CvBuf *b, unsigned c, int *odd)
{
    if (c < 13) ex_jstr(b, k_surfaces[c]);
    else { char n[16]; snprintf(n, sizeof n, "class%u", c); ex_jstr(b, n); (*odd)++; }
}

int ex_export_textures(const char *rom_path, const char *pack_dir, ExLogFn log, void *user)
{
    uint8_t *rom = NULL;
    size_t rom_size = 0;
    const char *region = NULL;
    const CvRegionData *rd = NULL;
    ExTexTable tbl;
    const char *err = NULL;
    char path[1300];
    CvBuf js;
    int nfiles = 0, nnull = 0, nodd = 0, bad = 0;

    if (ex_open_rom(rom_path, log, user, &rom, &rom_size, &region, &rd)) return 1;
    if (ex_tex_table_open(&tbl, rom, rom_size, region, &err)) {
        ex_logf(log, user, "texture pack: textures unavailable for region %s: %s", region, err ? err : "?");
        free(rom);
        return 1;
    }
    snprintf(path, sizeof path, "%s/textures", pack_dir);
    if (ex_mkdir_p(path)) {
        ex_logf(log, user, "texture pack: cannot create %s", path);
        ex_tex_table_close(&tbl); free(rom);
        return 1;
    }
    memset(&js, 0, sizeof js);
    ex_jp(&js, "{\n  \"version\": 1,\n  \"textures\": {\n");
    for (int n = 0; n < PACK_TEXTURES; n++) {
        ExTexture t;
        const char *derr = NULL;
        int ok;
        memset(&t, 0, sizeof t);
        ok = ex_tex_decode(&tbl, n, &t, &derr) == 0 && t.rgba && t.width > 0 && t.height > 0;
        if (ok) {
            snprintf(path, sizeof path, "%s/textures/%d.png", pack_dir, n);
            if (!gePngWrite(path, t.width, t.height, t.rgba, 0)) {
                ex_logf(log, user, "texture pack: cannot write %s", path);
                bad++; ok = 0;
            } else nfiles++;
        } else {
            ex_logf(log, user, "texture pack: texture %d has no picture: %s", n, derr ? derr : "decode failed");
            nnull++;
        }
        ex_jp(&js, "%s    \"%d\": { \"image\": ", n ? ",\n" : "", n);
        if (ok) ex_jp(&js, "\"textures/%d.png\"", n); else ex_jp(&js, "null");
        ex_jp(&js, ", \"surface\": "); put_class(&js, tbl.cls[n] & 0xF, &nodd);
        ex_jp(&js, ", \"impact_sound\": "); put_class(&js, tbl.cls[n] >> 4, &nodd);
        if (ok) {
            ex_jp(&js, ", \"alpha\": \"%s\"", (t.format >= 4 && t.format <= 8) ? "luminance" : "image");
            if (t.format >= 0 && t.format < 13) ex_jp(&js, ", \"format\": \"%s\"", k_formats[t.format]);
            ex_jp(&js, ", \"width\": %d, \"height\": %d", t.width, t.height);
            if (t.format >= 0 && t.format < 13) {   /* the stored (tile) size, texture-packs.md §tile_width */
                const int bits = k_texel_bits[t.format];
                const int tw = (t.width * bits + 63) / 64 * 64 / bits, th = t.height == 1 ? 2 : t.height;
                if (tw != t.width || th != t.height) ex_jp(&js, ", \"tile_width\": %d, \"tile_height\": %d", tw, th);
            }
        } else ex_jp(&js, ", \"alpha\": \"image\"");
        {
            uint32_t w1 = tbl.word1[n];
            ex_jp(&js, ", \"detail\": { \"s_offset\": %u, \"t_offset\": %u, \"s_shift\": %u, \"t_shift\": %u } }",
                  (w1 >> 28) & 15, (w1 >> 24) & 15, (w1 >> 20) & 15, (w1 >> 16) & 15);
        }
        if (t.rgba) ex_tex_free(&t);
        if (g_progress) g_progress(g_progress_user, n + 1, PACK_TEXTURES);
    }
    ex_jp(&js, "\n  }\n}\n");
    snprintf(path, sizeof path, "%s/textures.json", pack_dir);
    if (ex_write_file(path, js.p, js.len)) { ex_logf(log, user, "texture pack: cannot write %s", path); bad++; }
    cv_buf_free(&js);

    memset(&js, 0, sizeof js);
    ex_jp(&js, "{\n  \"name\": \"GoldenEye 007\",\n  \"author\": \"Rare\",\n  \"description\": ");
    ex_jstr(&js, "The game's own textures, exported from your ROM. GoldenEye 007 (c) 1997 Nintendo / Rare.");
    ex_jp(&js, ",\n  \"base\": true,\n  \"region\": "); ex_jstr(&js, region);
    ex_jp(&js, ",\n  \"exporter_version\": %d\n}\n", EX_EXPORTER_VERSION);
    snprintf(path, sizeof path, "%s/pack.json", pack_dir);
    if (ex_write_file(path, js.p, js.len)) { ex_logf(log, user, "texture pack: cannot write %s", path); bad++; }
    cv_buf_free(&js);

    if (nodd) ex_logf(log, user, "texture pack: %d surface / impact sound class(es) above 12 written as classNN", nodd);
    ex_logf(log, user, "texture pack: %d pictures, %d textures without one, %d failed -> %s", nfiles, nnull, bad, pack_dir);
    ex_tex_table_close(&tbl);
    free(rom);
    return bad;
}

int ex_pack_version(const char *pack_dir)
{
    char path[1300], buf[2048];
    FILE *f;
    size_t n;
    const char *p;
    snprintf(path, sizeof path, "%s/pack.json", pack_dir);
    f = fopen(path, "rb");
    if (!f) return -1;
    n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = 0;
    p = strstr(buf, "\"exporter_version\"");
    if (!p) return -1;
    p = strchr(p + 18, ':');
    if (!p) return -1;
    p++;
    while (*p == ' ' || *p == '\t') p++;
    if (*p < '0' || *p > '9') return -1;
    return atoi(p);
}

/* Removes a pack folder written by ex_export_textures (its known files; the folders when they are empty). */
static void remove_pack(const char *dir)
{
    char path[1300];
    for (int n = 0; n < PACK_TEXTURES; n++) {
        snprintf(path, sizeof path, "%s/textures/%d.png", dir, n);
        remove(path);
    }
    snprintf(path, sizeof path, "%s/textures", dir);
#ifdef _WIN32
    _rmdir(path);
#else
    rmdir(path);
#endif
    snprintf(path, sizeof path, "%s/pack.json", dir); remove(path);
    snprintf(path, sizeof path, "%s/textures.json", dir); remove(path);
#ifdef _WIN32
    _rmdir(dir);
#else
    rmdir(dir);
#endif
}

int ex_ensure_base_pack(const char *rom_path, const char *pack_dir, ExLogFn log, void *user)
{
    char tmp[1300];
    if (ex_pack_version(pack_dir) == EX_EXPORTER_VERSION) return 1;
    snprintf(tmp, sizeof tmp, "%s.tmp", pack_dir);
    remove_pack(tmp);                      /* a killed earlier run */
    if (ex_export_textures(rom_path, tmp, log, user)) { remove_pack(tmp); return -1; }
    remove_pack(pack_dir);                 /* the older pack (rename does not replace a folder on Windows) */
    if (rename(tmp, pack_dir)) {
        ex_logf(log, user, "texture pack: cannot rename %s to %s", tmp, pack_dir);
        remove_pack(tmp);
        return -1;
    }
    return 0;
}
