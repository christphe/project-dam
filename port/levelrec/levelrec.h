#ifndef PORT_LEVELREC_H
#define PORT_LEVELREC_H

/* Level export for the mission editor (issues #100-#102).
 *
 * `ge007 -level_XX --export-level DIR` (port/src/levelexport.c) writes:
 *   DIR/level.rec   this binary file: fast3d's own draw stream for every room
 *                   and every object/guard model type the level uses
 *   DIR/level.json  stan tiles, portals, pads, setup props (see below)
 * The editor (editor/) replays level.rec through fast3d's GfxRenderingAPI
 * (create_and_load_new_shader + upload_texture + mesh_create/mesh_draw), so a
 * level looks as in the game. Both files are ROM-derived: they live in the
 * user's data dir (gitignored) and are never shared or committed.
 *
 * level.rec, version 1, little-endian, no padding between records:
 *   LevelRecHeader
 *   ntextures x { LevelRecTexture, w*h*4 bytes RGBA32 }
 *   nowners   x LevelRecOwner
 *   nruns     x { LevelRecRun, nmtx*16 floats (model->world, row vectors:
 *                 world = (x y z 1) * M), nfloats floats of vertex data }
 *
 * Vertex data is the GE_MESHGPU mesh layout of the run's shader program
 * (port/fast3d/gfx_rendering_api.h, struct GfxMeshDraw): the position
 * attribute holds (model x, y, z, matrix index), the other attributes are the
 * program's. To replay: clip matrix i = world[i] * view * proj, put it in
 * GfxMeshDraw.mtx[i], copy the run's GfxMeshDraw fields, mesh_draw.
 *
 * level.json, version 1:
 * { "version": 1, "level": "bunker1", "build": "<stamp>",
 *   "rooms":   [ { "index": 1, "min": [x,y,z], "max": [x,y,z] } ],
 *   "stan":    [ { "room": 1, "v": [[x,y,z], [x,y,z], [x,y,z], ...] } ],   floor tiles (polygons)
 *   "portals": [ { "rooms": [1, 2], "v": [[x,y,z], ...] } ],
 *   "pads":    [ { "num": 0x2715, "pos": [x,y,z], "look": [x,y,z], "up": [x,y,z], "room": 1 } ],
 *   "props":   [ { "index": 17, "type": 9, "pad": 0x2715, "model": 3, "item": -1,
 *                  "ai": 0x401, "owner": 5 } ],        owner: LevelRecOwner index drawn for it, -1 none
 *   "ailists": [ 0x401, ... ] }
 * "index" is the prop's position in the setup propDefs list, the number
 * mission.json "prop" refers to.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LEVELREC_MAGIC   0x524C4547u   /* "GELR" */
#define LEVELREC_VERSION 1u
#define LEVELREC_MAX_MTX 32           /* = GFX_MESH_MAX_MTX */
#define LEVELREC_MAX_INPUTS 8         /* = GFX_MESH_MAX_INPUTS */

#pragma pack(push, 1)
typedef struct LevelRecHeader {
    uint32_t magic, version;
    char build[64];                   /* GE007_VERSION_HASH of the exporting game: re-export when it changes */
    char level[32];                   /* "bunker1" */
    uint32_t ntextures, nowners, nruns;
} LevelRecHeader;

typedef struct LevelRecTexture {
    uint64_t hash;                    /* hash of the RGBA32 pixels: stable name across runs */
    uint16_t w, h;
} LevelRecTexture;

enum { LEVELREC_OWNER_ROOM = 0, LEVELREC_OWNER_MODEL = 1 };

typedef struct LevelRecOwner {
    uint32_t kind;                    /* LEVELREC_OWNER_* */
    int32_t id;                       /* room index, or model id */
    char name[32];
} LevelRecOwner;

typedef struct LevelRecRun {
    uint32_t owner;                   /* index into the owner table */
    uint64_t shader_id0;              /* create_and_load_new_shader(id0, id1) */
    uint32_t shader_id1;
    int32_t tex[2];                   /* texture table index per tile, -1 none */
    uint8_t linear[2], cms[2], cmt[2], mipmaps[2];   /* set_sampler_parameters per tile */
    uint8_t depth_test, depth_update, depth_compare, depth_source_prim;
    uint16_t zmode;
    uint8_t use_alpha, modulate;      /* set_use_alpha */
    uint8_t cull, fog_from_z;         /* GfxMeshDraw */
    float fog_color[4], fog_mul, fog_off;
    float lod_mask[LEVELREC_MAX_INPUTS][4];
    float env_in[LEVELREC_MAX_INPUTS][4];
    float env_mask[LEVELREC_MAX_INPUTS][4];
    uint8_t tpf0, tpf1;
    uint32_t nverts, nmtx, nfloats;
} LevelRecRun;
#pragma pack(pop)

/* ---- reader: whole file in memory, pointers into it ---------------------- */

typedef struct LevelRecTexView { const LevelRecTexture *t; const uint8_t *rgba; } LevelRecTexView;
typedef struct LevelRecRunView { const LevelRecRun *r; const float *mtx; const float *verts; } LevelRecRunView;

typedef struct LevelRec {
    uint8_t *data; size_t size;
    const LevelRecHeader *hdr;
    LevelRecTexView *tex;
    const LevelRecOwner *owners;
    LevelRecRunView *runs;
} LevelRec;

static inline void levelrecFree(LevelRec *lr)
{
    free(lr->data); free(lr->tex); free(lr->runs);
    memset(lr, 0, sizeof(*lr));
}

/* Takes ownership of data (malloc'd). Returns 1 if the file is complete and valid. */
static inline int levelrecParse(LevelRec *lr, uint8_t *data, size_t size)
{
    memset(lr, 0, sizeof(*lr));
    lr->data = data; lr->size = size;
    size_t off = sizeof(LevelRecHeader);
    if (size < off) goto bad;
    lr->hdr = (const LevelRecHeader *)data;
    if (lr->hdr->magic != LEVELREC_MAGIC || lr->hdr->version != LEVELREC_VERSION) goto bad;
    lr->tex = (LevelRecTexView *)calloc(lr->hdr->ntextures + 1, sizeof(LevelRecTexView));
    lr->runs = (LevelRecRunView *)calloc(lr->hdr->nruns + 1, sizeof(LevelRecRunView));
    for (uint32_t i = 0; i < lr->hdr->ntextures; i++) {
        if (off + sizeof(LevelRecTexture) > size) goto bad;
        const LevelRecTexture *t = (const LevelRecTexture *)(data + off);
        off += sizeof(*t);
        size_t px = (size_t)t->w * t->h * 4;
        if (off + px > size) goto bad;
        lr->tex[i].t = t; lr->tex[i].rgba = data + off;
        off += px;
    }
    if (off + (size_t)lr->hdr->nowners * sizeof(LevelRecOwner) > size) goto bad;
    lr->owners = (const LevelRecOwner *)(data + off);
    off += (size_t)lr->hdr->nowners * sizeof(LevelRecOwner);
    for (uint32_t i = 0; i < lr->hdr->nruns; i++) {
        if (off + sizeof(LevelRecRun) > size) goto bad;
        const LevelRecRun *r = (const LevelRecRun *)(data + off);
        off += sizeof(*r);
        if (r->nmtx > LEVELREC_MAX_MTX || r->owner >= lr->hdr->nowners) goto bad;
        size_t nf = (size_t)r->nmtx * 16 + r->nfloats;
        if (off + nf * sizeof(float) > size) goto bad;
        lr->runs[i].r = r;
        lr->runs[i].mtx = (const float *)(data + off);
        lr->runs[i].verts = lr->runs[i].mtx + (size_t)r->nmtx * 16;
        off += nf * sizeof(float);
    }
    return off == size;
bad:
    levelrecFree(lr);
    return 0;
}

static inline int levelrecLoadFile(LevelRec *lr, const char *path)
{
    memset(lr, 0, sizeof(*lr));
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = (uint8_t *)malloc(n > 0 ? (size_t)n : 1);
    size_t got = buf ? fread(buf, 1, (size_t)n, f) : 0;
    fclose(f);
    if (!buf || got != (size_t)n) { free(buf); return 0; }
    return levelrecParse(lr, buf, got);
}

/* ---- writer: append records in file order -------------------------------- */
/* Write the header with the final counts first (the exporter knows them once
 * the capture is done), then the records in the order above. */
static inline int levelrecWrite(FILE *f, const void *p, size_t n) { return fwrite(p, 1, n, f) == n; }

#ifdef __cplusplus
}
#endif

#endif
