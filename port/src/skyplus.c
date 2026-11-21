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
#include "game/bgfog.h"
#include "game/bondview.h"
#include "game/player.h"
#include "game/matrixmath.h"
#include "game/image.h"
#include "game/unk_092E50.h"
#include "fr.h"   /* viSetFillColor etc.: the fill of a level without a sky (water-only) */

#include "skyplus.h"
#include "system.h"

int portSkyPlus = 0;

extern s32 g_SkyStageNum;

#define GAIN 0.4f
/* One cloud height and size for every level (2026-11-15, console + Ryujinx):
 * tuned on Dam, where GE's 5000 read too low and too fast and 3x that, with
 * the scroll halved, was approved. With the record's own height (Surface:
 * 10000, then x3) the same texture repeated visibly. For a flat layer only the
 * height/texture ratio is visible, so the record's height is not used. */
#define CLOUD_HEIGHT 15000.0f
#define SPEED 0.5f

enum { MOOD_DAY, MOOD_DUSK, MOOD_EXOTIC, MOOD_NIGHT, MOOD_OVERCAST };

/* Hand-set from the colours of each level's record (see the sky sheet). */
static int skyPlusMood(const struct CurrentEnvironmentRecord *env)
{
    const float lum = (0.30f * env->Red + 0.59f * env->Green + 0.11f * env->Blue) / 255.0f;

    switch (g_SkyStageNum % 100) {
    case LEVELID_DAM:
    case LEVELID_FRIGATE:
        return MOOD_DAY;
    case LEVELID_EGYPT:
        return lum < 0.02f ? MOOD_NIGHT : MOOD_DAY;   /* the alt record is black */
    case LEVELID_STREETS:
    case LEVELID_SURFACE:
    case LEVELID_BUNKER1:   /* Surface 1's sky, see skyPlusEnv */
    case LEVELID_TEMPLE:
    case LEVELID_CRADLE:
        return MOOD_DUSK;
    case LEVELID_COMPLEX:
    case LEVELID_CITADEL:
        return MOOD_EXOTIC;
    case LEVELID_RUNWAY:
        return MOOD_OVERCAST;
    default:
        return lum < 0.10f ? MOOD_NIGHT : MOOD_DAY;
    }
}

static void dirFrom(float elDeg, float az, float out[3])
{
    const float e = elDeg * (float)M_PI / 180.0f;
    out[0] = cosf(e) * sinf(az);
    out[1] = sinf(e);
    out[2] = cosf(e) * cosf(az);
}

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

/* The cloud image as 8-bit intensity. texLoad writes the loaded address back
 * into the word it is given, so it gets a copy of the image number (as
 * texLoadFromTextureNum does), never skywaterimages[].index. */
static int skyPlusTexture(s32 imageId)
{
    struct tex *t;
    const u8 *src;
    s32 x, y, pitch;
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
    for (y = 0; y < SKYPLUS_TEX_H; y++) {
        for (x = 0; x < SKYPLUS_TEX_W; x++) {
            s_tex[y * SKYPLUS_TEX_W + x] = (unsigned char)((src[y * pitch + x] >> 4) * 17);   /* I of I4A4 */
        }
    }
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
#define WTEX_SLOPE_K 6.0f   /* slope scale in G, B (skycommon.glsl texSlopeAt decodes it) */

static float wnoiseLattice(int x, int y, int n)
{
    unsigned h = (unsigned)(((x % n + n) % n) * 374761393u + ((y % n + n) % n) * 668265263u + (unsigned)n * 2246822519u);
    h = (h ^ (h >> 13)) * 1274126177u;
    return (float)((h ^ (h >> 16)) & 0xffff) / 65535.0f;
}

static float wnoise(float x, float y, int n)   /* x, y in 0..n cells, wraps at n */
{
    const int ix = (int)x, iy = (int)y;   /* x, y >= 0 */
    float fx = x - (float)ix, fy = y - (float)iy;
    float a, b, c, d;
    fx = fx * fx * (3.0f - 2.0f * fx);
    fy = fy * fy * (3.0f - 2.0f * fy);
    a = wnoiseLattice(ix, iy, n);
    b = wnoiseLattice(ix + 1, iy, n);
    c = wnoiseLattice(ix, iy + 1, n);
    d = wnoiseLattice(ix + 1, iy + 1, n);
    return (a + (b - a) * fx) + ((c + (d - c) * fx) - (a + (b - a) * fx)) * fy;
}

/* The level's water settings: tint (x the water's colour), swell (the big
 * waves), chop (the small ones, and how much they blur the far reflections),
 * speed. Defaults below; $S/water/<level>.ini overrides them, read when the
 * level changes, so they can be tuned on the SD card without a rebuild:
 *     Tint = 1.0, 1.0, 1.0
 *     Swell = 1.0
 *     Chop = 1.0
 *     Speed = 1.0
 * and for the level's own water surfaces (Dam, Caverns, Complex), see shaders-sea.md:
 *     Deep = 0.1, 0.2, 0.2        (0..1, default: the mean colour of the water texture's darker half)
 *     Light = 0.3, 0.5, 0.5       (default: the mean colour of its lighter half)
 *     ReflectLit = 0.0            (0..1, 1 = the vertex lighting dims the reflection as GE's does)
 *     PatchScale = 6.0            (the patch map's size in water-texture repeats)
 * Keys are not case-sensitive.                                                 */
struct WaterSettings {
    float tint[3], swell, chop, speed;
    /* the level's own water surfaces: the two colours the patch map blends (when not set here they are computed from the
     * level's water texture), the vertex lighting's weight on the reflection, the patch map's size in texture repeats */
    float deep[3], light[3];
    int deepSet, lightSet;
    float reflectLit, patchScale;
    float toneKeep, reflectGain;   /* the reflection pulled into the body's hue (0..1), and its strength */
};

static const char *waterLevelName(int stage)
{
    switch (stage) {
    case LEVELID_DAM:     return "dam";
    case LEVELID_FRIGATE: return "frigate";
    case LEVELID_CAVERNS: return "caverns";
    case LEVELID_COMPLEX: return "complex";
    default:              return NULL;
    }
}

static int keyeq(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
    }
    return *a == *b;
}

static void waterSettingsLoad(int stage, struct WaterSettings *w)
{
    char path[64], line[128];
    const char *name = waterLevelName(stage);
    FILE *f;

    w->tint[0] = w->tint[1] = w->tint[2] = 1.0f;
    w->swell = w->chop = w->speed = 1.0f;
    w->deepSet = w->lightSet = 0;
    w->deep[0] = w->deep[1] = w->deep[2] = w->light[0] = w->light[1] = w->light[2] = 0.0f;
    w->reflectLit = 1.0f;   /* unknown levels: GE's rule */
    w->patchScale = 6.0f;
    w->toneKeep = 0.0f;
    w->reflectGain = 1.0f;
    if (stage == LEVELID_FRIGATE) {   /* open sea: a real swell, not an oil-flat sea */
        w->swell = 2.0f;
        w->chop = 1.2f;
    } else if (stage == LEVELID_DAM) {   /* a lake: calm */
        w->swell = 0.3f;
        w->chop = 0.6f;
        w->speed = 0.7f;
        w->reflectLit = 0.0f;   /* Dam's water is lit dark: the reflection is not dimmed by it */
        w->toneKeep = 0.8f;     /* the original's dark teal: the sky reflection keeps the body's hue */
        w->reflectGain = 0.6f;  /* measured on the PC (intro camera 1): mean water colour 15,32,39 vs GE's 12,25,29 */
    } else if (stage == LEVELID_CAVERNS) {   /* still cave water, dark, no sky to mirror */
        w->swell = 0.3f;
        w->chop = 0.6f;
        w->speed = 0.7f;
        /* the water texture alone (TEXEL0, a dark I4) is ~half the tone of GE's lerp with TEXEL1: x2 brings the mean back to
         * the original's (measured on the PC, intro camera 1: 10,14,14 vs GE's 10,12.5,12.6); the reflection (the dark
         * fog colour) at half weight, in the water's own hue */
        w->tint[0] = w->tint[1] = w->tint[2] = 2.0f;
        w->reflectLit = 0.0f;
        w->toneKeep = 0.8f;
        w->reflectGain = 0.5f;
    }   /* Complex: reflectLit 1 keeps its pits black */
    if (name == NULL) {
        return;
    }
    snprintf(path, sizeof path, "$S/water/%s.ini", name);
    f = fopen(sysResolvePath(path), "r");
    if (f == NULL) {
        return;
    }
    while (fgets(line, sizeof line, f)) {
        char key[32];
        float a, b, c;
        if (sscanf(line, " %31[A-Za-z] = %f , %f , %f", key, &a, &b, &c) == 4) {
            if (keyeq(key, "Tint")) { w->tint[0] = a; w->tint[1] = b; w->tint[2] = c; }
            else if (keyeq(key, "Deep")) { w->deep[0] = a; w->deep[1] = b; w->deep[2] = c; w->deepSet = 1; }
            else if (keyeq(key, "Light")) { w->light[0] = a; w->light[1] = b; w->light[2] = c; w->lightSet = 1; }
        } else if (sscanf(line, " %31[A-Za-z] = %f", key, &a) == 2) {
            if (keyeq(key, "Swell")) w->swell = a;
            else if (keyeq(key, "Chop")) w->chop = a;
            else if (keyeq(key, "Speed")) w->speed = a;
            else if (keyeq(key, "ReflectLit")) w->reflectLit = a < 0.0f ? 0.0f : a > 1.0f ? 1.0f : a;
            else if (keyeq(key, "ToneKeep")) w->toneKeep = a < 0.0f ? 0.0f : a > 1.0f ? 1.0f : a;
            else if (keyeq(key, "ReflectGain")) w->reflectGain = a < 0.0f ? 0.0f : a > 2.0f ? 2.0f : a;
            else if (keyeq(key, "PatchScale")) w->patchScale = a < 0.5f ? 0.5f : a > 64.0f ? 64.0f : a;
        }
    }
    fclose(f);
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
    if (getenv("GE_SEADUMP")) {   /* dev: the source water texture for tools_pc/seaview (ROM-derived: stays in its data/) */
#if defined(PLATFORM_SWITCH)
        FILE *df = fopen("sdmc:/switch/project-dam/data/water_src.bin", "wb");
#else
        FILE *df = fopen("data/water_src.bin", "wb");
#endif
        if (df) { fwrite(&w, sizeof w, 1, df); fwrite(&h, sizeof h, 1, df); fwrite(rgba, 4, (size_t)n, df); fclose(df); }
    }
}
#else
int skyPlusWaterWanted(void)
{
    return 0;
}

void skyPlusWaterSource(const unsigned char *rgba, int w, int h, int fmt, int siz)
{
    (void)rgba; (void)w; (void)h; (void)fmt; (void)siz;
}
#endif

static int skyPlusWaterTexture(void)
{
    float lo = 1e9f, hi = -1e9f;
    static float v[SKYPLUS_WTEX_W * SKYPLUS_WTEX_H];
    s32 x, y, o;

    if (s_wtexOk >= 0) {
        return s_wtexOk;
    }
    for (y = 0; y < SKYPLUS_WTEX_H; y++) {
        for (x = 0; x < SKYPLUS_WTEX_W; x++) {
            float sum = 0.0f, amp = 1.0f;
            int n = 4;
            for (o = 0; o < 4; o++, n *= 2, amp *= 0.5f) {
                sum += amp * wnoise((float)x * n / SKYPLUS_WTEX_W, (float)y * n / SKYPLUS_WTEX_H, n);
            }
            v[y * SKYPLUS_WTEX_W + x] = sum;
            if (sum < lo) lo = sum;
            if (sum > hi) hi = sum;
        }
    }
    /* R = height; G, B = its slope to the next texel in x, y (wrapping),
     * precomputed so the shader reads one texel per sample, not three (GPU
     * cost on the console: 36 fetches per sea pixel were too many) */
    for (x = 0; x < SKYPLUS_WTEX_W * SKYPLUS_WTEX_H; x++) {
        v[x] = (v[x] - lo) / (hi - lo + 1e-6f);
    }
    for (y = 0; y < SKYPLUS_WTEX_H; y++) {
        for (x = 0; x < SKYPLUS_WTEX_W; x++) {
            const float h = v[y * SKYPLUS_WTEX_W + x];
            const float dx = v[y * SKYPLUS_WTEX_W + (x + 1) % SKYPLUS_WTEX_W] - h;
            const float dy = v[((y + 1) % SKYPLUS_WTEX_H) * SKYPLUS_WTEX_W + x] - h;
            unsigned char *o = &s_wtex[(y * SKYPLUS_WTEX_W + x) * 4];
            float g = 127.5f + dx * WTEX_SLOPE_K * 255.0f, b = 127.5f + dy * WTEX_SLOPE_K * 255.0f;
            o[0] = (unsigned char)(h * 255.0f + 0.5f);
            o[1] = (unsigned char)(g < 0.0f ? 0.0f : g > 255.0f ? 255.0f : g + 0.5f);
            o[2] = (unsigned char)(b < 0.0f ? 0.0f : b > 255.0f ? 255.0f : b + 0.5f);
            o[3] = 255;
        }
    }
    s_wtexGen++;
    s_wtexOk = 1;
    return 1;
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

Gfx *skyPlusRender(Gfx *gdl)
{
    const struct CurrentEnvironmentRecord *env = skyPlusEnv(fogGetCurrentEnvironmentp());
    PortSkyPlus *p;
    coord3d r[5];
    coord3d *eye;
    float fwd[3], len;
    int i, mood;
    int waterOnly = 0;   /* no sky on this level (Caverns): only the water block is set */
    /* per extra layer: wind speed and size relative to the GE layer */
    static const float kSpeed[3] = { 0.35f, 1.0f, 1.9f };
    static const float kScale[3] = { 1.7f, 1.0f, 0.45f };
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
    p->eye[0] = eye->x;
    p->eye[1] = eye->y;
    p->eye[2] = eye->z;
    p->cloudY = eye->y + CLOUD_HEIGHT;
    p->sky[0] = env->Red / 255.0f;
    p->sky[1] = env->Green / 255.0f;
    p->sky[2] = env->Blue / 255.0f;
    p->cloud[0] = env->CloudRed / 255.0f;
    p->cloud[1] = env->CloudGreen / 255.0f;
    p->cloud[2] = env->CloudBlue / 255.0f;
    p->gain = GAIN;
    {
        /* the horizon band's top: the elevation of the level's sky height
         * (the record's skyheight, GE's cloud plane) seen at the fog's far
         * distance (bgfog.c g_ScaledFarFogIntensity, Fog x2 included): nothing
         * is drawn beyond it, so nothing pops in above it */
        extern f32 g_ScaledFarFogIntensity;
        const float D = g_ScaledFarFogIntensity;
        const float Hs = env->CloudRepeat;
        /* no fog (D left at FLT_MAX: Frigate): nothing pops in, GE's clouds
         * reach the horizon, the band stays at its floor */
        float b = (D > 1.0f && D < 1e20f && Hs > 0.0f) ? Hs / sqrtf(D * D + Hs * Hs) : 0.0f;
        if (b < 0.02f) b = 0.02f;
        if (b > 0.22f) b = 0.22f;
        p->band = b;
    }
    p->texScale = 1.0f;   /* kept at the size tuned on Dam (a 25..2000 % slider settled on 100) */

    /* scroll: GE's layer follows g_SkyCloudOffset exactly; the others run at
     * their own speed on the unwrapped time (no jump at the 4096 wrap) */
    if (s_lastGeOfs >= 0.0f) {
        float d = g_SkyCloudOffset - s_lastGeOfs;
        if (d < -2048.0f) d += 4096.0f;
        if (d > 0.0f && d < 2048.0f) s_time += d;
    }
    s_lastGeOfs = g_SkyCloudOffset;
    p->geOffset = g_SkyCloudOffset;
    for (i = 0; i < 3; i++) {
        /* tc units -> texels (S10.5), then into the layer's own texel scale */
        const float tex = (float)fmod(s_time * SPEED * kSpeed[i] / 32.0, 64.0 * 4096.0) / kScale[i];
        p->layerOfs[i][0] = 0.0f;
        p->layerOfs[i][1] = wrapf(tex, 64.0f);
    }
    p->layerOfs[1][1] = wrapf(g_SkyCloudOffset * SPEED / 32.0f, 64.0f);   /* 4096 * 0.5 / 32 = 64: still seamless at the wrap */

    mood = skyPlusMood(env);
    if (mood == MOOD_DUSK || mood == MOOD_EXOTIC) {
        /* the redder the light, the lower the sun (yellow 20 deg, deep red 6: just
         * above the horizon band of the GL pass, its halo still shows) */
        float col[3], red, mx;
        col[0] = p->sky[0] + p->cloud[0] * (1.0f - p->sky[0]);
        col[1] = p->sky[1] + p->cloud[1] * (1.0f - p->sky[1]);
        col[2] = p->sky[2] + p->cloud[2] * (1.0f - p->sky[2]);
        mx = col[1] > col[2] ? col[1] : col[2];
        red = col[0] > 1e-3f ? (col[0] - mx) / col[0] : 0.0f;
        if (red < 0.0f) red = 0.0f;
        if (red > 1.0f) red = 1.0f;
        /* azimuth: Surface's shadows put its sun 90 deg to the right of the
         * default, 12 deg back left, 6 back right (console check 2026-11-15;
         * right-handed world: right = -) */
        dirFrom(20.0f + (6.0f - 20.0f) * red,
                (g_SkyStageNum % 100) == LEVELID_SURFACE ? 0.3f + (-90.0f + 12.0f - 6.0f) * (float)M_PI / 180.0f : 0.3f, p->sunDir);
        memcpy(p->sunCol, col, sizeof col);
        p->sunGlow = 0.4f + 0.35f * red;
    } else if (mood == MOOD_DAY) {
        /* Dam: 120 deg to the right of the default to match its shadows (2026-11-15) */
        dirFrom(50.0f, (g_SkyStageNum % 100) == LEVELID_DAM ? 0.6f - 120.0f * (float)M_PI / 180.0f : 0.6f, p->sunDir);
        p->sunCol[0] = 1.0f;
        p->sunCol[1] = 0.97f;
        p->sunCol[2] = 0.9f;
        if ((g_SkyStageNum % 100) == LEVELID_FRIGATE) {
            /* Frigate's navy sky with pale clouds is the blue hour: the sun has
             * already set, 4-6 deg under the horizon in photos of such skies, so
             * no disc and no glints, only a pale glow low over the horizon on
             * its side. Its light is the level's own lit-cloud colour (GE's
             * cloud shade at full fade), never a warm tint. */
            dirFrom(-5.0f, 0.6f, p->sunDir);
            for (i = 0; i < 3; i++) {
                p->sunCol[i] = p->sky[i] + p->cloud[i] * (1.0f - p->sky[i]);
            }
        }
        p->sunGlow = 0.35f;
    } else if (mood == MOOD_NIGHT) {
        p->night = 1;
        dirFrom(35.0f, -0.5f, p->moonDir);
    }

    p->texGen = s_texGen;
    p->tex = waterOnly ? NULL : s_tex;
    if (waterOnly) {   /* the water mirrors the fog colour (the horizon: no sky here), no sun, no glints */
        p->sunGlow = 0.0f;
        p->night = 0;
        p->waterOnly = 1;
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
        }
        p->swell = s_ws.swell;
        p->chop = s_ws.chop;
        p->speed = s_ws.speed;
        /* no sea: water[] is the tint of the level's own water draws */
        for (i = 0; i < 3; i++) {
            p->water[i] = s_ws.tint[i];
        }
        skyPlusWaterTexture();   /* the wave heights, for the sea and the water draws */
        p->waterTexGen = s_wtexGen;
        p->waterTex = s_wtex;
        p->reflectLit = s_ws.reflectLit;
        p->patchScale = s_ws.patchScale;
        p->toneKeep = s_ws.toneKeep;
        p->reflectGain = s_ws.reflectGain;
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
    if (env->IsWater) {
        p->sea = 1;
        p->seaY = env->WaterRepeat;
        /* the sea's body: the sky's own colour, desaturated and darkened, as
         * open sea reads under such a sky (the film's harbour shots: slate
         * navy, darker than the sky, never GE's turquoise); its brightness
         * toward the horizon comes from the sky reflection (Fresnel) */
        {
            const float lum = 0.30f * p->sky[0] + 0.59f * p->sky[1] + 0.11f * p->sky[2];
            for (i = 0; i < 3; i++) {
                p->water[i] *= (p->sky[i] + (lum - p->sky[i]) * 0.4f) * 0.9f;
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
