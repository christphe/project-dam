#ifndef PORTFX_H
#define PORTFX_H

/*
 * portfx.h -- render-only effect markers (project-dam, RULE-2-SIGNOFF
 * 2026-11-23, docs/dev/findings/2026-11-23-rule-2-signoff-fx-markers.md).
 *
 * The game draws its muzzle flashes, tracers, impact puffs, blood puffs and
 * bullet-hole decals with the same generic RDP state as everything else, so
 * the renderer cannot tell them apart. Under #ifdef PORT the game brackets
 * those draws with a tagged G_NOOP (gDPNoOpTag): the N64 RSP ignores the
 * command and the game never emits G_NOOP otherwise (finding B1), so the
 * markers change no game state and no N64 behaviour. fast3d reads the tag
 * and applies it to the triangles until the end marker.
 *
 * Tag layout (w1): PORTFX_TAG_MAGIC | kind. kind PORTFX_NONE ends a span.
 */

#define PORTFX_TAG_MAGIC 0x50460000u   /* "PF" in the high half */
#define PORTFX_TAG_MASK  0xFFFF0000u

enum {
    PORTFX_NONE = 0,
    PORTFX_MUZZLE,   /* 1P gun-model flash (Switches[1]) and chr GUNFIRE nodes */
    PORTFX_TRACER,   /* BeamRecord tracers: player, guards, autoguns */
    PORTFX_SPARK,    /* bullet impact puffs (wall / gun / hat) */
    PORTFX_BLOOD,    /* the Game.BloodOnHit body puff (puff type 2) */
    PORTFX_IMPACT,   /* the game's bullet holes on props (the level's are clusters): port/shaders/fx/impacts.glsl fxImpact */
    PORTFX_TRACER_1P,/* the player's own tracer, drawn in the gun pass (render-replace hook only, no marker) */
    PORTFX_CLUSTER,      /* 7: a bullet-hole cluster (fxparticles.c, fxcluster.h): port/shaders/fx/impacts.glsl fxClusterPom */
    PORTFX_COUNT
};

#define gDPPortFxBegin(pkt, kind) gDPNoOpTag(pkt, PORTFX_TAG_MAGIC | (u32)(kind))
#define gDPPortFxEnd(pkt)         gDPNoOpTag(pkt, PORTFX_TAG_MAGIC | (u32)PORTFX_NONE)

/* src/game/model.c (PORT): while set, subdraw() brackets this node's subtree
 * (the 1P gun model's flash switch) with PORTFX_MUZZLE. Set and cleared
 * around the hand-weapon subdraw in src/game/gunfire.c. */
extern void *g_PortFxMuzzleNode;

/* port/src/fxparticles.c: the effects at run time (1 on), F9 toggles them (port/src/video.c) so the
 * game's own look and the effects compare at the same spot. Read by fast3d and the particles. */
extern int portFxUserOn;
void portFxToggle(void);

#endif
