/* The guard domain of the campaign API (#165): host side, the game's own functions behind
 * port/mission/mission_host_guard.h. Layout: port/mission/mission_lua_ext.h.
 *
 * Every verb calls what the game's own AI interpreter calls for the same command (src/game/chrai.c: the AI_* case, then
 * the chraction.c / chr.c function behind it); the case's own few lines are copied only where the game has no function
 * to call (teleport, clone, spawn). Nothing in src/game changes.
 *
 * Per-tick work (missionsHostGuardTick): the one-update engine signals (a sight check that passed, a heard shot, a
 * near miss, a hit or death another guard saw) become the events "guard_spotted" / "guard_heard" / "guard_near_miss" /
 * "guard_witnessed" (a script that decides every 0.1 s would miss them as queries); spotted_player(), was_shot_at()
 * and witnessed() read what the last tick found. Also "anim_end", "guard_damaged" and the alarm walk of press_alarm.
 * State is per g_ChrSlots slot, keyed by chr number. */
#include "platform.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>

#include <ultra64.h>
#include "game/chr.h"
#include "game/chraction.h"
#include "game/chrai.h"
#include "game/bondview.h"
#include "game/lv.h"
#include "game/player.h"
#include "game/propobj.h"
#include "game/objecthandler.h"
#include "game/model.h"
#include "game/stan.h"
#include "game/gun.h"
#include "game/math_atan2f.h"   /* atan2f: the game's own header declares it */
#include "game/file.h"
#include "snd.h"
#include "music.h"
#include "random.h"
#include "drawdistgameplay.h"
#include "bondaicommands.h"
#include "mission.h"
#include "mission_lua.h"
#include "mission_host_guard.h"
#include "missions_priv.h"
#include "system.h"
#include "portevents.h"
#include "mission_apply.h"   /* missionPropdefSize */
#include "game/bondhead.h"   /* waypointFindRoute */
#include <math.h>   /* after the game headers, as missions_host.c: atan2f / sqrtf */

extern s32 g_ClockTimer;
MissionRuntime *missionsRuntime(void);                 /* missions_host.c */
s32 chrIsNotDeadOrShot(ChrRecord *chr);                 /* chraction.c, not in its header */
waypoint *chrlvStanPathRelated(coord3d *pos, StandTile *stan);   /* chraction.c, not in its header */
PathRecord *pathFindById(s32 ID);                       /* chrai.c, not in its header */
void propweaponSetDual(WeaponObjRecord *a, WeaponObjRecord *b);   /* propobj.c, as chrai.c's AI_TRYCloningChr uses it */

#define ALARM_NEAR     100.0f /* units: press_alarm starts the press this close to the switch's pad */
#define SINK_COORD     100000.0f   /* mission_apply.c: the removal sink pad */

static MissionHostGuard s_host;

/* One record per g_ChrSlots slot, valid while chrnum matches. */
typedef struct Track {
    int    chrnum;            /* -1: unused */
    f32    dmg;
    s32    numarghs;
    s8     closeargs;
    s32    lastHit60;         /* g_GlobalTimer of the last hit (damage, or a hit an invincible guard shrugged off) */
    int    nearTotal, nearNow;  /* near_misses(); a shot passed it in the last tick */
    int    saw, sawOther;       /* witnessed() of the last tick: MG_SAW_*, the chr */
    int    spotted;             /* spotted_player() of the last tick: the player; -1 none */
    int    prevAct;             /* its action at the last tick (a hit / death starts the witnesses) */
    s32    prevHeard;           /* lastheartarget60 at the last tick */
    int    animActive, animInterruptible;
    int    alarmPad;          /* press_alarm: walking to this pad's switch; -1 none */
    int    dormant;
    AIRecord *dormantList;
    u16    dormantOff;
} Track;
static Track *s_trk;
static int    s_ntrk;

static void trackInit(Track *t, const ChrRecord *c)
{
    memset(t, 0, sizeof *t);
    t->chrnum = c->chrnum;
    t->dmg = c->damage;
    t->numarghs = c->numarghs;
    t->closeargs = c->numclosearghs;
    t->saw = MG_SAW_NONE;
    t->sawOther = -1;
    t->spotted = -1;
    t->prevAct = c->actiontype;
    t->prevHeard = c->lastheartarget60;
    t->alarmPad = -1;
}

static ChrRecord *chrAny(int chr)
{
    ChrRecord *c;
    if (chr < 0) return NULL;
    c = chrFindByLiteralId(chr);
    return (c && c->prop && c->model) ? c : NULL;
}

static ChrRecord *chrLive(int chr)
{
    ChrRecord *c = chrAny(chr);
    return (c && !chrIsDead(c)) ? c : NULL;
}

static int setupPropOfChr(int chr);

/* The guard an action verb works on: alive, and not a revivable setup guard lying down (no order reaches it, as for the
 * setup methods: missionsHostGuardDown). */
static ChrRecord *chrGo(int chr)
{
    ChrRecord *c = chrLive(chr);
    if (c) {
        int p = setupPropOfChr(chr);
        if (p >= 0 && missionsHostGuardDown(p)) return NULL;
    }
    return c;
}

static Track *trkOf(const ChrRecord *c)
{
    int i;
    if (!c || !g_ChrSlots) return NULL;
    i = (int)(c - g_ChrSlots);
    if (i < 0 || i >= g_NumChrSlots) return NULL;
    if (i >= s_ntrk) {
        Track *n = (Track *)realloc(s_trk, sizeof(Track) * (size_t)g_NumChrSlots);
        if (!n) return NULL;
        for (int k = s_ntrk; k < g_NumChrSlots; k++) { memset(&n[k], 0, sizeof n[k]); n[k].chrnum = -1; }
        s_trk = n;
        s_ntrk = g_NumChrSlots;
    }
    if (s_trk[i].chrnum != c->chrnum) trackInit(&s_trk[i], c);
    return &s_trk[i];
}

/* "The player" of a guard: the one asked for, else the nearest living one (the game's own pick for a guard's tick in
 * co-op is coop.c coopChooseTarget, which is file-local; the nearest living player is what it picks for a guard). */
static int playerFor(const ChrRecord *c, int player)
{
    const int n = getPlayerCount() < 4 ? getPlayerCount() : 4;
    int best = -1;
    f32 bd = 0.0f;
    extern struct player *g_playerPointers[4];
    if (player >= 0 && player < n) return player;
    for (int p = 0; p < n; p++) {
        const struct player *pl = g_playerPointers[p];
        f32 dx, dy, dz, d;
        if (!pl || !pl->prop || pl->bonddead) continue;
        dx = pl->prop->pos.f[0] - c->prop->pos.f[0];
        dy = pl->prop->pos.f[1] - c->prop->pos.f[1];
        dz = pl->prop->pos.f[2] - c->prop->pos.f[2];
        d = dx * dx + dy * dy + dz * dz;
        if (best < 0 || d < bd) { best = p; bd = d; }
    }
    return best < 0 ? 0 : best;
}

static int setupPropOfChr(int chr)
{
    for (int i = 0; i < missionsHostPropCount(); i++) {
        const PropDefHeaderRecord *h = (const PropDefHeaderRecord *)missionsHostPropRec(i);
        if (h && h->type == PROPDEF_GUARD && ((const GuardRecord *)h)->chrnum == chr) return i;
    }
    return -1;
}

static PadRecord *padRec(int pad)
{
    if (pad < 0 || !g_CurrentSetup.pads) return NULL;
    if (isNotBoundPad(pad)) return &g_CurrentSetup.pads[pad];
    return g_CurrentSetup.boundpads ? (PadRecord *)&g_CurrentSetup.boundpads[getBoundPadNum(pad)] : NULL;
}

static SPEED speedOf(int s) { return s >= MG_SPRINT ? SPEED_SPRINT : s == MG_RUN ? SPEED_RUN : SPEED_WALK; }

static int s_warned[64];
static void notYet(int key, const char *what)
{
    if (key >= 0 && key < 64 && !s_warned[key]) {
        s_warned[key] = 1;
        sysLogPrintf(LOG_INFO, "missions: %s: not implemented yet", what);
    }
}

/* ---- presence -------------------------------------------------------------------------------------------------- */
static int hExists(void *ud, int chr) { (void)ud; return chrLive(chr) != NULL; }

static int hIsDying(void *ud, int chr)
{
    ChrRecord *c = chrAny(chr);
    (void)ud;
    return c && c->actiontype == ACT_DIE;
}

/* AI_ChrRemoveInstant */
static void hRemove(void *ud, int chr)
{
    ChrRecord *c = chrAny(chr);
    (void)ud;
    if (c) c->hidden |= CHRHIDDEN_REMOVE;
}

/* AI_RemoveMe */
static void hFadeOut(void *ud, int chr)
{
    ChrRecord *c = chrAny(chr);
    (void)ud;
    if (c) chrFadeOut(c);
}

/* AI_TRYTeleportingChrToPad, minus the label jump: the chr is placed on the pad (chrAdjustPosForSpawn may move it a
 * little off a taken spot); 0 when no spot near the pad is free. */
static int hTeleport(void *ud, int chr, int padnum)
{
    ChrRecord *c = chrAny(chr);
    PadRecord *pad = padRec(padnum);
    f32 facing;
    coord3d pos;
    StandTile *stan;
    int pass = 0;
    (void)ud;
    if (!c || !pad) return 0;
    facing = atan2f(pad->look.x, pad->look.z);
    pos.x = pad->pos.x; pos.y = pad->pos.y; pos.z = pad->pos.z;
    stan = pad->stan;
    sub_GAME_7F03D058(c->prop, FALSE);
    if (chrAdjustPosForSpawn(&pos, &stan, facing, TRUE)) {
        c->prop->pos.x = pos.x; c->prop->pos.y = pos.y; c->prop->pos.z = pos.z;
        c->prop->stan = stan;
        c->chrflags |= CHRFLAG_INIT;
        setsubroty(c->model, facing);
        setsuboffset(c->model, &pos);
        chrDetectRooms(c);
        pass = 1;
    }
    sub_GAME_7F03D058(c->prop, TRUE);
    return pass;
}

/* AI_HideAllChrs / AI_ShowAllChrs, for one chr */
static void hSetHidden(void *ud, int chr, int on)
{
    ChrRecord *c = chrAny(chr);
    (void)ud;
    if (!c) return;
    if (on) c->chrflags |= CHRFLAG_HIDDEN;
    else c->chrflags &= ~CHRFLAG_HIDDEN;
}

/* AI_SetChrAiList */
static int hSetAi(void *ud, int chr, int list)
{
    ChrRecord *c = chrAny(chr);
    (void)ud;
    if (!c) return 0;
    c->ailist = ailistFindById(list);
    c->aioffset = 0;
    c->sleep = 0;
    return 1;
}

/* the AI list id the setup's guard record gives it (GuardRecord.AIListID, as the mission's "ai" put it there) */
static int hSetupAi(void *ud, int chr)
{
    int p = setupPropOfChr(chr);
    (void)ud;
    return p < 0 ? -1 : (int)((const GuardRecord *)missionsHostPropRec(p))->AIListID;
}

static int hAllGuards(void *ud, int *props, int *chrs, int max)
{
    int n = 0;
    (void)ud;
    for (int i = 0; i < missionsHostPropCount() && n < max; i++) {
        const PropDefHeaderRecord *h = (const PropDefHeaderRecord *)missionsHostPropRec(i);
        const GuardRecord *g;
        if (!h || h->type != PROPDEF_GUARD) continue;
        g = (const GuardRecord *)h;
        if (!g->Data || !chrAny(g->chrnum)) continue;
        props[n] = i;
        chrs[n] = g->chrnum;
        n++;
    }
    return n;
}

static int padCount(void)
{
    const PadRecord *pads = g_CurrentSetup.pads;
    int i;
    for (i = 0; pads && i < 8000; i++) {
        const PadRecord *pd = &pads[i];
        if (pd->plink == NULL && pd->pos.x == 0.0f && pd->pos.y == 0.0f && pd->pos.z == 0.0f) break;   /* the terminator (relight.c) */
    }
    return i;
}

/* A mission pad's number: mission_apply.c appends the mission's pads after the level's own, then (when a guard is removed
 * or kept off this difficulty) one sink pad at SINK_COORD. */
static int hPadById(void *ud, const char *id)
{
    const Mission *m = missionsCurrent();
    int k = -1, n, sink;
    (void)ud;
    if (!m || !id) return -1;
    for (int i = 0; i < m->npads; i++)
        if (!strcmp(m->pads[i].id, id)) k = i;
    if (k < 0) return -1;
    n = padCount();
    sink = n > 0 && g_CurrentSetup.pads[n - 1].pos.f[0] == SINK_COORD;
    return n - m->npads - sink + k;
}

/* spawn_guard: AI_TRYSpawningChrAtPad. The new guard runs GAILIST_DEAD_AI (inert: the script drives it, set_ai changes it).
 * A chr number that exists (alive or lying dead) is refused. */
static int hSpawn(void *ud, const MissionGuardSpawn *s)
{
    PropRecord *prop;
    ChrRecord *c;
    s32 flags = (s->sunglasses ? 1 : 0) | (s->force ? 0x10 : 0);
    (void)ud;
    if (s->chr >= 0 && chrFindByLiteralId(s->chr)) return -1;
    if (!padRec(s->pad)) return -1;
    prop = chrSpawnAtPad(NULL, s->body, s->head, s->pad, (AIListRecord *)ailistFindById(GAILIST_DEAD_AI), flags);
    if (!prop || !(c = prop->chr)) return -1;
    if (s->chr >= 0) c->chrnum = (s16)s->chr;
    return c->chrnum;
}

/* ---- settings -------------------------------------------------------------------------------------------------- */
/* AI_SetMyHealthTotal (chrSetMaxDamage), AI_SetMyArmour (chrAddHealth), AI_SetMySpeedRating / ArghRating /
 * AccuracyRating / SetMyGrenadeProbability / SetMyVisionRange; raw skips get_007_health_mod() (the 007 health scale
 * those two apply). Health is set through the damage: damage = maxdamage - health. Armour is negative damage
 * (chrGetArmor): it replaces any damage taken. The speed rating's 007 scaling is applied where it is read
 * (chrlvGetGuard007SpeedRating), so it needs no raw variant. Reaction is the setup's ReactionTime: visionrange. */
static int clampRating(double v) { return v < 0 ? 0 : v > 100 ? 100 : (int)(v + 0.5); }

static void hSetStats(void *ud, int chr, const MissionGuardStats *st)
{
    ChrRecord *c = chrAny(chr);
    const f32 mod = st->raw ? 1.0f : get_007_health_mod();
    (void)ud;
    if (!c) return;
    if (st->has & MG_ST_MAX_HEALTH) c->maxdamage = (f32)st->max_health * mod;
    if (st->has & MG_ST_HEALTH) {
        f32 d = c->maxdamage - (f32)st->health * mod;
        c->damage = d < 0.0f ? 0.0f : d;
    }
    if (st->has & MG_ST_ARMOUR) c->damage = -(f32)st->armour * mod;
    if (st->has & MG_ST_SPEED) c->speedrating = (s8)clampRating(st->speed);
    if (st->has & MG_ST_ACCURACY) c->accuracyrating = (s8)clampRating(st->accuracy);
    if (st->has & MG_ST_REACTION) c->visionrange = (f32)st->reaction;
    if (st->has & MG_ST_GRENADE) c->grenadeprob = (u8)(clampRating(st->grenade_chance) * 255 / 100);
    if (st->has & MG_ST_ADD_ARMOUR) chrAddHealth(c, (f32)st->add_armour);   /* AI_SetMyArmour: chrAddHealth (scaled by the health mod itself) */
    if (st->has & MG_ST_HEARING) c->hearingscale = (f32)st->hearing;       /* AI_SetMyHearingScale */
    if (st->has & MG_ST_VISION) c->visionrange = (f32)st->vision;          /* AI_SetMyVisionRange */
    if (st->has & MG_ST_RECOVERY) c->arghrating = (s8)clampRating(st->recovery);   /* AI_SetMyArghRating */
    if ((st->has & MG_ST_FLINCH) && st->flinch < 100) notYet(0, "set_stats flinch below 100 (the game has no per-guard switch for hit reactions)");
}

static u32 flagBit(int flag)
{
    switch (flag) {
    case MG_FLAG_INVINCIBLE:        return CHRFLAG_INVINCIBLE;
    case MG_FLAG_NO_AUTOAIM:        return CHRFLAG_NO_AUTOAIM;
    case MG_FLAG_CAN_SHOOT_GUARDS:  return CHRFLAG_CAN_SHOOT_CHRS;
    case MG_FLAG_CIVILIAN:          return CHRFLAG_COUNT_DEATH_AS_CIVILIAN;
    case MG_FLAG_NO_BLOOD:          return CHRSTART_FORCENOBLOOD;
    case MG_FLAG_FAST_SPRINT:       return CHRFLAG_INCREASE_RUNNING_SPEED;
    case MG_FLAG_NO_SHADOW:         return CHRFLAG_NO_SHADOW;
    case MG_FLAG_CLONE_SENDER:      return CHRFLAG_CLONE;
    default:                        return 0;
    }
}

/* dormant: the game has no such flag; the guard stands and its AI list is parked on GAILIST_DEAD_AI (the way the host
 * parks a revivable guard that is down) until it is switched off, which gives its list back. */
static void hSetFlag(void *ud, int chr, int flag, int on)
{
    ChrRecord *c = chrAny(chr);
    Track *t = trkOf(c);
    u32 bit = flagBit(flag);
    (void)ud;
    if (!c) return;
    if (bit) {
        if (on) c->chrflags |= bit;
        else c->chrflags &= ~bit;
    } else if (flag == MG_FLAG_DORMANT && t) {
        if (on && !t->dormant) {
            t->dormant = 1;
            t->dormantList = c->ailist;
            t->dormantOff = c->aioffset;
            c->ailist = ailistFindById(GAILIST_DEAD_AI);
            c->aioffset = 0;
            c->sleep = 0;
            chraiStopAnimation(c);
        } else if (!on && t->dormant) {
            t->dormant = 0;
            c->ailist = t->dormantList;
            c->aioffset = t->dormantOff;
            c->sleep = 0;
        }
    } else if (flag == MG_FLAG_SUNGLASSES) {
        notYet(1, "set_flag sunglasses (the glasses are part of the model chosen at spawn: spawn_guard{ sunglasses = true })");
    }
}

static int hGetFlag(void *ud, int chr, int flag)
{
    ChrRecord *c = chrAny(chr);
    Track *t = trkOf(c);
    u32 bit = flagBit(flag);
    (void)ud;
    if (!c) return 0;
    if (bit) return (c->chrflags & bit) == bit;
    if (flag == MG_FLAG_DORMANT) return t && t->dormant;
    return 0;
}

/* The chr model the game's own lists give each weapon (chraidata.c TRYGiveMeItem(PROP_CHRxxx, ITEM_xxx ...)). */
static int weaponModel(int item)
{
    switch (item) {
    case ITEM_KNIFE: return PROP_CHRKNIFE;
    case ITEM_THROWKNIFE: return PROP_CHRTHROWKNIFE;
    case ITEM_WPPK: return PROP_CHRWPPK;
    case ITEM_WPPKSIL: return PROP_CHRWPPKSIL;
    case ITEM_TT33: return PROP_CHRTT33;
    case ITEM_SKORPION: return PROP_CHRSKORPION;
    case ITEM_AK47: return PROP_CHRKALASH;
    case ITEM_UZI: return PROP_CHRUZI;
    case ITEM_MP5K: return PROP_CHRMP5K;
    case ITEM_MP5KSIL: return PROP_CHRMP5KSIL;
    case ITEM_SPECTRE: return PROP_CHRSPECTRE;
    case ITEM_M16: return PROP_CHRM16;
    case ITEM_FNP90: return PROP_CHRFNP90;
    case ITEM_SHOTGUN: return PROP_CHRSHOTGUN;
    case ITEM_AUTOSHOT: return PROP_CHRAUTOSHOT;
    case ITEM_SNIPERRIFLE: return PROP_CHRSNIPERRIFLE;
    case ITEM_RUGER: return PROP_CHRRUGER;
    case ITEM_GOLDENGUN: return PROP_CHRGOLDEN;
    case ITEM_SILVERWPPK: return PROP_CHRSILVERWPPK;
    case ITEM_GOLDWPPK: return PROP_CHRGOLDWPPK;
    case ITEM_LASER: return PROP_CHRLASER;
    case ITEM_GRENADELAUNCH: return PROP_CHRGRENADELAUNCH;
    case ITEM_ROCKETLAUNCH: return PROP_CHRROCKETLAUNCH;
    case ITEM_GRENADE: return PROP_CHRGRENADE;
    default: return -1;
    }
}

/* AI_TRYGiveMeItem: chrGiveWeapon(chr, model, item, flags); left hand PROPFLAG_WEAPON_LEFTHANDED, hidden PROPFLAG_CONCEAL_GUN. */
static int hGiveWeapon(void *ud, int chr, int item, int model, int left, int concealed)
{
    ChrRecord *c = chrLive(chr);
    s32 flags = (left ? PROPFLAG_WEAPON_LEFTHANDED : 0) | (concealed ? PROPFLAG_CONCEAL_GUN : 0);
    (void)ud;
    if (!c) return 0;
    if (model < 0) model = weaponModel(item);
    if (model < 0) return 0;
    return chrGiveWeapon(c, model, (ITEM_IDS)item, flags) != NULL;
}

/* AI_TRYGiveMeHat */
static int hGiveHat(void *ud, int chr, int model)
{
    ChrRecord *c = chrLive(chr);
    (void)ud;
    return c && hatCreateForChr(c, model, 0) != NULL;
}

/* AI_ChrDropAllConcealedItems + AI_ChrDropAllHeldItems */
static void hDropItems(void *ud, int chr)
{
    ChrRecord *c = chrAny(chr);
    (void)ud;
    if (!c) return;
    chrDropItems(c);
    for (int h = 0; h < 2; h++)
        if (c->weapons_held[h]) {
            propobjSetDropped(c->weapons_held[h], 1);
            c->hidden |= CHRHIDDEN_DROP_HELD_ITEMS;
        }
}

/* AI_ChrRemoveItemInHand, both hands */
static void hRemoveHeld(void *ud, int chr)
{
    ChrRecord *c = chrAny(chr);
    (void)ud;
    if (!c) return;
    chrSetWeaponFlag4(c, GUNRIGHT);
    chrSetWeaponFlag4(c, GUNLEFT);
}

static int hPresetPad(void *ud, int chr)
{
    ChrRecord *c = chrAny(chr);
    (void)ud;
    return c ? (int)c->padpreset1 : -1;
}

/* set_flag_raw: the chr's own flag word, as AI_SetMyFlags / AI_UnsetMyFlags would */
static void hSetFlagRaw(void *ud, int chr, unsigned bits, int on)
{
    ChrRecord *c = chrAny(chr);
    (void)ud;
    if (!c) return;
    if (on) c->chrflags |= (CHRFLAG)bits;
    else c->chrflags &= ~(CHRFLAG)bits;
}

/* the item in the right hand (a prop that is no weapon, or an empty hand: -1) */
static int hHeldWeapon(void *ud, int chr)
{
    ChrRecord *c = chrAny(chr);
    PropRecord *rp;
    (void)ud;
    if (!c) return -1;
    rp = chrGetEquippedWeaponProp(c, GUNRIGHT);
    return (rp && rp->type == PROP_TYPE_WEAPON && rp->weapon) ? (int)rp->weapon->weaponnum : -1;
}

/* a setup object as the game's object record (a door or a guard is no carried object) */
static ObjectRecord *carriedObj(int prop)
{
    PropDefHeaderRecord *h = (PropDefHeaderRecord *)missionsHostPropRec(prop);
    if (!h || h->type == PROPDEF_GUARD || h->type == PROPDEF_DOOR || missionPropdefSize(h->type) < 144) return NULL;
    return ((ObjectRecord *)h)->prop ? (ObjectRecord *)h : NULL;
}

/* AI_DropObject: only an object this guard carries */
static int hDropObject(void *ud, int chr, int prop)
{
    ChrRecord *c = chrAny(chr);
    ObjectRecord *o = carriedObj(prop);
    (void)ud;
    if (!c || !o || !o->prop->parent || o->prop->parent->type != PROP_TYPE_CHR || o->prop->parent->chr != c) return 0;
    propobjSetDropped(o->prop, 2);
    c->hidden |= CHRHIDDEN_DROP_HELD_ITEMS;
    return 1;
}

/* AI_ChrEquipObject: taken from where it is (a carrier, or the world), then into the hand if it is a weapon, else carried */
static int hGiveObject(void *ud, int chr, int prop)
{
    ChrRecord *c = chrAny(chr);
    ObjectRecord *o = carriedObj(prop);
    (void)ud;
    if (!c || !o) return 0;
    if (o->prop->parent) {
        objDetach(o->prop);
    } else {
        chrpropDeregisterRooms(o->prop);
        chrpropDelist(o->prop);
        chrpropDisable(o->prop);
    }
    if (o->type != PROPDEF_COLLECTABLE || !chrEquipWeapon((WeaponObjRecord *)o, c)) chrpropReparent(o->prop, c->prop);
    return 1;
}

/* ---- movement -------------------------------------------------------------------------------------------------- */
/* AI_StartPatrol */
static void hPatrol(void *ud, int chr, int pathid)
{
    ChrRecord *c = chrAny(chr);
    PathRecord *path = pathFindById(pathid);
    (void)ud;
    if (c && path && !(setupPropOfChr(chr) >= 0 && missionsHostGuardDown(setupPropOfChr(chr)))) if_actor_able_set_on_path(c, (s32)(intptr_t)path);
}

static int hIsPatrolling(void *ud, int chr)
{
    ChrRecord *c = chrAny(chr);
    (void)ud;
    return c && c->actiontype == ACT_PATROL;
}

static int hIsMoving(void *ud, int chr)
{
    ChrRecord *c = chrAny(chr);
    (void)ud;
    return c && (c->actiontype == ACT_GOPOS || c->actiontype == ACT_PATROL);
}

/* AI_RunToPad / WalkToPad / SprintToPad */
static int hRunTo(void *ud, int chr, int pad, int speed)
{
    ChrRecord *c = chrGo(chr);
    (void)ud;
    return c && chrGoToPad(c, pad, speedOf(speed)) != 0;
}

/* AI_TRYRunToBond / WalkToBond / SprintToBond: chrGoToBond targets the CURRENT player (and refuses a dying chr and a
 * tenth chasing guard itself). */
struct runArg { ChrRecord *c; SPEED speed; int res; };
static void doRunToPlayer(void *a) { struct runArg *r = (struct runArg *)a; r->res = chrGoToBond(r->c, r->speed) != 0; }

static int hRunToPlayer(void *ud, int chr, int speed, int player)
{
    ChrRecord *c = chrGo(chr);
    struct runArg a;
    (void)ud;
    if (!c) return 0;
    a.c = c; a.speed = speedOf(speed); a.res = 0;
    missionsHostWithPlayer(playerFor(c, player), doRunToPlayer, &a);
    return a.res;
}

/* face_player turns once: the game's AI_TRYFacingTarget with TARGET_BOND keeps tracking the player, so the compass
 * heading to the player's position now is set instead (TARGET_COMPASS, as missions_host.c face does). */
static void hFacePlayer(void *ud, int chr, int player)
{
    ChrRecord *c = chrGo(chr);
    extern struct player *g_playerPointers[4];
    const struct player *pl;
    f32 a;
    (void)ud;
    if (!c) return;
    pl = g_playerPointers[playerFor(c, player)];
    if (!pl || !pl->prop) return;
    a = atan2f(pl->prop->pos.f[0] - c->prop->pos.f[0], pl->prop->pos.f[2] - c->prop->pos.f[2]);
    if (a < 0) a += 6.2831853f;
    check_set_actor_standing_still(c, TARGET_COMPASS, (s32)(a * 65535.0f / 6.2831853f));
}

static void hFacePad(void *ud, int chr, int pad)
{
    ChrRecord *c = chrGo(chr);
    (void)ud;
    if (c) check_set_actor_standing_still(c, TARGET_PAD, pad);
}

/* AI_Stop (chraiStopAnimation): a guard that is neither on its way nor in an animation is left alone. */
static void hHalt(void *ud, int chr)
{
    ChrRecord *c = chrGo(chr);
    Track *t = trkOf(c);
    (void)ud;
    if (!c) return;
    if (t) t->alarmPad = -1;
    if (c->actiontype == ACT_GOPOS || c->actiontype == ACT_PATROL || c->actiontype == ACT_RUNPOS || c->actiontype == ACT_ANIM)
        chraiStopAnimation(c);
}

/* The game has removed its flee and find-cover routines (chraction.c removed_animation_routine_27 / _2B return FALSE
 * and AI_TRYRunFromBond / AI_TRYFindCover never do anything), so there is no pad it would pick. */
static int hFleePad(void *ud, int chr) { (void)ud; (void)chr; notYet(2, "flee_pad (the game's flee routine is removed: it never picks a pad)"); return -1; }
static int hCoverPad(void *ud, int chr) { (void)ud; (void)chr; notYet(3, "cover_pad (the game's find-cover routine is removed: it never picks a pad)"); return -1; }

/* A pad near the player that the guard has a route to. The game has no such picker (its flee / cover routines are
 * removed), so: the nearest floor pad at least SPOT_MIN from the player, with the game's own waypoint route test
 * (what plot_course_for_actor asks, without starting the walk). */
#define SPOT_MIN 150.0f
static int hSpotNearPlayer(void *ud, int chr, int player)
{
    ChrRecord *c = chrLive(chr);
    const struct player *pl;
    waypoint *from;
    f32 bd = 0.0f;
    int best = -1, n;
    (void)ud;
    if (!c) return -1;
    pl = g_playerPointers[playerFor(c, player)];
    if (!pl || !pl->prop || !g_CurrentSetup.pads) return -1;
    from = chrlvStanPathRelated(&c->prop->pos, c->prop->stan);
    if (!from) return -1;
    n = padCount();
    for (int i = 0; i < n; i++) {
        const PadRecord *pd = &g_CurrentSetup.pads[i];
        f32 dx, dy, dz, d;
        waypoint *to, *route[MAX_CHRWAYPOINTS];
        if (!pd->stan || pd->up.y < 0.5f || pd->pos.x >= SINK_COORD * 0.5f) continue;
        dx = pd->pos.x - pl->prop->pos.f[0]; dy = pd->pos.y - pl->prop->pos.f[1]; dz = pd->pos.z - pl->prop->pos.f[2];
        d = dx * dx + dy * dy + dz * dz;
        if (d < SPOT_MIN * SPOT_MIN || (best >= 0 && d >= bd)) continue;
        to = chrlvStanPathRelated((coord3d *)&pd->pos, pd->stan);
        if (!to || waypointFindRoute(from, to, route, MAX_CHRWAYPOINTS) < 2) continue;
        best = i; bd = d;
    }
    return best;
}

/* ---- gestures and animation ------------------------------------------------------------------------------------ */
static void hLookAround(void *ud, int chr) { ChrRecord *c = chrGo(chr); (void)ud; if (c) chrTrySurprisedLookAround(c); }   /* AI_LookSurprised */
static void hPointAtPlayer(void *ud, int chr) { ChrRecord *c = chrGo(chr); (void)ud; if (c) chrTrySurprisedOneHand(c); }  /* AI_PointAtBond */
static void hRaiseArms(void *ud, int chr) { ChrRecord *c = chrGo(chr); (void)ud; if (c) chrTrySurprisedSurrender(c); }    /* AI_RaiseArms */
static void hKneel(void *ud, int chr) { ChrRecord *c = chrGo(chr); (void)ud; if (c) check_if_able_to_then_kneel(c); }     /* AI_Kneel */

#define ANIM_FLAG_HOLD   0x04    /* bondaicommands.h ANIM_LOOP_HOLD_LAST_FRAME */
#define ANIM_FLAG_SFX    0x08
#define ANIM_FLAG_IDLE   0x10
#define ANIM_FLAG_NOMOVE 0x40
#define ANIM_FLAG_REVERSE 0x80
#define ANIM_FLAG_MIRROR 0x01

/* the same names and frame ranges as missions_host.c's play_anim */
static const struct { const char *name; int anim, start, end; } kAnims[] = {
    { "keyboard_right_hand_tapping", ANIM_keyboard_right_hand_tapping, 0, 89 },
    { "keyboard_left_hand",          ANIM_keyboard_left_hand,          0, 79 },
    { "keyboard_right_hand2",        ANIM_keyboard_right_hand2,        0, 74 },
    { "kneel_cough",                 ANIM_coughing_kneel1,             0, -1 },
    { "kneel_cough2",                ANIM_coughing_kneel2,             0, -1 },
    { "fall_face_down",              ANIM_death_forward_face_down_soft, 0, -1 },
    { "fall_face_up",                ANIM_death_backward_fall_face_up1, 0, -1 },
};

/* AI_PlayAnimation: check_if_able_to_then_perform_animation(chr, anim, start, end, flags, interpolation). The options are
 * the command's own fields; speed and loop are set on the model after it. */
static int hAnim(void *ud, int chr, int id, const char *name, const MissionGuardAnim *o)
{
    ChrRecord *c = chrGo(chr);
    Track *t = trkOf(c);
    int start = 0, end = -1, flags = 0, interp = 16;
    (void)ud;
    if (!c || !t) return 0;
    if (id < 0) {
        int found = 0;
        for (size_t k = 0; name && k < sizeof kAnims / sizeof kAnims[0]; k++)
            if (!strcmp(kAnims[k].name, name)) { id = kAnims[k].anim; start = kAnims[k].start; end = kAnims[k].end; found = 1; }
        if (!found) return 0;
    }
    if (id >= ANIM_MAX) return 0;
    if (o->from >= 0) start = o->from;
    if (o->to >= 0) end = o->to;
    if (o->hold) flags |= ANIM_FLAG_HOLD;
    if (o->reverse) flags |= ANIM_FLAG_REVERSE | ANIM_FLAG_HOLD;   /* the game: reverse needs the hold bit */
    if (o->no_move) flags |= ANIM_FLAG_NOMOVE;
    if (o->idle_after > 0 || (o->idle_after < 0 && !o->hold && !o->loop)) flags |= ANIM_FLAG_IDLE;
    if (o->blend >= 0) interp = (s32)(o->blend * 60.0);
    if (!check_if_able_to_then_perform_animation(c, id, start, end, (u8)flags, interp)) return 0;
    if (o->loop) modelSetAnimLooping(c->model, (f32)start, 16.0f);
    if (o->speed > 0 || o->reverse) {
        f32 sp = 0.5f * (o->speed > 0 ? (f32)o->speed : 1.0f);
        modelSetAnimSpeed(c->model, o->reverse ? -sp : sp, 0.0f);
    }
    if (o->sound >= 0) {
        ALSoundState *p = sndPlaySfx((struct ALBankAlt_s *)g_musicSfxBufferPtr, (s16)o->sound, NULL);
        chrobjSndCreatePostEventDefault(p, &c->prop->pos);
    }
    t->animActive = 1;
    t->animInterruptible = o->interruptible;
    return 1;
}

/* an anim() is over when the guard left the animation (cut short), or its last frame was reached (a held one stays there) */
static int animFinished(const ChrRecord *c)
{
    if (c->actiontype != ACT_ANIM) return 1;
    if (modelGetAnimSpeed(c->model) >= 0.0f) return modelGetAnimFrame(c->model) >= modelGetAnimEndFrame(c->model);
    return modelGetAnimFrame(c->model) <= 0.0f;
}

/* :surrender() has played out when the hands are up: the surrender animation loops from its frame 30 (armed) / 40 (the
 * weapon-dropping one) and runs on, chrlvActorThrowWeaponSurrender; frame 40 is past both loop starts. */
static int surrenderReached(const ChrRecord *c)
{
    return modelGetAnimFrame(c->model) >= 40.0f;
}

static int hAnimDone(void *ud, int chr)
{
    ChrRecord *c = chrAny(chr);
    Track *t = trkOf(c);
    (void)ud;
    if (!c || !t) return 1;
    if (!t->animActive) return c->actiontype != ACT_SURRENDER || surrenderReached(c);
    return animFinished(c);
}

static void hStopAnim(void *ud, int chr)
{
    ChrRecord *c = chrGo(chr);
    (void)ud;
    if (c && c->actiontype == ACT_ANIM) chraiStopAnimation(c);
}

/* GAILIST_PLAY_IDLE_ANIMATION (chraidata.c m_IdleAnimations): one of six fidgets by a random byte */
static void hIdleAnim(void *ud, int chr)
{
    static const struct { int lim, anim, end; } fid[] = {
        { 50, ANIM_yawning, 193 }, { 100, ANIM_swatting_flies, 294 }, { 150, ANIM_scratching_leg, 183 },
        { 200, ANIM_scratching_butt, 123 }, { 250, ANIM_adjusting_crotch, 56 }, { 255, ANIM_sneeze, 137 },
    };
    ChrRecord *c = chrGo(chr);
    u32 r = (u32)randomGetNext() & 0xFF;
    size_t k = 0;
    (void)ud;
    if (!c) return;
    while (k < 5 && r > (u32)fid[k].lim) k++;
    check_if_able_to_then_perform_animation(c, fid[k].anim, 0, fid[k].end, ANIM_FLAG_IDLE | ANIM_FLAG_SFX, 16);
}

/* ---- combat ---------------------------------------------------------------------------------------------------- */
struct fireArg { ChrRecord *c; s32 type, id; int kneel, res; };
static void doFire(void *a)
{
    struct fireArg *f = (struct fireArg *)a;
    f->res = (f->kneel ? actor_kneel_aim_at_actor(f->c, f->type, f->id) : actor_aim_at_actor(f->c, f->type, f->id)) != 0;
}

/* AI_TRYFireOrAimAtTarget / ...Kneel: aims and fires (TARGET_AIM_ONLY: only aims); false: unarmed. TARGET_BOND is the
 * current player, so the call runs as the wanted one. */
static int hFireAt(void *ud, int chr, int kind, int target, int kneel, int aim_only)
{
    ChrRecord *c = chrGo(chr);
    struct fireArg a;
    int player = -1;
    (void)ud;
    if (!c) return 0;
    a.c = c; a.kneel = kneel; a.res = 0; a.id = 0;
    a.type = aim_only ? TARGET_AIM_ONLY : 0;
    if (kind == MG_TGT_CHR) {
        ChrRecord *t = chrLive(target);
        if (!t) return 0;
        a.type |= TARGET_CHR; a.id = t->chrnum;
    } else if (kind == MG_TGT_PAD) {
        a.type |= TARGET_PAD; a.id = target;
    } else if (kind == MG_TGT_BODY) {
        /* bond_body(p) is player p's own body: TARGET_BOND with that player current aims at its chr */
        if (target < 0 || target >= getPlayerCount()) return 0;
        a.type |= TARGET_BOND;
        player = target;
    } else {
        a.type |= TARGET_BOND;
        player = target;
    }
    if (a.type & TARGET_BOND) missionsHostWithPlayer(playerFor(c, player), doFire, &a);
    else doFire(&a);
    return a.res;
}

struct moveArg { ChrRecord *c; int move, res; };
static void doMove(void *a)
{
    struct moveArg *m = (struct moveArg *)a;
    switch (m->move) {
    case MG_MOVE_ROLL_FIRE: m->res = actor_rolls_fires_crouched(m->c); break;
    case MG_MOVE_WALK_FIRE: m->res = actor_walks_and_fires(m->c); break;
    case MG_MOVE_RUN_FIRE:  m->res = actor_runs_and_fires(m->c); break;
    case MG_MOVE_SIDESTEP:  m->res = actor_steps_sideways(m->c); break;
    case MG_MOVE_SIDE_HOP:  m->res = actor_hops_sideways(m->c); break;
    case MG_MOVE_SIDE_RUN:  m->res = actor_jogs_sideways(m->c); break;
    default: m->res = 0;
    }
    m->res = m->res != 0;
}

/* AI_TRYFiringRoll / Walk / Run, AI_TRYSidestepping / SideHopping / SideRunning: they test the player (the current one). */
static int hCombatMove(void *ud, int chr, int move, int player)
{
    ChrRecord *c = chrGo(chr);
    struct moveArg a;
    (void)ud;
    if (!c) return 0;
    a.c = c; a.move = move; a.res = 0;
    missionsHostWithPlayer(playerFor(c, player), doMove, &a);
    return a.res;
}

static void doGrenade(void *a) { struct runArg *r = (struct runArg *)a; r->res = actor_draws_throws_grenade_at_player_if_possible(r->c) != 0; }

/* AI_TRYThrowingGrenade (it rolls the guard's own grenade probability first) */
static int hThrowGrenade(void *ud, int chr, int player)
{
    ChrRecord *c = chrGo(chr);
    struct runArg a;
    (void)ud;
    if (!c) return 0;
    a.c = c; a.speed = SPEED_WALK; a.res = 0;
    missionsHostWithPlayer(playerFor(c, player), doGrenade, &a);
    return a.res;
}

/* drop_grenade: the game's own drop path (AI_TRYDroppingItem: chrDropItem) with the hand grenade model the throw uses
 * (0xC4, chraction.c): the new grenade is armed (timer CHRLV_DEFAULT_TIMER) and falls at the guard's feet. */
static int hDropGrenade(void *ud, int chr)
{
    ChrRecord *c = chrGo(chr);
    (void)ud;
    return c && chrDropItem(c, 0xC4, ITEM_GRENADE) != 0;
}

/* the setup's 0..255 byte (chraction.c compares it with a random byte) as a percentage */
static int hGrenadeChance(void *ud, int chr)
{
    ChrRecord *c = chrAny(chr);
    (void)ud;
    return c ? ((int)c->grenadeprob * 100 + 127) / 255 : 0;
}

/* AI_HitChrWithItem */
static void hHit(void *ud, int chr, int part, int item)
{
    ChrRecord *c = chrAny(chr);
    coord3d v = { 0 };
    (void)ud;
    if (c) handles_shot_actors(c, part, &v, item, FALSE);
}

static int hIsIdle(void *ud, int chr)
{
    ChrRecord *c = chrLive(chr);
    Track *t = trkOf(c);
    (void)ud;
    if (!c) return 0;
    if (t && t->animActive && t->animInterruptible && c->actiontype == ACT_ANIM) return 1;
    return chrHasStoppedOrPatroling(c) != 0;
}

static int hIsBusy(void *ud, int chr)
{
    ChrRecord *c = chrLive(chr);
    (void)ud;
    if (!c) return 0;
    if (hIsIdle(ud, chr)) return 0;
    switch (c->actiontype) {
    case ACT_ANIM: case ACT_ARGH: case ACT_PREARGH: case ACT_ATTACK: case ACT_ATTACKWALK: case ACT_ATTACKROLL:
    case ACT_SIDESTEP: case ACT_JUMPOUT: case ACT_RUNPOS: case ACT_GOPOS: case ACT_LOOKATTARGET: case ACT_SURPRISED:
    case ACT_STARTALARM: case ACT_THROWGRENADE: case ACT_TURNDIR:
        return 1;
    default:
        return 0;
    }
}

/* AI_TRYCloningChr (GAILIST_STANDARD_CLONE as the clone's list): the clone appears where the guard stands (chrSpawnAtChr
 * refuses a guard that has been on screen), numbered chr + 10000 when that is free, armed like it. The game also asks for
 * the CHRFLAG_CLONE ("clone_sender") flag; a script calling clone() has asked, so it is not required here. */
static int hClone(void *ud, int chr)
{
    ChrRecord *c = chrLive(chr), *clone;
    PropRecord *cp, *rp, *lp, *crp = NULL, *clp = NULL;
    WeaponObjRecord *sr = NULL, *sl = NULL, *cr = NULL, *cl = NULL;
    int num;
    (void)ud;
    if (!c) return -1;
    cp = chrSpawnAtChr(c, c->bodynum, -1, c->chrnum, (AIListRecord *)ailistFindById(GAILIST_STANDARD_CLONE), 0);
    if (!cp) return -1;
    clone = cp->chr;
    num = c->chrnum + 10000;
    if (!chrFindByLiteralId(num)) clone->chrnum = (s16)num;
    rp = chrGetEquippedWeaponProp(c, GUNRIGHT);
    if (rp) {
        sr = rp->weapon;
        crp = chrGiveWeapon(clone, sr->obj, sr->weaponnum, 0);
        if (crp) cr = crp->weapon;
    }
    lp = chrGetEquippedWeaponProp(c, GUNLEFT);
    if (lp) {
        sl = lp->weapon;
        clp = chrGiveWeapon(clone, sl->obj, sl->weaponnum, 0x10000000);
        if (clp) cl = clp->weapon;
    }
    if (sl && sr && cl && cr && sr == sl->dualweapon && sl == sr->dualweapon) propweaponSetDual(cl, cr);
    if (c->handle_positiondata_hat) hatCreateForChr(clone, c->handle_positiondata_hat->obj->obj, 0);
    return clone->chrnum;
}

/* AI_TRYTriggeringAlarmAtPad (chrTryStartAlarm) is the press itself; the walk to the switch's pad comes first, and the
 * tick presses when the guard arrives. False: the switch is destroyed or missing, no route, or the guard is down. */
static int hPressAlarm(void *ud, int chr, int pad)
{
    ChrRecord *c = chrGo(chr);
    Track *t = trkOf(c);
    PadRecord *p = padRec(pad);
    ObjectRecord *sw;
    f32 dx, dz;
    (void)ud;
    if (!c || !t || !p || !chrIsNotDeadOrShot(c)) return 0;
    sw = scan_position_data_table_for_normal_object_at_preset(pad);
    if (!sw || !objIsHealthy(sw)) return 0;
    dx = c->prop->pos.f[0] - p->pos.f[0];
    dz = c->prop->pos.f[2] - p->pos.f[2];
    if (dx * dx + dz * dz < ALARM_NEAR * ALARM_NEAR) return chrTryStartAlarm(c, pad) != 0;
    if (!chrGoToPad(c, pad, SPEED_RUN)) return 0;
    t->alarmPad = pad;
    return 1;
}

/* ---- perception ------------------------------------------------------------------------------------------------ */
/* what the last tick's sight check found (the engine's own check, rolled once per tick, not once per call) */
static int hSpotted(void *ud, int chr)
{
    Track *t = trkOf(chrAny(chr));
    (void)ud;
    return (t && !missionsHostSceneActive()) ? t->spotted : -1;   /* a cinema scene: guards are blind */
}

struct seeArg { ChrRecord *c; int res; s32 keepSeen; };
static void doCanSee(void *a)
{
    struct seeArg *s = (struct seeArg *)a;
    s32 keep = s->c->seen_bond_time;
    s->res = chrCanSeeBond(s->c) != 0;
    s->c->seen_bond_time = keep;   /* chrCanSeeBond stamps it ("has seen bond recently": it throttles chrGoToBond); a query must not */
}

/* chrCanSeeBond: a clear line (no cone, no range) to a player */
static int hLineToPlayer(void *ud, int chr)
{
    ChrRecord *c = chrLive(chr);
    (void)ud;
    if (!c || missionsHostSceneActive()) return -1;
    for (int p = 0; p < getPlayerCount() && p < 4; p++) {
        struct seeArg a;
        if (!g_playerPointers[p] || !g_playerPointers[p]->prop || g_playerPointers[p]->bonddead) continue;
        a.c = c; a.res = 0; a.keepSeen = 0;
        missionsHostWithPlayer(p, doCanSee, &a);
        if (a.res) return p;
    }
    return -1;
}

static int hOnScreen(void *ud, int chr)
{
    ChrRecord *c = chrAny(chr);
    (void)ud;
    return c && portPropGameplayOnScreen(c->prop) != 0;   /* AI_IFImOnScreen */
}

static int hWasSeen(void *ud, int chr)
{
    ChrRecord *c = chrAny(chr);
    (void)ud;
    return c && (c->chrflags & CHRFLAG_HAS_BEEN_ON_SCREEN) != 0;   /* AI_IFIveNotBeenSeen, inverted */
}

static void doAimed(void *a) { struct runArg *r = (struct runArg *)a; if (sub_GAME_7F0333F8(r->c)) r->res = 1; }

/* AI_IFImTargetedByBond, for each player */
static int hIsAimedAt(void *ud, int chr)
{
    ChrRecord *c = chrLive(chr);
    struct runArg a;
    (void)ud;
    if (!c) return 0;
    a.c = c; a.res = 0; a.speed = SPEED_WALK;
    for (int p = 0; p < getPlayerCount() && p < 4 && !a.res; p++) missionsHostWithPlayer(p, doAimed, &a);
    return a.res;
}

static int withinSeconds(s32 stamp, double seconds)
{
    return stamp > 0 && (double)(g_GlobalTimer - stamp) < seconds * (double)CHRLV_FRAMERATE_F;
}

/* "saw a player, or was hit": lastseetarget60 is stamped by every sight check that passes (chrCheckTargetInSight) */
static int hAlerted(void *ud, int chr, double seconds)
{
    ChrRecord *c = chrAny(chr);
    Track *t = trkOf(c);
    (void)ud;
    return c && (withinSeconds(c->lastseetarget60, seconds) || (t && withinSeconds(t->lastHit60, seconds)));
}

static int hHeardShot(void *ud, int chr, double seconds)
{
    ChrRecord *c = chrAny(chr);
    (void)ud;
    return c && withinSeconds(c->lastheartarget60, seconds);   /* AI_IFIHeardBondRecently */
}

static int hNearMisses(void *ud, int chr)
{
    Track *t = trkOf(chrAny(chr));
    (void)ud;
    return t ? t->nearTotal : 0;
}

static int hWasShotAt(void *ud, int chr)
{
    Track *t = trkOf(chrAny(chr));
    (void)ud;
    return t && t->nearNow;
}

static int hWitnessed(void *ud, int chr, int *other)
{
    Track *t = trkOf(chrAny(chr));
    (void)ud;
    if (!t) return MG_SAW_NONE;
    *other = t->sawOther;
    return t->saw;
}

/* chrGetAngleToBond (0..2pi, from the guard's heading to the player) as -180..180 degrees */
struct angArg { ChrRecord *c; double deg; };
static void doAngle(void *a)
{
    struct angArg *g = (struct angArg *)a;
    f32 r = chrGetAngleToBond(g->c);
    if (r > 3.14159265f) r -= 6.2831853f;
    g->deg = (double)r * 180.0 / 3.14159265358979;
}

static double hAngle(void *ud, int chr, int player)
{
    ChrRecord *c = chrLive(chr);
    struct angArg a;
    (void)ud;
    if (!c) return 0.0;
    a.c = c; a.deg = 0.0;
    missionsHostWithPlayer(playerFor(c, player), doAngle, &a);
    return a.deg;
}

/* AI_IFChrWasDamagedSinceLastCheck: tested and cleared */
static int hWasDamaged(void *ud, int chr)
{
    ChrRecord *c = chrAny(chr);
    (void)ud;
    if (!c || !(c->chrflags & CHRFLAG_WAS_DAMAGED)) return 0;
    c->chrflags &= ~CHRFLAG_WAS_DAMAGED;
    return 1;
}

static int hCivilians(void *ud) { (void)ud; return (int)get_civilian_casualties(); }

/* ---- the table, the tick, the reset -------------------------------------------------------------------------- */
const MissionHostGuard *missionsHostGuard(void)
{
    memset(&s_host, 0, sizeof s_host);
    s_host.exists = hExists; s_host.is_dying = hIsDying; s_host.chr_remove = hRemove; s_host.fade_out = hFadeOut;
    s_host.teleport = hTeleport; s_host.set_hidden = hSetHidden; s_host.spawn = hSpawn; s_host.set_ai = hSetAi;
    s_host.setup_ai = hSetupAi; s_host.all_guards = hAllGuards; s_host.pad_by_id = hPadById;
    s_host.set_stats = hSetStats; s_host.set_flag = hSetFlag; s_host.get_flag = hGetFlag; s_host.give_weapon = hGiveWeapon;
    s_host.give_hat = hGiveHat; s_host.drop_items = hDropItems; s_host.remove_held_items = hRemoveHeld;
    s_host.preset_pad = hPresetPad;
    s_host.patrol = hPatrol; s_host.is_patrolling = hIsPatrolling; s_host.is_moving = hIsMoving; s_host.run_to = hRunTo;
    s_host.run_to_player = hRunToPlayer; s_host.face_player = hFacePlayer; s_host.face_pad = hFacePad; s_host.halt = hHalt;
    s_host.flee_pad = hFleePad; s_host.cover_pad = hCoverPad;
    s_host.look_around = hLookAround; s_host.point_at_player = hPointAtPlayer; s_host.raise_arms = hRaiseArms;
    s_host.kneel = hKneel; s_host.idle_anim = hIdleAnim; s_host.anim = hAnim; s_host.anim_done = hAnimDone;
    s_host.stop_anim = hStopAnim;
    s_host.fire_at = hFireAt; s_host.combat_move = hCombatMove; s_host.throw_grenade = hThrowGrenade;
    s_host.grenade_chance = hGrenadeChance; s_host.hit = hHit; s_host.is_busy = hIsBusy; s_host.is_idle = hIsIdle;
    s_host.clone = hClone; s_host.press_alarm = hPressAlarm;
    s_host.spotted_player = hSpotted; s_host.line_to_player = hLineToPlayer; s_host.on_screen = hOnScreen;
    s_host.was_seen = hWasSeen; s_host.is_aimed_at = hIsAimedAt; s_host.alerted_within = hAlerted;
    s_host.heard_shot_within = hHeardShot; s_host.near_misses = hNearMisses; s_host.was_shot_at = hWasShotAt;
    s_host.witnessed = hWitnessed; s_host.angle_to_player = hAngle; s_host.was_damaged = hWasDamaged;
    s_host.civilians_killed = hCivilians;
    s_host.held_weapon = hHeldWeapon; s_host.spot_near_player = hSpotNearPlayer; s_host.drop_grenade = hDropGrenade;
    s_host.drop_object = hDropObject; s_host.give_object = hGiveObject; s_host.set_flag_raw = hSetFlagRaw;
    return &s_host;
}

static void doSightCheck(void *a)
{
    struct seeArg *s = (struct seeArg *)a;
    if (chrCheckTargetInSight(s->c)) s->res = 1;
}

extern struct player *g_playerPointers[4];

/* guard_died {player}: the game keeps no record of who killed a guard (ChrRecord.lastshooter is never written), so the
 * player whose weapon fired most recently (PEV_SHOT, a beam in its struct player or its chr) within DIED_WINDOW ticks of
 * the death is the one named; none when nobody fired. */
#define DIED_WINDOW 180
static int  s_shotSub = -1;
static s32  s_lastFire[4];

static int onFire(int type, int phase, void *data, void *ctx)
{
    const PortEvShot *e = (const PortEvShot *)data;
    const char *b = e ? (const char *)e->beam : NULL;
    (void)type; (void)phase; (void)ctx;
    for (int p = 0; b && p < getPlayerCount() && p < 4; p++) {
        const struct player *pl = g_playerPointers[p];
        const ChrRecord *c;
        if (!pl) continue;
        if ((b >= (const char *)pl && b < (const char *)pl + sizeof *pl) ||
            (pl->prop && (c = pl->prop->chr) && b >= (const char *)c->beams && b < (const char *)(c->beams + 2))) {
            s_lastFire[p] = g_GlobalTimer;
            break;
        }
    }
    return PEV_CONTINUE;
}

static int killerPlayer(void)
{
    int best = -1;
    for (int p = 0; p < 4; p++)
        if (s_lastFire[p] > 0 && g_GlobalTimer - s_lastFire[p] <= DIED_WINDOW && (best < 0 || s_lastFire[p] > s_lastFire[best])) best = p;
    return best;
}

extern s32 chrlvMaybeSameRoom(ChrRecord *self, coord3d *pos, StandTile *stan);   /* chraction.c, not in its header */

/* An event of a guard: chr, prop (omitted for a guard with no setup record), then the optional fields. */
static void raiseGuard(MissionRuntime *rt, const char *name, const ChrRecord *c, int player, const char *what, int other)
{
    MissionEvField f[5];
    int n = 0, prop = setupPropOfChr(c->chrnum);
    f[n].key = "chr"; f[n].str = NULL; f[n++].num = c->chrnum;
    if (prop >= 0) { f[n].key = "prop"; f[n].str = NULL; f[n++].num = prop; }
    if (player >= 0) { f[n].key = "player"; f[n].str = NULL; f[n++].num = player; }
    if (what) { f[n].key = "what"; f[n].str = what; f[n++].num = 0; }
    if (what && other >= 0) { f[n].key = "other"; f[n].str = NULL; f[n++].num = other; }
    missionRtRaise(rt, name, f, n);
}

/* The living player nearest to pos; -1 when none. */
static int nearestPlayerTo(const f32 *pos)
{
    int best = -1;
    f32 bd = 0.0f;
    for (int p = 0; p < getPlayerCount() && p < 4; p++) {
        const struct player *pl = g_playerPointers[p];
        f32 dx, dy, dz, d;
        if (!pl || !pl->prop || pl->bonddead) continue;
        dx = pl->prop->pos.f[0] - pos[0]; dy = pl->prop->pos.f[1] - pos[1]; dz = pl->prop->pos.f[2] - pos[2];
        d = dx * dx + dy * dy + dz * dz;
        if (best < 0 || d < bd) { best = p; bd = d; }
    }
    return best;
}

/* Per tick, after the scripts ran (missions_host.c). One pass over the guards finds what each did and saw; the witnesses
 * come after, since they need every guard's new action. */
void missionsHostGuardTick(void)
{
    MissionRuntime *rt = missionsRuntime();
    struct { ChrRecord *c; int what; } acts[64];
    int nacts = 0;
    const int scene = missionsHostSceneActive();   /* a cinema scene: guards are blind and deaf */
    if (!rt || !g_ChrSlots) return;
    if (s_shotSub < 0) s_shotSub = portEventSubscribe(PEV_SHOT, PEV_AFTER, 2100, onFire, NULL);
    for (int i = 0; i < g_NumChrSlots; i++) {
        ChrRecord *c = &g_ChrSlots[i];
        Track *t;
        s8 dclose;
        int act, seen = -1;
        if (!c->model || !c->prop || c->prop->type != PROP_TYPE_CHR) continue;
        t = trkOf(c);
        if (!t) continue;
        act = c->actiontype;
        t->saw = MG_SAW_NONE;
        t->sawOther = -1;
        t->nearNow = 0;

        /* "guard_damaged" {chr, prop}: any guard, spawned ones too */
        if (c->damage > t->dmg + 1e-3f) {
            raiseGuard(rt, "guard_damaged", c, -1, NULL, -1);
            t->lastHit60 = g_GlobalTimer;
        } else if (c->numarghs != t->numarghs) {
            t->lastHit60 = g_GlobalTimer;      /* hit, and shrugged it off (invincible) */
        }
        t->dmg = c->damage;
        t->numarghs = c->numarghs;

        /* a hit (the flinch / argh action starts) or a death: the guards that see it come after this pass */
        if ((act == ACT_ARGH || act == ACT_PREARGH) && t->prevAct != ACT_ARGH && t->prevAct != ACT_PREARGH && nacts < 64) {
            acts[nacts].c = c; acts[nacts++].what = MG_SAW_HIT;
        } else if ((act == ACT_DIE || act == ACT_DEAD) && t->prevAct != ACT_DIE && t->prevAct != ACT_DEAD && nacts < 64) {
            acts[nacts].c = c; acts[nacts++].what = MG_SAW_DEATH;
        }
        /* "guard_died" {chr, prop, player}: once, on the edge into dying (every guard, spawned ones too) */
        if ((act == ACT_DIE || act == ACT_DEAD) && t->prevAct != ACT_DIE && t->prevAct != ACT_DEAD)
            raiseGuard(rt, "guard_died", c, killerPlayer(), NULL, -1);
        t->prevAct = act;

        /* "guard_near_miss" {chr, prop, player}: numclosearghs counts the shots that passed it (chr.c); the flag lives one chr tick */
        dclose = (s8)(c->numclosearghs - t->closeargs);
        t->closeargs = c->numclosearghs;
        if (dclose > 0 || (c->chrflags & CHRFLAG_NEAR_MISS)) {
            if (dclose > 0) t->nearTotal += dclose;
            t->nearNow = 1;
            raiseGuard(rt, "guard_near_miss", c, nearestPlayerTo(c->prop->pos.f), NULL, -1);
        }

        /* "guard_heard" {chr, prop, player}: lastheartarget60 is stamped when it hears a player's shot, with where from */
        if (!scene && c->lastheartarget60 > 0 && c->lastheartarget60 != t->prevHeard)
            raiseGuard(rt, "guard_heard", c, nearestPlayerTo(c->lastknowntargetpos.f), NULL, -1);
        t->prevHeard = c->lastheartarget60;

        /* "guard_spotted" {chr, prop, player} on the edge to seeing: the engine's sight check (chrCheckTargetInSight, its
         * reaction roll and its 10 s of memory included), once per tick. When the guard's own AI already passed it this
         * tick (lastseetarget60 is stamped) that result is taken and nothing is rolled. */
        if (!scene && !chrIsDead(c) && !(c->chrflags & CHRFLAG_HIDDEN) && !(c->chrflags & CHRFLAG_INIT)) {
            if (c->lastseetarget60 > 0 && c->lastseetarget60 == g_GlobalTimer) seen = nearestPlayerTo(c->prop->pos.f);
            else
                for (int p = 0; p < getPlayerCount() && p < 4 && seen < 0; p++) {
                    struct seeArg a;
                    if (!g_playerPointers[p] || !g_playerPointers[p]->prop || g_playerPointers[p]->bonddead) continue;
                    a.c = c; a.res = 0; a.keepSeen = 0;
                    missionsHostWithPlayer(p, doSightCheck, &a);
                    if (a.res) seen = p;
                }
        }
        if (seen >= 0 && t->spotted < 0) raiseGuard(rt, "guard_spotted", c, seen, NULL, -1);
        t->spotted = seen;

        /* "anim_end" {chr}: its anim() finished (or was cut short) */
        if (t->animActive && animFinished(c)) {
            MissionEvField f[1];
            t->animActive = 0;
            f[0].key = "chr"; f[0].str = NULL; f[0].num = c->chrnum;
            missionRtRaise(rt, "anim_end", f, 1);
        }

        /* press_alarm: on its way to the switch; pressed on arrival, dropped when something stopped it */
        if (t->alarmPad >= 0) {
            PadRecord *p = padRec(t->alarmPad);
            if (!p || chrIsDead(c)) t->alarmPad = -1;
            else {
                f32 dx = c->prop->pos.f[0] - p->pos.f[0], dz = c->prop->pos.f[2] - p->pos.f[2];
                if (dx * dx + dz * dz < ALARM_NEAR * ALARM_NEAR) { chrTryStartAlarm(c, t->alarmPad); t->alarmPad = -1; }
                else if (c->actiontype != ACT_GOPOS) t->alarmPad = -1;
            }
        }
    }

    /* "guard_witnessed" {chr, prop, what, other}: the rule of the game's chrlvIterateGuardSeeShotDie (chraction.c): a guard
     * within 2000 units and in the room of the one hit / killed, at most 4 witnesses of each. */
    for (int k = 0; k < nacts; k++) {
        ChrRecord *x = acts[k].c;
        int alerts = 0;
        for (int i = 0; i < g_NumChrSlots && alerts < 4; i++) {
            ChrRecord *w = &g_ChrSlots[i];
            Track *t;
            f32 dx, dy, dz;
            if (w == x || !w->model || !w->prop || w->prop->type != PROP_TYPE_CHR || chrIsDead(w)) continue;
            dx = w->prop->pos.f[0] - x->prop->pos.f[0];
            dy = w->prop->pos.f[1] - x->prop->pos.f[1];
            dz = w->prop->pos.f[2] - x->prop->pos.f[2];
            if (dx * dx + dy * dy + dz * dz >= 4000000.0f) continue;
            alerts++;
            if (!chrlvMaybeSameRoom(w, &x->prop->pos, x->prop->stan)) continue;
            t = trkOf(w);
            if (!t) continue;
            if (!(acts[k].what == MG_SAW_HIT && t->saw == MG_SAW_DEATH)) { t->saw = acts[k].what; t->sawOther = x->chrnum; }
            raiseGuard(rt, "guard_witnessed", w, -1, acts[k].what == MG_SAW_DEATH ? "death" : "hit", x->chrnum);
        }
    }
}

/* A level starts or the runtime stops: drop this domain's state (missionsHostDomainsReset). */
void missionsHostGuardReset(void)
{
    if (s_shotSub >= 0) { portEventUnsubscribe(s_shotSub); s_shotSub = -1; }
    memset(s_lastFire, 0, sizeof s_lastFire);
    free(s_trk);
    s_trk = NULL;
    s_ntrk = 0;
    memset(s_warned, 0, sizeof s_warned);
}
