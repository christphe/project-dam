/* The pure part of the GE+ sky: see port/include/skyplus_core.h. Moved from skyplus.c without changing what it computes. */
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#include "skyplus_core.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

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
static int skyPlusMood(const struct SkyPlusInput *env)
{
    const float lum = (0.30f * env->red + 0.59f * env->green + 0.11f * env->blue) / 255.0f;

    switch (env->stage) {
    case SPC_DAM:
    case SPC_FRIGATE:
        return MOOD_DAY;
    case SPC_EGYPT:
        return lum < 0.02f ? MOOD_NIGHT : MOOD_DAY;   /* the alt record is black */
    case SPC_STREETS:
    case SPC_SURFACE:
    case SPC_BUNKER1:   /* Surface 1's sky, see skyPlusEnv */
    case SPC_TEMPLE:
    case SPC_CRADLE:
        return MOOD_DUSK;
    case SPC_COMPLEX:
    case SPC_CITADEL:
        return MOOD_EXOTIC;
    case SPC_RUNWAY:
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

static float wrapf(float v, float m)
{
    v = fmodf(v, m);
    return v < 0.0f ? v + m : v;
}

void skyPlusCoreCloudTexture(const unsigned char *src, int pitch, unsigned char *out)
{
    int x, y;
    for (y = 0; y < SKYPLUS_TEX_H; y++) {
        for (x = 0; x < SKYPLUS_TEX_W; x++) {
            out[y * SKYPLUS_TEX_W + x] = (unsigned char)((src[y * pitch + x] >> 4) * 17);   /* I of I4A4 */
        }
    }
}

#define WTEX_SLOPE_K 6.0f   /* slope scale in G, B (sky/common.glsl texSlopeAt decodes it) */

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

void skyPlusCoreWaveTexture(unsigned char *out)
{
    float lo = 1e9f, hi = -1e9f;
    static float v[SKYPLUS_WTEX_W * SKYPLUS_WTEX_H];
    int x, y, o;

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
            unsigned char *q = &out[(y * SKYPLUS_WTEX_W + x) * 4];
            float g = 127.5f + dx * WTEX_SLOPE_K * 255.0f, b = 127.5f + dy * WTEX_SLOPE_K * 255.0f;
            q[0] = (unsigned char)(h * 255.0f + 0.5f);
            q[1] = (unsigned char)(g < 0.0f ? 0.0f : g > 255.0f ? 255.0f : g + 0.5f);
            q[2] = (unsigned char)(b < 0.0f ? 0.0f : b > 255.0f ? 255.0f : b + 0.5f);
            q[3] = 255;
        }
    }
}

const char *skyPlusCoreWaterName(int stage)
{
    switch (stage) {
    case SPC_DAM:     return "dam";
    case SPC_FRIGATE: return "frigate";
    case SPC_CAVERNS: return "caverns";
    case SPC_COMPLEX: return "complex";
    default:          return NULL;
    }
}

static int keyeq(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
    }
    return *a == *b;
}

void skyPlusCoreWaterDefaults(int stage, struct SkyPlusWaterSettings *w)
{
    w->tint[0] = w->tint[1] = w->tint[2] = 1.0f;
    w->swell = w->chop = w->speed = 1.0f;
    w->deepSet = w->lightSet = 0;
    w->deep[0] = w->deep[1] = w->deep[2] = w->light[0] = w->light[1] = w->light[2] = 0.0f;
    w->reflectLit = 1.0f;   /* unknown levels: GE's rule */
    w->patchScale = 6.0f;
    w->toneKeep = 0.0f;
    w->reflectGain = 1.0f;
    w->farHeight = 300.0f;
    if (stage == SPC_FRIGATE) {   /* open sea: a real swell, not an oil-flat sea */
        w->swell = 2.0f;
        w->chop = 1.2f;
    } else if (stage == SPC_DAM) {   /* a lake: calm */
        w->swell = 0.3f;
        w->chop = 0.6f;
        w->speed = 0.7f;
        w->reflectLit = 0.0f;   /* Dam's water is lit dark: the reflection is not dimmed by it */
        w->toneKeep = 0.8f;     /* the original's dark teal: the sky reflection keeps the body's hue */
        w->reflectGain = 0.6f;  /* measured on the PC (intro camera 1): mean water colour 15,32,39 vs GE's 12,25,29 */
    } else if (stage == SPC_CAVERNS) {   /* still cave water, dark, no sky to mirror */
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
}

int skyPlusCoreWaterIni(const char *path, struct SkyPlusWaterSettings *w)
{
    char line[128];
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        return 0;
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
            else if (keyeq(key, "FarHeight")) w->farHeight = a < 20.0f ? 20.0f : a > 3000.0f ? 3000.0f : a;
        }
    }
    fclose(f);
    return 1;
}

void skyPlusCoreSky(PortSkyPlus *p, const struct SkyPlusInput *env)
{
    int i, mood;
    /* per extra layer: wind speed and size relative to the GE layer */
    static const float kSpeed[3] = { 0.35f, 1.0f, 1.9f };
    static const float kScale[3] = { 1.7f, 1.0f, 0.45f };

    p->eye[0] = env->eye[0];
    p->eye[1] = env->eye[1];
    p->eye[2] = env->eye[2];
    p->cloudY = env->eye[1] + CLOUD_HEIGHT;
    p->sky[0] = env->red / 255.0f;
    p->sky[1] = env->green / 255.0f;
    p->sky[2] = env->blue / 255.0f;
    p->cloud[0] = env->cloudRed / 255.0f;
    p->cloud[1] = env->cloudGreen / 255.0f;
    p->cloud[2] = env->cloudBlue / 255.0f;
    p->gain = GAIN;
    {
        /* the horizon band's top: the elevation of the level's sky height
         * (the record's skyheight, GE's cloud plane) seen at the fog's far
         * distance (bgfog.c g_ScaledFarFogIntensity, Fog x2 included): nothing
         * is drawn beyond it, so nothing pops in above it */
        const float D = env->farFog;
        const float Hs = env->cloudRepeat;
        /* no fog (D left at FLT_MAX: Frigate): nothing pops in, GE's clouds
         * reach the horizon, the band stays at its floor */
        float b = (D > 1.0f && D < 1e20f && Hs > 0.0f) ? Hs / sqrtf(D * D + Hs * Hs) : 0.0f;
        if (b < 0.02f) b = 0.02f;
        if (b > 0.22f) b = 0.22f;
        p->band = b;
    }
    p->texScale = 1.0f;   /* kept at the size tuned on Dam (a 25..2000 % slider settled on 100) */

    p->geOffset = env->geOffset;
    for (i = 0; i < 3; i++) {
        /* tc units -> texels (S10.5), then into the layer's own texel scale */
        const float tex = (float)fmod(env->time * SPEED * kSpeed[i] / 32.0, 64.0 * 4096.0) / kScale[i];
        p->layerOfs[i][0] = 0.0f;
        p->layerOfs[i][1] = wrapf(tex, 64.0f);
    }
    p->layerOfs[1][1] = wrapf(env->geOffset * SPEED / 32.0f, 64.0f);   /* 4096 * 0.5 / 32 = 64: still seamless at the wrap */

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
                env->stage == SPC_SURFACE ? 0.3f + (-90.0f + 12.0f - 6.0f) * (float)M_PI / 180.0f : 0.3f, p->sunDir);
        memcpy(p->sunCol, col, sizeof col);
        p->sunGlow = 0.4f + 0.35f * red;
    } else if (mood == MOOD_DAY) {
        /* Dam: 120 deg to the right of the default to match its shadows (2026-11-15) */
        dirFrom(50.0f, env->stage == SPC_DAM ? 0.6f - 120.0f * (float)M_PI / 180.0f : 0.6f, p->sunDir);
        p->sunCol[0] = 1.0f;
        p->sunCol[1] = 0.97f;
        p->sunCol[2] = 0.9f;
        if (env->stage == SPC_FRIGATE) {
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
}

void skyPlusCoreWater(PortSkyPlus *p, const struct SkyPlusWaterSettings *ws, int isWater, float waterRepeat)
{
    int i;
    p->swell = ws->swell;
    p->chop = ws->chop;
    p->speed = ws->speed;
    /* no sea: water[] is the tint of the level's own water draws */
    for (i = 0; i < 3; i++) {
        p->water[i] = ws->tint[i];
    }
    p->reflectLit = ws->reflectLit;
    p->patchScale = ws->patchScale;
    p->toneKeep = ws->toneKeep;
    p->reflectGain = ws->reflectGain;
    if (isWater) {
        p->sea = 1;
        p->seaY = waterRepeat;
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
}
