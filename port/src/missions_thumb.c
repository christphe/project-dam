/*
 * missions_thumb.c -- the mission images of a mod in the game's own mission select (issue #103).
 *
 * The mission select is a film strip of 20 level photos, each a 68x44 I8 texture of the wallet model. While a mod's
 * story is chosen and that screen is up, fast3d's import_texture (port/fast3d/gfx_pc.cpp) asks this file for a
 * replacement of every such texture: the n-th one imported is the n-th slot (the model's tiles are drawn in slot
 * order, the first frame imports them all), and the answer is the slot mission's thumb.png (320x240, the editor's
 * capture with the old-photo look baked in) reduced to the same 68x44 grey image. No thumb, no change: the level's
 * own photo stays. Textures are cached by source address, so the cache key carries a salt that changes with the
 * mod: leaving the screen or the mod brings the original photos back.
 *
 * PNG: 8-bit grey / grey+alpha / RGB / RGBA, not interlaced (what the editor writes), inflated with the host zlib.
 */
#include "platform.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <ultra64.h>
#include "system.h"
#include "fs.h"
#include "missions.h"
#include "missions_priv.h"
#include "realzlib.h"
#include "game/front.h"   /* current_menu, MENU_MISSION_SELECT */

#define THUMB_W 68
#define THUMB_H 44

#define FULL_W 320
#define FULL_H 240
static uint8_t s_pix[20][FULL_W * FULL_H];   /* the thumb as grey, 320x240 (whatever size the PNG is) */
static int     s_have[20];
static unsigned s_gen = 1;

static uint32_t be32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }

/* Decode `path` into a w*h grey image (area average). 1 on success. */
static int pngGray(const char *path, int dw, int dh, uint8_t *out)
{
    FSFile *f = fsOpen(path, "rb");
    uint8_t *file = NULL, *idat = NULL, *raw = NULL, *pix = NULL;
    int32_t fsz;
    uint32_t w = 0, h = 0, idatn = 0, pos = 8, ch, bpp, stride;
    int ctype = -1, ok = 0;
    z_stream z;
    if (!f) return 0;
    fsz = fsSize(f);
    file = (uint8_t *)malloc((size_t)fsz + 1);
    if (!file || fsRead(f, file, fsz) != fsz) { fsClose(f); free(file); return 0; }
    fsClose(f);
    if (fsz < 33 || memcmp(file, "\x89PNG\r\n\x1a\n", 8)) goto done;
    idat = (uint8_t *)malloc((size_t)fsz);
    if (!idat) goto done;
    while (pos + 12 <= (uint32_t)fsz) {
        uint32_t len = be32(file + pos);
        const uint8_t *type = file + pos + 4, *d = file + pos + 8;
        if (pos + 12 + len > (uint32_t)fsz) goto done;
        if (!memcmp(type, "IHDR", 4) && len >= 13) {
            w = be32(d); h = be32(d + 4);
            if (d[8] != 8 || d[12] != 0) goto done;       /* 8-bit, not interlaced */
            ctype = d[9];
        } else if (!memcmp(type, "IDAT", 4)) {
            memcpy(idat + idatn, d, len);
            idatn += len;
        } else if (!memcmp(type, "IEND", 4)) {
            break;
        }
        pos += 12 + len;
    }
    ch = ctype == 0 ? 1 : ctype == 2 ? 3 : ctype == 4 ? 2 : ctype == 6 ? 4 : 0;
    if (!ch || !w || !h || w > 8192 || h > 8192 || !idatn) goto done;
    bpp = ch;
    stride = w * ch;
    raw = (uint8_t *)malloc((size_t)(stride + 1) * h);
    pix = (uint8_t *)malloc((size_t)stride * h);
    if (!raw || !pix) goto done;
    memset(&z, 0, sizeof z);
    if (inflateInit(&z) != Z_OK) goto done;
    z.next_in = idat; z.avail_in = idatn;
    z.next_out = raw; z.avail_out = (stride + 1) * h;
    if (inflate(&z, Z_FINISH) != Z_STREAM_END || z.avail_out != 0) { inflateEnd(&z); goto done; }
    inflateEnd(&z);
    for (uint32_t y = 0; y < h; y++) {                      /* unfilter */
        const uint8_t *src = raw + (size_t)y * (stride + 1) + 1;
        uint8_t *dst = pix + (size_t)y * stride, *up = y ? dst - stride : NULL;
        int ft = raw[(size_t)y * (stride + 1)];
        for (uint32_t x = 0; x < stride; x++) {
            int a = x >= bpp ? dst[x - bpp] : 0, b = up ? up[x] : 0, c = (up && x >= bpp) ? up[x - bpp] : 0, v = src[x];
            switch (ft) {
            case 1: v += a; break;
            case 2: v += b; break;
            case 3: v += (a + b) / 2; break;
            case 4: { int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
                      v += (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c); break; }
            case 0: break;
            default: goto done;
            }
            dst[x] = (uint8_t)v;
        }
    }
    for (int oy = 0; oy < dh; oy++) {                       /* grey, area average */
        uint32_t y0 = (uint32_t)((uint64_t)oy * h / dh), y1 = (uint32_t)((uint64_t)(oy + 1) * h / dh);
        if (y1 <= y0) y1 = y0 + 1;
        for (int ox = 0; ox < dw; ox++) {
            uint32_t x0 = (uint32_t)((uint64_t)ox * w / dw), x1 = (uint32_t)((uint64_t)(ox + 1) * w / dw), n = 0;
            uint64_t sum = 0;
            if (x1 <= x0) x1 = x0 + 1;
            for (uint32_t y = y0; y < y1 && y < h; y++)
                for (uint32_t x = x0; x < x1 && x < w; x++, n++) {
                    const uint8_t *p = pix + (size_t)y * stride + (size_t)x * ch;
                    sum += (ch < 3) ? p[0] : (uint32_t)(p[0] * 77 + p[1] * 150 + p[2] * 29) >> 8;
                }
            out[oy * dw + ox] = n ? (uint8_t)(sum / n) : 0;
        }
    }
    ok = 1;
done:
    free(file); free(idat); free(raw); free(pix);
    return ok;
}

/* The mod's slot thumbs (missions_mods.c calls this when it loads a mod's slots, and with count 0 to clear). */
void modsThumbsLoad(const char *modDir, const char (*folders)[MISSION_ID_LEN], int count)
{
    memset(s_have, 0, sizeof s_have);
    for (int i = 0; i < count && i < 20; i++) {
        char path[760];
        snprintf(path, sizeof path, "%s/%s/thumb.png", modDir, folders[i]);
        if (!fsExists(path)) continue;
        if (pngGray(path, FULL_W, FULL_H, s_pix[i])) {
            s_have[i] = 1;
            sysLogPrintf(LOG_INFO, "missions: thumb %s (grey %dx%d)", path, FULL_W, FULL_H);
        } else {
            sysLogPrintf(LOG_WARNING, "missions: cannot read %s (8-bit non-interlaced PNG expected)", path);
        }
    }
    s_gen++;
}

/* ---- fast3d hooks (render thread) ----------------------------------------- */

/* Area average of the grey window (x0,y0,ww,hh) of slot `slot` into dw x dh. */
static void window(int slot, int x0, int y0, int ww, int hh, int dw, int dh, uint8_t *out, int ostride)
{
    for (int oy = 0; oy < dh; oy++) {
        int ya = y0 + oy * hh / dh, yb = y0 + (oy + 1) * hh / dh;
        if (yb <= ya) yb = ya + 1;
        for (int ox = 0; ox < dw; ox++) {
            int xa = x0 + ox * ww / dw, xb = x0 + (ox + 1) * ww / dw, n = 0;
            unsigned sum = 0;
            if (xb <= xa) xb = xa + 1;
            for (int y = ya; y < yb && y < FULL_H; y++)
                for (int x = xa; x < xb && x < FULL_W; x++, n++) sum += s_pix[slot][y * FULL_W + x];
            out[oy * ostride + ox] = n ? (uint8_t)(sum / n) : 0;
        }
    }
}

static const void *s_addr[20];            /* mission select: the n-th 68x44 import is slot n */
static int s_naddr = 0;
static unsigned s_mapGen = 0;
static uint8_t s_out[20][72 * THUMB_H];
static const void *s_baddr[2];            /* briefing polaroid: the first 128x64 I4 import is the top half */
static int s_nb = 0, s_bslot = -2;
static unsigned s_bgen = 0;
static uint8_t s_bout[2][4096];

static int activeThumb(void)
{
    int slot = modsActiveSlot();
    return (slot >= 0 && slot < 20 && s_have[slot]) ? slot : -1;
}

/* 0 = leave the texture alone; else a value for the texture cache key (every import asks, before the cache lookup). */
unsigned portThumbSalt(int fmt, int siz, int w, int h, const void *addr)
{
    (void)addr;
    if (!modsStoryActive()) return 0u;
    if ((int)current_menu == (int)MENU_MISSION_SELECT && fmt == 4 && siz == 1 && w == THUMB_W && h == THUMB_H)
        return (0x7B000000u ^ (s_gen * 2654435761u)) | 1u;
    if ((int)current_menu == (int)MENU_BRIEFING && fmt == 4 && siz == 0 && w == 128 && h == 64 && activeThumb() >= 0)
        return (0x6C000000u ^ (s_gen * 2654435761u) ^ ((unsigned)activeThumb() * 40503u)) | 1u;
    return 0u;
}

/* The replacement bytes for the texture at `addr` (line = row bytes of the upload), or NULL. */
const uint8_t *portThumbTexture(const void *addr, const uint8_t *cur, int fmt, int siz, int w, int h, int line)
{
    if (fmt == 4 && siz == 1 && w == THUMB_W && h == THUMB_H && line >= THUMB_W && line <= 72) {
        int slot = -1;
        if (s_mapGen != s_gen) { s_mapGen = s_gen; s_naddr = 0; }
        for (int i = 0; i < s_naddr; i++)
            if (s_addr[i] == addr) { slot = i; break; }
        if (slot < 0 && s_naddr < 20) { slot = s_naddr; s_addr[s_naddr++] = addr; }
        if (slot < 0) return NULL;
        memset(s_out[slot], 0, sizeof s_out[slot]);
        if (slot < modsSlotCount() && s_have[slot]) {
            /* the frame shows 68:44; take that crop of the 4:3 source (centred), not a squeezed copy */
            int ch = FULL_W * THUMB_H / THUMB_W;
            window(slot, 0, (FULL_H - ch) / 2, FULL_W, ch, THUMB_W, THUMB_H, s_out[slot], line);
            for (int y = 0; y < THUMB_H / 2; y++) {   /* the strip textures are stored bottom-up, like the polaroid's */
                for (int x = 0; x < THUMB_W; x++) {
                    uint8_t t = s_out[slot][y * line + x];
                    s_out[slot][y * line + x] = s_out[slot][(THUMB_H - 1 - y) * line + x];
                    s_out[slot][(THUMB_H - 1 - y) * line + x] = t;
                }
            }
            sysLogPrintf(LOG_INFO, "missions: mission select photo %d replaced by the mod's thumb", slot);
        } else if (slot >= modsSlotCount()) {
            sysLogPrintf(LOG_INFO, "missions: mission select photo %d: no mission, drawn empty", slot);
        } else {
            return NULL;   /* a mission without a thumb keeps the level's photo */
        }
        return s_out[slot];
    }
    if (fmt == 4 && siz == 0 && w == 128 && h == 64 && line == 64 && activeThumb() >= 0) {
        int slot = activeThumb(), half = -1;
        if (s_bgen != s_gen || s_bslot != slot) { s_bgen = s_gen; s_bslot = slot; s_nb = 0; }
        for (int i = 0; i < s_nb; i++)
            if (s_baddr[i] == addr) { half = i; break; }
        if (half < 0 && s_nb < 2) { half = s_nb; s_baddr[s_nb++] = addr; }
        if (half < 0) return NULL;
        {   /* a 128x128 polaroid: a white frame, the centred square of the source inside, 4 bits per pixel */
            uint8_t sq[124 * 124];
            window(slot, (FULL_W - FULL_H) / 2, 0, FULL_H, FULL_H, 124, 124, sq, 124);
            for (int y = 0; y < 64; y++) {
                int gy = half * 64 + (63 - y);   /* the polaroid halves are stored bottom-up: buffer row y shows composite row 63 - y */
                for (int x = 0; x < 128; x += 2) {
                    unsigned v[2];
                    for (int k = 0; k < 2; k++) {
                        int gx = x + k;
                        v[k] = (gx < 2 || gx > 125 || gy < 2 || gy > 125) ? 15u : (unsigned)(sq[(gy - 2) * 124 + (gx - 2)] >> 4);
                    }
                    s_bout[half][y * 64 + x / 2] = (uint8_t)((v[0] << 4) | v[1]);
                }
            }
        }
        sysLogPrintf(LOG_INFO, "missions: briefing polaroid half %d replaced by the mod's thumb", half);
        return s_bout[half];
    }
    return NULL;
}
