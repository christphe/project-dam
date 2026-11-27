/*
 * missions_thumb.c -- the mission images of a mod in the game's own mission select (issue #103).
 *
 * The mission select is a film strip of 20 level photos, each a 68x44 I8 texture of the wallet model. While a mod's
 * story is chosen and that screen is up, fast3d's import_texture (port/fast3d/gfx_pc.cpp) asks this file for a
 * replacement of every such texture: the n-th one imported is the n-th slot (the model's tiles are drawn in slot
 * order, the first frame imports them all), and the answer is the slot mission's thumb.png (320x240, the editor's
 * capture, a plain colour crop of its view) reduced to the same 68x44 grey image. The briefing polaroid gets the same
 * image as 16 greys. The pixel conversion is port/mission/mission_thumb.c (shared with the editor's preview); this
 * file decodes the PNG and answers fast3d's hooks. No thumb, no change: the level's
 * own photo stays. Textures are cached by source address, so the cache key carries a salt that changes with the
 * mod: leaving the screen or the mod brings the original photos back.
 *
 * PNG: ge_assets' reader (port/assets/ge_png.h): 8-bit grey / grey+alpha / RGB / RGBA / palette, not interlaced.
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
#include "ge_pkg.h"
#include "ge_png.h"

#include "mission_thumb.h"
#include "game/front.h"   /* current_menu, MENU_MISSION_SELECT */

#define THUMB_W 68
#define THUMB_H 44

#define FULL_W 320
#define FULL_H 240
static uint8_t s_pix[20][FULL_W * FULL_H];   /* the thumb as grey, 320x240 (whatever size the PNG is) */
static int     s_have[20];
static unsigned s_gen = 1;

/* Decode `path` into a w*h grey image (area average). 1 on success. */
static int pngGray(const char *path, int dw, int dh, uint8_t *out)
{
    uint8_t *file = NULL, *rgba = NULL;
    size_t flen = 0;
    int w = 0, h = 0, ok = 0;
    if (!pkgReadFile(path, &file, &flen)) return 0;   /* the package layer: the PNG may be inside a .pmod */
    if (gePngDecode(file, flen, &w, &h, &rgba)) {     /* ge_assets' reader: RGBA8 */
        mthumbGrey(rgba, w, h, 4, dw, dh, out);       /* grey, area average */
        ok = 1;
    }
    pkgFree(file);
    free(rgba);
    return ok;
}

/* The mod's slot thumbs (missions_mods.c calls this when it loads a mod's slots, and with count 0 to clear). */
void modsThumbsLoad(const char *modDir, const char (*folders)[MISSION_ID_LEN], int count)
{
    memset(s_have, 0, sizeof s_have);
    for (int i = 0; i < count && i < 20; i++) {
        char path[760];
        snprintf(path, sizeof path, "%s/%s/thumb.png", modDir, folders[i]);
        if (!pkgExists(path)) continue;
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
            mthumbStrip(s_pix[slot], s_out[slot], line);
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
            uint8_t img[MTHUMB_POL_SIZE * MTHUMB_POL_SIZE];
            mthumbPolaroidImage(s_pix[slot], img);
            mthumbPolaroidHalf(img, half, s_bout[half]);
        }
        sysLogPrintf(LOG_INFO, "missions: briefing polaroid half %d replaced by the mod's thumb", half);
        return s_bout[half];
    }
    return NULL;
}
