/*
 * missions_mods.c -- mods: a mod IS a campaign (mods/<mod>/campaign.json, issue #103).
 *
 * The player picks the mod on the game's mode select (a port row, front.c, D960 pattern); after that the game's own
 * mission select, difficulty select and briefing are filled from the mod: this file answers their questions
 * (which missions, in which order, which are open, what they are called). The player's progress lives in
 * <save dir>/mods/<mod>.json: never in the mod folder, never in the game's EEPROM save.
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
#include "cJSON.h"

typedef struct ModSlot {
    char folder[MISSION_ID_LEN];
    char name[MISSION_TEXT_LEN];
    int stage;
    int done;                   /* bit d: completed on difficulty d */
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

static void scan(void)
{
    char base[512];
    char names[MODS_MAX * 2][64];
    int n;
    s_scanned = 1;
    snprintf(base, sizeof base, "%s", sysResolvePath("$S/../mods"));
    n = missionListDir(base, names, MODS_MAX * 2);
    for (int k = 0; k < n && s_nmods < MODS_MAX; k++) {
        char path[640], err[160] = "";
        ModEntry *me = &s_mods[s_nmods];
        snprintf(path, sizeof path, "%s/%s/campaign.json", base, names[k]);
        if (!fsExists(path)) continue;
        campaignInit(&me->c);
        if (!campaignLoadFile(path, &me->c, err, sizeof err) || !campaignValidate(&me->c, err, sizeof err)) {
            sysLogPrintf(LOG_WARNING, "missions: %s: %s", path, err);
            campaignFree(&me->c);
            continue;
        }
        snprintf(me->folder, sizeof me->folder, "%s", names[k]);
        snprintf(me->dir, sizeof me->dir, "%s/%s", base, names[k]);
        s_nmods++;
    }
    for (int i = 1; i < s_nmods; i++)               /* folder order: not the directory's */
        for (int j = i; j > 0 && strcmp(s_mods[j - 1].folder, s_mods[j].folder) > 0; j--) {
            ModEntry t = s_mods[j];
            s_mods[j] = s_mods[j - 1];
            s_mods[j - 1] = t;
        }
    sysLogPrintf(LOG_INFO, "missions: %d mod(s) in %s", s_nmods, base);
}

int modsCount(void)
{
    if (!s_scanned) scan();
    return s_nmods;
}

/* ---- progress: <save dir>/mods/<mod>.json  {"missions": {"<mission folder>": <mask of completed difficulties>}} ---- */

static void progressPath(char *out, size_t n)
{
    snprintf(out, n, "%s/%s.json", sysResolvePath("$S/mods"), s_mods[s_cur].folder);
}

static void progressLoad(void)
{
    char path[700];
    FSFile *f;
    int32_t sz;
    char *buf;
    cJSON *root, *ms, *it;
    progressPath(path, sizeof path);
    if (!fsExists(path)) return;
    f = fsOpen(path, "rb");
    if (!f) return;
    sz = fsSize(f);
    buf = (char *)malloc((size_t)sz + 1);
    if (!buf) { fsClose(f); return; }
    sz = fsRead(f, buf, sz);
    fsClose(f);
    buf[sz < 0 ? 0 : sz] = 0;
    root = cJSON_Parse(buf);
    free(buf);
    if (!root) return;
    ms = cJSON_GetObjectItem(root, "missions");
    for (int i = 0; i < s_nslots; i++) {
        it = ms ? cJSON_GetObjectItem(ms, s_slots[i].folder) : NULL;
        if (cJSON_IsNumber(it)) s_slots[i].done = (int)it->valuedouble & 15;
    }
    cJSON_Delete(root);
}

static void progressSave(void)
{
    char path[700], dir[600];
    cJSON *root = cJSON_CreateObject(), *ms = cJSON_CreateObject();
    char *txt;
    FSFile *f;
    for (int i = 0; i < s_nslots; i++)
        if (s_slots[i].done) cJSON_AddNumberToObject(ms, s_slots[i].folder, s_slots[i].done);
    cJSON_AddItemToObject(root, "missions", ms);
    txt = cJSON_Print(root);
    cJSON_Delete(root);
    progressPath(path, sizeof path);
    snprintf(dir, sizeof dir, "%s", sysResolvePath("$S/mods"));
    missionMakeDir(dir);
    f = txt ? fsOpen(path, "wb") : NULL;
    if (f) {
        fsWrite(f, txt, (int32_t)strlen(txt));
        fsClose(f);
    } else {
        sysLogPrintf(LOG_WARNING, "missions: cannot write %s", path);
    }
    free(txt);
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
    progressLoad();
    sysLogPrintf(LOG_INFO, "missions: mod \"%s\": %d mission(s)", me->c.name, s_nslots);
}

void modsCycle(void)
{
    if (!s_scanned) scan();
    missionsDeactivate();
    s_cur = (s_cur + 1 >= s_nmods) ? -1 : s_cur + 1;
    s_nslots = 0;
    if (s_cur >= 0) loadSlots();
}

int modsStoryActive(void)
{
    return s_cur >= 0 && (s_mods[s_cur].c.affects & CAMPAIGN_AFFECTS_STORY);
}

const char *modsRowText(void)
{
    static char buf[200];
    const ModEntry *me;
    int st, mp;
    if (s_cur < 0) return "GoldenEye";
    me = &s_mods[s_cur];
    st = (me->c.affects & CAMPAIGN_AFFECTS_STORY) != 0;
    mp = (me->c.affects & CAMPAIGN_AFFECTS_MULTIPLAYER) != 0;
    snprintf(buf, sizeof buf, "%s (%s%s%s)", me->c.name, st ? "Story" : "", st && mp ? ", " : "", mp ? "Multiplayer" : "");
    return buf;
}

/* Slot n of the mission select's grid: the highest difficulty the player may pick, -1 = not shown (no such mission,
 * or still locked). Same meaning as the game's get_highest_unlocked_difficulty_for_level. */
int modsSlotDifficulty(int slot)
{
    int best = -1;
    if (!modsStoryActive() || slot < 0 || slot >= s_nslots) return -1;
    if (s_mods[s_cur].c.unlock == CAMPAIGN_UNLOCK_ORDERED && slot > 0 && !s_slots[slot - 1].done) return -1;
    for (int d = 0; d < 3; d++)
        if (s_slots[slot].done & (1 << d)) best = d;
    return best + 1 > 2 ? 2 : best + 1;      /* Agent always; completing one opens the next */
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
    return 1;
}

/* The active mission was won on `difficulty`. */
void missionsRecordComplete(int difficulty)
{
    int slot = modsActiveSlot();
    if (s_cur < 0 || slot < 0 || slot >= s_nslots) return;
    s_slots[slot].done |= 1 << (difficulty < 0 ? 0 : difficulty > 3 ? 3 : difficulty);
    progressSave();
    sysLogPrintf(LOG_INFO, "missions: progress saved: %s on difficulty %d", s_slots[slot].folder, difficulty);
}
