/*
 * missions_host.c -- the Luau runtime's window onto the game (issue #104).
 *
 * port/mission/mission_lua.cpp knows nothing of the game; every read and action it makes goes through
 * the MissionHost callbacks below, which call EXISTING game functions (bondinvAddInvItem, doorActivate,
 * chrIsDead, alarmActivate, bossReturnTitleStage, hudmsgBottomShow) and read the loaded setup
 * (g_CurrentSetup.propDefs). No game function is changed.
 *
 * missionsFrameTick() runs once per game frame in a level (the call is in port/mission/hooks).
 */
#include "platform.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <ultra64.h>
#include "game/chr.h"
#include "game/chraction.h"
#include "game/chrai.h"
#include "game/bondview.h"
#include "game/bondinv.h"
#include "game/lv.h"
#include "game/player.h"
#include "game/propobj.h"
#include "game/loadobjectmodel.h"
#include "boss.h"
#include "system.h"
#include "mission.h"
#include "mission_apply.h"
#include "mission_lua.h"
#include "missions.h"
#include "missions_priv.h"

extern struct player *g_playerPointers[4];
extern s32 g_ClockTimer;
extern s32 g_GlobalTimer;
extern int bondinvHasInvItem(ITEM_IDS item);
extern s32 lvlGetSelectedDifficulty(void);
extern void end_of_mission_briefing(void);   /* file.c */

#define KEY_LOCK_BIT 0x80000000u   /* a key bit no key has: the door stays shut for the player */

static MissionRuntime *s_rt = NULL;
static PropDefHeaderRecord **s_prop = NULL;
static int   s_nprop = 0;
static u32  *s_origKey = NULL;      /* a door's own keyflags while this runtime holds it locked */
static void *s_setupKey = NULL;
static s32   s_lastTimer = 0;
static int   s_winSent = 0;

MissionRuntime *missionsRuntime(void) { return s_rt; }

void missionsHostReset(void)
{
    if (s_rt) {
        missionRtDestroy(s_rt);
        s_rt = NULL;
    }
    free(s_prop); s_prop = NULL;
    free(s_origKey); s_origKey = NULL;
    s_nprop = 0;
    s_setupKey = NULL;
    s_winSent = 0;
}

/* ---- callbacks ------------------------------------------------------------ */

static void hLog(void *ud, const char *line)
{
    (void)ud;
    sysLogPrintf(LOG_INFO, "missions: %s", line);
}

static void withPlayer(int p, void (*fn)(void *), void *arg)
{
    s32 saved = get_cur_playernum();
    set_cur_player(p);
    fn(arg);
    set_cur_player(saved);
}

static char s_msgBuf[4][256];
static int  s_msgNext = 0;

static void showMsg(void *arg)
{
    hudmsgBottomShow((char *)arg);
}

static void hMessage(void *ud, const char *text)
{
    int n = getPlayerCount();
    (void)ud;
    for (int p = 0; p < n; p++) {
        char *b = s_msgBuf[s_msgNext++ & 3];
        snprintf(b, sizeof s_msgBuf[0], "%s", text);
        withPlayer(p, showMsg, b);
    }
}

struct itemArg { int item; int result; };
static void doGive(void *a) { bondinvAddInvItem((ITEM_IDS)((struct itemArg *)a)->item); }
static void doTake(void *a) { bondinvRemoveItemByID((ITEM_IDS)((struct itemArg *)a)->item); }
static void doHas(void *a) { ((struct itemArg *)a)->result = bondinvHasInvItem((ITEM_IDS)((struct itemArg *)a)->item) != 0; }

static void forPlayers(int player, void (*fn)(void *), struct itemArg *a)
{
    int n = getPlayerCount();
    if (player >= 0) {
        if (player < n) withPlayer(player, fn, a);
        return;
    }
    for (int p = 0; p < n; p++) withPlayer(p, fn, a);
}

static void hGive(void *ud, int player, int item)
{
    struct itemArg a = { item, 0 };
    (void)ud;
    forPlayers(player, doGive, &a);
}

static void hTake(void *ud, int player, int item)
{
    struct itemArg a = { item, 0 };
    (void)ud;
    forPlayers(player, doTake, &a);
}

static int hHas(void *ud, int player, int item)
{
    struct itemArg a = { item, 0 };
    (void)ud;
    if (player < 0 || player >= getPlayerCount()) return 0;
    withPlayer(player, doHas, &a);
    return a.result;
}

static int hPlayerCount(void *ud) { (void)ud; return getPlayerCount(); }

static int hPlayerPos(void *ud, int p, float xyz[3])
{
    struct player *pl;
    (void)ud;
    if (p < 0 || p >= getPlayerCount()) return 0;
    pl = g_playerPointers[p];
    if (!pl || !pl->prop || pl->bonddead) return 0;
    xyz[0] = pl->prop->pos.f[0];
    xyz[1] = pl->prop->pos.f[1];
    xyz[2] = pl->prop->pos.f[2];
    return 1;
}

static int hPropCount(void *ud) { (void)ud; return s_nprop; }

static int hPropType(void *ud, int i)
{
    (void)ud;
    return (i >= 0 && i < s_nprop) ? s_prop[i]->type : 0;
}

static PropRecord *propOf(int i)
{
    PropDefHeaderRecord *h;
    if (i < 0 || i >= s_nprop) return NULL;
    h = s_prop[i];
    if (h->type == PROPDEF_GUARD) {
        ChrRecord *c = ((GuardRecord *)h)->Data;
        return c ? c->prop : NULL;
    }
    if (missionPropdefSize(h->type) >= 144) return ((ObjectRecord *)h)->prop;
    return NULL;
}

static int hPropPos(void *ud, int i, float xyz[3])
{
    PropRecord *p = propOf(i);
    (void)ud;
    if (!p) return 0;
    xyz[0] = p->pos.f[0];
    xyz[1] = p->pos.f[1];
    xyz[2] = p->pos.f[2];
    return 1;
}

static DoorRecord *doorOf(int i)
{
    if (i < 0 || i >= s_nprop || s_prop[i]->type != PROPDEF_DOOR) return NULL;
    return (DoorRecord *)s_prop[i];
}

static int hDoorState(void *ud, int i)
{
    DoorRecord *d = doorOf(i);
    (void)ud;
    if (!d || !d->prop) return MH_DOOR_NONE;
    if (doorIsClosed(d)) return MH_DOOR_CLOSED;
    if (d->openstate == DOORSTATE_STATIONARY && d->openPosition > 0.0f) return MH_DOOR_OPENED;
    return MH_DOOR_MOVING;
}

static void hDoorCmd(void *ud, int i, int cmd)
{
    DoorRecord *d = doorOf(i);
    (void)ud;
    if (!d || !d->prop) {
        sysLogPrintf(LOG_WARNING, "missions: prop %d is not a loaded door", i);
        return;
    }
    switch (cmd) {
    case MH_DOOR_LOCK:
        if (!(d->keyflags & KEY_LOCK_BIT)) {
            s_origKey[i] = d->keyflags;
            d->keyflags |= KEY_LOCK_BIT;
        }
        break;
    case MH_DOOR_UNLOCK:
        if (d->keyflags & KEY_LOCK_BIT) d->keyflags = s_origKey[i];
        break;
    case MH_DOOR_OPEN:
        doorActivate(d, DOORSTATE_OPENING);
        break;
    case MH_DOOR_CLOSE:
        doorActivate(d, DOORSTATE_CLOSING);
        break;
    }
}

static int hGuardDead(void *ud, int i)
{
    GuardRecord *g;
    ChrRecord *c;
    (void)ud;
    if (i < 0 || i >= s_nprop || s_prop[i]->type != PROPDEF_GUARD) return 0;
    g = (GuardRecord *)s_prop[i];
    if (!g->Data) return 0;                       /* never spawned */
    c = chrFindByLiteralId(g->chrnum);
    return c == NULL || c->prop == NULL || chrIsDead(c);
}

static void hGuardSetAi(void *ud, int i, int ailist)
{
    GuardRecord *g;
    ChrRecord *c;
    (void)ud;
    if (i < 0 || i >= s_nprop || s_prop[i]->type != PROPDEF_GUARD) return;
    g = (GuardRecord *)s_prop[i];
    c = g->Data ? chrFindByLiteralId(g->chrnum) : NULL;
    if (!c) return;
    c->ailist = ailistFindById(ailist);
    c->aioffset = 0;
    c->sleep = 0;
}

static int hDifficulty(void *ud) { (void)ud; return (int)lvlGetSelectedDifficulty(); }

static void hAlarm(void *ud) { (void)ud; alarmActivate(); }

static void hWin(void *ud)
{
    (void)ud;
    if (s_winSent) return;
    s_winSent = 1;
    /* bossReturnTitleStage minus its objectiveIsAllComplete() gate (the level's own objectives): a mod mission is won by its scripts */
    if (modsActiveSlot() >= 0)
        end_of_mission_briefing();   /* the game's own progress save (file.c), into the mod's EEPROM file */
    bossRunTitleStage();
    missionRtEnd(s_rt, 1);
}

/* ---- the level's props, and the runtime's life ----------------------------- */

static void buildProps(void)
{
    PropDefHeaderRecord *h = g_CurrentSetup.propDefs;
    int cap = 256;
    s_nprop = 0;
    s_prop = (PropDefHeaderRecord **)malloc(sizeof(*s_prop) * cap);
    while (h && h->type != PROPDEF_END) {
        if (s_nprop == cap) {
            cap *= 2;
            s_prop = (PropDefHeaderRecord **)realloc(s_prop, sizeof(*s_prop) * cap);
        }
        s_prop[s_nprop++] = h;
        h = (PropDefHeaderRecord *)(((u32 *)h) + sizepropdef(h));
    }
    s_origKey = (u32 *)calloc(s_nprop ? s_nprop : 1, sizeof(u32));
}

static void startRuntime(void)
{
    MissionHost host;
    char err[160] = "";
    char dir[600];
    missionsHostReset();
    buildProps();
    memset(&host, 0, sizeof host);
    host.log = hLog; host.message = hMessage; host.give = hGive; host.take = hTake; host.has = hHas;
    host.player_count = hPlayerCount; host.player_pos = hPlayerPos;
    host.prop_count = hPropCount; host.prop_type = hPropType; host.prop_pos = hPropPos;
    host.door_state = hDoorState; host.door_cmd = hDoorCmd;
    host.guard_dead = hGuardDead; host.guard_set_ai = hGuardSetAi; host.alarm = hAlarm; host.win = hWin;
    host.difficulty = hDifficulty;
    snprintf(dir, sizeof dir, "%s", missionsDir());
    s_rt = missionRtCreate(missionsCurrent(), &host, dir, missionsModDir(), err, sizeof err);
    if (!s_rt) {
        sysLogPrintf(LOG_WARNING, "missions: no script runtime: %s", err);
        return;
    }
    sysLogPrintf(LOG_INFO, "missions: runtime up: %d script(s), %d setup prop(s)", missionRtScriptCount(s_rt), s_nprop);
    {   /* what the setup spawned: the guards (record, chr number), so a mission's moved and added guards can be seen */
        char line[400];
        int n = 0, spawned = 0;
        line[0] = 0;
        for (int i = 0; i < s_nprop; i++) {
            GuardRecord *g;
            if (s_prop[i]->type != PROPDEF_GUARD) continue;
            g = (GuardRecord *)s_prop[i];
            n++;
            if (g->Data && chrFindByLiteralId(g->chrnum)) {
                spawned++;
                if (g->chrnum >= 25 || g->chrnum == 1) {
                    ChrRecord *c = chrFindByLiteralId(g->chrnum);
                    size_t u = strlen(line);
                    if (c && c->prop && u < sizeof line - 80)
                        snprintf(line + u, sizeof line - u, " [prop %d chr %d pad %d at %.0f %.0f %.0f]", i, g->chrnum, g->PadID,
                                 c->prop->pos.f[0], c->prop->pos.f[1], c->prop->pos.f[2]);
                }
            }
        }
        sysLogPrintf(LOG_INFO, "missions: setup guards: %d records, %d spawned;%s", n, spawned, line);
    }
}

void missionsFrameTick(void)
{
    double dt;
    if (!missionsActive() || missionsStage() < 0) return;
    if ((int)bossGetStageNum() != missionsStage() || !g_CurrentSetup.propDefs || getPlayerCount() < 1 || !g_playerPointers[0]) return;
    /* a new run of the level: another setup in memory, or the stage clock went back */
    if (!s_rt || s_setupKey != (void *)g_CurrentSetup.propDefs || g_GlobalTimer < s_lastTimer) {
        startRuntime();
        s_setupKey = (void *)g_CurrentSetup.propDefs;
    }
    s_lastTimer = g_GlobalTimer;
    if (!s_rt) return;
    dt = (double)g_ClockTimer / 60.0;
    if (dt < 0) dt = 0;
    if (dt > 0.25) dt = 0.25;
    missionRtTick(s_rt, dt);
}

/* The status the objective screens show. */
int missionsObjectivesSuccess(void)
{
    if (!missionsActive() || !s_rt) return 0;
    return missionRtAllComplete(s_rt, (int)lvlGetSelectedDifficulty());
}
