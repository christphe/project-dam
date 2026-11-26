#ifndef GE_RENDER_CFG_H
#define GE_RENDER_CFG_H

/* ge_render's configuration struct, in a header of its own with no <stdbool.h>: game C files (the decomp's headers make
 * `bool` a typedef of their own) can read the renderer's flags without pulling the whole API in. ge_render.h includes it. */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* What the host asks of a window (ge_render_open; ignored by deko3d, whose nwindow is the screen). */
struct GfxWindowInitSettings {
    const char *title;
    uint32_t width;     /* 0: pick from the desktop (about 85% of it, 4:3) */
    uint32_t height;
    int32_t x;
    int32_t y;
    int fullscreen;
    int fullscreen_is_exclusive;
    int maximized;
    int centered;
    int allow_hidpi;
};

typedef enum GeRenderBackend {
    GE_RENDER_GL = 0,
    GE_RENDER_VULKAN = 1,   /* GE_VULKAN builds */
    GE_RENDER_DEKO3D = 2,   /* the Switch */
} GeRenderBackend;

typedef struct GeRenderConfig {
    /* Fixed at ge_render_open. */
    GeRenderBackend backend;   /* Vulkan falls back to GL when no usable driver (ge_render_backend() tells) */
    struct GfxWindowInitSettings window;   /* ignored by deko3d (the nwindow is the screen) */

    /* Live: the host writes them, any time. */
    int fxaa_on;               /* FXAA post pass */
    int per_pixel_fog;         /* 1 = fog from the pixel's own depth, 0 = the RSP's per-vertex fog (the N64). Read at init;
                                  fog_vertex below is what follows it live */
    int display_fixes;         /* the game's display fixes are on (Video.Visuals >= Fixed; the translator reads it) */
    int skyplus_half;          /* GE+ sky and sea pass size: 0 full, 1 half, 2 quarter */

    /* Shared with the translator (fast3d) and the backends: the backend sets them at init, the host may change them. */
    int rdp_affine;            /* 1 = RDP-style screen-affine shade colour (the backend decides at init) */
    int fog_vertex;            /* 1 = per-vertex RSP fog, 0 = per-pixel; initialised from per_pixel_fog */
    int framebuffers_enabled; /* off-screen framebuffers exist (a backend that has none clears it at init) */

    /* Reported by the backends, read by the game. */
    volatile int skyplus_failed;   /* the GE+ sky program did not build: the game draws GE's own sky */
    volatile int skyplus_sea;      /* the backend draws the GE+ sea (Vulkan; the Switch always does) */
    volatile int water_surfaces;   /* the backend draws the GE+ water on the level's own water surfaces */
} GeRenderConfig;

/* The live config. Statically initialised (framebuffers_enabled = true, the rest 0); ge_render_open copies the
 * host's struct over it, so a host edits this one before open (config load) and passes it back with the choices. */
extern GeRenderConfig ge_render_cfg;

#ifdef __cplusplus
}
#endif

#endif
