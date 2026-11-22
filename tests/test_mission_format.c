/* The shared mission formats: mission.json round trip + validation, level.rec parse. */
#include "ge_test.h"
#include "mission.h"
#include "levelrec.h"

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
    GE_CHECK_EQ_INT(m->guards[1].weapon, -1);
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

int main(void)
{
    GE_TEST_BEGIN("mission format");
    test_roundtrip();
    test_validate();
    test_levelrec();
    test_campaign();
    return GE_TEST_END();
}
