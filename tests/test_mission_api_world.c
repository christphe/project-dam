/* The world domain of the campaign API (#165, port/mission/mission_lua_world.cpp) driven by a fake host: argument
 * parsing, return values, the stubs of a host without the domain, the objectives / alarm / mission tables and the events
 * the host raises. The scripts are files in a temporary mission directory (like tests/test_mission_lua.c). */
#include "ge_test.h"
#include "mission.h"
#include "mission_lua.h"
#include "mission_host_world.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define MKDIR(p) mkdir(p, 0755)
#endif

typedef struct Fake {
    int nplayers;
    char msgs[96][128];
    int nmsg;
    char logs[96][200];
    int nlog;
    char calls[4096];                 /* every world callback: "name(args);" */
    int won, failed;
    int diff;
    int alarm, exiting;
    double left;
} Fake;

static void call(Fake *f, const char *fmt, ...)
{
    char b[160];
    va_list ap;
    size_t n = strlen(f->calls);
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    if (n + strlen(b) + 2 < sizeof f->calls) { strcat(f->calls, b); strcat(f->calls, ";"); }
}
static int called(const Fake *f, const char *s) { return strstr(f->calls, s) != NULL; }

static void fLog(void *u, const char *l) { Fake *f = u; if (f->nlog < 96) snprintf(f->logs[f->nlog++], 200, "%s", l); }
static void fMsg(void *u, const char *t) { Fake *f = u; if (f->nmsg < 96) snprintf(f->msgs[f->nmsg++], 128, "%s", t); }
static int  fPc(void *u) { return ((Fake *)u)->nplayers; }
static int  fPpos(void *u, int p, float *xyz) { (void)u; xyz[0] = (float)p * 10.0f; xyz[1] = 0; xyz[2] = 0; return 1; }
static int  fPropCount(void *u) { (void)u; return 8; }
static int  fPropType(void *u, int i) { (void)u; return i == 5 ? 1 : i == 6 ? 9 : 3; }
static int  fPropPos(void *u, int i, float *xyz) { (void)u; xyz[0] = (float)i; xyz[1] = 0; xyz[2] = 0; return 1; }
static int  fDiff(void *u) { return ((Fake *)u)->diff; }
static void fWin(void *u) { ((Fake *)u)->won = 1; }
static void fFail(void *u) { ((Fake *)u)->failed = 1; }
static int  fTagProp(void *u, int tag) { (void)u; return tag == 7 ? 3 : -1; }
static int  fChrProp(void *u, int chr) { (void)u; return chr == 9 ? 6 : -1; }
static int  fPropChr(void *u, int prop) { (void)u; return prop == 6 ? 9 : -1; }
static int  fPadById(void *u, const char *id) { (void)u; return !strcmp(id, "mypad") ? 77 : -1; }
static int  fGiven[8], fNGiven;
static void fGive(void *u, int pl, int item) { (void)u; if (fNGiven < 4) { fGiven[fNGiven * 2] = pl; fGiven[fNGiven * 2 + 1] = item; fNGiven++; } }

/* ---- the world host ---- */
static int wInRoom(void *u, int kind, int index, int pad) { call(u, "in_room(%d,%d,%d)", kind, index, pad); return kind == MHW_ROOM_CHR || kind == MHW_ROOM_PAD; }
static int wPlayerInRoom(void *u, int pad) { call(u, "player_in_room(%d)", pad); return pad == 12 ? 1 : -1; }
static int wItemLanded(void *u, int item, int *tag) { call(u, "item_landed(%d)", item); *tag = item == 3 ? 7 : -1; return item == 3 ? MHW_LANDED_STUCK : item == 4 ? MHW_LANDED_LOOSE : MHW_LANDED_NONE; }
static int wSameRoom(void *u, int ka, int ia, int kb, int ib) { call(u, "same_room(%d,%d,%d,%d)", ka, ia, kb, ib); return ka == kb; }
static int wItemInRoom(void *u, int item, int pad) { call(u, "item_in_room(%d,%d)", item, pad); return item == 3 && pad == 77; }
static int wObjActive(void *u, int p, int on) { call(u, "active(%d,%d)", p, on); return p == 3; }
static int wObjDamage(void *u, int p, int k) { call(u, "damage(%d,%d)", p, k); return 1; }
static int wObjHidden(void *u, int p, int on) { call(u, "hidden(%d,%d)", p, on); return p == 3; }   /* only prop 3 is an object */
static int wObjAnim(void *u, int p, int id, int from, int to, double sp, double bl) { call(u, "oanim(%d,%d,%d,%d,%.1f,%.1f)", p, id, from, to, sp, bl); return id < 3; }
static int wGiveObj(void *u, int p, int pl) { call(u, "give_object(%d,%d)", p, pl); return 1; }
static int wDoorGuards(void *u, int p, int on) { call(u, "door_guards(%d,%d)", p, on); return 1; }
static int wPlHealth(void *u, int pl, double *out) { (void)u; *out = pl == 1 ? 0.5 : 1.0; return 1; }
static int wPlKill(void *u, int pl) { call(u, "kill(%d)", pl); return 1; }
static int wExpl(void *u, int on) { call(u, "explosions(%d)", on); return 1; }
static int wKia(void *u) { call(u, "kia"); return 1; }
/* player 1 is placed, facing 30; chr 9 faces -170; player 0 is not placed */
static int wFacing(void *u, int kind, int index, double *out)
{
    (void)u;
    if (kind == MHW_ROOM_PLAYER && index == 1) { *out = 30; return 1; }
    if (kind == MHW_ROOM_CHR && index == 9) { *out = -170; return 1; }
    return 0;
}
static int wEye(void *u, int pl, double out[3]) { (void)u; if (pl != 1) return 0; out[0] = 1; out[1] = 175; out[2] = -3; return 1; }
static int wObjDestroyed(void *u, int p) { call(u, "is_destroyed(%d)", p); return p == 3; }
static int wObjDestroy(void *u, int p) { call(u, "destroy(%d)", p); return 1; }
static int wObjExplode(void *u, int p) { call(u, "explode(%d)", p); return 1; }
static int wObjInv(void *u, int p, int on) { call(u, "obj_invincible(%d,%d)", p, on); return p == 3; }   /* only prop 3 is an object */
static int wObjColl(void *u, int p, int on) { call(u, "collectable(%d,%d)", p, on); return 1; }
static int wObjMove(void *u, int p, int pad) { call(u, "move_to(%d,%d)", p, pad); return 1; }
static int wObjAct(void *u, int p) { call(u, "activate(%d)", p); return 1; }
static int wObjScreen(void *u, int p, int bank, int scr) { call(u, "screen(%d,%d,%d)", p, bank, scr); return 1; }
static int wObjHeld(void *u, int p, int pl) { call(u, "held_by(%d,%d)", p, pl); return pl == 1; }
static int wObjRotor(void *u, int p, double s, double r) { call(u, "rotor(%d,%.0f,%.0f)", p, s, r); return 1; }
static int wObjDrive(void *u, int p, int path, double s, double r) { call(u, "drive(%d,%d,%.0f,%.0f)", p, path, s, r); return 1; }
static int wDoorPhase(void *u, int p) { (void)u; return p == 5 ? MHW_DOOR_CLOSING : MHW_DOOR_NONE; }
static int wDoorNow(void *u, int p) { call(u, "open_now(%d)", p); return 1; }
static int wDoorPass(void *u, int p, int on) { call(u, "passable(%d,%d)", p, on); return 1; }
static int wPlInv(void *u, int pl, int on) { call(u, "player_invincible(%d,%d)", pl, on); return 1; }
static int wPlArmour(void *u, int pl, double *out) { (void)u; *out = pl == 1 ? 0.75 : 0.0; return 1; }
static int wPlHeld(void *u, int pl) { (void)u; return pl == 1 ? 17 : 1; }
static int wPlHolds(void *u, int pl, int item) { (void)u; return pl == 1 && item == 5; }
static int wPlForce(void *u, int pl, int item) { call(u, "force_weapon(%d,%d)", pl, item); return 1; }
static int wPlPush(void *u, int pl, double dx, double dz) { call(u, "push(%d,%.0f,%.0f)", pl, dx, dz); return 1; }
static int wHudTop(void *u, const char *t) { call(u, "hud_top(%s)", t); return 1; }
static int wHudHide(void *u, int on) { call(u, "hud_hide(%d)", on); return 1; }
static int wCdSet(void *u, double s) { call(u, "cd_set(%.0f)", s); ((Fake *)u)->left = s; return 1; }
static int wCdRun(void *u, int on) { call(u, "cd_run(%d)", on); return 1; }
static int wCdShow(void *u, int on) { call(u, "cd_show(%d)", on); return 1; }
static double wCdLeft(void *u) { return ((Fake *)u)->left; }
static int wSndPlay(void *u, int id, int ch, int kind, int index) { call(u, "sound_play(%d,%d,%d,%d)", id, ch, kind, index); return ch >= 0 ? ch : 4; }
static int wSndStop(void *u, int ch) { call(u, "sound_stop(%d)", ch); return 1; }
static int wSndVol(void *u, int ch, double v, double f) { call(u, "sound_volume(%d,%.2f,%.0f)", ch, v, f); return 1; }
static int wMusPlay(void *u, int slot, int a, int t, double f) { call(u, "music_play(%d,%d,%d,%.0f)", slot, a, t, f); return 1; }
static int wMusStop(void *u, int slot) { call(u, "music_stop(%d)", slot); return 1; }
static int wFog(void *u, int slow) { call(u, "fog_next(%d)", slow); return 1; }
static int wAlarmOn(void *u) { return ((Fake *)u)->alarm; }
static int wAlarmStop(void *u) { call(u, "alarm_stop"); ((Fake *)u)->alarm = 0; return 1; }
static int wExiting(void *u) { return ((Fake *)u)->exiting; }
static int wObjId(void *u, int i, char *out, size_t n, int *diff)
{
    static const char *const ids[] = { "o1", "o2" };
    (void)u;
    if (i < 0 || i > 1) return 0;
    snprintf(out, n, "%s", ids[i]);
    *diff = i == 0 ? 0 : 2;
    return 1;
}

static MissionHostWorld worldHost(Fake *f)
{
    MissionHostWorld w;
    memset(&w, 0, sizeof w);
    w.ud = f;
    w.in_room = wInRoom; w.player_in_room = wPlayerInRoom; w.item_landed = wItemLanded;
    w.obj_destroyed = wObjDestroyed; w.obj_destroy = wObjDestroy; w.obj_explode = wObjExplode;
    w.obj_set_invincible = wObjInv; w.obj_set_collectable = wObjColl; w.obj_move_to = wObjMove; w.obj_activate = wObjAct;
    w.obj_show_screen = wObjScreen; w.obj_held_by = wObjHeld; w.obj_set_rotor = wObjRotor; w.obj_drive = wObjDrive;
    w.door_phase = wDoorPhase; w.door_open_now = wDoorNow; w.door_set_passable = wDoorPass;
    w.player_set_invincible = wPlInv; w.player_armour = wPlArmour; w.player_held_item = wPlHeld; w.player_holds = wPlHolds;
    w.player_force_weapon = wPlForce; w.player_push = wPlPush;
    w.hud_top = wHudTop; w.hud_hide = wHudHide;
    w.countdown_set = wCdSet; w.countdown_run = wCdRun; w.countdown_show = wCdShow; w.countdown_left = wCdLeft;
    w.sound_play = wSndPlay; w.sound_stop = wSndStop; w.sound_volume = wSndVol;
    w.music_play = wMusPlay; w.music_stop = wMusStop; w.fog_next = wFog;
    w.same_room = wSameRoom; w.item_in_room = wItemInRoom; w.obj_set_active = wObjActive; w.obj_set_damage = wObjDamage;
    w.obj_set_hidden = wObjHidden; w.obj_anim = wObjAnim; w.give_object = wGiveObj; w.door_set_usable_by_guards = wDoorGuards;
    w.player_health = wPlHealth; w.player_kill = wPlKill; w.explosions_around_players = wExpl; w.killed_in_action = wKia;
    w.facing = wFacing; w.player_eye = wEye;
    w.alarm_is_on = wAlarmOn; w.alarm_stop = wAlarmStop; w.exiting = wExiting; w.objective_id = wObjId;
    return w;
}

static MissionHost host(Fake *f, const MissionHostWorld *w)
{
    MissionHost h;
    memset(&h, 0, sizeof h);
    h.ud = f; h.log = fLog; h.message = fMsg; h.player_count = fPc; h.player_pos = fPpos;
    h.prop_count = fPropCount; h.prop_type = fPropType; h.prop_pos = fPropPos; h.difficulty = fDiff; h.win = fWin; h.fail = fFail;
    h.pad_by_id = fPadById; h.give = fGive; h.tag_prop = fTagProp; h.chr_prop = fChrProp; h.prop_chr = fPropChr;
    h.world_ext = w;
    return h;
}

static int hasMsg(const Fake *f, const char *s)
{
    for (int i = 0; i < f->nmsg; i++) if (!strcmp(f->msgs[i], s)) return 1;
    return 0;
}

static int countLog(const Fake *f, const char *needle)
{
    int n = 0;
    for (int i = 0; i < f->nlog; i++) if (strstr(f->logs[i], needle)) n++;
    return n;
}

static void run(MissionRuntime *rt, double secs)
{
    for (double t = 0; t < secs; t += 1.0 / 60.0) missionRtTick(rt, 1.0 / 60.0);
}

static void writeScript(const char *name, const char *src)
{
    char path[200];
    FILE *fp;
    snprintf(path, sizeof path, "tmp_worldtest/scripts/%s.luau", name);
    fp = fopen(path, "wb");
    if (fp) { fputs(src, fp); fclose(fp); }
}

static const char *kScript =
    "local function rec(k, v) message(k .. '=' .. tostring(v)) end\n"
    "local o = tag(7)\n"
    "rec('destroyed', o:is_destroyed())\n"
    "o:destroy(); o:explode(); o:set_invincible(true); o:set_collectable(false); o:move_to(12); o:activate()\n"
    "o:show_screen(4, 2); o:show_screen(6)\n"
    "rec('held', o:held_by()); rec('held1', o:held_by(1))\n"
    "o:set_rotor(100, 2); o:drive(5, 50, 1)\n"
    "local d = door(5)\n"
    "rec('state', d:state()); rec('state_obj', o:state()); d:open_now(); d:set_passable(false)\n"
    "local p = player(1)\n"
    "rec('armour', p:armour()); rec('held_item', p:held_item()); rec('holds5', p:holds(5)); rec('holds6', p:holds(6))\n"
    "p:force_weapon(7); p:push(3, -4); p:set_invincible(true)\n"
    "rec('room_chr', chr(9):in_room_of(12)); rec('room_obj', o:in_room_of(12)); rec('room_player', p:in_room_of(12))\n"
    "rec('room_pad', pad(13):in_room_of(12)); rec('room_str', p:in_room_of('x')); rec('room_mypad', chr(9):in_room_of('mypad'))\n"
    "rec('pir', player_in_room(12)); rec('pir2', tostring(player_in_room(99)))\n"
    "local s, t = item_landed(3); rec('landed', tostring(s) .. '/' .. tostring(t))\n"
    "s, t = item_landed(4); rec('landed2', tostring(s) .. '/' .. tostring(t))\n"
    "s, t = item_landed(9); rec('landed3', tostring(s) .. '/' .. tostring(t))\n"
    "rec('pad_kind', pad('mypad').kind .. ':' .. pad(3).pad)\n"
    "hud.top('hi'); hud.bottom('low'); hud.hide(true)\n"
    "countdown.set(30); countdown.start(); countdown.stop(); countdown.show(true); rec('left', countdown.left())\n"
    "rec('ch', sound.play(77, { from = o, channel = 2 })); rec('ch2', sound.play(78)); sound.stop(2); sound.volume(2, 2.0, 1)\n"
    "music.play_extra(1, { stop_after = 5, total = 20 }); music.play_extra(2); music.stop_extra(1)\n"
    "fog.next(true)\n"
    "rec('alarm', alarm.is_on()); alarm.stop(); rec('alarm2', alarm.is_on())\n"
    "rec('exiting', mission.exiting())\n"
    "objectives.add('a', 'A'); objectives.add('b', 'B', 3)\n"
    "objectives.set_failed('a', true); rec('failed', objectives.is_failed('a'))\n"
    "objectives.set_failed('a', false); rec('failed2', objectives.is_failed('a'))\n"
    "objectives.fail_all()\n"
    "rec('o1', objectives.is_failed('o1')); rec('o2', objectives.is_failed('o2')); rec('a', objectives.is_failed('a')); rec('b', objectives.is_failed('b'))\n"
    "on('destroyed', function(e) message('ev destroyed ' .. e.tag .. ' ' .. e.prop) end)\n"
    "on('picked_up', function(e) message('ev picked ' .. e.tag .. ' ' .. e.prop .. ' ' .. e.item .. ' ' .. e.player) end)\n"
    "on('door_refused', function(e) message('ev refused ' .. e.prop .. ' ' .. e.player) end)\n"
    "on('gadget_used', function(e) message('ev gadget ' .. e.item .. ' ' .. e.tag .. ' ' .. e.player) end)\n"
    "on('activated', function(e) message('ev activated ' .. e.prop .. ' ' .. e.player) end)\n"
    "mission.finish()\n";

static const char *kMore =
    "local function rec(k, v) message(k .. '=' .. tostring(v)) end\n"
    "local o, d, p, g = tag(7), door(5), player(1), chr(9)\n"
    "o:set_active(true); o:set_active(false); o:set_damage('explosions'); o:set_damage('none'); o:set_damage('all')\n"
    "o:set_hidden(true); g:set_hidden(true)\n"
    "rec('anim_obj', o:anim(1, { from = 12 })); rec('anim_bad', o:anim(7))\n"
    "rec('same1', o:same_room_as(p)); rec('same2', o:same_room_as(pad(2))); rec('same3', o:same_room_as(o)); rec('same4', o:same_room_as(player(0)))\n"
    "d:set_usable_by_guards(false); d:set_usable_by_guards(true)\n"
    "rec('health_p1', p:health()); rec('health_p0', player(0):health())\n"
    "p:kill()\n"
    "rec('facing_p', p:facing()); rec('facing_g', g:facing()); rec('facing_none', tag(7):facing()); rec('facing_unplaced', player(0):facing())\n"
    "local ex, ey, ez = p:eye(); rec('eye', ex .. ',' .. ey .. ',' .. ez)\n"
    "local nx, ny, nz = player(0):eye(); rec('eye_unplaced', tostring(nx) .. tostring(ny) .. tostring(nz))\n"
    "local gx, gy, gz = g:eye(); rec('eye_guard', tostring(gx) .. tostring(gy) .. tostring(gz))\n"
    "rec('give_obj', give_object(o)); give_object(o, 1)\n"
    "rec('iir', item_in_room(3, 'mypad')); rec('iir2', item_in_room(3, 12)); rec('iir3', item_in_room(4, 77))\n"
    "explosions_around_players(true); explosions_around_players(false)\n"
    "rec('players', mission.players())\n"
    "give(5, 1, { silent = true }); give(6)\n"
    "countdown.set(30); countdown.start(); countdown.set(10); rec('left', countdown.left())\n"
    "on('key_copied', function(e) message('ev key ' .. e.player) end)\n"
    "on('photographed', function(e) message('ev photo ' .. e.tag .. ' ' .. e.player) end)\n"
    "mission.killed_in_action()\n";

static const char *kStubs =
    "local function rec(k, v) message(k .. '=' .. tostring(v)) end\n"
    "hud.hide(true); hud.hide(false); countdown.set(5)\n"
    "rec('left', countdown.left()); rec('alarm', alarm.is_on()); rec('exiting', mission.exiting())\n"
    "rec('armour', player(0):armour()); rec('destroyed', tag(7):is_destroyed()); rec('room', chr(9):in_room_of(1))\n"
    "rec('pir', tostring(player_in_room(1))); rec('state', door(5):state())\n"
    "tag(7):set_invincible(true); chr(9):set_invincible(true); objectives.fail_all()\n"
    "tag(7):set_active(true); tag(7):set_damage('none'); tag(7):set_hidden(true); tag(7):same_room_as(player(0)); door(5):set_usable_by_guards(true)\n"
    "rec('facing', player(0):facing()); local sx, sy, sz = player(0):eye(); rec('eye', tostring(sx) .. tostring(sy) .. tostring(sz))\n"
    "rec('health', player(0):health()); player(0):kill(); rec('gobj', give_object(tag(7))); rec('iir', item_in_room(3, 12))\n"
    "explosions_around_players(true); mission.killed_in_action(); rec('players', mission.players())\n"
    "rec('done', 1)\n";

int main(void)
{
    static Mission m, m2;
    Fake f;
    MissionHostWorld w;
    MissionHost h;
    MissionRuntime *rt;
    char err[160] = "";

    GE_TEST_BEGIN("mission api world");
    MKDIR("tmp_worldtest");
    MKDIR("tmp_worldtest/scripts");
    {
        FILE *t = fopen("tmp_worldtest/scripts/.w", "wb");
        if (!t) { printf("SKIP world api checks: cannot write tmp_worldtest\n"); return GE_TEST_END(); }
        fclose(t);
        remove("tmp_worldtest/scripts/.w");
    }
    writeScript("worldall", kScript);
    writeScript("worldstubs", kStubs);
    writeScript("worldmore", kMore);

    missionInit(&m);
    GE_CHECK_EQ_INT(missionParse(
        "{ \"format\": 1, \"name\": \"t\", \"level\": \"bunker1\",\n"
        "  \"objectives\": [ { \"id\": \"o1\", \"text\": \"One\", \"difficulty\": 0 }, { \"id\": \"o2\", \"text\": \"Two\", \"difficulty\": 2 } ],\n"
        "  \"scripts\": [ { \"use\": \"worldall\", \"target\": { \"kind\": \"mission\" } } ] }", &m, err, sizeof err), 1);

    /* ---- a host with the whole domain ---- */
    memset(&f, 0, sizeof f);
    f.nplayers = 2;
    f.alarm = 1;
    w = worldHost(&f);
    h = host(&f, &w);
    rt = missionRtCreate(&m, &h, "tmp_worldtest", NULL, err, sizeof err);
    GE_CHECK(rt != NULL);
    if (!rt) { printf("%s\n", err); return GE_TEST_END(); }
    run(rt, 0.3);
    if (!hasMsg(&f, "state=closing")) for (int i = 0; i < f.nlog; i++) printf("  log: %s\n", f.logs[i]);

    /* objects (tag 7 is prop 3) */
    GE_CHECK(hasMsg(&f, "destroyed=true"));
    GE_CHECK(called(&f, "destroy(3);explode(3);obj_invincible(3,1);collectable(3,0);move_to(3,12);activate(3);"));
    GE_CHECK(called(&f, "screen(3,4,2);screen(3,6,0);"));      /* the screen index defaults to 0 */
    GE_CHECK(hasMsg(&f, "held=false"));                         /* any player: held_by(prop, -1) */
    GE_CHECK(called(&f, "held_by(3,-1);held_by(3,1);"));
    GE_CHECK(hasMsg(&f, "held1=true"));
    GE_CHECK(called(&f, "rotor(3,100,2);drive(3,5,50,1);"));
    /* doors */
    GE_CHECK(hasMsg(&f, "state=closing"));
    GE_CHECK(hasMsg(&f, "state_obj=closed"));                   /* not a door: the neutral "closed" */
    GE_CHECK(called(&f, "open_now(5);passable(5,0);"));
    /* players */
    GE_CHECK(hasMsg(&f, "armour=0.75"));
    GE_CHECK(hasMsg(&f, "held_item=17"));
    GE_CHECK(hasMsg(&f, "holds5=true"));
    GE_CHECK(hasMsg(&f, "holds6=false"));
    GE_CHECK(called(&f, "force_weapon(1,7);push(1,3,-4);player_invincible(1,1);"));
    /* set_invincible on an object went to the object verb, not the player's */
    GE_CHECK(!called(&f, "player_invincible(3"));
    /* rooms: a guard by chr, an object by prop, a player, a pad; a mission pad id is refused */
    GE_CHECK(called(&f, "in_room(0,9,12);in_room(1,3,12);in_room(2,1,12);in_room(3,13,12);"));
    GE_CHECK(hasMsg(&f, "room_chr=true"));
    GE_CHECK(hasMsg(&f, "room_obj=false"));
    GE_CHECK(hasMsg(&f, "room_pad=true"));
    GE_CHECK(hasMsg(&f, "room_str=false"));
    GE_CHECK(called(&f, "in_room(0,9,77);"));                   /* a mission pad's string id resolves through pad_by_id */
    GE_CHECK(hasMsg(&f, "room_mypad=true"));
    GE_CHECK(!called(&f, "in_room(2,1,-1)"));                   /* an unknown pad id never reaches the host */
    GE_CHECK(hasMsg(&f, "pir=1"));
    GE_CHECK(hasMsg(&f, "pir2=nil"));
    GE_CHECK(hasMsg(&f, "landed=stuck/7"));
    GE_CHECK(hasMsg(&f, "landed2=loose/nil"));
    GE_CHECK(hasMsg(&f, "landed3=nil/nil"));
    GE_CHECK(hasMsg(&f, "pad_kind=pad:3"));
    /* hud, countdown, sound, music, fog */
    GE_CHECK(called(&f, "hud_top(hi);hud_hide(1);"));
    GE_CHECK(hasMsg(&f, "low"));                                /* hud.bottom is message() */
    GE_CHECK(called(&f, "cd_set(30);cd_run(1);cd_run(0);cd_show(1);"));
    GE_CHECK(hasMsg(&f, "left=30"));
    GE_CHECK(called(&f, "sound_play(77,2,1,3);sound_play(78,-1,0,-1);sound_stop(2);sound_volume(2,1.00,1);"));   /* volume clamped to 0..1 */
    GE_CHECK(hasMsg(&f, "ch=2"));
    GE_CHECK(hasMsg(&f, "ch2=4"));
    GE_CHECK(called(&f, "music_play(1,5,20,0);music_play(2,0,0,0);music_stop(1);"));
    GE_CHECK(called(&f, "fog_next(1);"));
    /* alarm, mission */
    GE_CHECK(hasMsg(&f, "alarm=true"));
    GE_CHECK(hasMsg(&f, "alarm2=false"));
    GE_CHECK(hasMsg(&f, "exiting=false"));
    /* objectives: set_failed on / off, fail_all over the mission's own objectives and the script-added ones, of the difficulty */
    GE_CHECK(hasMsg(&f, "failed=true"));
    GE_CHECK(hasMsg(&f, "failed2=false"));
    GE_CHECK(hasMsg(&f, "o1=true"));
    GE_CHECK(hasMsg(&f, "o2=false"));                           /* difficulty 2 > the 0 being played */
    GE_CHECK(hasMsg(&f, "a=true"));
    GE_CHECK(hasMsg(&f, "b=false"));                            /* difficulty 3 */
    GE_CHECK_EQ_INT(missionRtObjectiveStatus(rt, 0), 2);
    GE_CHECK_EQ_INT(missionRtObjectiveStatus(rt, 1), 0);
    GE_CHECK_EQ_INT(missionRtObjectiveStatus(rt, 2), 2);
    GE_CHECK_EQ_INT(missionRtObjectiveStatus(rt, 3), 0);
    /* mission.finish with a failed objective: lost */
    GE_CHECK_EQ_INT(f.failed, 1);
    GE_CHECK_EQ_INT(f.won, 0);

    /* the events the host raises reach the scripts with their fields */
    {
        MissionEvField a[4];
        a[0].key = "tag"; a[0].str = NULL; a[0].num = 7;
        a[1].key = "prop"; a[1].str = NULL; a[1].num = 3;
        missionRtRaise(rt, "destroyed", a, 2);
        a[2].key = "item"; a[2].str = NULL; a[2].num = 21;
        a[3].key = "player"; a[3].str = NULL; a[3].num = 1;
        missionRtRaise(rt, "picked_up", a, 4);
        a[0].key = "prop"; a[0].num = 5; a[1].key = "tag"; a[1].num = 8; a[2].key = "player"; a[2].num = 0;
        missionRtRaise(rt, "door_refused", a, 3);
        a[0].key = "item"; a[0].num = 40; a[1].key = "tag"; a[1].num = 7; a[2].key = "player"; a[2].num = 0;
        missionRtRaise(rt, "gadget_used", a, 3);
        a[0].key = "prop"; a[0].num = 3; a[1].key = "player"; a[1].num = 1;
        missionRtRaise(rt, "activated", a, 2);
        run(rt, 0.1);
        GE_CHECK(hasMsg(&f, "ev destroyed 7 3"));
        GE_CHECK(hasMsg(&f, "ev picked 7 3 21 1"));
        GE_CHECK(hasMsg(&f, "ev refused 5 0"));
        GE_CHECK(hasMsg(&f, "ev gadget 40 7 0"));
        GE_CHECK(hasMsg(&f, "ev activated 3 1"));
    }
    missionRtDestroy(rt);

    /* mission.finish with every objective of the difficulty complete: won */
    {
        Mission mw;
        missionInit(&mw);
        writeScript("worldwin", "objectives.complete('o1'); mission.finish()\n");
        GE_CHECK_EQ_INT(missionParse(
            "{ \"format\": 1, \"name\": \"t\", \"level\": \"bunker1\",\n"
            "  \"objectives\": [ { \"id\": \"o1\", \"text\": \"One\", \"difficulty\": 0 }, { \"id\": \"o2\", \"text\": \"Two\", \"difficulty\": 2 } ],\n"
            "  \"scripts\": [ { \"use\": \"worldwin\", \"target\": { \"kind\": \"mission\" } } ] }", &mw, err, sizeof err), 1);
        memset(&f, 0, sizeof f);
        f.nplayers = 1;
        w = worldHost(&f);
        h = host(&f, &w);
        rt = missionRtCreate(&mw, &h, "tmp_worldtest", NULL, err, sizeof err);
        GE_CHECK(rt != NULL);
        run(rt, 0.2);
        GE_CHECK_EQ_INT(f.won, 1);
        GE_CHECK_EQ_INT(f.failed, 0);
        missionRtDestroy(rt);
        missionFree(&mw);
    }

    /* ---- the second pass: object / door / player verbs, globals, mission table, events ---- */
    {
        Mission mm;
        missionInit(&mm);
        GE_CHECK_EQ_INT(missionParse(
            "{ \"format\": 1, \"name\": \"t\", \"level\": \"bunker1\",\n"
            "  \"scripts\": [ { \"use\": \"worldmore\", \"target\": { \"kind\": \"mission\" } } ] }", &mm, err, sizeof err), 1);
        memset(&f, 0, sizeof f);
        f.nplayers = 3;
        fNGiven = 0;
        w = worldHost(&f);
        h = host(&f, &w);
        rt = missionRtCreate(&mm, &h, "tmp_worldtest", NULL, err, sizeof err);
        GE_CHECK(rt != NULL);
        run(rt, 0.3);
        if (!called(&f, "kia;")) for (int i = 0; i < f.nlog; i++) printf("  log: %s\n", f.logs[i]);
        GE_CHECK(called(&f, "active(3,1);active(3,0);damage(3,1);damage(3,2);damage(3,0);"));
        GE_CHECK(called(&f, "hidden(3,1);"));
        GE_CHECK(!called(&f, "hidden(6"));                        /* a guard goes to the guard domain, never to the object verb */
        GE_CHECK_EQ_INT(countLog(&f, "set_hidden: not implemented yet"), 1);   /* the guard domain's stub answered (no guard host here) */
        GE_CHECK(called(&f, "oanim(3,1,12,-1,1.0,-1.0);"));
        GE_CHECK(hasMsg(&f, "anim_obj=true"));
        GE_CHECK(hasMsg(&f, "anim_bad=false"));                   /* the host refused animation 7 */
        GE_CHECK(called(&f, "same_room(1,3,2,1);same_room(1,3,3,2);same_room(1,3,1,3);same_room(1,3,2,0);"));
        GE_CHECK(hasMsg(&f, "same1=false") && hasMsg(&f, "same2=false") && hasMsg(&f, "same3=true"));
        GE_CHECK(called(&f, "door_guards(5,0);door_guards(5,1);"));
        GE_CHECK(hasMsg(&f, "health_p1=0.5"));
        GE_CHECK(hasMsg(&f, "health_p0=1"));
        GE_CHECK(called(&f, "kill(1);"));
        GE_CHECK(hasMsg(&f, "facing_p=30") && hasMsg(&f, "facing_g=-170"));
        GE_CHECK(hasMsg(&f, "facing_none=0") && hasMsg(&f, "facing_unplaced=0"));
        GE_CHECK(hasMsg(&f, "eye=1,175,-3"));
        GE_CHECK(hasMsg(&f, "eye_unplaced=nilnilnil") && hasMsg(&f, "eye_guard=nilnilnil"));
        GE_CHECK(hasMsg(&f, "give_obj=true"));
        GE_CHECK(called(&f, "give_object(3,-1);give_object(3,1);"));
        GE_CHECK(hasMsg(&f, "iir=true") && hasMsg(&f, "iir2=false") && hasMsg(&f, "iir3=false"));
        GE_CHECK(called(&f, "item_in_room(3,77);item_in_room(3,12);item_in_room(4,77);"));
        GE_CHECK(called(&f, "explosions(1);explosions(0);"));
        GE_CHECK(hasMsg(&f, "players=3"));
        GE_CHECK_EQ_INT(fNGiven, 2);                              /* the original give ran for both, silent or not */
        GE_CHECK(fGiven[0] == 1 && fGiven[1] == 5 && fGiven[2] == -1 && fGiven[3] == 6);
        GE_CHECK(called(&f, "cd_set(30);cd_run(1);cd_set(10);"));   /* set() on a running countdown never stops it */
        GE_CHECK(!called(&f, "cd_run(0)"));
        GE_CHECK(hasMsg(&f, "left=10"));
        GE_CHECK(called(&f, "kia;"));
        GE_CHECK_EQ_INT(f.failed, 1);                             /* killed in action ends the level lost */
        {
            MissionEvField a[2];
            a[0].key = "player"; a[0].str = NULL; a[0].num = 1;
            missionRtRaise(rt, "key_copied", a, 1);
            a[0].key = "tag"; a[0].num = 9; a[1].key = "player"; a[1].str = NULL; a[1].num = 0;
            missionRtRaise(rt, "photographed", a, 2);
            run(rt, 0.1);
            GE_CHECK(hasMsg(&f, "ev key 1"));
            GE_CHECK(hasMsg(&f, "ev photo 9 0"));
        }
        missionRtDestroy(rt);
        missionFree(&mm);
    }

    /* ---- a host without the domain: every verb answers its neutral value and logs once ---- */
    missionInit(&m2);
    GE_CHECK_EQ_INT(missionParse(
        "{ \"format\": 1, \"name\": \"t\", \"level\": \"bunker1\",\n"
        "  \"objectives\": [ { \"id\": \"o1\", \"text\": \"One\", \"difficulty\": 0 } ],\n"
        "  \"scripts\": [ { \"use\": \"worldstubs\", \"target\": { \"kind\": \"mission\" } } ] }", &m2, err, sizeof err), 1);
    memset(&f, 0, sizeof f);
    f.nplayers = 1;
    h = host(&f, NULL);
    rt = missionRtCreate(&m2, &h, "tmp_worldtest", NULL, err, sizeof err);
    GE_CHECK(rt != NULL);
    run(rt, 0.2);
    GE_CHECK(hasMsg(&f, "done=1"));                             /* nothing raised an error */
    GE_CHECK(hasMsg(&f, "left=0"));
    GE_CHECK(hasMsg(&f, "alarm=false"));
    GE_CHECK(hasMsg(&f, "exiting=false"));
    GE_CHECK(hasMsg(&f, "armour=0"));
    GE_CHECK(hasMsg(&f, "destroyed=false"));
    GE_CHECK(hasMsg(&f, "room=false"));
    GE_CHECK(hasMsg(&f, "pir=nil"));
    GE_CHECK(hasMsg(&f, "state=closed"));
    GE_CHECK(hasMsg(&f, "health=0"));
    GE_CHECK(hasMsg(&f, "facing=0") && hasMsg(&f, "eye=nilnilnil"));
    GE_CHECK_EQ_INT(countLog(&f, "Target:facing: not implemented yet"), 1);
    GE_CHECK(hasMsg(&f, "gobj=false"));
    GE_CHECK(hasMsg(&f, "iir=false"));
    GE_CHECK(hasMsg(&f, "players=1"));
    GE_CHECK_EQ_INT(f.failed, 0);                               /* no KIA host: killed_in_action does nothing */
    GE_CHECK_EQ_INT(countLog(&f, "mission.killed_in_action: not implemented yet"), 1);
    GE_CHECK_EQ_INT(countLog(&f, "hud.hide: not implemented yet"), 1);   /* called twice, logged once */
    GE_CHECK_EQ_INT(countLog(&f, "countdown.set: not implemented yet"), 1);
    GE_CHECK_EQ_INT(missionRtObjectiveStatus(rt, 0), 0);        /* fail_all without a host list: the stub, nothing failed */
    missionRtDestroy(rt);

    missionFree(&m);
    missionFree(&m2);
    return GE_TEST_END();
}
