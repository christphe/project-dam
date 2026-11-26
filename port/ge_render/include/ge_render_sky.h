#ifndef GE_RENDER_SKY_H
#define GE_RENDER_SKY_H

/* The GE+ sky/sea/water pass descriptor: what the game side (port/src/skyplus.c) hands the renderer for one
 * fullscreen sky draw, through the backends' skyplus_stage / skyplus_pass (ge_render.h, GeRenderPasses).
 * A plain data block with no dependency on the game: a tool can fill one too. See port/include/skyplus.h
 * for how the game builds it. */

#define SKYPLUS_TEX_W 64
#define SKYPLUS_TEX_H 64
#define SKYPLUS_WTEX_W 128
#define SKYPLUS_WTEX_H 128
#define SKYPLUS_WMAP_W 64    /* the level's water colour map (the patches that blend deep into light) */
#define SKYPLUS_WMAP_H 64

typedef struct PortSkyPlus {
    float corner[4][3];   /* world-space rays to the image plane: TL, TR, BL, BR (not normalised) */
    float eye[3];
    float cloudY;         /* the cloud shell's height at the eye (world y) */
    float sky[3];         /* sky = fog = horizon colour, 0..1 */
    float cloud[3];       /* CloudRed/Green/Blue, 0..1 */
    float geOffset;       /* g_SkyCloudOffset (Vtx.tc S10.5 units, wraps at 4096) */
    float layerOfs[3][2]; /* per-layer texture scroll in texels, wrapped (layer 1 = GE) */
    float gain;           /* GE horizon-fade calibration (see skyplus.c) */
    float texScale;       /* world -> texture scale factor */
    float sunDir[3];
    float sunCol[3];
    float sunGlow;        /* 0 = no sun */
    float moonDir[3];
    int night;            /* stars + moon */
    float band;           /* top of the horizon fog band, sin(elevation), from the level's fog distance */
    int texGen;           /* bumps when tex changes */
    const unsigned char *tex;   /* SKYPLUS_TEX_W x SKYPLUS_TEX_H intensity, 0..255 */
    /* GE+ water (Switch): the sea of a water level, drawn by this pass under
     * the horizon, and the time that animates the waves of every water
     * surface (the level's own water draws use the same block, gfx_deko3d.cpp) */
    int sea;              /* 1 = draw the sea under the horizon (env->IsWater) */
    float seaY;           /* the sea's height (world y, the record's WaterRepeat) */
    float water[3];       /* WaterRed/Green/Blue, 0..1 */
    float time;           /* wave time, seconds of game time */
    float swell, chop, speed;   /* the level's water settings (skyplus.c, $S/water/<level>.ini) */
    int bakeReady;        /* 1: a level is running: the far sea may be baked */
    int bakeReset;        /* 1 on the first frame of a level's intro (a new level, a restart): the bake is redone */
    float bakeEye[3];     /* where the far sea is baked from: the player's eye at the start (not the intro's flying camera) */
    int waterTexGen;      /* bumps when waterTex changes */
    const unsigned char *waterTex;   /* SKYPLUS_WTEX_W x SKYPLUS_WTEX_H RGBA8 (a tiling fbm noise, the wave heights) */
    /* the level's own water surfaces (Dam, Caverns, Complex; Switch): their colour comes from the level's water texture,
     * baked once per level (skyplus.c skyPlusWaterSource) into a small tiling patch map (R = 0 deep .. 1 light) */
    int wmapGen;          /* bumps when wmap changes */
    const unsigned char *wmap;   /* SKYPLUS_WMAP_W x SKYPLUS_WMAP_H RGBA8, NULL until the bake is done */
    float deep[3], light[3];     /* the two colours the patch map blends, 0..1 */
    float reflectLit;     /* 0..1: how much the vertex lighting dims the water's reflection (1 = GE's rule) */
    float patchScale;     /* the patch map's size in water-texture repeats */
    float toneKeep;       /* 0..1: how much the reflection is pulled into the body's hue (keeps the original's tone) */
    float reflectGain;    /* the reflection's weight, x the Fresnel */
    float worldScale;     /* bg.c room_data_float2 (1 / the level's scale): turns the water draws' depth into the eye's units */
    int waterOnly;        /* 1: no sky (a level without one: Caverns): this command only sets the water block, nothing is drawn */
    /* Switch: the far sea baked for the level's own water surfaces (Dam, Complex; #79): the same panorama as Frigate's sea, of a
     * plane farHeight under the player's start eye (a fixed height: far away the height does not show), read by the water draws
     * beyond the near distance. farSeaY = that plane's world height, kept from the level's start so the bake is not redone. */
    int farBake;
    float farHeight, farSeaY;
} PortSkyPlus;

#endif
