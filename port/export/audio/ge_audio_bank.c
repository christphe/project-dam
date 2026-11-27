/*
 * Bank file ("ctl") and sample data ("tbl"): parsing, ADPCM decoding, WAV.
 * See docs/specs/audio.md "Bank file", "ADPCM decoding".
 */
#include <stdlib.h>
#include <string.h>

#include "ge_audio_internal.h"

#define MAX_INSTRUMENTS 1024
#define MAX_SOUNDS 4096
#define LOOP_FOREVER 0xFFFFFFFFu

static void bank_error(ge_ae_bank *b, int inst, int sound, const char *what)
{
    b->errors++;
    if (!b->first_error[0])
        snprintf(b->first_error, sizeof b->first_error, "instrument %d sound %d: %s", inst, sound, what);
}

/* ---- ADPCM (order-2 codebook) -------------------------------------------------- */
static int16_t clamp16(int64_t v)
{
    return (int16_t)(v < -32768 ? -32768 : (v > 32767 ? 32767 : v));
}

/* Decode `nframes` 9-byte frames. book = npred * 16 coefficients (c0[8], c1[8]
 * per predictor). Returns 0, or -1 when a frame names a missing predictor. */
static int adpcm_decode(const uint8_t *in, size_t nframes, const int16_t *book, int npred, int16_t *out)
{
    int32_t s1 = 0, s2 = 0; /* last and second-to-last output samples */
    size_t f;
    for (f = 0; f < nframes; f++) {
        const uint8_t *fr = in + f * 9;
        int scale = fr[0] >> 4, p = fr[0] & 15, half, j, k;
        const int16_t *c0, *c1;
        if (p >= npred)
            return -1;
        c0 = book + 16 * p;
        c1 = c0 + 8;
        for (half = 0; half < 2; half++) {
            int64_t x[8];
            for (j = 0; j < 8; j++) {
                int idx = half * 8 + j;
                int nib = (fr[1 + idx / 2] >> ((idx & 1) ? 0 : 4)) & 15;
                if (nib & 8)
                    nib -= 16;
                x[j] = (int64_t)nib * ((int64_t)1 << scale);
            }
            for (j = 0; j < 8; j++) {
                int64_t acc = (int64_t)c0[j] * s2 + (int64_t)c1[j] * s1 + x[j] * 2048;
                for (k = 0; k < j; k++)
                    acc += (int64_t)c1[j - 1 - k] * x[k];
                out[f * 16 + half * 8 + j] = clamp16(acc >> 11); /* arithmetic shift */
            }
            s1 = out[f * 16 + half * 8 + 7];
            s2 = out[f * 16 + half * 8 + 6];
        }
    }
    return 0;
}

/* Decode the wave table at ctl offset `woff` into *w. 0 on success, else -1 with msg. */
static int load_wave(ge_view ctl, ge_view tbl, uint32_t woff, ge_ae_wave *w, char *msg)
{
    int bad = 0;
    uint32_t base, loop_off, book_off = 0;
    int32_t len;
    uint32_t type;

    memset(w, 0, sizeof *w);
    w->src_off = woff;
    base = ge_v_u32(ctl, woff, &bad);
    len = ge_v_s32(ctl, woff + 4u, &bad);
    type = ge_v_u8(ctl, woff + 8u, &bad);
    loop_off = ge_v_u32(ctl, woff + 12u, &bad);
    if (type == 0)
        book_off = ge_v_u32(ctl, woff + 16u, &bad);
    if (bad) {
        snprintf(msg, GE_AE_ERRLEN, "wave table at 0x%X is outside the ctl file", (unsigned)woff);
        return -1;
    }
    if (type > 1) {
        snprintf(msg, GE_AE_ERRLEN, "wave table type %u", (unsigned)type);
        return -1;
    }
    if (len <= 0 || base > tbl.n || (size_t)len > tbl.n - base) {
        snprintf(msg, GE_AE_ERRLEN, "sample data 0x%X + %d is outside the tbl file", (unsigned)base, (int)len);
        return -1;
    }
    w->type = (uint8_t)type;

    if (type == 0) {
        int32_t order, npred;
        int16_t *book;
        size_t ncoef, i, nframes = (size_t)len / 9;
        if (!book_off) {
            snprintf(msg, GE_AE_ERRLEN, "ADPCM wave without a codebook");
            return -1;
        }
        order = ge_v_s32(ctl, book_off, &bad);
        npred = ge_v_s32(ctl, book_off + 4u, &bad);
        if (bad || order != 2 || npred < 1 || npred > 16) {
            snprintf(msg, GE_AE_ERRLEN, "unsupported codebook (order %d, %d predictors)", (int)order, (int)npred);
            return -1;
        }
        ncoef = (size_t)npred * 16;
        book = (int16_t *)malloc(ncoef * sizeof *book);
        if (!book) {
            snprintf(msg, GE_AE_ERRLEN, "out of memory");
            return -1;
        }
        for (i = 0; i < ncoef; i++)
            book[i] = ge_v_s16(ctl, book_off + 8u + 2 * i, &bad);
        if (bad) {
            free(book);
            snprintf(msg, GE_AE_ERRLEN, "codebook is outside the ctl file");
            return -1;
        }
        w->nsamples = (uint32_t)(nframes * 16);
        w->pcm = (int16_t *)malloc((w->nsamples ? w->nsamples : 1) * sizeof *w->pcm);
        if (!w->pcm || adpcm_decode(tbl.p + base, nframes, book, npred, w->pcm)) {
            free(book);
            free(w->pcm);
            w->pcm = NULL;
            snprintf(msg, GE_AE_ERRLEN, "ADPCM decode failed (bad predictor index or out of memory)");
            return -1;
        }
        free(book);
        if (loop_off) {
            w->loop_start = ge_v_u32(ctl, loop_off, &bad);
            w->loop_end = ge_v_u32(ctl, loop_off + 4u, &bad);
            w->loop_count = ge_v_u32(ctl, loop_off + 8u, &bad);
        }
    } else {
        size_t i, n = (size_t)len / 2;
        w->nsamples = (uint32_t)n;
        w->pcm = (int16_t *)malloc((n ? n : 1) * sizeof *w->pcm);
        if (!w->pcm) {
            snprintf(msg, GE_AE_ERRLEN, "out of memory");
            return -1;
        }
        for (i = 0; i < n; i++)
            w->pcm[i] = (int16_t)(((unsigned)tbl.p[base + 2 * i] << 8) | tbl.p[base + 2 * i + 1]);
        if (loop_off) {
            w->loop_start = ge_v_u32(ctl, loop_off, &bad);
            w->loop_end = ge_v_u32(ctl, loop_off + 4u, &bad);
            w->loop_count = ge_v_u32(ctl, loop_off + 8u, &bad);
        }
    }
    if (bad) {
        free(w->pcm);
        w->pcm = NULL;
        snprintf(msg, GE_AE_ERRLEN, "loop record is outside the ctl file");
        return -1;
    }
    if (w->nsamples == 0) {
        free(w->pcm);
        w->pcm = NULL;
        snprintf(msg, GE_AE_ERRLEN, "empty sample");
        return -1;
    }
    if (loop_off && w->loop_count != 0) {
        if (w->loop_start < w->loop_end && w->loop_end <= w->nsamples)
            w->has_loop = 1;
        else {
            snprintf(msg, GE_AE_ERRLEN, "loop %u..%u outside the %u-sample wave", (unsigned)w->loop_start,
                     (unsigned)w->loop_end, (unsigned)w->nsamples);
            /* keep the sample, drop the loop; reported through bank->errors by the caller */
            w->valid = 1;
            return 1;
        }
    }
    w->valid = 1;
    return 0;
}

/* Find or decode the wave at `woff`. Returns the index in b->waves or -1. */
static int get_wave(ge_ae_bank *b, ge_view ctl, ge_view tbl, uint32_t woff, int *cap, char *msg)
{
    int i, rc;
    ge_ae_wave w;
    for (i = 0; i < b->nwaves; i++)
        if (b->waves[i].src_off == woff)
            return b->waves[i].valid ? i : -1;
    if (b->nwaves == *cap) {
        int nc = *cap ? *cap * 2 : 64;
        ge_ae_wave *nw = (ge_ae_wave *)realloc(b->waves, (size_t)nc * sizeof *nw);
        if (!nw) {
            snprintf(msg, GE_AE_ERRLEN, "out of memory");
            return -1;
        }
        b->waves = nw;
        *cap = nc;
    }
    rc = load_wave(ctl, tbl, woff, &w, msg);
    if (rc < 0) {
        w.valid = 0; /* remember the failure so the wave is not retried */
        w.src_off = woff;
        w.pcm = NULL;
        b->waves[b->nwaves++] = w;
        return -1;
    }
    if (w.type == 0)
        b->adpcm_waves++;
    else
        b->raw_waves++;
    if (w.has_loop)
        b->looped_waves++;
    b->waves[b->nwaves++] = w;
    if (rc == 1) /* wave kept, loop dropped: still an error to report */
        return -2 - (b->nwaves - 1);
    return b->nwaves - 1;
}

void ge_ae_bank_free(ge_ae_bank *b)
{
    int i;
    if (!b)
        return;
    for (i = 0; i < b->ninst; i++)
        free(b->inst[i].sounds);
    for (i = 0; i < b->nwaves; i++)
        free(b->waves[i].pcm);
    free(b->inst);
    free(b->waves);
    free(b);
}

ge_ae_bank *ge_ae_bank_load(const uint8_t *rom, size_t size, uint32_t ctl_off, uint32_t ctl_size, uint32_t tbl_off,
                            uint32_t tbl_size, int sfx, char *err)
{
    ge_view ctl, tbl;
    ge_ae_bank *b;
    int bad = 0, i, k, wcap = 0, nbanks;
    uint32_t boff;

    if ((uint64_t)ctl_off + ctl_size > size || (uint64_t)tbl_off + tbl_size > size) {
        ge_seterr(err, "bank files lie outside the ROM");
        return NULL;
    }
    ctl.p = rom + ctl_off;
    ctl.n = ctl_size;
    tbl.p = rom + tbl_off;
    tbl.n = tbl_size;

    if (ge_v_u16(ctl, 0, &bad) != 0x4231) {
        ge_seterr(err, "bank revision 0x%04X, expected 0x4231", (unsigned)ge_v_u16(ctl, 0, &bad));
        return NULL;
    }
    nbanks = (int)ge_v_s16(ctl, 2, &bad);
    if (nbanks != 1) {
        ge_seterr(err, "bank file declares %d banks, expected 1", nbanks);
        return NULL;
    }
    boff = ge_v_u32(ctl, 4, &bad);
    if (bad || !boff) {
        ge_seterr(err, "bank offset missing");
        return NULL;
    }

    b = (ge_ae_bank *)calloc(1, sizeof *b);
    if (!b) {
        ge_seterr(err, "out of memory");
        return NULL;
    }
    b->ninst = (int)ge_v_s16(ctl, boff, &bad);
    b->sample_rate = (int)ge_v_s32(ctl, boff + 4u, &bad);
    if (bad || b->ninst < 0 || b->ninst > MAX_INSTRUMENTS || b->sample_rate < 4000 || b->sample_rate > 96000) {
        ge_seterr(err, "bank header: %d instruments, sample rate %d", b->ninst, b->sample_rate);
        ge_ae_bank_free(b);
        return NULL;
    }
    b->inst = (ge_ae_inst *)calloc((size_t)(b->ninst ? b->ninst : 1), sizeof *b->inst);
    if (!b->inst) {
        ge_seterr(err, "out of memory");
        ge_ae_bank_free(b);
        return NULL;
    }

    for (i = 0; i < b->ninst; i++) {
        ge_ae_inst *in = &b->inst[i];
        uint32_t ioff = ge_v_u32(ctl, boff + 12u + 4u * (uint32_t)i, &bad);
        int ns;
        if (bad) {
            ge_seterr(err, "instrument offset table is outside the ctl file");
            ge_ae_bank_free(b);
            return NULL;
        }
        if (!ioff)
            continue; /* absent: an empty preset is still written */
        in->vol = (uint8_t)ge_v_u8(ctl, ioff, &bad);
        in->pan = (uint8_t)ge_v_u8(ctl, ioff + 1u, &bad);
        in->bend_cents = ge_v_s16(ctl, ioff + 12u, &bad);
        ns = (int)ge_v_s16(ctl, ioff + 14u, &bad);
        if (bad || ns < 0 || ns > MAX_SOUNDS) {
            ge_seterr(err, "instrument %d: bad header (%d sounds)", i, ns);
            ge_ae_bank_free(b);
            return NULL;
        }
        in->present = 1;
        in->nsounds = ns;
        in->sounds = (ge_ae_sound *)calloc((size_t)(ns ? ns : 1), sizeof *in->sounds);
        if (!in->sounds) {
            ge_seterr(err, "out of memory");
            ge_ae_bank_free(b);
            return NULL;
        }
        for (k = 0; k < ns; k++) {
            ge_ae_sound *s = &in->sounds[k];
            uint32_t soff = ge_v_u32(ctl, ioff + 16u + 4u * (uint32_t)k, &bad);
            uint32_t eoff, koff, woff;
            int sbad = 0, w;
            char msg[GE_AE_ERRLEN];
            if (bad) {
                bank_error(b, i, k, "sound offset table is outside the ctl file");
                break;
            }
            if (!soff)
                continue;
            s->present = 1;
            b->total_sounds++;
            eoff = ge_v_u32(ctl, soff, &sbad);
            koff = ge_v_u32(ctl, soff + 4u, &sbad);
            woff = ge_v_u32(ctl, soff + 8u, &sbad);
            s->pan = (uint8_t)ge_v_u8(ctl, soff + 12u, &sbad);
            s->vol = (uint8_t)ge_v_u8(ctl, soff + 13u, &sbad);
            if (sbad || !eoff || !koff || !woff) {
                bank_error(b, i, k, "sound record incomplete (missing envelope, key map or wave table)");
                continue;
            }
            s->attack_us = ge_v_s32(ctl, eoff, &sbad);
            s->decay_us = ge_v_s32(ctl, eoff + 4u, &sbad);
            s->release_us = ge_v_s32(ctl, eoff + 8u, &sbad);
            s->attack_vol = (uint8_t)ge_v_u8(ctl, eoff + 12u, &sbad);
            s->decay_vol = (uint8_t)ge_v_u8(ctl, eoff + 13u, &sbad);
            s->vel_min = (uint8_t)ge_v_u8(ctl, koff, &sbad);
            s->vel_max = (uint8_t)ge_v_u8(ctl, koff + 1u, &sbad);
            s->key_min = (uint8_t)ge_v_u8(ctl, koff + 2u, &sbad);
            s->key_max = (uint8_t)ge_v_u8(ctl, koff + 3u, &sbad);
            s->key_base = (uint8_t)ge_v_u8(ctl, koff + 4u, &sbad);
            s->detune = (int8_t)ge_v_u8(ctl, koff + 5u, &sbad);
            if (sbad) {
                bank_error(b, i, k, "envelope or key map is outside the ctl file");
                continue;
            }
            /* Instrument bank: a real key map. The SFX bank reuses these bytes as effect
             * parameters (docs/specs/audio.md), so they are kept raw there. */
            if (!sfx && (s->vel_min > s->vel_max || s->key_min > s->key_max || s->vel_max > 127 || s->key_max > 127 ||
                         s->key_base > 127)) {
                bank_error(b, i, k, "key map out of range");
                continue;
            }
            msg[0] = 0;
            w = get_wave(b, ctl, tbl, woff, &wcap, msg);
            if (w <= -2) { /* wave kept without its loop */
                w = -2 - w;
                bank_error(b, i, k, msg);
                b->waves[w].has_loop = 0;
            }
            if (w < 0) {
                bank_error(b, i, k, msg[0] ? msg : "wave table failed earlier");
                continue;
            }
            s->wave = w;
            s->valid = 1;
            if (b->waves[w].has_loop && b->waves[w].loop_count != LOOP_FOREVER && b->finite_loop_n < 64) {
                ge_ae_finite_loop *fl = &b->finite_loops[b->finite_loop_n++];
                fl->inst_index = (uint32_t)i;
                fl->sound_index = (uint32_t)k;
                fl->count = b->waves[w].loop_count;
            }
        }
    }
    return b;
}

/* ---- WAV ------------------------------------------------------------------------ */
int ge_ae_sound_to_wav(const ge_ae_bank *b, int inst, int sound, uint32_t rate_hz, ge_ae_buf *out, int *peak)
{
    const ge_ae_sound *s;
    const ge_ae_wave *w;
    uint32_t i, bytes;
    int pk = 0;

    if (inst < 0 || inst >= b->ninst || !b->inst[inst].present || sound < 0 || sound >= b->inst[inst].nsounds)
        return -1;
    s = &b->inst[inst].sounds[sound];
    if (!s->valid)
        return -1;
    w = &b->waves[s->wave];
    bytes = w->nsamples * 2;
    out->data = NULL;
    out->size = out->cap = 0;
    /* A looped wave carries its loop points in a standard 'smpl' chunk (continuous loop). */
    if (ge_buf_put(out, "RIFF", 4) || ge_buf_le32(out, 36 + bytes + (w->has_loop ? 68 : 0)) ||
        ge_buf_put(out, "WAVEfmt ", 8) || ge_buf_le32(out, 16) || ge_buf_le16(out, 1) || ge_buf_le16(out, 1) ||
        ge_buf_le32(out, rate_hz) || ge_buf_le32(out, rate_hz * 2) || ge_buf_le16(out, 2) || ge_buf_le16(out, 16) ||
        ge_buf_put(out, "data", 4) || ge_buf_le32(out, bytes)) {
        ge_ae_buf_free(out);
        return -1;
    }
    for (i = 0; i < w->nsamples; i++) {
        int v = w->pcm[i];
        if (ge_buf_le16(out, (uint32_t)v & 0xFFFF)) {
            ge_ae_buf_free(out);
            return -1;
        }
        if (v < 0)
            v = -v;
        if (v > pk)
            pk = v;
    }
    if (w->has_loop) {
        if (ge_buf_put(out, "smpl", 4) || ge_buf_le32(out, 60) || ge_buf_le32(out, 0) || ge_buf_le32(out, 0) ||
            ge_buf_le32(out, 1000000000u / rate_hz) || ge_buf_le32(out, 60) || ge_buf_le32(out, 0) ||
            ge_buf_le32(out, 0) || ge_buf_le32(out, 0) || ge_buf_le32(out, 1) || ge_buf_le32(out, 0) ||
            ge_buf_le32(out, 0) || ge_buf_le32(out, 0) || ge_buf_le32(out, w->loop_start) ||
            ge_buf_le32(out, w->loop_end) || ge_buf_le32(out, 0) || ge_buf_le32(out, 0)) {
            ge_ae_buf_free(out);
            return -1;
        }
    }
    if (peak)
        *peak = pk;
    return 0;
}
