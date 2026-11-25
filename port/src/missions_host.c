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
#include "game/textrelated.h"
#include "game/language.h"
#include "assets/obseg/text/LmiscE.h"
#include "game/stan.h"
#include "game/model.h"
#include <math.h>
#include "fr.h"
#include "boss.h"
#include "system.h"
#include "mission.h"
#include "mission_apply.h"
#include "mission_lua.h"
#include "missions.h"
#include "missions_priv.h"
#include "saymarker.h"

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
static int   s_winSent = 0;         /* win or fail went out: the level ends once */

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
    case MH_DOOR_UNLOCK_FORCE:   /* the native lock goes too: no key bit at all, for the rest of the level */
        d->keyflags = 0;
        s_origKey[i] = 0;
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

static GuardRecord *guardRec(int i)
{
    if (i < 0 || i >= s_nprop || s_prop[i]->type != PROPDEF_GUARD) return NULL;
    return (GuardRecord *)s_prop[i];
}

/* guard:follow(): the guard's chr plots a course to player `player`, as the AI commands TRYWalkToBond / TRYRunToBond
 * do (chrai.c AI_TRYWalkToBond: chrGoToBond(chr, SPEED_WALK)). chrGoToBond targets the CURRENT player's prop, so the
 * call runs with that player current (withPlayer, like every per-player call here). Nothing in src/game changes. */
struct gotoArg { ChrRecord *chr; int run; int res; };
static void doGoto(void *a)
{
    struct gotoArg *g = (struct gotoArg *)a;
    /* chrGoToBond refuses while 10 or more guards have seen a player recently (chraction.c), and fails without a route */
    if (g_SeenBondRecentlyGuardCount >= 10) { g->res = MH_GOTO_BUSY; return; }
    g->res = chrGoToBond(g->chr, g->run >= 2 ? SPEED_SPRINT : g->run ? SPEED_RUN : SPEED_WALK) ? MH_GOTO_OK : MH_GOTO_NOROUTE;
}

static int hGuardGoto(void *ud, int i, int player, int run)
{
    GuardRecord *g = guardRec(i);
    struct gotoArg a;
    (void)ud;
    if (!g || !g->Data || player < 0 || player >= getPlayerCount()) return MH_GOTO_GONE;
    a.chr = chrFindByLiteralId(g->chrnum);
    a.run = run;
    a.res = MH_GOTO_GONE;
    if (!a.chr || !a.chr->prop || !a.chr->model || chrIsDead(a.chr)) return MH_GOTO_GONE;
    withPlayer(player, doGoto, &a);
    return a.res;
}

/* :surrender() / :stand(): the game's own actions, run with the player current (stand faces TARGET_BOND = that player). */
struct poseArg { ChrRecord *chr; int pose; };
static void doPose(void *a)
{
    struct poseArg *p = (struct poseArg *)a;
    if (p->pose == MH_POSE_SURRENDER) chrTrySurrender(p->chr);
    else check_set_actor_standing_still(p->chr, 0x0001 /* TARGET_BOND, bondaicommands.h */, 0);
}

static void hGuardPose(void *ud, int i, int pose, int player)
{
    GuardRecord *g = guardRec(i);
    struct poseArg a;
    (void)ud;
    if (!g || !g->Data || player < 0 || player >= getPlayerCount()) return;
    a.chr = chrFindByLiteralId(g->chrnum);
    a.pose = pose;
    if (!a.chr || !a.chr->prop || !a.chr->model || chrIsDead(a.chr)) return;
    withPlayer(player, doPose, &a);
}

/* The pad a script means by a prop: an object's own pad (numbered as the setup has them: >= 10000 bound), a door's
 * bound pad (stored without the +10000), a guard's pad. -1 when the prop has none. */
static int propPad(int i)
{
    if (i < 0 || i >= s_nprop) return -1;
    switch (s_prop[i]->type) {
    case PROPDEF_GUARD: return ((GuardRecord *)s_prop[i])->PadID;
    case PROPDEF_DOOR:  return ((DoorRecord *)s_prop[i])->pad + 10000;
    default:            return ((ObjectRecord *)s_prop[i])->pad;
    }
}

static ChrRecord *liveChr(int i)
{
    GuardRecord *g = guardRec(i);
    ChrRecord *c;
    if (!g || !g->Data) return NULL;
    c = chrFindByLiteralId(g->chrnum);
    return (c && c->prop && c->model && !chrIsDead(c)) ? c : NULL;
}

/* chraction.c, not in its header */
s32 plot_course_for_actor(ChrRecord *self, coord3d *arg1, StandTile *stan, SPEED speed);


/* The pad record of a setup pad number (>= 10000: bound). */
static PadRecord *padRec(int pad)
{
    if (pad < 0 || !g_CurrentSetup.pads) return NULL;
    if (isNotBoundPad(pad)) return &g_CurrentSetup.pads[pad];
    return g_CurrentSetup.boundpads ? (PadRecord *)&g_CurrentSetup.boundpads[getBoundPadNum(pad)] : NULL;
}

/* Where a prop "faces": the horizontal direction out of its pad. A wall pad (up roughly horizontal) faces along up,
 * the way chrGoToPad steps out of it; a pad lying flat faces along look (seen in game on the Facility consoles). */
static int padFront(const PadRecord *p, float n[2])
{
    float x, z, l;
    if (p->up.f[1] < 0.5f) { x = p->up.f[0]; z = p->up.f[2]; }
    else { x = p->look.f[0]; z = p->look.f[2]; }
    l = sqrtf(x * x + z * z);
    if (l < 1e-4f) return 0;
    n[0] = x / l; n[1] = z / l;
    return 1;
}

/* The spot a guard stands on to use prop `target`: FRONT_FROM_CENTER out from the object's own centre along the way it
 * faces (an object on a bound pad is centred in the pad's box, not on the pad position, so the pad alone puts the guard
 * beside a wide console). Measured in game on the Facility console (model 337): 78 units in front of its centre is
 * square in front of it, 91 clears it completely; the volume test (stanTestVolume, CDTYPE_OBJS) does not see a
 * monitor, so it cannot find this by itself. The pad's tile is walked to the spot to find its floor tile; 0 when
 * that walk fails. */
#define FRONT_FROM_CENTER 85.0f
static int frontSpot(int target, int pad, const PadRecord *p, const float n[2], coord3d *to, StandTile **stanOut)
{
    float bx = p->pos.f[0], bz = p->pos.f[2];
    StandTile *stan = p->stan;
    ObjectRecord *o = (target >= 0 && target < s_nprop && s_prop[target]->type != PROPDEF_GUARD && s_prop[target]->type != PROPDEF_DOOR)
                        ? (ObjectRecord *)s_prop[target] : NULL;
    (void)pad;
    if (o && o->prop) { bx = o->prop->pos.f[0]; bz = o->prop->pos.f[2]; }
    to->f[0] = bx + n[0] * FRONT_FROM_CENTER;
    to->f[1] = p->pos.f[1];
    to->f[2] = bz + n[1] * FRONT_FROM_CENTER;
    if (!stan || !walkTilesBetweenPoints_NoCallback(&stan, p->pos.f[0], p->pos.f[2], to->f[0], to->f[2]) || !stan) return 0;
    *stanOut = stan;
    return 1;
}

static int hGuardGotoProp(void *ud, int i, int target, int run)
{
    ChrRecord *c = liveChr(i);
    int pad = propPad(target);
    PadRecord *p = padRec(pad);
    float n[2];
    (void)ud;
    if (!c || !p) return MH_GOTO_GONE;
    if (g_SeenBondRecentlyGuardCount >= 10) return MH_GOTO_BUSY;
    /* stand in front of the prop, on the first free spot out along the way it faces (frontSpot) */
    if (padFront(p, n)) {
        StandTile *stan = NULL;
        coord3d to;
        if (frontSpot(target, pad, p, n, &to, &stan)) {
            sysLogPrintf(LOG_INFO, "missions: go_to front of prop %d (pad %d, faces %.2f %.2f): stand at %.0f %.0f %.0f",
                         target, pad, n[0], n[1], to.f[0], to.f[1], to.f[2]);
            if (plot_course_for_actor(c, &to, stan, run >= 2 ? SPEED_SPRINT : run ? SPEED_RUN : SPEED_WALK)) return MH_GOTO_OK;
        }
    }
    return chrGoToPad(c, pad, run >= 2 ? SPEED_SPRINT : run ? SPEED_RUN : SPEED_WALK) ? MH_GOTO_OK : MH_GOTO_NOROUTE;
}

static int hGuardMoving(void *ud, int i)
{
    ChrRecord *c = liveChr(i);
    (void)ud;
    return c && c->actiontype == ACT_GOPOS;
}

#define FRONT_SNAP 120.0f  /* units: a guard this close to the spot in front of a prop is set exactly on it by :face() */

static int hGuardFaceProp(void *ud, int i, int target)
{
    ChrRecord *c = liveChr(i);
    int pad = propPad(target);
    PadRecord *p = padRec(pad);
    float n[2];
    int placed = 1;
    (void)ud;
    if (!c || !p) return 1;
    if (padFront(p, n)) {
        /* square on: the opposite of the way the prop faces (atan2f(x, z): the facing convention of the game's own
         * chr teleport, chrai.c TRYTeleportingChrToPad) */
        float a = atan2f(-n[0], -n[1]);
        StandTile *stan = NULL;
        coord3d to;
        if (frontSpot(target, pad, p, n, &to, &stan)) {
            const float dx = c->prop->pos.f[0] - to.f[0], dz = c->prop->pos.f[2] - to.f[2];
            if (dx * dx + dz * dz < FRONT_SNAP * FRONT_SNAP) {
                /* The game's chr teleport (chrai.c AI_TRYTeleportingChrToPad) onto the spot, never displaced
                 * (chrAdjustPosForSpawn would move a taken spot 60 units sideways): when a player or a chr stands
                 * there, the guard only turns and the caller asks again. */
                StandTile *test = stan;
                sub_GAME_7F03D058(c->prop, FALSE);
                if (stanTestVolume(&test, to.f[0], to.f[2], 20.0f, CDTYPE_PLAYERS | CDTYPE_CHRS, 0.0f, 1.0f) < 0) {
                    c->prop->pos = to;
                    c->prop->stan = stan;
                    c->chrflags |= CHRFLAG_INIT;
                    setsubroty(c->model, a);
                    setsuboffset(c->model, &to);
                    chrDetectRooms(c);
                    sysLogPrintf(LOG_INFO, "missions: face prop %d: set on %.0f %.0f %.0f facing %.2f", target, to.f[0], to.f[1], to.f[2], a);
                } else placed = 0;   /* someone stands on the spot: only turn; the caller asks again */
                sub_GAME_7F03D058(c->prop, TRUE);
            }
        }
        if (a < 0) a += 6.2831853f;
        check_set_actor_standing_still(c, 0x0010 /* TARGET_COMPASS, bondaicommands.h */, (s32)(a * 65535.0f / 6.2831853f));
    } else check_set_actor_standing_still(c, 0x0008 /* TARGET_PAD, bondaicommands.h */, pad);
    return placed;
}

/* The line the game shows when one of its objectives changes status (objective_status.c, the loop over
 * objectiveStatuses: "Objective a: completed" / "failed", lettered over the objectives of the difficulty played),
 * for the mission's own objectives, which that loop does not see. */
#define MAX_TRACKED_OBJ 64
static int s_objShown[MAX_TRACKED_OBJ];

static void objectiveLinesReset(void) { memset(s_objShown, 0, sizeof s_objShown); }

static void objectiveLinesTick(void)
{
    const int n = missionRtObjectiveCount(s_rt), diff = (int)lvlGetSelectedDifficulty();
    int letter = 0;
    for (int i = 0; i < n && i < MAX_TRACKED_OBJ; i++) {
        const int st = missionRtObjectiveStatus(s_rt, i);
        const int here = missionRtObjectiveDifficulty(s_rt, i) <= diff;
        if (st != s_objShown[i]) {
            s_objShown[i] = st;
            if (here && st != OBJECTIVESTATUS_INCOMPLETE) {
                char buffer[100];
                if (j_text_trigger) sprintf(buffer, "%s \x80%c ", langGet(getStringID(LMISC, MISC_STR_2C_OBJECTIVE)), letter + 0x1A);
                else sprintf(buffer, "%s %c: ", langGet(getStringID(LMISC, MISC_STR_2C_OBJECTIVE)), letter + 0x61);
                const s32 slot = st == OBJECTIVESTATUS_COMPLETE ? MISC_STR_2D_COMPLETED_LF : MISC_STR_2F_FAILED_LF;   /* not inside
                                                       getStringID: the macro is unparenthesised (missions_ui.c) */
                strcat(buffer, langGet(getStringID(LMISC, slot)));
                /* hudmsgBottomShow drops a line silently when 5 are already queued (bondview2.c) */
                sysLogPrintf(LOG_INFO, "missions: HUD line \"%s\"%s", buffer, display_statusbar >= 5 ? " (dropped: 5 lines already queued)" : "");
#ifdef VERSION_US
                hudmsgBottomShow(buffer);
#else
                jp_hudmsgBottomShow(buffer);
#endif
            }
        }
        if (here) letter++;
    }
}

/* Hostage taking, with the game's own calls (Frigate's hostage takers, UsetupdestZ.c ai_2):
 * guard_try_fire_or_aim_at_target(TARGET_CHR | TARGET_AIM_ONLY) to cover, ..._update(TARGET_CHR) to fire, and
 * chr_hit_body_part_with_item_damage(hostage, 8, 10) for the hit itself. */
static int hGuardAimAt(void *ud, int i, int target, int fire)
{
    ChrRecord *c = liveChr(i), *t = liveChr(target);
    (void)ud;
    if (!c || !t) return 0;
    return actor_aim_at_actor(c, fire ? 0x0004 : 0x0024 /* TARGET_CHR (| TARGET_AIM_ONLY), bondaicommands.h */, t->chrnum) != 0;
}

static void hGuardHit(void *ud, int i)
{
    ChrRecord *t = liveChr(i);
    coord3d v = { 0 };
    (void)ud;
    if (t) handles_shot_actors(t, 8, &v, 10, FALSE);
}

struct seeArg { ChrRecord *chr; int seen; };
static void doSee(void *a) { struct seeArg *s = (struct seeArg *)a; if (chrCheckTargetInSight(s->chr)) s->seen = 1; }

static int hGuardSeesPlayer(void *ud, int i)
{
    struct seeArg a;
    (void)ud;
    a.chr = liveChr(i);
    a.seen = 0;
    if (!a.chr) return 0;
    for (int p = 0; p < getPlayerCount() && !a.seen; p++) withPlayer(p, doSee, &a);
    return a.seen;
}

static int hGuardNearMiss(void *ud, int i)
{
    ChrRecord *c = liveChr(i);
    (void)ud;
    return c && chrIfNearMiss(c);
}

static int hGuardHits(void *ud, int i)
{
    GuardRecord *g = guardRec(i);
    ChrRecord *c = (g && g->Data) ? chrFindByLiteralId(g->chrnum) : NULL;
    (void)ud;
    return c ? chrGetNumArghs(c) : 0;
}

/* :guards_near(radius): the setup's guards whose chr is alive and within radius of pos, the prop `except` left out. */
static int hGuardsNear(void *ud, const float pos[3], float radius, int except)
{
    int n = 0;
    (void)ud;
    for (int i = 0; i < s_nprop; i++) {
        ChrRecord *c;
        float dx, dy, dz;
        if (i == except || s_prop[i]->type != PROPDEF_GUARD) continue;
        c = liveChr(i);
        if (!c) continue;
        dx = c->prop->pos.f[0] - pos[0]; dy = c->prop->pos.f[1] - pos[1]; dz = c->prop->pos.f[2] - pos[2];
        if (dx * dx + dy * dy + dz * dz <= radius * radius) n++;
    }
    return n;
}

/* :keep_body(): chrlvTickDead fades a dead chr out and removes it, counting in act_init.padding[0] while that is >= 0;
 * holding the counter at -1 every frame keeps the body where it fell. */
#define MAX_KEPT_BODIES 8
static int s_keepBody[MAX_KEPT_BODIES], s_nkeepBody;

static void hGuardKeepBody(void *ud, int i)
{
    (void)ud;
    for (int k = 0; k < s_nkeepBody; k++) if (s_keepBody[k] == i) return;
    if (s_nkeepBody < MAX_KEPT_BODIES) s_keepBody[s_nkeepBody++] = i;
}

static void keepBodiesTick(void)
{
    for (int k = 0; k < s_nkeepBody; k++) {
        GuardRecord *g = guardRec(s_keepBody[k]);
        ChrRecord *c = (g && g->Data) ? chrFindByLiteralId(g->chrnum) : NULL;
        if (c && c->actiontype == ACT_DEAD) {
            c->act_init.padding[0] = -1;
            c->fadealpha = 0xFF;
        }
    }
}

/* :make_usable(): the flag a setup tag gives an object (objTestForInteract: tagged objects can be activated);
 * :was_used(): the flag propobjInteract sets when a player activates it, cleared like AI_IFObjectWasActivated does. */
static ObjectRecord *objRec(int i)
{
    if (i < 0 || i >= s_nprop) return NULL;
    switch (s_prop[i]->type) {
    case PROPDEF_GUARD: case PROPDEF_DOOR: return NULL;
    default: return (ObjectRecord *)s_prop[i];
    }
}

static void hPropUsable(void *ud, int i)
{
    ObjectRecord *o = objRec(i);
    (void)ud;
    if (!o || !o->prop) {
        sysLogPrintf(LOG_WARNING, "missions: make_usable: prop %d is not a spawned object", i);
        return;
    }
    o->runtime_bitflags |= RUNTIMEBITFLAG_TAGGED;
}

static int hPropUsed(void *ud, int i)
{
    ObjectRecord *o = objRec(i);
    (void)ud;
    if (!o || !o->prop || !(o->runtime_bitflags & RUNTIMEBITFLAG_ACTIVATED)) return 0;
    o->runtime_bitflags &= ~RUNTIMEBITFLAG_ACTIVATED;
    return 1;
}

/* :play_anim(name): the animations a script may play, with the frame range the game's own AI lists use (chraidata.c). */
static const struct { const char *name; int anim, start, end; } kAnims[] = {
    { "keyboard_right_hand_tapping", ANIM_keyboard_right_hand_tapping, 0, 89 },
    { "keyboard_left_hand",          ANIM_keyboard_left_hand,          0, 79 },
    { "keyboard_right_hand2",        ANIM_keyboard_right_hand2,        0, 74 },
};

static int hGuardAnim(void *ud, int i, const char *name)
{
    ChrRecord *c = liveChr(i);
    (void)ud;
    if (!c || !name) return 0;
    for (size_t k = 0; k < sizeof kAnims / sizeof kAnims[0]; k++)
        if (!strcmp(kAnims[k].name, name))
            return check_if_able_to_then_perform_animation(c, kAnims[k].anim, kAnims[k].start, kAnims[k].end, 0, 16) != 0;
    return 0;
}

/* say(who, text): the line the AI command AI_TextPrintTop shows (chrai.c: hudmsgTopShow). It copies the string. */
struct sayArg { const char *line; SayAnchorFn fn; int key; const float *fixed; float range; };

static void showTop(void *arg)
{
    struct sayArg *a = arg;
    sayMarkerQueue(a->line, a->fn, NULL, a->key, a->fixed, a->range);   /* the slot this line lands in: its speaker */
    hudmsgTopShow((char *)a->line);
}

/* A setup prop's anchor while its line shows: above a guard's head (the game's own chr height over its ground, as
 * chr.c gives the collision top), an object's own position. */
static int sayPropAnchor(void *ud, int key, float xyz[3])
{
    PropRecord *p;
    (void)ud;
    if (key < 0 || key >= s_nprop || !(p = propOf(key))) return 0;
    xyz[0] = p->pos.f[0];
    xyz[1] = p->pos.f[1];
    xyz[2] = p->pos.f[2];
    if (s_prop[key]->type == PROPDEF_GUARD) {
        ChrRecord *c = ((GuardRecord *)s_prop[key])->Data;
        if (!c || !c->model) return 0;
        xyz[1] = c->ground + c->chrheight + 10.0f;
    }
    return 1;
}

static void sayLine(const char *who, const char *text, struct sayArg *a)
{
    char raw[0x200], line[0x200];
    size_t len;
    snprintf(raw, sizeof raw, "%s: %s", who ? who : "", text ? text : "");
    /* The top message is drawn as is: never wrapped, and its dark box is as tall as textMeasure says, which counts
     * the newlines (the game's own strings all end in one). Wrap it to the width the HUD gives it (bondview2.c: x 0x1e
     * on both sides) and end it with a newline, or the box is a thin bar and a long line runs off the screen. */
    textWrap(viGetViewWidth() - 2 * 0x1e, raw, line, ptrFontZurichBoldChars, ptrFontZurichBold);
    len = strlen(line);
    if (len + 2 >= 0x97) len = 0x97 - 2;   /* bondview2.c BONDVIEW_HUD_MSG_TOP_BUFFER_LENGTH, with room for the newline */
    if (len == 0 || line[len - 1] != '\n') line[len++] = '\n';
    line[len] = 0;
    a->line = line;
    /* Once: the top message is one queue shared by every player's view (bondview2.c hudmsgTopShow), each player's
     * HUD pass draws it. One call per player queued the same line twice in co-op. */
    showTop(a);
}

static void hSay(void *ud, const char *who, const char *text)
{
    struct sayArg a = { NULL, NULL, -1, NULL, 0.0f };
    (void)ud;
    sayLine(who, text, &a);
}

/* target:say(): the same line, with a marker over its speaker in every player's view (port/src/saymarker.c). */
static void hSayAt(void *ud, const char *who, const char *text, int prop, const float pos[3], float range)
{
    struct sayArg a = { NULL, NULL, -1, NULL, range };
    (void)ud;
    if (prop >= 0) {
        a.fn = sayPropAnchor;   /* followed while the line shows */
        a.key = prop;
    } else {
        a.fixed = pos;          /* a pad's or a zone's point (sayMarkerQueue copies it) */
    }
    sayLine(who, text, &a);
}

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

/* mission.fail(): the level ends as the game ends it when the player dies (bondview2.c: bossRunTitleStage), with no
 * end_of_mission_briefing(): no progress save, the objectives screen shows the mission as not completed. */
static void hFail(void *ud)
{
    (void)ud;
    if (s_winSent) return;
    s_winSent = 1;
    bossRunTitleStage();
    missionRtEnd(s_rt, 0);
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
    host.guard_goto = hGuardGoto; host.say = hSay; host.say_at = hSayAt; host.fail = hFail; host.guard_pose = hGuardPose;
    host.guard_goto_prop = hGuardGotoProp; host.guard_moving = hGuardMoving; host.guard_face_prop = hGuardFaceProp;
    host.guard_anim = hGuardAnim; host.prop_usable = hPropUsable; host.prop_used = hPropUsed;
    host.guard_keep_body = hGuardKeepBody;
    host.guard_aim_at = hGuardAimAt; host.guard_hit = hGuardHit; host.guard_sees_player = hGuardSeesPlayer;
    host.guard_near_miss = hGuardNearMiss; host.guards_near = hGuardsNear; host.guard_hits = hGuardHits;
    s_nkeepBody = 0;
    objectiveLinesReset();
    snprintf(dir, sizeof dir, "%s", missionsDir());
    s_rt = missionRtCreate(missionsCurrent(), &host, dir, missionsModDir(), err, sizeof err);
    if (!s_rt) {
        sysLogPrintf(LOG_WARNING, "missions: no script runtime: %s", err);
        return;
    }
    sysLogPrintf(LOG_INFO, "missions: runtime up: %d script(s), %d setup prop(s)", missionRtScriptCount(s_rt), s_nprop);
    {   /* what the setup spawned: the guards (record, chr number), so a mission's moved and added guards can be seen */
        char line[4000];
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
                    if (c && c->prop && u < sizeof line - 120)
                        snprintf(line + u, sizeof line - u, " [prop %d chr %d pad %d body %d head %d (record %d) at %.0f %.0f %.0f]", i, g->chrnum, g->PadID, c->bodynum, c->headnum, g->HeadID,
                                 c->prop->pos.f[0], c->prop->pos.f[1], c->prop->pos.f[2]);
                }
            }
        }
        sysLogPrintf(LOG_INFO, "missions: setup guards: %d records, %d spawned;%s", n, spawned, line);
        {   /* the mission's own guards, one line each (the summary above is cut at the log's line length) */
            const Mission *m = missionsCurrent();
            for (int k = 0; m && k < m->nguards; k++) {
                const MissionGuardOp *op = &m->guards[k];
                GuardRecord *g;
                ChrRecord *c;
                if (op->op != MISSION_OP_ADD || op->setup_prop < 0 || op->setup_prop >= s_nprop) continue;
                g = (GuardRecord *)s_prop[op->setup_prop];
                c = g->Data ? chrFindByLiteralId(g->chrnum) : NULL;
                sysLogPrintf(LOG_INFO, "missions: added guard \"%s\" prop %d chr %d: record body %d head %d; spawned body %d head %d",
                             op->name, op->setup_prop, g->chrnum, g->BodyID, g->HeadID, c ? c->bodynum : -1, c ? c->headnum : -1);
            }
        }
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
    keepBodiesTick();
    objectiveLinesTick();
}

/* The status the objective screens show. */
int missionsObjectivesSuccess(void)
{
    if (!missionsActive() || !s_rt) return 0;
    return missionRtAllComplete(s_rt, (int)lvlGetSelectedDifficulty());
}
