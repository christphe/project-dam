/*
 * dynlights.c -- dynamic point lights (Video.Visuals = Enhanced), game side.
 * See port/include/dynlights.h.
 *
 * Read-only on game state: the explosion buffer and the current player's
 * camera. The lights live in the frame's dyn memory, like the matrices the
 * same display list points at.
 */
#include <ultra64.h>
#include <math.h>
#include "bondtypes.h"
#include "game/bondview.h"
#include "game/dyn.h"
#include "game/explosion.h"
#include "game/matrixmath.h"
#include "game/chr.h"
#include "game/player.h"
#include "game/propobj.h"
#include "dynlights.h"
#include "skyplus.h"
#include "relight.h"
#include <stdlib.h>

extern int portVisuals;                    /* port/src/video.c: 2 = Enhanced */
extern s_explosiontype g_ExplosionTypes[]; /* src/game/explosion.c */

/* Warm fire colour, well over 1: the shaders scale it by the surface's own
 * texture colour, and the blast should still read in daylight. */
#define EXPL_R 2.60f
#define EXPL_G 1.60f
#define EXPL_B 0.70f
#define EXPL_RANGE_MUL 4.0f   /* light radius = the type's explosion_range x this */
#define EXPL_LIFT 0.5f        /* the light sits this x explosion_size above the blast's base (a flat light only grazes the floor) */
#define EXPL_MIN_SIZE 10.0f   /* the sparks / no-blast types (size 0.1 .. 1) give no light */
#define EXPL_RISE_TICKS 4.0f
#define EXPL_SIZE_GAIN 0.25f  /* brightness gained per doubling of explosion_size over 100 (clamped 0.8 .. 1.8) */
#define EXPL_GAIN 1.15f       /* every blast's brightness, on top of the size scale */

#define DYNL_CANDIDATES 32    /* point lights gathered per view before keepStrongest trims them to what the shaders take */

/* More point lights than places: keep the ones that light the view most, the brightest relative to how far they
 * are from the camera (eye space, so the camera is at the origin); a light whose reach covers the camera ranks
 * by brightness alone. The order of the kept lights does not matter. */
static float lightWeight(const PortDynLight *l)
{
    const float d = sqrtf(l->pos[0] * l->pos[0] + l->pos[1] * l->pos[1] + l->pos[2] * l->pos[2]);
    const float b = l->col[0] > l->col[1] ? (l->col[0] > l->col[2] ? l->col[0] : l->col[2]) : (l->col[1] > l->col[2] ? l->col[1] : l->col[2]);

    return d <= l->radius ? b * 1e6f : b * l->radius / d;
}

static int keepStrongest(PortDynLight *l, int n, int max)
{
    while (n > max) {   /* drop the weakest, n - max times (n <= 32) */
        int i, worst = 0;
        float w = lightWeight(&l[0]);

        for (i = 1; i < n; i++) {
            const float wi = lightWeight(&l[i]);

            if (wi < w) {
                w = wi;
                worst = i;
            }
        }

        l[worst] = l[--n];
    }

    return n;
}

/* GE draws in a space scaled by the level's visibility scale (bg.c bgGetLevelVisibilityScale, fed to the camera as
 * D_800364CC: bondview2.c builds the render lookat from (camera - player) x scale). The eye positions the shaders
 * rebuild are in that scaled space, the camera matrix field_64 (worldToEye) in world units: a light's eye position
 * and radius are scaled by it. */
static float renderScale(void)
{
    extern f32 bgGetLevelVisibilityScale(void);
    const float s = bgGetLevelVisibilityScale();

    return s > 0.0f ? s : 1.0f;
}

static int addExplosions(PortDynLight *out, int n, Mtxf *worldToEye)
{
    s32 i;

    for (i = 0; i < EXPLOSION_BUFFER_LEN && n < DYNL_CANDIDATES; i++) {
        struct Explosion *exp = &g_ExplosionBuffer[i];
        s_explosiontype *type;
        struct coord3d pos;
        f32 life, t, k;

        if (exp->prop == NULL) {
            continue;
        }

        type = &g_ExplosionTypes[exp->explosion_type];

        if (type->explosion_size < EXPL_MIN_SIZE || type->explosion_range <= 0.0f) {
            continue;
        }

        /* the blast lives until duration + the flare tail (explosion.c explosionTick) */
        life = (f32)type->duration + 16.0f * type->flareanimspeed;
        t = (f32)exp->age;

        if (life <= 0.0f || t >= life) {
            continue;
        }

        k = 1.0f - t / life;
        k = k * k;

        if (t < EXPL_RISE_TICKS) {
            k *= (t + 1.0f) / (EXPL_RISE_TICKS + 1.0f);
        }

        if (k <= 0.01f) {
            continue;
        }

        {   /* bigger blasts burn brighter: x1 at size 100, +25 % per doubling (grenade 200: x1.25, 600: x1.65),
             * clamped 0.8 .. 1.8, the whole scale x EXPL_GAIN */
            float g = 1.0f + EXPL_SIZE_GAIN * __builtin_log2f(type->explosion_size / 100.0f);   /* GE's math.h has no log2f */

            k *= (g < 0.8f ? 0.8f : g > 1.8f ? 1.8f : g) * EXPL_GAIN;
        }

        pos = exp->prop->pos;
        pos.y += type->explosion_size * EXPL_LIFT;
        mtx4TransformVecInPlace(worldToEye, &pos);

        out[n].pos[0] = pos.x * renderScale();
        out[n].pos[1] = pos.y * renderScale();
        out[n].pos[2] = pos.z * renderScale();
        out[n].radius = type->explosion_range * EXPL_RANGE_MUL * renderScale();
        out[n].col[0] = EXPL_R * k;
        out[n].col[1] = EXPL_G * k;
        out[n].col[2] = EXPL_B * k;
        out[n].pad = 0.0f;
        n++;
    }

    return n;
}

/* Muzzle flashes: a short warm light while GE shows a gun's flash. The players' flash positions are GE's own
 * (gunfire.c: hand field_B58, the flash in world space, set while the hand's flash is visible, field_87D); a
 * guard's is its chest while its held weapon shows the gunfire (propobj.c weaponIsGunfireVisible). Both are
 * the last frame's, the guns are drawn after this. */
#define MUZZLE_R 2.20f
#define MUZZLE_G 1.50f
#define MUZZLE_B 0.70f
#define MUZZLE_RANGE 260.0f      /* world units */
#define MUZZLE_CHEST 0.72f       /* a guard's flash height, x its chrheight above its ground */
#define MUZZLE_SAME 120.0f       /* a guard flash this close to a player's is that player's body */

static int addMuzzle(PortDynLight *out, int n, Mtxf *worldToEye, const struct coord3d *world)
{
    struct coord3d pos = *world;

    if (n >= DYNL_CANDIDATES) {
        return n;
    }

    mtx4TransformVecInPlace(worldToEye, &pos);
    out[n].pos[0] = pos.x * renderScale();
    out[n].pos[1] = pos.y * renderScale();
    out[n].pos[2] = pos.z * renderScale();
    out[n].radius = MUZZLE_RANGE * renderScale();
    out[n].col[0] = MUZZLE_R;
    out[n].col[1] = MUZZLE_G;
    out[n].col[2] = MUZZLE_B;
    out[n].pad = 0.0f;
    return n + 1;
}

static int addMuzzles(PortDynLight *out, int n, Mtxf *worldToEye)
{
    struct coord3d seen[8];
    int nseen = 0, p, h, i;
    const int nplayers = getPlayerCount();

    for (p = 0; p < 4 && p < nplayers; p++) {
        struct player *pl = g_playerPointers[p];

        if (pl == NULL) {
            continue;
        }

        for (h = 0; h < 2; h++) {
            if (pl->hands[h].field_87D != 0) {
                n = addMuzzle(out, n, worldToEye, &pl->hands[h].field_B58);
                if (nseen < 8) seen[nseen++] = pl->hands[h].field_B58;
            }
        }
    }

    for (i = 0; i < g_NumChrSlots; i++) {
        ChrRecord *chr = &g_ChrSlots[i];

        if (chr->model == NULL || chr->prop == NULL) {
            continue;
        }

        for (h = 0; h < 2; h++) {
            PropRecord *wp = chrGetEquippedWeaponProp(chr, (GUNHAND)h);
            struct coord3d at;
            int k, dup = 0;

            if (wp == NULL || !weaponIsGunfireVisible(wp)) {
                continue;
            }

            at.x = chr->prop->pos.x;
            at.y = chr->ground + chr->chrheight * MUZZLE_CHEST;
            at.z = chr->prop->pos.z;

            for (k = 0; k < nseen; k++) {
                const float dx = at.x - seen[k].x, dz = at.z - seen[k].z;

                if (dx * dx + dz * dz < MUZZLE_SAME * MUZZLE_SAME) dup = 1;
            }

            if (!dup) {
                n = addMuzzle(out, n, worldToEye, &at);
            }

            break;   /* two guns, one light */
        }
    }

    return n;
}

/* The GE+ sky's sun is no shader light: without the level's shadows it lit guards in tunnels. Its direction
 * only feeds the PROTOTYPE GE_RELIGHT=1, which removes GE's painted light from the level's vertex colours
 * (relight.c) and puts the sun there by orientation. */
static void relightFromSun(void)
{
    static int relight = -1;
    float dir[3], col[3];

    if (relight < 0) {
        const char *e = getenv("GE_RELIGHT");
        relight = e != NULL && atoi(e) != 0;   /* default 0 */
    }

    if (relight && skyPlusSun(dir, col)) {
        portRelightLevel(dir);
    }
}

/* The relief light of the bullet holes (uber.frag.glsl fxClusterPom): a fixed light from above, 80 degrees
 * over the horizon in the world, so a hole's relief keeps one shading however the camera turns (the old
 * fallback was a direction in eye space, which followed the camera's pitch). Radius -2 marks it: fast3d puts
 * it in the Lights block's sun_dir without lighting the scene (count[1] stays for the real sun). */
#define RELIEF_ELEV 80.0f
static int addReliefLight(PortDynLight *out, int n, Mtxf *worldToEye)
{
    const float e = RELIEF_ELEV * (3.14159265f / 180.0f);
    const float w[3] = { 0.0f, __builtin_sinf(e), __builtin_cosf(e) };   /* towards the light; GE's world is y up */
    float d[3];
    int i;

    if (n >= DYNL_CANDIDATES) {
        return n;
    }
    for (i = 0; i < 3; i++) {   /* the rotation only: a direction */
        d[i] = worldToEye->m[0][i] * w[0] + worldToEye->m[1][i] * w[1] + worldToEye->m[2][i] * w[2];
    }
    {
        const float l = __builtin_sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (l < 1e-6f) {
            return n;
        }
        for (i = 0; i < 3; i++) out[n].pos[i] = d[i] / l;
    }
    out[n].radius = -2.0f;
    out[n].col[0] = out[n].col[1] = out[n].col[2] = 0.0f;
    out[n].pad = 0.0f;
    return n + 1;
}

Gfx *portDynLightsEmit(Gfx *gdl)
{
    PortDynLight lights[DYNL_CANDIDATES];
    PortDynLight *dst;
    Mtxf *worldToEye;
    int n = 0;
    int i;

    if (portVisuals < 2) {
        return gdl;
    }

    worldToEye = camGetWorldToScreenMtxf();

    if (worldToEye == NULL) {
        return gdl;
    }

    n = addExplosions(lights, n, worldToEye);
    n = addMuzzles(lights, n, worldToEye);
    n = keepStrongest(lights, n, PORT_DYNLIGHTS_MAX);
    relightFromSun();
    n = addReliefLight(lights, n, worldToEye);

    /* always emitted (n = 0 too): the previous view's lights must not leak
     * into this one */
    dst = (PortDynLight *)dynAllocate(sizeof(PortDynLight) * (n > 0 ? n : 1));

    for (i = 0; i < n; i++) {
        dst[i] = lights[i];
    }

    gdl->words.w0 = ((u32)G_DYNLIGHTS_EXT << 24) | (u32)n;
    gdl->words.w1 = (uintptr_t)dst;
    gdl++;

    return gdl;
}

Gfx *portShadowCastMark(Gfx *gdl, int on)
{
    if (portVisuals < 2) {
        return gdl;
    }

    gdl->words.w0 = ((u32)G_SHADOWCAST_EXT << 24) | (u32)(on != 0);
    gdl->words.w1 = 0;
    gdl++;

    return gdl;
}

/* PROTOTYPE: GE_NOBLOB=1 hides GE's soft round shadow under characters and objects (model.c doshadow) */
int portHideBlob(void)
{
    static int hide = -1;

    if (hide < 0) {
        const char *e = getenv("GE_NOBLOB");
        hide = e != NULL && atoi(e) != 0;   /* default 0 */
    }

    return hide;
}

