#ifndef PORT_SKYPLUS_H
#define PORT_SKYPLUS_H

/* GE+ sky (Video.Visuals = Enhanced): a per-pixel replacement for sky.c's cloud plane.
 *
 * The game side (port/src/skyplus.c, called from the top of skyRender) fills a
 * PortSkyPlus with the level's sky record, the camera (the world rays through
 * the four corners of the player's screen, from the game's own
 * skyGetWorldPosFromScreenPos math) and the cloud texture, then emits one
 * G_SKYPLUS_EXT command. fast3d (gfx_pc.cpp) copies it with the current
 * viewport and aspect mapping and records a GL pass (gfx_opengl.cpp) that
 * draws the whole viewport in one fullscreen triangle:
 *   - layer 1 = GE's own cloud layer (same height, texture scale, scroll and
 *     shade formula: sky + cloud * (1 - sky) * (1 - frac));
 *   - layer 0 = the same texture higher, larger, slower, rotated, fainter;
 *   - layer 2 = lower, smaller, faster, fainter;
 *   - a light shading toward the sun/moon, a sun glow on dusk levels, stars
 *     and a moon on night levels, both hidden by the clouds in front of them.
 * Below the horizon the sky colour, as GE's clear colour.
 *
 * w0 = G_SKYPLUS_EXT << 24, w1 = const PortSkyPlus *. */
#define G_SKYPLUS_EXT 0x48

#include "ge_render_sky.h"   /* PortSkyPlus and the SKYPLUS_* sizes moved into the renderer library */
#include "ge_render_cfg.h"   /* ge_render_cfg: the sky/water flags below (no <stdbool.h>: this header reaches the decomp's C files) */

#define gSPSkyPlusExt(pkt, ptr)                                       \
    do {                                                              \
        Gfx *_g = (Gfx *)(pkt);                                       \
        _g->words.w0 = ((uintptr_t)G_SKYPLUS_EXT << 24);              \
        _g->words.w1 = GBI_W1(ptr);                                   \
    } while (0)

#ifdef __cplusplus
extern "C" {
#endif

/* Video.Visuals = Enhanced (0 = GE's sky, 1 = GE+). */
extern int portSkyPlus;

#ifndef __cplusplus
/* sky.c: returns the advanced gdl when GE+ drew the sky, NULL to run GE's path
 * (option off, no clouds, a water level, an unknown cloud image). */
Gfx *skyPlusRender(Gfx *gdl);
#endif

/* Switch: fast3d hands the decoded water texture of the level's own water draws (GE's water combiner, TEXEL0) to the
 * game side while it is wanted (once per level). rgba = w x h RGBA8; fmt/siz = the N64 tile format (for the log). */
int skyPlusWaterWanted(void);
/* Dynamic lights: the sun of the GE+ sky just drawn for this view (world
 * space, toward the sun; colour 0..1). 0 when that sky has no sun above the
 * horizon or GE+ drew no sky. Consumes it: one read per view. */
int skyPlusSun(float dir[3], float col[3]);
void skyPlusWaterSource(const unsigned char *rgba, int w, int h, int fmt, int siz);

/* The renderer's sky/water flags live in its config (ge_render.h, ge_render_cfg); these names are the game's. */
#define gfx_skyplus_failed (ge_render_cfg.skyplus_failed)   /* set by the GL thread when the GE+ program fails to build: GE's sky is drawn instead */
#define gfx_skyplus_half (ge_render_cfg.skyplus_half)       /* PC, Video.SkyScale: 1 = the sky and sea passes drawn at half size (the Switch always does) */
#define gfx_water_surfaces (ge_render_cfg.water_surfaces)   /* 1 = the active backend draws the GE+ water on the level's own water surfaces (water.frag; deko3d, Vulkan, GL 4.6) */
#define gfx_skyplus_sea (ge_render_cfg.skyplus_sea)         /* PC: 1 = the active backend draws the GE+ sea (Vulkan); the Switch always does */

#ifdef __cplusplus
}
#endif

#endif
