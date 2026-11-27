/*
 * export.h -- static level export (issue #150): ROM -> level decor mesh +
 * textures -> glTF 2.0 (.gltf + .bin + PNG).
 *
 * CLEAN ROOM: everything in port/export/ is written from docs/specs/ only.
 * No decomp header, no src/game, no include/, no PR/gbi.h, no code derived
 * from them. Freestanding like port/convert/: libc + zlib (+ libm), and the
 * converter library for the ROM file table, rarezip inflate and the ROM check
 * (port/convert/convert.h, conv_romcheck.h).
 *
 * Output is ROM-derived: it goes only to the directory given, never into the
 * repo or a mod.
 */
#ifndef PORT_EXPORT_H
#define PORT_EXPORT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EX_EXPORTER_VERSION 5   /* written to every glTF (asset.extras.exporter_version); bump on any output change */

/* ---- textures (tex_decode.c, docs/specs/textures.md) --------------------- */

typedef struct ExTexTable {
    const uint8_t *rom;        /* whole .z64 image (big-endian), not owned */
    size_t rom_size;
    uint32_t images_start;     /* ROM offset of the images segment */
    uint32_t count;            /* textures in the size table (before the 0xFFFF terminator) */
    uint32_t *offset;          /* count + 1 entries: start of texture n, relative to images_start; offset[count] = total */
    uint32_t *word1;           /* count entries: word 1 of the size-table entry (detail fields), raw */
} ExTexTable;

typedef struct ExTexture {
    int texnum;
    int width, height;         /* level 0 */
    int format;                /* 0..12, textures.md */
    int zlib;                  /* 1 = zlib family, 0 = bit-packed */
    int method;                /* bit-packed method 0..9; -1 for zlib */
    int explicit_lods;         /* header bit 7 (the UV half-texel rule needs it) */
    uint8_t *rgba;             /* width * height * 4, row 0 at the top; malloc'd */
} ExTexture;

/* Locates the size table and the images segment for this ROM's region
 * ("ntsc-final" uses the measured constants; other regions: the checked search
 * of textures.md, or failure). 0 on success; on failure *err names why. */
int ex_tex_table_open(ExTexTable *t, const uint8_t *rom, size_t rom_size, const char *region, const char **err);
void ex_tex_table_close(ExTexTable *t);

/* Decodes level 0 of texture `texnum` to RGBA8. 0 on success; on failure
 * *err names why (unknown format / method, size out of range, truncated). */
int ex_tex_decode(const ExTexTable *t, int texnum, ExTexture *out, const char **err);
void ex_tex_free(ExTexture *tex);

/* Writes an 8-bit RGBA PNG (zlib-compressed IDAT, no filter needed). 0 on success. */
int ex_png_write(const char *path, int width, int height, const uint8_t *rgba);
/* Same, into memory (malloc'd *out, *out_len bytes). 0 on success. */
int ex_png_encode(int width, int height, const uint8_t *rgba, uint8_t **out, size_t *out_len);

/* ---- levels (level_export.c, docs/specs/level-decor.md, display-lists.md) */

typedef void (*ExLogFn)(void *user, const char *line);

/* Exports every decor file of the ROM (the levels of level-decor.md) into
 * <out_dir>/<level>/<level>.gltf + <level>.bin + textures/<texnum>.png and
 * <out_dir>/<level>/report.txt (opcode census, skipped commands, warnings).
 * The ROM must pass cv_romcheck. Returns the number of levels that failed
 * (0 = all exported). `log` may be NULL. */
int ex_export_levels(const char *rom_path, const char *out_dir, ExLogFn log, void *user);

/* ---- object models (model_export.c, docs/specs/models.md) ---------------- */

/* Exports every object model of the model-number table into
 * <out_dir>/<file name without the trailing Z>/<same>.gltf + .bin + textures/<n>.png
 * and <out_dir>/models.json (one entry per model number: file, base scale,
 * bounding box, node and triangle counts, textures, notes). Returns the number
 * of models that failed, or 1000 + the count when the model table is refused
 * (its invariants do not hold for this ROM). `log` may be NULL. */
int ex_export_models(const char *rom_path, const char *out_dir, ExLogFn log, void *user);

#ifdef __cplusplus
}
#endif

#endif
