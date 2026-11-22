/* Rebuilding a stage setup for a mission (port/mission/mission_apply.c) on a synthetic PC-layout setup. */
#include "ge_test.h"
#include "mission.h"
#include "mission_apply.h"
#include "mission_dir.h"

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

    /* a file that is not a setup */
    ok = missionApplySetup(m, g_file, 16, -1, 1.0f, &r, err, sizeof err);
    GE_CHECK_EQ_INT(ok, 0);

    return GE_TEST_END();
}
