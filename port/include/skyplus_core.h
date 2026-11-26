#ifndef PORT_SKYPLUS_CORE_H
#define PORT_SKYPLUS_CORE_H

/* The pure part of the GE+ sky (issue #145): everything port/src/skyplus.c computes without game state, moved here unchanged so that
 * the game and the dev tool (tools_pc/fxview/sea) fill the same PortSkyPlus the same way: the cloud texture conversion, the wave
 * texture (a tiling fbm), the per-level water settings, and the sky, sun, layer scroll and water fields of the block from an
 * environment record, the eye and the time. No game headers; the level numbers are the game's LEVELID_* (checked in skyplus.c). */

#include "ge_render_sky.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The game's LEVELID_* values the sky and water settings depend on (skyplus.c asserts they match). */
enum {
    SPC_BUNKER1 = 9, SPC_FRIGATE = 26, SPC_STREETS = 29, SPC_COMPLEX = 31, SPC_EGYPT = 32, SPC_DAM = 33, SPC_RUNWAY = 35, SPC_SURFACE = 36,
    SPC_TEMPLE = 38, SPC_CAVERNS = 39, SPC_CITADEL = 40, SPC_CRADLE = 41
};

/* The cloud image (IA8, 64x64, rows `pitch` bytes apart) as 8-bit intensity (the I of I4A4). out: SKYPLUS_TEX_W * SKYPLUS_TEX_H bytes. */
void skyPlusCoreCloudTexture(const unsigned char *ia8, int pitch, unsigned char *out);
/* The sea's wave heights: out = SKYPLUS_WTEX_W * SKYPLUS_WTEX_H * 4 bytes (R height, G and B slope). */
void skyPlusCoreWaveTexture(unsigned char *out);

struct SkyPlusWaterSettings {
    float tint[3], swell, chop, speed;
    float deep[3], light[3];
    int deepSet, lightSet;
    float reflectLit, patchScale;
    float toneKeep, reflectGain;
    float farHeight;
};
/* "dam", "frigate", "caverns", "complex", or NULL for a level without GE+ water. */
const char *skyPlusCoreWaterName(int stage);
/* The level's defaults. */
void skyPlusCoreWaterDefaults(int stage, struct SkyPlusWaterSettings *w);
/* Overrides from a $S/water/<level>.ini (already resolved path); 1 = the file was read. */
int skyPlusCoreWaterIni(const char *path, struct SkyPlusWaterSettings *w);

/* What the sky fill reads: the level's environment record (colours as the record has them, 0..255), the eye and the clock. */
struct SkyPlusInput {
    int stage;                       /* g_SkyStageNum % 100 */
    float red, green, blue;          /* the sky / fog colour */
    float cloudRed, cloudGreen, cloudBlue, cloudRepeat;   /* the cloud colour, the record's sky height */
    float eye[3];
    float farFog;                    /* g_ScaledFarFogIntensity (FLT_MAX: no fog) */
    double time;                     /* the unwrapped GE scroll time (the game's s_time) */
    float geOffset;                  /* g_SkyCloudOffset */
};
/* p has been cleared and holds the corner rays; this sets the eye, the colours, the band, the scroll of the three layers, the sun, the moon
 * and the night flag (texGen, tex and the water fields are the caller's). */
void skyPlusCoreSky(PortSkyPlus *p, const struct SkyPlusInput *in);
/* The water fields of a level's block from its settings (the sea's tint and body colour, the level's own water surfaces' constants). */
void skyPlusCoreWater(PortSkyPlus *p, const struct SkyPlusWaterSettings *ws, int isWater, float waterRepeat);

#ifdef __cplusplus
}
#endif

#endif
