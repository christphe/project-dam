/*
 * fxclusters.c -- the bullet holes on the level as clusters (Video.Visuals = Enhanced).
 *
 * The holes on one surface are stamped into one cluster texture with "the deepest wins" per texel
 * (port/include/fxcluster.h): a new hole never paints its rim over an older hole's pit, and there are no
 * seams. Each cluster is drawn as one quad; uber.frag.glsl fxClusterPom runs parallax occlusion over its
 * fused depth and lights the relief.
 *
 * Textures go through the port's texture registry (port/include/porttex.h), by name:
 *  - the hole templates: "impact/<material>/<variant>/color" (RGBA; A = coverage; RGB a multiplier of the
 *    material's base colour around 128, or the colour itself for an absolute material) and ".../depth"
 *    (R = depth, 0 the wall .. 255 the deepest; G = 255 marks a hole that stays empty, glass). The built-in
 *    generator registers them at boot; whoever registers the same name later replaces a template, with no
 *    code change. The per-material parameters (depth, light, colour mode, fusion rule) are kMat below.
 *  - each cluster: "impact/cluster/<n>/color" (128 x 128) and ".../depth" (64 x 64), external buffers this
 *    file updates and touches at each impact.
 *  - the game's own hole images: "game/impactimages/<type>", decoded by fast3d on request
 *    (G_PORTTEX_CAPTURE_EXT); glass holes are made from them (fxc_template_glass_from).
 *
 * A hole's colour is the hit wall's: its texture's average (fast3d keeps it per decoded texture,
 * gfx_texture_average) times the triangle's baked light, 15 % lighter. A cluster only covers the surface it
 * is on: the room's triangles coplanar with the hit one are rasterised into a mask, so a hole near an edge
 * stops at the edge instead of hanging in the air.
 */
#include "platform.h"
#include <ultra64.h>
#include <stdio.h>
#include "bondtypes.h"
#include "game/bondview.h"
#include "game/bgroomtrans.h"
#include "game/dyn.h"
#include "game/explosion.h"
#include <gbi_extension.h>
#include "game/bg.h"
#include "game/image_bank.h"
#include "game/othermodemicrocode.h"
#include "game/image.h"
#include <stdlib.h>
#include "system.h"
#include "game/tex.h"
#include "portevents.h"
#include "portfx.h"
#include "porttex.h"
#include "fxcluster.h"
#include "fxclusters.h"

extern struct s_impacttype g_ImpactTypes[];
f32 get_room_data_float1(void);
Mtx *currentPlayerGetProjectionMatrix(void);
Mtx *get_BONDdata_field_10E0(void);
int gfx_texture_average(uintptr_t w1, float out[3]);   /* port/fast3d/gfx_pc.cpp */

#define FXCL_MAX   12    /* clusters kept; the oldest is reused */
#define FXCL_DEPTH 64    /* the depth texture's size (half the colour's: a softer transition) */
#define FXCL_VARS  4     /* variants per material */

/* The materials (port/include/portevents.h PMAT_* map onto them) and their parameters. */
typedef struct MatInfo {
    const char *name;
    int gen;              /* fxcluster.h FXC_* generator */
    float light;          /* how much the relief is lit (0: snow, glass) */
    float base[3];        /* the base colour a tint template multiplies (the hit texture's average, later) */
} MatInfo;

static const MatInfo kMat[] = {
    { "stone", FXC_STONE, 1.0f, { 0.38f, 0.37f, 0.35f } },
    { "wood",  FXC_WOOD,  1.0f, { 0.42f, 0.31f, 0.19f } },
    { "metal", FXC_METAL, 1.0f, { 1.0f, 1.0f, 1.0f } },
    { "dirt",  FXC_DIRT,  1.0f, { 0.30f, 0.25f, 0.19f } },
    { "snow",  FXC_SNOW,  0.0f, { 0.70f, 0.72f, 0.76f } },
    { "tile",  FXC_TILE,  1.0f, { 0.42f, 0.43f, 0.41f } },
    { "glass", FXC_GLASS, 0.0f, { 1.0f, 1.0f, 1.0f } },
};
#define NMAT ((int)(sizeof kMat / sizeof kMat[0]))

static int matOf(int pmat)
{
    switch (pmat) {
        case PMAT_WOOD: return 1;
        case PMAT_METAL: case PMAT_METALOBJ: return 2;
        case PMAT_DIRT: case PMAT_MUD: return 3;
        case PMAT_SNOW: return 4;
        case PMAT_TILE: return 5;
        case PMAT_GLASS: case PMAT_GLASS_XLU: return 6;
        default: return 0;
    }
}

/* ---- the templates, through the registry ------------------------------------------------------ */

static unsigned int s_tplVersion[NMAT][FXCL_VARS][2];   /* the registry versions a cached template was built from */
static FxcTemplate s_tpl[NMAT][FXCL_VARS];
static int s_tplReady[NMAT][FXCL_VARS];

static void registerBuiltIn(void)
{
    static FxcTemplate t;
    static unsigned char col[FXC_TPL * FXC_TPL * 4], dep[FXC_TPL * FXC_TPL * 4];
    char name[PORTTEX_NAME_MAX];
    for (int m = 0; m < NMAT; m++) {
        for (int v = 0; v < FXCL_VARS; v++) {
            fxc_template(&t, kMat[m].gen, v);
            for (int i = 0; i < FXC_TPL * FXC_TPL; i++) {
                __builtin_memcpy(&col[i * 4], &t.rgba[i * 4], 4);
                const float d = t.depth[i];
                dep[i * 4 + 0] = (unsigned char)(d > 0.0f ? (d > 1.0f ? 255.0f : d * 255.0f + 0.5f) : 0.0f);
                dep[i * 4 + 1] = d < 0.0f ? 255 : 0;   /* a hole that stays empty (glass) */
                dep[i * 4 + 2] = 0;
                dep[i * 4 + 3] = 255;
            }
            snprintf(name, sizeof name, "impact/%s/%d/color", kMat[m].name, v);
            portTexRegister(name, FXC_TPL, FXC_TPL, col, PORTTEX_COPY);
            snprintf(name, sizeof name, "impact/%s/%d/depth", kMat[m].name, v);
            portTexRegister(name, FXC_TPL, FXC_TPL, dep, PORTTEX_COPY);
        }
    }
}

/* The template for (material, variant) from the registry's textures (any size: sampled at FXC_TPL), rebuilt
 * when someone registered a new version. The parameters stay the generator's for its material. */
static const FxcTemplate *templateFor(int m, int v)
{
    char cn[PORTTEX_NAME_MAX], dn[PORTTEX_NAME_MAX];
    snprintf(cn, sizeof cn, "impact/%s/%d/color", kMat[m].name, v);
    snprintf(dn, sizeof dn, "impact/%s/%d/depth", kMat[m].name, v);
    const PortTex *c = portTexGet(portTexFind(cn)), *d = portTexGet(portTexFind(dn));
    if (c == NULL || d == NULL) return v > 0 ? templateFor(m, 0) : NULL;
    if (s_tplReady[m][v] && s_tplVersion[m][v][0] == c->version && s_tplVersion[m][v][1] == d->version) return &s_tpl[m][v];
    FxcTemplate *t = &s_tpl[m][v];
    static FxcTemplate params;
    fxc_template(&params, kMat[m].gen, 0);   /* depthWorld, absolute, glass: the material's own */
    t->depthWorld = params.depthWorld;
    t->absolute = params.absolute;
    t->glass = params.glass;
    for (int y = 0; y < FXC_TPL; y++) {
        for (int x = 0; x < FXC_TPL; x++) {
            const int cx = x * c->w / FXC_TPL, cy = y * c->h / FXC_TPL, dx = x * d->w / FXC_TPL, dy = y * d->h / FXC_TPL;
            const unsigned char *cp = &c->rgba[(cy * c->w + cx) * 4], *dp = &d->rgba[(dy * d->w + dx) * 4];
            const int i = y * FXC_TPL + x;
            __builtin_memcpy(&t->rgba[i * 4], cp, 4);
            t->depth[i] = dp[1] >= 128 ? -1.0f : dp[0] / 255.0f;
        }
    }
    s_tplVersion[m][v][0] = c->version;
    s_tplVersion[m][v][1] = d->version;
    s_tplReady[m][v] = 1;
    return t;
}

/* ---- clusters ------------------------------------------------------------------------------ */

typedef struct Cluster {
    int used;
    unsigned int age;
    float origin[3], T[3], B[3], N[3];
    float size;          /* world units across */
    int room, mat;
    FxcCluster c;
    unsigned char mask[FXC_N * FXC_N];   /* 1 where the surface is (the coplanar triangles) */
    unsigned char depthTex[FXCL_DEPTH * FXCL_DEPTH * 4];
    int colorId, depthId;
} Cluster;

static Cluster s_cl[FXCL_MAX];
static unsigned int s_age;
static unsigned int s_rng = 0x9E3779B9u;
static int s_ready;

static float rnd01(void)
{
    s_rng ^= s_rng << 13; s_rng ^= s_rng >> 17; s_rng ^= s_rng << 5;
    return (float)(s_rng & 0xFFFFFF) / (float)0x1000000;
}

static float dot3(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

static void upload(Cluster *k)
{
    static unsigned char tmpRgba[FXCL_DEPTH * FXCL_DEPTH * 4];
    static float dep01[FXCL_DEPTH * FXCL_DEPTH];
    fxc_cluster_resample(&k->c, FXCL_DEPTH, tmpRgba, dep01);
    for (int i = 0; i < FXCL_DEPTH * FXCL_DEPTH; i++) {
        const float d = dep01[i];
        k->depthTex[i * 4 + 0] = (unsigned char)(d > 1.0f ? 255.0f : d * 255.0f + 0.5f);
        k->depthTex[i * 4 + 1] = 0;
        k->depthTex[i * 4 + 2] = 0;
        k->depthTex[i * 4 + 3] = 255;
    }
    portTexTouch(k->colorId);
    portTexTouch(k->depthId);
}

/* ---- the hit surface: the room's display lists --------------------------------------------- */

typedef struct SurfaceWalk {
    Cluster *k;
    int room;
    float scale, rp[3];
    const void *tricmd;   /* the hit triangle's command: the texture in force there is the wall's */
    uintptr_t timg;       /* the last G_SETTIMG word before it */
    int found;
} SurfaceWalk;

/* One triangle (room-local coordinates): rasterised into the mask when it lies on the cluster's plane. */
static void surfaceTri(SurfaceWalk *w, const Vertex *a, const Vertex *b, const Vertex *c)
{
    Cluster *k = w->k;
    float p[3][3];
    const Vertex *v[3] = { a, b, c };
    for (int i = 0; i < 3; i++) {
        p[i][0] = (v[i]->coord.x + w->rp[0]) / w->scale;
        p[i][1] = (v[i]->coord.y + w->rp[1]) / w->scale;
        p[i][2] = (v[i]->coord.z + w->rp[2]) / w->scale;
    }
    const float e1[3] = { p[1][0] - p[0][0], p[1][1] - p[0][1], p[1][2] - p[0][2] };
    const float e2[3] = { p[2][0] - p[0][0], p[2][1] - p[0][1], p[2][2] - p[0][2] };
    float n[3] = { e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0] };
    const float nl = __builtin_sqrtf(dot3(n, n));
    if (nl < 1e-6f) return;
    if (__builtin_fabsf(dot3(n, k->N)) / nl < 0.97f) return;   /* another orientation */
    float q[3][2];
    for (int i = 0; i < 3; i++) {
        const float d[3] = { p[i][0] - k->origin[0], p[i][1] - k->origin[1], p[i][2] - k->origin[2] };
        if (__builtin_fabsf(dot3(d, k->N)) > 1.0f + k->size * 0.03f) return;   /* another plane */
        q[i][0] = (dot3(d, k->T) - k->c.x0) / k->size * FXC_N;   /* mask texels */
        q[i][1] = (dot3(d, k->B) - k->c.y0) / k->size * FXC_N;
    }
    float x0 = q[0][0], x1 = q[0][0], y0 = q[0][1], y1 = q[0][1];
    for (int i = 1; i < 3; i++) {
        x0 = q[i][0] < x0 ? q[i][0] : x0; x1 = q[i][0] > x1 ? q[i][0] : x1;
        y0 = q[i][1] < y0 ? q[i][1] : y0; y1 = q[i][1] > y1 ? q[i][1] : y1;
    }
    if (x1 < 0 || y1 < 0 || x0 > FXC_N || y0 > FXC_N) return;
    const float area = (q[1][0] - q[0][0]) * (q[2][1] - q[0][1]) - (q[2][0] - q[0][0]) * (q[1][1] - q[0][1]);
    if (__builtin_fabsf(area) < 1e-6f) return;
    const int ix0 = x0 < 0 ? 0 : (int)x0, ix1 = x1 >= FXC_N ? FXC_N - 1 : (int)x1;
    const int iy0 = y0 < 0 ? 0 : (int)y0, iy1 = y1 >= FXC_N ? FXC_N - 1 : (int)y1;
    const float eps = 0.75f * __builtin_fabsf(area) / (FXC_N * 4.0f) + 1e-3f;   /* a little conservative: no cracks */
    for (int y = iy0; y <= iy1; y++) {
        for (int x = ix0; x <= ix1; x++) {
            const float px = x + 0.5f, py = y + 0.5f;
            float s0 = (q[1][0] - q[0][0]) * (py - q[0][1]) - (q[1][1] - q[0][1]) * (px - q[0][0]);
            float s1 = (q[2][0] - q[1][0]) * (py - q[1][1]) - (q[2][1] - q[1][1]) * (px - q[1][0]);
            float s2 = (q[0][0] - q[2][0]) * (py - q[2][1]) - (q[0][1] - q[2][1]) * (px - q[2][0]);
            if (area < 0) { s0 = -s0; s1 = -s1; s2 = -s2; }
            if (s0 >= -eps && s1 >= -eps && s2 >= -eps) k->mask[y * FXC_N + x] = 1;
        }
    }
}

/* Walks one room display list (the converted 16-byte PC Gfx, as bg.c's hit test reads it). */
static void surfaceWalkDl(SurfaceWalk *w, const Gfx *gdl)
{
    if (gdl == NULL) return;
    const Vertex *vtxbase = NULL;
    int vtxoff = 0;
    uintptr_t timg = 0;
    for (int guard = 0; guard < 65536; guard++, gdl++) {
        if ((const void *)gdl == w->tricmd && !w->found) {
            w->timg = timg;
            w->found = 1;
        }
        const u8 op = (u8)gdl->dma.cmd;
        if (op == (u8)G_ENDDL) return;
        if (op == (u8)G_SETTIMG) {
            timg = gdl->words.w1;
        } else if (op == (u8)G_VTX) {
            vtxoff = ((u32)gdl->words.w0 >> 16) & 0xf;
            vtxbase = (const Vertex *)((uintptr_t)g_BgRoomInfo[w->room].vertices + ((u32)gdl->words.w1 & 0x00ffffff));
        } else if (op == (u8)G_TRI1 && vtxbase != NULL && w->k != NULL) {
            const u32 g1 = (u32)gdl->words.w1;
            const int i0 = (int)((g1 >> 16) & 0xff) / 10 - vtxoff, i1 = (int)((g1 >> 8) & 0xff) / 10 - vtxoff,
                      i2 = (int)(g1 & 0xff) / 10 - vtxoff;
            if (i0 >= 0 && i1 >= 0 && i2 >= 0) surfaceTri(w, &vtxbase[i0], &vtxbase[i1], &vtxbase[i2]);
        } else if (op == (u8)G_TRI4 && vtxbase != NULL && w->k != NULL) {
            const u32 g0 = (u32)gdl->words.w0, g1 = (u32)gdl->words.w1;
            for (int s = 0; s < 4; s++) {   /* the nibbles as bg.c decodes them */
                const int i0 = (int)((g1 >> (8 * s)) & 0xf) - vtxoff, i1 = (int)((g1 >> (8 * s + 4)) & 0xf) - vtxoff,
                          i2 = (int)((g0 >> (4 * s)) & 0xf) - vtxoff;
                if (i0 == i1 && i1 == i2) continue;   /* an unused slot */
                if (i0 >= 0 && i1 >= 0 && i2 >= 0) surfaceTri(w, &vtxbase[i0], &vtxbase[i1], &vtxbase[i2]);
            }
        }
    }
}

/* The cluster's mask from the room's triangles on its plane (k NULL: only the texture), and the hit
 * triangle's texture word. Returns the G_SETTIMG word in force at `tricmd`, 0 when unknown. */
static uintptr_t surfaceScan(Cluster *k, int room, const void *tricmd)
{
    SurfaceWalk w;
    __builtin_memset(&w, 0, sizeof w);
    w.k = k;
    w.room = room;
    w.tricmd = tricmd;
    w.scale = get_room_data_float1();
    if (w.scale <= 0.0f) w.scale = 1.0f;
    if (room <= 0 || room >= g_MaxNumRooms || g_BgRoomInfo[room].vertices == NULL) return 0;
    const struct coord3d *rp = getRoomPositionByIndex(room);
    w.rp[0] = rp->x; w.rp[1] = rp->y; w.rp[2] = rp->z;
    surfaceWalkDl(&w, (const Gfx *)g_BgRoomInfo[room].ptr_expanded_mapping_info);
    surfaceWalkDl(&w, (const Gfx *)g_BgRoomInfo[room].ptr_secondary_expanded_mapping_info);
    return w.found ? w.timg : 0;
}

/* Clears what lies off the surface (after each stamp: a hole near an edge stops there). */
static void applyMask(Cluster *k)
{
    for (int i = 0; i < FXC_N * FXC_N; i++) {
        if (k->mask[i]) continue;
        k->c.depth[i] = 0.0f;
        __builtin_memset(&k->c.rgba[i * 4], 0, 4);
    }
}

/* ---- the game's glass holes ---------------------------------------------------------------- */

#define FXCL_TYPES 20
static int s_gameImg[FXCL_TYPES];        /* "game/impactimages/<t>" ids, 0 = not asked yet (id + 1) */
static unsigned int s_glassVer[FXCL_TYPES];
static FxcTemplate s_glassTpl[FXCL_TYPES];
static int s_glassReady[FXCL_TYPES];

/* The glass template of impact type t from the game's own image, once fast3d decoded it; NULL before. */
static const FxcTemplate *glassTemplate(int t)
{
    if (s_gameImg[t] == 0) {   /* ask fast3d for it (fxClustersDraw emits the capture) */
        char name[PORTTEX_NAME_MAX];
        static const unsigned char none[4] = { 0, 0, 0, 0 };
        snprintf(name, sizeof name, "game/impactimages/%d", t);
        s_gameImg[t] = portTexRegister(name, 1, 1, none, PORTTEX_COPY) + 1;
        return NULL;
    }
    const PortTex *p = portTexGet(s_gameImg[t] - 1);
    if (p == NULL || p->w <= 1) return NULL;
    if (!s_glassReady[t] || s_glassVer[t] != p->version) {
        fxc_template_glass_from(&s_glassTpl[t], p->rgba, p->w, p->h);
        s_glassVer[t] = p->version;
        s_glassReady[t] = 1;
    }
    return &s_glassTpl[t];
}

/* Impacts waiting for their template (glass before fast3d decoded the game's image). */
#define FXCL_PENDING 16
static PortEvImpact s_pending[FXCL_PENDING];
static int s_numPending;

static Cluster *newCluster(const float pos[3], const float n[3], float size, int room, int mat)
{
    Cluster *k = NULL;
    for (int i = 0; i < FXCL_MAX && k == NULL; i++) {
        if (!s_cl[i].used) k = &s_cl[i];
    }
    if (k == NULL) {   /* reuse the oldest */
        k = &s_cl[0];
        for (int i = 1; i < FXCL_MAX; i++) {
            if (s_cl[i].age < k->age) k = &s_cl[i];
        }
    }
    const int idx = (int)(k - s_cl);
    k->used = 1;
    k->age = ++s_age;
    k->size = size;
    k->room = room;
    k->mat = mat;
    for (int j = 0; j < 3; j++) {
        k->N[j] = n[j];
        k->origin[j] = pos[j] + n[j] * 0.4f;   /* a little off the surface */
    }
    /* the quad's axes: never turned (the templates' painted light keeps one direction) */
    const int vert = n[1] > 0.9f || n[1] < -0.9f;
    const float ref[3] = { 0.0f, vert ? 0.0f : 1.0f, vert ? 1.0f : 0.0f };
    float t[3] = { ref[1] * n[2] - ref[2] * n[1], ref[2] * n[0] - ref[0] * n[2], ref[0] * n[1] - ref[1] * n[0] };
    const float tl = __builtin_sqrtf(dot3(t, t));
    for (int j = 0; j < 3; j++) t[j] /= tl > 1e-6f ? tl : 1.0f;
    for (int j = 0; j < 3; j++) k->T[j] = t[j];
    k->B[0] = n[1] * t[2] - n[2] * t[1];
    k->B[1] = n[2] * t[0] - n[0] * t[2];
    k->B[2] = n[0] * t[1] - n[1] * t[0];
    fxc_cluster_clear(&k->c, -size * 0.5f, -size * 0.5f, size);
    __builtin_memset(k->depthTex, 0, sizeof k->depthTex);
    __builtin_memset(k->mask, 1, sizeof k->mask);   /* the whole square, until the surface is known */
    char name[PORTTEX_NAME_MAX];
    snprintf(name, sizeof name, "impact/cluster/%d/color", idx);
    k->colorId = portTexRegister(name, FXC_N, FXC_N, k->c.rgba, PORTTEX_EXTERNAL);
    snprintf(name, sizeof name, "impact/cluster/%d/depth", idx);
    k->depthId = portTexRegister(name, FXCL_DEPTH, FXCL_DEPTH, k->depthTex, PORTTEX_EXTERNAL);
    return k;
}

/* ---- the props' holes, drawn in the game's place (GE_FX_PROP_IMPACT) ---------------------------- */

typedef struct PropHole {
    void *prop;
    int type, mat, var;
    float base[3];
    int grp;                 /* its group (s_grp), -1: none yet */
    unsigned int grpGen;     /* that group's generation when it joined */
} PropHole;
static PropHole s_propHole[BULLET_IMPACT_BUFFER_LEN];

/* The texture in force at a model's triangle command: the last G_SETTIMG before it, 0 if not found. */
static uintptr_t modelTexImage(const void *tricmd)
{
    const Gfx *g = (const Gfx *)tricmd;
    for (int n = 0; n < 96; n++) {
        g--;
        const u8 op = (u8)g->dma.cmd;
        if (op == (u8)G_SETTIMG) return g->words.w1;
        if (op == (u8)G_ENDDL || op == (u8)G_DL) return 0;
    }
    return 0;
}

static void propImpact(const PortEvImpact *e)
{
    if (e->slot < 0 || e->slot >= BULLET_IMPACT_BUFFER_LEN) return;
    PropHole *ph = &s_propHole[e->slot];
    ph->prop = NULL;
    ph->grp = -1;
    int m = matOf(e->material);
    if (kMat[m].gen == FXC_GLASS) {   /* windows: the game's glass image, its middle empty (built when drawn) */
        ph->prop = e->prop;
        ph->type = e->impact_type;
        ph->mat = m;
        ph->var = e->impact_type;   /* the glass template is per impact type */
        ph->base[0] = ph->base[1] = ph->base[2] = 1.0f;
        glassTemplate(e->impact_type);   /* asks fast3d for the image */
        return;
    }
    if (kMat[m].gen == FXC_WOOD || kMat[m].gen == FXC_STONE) m = 3;   /* crates and the like: the dirt hole */
    float base[3] = { kMat[m].base[0], kMat[m].base[1], kMat[m].base[2] };
    float avg[3];
    const uintptr_t timg = e->tricmd != NULL ? modelTexImage(e->tricmd) : 0;
    if (timg != 0 && gfx_texture_average(timg, avg)) {   /* the prop's texture, 15 % lighter */
        for (int j = 0; j < 3; j++) base[j] = avg[j] * 1.15f > 1.0f ? 1.0f : avg[j] * 1.15f;
    }
    ph->prop = e->prop;
    ph->type = e->impact_type;
    ph->mat = m;
    ph->var = (int)(rnd01() * FXCL_VARS) % FXCL_VARS;
    for (int j = 0; j < 3; j++) ph->base[j] = base[j];
}

/* The render state of a port hole drawn in the game's prop loop (the game's matrix stays), its textures bound. */
static Gfx *propDrawBegin(Gfx *gdl, int colorId, int depthId)
{
    gDPPipeSync(gdl++);
    gDPSetCycleType(gdl++, G_CYC_1CYCLE);
    gSPTexture(gdl++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
    gDPSetTextureFilter(gdl++, G_TF_BILERP);
    gDPSetTextureLUT(gdl++, G_TT_NONE);
    gDPSetTexturePersp(gdl++, G_TP_PERSP);
    gDPSetCombineLERP(gdl++, TEXEL1, 0, TEXEL0, SHADE, TEXEL1, 0, TEXEL0, SHADE,
                      TEXEL1, 0, TEXEL0, SHADE, TEXEL1, 0, TEXEL0, SHADE);
    gDPSetRenderMode(gdl++, G_RM_ZB_XLU_DECAL, G_RM_ZB_XLU_DECAL2);
    gDPSetAlphaCompare(gdl++, G_AC_NONE);
    gDPSetAlphaDither(gdl++, G_AD_DISABLE);
    gSPClearGeometryMode(gdl++, G_LIGHTING | G_TEXTURE_GEN);
    gSPSetGeometryMode(gdl++, G_SHADE | G_SHADING_SMOOTH | G_ZBUFFER);
    gDPSetTile(gdl++, G_IM_FMT_RGBA, G_IM_SIZ_32b, FXC_N / 4, 0, G_TX_RENDERTILE, 0, G_TX_CLAMP, 0, 0, G_TX_CLAMP, 0, 0);
    gDPSetTileSize(gdl++, G_TX_RENDERTILE, 0, 0, (FXC_N - 1) << G_TEXTURE_IMAGE_FRAC, (FXC_N - 1) << G_TEXTURE_IMAGE_FRAC);
    gDPSetTile(gdl++, G_IM_FMT_RGBA, G_IM_SIZ_32b, FXC_N / 4, 0, G_TX_RENDERTILE + 1, 0, G_TX_CLAMP, 0, 0, G_TX_CLAMP, 0, 0);
    gDPSetTileSize(gdl++, G_TX_RENDERTILE + 1, 0, 0, (FXC_N - 1) << G_TEXTURE_IMAGE_FRAC, (FXC_N - 1) << G_TEXTURE_IMAGE_FRAC);
    gDPPortFxBegin(gdl++, PORTFX_CLUSTER);
    gDPPortTexture(gdl++, 0, colorId);
    gDPPortTexture(gdl++, 1, depthId);
    return gdl;
}

static Gfx *propDrawEnd(Gfx *gdl)
{
    gDPPortFxEnd(gdl++);
    gDPPortTexture(gdl++, 0, -1);
    gDPPortTexture(gdl++, 1, -1);
    gDPPipeSync(gdl++);
    gDPPortFxBegin(gdl++, PORTFX_IMPACT);   /* the game's loop goes on inside its marker */
    return gdl;
}

/* ---- the props' holes fused: one cluster per group of nearby holes on one plane ------------------
 * The game draws each hole of a prop on its own, so overlapping holes covered each other: one hole's glass cracks
 * over another's empty middle, a metal hole's dent and soft edge over another's pit (stacked grey discs). The holes
 * of a prop that lie on one plane (one model part) and near each other share a cluster here, in the part's model
 * space (the game's matrix), fused as the level's are ("the deepest wins", fxcluster.h). It is drawn once, in place
 * of the first of its holes the game's loop meets, and the others draw nothing. Glass never shares a group with
 * the other materials (it is filtered as the game's image and drawn unlit). Rebuilt when its holes change. */
#define FXCL_PROPGRP 16
#define FXCL_GRP_REACH 5.0f   /* a group's holes lie within this many radii of its first hole (a level cluster: 10 across) */
typedef struct PropGroup {
    int used, glass;
    unsigned int gen;            /* bumped when the slot is reused: its old holes look for a group again */
    void *prop;
    int rpi;                     /* model_render_pos_index */
    float N[3], d;               /* the plane, model space */
    float T[3], B[3], o[3];      /* the cluster's axes and centre */
    float a[3], reach;           /* its first hole's centre, and how far from it (in T and B) a hole may reach */
    unsigned int members;        /* a hash of its holes */
    unsigned int drawnAt, age;
    float depthUV, light;
    FxcCluster c;
    unsigned char depthTex[FXCL_DEPTH * FXCL_DEPTH * 4];
    int colorId, depthId;
} PropGroup;
static PropGroup s_grp[FXCL_PROPGRP];
static unsigned int s_drawSerial;   /* one per fxClustersDraw (each view of each frame) */

/* A hole's quad in model space (explosion.c: v0 = c-U-V tc(0,h), v1 = c-U+V tc(0,0), v2 = c+U+V, v3 = c+U-V);
 * n = U x V. */
static void holeFrame(const struct BulletImpact *b, float c[3], float u[3], float v[3], float n[3], float *hw, float *hh)
{
    const Vtx *q = b->vertex_list;
    for (int j = 0; j < 3; j++) {
        c[j] = (q[0].v.ob[j] + q[1].v.ob[j] + q[2].v.ob[j] + q[3].v.ob[j]) * 0.25f;
        u[j] = (q[3].v.ob[j] - q[0].v.ob[j]) * 0.5f;   /* tc u grows along it */
        v[j] = (q[0].v.ob[j] - q[1].v.ob[j]) * 0.5f;   /* tc v grows along it */
    }
    *hw = __builtin_sqrtf(dot3(u, u));
    *hh = __builtin_sqrtf(dot3(v, v));
    for (int j = 0; j < 3; j++) {
        u[j] /= *hw > 1e-6f ? *hw : 1.0f;
        v[j] /= *hh > 1e-6f ? *hh : 1.0f;
    }
    n[0] = u[1] * v[2] - u[2] * v[1]; n[1] = u[2] * v[0] - u[0] * v[2]; n[2] = u[0] * v[1] - u[1] * v[0];
}

/* A hole the game still draws, as we recorded it. */
static int propHoleOk(int s)
{
    const PropHole *h = &s_propHole[s];
    const struct BulletImpact *b = &g_BulletImpactBuffer[s];
    return h->prop != NULL && h->prop == (void *)b->prop && h->type == b->impact_type && b->room >= 0;
}

static int isGlass(int s) { return kMat[s_propHole[s].mat].gen == FXC_GLASS; }

static int grpValid(const PropHole *h)
{
    return h->grp >= 0 && s_grp[h->grp].used && s_grp[h->grp].gen == h->grpGen;
}

static int inGroup(const PropGroup *g, int s)
{
    return propHoleOk(s) && s_propHole[s].grp == (int)(g - s_grp) && s_propHole[s].grpGen == g->gen;
}

/* Whether hole s may join g: same prop, part, kind, plane, and it fits whole within the group's reach. */
static int fitsGroup(const PropGroup *g, int s)
{
    const struct BulletImpact *b = &g_BulletImpactBuffer[s];
    if (b->prop != g->prop || b->model_render_pos_index != g->rpi || isGlass(s) != g->glass) return 0;
    float c[3], u[3], v[3], n[3], hw, hh;
    holeFrame(b, c, u, v, n, &hw, &hh);
    if (__builtin_fabsf(dot3(n, g->N)) <= 0.95f || __builtin_fabsf(dot3(c, g->N) - g->d) >= 2.0f) return 0;
    const float d[3] = { c[0] - g->a[0], c[1] - g->a[1], c[2] - g->a[2] }, r = hw > hh ? hw : hh;
    return __builtin_fabsf(dot3(d, g->T)) + r <= g->reach && __builtin_fabsf(dot3(d, g->B)) + r <= g->reach;
}

/* A separable box filter of width w texels over an RGBA FXC_N x FXC_N image, on premultiplied alpha. */
static void boxFilter(unsigned char *rgba, int w)
{
    if (w <= 1) return;
    if (w > 15) w = 15;
    static float a[FXC_N * FXC_N * 4], b[FXC_N * FXC_N * 4];
    for (int i = 0; i < FXC_N * FXC_N; i++) {
        const float al = rgba[i * 4 + 3] / 255.0f;
        for (int c = 0; c < 3; c++) a[i * 4 + c] = rgba[i * 4 + c] * al;
        a[i * 4 + 3] = rgba[i * 4 + 3];
    }
    const int lo = -(w - 1) / 2, hi = w / 2;
    for (int pass = 0; pass < 2; pass++) {
        const float *in = pass == 0 ? a : b;
        float *out = pass == 0 ? b : a;
        for (int y = 0; y < FXC_N; y++) {
            for (int x = 0; x < FXC_N; x++) {
                float sum[4] = { 0, 0, 0, 0 };
                for (int k = lo; k <= hi; k++) {
                    int sx = pass == 0 ? x + k : x, sy = pass == 0 ? y : y + k;
                    sx = sx < 0 ? 0 : sx >= FXC_N ? FXC_N - 1 : sx;
                    sy = sy < 0 ? 0 : sy >= FXC_N ? FXC_N - 1 : sy;
                    for (int c = 0; c < 4; c++) sum[c] += in[(sy * FXC_N + sx) * 4 + c];
                }
                for (int c = 0; c < 4; c++) out[(y * FXC_N + x) * 4 + c] = sum[c] / (float)w;
            }
        }
    }
    for (int i = 0; i < FXC_N * FXC_N; i++) {
        const float al = a[i * 4 + 3];
        for (int c = 0; c < 3; c++) {
            const float v = al > 0.5f ? a[i * 4 + c] / (al / 255.0f) : 0.0f;
            rgba[i * 4 + c] = (unsigned char)(v > 255.0f ? 255.0f : v + 0.5f);
        }
        rgba[i * 4 + 3] = (unsigned char)(al > 255.0f ? 255.0f : al + 0.5f);
    }
}

/* Hole s's template, NULL while a glass image is not decoded yet. */
static const FxcTemplate *holeTemplate(int s)
{
    const PropHole *h = &s_propHole[s];
    return isGlass(s) ? glassTemplate(h->type) : templateFor(h->mat, h->var);
}

/* The template with its coverage scaled by the game's fade (explosionSetBulletImpactAlpha fades the oldest
 * holes out through the quad's vertex alpha, which a fused cluster cannot use). */
static const FxcTemplate *fadedTemplate(const FxcTemplate *t, int alpha)
{
    if (alpha >= 255) return t;
    static FxcTemplate f;
    f = *t;
    for (int i = 0; i < FXC_TPL * FXC_TPL; i++) f.rgba[i * 4 + 3] = (unsigned char)(t->rgba[i * 4 + 3] * alpha / 255);
    return &f;
}

/* Stamps every hole of the group again; returns 0 when a template is not ready (a glass image not decoded). */
static int groupBuild(PropGroup *g)
{
    float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
    int n = 0;
    for (int s = 0; s < BULLET_IMPACT_BUFFER_LEN; s++) {
        if (!inGroup(g, s)) continue;
        if (holeTemplate(s) == NULL) return 0;
        float c[3], u[3], v[3], nn[3], hw, hh;
        holeFrame(&g_BulletImpactBuffer[s], c, u, v, nn, &hw, &hh);
        const float d[3] = { c[0] - g->o[0], c[1] - g->o[1], c[2] - g->o[2] };
        const float x = dot3(d, g->T), y = dot3(d, g->B), r = hw > hh ? hw : hh;
        x0 = x - r < x0 ? x - r : x0; x1 = x + r > x1 ? x + r : x1;
        y0 = y - r < y0 ? y - r : y0; y1 = y + r > y1 ? y + r : y1;
        n++;
    }
    if (n == 0) return 0;
    const float size = (x1 - x0 > y1 - y0 ? x1 - x0 : y1 - y0) * 1.02f;
    float texelSum = 0.0f;   /* glass: the game image's texel, in cluster texels (for the filter after the fusion) */
    const float cx = 0.5f * (x0 + x1), cy = 0.5f * (y0 + y1);
    for (int j = 0; j < 3; j++) g->o[j] += g->T[j] * cx + g->B[j] * cy;   /* centre the square on the holes */
    fxc_cluster_clear(&g->c, -size * 0.5f, -size * 0.5f, size);
    g->light = 0.0f;
    for (int s = 0; s < BULLET_IMPACT_BUFFER_LEN; s++) {
        if (!inGroup(g, s)) continue;
        const PropHole *ph = &s_propHole[s];
        const struct BulletImpact *b = &g_BulletImpactBuffer[s];
        float c[3], u[3], v[3], nn[3], hw, hh;
        holeFrame(b, c, u, v, nn, &hw, &hh);
        const float d[3] = { c[0] - g->o[0], c[1] - g->o[1], c[2] - g->o[2] };
        FxcHole h;
        h.x = dot3(d, g->T); h.y = dot3(d, g->B); h.hw = hw; h.hh = hh;
        h.mat = kMat[ph->mat].gen; h.var = ph->var;
        for (int j = 0; j < 3; j++) h.base[j] = ph->base[j];
        fxc_stamp(&g->c, fadedTemplate(holeTemplate(s), b->vertex_list[0].v.cn[3]), &h);
        if (kMat[ph->mat].light > g->light) g->light = kMat[ph->mat].light;
        if (g->glass) {
            const PortTex *src = portTexGet(s_gameImg[ph->type] - 1);
            if (src != NULL && src->w > 0) texelSum += 2.0f * hw / src->w / size * FXC_N;
        }
    }
    /* glass: the fusion is texel-exact (the game's texels, nearest); now filter it as the game filters its image:
     * a box one game texel wide, on premultiplied colour (empty texels do not darken the cracks' edges) */
    if (g->glass) boxFilter(g->c.rgba, (int)(texelSum / n + 0.5f));
    static unsigned char tmpRgba[FXCL_DEPTH * FXCL_DEPTH * 4];
    static float dep01[FXCL_DEPTH * FXCL_DEPTH];
    fxc_cluster_resample(&g->c, FXCL_DEPTH, tmpRgba, dep01);
    for (int i = 0; i < FXCL_DEPTH * FXCL_DEPTH; i++) {
        const float dd = dep01[i];
        g->depthTex[i * 4 + 0] = (unsigned char)(dd > 1.0f ? 255.0f : dd * 255.0f + 0.5f);
        g->depthTex[i * 4 + 1] = 0; g->depthTex[i * 4 + 2] = 0; g->depthTex[i * 4 + 3] = 255;
    }
    g->depthUV = g->c.depthMax / g->c.size;
    portTexTouch(g->colorId);
    portTexTouch(g->depthId);
    return 1;
}

static s32 groupDraw(Gfx **gdlp, int slot)
{
    PropHole *ph = &s_propHole[slot];
    const struct BulletImpact *b = &g_BulletImpactBuffer[slot];
    float c[3], u[3], v[3], nn[3], hw, hh;
    holeFrame(b, c, u, v, nn, &hw, &hh);
    if (hw < 1e-3f || hh < 1e-3f) return 0;

    PropGroup *g = grpValid(ph) ? &s_grp[ph->grp] : NULL;
    for (int i = 0; i < FXCL_PROPGRP && g == NULL; i++) {
        if (s_grp[i].used && fitsGroup(&s_grp[i], slot)) g = &s_grp[i];
    }
    if (g == NULL) {   /* a new group around this hole: the oldest goes */
        g = &s_grp[0];
        for (int i = 0; i < FXCL_PROPGRP; i++) {
            if (!s_grp[i].used) { g = &s_grp[i]; break; }
            if (s_grp[i].age < g->age) g = &s_grp[i];
        }
        const int idx = (int)(g - s_grp);
        g->used = 1;
        g->gen++;
        g->glass = isGlass(slot);
        g->prop = b->prop;
        g->rpi = b->model_render_pos_index;
        for (int j = 0; j < 3; j++) { g->N[j] = nn[j]; g->T[j] = u[j]; g->B[j] = v[j]; g->o[j] = c[j]; g->a[j] = c[j]; }
        g->d = dot3(c, nn);
        g->reach = (hw > hh ? hw : hh) * FXCL_GRP_REACH;
        g->members = 0;
        g->drawnAt = 0;
        char name[PORTTEX_NAME_MAX];
        snprintf(name, sizeof name, "impact/propgroup/%d/color", idx);
        g->colorId = portTexRegister(name, FXC_N, FXC_N, g->c.rgba, PORTTEX_EXTERNAL);
        snprintf(name, sizeof name, "impact/propgroup/%d/depth", idx);
        g->depthId = portTexRegister(name, FXCL_DEPTH, FXCL_DEPTH, g->depthTex, PORTTEX_EXTERNAL);
    }
    ph->grp = (int)(g - s_grp);
    ph->grpGen = g->gen;
    g->age = ++s_age;
    if (g->drawnAt == s_drawSerial) return 1;   /* drawn already with the first of its holes */

    /* the holes without a group yet that fit this one join it now, so they are in it from their first frame */
    unsigned int h = 2166136261u;   /* its holes now */
    for (int s = 0; s < BULLET_IMPACT_BUFFER_LEN; s++) {
        if (!propHoleOk(s)) continue;
        PropHole *o = &s_propHole[s];
        if (!grpValid(o) && fitsGroup(g, s)) { o->grp = (int)(g - s_grp); o->grpGen = g->gen; }
        if (!inGroup(g, s)) continue;
        const struct BulletImpact *bs = &g_BulletImpactBuffer[s];
        h = (h ^ (unsigned int)(s * 131 + bs->impact_type)) * 16777619u;
        h = (h ^ bs->vertex_list[0].v.cn[3]) * 16777619u;   /* the game's fade */
    }
    if (h != g->members) {
        if (!groupBuild(g)) return 0;   /* the game draws it until the glass image is decoded */
        g->members = h;
    }
    g->drawnAt = s_drawSerial;

    Gfx *gdl = propDrawBegin(*gdlp, g->colorId, g->depthId);
    Vtx *vt = dynAllocateVertices(4);
    static const float cs[4][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
    const float half = g->c.size * 0.5f;
    /* the vertex colour: r the relief's depth in uv units x 4, g how much it is lit (0 for glass: flat, unlit) */
    const unsigned char vr = (unsigned char)(g->depthUV * 4.0f > 1.0f ? 255.0f : g->depthUV * 4.0f * 255.0f + 0.5f);
    const unsigned char vg = (unsigned char)(g->light * 255.0f + 0.5f);
    for (int q = 0; q < 4; q++) {
        for (int j = 0; j < 3; j++) {
            const float w = g->o[j] + (g->T[j] * cs[q][0] + g->B[j] * cs[q][1]) * half;
            vt[q].v.ob[j] = (s16)(w < -32000.0f ? -32000.0f : w > 32000.0f ? 32000.0f : w);
        }
        vt[q].v.flag = 0;
        vt[q].v.tc[0] = (s16)(cs[q][0] > 0 ? FXC_N << 5 : 0);
        vt[q].v.tc[1] = (s16)(cs[q][1] > 0 ? FXC_N << 5 : 0);
        vt[q].v.cn[0] = vr; vt[q].v.cn[1] = vg; vt[q].v.cn[2] = 255; vt[q].v.cn[3] = 255;   /* the fade is in the texture */
    }
    gSPVertex(gdl++, osVirtualToPhysical(vt), 4, 0);
    gSP2Triangles(gdl++, 0, 1, 2, 0, 0, 2, 3, 0);
    *gdlp = propDrawEnd(gdl);
    return 1;
}

s32 portFxPropImpact(Gfx **gdlp, s32 slot)
{
    if (slot < 0 || slot >= BULLET_IMPACT_BUFFER_LEN || !portFxUserOn) return 0;
    if (!propHoleOk(slot)) return 0;
    return groupDraw(gdlp, slot);
}

/* The texture number of the pool texture whose pixels a G_SETTIMG word names, -1 if none: on PC the level's hit
 * test cannot read it (D154), so the level's material comes from here. */
static int texnumOfImage(uintptr_t w1)
{
    if (w1 == 0) return -1;
    for (struct tex *t = ptr_texture_alloc_start.rightpos; t != NULL && t < ptr_texture_alloc_start.end; t++) {
        if ((uintptr_t)t->data == w1 || (u32)osVirtualToPhysical(t->data) == (u32)w1) return t->texturenum;
    }
    return -1;
}

void fxClustersAddImpact(const PortEvImpact *e0)
{
    PortEvImpact ev = *e0;
    const PortEvImpact *e = &ev;
    if (!s_ready) {
        registerBuiltIn();
        s_ready = 1;
    }
    if (e->material == PMAT_WATER || e->material == PMAT_CHR) return;
    if (e->prop != NULL) {
        if (e->impact_type >= 0 && e->impact_type < FXCL_TYPES) propImpact(e);
        return;
    }
    if (e->impact_type < 0 || e->impact_type > 19) return;
    if (e->tricmd != NULL) {   /* the level's material, from the hit texture */
        const uintptr_t timg = surfaceScan(NULL, e->room, e->tricmd);
        const int tn = texnumOfImage(timg);
        static int logOn = -1;
        if (logOn < 0) logOn = getenv("GE_FXCLLOG") != NULL;
        if (logOn) {
            sysLogPrintf(LOG_NOTE, "FXCL level hit: room %d timg %llx texnum %d hitTexture %d type %d", e->room,
                         (unsigned long long)timg, tn, tn >= 0 && tn < (int)NUM_TEXTURES ? (int)g_Textures[tn].hitTexture : -1,
                         e->impact_type);
        }
        if (tn >= 0 && tn < NUM_TEXTURES) {
            ev.material = (int)g_Textures[tn].hitTexture;
            const struct image_sound *snd = g_HitTypeSounds[ev.material];
            int listed = 0;   /* a hole of that material (the game picked one from the default list) */
            for (int i = 0; snd != NULL && i < snd->thing2_len; i++) listed |= snd->thing2[i] == ev.impact_type;
            if (!listed && snd != NULL && snd->thing2_len > 0) {
                const int t = snd->thing2[(int)(rnd01() * snd->thing2_len) % snd->thing2_len];
                if (t >= 0 && t <= 19) ev.impact_type = t;
            }
            if (ev.material == PMAT_WATER) return;
        }
    }
    const int m = matOf(e->material);
    const FxcTemplate *glassTpl = NULL;
    if (kMat[m].gen == FXC_GLASS) {   /* the game's own glass image: wait for fast3d to decode it */
        glassTpl = glassTemplate(e->impact_type);
        if (glassTpl == NULL) {
            if (s_numPending < FXCL_PENDING) s_pending[s_numPending++] = *e;
            return;
        }
    }
    const float k = 0.85f + 0.3f * rnd01();
    const float hw = g_ImpactTypes[e->impact_type].width * k, hh = g_ImpactTypes[e->impact_type].height * k;
    const float r = hw > hh ? hw : hh;

    /* a cluster on this surface where the hole fits whole, else a new one around it */
    Cluster *cl = NULL;
    float lx = 0.0f, ly = 0.0f;
    for (int i = 0; i < FXCL_MAX && cl == NULL; i++) {
        Cluster *c = &s_cl[i];
        if (!c->used || dot3(c->N, e->normal) < 0.95f) continue;
        const float d[3] = { e->pos[0] - c->origin[0], e->pos[1] - c->origin[1], e->pos[2] - c->origin[2] };
        if (__builtin_fabsf(dot3(d, c->N) + 0.4f) > 2.0f) continue;   /* another plane */
        const float x = dot3(d, c->T), y = dot3(d, c->B), half = c->size * 0.5f;
        if (__builtin_fabsf(x) + hw > half || __builtin_fabsf(y) + hh > half) continue;
        cl = c; lx = x; ly = y;
    }
    const int fresh = cl == NULL;
    if (cl == NULL) cl = newCluster(e->pos, e->normal, r * 10.0f, e->room, m);
    cl->age = ++s_age;

    /* the surface: the mask of a new cluster, and the hit texture */
    uintptr_t timg = 0;
    if (e->tricmd != NULL) {
        if (fresh) __builtin_memset(cl->mask, 0, sizeof cl->mask);
        timg = surfaceScan(fresh ? cl : NULL, e->room, e->tricmd);
        if (fresh) {   /* the hit point itself is on the surface, whatever the scan found */
            int any = 0;
            for (int i = 0; i < FXC_N * FXC_N && !any; i++) any = cl->mask[i];
            if (!any) __builtin_memset(cl->mask, 1, sizeof cl->mask);
        }
    }

    const int var = (int)(rnd01() * FXCL_VARS) % FXCL_VARS;
    const FxcTemplate *t = glassTpl != NULL ? glassTpl : templateFor(m, var);
    if (t == NULL) return;
    FxcHole h;
    h.x = lx; h.y = ly; h.hw = hw; h.hh = hh; h.mat = kMat[m].gen; h.var = var;
    for (int j = 0; j < 3; j++) h.base[j] = kMat[m].base[j];
    float avg[3];
    if (!t->absolute && timg != 0 && gfx_texture_average(timg, avg)) {
        /* the wall's colour: its texture's average x its baked light, 15 % lighter (as fxview) */
        for (int j = 0; j < 3; j++) {
            const float lit = avg[j] * (e->shade[j] >= 0.0f ? e->shade[j] : 1.0f) * 1.15f;
            h.base[j] = lit > 1.0f ? 1.0f : lit;
        }
    }
    fxc_stamp(&cl->c, t, &h);
    applyMask(cl);
    upload(cl);
}

/* ---- drawing ------------------------------------------------------------------------------- */

Gfx *fxClustersDraw(Gfx *gdl)
{
    s_drawSerial++;   /* the glass panes draw once per view */
    /* the glass holes waiting for the game's image: stamped once fast3d decoded it (the frame after) */
    if (s_numPending > 0) {
        int n = s_numPending;
        PortEvImpact p[FXCL_PENDING];
        __builtin_memcpy(p, s_pending, sizeof p);
        s_numPending = 0;
        for (int i = 0; i < n; i++) {
            const PortTex *t = s_gameImg[p[i].impact_type] ? portTexGet(s_gameImg[p[i].impact_type] - 1) : NULL;
            if (t != NULL && t->w > 1) fxClustersAddImpact(&p[i]);
            else if (s_numPending < FXCL_PENDING) s_pending[s_numPending++] = p[i];
        }
    }
    /* the game images asked for and not decoded yet: load each as the game does, then capture it */
    for (int t = 0; t < FXCL_TYPES; t++) {
        const PortTex *p = s_gameImg[t] ? portTexGet(s_gameImg[t] - 1) : NULL;
        if (p == NULL || p->w > 1 || impactimages == NULL) continue;
        gDPPipeSync(gdl++);
        texSelect(&gdl, &impactimages[t], g_ImpactTypes[t].unk1, g_ImpactTypes[t].unk2, 2U);
        gDPPortTexCapture(gdl++, s_gameImg[t] - 1);
    }

    int any = 0;
    for (int i = 0; i < FXCL_MAX; i++) any |= s_cl[i].used;
    if (!any) return gdl;
    const float scale = get_room_data_float1();
    gDPPipeSync(gdl++);
    gDPSetCycleType(gdl++, G_CYC_1CYCLE);
    gSPTexture(gdl++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
    gDPSetTextureFilter(gdl++, G_TF_BILERP);
    gDPSetTextureLUT(gdl++, G_TT_NONE);
    gDPSetTexturePersp(gdl++, G_TP_PERSP);
    /* fxClusterPom reads both textures itself and the vertex colour from the combiner's first input: the
     * combiner must really use TEXEL0 and TEXEL1 so fast3d binds them (it folds (A - A) away), and SHADE comes
     * first among its other inputs: TEXEL1 * TEXEL0 + SHADE */
    gDPSetCombineLERP(gdl++, TEXEL1, 0, TEXEL0, SHADE, TEXEL1, 0, TEXEL0, SHADE,
                      TEXEL1, 0, TEXEL0, SHADE, TEXEL1, 0, TEXEL0, SHADE);
    gDPSetRenderMode(gdl++, G_RM_ZB_XLU_DECAL, G_RM_ZB_XLU_DECAL2);
    gDPSetAlphaCompare(gdl++, G_AC_NONE);
    gDPSetAlphaDither(gdl++, G_AD_DISABLE);
    gDPSetColorDither(gdl++, G_CD_DISABLE);
    gSPClearGeometryMode(gdl++, G_CULL_BOTH | G_LIGHTING | G_TEXTURE_GEN);
    gSPSetGeometryMode(gdl++, G_SHADE | G_SHADING_SMOOTH | G_ZBUFFER);
    /* tiles 0 and 1: RGBA32, clamped; their size is the port textures' own (G_PORTTEX_EXT) */
    gDPSetTile(gdl++, G_IM_FMT_RGBA, G_IM_SIZ_32b, FXC_N / 4, 0, G_TX_RENDERTILE, 0, G_TX_CLAMP, 0, 0, G_TX_CLAMP, 0, 0);
    gDPSetTileSize(gdl++, G_TX_RENDERTILE, 0, 0, (FXC_N - 1) << G_TEXTURE_IMAGE_FRAC, (FXC_N - 1) << G_TEXTURE_IMAGE_FRAC);
    gDPSetTile(gdl++, G_IM_FMT_RGBA, G_IM_SIZ_32b, FXC_N / 4, 0, G_TX_RENDERTILE + 1, 0, G_TX_CLAMP, 0, 0, G_TX_CLAMP, 0, 0);
    gDPSetTileSize(gdl++, G_TX_RENDERTILE + 1, 0, 0, (FXC_N - 1) << G_TEXTURE_IMAGE_FRAC, (FXC_N - 1) << G_TEXTURE_IMAGE_FRAC);
    gSPMatrix(gdl++, osVirtualToPhysical((void *)get_BONDdata_field_10E0()), G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
    gDPPortFxBegin(gdl++, PORTFX_CLUSTER);
    for (int i = 0; i < FXCL_MAX; i++) {
        Cluster *k = &s_cl[i];
        if (!k->used) continue;
        gDPPortTexture(gdl++, 0, k->colorId);
        gDPPortTexture(gdl++, 1, k->depthId);
        gdl = applyRoomMatrixToDisplayList(gdl, k->room);
        const struct coord3d *rp = getRoomPositionByIndex(k->room);
        /* the vertex colour: r = the depth in the quad's uv units x 4, g = how much the relief is lit */
        const float depthUV = k->c.depthMax / k->size;
        const unsigned char vr = (unsigned char)(depthUV * 4.0f > 1.0f ? 255.0f : depthUV * 4.0f * 255.0f + 0.5f);
        const unsigned char vg = (unsigned char)(kMat[k->mat].light * 255.0f + 0.5f);
        static const float cs[4][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
        Vtx *v = dynAllocateVertices(4);
        int ok = 1;
        for (int q = 0; q < 4 && ok; q++) {
            float w[3];
            for (int j = 0; j < 3; j++) w[j] = k->origin[j] + (k->T[j] * cs[q][0] + k->B[j] * cs[q][1]) * k->size * 0.5f;
            const float x = w[0] * scale - rp->x, y = w[1] * scale - rp->y, z = w[2] * scale - rp->z;
            if (x < -32000 || x > 32000 || y < -32000 || y > 32000 || z < -32000 || z > 32000) { ok = 0; break; }
            v[q].v.ob[0] = (s16)x; v[q].v.ob[1] = (s16)y; v[q].v.ob[2] = (s16)z;
            v[q].v.flag = 0;
            v[q].v.tc[0] = (s16)(cs[q][0] > 0 ? FXC_N << 5 : 0);
            v[q].v.tc[1] = (s16)(cs[q][1] > 0 ? FXC_N << 5 : 0);
            v[q].v.cn[0] = vr; v[q].v.cn[1] = vg; v[q].v.cn[2] = 255; v[q].v.cn[3] = 255;
        }
        if (!ok) continue;
        gSPVertex(gdl++, osVirtualToPhysical(v), 4, 0);
        gSP2Triangles(gdl++, 0, 1, 2, 0, 0, 2, 3, 0);
    }
    gDPPortFxEnd(gdl++);
    gDPPortTexture(gdl++, 0, -1);
    gDPPortTexture(gdl++, 1, -1);
    gDPPipeSync(gdl++);
    gSPMatrix(gdl++, osVirtualToPhysical((void *)currentPlayerGetProjectionMatrix()), G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
    return gdl;
}
