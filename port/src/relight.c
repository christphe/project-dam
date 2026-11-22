/*
 * project-dam: PROTOTYPE (GE_RELIGHT=1). Removes GE's hand-painted light from
 * the level's outdoor vertex colours: every room vertex keeps its hue, and its
 * brightness becomes sky ambient (how much sky it sees, rays over the
 * hemisphere) + the GE+ sun by orientation (N.L), with no shadow, scaled to
 * GE's overall outdoor brightness. Vertices that see no sky (tunnels,
 * interiors) keep GE's colour.
 *
 * Runs once per level, the first frame the sun is known (dynlights.c); the
 * colours are reapplied whenever GE reloads a room (bg.c bgLoadRoomModelData).
 * Rooms are in room units (world x room_data_float1); the sun direction is the
 * same in both spaces. The level's triangles and their BVH stay loaded.
 */
#include <ultra64.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <stdint.h>
#include "bondtypes.h"
#include "game/bg.h"
#include "game/bondview.h"
#include "game/chrai.h"
#include "system.h"
#include "relight.h"

#include <gbi_extension.h>

/* GE's math.h / stdlib.h (found first) lack these: an implicit declaration would return an int
 * (docs/dev/findings/2026-11-23-ge-math-h-has-no-log2f.md) */
#define fabsf(x) __builtin_fabsf(x)
extern void qsort(void *base, size_t n, size_t size, int (*cmp)(const void *, const void *));
extern void bgLoadRoomModelData(s32 roomID);

typedef struct {
    float a[3], b[3], c[3];
} RlTri;

typedef struct {
    float mn[3], mx[3];
    int left, right;     /* children, or -1 for a leaf */
    int first, count;    /* leaf: range in s_order */
} RlNode;

typedef struct {
    int nv;
    u8 *rgb;             /* nv x 3, NULL = not relit */
} RlRoom;

static RlTri *s_tris;
static int *s_triRoom;   /* the room of each triangle */
static int s_curRoom;
static int s_dropTris, s_vtxOut;
static int s_ntris, s_captris;
static int *s_order;
static RlNode *s_nodes;
static int s_nnodes;
static RlRoom *s_rooms;
static int s_nrooms;
static const void *s_levelKey;   /* the level the colours belong to */
static int s_active;
static int s_busy;
#define RL_MAXPADS 2048
static float s_pads[RL_MAXPADS][3];
static int s_npads;
static int s_sideByPad, s_sideByHits;   /* stats */

int portRelightActive(void)
{
    return s_active;
}

/* ---------------- triangles ---------------- */

static void addTri(const float *a, const float *b, const float *c)
{
    if (s_ntris == s_captris) {
        s_captris = s_captris ? s_captris * 2 : 16384;
        s_tris = (RlTri *)realloc(s_tris, sizeof(RlTri) * (size_t)s_captris);
        s_triRoom = (int *)realloc(s_triRoom, sizeof(int) * (size_t)s_captris);
    }

    memcpy(s_tris[s_ntris].a, a, sizeof(float) * 3);
    memcpy(s_tris[s_ntris].b, b, sizeof(float) * 3);
    memcpy(s_tris[s_ntris].c, c, sizeof(float) * 3);
    s_triRoom[s_ntris] = s_curRoom;
    s_ntris++;
}

/* Walks a room's primary DL (16-byte PC Gfx, N64 words in the low dwords):
 * G_VTX fills the 16-slot vertex cache from the room's vertex array, G_TRI1 /
 * G_TRI4 emit triangles. Calls back per triangle with room vertex indices. */
typedef void (*RlTriFn)(int i0, int i1, int i2, void *ctx);

static void walkRoomDl(int room, int nv, RlTriFn fn, void *ctx)
{
    Gfx *g = (Gfx *)g_BgRoomInfo[room].ptr_expanded_mapping_info;
    int ncmd = g_BgRoomInfo[room].usize_primary_DL_binary / (int)sizeof(Gfx);
    int slot[32];
    int i, k;

    for (i = 0; i < 32; i++) {
        slot[i] = -1;
    }

    for (i = 0; i < ncmd; i++, g++) {
        const u32 w0 = (u32)g->words.w0;
        const u32 w1 = (u32)g->words.w1;
        const s8 op = (s8)(w0 >> 24);

        if (op == (s8)G_ENDDL) {
            break;
        }

        if (op == (s8)G_VTX) {
            const int n = (int)((w0 >> 20) & 0xf) + 1;
            const int v0 = (int)((w0 >> 16) & 0xf);
            const int base = (int)((w1 & 0x00ffffff) / sizeof(Vtx));

            for (k = 0; k < n && v0 + k < 32; k++) {
                slot[v0 + k] = (base + k < nv) ? base + k : -1;
            }

            if (base + n > nv) {
                s_vtxOut++;
            }
        } else if (op == (s8)G_TRI1) {
            const int a = (int)(((w1 >> 16) & 0xff) / 10);
            const int b = (int)(((w1 >> 8) & 0xff) / 10);
            const int c = (int)((w1 & 0xff) / 10);

            if (a < 32 && b < 32 && c < 32 && slot[a] >= 0 && slot[b] >= 0 && slot[c] >= 0) {
                fn(slot[a], slot[b], slot[c], ctx);
            } else {
                s_dropTris++;
            }
        } else if (op == (s8)G_TRI4) {
            int t[4][3];

            t[0][0] = w1 & 0xf;          t[0][1] = (w1 >> 4) & 0xf;  t[0][2] = w0 & 0xf;
            t[1][0] = (w1 >> 8) & 0xf;   t[1][1] = (w1 >> 12) & 0xf; t[1][2] = (w0 >> 4) & 0xf;
            t[2][0] = (w1 >> 16) & 0xf;  t[2][1] = (w1 >> 20) & 0xf; t[2][2] = (w0 >> 8) & 0xf;
            t[3][0] = (w1 >> 24) & 0xf;  t[3][1] = (w1 >> 28) & 0xf; t[3][2] = (w0 >> 12) & 0xf;

            for (k = 0; k < 4; k++) {
                const int a = t[k][0], b = t[k][1], c = t[k][2];

                if (a == b || b == c || a == c) {
                    continue;   /* unused slot of the TRI4 */
                }

                if (slot[a] >= 0 && slot[b] >= 0 && slot[c] >= 0) {
                    fn(slot[a], slot[b], slot[c], ctx);
                } else {
                    s_dropTris++;
                }
            }
        }
    }
}

typedef struct {
    float *pos;    /* nv x 3, room units, level space */
    float *nrm;    /* nv x 3, accumulated */
} RlRoomGeo;

static void collectTri(int i0, int i1, int i2, void *ctx)
{
    RlRoomGeo *g = (RlRoomGeo *)ctx;
    const float *a = &g->pos[i0 * 3], *b = &g->pos[i1 * 3], *c = &g->pos[i2 * 3];
    float e1[3], e2[3], n[3];
    int k;

    addTri(a, b, c);

    for (k = 0; k < 3; k++) {
        e1[k] = b[k] - a[k];
        e2[k] = c[k] - a[k];
    }

    n[0] = e1[1] * e2[2] - e1[2] * e2[1];
    n[1] = e1[2] * e2[0] - e1[0] * e2[2];
    n[2] = e1[0] * e2[1] - e1[1] * e2[0];

    for (k = 0; k < 3; k++) {   /* area-weighted */
        g->nrm[i0 * 3 + k] += n[k];
        g->nrm[i1 * 3 + k] += n[k];
        g->nrm[i2 * 3 + k] += n[k];
    }
}

/* ---------------- BVH ---------------- */

static void triBounds(int t, float mn[3], float mx[3])
{
    int k;

    for (k = 0; k < 3; k++) {
        float lo = s_tris[t].a[k], hi = lo;

        if (s_tris[t].b[k] < lo) lo = s_tris[t].b[k];
        if (s_tris[t].b[k] > hi) hi = s_tris[t].b[k];
        if (s_tris[t].c[k] < lo) lo = s_tris[t].c[k];
        if (s_tris[t].c[k] > hi) hi = s_tris[t].c[k];
        mn[k] = lo;
        mx[k] = hi;
    }
}

static int s_sortAxis;

static int cmpCentroid(const void *pa, const void *pb)
{
    const RlTri *ta = &s_tris[*(const int *)pa];
    const RlTri *tb = &s_tris[*(const int *)pb];
    const float ca = ta->a[s_sortAxis] + ta->b[s_sortAxis] + ta->c[s_sortAxis];
    const float cb = tb->a[s_sortAxis] + tb->b[s_sortAxis] + tb->c[s_sortAxis];

    return ca < cb ? -1 : ca > cb ? 1 : 0;
}

static int buildNode(int first, int count)
{
    const int id = s_nnodes++;
    RlNode *nd = &s_nodes[id];
    float mn[3], mx[3], ext[3];
    int i, k, axis;

    nd->mn[0] = nd->mn[1] = nd->mn[2] = 1e30f;
    nd->mx[0] = nd->mx[1] = nd->mx[2] = -1e30f;

    for (i = first; i < first + count; i++) {
        triBounds(s_order[i], mn, mx);

        for (k = 0; k < 3; k++) {
            if (mn[k] < nd->mn[k]) nd->mn[k] = mn[k];
            if (mx[k] > nd->mx[k]) nd->mx[k] = mx[k];
        }
    }

    if (count <= 4) {
        nd->left = nd->right = -1;
        nd->first = first;
        nd->count = count;
        return id;
    }

    for (k = 0; k < 3; k++) {
        ext[k] = nd->mx[k] - nd->mn[k];
    }

    axis = ext[0] > ext[1] ? (ext[0] > ext[2] ? 0 : 2) : (ext[1] > ext[2] ? 1 : 2);
    s_sortAxis = axis;
    qsort(&s_order[first], (size_t)count, sizeof(int), cmpCentroid);

    {
        const int half = count / 2;
        const int l = buildNode(first, half);
        const int r = buildNode(first + half, count - half);

        s_nodes[id].left = l;
        s_nodes[id].right = r;
        s_nodes[id].count = 0;
    }

    return id;
}

static int rayBox(const RlNode *nd, const float o[3], const float inv[3], float tmax)
{
    float t0 = 0.0f, t1 = tmax;
    int k;

    for (k = 0; k < 3; k++) {
        float a = (nd->mn[k] - o[k]) * inv[k];
        float b = (nd->mx[k] - o[k]) * inv[k];

        if (a > b) {
            float t = a;
            a = b;
            b = t;
        }

        if (a > t0) t0 = a;
        if (b < t1) t1 = b;

        if (t0 > t1) {
            return 0;
        }
    }

    return 1;
}

static int rayTri(const RlTri *t, const float o[3], const float d[3], float tmax)
{
    float e1[3], e2[3], p[3], q[3], s[3], det, inv, u, v, tt;
    int k;

    for (k = 0; k < 3; k++) {
        e1[k] = t->b[k] - t->a[k];
        e2[k] = t->c[k] - t->a[k];
    }

    p[0] = d[1] * e2[2] - d[2] * e2[1];
    p[1] = d[2] * e2[0] - d[0] * e2[2];
    p[2] = d[0] * e2[1] - d[1] * e2[0];
    det = e1[0] * p[0] + e1[1] * p[1] + e1[2] * p[2];

    if (fabsf(det) < 1e-8f) {
        return 0;
    }

    inv = 1.0f / det;

    for (k = 0; k < 3; k++) {
        s[k] = o[k] - t->a[k];
    }

    u = (s[0] * p[0] + s[1] * p[1] + s[2] * p[2]) * inv;

    if (u < 0.0f || u > 1.0f) {
        return 0;
    }

    q[0] = s[1] * e1[2] - s[2] * e1[1];
    q[1] = s[2] * e1[0] - s[0] * e1[2];
    q[2] = s[0] * e1[1] - s[1] * e1[0];
    v = (d[0] * q[0] + d[1] * q[1] + d[2] * q[2]) * inv;

    if (v < 0.0f || u + v > 1.0f) {
        return 0;
    }

    tt = (e2[0] * q[0] + e2[1] * q[1] + e2[2] * q[2]) * inv;
    return tt > 0.5f && tt < tmax;
}

/* 1 = something in the level blocks the ray before tmax */
static int occluded(const float o[3], const float d[3], float tmax)
{
    int stack[128];
    int sp = 0;
    float inv[3];
    int k;

    for (k = 0; k < 3; k++) {
        inv[k] = fabsf(d[k]) > 1e-12f ? 1.0f / d[k] : 1e30f;
    }

    stack[sp++] = 0;

    while (sp > 0) {
        const RlNode *nd = &s_nodes[stack[--sp]];

        if (!rayBox(nd, o, inv, tmax)) {
            continue;
        }

        if (nd->left < 0) {
            int i;

            for (i = nd->first; i < nd->first + nd->count; i++) {
                if (rayTri(&s_tris[s_order[i]], o, d, tmax)) {
                    return 1;
                }
            }
        } else if (sp < 126) {
            stack[sp++] = nd->left;
            stack[sp++] = nd->right;
        }
    }

    return 0;
}

#define RL_SKY_RAYS 32
#define RL_FAR 60000.0f
#define RL_EPS 2.0f

static float hash01(unsigned int x)
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return (float)(x & 0xffffff) / 16777216.0f;
}

static void basis(const float n[3], float t[3], float b[3])
{
    const float s = n[1] > 0.9f || n[1] < -0.9f ? 1.0f : 0.0f;
    float up[3], len;

    up[0] = s;
    up[1] = 1.0f - s;
    up[2] = 0.0f;
    t[0] = up[1] * n[2] - up[2] * n[1];
    t[1] = up[2] * n[0] - up[0] * n[2];
    t[2] = up[0] * n[1] - up[1] * n[0];
    len = sqrtf(t[0] * t[0] + t[1] * t[1] + t[2] * t[2]);
    t[0] /= len;
    t[1] /= len;
    t[2] /= len;
    b[0] = n[1] * t[2] - n[2] * t[1];
    b[1] = n[2] * t[0] - n[0] * t[2];
    b[2] = n[0] * t[1] - n[1] * t[0];
}

static void freeAll(void)
{
    int r;

    for (r = 0; r < s_nrooms; r++) {
        free(s_rooms[r].rgb);
    }

    free(s_rooms);
    s_rooms = NULL;
    s_nrooms = 0;
    s_active = 0;
}

void portRelightApplyRoom(s32 room)
{
    Vtx *v;
    int i, nv;

    if (!s_active || s_busy || room <= 0 || room >= s_nrooms || s_rooms[room].rgb == NULL) {
        return;
    }

    v = g_BgRoomInfo[room].vertices;
    nv = g_BgRoomInfo[room].usize_point_index_binary / (int)sizeof(Vtx);

    if (v == NULL || nv != s_rooms[room].nv) {
        return;
    }

    for (i = 0; i < nv; i++) {
        const u8 *c = s_rooms[room].rgb;

        v[i].v.cn[0] = c[i * 3 + 0];
        v[i].v.cn[1] = c[i * 3 + 1];
        v[i].v.cn[2] = c[i * 3 + 2];
    }
}

/* The visible side of a vertex: GE draws much of its level double-sided, so the winding does not say which side
 * faces the player. The nearest pads the vertex can see vote; with none in sight, the side that sees more of the
 * level (the other side looks out into the void around the level). */
static void visibleSide(const float pv[3], float n[3], int slot)
{
    float vote = 0.0f, t[3], b[3], d[3];
    int seen = 0, tries, used[8], nu = 0, k, j;

    for (tries = 0; tries < 8 && seen < 3 && s_npads > 0; tries++) {
        int best = -1, q, u;
        float bd = 1e30f;

        for (q = 0; q < s_npads; q++) {
            float dd = 0.0f;
            int skip = 0;

            for (u = 0; u < nu; u++) if (used[u] == q) skip = 1;
            if (skip) continue;

            for (k = 0; k < 3; k++) dd += (s_pads[q][k] - pv[k]) * (s_pads[q][k] - pv[k]);
            if (dd < bd) { bd = dd; best = q; }
        }

        if (best < 0) break;
        used[nu++] = best;

        {
            float dir[3], dl = sqrtf(bd), dn, o2[3];

            if (dl < 1.0f) continue;

            for (k = 0; k < 3; k++) dir[k] = (s_pads[best][k] - pv[k]) / dl;
            dn = dir[0] * n[0] + dir[1] * n[1] + dir[2] * n[2];

            for (k = 0; k < 3; k++) o2[k] = pv[k] + (dn >= 0 ? n[k] : -n[k]) * RL_EPS;

            if (!occluded(o2, dir, dl - 1.0f)) {
                vote += dn >= 0 ? 1.0f : -1.0f;
                seen++;
            }
        }
    }

    if (seen > 0 && vote != 0.0f) {
        if (vote < 0.0f) for (k = 0; k < 3; k++) n[k] = -n[k];
        s_sideByPad++;
    } else {
        int side, bestHits = -1;
        float bestN[3];

        for (side = 0; side < 2; side++) {
            float ns[3], os[3];
            int hitS = 0;

            for (k = 0; k < 3; k++) {
                ns[k] = side ? -n[k] : n[k];
                os[k] = pv[k] + ns[k] * RL_EPS;
            }

            basis(ns, t, b);

            for (j = 0; j < 16; j++) {
                const unsigned int seed = (unsigned int)(slot * 577 + j * 71);
                const float u1 = ((float)j + hash01(seed)) / 16.0f;
                const float u2 = hash01(seed ^ 0x51ed27u);
                const float rr = sqrtf(u1), ph = 6.2831853f * u2;

                for (k = 0; k < 3; k++) d[k] = t[k] * rr * cosf(ph) + b[k] * rr * sinf(ph) + ns[k] * sqrtf(1.0f - u1);
                if (occluded(os, d, RL_FAR)) hitS++;
            }

            if (hitS > bestHits) {
                bestHits = hitS;
                memcpy(bestN, ns, sizeof bestN);
            }
        }

        memcpy(n, bestN, sizeof bestN);
        s_sideByHits++;
    }
}

void portRelightLevel(const float sunDir[3])
{
    RlRoomGeo *geo;
    float *light, *skyv, *hitv, sumGe = 0.0f, sumL = 0.0f, scale;
    int *roomOf, *idxOf;
    int r, i, k, nvAll = 0, nOut = 0, flip = 0, up = 0, down = 0;
    const clock_t t0 = clock();
    static const float SKY[3] = { 0.62f, 0.68f, 0.80f };
    static const float SUN[3] = { 1.00f, 0.94f, 0.82f };
    const float SKY_W = 0.55f, SUN_W = 0.85f, BOUNCE_W = 0.30f;   /* bounce: rays that hit the level see lit snow/rock */

    if (s_levelKey == (const void *)ptr_bgdata_room_fileposition_list) {
        return;
    }

    free(s_order);
    free(s_nodes);
    s_order = NULL;
    s_nodes = NULL;
    s_ntris = 0;
    freeAll();
    s_levelKey = (const void *)ptr_bgdata_room_fileposition_list;
    s_busy = 1;
    s_dropTris = s_vtxOut = s_sideByPad = s_sideByHits = 0;
    s_nrooms = g_MaxNumRooms;
    s_rooms = (RlRoom *)calloc((size_t)s_nrooms, sizeof(RlRoom));
    geo = (RlRoomGeo *)calloc((size_t)s_nrooms, sizeof(RlRoomGeo));

    /* 1. every room once: positions, normals, triangles; GE's colours kept in rgb for now */
    for (r = 1; r < s_nrooms; r++) {
        const u8 wasLoaded = g_BgRoomInfo[r].model_bin_loaded;
        Vtx *v;
        int nv;

        if (ptr_bgdata_room_fileposition_list[r].pPointTableBin == 0) {
            continue;
        }

        if (!wasLoaded) {
            bgLoadRoomModelData(r);
        }

        v = g_BgRoomInfo[r].vertices;
        nv = g_BgRoomInfo[r].usize_point_index_binary / (int)sizeof(Vtx);

        if (g_BgRoomInfo[r].model_bin_loaded && v != NULL && nv > 0 && g_BgRoomInfo[r].ptr_expanded_mapping_info) {
            const coord3d *rp = &ptr_bgdata_room_fileposition_list[r].pos;

            s_rooms[r].nv = nv;
            s_rooms[r].rgb = (u8 *)malloc((size_t)nv * 3);
            geo[r].pos = (float *)malloc(sizeof(float) * 3 * (size_t)nv);
            geo[r].nrm = (float *)calloc((size_t)nv * 3, sizeof(float));

            for (i = 0; i < nv; i++) {
                geo[r].pos[i * 3 + 0] = rp->x + v[i].v.ob[0];
                geo[r].pos[i * 3 + 1] = rp->y + v[i].v.ob[1];
                geo[r].pos[i * 3 + 2] = rp->z + v[i].v.ob[2];
                s_rooms[r].rgb[i * 3 + 0] = v[i].v.cn[0];
                s_rooms[r].rgb[i * 3 + 1] = v[i].v.cn[1];
                s_rooms[r].rgb[i * 3 + 2] = v[i].v.cn[2];
            }

            s_curRoom = r;
            walkRoomDl(r, nv, collectTri, &geo[r]);
            nvAll += nv;
        }

        if (!wasLoaded && g_BgRoomInfo[r].model_bin_loaded) {
            delete_room_data(r);
        }
    }

    /* N64 front faces: decide the winding from the floors (most near-horizontal faces look up) */
    for (r = 1; r < s_nrooms; r++) {
        if (geo[r].nrm == NULL) continue;

        for (i = 0; i < s_rooms[r].nv; i++) {
            const float *n = &geo[r].nrm[i * 3];
            const float len = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);

            if (len > 0.0f && fabsf(n[1]) > 0.9f * len) {
                if (n[1] > 0.0f) up++; else down++;
            }
        }
    }

    flip = down > up;

    s_order = (int *)malloc(sizeof(int) * (size_t)(s_ntris > 0 ? s_ntris : 1));
    s_nodes = (RlNode *)malloc(sizeof(RlNode) * (size_t)(2 * (s_ntris > 0 ? s_ntris : 1)));
    s_nnodes = 0;

    for (i = 0; i < s_ntris; i++) {
        s_order[i] = i;
    }

    if (s_ntris > 0) {
        buildNode(0, s_ntris);
    }

    {   /* the playable space: the setup pads, in room space, at chest height */
        PadRecord *pads = g_CurrentSetup.pads;
        const float sc = get_room_data_float1();
        int pi;

        s_npads = 0;

        for (pi = 0; pads != NULL && pi < 4000 && s_npads < RL_MAXPADS; pi++) {
            const PadRecord *pd = &pads[pi];

            if (pd->plink == NULL && pd->pos.x == 0.0f && pd->pos.y == 0.0f && pd->pos.z == 0.0f) break;

            s_pads[s_npads][0] = pd->pos.x * sc;
            s_pads[s_npads][1] = pd->pos.y * sc + 30.0f;
            s_pads[s_npads][2] = pd->pos.z * sc;
            s_npads++;
        }
    }

    /* 2. per vertex: the visible side, sky visibility (cosine-weighted hemisphere), bounce, sun by orientation */
    light = (float *)calloc((size_t)(nvAll > 0 ? nvAll : 1) * 3, sizeof(float));
    skyv = (float *)calloc((size_t)(nvAll > 0 ? nvAll : 1), sizeof(float));
    hitv = (float *)calloc((size_t)(nvAll > 0 ? nvAll : 1), sizeof(float));
    roomOf = (int *)malloc(sizeof(int) * (size_t)(nvAll > 0 ? nvAll : 1));
    idxOf = (int *)malloc(sizeof(int) * (size_t)(nvAll > 0 ? nvAll : 1));

    for (r = 1; r < s_nrooms; r++) {
        if (geo[r].nrm == NULL) continue;

        for (i = 0; i < s_rooms[r].nv; i++) {
            const float *pv = &geo[r].pos[i * 3];
            float n[3], o[3], t[3], b[3], d[3], len, ndl;
            int sky = 0, hit = 0, j;
            const int slot = nOut++;

            roomOf[slot] = r;
            idxOf[slot] = i;
            memcpy(n, &geo[r].nrm[i * 3], sizeof n);
            len = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);

            if (len <= 0.0f) {
                skyv[slot] = -1.0f;   /* not on a triangle: keep GE's colour */
                continue;
            }

            for (k = 0; k < 3; k++) {
                n[k] = (flip ? -n[k] : n[k]) / len;
            }

            visibleSide(pv, n, slot);

            for (k = 0; k < 3; k++) o[k] = pv[k] + n[k] * RL_EPS;
            basis(n, t, b);

            for (j = 0; j < RL_SKY_RAYS; j++) {
                const unsigned int seed = (unsigned int)(slot * 977 + j * 131);
                const float u1 = ((float)j + hash01(seed)) / RL_SKY_RAYS;
                const float u2 = hash01(seed ^ 0x9e3779b9U);
                const float rr = sqrtf(u1), ph = 6.2831853f * u2;
                const float x = rr * cosf(ph), y = rr * sinf(ph), z = sqrtf(1.0f - u1);

                for (k = 0; k < 3; k++) {
                    d[k] = t[k] * x + b[k] * y + n[k] * z;
                }

                if (!occluded(o, d, RL_FAR)) {
                    if (d[1] > 0.0f) sky++;
                } else {
                    hit++;
                }
            }

            skyv[slot] = (float)sky / RL_SKY_RAYS;
            hitv[slot] = (float)hit / RL_SKY_RAYS;
            ndl = n[0] * sunDir[0] + n[1] * sunDir[1] + n[2] * sunDir[2];

            for (k = 0; k < 3; k++) {
                light[slot * 3 + k] = SKY[k] * SKY_W * (0.6f + 0.4f * skyv[slot]) + BOUNCE_W * 0.5f * hitv[slot] +
                                      SUN[k] * SUN_W * (ndl > 0.0f ? ndl : 0.0f);
            }

            if (skyv[slot] > 0.05f) {   /* every outdoor vertex sets the brightness match */
                const u8 *c = &s_rooms[r].rgb[i * 3];

                sumGe += (c[0] + c[1] + c[2]) / (3.0f * 255.0f);
                sumL += (light[slot * 3] + light[slot * 3 + 1] + light[slot * 3 + 2]) / 3.0f;
            }
        }
    }

    scale = sumL > 0.0f ? sumGe / sumL : 1.0f;

    /* 3. the new colour: GE's hue, the computed brightness, blended in by how much sky the vertex sees */
    for (i = 0; i < nOut; i++) {
        u8 *c = &s_rooms[roomOf[i]].rgb[idxOf[i] * 3];
        const float lg = (0.299f * c[0] + 0.587f * c[1] + 0.114f * c[2]) / 255.0f;
        const float ln = (0.299f * light[i * 3] + 0.587f * light[i * 3 + 1] + 0.114f * light[i * 3 + 2]) * scale;
        float w;

        if (skyv[i] < 0.0f) continue;

        w = (skyv[i] - 0.02f) / (0.20f - 0.02f);
        w = w < 0.0f ? 0.0f : w > 1.0f ? 1.0f : w;
        w = w * w * (3.0f - 2.0f * w);

        for (k = 0; k < 3; k++) {
            const float tint = lg > 0.02f ? (c[k] / 255.0f) / lg : 1.0f;
            float nv = tint * ln * 255.0f;

            nv = nv > 255.0f ? 255.0f : nv;
            c[k] = (u8)(c[k] + (nv - c[k]) * w + 0.5f);
        }
    }

    for (r = 1; r < s_nrooms; r++) {
        free(geo[r].pos);
        free(geo[r].nrm);
    }

    free(geo);
    free(light);
    free(skyv);
    free(hitv);
    free(roomOf);
    free(idxOf);
    s_busy = 0;
    s_active = 1;

    for (r = 1; r < s_nrooms; r++) {
        if (g_BgRoomInfo[r].model_bin_loaded) {
            portRelightApplyRoom(r);
        }
    }

    sysLogPrintf(LOG_NOTE, "relight: %d rooms, %d vertices, %d tris (%d dropped, %d G_VTX out of range), winding %s, "
                 "sides %d by pads / %d by the level, scale %.3f, %u ms",
                 s_nrooms, nvAll, s_ntris, s_dropTris, s_vtxOut, flip ? "flipped" : "as is", s_sideByPad, s_sideByHits,
                 scale, (unsigned)((clock() - t0) * 1000 / CLOCKS_PER_SEC));
}
