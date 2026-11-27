/*
 * SFX effects: decoded parameters (docs/specs/audio.md, "The SFX bank: the key map is not a
 * key map") and sfx.json.
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "ge_audio_internal.h"

#define SFX_CENTRE_KEY_CENTS 6000 /* key 60 at detune 0 plays at the recorded rate */
#define SFX_FLAG_RETRIGGER 0x1    /* 0x10 in the key max byte */
#define SFX_FLAG_SLIDE 0x2        /* 0x20 */

static const ge_ae_sound *sfx_sound(const ge_ae_bank *b, int sound)
{
    if (b->ninst < 1 || !b->inst[0].present || sound < 0 || sound >= b->inst[0].nsounds)
        return NULL;
    return &b->inst[0].sounds[sound];
}

int ge_ae_sfx_params(const ge_ae_bank *b, int sound, ge_ae_sfx *o)
{
    const ge_ae_sound *s = sfx_sound(b, sound);
    int cents;
    if (!s || !s->valid)
        return -1;
    memset(o, 0, sizeof *o);
    o->id = sound + 1;
    o->flags = s->key_max >> 4;
    o->retrigger = (o->flags & SFX_FLAG_RETRIGGER) != 0;
    cents = s->key_base * 100 - SFX_CENTRE_KEY_CENTS;
    if (o->flags & SFX_FLAG_SLIDE) { /* the detune is a per-step slide, not part of the start pitch */
        o->has_slide = 1;
        o->slide_cents = s->detune;
    } else {
        cents += s->detune;
    }
    o->pitch_cents = cents;
    o->rate_hz = (uint32_t)lrint((double)b->sample_rate * pow(2.0, cents / 1200.0));
    if (o->rate_hz < 1)
        o->rate_hz = 1;
    o->volume_group = s->key_min & 0x3F;
    o->reverb = s->key_max & 0x0F;
    o->next = s->vel_min + (s->key_min & 0xC0) * 4;
    o->next_delay_ms = (int)lrint(s->vel_max * 33.333);
    return 0;
}

static int jprintf(ge_ae_buf *out, const char *fmt, ...)
{
    char tmp[1024];
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if (n < 0 || n >= (int)sizeof tmp)
        return -1;
    return ge_buf_put(out, tmp, (size_t)n);
}

int ge_ae_sfx_json(const ge_ae_bank *b, ge_ae_buf *out, ge_ae_sfx_stats *st)
{
    int n = b->ninst > 0 ? b->inst[0].nsounds : 0, k, first = 1;
    unsigned char *state = (unsigned char *)calloc((size_t)(n + 2), 1);
    unsigned char *targeted = (unsigned char *)calloc((size_t)(n + 2), 1);

    memset(st, 0, sizeof *st);
    out->data = NULL;
    out->size = out->cap = 0;
    if (!state || !targeted || jprintf(out, "[\n"))
        goto fail;
    for (k = 0; k < n; k++) {
        const ge_ae_sound *s = &b->inst[0].sounds[k];
        const ge_ae_wave *w;
        ge_ae_sfx p;
        if (ge_ae_sfx_params(b, k, &p))
            continue;
        w = &b->waves[s->wave];
        st->effects++;
        st->flag_count[p.flags & 15]++;
        st->retrigger += p.retrigger;
        st->slides += p.has_slide;
        st->held += s->decay_us == -1;
        st->loops += w->has_loop;
        if (jprintf(out, "%s  {\"id\": %d, \"wav\": \"sfx_%04d.wav\", \"rate_hz\": %u, \"pitch_cents\": %d, ", first ? "" : ",\n",
                    p.id, p.id, (unsigned)p.rate_hz, p.pitch_cents) ||
            jprintf(out, "\"volume_group\": %d, \"reverb\": %d, \"flags\": %d, \"retrigger\": %s, ", p.volume_group, p.reverb,
                    p.flags, p.retrigger ? "true" : "false"))
            goto fail;
        if (p.has_slide ? jprintf(out, "\"pitch_slide_cents\": %d, ", p.slide_cents) : jprintf(out, "\"pitch_slide_cents\": null, "))
            goto fail;
        if (jprintf(out, "\"next\": %d, \"next_delay_ms\": %d, \"sample_volume\": %d, \"pan\": %d, ", p.next, p.next_delay_ms,
                    s->vol, s->pan) ||
            jprintf(out,
                    "\"envelope\": {\"attack_us\": %d, \"attack_volume\": %d, \"decay_us\": %d, \"decay_volume\": %d, "
                    "\"release_us\": %d}, ",
                    (int)s->attack_us, s->attack_vol, (int)s->decay_us, s->decay_vol, (int)s->release_us))
            goto fail;
        if (w->has_loop ? jprintf(out, "\"loop\": {\"start\": %u, \"end\": %u}}", (unsigned)w->loop_start, (unsigned)w->loop_end)
                        : jprintf(out, "\"loop\": null}"))
            goto fail;
        first = 0;
    }
    if (jprintf(out, "\n]\n"))
        goto fail;

    for (k = 0; k < n; k++) {
        ge_ae_sfx p;
        if (!ge_ae_sfx_params(b, k, &p) && p.next > 0 && p.next <= n)
            targeted[p.next] = 1;
    }
    /* chains: targets must exist; walk each chain, a revisit is a cycle */
    for (k = 0; k < n; k++) {
        ge_ae_sfx p;
        int cur, len = 0, seen_ret = 0;
        if (ge_ae_sfx_params(b, k, &p) || !p.next)
            continue;
        st->links++;
        st->chains += !targeted[k + 1];
        memset(state, 0, (size_t)(n + 2));
        cur = k + 1;
        while (cur) {
            ge_ae_sfx q;
            if (cur > n || ge_ae_sfx_params(b, cur - 1, &q)) {
                st->bad_next++;
                break;
            }
            if (state[cur]) { /* revisited */
                if (cur == k + 1) {
                    st->cycles++;
                    if (!seen_ret)
                        st->cycles_without_retrigger++;
                }
                break;
            }
            state[cur] = 1;
            seen_ret |= q.retrigger;
            len++;
            cur = q.next;
        }
        if (len > st->longest_chain)
            st->longest_chain = len;
    }
    free(state);
    free(targeted);
    return 0;
fail:
    free(state);
    free(targeted);
    ge_ae_buf_free(out);
    return -1;
}
