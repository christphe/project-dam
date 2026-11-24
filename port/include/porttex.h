#ifndef PORTTEX_H
#define PORTTEX_H

/*
 * porttex.h -- the port's own textures, by name (project-dam).
 *
 * A registry of RGBA32 textures the port creates or loads, separate from the game's ROM textures. Each has
 * a name ("impact/stone/0/color", "impact/cluster/3/depth", later a level's or a mod's own), an id, a size
 * and a version that changes with its content. A display list binds one to a tile by id with
 * G_PORTTEX_EXT (fast3d caches it on its pixels and version), so engine code, and later level data, can
 * reference textures that are not in the ROM and are larger than TMEM.
 *
 * Registering a name that exists replaces it in place (same id): whoever registers a name last provides
 * it. Pure C, no SDL, no game state (tests/test_porttex.c).
 */

#ifdef __cplusplus
extern "C" {
#endif

#define PORTTEX_MAX      256
#define PORTTEX_NAME_MAX 64

enum {
    PORTTEX_COPY     = 0,        /* the registry keeps its own copy of the pixels */
    PORTTEX_EXTERNAL = 1 << 0,   /* the pixels stay the caller's (a texture it updates: call portTexTouch after) */
};

typedef struct PortTex {
    char name[PORTTEX_NAME_MAX];
    int w, h;
    const unsigned char *rgba;   /* w x h x 4, rows from the top */
    unsigned int version;        /* changes whenever the pixels do */
    int flags;
} PortTex;

/* Creates or replaces `name` (w x h RGBA32). Returns its id (>= 0), or -1 (bad arguments, full, no memory). */
int portTexRegister(const char *name, int w, int h, const unsigned char *rgba, int flags);

/* The id of `name`, or -1. */
int portTexFind(const char *name);

/* The texture with this id, or NULL. */
const PortTex *portTexGet(int id);

/* Its pixels changed (an external buffer the caller updated): a new version, fast3d uploads it again. */
void portTexTouch(int id);

/* Frees an entry (its id becomes free; a later register may reuse it). */
void portTexRelease(int id);

/* Drops everything (tests, and a level change if a caller wants it). */
void portTexReset(void);

/* G_PORTTEX_EXT: bind texture `id` to `tile` (0 or 1) for the draws that follow; id -1 ends it. fast3d
 * takes the texture's own size for the tile's coordinates. */
#define G_PORTTEX_EXT 0x4B
#define gDPPortTexture(pkt, tile, id) do { \
        Gfx *_g = (Gfx *)(pkt); \
        _g->words.w0 = ((unsigned int)G_PORTTEX_EXT << 24) | ((unsigned int)(tile) & 1u); \
        _g->words.w1 = (unsigned int)((id) + 1); \
    } while (0)

/* G_PORTTEX_CAPTURE_EXT: fast3d decodes the game texture loaded for tile 0 (as the draw that would follow
 * sees it) and registers its pixels under the name of texture `id` (PORTTEX_COPY), which gets a new version.
 * So port code can read a ROM texture by name without decoding N64 formats itself. */
#define G_PORTTEX_CAPTURE_EXT 0x4C
#define gDPPortTexCapture(pkt, id) do { \
        Gfx *_g = (Gfx *)(pkt); \
        _g->words.w0 = (unsigned int)G_PORTTEX_CAPTURE_EXT << 24; \
        _g->words.w1 = (unsigned int)((id) + 1); \
    } while (0)

#ifdef __cplusplus
}
#endif

#endif
