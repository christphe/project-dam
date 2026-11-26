/*
 * skyplus.c -- GE+ sky (Video.Visuals = Enhanced), game side. See port/include/skyplus.h.
 *
 * The look is GE's own, with depth added: the middle cloud layer is the
 * original one (same texture, height, scale, scroll and shade formula), two
 * more layers of the same texture sit above and below it, and the level's
 * mood adds a sun (dusk), stars and a moon (night). Every level parameter
 * comes from the game's sky record (bgfog.c); the only port data is the mood
 * table below.
 *
 * Calibration (2026-11-15, Dam in Ryujinx): GE's per-vertex horizon fade, done
 * per pixel, came out ~2.5x brighter than the game shows (GE interpolates it
 * between the screen corners); GAIN scales the fade so the GE layer matches the
 * capture (clouds peaking at ~(96,117,149) over (16,48,96)).
 */
#include <ultra64.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#include "bondconstants.h"
#include "bondtypes.h"
#include "game/bg.h"
#include "game/bgfog.h"
#include "game/bondview.h"
#include "game/player.h"
#include "game/matrixmath.h"
#include "game/image.h"
#include "game/unk_092E50.h"
#include "fr.h"   /* viSetFillColor etc.: the fill of a level without a sky (water-only) */

#include "skyplus.h"
#include "skyplus_core.h"
#include "system.h"

int portSkyPlus = 0;

extern s32 g_SkyStageNum;

/* The level numbers skyplus_core.c uses are the game's. */
_Static_assert(SPC_BUNKER1 == LEVELID_BUNKER1 && SPC_FRIGATE == LEVELID_FRIGATE && SPC_STREETS == LEVELID_STREETS &&
               SPC_COMPLEX == LEVELID_COMPLEX && SPC_EGYPT == LEVELID_EGYPT && SPC_DAM == LEVELID_DAM && SPC_RUNWAY == LEVELID_RUNWAY &&
               SPC_SURFACE == LEVELID_SURFACE && SPC_TEMPLE == LEVELID_TEMPLE && SPC_CAVERNS == LEVELID_CAVERNS &&
               SPC_CITADEL == LEVELID_CITADEL && SPC_CRADLE == LEVELID_CRADLE, "skyplus_core.h level numbers");

#define SPEED 0.5f   /* skyplus_core.c's, for the unwrapped scroll time below */

/* World ray through a point of the player's screen, as skyGetWorldPosFromScreenPos
 * without the WaterConcavity offset (the water fan's, not the camera's). */
static void screenRay(f32 x, f32 y, coord3d *out)
{
    coord2d c;
    c.x = getPlayer_c_screenleft() + x;
    c.y = getPlayer_c_screentop() + y;
    transformAndNormalizeByLength2Dto3D(&c, out, 100.0f);
    mtx4RotateVecInPlace(currentPlayerGetViewToWorldMtxf(), out);
}

static unsigned char s_tex[SKYPLUS_TEX_W * SKYPLUS_TEX_H];
static int s_texGen;
static int s_texNum = -1;
static int s_farHave;      /* the far sea's plane height is latched for this level */
static float s_farY;

/* The cloud image as 8-bit intensity. texLoad writes the loaded address back
 * into the word it is given, so it gets a copy of the image number (as
 * texLoadFromTextureNum does), never skywaterimages[].index. */
static int skyPlusTexture(s32 imageId)
{
    struct tex *t;
    const u8 *src;
    s32 pitch;
    s32 num;

    if (imageId != 0) {
        return 0;   /* every level uses image 0 (bg/clouds, IA8 64x64) */
    }
    num = IMAGE_CLOUDS_GRAYSCALE;
    if (num == s_texNum) {
        return 1;
    }
    texLoadFromTextureNum(num, NULL);
    t = texFindInPool(num, NULL);
    if (t == NULL || t->data == NULL || t->width != SKYPLUS_TEX_W || t->height != SKYPLUS_TEX_H) {
        return 0;
    }
    src = t->data;
    pitch = (t->width + 7) & ~7;   /* texChannelsToPixels IA8 row pitch */
    skyPlusCoreCloudTexture(src, pitch, s_tex);
    s_texNum = num;
    s_texGen++;
    return 1;
}

#if defined(PLATFORM_SWITCH) || defined(GE_VULKAN)
/* GE+ water (Switch, and the Vulkan backend on PC): the sea's wave heights, a tiling fbm (value noise,
 * octaves of 4..32 cells over the tile) made once. GE's own water texture
 * (bg/water1, I4) was tried first: mostly flat with scattered spots, it reads
 * as mud bubbles at any scale (Ryujinx, 2026-11-17). */
static unsigned char s_wtex[SKYPLUS_WTEX_W * SKYPLUS_WTEX_H * 4];
static int s_wtexGen;
static int s_wtexOk = -1;   /* -1 = not made yet */

/* The level's water settings: see skyplus_core.h (defaults per level) and shaders-sea.md ($S/water/<level>.ini keys:
 * Tint, Swell, Chop, Speed, Deep, Light, ReflectLit, PatchScale, ToneKeep, ReflectGain, FarHeight; not case-sensitive). */
#define WaterSettings SkyPlusWaterSettings
#define waterLevelName skyPlusCoreWaterName

static void waterSettingsLoad(int stage, struct WaterSettings *w)
{
    char path[64];
    const char *name = waterLevelName(stage);

    skyPlusCoreWaterDefaults(stage, w);
    if (name == NULL) {
        return;
    }
    snprintf(path, sizeof path, "$S/water/%s.ini", name);
    if (!skyPlusCoreWaterIni(sysResolvePath(path), w)) {
        return;
    }
    sysLogPrintf(LOG_NOTE, "GE+ water: %s (tint %.2f,%.2f,%.2f swell %.2f chop %.2f speed %.2f)", path,
                 w->tint[0], w->tint[1], w->tint[2], w->swell, w->chop, w->speed);
}

/* ---- the level's own water surfaces: colour from the level's water texture ----
 * fast3d calls skyPlusWaterSource (decoder thread) with the decoded texture of the first water draw of a level (TEXEL0 of GE's
 * water combiner). It is enlarged and blurred heavily into a small tiling patch map (brightness stretched to 0..1: light and
 * dark patches), and the colours the shader blends by it are the mean colour of the texture's darker and lighter half. */
#if defined(PLATFORM_SWITCH) || defined(GE_VULKAN)
#define SKYPLUS_WATER 1   /* a backend that draws the level's own water surfaces exists in this build (deko3d, Vulkan, GL 4.6) */
static volatile int s_wbakeWanted;   /* a water level is waiting for its water texture */
static volatile int s_wbaked;
static volatile int s_wmapGen;
static volatile int s_wmapCur;
static unsigned char s_wmap[2][SKYPLUS_WMAP_W * SKYPLUS_WMAP_H * 4];
static float s_wdeep[3], s_wlight[3];
static int s_wsrcW, s_wsrcH, s_wsrcFmt, s_wsrcSiz;

int skyPlusWaterWanted(void)
{
    return s_wbakeWanted;
}

static int wfloor(float v)   /* floor without libm (GE's math.h has no floorf) */
{
    const int i = (int)v;
    return v < (float)i ? i - 1 : i;
}

static void wblur(float *a, float *t, int R)   /* one box blur pass over the wrapping map, both axes */
{
    const int W = SKYPLUS_WMAP_W, H = SKYPLUS_WMAP_H;
    int x, y, k;
    for (y = 0; y < H; y++) {
        for (x = 0; x < W; x++) {
            float sum = 0.0f;
            for (k = -R; k <= R; k++) sum += a[y * W + ((x + k) % W + W) % W];
            t[y * W + x] = sum / (float)(2 * R + 1);
        }
    }
    for (y = 0; y < H; y++) {
        for (x = 0; x < W; x++) {
            float sum = 0.0f;
            for (k = -R; k <= R; k++) sum += t[(((y + k) % H + H) % H) * W + x];
            a[y * W + x] = sum / (float)(2 * R + 1);
        }
    }
}

void skyPlusWaterSource(const unsigned char *rgba, int w, int h, int fmt, int siz)
{
    static float lum[128 * 128], map[SKYPLUS_WMAP_W * SKYPLUS_WMAP_H], tmp[SKYPLUS_WMAP_W * SKYPLUS_WMAP_H];
    const int MW = SKYPLUS_WMAP_W, MH = SKYPLUS_WMAP_H;
    float dsum[3] = { 0, 0, 0 }, lsum[3] = { 0, 0, 0 }, med, lo = 1e9f, hi = -1e9f;
    int n = w * h, nd = 0, nl = 0, i, x, y, nxt;

    if (!s_wbakeWanted || rgba == NULL) {
        return;
    }
    s_wbakeWanted = 0;   /* one try per level */
    if (w < 2 || h < 2 || w > 128 || h > 128) {
        sysLogPrintf(LOG_NOTE, "GE+ water: source texture %dx%d fmt %d siz %d not usable (2..128 wide), GE's colour stays", w, h, fmt, siz);
        return;
    }
    for (i = 0; i < n; i++) {
        lum[i] = (0.299f * rgba[i * 4] + 0.587f * rgba[i * 4 + 1] + 0.114f * rgba[i * 4 + 2]) / 255.0f;
    }
    {   /* the median luminance, from a 256-bin histogram (GE's stdlib.h has no qsort) */
        int hist[256] = { 0 }, acc = 0, b;
        for (i = 0; i < n; i++) hist[lum[i] >= 1.0f ? 255 : (int)(lum[i] * 256.0f)]++;
        for (b = 0; b < 256 && acc + hist[b] <= n / 2; b++) acc += hist[b];
        med = ((float)b + 0.5f) / 256.0f;
    }
    for (i = 0; i < n; i++) {
        float *d = lum[i] < med ? dsum : lsum;
        int j;
        for (j = 0; j < 3; j++) d[j] += rgba[i * 4 + j] / 255.0f;
        if (lum[i] < med) nd++; else nl++;
    }
    for (i = 0; i < 3; i++) {
        s_wdeep[i] = nd ? dsum[i] / (float)nd : lsum[i] / (float)nl;
        s_wlight[i] = nl ? lsum[i] / (float)nl : dsum[i] / (float)nd;
    }
    /* enlarge the whole texture to the map (bilinear, wrapping), then blur it heavily (3 box passes = a gaussian of ~10 % of
     * the map), then stretch min..max */
    for (y = 0; y < MH; y++) {
        for (x = 0; x < MW; x++) {
            const float sx = ((float)x + 0.5f) * (float)w / (float)MW - 0.5f, sy = ((float)y + 0.5f) * (float)h / (float)MH - 0.5f;
            const int x0 = wfloor(sx), y0 = wfloor(sy);
            const float fx = sx - (float)x0, fy = sy - (float)y0;
            const int xa = ((x0 % w) + w) % w, xb = (((x0 + 1) % w) + w) % w, ya = ((y0 % h) + h) % h, yb = (((y0 + 1) % h) + h) % h;
            const float a = lum[ya * w + xa] + (lum[ya * w + xb] - lum[ya * w + xa]) * fx;
            const float b = lum[yb * w + xa] + (lum[yb * w + xb] - lum[yb * w + xa]) * fx;
            map[y * MW + x] = a + (b - a) * fy;
        }
    }
    for (i = 0; i < 3; i++) wblur(map, tmp, 6);
    for (i = 0; i < MW * MH; i++) {
        if (map[i] < lo) lo = map[i];
        if (map[i] > hi) hi = map[i];
    }
    nxt = 1 - s_wmapCur;
    for (i = 0; i < MW * MH; i++) {
        const float t = hi - lo > 1e-4f ? (map[i] - lo) / (hi - lo) : 0.5f;
        const unsigned char v = (unsigned char)(t * 255.0f + 0.5f);
        s_wmap[nxt][i * 4 + 0] = s_wmap[nxt][i * 4 + 1] = s_wmap[nxt][i * 4 + 2] = v;
        s_wmap[nxt][i * 4 + 3] = 255;
    }
    s_wsrcW = w; s_wsrcH = h; s_wsrcFmt = fmt; s_wsrcSiz = siz;
    s_wmapCur = nxt;
    s_wmapGen++;
    s_wbaked = 1;
}
#endif

static int skyPlusWaterTexture(void)
{
    if (s_wtexOk >= 0) {
        return s_wtexOk;
    }
    skyPlusCoreWaveTexture(s_wtex);
    s_wtexGen++;
    s_wtexOk = 1;
    return 1;
}
#endif

#if !(defined(PLATFORM_SWITCH) || defined(GE_VULKAN))
/* No backend draws the level's own water surfaces (a GL-only build): fast3d still calls these.
 * Outside the block above, which is the whole GE+ water section of those builds. */
int skyPlusWaterWanted(void)
{
    return 0;
}

void skyPlusWaterSource(const unsigned char *rgba, int w, int h, int fmt, int siz)
{
    (void)rgba; (void)w; (void)h; (void)fmt; (void)siz;
}
#endif

/* Per-player parameter blocks, double-buffered over frames: fast3d copies the
 * block when it decodes the command, which may be a frame after this call. */
#define SKYPLUS_SLOTS 16
static PortSkyPlus s_blocks[SKYPLUS_SLOTS];
static int s_next;

static float s_lastGeOfs = -1.0f;
static double s_time;   /* continuous GE scroll units (g_SkyCloudOffset unwrapped) */

static float wrapf(float v, float m)
{
    v = fmodf(v, m);
    return v < 0.0f ? v + m : v;
}

/* Bunker 1 has no sky record for one player (bgfog.c: only the 2-4 player
 * ones), so it falls back to fog_tables2's LEVELID_NONE: a flat navy fill, no
 * clouds, seen at its exit and through its windows. The bunker sits under
 * Surface 1 (the mission before it), so GE+ gives it Surface 1's sky: the
 * LEVELID_SURFACE record's sky and cloud colours and height (bgfog.c). Only
 * GE+ sees this copy; fog, draw distance and the game's own sky are unchanged. */
static const struct CurrentEnvironmentRecord *skyPlusEnv(const struct CurrentEnvironmentRecord *env)
{
    static struct CurrentEnvironmentRecord s_bunker1;

    if (g_SkyStageNum != LEVELID_BUNKER1 || env->Clouds || env->IsWater) {
        return env;
    }
    s_bunker1 = *env;
    s_bunker1.Red = 0x60;
    s_bunker1.Green = 0x60;
    s_bunker1.Blue = 0x80;
    s_bunker1.Clouds = 1;
    s_bunker1.CloudRepeat = 10000.0f;
    s_bunker1.SkyImageId = 0;
    s_bunker1.CloudRed = 240.0f;
    s_bunker1.CloudGreen = 120.0f;
    s_bunker1.CloudBlue = 30.0f;
    return &s_bunker1;
}

/* The sun of the last GE+ sky drawn, for the dynamic lights (dynlights.c),
 * which read it once right after the sky of the same view. */
static int s_sunValid;
static float s_sunDir[3], s_sunCol[3];

int skyPlusSun(float dir[3], float col[3])
{
    const int ok = s_sunValid;
    if (ok) {
        memcpy(dir, s_sunDir, sizeof s_sunDir);
        memcpy(col, s_sunCol, sizeof s_sunCol);
    }
    s_sunValid = 0;
    return ok;
}

Gfx *skyPlusRender(Gfx *gdl)
{
    const struct CurrentEnvironmentRecord *env = skyPlusEnv(fogGetCurrentEnvironmentp());
    PortSkyPlus *p;
    s_sunValid = 0;
    coord3d r[5];
    coord3d *eye;
    float fwd[3], len;
    int i;
    int waterOnly = 0;   /* no sky on this level (Caverns): only the water block is set */
#ifdef SKYPLUS_TEST_ORIGINAL
    return NULL;   /* A/B test build: GE's sky whatever the ini says */
#endif
    if (!portSkyPlus || gfx_skyplus_failed) {
        return NULL;
    }
#if !defined(PLATFORM_SWITCH)
    /* PC: only a backend that draws the sea (Vulkan, GL 4.6 set gfx_skyplus_sea) gets the water levels */
    if (env->IsWater && !gfx_skyplus_sea) {
        return NULL;
    }
#endif
    if (!env->Clouds) {
#ifdef SKYPLUS_WATER
        /* no sky: GE+ still gives the level's own water surfaces (Caverns) their water, where a backend draws them; the sky
         * itself stays GE's fill */
        if (!gfx_water_surfaces || waterLevelName(g_SkyStageNum % 100) == NULL) {
            return NULL;
        }
        waterOnly = 1;
#else
        return NULL;
#endif
    }
    if (!waterOnly && !skyPlusTexture(env->SkyImageId)) {
        return NULL;
    }

    p = &s_blocks[s_next];
    s_next = (s_next + 1) % SKYPLUS_SLOTS;
    memset(p, 0, sizeof *p);

    /* corner rays, put on the image plane (distance 1 along the view axis) so
     * the GL pass can interpolate them linearly across the screen */
    screenRay(0.0f, 0.0f, &r[0]);
    screenRay(getPlayer_c_screenwidth(), 0.0f, &r[1]);
    screenRay(0.0f, getPlayer_c_screenheight(), &r[2]);
    screenRay(getPlayer_c_screenwidth(), getPlayer_c_screenheight(), &r[3]);
    screenRay(getPlayer_c_screenwidth() * 0.5f, getPlayer_c_screenheight() * 0.5f, &r[4]);
    len = sqrtf(r[4].x * r[4].x + r[4].y * r[4].y + r[4].z * r[4].z);
    if (len < 1e-6f) {
        return NULL;
    }
    fwd[0] = r[4].x / len;
    fwd[1] = r[4].y / len;
    fwd[2] = r[4].z / len;
    for (i = 0; i < 4; i++) {
        const float d = r[i].x * fwd[0] + r[i].y * fwd[1] + r[i].z * fwd[2];
        if (d < 1e-6f) {
            return NULL;
        }
        p->corner[i][0] = r[i].x / d;
        p->corner[i][1] = r[i].y / d;
        p->corner[i][2] = r[i].z / d;
    }

    eye = bondviewGetCurrentPlayersPosition();
    {
        extern f32 g_ScaledFarFogIntensity;
        struct SkyPlusInput in;

        /* scroll: GE's layer follows g_SkyCloudOffset exactly; the others run at
         * their own speed on the unwrapped time (no jump at the 4096 wrap) */
        if (s_lastGeOfs >= 0.0f) {
            float d = g_SkyCloudOffset - s_lastGeOfs;
            if (d < -2048.0f) d += 4096.0f;
            if (d > 0.0f && d < 2048.0f) s_time += d;
        }
        s_lastGeOfs = g_SkyCloudOffset;
        in.stage = g_SkyStageNum % 100;
        in.red = env->Red; in.green = env->Green; in.blue = env->Blue;
        in.cloudRed = env->CloudRed; in.cloudGreen = env->CloudGreen; in.cloudBlue = env->CloudBlue;
        in.cloudRepeat = env->CloudRepeat;
        in.eye[0] = eye->x; in.eye[1] = eye->y; in.eye[2] = eye->z;
        in.farFog = g_ScaledFarFogIntensity;
        in.time = s_time;
        in.geOffset = g_SkyCloudOffset;
        skyPlusCoreSky(p, &in);
    }

    p->texGen = s_texGen;
    p->tex = waterOnly ? NULL : s_tex;
    if (waterOnly) {   /* the water mirrors the fog colour (the horizon: no sky here), no sun, no glints */
        p->sunGlow = 0.0f;
        p->night = 0;
        p->waterOnly = 1;
    }
    if (p->sunGlow > 0.0f && !p->night && p->sunDir[1] > 0.0f) {   /* a sun above the horizon (not Frigate's set one) */
        memcpy(s_sunDir, p->sunDir, sizeof s_sunDir);
        memcpy(s_sunCol, p->sunCol, sizeof s_sunCol);
        s_sunValid = 1;
    }
#ifdef SKYPLUS_WATER
    p->time = (float)(s_time / 60.0);   /* GE ticks (60 per second) */
    {
        static int s_wsStage = -1;
        static struct WaterSettings s_ws;
        const int stage = g_SkyStageNum % 100;
        if (stage != s_wsStage) {
            waterSettingsLoad(stage, &s_ws);
            s_wsStage = stage;
            s_wbaked = 0;   /* a new level: its water texture is captured again */
            s_wbakeWanted = waterLevelName(stage) != NULL && gfx_water_surfaces;
            s_farHave = 0;
#if defined(PLATFORM_SWITCH)
            if (waterLevelName(stage) != NULL && !env->IsWater && gfx_water_surfaces) {
                if (waterOnly) {
                    sysLogPrintf(LOG_NOTE, "GE+ water: %s: far sea not baked (no sky on this level: the live waves everywhere)", waterLevelName(stage));
                } else {
                    sysLogPrintf(LOG_NOTE, "GE+ water: %s: far sea baked from a fake plane %.0f under the start eye (FarHeight)", waterLevelName(stage), s_ws.farHeight);
                }
            }
#endif
        }
        skyPlusCoreWater(p, &s_ws, env->IsWater, env->WaterRepeat);   /* the sea's and the water draws' fields */
        skyPlusWaterTexture();   /* the wave heights, for the sea and the water draws */
        p->waterTexGen = s_wtexGen;
        p->waterTex = s_wtex;
        p->worldScale = get_room_data_float2();
#if defined(PLATFORM_SWITCH)
        /* the levels with their own water surfaces and a sky (Dam, Complex) get Frigate's baked far sea; Frigate has its own sea,
         * Caverns no sky (its water is live everywhere) and the other levels no water at all (no bake, no cost) */
        p->farBake = (!waterOnly && !env->IsWater && waterLevelName(stage) != NULL && gfx_water_surfaces) ? 1 : 0;
        p->farHeight = s_ws.farHeight;
#endif
        if (s_wbaked) {
            static int s_wlogStage = -1, s_wlogGen = -1;
            for (i = 0; i < 3; i++) {
                p->deep[i] = s_ws.deepSet ? s_ws.deep[i] : s_wdeep[i];
                p->light[i] = s_ws.lightSet ? s_ws.light[i] : s_wlight[i];
            }
            p->wmapGen = s_wmapGen;
            p->wmap = s_wmap[s_wmapCur];
            if (s_wlogStage != stage || s_wlogGen != s_wmapGen) {
                s_wlogStage = stage;
                s_wlogGen = s_wmapGen;
                sysLogPrintf(LOG_NOTE, "GE+ water: %s source texture %dx%d fmt %d siz %d; computed deep %.2f,%.2f,%.2f light %.2f,%.2f,%.2f%s%s; "
                             "used deep %.2f,%.2f,%.2f light %.2f,%.2f,%.2f tint %.2f,%.2f,%.2f reflectLit %.2f patchScale %.1f",
                             waterLevelName(stage), s_wsrcW, s_wsrcH, s_wsrcFmt, s_wsrcSiz,
                             s_wdeep[0], s_wdeep[1], s_wdeep[2], s_wlight[0], s_wlight[1], s_wlight[2],
                             s_ws.deepSet ? " (deep from the ini)" : "", s_ws.lightSet ? " (light from the ini)" : "",
                             p->deep[0], p->deep[1], p->deep[2], p->light[0], p->light[1], p->light[2],
                             s_ws.tint[0], s_ws.tint[1], s_ws.tint[2], s_ws.reflectLit, s_ws.patchScale);
            }
        }
    }
#endif

    {   /* the far sea is baked on the level's first frame, before its sky, from the player's eye at the start (set when the level is
         * set up, before the intro): so no image is ever shown without it, and the intro's flying camera is not where it is baked from */
        extern enum CAMERAMODE g_CameraMode;
        static enum CAMERAMODE prev = CAMERAMODE_NONE;
        const coord3d *start = &g_CurrentPlayer->field_488.pos;
        p->bakeReady = g_CameraMode != CAMERAMODE_NONE && g_CameraMode != CAMERAMODE_FADE_TO_TITLE;
        p->bakeReset = g_CameraMode == CAMERAMODE_INTRO && prev != CAMERAMODE_INTRO;
        prev = g_CameraMode;
        p->bakeEye[0] = start->x; p->bakeEye[1] = start->y; p->bakeEye[2] = start->z;
        if (p->farBake) {   /* the fake plane's height is fixed from the level's start (the bake's key must not move with the player) */
            if (p->bakeReset || !s_farHave) {
                s_farY = start->y - p->farHeight;
                s_farHave = 1;
            }
            p->farSeaY = s_farY;
        }
    }
    gSPSkyPlusExt(gdl++, p);
    if (waterOnly) {
        /* what sky.c does for a level without clouds: fill the view with the sky colour */
        if (GE_AI_PLAYERS() == 1) {
            gDPSetCycleType(gdl++, G_CYC_FILL);
            gdl = viSetFillColor(gdl, env->Red, env->Green, env->Blue);
            gDPFillRectangle(gdl++, viGetViewLeft(), viGetViewTop(),
                    viGetViewLeft() + viGetViewWidth() - 1,
                    viGetViewTop() + viGetViewHeight() - 1);
            gDPPipeSync(gdl++);
            return gdl;
        }
        gDPPipeSync(gdl++);
        gDPSetCycleType(gdl++, G_CYC_FILL);
        gDPSetRenderMode(gdl++, G_RM_NOOP, G_RM_NOOP2);
        gDPFillRectangle(gdl++,
                g_CurrentPlayer->viewleft, g_CurrentPlayer->viewtop,
                (g_CurrentPlayer->viewleft + g_CurrentPlayer->viewx) - 1,
                (g_CurrentPlayer->viewtop + g_CurrentPlayer->viewy) - 1);
        gDPPipeSync(gdl++);
    }
    return gdl;
}
