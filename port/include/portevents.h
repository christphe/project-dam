#ifndef PORTEVENTS_H
#define PORTEVENTS_H

/*
 * portevents.h -- the port's game event bus (project-dam, RULE-2-SIGNOFF
 * 2026-11-23, docs/dev/findings/2026-11-23-rule-2-signoff-fx-markers.md).
 *
 * The game raises an event from a one-line GE_EV_* / GE_FX_* hook macro
 * (src/game/player.h, same convention as the co-op GE_* hooks of D960); port
 * code subscribes to it. Pure C, no game types: payloads use plain floats.
 *
 * Phases: PEV_BEFORE runs before the game's own action, PEV_AFTER after it.
 *  - Action: a subscriber reads the payload and returns PEV_CONTINUE.
 *  - Filter: a subscriber may change the payload; later subscribers and the
 *    game see the change (the payload is passed by pointer, in order).
 *  - Replace (PEV_BEFORE only): a subscriber returns PEV_HANDLED; the chain
 *    stops and the emitter skips the game's own action. Used for rendering
 *    only (a replaced game action that is not a draw needs its own rule-2
 *    sign-off).
 * Order: ascending priority, then subscription order. Subscribers run on the
 * emitting (game) thread and must not block.
 */

enum PortEventType {
    PEV_SHOT = 0,    /* PortEvShot: a tracer / beam starts (player, guard, autogun) */
    PEV_PUFF,        /* PortEvPuff: an impact puff is created (bullet_spark_create) */
    PEV_IMPACT,      /* PortEvImpact: a bullet hits a surface (explosionCreateBulletImpact) */
    PEV_HIT_CHR,     /* PortEvHitChr: a bullet hits a character (chrCreateHitPuffs) */
    PEV_RENDER_FX,   /* PortEvRenderFx: the game is about to draw one effect kind (replaceable) */
    PEV_COUNT
};

enum PortEventPhase {
    PEV_BEFORE = 0,
    PEV_AFTER = 1,
    PEV_PHASES
};

#define PEV_CONTINUE 0
#define PEV_HANDLED  1

#define PEV_MAX_SUBSCRIBERS 32

typedef int (*PortEventFn)(int type, int phase, void *data, void *ctx);

/* Returns a handle (>= 0) for portEventUnsubscribe, or -1 when the table is full / the arguments are bad. */
int portEventSubscribe(int type, int phase, int priority, PortEventFn fn, void *ctx);
void portEventUnsubscribe(int handle);

/* Runs the subscribers of (type, phase) in order. Returns PEV_HANDLED when a PEV_BEFORE subscriber
 * handled the event (the chain stopped there), PEV_CONTINUE otherwise. */
int portEventEmit(int type, int phase, void *data);

/* Number of subscribers to (type, any phase): hooks skip building the payload when it is 0. */
int portEventCount(int type);

/* Drops every subscriber (tests). */
void portEventReset(void);

/* Payloads. Positions and directions are world space unless the field says otherwise. */
typedef struct PortEvShot {
    float from[3], to[3];
    int item;             /* ITEM_IDS of the weapon */
    void *beam;           /* the game's BeamRecord */
} PortEvShot;

typedef struct PortEvPuff {
    float pos[3];
    int type;             /* g_BulletSparkColors index: 1 dust, 2 blood (Game.BloodOnHit), 3 scatter, 4 flare */
    float size;
    int room;
} PortEvPuff;

enum PortMaterial {      /* g_HitTypeSounds order (src/game/tex.c) */
    PMAT_UNKNOWN = -1,
    PMAT_DEFAULT = 0, PMAT_STONE, PMAT_WOOD, PMAT_METAL, PMAT_GLASS, PMAT_WATER, PMAT_SNOW,
    PMAT_DIRT, PMAT_MUD, PMAT_TILE, PMAT_METALOBJ, PMAT_CHR, PMAT_GLASS_XLU,
    PMAT_COUNT
};

typedef struct PortEvImpact {
    float pos[3], normal[3];
    int impact_type;      /* g_ImpactTypes index (the decal) */
    int material;         /* PortMaterial, from the impact type (PMAT_UNKNOWN when ambiguous) */
    int room;
    void *prop;           /* the prop hit, NULL for the level */
    /* the surface, when the call site knew it (GE_EV_SURFACE), else tricmd NULL and shade -1 */
    const void *tricmd;   /* the room display-list command (Gfx *) that drew the hit triangle */
    float shade[3];       /* the hit triangle's baked light (its vertex colours' average, 0..1) */
    int slot;             /* the game's bullet-hole slot this impact takes (g_BulletImpactBuffer) */
} PortEvImpact;

typedef struct PortEvHitChr {
    float pos[3];
    float dir_view[3];    /* the shot direction, view space (as chrCreateHitPuffs receives it) */
    int hitpart;          /* HIT_* body part */
    void *prop;           /* the character's prop */
} PortEvHitChr;

typedef struct PortEvRenderFx {
    void *gdl;            /* Gfx ** of the game's display list: a replacing subscriber appends to it */
    int kind;             /* PORTFX_* (portfx.h) */
} PortEvRenderFx;

#endif
