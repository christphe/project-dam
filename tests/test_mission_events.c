/* The event-driven half of the Luau mission runtime (port/mission/mission_lua.cpp): on() / :off(), spawn() / :cancel(),
 * clock(), require(), objectives.reset(), guard health and target:pos(), driven by a fake game host. The events the real
 * host finds (player_death, gunfire ...) are raised here with missionRtRaise, as port/src/missions_host.c does. */
#include "ge_test.h"
#include "mission.h"
#include "mission_lua.h"

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
    float ppos[4][3];
    int nplayers;
    char msgs[48][96];
    int nmsg;
    char logs[64][160];
    int nlog;
    float dmg6, max6;                 /* guard 6's damage and maxdamage (game units) */
    char anim[48];                    /* the last guard_anim: name, and whether it was held */
    int animHold;
} Fake;

static void fLog(void *u, const char *l) { Fake *f = u; if (getenv("LUA_TEST_V")) printf("  [log] %s\n", l); if (f->nlog < 64) snprintf(f->logs[f->nlog++], 160, "%s", l); }
static void fMsg(void *u, const char *t) { Fake *f = u; if (f->nmsg < 48) snprintf(f->msgs[f->nmsg++], 96, "%s", t); }
static int  fPc(void *u) { return ((Fake *)u)->nplayers; }
static int  fPpos(void *u, int p, float *xyz) { Fake *f = u; memcpy(xyz, f->ppos[p], 12); return 1; }
static int  fPropCount(void *u) { (void)u; return 8; }
static int  fPropType(void *u, int i) { (void)u; return i == 6 ? 9 : 3; }
static int  fPropPos(void *u, int i, float *xyz) { (void)u; xyz[0] = (float)i * 10.0f; xyz[1] = 5; xyz[2] = 0; return 1; }
static int  fAnim(void *u, int prop, const char *name, int hold)
{
    Fake *f = u;
    (void)prop;
    snprintf(f->anim, sizeof f->anim, "%s", name);
    f->animHold = hold;
    return 1;
}
static int  fHealth(void *u, int prop, double *h, double *max)
{
    Fake *f = u;
    if (prop != 6) return 0;
    *max = f->max6;
    *h = f->max6 - f->dmg6;
    return 1;
}
static void fSetHealth(void *u, int prop, int which, double v)
{
    Fake *f = u;
    if (prop != 6) return;
    if (which == MH_HEALTH_MAX) f->max6 = (float)v;
    else { f->dmg6 = f->max6 - (float)v; if (f->dmg6 < 0) f->dmg6 = 0; }
}

static MissionHost host(Fake *f)
{
    MissionHost h;
    memset(&h, 0, sizeof h);
    h.ud = f; h.log = fLog; h.message = fMsg; h.player_count = fPc; h.player_pos = fPpos;
    h.prop_count = fPropCount; h.prop_type = fPropType; h.prop_pos = fPropPos;
    h.guard_anim = fAnim; h.guard_health = fHealth; h.guard_set_health = fSetHealth;
    return h;
}

static int hasMsg(const Fake *f, const char *s)
{
    for (int i = 0; i < f->nmsg; i++) if (!strcmp(f->msgs[i], s)) return 1;
    return 0;
}

static int countMsg(const Fake *f, const char *s)
{
    int n = 0;
    for (int i = 0; i < f->nmsg; i++) if (!strcmp(f->msgs[i], s)) n++;
    return n;
}

static int hasLog(const Fake *f, const char *needle)
{
    for (int i = 0; i < f->nlog; i++) if (strstr(f->logs[i], needle)) return 1;
    return 0;
}

static void run(MissionRuntime *rt, double secs)
{
    for (double t = 0; t < secs; t += 1.0 / 60.0) missionRtTick(rt, 1.0 / 60.0);
}

static void put(const char *name, const char *src)
{
    char path[160];
    FILE *fp;
    snprintf(path, sizeof path, "tmp_missionevents/scripts/%s.luau", name);
    fp = fopen(path, "wb");
    if (fp) { fputs(src, fp); fclose(fp); }
}

static void unput(const char *name)
{
    char path[160];
    snprintf(path, sizeof path, "tmp_missionevents/scripts/%s.luau", name);
    remove(path);
}

/* A mission that runs the one script `use` on its own. */
static int load(Mission *m, const char *use, char *err, size_t n)
{
    char json[400];
    snprintf(json, sizeof json, "{ \"format\": 1, \"name\": \"t\", \"level\": \"bunker1\", \"scripts\": [ { \"use\": \"%s\", \"target\": { \"kind\": \"mission\" } } ] }", use);
    missionInit(m);
    return missionParse(json, m, err, n);
}

static void raiseNum(MissionRuntime *rt, const char *name, const char *k1, double v1, const char *k2, double v2)
{
    MissionEvField f[2];
    int n = 0;
    if (k1) { f[n].key = k1; f[n].str = NULL; f[n].num = v1; n++; }
    if (k2) { f[n].key = k2; f[n].str = NULL; f[n].num = v2; n++; }
    missionRtRaise(rt, name, f, n);
}

int main(void)
{
    static Mission m;
    Fake f;
    MissionHost h;
    MissionRuntime *rt;
    char err[160] = "";

    GE_TEST_BEGIN("mission events");
    MKDIR("tmp_missionevents");
    MKDIR("tmp_missionevents/scripts");
    {
        FILE *t = fopen("tmp_missionevents/scripts/.w", "wb");
        if (!t) { printf("SKIP: cannot write tmp_missionevents\n"); return GE_TEST_END(); }
        fclose(t);
        remove("tmp_missionevents/scripts/.w");
    }

    /* ---- on() / :off(), filter, a callback that waits ------------------------------ */
    put("onprobe",
        "local n = 0\n"
        "local c = on('gunfire', function(ev) n += 1; message('g' .. n .. ' ' .. ev.x .. ' ' .. tostring(ev.player)) end)\n"
        "on('player_hurt', function(ev) wait(0.5); message('hurt ' .. ev.player .. ' ' .. ev.health) end, { player = 1 })\n"
        "on('player_hurt', function(ev) error('callback boom') end, { player = 3 })\n"
        "wait(event('player_death'))\n"
        "c:off()\n"
        "message('off')\n"
        "wait(event('start'))\n");
    GE_CHECK_EQ_INT(load(&m, "onprobe", err, sizeof err), 1);
    memset(&f, 0, sizeof f);
    f.nplayers = 2;
    h = host(&f);
    rt = missionRtCreate(&m, &h, "tmp_missionevents", NULL, err, sizeof err);
    GE_CHECK(rt != NULL);
    if (!rt) { printf("%s\n", err); return GE_TEST_END(); }
    run(rt, 0.1);
    raiseNum(rt, "gunfire", "x", 10, "player", 0);
    run(rt, 0.1);
    GE_CHECK(hasMsg(&f, "g1 10 0"));                         /* delivered on the tick, event fields in the table */
    raiseNum(rt, "gunfire", "x", 20, NULL, 0);
    run(rt, 0.1);
    GE_CHECK(hasMsg(&f, "g2 20 nil"));                       /* an optional field is nil */
    raiseNum(rt, "player_hurt", "player", 0, "health", 0.25);
    run(rt, 0.7);
    GE_CHECK_EQ_INT(countMsg(&f, "hurt 0 0.25"), 0);         /* the filter { player = 1 } */
    raiseNum(rt, "player_hurt", "player", 1, "health", 0.5);
    run(rt, 0.3);
    GE_CHECK_EQ_INT(countMsg(&f, "hurt 1 0.5"), 0);          /* the callback is waiting ... */
    run(rt, 0.4);
    GE_CHECK_EQ_INT(countMsg(&f, "hurt 1 0.5"), 1);          /* ... and finishes: a callback may wait() */
    raiseNum(rt, "player_hurt", "player", 3, "health", 0.1);
    run(rt, 0.1);
    GE_CHECK(hasLog(&f, "callback boom"));                   /* an error in a callback is logged ... */
    raiseNum(rt, "player_death", "player", 0, NULL, 0);
    run(rt, 0.1);
    GE_CHECK(hasMsg(&f, "off"));                             /* ... the mission goes on, wait(event) still works */
    {
        const int before = f.nmsg;
        raiseNum(rt, "gunfire", "x", 30, NULL, 0);
        run(rt, 0.2);
        GE_CHECK_EQ_INT(f.nmsg, before);                     /* :off(): no more deliveries */
    }
    missionRtDestroy(rt);
    missionFree(&m);
    unput("onprobe");

    /* ---- spawn(), :cancel(), clock() ------------------------------------------------- */
    put("taskprobe",
        "local t = spawn(function() wait(0.3); message('task ' .. math.floor(clock() * 10)) end)\n"
        "local u = spawn(function() wait(0.3); message('cancelled ran') end)\n"
        "u:cancel()\n"
        "u:cancel()\n"
        "local k = spawn(function() while true do wait(0.1); message('beat') end end)\n"
        "wait(0.45)\n"
        "k:cancel()\n"
        "t:cancel()\n"
        "message('clock ' .. tostring(clock() >= 0.45))\n"
        "local me\n"
        "me = spawn(function() message('self'); me:cancel(); wait(0.1); message('after self cancel') end)\n"
        "wait(1)\n");
    GE_CHECK_EQ_INT(load(&m, "taskprobe", err, sizeof err), 1);
    memset(&f, 0, sizeof f);
    f.nplayers = 1;
    h = host(&f);
    rt = missionRtCreate(&m, &h, "tmp_missionevents", NULL, err, sizeof err);
    GE_CHECK(rt != NULL);
    run(rt, 2.0);
    GE_CHECK(hasMsg(&f, "task 3"));                          /* a spawned coroutine waits on the runtime's clock */
    GE_CHECK_EQ_INT(countMsg(&f, "cancelled ran"), 0);       /* cancelled before it ran */
    {
        const int beats = countMsg(&f, "beat");
        GE_CHECK(beats >= 3 && beats <= 5);                  /* a task that loops, until it is cancelled at 0.45 s */
    }
    GE_CHECK(hasMsg(&f, "clock true"));                      /* cancelling a finished task twice / a running one is fine */
    GE_CHECK_EQ_INT(countMsg(&f, "after self cancel"), 0);   /* a task that cancels itself stops at its next wait */
    missionRtDestroy(rt);
    missionFree(&m);
    unput("taskprobe");

    /* ---- require(): once, cached, a name built at run time ------------------------- */
    put("modcache", "message('mod loaded')\nreturn { n = 7 }\n");
    put("modvoid", "x_in_module = 5\n");
    put("modwait", "return { f = function() wait(0.1); message('mod waited') end }\n");
    put("reqprobe",
        "local a = require('modcache')\n"
        "local b = require('modcache')\n"
        "message('same ' .. tostring(a == b) .. ' ' .. a.n)\n"
        "local name = 'mod' .. 'cache'\n"
        "message('dyn ' .. tostring(require(name) == a))\n"
        "message('void ' .. tostring(require('modvoid')) .. ' ' .. tostring(x_in_module))\n"
        "message('missing ' .. tostring((pcall(require, 'no_such_mod'))))\n"
        "message('bad ' .. tostring((pcall(require, '../evil'))))\n"
        "require('modwait').f()\n"
        "message('done')\n"
        "wait(event('start'))\n");
    GE_CHECK_EQ_INT(load(&m, "reqprobe", err, sizeof err), 1);
    memset(&f, 0, sizeof f);
    f.nplayers = 1;
    h = host(&f);
    rt = missionRtCreate(&m, &h, "tmp_missionevents", NULL, err, sizeof err);
    GE_CHECK(rt != NULL);
    run(rt, 0.5);
    GE_CHECK_EQ_INT(countMsg(&f, "mod loaded"), 1);          /* the module body ran once */
    GE_CHECK(hasMsg(&f, "same true 7"));
    GE_CHECK(hasMsg(&f, "dyn true"));
    GE_CHECK(hasMsg(&f, "void true nil"));                   /* no return value: true; its globals are its own */
    GE_CHECK(hasMsg(&f, "missing false"));
    GE_CHECK(hasMsg(&f, "bad false"));                       /* the name is validated: no path */
    GE_CHECK(hasMsg(&f, "mod waited"));                      /* a function of the module may wait() when a script calls it */
    GE_CHECK(hasMsg(&f, "done"));
    missionRtDestroy(rt);
    missionFree(&m);
    unput("modcache"); unput("modvoid"); unput("modwait"); unput("reqprobe");

    /* ---- objectives.reset, guard health, play_anim hold, target:pos ------------------- */
    put("healthprobe",
        "objectives.add('keep', 'Keep him alive')\n"
        "objectives.complete('keep')\n"
        "message('c ' .. tostring(objectives.is_complete('keep')))\n"
        "objectives.reset('keep')\n"
        "message('r ' .. tostring(objectives.is_complete('keep')) .. ' ' .. tostring(objectives.is_failed('keep')) .. ' ' .. tostring(objectives.all_complete()))\n"
        "objectives.fail('keep')\n"
        "objectives.reset('keep')\n"
        "message('f ' .. tostring(objectives.is_failed('keep')))\n"
        "objectives.complete('keep')\n"
        "message('again ' .. tostring(objectives.is_complete('keep')))\n"
        "local g = guard(6)\n"
        "message('h ' .. g:health() .. ' ' .. g:max_health())\n"
        "g:set_max_health(1000)\n"
        "g:set_health(400)\n"
        "message('h2 ' .. g:health() .. ' ' .. g:max_health())\n"
        "g:set_health(5000)\n"
        "message('h3 ' .. g:health())\n"
        "message('gone ' .. guard(2):health())\n"
        "local x, y, z = g:pos()\n"
        "message('pos ' .. x .. ' ' .. y .. ' ' .. z)\n"
        "local px, py, pz = player(1):pos()\n"
        "message('ppos ' .. px .. ' ' .. pz)\n"
        "g:play_anim('kneel_cough', true)\n"
        "wait(event('start'))\n");
    GE_CHECK_EQ_INT(load(&m, "healthprobe", err, sizeof err), 1);
    memset(&f, 0, sizeof f);
    f.nplayers = 2;
    f.ppos[1][0] = 300; f.ppos[1][2] = 40;
    f.max6 = 100; f.dmg6 = 30;
    h = host(&f);
    rt = missionRtCreate(&m, &h, "tmp_missionevents", NULL, err, sizeof err);
    GE_CHECK(rt != NULL);
    run(rt, 0.2);
    GE_CHECK(hasMsg(&f, "c true"));
    GE_CHECK(hasMsg(&f, "r false false false"));             /* reset: incomplete, not failed */
    GE_CHECK(hasMsg(&f, "f false"));                         /* a failed objective goes back too */
    GE_CHECK(hasMsg(&f, "again true"));                      /* ... and can be completed again */
    GE_CHECK(hasMsg(&f, "h 70 100"));                        /* maxdamage - damage, maxdamage */
    GE_CHECK(hasMsg(&f, "h2 400 1000"));                     /* set_max_health is raw, set_health sets the damage */
    GE_CHECK(hasMsg(&f, "h3 1000"));                         /* clamped at the maximum (damage >= 0) */
    GE_CHECK(hasMsg(&f, "gone 0"));                          /* a guard that is not there */
    GE_CHECK(hasMsg(&f, "pos 60 5 0"));
    GE_CHECK(hasMsg(&f, "ppos 300 40"));
    GE_CHECK_STR(f.anim, "kneel_cough");
    GE_CHECK_EQ_INT(f.animHold, 1);
    missionRtDestroy(rt);
    missionFree(&m);
    unput("healthprobe");

    remove("tmp_missionevents/scripts");
    return GE_TEST_END();
}
