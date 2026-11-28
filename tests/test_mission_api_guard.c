/* The guard domain of the campaign API (#165, port/mission/mission_lua_guard.cpp) driven by a fake host: argument parsing,
 * return values, target-by-chr resolution (setup guards, spawned and cloned ones), the events and the "not implemented
 * yet" stubs. The game side (port/src/missions_host_guard.c) is exercised in the game by the lead. */
#include "ge_test.h"
#include "mission.h"
#include "mission_lua.h"
#include "mission_host_guard.h"

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
    char calls[256][112];       /* "name chr args" per host call */
    int ncalls;
    char msgs[256][112];
    int nmsg;
    char logs[64][160];
    int nlog;
    int propAi[4];              /* MissionHost.guard_set_ai: prop, list */
    int nPropAi;
} Fake;

static Fake f;

static void rec(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    if (f.ncalls < 256) vsnprintf(f.calls[f.ncalls++], sizeof f.calls[0], fmt, ap);
    va_end(ap);
}
static int called(const char *s)
{
    for (int i = 0; i < f.ncalls; i++) if (!strcmp(f.calls[i], s)) return 1;
    return 0;
}
static int hasMsg(const char *s)
{
    for (int i = 0; i < f.nmsg; i++) if (!strcmp(f.msgs[i], s)) return 1;
    return 0;
}
static int countLog(const char *needle)
{
    int n = 0;
    for (int i = 0; i < f.nlog; i++) if (strstr(f.logs[i], needle)) n++;
    return n;
}

static void fLog(void *u, const char *l) { (void)u; if (f.nlog < 64) snprintf(f.logs[f.nlog++], 160, "%s", l); }
static void fMsg(void *u, const char *t) { (void)u; if (f.nmsg < 256) snprintf(f.msgs[f.nmsg++], 112, "%s", t); }
static int  fPadId(void *u, const char *id) { (void)u; return !strcmp(id, "p1") ? 200 : -1; }
static int  fPc(void *u) { (void)u; return 1; }
static int  fChrProp(void *u, int chr) { (void)u; return chr == 7 ? 3 : -1; }
static int  fPropChr(void *u, int prop) { (void)u; return prop == 3 ? 7 : -1; }
static void fPropSetAi(void *u, int prop, int list) { (void)u; if (f.nPropAi < 2) { f.propAi[f.nPropAi * 2] = prop; f.propAi[f.nPropAi * 2 + 1] = list; f.nPropAi++; } }

/* ---- the guard host */
static int  gExists(void *u, int c) { (void)u; return c == 7 || c == 5000; }
static int  gDying(void *u, int c) { (void)u; return c == 7; }
static void gRemove(void *u, int c) { (void)u; rec("remove %d", c); }
static void gFade(void *u, int c) { (void)u; rec("fade_out %d", c); }
static int  gTele(void *u, int c, int pad) { (void)u; rec("teleport %d %d", c, pad); return pad == 100; }
static void gHidden(void *u, int c, int on) { (void)u; rec("set_hidden %d %d", c, on); }
static int  gSpawn(void *u, const MissionGuardSpawn *s)
{
    (void)u;
    rec("spawn chr=%d pad=%d body=%d head=%d force=%d sg=%d", s->chr, s->pad, s->body, s->head, s->force, s->sunglasses);
    return s->chr >= 0 ? s->chr : 5001;
}
static int  gSetAi(void *u, int c, int list) { (void)u; rec("set_ai %d %d", c, list); return c != 99; }
static int  gSetupAi(void *u, int c) { (void)u; return c == 7 ? 1042 : -1; }
static int  gAll(void *u, int *props, int *chrs, int max) { (void)u; (void)max; props[0] = 3; chrs[0] = 7; props[1] = 4; chrs[1] = 8; return 2; }
static int  gPadById(void *u, const char *id) { (void)u; return !strcmp(id, "p1") ? 200 : -1; }
static void gStats(void *u, int c, const MissionGuardStats *s)
{
    (void)u;
    rec("set_stats %d has=%x health=%g max=%g armour=%g speed=%g flinch=%d raw=%d add=%g hear=%g vis=%g rec=%g", c, s->has,
        s->health, s->max_health, s->armour, s->speed, s->flinch, s->raw, s->add_armour, s->hearing, s->vision, s->recovery);
}
static void gSetFlag(void *u, int c, int fl, int on) { (void)u; rec("set_flag %d %d %d", c, fl, on); }
static int  gGetFlag(void *u, int c, int fl) { (void)u; (void)c; return fl == MG_FLAG_CIVILIAN; }
static int  gWeapon(void *u, int c, int item, int model, int left, int conc) { (void)u; rec("give_weapon %d %d %d %d %d", c, item, model, left, conc); return 1; }
static int  gHat(void *u, int c, int model) { (void)u; rec("give_hat %d %d", c, model); return 1; }
static void gDrop(void *u, int c) { (void)u; rec("drop_items %d", c); }
static void gRemHeld(void *u, int c) { (void)u; rec("remove_held_items %d", c); }
static int  gPreset(void *u, int c) { (void)u; return c == 7 ? 77 : -1; }
static void gPatrol(void *u, int c, int path) { (void)u; rec("patrol %d %d", c, path); }
static int  gPatrolling(void *u, int c) { (void)u; return c == 7; }
static int  gMoving(void *u, int c) { (void)u; return c == 5000; }
static int  gRunTo(void *u, int c, int pad, int sp) { (void)u; rec("run_to %d %d %d", c, pad, sp); return pad == 100; }
static int  gRunToP(void *u, int c, int sp, int pl) { (void)u; rec("run_to_player %d %d %d", c, sp, pl); return 1; }
static void gFaceP(void *u, int c, int pl) { (void)u; rec("face_player %d %d", c, pl); }
static void gFacePad(void *u, int c, int pad) { (void)u; rec("face_pad %d %d", c, pad); }
static void gHalt(void *u, int c) { (void)u; rec("halt %d", c); }
static int  gFlee(void *u, int c) { (void)u; (void)c; return -1; }
static void gLook(void *u, int c) { (void)u; rec("look_around %d", c); }
static void gPoint(void *u, int c) { (void)u; rec("point_at_player %d", c); }
static void gArms(void *u, int c) { (void)u; rec("raise_arms %d", c); }
static void gKneel(void *u, int c) { (void)u; rec("kneel %d", c); }
static void gIdle(void *u, int c) { (void)u; rec("idle_anim %d", c); }
static int  gAnim(void *u, int c, int id, const char *name, const MissionGuardAnim *o)
{
    (void)u;
    rec("anim %d id=%d name=%s from=%d to=%d speed=%g hold=%d rev=%d loop=%d nomove=%d blend=%g idle=%d snd=%d int=%d", c, id,
        name ? name : "-", o->from, o->to, o->speed, o->hold, o->reverse, o->loop, o->no_move, o->blend, o->idle_after, o->sound,
        o->interruptible);
    return 1;
}
static int  gAnimDone(void *u, int c) { (void)u; return c == 7; }
static void gStopAnim(void *u, int c) { (void)u; rec("stop_anim %d", c); }
static int  gFire(void *u, int c, int kind, int target, int kneel, int aim) { (void)u; rec("fire_at %d kind=%d target=%d kneel=%d aim=%d", c, kind, target, kneel, aim); return 1; }
static int  gMove(void *u, int c, int move, int pl) { (void)u; rec("combat_move %d %d %d", c, move, pl); return move == MG_MOVE_SIDESTEP; }
static int  gGrenade(void *u, int c, int pl) { (void)u; (void)c; (void)pl; return 1; }
static int  gGChance(void *u, int c) { (void)u; (void)c; return 40; }
static void gHit(void *u, int c, int part, int item) { (void)u; rec("hit %d %d %d", c, part, item); }
static int  gBusy(void *u, int c) { (void)u; return c == 5000; }
static int  gIdleQ(void *u, int c) { (void)u; return c == 7; }
static int  gClone(void *u, int c) { (void)u; rec("clone %d", c); return 10007; }
static int  gAlarm(void *u, int c, int pad) { (void)u; rec("press_alarm %d %d", c, pad); return 1; }
static int  gSpotted(void *u, int c) { (void)u; return c == 7 ? 1 : -1; }
static int  gLine(void *u, int c) { (void)u; (void)c; return -1; }
static int  gOnScreen(void *u, int c) { (void)u; return c == 7; }
static int  gSeconds(void *u, int c, double s) { (void)u; rec("within %d %g", c, s); return s >= 5; }
static int  gNear(void *u, int c) { (void)u; (void)c; return 3; }
static int  gShotAt(void *u, int c) { (void)u; return c == 7; }
static int  gWitness(void *u, int c, int *other) { (void)u; if (c != 7) return MG_SAW_NONE; *other = 8; return MG_SAW_DEATH; }
static double gAngle(void *u, int c, int pl) { (void)u; (void)c; (void)pl; return -45.5; }
static int  gDamaged(void *u, int c) { (void)u; return c == 7; }
static int  gCivs(void *u) { (void)u; return 2; }

static int  gHeld(void *u, int c) { (void)u; return c == 7 ? 17 : -1; }
static int  gSpot(void *u, int c, int pl) { (void)u; rec("spot_near_player %d %d", c, pl); return c == 7 ? 321 : -1; }
static int  gDropGren(void *u, int c) { (void)u; rec("drop_grenade %d", c); return c == 7; }
static int  gDropObj(void *u, int c, int prop) { (void)u; rec("drop_object %d %d", c, prop); return prop == 3; }
static int  gGiveObj(void *u, int c, int prop) { (void)u; rec("give_object %d %d", c, prop); return 1; }
static void gFlagRaw(void *u, int c, unsigned bits, int on) { (void)u; rec("set_flag_raw %d %u %d", c, bits, on); }

static MissionHostGuard fullGuard(void)
{
    MissionHostGuard g;
    memset(&g, 0, sizeof g);
    g.exists = gExists; g.is_dying = gDying; g.chr_remove = gRemove; g.fade_out = gFade; g.teleport = gTele;
    g.set_hidden = gHidden; g.spawn = gSpawn; g.set_ai = gSetAi; g.setup_ai = gSetupAi; g.all_guards = gAll;
    g.pad_by_id = gPadById; g.set_stats = gStats; g.set_flag = gSetFlag; g.get_flag = gGetFlag; g.give_weapon = gWeapon;
    g.give_hat = gHat; g.drop_items = gDrop; g.remove_held_items = gRemHeld; g.preset_pad = gPreset; g.patrol = gPatrol;
    g.is_patrolling = gPatrolling; g.is_moving = gMoving; g.run_to = gRunTo; g.run_to_player = gRunToP; g.face_player = gFaceP;
    g.face_pad = gFacePad; g.halt = gHalt; g.flee_pad = gFlee; g.cover_pad = gFlee; g.look_around = gLook;
    g.point_at_player = gPoint; g.raise_arms = gArms; g.kneel = gKneel; g.idle_anim = gIdle; g.anim = gAnim;
    g.anim_done = gAnimDone; g.stop_anim = gStopAnim; g.fire_at = gFire; g.combat_move = gMove; g.throw_grenade = gGrenade;
    g.grenade_chance = gGChance; g.hit = gHit; g.is_busy = gBusy; g.is_idle = gIdleQ; g.clone = gClone; g.press_alarm = gAlarm;
    g.spotted_player = gSpotted; g.line_to_player = gLine; g.on_screen = gOnScreen; g.was_seen = gOnScreen;
    g.is_aimed_at = gOnScreen; g.alerted_within = gSeconds; g.heard_shot_within = gSeconds; g.near_misses = gNear;
    g.was_shot_at = gShotAt; g.witnessed = gWitness; g.angle_to_player = gAngle; g.was_damaged = gDamaged;
    g.civilians_killed = gCivs;
    g.held_weapon = gHeld; g.spot_near_player = gSpot; g.drop_grenade = gDropGren; g.drop_object = gDropObj;
    g.give_object = gGiveObj; g.set_flag_raw = gFlagRaw;
    return g;
}

static MissionHost makeHost(const MissionHostGuard *g)
{
    MissionHost h;
    memset(&h, 0, sizeof h);
    h.ud = &f; h.log = fLog; h.message = fMsg; h.player_count = fPc; h.chr_prop = fChrProp; h.prop_chr = fPropChr;
    h.guard_set_ai = fPropSetAi; h.pad_by_id = fPadId;
    h.guard_ext = g;
    return h;
}

static void run(MissionRuntime *rt, double secs)
{
    for (double t = 0; t < secs; t += 1.0 / 60.0) missionRtTick(rt, 1.0 / 60.0);
}

static void writeScript(const char *name, const char *src)
{
    char path[128];
    FILE *fp;
    snprintf(path, sizeof path, "tmp_guardtest/scripts/%s.luau", name);
    fp = fopen(path, "wb");
    if (fp) { fputs(src, fp); fclose(fp); }
}

static MissionRuntime *start(const char *script, const MissionHostGuard *g)
{
    static Mission m;
    static char json[256];
    MissionHost h = makeHost(g);
    char err[160] = "";
    missionInit(&m);
    snprintf(json, sizeof json, "{ \"format\": 1, \"name\": \"t\", \"level\": \"bunker1\", \"scripts\": [ "
             "{ \"use\": \"%s\", \"target\": { \"kind\": \"mission\" } } ] }", script);
    GE_CHECK_EQ_INT(missionParse(json, &m, err, sizeof err), 1);
    memset(&f, 0, sizeof f);
    return missionRtCreate(&m, &h, "tmp_guardtest", NULL, err, sizeof err);
}

int main(void)
{
    MissionHostGuard full = fullGuard(), none;
    MissionRuntime *rt;

    GE_TEST_BEGIN("mission api guard");
    MKDIR("tmp_guardtest");
    MKDIR("tmp_guardtest/scripts");
    {   /* a read-only working directory: skip */
        FILE *t = fopen("tmp_guardtest/scripts/.w", "wb");
        if (!t) { printf("SKIP: cannot write tmp_guardtest\n"); return GE_TEST_END(); }
        fclose(t);
        remove("tmp_guardtest/scripts/.w");
    }

    /* ---- presence, settings, movement: by chr number, by setup prop, and a chr with no setup record */
    writeScript("g1",
        "local g = chr(7)\n"
        "local s = chr(5000)\n"
        "message('exists ' .. tostring(g:exists()) .. tostring(s:exists()) .. tostring(chr(9):exists()))\n"
        "message('dying ' .. tostring(g:is_dying()))\n"
        "message('byprop ' .. tostring(guard(3):exists()) .. tostring(guard(4):exists()))\n"   /* prop 3 -> chr 7; prop 4 has none */
        "g:remove() g:fade_out() g:set_hidden(true)\n"
        "message('tele ' .. tostring(g:teleport(100)) .. tostring(g:teleport(101)) .. tostring(g:teleport('p1')) .. tostring(g:teleport('nope')))\n"
        "message('setupai ' .. tostring(g:setup_ai()) .. ' ' .. tostring(s:setup_ai()))\n"
        "g:set_stats({ health = 40, max_health = 80, armour = 5, speed = 70, flinch = false, raw = true })\n"
        "g:set_flag('invincible', true) g:set_flag('dormant', false)\n"
        "message('flag ' .. tostring(g:get_flag('civilian')) .. tostring(g:get_flag('no_blood')))\n"
        "message('weapon ' .. tostring(g:give_weapon(17, { model = 5, left_hand = true, concealed = true })) .. tostring(g:give_hat(12)))\n"
        "g:give_weapon(8) g:drop_items() g:remove_held_items()\n"
        "message('preset ' .. tostring(g:preset_pad()) .. ' ' .. tostring(s:preset_pad()))\n"
        "g:patrol(4)\n"
        "message('patrolling ' .. tostring(g:is_patrolling()) .. ' moving ' .. tostring(s:is_moving()) .. tostring(g:is_moving()))\n"
        "message('runto ' .. tostring(g:run_to(100)) .. tostring(g:run_to(101, 'walk')) .. tostring(g:run_to('p1', 'sprint')))\n"
        "message('runtoplayer ' .. tostring(g:run_to_player('walk', 1)))\n"
        "g:run_to_player()\n"
        "g:face_player() g:face_pad(55) g:halt()\n"
        "message('flee ' .. tostring(g:flee_pad()) .. tostring(g:cover_pad()))\n"
        "s:set_ai(1) chr(99):set_ai(2) guard(3):set_ai(4) door(77):set_ai(5)\n"
        "message('done')\n");
    rt = start("g1", &full);
    GE_CHECK(rt != NULL);
    if (!rt) return GE_TEST_END();
    run(rt, 0.3);
    GE_CHECK(hasMsg("exists truetruefalse"));
    GE_CHECK(hasMsg("dying true"));
    GE_CHECK(hasMsg("byprop truefalse"));
    GE_CHECK(called("remove 7") && called("fade_out 7") && called("set_hidden 7 1"));
    GE_CHECK(hasMsg("tele truefalsefalsefalse"));              /* 100 ok; 101 blocked; 'p1' -> pad 200 blocked; unknown id: no call */
    GE_CHECK(called("teleport 7 100") && called("teleport 7 101") && called("teleport 7 200"));
    GE_CHECK(hasMsg("setupai 1042 nil"));
    GE_CHECK(called("set_stats 7 has=8f health=40 max=80 armour=5 speed=70 flinch=0 raw=1 add=0 hear=0 vis=0 rec=0"));
    GE_CHECK(called("set_flag 7 0 1") && called("set_flag 7 4 0"));
    GE_CHECK(hasMsg("flag truefalse"));
    GE_CHECK(hasMsg("weapon truetrue"));
    GE_CHECK(called("give_weapon 7 17 5 1 1") && called("give_weapon 7 8 -1 0 0") && called("give_hat 7 12"));
    GE_CHECK(called("drop_items 7") && called("remove_held_items 7"));
    GE_CHECK(hasMsg("preset 77 nil"));
    GE_CHECK(called("patrol 7 4"));
    GE_CHECK(hasMsg("patrolling true moving truefalse"));      /* is_moving by chr number: the spawned one, not the setup one */
    GE_CHECK(called("run_to 7 100 1") && called("run_to 7 101 0") && called("run_to 7 200 2"));
    GE_CHECK(hasMsg("runto truefalsefalse"));
    GE_CHECK(called("run_to_player 7 0 1") && called("run_to_player 7 1 -1"));
    GE_CHECK(called("face_player 7 -1") && called("face_pad 7 55") && called("halt 7"));
    GE_CHECK(hasMsg("flee nilnil"));
    GE_CHECK(called("set_ai 5000 1") && called("set_ai 99 2") && called("set_ai 7 4"));
    GE_CHECK_EQ_INT(f.nPropAi, 1);                              /* chr 99 has no chr: the setup-prop path; door(77) has neither */
    GE_CHECK(hasMsg("done"));
    missionRtDestroy(rt);

    /* ---- gestures, combat, perception */
    writeScript("g2",
        "local g = chr(7)\n"
        "g:look_around() g:point_at_player() g:raise_arms() g:kneel() g:idle_anim() g:stop_anim()\n"
        "message('anim ' .. tostring(g:anim(77, { from = 3, to = 20, speed = 2, hold = true, move = false, blend = 0.5, sound = 9, interruptible = true, idle_after = false })))\n"
        "g:anim('kneel_cough')\n"
        "message('animdone ' .. tostring(g:anim_done()) .. tostring(chr(5000):anim_done()))\n"
        "message('fire ' .. tostring(g:fire_at()) .. tostring(g:fire_at(player(1))) .. tostring(g:fire_at(chr(8), { stance = 'kneel', aim_only = true })) .. tostring(g:fire_at({ kind = 'pad', pad = 12 })))\n"
        "message('fire2 ' .. tostring(g:fire_at(guard(99))))\n"
        "message('move ' .. tostring(g:combat_move('sidestep')) .. tostring(g:combat_move('roll_fire')))\n"
        "message('grenade ' .. tostring(g:throw_grenade()) .. ' ' .. g:grenade_chance())\n"
        "g:hit(3, 10)\n"
        "message('state ' .. tostring(g:is_busy()) .. tostring(chr(5000):is_busy()) .. tostring(g:is_idle()))\n"
        "local c = g:clone()\n"
        "message('clone ' .. tostring(c.chr) .. ' ' .. tostring(c.prop) .. ' ' .. tostring(c:exists()))\n"
        "message('alarm ' .. tostring(g:press_alarm(66)))\n"
        "message('spotted ' .. tostring(g:spotted_player()) .. ' ' .. tostring(chr(5000):spotted_player()) .. ' line ' .. tostring(g:line_to_player()))\n"
        "message('see ' .. tostring(g:on_screen()) .. tostring(g:was_seen()) .. tostring(g:is_aimed_at()) .. tostring(chr(5000):on_screen()))\n"
        "message('within ' .. tostring(g:alerted_within(2)) .. tostring(g:alerted_within(9)) .. tostring(g:heard_shot_within(7)))\n"
        "message('near ' .. g:near_misses() .. ' ' .. tostring(g:was_shot_at()) .. tostring(chr(5000):was_shot_at()))\n"
        "local what, who = g:witnessed()\n"
        "message('witness ' .. tostring(what) .. ' ' .. tostring(who.chr) .. ' ' .. tostring(chr(5000):witnessed()))\n"
        "message('angle ' .. g:angle_to_player() .. ' damaged ' .. tostring(g:was_damaged()))\n"
        "message('civs ' .. mission.civilians_killed() .. ' ' .. tostring(type(mission.win)))\n"
        "message('spawn ' .. tostring(spawn_guard({ pad = 12, body = 3 }).chr) .. ' ' .. tostring(spawn_guard({ chr = 8000, pad = 'p1', body = 3, head = 4, force = true, sunglasses = true }).chr))\n"
        "message('spawnbad ' .. tostring(spawn_guard({ pad = 'nope', body = 3 })))\n"
        "local all = all_guards()\n"
        "message('all ' .. #all .. ' ' .. all[1].chr .. ' ' .. all[1].prop .. ' ' .. all[2].chr .. ' ' .. all[2].prop)\n"
        "message('done')\n");
    rt = start("g2", &full);
    GE_CHECK(rt != NULL);
    if (!rt) return GE_TEST_END();
    run(rt, 0.3);
    GE_CHECK(called("look_around 7") && called("point_at_player 7") && called("raise_arms 7") && called("kneel 7"));
    GE_CHECK(called("idle_anim 7") && called("stop_anim 7"));
    GE_CHECK(hasMsg("anim true"));
    GE_CHECK(called("anim 7 id=77 name=- from=3 to=20 speed=2 hold=1 rev=0 loop=0 nomove=1 blend=0.5 idle=0 snd=9 int=1"));
    GE_CHECK(called("anim 7 id=-1 name=kneel_cough from=-1 to=-1 speed=0 hold=0 rev=0 loop=0 nomove=0 blend=-1 idle=-1 snd=-1 int=0"));
    GE_CHECK(hasMsg("animdone truefalse"));
    GE_CHECK(hasMsg("fire truetruetruetrue"));
    GE_CHECK(called("fire_at 7 kind=0 target=-1 kneel=0 aim=0") && called("fire_at 7 kind=0 target=1 kneel=0 aim=0"));
    GE_CHECK(called("fire_at 7 kind=1 target=8 kneel=1 aim=1") && called("fire_at 7 kind=2 target=12 kneel=0 aim=0"));
    GE_CHECK(hasMsg("fire2 false"));                          /* guard(99): no chr */
    GE_CHECK(hasMsg("move truefalse"));
    GE_CHECK(hasMsg("grenade true 40"));
    GE_CHECK(called("hit 7 3 10"));
    GE_CHECK(hasMsg("state falsetruetrue"));
    GE_CHECK(called("clone 7"));
    GE_CHECK(hasMsg("clone 10007 nil false"));                 /* the clone has a chr and no setup prop */
    GE_CHECK(hasMsg("alarm true") && called("press_alarm 7 66"));
    GE_CHECK(hasMsg("spotted 1 nil line nil"));
    GE_CHECK(hasMsg("see truetruetruefalse"));
    GE_CHECK(hasMsg("within falsetruetrue") && called("within 7 2") && called("within 7 9"));
    GE_CHECK(hasMsg("near 3 truefalse"));
    GE_CHECK(hasMsg("witness death 8 nil"));
    GE_CHECK(hasMsg("angle -45.5 damaged true"));
    GE_CHECK(hasMsg("civs 2 function"));                       /* the mission table kept win / fail / difficulty */
    GE_CHECK(hasMsg("spawn 5001 8000"));
    GE_CHECK(called("spawn chr=-1 pad=12 body=3 head=-1 force=0 sg=0"));
    GE_CHECK(called("spawn chr=8000 pad=200 body=3 head=4 force=1 sg=1"));
    GE_CHECK(hasMsg("spawnbad nil"));
    GE_CHECK(hasMsg("all 2 7 3 8 4"));
    GE_CHECK(hasMsg("done"));
    missionRtDestroy(rt);

    /* ---- the events carry a chr: wait(event('anim_end', { chr = 7 })) and on('guard_damaged', ...) */
    writeScript("g3",
        "spawn(function()\n"
        "  local e = wait(event('anim_end', { chr = 7 }))\n"
        "  message('anim_end ' .. e.chr)\n"
        "end)\n"
        "on('guard_damaged', function(e) message('damaged ' .. e.chr .. ' ' .. tostring(e.prop)) end, { chr = 5001 })\n");
    rt = start("g3", &full);
    GE_CHECK(rt != NULL);
    if (!rt) return GE_TEST_END();
    run(rt, 0.2);
    {
        MissionEvField a[1] = { { "chr", NULL, 8 } };
        MissionEvField b[2] = { { "chr", NULL, 5001 }, { "prop", NULL, 4 } };
        MissionEvField c[2] = { { "chr", NULL, 5001 }, { "prop", NULL, 4 } };
        missionRtRaise(rt, "anim_end", a, 1);                  /* another guard: no */
        run(rt, 0.2);
        GE_CHECK(!hasMsg("anim_end 8"));
        a[0].num = 7;
        missionRtRaise(rt, "anim_end", a, 1);
        missionRtRaise(rt, "guard_damaged", b, 2);
        c[0].num = 3;
        missionRtRaise(rt, "guard_damaged", c, 2);             /* chr 3: filtered out */
        run(rt, 0.2);
        GE_CHECK(hasMsg("anim_end 7"));
        GE_CHECK(hasMsg("damaged 5001 4"));
        GE_CHECK(!hasMsg("damaged 3 4"));
    }
    missionRtDestroy(rt);

    /* ---- the perception events: guard_spotted / guard_heard / guard_near_miss {chr, prop?, player}, guard_witnessed {chr, prop?, what, other} */
    writeScript("g5",
        "on('guard_spotted', function(e) message('spotted ' .. e.chr .. ' ' .. tostring(e.prop) .. ' ' .. e.player) end)\n"
        "on('guard_heard', function(e) message('heard ' .. e.chr .. ' ' .. e.player) end, { chr = 5001 })\n"
        "on('guard_near_miss', function(e) message('miss ' .. e.chr .. ' ' .. e.player) end)\n"
        "on('guard_witnessed', function(e) message('saw ' .. e.chr .. ' ' .. e.what .. ' ' .. e.other) end, { what = 'death' })\n");
    rt = start("g5", &full);
    GE_CHECK(rt != NULL);
    if (!rt) return GE_TEST_END();
    run(rt, 0.2);
    {
        MissionEvField sp[3] = { { "chr", NULL, 7 }, { "prop", NULL, 3 }, { "player", NULL, 1 } };
        MissionEvField sp2[2] = { { "chr", NULL, 5001 }, { "player", NULL, 0 } };      /* a spawned guard: no prop field */
        MissionEvField heard[2] = { { "chr", NULL, 5001 }, { "player", NULL, 0 } };
        MissionEvField heard2[2] = { { "chr", NULL, 7 }, { "player", NULL, 0 } };
        MissionEvField w[4] = { { "chr", NULL, 7 }, { "prop", NULL, 3 }, { "what", "death", 0 }, { "other", NULL, 8 } };
        MissionEvField w2[3] = { { "chr", NULL, 9 }, { "what", "hit", 0 }, { "other", NULL, 8 } };
        missionRtRaise(rt, "guard_spotted", sp, 3);
        missionRtRaise(rt, "guard_spotted", sp2, 2);
        missionRtRaise(rt, "guard_heard", heard, 2);
        missionRtRaise(rt, "guard_heard", heard2, 2);               /* chr 7: filtered out */
        missionRtRaise(rt, "guard_near_miss", sp2, 2);
        missionRtRaise(rt, "guard_witnessed", w, 4);
        missionRtRaise(rt, "guard_witnessed", w2, 3);                /* a hit: filtered out */
        run(rt, 0.2);
        GE_CHECK(hasMsg("spotted 7 3 1"));
        GE_CHECK(hasMsg("spotted 5001 nil 0"));
        GE_CHECK(hasMsg("heard 5001 0"));
        GE_CHECK(!hasMsg("heard 7 0"));
        GE_CHECK(hasMsg("miss 5001 0"));
        GE_CHECK(hasMsg("saw 7 death 8"));
        GE_CHECK(!hasMsg("saw 9 hit 8"));
    }
    missionRtDestroy(rt);

    /* ---- the second pass: held_weapon, spot_near_player, drop_grenade, drop, give_object, set_flag_raw, the new stats,
     * a body as a fire_at target, a mission pad as a pad target */
    writeScript("g6",
        "local g = chr(7)\n"
        "message('held ' .. tostring(g:held_weapon()) .. ' ' .. tostring(chr(5000):held_weapon()))\n"
        "message('spot ' .. tostring(g:spot_near_player(1)) .. ' ' .. tostring(g:spot_near_player()) .. ' ' .. tostring(chr(5000):spot_near_player()))\n"
        "message('dg ' .. tostring(g:drop_grenade()) .. tostring(chr(5000):drop_grenade()))\n"
        "message('drop ' .. tostring(g:drop(door(3))) .. tostring(g:drop(door(4))) .. tostring(g:drop(chr(9))))\n"
        "message('give ' .. tostring(g:give_object(door(4))))\n"
        "g:set_flag_raw(0x10, true) g:set_flag_raw(8, false)\n"
        "g:set_stats({ add_armour = 2.5, hearing = 1.5, vision = 300, recovery = 50, flinch = 40 })\n"
        "g:set_stats({ flinch = true })\n"
        "g:set_stats({ flinch = false })\n"
        "message('fire ' .. tostring(g:fire_at(bond_body(1))) .. tostring(g:fire_at(bond_body(0), { aim_only = true })))\n"
        "g:fire_at({ kind = 'pad', pad = 'p1' }) g:run_to({ kind = 'pad', pad = 'p1' }) g:face_pad('p1')\n"
        "message('done')\n");
    rt = start("g6", &full);
    GE_CHECK(rt != NULL);
    if (!rt) return GE_TEST_END();
    run(rt, 0.3);
    GE_CHECK(hasMsg("held 17 nil"));
    GE_CHECK(hasMsg("spot 321 321 nil") && called("spot_near_player 7 1") && called("spot_near_player 7 -1"));
    GE_CHECK(hasMsg("dg truefalse") && called("drop_grenade 7"));
    GE_CHECK(hasMsg("drop truefalsefalse") && called("drop_object 7 3") && called("drop_object 7 4"));
    GE_CHECK(!called("drop_object 7 -1"));
    GE_CHECK(hasMsg("give true") && called("give_object 7 4"));
    GE_CHECK(called("set_flag_raw 7 16 1") && called("set_flag_raw 7 8 0"));
    GE_CHECK(called("set_stats 7 has=f80 health=0 max=0 armour=0 speed=0 flinch=40 raw=0 add=2.5 hear=1.5 vis=300 rec=50"));
    GE_CHECK(called("set_stats 7 has=80 health=0 max=0 armour=0 speed=0 flinch=100 raw=0 add=0 hear=0 vis=0 rec=0"));
    GE_CHECK(called("set_stats 7 has=80 health=0 max=0 armour=0 speed=0 flinch=0 raw=0 add=0 hear=0 vis=0 rec=0"));
    GE_CHECK(hasMsg("fire truetrue"));
    GE_CHECK(called("fire_at 7 kind=3 target=1 kneel=0 aim=0") && called("fire_at 7 kind=3 target=0 kneel=0 aim=1"));
    GE_CHECK(called("fire_at 7 kind=2 target=200 kneel=0 aim=0") && called("run_to 7 200 1") && called("face_pad 7 200"));
    GE_CHECK(hasMsg("done"));
    missionRtDestroy(rt);

    /* ---- a host without the callbacks: neutral values, "not implemented yet" logged once */
    writeScript("g4",
        "local g = chr(7)\n"
        "message('n ' .. tostring(g:exists()) .. tostring(g:exists()) .. tostring(g:teleport(1)) .. tostring(g:preset_pad()) .. tostring(g:spotted_player()))\n"
        "g:patrol(1) g:hit(1, 2) g:set_ai(3)\n"
        "message('s ' .. tostring(spawn_guard({ pad = 1, body = 2 })) .. #all_guards() .. mission.civilians_killed() .. g:grenade_chance())\n"
        "message('w ' .. tostring(g:witnessed()) .. ' ' .. tostring(g:is_moving()))\n"
        "message('x ' .. tostring(g:held_weapon()) .. tostring(g:spot_near_player()) .. tostring(g:drop_grenade()) .. tostring(g:drop(door(1))) .. tostring(g:give_object(door(1))))\n"
        "g:set_flag_raw(1, true) g:set_flag_raw(1, false)\n");
    memset(&none, 0, sizeof none);
    rt = start("g4", &none);
    GE_CHECK(rt != NULL);
    if (!rt) return GE_TEST_END();
    run(rt, 0.2);
    GE_CHECK(hasMsg("n falsefalsefalsenilnil"));
    GE_CHECK(hasMsg("s nil000"));
    GE_CHECK(hasMsg("w nil false"));
    GE_CHECK(hasMsg("x nilnilfalsefalsefalse"));
    GE_CHECK_EQ_INT(countLog("set_flag_raw: not implemented yet"), 1);
    GE_CHECK_EQ_INT(countLog("held_weapon: not implemented yet"), 1);
    GE_CHECK_EQ_INT(countLog("exists: not implemented yet"), 1);
    GE_CHECK_EQ_INT(countLog("patrol: not implemented yet"), 1);
    GE_CHECK_EQ_INT(countLog("spawn_guard: not implemented yet"), 1);
    GE_CHECK_EQ_INT(f.nPropAi, 1);                              /* set_ai falls back to the setup-prop version: chr 7 is prop 3 */
    missionRtDestroy(rt);

    /* no guard_ext at all */
    rt = start("g4", NULL);
    GE_CHECK(rt != NULL);
    if (rt) { run(rt, 0.2); GE_CHECK(hasMsg("n falsefalsefalsenilnil")); missionRtDestroy(rt); }

    return GE_TEST_END();
}
