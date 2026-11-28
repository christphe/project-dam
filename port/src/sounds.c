/*
 * sounds.c -- the sound effects from the asset packs (port/include/sounds.h). The data and its meaning are in
 * docs/specs/audio.md ("The SFX bank: the key map is not a key map"); this file only reads sfx.json and the WAVs.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sounds.h"

#define SFX_MAX_ID 2048
#define SFX_ARENA_BYTES (8u * 1024 * 1024)   /* the whole bank decoded is about 3 MB */

/* ---- sfx.json -------------------------------------------------------------------------------------------------- */

/* not <ctype.h>: the game defines its own isspace (src/str.c), which clashes at link time */
static int isws(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
static int isdig(int c) { return c >= '0' && c <= '9'; }

static const char *skipws(const char *p, const char *e)
{
    while (p < e && isws((unsigned char)*p))
        p++;
    return p;
}

/* A JSON string at p (the opening quote is checked): copies up to cap-1 chars, returns the pointer after it. */
static const char *rdstr(const char *p, const char *e, char *out, size_t cap)
{
    size_t n = 0;
    if (p >= e || *p != '"')
        return NULL;
    p++;
    while (p < e && *p != '"') {
        if (*p == '\\' && p + 1 < e)
            p++;
        if (n + 1 < cap)
            out[n++] = *p;
        p++;
    }
    if (p >= e)
        return NULL;
    out[n] = 0;
    return p + 1;
}

static const char *skipcontainer(const char *p, const char *e, char open, char close)
{
    int depth = 0;
    for (; p < e; p++) {
        if (*p == '"') {
            char tmp[2];
            p = rdstr(p, e, tmp, sizeof tmp);
            if (!p)
                return NULL;
            p--;
        } else if (*p == open) {
            depth++;
        } else if (*p == close && --depth == 0) {
            return p + 1;
        }
    }
    return NULL;
}

static const char *skipval(const char *p, const char *e)
{
    p = skipws(p, e);
    if (p >= e)
        return NULL;
    if (*p == '"') {
        char tmp[2];
        return rdstr(p, e, tmp, sizeof tmp);
    }
    if (*p == '{')
        return skipcontainer(p, e, '{', '}');
    if (*p == '[')
        return skipcontainer(p, e, '[', ']');
    while (p < e && *p != ',' && *p != '}' && *p != ']' && !isws((unsigned char)*p))
        p++;
    return p;
}

static void setfield(SfxEffect *x, const char *key, long v)
{
    if (!strcmp(key, "id")) x->id = (int)v;
    else if (!strcmp(key, "rate_hz")) x->rate_hz = (int)v;
    else if (!strcmp(key, "pitch_cents")) x->pitch_cents = (int)v;
    else if (!strcmp(key, "volume_group")) x->volume_group = (int)v;
    else if (!strcmp(key, "reverb")) x->reverb = (int)v;
    else if (!strcmp(key, "flags")) x->flags = (int)v;
    else if (!strcmp(key, "retrigger")) x->retrigger = v != 0;
    else if (!strcmp(key, "pitch_slide_cents")) { x->has_slide = 1; x->slide_cents = (int)v; }
    else if (!strcmp(key, "next")) x->next = (int)v;
    else if (!strcmp(key, "next_delay_ms")) x->next_delay_ms = (int)v;
    else if (!strcmp(key, "sample_volume")) x->sample_volume = (int)v;
    else if (!strcmp(key, "pan")) x->pan = (int)v;
    else if (!strcmp(key, "envelope.attack_us")) x->attack_us = (int)v;
    else if (!strcmp(key, "envelope.attack_volume")) x->attack_vol = (int)v;
    else if (!strcmp(key, "envelope.decay_us")) x->decay_us = (int)v;
    else if (!strcmp(key, "envelope.decay_volume")) x->decay_vol = (int)v;
    else if (!strcmp(key, "envelope.release_us")) x->release_us = (int)v;
    else if (!strcmp(key, "loop.start")) { x->has_loop = 1; x->loop_start = (uint32_t)v; }
    else if (!strcmp(key, "loop.end")) x->loop_end = (uint32_t)v;
}

/* One object at p ('{'), keys prefixed by `prefix` ("" or "envelope."). Returns the pointer after '}' or NULL. */
static const char *rdobj(const char *p, const char *e, const char *prefix, SfxEffect *x, int depth)
{
    p = skipws(p, e);
    if (depth > 3 || p >= e || *p != '{')
        return NULL;
    p++;
    for (;;) {
        char key[64], full[128];
        p = skipws(p, e);
        if (p < e && *p == '}')
            return p + 1;
        if (!(p = rdstr(p, e, key, sizeof key)))
            return NULL;
        p = skipws(p, e);
        if (p >= e || *p != ':')
            return NULL;
        p = skipws(p + 1, e);
        snprintf(full, sizeof full, "%s%s", prefix, key);
        if (p < e && *p == '{') {
            char sub[140];
            snprintf(sub, sizeof sub, "%s.", full);
            if (!(p = rdobj(p, e, sub, x, depth + 1)))
                return NULL;
        } else if (p < e && (*p == '-' || isdig((unsigned char)*p))) {
            char *end;
            long v = strtol(p, &end, 10);
            if (end == p)
                return NULL;
            setfield(x, full, v);
            p = end;
            while (p < e && (isdig((unsigned char)*p) || *p == '.' || *p == 'e' || *p == 'E' || *p == '+' || *p == '-'))
                p++;   /* a fraction or exponent is never written by the exporter: skipped */
        } else if (e - p >= 4 && !strncmp(p, "true", 4)) {
            setfield(x, full, 1);
            p += 4;
        } else if (e - p >= 5 && !strncmp(p, "false", 5)) {
            setfield(x, full, 0);
            p += 5;
        } else if (e - p >= 4 && !strncmp(p, "null", 4)) {
            p += 4;
        } else {
            if (!(p = skipval(p, e)))
                return NULL;
        }
        p = skipws(p, e);
        if (p < e && *p == ',')
            p++;
    }
}

int sfxParseJson(const char *json, size_t len, SfxEffect *eff, int max)
{
    const char *p = json, *e = json + len;
    int n = 0;
    p = skipws(p, e);
    if (p >= e || *p != '[')
        return -1;
    p++;
    for (;;) {
        SfxEffect x;
        p = skipws(p, e);
        if (p >= e)
            return -1;
        if (*p == ']')
            return n;
        memset(&x, 0, sizeof x);
        if (!(p = rdobj(p, e, "", &x, 0)))
            return -1;
        if (x.id > 0 && x.id < max) {
            x.present = 1;
            eff[x.id] = x;
            n++;
        }
        p = skipws(p, e);
        if (p < e && *p == ',')
            p++;
    }
}

/* ---- WAV ------------------------------------------------------------------------------------------------------- */

static uint32_t le32(const unsigned char *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static unsigned le16(const unsigned char *p) { return p[0] | (p[1] << 8); }

int sfxWavSamples(const unsigned char *wav, size_t len, const unsigned char **pcm, uint32_t *nsamples)
{
    size_t p = 12;
    int fmt = 0;
    if (len < 12 || memcmp(wav, "RIFF", 4) || memcmp(wav + 8, "WAVE", 4))
        return 0;
    while (p + 8 <= len) {
        uint32_t sz = le32(wav + p + 4);
        const unsigned char *c = wav + p + 8;
        if (!memcmp(wav + p, "fmt ", 4)) {
            if (sz < 16 || p + 8 + 16 > len || le16(c) != 1 || le16(c + 2) != 1 || le16(c + 14) != 16)
                return 0;
            fmt = 1;
        } else if (!memcmp(wav + p, "data", 4)) {
            if (!fmt || sz > len - (p + 8))
                return 0;
            *pcm = c;
            *nsamples = sz / 2;
            return 1;
        }
        p += 8 + (size_t)sz + (sz & 1);
    }
    return 0;
}

/* ---- the stack -------------------------------------------------------------------------------------------------- */

#include "texpack.h"
#include "ge_pkg.h"
#include "system.h"

extern char *getenv(const char *);

/* Two sets: a reload builds into the idle one and then flips, and the previous set stays intact, so a voice still releasing
 * from it reads valid PCM. A set's PCM addresses carry its slot (SFX_SET_STRIDE apart). */
typedef struct SfxSet {
    SfxEffect *eff;                 /* [SFX_MAX_ID], read-only once published */
    unsigned char *arena;
    uint32_t used;
    int maxId;
    unsigned char warned[SFX_MAX_ID];
} SfxSet;

static SfxSet sSet[2];
static volatile int sCur;           /* the published set */
static volatile int sActive;
static int sInited, sRomForced;

int sfxActive(void) { return sActive; }
int sfxCount(void) { return sActive ? sSet[sCur].maxId + 1 : 0; }
int sfxSet(void) { return sCur; }

int sfxGenChanged(unsigned *built, unsigned now)
{
    if (*built == now)
        return 0;
    *built = now;
    return 1;
}

static void setFree(SfxSet *s)
{
    free(s->eff);
    free(s->arena);
    memset(s, 0, sizeof *s);
}

int sfxInit(void)
{
    unsigned char *js = NULL;
    unsigned long jlen = 0;
    const char *rom;
    int n, id, loaded = 0, bad = 0, slot;
    SfxSet *s;

    if (!sInited) {
        sInited = 1;
        rom = getenv("GE_ROM_SOUNDS");
        if (rom && *rom && *rom != '0') {
            sRomForced = 1;
            sysLogPrintf(LOG_INFO, "sounds: GE_ROM_SOUNDS set, effects from the ROM sound bank");
        }
    }
    if (sRomForced)
        return 0;
    slot = sActive ? sCur ^ 1 : sCur;   /* never build into the published set */
    s = &sSet[slot];
    setFree(s);
    if (!assetRead("sounds/sfx/sfx.json", &js, &jlen, NULL)) {
        sysLogPrintf(LOG_WARNING, "sounds: no sounds/sfx/sfx.json in the asset packs, effects from the ROM sound bank");
        sActive = 0;
        return 0;
    }
    s->eff = (SfxEffect *)calloc(SFX_MAX_ID, sizeof *s->eff);
    s->arena = (unsigned char *)calloc(1, SFX_ARENA_BYTES);
    if (!s->eff || !s->arena) {
        setFree(s);
        pkgFree(js);
        sActive = 0;
        return 0;
    }
    n = sfxParseJson((const char *)js, jlen, s->eff, SFX_MAX_ID);
    pkgFree(js);
    if (n < 0) {
        sysLogPrintf(LOG_ERROR, "sounds: sfx.json is not readable, effects from the ROM sound bank");
        setFree(s);
        sActive = 0;
        return 0;
    }
    for (id = 1; id < SFX_MAX_ID; id++) {
        char path[64];
        unsigned char *wav = NULL;
        unsigned long wlen = 0;
        const unsigned char *pcm;
        uint32_t ns;
        SfxEffect *x = &s->eff[id];
        if (!x->present)
            continue;
        if (id > s->maxId)
            s->maxId = id;
        snprintf(path, sizeof path, "sounds/sfx/sfx_%04d.wav", id);
        if (!assetRead(path, &wav, &wlen, NULL)) {
            bad++;
            continue;
        }
        if (!sfxWavSamples(wav, wlen, &pcm, &ns) || ns == 0 || (uint64_t)s->used + (uint64_t)ns * 2 + 1024 > SFX_ARENA_BYTES) {
            pkgFree(wav);
            bad++;
            continue;
        }
        x->pcm_off = ((uint32_t)slot * SFX_SET_STRIDE) + s->used;
        x->pcm_samples = ns;
        memcpy(s->arena + s->used, pcm, (size_t)ns * 2);   /* little-endian hosts only (all targets) */
        s->used += (ns * 2 + 15u) & ~15u;
        pkgFree(wav);
        loaded++;
    }
    sCur = slot;
    sActive = 1;
    sysLogPrintf(LOG_INFO, "sounds: %d effects from the asset packs (%d json entries, %d without a usable wav), %u KB of PCM", loaded, n,
                 bad, (unsigned)(s->used / 1024));
    return 1;
}

const SfxEffect *sfxGet(int id)
{
    SfxSet *s;
    if (!sActive || id <= 0 || id >= SFX_MAX_ID)
        return NULL;
    s = &sSet[sCur];
    if (s->eff && s->eff[id].present && s->eff[id].pcm_samples)
        return &s->eff[id];
    if (s->eff && !s->warned[id]) {
        s->warned[id] = 1;
        sysLogPrintf(LOG_WARNING, "sounds: effect %d has no usable entry in the asset packs (silence)", id);
    }
    return NULL;
}

int sfxFetch(uint32_t addr, void *dst, uint32_t len)
{
    uint32_t off;
    const SfxSet *s;
    if (addr < SFX_ADDR_BASE || addr - SFX_ADDR_BASE >= 2 * SFX_SET_STRIDE)
        return 0;
    off = addr - SFX_ADDR_BASE;
    s = &sSet[off / SFX_SET_STRIDE];
    off %= SFX_SET_STRIDE;
    if (!s->arena || off >= s->used) {
        memset(dst, 0, len);
    } else {
        uint32_t n = s->used - off < len ? s->used - off : len;
        memcpy(dst, s->arena + off, n);
        if (n < len)
            memset((unsigned char *)dst + n, 0, len - n);
    }
    return 1;
}
