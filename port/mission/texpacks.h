/*
 * texpacks.h -- texture packs: replacement pictures for the game's textures, shared by the game and the editor.
 *
 * A texture pack is a `.ptexpack`: a plain uncompressed ustar tar (the .pmod format, mission_pkg.h) holding
 *
 *     pack.json              optional: {"name": "...", "author": "...", "description": "..."}
 *     textures/<n>.png       the picture for texture number n (0..4095, the game's own texture numbering)
 *
 * Packs sit in the game's texturepacks/ folder (next to data/ and mods/). An unpacked folder X.ptexpack/ works like the
 * tar X.ptexpack. A mod carries the same textures/<n>.png in its own folder. When several sources have a picture for
 * one texture, the first one in the caller's order wins: the mod's, then the selected pack's, then the game's own.
 *
 * The newer format (textures.json: one entry per texture key, a complete base pack ge007.ptexpack, mods merged over a pack) is the
 * parsed-pack API further down; docs/specs/texture-packs.md. The picture index above is the game's reader and does not use it.
 *
 * Plain C, no game headers. Not thread-safe (the package layer has one index cache). Built with -DTEXPACK_NO_PNG, texpackDecodePng
 * is left out (a reader without zlib, modelview, decodes the PNGs itself).
 */
#ifndef TEXPACKS_H
#define TEXPACKS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TEXPACK_FILE_LEN 128
#define TEXPACK_MAX_TEXNUM 4095

typedef struct TexPackInfo {
    char file[TEXPACK_FILE_LEN];   /* "hd.ptexpack": the name in texturepacks/, what Video.TexturePack holds */
    char name[64];                 /* pack.json "name", else the file name without .ptexpack */
    char author[64];
    char description[256];
} TexPackInfo;

/* The packs in `dir` (regular files or folders named *.ptexpack), sorted by file name. Fills up to `max` entries and
 * returns how many there are in all (0 when the folder is missing). */
int texpackScan(const char *dir, TexPackInfo *out, int max);

/* "<root>/textures/<n>.png": the picture of texture n in a pack (root = "<dir>/<file>") or a mod folder. */
void texpackPicturePath(const char *root, int texnum, char *out, size_t outlen);

/* The texture number of a file name "<n>.png" (any folder before it), -1 when it is not one. */
int texpackTexnumOfName(const char *name);

/* ---- one pack's pictures, for a reader on another thread ----
 * texpackIndexOpen reads the pack's table once (texture number -> file, offset, size): `root` is a .ptexpack tar or a
 * folder (X.ptexpack/ or a mod's folder) holding textures/<n>.png. The table never changes afterwards, so
 * texpackIndexRead may run on any thread (it opens the file itself, never the package layer's shared cache). */
typedef struct TexPackIndex TexPackIndex;
TexPackIndex *texpackIndexOpen(const char *root);   /* NULL when root is neither a readable tar nor a folder */
void texpackIndexClose(TexPackIndex *ix);
int texpackIndexCount(const TexPackIndex *ix);      /* how many texture numbers it has a picture for */
int texpackIndexHas(const TexPackIndex *ix, int texnum);
/* The PNG file of texture `texnum`: *out malloc'd (free()), *len bytes. 1 on success. */
int texpackIndexRead(const TexPackIndex *ix, int texnum, uint8_t **out, size_t *len);

/* PNG -> RGBA8 (top-down). 8-bit grey / grey+alpha / RGB / RGBA / palette (with tRNS), not interlaced. 1 on success;
 * *rgba is malloc'd (free with free()). Sizes over 8192 on a side are refused. */
int texpackDecodePng(const uint8_t *file, size_t len, int *w, int *h, uint8_t **rgba);


/* ---- the texture-pack format with textures.json (docs/specs/texture-packs.md) ----
 * A pack (X.ptexpack folder or tar) or a mod (X.pmod folder or tar) parsed once into a key -> entry table. Keys are the
 * texture number in decimal ("412") for the game's textures, or a mod's own name ("mymod/rusty"). A pack is complete (it has
 * every key of ge007.ptexpack and each picture), a mod is partial and merges field by field over the chosen pack. Not
 * thread-safe (it reads through the package layer). */
#define TEXPACK_KEY_LEN 64
#define TEXPACK_PATH_LEN 192
#define TEXPACK_SURFACE_NONE (-1)

enum {   /* TexPackTex.fields: which fields the entry gives */
    TEXPACK_F_IMAGE = 1, TEXPACK_F_SURFACE = 2, TEXPACK_F_IMPACT = 4, TEXPACK_F_ALPHA = 8, TEXPACK_F_FORMAT = 16,
    TEXPACK_F_WIDTH = 32, TEXPACK_F_HEIGHT = 64, TEXPACK_F_DETAIL = 128, TEXPACK_F_NORMAL = 256, TEXPACK_F_TILE = 512
};

typedef struct TexPackTex {
    char key[TEXPACK_KEY_LEN];
    unsigned fields;                 /* TEXPACK_F_*: the fields this entry has */
    int imageNull;                   /* TEXPACK_F_IMAGE and "image": null: the texture has no picture */
    char image[TEXPACK_PATH_LEN];    /* the picture's path inside its source; TEXPACK_F_IMAGE and not imageNull */
    int surface, impactSound;        /* 0..15 (texpackSurfaceName), TEXPACK_SURFACE_NONE when the field is absent or unknown */
    int alphaLuminance;              /* 0 "image", 1 "luminance" */
    char format[16];                 /* informational */
    int width, height;               /* informational */
    int tileWidth, tileHeight;       /* TEXPACK_F_TILE: the held (padded) size; else width x height (texture-packs.md) */
    int detail[4];                   /* informational: s_offset, t_offset, s_shift, t_shift */
    char normal[TEXPACK_PATH_LEN];   /* reserved */
} TexPackTex;

typedef struct TexPackMeta {
    char name[64], author[64], description[256], region[32];
    int base;                        /* pack.json "base": true: the game's own ge007.ptexpack */
    int exporterVersion;             /* pack.json "exporter_version", 0 when absent */
    int hasPackJson;
} TexPackMeta;

typedef struct TexPackSrc TexPackSrc;
/* Opens a pack or mod root (folder or tar) and reads its pack.json and textures.json (each may be missing: a mod has no
 * pack.json). NULL when `root` is neither a readable folder nor a tar. A key that is not valid (texpackKeyValid) is skipped. */
TexPackSrc *texpackSrcOpen(const char *root);
void texpackSrcClose(TexPackSrc *s);
const char *texpackSrcRoot(const TexPackSrc *s);
const TexPackMeta *texpackSrcMeta(const TexPackSrc *s);
int texpackSrcHasTexturesJson(const TexPackSrc *s);       /* textures.json was present and parsed */
int texpackSrcCount(const TexPackSrc *s);                 /* entries, sorted by key */
const TexPackTex *texpackSrcAt(const TexPackSrc *s, int i);
const TexPackTex *texpackSrcFind(const TexPackSrc *s, const char *key);
/* A file of the source by its path inside it ("textures/412.png"): 1 when it exists. Paths with "..", a drive or a leading
 * slash are refused. Read: *out malloc'd (free()), *len bytes. */
int texpackSrcHasFile(const TexPackSrc *s, const char *rel);
int texpackSrcReadFile(const TexPackSrc *s, const char *rel, uint8_t **out, size_t *len);

/* How many textures `pack` lacks against `base` (ge007.ptexpack): a key of base missing from the pack, or an entry whose base
 * image is not null and whose picture is not in the pack (no image, null, or the file is absent). 0 = complete. */
int texpackMissing(const TexPackSrc *pack, const TexPackSrc *base);

enum { TEXPACK_FROM_NONE = -1, TEXPACK_FROM_MOD = 0, TEXPACK_FROM_PACK = 1 };
typedef struct TexPackResolved {
    TexPackTex tex;                  /* the merged entry; tex.image is the path inside imageFrom's source */
    int imageFrom;                   /* TEXPACK_FROM_*: the source of the picture; NONE: the texture has no picture */
    unsigned modFields;              /* TEXPACK_F_*: the fields the mod gave (0: all from the pack) */
    int inPack;                      /* the chosen pack has an entry for the key */
    int modImageMissing;             /* the mod names a picture it does not hold (ignored: the pack's picture stays) */
} TexPackResolved;
/* The texture `key` seen through the mod (may be NULL) over the pack: field by field, the mod first. A mod
 * textures/<key>.png without an entry replaces the picture only. 0 when the key is unknown, or is a new mod key without a
 * picture or without a surface. */
int texpackResolve(const TexPackSrc *mod, const TexPackSrc *pack, const char *key, TexPackResolved *out);
/* The picture of a resolved texture: *out malloc'd (free()). 0 when it has none or cannot be read. */
int texpackResolvedRead(const TexPackSrc *mod, const TexPackSrc *pack, const TexPackResolved *r, uint8_t **out, size_t *len);
/* "mod" / "pack": a label for a source of a resolved texture. */
const char *texpackFromName(int from);

/* A key: non-empty, at most 63 bytes of a-z 0-9 _ - . / ; texpackKeyIsNumber: a decimal number without leading zeros. */
int texpackKeyValid(const char *key);
int texpackKeyIsNumber(const char *key);

/* The 13 hit classes: 0 default, 1 stone, 2 wood, 3 metal, 4 glass, 5 water, 6 snow, 7 dirt, 8 mud, 9 tile, 10 metalobj, 11 chr,
 * 12 glass_xlu; 13..15 are "class13".."class15". */
const char *texpackSurfaceName(int n);                 /* NULL out of 0..15 */
int texpackSurfaceFromName(const char *name);          /* -1 when it is not a name */

#ifdef __cplusplus
}
#endif
#endif
