/* The world domain of the campaign API (#165): host side, the game's own functions behind
 * port/mission/mission_host_world.h. Layout: port/mission/mission_lua_ext.h.
 *
 * Every verb here calls the function the game's own AI command interpreter calls for the same job (src/game/chrai.c:
 * the AI_* handlers named at each callback); nothing in src/game changes. A prop is a setup prop number, resolved with
 * missionsHostPropRec (the setup records of the running level, missions_host.c). */
#include "platform.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <math.h>

#include <ultra64.h>
#include "game/chr.h"
#include "game/chraction.h"
#include "game/chrai.h"
#include "game/bondview.h"
#include "game/bondinv.h"
#include "game/lv.h"
#include "game/player.h"
#include "game/propobj.h"
#include "game/gun.h"
#include "game/loadobjectmodel.h"
#include "game/matrixmath.h"
#include "game/objective_status.h"
#include "game/textrelated.h"
#include "game/mp_music.h"
#include "game/bgfog.h"
#include "game/stan.h"
#include "game/model.h"
#include "game/initanitable.h"
#include "boss.h"
#include "fr.h"
#include "snd.h"
#include "system.h"
#include "mission.h"
#include "mission_apply.h"
#include "mission_lua.h"
#include "mission_host_world.h"
#include "missions.h"
#include "missions_priv.h"

MissionRuntime *missionsRuntime(void);   /* missions_host.c */
extern struct player *g_playerPointers[4];
extern s32 g_GlobalTimer;
extern s32 g_ClockTimer;
extern s32 stop_time_flag;
extern s32 cameraFrameCounter2;
extern PathRecord *pathFindById(s32 ID);                          /* chrai.c, not in its header */
extern void audioPlayFromProp(s32 slot, s16 soundIndex);          /* chrai.c */
extern void audioPlayFromProp2(s32 slot);
extern void sub_GAME_7F0349BC(s32 slot);
extern void bondviewCalcUpdatePlayerCollision(struct coord3d *offset, s32 allow_scoot);   /* bondview2.c */
extern void bondviewUpdatePlayerCollisionPositionFields(void);
extern void bondviewKillCurrentPlayer(void);                      /* bondview2.c: the player dies (the debug menu's "bond die") */
extern s32 g_isBondKIA;                                           /* front.c: the debrief's "killed in action" */
#if defined(VERSION_JP) || defined(VERSION_EU)
extern s32 propExplode(PropRecord *prop, s32 explosionType);       /* propobj.c, not in its header */
#else
extern void propExplode(PropRecord *prop, s32 explosionType);
#endif

#define TICKRATE 60.0f          /* CHRAI_TICKRATE_F: the AI's seconds are 60 ticks */
#define KEY_LOCK_BIT 0x80000000u /* a key bit no key has: the door stays shut for the player (as missions_host.c) */

static MissionHostWorld s_host;

/* ---- setup props ------------------------------------------------------------------------------------------------- */

static ObjectRecord *wObj(int prop)   /* an object record (a door too); NULL for a guard, a tag, an objective ... */
{
    PropDefHeaderRecord *h = (PropDefHeaderRecord *)missionsHostPropRec(prop);
    if (!h || h->type == PROPDEF_GUARD) return NULL;
    if (missionPropdefSize(h->type) < 144) return NULL;
    return (ObjectRecord *)h;
}

static DoorRecord *wDoor(int prop)
{
    PropDefHeaderRecord *h = (PropDefHeaderRecord *)missionsHostPropRec(prop);
    return (h && h->type == PROPDEF_DOOR) ? (DoorRecord *)h : NULL;
}

static ChrRecord *wChr(int prop)
{
    PropDefHeaderRecord *h = (PropDefHeaderRecord *)missionsHostPropRec(prop);
    if (!h || h->type != PROPDEF_GUARD || !((GuardRecord *)h)->Data) return NULL;
    return chrFindByLiteralId(((GuardRecord *)h)->chrnum);
}

static PadRecord *wPad(int pad)
{
    if (pad < 0 || !g_CurrentSetup.pads) return NULL;
    if (isNotBoundPad(pad)) return &g_CurrentSetup.pads[pad];
    return g_CurrentSetup.boundpads ? (PadRecord *)&g_CurrentSetup.boundpads[getBoundPadNum(pad)] : NULL;
}

static int wPlayers(void) { int n = getPlayerCount(); return n < 4 ? n : 4; }

/* The object tag naming `o` (the setup's tag records, objective_status.c), -1: none. */
static int tagOf(const ObjectRecord *o)
{
    const TagObjectRecord *t = (const TagObjectRecord *)(void *)ptr_last_tag_entry_type16;
    for (; t; t = t->NextTag)
        if (t->TaggedObject == o) return t->ID;
    return -1;
}

/* The player nearest to a point, -1 when none is placed. */
static int nearestPlayer(const float p[3])
{
    int best = -1;
    float bd = 3.0e38f;
    for (int i = 0; i < wPlayers(); i++) {
        const struct player *pl = g_playerPointers[i];
        float dx, dy, dz, d;
        if (!pl || !pl->prop || pl->bonddead) continue;
        dx = pl->prop->pos.f[0] - p[0]; dy = pl->prop->pos.f[1] - p[1]; dz = pl->prop->pos.f[2] - p[2];
        d = dx * dx + dy * dy + dz * dz;
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

struct itemArg { int item; int result; };
static void doHeld(void *a) { ((struct itemArg *)a)->result = (int)getCurrentPlayerWeaponId(GUNRIGHT); }
static void doHolds(void *a)
{
    struct itemArg *x = a;
    x->result = x->item == (int)getCurrentPlayerWeaponId(GUNRIGHT) || x->item == (int)getCurrentPlayerWeaponId(GUNLEFT);
}
static void doEquip(void *a)
{
    /* AI_BondEquipItem: the item in the right hand, the left hand empty */
    currentPlayerEquipWeaponWrapper(GUNRIGHT, ((struct itemArg *)a)->item);
    currentPlayerEquipWeaponWrapper(GUNLEFT, 0);
}
static int heldItemOf(int player)
{
    struct itemArg a = { 0, 0 };
    if (player < 0 || player >= wPlayers()) return 0;
    missionsHostWithPlayer(player, doHeld, &a);
    return a.result;
}

/* ---- rooms (chrai.c AI_IFChrInRoomWithPad, AI_IFBondInRoomWithPad, AI_IFObjectInRoomWithPad: the pad's stan tile's room
 * against the thing's own) ------------------------------------------------------------------------------------------ */

static StandTile *propStan(const PropRecord *p) { return p ? p->stan : NULL; }

static int roomOfPad(int pad, int *room)
{
    const PadRecord *p = wPad(pad);
    if (!p || !p->stan) return 0;
    *room = p->stan->room;
    return 1;
}

/* The stan tile a chr / setup prop / player / pad stands on, NULL when it is not placed. */
static StandTile *subjectStan(int kind, int index)
{
    switch (kind) {
    case MHW_ROOM_CHR: {
        const ChrRecord *c = chrFindByLiteralId(index);
        return c ? propStan(c->prop) : NULL;
    }
    case MHW_ROOM_PROP: {
        const ChrRecord *c = wChr(index);
        const ObjectRecord *o = wObj(index);
        return c ? propStan(c->prop) : o ? propStan(o->prop) : NULL;
    }
    case MHW_ROOM_PLAYER:
        return (index >= 0 && index < wPlayers() && g_playerPointers[index]) ? propStan(g_playerPointers[index]->prop) : NULL;
    case MHW_ROOM_PAD: {
        const PadRecord *p = wPad(index);
        return p ? p->stan : NULL;
    }
    }
    return NULL;
}

static int wInRoom(void *ud, int kind, int index, int pad)
{
    int room;
    StandTile *st;
    (void)ud;
    if (!roomOfPad(pad, &room)) return 0;
    st = subjectStan(kind, index);
    return st && st->room == room;
}

/* two things in one room (their stan tiles' rooms equal) */
static int wSameRoom(void *ud, int ka, int ia, int kb, int ib)
{
    const StandTile *a = subjectStan(ka, ia), *b = subjectStan(kb, ib);
    (void)ud;
    return a && b && a->room == b->room;
}

static int wPlayerInRoom(void *ud, int pad)
{
    (void)ud;
    for (int i = 0; i < wPlayers(); i++)
        if (wInRoom(NULL, MHW_ROOM_PLAYER, i, pad)) return i;
    return -1;
}

/* A thrown item at rest: a weapon prop (PROP_TYPE_WEAPON) of that item that is no setup record, held by no chr and
 * without a live projectile (in flight otherwise). Stuck: it sits on an object (AI_IFItemIsAttachedToObject walks the
 * object's children the same way); the tag of that object is returned. */
static int wItemLanded(void *ud, int item, int *tag)
{
    PropRecord *prop;
    (void)ud;
    *tag = -1;
    for (prop = chrpropGetActiveTail(); prop; prop = prop->prev) {
        const WeaponObjRecord *wep;
        int setup = 0;
        if (prop->type != PROP_TYPE_WEAPON || !(wep = prop->weapon) || wep->weaponnum != item) continue;
        for (int i = 0; i < missionsHostPropCount() && !setup; i++) setup = missionsHostPropRec(i) == (void *)wep;
        if (setup || (wep->runtime_bitflags & RUNTIMEBITFLAG_HASPROJECTILE)) continue;
        if (!prop->parent) return MHW_LANDED_LOOSE;
        if (prop->parent->type == PROP_TYPE_OBJ || prop->parent->type == PROP_TYPE_DOOR) {
            *tag = tagOf(prop->parent->obj);
            return MHW_LANDED_STUCK;
        }
    }
    return MHW_LANDED_NONE;
}

/* AI_IFObjectInRoomWithPad for a thrown / planted weapon: a weapon prop of that item, no setup record, held by no chr and
 * with no live projectile (at rest), whose stan tile's room is the room of the pad. */
static int wItemInRoom(void *ud, int item, int pad)
{
    PropRecord *prop;
    int room;
    (void)ud;
    if (!roomOfPad(pad, &room)) return 0;
    for (prop = chrpropGetActiveTail(); prop; prop = prop->prev) {
        const WeaponObjRecord *wep;
        int setup = 0;
        if (prop->type != PROP_TYPE_WEAPON || !(wep = prop->weapon) || wep->weaponnum != item) continue;
        for (int i = 0; i < missionsHostPropCount() && !setup; i++) setup = missionsHostPropRec(i) == (void *)wep;
        if (setup || (wep->runtime_bitflags & RUNTIMEBITFLAG_HASPROJECTILE)) continue;
        if (prop->parent && prop->parent->type == PROP_TYPE_CHR) continue;
        if (prop->stan && prop->stan->room == room) return 1;
    }
    return 0;
}

/* ---- objects ------------------------------------------------------------------------------------------------------ */

static int wObjDestroyed(void *ud, int prop)
{
    const ObjectRecord *o = wObj(prop);
    (void)ud;
    if (!o) return 0;
    /* AI_IFObjectNotDestroyed: the object exists and is healthy; one that is gone counts as destroyed */
    return !o->prop || objGetDestroyedLevel((ObjectRecord *)o) != 0;
}

static void destroyObj(ObjectRecord *obj)
{
    /* AI_DestroyObject */
    if (!objGetDestroyedLevel(obj)) {
        f32 damage = ((obj->damage - obj->maxdamage) + 1) / 250.0f;
        objApplyDamage(obj, damage, &obj->runtime_pos, 0x1D, -1);
    }
}

static int wObjDestroy(void *ud, int prop)
{
    ObjectRecord *o = wObj(prop);
    (void)ud;
    if (!o || !o->prop) return 0;
    destroyObj(o);
    return 1;
}

/* An explosion at the object (propobj.c propExplode, the call a timed mine's fuse makes), then the object itself is
 * destroyed when it can be (objIsMortal), as the explosion would. */
static int wObjExplode(void *ud, int prop)
{
    ObjectRecord *o = wObj(prop);
    (void)ud;
    if (!o || !o->prop) return 0;
    propExplode(o->prop, EXPLOSION_DEF_STANDARD);
    if (objIsMortal(o)) destroyObj(o);
    return 1;
}

static int wObjSetInvincible(void *ud, int prop, int on)
{
    ObjectRecord *o = wObj(prop);
    (void)ud;
    if (!o) return 0;
    if (on) o->flags |= PROPFLAG_INVINCIBLE; else o->flags &= ~PROPFLAG_INVINCIBLE;   /* propobj.c objIsMortal / objApplyDamage test it */
    return 1;
}

static int wObjSetCollectable(void *ud, int prop, int on)
{
    ObjectRecord *o = wObj(prop);
    (void)ud;
    if (!o) return 0;
    if (on) o->flags &= ~PROPFLAG_UNCOLLECTABLE; else o->flags |= PROPFLAG_UNCOLLECTABLE;
    return 1;
}

/* AI_MoveObject: the object takes the pad's orientation and position. */
static int wObjMoveTo(void *ud, int prop, int padnum)
{
    ObjectRecord *obj = wObj(prop);
    PadRecord *pad = wPad(padnum);
    Mtxf matrix;
    (void)ud;
    if (!obj || !obj->prop || !pad) return 0;
    matrix_4x4_set_basis_and_position_target(&matrix, 0, 0, 0, -pad->look.x, -pad->look.y, -pad->look.z, pad->up.x, pad->up.y, pad->up.z);
    if (obj->model) matrix_scalar_multiply(obj->model->scale, (f32 *)&matrix);
    sub_GAME_7F04088C(obj, &pad->pos, &matrix, pad->stan, &pad->pos);
    setupUpdateObjectRoomPosition(obj);
    return 1;
}

static void doActivate(void *a)
{
    /* AI_ActivateObject */
    ObjectRecord *obj = a;
    if (obj->prop->type == PROP_TYPE_DOOR) doorActivateWrapper(obj->prop);
    else if (obj->prop->type == PROP_TYPE_OBJ || obj->prop->type == PROP_TYPE_WEAPON) propobjInteract(obj->prop);
}

static int wObjActivate(void *ud, int prop)
{
    ObjectRecord *o = wObj(prop);
    (void)ud;
    if (!o || !o->prop) return 0;
    missionsHostWithPlayer(0, doActivate, o);   /* the interaction reads the current player: player 1 */
    return 1;
}

/* AI_TvChangeScreenBank */
static int wObjShowScreen(void *ud, int prop, int bank, int screen)
{
    ObjectRecord *o = wObj(prop);
    (void)ud;
    if (!o || !o->prop) return 0;
    if (o->type == PROPDEF_MONITOR) {
        monitorSetImageByNum((MonitorRecord *)&((MonitorObjRecord *)o)->Monitor.cmdlist, bank);   /* as AI_TvChangeScreenBank passes it */
        return 1;
    }
    if (o->type == PROPDEF_MULTI_MONITOR && screen >= 0 && screen < 4) {
        monitorSetImageByNum((MonitorRecord *)&((MultiMonitorObjRecord *)o)->Monitor[screen].cmdlist, bank);
        return 1;
    }
    return 0;
}

struct heldArg { PropRecord *prop; int result; };
static void doHeldBy(void *a) { struct heldArg *h = a; h->result = bondinvHasPropInInv(h->prop) != 0; }

/* AI_IFBondCollectedObject: the object's prop is in the (current) player's inventory. */
static int wObjHeldBy(void *ud, int prop, int player)
{
    ObjectRecord *o = wObj(prop);
    struct heldArg a;
    (void)ud;
    if (!o || !o->prop) return 0;
    a.prop = o->prop;
    a.result = 0;
    for (int p = 0; p < wPlayers() && !a.result; p++)
        if (player < 0 || player == p) missionsHostWithPlayer(p, doHeldBy, &a);
    return a.result;
}

/* AI_AircraftRotorSpeed: ROTOR_SPEED units (1/10 degree per tick: speed * TAU / 3600), ramp in ticks. */
static int wObjSetRotor(void *ud, int prop, double speed, double ramp)
{
    ObjectRecord *o = wObj(prop);
    (void)ud;
    if (!o || o->type != PROPDEF_AIRCRAFT) return 0;
    ((AircraftRecord *)o)->rotaryspeedaim = (f32)(speed * M_TAU_F / 3600.0);
    ((AircraftRecord *)o)->rotaryspeedtime = (f32)(ramp * TICKRATE);
    return 1;
}

/* AI_VehicleStartPath + AI_VehicleSpeed: a vehicle (or an aircraft) follows the path from its first step; speed in the
 * command's TOP_SPEED units (speed * 100 / 15360), ramp in ticks. */
static int wObjDrive(void *ud, int prop, int path, double speed, double ramp)
{
    ObjectRecord *o = wObj(prop);
    PathRecord *p = pathFindById(path);
    (void)ud;
    if (!o || !p) return 0;
    if (o->type == PROPDEF_VEHICHLE) {
        VehichleRecord *v = (VehichleRecord *)o;
        v->path = p;
        v->nextstep = 0;
        v->speedaim = (f32)(speed * 100.0 / 15360.0);
        v->speedtime60 = (f32)(ramp * TICKRATE);
        return 1;
    }
    if (o->type == PROPDEF_AIRCRAFT) {
        AircraftRecord *a = (AircraftRecord *)o;
        a->path = p;
        a->nextstep = 0;
        a->speedaim = (f32)(speed * 100.0 / 15360.0);
        a->speedtime60 = (f32)(ramp * TICKRATE);
        return 1;
    }
    return 0;
}

/* set_active: the gun's / camera's "disabled" bit (PROPFLAG_IS_DRONE_GUN on an autogun, PROPFLAG_CCTV_DISABLED on a camera:
 * one bit, 0x10000000; the game sets it itself when the object is destroyed, propobj.c), as AI_SetObjectFlags /
 * AI_UnsetObjectFlags do. A destroyed one stays off. */
static int wObjSetActive(void *ud, int prop, int on)
{
    ObjectRecord *o = wObj(prop);
    (void)ud;
    if (!o || !o->prop || (o->type != PROPDEF_AUTOGUN && o->type != PROPDEF_CCTV)) return 0;
    if (on) {
        if (objGetDestroyedLevel(o)) return 0;
        o->flags &= ~PROPFLAG_IS_DRONE_GUN;
    } else {
        o->flags |= PROPFLAG_IS_DRONE_GUN;
    }
    return 1;
}

#define FLAG2_IMMUNE_GUNFIRE     0x00004000u   /* chrobjMaybeDetonateObjectIfFlags: gunfire damage is skipped, explosions still hurt */
#define FLAG2_IMMUNE_EXPLOSIONS  0x00200000u   /* explosion.c: the explosion's damage skips it */
#define FLAG2_HIDDEN_NO_SOUND    0x00080000u   /* propobj.c: never drawn, an aircraft / truck makes no engine sound */

/* set_damage: "explosions" = the game's "immune to gunfire" flags2 bit (only explosions hurt it); "none" adds the
 * invincible flag objIsMortal tests and the explosion immunity; "all" clears all three. */
static int wObjSetDamage(void *ud, int prop, int kind)
{
    ObjectRecord *o = wObj(prop);
    (void)ud;
    if (!o || o->type == PROPDEF_DOOR) return 0;
    o->flags &= ~PROPFLAG_INVINCIBLE;
    o->flags2 &= ~(FLAG2_IMMUNE_GUNFIRE | FLAG2_IMMUNE_EXPLOSIONS);
    if (kind == MHW_DMG_EXPLOSIONS) o->flags2 |= FLAG2_IMMUNE_GUNFIRE;
    else if (kind == MHW_DMG_NONE) { o->flags |= PROPFLAG_INVINCIBLE; o->flags2 |= FLAG2_IMMUNE_GUNFIRE | FLAG2_IMMUNE_EXPLOSIONS; }
    return 1;
}

static int wObjSetHidden(void *ud, int prop, int on)
{
    ObjectRecord *o = wObj(prop);
    (void)ud;
    if (!o) return 0;
    if (on) o->flags2 |= FLAG2_HIDDEN_NO_SOUND; else o->flags2 &= ~FLAG2_HIDDEN_NO_SOUND;
    return 1;
}

/* AI_PlayAnimation on an aircraft (chrai.c): animation_table_ptrs2 (helicopter cradle, plane runway, helicopter takeoff),
 * flip 0, speed 0.5 per tick at 1x, the end frame when given. */
static int wObjAnim(void *ud, int prop, int id, int from, int to, double speed, double blend)
{
    ObjectRecord *o = wObj(prop);
    (void)ud;
    if (!o || o->type != PROPDEF_AIRCRAFT || !o->model || !o->prop) return 0;
    if (id < 0 || id > 2) return 0;   /* the table's three entries */
    modelSetAnimation(o->model, PORT_N64PTR(ModelAnimation, animation_table_ptrs2[id]), 0, (f32)(from >= 0 ? from : 0),
                      (f32)(0.5 * (speed > 0 ? speed : 1.0)), blend >= 0 ? (f32)(blend * TICKRATE) : 16.0f);
    if (to >= 0) modelSetAnimEndFrame(o->model, (f32)to);
    return 1;
}

static void doCollect(void *a)
{
    /* AI_BondCollectObject */
    ObjectRecord *obj = a;
    TICKOP op = propPickupByPlayer(obj->prop, FALSE);
    propExecuteTickOperation(obj->prop, op);
}

static int wGiveObject(void *ud, int prop, int player)
{
    ObjectRecord *o = wObj(prop);
    int given = 0;
    (void)ud;
    if (!o) return 0;
    for (int p = 0; p < wPlayers(); p++) {
        if ((player >= 0 && player != p) || !o->prop) continue;   /* the first pickup takes the prop: one player gets it */
        missionsHostWithPlayer(p, doCollect, o);
        given = 1;
    }
    return given;
}

/* ---- doors -------------------------------------------------------------------------------------------------------- */

/* AI_IFDoorStateEqual's reading of openstate / openPosition (waiting: fully open, about to close, reads "open"). */
static int wDoorPhase(void *ud, int prop)
{
    DoorRecord *d = wDoor(prop);
    (void)ud;
    if (!d || !d->prop) return MHW_DOOR_NONE;
    if (d->openstate == DOORSTATE_OPENING) return MHW_DOOR_OPENING;
    if (d->openstate == DOORSTATE_CLOSING) return MHW_DOOR_CLOSING;
    return d->openPosition <= 0 ? MHW_DOOR_CLOSED : MHW_DOOR_OPEN;
}

/* AI_DoorOpenInstant */
static int wDoorOpenNow(void *ud, int prop)
{
    DoorRecord *door = wDoor(prop);
    (void)ud;
    if (!door || !door->prop) return 0;
    door->speed = 0;
    door->openPosition = door->maxFrac;
    door->openedTime = g_GlobalTimer;
    door->openstate = DOORSTATE_STATIONARY;
    doorUpdateBbox(door);
    doorActivatePortal(door);
    door7F053B10(door);
    return 1;
}

/* set_passable(false): the door is shut and kept shut. Players are refused by a key bit nobody holds (propdoorInteract
 * only opens a keyed door for a holder); guards open doors with doorActivate directly (chraction.c), so the tick below
 * closes the door again whenever it is not shut (missionsHostWorldTick). */
#define MAX_BLOCKED 32
static struct { int prop; u32 key; } s_blocked[MAX_BLOCKED];
static int s_nblocked;

static int wDoorSetPassable(void *ud, int prop, int on)
{
    DoorRecord *d = wDoor(prop);
    (void)ud;
    if (!d || !d->prop) return 0;
    for (int k = 0; k < s_nblocked; k++) {
        if (s_blocked[k].prop != prop) continue;
        if (on) {
            if (d->keyflags & KEY_LOCK_BIT) d->keyflags = s_blocked[k].key;
            s_blocked[k] = s_blocked[--s_nblocked];
        }
        return 1;
    }
    if (on) return 1;
    if (s_nblocked >= MAX_BLOCKED) return 0;
    s_blocked[s_nblocked].prop = prop;
    s_blocked[s_nblocked].key = d->keyflags;
    s_nblocked++;
    d->keyflags |= KEY_LOCK_BIT;
    return 1;
}

static void blockedDoorsTick(void)
{
    for (int k = 0; k < s_nblocked; k++) {
        DoorRecord *d = wDoor(s_blocked[k].prop);
        if (!d || !d->prop) continue;
        if (!(d->keyflags & KEY_LOCK_BIT)) d->keyflags |= KEY_LOCK_BIT;
        if (d->openstate != DOORSTATE_CLOSING && (d->openstate != DOORSTATE_STATIONARY || d->openPosition > 0.0f))
            doorActivate(d, DOORSTATE_CLOSING);
    }
}

/* The door's "character AI cannot operate" bit: guards do not open it (flags2 0x20000000, chraction.c reads it as
 * PROPFLAG_DOOR_OPENTOFRONT when a chr walks up to a door). */
static int wDoorSetUsableByGuards(void *ud, int prop, int on)
{
    DoorRecord *d = wDoor(prop);
    (void)ud;
    if (!d) return 0;
    if (on) d->flags2 &= ~PROPFLAG2_20000000; else d->flags2 |= PROPFLAG2_20000000;
    return 1;
}

/* ---- players ------------------------------------------------------------------------------------------------------ */

static int wPlayerSetInvincible(void *ud, int player, int on)
{
    (void)ud;
    if (player < 0 || player >= wPlayers() || !g_playerPointers[player]) return 0;
    g_playerPointers[player]->cheatBondInvincible = on ? 1 : 0;   /* the game's own per-player invincibility (the cheat) */
    return 1;
}

static int wPlayerArmour(void *ud, int player, double *out)
{
    (void)ud;
    if (player < 0 || player >= wPlayers() || !g_playerPointers[player]) return 0;
    *out = g_playerPointers[player]->bondarmour;
    return 1;
}

static int wPlayerHealth(void *ud, int player, double *out)
{
    (void)ud;
    if (player < 0 || player >= wPlayers() || !g_playerPointers[player]) return 0;
    *out = g_playerPointers[player]->bondhealth;   /* 0..1 (currentPlayerGetHealth) */
    return 1;
}

static void doKill(void *a) { (void)a; bondviewKillCurrentPlayer(); }

/* The player dies at once through the game's own death (bondviewKillCurrentPlayer: sets the KIA mark, drops the weapons);
 * a player under the invincibility cheat / set_invincible does not die, as in the game. */
static int wPlayerKill(void *ud, int player)
{
    (void)ud;
    if (player < 0 || player >= wPlayers() || !g_playerPointers[player] || g_playerPointers[player]->bonddead) return 0;
    missionsHostWithPlayer(player, doKill, NULL);
    return 1;
}

/* AI_TriggerExplosionsAroundBond: SurroundWithExplosions(0) raises g_SurroundBondWithExplosionsFlag, which every living
 * player's tick (bondviewPlayerTickExplode) turns into explosions around that player at the game's own cadence; off
 * clears the flag (the level's start clears it too, bondview_r.c). */
static int s_explosionsOn;
static int wExplosionsAroundPlayers(void *ud, int on)
{
    (void)ud;
    if (on) { SurroundWithExplosions(0); s_explosionsOn = 1; }
    else { g_SurroundBondWithExplosionsFlag = 0; s_explosionsOn = 0; }
    return 1;
}

/* AI_BondKilledInAction: g_isBondKIA (the debrief reads it, front.c); the binding then fails the level. */
static int wKilledInAction(void *ud) { (void)ud; g_isBondKIA = TRUE; return 1; }

/* The one place the game's facing angles become the camera API's yaw (degrees, 0 = +z, positive turns left towards +x,
 * wrapped to -180..180; the same yaw camera.set takes, whose game theta is pi - yaw).
 *  - a chr: getsubroty(model) is radians with the heading atan2(dx, dz) (chraction.c turns a chr to face a point with
 *    atan2(dx, dz) - subroty, and a spawn sets subroty to atan2(pad look x, look z)): it already is the API yaw.
 *  - a player: vv_theta is degrees with theta = 360 - atan2(look x, look z) (bondview_r.c start, theta_transform =
 *    (-sin theta, 0, cos theta) is the forward vector): the API yaw is -vv_theta. */
static double apiYawDeg(double deg)
{
    deg = fmod(deg, 360.0);
    if (deg > 180.0) deg -= 360.0;
    if (deg <= -180.0) deg += 360.0;
    return deg;
}

static int wFacing(void *ud, int kind, int index, double *out)
{
    (void)ud;
    if (kind == MHW_ROOM_CHR) {
        const ChrRecord *c = chrFindByLiteralId(index);
        if (!c || !c->prop || !c->model) return 0;
        *out = apiYawDeg((double)getsubroty(c->model) * 180.0 / M_PI);
        return 1;
    }
    if (kind == MHW_ROOM_PLAYER) {
        if (index < 0 || index >= wPlayers() || !g_playerPointers[index] || !g_playerPointers[index]->prop) return 0;
        *out = apiYawDeg(-(double)g_playerPointers[index]->vv_theta);
        return 1;
    }
    return 0;
}

/* The prop's position of a player is its collision position, whose y is the floor plus the eye height
 * (bondviewUpdatePlayerCollisionPositionFields): in first person that is the eye point, the intro swirl's origin. In
 * the scripted-camera mode (CAMERAMODE_POSEND, the cinema domain's) the head animation is not driven and the same
 * field falls by ~57 units, so there the eye is the same formula with the standing head height (standheight, what
 * headpos.y settles at when the player stands still) in place of the live one. */
struct eyeArg { double y; int fp; };
static void doEye(void *a)
{
    struct eyeArg *e = a;
    const f32 k = g_playerPerm->player_perspective_height;
    f32 h = g_CurrentPlayer->standheight * k + 7.0f + (g_CurrentPlayer->field_88 + g_CurrentPlayer->ducking_height_offset) * k;
    const s32 mode = bondviewGetCameraMode();
    e->fp = mode == CAMERAMODE_FP || mode == CAMERAMODE_FP_NOINPUT;
    if (h < 30.0f) h = 30.0f;
    e->y = g_CurrentPlayer->field_70 + h;
}

static int wPlayerEye(void *ud, int player, double out[3])
{
    const struct player *pl;
    struct eyeArg e;
    (void)ud;
    if (player < 0 || player >= wPlayers() || !(pl = g_playerPointers[player]) || !pl->prop) return 0;
    e.y = 0; e.fp = 1;
    missionsHostWithPlayer(player, doEye, &e);
    out[0] = pl->prop->pos.f[0];
    out[1] = e.fp ? pl->prop->pos.f[1] : e.y;
    out[2] = pl->prop->pos.f[2];
    return 1;
}

static int wPlayerHeldItem(void *ud, int player) { (void)ud; return heldItemOf(player); }

static int wPlayerHolds(void *ud, int player, int item)
{
    struct itemArg a;
    (void)ud;
    a.item = item;
    a.result = 0;
    if (player < 0 || player >= wPlayers()) return 0;
    missionsHostWithPlayer(player, doHolds, &a);
    return a.result;
}

static int wPlayerForceWeapon(void *ud, int player, int item)
{
    struct itemArg a;
    (void)ud;
    a.item = item;
    a.result = 0;
    if (player < 0 || player >= wPlayers()) return 0;
    missionsHostWithPlayer(player, doEquip, &a);
    return 1;
}

struct pushArg { float dx, dz; };
static void doPush(void *a)
{
    /* The frozen player's move (bondview2.c, the cutscene path): the offset goes through the game's own collision, then
     * the player's height and position fields follow. */
    struct pushArg *p = a;
    struct coord3d off;
    off.f[0] = p->dx; off.f[1] = 0.0f; off.f[2] = p->dz;
    bondviewCalcUpdatePlayerCollision(&off, 1);
    bondviewUpdatePlayerY(0, 0.0f);
    bondviewUpdatePlayerCollisionPositionFields();
}

static int wPlayerPush(void *ud, int player, double dx, double dz)
{
    struct pushArg a;
    (void)ud;
    if (player < 0 || player >= wPlayers() || !g_playerPointers[player] || g_playerPointers[player]->bonddead) return 0;
    a.dx = (float)dx; a.dz = (float)dz;
    missionsHostWithPlayer(player, doPush, &a);
    return 1;
}

/* ---- HUD, countdown, sound, music, fog ---------------------------------------------------------------------------- */

/* AI_TextPrintTop: hudmsgTopShow (one queue for every player's view, bondview2.c). The line is wrapped to the HUD's
 * width and ends in a newline, as say() does (missions_host.c sayLine). */
static int wHudTop(void *ud, const char *text)
{
    char line[0x200];
    size_t len;
    (void)ud;
    textWrap(viGetViewWidth() - 2 * 0x1e, (char *)(text ? text : ""), line, ptrFontZurichBoldChars, ptrFontZurichBold);
    len = strlen(line);
    if (len + 2 >= 0x97) len = 0x97 - 2;   /* bondview2.c BONDVIEW_HUD_MSG_TOP_BUFFER_LENGTH, with room for the newline */
    if (len == 0 || line[len - 1] != '\n') line[len++] = '\n';
    line[len] = 0;
    hudmsgTopShow(line);
    return 1;
}

/* AI_HudCountdown*: the clock counts ticks; Show clears the "hidden" bit, Hide sets it (countdownTimerSetVisible's unset). */
static int wCountdownSet(void *ud, double seconds) { (void)ud; countdownTimerSetValue((f32)(seconds * TICKRATE)); return 1; }
static int wCountdownRun(void *ud, int on) { (void)ud; countdownTimerSetRunning(on ? TRUE : FALSE); return 1; }
static int wCountdownShow(void *ud, int on) { (void)ud; countdownTimerSetVisible(1, on ? TRUE : FALSE); return 1; }
static double wCountdownLeft(void *ud) { (void)ud; return (double)countdownTimerGetValue() / TICKRATE; }

static int slotFree(int i)
{
    const sfxRecord *s = &sfx_related[i];
    return !s->state || !sndGetPlayingState(s->state);
}

/* AI_SfxPlay (audioPlayFromProp) in one of the AI's 8 sound slots; from a prop or a pad as AI_SfxEmitFromObject /
 * AI_SfxEmitFromPad: the slot's Obj / pos, followed by audioPlayFromProp2 each frame (loop_set_sound_effect_all_slots). */
static int wSoundPlay(void *ud, int id, int channel, int from_kind, int from_index)
{
    (void)ud;
    if (channel < 0) {
        for (int i = 0; i < SFX_RELATED_LEN; i++)
            if (slotFree(i)) { channel = i; break; }
        if (channel < 0) return -1;
    } else if (channel >= SFX_RELATED_LEN) {
        return -1;
    }
    audioPlayFromProp(channel, (s16)id);
    if (from_kind == MHW_FROM_PROP) {
        ObjectRecord *o = wObj(from_index);
        if (o) { sfx_related[channel].pos = NULL; sfx_related[channel].Obj = o; audioPlayFromProp2(channel); }
    } else if (from_kind == MHW_FROM_PAD) {
        PadRecord *p = wPad(from_index);
        if (p) { sfx_related[channel].pos = &p->pos; sfx_related[channel].Obj = NULL; audioPlayFromProp2(channel); }
    }
    return channel;
}

static int wSoundStop(void *ud, int channel) { (void)ud; sub_GAME_7F0349BC(channel); return 1; }

/* AI_SfxSetChannelVolume / AI_SfxFadeChannelVolume: the slot's Volume, with a fade length in ticks in its sfxID field
 * (audioPlayFromProp2 counts it down); 0 applies at once. The slot keeps its emitter. */
static int wSoundVolume(void *ud, int channel, double volume, double fade)
{
    sfxRecord *s;
    (void)ud;
    if (channel < 0 || channel >= SFX_RELATED_LEN) return 0;
    s = &sfx_related[channel];
    s->Volume = (s32)(volume * SHRT_MAX);
    s->sfxID = fade > 0 ? (s32)(fade * TICKRATE) : 0;
    if (fade <= 0) audioPlayFromProp2(channel);
    return 1;
}

/* AI_MusicPlaySlot / AI_MusicStopSlot (the fade is not a parameter of the game's slot) */
static int wMusicPlay(void *ud, int slot, int stop_after, int total, double fade)
{
    (void)ud; (void)fade;
    if (slot < 0 || slot > 3) return 0;
    musicPlaySlot(slot, stop_after, total);
    return 1;
}
static int wMusicStop(void *ud, int slot) { (void)ud; if (slot < 0 || slot > 3) return 0; musicStopSlot(slot); return 1; }

/* AI_SwitchSky: the level's next environment (fogSwitchToSolosky2 with 1.0 = fully the alternate). `slow` has no
 * counterpart in the game's one switch: it is the same call. */
static int wFogNext(void *ud, int slow) { (void)ud; (void)slow; fogSwitchToSolosky2(1.0f); return 1; }

/* ---- alarm, the end of the level, objectives ---------------------------------------------------------------------- */

static int wAlarmIsOn(void *ud) { (void)ud; return alarmIsActive() ? 1 : 0; }
static int wAlarmStop(void *ud) { (void)ud; alarmDeactivate(); return 1; }   /* AI_AlarmOff */

/* The end sequence has started: the level's end trigger (AI_TriggerFadeAndExitLevelOnButtonPress / AI_EndLevel) or the
 * stage already left for the title (a win or fail went out). */
static int wExiting(void *ud)
{
    (void)ud;
    return stop_time_flag != 0 || cameraFrameCounter2 != 0 || (int)bossGetStageNum() != missionsStage();
}

static int wObjectiveId(void *ud, int index, char *out, size_t outlen, int *difficulty)
{
    const Mission *m = missionsCurrent();
    (void)ud;
    if (!m || index < 0 || index >= m->nobjectives) return 0;
    snprintf(out, outlen, "%s", m->objectives[index].id);
    *difficulty = m->objectives[index].difficulty;
    return 1;
}

/* ---- the host ----------------------------------------------------------------------------------------------------- */

const MissionHostWorld *missionsHostWorld(void)
{
    memset(&s_host, 0, sizeof s_host);
    s_host.in_room = wInRoom;
    s_host.player_in_room = wPlayerInRoom;
    s_host.item_landed = wItemLanded;
    s_host.same_room = wSameRoom;
    s_host.item_in_room = wItemInRoom;
    s_host.obj_destroyed = wObjDestroyed;
    s_host.obj_destroy = wObjDestroy;
    s_host.obj_explode = wObjExplode;
    s_host.obj_set_invincible = wObjSetInvincible;
    s_host.obj_set_collectable = wObjSetCollectable;
    s_host.obj_move_to = wObjMoveTo;
    s_host.obj_activate = wObjActivate;
    s_host.obj_show_screen = wObjShowScreen;
    s_host.obj_held_by = wObjHeldBy;
    s_host.obj_set_rotor = wObjSetRotor;
    s_host.obj_drive = wObjDrive;
    s_host.obj_set_active = wObjSetActive;
    s_host.obj_set_damage = wObjSetDamage;
    s_host.obj_set_hidden = wObjSetHidden;
    s_host.obj_anim = wObjAnim;
    s_host.give_object = wGiveObject;
    s_host.door_phase = wDoorPhase;
    s_host.door_open_now = wDoorOpenNow;
    s_host.door_set_passable = wDoorSetPassable;
    s_host.door_set_usable_by_guards = wDoorSetUsableByGuards;
    s_host.player_set_invincible = wPlayerSetInvincible;
    s_host.player_armour = wPlayerArmour;
    s_host.player_held_item = wPlayerHeldItem;
    s_host.player_holds = wPlayerHolds;
    s_host.player_force_weapon = wPlayerForceWeapon;
    s_host.player_push = wPlayerPush;
    s_host.player_health = wPlayerHealth;
    s_host.player_kill = wPlayerKill;
    s_host.explosions_around_players = wExplosionsAroundPlayers;
    s_host.killed_in_action = wKilledInAction;
    s_host.facing = wFacing;
    s_host.player_eye = wPlayerEye;
    s_host.hud_top = wHudTop;
    /* hud_hide: no single game switch hides the whole HUD (it is drawn piece by piece: ammo, sight, health, messages ...);
     * left NULL, the binding logs "hud.hide: not implemented yet" */
    s_host.countdown_set = wCountdownSet;
    s_host.countdown_run = wCountdownRun;
    s_host.countdown_show = wCountdownShow;
    s_host.countdown_left = wCountdownLeft;
    s_host.sound_play = wSoundPlay;
    s_host.sound_stop = wSoundStop;
    s_host.sound_volume = wSoundVolume;
    s_host.music_play = wMusicPlay;
    s_host.music_stop = wMusicStop;
    s_host.fog_next = wFogNext;
    s_host.alarm_is_on = wAlarmIsOn;
    s_host.alarm_stop = wAlarmStop;
    s_host.exiting = wExiting;
    s_host.objective_id = wObjectiveId;
    return &s_host;
}

/* ---- events, diffed per tick -------------------------------------------------------------------------------------- */

/* Per setup object: the flags seen at the last tick. "destroyed": the destroyed state bit comes on; "activated": the
 * activated bit (RUNTIMEBITFLAG_ACTIVATED, what propobjInteract sets and AI_IFObjectWasActivated clears) comes on (on a
 * door that is a refusal: propdoorInteract sets it for a door the player cannot open); "gadget_used": the object's
 * PROPSTATE_ACTIVATED (gunfire.c sets it when a gadget hits an object; AI_IFBondUsedGadgetOnObject clears it);
 * "picked_up": a collectable's prop is gone without the object being destroyed (propPickupByPlayer frees it). An edge
 * that the same frame's was_used() or AI list clears before this tick is not seen (the scripts' own consumption wins). */
struct objSeen { unsigned char init, dest, act, gad, had; short item; float pos[3]; };
static struct objSeen *s_seen;
static int s_nseen;

static int pickupType(int t)
{
    return t == PROPDEF_KEY || t == PROPDEF_MAGAZINE || t == PROPDEF_AMMO || t == PROPDEF_COLLECTABLE || t == PROPDEF_ARMOUR;
}

static void evObj(const char *name, int prop, int tag, int player, int item, int withItem)
{
    MissionEvField f[4];
    int n = 0;
    if (tag >= 0)    { f[n].key = "tag";    f[n].str = NULL; f[n++].num = tag; }
    f[n].key = "prop"; f[n].str = NULL; f[n++].num = prop;
    if (withItem)    { f[n].key = "item";   f[n].str = NULL; f[n++].num = item; }
    if (player >= 0) { f[n].key = "player"; f[n].str = NULL; f[n++].num = player; }
    missionRtRaise(missionsRuntime(), name, f, n);
}

/* "key_copied": a player's copiedgoldeneye turns on (gunfire.c analyzeGEKey, the Bunker 1 key analyzer; the game knows no
 * tag for it, so the event carries the player only). "photographed": a photograph criterion's flag turns on
 * (objective_status.c objectiveTakePictureHandler); tag = the photographed object's tag, player = the one nearest to it. */
#define MAX_PHOTOS 16
static struct { const struct criteria_picture *c; unsigned char seen; } s_photo[MAX_PHOTOS];
static int s_nphoto;
static unsigned char s_keySeen[4], s_keyInit;

static void evPlayerOnly(const char *name, int tag, int player)
{
    MissionEvField f[2];
    int n = 0;
    if (tag >= 0) { f[n].key = "tag"; f[n].str = NULL; f[n++].num = tag; }
    f[n].key = "player"; f[n].str = NULL; f[n++].num = player;
    missionRtRaise(missionsRuntime(), name, f, n);
}

static void keyAndPhotoEvents(void)
{
    const struct criteria_picture *c;
    for (int p = 0; p < wPlayers(); p++) {
        const int now = g_playerPointers[p] && g_playerPointers[p]->copiedgoldeneye;
        if (s_keyInit && now && !s_keySeen[p]) evPlayerOnly("key_copied", -1, p);
        s_keySeen[p] = (unsigned char)now;
    }
    s_keyInit = 1;
    for (c = ptr_last_photo_obj_in_room_subobject_entry_type1E; c; c = c->next) {
        int k;
        for (k = 0; k < s_nphoto && s_photo[k].c != c; k++) ;
        if (k == s_nphoto) {
            if (s_nphoto >= MAX_PHOTOS) continue;
            s_photo[s_nphoto].c = c;
            s_photo[s_nphoto].seen = (unsigned char)(c->flag != 0);   /* already taken when first seen: no event */
            s_nphoto++;
            continue;
        }
        if (c->flag != 0 && !s_photo[k].seen) {
            const ObjectRecord *o = objFindByTagId(c->tag_id);
            float where[3] = { 0, 0, 0 };
            int pl;
            if (o && o->prop) { where[0] = o->prop->pos.f[0]; where[1] = o->prop->pos.f[1]; where[2] = o->prop->pos.f[2]; }
            pl = nearestPlayer(where);
            evPlayerOnly("photographed", c->tag_id, pl < 0 ? 0 : pl);
        }
        s_photo[k].seen = (unsigned char)(c->flag != 0);
    }
}

/* Per tick, after the scripts ran (missions_host.c). */
void missionsHostWorldTick(void)
{
    const int n = missionsHostPropCount();
    if (!missionsRuntime()) return;
    if (n != s_nseen) {
        free(s_seen);
        s_seen = (struct objSeen *)calloc(n ? (size_t)n : 1, sizeof *s_seen);
        s_nseen = n;
    }
    blockedDoorsTick();
    keyAndPhotoEvents();
    for (int i = 0; s_seen && i < n; i++) {
        ObjectRecord *o = wObj(i);
        struct objSeen *s = &s_seen[i];
        int dest, act, gad, had;
        if (!o) continue;
        dest = (o->state & PROPSTATE_DESTROYED) != 0;
        act = (o->runtime_bitflags & RUNTIMEBITFLAG_ACTIVATED) != 0;
        gad = (o->state & PROPSTATE_ACTIVATED) != 0;
        had = o->prop != NULL;
        if (s->init) {
            float where[3];
            const int tag = (dest && !s->dest) || (act && !s->act) || (gad && !s->gad) || (!had && s->had) ? tagOf(o) : -1;
            where[0] = o->prop ? o->prop->pos.f[0] : s->pos[0];
            where[1] = o->prop ? o->prop->pos.f[1] : s->pos[1];
            where[2] = o->prop ? o->prop->pos.f[2] : s->pos[2];
            if (dest && !s->dest) evObj("destroyed", i, tag, -1, 0, 0);
            if (act && !s->act) {
                if (o->type == PROPDEF_DOOR) evObj("door_refused", i, tag, nearestPlayer(where), 0, 0);
                else evObj("activated", i, tag, nearestPlayer(where), 0, 0);
            }
            if (gad && !s->gad) {
                const int pl = nearestPlayer(where);
                evObj("gadget_used", i, tag, pl, heldItemOf(pl), 1);
            }
            if (!had && s->had && !dest && pickupType(o->type) && !(o->flags & PROPFLAG_ASSIGNEDTOCHR))   /* a guard's own weapon goes with the guard */
                evObj("picked_up", i, tag, nearestPlayer(where), s->item, 1);
        }
        s->init = 1; s->dest = (unsigned char)dest; s->act = (unsigned char)act; s->gad = (unsigned char)gad; s->had = (unsigned char)had;
        if (o->type == PROPDEF_COLLECTABLE) s->item = ((WeaponObjRecord *)o)->weaponnum;
        if (o->prop) { s->pos[0] = o->prop->pos.f[0]; s->pos[1] = o->prop->pos.f[1]; s->pos[2] = o->prop->pos.f[2]; }
    }
}

/* A level starts or the runtime stops: drop this domain's state (missionsHostDomainsReset). */
void missionsHostWorldReset(void)
{
    free(s_seen);
    s_seen = NULL;
    s_nseen = 0;
    s_nblocked = 0;
    s_nphoto = 0;
    s_keyInit = 0;
    memset(s_keySeen, 0, sizeof s_keySeen);
    if (s_explosionsOn) g_SurroundBondWithExplosionsFlag = 0;
    s_explosionsOn = 0;
}
