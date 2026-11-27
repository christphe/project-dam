/* export_common.h -- code shared by the level exporter (level_export.c) and the
 * model exporter (model_export.c): byte readers, JSON/file helpers, the
 * display-list state, material keys, primitive building and the glTF writer.
 *
 * CLEAN ROOM: written from docs/specs/ only (see export.h). Binary output is
 * little-endian (every supported host is little-endian; glTF buffers are).
 */
#ifndef PORT_EXPORT_COMMON_H
#define PORT_EXPORT_COMMON_H

#include "export.h"
#include "convert.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ---- bytes, JSON, files ---- */

static inline uint32_t ex_be32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
static inline uint16_t ex_be16(const uint8_t *p) { return (uint16_t)(((unsigned)p[0] << 8) | p[1]); }
static inline int16_t ex_be16s(const uint8_t *p) { return (int16_t)ex_be16(p); }
static inline float ex_bef32(const uint8_t *p) { uint32_t u = ex_be32(p); float f; memcpy(&f, &u, 4); return f; }

int ex_grow(void **arr, size_t *cap, size_t need, size_t esz);
void ex_jp(CvBuf *b, const char *fmt, ...);
void ex_jstr(CvBuf *b, const char *s);
int ex_mkdir_p(const char *path);
int ex_write_file(const char *path, const void *d, size_t n);
int ex_read_rom(const char *path, uint8_t **out, size_t *len);
void ex_logf(ExLogFn log, void *user, const char *fmt, ...);
/* Validates the ROM (SHA-1) and finds its file table. 0 on success (log says why on failure). */
int ex_open_rom(const char *rom_path, ExLogFn log, void *user, uint8_t **rom, size_t *rom_size, const char **region,
                const CvRegionData **rd);

/* ---- geometry mode / other-mode bits ---- */

#define GEOM_TEXGEN        0x00040000u   /* texture generation */
#define GEOM_TEXGEN_LINEAR 0x00080000u   /* its linear variant (display-lists.md, corrected) */
#define GEOM_LIGHTING      0x00020000u   /* lighting: colour bytes are a normal (models.md) */

/* ---- material key ---- */

typedef struct {   /* all uint32 so memcmp is a valid comparison */
    uint32_t textured;      /* 1: baseColorTexture */
    uint32_t texnum;        /* 0xFFFFFFFF: none */
    uint32_t decode_fail;   /* texture wanted but could not be decoded */
    uint32_t list;          /* 0 primary, 1 secondary */
    uint32_t type, smode, tmode, offset, shifts, shiftt, min, second;
    uint32_t comb0, comb1, omh, oml, geom;
    uint32_t env, prim;
    uint32_t ks, kt;        /* raw texture scale values (textured only) */
} MatKey;

typedef struct {
    MatKey k;
    int tw, th, expl;       /* of the decoded texture (textured only) */
    char err[128];
    int env_rule;           /* room rule: secondary-list shade alpha -> environment alpha */
    uint32_t normal_mask;   /* geometry-mode bit that turns the colour bytes into a normal */
    int emit_decal;         /* write extras.decal for a decal z mode */
} Material;

int ex_uses_env_alpha(const Material *m);
int ex_alpha_mode(uint32_t oml);
double ex_scale_of(uint32_t raw);

/* ---- texture cache (decoded textures, kept for the whole export) ---- */

typedef struct {
    int texnum;
    int ok;
    ExTexture t;
    char err[128];
} TexEnt;

typedef struct {
    const uint8_t *rom; size_t rom_size;
    const char *region;
    ExTexTable tbl; int tbl_ok; const char *tbl_err;
    TexEnt *tex; size_t nt, capt;
    int oom;
} ExTexCache;

void ex_texcache_open(ExTexCache *tc, const uint8_t *rom, size_t rom_size, const char *region, ExLogFn log, void *user);
void ex_texcache_close(ExTexCache *tc);
void ex_texcache_clear(ExTexCache *tc);     /* drops the decoded textures, keeps the table */
TexEnt *ex_texcache_get(ExTexCache *tc, int texnum);
/* Writes <dir>/textures/<n>.png for every textured material's texture. *count = files written. 0 on success. */
int ex_write_textures(ExTexCache *tc, const Material *mats, size_t nm, const char *dir, size_t *count);

/* ---- display-list state shared by every list interpreter ---- */

typedef struct { int16_t x, y, z, s, t; uint8_t c[4]; int valid; int mtx; } ExVtx;

typedef struct {
    uint32_t geom, omh, oml, c0, c1, env, prim;
    int have_tex;
    uint32_t texnum, type, smode, tmode, offset, shifts, shiftt, min, second;
    uint32_t ks, kt;
    ExVtx cache[16];
    int mat;   /* -1: dirty */
    int mtx;   /* matrix number in force (models) */
} ExGfxState;

/* Handles the state commands 0xBB 0xB7 0xB6 0xBA 0xB9 0xFC 0xFA 0xFB 0xC0. 1 = handled. */
int ex_dl_state(ExGfxState *s, unsigned op, uint32_t w0, uint32_t w1);

/* ---- materials of one export unit (a level or a model) ---- */

typedef struct {
    ExTexCache *tc;
    Material *mats; size_t nm, capm;
    int env_rule; uint32_t normal_mask; int emit_decal;
    int oom;
} MatCtx;

void ex_matctx_free(MatCtx *mc);
void ex_matctx_reset(MatCtx *mc);
int ex_resolve_material(MatCtx *mc, const ExGfxState *s, int list);

/* ---- primitives ---- */

typedef struct { int16_t v[5]; uint8_t c[4]; } VKey;   /* x y z s t rgba: 14 bytes, no padding */

typedef struct {
    int mat;
    float *pos; float *uv; uint8_t *col; VKey *key;
    size_t nv, capv;
    uint32_t *idx; size_t ni, capi;
    uint32_t *ht; size_t hcap;      /* open addressing, value = vertex index + 1 */
} Prim;

typedef struct { Prim *prims; size_t np, cap, last; } PrimSet;

Prim *ex_primset_find(PrimSet *ps, int mat, int *oom);
void ex_primset_free(PrimSet *ps);
/* index of the vertex in the primitive, or -1 on OOM. Positions are v / scale. */
long ex_prim_vertex(Prim *p, const ExVtx *v, const Material *m, double scale, const ExGfxState *s);

/* ---- glTF writer ---- */

typedef struct { CvBuf bin, views, accs; int nviews, naccs; } GW;

int gw_view(GW *g, const void *d, size_t n, int target);
int gw_acc(GW *g, int view, int ctype, size_t count, const char *type, int norm, const float *mn, const float *mx, int ncomp);
void gw_free(GW *g);
/* Appends the primitives of `ps` to the JSON array body `pj` (no brackets); returns how many, -1 on OOM. */
int ex_emit_prims(GW *g, CvBuf *pj, const PrimSet *ps, const MatCtx *mc);

/* Samplers, textures, images and materials JSON of one export unit. */
typedef struct { int texnum, ws, wt; } GTex;
typedef struct {
    CvBuf mats, texs, imgs, smps;
    GTex *gt; size_t ngt, capgt;
    int *imgtex; size_t nimg, capimg;
    int smap[9], nsmp;
} MatOut;

void ex_matout_init(MatOut *mo);
void ex_matout_free(MatOut *mo);
int ex_emit_materials(MatOut *mo, const MatCtx *mc);   /* 0 ok, -1 OOM */
/* ",\"textures\":[...],\"images\":[...],\"samplers\":[...]" when there are any */
void ex_emit_texture_arrays(CvBuf *json, const MatOut *mo);

#endif
