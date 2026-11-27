/* The "Formula" sample mod (docs/modding/samples/formula): the files load and validate, and its script plays through
 * on the fake game host: Kalinin is found (Agent: in the washrooms; Secret Agent and up: a hostage, freed by killing his
 * captor, or shot when the captor sees a player too long), follows, opens the four doors at the terminals, the
 * formulas are picked up, the exit wins; and his death fails his objective. Skipped (not failed) when the sample
 * directory cannot be found. */
#include "ge_test.h"
#include "mission.h"
#include "mission_lua.h"
#include "mission_dir.h"
#include "ge_pkg.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#define RMDIR(p) _rmdir(p)
#else
#include <unistd.h>
#define RMDIR(p) rmdir(p)
#endif

#define NPROPS 600
#define KAL 523    /* the washroom Kalinin (Agent) */
#define HOST 524   /* the hostage Kalinin (Secret Agent and up) */
#define CAPT 525   /* his captor (526 is the captor's weapon record) */
#define T1 527
#define T2 528
#define T3 529
#define COMP 530
#define ITEM_PASSWORDS 66
#define ITEM_FORMULAS 75
#define HIDE 32

typedef struct Fake {
    float pos[NPROPS][3];
    float ppos[3];
    int has[128];
    int doors[NPROPS][4], ndoors[NPROPS];
    char msgs[32][128]; int nmsg;
    char says[32][200]; int nsay;
    int diff, open_hide, won, failed, dead, ai_set, gotos, follow_player;
    int used[NPROPS];                 /* a player activated the prop (prop_used reports it once) */
    int kal;                          /* the Kalinin present on this difficulty: KAL or HOST */
    int captor_dead, captor_sees, hits, aims;
    int open56;                       /* door 56, into the hostage's room, is open */
    int others;                       /* living guards near Kalinin besides the captor */
    int captor_hits;                  /* times the captor was hit */
} Fake;

static int  fUsed(void *u, int i) { Fake *f = u; if (i >= 0 && i < NPROPS && f->used[i]) { f->used[i] = 0; return 1; } return 0; }
static void fMsg(void *u, const char *t) { Fake *f = u; if (f->nmsg < 32) snprintf(f->msgs[f->nmsg++], 128, "%s", t); }
static int  fHas(void *u, int p, int it) { (void)p; return ((Fake *)u)->has[it]; }
static int  fPc(void *u) { (void)u; return 1; }
static int  fPpos(void *u, int p, float *xyz) { (void)p; memcpy(xyz, ((Fake *)u)->ppos, 12); return 1; }
static int  fPropCount(void *u) { (void)u; return NPROPS; }
static int  fPropType(void *u, int i) { (void)u; return (i == KAL || i == HOST || i == CAPT) ? 9 : (i == HIDE || i == 56 || i == 63 || i == 69 || i == 77) ? 1 : 10; }
static int  fPropPos(void *u, int i, float *xyz) { memcpy(xyz, ((Fake *)u)->pos[i], 12); return 1; }
static int  fDoorState(void *u, int i)
{
    const Fake *f = u;
    if (fPropType(u, i) != 1) return MH_DOOR_NONE;
    return ((i == HIDE && f->open_hide) || (i == 56 && f->open56)) ? MH_DOOR_OPENED : MH_DOOR_CLOSED;
}
static void fDoorCmd(void *u, int i, int c) { Fake *f = u; if (f->ndoors[i] < 4) f->doors[i][f->ndoors[i]++] = c; }
static int  fDead(void *u, int i) { Fake *f = u; return (i == f->kal && f->dead) || (i == CAPT && f->captor_dead); }
static void fSetAi(void *u, int i, int ai) { Fake *f = u; if (i == f->kal && ai == MISSION_FOLLOW_AI) f->ai_set++; }
static int  fAim(void *u, int i, int t, int fire) { Fake *f = u; (void)fire; if (i == CAPT && t == f->kal) f->aims++; return 1; }
static void fHit(void *u, int i) { Fake *f = u; if (i == f->kal) { f->hits++; f->dead = 1; } }
static int  fSees(void *u, int i) { Fake *f = u; return i == CAPT && f->captor_sees; }
static int  fHits(void *u, int i) { Fake *f = u; return i == CAPT ? f->captor_hits : 0; }
static int  fGuardsNear(void *u, const float *pos, float r, int except) { (void)pos; (void)r; (void)except; return ((Fake *)u)->others; }
static void fWin(void *u) { ((Fake *)u)->won = 1; }
static void fFail(void *u) { ((Fake *)u)->failed = 1; }
static void fSay(void *u, const char *who, const char *t) { Fake *f = u; if (f->nsay < 32) snprintf(f->says[f->nsay++], 200, "%s|%s", who, t); }
static int  fGoto(void *u, int i, int p, int run) { Fake *f = u; (void)run; if (i == f->kal) { f->gotos++; f->follow_player = p; } return MH_GOTO_OK; }
static int  fDiff(void *u) { return ((Fake *)u)->diff; }
static void fGive(void *u, int p, int it) { Fake *f = u; (void)p; f->has[it] = 1; }

static int hasMsg(const Fake *f, const char *s) { for (int i = 0; i < f->nmsg; i++) if (strstr(f->msgs[i], s)) return 1; return 0; }
static int hasSay(const Fake *f, const char *s) { for (int i = 0; i < f->nsay; i++) if (strstr(f->says[i], s)) return 1; return 0; }
static int lastDoorCmd(const Fake *f, int d) { return f->ndoors[d] ? f->doors[d][f->ndoors[d] - 1] : -1; }

static void run(MissionRuntime *rt, double secs) { for (double t = 0; t < secs; t += 1.0 / 60.0) missionRtTick(rt, 1.0 / 60.0); }

static void place(Fake *f, int prop, float x, float y, float z) { f->pos[prop][0] = x; f->pos[prop][1] = y; f->pos[prop][2] = z; }
static void playerAt(Fake *f, float x, float y, float z) { f->ppos[0] = x; f->ppos[1] = y; f->ppos[2] = z; }

static int findSample(char *dir, size_t n)
{
    const char *env = getenv("GE_SAMPLES_DIR");
    static const char *up[] = { "", "../", "../../", "../../../", "../../../../", "../../../../../" };
    char path[600];
    if (env && *env) {
        snprintf(path, sizeof path, "%s/formula/campaign.json", env);
        FILE *fp = fopen(path, "rb");
        if (fp) { fclose(fp); snprintf(dir, n, "%s/formula", env); return 1; }
    }
    for (size_t i = 0; i < sizeof up / sizeof *up; i++) {
        FILE *fp;
        snprintf(path, sizeof path, "%sdocs/modding/samples/formula/campaign.json", up[i]);
        fp = fopen(path, "rb");
        if (fp) { fclose(fp); snprintf(dir, n, "%sdocs/modding/samples/formula", up[i]); return 1; }
    }
    return 0;
}

static MissionRuntime *start(const Mission *m, Fake *f, const char *mdir, int diff)
{
    MissionHost h;
    char err[160] = "";
    MissionRuntime *rt;
    memset(f, 0, sizeof *f);
    f->diff = diff;
    f->kal = diff >= 1 ? HOST : KAL;
    place(f, f->kal, 1000, 0, 0);
    place(f, CAPT, 1000, 0, 110);
    place(f, 56, 1000, 0, 300);                               /* door 56, into the hostage's room */
    place(f, T1, 5000, 0, 0); place(f, T2, 5000, 0, 1000); place(f, T3, 5000, 0, 2000); place(f, COMP, 6000, 0, 0);
    playerAt(f, 0, 0, 0);
    memset(&h, 0, sizeof h);
    h.ud = f; h.message = fMsg; h.has = fHas; h.player_count = fPc; h.player_pos = fPpos; h.prop_count = fPropCount;
    h.prop_type = fPropType; h.prop_pos = fPropPos; h.door_state = fDoorState; h.door_cmd = fDoorCmd; h.guard_dead = fDead;
    h.guard_set_ai = fSetAi; h.win = fWin; h.fail = fFail; h.say = fSay; h.guard_goto = fGoto; h.difficulty = fDiff; h.give = fGive; h.prop_used = fUsed;
    h.guard_aim_at = fAim; h.guard_hit = fHit; h.guard_sees_player = fSees; h.guards_near = fGuardsNear; h.guard_hits = fHits;
    rt = missionRtCreate(m, &h, mdir, NULL, err, sizeof err);
    if (!rt) printf("  runtime: %s\n", err);
    return rt;
}

/* Kalinin and the player at a terminal: a step of the script waits TYPING seconds, then opens the door. */
static void atTerminal(MissionRuntime *rt, Fake *f, int term)
{
    run(rt, 2.6);   /* the script waits NEXT_DELAY (2.5 s) after a terminal before it takes another */
    playerAt(f, f->pos[term][0] + 50, f->pos[term][1], f->pos[term][2]);
    memcpy(f->pos[f->kal], f->ppos, 12);
    f->used[term] = 1;   /* the player activates the terminal: that is what calls Kalinin over */
    run(rt, 1.0);
}

/* the pieces of a playthrough */
/* Agent: walk up to him; Secret Agent and up: kill his captor */
static void meet(MissionRuntime *rt, Fake *f) { if (f->diff >= 1) f->captor_dead = 1; else playerAt(f, 1100, 0, 0); run(rt, 8.5); }
static void toExit(MissionRuntime *rt, Fake *f) { playerAt(f, 7971.21f, -396.54f, -5432.11f); memcpy(f->pos[f->kal], f->ppos, 12); run(rt, 1.0); }
static void doorTerminals(MissionRuntime *rt, Fake *f)
{
    atTerminal(rt, f, T1); run(rt, 3.5);
    atTerminal(rt, f, T3); run(rt, 3.5);
    atTerminal(rt, f, T2); run(rt, 3.5);
}

int main(void)
{
    static Mission m;
    static Campaign c;
    static Fake f;
    char dir[600], path[700], err[200] = "";
    MissionRuntime *rt;

    GE_TEST_BEGIN("mission formula");
    if (!findSample(dir, sizeof dir)) {
        printf("SKIP: docs/modding/samples/formula not found from the working directory (set GE_SAMPLES_DIR)\n");
        return GE_TEST_END();
    }
    snprintf(path, sizeof path, "%s/campaign.json", dir);
    campaignInit(&c);
    GE_CHECK_EQ_INT(campaignLoadFile(path, &c, err, sizeof err), 1);
    GE_CHECK_EQ_INT(campaignValidate(&c, err, sizeof err), 1);
    GE_CHECK(!strcmp(c.name, "Formula"));
    GE_CHECK_EQ_INT(c.nmissions, 1);
    GE_CHECK_EQ_INT(c.affects, CAMPAIGN_AFFECTS_STORY);

    snprintf(path, sizeof path, "%s/formula/mission.json", dir);
    missionInit(&m);
    GE_CHECK_EQ_INT(missionLoadFile(path, &m, err, sizeof err), 1);
    GE_CHECK_EQ_INT(missionValidate(&m, err, sizeof err), 1);
    if (err[0]) printf("  validate: %s\n", err);
    {   /* what mission_apply records: the level has KAL (523) records, additions follow (guards with their weapons, then objects) */
        int n = KAL;
        for (int i = 0; i < m.nguards; i++) if (m.guards[i].op == MISSION_OP_ADD) { m.guards[i].setup_prop = n; n += 1 + m.guards[i].nweapons; }
        for (int i = 0; i < m.nobjects; i++) if (m.objects[i].op == MISSION_OP_ADD) m.objects[i].setup_prop = n++;
        GE_CHECK_EQ_INT(n, 532);
        GE_CHECK(!strcmp(m.guards[1].name, "Kalinin"));
        GE_CHECK_EQ_INT(missionAddedSetupProp(&m, m.guards[1].uuid), KAL);
        GE_CHECK_EQ_INT(missionAddedSetupProp(&m, m.guards[2].uuid), HOST);
        GE_CHECK_EQ_INT(missionAddedSetupProp(&m, m.guards[3].uuid), CAPT);
        GE_CHECK_EQ_INT(m.guards[1].diff_mask, 0x1);          /* the washroom Kalinin: Agent only */
        GE_CHECK_EQ_INT(m.guards[2].diff_mask, 0xE);          /* the hostage and the captor: Secret Agent and up */
        GE_CHECK_EQ_INT(m.guards[3].diff_mask, 0xE);
        GE_CHECK_EQ_INT(m.scripts[0].props[0].kind, MISSION_VAL_STRING);   /* kalinin: a uuid */
    }
    GE_CHECK(!strcmp(m.level, "facility"));
    GE_CHECK_EQ_INT(m.npads, 5);
    GE_CHECK_EQ_INT(m.nzones, 1);
    GE_CHECK_EQ_INT(m.nguards, 4);
    GE_CHECK_EQ_INT(m.nobjects, 8);
    GE_CHECK_EQ_INT(m.nobjectives, 6);
    GE_CHECK_EQ_INT(m.nscripts, 1);
    GE_CHECK(strlen(m.briefing) > 100);
    GE_CHECK_EQ_INT(m.objectives[3].difficulty, 2);           /* passwords: 00 Agent and up */
    GE_CHECK_EQ_INT(m.objectives[4].difficulty, 1);           /* formulas: Secret Agent and up */
    GE_CHECK_EQ_INT(m.objects[7].diff_mask, 0xC);             /* the password list: difficulties 2 and 3 */

    snprintf(path, sizeof path, "%s/formula", dir);

    /* ---- the same mod as a .pmod: campaign, mission and its script load through the package layer ---------------- */
    {
        char tmp[500], pkg[600], file[700];
        static Campaign pc;
        static Mission pm;
        MissionRuntime *prt;
        static Fake pf;
        const char *t = getenv("TEMP");
        if (!t || !*t) t = getenv("TMPDIR");
        if (!t || !*t) t = "/tmp";
        snprintf(tmp, sizeof tmp, "%s/ge_pmod_formula_test", t);
        missionMakeDir(tmp);
        snprintf(pkg, sizeof pkg, "%s/formula.pmod", tmp);
        GE_CHECK_EQ_INT(pkgWriteDir(dir, pkg, err, sizeof err), 1);
        if (err[0]) printf("  pkgWriteDir: %s\n", err);
        snprintf(file, sizeof file, "%s/campaign.json", pkg);
        campaignInit(&pc);
        GE_CHECK_EQ_INT(campaignLoadFile(file, &pc, err, sizeof err), 1);
        GE_CHECK(!strcmp(pc.name, "Formula"));
        campaignFree(&pc);
        snprintf(file, sizeof file, "%s/formula/mission.json", pkg);
        missionInit(&pm);
        GE_CHECK_EQ_INT(missionLoadFile(file, &pm, err, sizeof err), 1);
        GE_CHECK_EQ_INT(missionValidate(&pm, err, sizeof err), 1);
        snprintf(file, sizeof file, "%s/cover.png", pkg);
        GE_CHECK_EQ_INT(pkgExists(file), 1);
        snprintf(file, sizeof file, "%s/formula/formula/mission.json", pkg);
        GE_CHECK_EQ_INT(pkgExists(file), 0);
        snprintf(file, sizeof file, "%s/formula", pkg);
        prt = start(&pm, &pf, file, 1);   /* the script is read from <pkg>/formula/scripts/ */
        GE_CHECK(prt != NULL);
        if (prt) {
            GE_CHECK_EQ_INT(missionRtScriptCount(prt), 1);
            run(prt, 1.0);
            missionRtDestroy(prt);
        }
        missionFree(&pm);
        remove(pkg);
        RMDIR(tmp);
    }

    /* ---- Secret Agent: a full playthrough ----------------------------------- */
    rt = start(&m, &f, path, 1);
    GE_CHECK(rt != NULL);
    if (!rt) return GE_TEST_END();
    GE_CHECK_EQ_INT(missionRtScriptCount(rt), 1);
    run(rt, 1.0);
    GE_CHECK_EQ_INT(lastDoorCmd(&f, 63), MH_DOOR_LOCK);      /* the three doors start locked */
    GE_CHECK_EQ_INT(lastDoorCmd(&f, 69), MH_DOOR_LOCK);
    GE_CHECK_EQ_INT(lastDoorCmd(&f, 77), MH_DOOR_LOCK);
    GE_CHECK_EQ_INT(lastDoorCmd(&f, 102), -1);               /* door 102 is left to the game */
    GE_CHECK_EQ_INT(lastDoorCmd(&f, HIDE), MH_DOOR_UNLOCK_FORCE);

    playerAt(&f, 5000 - 50, 0, 0);                            /* at terminal 1 without Kalinin */
    run(rt, 1.0);
    GE_CHECK(!hasMsg(&f, "The terminal needs a security override."));   /* standing there does nothing */
    f.used[T1] = 1;                                           /* activating it without Kalinin */
    run(rt, 1.0);
    GE_CHECK(hasMsg(&f, "The terminal needs a security override."));
    GE_CHECK_EQ_INT(f.ndoors[63], 1);

    playerAt(&f, 3000, 0, 0);                                 /* the hostage: the captor covers him */
    run(rt, 1.0);
    GE_CHECK_EQ_INT(missionRtObjectiveComplete(rt, 0), 0);
    GE_CHECK_EQ_INT(f.aims, 1);                               /* covered once, not re-ordered every poll */
    playerAt(&f, 1100, 0, 0);                                 /* right next to him: still a hostage */
    run(rt, 1.0);
    GE_CHECK_EQ_INT(missionRtObjectiveComplete(rt, 0), 0);
    GE_CHECK(!hasSay(&f, "Kalinin|Please! Don't let him shoot me!"));
    GE_CHECK(hasSay(&f, "Guard|Sit, Comrade Kalinin."));    /* the interrogation, overheard near door 56 */
    f.open56 = 1;                                             /* the door into his room opens: he calls out */
    run(rt, 0.5);
    GE_CHECK(hasSay(&f, "Kalinin|Please! Don't let him shoot me!"));

    f.others = 1;                                             /* the captor is shot, another guard still close */
    f.captor_dead = 1;
    run(rt, 3.0);
    GE_CHECK(!hasSay(&f, "Kalinin|Thank you... you saved my life"));   /* hands still up */
    GE_CHECK_EQ_INT(missionRtObjectiveComplete(rt, 0), 0);
    f.others = 0;                                             /* the last one down: 2 s of calm */
    run(rt, 1.0);
    GE_CHECK(!hasSay(&f, "Kalinin|Thank you... you saved my life"));
    run(rt, 1.5);
    GE_CHECK(hasSay(&f, "Kalinin|Thank you... you saved my life"));
    GE_CHECK_EQ_INT(missionRtObjectiveComplete(rt, 0), 1);    /* find */
    run(rt, 5.0);
    GE_CHECK(hasSay(&f, "Kalinin|Stay close."));
    GE_CHECK_EQ_INT(f.ai_set, 1);                             /* :follow() */

    playerAt(&f, 3100, 0, 0);                                 /* he lags: the player is 2100 away */
    {
        const int before = f.nsay;
        run(rt, 1.0);
        GE_CHECK(f.nsay > before && !strncmp(f.says[f.nsay - 1], "Kalinin|", 8));   /* one of his WAIT_LINES, at random */
    }

    atTerminal(rt, &f, T1);                                   /* terminal 1: typing, then the lab door */
    GE_CHECK(hasSay(&f, "Laboratory access"));
    GE_CHECK_EQ_INT(lastDoorCmd(&f, 63), MH_DOOR_LOCK);       /* still typing */
    run(rt, 3.5);                                             /* turn (0.5 s) + typing (3 s) */
    GE_CHECK_EQ_INT(f.doors[63][1], MH_DOOR_UNLOCK_FORCE);    /* the native lock goes too */
    GE_CHECK_EQ_INT(f.doors[63][2], MH_DOOR_OPEN);
    GE_CHECK_EQ_INT(missionRtObjectiveComplete(rt, 1), 1);    /* lab */

    atTerminal(rt, &f, T3);                                   /* the order of the east terminals is free */
    run(rt, 3.5);
    GE_CHECK_EQ_INT(f.doors[77][2], MH_DOOR_OPEN);
    GE_CHECK_EQ_INT(missionRtObjectiveComplete(rt, 2), 0);    /* east needs both */
    atTerminal(rt, &f, T2);
    run(rt, 3.5);
    GE_CHECK_EQ_INT(f.doors[69][2], MH_DOOR_OPEN);
    GE_CHECK_EQ_INT(missionRtObjectiveComplete(rt, 2), 1);    /* east */

    toExit(rt, &f);                                           /* the formulas are missing: the exit stays shut */
    run(rt, 5.0);
    GE_CHECK_EQ_INT(f.won, 0);

    atTerminal(rt, &f, COMP);                                 /* the computer gives the formulas (no list needed here) */
    GE_CHECK(hasSay(&f, "The formulas are on this machine."));
    GE_CHECK_EQ_INT(f.has[ITEM_FORMULAS], 0);
    run(rt, 3.5);
    GE_CHECK(hasSay(&f, "Downloading... done. Now we leave."));
    GE_CHECK_EQ_INT(f.has[ITEM_FORMULAS], 1);
    GE_CHECK_EQ_INT(missionRtObjectiveComplete(rt, 4), 1);    /* formulas */
    GE_CHECK_EQ_INT(lastDoorCmd(&f, 102), -1);
    GE_CHECK_EQ_INT(f.won, 0);

    toExit(rt, &f);
    GE_CHECK_EQ_INT(missionRtObjectiveComplete(rt, 5), 1);    /* alive */
    GE_CHECK(hasSay(&f, "I never want to see this place again."));
    run(rt, 3.5);
    GE_CHECK_EQ_INT(f.won, 1);
    GE_CHECK_EQ_INT(f.failed, 0);
    missionRtDestroy(rt);

    /* ---- Agent: keep Kalinin alive and leave ------------------------------------ */
    rt = start(&m, &f, path, 0);
    GE_CHECK(rt != NULL);
    toExit(rt, &f);                                           /* not met yet: nothing */
    run(rt, 4.0);
    GE_CHECK_EQ_INT(f.won, 0);
    meet(rt, &f);
    toExit(rt, &f);
    run(rt, 4.0);
    GE_CHECK_EQ_INT(f.won, 1);                                /* no doors, no formulas */
    GE_CHECK_EQ_INT(f.has[ITEM_FORMULAS], 0);
    missionRtDestroy(rt);

    /* ---- Agent: a terminal still opens its door ----------------------------------- */
    rt = start(&m, &f, path, 0);
    meet(rt, &f);
    atTerminal(rt, &f, T1);
    run(rt, 3.5);
    GE_CHECK_EQ_INT(f.doors[63][2], MH_DOOR_OPEN);
    missionRtDestroy(rt);

    /* ---- 00 Agent: the computer is encrypted until a player has the password list -- */
    rt = start(&m, &f, path, 2);
    GE_CHECK(rt != NULL);
    meet(rt, &f);
    doorTerminals(rt, &f);
    atTerminal(rt, &f, COMP);
    run(rt, 4.0);
    GE_CHECK(hasSay(&f, "Kalinin|Encrypted. Without today's password list I can't get in."));
    GE_CHECK_EQ_INT(f.has[ITEM_FORMULAS], 0);
    GE_CHECK(!hasSay(&f, "The formulas are on this machine."));
    toExit(rt, &f);
    run(rt, 4.0);
    GE_CHECK_EQ_INT(f.won, 0);
    f.has[ITEM_PASSWORDS] = 1;                                /* the list is picked up */
    run(rt, 1.0);
    GE_CHECK_EQ_INT(missionRtObjectiveComplete(rt, 3), 1);    /* passwords */
    GE_CHECK(hasSay(&f, "Today's passwords! That's what I need for the main computer."));
    atTerminal(rt, &f, COMP);
    run(rt, 3.5);
    GE_CHECK_EQ_INT(f.has[ITEM_FORMULAS], 1);
    GE_CHECK_EQ_INT(missionRtObjectiveComplete(rt, 4), 1);
    toExit(rt, &f);
    run(rt, 4.0);
    GE_CHECK_EQ_INT(f.won, 1);
    missionRtDestroy(rt);

    /* ---- the password list before Kalinin is found: a message, not his line ---------- */
    rt = start(&m, &f, path, 3);
    run(rt, 0.5);
    f.has[ITEM_PASSWORDS] = 1;
    run(rt, 1.0);
    GE_CHECK(hasMsg(&f, "Today's password list. The programmer will need it."));
    GE_CHECK(!hasSay(&f, "Kalinin|Today's passwords!"));      /* not his line: he is not with the player */
    missionRtDestroy(rt);

    /* ---- Secret Agent: the captor sees a player for 2.5 s and shoots the hostage -------- */
    rt = start(&m, &f, path, 1);
    run(rt, 1.0);
    f.captor_sees = 1;
    run(rt, 2.0);
    GE_CHECK_EQ_INT(f.hits, 0);                               /* not yet */
    run(rt, 1.0);
    GE_CHECK(f.hits > 0);
    GE_CHECK(!hasSay(&f, "Guard|Not one step closer!"));      /* the captor's voice is the game's own */
    run(rt, 1.0);
    GE_CHECK_EQ_INT(missionRtObjectiveStatus(rt, 5), 2);      /* "alive": failed */
    GE_CHECK_EQ_INT(missionRtObjectiveComplete(rt, 0), 0);
    missionRtDestroy(rt);

    /* ---- Secret Agent: the captor is hit but not killed: once he recovers he shoots ---- */
    rt = start(&m, &f, path, 1);
    run(rt, 1.0);
    f.captor_hits = 1;
    run(rt, 0.5);
    GE_CHECK_EQ_INT(f.hits, 0);                               /* still reeling */
    run(rt, 1.0);
    GE_CHECK(f.hits > 0);
    run(rt, 0.5);
    GE_CHECK_EQ_INT(missionRtObjectiveStatus(rt, 5), 2);
    missionRtDestroy(rt);

    /* ---- the cubicle door (Agent): opening it finds Kalinin, standing far away -------- */
    rt = start(&m, &f, path, 0);
    playerAt(&f, 400, 0, 0);                                  /* 600 from Kalinin: walking in finds nobody */
    run(rt, 2.0);
    GE_CHECK_EQ_INT(missionRtObjectiveComplete(rt, 0), 0);
    f.open_hide = 1;
    run(rt, 1.0);
    GE_CHECK_EQ_INT(missionRtObjectiveComplete(rt, 0), 1);
    GE_CHECK(hasSay(&f, "Kalinin|Don't shoot!"));
    missionRtDestroy(rt);

    /* ---- Kalinin dies: his objective fails, the mission goes on; the exit ends it as failed */
    rt = start(&m, &f, path, 1);
    meet(rt, &f);
    f.dead = 1;
    run(rt, 1.0);
    GE_CHECK_EQ_INT(missionRtObjectiveStatus(rt, 5), 2);      /* "alive": failed */
    GE_CHECK(!hasMsg(&f, "Kalinin is dead. The doors stay locked."));
    run(rt, 3.0);
    GE_CHECK_EQ_INT(f.failed, 0);                             /* not the end of the mission */
    f.used[COMP] = 1;                                         /* trying the computer without him */
    run(rt, 1.0);
    GE_CHECK(hasMsg(&f, "Kalinin is dead. You cannot get the formulas without him."));
    toExit(rt, &f);
    GE_CHECK_EQ_INT(f.failed, 1);                             /* leaving ends it, failed */
    GE_CHECK_EQ_INT(f.won, 0);
    missionRtDestroy(rt);

    campaignFree(&c);
    missionFree(&m);
    return GE_TEST_END();
}
