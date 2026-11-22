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

static uint8_t s_pix[20][THUMB_W * THUMB_H];
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
        if (pngGray(path, THUMB_W, THUMB_H, s_pix[i])) {
            s_have[i] = 1;
            sysLogPrintf(LOG_INFO, "missions: thumb %s (%dx%d grey)", path, THUMB_W, THUMB_H);
        } else {
            sysLogPrintf(LOG_WARNING, "missions: cannot read %s (8-bit non-interlaced PNG expected)", path);
        }
    }
    s_gen++;
}

/* ---- fast3d hooks (render thread) ----------------------------------------- */

static const void *s_addr[20];
static int s_naddr = 0;
static unsigned s_mapGen = 0;
static uint8_t s_out[20][72 * THUMB_H];

static int thumbsOn(void)
{
    return modsStoryActive() && (int)current_menu == (int)MENU_MISSION_SELECT;
}

/* 0 = leave the texture alone; else a value for the texture cache key. */
unsigned portThumbSalt(void)
{
    return thumbsOn() ? (0x7B000000u ^ (s_gen * 2654435761u)) | 1u : 0u;
}

/* The replacement for the 68x44 I8 texture at `addr` (line = row bytes of the upload), or NULL. */
const uint8_t *portThumbTexture(const void *addr, int w, int h, int line)
{
    int slot = -1;
    if (!thumbsOn() || w != THUMB_W || h != THUMB_H || line < THUMB_W || line > 72) return NULL;
    if (s_mapGen != s_gen) { s_mapGen = s_gen; s_naddr = 0; }
    for (int i = 0; i < s_naddr; i++)
        if (s_addr[i] == addr) { slot = i; break; }
    if (slot < 0 && s_naddr < 20) { slot = s_naddr; s_addr[s_naddr++] = addr; }
    if (slot < 0 || !s_have[slot]) return NULL;
    memset(s_out[slot], 0, sizeof s_out[slot]);
    for (int y = 0; y < THUMB_H; y++)
        memcpy(s_out[slot] + (size_t)y * line, s_pix[slot] + y * THUMB_W, THUMB_W);
    sysLogPrintf(LOG_INFO, "missions: mission select photo %d replaced by the mod's thumb", slot);
    return s_out[slot];
}
