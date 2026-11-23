/* Rebuilding a stage setup for a mission (port/mission/mission_apply.c) on a synthetic PC-layout setup. */
#include "ge_test.h"
#include "mission.h"
#include "mission_apply.h"
#include "mission_dir.h"
#include "mission_setup.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static uint32_t rd32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static unsigned rd16(const uint8_t *p) { return p[0] | (p[1] << 8); }
static void wr32(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
static void wr16(uint8_t *p, unsigned v) { p[0] = v; p[1] = v >> 8; }
static void wrf(uint8_t *p, float f) { uint32_t u; memcpy(&u, &f, 4); wr32(p, u); }
static float rdf(const uint8_t *p) { uint32_t u = rd32(p); float f; memcpy(&f, &u, 4); return f; }

enum { INTRO = 0x50, PADS = 0x60, STR = 0x140, STREAM = 0x150 };
enum { NPADS = 3 };

static uint8_t g_file[4096];
static uint32_t g_size;
static uint32_t g_recoff[8];

static uint8_t *rec(uint32_t *cur, int type)
{
    uint8_t *p = g_file + *cur;
    p[3] = (uint8_t)type;
    wr16(p, 0x100);
    g_recoff[0] = 0;
    *cur += missionPropdefSize(type);
    return p;
}

/* header, intro (spawn pad 1, end), 3 pads, a string, then: guard 0, guard 1, prop(3), assigned weapon, pickup, END */
static void build(void)
{
    uint32_t cur = STREAM;
    uint8_t *p;
    memset(g_file, 0, sizeof g_file);
    wr32(g_file + 8 * 2, INTRO);
    wr32(g_file + 8 * 3, STREAM);
    wr32(g_file + 8 * 6, PADS);
    wr32(g_file + INTRO, 0); wr32(g_file + INTRO + 4, 1); wr32(g_file + INTRO + 8, 0);
    wr32(g_file + INTRO + 12, 9);
    for (int i = 0; i < NPADS; i++) {
        uint8_t *d = g_file + PADS + i * MISSION_SETUP_PAD_SIZE;
        wrf(d, 100.0f * (i + 1)); wrf(d + 4, 10.0f); wrf(d + 8, 0.0f);
        wr32(d + 0x28, STR);
    }
    memcpy(g_file + STR, "tile", 5);
    p = rec(&cur, 9); wr16(p + 4, 0); wr16(p + 6, 0); wr16(p + 8, 2); wr16(p + 10, 2);     /* guard chr 0 on pad 0 */
    p = rec(&cur, 9); wr16(p + 4, 1); wr16(p + 6, 1); wr16(p + 8, 2); wr16(p + 10, 3);     /* guard chr 1 on pad 1 */
    p = rec(&cur, 3); wr16(p + 4, 38); wr16(p + 6, 2); wr32(p + 8, 0x101);                 /* prop on pad 2 */
    p = rec(&cur, 8); wr16(p + 4, 184); wr16(p + 6, 1); wr32(p + 8, 0x4000); p[144] = 8;   /* chr 1's gun */
    p = rec(&cur, 8); wr16(p + 4, 119); wr16(p + 6, 2); wr32(p + 8, 0x1); p[144] = 26;     /* a pickup */
    p = rec(&cur, 48);
    g_size = cur + 8;
}

static Mission *load(const char *json)
{
    static Mission m;
    char err[128];
    missionFree(&m);
    missionInit(&m);
    if (!missionParse(json, &m, err, sizeof err)) { printf("parse: %s\n", err); return NULL; }
    return &m;
}

static const char *kJson =
    "{ \"format\": 1, \"name\": \"t\", \"level\": \"bunker1\",\n"
    "  \"pads\": [ { \"id\": \"p1\", \"pos\": [250, 20, 5], \"look\": [0, 0, 2], \"room\": 1 } ],\n"
    "  \"guards\": [ { \"op\": \"move\", \"prop\": 0, \"pad\": 2 },\n"
    "              { \"op\": \"add\", \"pad\": \"p1\", \"model\": 5, \"ai\": 7, \"weapon\": 17 },\n"
    "              { \"op\": \"remove\", \"prop\": 1 },\n"
    "              { \"op\": \"move\", \"prop\": 2, \"pad\": 0 } ],\n"
    "  \"objects\": [ { \"op\": \"add\", \"type\": 8, \"pad\": \"p1\", \"model\": 248, \"item\": 61 },\n"
    "               { \"op\": \"move\", \"prop\": 2, \"pad\": 0 },\n"
    "               { \"op\": \"remove\", \"prop\": 4 },\n"
    "               { \"op\": \"add\", \"type\": 4, \"pad\": 0 } ] }\n";


/* An add op's fields land in the cloned record; chrnum is new; absent fields keep the template's bytes. */
static void test_fields(void)
{
    static const char *json =
        "{ \"format\": 1, \"name\": \"t\", \"level\": \"bunker1\",\n"
        "  \"guards\": [ { \"op\": \"add\", \"pad\": 1, \"model\": 65535, \"head\": -1, \"health\": 300, \"bitflags\": 513 },\n"
        "              { \"op\": \"add\", \"pad\": 2 } ],\n"
        "  \"objects\": [ { \"op\": \"add\", \"type\": 3, \"pad\": 2, \"flags\": 1193046, \"maxdamage\": 2.5, \"extrascale\": 512 },\n"
        "               { \"op\": \"add\", \"type\": 8, \"pad\": 2, \"item\": 61, \"collectable\": { \"timer\": -3 } } ] }\n";
    MissionApplyResult r;
    char err[128] = "";
    const Mission *m;
    uint32_t q;
    const uint8_t *tg;
    build();
    tg = g_file + STREAM;                                   /* the template guard (chr 0) */
    wr16(g_file + STREAM + 12, 77); wr16(g_file + STREAM + 16, 90); wr16(g_file + STREAM + 18, 33);
    wr16(g_file + STREAM + 22, 5);                          /* a fixed head */
    m = load(json);
    GE_CHECK(m != NULL);
    if (!m) return;
    GE_CHECK_EQ_INT(missionApplySetup(m, g_file, g_size, -1, 1.0f, &r, err, sizeof err), 1);
    GE_CHECK_EQ_INT(r.skipped, 0);
    q = rd32(r.data + 8 * 3) + 32 + 32 + 144 + 160 + 160;   /* first appended record: guard 1 */
    GE_CHECK_EQ_INT(r.data[q + 3], 9);
    GE_CHECK_EQ_INT(rd16(r.data + q + 4), 2);               /* a new chrnum, not the template's 0 */
    GE_CHECK_EQ_INT(rd16(r.data + q + 6), 1);
    GE_CHECK_EQ_INT(rd16(r.data + q + 8), 0xFFFF);          /* random body */
    GE_CHECK_EQ_INT((int16_t)rd16(r.data + q + 22), -1);    /* random head */
    GE_CHECK_EQ_INT(rd16(r.data + q + 16), 300);
    GE_CHECK_EQ_INT(rd16(r.data + q + 20), 513);
    GE_CHECK_EQ_INT(rd16(r.data + q + 12), 77);             /* not given: the template's */
    GE_CHECK_EQ_INT(rd16(r.data + q + 18), 33);
    q += 32;                                                /* guard 2: nothing given, a pure clone but for chrnum and pad */
    GE_CHECK_EQ_INT(rd16(r.data + q + 4), 3);
    GE_CHECK_EQ_INT(rd16(r.data + q + 6), 2);
    GE_CHECK(!memcmp(r.data + q + 8, tg + 8, 24));
    q += 32;                                                /* the prop: flags, maxdamage, extrascale from the op */
    GE_CHECK_EQ_INT(r.data[q + 3], 3);
    GE_CHECK_EQ_INT(rd32(r.data + q + 8), 1193046);
    GE_CHECK_EQ_INT(rd32(r.data + q + 128), 163840);
    GE_CHECK_EQ_INT(rd16(r.data + q), 512);
    GE_CHECK_EQ_INT(rd16(r.data + q + 4), 38);              /* template model kept */
    GE_CHECK_EQ_INT(rd32(r.data + q + 12), 0);
    q += 144;                                               /* the pickup: the type's own field, and "item" */
    GE_CHECK_EQ_INT(r.data[q + 144], 61);
    GE_CHECK_EQ_INT((int16_t)rd16(r.data + q + 146), -3);
    missionApplyFree(&r);
}

/* A guard "move" op may carry "ai": the record gets the new AI list; without it the list stays; "ai" survives a save. */
static void test_move_ai(void)
{
    static const char *json =
        "{ \"format\": 1, \"name\": \"t\", \"level\": \"bunker1\",\n"
        "  \"guards\": [ { \"op\": \"move\", \"prop\": 0, \"pad\": 0, \"ai\": 1025 },\n"
        "              { \"op\": \"move\", \"prop\": 1, \"pad\": 2 } ] }\n";
    MissionApplyResult r;
    char err[128] = "";
    const Mission *m;
    const uint8_t *g0;
    char *back;
    build();
    m = load(json);
    GE_CHECK(m != NULL);
    if (!m) return;
    GE_CHECK_EQ_INT(m->guards[0].ai, 1025);
    GE_CHECK_EQ_INT(m->guards[1].ai, -1);
    GE_CHECK_EQ_INT(missionApplySetup(m, g_file, g_size, -1, 1.0f, &r, err, sizeof err), 1);
    GE_CHECK_EQ_INT(r.skipped, 0);
    g0 = r.data + rd32(r.data + 8 * 3);
    GE_CHECK_EQ_INT(rd16(g0 + 10), 1025);                  /* guard 0: the new AI list, same pad */
    GE_CHECK_EQ_INT(rd16(g0 + 6), 0);
    GE_CHECK_EQ_INT(rd16(g0 + 32 + 10), 3);                /* guard 1: moved, AI list untouched */
    GE_CHECK_EQ_INT(rd16(g0 + 32 + 6), 2);
    missionApplyFree(&r);
    back = missionToJson(m);
    GE_CHECK(back != NULL);
    {
        Mission again;
        missionInit(&again);
        GE_CHECK_EQ_INT(back ? missionParse(back, &again, err, sizeof err) : 0, 1);
        GE_CHECK(again.nguards == 2 && again.guards[0].ai == 1025 && again.guards[1].ai == -1);
        missionFree(&again);
    }
    free(back);
}

/* "difficulties": objects get the game's do-not-load bits (flags2 0x10 << difficulty) for the others; the game loads every guard at
 * every difficulty, so a guard that is not listed for the chosen one is sunk with its weapons; an added guard is not added. */
static void test_difficulties(void)
{
    static const char *json =
        "{ \"format\": 1, \"name\": \"t\", \"level\": \"bunker1\",\n"
        "  \"guards\": [ { \"op\": \"move\", \"prop\": 0, \"difficulties\": [2, 3] },\n"
        "              { \"op\": \"move\", \"prop\": 1, \"difficulties\": [0] },\n"
        "              { \"op\": \"add\", \"pad\": 2, \"model\": 5, \"difficulties\": [3] } ],\n"
        "  \"objects\": [ { \"op\": \"move\", \"prop\": 2, \"difficulties\": [1] },\n"
        "               { \"op\": \"add\", \"type\": 3, \"pad\": 2, \"model\": 38, \"difficulties\": [0, 3] } ] }\n";
    MissionApplyResult r;
    char err[128] = "";
    const Mission *m;
    const uint8_t *s;
    char *back;
    build();
    m = load(json);
    GE_CHECK(m != NULL);
    if (!m) return;
    GE_CHECK_EQ_INT(m->guards[0].diff_mask, 12);
    GE_CHECK_EQ_INT(m->guards[0].pad.num, -1);             /* no pad: a presence edit */
    GE_CHECK_EQ_INT(missionValidate(m, err, sizeof err), 1);

    /* difficulty 0 (Agent): guard 0 is not listed -> sunk; guard 1 stays; the 007-only added guard is not added */
    GE_CHECK_EQ_INT(missionApplySetupEx(m, g_file, g_size, -1, 1.0f, 0, &r, err, sizeof err), 1);
    s = r.data + rd32(r.data + 8 * 3);
    GE_CHECK_EQ_INT(rd16(s + 6), 3);                       /* the sink pad (index 3, after the three originals) */
    GE_CHECK_EQ_INT(rd16(s + 32 + 6), 1);                  /* guard 1 where it was */
    GE_CHECK_EQ_INT(rd32(s + 208 + 12) & 0xF0, 0);         /* chr 1's weapon still loads */
    GE_CHECK_EQ_INT(rd32(s + 64 + 12) & 0xF0, 0xD0);       /* prop 2: only Secret Agent */
    GE_CHECK_EQ_INT(r.guards_added, 0);
    GE_CHECK_EQ_INT(r.guards_removed, 1);
    GE_CHECK_EQ_INT(rd32(s + 528 + 12) & 0xF0, 0x60);      /* the added object: Agent and 007 only */
    missionApplyFree(&r);

    /* difficulty 2 (00 Agent): guard 0 stays, guard 1 sunk and its weapon not loaded */
    GE_CHECK_EQ_INT(missionApplySetupEx(m, g_file, g_size, -1, 1.0f, 2, &r, err, sizeof err), 1);
    s = r.data + rd32(r.data + 8 * 3);
    GE_CHECK_EQ_INT(rd16(s + 6), 0);
    GE_CHECK_EQ_INT(rd16(s + 32 + 6), 3);
    GE_CHECK_EQ_INT(rd32(s + 208 + 12) & 0xF0, 0xF0);
    GE_CHECK_EQ_INT(rd32(s + 368 + 12) & 0xF0, 0);         /* the pickup (not carried) is untouched */
    GE_CHECK_EQ_INT(r.guards_added, 0);
    missionApplyFree(&r);

    /* difficulty 3 (007): the added guard is there; unknown difficulty (-1): every guard present, object bits still set */
    GE_CHECK_EQ_INT(missionApplySetupEx(m, g_file, g_size, -1, 1.0f, 3, &r, err, sizeof err), 1);
    GE_CHECK_EQ_INT(r.guards_added, 1);
    missionApplyFree(&r);
    GE_CHECK_EQ_INT(missionApplySetupEx(m, g_file, g_size, -1, 1.0f, -1, &r, err, sizeof err), 1);
    s = r.data + rd32(r.data + 8 * 3);
    GE_CHECK_EQ_INT(rd16(s + 6), 0);
    GE_CHECK_EQ_INT(rd16(s + 32 + 6), 1);
    GE_CHECK_EQ_INT(rd32(s + 64 + 12) & 0xF0, 0xD0);
    missionApplyFree(&r);

    /* round trip */
    back = missionToJson(m);
    GE_CHECK(back != NULL);
    {
        Mission again;
        missionInit(&again);
        GE_CHECK_EQ_INT(back ? missionParse(back, &again, err, sizeof err) : 0, 1);
        GE_CHECK(again.guards[0].diff_mask == 12 && again.guards[1].diff_mask == 1 && again.objects[1].diff_mask == 9);
        missionFree(&again);
    }
    free(back);
}

/* The editor's reader sees the same setup the apply code edits: pads (scaled), props with their fields, AI lists. */
static void test_setup_decode(void)
{
    SetupData sd;
    char err[128] = "";
    build();
    wr16(g_file + STREAM + 16, 90);                            /* guard 0 health */
    GE_CHECK_EQ_INT(missionSetupDecode(g_file, g_size, 2.0f, &sd, err, sizeof err), 1);
    GE_CHECK_EQ_INT(sd.npads, NPADS);
    GE_CHECK(fabsf(sd.pads[1].pos[0] - 400.0f) < 1e-3f);       /* stored 200 * scale 2 */
    GE_CHECK_EQ_INT(sd.nprops, 5);
    GE_CHECK_EQ_INT(sd.props[0].type, 9);
    GE_CHECK_EQ_INT(sd.props[0].model, 2);
    GE_CHECK_EQ_INT(sd.props[2].type, 3);
    GE_CHECK_EQ_INT(sd.props[2].model, 38);
    GE_CHECK_EQ_INT(sd.props[3].item, 8);
    GE_CHECK_EQ_INT(sd.props[4].item, 26);
    {
        const MissionField *f;
        const int n = missionRecFields(9, &f);
        int found = 0;
        for (int i = 0; i < n; i++)
            if (!strcmp(f[i].name, "health")) { found = 1; GE_CHECK((sd.props[0].f.set >> i) & 1); GE_CHECK(sd.props[0].f.v[i] == 90.0); }
        GE_CHECK(found);
    }
    missionSetupFree(&sd);
    GE_CHECK_EQ_INT(missionSetupDecode(g_file, 16, 1.0f, &sd, err, sizeof err), 0);
}

/* An added guard with two weapons gets two assigned records carrying its new chrnum. */
static void test_weapons(void)
{
    static const char *json =
        "{ \"format\": 1, \"name\": \"t\", \"level\": \"bunker1\",\n"
        "  \"guards\": [ { \"op\": \"add\", \"pad\": 1, \"model\": 5, \"weapons\": [ { \"item\": 17, \"flags\": 16385 }, { \"item\": 8, \"model\": 191, \"collectable\": { \"timer\": 7 } } ] } ] }\n";
    MissionApplyResult r;
    char err[128] = "";
    const Mission *m;
    uint32_t q;
    build();
    m = load(json);
    GE_CHECK(m != NULL);
    if (!m) return;
    GE_CHECK_EQ_INT(missionApplySetup(m, g_file, g_size, -1, 1.0f, &r, err, sizeof err), 1);
    GE_CHECK_EQ_INT(r.skipped, 0);
    GE_CHECK_EQ_INT(r.guards_added, 1);
    q = rd32(r.data + 8 * 3) + 32 + 32 + 144 + 160 + 160;   /* the added guard (chr 2) */
    GE_CHECK_EQ_INT(r.data[q + 3], 9);
    GE_CHECK_EQ_INT(rd16(r.data + q + 4), 2);
    q += 32;                                                /* weapon 1: item 17, the sniper model, the op's flags */
    GE_CHECK_EQ_INT(r.data[q + 3], 8);
    GE_CHECK_EQ_INT(rd16(r.data + q + 6), 2);               /* the carrier's chr number */
    GE_CHECK_EQ_INT(r.data[q + 144], 17);
    GE_CHECK_EQ_INT(rd16(r.data + q + 4), 210);
    GE_CHECK_EQ_INT(rd32(r.data + q + 8), 16385);           /* 0x4001: assigned (always set) + the op's bit */
    q += 160;                                               /* weapon 2: item 8, model 191, timer 7 */
    GE_CHECK_EQ_INT(r.data[q + 3], 8);
    GE_CHECK_EQ_INT(rd16(r.data + q + 6), 2);
    GE_CHECK_EQ_INT(r.data[q + 144], 8);
    GE_CHECK_EQ_INT(rd16(r.data + q + 4), 191);
    GE_CHECK_EQ_INT((int16_t)rd16(r.data + q + 146), 7);
    GE_CHECK(rd32(r.data + q + 8) & 0x4000);
    q += 160;
    GE_CHECK_EQ_INT(r.data[q + 3], 48);                     /* nothing else was added */
    missionApplyFree(&r);
}

int main(void)
{
    MissionApplyResult r;
    char err[128] = "";
    const Mission *m;
    int ok;
    uint32_t pads, stream, n, o;
    int count = 0, guards = 0, collect = 0;

    GE_TEST_BEGIN("mission apply");
    build();
    m = load(kJson);
    GE_CHECK(m != NULL);
    if (!m) return GE_TEST_END();

    ok = missionApplySetup(m, g_file, g_size, 2, 1.0f, &r, err, sizeof err);
    GE_CHECK_EQ_INT(ok, 1);
    if (!ok) { printf("%s\n", err); return GE_TEST_END(); }
    printf("%s", r.log);

    GE_CHECK_EQ_INT(r.guards_moved, 1);
    GE_CHECK_EQ_INT(r.guards_added, 1);
    GE_CHECK_EQ_INT(r.guards_removed, 1);
    GE_CHECK_EQ_INT(r.objects_added, 1);
    GE_CHECK_EQ_INT(r.objects_moved, 1);
    GE_CHECK_EQ_INT(r.objects_removed, 1);
    GE_CHECK_EQ_INT(r.pads_added, 1);
    GE_CHECK_EQ_INT(r.skipped, 2);     /* the guard "move" of a prop that is a prop; the key add (no key record to clone) */

    /* the original file bytes are untouched except the header, the spawn and nothing else */
    GE_CHECK(r.size > g_size);
    GE_CHECK(!memcmp(r.data + STREAM, g_file + STREAM, 32));              /* old stream still there, unedited */
    GE_CHECK_EQ_INT(rd32(r.data + INTRO + 4), 2);                          /* spawn pad */

    /* pads: 3 originals, p1, the removal sink, a terminator */
    pads = rd32(r.data + 8 * 6);
    GE_CHECK(pads >= g_size && (pads % 8) == 0);
    GE_CHECK_EQ_INT(rd32(r.data + pads + 3 * 56 + 0x28) != 0, 1);          /* p1 has a plink */
    GE_CHECK(fabsf(rdf(r.data + pads + 3 * 56) - 250.0f) < 1e-3f);
    GE_CHECK(fabsf(rdf(r.data + pads + 3 * 56 + 24 + 8) - 1.0f) < 1e-3f);  /* look normalised */
    GE_CHECK_EQ_INT(rd32(r.data + pads + 3 * 56 + 0x28), STR);             /* nearest pad's tile */
    GE_CHECK(rdf(r.data + pads + 4 * 56) > 1000.0f);                       /* the sink is far outside */
    GE_CHECK_EQ_INT(rd32(r.data + pads + 5 * 56 + 0x28), 0);               /* terminator */

    /* the stream */
    stream = rd32(r.data + 8 * 3);
    GE_CHECK(stream >= pads);
    o = stream;
    while (r.data[o + 3] != 48) {
        int t = r.data[o + 3];
        if (count == 0) { GE_CHECK_EQ_INT(rd16(r.data + o + 6), 2); }       /* guard 0 moved to pad 2 */
        if (count == 1) { GE_CHECK_EQ_INT(rd16(r.data + o + 6), 4); }       /* guard 1 removed: on the sink pad (index 4, after p1) */
        if (count == 2) { GE_CHECK_EQ_INT(rd16(r.data + o + 6), 0); }       /* prop moved to pad 0 */
        if (count == 4) { GE_CHECK(rd32(r.data + o + 12) & 0xF0); }          /* pickup removed: not loaded */
        if (t == 9) guards++;
        if (t == 8) collect++;
        o += missionPropdefSize(t);
        count++;
    }
    GE_CHECK_EQ_INT(count, 5 + 3);          /* 5 records, an added guard, its weapon, an added pickup */
    GE_CHECK_EQ_INT(guards, 3);
    GE_CHECK_EQ_INT(collect, 4);
    /* the added guard: chrnum 2, on p1 (pad 3), body 5, ai 7 */
    o = stream + 5 * 0;
    {
        uint32_t q = stream + 32 + 32 + 144 + 160 + 160;                      /* first appended record */
        GE_CHECK_EQ_INT(r.data[q + 3], 9);
        GE_CHECK_EQ_INT(rd16(r.data + q + 4), 2);
        GE_CHECK_EQ_INT(rd16(r.data + q + 6), 3);
        GE_CHECK_EQ_INT(rd16(r.data + q + 8), 5);
        GE_CHECK_EQ_INT(rd16(r.data + q + 10), 7);
        q += 32;                                                               /* its weapon */
        GE_CHECK_EQ_INT(r.data[q + 3], 8);
        GE_CHECK_EQ_INT(rd16(r.data + q + 6), 2);                              /* assigned to chr 2 */
        GE_CHECK_EQ_INT(r.data[q + 144], 17);
        GE_CHECK_EQ_INT(rd16(r.data + q + 4), 210);                            /* the sniper's guard model */
        q += 160;                                                              /* the added pickup */
        GE_CHECK_EQ_INT(r.data[q + 3], 8);
        GE_CHECK_EQ_INT(rd16(r.data + q + 4), 248);
        GE_CHECK_EQ_INT(rd16(r.data + q + 6), 3);
        GE_CHECK_EQ_INT(r.data[q + 144], 61);
        GE_CHECK_EQ_INT(rd32(r.data + q + 8) & 0x4000, 0);
    }
    (void)o;
    missionApplyFree(&r);

    /* world units: with a level scale of 2 the stored (unscaled) pad is half the mission's position */
    ok = missionApplySetup(m, g_file, g_size, -1, 2.0f, &r, err, sizeof err);
    GE_CHECK_EQ_INT(ok, 1);
    if (ok) {
        uint32_t pp = rd32(r.data + 8 * 6);
        GE_CHECK(fabsf(rdf(r.data + pp + 3 * 56) - 125.0f) < 1e-3f);
        GE_CHECK(fabsf(rdf(r.data + pp + 3 * 56 + 4) - 10.0f) < 1e-3f);
        missionApplyFree(&r);
    }

    /* the per-mod EEPROM file: only a campaign UUID may name it */
    {
        char eep[128];
        GE_CHECK_EQ_INT(missionEepPath("3f0c9a52-7d41-4b1e-9a0e-2c6f5b8d1e47", eep, sizeof eep), 1);
        GE_CHECK_STR(eep, "$S/mods/3f0c9a52-7d41-4b1e-9a0e-2c6f5b8d1e47.eep");
        GE_CHECK_EQ_INT(missionEepPath("../ge007", eep, sizeof eep), 0);
        GE_CHECK_EQ_INT(missionEepPath("", eep, sizeof eep), 0);
        GE_CHECK_EQ_INT(missionEepPath("abc", eep, sizeof eep), 0);
        GE_CHECK_EQ_INT(missionEepPath("3f0c9a52/7d41", eep, sizeof eep), 0);
    }

    test_fields();
    test_move_ai();
    test_difficulties();
    test_weapons();
    test_setup_decode();

    /* a file that is not a setup */
    ok = missionApplySetup(m, g_file, 16, -1, 1.0f, &r, err, sizeof err);
    GE_CHECK_EQ_INT(ok, 0);

    return GE_TEST_END();
}
