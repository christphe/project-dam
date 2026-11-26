/*
 * fxparticles.c -- combat particles (Video.Visuals = Enhanced; GE_FX=0 keeps the game's own sprites).
 *
 * Subscribes to the event bus (port/include/portevents.h):
 *  - PEV_IMPACT: a bullet hits a surface; a burst by material (metal sparks, stone chips and dust,
 *    wood splinters, glass shards, water splash, snow and dirt sprays). The decal stays the game's.
 *  - PEV_HIT_CHR + PEV_PUFF type 2: blood droplets thrown along the shot.
 *  - PEV_SHOT: a tracer streak that flies from the muzzle to the hit.
 *  - PEV_RENDER_FX (replace): the game's impact / blood puffs and tracers are not drawn; the
 *    particles are drawn in their place, in the world pass (lv.c), with the game's own recipe for
 *    world-space sprites (glass2.c bullet_spark_render: the BONDdata projection, the room matrix,
 *    room-relative vertices).
 *
 * The simulation runs in world space, once per game frame (g_GlobalTimer), with g_ClockTimer
 * ticks of 1/60 s. It never touches the game's state and has its own random numbers (calling
 * randomGetNext would shift the game's sequence and change the game).
 * Shapes are procedural: no texture; the vertex alpha is 1 in the middle of a particle and 0 at its
 * rim, and port/shaders/fx/ (bursts.glsl, bullets.glsl) turns that ramp into a spark, a droplet or a tracer core.
 */
#include "platform.h"
#include <ultra64.h>
#include <stdlib.h>
#include "bondtypes.h"
#include "game/bondview.h"
#include "game/bgroomtrans.h"
#include "game/dyn.h"
#include "game/lv.h"
#include "game/bg.h"
#include "game/explosion.h"
#include "game/image_bank.h"
#include "game/othermodemicrocode.h"
#include "portevents.h"
#include "portfx.h"
#include "system.h"
#include "fxclusters.h"
#include "fxburst.h"   /* the burst and blood recipes, shared with the effect viewer */

extern int portVisuals;   /* port/src/video.c: 2 = Enhanced */
f32 get_room_data_float1(void);
Mtx *currentPlayerGetProjectionMatrix(void);
Mtx *get_BONDdata_field_10E0(void);

#define FXP_MAX 384

#define FXP_STREAK FXB_STREAK
#define FXP_DISC   FXB_DISC
#define FXP_SOLID  FXB_SOLID

/* A particle: fxburst.h's fields first (its recipes fill them), then the game's own. */
typedef struct Particle {
    FXB_PART_FIELDS;
    float org[3];            /* tracers: where the shot left; the tail never reaches behind it */
    int tracer;
    short room;
} Particle;

static Particle s_p[FXP_MAX];
static int s_n;
static int s_lastTimer = -1;
static unsigned s_rng = 0x2545F491u;
static int s_lastRoom = -1;
static int s_enabled = -1;
static float s_lastHitDir[3];
static int s_lastHitTimer = -1000;

static float frand(void)   /* 0..1, the port's own sequence */
{
    s_rng ^= s_rng << 13;
    s_rng ^= s_rng >> 17;
    s_rng ^= s_rng << 5;
    return (float)(s_rng & 0xFFFFFF) / (float)0x1000000;
}

static float frange(float a, float b) { return a + (b - a) * frand(); }

int portFxUserOn = 1;

void portFxToggle(void)
{
    portFxUserOn = !portFxUserOn;
    sysLogPrintf(LOG_NOTE, "fx: combat effects %s (F9)", portFxUserOn ? "on" : "off (the game's own)");
}

static int fxpOn(void)
{
    if (s_enabled < 0) {
        const char *e = getenv("GE_FX");
        s_enabled = (e != NULL && e[0] == '0') ? 0 : 1;
    }
    return s_enabled && portVisuals >= 2 && portFxUserOn;
}

static Particle *fxpNew(int room)
{
    if (s_n >= FXP_MAX) return NULL;
    Particle *p = &s_p[s_n++];
    __builtin_memset(p, 0, sizeof(*p));
    p->room = (short)room;
    return p;
}

static void norm3(float v[3])
{
    const float l = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
    if (l > 1e-8f) {
        const float k = 1.0f / __builtin_sqrtf(l);
        v[0] *= k; v[1] *= k; v[2] *= k;
    }
}

/* A random direction within `spread` (0 = along n, 1 = the whole hemisphere) of unit vector n. */
static void coneDir(float out[3], const float n[3], float spread)
{
    float r[3] = { frange(-1, 1), frange(-1, 1), frange(-1, 1) };
    norm3(r);
    const float d = r[0] * n[0] + r[1] * n[1] + r[2] * n[2];
    if (d < 0) { r[0] = -r[0]; r[1] = -r[1]; r[2] = -r[2]; }   /* same side as n */
    out[0] = n[0] + spread * r[0];
    out[1] = n[1] + spread * r[1];
    out[2] = n[2] + spread * r[2];
    norm3(out);
}

static void setRgba(Particle *p, int r, int g, int b, int a)
{
    p->rgba[0] = (unsigned char)r; p->rgba[1] = (unsigned char)g; p->rgba[2] = (unsigned char)b; p->rgba[3] = (unsigned char)a;
}

/* fxburst.h's allocator: a particle in room *ctx */
static FxbPart *fxpAlloc(void *ctx)
{
    Particle *p = fxpNew(*(const int *)ctx);
    return (FxbPart *)p;   /* FXB_PART_FIELDS come first */
}

/* ---- events ------------------------------------------------------------------------------- */

static int onImpact(int type, int phase, void *data, void *ctx)
{
    (void)type; (void)phase; (void)ctx;
    if (!fxpOn()) return PEV_CONTINUE;
    const PortEvImpact *e = data;
    const float *P = e->pos, *N = e->normal;
    const int room = e->room;
    s_lastRoom = room;
    fxClustersAddImpact(e);   /* the level's holes: clusters (fxclusters.c) */
    int r = room;
    const FxbEmit em = { fxpAlloc, &r, &s_rng };
    fxb_impact(&em, e->material, P, N);   /* fxburst.h */
    return PEV_CONTINUE;
}

static int onHitChr(int type, int phase, void *data, void *ctx)
{
    (void)type; (void)phase; (void)ctx;
    const PortEvHitChr *e = data;
    /* the shot direction in world space: the view matrix's rotation (no translation) */
    Mtxf *m = currentPlayerGetViewToWorldMtxf();
    const float x = e->dir_view[0], y = e->dir_view[1], z = e->dir_view[2];
    s_lastHitDir[0] = m->m[0][0] * x + m->m[1][0] * y + m->m[2][0] * z;
    s_lastHitDir[1] = m->m[0][1] * x + m->m[1][1] * y + m->m[2][1] * z;
    s_lastHitDir[2] = m->m[0][2] * x + m->m[1][2] * y + m->m[2][2] * z;
    norm3(s_lastHitDir);
    s_lastHitTimer = g_GlobalTimer;
    return PEV_CONTINUE;
}

static int onPuff(int type, int phase, void *data, void *ctx)
{
    (void)type; (void)phase; (void)ctx;
    if (!fxpOn()) return PEV_CONTINUE;
    const PortEvPuff *e = data;
    if (e->type != 2) return PEV_CONTINUE;   /* dust puffs: the impact event made the material burst */
    float dir[3] = { 0, 1, 0 };
    if (g_GlobalTimer - s_lastHitTimer <= 2) {
        /* thrown on along the shot (it went through), plus a little up */
        dir[0] = s_lastHitDir[0]; dir[1] = s_lastHitDir[1] + 0.25f; dir[2] = s_lastHitDir[2];
        norm3(dir);
    }
    s_lastRoom = e->room;
    int r = e->room;
    const FxbEmit em = { fxpAlloc, &r, &s_rng };
    fxb_blood(&em, e->pos, dir);   /* fxburst.h: jets along the shot, a back-spray, heavy drops */
    return PEV_CONTINUE;
}

#define TRACER_SPEED 30000.0f  /* world units per second */
#define TRACER_START 30.0f     /* the streak starts this far ahead of the muzzle */
#define NEAR_FADE0   40.0f     /* a streak point closer than this to the eye is invisible ... */
#define NEAR_FADE1   140.0f    /* ... and fully visible from this distance on: a shot never covers the view */

static int onShot(int type, int phase, void *data, void *ctx)
{
    (void)type; (void)phase; (void)ctx;
    if (!fxpOn()) return PEV_CONTINUE;
    const PortEvShot *e = data;
    float d[3] = { e->to[0] - e->from[0], e->to[1] - e->from[1], e->to[2] - e->from[2] };
    const float dist = __builtin_sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (dist < 1.0f) return PEV_CONTINUE;
    norm3(d);
    const int room = s_lastRoom >= 0 ? s_lastRoom : 0;
    if (dist < TRACER_START + 1.0f) return PEV_CONTINUE;
    Particle *p = fxpNew(room);
    if (p == NULL) return PEV_CONTINUE;
    p->org[0] = e->from[0]; p->org[1] = e->from[1]; p->org[2] = e->from[2];
    p->tracer = 1;
    p->pos[0] = e->from[0] + d[0] * TRACER_START;
    p->pos[1] = e->from[1] + d[1] * TRACER_START;
    p->pos[2] = e->from[2] + d[2] * TRACER_START;
    /* at least two frames on screen: a short shot flies slower and still ends on the hit */
    float speed = TRACER_SPEED;
    if ((dist - TRACER_START) / speed < 4.0f / 60.0f) speed = (dist - TRACER_START) * 15.0f;   /* >= 4 frames */
    p->vel[0] = d[0] * speed; p->vel[1] = d[1] * speed; p->vel[2] = d[2] * speed;
    p->maxlife = p->life = (dist - TRACER_START) / speed;
    p->size = 2.2f;
    p->stretch = 0.02f;   /* a ~600-unit tail at TRACER_SPEED: visible over a frame or two */
    p->minlen = 60.0f;
    p->shape = FXP_STREAK;
    p->kind = PORTFX_TRACER;
    setRgba(p, 255, 232, 180, 255);
    return PEV_CONTINUE;
}

/* ---- simulation --------------------------------------------------------------------------- */

static void fxpStep(float dt)
{
    int w = 0;
    for (int i = 0; i < s_n; i++) {
        Particle p = s_p[i];
        if (!fxb_step((FxbPart *)&p, dt)) continue;
        s_p[w++] = p;
    }
    s_n = w;
}

static void fxpTick(void)
{
    if (g_GlobalTimer == s_lastTimer) return;   /* a second view of the same frame (split screen) */
    s_lastTimer = g_GlobalTimer;
    int ticks = g_ClockTimer;
    if (ticks < 0) ticks = 0;
    if (ticks > 6) ticks = 6;
    fxpStep(ticks / 60.0f);
}

/* ---- drawing ------------------------------------------------------------------------------ */

static Vtx *s_v;
static int s_vroom = -1;

static int toVtx(Vtx *v, const float w[3], const struct coord3d *roompos, float scale, const unsigned char rgba[4], int alpha)
{
    const float x = w[0] * scale - roompos->x;
    const float y = w[1] * scale - roompos->y;
    const float z = w[2] * scale - roompos->z;
    if (x < -32000 || x > 32000 || y < -32000 || y > 32000 || z < -32000 || z > 32000) return 0;
    v->v.ob[0] = (s16)x; v->v.ob[1] = (s16)y; v->v.ob[2] = (s16)z;
    v->v.flag = 0;
    v->v.tc[0] = 0; v->v.tc[1] = 0;
    v->v.cn[0] = rgba[0]; v->v.cn[1] = rgba[1]; v->v.cn[2] = rgba[2];
    v->v.cn[3] = (u8)(alpha < 0 ? 0 : alpha > 255 ? 255 : alpha);
    return 1;
}

static Gfx *drawParticle(Gfx *gdl, const Particle *p, const Mtxf *view, float scale)
{
    const struct coord3d *rp = getRoomPositionByIndex(p->room);
    const float fade = p->life / p->maxlife;
    const int a = (int)(p->rgba[3] * (p->kind == PORTFX_TRACER ? 1.0f : fade));
    const float camR[3] = { view->m[0][0], view->m[0][1], view->m[0][2] };
    const float camU[3] = { view->m[1][0], view->m[1][1], view->m[1][2] };
    Vtx *v;

    if (p->shape == FXP_STREAK || p->shape == FXB_SHARD) {
        float d[3] = { p->vel[0], p->vel[1], p->vel[2] };
        const float sp = __builtin_sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        norm3(d);
        float len = sp * p->stretch;
        if (len < p->minlen) len = p->minlen;
        if (p->tracer) {   /* the tail never reaches behind the muzzle: the streak grows as the shot flies */
            const float o[3] = { p->pos[0] - p->org[0], p->pos[1] - p->org[1], p->pos[2] - p->org[2] };
            const float flown = __builtin_sqrtf(o[0] * o[0] + o[1] * o[1] + o[2] * o[2]);
            if (len > flown) len = flown;
        }
        /* the width axis: across the streak, facing the camera (cross with the view direction) */
        const float vd[3] = { view->m[2][0], view->m[2][1], view->m[2][2] };
        float x[3] = { d[1] * vd[2] - d[2] * vd[1], d[2] * vd[0] - d[0] * vd[2], d[0] * vd[1] - d[1] * vd[0] };
        norm3(x);
        const float s = p->size;
        const float h[3] = { p->pos[0], p->pos[1], p->pos[2] };
        const float t[3] = { h[0] - d[0] * len, h[1] - d[1] * len, h[2] - d[2] * len };
        float q[6][3];
        for (int k = 0; k < 3; k++) {
            q[0][k] = h[k]; q[1][k] = h[k] + x[k] * s; q[2][k] = h[k] - x[k] * s;
            q[3][k] = t[k]; q[4][k] = t[k] + x[k] * s; q[5][k] = t[k] - x[k] * s;
        }
        v = dynAllocateVertices(6);
        int ah = a, at = p->kind == PORTFX_TRACER ? a / 2 : (int)(a * p->tail);
        {   /* fade the parts close to the eye (view->m[3] is the camera's world position) */
            const float cx = view->m[3][0], cy = view->m[3][1], cz = view->m[3][2];
            const float dh = __builtin_sqrtf((h[0] - cx) * (h[0] - cx) + (h[1] - cy) * (h[1] - cy) + (h[2] - cz) * (h[2] - cz));
            const float dt = __builtin_sqrtf((t[0] - cx) * (t[0] - cx) + (t[1] - cy) * (t[1] - cy) + (t[2] - cz) * (t[2] - cz));
            float kh = (dh - NEAR_FADE0) / (NEAR_FADE1 - NEAR_FADE0), kt = (dt - NEAR_FADE0) / (NEAR_FADE1 - NEAR_FADE0);
            kh = kh < 0 ? 0 : kh > 1 ? 1 : kh;
            kt = kt < 0 ? 0 : kt > 1 ? 1 : kt;
            ah = (int)(ah * kh);
            at = (int)(at * kt);
        }
        const int crisp = p->shape == FXB_SHARD;   /* a splinter: no soft rim, a pointed tail */
        const int al[6] = { ah, crisp ? ah : 0, crisp ? ah : 0, at, crisp ? at : 0, crisp ? at : 0 };
        if (crisp) {
            for (int k = 0; k < 3; k++) { q[4][k] = t[k] + x[k] * s * 0.15f; q[5][k] = t[k] - x[k] * s * 0.15f; }
        }
        for (int k = 0; k < 6; k++) {
            if (!toVtx(&v[k], q[k], rp, scale, p->rgba, al[k])) return gdl;
        }
        gSPVertex(gdl++, osVirtualToPhysical(v), 6, 0);
        gSP2Triangles(gdl++, 1, 0, 3, 0, 1, 3, 4, 0);
        gSP2Triangles(gdl++, 0, 2, 5, 0, 0, 5, 3, 0);
        return gdl;
    }

    /* disc / solid: a hexagon around the centre; a solid chunk keeps its alpha to the rim */
    const float s = p->size;
    float q[7][3];
    for (int k = 0; k < 3; k++) q[0][k] = p->pos[k];
    for (int i = 0; i < 6; i++) {
        const float ang = i * (6.2831853f / 6.0f);
        const float c = __builtin_cosf(ang) * s, sn = __builtin_sinf(ang) * s;
        for (int k = 0; k < 3; k++) q[1 + i][k] = p->pos[k] + camR[k] * c + camU[k] * sn;
    }
    v = dynAllocateVertices(7);
    const int rim = p->shape == FXP_SOLID ? a : 0;
    if (!toVtx(&v[0], q[0], rp, scale, p->rgba, a)) return gdl;
    for (int i = 1; i < 7; i++) {
        if (!toVtx(&v[i], q[i], rp, scale, p->rgba, rim)) return gdl;
    }
    gSPVertex(gdl++, osVirtualToPhysical(v), 7, 0);
    gSP2Triangles(gdl++, 0, 1, 2, 0, 0, 2, 3, 0);
    gSP2Triangles(gdl++, 0, 3, 4, 0, 0, 4, 5, 0);
    gSP2Triangles(gdl++, 0, 5, 6, 0, 0, 6, 1, 0);
    return gdl;
}

static Gfx *fxpDraw(Gfx *gdl)
{
    if (s_n == 0) return gdl;
    Mtxf *view = currentPlayerGetViewToWorldMtxf();
    const float scale = get_room_data_float1();
    (void)s_v; (void)s_vroom;

    gDPPipeSync(gdl++);
    gDPSetCycleType(gdl++, G_CYC_1CYCLE);
    gSPTexture(gdl++, 0, 0, 0, 0, G_OFF);
    gDPSetCombineMode(gdl++, G_CC_SHADE, G_CC_SHADE);
    gDPSetRenderMode(gdl++, G_RM_ZB_XLU_SURF, G_RM_ZB_XLU_SURF2);
    /* The draw before (the decals) leaves G_AC_DITHER: fast3d turns it into a per-pixel random alpha
     * cut (CF_NOISE) that riddled the chips with holes. Set the whole alpha path. */
    gDPSetAlphaCompare(gdl++, G_AC_NONE);
    gDPSetAlphaDither(gdl++, G_AD_DISABLE);
    gDPSetColorDither(gdl++, G_CD_DISABLE);
    gSPClearGeometryMode(gdl++, G_CULL_BOTH | G_LIGHTING | G_FOG | G_TEXTURE_GEN);
    gSPSetGeometryMode(gdl++, G_SHADE | G_SHADING_SMOOTH | G_ZBUFFER);
    gSPMatrix(gdl++, osVirtualToPhysical((void *)get_BONDdata_field_10E0()), G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);

    int room = -1, kind = -1;
    for (int i = 0; i < s_n; i++) {
        const Particle *p = &s_p[i];
        if (p->room != room) {
            room = p->room;
            gdl = applyRoomMatrixToDisplayList(gdl, room);
        }
        if (p->kind != kind) {
            if (kind > 0) gDPPortFxEnd(gdl++);
            kind = p->kind;
            if (kind > 0) gDPPortFxBegin(gdl++, kind);
        }
        if (p->kind == PORTFX_TRACER) {   /* a wide faint halo around the core: the shot reads at a glance */
            Particle h = *p;
            h.size *= 4.0f;
            h.rgba[3] = (unsigned char)(h.rgba[3] / 2);
            gdl = drawParticle(gdl, &h, view, scale);
        }
        gdl = drawParticle(gdl, p, view, scale);
    }
    if (kind > 0) gDPPortFxEnd(gdl++);

    gDPPipeSync(gdl++);
    gSPMatrix(gdl++, osVirtualToPhysical((void *)currentPlayerGetProjectionMatrix()), G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
    return gdl;
}

static int onRender(int type, int phase, void *data, void *ctx)
{
    (void)type; (void)phase; (void)ctx;
    if (!fxpOn()) return PEV_CONTINUE;
    PortEvRenderFx *e = data;
    Gfx **gdl = (Gfx **)e->gdl;
    switch (e->kind) {
        case PORTFX_TRACER_1P:
            return PEV_HANDLED;          /* drawn with the others in the world pass */
        case PORTFX_TRACER:
            return PEV_HANDLED;          /* the particles are drawn at the puffs' place, just after */
        case PORTFX_SPARK:
            fxpTick();
            *gdl = fxpDraw(*gdl);
            return PEV_HANDLED;
        case PORTFX_IMPACT:          /* the level's holes (bg.c), as clusters; the props' keep the game's draw */
            *gdl = fxClustersDraw(*gdl);
            return PEV_HANDLED;
        default:
            return PEV_CONTINUE;
    }
}

PD_CONSTRUCTOR static void fxParticlesInit(void)
{
    portEventSubscribe(PEV_IMPACT, PEV_AFTER, 0, onImpact, NULL);
    portEventSubscribe(PEV_HIT_CHR, PEV_AFTER, 0, onHitChr, NULL);
    portEventSubscribe(PEV_PUFF, PEV_AFTER, 0, onPuff, NULL);
    portEventSubscribe(PEV_SHOT, PEV_AFTER, 0, onShot, NULL);
    portEventSubscribe(PEV_RENDER_FX, PEV_BEFORE, 0, onRender, NULL);
}
