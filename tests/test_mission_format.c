/* The shared mission formats: mission.json round trip + validation, level.rec parse. */
#include "ge_test.h"
#include "mission.h"
#include "mission_apply.h"
#include "levelrec.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *kSample =
    "{ \"format\": 1, \"name\": \"Bunker remix\", \"level\": \"bunker1\",\n"
    "  \"description\": \"test\", \"briefing\": \"Get out.\",\n"
    "  \"pads\": [ { \"id\": \"p1\", \"pos\": [1, 2, 3], \"look\": [0, 0, 1], \"room\": 4 } ],\n"
    "  \"zones\": [ { \"id\": \"z1\", \"pos\": [5, 6, 7], \"radius\": 120, \"room\": 4 } ],\n"
    "  \"guards\": [ { \"op\": \"move\", \"prop\": 17, \"pad\": 10005 },\n"
    "              { \"op\": \"add\", \"pad\": \"p1\", \"model\": 3, \"ai\": 1025 },\n"
    "              { \"op\": \"remove\", \"prop\": 18 } ],\n"
    "  \"objects\": [ { \"op\": \"add\", \"type\": 4, \"pad\": \"p1\", \"item\": 33 } ],\n"
    "  \"objectives\": [ { \"id\": \"o1\", \"text\": \"Find the key\", \"difficulty\": 1 } ],\n"
    "  \"scripts\": [ { \"use\": \"key_door\", \"target\": { \"kind\": \"prop\", \"prop\": 52 },\n"
    "                 \"props\": { \"key\": 33, \"message\": \"Locked\", \"once\": true } },\n"
    "               { \"use\": \"exit_unlock\", \"target\": { \"kind\": \"zone\", \"zone\": \"z1\" } } ],\n"
    "  \"environment\": { \"mood\": \"dusk\", \"sun\": { \"azimuth\": 120, \"elevation\": 12 },\n"
    "                   \"sky\": [0.5, 0.6, 0.7], \"sea\": { \"enabled\": true, \"height\": -120, \"swell\": 2 } },\n"
    "  \"x_future\": { \"keep\": 1 } }\n";

static void check_mission(const Mission *m)
{
    GE_CHECK_EQ_INT(m->format, 1);
    GE_CHECK(!strcmp(m->level, "bunker1"));
    GE_CHECK_EQ_INT(m->npads, 1);
    GE_CHECK_EQ_INT(m->pads[0].room, 4);
    GE_CHECK_EQ_INT(m->nguards, 3);
    GE_CHECK_EQ_INT(m->guards[0].op, MISSION_OP_MOVE);
    GE_CHECK_EQ_INT(m->guards[0].pad.num, 10005);
    GE_CHECK_EQ_INT(m->guards[1].pad.is_new, 1);
    GE_CHECK(!strcmp(m->guards[1].pad.id, "p1"));
    GE_CHECK_EQ_INT(m->guards[1].ai, 1025);
    GE_CHECK_EQ_INT(m->guards[1].nweapons, 0);
    GE_CHECK_EQ_INT(m->nobjects, 1);
    GE_CHECK_EQ_INT(m->objects[0].item, 33);
    GE_CHECK_EQ_INT(m->nobjectives, 1);
    GE_CHECK_EQ_INT(m->objectives[0].difficulty, 1);
    GE_CHECK_EQ_INT(m->nscripts, 2);
    GE_CHECK_EQ_INT(m->scripts[0].kind, MISSION_TGT_PROP);
    GE_CHECK_EQ_INT(m->scripts[0].prop, 52);
    GE_CHECK_EQ_INT(m->scripts[0].nprops, 3);
    GE_CHECK_EQ_INT(m->scripts[1].kind, MISSION_TGT_ZONE);
    GE_CHECK(m->extra != NULL);
    GE_CHECK_EQ_INT(m->env.mood, MISSION_MOOD_DUSK);
    GE_CHECK_EQ_INT(m->env.has_sun, 1);
    GE_CHECK(m->env.sun_azimuth == 120.0f);
    GE_CHECK_EQ_INT(m->env.has_sky, 1);
    GE_CHECK_EQ_INT(m->env.has_clouds, 0);
    GE_CHECK_EQ_INT(m->env.has_sea, 1);
    GE_CHECK_EQ_INT(m->env.sea_enabled, 1);
    GE_CHECK(m->env.sea_height == -120.0f);
    GE_CHECK_EQ_INT(m->env.has_chop, 0);
}

static void test_roundtrip(void)
{
    Mission m, m2;
    char err[128];
    GE_CHECK(missionParse(kSample, &m, err, sizeof(err)));
    check_mission(&m);
    GE_CHECK(missionValidate(&m, err, sizeof(err)));
    char *json = missionToJson(&m);
    GE_CHECK(json != NULL);
    GE_CHECK(strstr(json, "x_future") != NULL);
    GE_CHECK(missionParse(json, &m2, err, sizeof(err)));
    check_mission(&m2);
    free(json);
    missionFree(&m);
    missionFree(&m2);
}

static void test_validate(void)
{
    Mission m;
    char err[128];
    missionInit(&m);
    GE_CHECK_EQ_INT(m.env.mood, MISSION_MOOD_KEEP);
    char *fresh = missionToJson(&m);
    GE_CHECK(strstr(fresh, "environment") == NULL);
    free(fresh);
    GE_CHECK(missionParse("{\"format\":1,\"name\":\"x\",\"level\":\"nowhere\"}", &m, err, sizeof(err)));
    GE_CHECK(!missionValidate(&m, err, sizeof(err)));
    missionFree(&m);
    GE_CHECK(missionParse("{\"format\":1,\"name\":\"x\",\"level\":\"bunker1\","
                          "\"guards\":[{\"op\":\"add\",\"pad\":\"missing\"}]}", &m, err, sizeof(err)));
    GE_CHECK(!missionValidate(&m, err, sizeof(err)));
    missionFree(&m);
    GE_CHECK(!missionParse("{\"format\":2}", &m, err, sizeof(err)));
    GE_CHECK(!missionParse("not json", &m, err, sizeof(err)));
    GE_CHECK_EQ_INT(missionLevelNumber("bunker1"), 9);
    GE_CHECK_EQ_INT(missionLevelNumber("dam"), 33);
}

static void test_levelrec(void)
{
    LevelRecHeader h = { LEVELREC_MAGIC, LEVELREC_VERSION, "test", "bunker1", 1, 1, 1 };
    LevelRecTexture t = { 0x1234, 2, 1 };
    uint8_t px[8] = { 255, 0, 0, 255, 0, 255, 0, 255 };
    LevelRecOwner o = { LEVELREC_OWNER_ROOM, 3, "room3" };
    LevelRecRun r;
    memset(&r, 0, sizeof(r));
    r.tex[0] = 0; r.tex[1] = -1; r.nverts = 3; r.nmtx = 1; r.nfloats = 12;
    float data[16 + 12] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };

    size_t size = sizeof(h) + sizeof(t) + sizeof(px) + sizeof(o) + sizeof(r) + sizeof(data);
    uint8_t *buf = malloc(size), *p = buf;
    memcpy(p, &h, sizeof(h)); p += sizeof(h);
    memcpy(p, &t, sizeof(t)); p += sizeof(t);
    memcpy(p, px, sizeof(px)); p += sizeof(px);
    memcpy(p, &o, sizeof(o)); p += sizeof(o);
    memcpy(p, &r, sizeof(r)); p += sizeof(r);
    memcpy(p, data, sizeof(data));

    LevelRec lr;
    GE_CHECK(levelrecParse(&lr, buf, size));
    GE_CHECK_EQ_INT(lr.tex[0].t->w, 2);
    GE_CHECK_EQ_INT(lr.tex[0].rgba[4], 0);
    GE_CHECK_EQ_INT(lr.owners[0].id, 3);
    GE_CHECK_EQ_INT(lr.runs[0].r->nverts, 3);
    GE_CHECK(lr.runs[0].mtx[15] == 1.0f);
    levelrecFree(&lr);

    /* A truncated file (header only, counts say more follows) is rejected;
     * levelrecParse frees the buffer it took. */
    LevelRec bad;
    uint8_t *copy = malloc(sizeof(h));
    memcpy(copy, &h, sizeof(h));
    GE_CHECK(!levelrecParse(&bad, copy, sizeof(h)));
}

static void test_campaign(void)
{
    Campaign c, c2;
    char err[128];
    GE_CHECK(campaignParse("{\"format\":1,\"id\":\"3f0c9a52-7d41-4b1e-9a0e-2c6f5b8d1e47\",\"name\":\"Bunker stories\",\"author\":\"me\","
                           "\"missions\":[\"bunker_remix\",\"dam_night\"],\"x\":2}", &c, err, sizeof(err)));
    GE_CHECK_EQ_INT(c.nmissions, 2);
    GE_CHECK_EQ_INT(c.unlock, CAMPAIGN_UNLOCK_ORDERED);
    GE_CHECK_EQ_INT(c.affects, CAMPAIGN_AFFECTS_STORY);
    GE_CHECK(campaignValidate(&c, err, sizeof(err)));
    char *json = campaignToJson(&c);
    GE_CHECK(campaignParse(json, &c2, err, sizeof(err)));
    GE_CHECK(!strcmp(c2.missions[1], "dam_night"));
    GE_CHECK(!strcmp(c2.id, "3f0c9a52-7d41-4b1e-9a0e-2c6f5b8d1e47"));
    GE_CHECK(strstr(json, "\"x\"") != NULL);
    free(json);
    campaignFree(&c); campaignFree(&c2);
    GE_CHECK(campaignParse("{\"format\":1,\"name\":\"x\",\"unlock\":\"open\",\"affects\":[\"story\",\"multiplayer\"],\"missions\":[\"../evil\"]}", &c, err, sizeof(err)));
    GE_CHECK_EQ_INT(c.unlock, CAMPAIGN_UNLOCK_OPEN);
    GE_CHECK_EQ_INT(c.affects, CAMPAIGN_AFFECTS_STORY | CAMPAIGN_AFFECTS_MULTIPLAYER);
    GE_CHECK(!campaignValidate(&c, err, sizeof(err)));
    campaignFree(&c);
    GE_CHECK(campaignParse("{\"format\":1,\"name\":\"x\",\"missions\":[\"a\"]}", &c, err, sizeof(err)));
    GE_CHECK(!campaignValidate(&c, err, sizeof(err)));   /* no id */
    campaignFree(&c);
    GE_CHECK(campaignParse("{\"format\":1,\"name\":\"x\",\"missions\":[]}", &c, err, sizeof(err)));
    GE_CHECK(!campaignValidate(&c, err, sizeof(err)));
    campaignFree(&c);
}

/* The record field tables (mission_fields.h): sane, and every type round-trips table -> JSON -> table. */
static void fill(uint8_t *rec, size_t n, unsigned seed)
{
    for (size_t i = 0; i < n; i++) { seed = seed * 1664525u + 1013904223u; rec[i] = (uint8_t)(seed >> 24); }
}

static void test_fields(void)
{
    static const int kTypes[] = { 1, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 17, 20, 21, 36, 39, 40, 42, 43, 45, 47 };
    int groups = 0;
    for (size_t t = 0; t < sizeof kTypes / sizeof *kTypes; t++) {
        const int type = kTypes[t];
        const MissionField *f;
        const int n = missionRecFields(type, &f);
        const char *group = missionRecGroup(type);
        const unsigned size = missionPropdefSize(type);
        uint8_t rec[700], back[700];
        char json[8192], doc[9000];
        Mission m;
        char err[128];
        const MissionFieldVals *fv;
        GE_CHECK(n > 0 && n <= MISSION_MAX_FIELDS);
        GE_CHECK(size > 0 && size <= sizeof rec);
        for (int i = 0; i < n; i++) {
            GE_CHECK(f[i].off + f[i].size <= size);
            GE_CHECK(f[i].size == 1 || f[i].size == 2 || f[i].size == 4);
            GE_CHECK((f[i].kind != MF_FLOAT && f[i].kind != MF_FIXED) || f[i].size == 4);
            GE_CHECK(!f[i].grouped || group != NULL);
            GE_CHECK(f[i].off + f[i].size <= 4 || f[i].off >= 8 || type == MISSION_REC_GUARD);  /* never the type / obj / pad words */
            for (int j = 0; j < i; j++) {               /* unique names (per group) and no overlapping bytes */
                GE_CHECK(strcmp(f[i].name, f[j].name) || f[i].grouped != f[j].grouped);
                GE_CHECK(f[i].off + f[i].size <= f[j].off || f[j].off + f[j].size <= f[i].off);
            }
        }
        if (group) groups++;

        /* a record with every field set to something: float fields get ordinary floats */
        fill(rec, size, 0x1234u + (unsigned)type);
        for (int i = 0; i < n; i++)
            if (f[i].kind == MF_FLOAT) missionFieldSet(&f[i], rec, (double)(int)(rec[f[i].off] % 200) * 0.125 - 7.0);

        /* the export's text, read as the mission's own op */
        GE_CHECK(missionRecJson(json, sizeof json, type, rec) > 0);
        if (type == MISSION_REC_GUARD)
            snprintf(doc, sizeof doc, "{\"format\":1,\"name\":\"x\",\"level\":\"bunker1\",\"guards\":[{\"op\":\"add\",\"pad\":0%s}]}", json);
        else
            snprintf(doc, sizeof doc, "{\"format\":1,\"name\":\"x\",\"level\":\"bunker1\",\"objects\":[{\"op\":\"add\",\"type\":%d,\"pad\":0%s}]}", type, json);
        GE_CHECK(missionParse(doc, &m, err, sizeof err));
        fv = type == MISSION_REC_GUARD ? &m.guards[0].f : &m.objects[0].f;
        GE_CHECK(fv->set == (n >= 64 ? ~0ull : (((uint64_t)1 << n) - 1)));
        memset(back, 0, sizeof back);
        for (int i = 0; i < n; i++) missionFieldSet(&f[i], back, fv->v[i]);
        for (int i = 0; i < n; i++) GE_CHECK(!memcmp(rec + f[i].off, back + f[i].off, f[i].size));

        /* and through the mission writer / reader again */
        {
            char *out = missionToJson(&m);
            Mission m2;
            GE_CHECK(missionParse(out, &m2, err, sizeof err));
            fv = type == MISSION_REC_GUARD ? &m2.guards[0].f : &m2.objects[0].f;
            memset(back, 0, sizeof back);
            for (int i = 0; i < n; i++) if (fv->set & ((uint64_t)1 << i)) missionFieldSet(&f[i], back, fv->v[i]);
            for (int i = 0; i < n; i++) GE_CHECK(!memcmp(rec + f[i].off, back + f[i].off, f[i].size));
            missionFree(&m2);
            free(out);
        }
        missionFree(&m);
    }
    GE_CHECK(groups >= 14);
    GE_CHECK(missionRecFields(2, NULL) == 0);       /* not an object record */
    GE_CHECK(missionRecFields(41, NULL) == 0);
}

/* Absent fields stay absent through a round trip; a guard's chrnum / pad / body / AI list are not fields. */
static void test_fields_absent(void)
{
    Mission m;
    char err[128];
    char *out;
    GE_CHECK(missionParse("{\"format\":1,\"name\":\"x\",\"level\":\"bunker1\","
                          "\"guards\":[{\"op\":\"add\",\"pad\":3,\"model\":65535,\"head\":-1,\"health\":80}],"
                          "\"objects\":[{\"op\":\"add\",\"type\":1,\"pad\":3,\"flags\":4097,\"door\":{\"max_speed\":0.1}},"
                          "{\"op\":\"add\",\"type\":3,\"pad\":3}]}", &m, err, sizeof err));
    GE_CHECK_EQ_INT(m.guards[0].model, 65535);
    GE_CHECK_EQ_INT(__builtin_popcountll(m.guards[0].f.set), 2);
    GE_CHECK_EQ_INT(__builtin_popcountll(m.objects[0].f.set), 2);
    GE_CHECK_EQ_INT(m.objects[1].f.set, 0);
    out = missionToJson(&m);
    GE_CHECK(strstr(out, "\"head\":") != NULL);
    GE_CHECK(strstr(out, "\"max_speed\":") != NULL);
    GE_CHECK(strstr(out, "\"preset\"") == NULL);
    GE_CHECK(strstr(out, "\"chrnum\"") == NULL);
    free(out);
    missionFree(&m);
}

/* A guard's weapons: a list (round trip, with their fields); the older "weapon" key is a one-item list. */
static void test_weapons(void)
{
    Mission m, m2;
    char err[128];
    char *out;
    GE_CHECK(missionParse("{\"format\":1,\"name\":\"x\",\"level\":\"bunker1\","
                          "\"guards\":[{\"op\":\"add\",\"pad\":3,\"weapons\":[{\"item\":12,\"model\":194,\"flags\":16385,\"collectable\":{\"timer\":-2}},{\"item\":8}]},"
                          "{\"op\":\"add\",\"pad\":4,\"weapon\":17},{\"op\":\"add\",\"pad\":5,\"weapon\":-1},{\"op\":\"add\",\"pad\":6}]}", &m, err, sizeof err));
    GE_CHECK_EQ_INT(m.guards[0].nweapons, 2);
    GE_CHECK_EQ_INT(m.guards[0].weapons[0].item, 12);
    GE_CHECK_EQ_INT(m.guards[0].weapons[0].model, 194);
    GE_CHECK_EQ_INT(__builtin_popcountll(m.guards[0].weapons[0].f.set), 2);
    GE_CHECK_EQ_INT(m.guards[0].weapons[1].item, 8);
    GE_CHECK_EQ_INT(m.guards[0].weapons[1].model, -1);
    GE_CHECK_EQ_INT(m.guards[1].nweapons, 1);
    GE_CHECK_EQ_INT(m.guards[1].weapons[0].item, 17);
    GE_CHECK_EQ_INT(m.guards[2].nweapons, 0);
    GE_CHECK_EQ_INT(m.guards[3].nweapons, 0);
    out = missionToJson(&m);
    GE_CHECK(strstr(out, "\"weapons\"") != NULL);
    GE_CHECK(strstr(out, "\"weapon\"") == NULL);          /* written in the list form only */
    GE_CHECK(missionParse(out, &m2, err, sizeof err));
    GE_CHECK_EQ_INT(m2.guards[0].nweapons, 2);
    GE_CHECK_EQ_INT(m2.guards[0].weapons[0].item, 12);
    GE_CHECK_EQ_INT(m2.guards[0].weapons[0].model, 194);
    GE_CHECK(m2.guards[0].weapons[0].f.set == m.guards[0].weapons[0].f.set);
    GE_CHECK_EQ_INT(m2.guards[1].nweapons, 1);
    GE_CHECK_EQ_INT(m2.guards[1].weapons[0].item, 17);
    GE_CHECK_EQ_INT(m2.guards[2].nweapons, 0);
    free(out);
    missionFree(&m);
    missionFree(&m2);
}

int main(void)
{
    GE_TEST_BEGIN("mission format");
    test_roundtrip();
    test_validate();
    test_levelrec();
    test_campaign();
    test_fields();
    test_fields_absent();
    test_weapons();
    return GE_TEST_END();
}
