/*
 * missions_screen.c -- the Mods screen (issue #103): A on the MODS row of the mode select opens a fullscreen screen
 * that lists GoldenEye and the installed mods, shows the highlighted mod (author, description, its missions with
 * their state) and applies the choice ("Play this mod" -> modsSelect, the same call the old row made).
 *
 * This file is the state and the data: input (player 1's pad, like the co-op player-count modal in coop.c; the mouse
 * arrives through modsScreenPointer), the mods' facts and the colour pictures. port/src/menu/menu_game.cpp +
 * menu_ui.cpp draw it with the menu's own look. front.c carries only the hooks (D960 pattern).
 *
 * Progress and locks come from the mod's own save (<save dir>/mods/<id>.eep, the game's EEPROM format) read straight
 * from the file: the active save is not switched. Slot n of a mod is stage n in that save (missions_mods.c).
 *
 * Pictures: <mod>/<mission>/thumb.png, and <mod>/cover.png for the mod itself (else its first mission's thumb.png);
 * 8-bit, not interlaced, grey / grey+alpha / RGB / RGBA, reduced to 96x72 RGBA.
 */
#include "platform.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <ultra64.h>
#include "game/front.h"   /* selected_folder_num */
#include "game/file.h"    /* save_data */
#include "joy.h"
#include "system.h"
#include "fs.h"
#include "mission.h"
#include "mission_dir.h"
#include "missions.h"
#include "missions_priv.h"
#include "realzlib.h"

static int iabs(int v) { return v < 0 ? -v : v; }   /* the game's stdlib.h shadows the host one: no abs() */

/* file2.c (no header) */
extern u32  fileGetSaveFolder(save_data *save);
extern bool fileGetSaveFlagDoReset(save_data *save);
extern bool fileGetSaveStageCompletedForDifficulty(save_data *save, int levelid, int difficulty);

#define SCR_MODS_MAX 16
#define SCR_MISSIONS_MAX CAMPAIGN_MAX_MISSIONS
#define THUMB_W 96
#define THUMB_H 72
#define THUMBS_MAX 128

typedef struct ScrMission {
    char name[96];
    char done[96];
    int state;
    int thumb;
} ScrMission;

typedef struct ScrEntry {
    char name[96], author[96], desc[320], progress[48];
    int affects, nmissions, thumb;
    ScrMission m[SCR_MISSIONS_MAX];
} ScrEntry;

static ScrEntry s_e[SCR_MODS_MAX + 1];
static int s_n = 0, s_active = 0, s_sel = 0, s_focus = 0, s_btn = 1, s_chosen = 0, s_stick = 1;
static int s_ptrHover = -1, s_ptrClick = 0;
static unsigned char *s_thumb[THUMBS_MAX];
static int s_nthumb = 0, s_rev = 0;

/* ---- the PNG reader --------------------------------------------------------------------------------------------- */

static uint32_t be32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }

/* path -> dw x dh RGBA (area average, alpha over black); malloc'd, NULL if it cannot be read. */
static unsigned char *pngRgba(const char *path, int dw, int dh)
{
    FSFile *f = fsOpen(path, "rb");
    uint8_t *file = NULL, *idat = NULL, *raw = NULL, *pix = NULL;
    unsigned char *out = NULL;
    int32_t fsz;
    uint32_t w = 0, h = 0, idatn = 0, pos = 8, ch, stride;
    int ctype = -1, ok = 0;
    z_stream z;
    if (!f) return NULL;
    fsz = fsSize(f);
    file = (uint8_t *)malloc((size_t)fsz + 1);
    if (!file || fsRead(f, file, fsz) != fsz) { fsClose(f); free(file); return NULL; }
    fsClose(f);
    if (fsz < 33 || memcmp(file, "\x89PNG\r\n\x1a\n", 8)) goto done;
    idat = (uint8_t *)malloc((size_t)fsz);
    if (!idat) goto done;
    while (pos + 12 <= (uint32_t)fsz) {
        uint32_t len = be32(file + pos);
        const uint8_t *type = file + pos + 4, *d = file + pos + 8;
        if (len > (uint32_t)fsz || pos + 12 + len > (uint32_t)fsz) goto done;
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
    stride = w * ch;
    raw = (uint8_t *)malloc((size_t)(stride + 1) * h);
    pix = (uint8_t *)malloc((size_t)stride * h);
    out = (unsigned char *)malloc((size_t)dw * dh * 4);
    if (!raw || !pix || !out) goto done;
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
            int a = x >= ch ? dst[x - ch] : 0, b = up ? up[x] : 0, c = (up && x >= ch) ? up[x - ch] : 0, v = src[x];
            switch (ft) {
            case 1: v += a; break;
            case 2: v += b; break;
            case 3: v += (a + b) / 2; break;
            case 4: { int p = a + b - c, pa = iabs(p - a), pb = iabs(p - b), pc = iabs(p - c);
                      v += (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c); break; }
            case 0: break;
            default: goto done;
            }
            dst[x] = (uint8_t)v;
        }
    }
    for (int oy = 0; oy < dh; oy++) {
        uint32_t y0 = (uint32_t)((uint64_t)oy * h / dh), y1 = (uint32_t)((uint64_t)(oy + 1) * h / dh);
        if (y1 <= y0) y1 = y0 + 1;
        for (int ox = 0; ox < dw; ox++) {
            uint32_t x0 = (uint32_t)((uint64_t)ox * w / dw), x1 = (uint32_t)((uint64_t)(ox + 1) * w / dw), n = 0;
            uint64_t sum[3] = { 0, 0, 0 };
            if (x1 <= x0) x1 = x0 + 1;
            for (uint32_t y = y0; y < y1 && y < h; y++)
                for (uint32_t x = x0; x < x1 && x < w; x++, n++) {
                    const uint8_t *p = pix + (size_t)y * stride + (size_t)x * ch;
                    unsigned a = (ch == 2) ? p[1] : (ch == 4 ? p[3] : 255), r, g, b;
                    if (ch < 3) r = g = b = p[0]; else { r = p[0]; g = p[1]; b = p[2]; }
                    sum[0] += r * a / 255; sum[1] += g * a / 255; sum[2] += b * a / 255;
                }
            unsigned char *o = out + ((size_t)oy * dw + ox) * 4;
            o[0] = n ? (unsigned char)(sum[0] / n) : 0;
            o[1] = n ? (unsigned char)(sum[1] / n) : 0;
            o[2] = n ? (unsigned char)(sum[2] / n) : 0;
            o[3] = 255;
        }
    }
    ok = 1;
done:
    free(file); free(idat); free(raw); free(pix);
    if (!ok) { free(out); out = NULL; }
    return out;
}

static int addThumb(const char *path)
{
    unsigned char *px;
    if (s_nthumb >= THUMBS_MAX || !fsExists(path)) return -1;
    px = pngRgba(path, THUMB_W, THUMB_H);
    if (!px) {
        sysLogPrintf(LOG_WARNING, "missions: cannot read %s (8-bit non-interlaced PNG expected)", path);
        return -1;
    }
    s_thumb[s_nthumb] = px;
    return s_nthumb++;
}

static void freeThumbs(void)
{
    for (int i = 0; i < s_nthumb; i++) { free(s_thumb[i]); s_thumb[i] = NULL; }
    if (s_nthumb) s_rev++;
    s_nthumb = 0;
}

/* ---- the mod's save --------------------------------------------------------------------------------------------- */

static const char *const kDiffName[4] = { "Agent", "Secret Agent", "00 Agent", "007" };

/* The mod's save slot of the player's folder (5 save_data from block 4 of the EEPROM file), or 0. */
static int readSave(const char *campaignId, save_data *out)
{
    char rel[128] = "";
    unsigned char buf[2048];
    FILE *fp;
    int found = 0;
    if (!missionEepPath(campaignId, rel, sizeof rel)) return 0;
    fp = fopen(sysResolvePath(rel), "rb");
    if (!fp) return 0;
    memset(buf, 0, sizeof buf);
    if (fread(buf, 1, sizeof buf, fp) < 32 + 5 * sizeof(save_data)) { fclose(fp); return 0; }
    fclose(fp);
    for (int i = 0; i < 5 && !found; i++) {
        save_data *s = (save_data *)(buf + 32 + i * sizeof(save_data));
        if (!fileGetSaveFlagDoReset(s) && (s32)fileGetSaveFolder(s) == selected_folder_num) {
            memcpy(out, s, sizeof *out);
            found = 1;
        }
    }
    return found;
}

static void fillEntry(ScrEntry *e, const Campaign *c, const char *dir)
{
    save_data sv;
    int have, done = 0, prevDone = 1;
    memset(e, 0, sizeof *e);
    snprintf(e->name, sizeof e->name, "%s", c->name);
    snprintf(e->author, sizeof e->author, "%s", c->author);
    snprintf(e->desc, sizeof e->desc, "%s", c->description);
    e->affects = c->affects;
    {   /* the mod's own picture (the editor's Mod tab > Choose picture); without it the first mission's thumb.png */
        char cover[760];
        snprintf(cover, sizeof cover, "%s/cover.png", dir);
        e->thumb = addThumb(cover);
    }
    have = readSave(c->id, &sv);
    for (int i = 0; i < c->nmissions && e->nmissions < SCR_MISSIONS_MAX; i++) {
        char path[760], err[160] = "";
        Mission m;
        ScrMission *sm = &e->m[e->nmissions];
        snprintf(path, sizeof path, "%s/%s/mission.json", dir, c->missions[i]);
        missionInit(&m);
        if (!missionLoadFile(path, &m, err, sizeof err) || !missionValidate(&m, err, sizeof err)) {
            missionFree(&m);
            continue;      /* the same ones the mission select skips: slot n = this index */
        }
        snprintf(sm->name, sizeof sm->name, "%s", m.name);
        missionFree(&m);
        sm->done[0] = 0;
        for (int d = 0; d < 4 && have; d++) {
            if (fileGetSaveStageCompletedForDifficulty(&sv, e->nmissions, d)) {
                size_t n = strlen(sm->done);
                snprintf(sm->done + n, sizeof sm->done - n, "%s%s", n ? ", " : "", kDiffName[d]);
            }
        }
        if (sm->done[0]) { sm->state = 2; done++; }
        else sm->state = (c->unlock == CAMPAIGN_UNLOCK_OPEN || e->nmissions == 0 || prevDone) ? 1 : 0;
        prevDone = sm->state == 2;
        snprintf(path, sizeof path, "%s/%s/thumb.png", dir, c->missions[i]);
        sm->thumb = addThumb(path);
        if (e->thumb < 0) e->thumb = sm->thumb;
        e->nmissions++;
    }
    if (e->nmissions > 0 && (c->affects & CAMPAIGN_AFFECTS_STORY)) {
        if (done >= e->nmissions) snprintf(e->progress, sizeof e->progress, "completed");
        else snprintf(e->progress, sizeof e->progress, "%d / %d missions", done, e->nmissions);
    }
}

/* ---- the screen ------------------------------------------------------------------------------------------------- */

void modsScreenOpen(void)
{
    int n = modsCount();
    freeThumbs();
    memset(&s_e[0], 0, sizeof s_e[0]);
    snprintf(s_e[0].name, sizeof s_e[0].name, "GoldenEye");
    snprintf(s_e[0].author, sizeof s_e[0].author, "Rare");
    snprintf(s_e[0].desc, sizeof s_e[0].desc, "The original game, no mod.");
    s_e[0].thumb = -1;
    s_n = 1;
    for (int i = 0; i < n && s_n <= SCR_MODS_MAX; i++, s_n++) {
        const Campaign *c = modsCampaign(i);
        if (!c) break;
        fillEntry(&s_e[s_n], c, modsDirOf(i));
    }
    s_chosen = modsCurrent() + 1;
    s_sel = s_chosen < s_n ? s_chosen : 0;
    s_focus = 0;
    s_btn = 1;
    s_ptrHover = -1;
    s_ptrClick = 0;
    s_stick = 1;      /* a stick already held does not move the cursor until it has been released */
    s_active = 1;
    s_rev++;
}

static void closeScreen(void)
{
    s_active = 0;
    freeThumbs();
}

int modsScreenActive(void) { return s_active; }

/* The mouse (render thread): the card or button it moved onto, and a click. */
void modsScreenPointer(int hover, int click)
{
    if (hover >= 0) s_ptrHover = hover;
    if (click) s_ptrClick = 1;
}

static void apply(void)
{
    if (s_sel != s_chosen) modsSelect(s_sel - 1);
}

int modsScreenTick(void)
{
    int move = 0, stick, press = 0, back = 0;
    s8 sx, sy;
    if (!s_active) return 0;
    if (s_ptrHover >= 0) {
        if (s_ptrHover < s_n) { if (s_ptrHover != s_sel || s_focus != 0) { s_sel = s_ptrHover; s_focus = 0; } }
        else if (s_ptrHover == MODS_HIT_BACK) { s_focus = 1; s_btn = 0; }
        else if (s_ptrHover == MODS_HIT_PLAY) { s_focus = 1; s_btn = 1; }
        if (s_ptrClick) {
            if (s_ptrHover < s_n) { s_focus = 1; s_btn = 1; }
            else press = 1;
        }
    }
    s_ptrHover = -1;
    s_ptrClick = 0;
    if (!press && joyGetButtonsPressedThisFrame(0, A_BUTTON | START_BUTTON | Z_TRIG)) press = 1, s_ptrHover = -1;
    if (joyGetButtonsPressedThisFrame(0, B_BUTTON)) back = 1;
    if (joyGetButtonsPressedThisFrame(0, U_JPAD)) move = -1;
    else if (joyGetButtonsPressedThisFrame(0, D_JPAD)) move = 1;
    {
        int hmove = 0;
        if (joyGetButtonsPressedThisFrame(0, L_JPAD)) hmove = -1;
        else if (joyGetButtonsPressedThisFrame(0, R_JPAD)) hmove = 1;
        sx = joyGetStickX(0);
        sy = joyGetStickY(0);
        stick = (sy > 40) ? -1 : (sy < -40 ? 1 : 0);
        if (stick != 0 && !s_stick) move = stick;
        if (!stick && (sx > 40 || sx < -40) && !s_stick) hmove = sx > 0 ? 1 : -1;
        s_stick = stick != 0 || sx > 40 || sx < -40;
        if (hmove) {
            if (s_focus == 1) { s_btn += hmove; if (s_btn < 0) s_btn = 0; if (s_btn > 1) s_btn = 1; }
            else if (hmove > 0) { s_focus = 1; s_btn = 1; }
        }
    }
    if (s_focus == 0 && move) {
        s_sel += move;
        if (s_sel < 0) s_sel = 0;
        if (s_sel >= s_n) s_sel = s_n - 1;
    } else if (s_focus == 1 && move < 0) {
        s_focus = 0;
    }
    if (back) {
        if (s_focus == 1) { s_focus = 0; return 0; }
        closeScreen();
        return 1;
    }
    if (press) {
        if (s_focus == 0) { s_focus = 1; s_btn = 1; return 0; }
        if (s_btn == 1) apply();
        closeScreen();
        return 1;
    }
    return 0;
}

/* ---- what the screen shows ---------------------------------------------------------------------------------------- */

int modsScreenCount(void) { return s_n; }
int modsScreenSel(void) { return s_sel; }
int modsScreenFocus(void) { return s_focus; }
int modsScreenButton(void) { return s_btn; }
int modsScreenChosen(void) { return s_chosen; }
const char *modsScreenName(int e) { return (e >= 0 && e < s_n) ? s_e[e].name : ""; }
const char *modsScreenAuthor(int e) { return (e >= 0 && e < s_n) ? s_e[e].author : ""; }
const char *modsScreenDescription(int e) { return (e >= 0 && e < s_n) ? s_e[e].desc : ""; }
const char *modsScreenProgress(int e) { return (e >= 0 && e < s_n) ? s_e[e].progress : ""; }
int modsScreenAffects(int e) { return (e >= 0 && e < s_n) ? s_e[e].affects : 0; }
int modsScreenMissions(int e) { return (e >= 0 && e < s_n) ? s_e[e].nmissions : 0; }
const char *modsScreenMissionName(int e, int m) { return (e >= 0 && e < s_n && m >= 0 && m < s_e[e].nmissions) ? s_e[e].m[m].name : ""; }
int modsScreenMissionState(int e, int m) { return (e >= 0 && e < s_n && m >= 0 && m < s_e[e].nmissions) ? s_e[e].m[m].state : 0; }
const char *modsScreenMissionDone(int e, int m) { return (e >= 0 && e < s_n && m >= 0 && m < s_e[e].nmissions) ? s_e[e].m[m].done : ""; }
int modsScreenThumbId(int e, int m)
{
    if (e < 0 || e >= s_n) return -1;
    if (m < 0) return s_e[e].thumb;
    return m < s_e[e].nmissions ? s_e[e].m[m].thumb : -1;
}
int modsScreenThumbCount(void) { return s_nthumb; }
int modsScreenThumbRev(void) { return s_rev; }
const unsigned char *modsScreenThumbPixels(int id, int *w, int *h)
{
    if (id < 0 || id >= s_nthumb) return NULL;
    *w = THUMB_W;
    *h = THUMB_H;
    return s_thumb[id];
}
