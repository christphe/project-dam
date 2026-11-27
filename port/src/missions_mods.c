/*
 * missions_mods.c -- mods: a mod IS a campaign (mods/<mod>/campaign.json, issue #103).
 *
 * The player picks the mod on the game's mode select (a port row, front.c, D960 pattern); after that the game's own
 * mission select, difficulty select and briefing are filled from the mod: this file answers their questions
 * (which missions, in which order, which are open, what they are called). The player's progress is the game's own
 * save, one EEPROM file per mod (<save dir>/mods/<campaign id>.eep, libultra.c portEepromSelect): while a mod is chosen
 * ge007.eep is never read or written, and the game's save code, folders, unlocks and best times work unchanged.
 */
#include "platform.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <ultra64.h>
#include "system.h"
#include "fs.h"
#include "mission.h"
#include "missions.h"
#include "missions_priv.h"
#include "mission_dir.h"
#include "ge_pkg.h"
#ifdef __SWITCH__
extern unsigned int romfsMountSelf(const char *name);   /* libnx romfs.h (romfsInit); <switch.h> clashes with the N64 types */
#endif
#include "game/front.h"   /* mission_folder_setup_entries */

extern int  portEepromSelect(const char *path);   /* libultra.c */
extern void fileValidateSaves(void);              /* file2.c: re-read the save data from the EEPROM */

typedef struct ModSlot {
    char folder[MISSION_ID_LEN];
    char name[MISSION_TEXT_LEN];
    int stage;
} ModSlot;

typedef struct ModEntry {
    char folder[64];
    char dir[512];
    Campaign c;
} ModEntry;

#define MODS_MAX 16
#define SLOTS_MAX 20            /* the mission select's 5 x 4 grid */
static ModEntry s_mods[MODS_MAX];
static int s_nmods = 0, s_scanned = 0, s_cur = -1;
static ModSlot s_slots[SLOTS_MAX];
static int s_nslots = 0;

static void loadSlots(void);
static void switchSave(void);

static int haveMod(const char *folder)
{
    for (int i = 0; i < s_nmods; i++)
        if (!strcmp(s_mods[i].folder, folder)) return 1;
    return 0;
}

/* The .pmod entries of one folder. A mod already found (same X of X.pmod) is skipped: the first folder scanned wins. */
static void scanDir(const char *base)
{
    char names[MODS_MAX * 2][64];
    int n = missionListDir(base, names, MODS_MAX * 2);
    for (int k = 0; k < n && s_nmods < MODS_MAX; k++) {
        char path[640], err[160] = "";
        ModEntry *me = &s_mods[s_nmods];
        size_t nl = strlen(names[k]);
        if (nl <= 5 || strcmp(names[k] + nl - 5, ".pmod")) {   /* mods/ holds only X.pmod (a tar file, or an unpacked folder) */
            sysLogPrintf(LOG_INFO, "missions: %s/%s ignored: not a .pmod", base, names[k]);
            continue;
        }
        snprintf(path, sizeof path, "%s/%s/campaign.json", base, names[k]);
        if (!pkgExists(path)) continue;
        {
            char folder[64];
            snprintf(folder, sizeof folder, "%.*s", (int)(nl - 5), names[k]);
            if (haveMod(folder)) { sysLogPrintf(LOG_INFO, "missions: %s/%s skipped: a mod %s is already installed", base, names[k], folder); continue; }
        }
        campaignInit(&me->c);
        if (!campaignLoadFile(path, &me->c, err, sizeof err) || !campaignValidate(&me->c, err, sizeof err)) {
            sysLogPrintf(LOG_WARNING, "missions: %s: %s", path, err);
            campaignFree(&me->c);
            continue;
        }
        snprintf(me->folder, sizeof me->folder, "%.*s", (int)(nl - 5), names[k]);   /* X of X.pmod: what --mod takes */
        snprintf(me->dir, sizeof me->dir, "%s/%s", base, names[k]);
        s_nmods++;
    }
}

static void scan(void)
{
    char base[512];
    s_scanned = 1;
    snprintf(base, sizeof base, "%s", sysResolvePath("$S/../mods"));
    scanDir(base);
#ifdef __SWITCH__
    {   /* a test build may carry mods in its romfs (cmake -DNX_ROMFS_DIR=<dir with mods/>): the SD card's win */
        static int s_romfs = -1;
        if (s_romfs < 0) s_romfs = romfsMountSelf("romfs") == 0;
        if (s_romfs) scanDir("romfs:/mods");
    }
#endif
    for (int i = 1; i < s_nmods; i++)               /* folder order: not the directory's */
        for (int j = i; j > 0 && strcmp(s_mods[j - 1].folder, s_mods[j].folder) > 0; j--) {
            ModEntry t = s_mods[j];
            s_mods[j] = s_mods[j - 1];
            s_mods[j - 1] = t;
        }
    sysLogPrintf(LOG_INFO, "missions: %d mod(s) in %s", s_nmods, base);
    {   /* --mod FOLDER: start with that mod chosen (the same as picking it on the mode select) */
        const char *want = sysArgGetString("--mod");
        for (int i = 0; want && i < s_nmods; i++) {
            if (!strcmp(s_mods[i].folder, want)) {
                s_cur = i;
                loadSlots();
                switchSave();
            }
        }
    }
}

int modsCount(void)
{
    if (!s_scanned) scan();
    return s_nmods;
}

/* The mod's missions in campaign order, with the facts the mission select shows. */
static void loadSlots(void)
{
    const ModEntry *me = &s_mods[s_cur];
    s_nslots = 0;
    for (int i = 0; i < me->c.nmissions && s_nslots < SLOTS_MAX; i++) {
        char path[700], err[160] = "";
        Mission m;
        ModSlot *sl = &s_slots[s_nslots];
        snprintf(path, sizeof path, "%s/%s/mission.json", me->dir, me->c.missions[i]);
        missionInit(&m);
        if (!missionLoadFile(path, &m, err, sizeof err) || !missionValidate(&m, err, sizeof err)) {
            sysLogPrintf(LOG_WARNING, "missions: %s: %s", path, err);
            missionFree(&m);
            continue;
        }
        memset(sl, 0, sizeof *sl);
        snprintf(sl->folder, sizeof sl->folder, "%s", me->c.missions[i]);
        snprintf(sl->name, sizeof sl->name, "%s", m.name);
        sl->stage = missionLevelNumber(m.level);
        missionFree(&m);
        s_nslots++;
    }
    {   /* the mission select photos: mods/<mod>/<mission>/thumb.png */
        char folders[SLOTS_MAX][MISSION_ID_LEN];
        for (int i = 0; i < s_nslots; i++) snprintf(folders[i], sizeof folders[i], "%s", s_slots[i].folder);
        modsThumbsLoad(me->dir, (const char (*)[MISSION_ID_LEN])folders, s_nslots);
    }
    sysLogPrintf(LOG_INFO, "missions: mod \"%s\": %d mission(s)", me->c.name, s_nslots);
}

static int s_boundEntry = -1, s_boundNum = 0;   /* the folder entry whose mission_num is the mod slot */

static void unbindEntry(void)
{
    if (s_boundEntry >= 0) mission_folder_setup_entries[s_boundEntry].mission_num = s_boundNum;
    s_boundEntry = -1;
}

/* The game's save follows the chosen mod: its own EEPROM file, and the game re-reads it. */
static void switchSave(void)
{
    char rel[128] = "";
    int changed;
    if (s_cur >= 0 && !missionEepPath(s_mods[s_cur].c.id, rel, sizeof rel)) {
        sysLogPrintf(LOG_WARNING, "missions: bad campaign id \"%s\": the mod would share ge007.eep, refused", s_mods[s_cur].c.id);
        s_cur = -1;
    }
    changed = portEepromSelect(s_cur >= 0 ? rel : NULL);
    if (changed) fileValidateSaves();   /* saves[] from the new file; a fresh file gets fresh folders */
}

/* Choose a mod (index into the installed mods) or -1 for the original game: the save, the mission select and the
 * thumbs follow. The Mods screen (missions_screen.c) calls it; the same index again changes nothing. */
void modsSelect(int idx)
{
    if (!s_scanned) scan();
    if (idx < -1 || idx >= s_nmods) idx = -1;
    if (idx == s_cur) return;
    missionsDeactivate();
    s_cur = idx;
    s_nslots = 0;
    if (s_cur >= 0) loadSlots();
    else modsThumbsLoad("", NULL, 0);
    switchSave();
}

int modsCurrent(void) { return s_cur; }

/* The installed mods, for the Mods screen: campaign, folder (its mission folders live in <dir>/<mission>/). */
const Campaign *modsCampaign(int i) { return (i >= 0 && i < s_nmods) ? &s_mods[i].c : NULL; }
const char *modsDirOf(int i) { return (i >= 0 && i < s_nmods) ? s_mods[i].dir : ""; }

void modsUnbind(void) { unbindEntry(); }

int modsStoryActive(void)
{
    return s_cur >= 0 && (s_mods[s_cur].c.affects & CAMPAIGN_AFFECTS_STORY);
}

/* The mode select's row: just the chosen mod's name (the Mods screen has the details). */
const char *modsRowText(void)
{
    return s_cur >= 0 ? s_mods[s_cur].c.name : "GoldenEye";
}

/* Slot n of the mission select's grid, before the game's own save is asked: -1 = no such mission (hidden); an open
 * campaign: 2 (all difficulties); an ordered one: -2 = let the game's save decide (the slot is stored as mission
 * index n, see modsSelectSlot). */
int modsSlotGate(int slot)
{
    if (!modsStoryActive() || slot < 0 || slot >= s_nslots) return -1;
    return s_mods[s_cur].c.unlock == CAMPAIGN_UNLOCK_OPEN ? 2 : -2;
}

const char *modsSlotName(int slot)
{
    return (slot >= 0 && slot < s_nslots) ? s_slots[slot].name : "";
}

int modsSelectSlot(int slot, int *stage)
{
    char path[700];
    if (!modsStoryActive() || slot < 0 || slot >= s_nslots) return 0;
    snprintf(path, sizeof path, "%s/%s/mission.json", s_mods[s_cur].dir, s_slots[slot].folder);
    if (!missionsActivateFile(path, -1, s_mods[s_cur].dir, slot)) return 0;
    *stage = s_slots[slot].stage;
    /* The game's save (file.c) stores a finished mission under entries[briefingpage].mission_num: make that the mod's
     * slot, so mission n of the mod is mission index n of the mod's save. Undone on deactivation. */
    unbindEntry();
    for (int i = 0; mission_folder_setup_entries[i].folder_text_preset != 0; i++) {
        if (mission_folder_setup_entries[i].stage_id == *stage) {
            s_boundEntry = i;
            s_boundNum = mission_folder_setup_entries[i].mission_num;
            mission_folder_setup_entries[i].mission_num = slot;
            break;
        }
    }
    return 1;
}

int modsSlotCount(void) { return s_nslots; }

const char *modsModName(void)
{
    return s_cur >= 0 ? s_mods[s_cur].c.name : "";
}
