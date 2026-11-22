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
static int  fPropPos(void *u, int i, float *xyz) { (void)u; xyz[0] = (float)i * 10.0f; xyz[1] = 0; xyz[2] = 0; return 1; }
static int  fDoorState(void *u, int i) { return i == 5 ? ((Fake *)u)->doorstate : MH_DOOR_NONE; }
static void fDoorCmd(void *u, int i, int c) { Fake *f = u; (void)i; if (f->ndoor < 16) f->door_cmds[f->ndoor++] = c; }
static int  fDead(void *u, int i) { return i == 6 && ((Fake *)u)->dead6; }
static void fWin(void *u) { ((Fake *)u)->won = 1; }
static int  fDiff(void *u) { (void)u; return 0; }

static MissionHost host(Fake *f)
{
    MissionHost h;
    memset(&h, 0, sizeof h);
    h.ud = f; h.log = fLog; h.message = fMsg; h.give = fGive; h.take = fTake; h.has = fHas;
    h.player_count = fPc; h.player_pos = fPpos; h.prop_count = fPropCount; h.prop_type = fPropType;
    h.prop_pos = fPropPos; h.door_state = fDoorState; h.door_cmd = fDoorCmd; h.guard_dead = fDead; h.win = fWin; h.difficulty = fDiff;
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

    missionFree(&m);
    missionFree(&m2);
    remove("tmp_missiontest/scripts/probe.luau");
    remove("tmp_missiontest/scripts/spin.luau");
    remove("tmp_missiontest/scripts/bad.luau");
    remove("tmp_missiontest/scripts/escape.luau");
    return GE_TEST_END();
}
