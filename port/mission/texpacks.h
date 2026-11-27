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
 * Plain C, no game headers. Not thread-safe (the package layer has one index cache).
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

#ifdef __cplusplus
}
#endif
#endif
