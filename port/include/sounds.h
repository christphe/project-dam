#ifndef SOUNDS_H
#define SOUNDS_H
/*
 * sounds.h -- the sound effects from the asset packs (sounds/sfx/sfx.json + sfx_NNNN.wav, docs/specs/audio.md "The SFX
 * bank") instead of the ROM's sound bank. GE_ROM_SOUNDS=1 keeps the ROM path.
 *
 * sfxInit decodes every effect of the stack once, on the game thread (assetRead is game-thread only), into one PCM arena.
 * After that the tables are read-only, so the audio thread may read them (sfxGet, sfxFetch).
 */
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One effect of sfx.json (the fields are the exporter's; see the spec). */
typedef struct SfxEffect {
    int id;                 /* 1-based effect number */
    int present;            /* 1 when the entry exists in sfx.json */
    int rate_hz;            /* the effect's playback rate (informative: the engine derives it from pitch_cents) */
    int pitch_cents;        /* start pitch relative to the recorded rate */
    int volume_group, reverb, flags, retrigger;
    int has_slide, slide_cents;
    int next, next_delay_ms;
    int sample_volume, pan;
    int attack_us, attack_vol, decay_us, decay_vol, release_us;
    int has_loop;
    uint32_t loop_start, loop_end;   /* samples */
    uint32_t pcm_off;       /* byte offset of the PCM in the arena (sfxInit) */
    uint32_t pcm_samples;   /* 0 = no usable WAV */
} SfxEffect;

/* Pure parsing (unit tested). sfxParseJson fills eff[id] (1 <= id < max) for every object of the array and returns how many
 * objects it read, or -1 for a malformed document. `eff` must be zeroed by the caller. */
int sfxParseJson(const char *json, size_t len, SfxEffect *eff, int max);
/* A canonical PCM WAV (mono, 16 bit): points *pcm at its samples (little-endian, inside `wav`). 1 on success. */
int sfxWavSamples(const unsigned char *wav, size_t len, const unsigned char **pcm, uint32_t *nsamples);

/* Game thread: (re)loads the effects from the current stack, into the idle one of two sets, then publishes it (the previous
 * set stays readable for voices still releasing; the caller stops the playing effects first). Returns 1 when the pack sounds
 * are in use, 0 for the ROM path (GE_ROM_SOUNDS=1 only). A stack without sfx.json is in use too: every effect is silent
 * (sfxGet is NULL), one warning is logged. */
int sfxInit(void);
int sfxActive(void);
int sfxSet(void);                         /* the published set (0/1) */
int sfxCount(void);                       /* highest effect number + 1 */
/* The effect `id`, or NULL when sfx.json has no usable entry for it (one log line per id). Any thread, after sfxInit. */
const SfxEffect *sfxGet(int id);

/* The engine's sample fetch addresses of a pack effect lie from SFX_ADDR_BASE: base of effect = SFX_ADDR_BASE + pcm_off. */
#define SFX_ADDR_BASE 0xE0000000u
#define SFX_SET_STRIDE 0x01000000u   /* the second set's PCM lies this far above the first's */
/* Pure: 1 (and *built = now) when the asset stack generation `now` differs from the one the sounds were built from. */
int sfxGenChanged(unsigned *built, unsigned now);
void sfxLogReload(unsigned gen);   /* the "sounds: reloaded for asset stack generation N" log line */
/* Audio thread: when `addr` is in the pack window copies `len` bytes of the arena at that address into dst (zero past the
 * end) and returns 1; else 0. */
int sfxFetch(uint32_t addr, void *dst, uint32_t len);

#ifdef __cplusplus
}
#endif
#endif
