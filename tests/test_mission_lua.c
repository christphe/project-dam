/* The Luau mission runtime (port/mission/mission_lua.cpp) driven by a fake game host: the three library
 * scripts, the event diffing, the sandbox and the time budget. */
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
    int has[4][128];
    float ppos[4][3];
    int nplayers;
    int door_cmds[16], ndoor;
    char msgs[16][96];
    int nmsg;
    int won;
    int dead6;
    int doorstate;
    char logs[64][160];
    int nlog;
    int diff;
    int gotos[16][3], ngoto;          /* guard_goto: prop, player, run */
    int set_ai[8][2], nset_ai;        /* guard_set_ai: prop, list */
    char says[8][160];
    int nsay;
    int failed;
    float g6z;                        /* guard 6's z (it is at x = 60) */
} Fake;

static void fLog(void *u, const char *l) { Fake *f = u; if (getenv("LUA_TEST_V")) printf("  [log] %s\n", l); if (f->nlog < 64) snprintf(f->logs[f->nlog++], 160, "%s", l); }
static void fMsg(void *u, const char *t) { Fake *f = u; if (f->nmsg < 16) snprintf(f->msgs[f->nmsg++], 96, "%s", t); }
static void fGive(void *u, int p, int it) { Fake *f = u; for (int i = 0; i < 4; i++) if (p < 0 || p == i) f->has[i][it] = 1; }
static void fTake(void *u, int p, int it) { Fake *f = u; for (int i = 0; i < 4; i++) if (p < 0 || p == i) f->has[i][it] = 0; }
static int  fHas(void *u, int p, int it) { Fake *f = u; return f->has[p][it]; }
static int  fPc(void *u) { return ((Fake *)u)->nplayers; }
static int  fPpos(void *u, int p, float *xyz) { Fake *f = u; memcpy(xyz, f->ppos[p], 12); return 1; }
static int  fPropCount(void *u) { (void)u; return 8; }
static int  fPropType(void *u, int i) { (void)u; return i == 5 ? 1 : i == 6 ? 9 : 3; }
static int  fPropPos(void *u, int i, float *xyz) { xyz[0] = (float)i * 10.0f; xyz[1] = 0; xyz[2] = i == 6 ? ((Fake *)u)->g6z : 0; return 1; }
static int  fDoorState(void *u, int i) { return i == 5 ? ((Fake *)u)->doorstate : MH_DOOR_NONE; }
static void fDoorCmd(void *u, int i, int c) { Fake *f = u; (void)i; if (f->ndoor < 16) f->door_cmds[f->ndoor++] = c; }
static int  fDead(void *u, int i) { return i == 6 && ((Fake *)u)->dead6; }
static void fWin(void *u) { ((Fake *)u)->won = 1; }
static int  fDiff(void *u) { return ((Fake *)u)->diff; }
static int  fGoto(void *u, int prop, int player, int run) { Fake *f = u; if (f->ngoto < 16) { f->gotos[f->ngoto][0] = prop; f->gotos[f->ngoto][1] = player; f->gotos[f->ngoto][2] = run; f->ngoto++; } return MH_GOTO_OK; }
static void fSetAi(void *u, int prop, int ai) { Fake *f = u; if (f->nset_ai < 8) { f->set_ai[f->nset_ai][0] = prop; f->set_ai[f->nset_ai][1] = ai; f->nset_ai++; } }
static void fSay(void *u, const char *who, const char *t) { Fake *f = u; if (f->nsay < 8) snprintf(f->says[f->nsay++], 160, "%s|%s", who, t); }
static void fFail(void *u) { ((Fake *)u)->failed = 1; }

static MissionHost host(Fake *f)
{
    MissionHost h;
    memset(&h, 0, sizeof h);
    h.ud = f; h.log = fLog; h.message = fMsg; h.give = fGive; h.take = fTake; h.has = fHas;
    h.player_count = fPc; h.player_pos = fPpos; h.prop_count = fPropCount; h.prop_type = fPropType;
    h.prop_pos = fPropPos; h.door_state = fDoorState; h.door_cmd = fDoorCmd; h.guard_dead = fDead; h.win = fWin; h.difficulty = fDiff;
    h.guard_goto = fGoto; h.guard_set_ai = fSetAi; h.say = fSay; h.fail = fFail;
    return h;
}

static int hasMsg(const Fake *f, const char *s)
{
    for (int i = 0; i < f->nmsg; i++) if (!strcmp(f->msgs[i], s)) return 1;
    return 0;
}

static int hasLog(const Fake *f, const char *needle)
{
    for (int i = 0; i < f->nlog; i++) if (strstr(f->logs[i], needle)) return 1;
    return 0;
}

static const char *kLib =
    "{ \"format\": 1, \"name\": \"t\", \"level\": \"bunker1\",\n"
    "  \"zones\": [ { \"id\": \"z1\", \"pos\": [500, 0, 0], \"radius\": 50, \"room\": 1 },\n"
    "               { \"id\": \"zx\", \"pos\": [2000, 0, 0], \"radius\": 50, \"room\": 1 } ],\n"
    "  \"objectives\": [ { \"id\": \"o1\", \"text\": \"Find the key\", \"difficulty\": 0 },\n"
    "                    { \"id\": \"o2\", \"text\": \"Hard one\", \"difficulty\": 2 } ],\n"
    "  \"scripts\": [\n"
    "    { \"use\": \"key_door\", \"target\": { \"kind\": \"prop\", \"prop\": 5 },\n"
    "      \"props\": { \"key\": 61, \"message\": \"Locked\", \"objective\": \"o1\" } },\n"
    "    { \"use\": \"give_item\", \"target\": { \"kind\": \"zone\", \"zone\": \"z1\" }, \"props\": { \"item\": 40, \"message\": \"Got it\" } },\n"
    "    { \"use\": \"exit_unlock\", \"target\": { \"kind\": \"zone\", \"zone\": \"zx\" } } ] }\n";

static void run(MissionRuntime *rt, double secs)
{
    for (double t = 0; t < secs; t += 1.0 / 60.0) missionRtTick(rt, 1.0 / 60.0);
}

int main(void)
{
    static Mission m, m2;
    Fake f;
    MissionHost h;
    MissionRuntime *rt;
    char err[160] = "";

    GE_TEST_BEGIN("mission lua");
    missionInit(&m);
    GE_CHECK_EQ_INT(missionParse(kLib, &m, err, sizeof err), 1);

    /* ---- the library scripts ------------------------------------------------ */
    memset(&f, 0, sizeof f);
    f.nplayers = 2;
    f.ppos[0][0] = 1000; f.ppos[1][0] = 1000;
    h = host(&f);
    rt = missionRtCreate(&m, &h, NULL, NULL, err, sizeof err);
    GE_CHECK(rt != NULL);
    if (!rt) { printf("%s\n", err); return GE_TEST_END(); }
    GE_CHECK_EQ_INT(missionRtScriptCount(rt), 3);
    GE_CHECK_EQ_INT(missionRtObjectiveCount(rt), 2);

    run(rt, 0.2);
    GE_CHECK_EQ_INT(f.ndoor, 1);
    GE_CHECK_EQ_INT(f.door_cmds[0], MH_DOOR_LOCK);          /* key_door locks the door at once */
    GE_CHECK_EQ_INT(f.nmsg, 0);                              /* nobody is near it */

    f.ppos[0][0] = 50;                                       /* prop 5 is at x = 50 */
    run(rt, 0.6);
    GE_CHECK(hasMsg(&f, "Locked"));                          /* near the locked door: the message */

    f.ppos[0][0] = 500;                                      /* into zone z1: give_item */
    run(rt, 0.4);
    GE_CHECK(hasMsg(&f, "Got it"));
    GE_CHECK_EQ_INT(f.has[0][40], 1);
    GE_CHECK_EQ_INT(f.has[1][40], 1);                        /* give(item) with no player: everyone */
    GE_CHECK_EQ_INT(missionRtObjectiveComplete(rt, 0), 0);

    f.has[1][61] = 1;                                        /* player 2 picks up the key */
    run(rt, 0.6);
    GE_CHECK_EQ_INT(f.door_cmds[f.ndoor - 1], MH_DOOR_UNLOCK);
    GE_CHECK(hasMsg(&f, "Unlocked"));
    GE_CHECK_EQ_INT(missionRtObjectiveComplete(rt, 0), 1);   /* o1 by the script */
    GE_CHECK_EQ_INT(missionRtAllComplete(rt, 0), 1);         /* o2 is 00 Agent only */
    GE_CHECK_EQ_INT(missionRtAllComplete(rt, 2), 0);
    GE_CHECK_EQ_INT(f.won, 0);

    f.ppos[0][0] = 2000;                                     /* the exit zone, objectives done (o2 is not on Agent) */
    run(rt, 0.5);
    GE_CHECK_EQ_INT(f.won, 1);                               /* all complete + in the zone: mission.win() */
    if (!f.won) for (int i = 0; i < f.nlog; i++) printf("  log: %s\n", f.logs[i]);
    GE_CHECK(hasLog(&f, "script key_door#0 loaded"));
    missionRtDestroy(rt);

    /* ---- the typed library scripts: pickup_objective; the API definition file is not a script ---- */
    {
        Mission mp;
        int lib_has_defs_script = 0;
        for (const MissionLibScript *l = g_missionLibScripts; l->name; l++) if (!strcmp(l->name, "mission")) lib_has_defs_script = 1;
        GE_CHECK_EQ_INT(lib_has_defs_script, 0);
        GE_CHECK(g_missionApiDefs != NULL && strstr(g_missionApiDefs, "declare function say") && strstr(g_missionApiDefs, "type Objective = string"));
        missionInit(&mp);
        GE_CHECK_EQ_INT(missionParse(
            "{ \"format\": 1, \"name\": \"t\", \"level\": \"bunker1\",\n"
            "  \"objectives\": [ { \"id\": \"got\", \"text\": \"Get it\", \"difficulty\": 0 } ],\n"
            "  \"scripts\": [ { \"use\": \"pickup_objective\", \"target\": { \"kind\": \"mission\" },\n"
            "    \"props\": { \"item\": 77, \"objective\": \"got\", \"message\": \"Taken\" } } ] }", &mp, err, sizeof err), 1);
        memset(&f, 0, sizeof f);
        f.nplayers = 1;
        h = host(&f);
        rt = missionRtCreate(&mp, &h, NULL, NULL, err, sizeof err);
        GE_CHECK(rt != NULL);
        GE_CHECK_EQ_INT(missionRtScriptCount(rt), 1);
        run(rt, 0.3);
        GE_CHECK_EQ_INT(missionRtObjectiveComplete(rt, 0), 0);
        f.has[0][76] = 1;                                    /* another item: nothing */
        run(rt, 0.3);
        GE_CHECK_EQ_INT(missionRtObjectiveComplete(rt, 0), 0);
        f.has[0][77] = 1;
        run(rt, 0.3);
        GE_CHECK_EQ_INT(missionRtObjectiveComplete(rt, 0), 1);
        GE_CHECK(hasMsg(&f, "Taken"));
        missionRtDestroy(rt);
        missionFree(&mp);
    }

    /* ---- events, sandbox, time budget (a mission's own scripts) ---------------- */
    MKDIR("tmp_missiontest");
    MKDIR("tmp_missiontest/scripts");
    {   /* a read-only working directory (a container-owned build tree): skip the file-backed part */
        FILE *t = fopen("tmp_missiontest/scripts/.w", "wb");
        if (!t) { printf("SKIP own-script checks: cannot write tmp_missiontest\n"); missionFree(&m); return GE_TEST_END(); }
        fclose(t);
        remove("tmp_missiontest/scripts/.w");
    }
    {
        FILE *fp = fopen("tmp_missiontest/scripts/probe.luau", "wb");
        const char *src =
            "local e = wait(event('door', { prop = 5, state = 'open' }))\n"
            "message('door ' .. e.state)\n"
            "wait(event('death', { prop = 6 }))\n"
            "message('dead')\n"
            "wait(event('pickup', { item = 77 }))\n"
            "message('picked ' .. tostring(has(77)))\n"
            "message(tostring(os) .. tostring(io) .. tostring(debug))\n"
            "wait(event('start'))\n";
        if (fp) { fputs(src, fp); fclose(fp); }
        fp = fopen("tmp_missiontest/scripts/spin.luau", "wb");
        if (fp) { fputs("while true do end\n", fp); fclose(fp); }
        fp = fopen("tmp_missiontest/scripts/bad.luau", "wb");
        if (fp) { fputs("this is not luau (\n", fp); fclose(fp); }
        fp = fopen("tmp_missiontest/scripts/escape.luau", "wb");
        if (fp) { fputs("message(tostring(require) .. tostring(loadfile)); string.x = 1\n", fp); fclose(fp); }
    }
    missionInit(&m2);
    GE_CHECK_EQ_INT(missionParse(
        "{ \"format\": 1, \"name\": \"t\", \"level\": \"bunker1\", \"scripts\": [\n"
        "  { \"use\": \"spin\", \"target\": { \"kind\": \"mission\" } },\n"
        "  { \"use\": \"bad\", \"target\": { \"kind\": \"mission\" } },\n"
        "  { \"use\": \"escape\", \"target\": { \"kind\": \"mission\" } },\n"
        "  { \"use\": \"nonexistent\", \"target\": { \"kind\": \"mission\" } },\n"
        "  { \"use\": \"probe\", \"target\": { \"kind\": \"mission\" } } ] }", &m2, err, sizeof err), 1);
    memset(&f, 0, sizeof f);
    f.nplayers = 1;
    f.ppos[0][0] = 1000;
    h = host(&f);
    rt = missionRtCreate(&m2, &h, "tmp_missiontest", NULL, err, sizeof err);
    GE_CHECK(rt != NULL);
    run(rt, 0.3);
    GE_CHECK(hasLog(&f, "ran over its time budget"));        /* spin.luau cut off, the frame went on */
    GE_CHECK(hasLog(&f, "script bad#1"));                    /* the syntax error is reported */
    GE_CHECK(hasLog(&f, "not found"));
    GE_CHECK(hasMsg(&f, "nilnil") || hasMsg(&f, "nilnilnil") || hasMsg(&f, "nil"));   /* require / loadfile are gone */
    GE_CHECK(hasLog(&f, "error") );                          /* string.x = 1: the libraries are read-only */

    f.doorstate = MH_DOOR_CLOSED;
    run(rt, 0.3);
    f.doorstate = MH_DOOR_OPENED;                            /* a door event from the state diff */
    run(rt, 0.3);
    GE_CHECK(hasMsg(&f, "door open"));
    f.dead6 = 1;
    run(rt, 0.3);
    GE_CHECK(hasMsg(&f, "dead"));
    f.has[0][77] = 1;
    run(rt, 0.3);
    GE_CHECK(hasMsg(&f, "picked true"));
    GE_CHECK(hasMsg(&f, "nilnilnil"));                       /* no os, io, debug */
    missionRtDestroy(rt);

    /* mission.difficulty(): the host's difficulty, -1 without one */
    {
        FILE *fp = fopen("tmp_missiontest/scripts/diff.luau", "wb");
        Mission m3;
        if (fp) { fputs("message('difficulty ' .. mission.difficulty())\nwait(event('start'))\n", fp); fclose(fp); }
        missionInit(&m3);
        GE_CHECK_EQ_INT(missionParse(
            "{ \"format\": 1, \"name\": \"t\", \"level\": \"bunker1\", \"scripts\": [\n"
            "  { \"use\": \"diff\", \"target\": { \"kind\": \"mission\" } } ] }", &m3, err, sizeof err), 1);
        memset(&f, 0, sizeof f);
        f.nplayers = 1; f.diff = 3;
        h = host(&f);
        rt = missionRtCreate(&m3, &h, "tmp_missiontest", NULL, err, sizeof err);
        GE_CHECK(rt != NULL);
        run(rt, 0.3);
        GE_CHECK(hasMsg(&f, "difficulty 3"));
        missionRtDestroy(rt);
        memset(&f, 0, sizeof f);
        f.nplayers = 1;
        h = host(&f);
        h.difficulty = NULL;
        rt = missionRtCreate(&m3, &h, "tmp_missiontest", NULL, err, sizeof err);
        GE_CHECK(rt != NULL);
        run(rt, 0.3);
        GE_CHECK(hasMsg(&f, "difficulty -1"));
        missionRtDestroy(rt);
        missionFree(&m3);
        remove("tmp_missiontest/scripts/diff.luau");
    }

    /* follow / stop, distance_to, say, mission.fail() */
    {
        FILE *fp = fopen("tmp_missiontest/scripts/follower.luau", "wb");
        Mission m4;
        if (fp) {
            fputs("local g = guard(6)\n"
                  "message('d1 ' .. g:distance_to(door(5)))\n"
                  "message('d2 ' .. door(5):distance_to(g))\n"
                  "g:follow()\n"
                  "wait(event('pickup', { item = 90 }))\n"
                  "g:stop()\n"
                  "say('Yuri', 'Wait for me!')\n"
                  "wait(event('pickup', { item = 91 }))\n"
                  "door(5):unlock(true)\n"
                  "door(5):unlock()\n"
                  "mission.fail()\n", fp);
            fclose(fp);
        }
        fp = fopen("tmp_missiontest/scripts/padprobe.luau", "wb");
        if (fp) { fputs("message('padd ' .. target:distance_to(guard(6)))\nwait(event('start'))\n", fp); fclose(fp); }
        fp = fopen("tmp_missiontest/scripts/zoneprobe.luau", "wb");
        if (fp) { fputs("message('zoned ' .. target:distance_to(guard(6)))\nwait(event('start'))\n", fp); fclose(fp); }
        missionInit(&m4);
        GE_CHECK_EQ_INT(missionParse(
            "{ \"format\": 1, \"name\": \"t\", \"level\": \"bunker1\",\n"
            "  \"pads\": [ { \"id\": \"p1\", \"pos\": [60, 0, 100], \"look\": [0, 0, 1], \"room\": 1 } ],\n"
            "  \"zones\": [ { \"id\": \"z1\", \"pos\": [500, 0, 0], \"radius\": 50, \"room\": 1 } ],\n"
            "  \"scripts\": [\n"
            "    { \"use\": \"follower\", \"target\": { \"kind\": \"mission\" } },\n"
            "    { \"use\": \"padprobe\", \"target\": { \"kind\": \"pad\", \"pad\": \"p1\" } },\n"
            "    { \"use\": \"zoneprobe\", \"target\": { \"kind\": \"zone\", \"zone\": \"z1\" } } ] }", &m4, err, sizeof err), 1);
        memset(&f, 0, sizeof f);
        f.nplayers = 2;
        f.ppos[0][0] = 60; f.ppos[1][0] = 100;               /* guard 6 is at x = 60 */
        h = host(&f);
        rt = missionRtCreate(&m4, &h, "tmp_missiontest", NULL, err, sizeof err);
        GE_CHECK(rt != NULL);
        run(rt, 0.3);
        GE_CHECK(hasMsg(&f, "d1 10"));                       /* prop 6 (x 60) to prop 5 (x 50) */
        GE_CHECK(hasMsg(&f, "d2 10"));
        GE_CHECK(hasMsg(&f, "padd 100"));                    /* a mission pad by id */
        GE_CHECK(hasMsg(&f, "zoned 440"));                   /* a zone */
        GE_CHECK_EQ_INT(f.nset_ai, 1);                       /* :follow() gives the passive list once */
        GE_CHECK_EQ_INT(f.set_ai[0][0], 6);
        GE_CHECK_EQ_INT(f.set_ai[0][1], MISSION_FOLLOW_AI);
        run(rt, 1.0);
        GE_CHECK_EQ_INT(f.ngoto, 0);                         /* the players are on top of it */

        f.ppos[0][0] = 200;                                  /* 140 away: under the 200 threshold */
        run(rt, 1.0);
        GE_CHECK_EQ_INT(f.ngoto, 0);
        f.ppos[0][0] = 500;                                  /* 440 away from player 0; player 1 (x 100) is 40 away */
        run(rt, 1.0);
        GE_CHECK_EQ_INT(f.ngoto, 0);                         /* the NEAREST living player counts */
        f.ppos[1][0] = 700;                                  /* both far: player 0 at 440 is the nearest */
        run(rt, 0.6);
        GE_CHECK_EQ_INT(f.ngoto, 1);
        GE_CHECK_EQ_INT(f.gotos[0][0], 6);
        GE_CHECK_EQ_INT(f.gotos[0][1], 0);
        GE_CHECK_EQ_INT(f.gotos[0][2], 1);                   /* 440 > 350: run */
        for (int k = 0; k < 4; k++) { f.g6z += 20; run(rt, 0.5); }
        GE_CHECK_EQ_INT(f.ngoto, 1);                         /* the guard is moving, the player still: no new order (it would restart him) */
        run(rt, 2.0);
        GE_CHECK_EQ_INT(f.ngoto, 1);                         /* stopped for under 3 s: still left alone (starting takes time) */
        run(rt, 1.6);
        GE_CHECK_EQ_INT(f.ngoto, 2);                         /* stopped for 3 s: ordered again */
        f.ngoto = 0;
        f.ppos[0][2] = 200;                                  /* the player moved 200 from where the guard is heading */
        f.g6z += 20; run(rt, 0.5);
        GE_CHECK_EQ_INT(f.ngoto, 0);                         /* not within 2 s of the last order */
        for (int k = 0; k < 3; k++) { f.g6z += 20; run(rt, 0.5); }
        GE_CHECK_EQ_INT(f.ngoto, 1);
        f.ppos[0][0] = 1500;                                 /* player 1 (about 650) is nearest now: run */
        f.ngoto = 0;
        f.g6z += 20; run(rt, 0.5);
        GE_CHECK(f.ngoto == 1 && f.gotos[0][1] == 1 && f.gotos[0][2] == 1);
        f.ppos[1][0] = 2500;                                 /* player 0, about 1440: sprint */
        f.ngoto = 0;
        f.g6z += 20; run(rt, 0.5);
        GE_CHECK(f.ngoto == 1 && f.gotos[0][1] == 0 && f.gotos[0][2] == 2);
        f.ppos[0][0] = 60; f.ppos[0][2] = f.g6z + 300;       /* player 0 300 away: walk, once 2 s have passed (not urgent) */
        f.ngoto = 0;
        for (int k = 0; k < 4; k++) { f.g6z += 20; run(rt, 0.5); }
        GE_CHECK(f.ngoto == 1 && f.gotos[0][1] == 0 && f.gotos[0][2] == 0);

        f.has[0][90] = 1;                                    /* the script calls :stop() and says a line */
        run(rt, 0.3);
        f.ngoto = 0;
        run(rt, 2.0);
        GE_CHECK_EQ_INT(f.ngoto, 0);                         /* stopped: no more orders although everybody is far */
        GE_CHECK_EQ_INT(f.nsay, 1);
        GE_CHECK(!strcmp(f.says[0], "Yuri|Wait for me!"));
        GE_CHECK_EQ_INT(f.failed, 0);
        f.has[0][91] = 1;
        run(rt, 0.3);
        GE_CHECK_EQ_INT(f.failed, 1);                        /* mission.fail() reaches the host */
        GE_CHECK_EQ_INT(f.door_cmds[f.ndoor - 2], MH_DOOR_UNLOCK_FORCE);   /* unlock(true) forces, plain unlock() does not */
        GE_CHECK_EQ_INT(f.door_cmds[f.ndoor - 1], MH_DOOR_UNLOCK);
        GE_CHECK_EQ_INT(f.won, 0);
        missionRtDestroy(rt);
        missionFree(&m4);
        remove("tmp_missiontest/scripts/follower.luau");
        remove("tmp_missiontest/scripts/padprobe.luau");
        remove("tmp_missiontest/scripts/zoneprobe.luau");
    }

    {   /* props a mission adds are named by uuid in mission.json; the runtime hands the script the setup prop number they got */
        static Mission m5;
        FILE *fp = fopen("tmp_missiontest/scripts/uuidprobe.luau", "wb");
        if (fp) {
            fputs("export type Props = { g: Guard, d: Door, gone: Guard, num: Guard, text: string }\n"
                  "local p = props :: Props\n"
                  "message('g ' .. tostring(p.g) .. ' d ' .. tostring(p.d) .. ' gone ' .. tostring(p.gone) .. ' num ' .. tostring(p.num) .. ' ' .. p.text)\n"
                  "message('t ' .. tostring((target :: any).prop))\n"
                  "wait(event('start'))\n", fp);
            fclose(fp);
        }
        missionInit(&m5);
        GE_CHECK_EQ_INT(missionParse(
            "{ \"format\": 1, \"name\": \"t\", \"level\": \"bunker1\",\n"
            "  \"guards\": [ { \"op\": \"add\", \"uuid\": \"11111111-1111-4111-8111-111111111111\", \"name\": \"Kalinin\", \"pad\": 1 },\n"
            "                { \"op\": \"add\", \"uuid\": \"33333333-3333-4333-8333-333333333333\", \"name\": \"Late\", \"pad\": 1, \"difficulties\": [3] } ],\n"
            "  \"objects\": [ { \"op\": \"add\", \"uuid\": \"22222222-2222-4222-8222-222222222222\", \"type\": 1, \"pad\": 10001 } ],\n"
            "  \"scripts\": [\n"
            "    { \"use\": \"uuidprobe\", \"target\": { \"kind\": \"prop\", \"uuid\": \"11111111-1111-4111-8111-111111111111\" },\n"
            "      \"props\": { \"g\": \"11111111-1111-4111-8111-111111111111\", \"d\": \"22222222-2222-4222-8222-222222222222\",\n"
            "                  \"gone\": \"33333333-3333-4333-8333-333333333333\", \"num\": 5, \"text\": \"plain text\" } },\n"
            "    { \"use\": \"uuidprobe\", \"target\": { \"kind\": \"prop\", \"prop\": 9 },\n"
            "      \"props\": { \"g\": \"11111111-1111-4111-8111-111111111111\", \"d\": \"22222222-2222-4222-8222-222222222222\",\n"
            "                  \"gone\": \"44444444-4444-4444-8444-444444444444\", \"num\": 6, \"text\": \"stale\" } } ] }", &m5, err, sizeof err), 1);
        GE_CHECK_EQ_INT(missionValidate(&m5, err, sizeof err), 1);
        m5.guards[0].setup_prop = 8;                         /* what mission_apply records (the fake level has 8 props) */
        m5.guards[1].setup_prop = -1;                        /* present on 007 only: this is another difficulty */
        m5.objects[0].setup_prop = 9;
        memset(&f, 0, sizeof f);
        f.nplayers = 1;
        h = host(&f);
        rt = missionRtCreate(&m5, &h, "tmp_missiontest", NULL, err, sizeof err);
        GE_CHECK(rt != NULL);
        run(rt, 0.3);
        GE_CHECK(hasMsg(&f, "g 8 d 9 gone -1 num 5 plain text"));      /* a uuid is its setup prop number; an absent prop is -1 */
        GE_CHECK(hasMsg(&f, "t 8"));                                    /* a target by uuid */
        GE_CHECK(hasMsg(&f, "g 8 d 9 gone 44444444-4444-4444-8444-444444444444 num 6 stale"));   /* an unknown uuid stays the text */
        GE_CHECK(hasMsg(&f, "t nil"));                                  /* a number that designates an added prop: missing */
        GE_CHECK(hasLog(&f, "\"Kalinin\" 11111111-1111-4111-8111-111111111111 -> setup prop 8"));
        GE_CHECK(hasLog(&f, "\"Late\" 33333333-3333-4333-8333-333333333333 -> setup prop -1"));
        GE_CHECK(hasLog(&f, "is an added prop: reference it by its uuid"));
        missionRtDestroy(rt);
        missionFree(&m5);
        remove("tmp_missiontest/scripts/uuidprobe.luau");
    }

    missionFree(&m);
    missionFree(&m2);
    remove("tmp_missiontest/scripts/probe.luau");
    remove("tmp_missiontest/scripts/spin.luau");
    remove("tmp_missiontest/scripts/bad.luau");
    remove("tmp_missiontest/scripts/escape.luau");
    return GE_TEST_END();
}
