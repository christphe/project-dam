/*
 * midiplay: small interactive player for the exported GoldenEye music.
 *
 *   midiplay [--rate 44100|22050] <export_dir> [track]
 *   midiplay --render <export_dir> <track> <out.wav> [seconds]
 *   midiplay --check <export_dir>
 *
 * --check loads every music/track_NN.mid and both .sf2 files and prints what the
 * loaders found (used by automated checks).
 *
 * Plays <export_dir>/music/track_NN.mid with <export_dir>/instruments.sf2 through
 * TinySoundFont / TinyMidiLoader. Both files are watched: when one changes on disk
 * it is reloaded and playback continues at the same position, so the exporter can
 * be re-run while listening. Channel 10 is an ordinary melodic channel (the bank
 * has no drum meaning).
 *
 * Keys: Right/N next track, Left/P previous, R restart, Space stop/play,
 *       L loop on/off, Up/Down volume, Q/Esc quit.
 */
#define SDL_MAIN_HANDLED
#include <SDL.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define TSF_IMPLEMENTATION
#include "tsf.h"
#define TML_IMPLEMENTATION
#include "tml.h"

#define MAX_TRACKS 128
#define BLOCK_FRAMES 64          /* TSF render granularity; messages are dispatched per block */
#define BASE_GAIN_DB (-9.0f)     /* headroom: sequences stack many voices */
#define MAX_VOICES 128
#define TAIL_MS 3000.0           /* let releases ring out after the last message */
#define POLL_MS 400              /* file change polling interval */

/* ---- files ------------------------------------------------------------------- */
typedef struct {
    long long mtime;
    long long size;
} filestamp;

static int stamp_of(const char *path, filestamp *fs)
{
    struct stat st;
    if (stat(path, &st) != 0)
        return -1;
    fs->mtime = (long long)st.st_mtime;
    fs->size = (long long)st.st_size;
    return 0;
}

static int stamp_eq(const filestamp *a, const filestamp *b)
{
    return a->mtime == b->mtime && a->size == b->size;
}

static unsigned char *read_file(const char *path, int *size)
{
    FILE *f = fopen(path, "rb");
    unsigned char *p;
    long n;
    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) || (n = ftell(f)) <= 0 || fseek(f, 0, SEEK_SET)) {
        fclose(f);
        return NULL;
    }
    p = (unsigned char *)malloc((size_t)n);
    if (!p || fread(p, 1, (size_t)n, f) != (size_t)n) {
        free(p);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *size = (int)n;
    return p;
}

/* ---- SMF scan: loop markers in milliseconds ------------------------------------
 * TinyMidiLoader drops marker events, so the loopStart / loopEnd markers the
 * exporter writes are found here. Loop region: from the earliest loopStart to the
 * latest loopEnd (or to the end of the music when there is no loopEnd). */
typedef struct {
    int has_loop;
    double start_ms, end_ms; /* end_ms < 0: until the end of the music */
} loopinfo;

typedef struct {
    unsigned tick, us;
} tempo_ev;

static unsigned be16(const unsigned char *p) { return (unsigned)(p[0] << 8 | p[1]); }
static unsigned be32(const unsigned char *p) { return (unsigned)p[0] << 24 | (unsigned)p[1] << 16 | (unsigned)p[2] << 8 | p[3]; }

static int tempo_cmp(const void *a, const void *b)
{
    const tempo_ev *x = (const tempo_ev *)a, *y = (const tempo_ev *)b;
    return x->tick < y->tick ? -1 : (x->tick > y->tick);
}

static double ticks_to_ms(const tempo_ev *t, int nt, unsigned division, unsigned tick)
{
    double ms = 0.0;
    unsigned last = 0, us = 500000;
    int i;
    for (i = 0; i < nt && t[i].tick < tick; i++) {
        ms += (double)(t[i].tick - last) * us / division / 1000.0;
        last = t[i].tick;
        us = t[i].us;
    }
    return ms + (double)(tick - last) * us / division / 1000.0;
}

static loopinfo scan_loops(const unsigned char *d, int n)
{
    loopinfo li = {0, 0.0, -1.0};
    tempo_ev tempi[256];
    int nt = 0, ntrk, t;
    size_t p = 14;
    unsigned division, min_start = 0xFFFFFFFFu, max_end = 0;
    int have_start = 0, have_end = 0;

    if (n < 14 || memcmp(d, "MThd", 4) != 0)
        return li;
    ntrk = (int)be16(d + 10);
    division = be16(d + 12);
    if (!division)
        return li;
    for (t = 0; t < ntrk && p + 8 <= (size_t)n; t++) {
        size_t q, e;
        unsigned tick = 0;
        int running = 0;
        if (memcmp(d + p, "MTrk", 4) != 0)
            break;
        q = p + 8;
        e = q + be32(d + p + 4);
        if (e > (size_t)n)
            e = (size_t)n;
        p = e;
        while (q < e) {
            unsigned v = 0;
            int i = 0, st;
            do {
                if (q >= e || i++ == 4)
                    goto next_track;
                v = (v << 7) | (d[q] & 0x7F);
            } while (d[q++] & 0x80);
            tick += v;
            if (q >= e)
                break;
            st = d[q];
            if (st == 0xFF) {
                unsigned type, len;
                if (q + 3 > e)
                    break;
                type = d[q + 1];
                len = d[q + 2];
                q += 3;
                if (q + len > e)
                    break;
                if (type == 0x51 && len == 3 && nt < 256) {
                    tempi[nt].tick = tick;
                    tempi[nt++].us = (unsigned)d[q] << 16 | (unsigned)d[q + 1] << 8 | d[q + 2];
                } else if (type == 0x06) {
                    if (len == 9 && memcmp(d + q, "loopStart", 9) == 0) {
                        have_start = 1;
                        if (tick < min_start)
                            min_start = tick;
                    } else if (len == 7 && memcmp(d + q, "loopEnd", 7) == 0) {
                        have_end = 1;
                        if (tick > max_end)
                            max_end = tick;
                    }
                }
                q += len;
                running = 0;
            } else {
                if (st & 0x80) {
                    running = st;
                    q++;
                } else if (!running) {
                    break;
                }
                q += ((running & 0xF0) == 0xC0 || (running & 0xF0) == 0xD0) ? 1 : 2;
            }
        }
    next_track:;
    }
    if (!have_start)
        return li;
    qsort(tempi, (size_t)nt, sizeof tempi[0], tempo_cmp);
    li.has_loop = 1;
    li.start_ms = ticks_to_ms(tempi, nt, division, min_start);
    li.end_ms = have_end ? ticks_to_ms(tempi, nt, division, max_end) : -1.0;
    if (have_end && li.end_ms <= li.start_ms)
        li.has_loop = 0;
    return li;
}

/* ---- the player ------------------------------------------------------------------ */
typedef struct {
    tsf *sf;
    tml_message *mid, *cur;
    loopinfo loop;
    double msec, length_ms;
    int rate;
    int playing;
    int loop_on;
    float volume;
} player;

static void apply_state(tsf *sf, const tml_message *m)
{
    int ch = m->channel;
    switch (m->type) {
    case TML_PROGRAM_CHANGE:
        /* Bank 0, program = instrument index. Not tsf_channel_set_presetnumber: that one
         * applies the drum rules to channel 10 and the bank has no drums. */
        tsf_channel_set_bank_preset(sf, ch, 0, (unsigned char)m->program);
        break;
    case TML_CONTROL_CHANGE:
        tsf_channel_midi_control(sf, ch, (unsigned char)m->control, (unsigned char)m->control_value);
        break;
    case TML_PITCH_BEND:
        tsf_channel_set_pitchwheel(sf, ch, m->pitch_bend);
        break;
    default:
        break;
    }
}

static void dispatch(tsf *sf, const tml_message *m)
{
    int ch = m->channel;
    switch (m->type) {
    case TML_NOTE_ON:
        tsf_channel_note_on(sf, ch, (unsigned char)m->key, (unsigned char)m->velocity / 127.0f);
        break;
    case TML_NOTE_OFF:
        tsf_channel_note_off(sf, ch, (unsigned char)m->key);
        break;
    default:
        apply_state(sf, m);
        break;
    }
}

/* Channel state (programs, controllers, bends) as it is at `ms`, without sounding notes. */
static void replay_state(tsf *sf, tml_message *mid, double ms, tml_message **cur_out)
{
    tml_message *m = mid;
    while (m && m->time < ms) {
        apply_state(sf, m);
        m = m->next;
    }
    *cur_out = m;
}

static tsf *new_synth(tsf *loaded, int rate)
{
    int ch;
    tsf_set_output(loaded, TSF_STEREO_INTERLEAVED, rate, BASE_GAIN_DB);
    tsf_set_max_voices(loaded, MAX_VOICES);
    for (ch = 0; ch < 16; ch++)
        tsf_channel_set_bank_preset(loaded, ch, 0, 0);
    return loaded;
}

static void seek(player *p, double ms)
{
    tsf_reset(p->sf);
    new_synth(p->sf, p->rate);
    p->msec = ms;
    replay_state(p->sf, p->mid, ms, &p->cur);
}

static double music_length_ms(tml_message *m)
{
    unsigned len = 0;
    tml_get_info(m, NULL, NULL, NULL, NULL, &len);
    return (double)len;
}

/* Render `frames` stereo float frames. Called from the audio thread (device locked
 * for every state change made by the main thread). */
static void player_render(player *p, float *out, int frames)
{
    int done = 0;
    if (!p->sf || !p->playing) {
        memset(out, 0, (size_t)frames * 2 * sizeof(float));
        return;
    }
    while (done < frames) {
        int n = frames - done > BLOCK_FRAMES ? BLOCK_FRAMES : frames - done;
        int looping = p->loop_on && p->loop.has_loop;
        double loop_end = p->loop.end_ms >= 0 ? p->loop.end_ms : p->length_ms;

        p->msec += n * 1000.0 / p->rate;
        while (p->cur && p->cur->time <= p->msec) {
            if (looping && p->cur->time >= loop_end)
                break;
            dispatch(p->sf, p->cur);
            p->cur = p->cur->next;
        }
        if (looping && p->msec >= loop_end) {
            /* back to the start of the loop; channel state carries over */
            double over = p->msec - loop_end;
            tml_message *m = p->mid;
            tsf_note_off_all(p->sf);
            while (m && m->time < p->loop.start_ms)
                m = m->next;
            p->cur = m;
            p->msec = p->loop.start_ms + over;
        } else if (!p->cur && p->msec > p->length_ms + TAIL_MS) {
            if (p->loop_on) { /* no loop markers: loop the whole piece */
                tsf_note_off_all(p->sf);
                p->cur = p->mid;
                p->msec = 0.0;
            } else {
                p->playing = 0;
            }
        }
        tsf_render_float(p->sf, out + (size_t)done * 2, n, 0);
        done += n;
    }
    if (p->volume != 1.0f) {
        int i;
        for (i = 0; i < frames * 2; i++)
            out[i] *= p->volume;
    }
}

/* ---- loading ------------------------------------------------------------------------ */
static int load_synth(const char *path, int rate, tsf **out)
{
    int n;
    unsigned char *d = read_file(path, &n);
    tsf *sf;
    if (!d)
        return -1;
    sf = tsf_load_memory(d, n);
    free(d);
    if (!sf)
        return -1;
    *out = new_synth(sf, rate);
    return 0;
}

static int load_midi(const char *path, tml_message **mid, loopinfo *li)
{
    int n;
    unsigned char *d = read_file(path, &n);
    if (!d)
        return -1;
    *mid = tml_load_memory(d, n);
    if (*mid)
        *li = scan_loops(d, n);
    free(d);
    return *mid ? 0 : -1;
}

/* ---- --check ----------------------------------------------------------------------------- */
static int check_mode(const char *dir)
{
    char path[1024];
    int t, bad = 0, nfiles = 0;
    const char *banks[2] = {"instruments.sf2", "sfx.sf2"};

    for (t = 0; t < 2; t++) {
        int n;
        unsigned char *d;
        tsf *sf;
        snprintf(path, sizeof path, "%s/%s", dir, banks[t]);
        d = read_file(path, &n);
        sf = d ? tsf_load_memory(d, n) : NULL;
        if (!sf) {
            printf("%s: LOAD FAILED\n", banks[t]);
            bad++;
        } else {
            printf("%s: %d presets\n", banks[t], tsf_get_presetcount(sf));
            tsf_close(sf);
        }
        free(d);
    }
    for (t = 0; t < MAX_TRACKS; t++) {
        int n, notes = 0, channels = 0, programs = 0, msgs = 0, tempos = 0;
        unsigned first = 0, len = 0;
        unsigned char *d;
        tml_message *m, *p;
        loopinfo li;
        snprintf(path, sizeof path, "%s/music/track_%02d.mid", dir, t);
        d = read_file(path, &n);
        if (!d)
            continue;
        nfiles++;
        m = tml_load_memory(d, n);
        if (!m) {
            printf("track_%02d.mid: LOAD FAILED\n", t);
            bad++;
            free(d);
            continue;
        }
        li = scan_loops(d, n);
        tml_get_info(m, &channels, &programs, &notes, &first, &len);
        for (p = m; p; p = p->next) {
            msgs++;
            tempos += p->type == TML_SET_TEMPO;
        }
        printf("track_%02d.mid: messages %d notes %d tempo %d channels %d programs %d length %.1f s loop %s\n", t, msgs,
               notes, tempos, channels, programs, len / 1000.0, li.has_loop ? "yes" : "no");
        tml_free(m);
        free(d);
    }
    printf("checked %d midi files, %d failures\n", nfiles, bad);
    return bad ? 1 : 0;
}

/* ---- offline render ------------------------------------------------------------------- */
static void put_le32(unsigned char *p, unsigned v)
{
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
}

static int render_mode(const char *dir, int track, const char *wav_path, double seconds, int rate)
{
    char path[1024];
    player p;
    float *buf;
    short *pcm;
    long total, i, clipped = 0, second_frames = rate;
    FILE *f;
    unsigned char hdr[44];
    double rms_acc = 0.0, peak = 0.0;
    long sec_n = 0;
    int sec = 0;

    memset(&p, 0, sizeof p);
    p.rate = rate;
    p.volume = 1.0f;
    p.loop_on = 1;
    p.playing = 1;
    snprintf(path, sizeof path, "%s/instruments.sf2", dir);
    if (load_synth(path, rate, &p.sf)) {
        fprintf(stderr, "cannot load %s\n", path);
        return 1;
    }
    snprintf(path, sizeof path, "%s/music/track_%02d.mid", dir, track);
    if (load_midi(path, &p.mid, &p.loop)) {
        fprintf(stderr, "cannot load %s\n", path);
        return 1;
    }
    p.length_ms = music_length_ms(p.mid);
    p.cur = p.mid;
    printf("track %d: %d presets, length %.1f s, loop %s (%.1f..%.1f s)\n", track, tsf_get_presetcount(p.sf),
           p.length_ms / 1000.0, p.loop.has_loop ? "yes" : "no", p.loop.start_ms / 1000.0,
           (p.loop.end_ms < 0 ? p.length_ms : p.loop.end_ms) / 1000.0);

    total = (long)(seconds * rate);
    buf = (float *)malloc((size_t)total * 2 * sizeof(float));
    pcm = (short *)malloc((size_t)total * 2 * sizeof(short));
    if (!buf || !pcm)
        return 1;
    player_render(&p, buf, (int)total);
    for (i = 0; i < total * 2; i++) {
        float v = buf[i];
        double a = fabs((double)v);
        if (a >= 1.0)
            clipped++;
        if (v > 1.0f)
            v = 1.0f;
        if (v < -1.0f)
            v = -1.0f;
        pcm[i] = (short)lrintf(v * 32767.0f);
        rms_acc += (double)buf[i] * buf[i];
        if (a > peak)
            peak = a;
        if (++sec_n == second_frames * 2) {
            if (sec < 10)
                printf("  second %2d: rms %.4f  peak %.4f\n", sec + 1, sqrt(rms_acc / sec_n), peak);
            sec++;
            sec_n = 0;
            rms_acc = 0.0;
            peak = 0.0;
        }
    }
    printf("clipped samples: %ld of %ld (%.3f%%)\n", clipped, total * 2, 100.0 * clipped / (double)(total * 2));

    f = fopen(wav_path, "wb");
    if (!f) {
        fprintf(stderr, "cannot write %s\n", wav_path);
        return 1;
    }
    memcpy(hdr, "RIFF", 4);
    put_le32(hdr + 4, 36 + (unsigned)(total * 4));
    memcpy(hdr + 8, "WAVEfmt ", 8);
    put_le32(hdr + 16, 16);
    hdr[20] = 1; hdr[21] = 0; hdr[22] = 2; hdr[23] = 0;
    put_le32(hdr + 24, (unsigned)rate);
    put_le32(hdr + 28, (unsigned)rate * 4);
    hdr[32] = 4; hdr[33] = 0; hdr[34] = 16; hdr[35] = 0;
    memcpy(hdr + 36, "data", 4);
    put_le32(hdr + 40, (unsigned)(total * 4));
    fwrite(hdr, 1, 44, f);
    fwrite(pcm, 2, (size_t)total * 2, f);
    fclose(f);
    printf("wrote %s\n", wav_path);
    tml_free(p.mid);
    tsf_close(p.sf);
    free(buf);
    free(pcm);
    return 0;
}

/* ---- interactive ------------------------------------------------------------------------- */
typedef struct {
    player p;
    SDL_AudioDeviceID dev;
    char dir[900];
    int tracks[MAX_TRACKS], ntracks, ti; /* existing track numbers, index into them */
    filestamp sf_stamp, mid_stamp;
} app;

static void audio_cb(void *ud, Uint8 *stream, int len)
{
    player_render((player *)ud, (float *)stream, len / (int)(2 * sizeof(float)));
}

static void midi_path(const app *a, char *path, size_t n)
{
    snprintf(path, n, "%s/music/track_%02d.mid", a->dir, a->tracks[a->ti]);
}

static void scan_tracks(app *a)
{
    int t, keep = a->ntracks ? a->tracks[a->ti] : -1;
    char path[1024];
    a->ntracks = 0;
    for (t = 0; t < MAX_TRACKS; t++) {
        filestamp fs;
        snprintf(path, sizeof path, "%s/music/track_%02d.mid", a->dir, t);
        if (stamp_of(path, &fs) == 0)
            a->tracks[a->ntracks++] = t;
    }
    a->ti = 0;
    for (t = 0; t < a->ntracks; t++)
        if (a->tracks[t] == keep)
            a->ti = t;
}

/* Load the current track from disk; restart from `ms` (0 = from the beginning). */
static int open_track(app *a, double ms, int keep_playing)
{
    char path[1024];
    tml_message *mid = NULL;
    loopinfo li = {0, 0, -1};
    filestamp fs;

    midi_path(a, path, sizeof path);
    if (stamp_of(path, &fs) || load_midi(path, &mid, &li))
        return -1;
    SDL_LockAudioDevice(a->dev);
    tml_free(a->p.mid);
    a->p.mid = mid;
    a->p.loop = li;
    a->p.length_ms = music_length_ms(mid);
    seek(&a->p, ms);
    a->p.playing = keep_playing;
    SDL_UnlockAudioDevice(a->dev);
    a->mid_stamp = fs;
    return 0;
}

static void poll_reload(app *a)
{
    char path[1024];
    filestamp fs;

    snprintf(path, sizeof path, "%s/instruments.sf2", a->dir);
    if (stamp_of(path, &fs) == 0 && !stamp_eq(&fs, &a->sf_stamp)) {
        tsf *sf = NULL;
        if (load_synth(path, a->p.rate, &sf) == 0) {
            tsf *old;
            SDL_LockAudioDevice(a->dev);
            old = a->p.sf;
            a->p.sf = sf;
            replay_state(sf, a->p.mid, a->p.msec, &a->p.cur);
            SDL_UnlockAudioDevice(a->dev);
            tsf_close(old);
            a->sf_stamp = fs;
            printf("reloaded instruments.sf2 (%d presets)\n", tsf_get_presetcount(sf));
        } /* else: probably mid-write; the stamp stays old so it is retried */
    }
    midi_path(a, path, sizeof path);
    if (stamp_of(path, &fs) == 0 && !stamp_eq(&fs, &a->mid_stamp)) {
        double at = a->p.msec; /* approximately current; read without the lock on purpose, only a hint */
        if (open_track(a, at, a->p.playing) == 0)
            printf("reloaded track_%02d.mid\n", a->tracks[a->ti]);
    }
}

static void fmt_time(char *s, size_t n, double ms)
{
    int sec = (int)(ms / 1000.0);
    snprintf(s, n, "%d:%02d", sec / 60, sec % 60);
}

static void update_title(app *a, SDL_Window *w)
{
    char title[256], t1[16], t2[16];
    int voices;
    double ms, len;
    SDL_LockAudioDevice(a->dev);
    voices = a->p.sf ? tsf_active_voice_count(a->p.sf) : 0;
    ms = a->p.msec;
    len = a->p.length_ms;
    SDL_UnlockAudioDevice(a->dev);
    fmt_time(t1, sizeof t1, ms);
    fmt_time(t2, sizeof t2, len);
    snprintf(title, sizeof title, "midiplay  track_%02d.mid [%d/%d]  %s / %s  voices %d  loop %s%s  vol %d%%  %s",
             a->tracks[a->ti], a->ti + 1, a->ntracks, t1, t2, voices, a->p.loop_on ? "on" : "off",
             a->p.loop.has_loop ? "" : " (no markers)", (int)lrintf(a->p.volume * 100.0f), a->p.playing ? "playing" : "stopped");
    SDL_SetWindowTitle(w, title);
}

static void draw(app *a, SDL_Renderer *r)
{
    int w, h;
    double frac;
    SDL_Rect bar, fill;
    SDL_GetRendererOutputSize(r, &w, &h);
    SDL_SetRenderDrawColor(r, 24, 26, 32, 255);
    SDL_RenderClear(r);
    SDL_LockAudioDevice(a->dev);
    frac = a->p.length_ms > 0 ? a->p.msec / a->p.length_ms : 0;
    SDL_UnlockAudioDevice(a->dev);
    if (frac > 1)
        frac = 1;
    bar.x = 16; bar.y = 16; bar.w = w - 32; bar.h = 16;
    SDL_SetRenderDrawColor(r, 56, 60, 72, 255);
    SDL_RenderFillRect(r, &bar);
    fill = bar;
    fill.w = (int)(bar.w * frac);
    SDL_SetRenderDrawColor(r, 90, 170, 255, 255);
    SDL_RenderFillRect(r, &fill);
    bar.y = 44; bar.h = 10;
    SDL_SetRenderDrawColor(r, 56, 60, 72, 255);
    SDL_RenderFillRect(r, &bar);
    fill = bar;
    fill.w = (int)(bar.w * (a->p.volume > 1.0f ? 1.0f : a->p.volume));
    SDL_SetRenderDrawColor(r, 120, 210, 120, 255);
    SDL_RenderFillRect(r, &fill);
    SDL_RenderPresent(r);
}

static int interactive(const char *dir, int first_track, int rate)
{
    app a;
    SDL_AudioSpec want, have;
    SDL_Window *win;
    SDL_Renderer *ren;
    char path[1024];
    int running = 1, t;
    Uint32 last_poll = 0, last_title = 0;

    memset(&a, 0, sizeof a);
    snprintf(a.dir, sizeof a.dir, "%s", dir);
    a.p.volume = 0.8f;
    a.p.loop_on = 1;

    if (SDL_Init(SDL_INIT_AUDIO | SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    scan_tracks(&a);
    if (!a.ntracks) {
        fprintf(stderr, "no music/track_NN.mid in %s (run ge007-audio-export first)\n", dir);
        return 1;
    }
    for (t = 0; t < a.ntracks; t++)
        if (a.tracks[t] == first_track)
            a.ti = t;
    if (first_track < 0)
        for (t = 0; t < a.ntracks; t++)
            if (a.tracks[t] >= 1) { /* track 0 is the "no music" placeholder */
                a.ti = t;
                break;
            }

    SDL_zero(want);
    want.freq = rate;
    want.format = AUDIO_F32SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = audio_cb;
    want.userdata = &a.p;
    a.dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!a.dev) {
        fprintf(stderr, "SDL_OpenAudioDevice: %s\n", SDL_GetError());
        return 1;
    }
    a.p.rate = have.freq;

    snprintf(path, sizeof path, "%s/instruments.sf2", dir);
    if (stamp_of(path, &a.sf_stamp) || load_synth(path, a.p.rate, &a.p.sf)) {
        fprintf(stderr, "cannot load %s\n", path);
        return 1;
    }
    printf("%s: %d presets, audio %d Hz %s\n", path, tsf_get_presetcount(a.p.sf), have.freq,
           have.format == AUDIO_F32SYS ? "float" : "other format");
    if (open_track(&a, 0.0, 1)) {
        fprintf(stderr, "cannot load the track\n");
        return 1;
    }
    printf("playing track_%02d.mid  (%.1f s, loop markers %s)\n", a.tracks[a.ti], a.p.length_ms / 1000.0,
           a.p.loop.has_loop ? "found" : "none");
    printf("keys: Right/N next  Left/P previous  R restart  Space stop/play  L loop  Up/Down volume  Q quit\n");

    win = SDL_CreateWindow("midiplay", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 560, 72, 0);
    ren = win ? SDL_CreateRenderer(win, -1, 0) : NULL;
    if (!win || !ren) {
        fprintf(stderr, "window: %s\n", SDL_GetError());
        return 1;
    }
    SDL_PauseAudioDevice(a.dev, 0);

    while (running) {
        SDL_Event e;
        Uint32 now = SDL_GetTicks();
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                running = 0;
            } else if (e.type == SDL_KEYDOWN && !e.key.repeat) {
                int step = 0;
                switch (e.key.keysym.sym) {
                case SDLK_q:
                case SDLK_ESCAPE:
                    running = 0;
                    break;
                case SDLK_RIGHT:
                case SDLK_n:
                    step = 1;
                    break;
                case SDLK_LEFT:
                case SDLK_p:
                    step = -1;
                    break;
                case SDLK_r:
                    open_track(&a, 0.0, 1);
                    break;
                case SDLK_SPACE:
                    SDL_LockAudioDevice(a.dev);
                    a.p.playing = !a.p.playing;
                    if (!a.p.playing)
                        tsf_note_off_all(a.p.sf);
                    SDL_UnlockAudioDevice(a.dev);
                    break;
                case SDLK_l:
                    SDL_LockAudioDevice(a.dev);
                    a.p.loop_on = !a.p.loop_on;
                    SDL_UnlockAudioDevice(a.dev);
                    break;
                default:
                    break;
                }
                if (step) {
                    a.ti = (a.ti + step + a.ntracks) % a.ntracks;
                    if (open_track(&a, 0.0, 1) == 0)
                        printf("track_%02d.mid  %.1f s\n", a.tracks[a.ti], a.p.length_ms / 1000.0);
                }
            }
        }
        {
            const Uint8 *ks = SDL_GetKeyboardState(NULL);
            float v = a.p.volume;
            if (ks[SDL_SCANCODE_UP])
                v += 0.01f;
            if (ks[SDL_SCANCODE_DOWN])
                v -= 0.01f;
            if (v < 0.0f)
                v = 0.0f;
            if (v > 2.0f)
                v = 2.0f;
            if (v != a.p.volume) {
                SDL_LockAudioDevice(a.dev);
                a.p.volume = v;
                SDL_UnlockAudioDevice(a.dev);
            }
        }
        if (now - last_poll >= POLL_MS) {
            last_poll = now;
            poll_reload(&a);
        }
        if (now - last_title >= 100) {
            last_title = now;
            update_title(&a, win);
        }
        draw(&a, ren);
        SDL_Delay(15);
    }

    SDL_CloseAudioDevice(a.dev);
    tml_free(a.p.mid);
    tsf_close(a.p.sf);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}

int main(int argc, char **argv)
{
    int rate = 44100, i = 1;

    setvbuf(stdout, NULL, _IONBF, 0); /* log lines show up at once when redirected */

    if (i + 1 < argc && strcmp(argv[i], "--rate") == 0) {
        rate = atoi(argv[i + 1]);
        i += 2;
        if (rate != 44100 && rate != 22050) {
            fprintf(stderr, "--rate must be 44100 or 22050\n");
            return 2;
        }
    }
    if (i + 1 < argc && strcmp(argv[i], "--check") == 0)
        return check_mode(argv[i + 1]);
    if (i < argc && strcmp(argv[i], "--render") == 0) {
        if (argc - i < 4) {
            fprintf(stderr, "usage: midiplay [--rate N] --render <export_dir> <track> <out.wav> [seconds]\n");
            return 2;
        }
        return render_mode(argv[i + 1], atoi(argv[i + 2]), argv[i + 3], argc - i > 4 ? atof(argv[i + 4]) : 30.0, rate);
    }
    if (i >= argc) {
        fprintf(stderr, "usage: midiplay [--rate 44100|22050] <export_dir> [track]\n"
                        "       midiplay [--rate N] --render <export_dir> <track> <out.wav> [seconds]\n");
        return 2;
    }
    return interactive(argv[i], i + 1 < argc ? atoi(argv[i + 1]) : -1, rate);
}
