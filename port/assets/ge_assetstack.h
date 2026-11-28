/*
 * ge_assetstack.h -- asset packs (.passets): the player's ordered list of packs over which the active mod wins
 * (docs/specs/asset-packs.md). Shared by the game and the tools (ge_assets).
 *
 * An asset pack is a folder X.passets/ or an uncompressed ustar tar X.passets (the .pmod format, ge_pkg.h). It may provide
 * any kind of asset; each kind has its own index at the pack's root (textures: textures.json + textures/<n>.png today; models,
 * music, fonts as their readers land). Every pack may be partial. A .ptexpack (the older texture-pack name) is read as a pack
 * with textures only.
 *
 * Resolution of a key of any kind: the active mod if it has it (it always wins), else the first enabled pack in the player's
 * order, else the key is missing and the caller draws its fallback (the game's own texture while the game still draws from
 * the ROM; a checker, a box, silence, a default font once it draws from the export).
 *
 * All known packs are kept as one string, the file names in order separated by ';', a disabled one prefixed "-" (Video.AssetPacks).
 *
 * Plain C, no game headers. Reading a texture (assetStackReadTexture) is safe on any thread: each pack's index is read once
 * when the stack is opened and never changes afterwards (texpackIndexOpen).
 */
#ifndef GE_ASSETSTACK_H
#define GE_ASSETSTACK_H

#include <stddef.h>
#include <stdint.h>

#include "ge_texpacks.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ASSETPACK_FILE_LEN 128
#define ASSETPACK_MAX 32                 /* packs in a list or a stack */
#define ASSETPACK_LIST_LEN (ASSETPACK_MAX * ASSETPACK_FILE_LEN)

typedef struct AssetPackInfo {
    char file[ASSETPACK_FILE_LEN];   /* "hd.passets": its name in the packs folder, what the list holds */
    char dir[600];                   /* the folder it was found in */
    char name[64];                   /* pack.json "name", else the file name without its suffix */
    char author[64];
    char description[256];
    int base;                        /* pack.json "base": true: the export of the player's ROM (ge007) */
    int legacy;                      /* a .ptexpack: textures only, the older name */
} AssetPackInfo;

/* 1 when `name` is a pack's file name: "<something>.passets" or "<something>.ptexpack". */
int assetPackIsFileName(const char *name);

/* The packs in the folders `dirs` (regular files or folders named *.passets or *.ptexpack), sorted by file name. A file name
 * already found in an earlier folder is skipped. Fills up to `max` entries and returns how many there are in all. */
int assetPackScan(const char *const *dirs, int ndirs, AssetPackInfo *out, int max);

/* ---- the enabled list: file names in order, ';'-separated ---- */
/* Splits `list` into `files` (spaces around names trimmed, empty and repeated names dropped, names that are not a pack's file
 * name dropped). Returns how many. */
int assetPackListParse(const char *list, char (*files)[ASSETPACK_FILE_LEN], int max);
void assetPackListFormat(char (*files)[ASSETPACK_FILE_LEN], int n, char *out, size_t outlen);
/* Moves files[from] to position `to` (the others keep their order). 1 when something moved. */
int assetPackListMove(char (*files)[ASSETPACK_FILE_LEN], int n, int from, int to);
/* The position of `file` in the list, -1 when absent. */
int assetPackListFind(char (*files)[ASSETPACK_FILE_LEN], int n, const char *file);

/* ---- the whole list: every known pack in the player's order, a disabled one written "-name" ----
 * assetPackListParse above reads such a list too and returns only the enabled names (a "-name" is not a file name). A list
 * with no "-" means that all of its packs are enabled (the older form). */
int assetPackEntriesParse(const char *list, char (*files)[ASSETPACK_FILE_LEN], int *on, int max);   /* files without the "-", on[i] 0/1; returns how many */
void assetPackEntriesFormat(char (*files)[ASSETPACK_FILE_LEN], const int *on, int n, char *out, size_t outlen);
int assetPackEntriesMove(char (*files)[ASSETPACK_FILE_LEN], int *on, int n, int from, int to);   /* like assetPackListMove, the flag goes with the name */
int assetPackEntriesEnabled(char (*files)[ASSETPACK_FILE_LEN], const int *on, int n, char (*out)[ASSETPACK_FILE_LEN], int max);   /* the enabled names in order; returns how many */

/* ---- a stack: the enabled packs, opened in order ---- */
typedef struct AssetStack AssetStack;
/* Opens `packRoots` (full paths, first = highest priority). A root that cannot be read is skipped (assetStackOpened tells).
 * Never NULL unless out of memory. */
AssetStack *assetStackOpen(const char *const *packRoots, int n);
void assetStackClose(AssetStack *s);
int assetStackCount(const AssetStack *s);                 /* the packs asked for */
int assetStackOpened(const AssetStack *s, int i);         /* 1 when pack i could be read */
const char *assetStackRoot(const AssetStack *s, int i);
int assetStackTextureCount(const AssetStack *s, int i);   /* the textures pack i has a picture for */

enum { ASSET_FROM_NONE = -1, ASSET_FROM_MOD = -2 };       /* else the pack's position in the stack (0 = highest) */
/* Where texture `texnum` comes from: ASSET_FROM_MOD when `mod` (may be NULL) has it, else the first pack that has it, else
 * ASSET_FROM_NONE. */
int assetStackTextureFrom(const AssetStack *s, const TexPackIndex *mod, int texnum);
/* Reads that picture's PNG file: *out malloc'd (free()), *len bytes. 1 on success, 0 when missing or unreadable. */
int assetStackReadTexture(const AssetStack *s, const TexPackIndex *mod, int texnum, uint8_t **out, size_t *len);

#ifdef __cplusplus
}
#endif

#endif
