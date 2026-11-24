#ifndef FXBURST_H
#define FXBURST_H

/*
 * fxburst.h -- the recipes of the port's combat particles: the burst a bullet throws off each material, and the
 * blood of a body hit. Shared by the game (port/src/fxparticles.c) and the effect viewer (tools_pc/fxview,
 * "Blood/smoke/debris" tab): what is tuned in the viewer is what the game runs.
 *
 * Each recipe is made to read at a glance, with one signature per material:
 *  - stone / tile: grey chips that fly and fall, a few hot sparks, a pale dust plume that lingers;
 *  - wood: crisp thin splinters, pale (fresh wood is lighter than its surface), a little wood dust;
 *  - metal: a shower of hot sparks that arc down, a white flash, a wisp of smoke;
 *  - glass: glinting shards that fall, a few bright glints;
 *  - dirt / mud: dark clods and a thick brown plume;  snow: a white powder burst;  water: a splash of drops;
 *  - blood: jets of droplets along the shot (through the body) and a smaller back-spray, heavy drops that fall,
 *    a small mist only at the wound.
 * Particles are drawn untextured; the shape comes from the vertex alpha ramp and uber.frag.glsl fxShade (kind).
 *
 * Pure C, header-only, no game state: the caller gives an allocator and its random state.
 */

#include "portfx.h"       /* PORTFX_* kinds */
#include "portevents.h"   /* PMAT_* materials */

/* shape: a streak along the velocity (soft edges), a soft disc, a solid flake, a shard (a streak with crisp
 * edges: a splinter) */
enum { FXB_STREAK, FXB_DISC, FXB_SOLID, FXB_SHARD };

#define FXB_PART_FIELDS \
    float pos[3], vel[3]; \
    float life, maxlife; \
    float size, grow;      /* world units; size change per second */ \
    float gravity, drag;   /* units/s^2; fraction of speed lost per second */ \
    float stretch, minlen; /* streaks: tail length = speed * stretch (s), at least minlen */ \
    float tail;            /* streaks: the tail's alpha, a fraction of the head's */ \
    unsigned char rgba[4]; \
    unsigned char shape, kind   /* FXB_*, PORTFX_* for fxShade (PORTFX_NONE = plain gradient) */

typedef struct FxbPart {
    FXB_PART_FIELDS;
} FxbPart;

typedef struct FxbEmit {
    FxbPart *(*alloc)(void *ctx);   /* a zeroed particle, or NULL when full */
    void *ctx;
    unsigned *rng;                  /* xorshift state */
} FxbEmit;

static float fxb_rand(const FxbEmit *em)
{
    unsigned s = *em->rng;
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    *em->rng = s;
    return (float)(s & 0xFFFFFF) / (float)0x1000000;
}

static float fxb_range(const FxbEmit *em, float a, float b) { return a + (b - a) * fxb_rand(em); }

static void fxb_norm(float v[3])
{
    const float l = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
    if (l > 1e-8f) {
        const float k = 1.0f / __builtin_sqrtf(l);
        v[0] *= k; v[1] *= k; v[2] *= k;
    }
}

/* A random direction within `spread` (0 = along n, 1 = about the hemisphere) of unit vector n. */
static void fxb_cone(const FxbEmit *em, float out[3], const float n[3], float spread)
{
    float r[3] = { fxb_range(em, -1, 1), fxb_range(em, -1, 1), fxb_range(em, -1, 1) };
    fxb_norm(r);
    if (r[0] * n[0] + r[1] * n[1] + r[2] * n[2] < 0) { r[0] = -r[0]; r[1] = -r[1]; r[2] = -r[2]; }
    for (int k = 0; k < 3; k++) out[k] = n[k] + spread * r[k];
    fxb_norm(out);
}

typedef struct FxbSpray {
    int count, shape, kind;
    float spread, vmin, vmax, lmin, lmax, size;
    float gravity, drag, stretch, minlen, tail;
    int r, g, b, a;
} FxbSpray;

static void fxb_spray(const FxbEmit *em, const float pos[3], const float n[3], const FxbSpray *s)
{
    for (int i = 0; i < s->count; i++) {
        FxbPart *p = em->alloc(em->ctx);
        if (p == 0) return;
        float d[3];
        fxb_cone(em, d, n, s->spread);
        const float v = fxb_range(em, s->vmin, s->vmax);
        for (int k = 0; k < 3; k++) { p->pos[k] = pos[k]; p->vel[k] = d[k] * v; }
        p->maxlife = p->life = fxb_range(em, s->lmin, s->lmax);
        p->size = s->size * fxb_range(em, 0.7f, 1.3f);
        p->gravity = s->gravity;
        p->drag = s->drag;
        p->stretch = s->stretch;
        p->minlen = s->minlen > 0.0f ? s->minlen : p->size * 2.0f;
        p->tail = s->tail;
        p->shape = (unsigned char)s->shape;
        p->kind = (unsigned char)s->kind;
        const float shade = fxb_range(em, 0.8f, 1.0f);
        p->rgba[0] = (unsigned char)(s->r * shade);
        p->rgba[1] = (unsigned char)(s->g * shade);
        p->rgba[2] = (unsigned char)(s->b * shade);
        p->rgba[3] = (unsigned char)s->a;
    }
}

/* Soft clouds that grow and fade, drifting off the surface (a plume: `count` of them, spread out). */
static void fxb_plume(const FxbEmit *em, const float pos[3], const float n[3], int count, float size, float life,
                      float speed, int r, int g, int b, int a)
{
    for (int i = 0; i < count; i++) {
        FxbPart *p = em->alloc(em->ctx);
        if (p == 0) return;
        float d[3];
        fxb_cone(em, d, n, 0.7f);
        const float v = speed * fxb_range(em, 0.5f, 1.2f);
        for (int k = 0; k < 3; k++) {
            p->pos[k] = pos[k] + n[k] * size * 0.3f;
            p->vel[k] = d[k] * v;
        }
        p->vel[1] += 15.0f;   /* dust rises a little */
        p->maxlife = p->life = life * fxb_range(em, 0.75f, 1.15f);
        p->size = size * fxb_range(em, 0.7f, 1.1f);
        p->grow = size * 1.6f;
        p->drag = 2.2f;
        p->shape = FXB_DISC;
        p->kind = PORTFX_NONE;
        const float shade = fxb_range(em, 0.9f, 1.05f);
        p->rgba[0] = (unsigned char)(r * shade > 255.0f ? 255.0f : r * shade);
        p->rgba[1] = (unsigned char)(g * shade > 255.0f ? 255.0f : g * shade);
        p->rgba[2] = (unsigned char)(b * shade > 255.0f ? 255.0f : b * shade);
        p->rgba[3] = (unsigned char)a;
    }
}

/* The pop: an opaque puff of fresh dust at the hit that bursts out to 3x its size and fades within `life`
 * (what makes the hit read at a glance, as the game's own puff does). */
static void fxb_pop(const FxbEmit *em, const float pos[3], const float n[3], float size, float life, int r, int g, int b, int a)
{
    FxbPart *p = em->alloc(em->ctx);
    if (p == 0) return;
    for (int k = 0; k < 3; k++) {
        p->pos[k] = pos[k] + n[k] * size * 0.5f;
        p->vel[k] = n[k] * 60.0f;
    }
    p->maxlife = p->life = life;
    p->size = size;
    p->grow = size * 2.0f / life;
    p->drag = 3.0f;
    p->shape = FXB_DISC;
    p->kind = PORTFX_NONE;
    p->rgba[0] = (unsigned char)r; p->rgba[1] = (unsigned char)g; p->rgba[2] = (unsigned char)b; p->rgba[3] = (unsigned char)a;
}

/* A short bright flash at the hit (additive). */
static void fxb_flash(const FxbEmit *em, const float pos[3], const float n[3], float size, float life, int r, int g, int b)
{
    FxbPart *p = em->alloc(em->ctx);
    if (p == 0) return;
    for (int k = 0; k < 3; k++) p->pos[k] = pos[k] + n[k] * 0.5f;
    p->maxlife = p->life = life;
    p->size = size;
    p->grow = -size * 0.5f / life;
    p->shape = FXB_DISC;
    p->kind = PORTFX_SPARK;
    p->rgba[0] = (unsigned char)r; p->rgba[1] = (unsigned char)g; p->rgba[2] = (unsigned char)b; p->rgba[3] = 255;
}

/* The burst of a bullet hitting material `pmat` (PMAT_*) at pos, surface normal n. */
static void fxb_impact(const FxbEmit *em, int pmat, const float pos[3], const float n[3])
{
    switch (pmat) {
        case PMAT_METAL:
        case PMAT_METALOBJ: {
            const FxbSpray sparks = { 26, FXB_STREAK, PORTFX_SPARK, 0.9f, 500, 1400, 0.25f, 0.6f, 1.6f, 1300, 0.6f, 0.03f, 4.0f, 0.35f, 255, 196, 110, 255 };
            fxb_spray(em, pos, n, &sparks);
            fxb_flash(em, pos, n, 9.0f, 0.07f, 255, 230, 180);
            fxb_plume(em, pos, n, 1, 7.0f, 0.5f, 40.0f, 120, 118, 115, 120);
            break;
        }
        case PMAT_WOOD: {
            const FxbSpray splinters = { 14, FXB_SHARD, PORTFX_NONE, 0.75f, 300, 800, 0.55f, 0.95f, 0.6f, 1100, 0.6f, 0.0f, 10.0f, 1.0f, 236, 202, 146, 255 };
            const FxbSpray slivers = { 8, FXB_SHARD, PORTFX_NONE, 0.85f, 250, 650, 0.5f, 0.9f, 0.4f, 1100, 0.6f, 0.0f, 6.0f, 1.0f, 214, 176, 120, 255 };
            fxb_spray(em, pos, n, &splinters);
            fxb_spray(em, pos, n, &slivers);
            fxb_plume(em, pos, n, 2, 9.0f, 0.6f, 70.0f, 215, 184, 140, 130);
            break;
        }
        case PMAT_GLASS:
        case PMAT_GLASS_XLU: {
            const FxbSpray shards = { 20, FXB_STREAK, PORTFX_NONE, 0.9f, 250, 700, 0.6f, 1.0f, 1.7f, 1100, 0.4f, 0.01f, 6.0f, 0.85f, 236, 246, 255, 255 };
            const FxbSpray glints = { 12, FXB_STREAK, PORTFX_SPARK, 0.9f, 300, 800, 0.2f, 0.45f, 1.5f, 900, 0.4f, 0.025f, 5.0f, 0.5f, 220, 235, 255, 255 };
            fxb_flash(em, pos, n, 8.0f, 0.06f, 220, 235, 255);
            fxb_spray(em, pos, n, &shards);
            fxb_spray(em, pos, n, &glints);
            fxb_plume(em, pos, n, 1, 8.0f, 0.4f, 40.0f, 235, 240, 248, 120);
            break;
        }
        case PMAT_WATER: {
            const FxbSpray drops = { 30, FXB_STREAK, PORTFX_BLOOD, 0.45f, 300, 800, 0.45f, 0.8f, 1.2f, 1200, 0.3f, 0.022f, 5.0f, 0.85f, 215, 232, 245, 240 };
            const FxbSpray spray = { 10, FXB_DISC, PORTFX_NONE, 0.8f, 150, 400, 0.4f, 0.7f, 3.0f, 900, 0.8f, 0.0f, 0.0f, 1.0f, 235, 242, 248, 200 };
            fxb_pop(em, pos, n, 9.0f, 0.25f, 240, 246, 250, 230);
            fxb_spray(em, pos, n, &drops);
            fxb_spray(em, pos, n, &spray);
            fxb_plume(em, pos, n, 3, 12.0f, 0.7f, 80.0f, 236, 242, 248, 170);
            break;
        }
        case PMAT_SNOW: {
            const FxbSpray powder = { 14, FXB_DISC, PORTFX_NONE, 0.9f, 120, 380, 0.5f, 0.9f, 2.6f, 500, 1.4f, 0.0f, 0.0f, 1.0f, 245, 248, 252, 230 };
            fxb_spray(em, pos, n, &powder);
            fxb_plume(em, pos, n, 3, 13.0f, 0.9f, 80.0f, 244, 247, 250, 190);
            break;
        }
        case PMAT_DIRT:
        case PMAT_MUD: {
            const FxbSpray clods = { 14, FXB_SOLID, PORTFX_NONE, 0.7f, 220, 560, 0.5f, 0.85f, 3.2f, 1300, 0.5f, 0.0f, 0.0f, 1.0f, 120, 92, 62, 255 };
            fxb_pop(em, pos, n, 9.0f, 0.25f, 170, 140, 104, 235);
            fxb_spray(em, pos, n, &clods);
            fxb_plume(em, pos, n, 4, 14.0f, 1.1f, 100.0f, 150, 122, 90, 200);
            break;
        }
        case PMAT_CHR:
            break;   /* the blood is fxb_blood (the body-hit puff) */
        default: {   /* stone, tile, default */
            const FxbSpray chips = { 14, FXB_SOLID, PORTFX_NONE, 0.8f, 280, 750, 0.45f, 0.85f, 2.6f, 1200, 0.5f, 0.0f, 0.0f, 1.0f, 196, 192, 184, 255 };
            const FxbSpray sparks = { 8, FXB_STREAK, PORTFX_SPARK, 0.7f, 500, 1100, 0.08f, 0.22f, 1.5f, 900, 0.6f, 0.03f, 4.0f, 0.35f, 255, 205, 130, 255 };
            fxb_pop(em, pos, n, 8.0f, 0.22f, 222, 218, 210, 240);
            fxb_spray(em, pos, n, &chips);
            fxb_spray(em, pos, n, &sparks);
            fxb_flash(em, pos, n, 7.0f, 0.05f, 255, 220, 170);
            fxb_plume(em, pos, n, 4, 13.0f, 1.1f, 90.0f, 200, 195, 186, 190);
            break;
        }
    }
}

/* The blood of a body hit at pos; dir = the shot's direction (through the body), unit. */
static void fxb_blood(const FxbEmit *em, const float pos[3], const float dir[3])
{
    const float back[3] = { -dir[0] * 0.8f, -dir[1] * 0.8f + 0.3f, -dir[2] * 0.8f };
    float b[3] = { back[0], back[1], back[2] };
    fxb_norm(b);
    /* the exit jet: a tight cone of fast droplets, the gerbe */
    const FxbSpray jet = { 14, FXB_STREAK, PORTFX_BLOOD, 0.4f, 250, 700, 0.35f, 0.7f, 1.3f, 1100, 0.5f, 0.02f, 4.0f, 0.85f, 150, 10, 12, 255 };
    /* the back-spray towards the shooter, smaller */
    const FxbSpray backspray = { 5, FXB_STREAK, PORTFX_BLOOD, 0.5f, 120, 380, 0.3f, 0.6f, 1.1f, 1100, 0.6f, 0.02f, 3.0f, 0.85f, 140, 8, 10, 255 };
    /* heavy drops that fall */
    const FxbSpray drops = { 4, FXB_STREAK, PORTFX_BLOOD, 0.6f, 80, 240, 0.5f, 0.9f, 2.4f, 1000, 0.4f, 0.015f, 0.0f, 0.9f, 120, 6, 8, 255 };
    fxb_spray(em, pos, dir, &jet);
    fxb_spray(em, pos, b, &backspray);
    fxb_spray(em, pos, dir, &drops);
    fxb_plume(em, pos, dir, 1, 5.0f, 0.25f, 30.0f, 130, 12, 14, 110);   /* a little mist at the wound only */
}

/* One step of dt seconds; returns 0 when the particle is gone. */
static int fxb_step(FxbPart *p, float dt)
{
    p->life -= dt;
    if (p->life <= 0) return 0;
    const float k = 1.0f - p->drag * dt;
    p->vel[0] *= k;
    p->vel[1] = p->vel[1] * k - p->gravity * dt;
    p->vel[2] *= k;
    for (int j = 0; j < 3; j++) p->pos[j] += p->vel[j] * dt;
    p->size += p->grow * dt;
    if (p->size < 0.05f) p->size = 0.05f;
    return 1;
}

#endif
