/*
 * fxhooks.c -- the game side of the event bus (port/include/portevents.h): the functions behind the
 * GE_EV_* / GE_FX_* hook macros of src/game/player.h. Each turns the game's arguments into a plain
 * payload and emits it. With no subscriber to an event type, a hook returns at once.
 *
 * GE_EVLOG=1 subscribes a log of every event (rate-capped), to check what the hooks deliver.
 */
#include "platform.h"
#include <ultra64.h>
#include <stdlib.h>
#include "bondtypes.h"
#include "game/tex.h"
#include "portevents.h"
#include "fxclusters.h"
#include "portfx.h"
#include "system.h"

/* ---- material from impact type ------------------------------------------------------------- */

/* The game picks an impact type (the decal) from the list of the surface's material
 * (g_HitTypeSounds[material]->thing2, src/game/tex.c). The reverse map gives each type the first
 * specific material that lists it; a type only the default list has stays PMAT_DEFAULT. */
#define FX_IMPACT_TYPES 256
static signed char s_typeMaterial[FX_IMPACT_TYPES];
static int s_typeMaterialReady;

static void fxBuildTypeMaterial(void)
{
    for (int t = 0; t < FX_IMPACT_TYPES; t++) s_typeMaterial[t] = PMAT_UNKNOWN;
    for (int pass = 0; pass < 2; pass++) {               /* specific materials first, then the default list */
        for (int m = 0; m < PMAT_COUNT; m++) {
            if ((pass == 0) == (m == PMAT_DEFAULT)) continue;
            const struct image_sound *s = g_HitTypeSounds[m];
            if (s == NULL || s->thing2 == NULL) continue;
            for (int i = 0; i < s->thing2_len; i++) {
                const int t = s->thing2[i];
                if (t >= 0 && t < FX_IMPACT_TYPES && s_typeMaterial[t] == PMAT_UNKNOWN) s_typeMaterial[t] = (signed char)m;
            }
        }
    }
    s_typeMaterialReady = 1;
}

static int fxMaterialOfType(int type)
{
    if (!s_typeMaterialReady) fxBuildTypeMaterial();
    return (type >= 0 && type < FX_IMPACT_TYPES) ? s_typeMaterial[type] : PMAT_UNKNOWN;
}

/* GE_EV_SURFACE: the call sites that know the surface (chrprop.c level hits, chr.c held gun / hat)
 * pass the hit right before explosionCreateBulletImpact; the impact event takes its material, its
 * triangle's display-list command and baked light. */
static int s_pendingMaterial = PMAT_UNKNOWN;
static const void *s_pendingTri;
static float s_pendingShade[3] = { -1.0f, -1.0f, -1.0f };

void portEvSurface(const void *hit)
{
    const HitThing *h = (const HitThing *)hit;
    s_pendingMaterial = h->texturenum < 0 ? PMAT_DEFAULT : (int)g_Textures[h->texturenum].hitTexture;
    s_pendingTri = h->tricmd;
    s_pendingShade[0] = s_pendingShade[1] = s_pendingShade[2] = -1.0f;
    if (h->vtx0 != NULL && h->vtx1 != NULL && h->vtx2 != NULL) {
        const Vertex *v[3] = { h->vtx0, h->vtx1, h->vtx2 };
        for (int c = 0; c < 3; c++) {
            const int s = c == 0 ? v[0]->r + v[1]->r + v[2]->r : c == 1 ? v[0]->g + v[1]->g + v[2]->g : v[0]->b + v[1]->b + v[2]->b;
            s_pendingShade[c] = (float)s / (3.0f * 255.0f);
        }
    }
}

static void v3(float out[3], const struct coord3d *c)
{
    out[0] = c->x;
    out[1] = c->y;
    out[2] = c->z;
}

/* ---- the hooks ---------------------------------------------------------------------------- */

void portEvShot(void *beam, s32 item, struct coord3d *from, struct coord3d *to)
{
    if (portEventCount(PEV_SHOT) == 0) return;
    PortEvShot e;
    v3(e.from, from);
    v3(e.to, to);
    e.item = item;
    e.beam = beam;
    portEventEmit(PEV_SHOT, PEV_AFTER, &e);
}

void portEvPuff(struct coord3d *pos, s32 type, f32 size, s16 room)
{
    if (portEventCount(PEV_PUFF) == 0) return;
    PortEvPuff e;
    v3(e.pos, pos);
    e.type = type;
    e.size = size;
    e.room = room;
    portEventEmit(PEV_PUFF, PEV_AFTER, &e);
}

void portEvImpact(struct coord3d *pos, struct coord3d *normal, s16 type, s16 room, void *prop)
{
    if (portEventCount(PEV_IMPACT) == 0) {
        s_pendingMaterial = PMAT_UNKNOWN;
        s_pendingTri = NULL;
        return;
    }
    PortEvImpact e;
    v3(e.pos, pos);
    v3(e.normal, normal);
    {   /* the game's hit normals are not unit length */
        const float l = e.normal[0] * e.normal[0] + e.normal[1] * e.normal[1] + e.normal[2] * e.normal[2];
        if (l > 1e-8f) {
            const float k = 1.0f / __builtin_sqrtf(l);
            e.normal[0] *= k; e.normal[1] *= k; e.normal[2] *= k;
        }
    }
    e.impact_type = type;
    e.material = s_pendingMaterial != PMAT_UNKNOWN ? s_pendingMaterial : fxMaterialOfType(type);
    {   /* a glass hole stays glass: the game picks 0x11..0x13 for glass objects without reading their texture
         * (propobj.c objHit), whose table material is often the default one */
        const int tm = fxMaterialOfType(type);
        if (tm == PMAT_GLASS || tm == PMAT_GLASS_XLU) e.material = tm;
    }
    e.room = room;
    e.prop = prop;
    e.tricmd = s_pendingTri;
    fxClustersResolveMaterial(&e);   /* the level's material from its hit texture, shared by the holes and the particles */
    {
        extern s32 g_NumImpactEntries;   /* src/game/explosion.c: the slot the impact is written to */
        e.slot = g_NumImpactEntries;
    }
    for (int c = 0; c < 3; c++) e.shade[c] = s_pendingShade[c];
    portEventEmit(PEV_IMPACT, PEV_AFTER, &e);
    s_pendingMaterial = PMAT_UNKNOWN;
    s_pendingTri = NULL;
    s_pendingShade[0] = s_pendingShade[1] = s_pendingShade[2] = -1.0f;
}

void portEvHitChr(void *prop, s32 hitpart, struct coord3d *dirView, struct coord3d *pos)
{
    if (portEventCount(PEV_HIT_CHR) == 0) return;
    PortEvHitChr e;
    v3(e.pos, pos);
    v3(e.dir_view, dirView);
    e.hitpart = hitpart;
    e.prop = prop;
    portEventEmit(PEV_HIT_CHR, PEV_AFTER, &e);
}

/* Nonzero = a subscriber drew this effect kind itself: the game skips its own draw. */
s32 portFxRenderReplaced(s32 kind, Gfx **gdl)
{
    if (portEventCount(PEV_RENDER_FX) == 0) return 0;
    PortEvRenderFx e;
    e.gdl = gdl;
    e.kind = kind;
    return portEventEmit(PEV_RENDER_FX, PEV_BEFORE, &e) == PEV_HANDLED;
}

/* ---- GE_EVLOG=1: a log subscriber --------------------------------------------------------- */

static const char *kMaterialNames[PMAT_COUNT] = {
    "default", "stone", "wood", "metal", "glass", "water", "snow", "dirt", "mud", "tile", "metalobj", "chr", "glass_xlu"
};

static int fxLogEvent(int type, int phase, void *data, void *ctx)
{
    static int budget = 400;   /* a few hundred lines, then quiet */
    (void)phase;
    (void)ctx;
    if (budget <= 0) return PEV_CONTINUE;
    budget--;
    switch (type) {
        case PEV_SHOT: {
            const PortEvShot *e = data;
            sysLogPrintf(LOG_NOTE, "EV shot item=%d from=(%.0f %.0f %.0f) to=(%.0f %.0f %.0f)", e->item,
                         e->from[0], e->from[1], e->from[2], e->to[0], e->to[1], e->to[2]);
            break;
        }
        case PEV_PUFF: {
            const PortEvPuff *e = data;
            sysLogPrintf(LOG_NOTE, "EV puff type=%d size=%.1f room=%d pos=(%.0f %.0f %.0f)", e->type, e->size, e->room,
                         e->pos[0], e->pos[1], e->pos[2]);
            break;
        }
        case PEV_IMPACT: {
            const PortEvImpact *e = data;
            sysLogPrintf(LOG_NOTE, "EV impact type=%d material=%s room=%d prop=%s pos=(%.0f %.0f %.0f) n=(%.2f %.2f %.2f)",
                         e->impact_type, e->material >= 0 ? kMaterialNames[e->material] : "?", e->room,
                         e->prop ? "yes" : "no", e->pos[0], e->pos[1], e->pos[2], e->normal[0], e->normal[1], e->normal[2]);
            break;
        }
        case PEV_HIT_CHR: {
            const PortEvHitChr *e = data;
            sysLogPrintf(LOG_NOTE, "EV hitchr part=%d pos=(%.0f %.0f %.0f) dirview=(%.2f %.2f %.2f)", e->hitpart,
                         e->pos[0], e->pos[1], e->pos[2], e->dir_view[0], e->dir_view[1], e->dir_view[2]);
            break;
        }
        default:
            break;
    }
    return PEV_CONTINUE;
}

PD_CONSTRUCTOR static void fxHooksInit(void)
{
    const char *e = getenv("GE_EVLOG");
    if (e == NULL || e[0] == '0') return;
    portEventSubscribe(PEV_SHOT, PEV_AFTER, 1000, fxLogEvent, NULL);
    portEventSubscribe(PEV_PUFF, PEV_AFTER, 1000, fxLogEvent, NULL);
    portEventSubscribe(PEV_IMPACT, PEV_AFTER, 1000, fxLogEvent, NULL);
    portEventSubscribe(PEV_HIT_CHR, PEV_AFTER, 1000, fxLogEvent, NULL);
}
