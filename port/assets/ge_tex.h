/*
 * ge_tex.h -- texture resolution (ge_assets, issue #172): a texture key -> RGBA pixels + its textures.json entry, through the
 * active mod over the chosen texture pack over ge007.ptexpack (docs/specs/texture-packs.md). The one call every tool uses:
 * the editor, modelview and fxview (the game reads its pictures through ge_texpacks.h's index, on its render thread).
 *
 *     GeTexSet *s = geTexSetNew();
 *     geTexSetOpen(s, modRoot, packRoot, baseRoot);     // "" for none
 *     GeTexImage im;
 *     if (geTexResolve(s, "412", &im, NULL, NULL)) draw(im.w, im.h, im.rgba, im.tex.surface);
 *
 * The decoded pictures are cached by key; the cache is dropped by every geTexSetOpen and by geTexSetFree. Plain C, no GL. Not
 * thread-safe (it reads through the package layer).
 */
#ifndef GE_TEX_H
#define GE_TEX_H

#include "ge_texpacks.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GeTexSet GeTexSet;

GeTexSet *geTexSetNew(void);
void geTexSetFree(GeTexSet *s);

/* Opens the mod (`modRoot`: a folder or .pmod, "" or NULL none) afresh on every call, and the pack when its root changed since the
 * last call. `packRoot` is the chosen pack, `baseRoot` ge007.ptexpack (each "" when there is none). A chosen pack that is not
 * complete against the base is refused: the base is used instead and geTexSetNote says why. Without a base or a pack, geTexSetPack
 * is NULL. Returns 1 when the pack and base were (re)read (the note may have changed), 0 when only the mod was. */
int geTexSetOpen(GeTexSet *s, const char *modRoot, const char *packRoot, const char *baseRoot);
const char *geTexSetNote(const GeTexSet *s);          /* "" when the last pack choice was taken as asked */
const TexPackSrc *geTexSetMod(const GeTexSet *s);     /* NULL: no mod, or it cannot be read */
const TexPackSrc *geTexSetPack(const GeTexSet *s);    /* the chosen pack (the base when none was chosen or it was refused) */
const TexPackSrc *geTexSetBase(const GeTexSet *s);    /* ge007.ptexpack; NULL when it is missing */

enum { GE_TEX_FROM_BASE = -1, GE_TEX_FROM_MOD = 0, GE_TEX_FROM_PACK = 1 };   /* GeTexImage.source */

typedef struct GeTexImage {
    int w, h;
    const uint8_t *rgba;      /* w*h*4, top row first, owned by the set: valid until the next geTexSetOpen / geTexSetFree */
    TexPackTex tex;           /* the merged entry (mod fields over the pack's) */
    int from;                 /* TEXPACK_FROM_MOD / TEXPACK_FROM_PACK: the source of the picture */
    int source;               /* GE_TEX_FROM_*: the mod, ge007.ptexpack, or another pack */
    char path[TEXPACK_PATH_LEN + 1100];   /* "<source root>/<image>": where the pixels came from */
} GeTexImage;

typedef void (*GeTexWarnFn)(void *user, const char *msg);

/* The picture of `key` through the mod over the pack. 1 when a source had it and it decoded. A mod picture that is missing or does
 * not decode is skipped with a warning and the pack's is tried; under alpha "luminance" an opaque picture gets
 * (299 R + 587 G + 114 B) / 1000 as alpha (the spec's rule). `warn` (may be NULL) gets each warning, also on a cached answer. */
int geTexResolve(GeTexSet *s, const char *key, GeTexImage *out, GeTexWarnFn warn, void *user);

/* ---- the texture packs of a folder ---- */
typedef struct GeTexPackChoice {
    char file[TEXPACK_FILE_LEN];   /* "ge007.ptexpack", "hd.ptexpack": what a preference holds */
    char path[1024];               /* the pack's root; "" for ge007 when it does not exist */
    char name[64], author[64], description[256];
    int base;                      /* ge007.ptexpack */
    int missing;                   /* textures it lacks against ge007; > 0: not selectable */
    char reason[96];               /* why it is not selectable, "" when it is */
} GeTexPackChoice;

/* ge007.ptexpack's root into out: <packsDir>/ge007.ptexpack when the game wrote it, else <exportRoot>/ge007.ptexpack; "" when
 * neither exists. Separators are '/'. */
void geTexBaseRoot(const char *packsDir, const char *exportRoot, char *out, size_t outlen);
/* The packs to choose from: ge007 first (always, even when missing: then it has a reason), then the other packs of the folder, each
 * checked against ge007 (opens every pack: not cheap). Fills up to `max`, returns how many there are in all. */
int geTexPackList(const char *packsDir, const char *exportRoot, GeTexPackChoice *out, int max);
/* The root a preference value ("" = ge007, else a file name of the folder) stands for. */
void geTexPackPath(const char *pref, const char *packsDir, const char *exportRoot, char *out, size_t outlen);

#ifdef __cplusplus
}
#endif
#endif
