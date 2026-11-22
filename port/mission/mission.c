/* Custom co-op missions: mission.json load/save/validate (format in mission.h). */
#include "mission.h"
#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const struct { const char *name; int num; } kLevels[] = {
    {"dam",33}, {"facility",34}, {"runway",35}, {"surface1",36},
    {"bunker1",9}, {"silo",20}, {"frigate",26}, {"surface2",43},
    {"bunker2",27}, {"statue",22}, {"archives",24}, {"streets",29},
    {"depot",30}, {"train",25}, {"jungle",37}, {"control",23},
    {"caverns",39}, {"cradle",41}, {"aztec",28}, {"egypt",32},
    {"cuba",54},
};

const char *missionLevelNameAt(int i)
{
    return i >= 0 && i < (int)(sizeof(kLevels) / sizeof(kLevels[0])) ? kLevels[i].name : NULL;
}

int missionLevelNumber(const char *name)
{
    if (!name) return -1;
    for (size_t i = 0; i < sizeof(kLevels) / sizeof(kLevels[0]); i++)
        if (strcmp(kLevels[i].name, name) == 0) return kLevels[i].num;
    return -1;
}

static void seterr(char *err, size_t n, const char *msg, const char *ctx)
{
    if (err && n) snprintf(err, n, "%s%s%s", msg, ctx ? ": " : "", ctx ? ctx : "");
}

static void copystr(char *dst, size_t n, const char *src)
{
    if (!src) src = "";
    snprintf(dst, n, "%s", src);
}

void missionInit(Mission *m)
{
    memset(m, 0, sizeof(*m));
    m->format = MISSION_FORMAT;
    m->env.mood = MISSION_MOOD_KEEP;   /* 0 would be "day": a new mission keeps the level's mood */
}

void missionFree(Mission *m)
{
    for (int i = 0; i < m->nscripts; i++) free(m->scripts[i].props);
    free(m->pads); free(m->zones); free(m->guards); free(m->objects);
    free(m->objectives); free(m->scripts);
    if (m->extra) cJSON_Delete((cJSON *)m->extra);
    missionInit(m);
}

/* ---- parse ---------------------------------------------------------------- */

static const char *kKnownKeys[] = {
    "format", "name", "level", "description", "briefing", "pads", "zones",
    "guards", "objects", "objectives", "scripts", "environment",
};

static int getnum(const cJSON *o, const char *k, double def)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsNumber(v) ? (int)v->valuedouble : (int)def;
}

static float getf(const cJSON *o, const char *k, float def)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsNumber(v) ? (float)v->valuedouble : def;
}

static const char *getstr(const cJSON *o, const char *k)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsString(v) ? v->valuestring : NULL;
}

static void getvec(const cJSON *o, const char *k, float out[3])
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    out[0] = out[1] = out[2] = 0;
    if (!cJSON_IsArray(v)) return;
    for (int i = 0; i < 3; i++) {
        const cJSON *e = cJSON_GetArrayItem(v, i);
        if (cJSON_IsNumber(e)) out[i] = (float)e->valuedouble;
    }
}

static int getpad(const cJSON *o, const char *k, MissionPadRef *r)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    memset(r, 0, sizeof(*r));
    r->num = -1;
    if (cJSON_IsNumber(v)) { r->num = (int)v->valuedouble; return 1; }
    if (cJSON_IsString(v)) { r->is_new = 1; copystr(r->id, sizeof(r->id), v->valuestring); return 1; }
    return 0;
}

static int getop(const cJSON *o, MissionOpKind *op)
{
    const char *s = getstr(o, "op");
    if (!s) return 0;
    if (!strcmp(s, "move")) *op = MISSION_OP_MOVE;
    else if (!strcmp(s, "add")) *op = MISSION_OP_ADD;
    else if (!strcmp(s, "remove")) *op = MISSION_OP_REMOVE;
    else return 0;
    return 1;
}

/* Allocates n zeroed items for an array key; returns NULL and 0 if absent. */
static void *allocarr(const cJSON *root, const char *k, size_t sz, int *n, const cJSON **arr)
{
    *arr = cJSON_GetObjectItemCaseSensitive(root, k);
    *n = cJSON_IsArray(*arr) ? cJSON_GetArraySize(*arr) : 0;
    return *n ? calloc((size_t)*n, sz) : NULL;
}

/* Reads an optional number / RGB array; returns 1 when present. */
static int optf(const cJSON *o, const char *k, float *out)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    if (!cJSON_IsNumber(v)) return 0;
    *out = (float)v->valuedouble;
    return 1;
}

static int optrgb(const cJSON *o, const char *k, float out[3])
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    if (!cJSON_IsArray(v) || cJSON_GetArraySize(v) != 3) return 0;
    getvec(o, k, out);
    return 1;
}

static void parseenv(const cJSON *e, MissionEnv *env)
{
    memset(env, 0, sizeof(*env));
    env->mood = MISSION_MOOD_KEEP;
    if (!cJSON_IsObject(e)) return;
    const char *mood = getstr(e, "mood");
    if (mood && !strcmp(mood, "day")) env->mood = MISSION_MOOD_DAY;
    else if (mood && !strcmp(mood, "dusk")) env->mood = MISSION_MOOD_DUSK;
    else if (mood && !strcmp(mood, "night")) env->mood = MISSION_MOOD_NIGHT;
    const cJSON *sun = cJSON_GetObjectItemCaseSensitive(e, "sun");
    if (cJSON_IsObject(sun)) {
        env->has_sun = 1;
        env->sun_azimuth = getf(sun, "azimuth", 0);
        env->sun_elevation = getf(sun, "elevation", 30);
        env->sun_color[0] = env->sun_color[1] = env->sun_color[2] = 1;
        optrgb(sun, "color", env->sun_color);
        env->sun_glow = getf(sun, "glow", 0.4f);
    }
    env->has_sky = optrgb(e, "sky", env->sky);
    env->has_clouds = optrgb(e, "clouds", env->clouds);
    env->has_cloud_speed = optf(e, "cloud_speed", &env->cloud_speed);
    const cJSON *sea = cJSON_GetObjectItemCaseSensitive(e, "sea");
    if (cJSON_IsObject(sea)) {
        const cJSON *en = cJSON_GetObjectItemCaseSensitive(sea, "enabled");
        env->has_sea = 1;
        env->sea_enabled = cJSON_IsBool(en) ? cJSON_IsTrue(en) : 1;
        env->has_sea_height = optf(sea, "height", &env->sea_height);
        env->has_sea_color = optrgb(sea, "color", env->sea_color);
        env->has_swell = optf(sea, "swell", &env->swell);
        env->has_chop = optf(sea, "chop", &env->chop);
        env->has_speed = optf(sea, "speed", &env->speed);
    }
}

int missionParse(const char *json, Mission *m, char *err, size_t errlen)
{
    missionInit(m);
    cJSON *root = cJSON_Parse(json);
    if (!root || !cJSON_IsObject(root)) {
        seterr(err, errlen, "not a JSON object", cJSON_GetErrorPtr());
        cJSON_Delete(root);
        return 0;
    }
    m->format = getnum(root, "format", 0);
    if (m->format != MISSION_FORMAT) {
        seterr(err, errlen, "unsupported mission format", NULL);
        cJSON_Delete(root);
        return 0;
    }
    copystr(m->name, sizeof(m->name), getstr(root, "name"));
    copystr(m->level, sizeof(m->level), getstr(root, "level"));
    copystr(m->description, sizeof(m->description), getstr(root, "description"));
    copystr(m->briefing, sizeof(m->briefing), getstr(root, "briefing"));

    const cJSON *arr, *e;
    int i;

    m->pads = allocarr(root, "pads", sizeof(MissionPad), &m->npads, &arr);
    i = 0; cJSON_ArrayForEach(e, arr) {
        MissionPad *p = &m->pads[i++];
        copystr(p->id, sizeof(p->id), getstr(e, "id"));
        getvec(e, "pos", p->pos); getvec(e, "look", p->look);
        p->room = getnum(e, "room", -1);
    }
    m->zones = allocarr(root, "zones", sizeof(MissionZone), &m->nzones, &arr);
    i = 0; cJSON_ArrayForEach(e, arr) {
        MissionZone *z = &m->zones[i++];
        copystr(z->id, sizeof(z->id), getstr(e, "id"));
        getvec(e, "pos", z->pos);
        z->radius = getf(e, "radius", 100);
        z->room = getnum(e, "room", -1);
    }
    m->guards = allocarr(root, "guards", sizeof(MissionGuardOp), &m->nguards, &arr);
    i = 0; cJSON_ArrayForEach(e, arr) {
        MissionGuardOp *g = &m->guards[i++];
        if (!getop(e, &g->op)) { seterr(err, errlen, "guard without a valid op", NULL); goto fail; }
        g->prop = getnum(e, "prop", -1);
        getpad(e, "pad", &g->pad);
        g->model = getnum(e, "model", -1); g->ai = getnum(e, "ai", -1); g->weapon = getnum(e, "weapon", -1);
    }
    m->objects = allocarr(root, "objects", sizeof(MissionObjectOp), &m->nobjects, &arr);
    i = 0; cJSON_ArrayForEach(e, arr) {
        MissionObjectOp *o = &m->objects[i++];
        if (!getop(e, &o->op)) { seterr(err, errlen, "object without a valid op", NULL); goto fail; }
        o->prop = getnum(e, "prop", -1);
        getpad(e, "pad", &o->pad);
        o->type = getnum(e, "type", -1); o->model = getnum(e, "model", -1); o->item = getnum(e, "item", -1);
    }
    m->objectives = allocarr(root, "objectives", sizeof(MissionObjective), &m->nobjectives, &arr);
    i = 0; cJSON_ArrayForEach(e, arr) {
        MissionObjective *o = &m->objectives[i++];
        copystr(o->id, sizeof(o->id), getstr(e, "id"));
        copystr(o->text, sizeof(o->text), getstr(e, "text"));
        o->difficulty = getnum(e, "difficulty", 0);
    }
    m->scripts = allocarr(root, "scripts", sizeof(MissionScript), &m->nscripts, &arr);
    i = 0; cJSON_ArrayForEach(e, arr) {
        MissionScript *s = &m->scripts[i++];
        copystr(s->use, sizeof(s->use), getstr(e, "use"));
        s->prop = -1; s->pad.num = -1;
        const cJSON *t = cJSON_GetObjectItemCaseSensitive(e, "target");
        const char *kind = t ? getstr(t, "kind") : "mission";
        if (!kind || !strcmp(kind, "mission")) s->kind = MISSION_TGT_MISSION;
        else if (!strcmp(kind, "prop")) { s->kind = MISSION_TGT_PROP; s->prop = getnum(t, "prop", -1); }
        else if (!strcmp(kind, "pad")) { s->kind = MISSION_TGT_PAD; getpad(t, "pad", &s->pad); }
        else if (!strcmp(kind, "zone")) { s->kind = MISSION_TGT_ZONE; copystr(s->zone, sizeof(s->zone), getstr(t, "zone")); }
        else { seterr(err, errlen, "unknown script target kind", kind); goto fail; }
        const cJSON *props = cJSON_GetObjectItemCaseSensitive(e, "props");
        int np = cJSON_IsObject(props) ? cJSON_GetArraySize(props) : 0;
        if (np) {
            s->props = calloc((size_t)np, sizeof(MissionProp));
            const cJSON *pv;
            cJSON_ArrayForEach(pv, props) {
                MissionProp *p = &s->props[s->nprops];
                copystr(p->name, sizeof(p->name), pv->string);
                if (cJSON_IsNumber(pv)) { p->kind = MISSION_VAL_NUMBER; p->num = pv->valuedouble; }
                else if (cJSON_IsBool(pv)) { p->kind = MISSION_VAL_BOOL; p->num = cJSON_IsTrue(pv) ? 1 : 0; }
                else if (cJSON_IsString(pv)) { p->kind = MISSION_VAL_STRING; copystr(p->str, sizeof(p->str), pv->valuestring); }
                else continue;
                s->nprops++;
            }
        }
    }

    parseenv(cJSON_GetObjectItemCaseSensitive(root, "environment"), &m->env);

    /* Keep unknown top-level keys. */
    cJSON *extra = cJSON_CreateObject();
    cJSON *it = root->child;
    while (it) {
        cJSON *next = it->next;
        int known = 0;
        for (size_t k = 0; k < sizeof(kKnownKeys) / sizeof(kKnownKeys[0]); k++)
            if (it->string && !strcmp(it->string, kKnownKeys[k])) known = 1;
        if (!known) cJSON_AddItemToObject(extra, it->string, cJSON_DetachItemViaPointer(root, it));
        it = next;
    }
    m->extra = extra;
    cJSON_Delete(root);
    return 1;

fail:
    cJSON_Delete(root);
    missionFree(m);
    return 0;
}

int missionLoadFile(const char *path, Mission *out, char *err, size_t errlen)
{
    FILE *f = fopen(path, "rb");
    if (!f) { missionInit(out); seterr(err, errlen, "cannot open", path); return 0; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    size_t got = buf ? fread(buf, 1, (size_t)n, f) : 0;
    fclose(f);
    if (!buf) { missionInit(out); seterr(err, errlen, "out of memory", NULL); return 0; }
    buf[got] = 0;
    int ok = missionParse(buf, out, err, errlen);
    free(buf);
    return ok;
}

/* ---- save ----------------------------------------------------------------- */

static cJSON *vec(const float v[3])
{
    cJSON *a = cJSON_CreateArray();
    for (int i = 0; i < 3; i++) cJSON_AddItemToArray(a, cJSON_CreateNumber(v[i]));
    return a;
}

static void addpad(cJSON *o, const char *k, const MissionPadRef *r)
{
    if (r->is_new) cJSON_AddStringToObject(o, k, r->id);
    else if (r->num >= 0) cJSON_AddNumberToObject(o, k, r->num);
}

static const char *opname(MissionOpKind op)
{
    return op == MISSION_OP_MOVE ? "move" : op == MISSION_OP_ADD ? "add" : "remove";
}

static void addopt(cJSON *o, const char *k, int v)
{
    if (v >= 0) cJSON_AddNumberToObject(o, k, v);
}

char *missionToJson(const Mission *m)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "format", MISSION_FORMAT);
    cJSON_AddStringToObject(root, "name", m->name);
    cJSON_AddStringToObject(root, "level", m->level);
    cJSON_AddStringToObject(root, "description", m->description);
    if (m->briefing[0]) cJSON_AddStringToObject(root, "briefing", m->briefing);

    cJSON *a;
    a = cJSON_AddArrayToObject(root, "pads");
    for (int i = 0; i < m->npads; i++) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "id", m->pads[i].id);
        cJSON_AddItemToObject(o, "pos", vec(m->pads[i].pos));
        cJSON_AddItemToObject(o, "look", vec(m->pads[i].look));
        cJSON_AddNumberToObject(o, "room", m->pads[i].room);
        cJSON_AddItemToArray(a, o);
    }
    a = cJSON_AddArrayToObject(root, "zones");
    for (int i = 0; i < m->nzones; i++) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "id", m->zones[i].id);
        cJSON_AddItemToObject(o, "pos", vec(m->zones[i].pos));
        cJSON_AddNumberToObject(o, "radius", m->zones[i].radius);
        cJSON_AddNumberToObject(o, "room", m->zones[i].room);
        cJSON_AddItemToArray(a, o);
    }
    a = cJSON_AddArrayToObject(root, "guards");
    for (int i = 0; i < m->nguards; i++) {
        const MissionGuardOp *g = &m->guards[i];
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "op", opname(g->op));
        if (g->op != MISSION_OP_ADD) addopt(o, "prop", g->prop);
        if (g->op != MISSION_OP_REMOVE) addpad(o, "pad", &g->pad);
        if (g->op == MISSION_OP_ADD) { addopt(o, "model", g->model); addopt(o, "ai", g->ai); addopt(o, "weapon", g->weapon); }
        cJSON_AddItemToArray(a, o);
    }
    a = cJSON_AddArrayToObject(root, "objects");
    for (int i = 0; i < m->nobjects; i++) {
        const MissionObjectOp *ob = &m->objects[i];
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "op", opname(ob->op));
        if (ob->op != MISSION_OP_ADD) addopt(o, "prop", ob->prop);
        if (ob->op != MISSION_OP_REMOVE) addpad(o, "pad", &ob->pad);
        if (ob->op == MISSION_OP_ADD) { addopt(o, "type", ob->type); addopt(o, "model", ob->model); addopt(o, "item", ob->item); }
        cJSON_AddItemToArray(a, o);
    }
    a = cJSON_AddArrayToObject(root, "objectives");
    for (int i = 0; i < m->nobjectives; i++) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "id", m->objectives[i].id);
        cJSON_AddStringToObject(o, "text", m->objectives[i].text);
        cJSON_AddNumberToObject(o, "difficulty", m->objectives[i].difficulty);
        cJSON_AddItemToArray(a, o);
    }
    a = cJSON_AddArrayToObject(root, "scripts");
    for (int i = 0; i < m->nscripts; i++) {
        const MissionScript *s = &m->scripts[i];
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "use", s->use);
        cJSON *t = cJSON_AddObjectToObject(o, "target");
        static const char *kinds[] = { "mission", "prop", "pad", "zone" };
        cJSON_AddStringToObject(t, "kind", kinds[s->kind]);
        if (s->kind == MISSION_TGT_PROP) cJSON_AddNumberToObject(t, "prop", s->prop);
        if (s->kind == MISSION_TGT_PAD) addpad(t, "pad", &s->pad);
        if (s->kind == MISSION_TGT_ZONE) cJSON_AddStringToObject(t, "zone", s->zone);
        cJSON *p = cJSON_AddObjectToObject(o, "props");
        for (int k = 0; k < s->nprops; k++) {
            const MissionProp *v = &s->props[k];
            if (v->kind == MISSION_VAL_NUMBER) cJSON_AddNumberToObject(p, v->name, v->num);
            else if (v->kind == MISSION_VAL_BOOL) cJSON_AddBoolToObject(p, v->name, v->num != 0);
            else cJSON_AddStringToObject(p, v->name, v->str);
        }
        cJSON_AddItemToArray(a, o);
    }
    const MissionEnv *env = &m->env;
    if (env->mood != MISSION_MOOD_KEEP || env->has_sun || env->has_sky || env->has_clouds ||
        env->has_cloud_speed || env->has_sea) {
        cJSON *e = cJSON_AddObjectToObject(root, "environment");
        static const char *moods[] = { "day", "dusk", "night" };
        if (env->mood != MISSION_MOOD_KEEP) cJSON_AddStringToObject(e, "mood", moods[env->mood]);
        if (env->has_sun) {
            cJSON *s = cJSON_AddObjectToObject(e, "sun");
            cJSON_AddNumberToObject(s, "azimuth", env->sun_azimuth);
            cJSON_AddNumberToObject(s, "elevation", env->sun_elevation);
            cJSON_AddItemToObject(s, "color", vec(env->sun_color));
            cJSON_AddNumberToObject(s, "glow", env->sun_glow);
        }
        if (env->has_sky) cJSON_AddItemToObject(e, "sky", vec(env->sky));
        if (env->has_clouds) cJSON_AddItemToObject(e, "clouds", vec(env->clouds));
        if (env->has_cloud_speed) cJSON_AddNumberToObject(e, "cloud_speed", env->cloud_speed);
        if (env->has_sea) {
            cJSON *s = cJSON_AddObjectToObject(e, "sea");
            cJSON_AddBoolToObject(s, "enabled", env->sea_enabled);
            if (env->has_sea_height) cJSON_AddNumberToObject(s, "height", env->sea_height);
            if (env->has_sea_color) cJSON_AddItemToObject(s, "color", vec(env->sea_color));
            if (env->has_swell) cJSON_AddNumberToObject(s, "swell", env->swell);
            if (env->has_chop) cJSON_AddNumberToObject(s, "chop", env->chop);
            if (env->has_speed) cJSON_AddNumberToObject(s, "speed", env->speed);
        }
    }
    if (m->extra)
        for (cJSON *it = ((cJSON *)m->extra)->child; it; it = it->next)
            cJSON_AddItemToObject(root, it->string, cJSON_Duplicate(it, 1));

    char *out = cJSON_Print(root);
    cJSON_Delete(root);
    return out;
}

static int writefile(const char *path, char *json, char *err, size_t errlen)
{
    if (!json) { seterr(err, errlen, "out of memory", NULL); return 0; }
    FILE *f = fopen(path, "wb");
    if (!f) { free(json); seterr(err, errlen, "cannot write", path); return 0; }
    size_t n = strlen(json);
    int ok = fwrite(json, 1, n, f) == n;
    ok &= fputc('\n', f) != EOF;
    ok &= fclose(f) == 0;
    free(json);
    if (!ok) seterr(err, errlen, "write failed", path);
    return ok;
}

static char *readfile(const char *path, char *err, size_t errlen)
{
    FILE *f = fopen(path, "rb");
    if (!f) { seterr(err, errlen, "cannot open", path); return NULL; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc(n > 0 ? (size_t)n + 1 : 1);
    size_t got = buf ? fread(buf, 1, (size_t)n, f) : 0;
    fclose(f);
    if (!buf) { seterr(err, errlen, "out of memory", NULL); return NULL; }
    buf[got] = 0;
    return buf;
}

/* ---- campaign ------------------------------------------------------------- */

void campaignInit(Campaign *c)
{
    memset(c, 0, sizeof(*c));
    c->format = CAMPAIGN_FORMAT;
}

void campaignFree(Campaign *c)
{
    free(c->missions);
    if (c->extra) cJSON_Delete((cJSON *)c->extra);
    campaignInit(c);
}

static const char *kCampaignKeys[] = { "format", "id", "name", "author", "description", "affects", "unlock", "missions" };

int campaignParse(const char *json, Campaign *c, char *err, size_t errlen)
{
    campaignInit(c);
    cJSON *root = cJSON_Parse(json);
    if (!root || !cJSON_IsObject(root)) {
        seterr(err, errlen, "not a JSON object", cJSON_GetErrorPtr());
        cJSON_Delete(root);
        return 0;
    }
    c->format = getnum(root, "format", 0);
    if (c->format != CAMPAIGN_FORMAT) {
        seterr(err, errlen, "unsupported campaign format", NULL);
        cJSON_Delete(root);
        return 0;
    }
    copystr(c->id, sizeof(c->id), getstr(root, "id"));
    copystr(c->name, sizeof(c->name), getstr(root, "name"));
    copystr(c->author, sizeof(c->author), getstr(root, "author"));
    copystr(c->description, sizeof(c->description), getstr(root, "description"));
    const char *unlock = getstr(root, "unlock");
    c->unlock = unlock && !strcmp(unlock, "open") ? CAMPAIGN_UNLOCK_OPEN : CAMPAIGN_UNLOCK_ORDERED;
    const cJSON *aff = cJSON_GetObjectItemCaseSensitive(root, "affects"), *e;
    c->affects = 0;
    cJSON_ArrayForEach(e, aff) {
        if (!cJSON_IsString(e)) continue;
        if (!strcmp(e->valuestring, "story")) c->affects |= CAMPAIGN_AFFECTS_STORY;
        if (!strcmp(e->valuestring, "multiplayer")) c->affects |= CAMPAIGN_AFFECTS_MULTIPLAYER;
    }
    if (!c->affects) c->affects = CAMPAIGN_AFFECTS_STORY;
    const cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "missions");
    int n = cJSON_IsArray(arr) ? cJSON_GetArraySize(arr) : 0;
    if (n) c->missions = calloc((size_t)n, MISSION_ID_LEN);
    cJSON_ArrayForEach(e, arr)
        if (cJSON_IsString(e)) copystr(c->missions[c->nmissions++], MISSION_ID_LEN, e->valuestring);

    cJSON *extra = cJSON_CreateObject();
    cJSON *it = root->child;
    while (it) {
        cJSON *next = it->next;
        int known = 0;
        for (size_t k = 0; k < sizeof(kCampaignKeys) / sizeof(kCampaignKeys[0]); k++)
            if (it->string && !strcmp(it->string, kCampaignKeys[k])) known = 1;
        if (!known) cJSON_AddItemToObject(extra, it->string, cJSON_DetachItemViaPointer(root, it));
        it = next;
    }
    c->extra = extra;
    cJSON_Delete(root);
    return 1;
}

int campaignLoadFile(const char *path, Campaign *out, char *err, size_t errlen)
{
    char *buf = readfile(path, err, errlen);
    if (!buf) { campaignInit(out); return 0; }
    int ok = campaignParse(buf, out, err, errlen);
    free(buf);
    return ok;
}

char *campaignToJson(const Campaign *c)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "format", CAMPAIGN_FORMAT);
    cJSON_AddStringToObject(root, "id", c->id);
    cJSON_AddStringToObject(root, "name", c->name);
    cJSON_AddStringToObject(root, "author", c->author);
    cJSON_AddStringToObject(root, "description", c->description);
    cJSON *aff = cJSON_AddArrayToObject(root, "affects");
    if (c->affects & CAMPAIGN_AFFECTS_STORY) cJSON_AddItemToArray(aff, cJSON_CreateString("story"));
    if (c->affects & CAMPAIGN_AFFECTS_MULTIPLAYER) cJSON_AddItemToArray(aff, cJSON_CreateString("multiplayer"));
    cJSON_AddStringToObject(root, "unlock", c->unlock == CAMPAIGN_UNLOCK_OPEN ? "open" : "ordered");
    cJSON *a = cJSON_AddArrayToObject(root, "missions");
    for (int i = 0; i < c->nmissions; i++) cJSON_AddItemToArray(a, cJSON_CreateString(c->missions[i]));
    if (c->extra)
        for (cJSON *it = ((cJSON *)c->extra)->child; it; it = it->next)
            cJSON_AddItemToObject(root, it->string, cJSON_Duplicate(it, 1));
    char *out = cJSON_Print(root);
    cJSON_Delete(root);
    return out;
}

int campaignSaveFile(const char *path, const Campaign *c, char *err, size_t errlen)
{
    return writefile(path, campaignToJson(c), err, errlen);
}

static int uuidok(const char *s)
{
    static const int dash[] = { 8, 13, 18, 23 };
    if (strlen(s) != 36) return 0;
    for (int i = 0; i < 36; i++) {
        int isdash = i == dash[0] || i == dash[1] || i == dash[2] || i == dash[3];
        char ch = s[i];
        if (isdash ? ch != '-' : !((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F')))
            return 0;
    }
    return 1;
}

int campaignValidate(const Campaign *c, char *err, size_t errlen)
{
    if (c->format != CAMPAIGN_FORMAT) { seterr(err, errlen, "unsupported campaign format", NULL); return 0; }
    if (!uuidok(c->id)) { seterr(err, errlen, "campaign id is not a UUID", c->id); return 0; }
    if (!c->name[0]) { seterr(err, errlen, "campaign has no name", NULL); return 0; }
    if (c->nmissions == 0) { seterr(err, errlen, "campaign has no mission", NULL); return 0; }
    if (c->nmissions > CAMPAIGN_MAX_MISSIONS) { seterr(err, errlen, "too many missions (max 20)", NULL); return 0; }
    for (int i = 0; i < c->nmissions; i++) {
        const char *d = c->missions[i];
        if (!d[0] || strchr(d, '/') || strchr(d, '\\') || strstr(d, ".."))
            { seterr(err, errlen, "bad mission folder name", d); return 0; }
        for (int j = 0; j < i; j++)
            if (!strcmp(d, c->missions[j])) { seterr(err, errlen, "mission listed twice", d); return 0; }
    }
    return 1;
}

int missionSaveFile(const char *path, const Mission *m, char *err, size_t errlen)
{
    char *json = missionToJson(m);
    if (!json) { seterr(err, errlen, "out of memory", NULL); return 0; }
    FILE *f = fopen(path, "wb");
    if (!f) { free(json); seterr(err, errlen, "cannot write", path); return 0; }
    size_t n = strlen(json);
    int ok = fwrite(json, 1, n, f) == n;
    ok &= fputc('\n', f) != EOF;
    ok &= fclose(f) == 0;
    free(json);
    if (!ok) seterr(err, errlen, "write failed", path);
    return ok;
}

/* ---- validate ------------------------------------------------------------- */

static int padknown(const Mission *m, const MissionPadRef *r)
{
    if (!r->is_new) return r->num >= 0;
    for (int i = 0; i < m->npads; i++)
        if (!strcmp(m->pads[i].id, r->id)) return 1;
    return 0;
}

int missionValidate(const Mission *m, char *err, size_t errlen)
{
    char ctx[96];
    if (m->format != MISSION_FORMAT) { seterr(err, errlen, "unsupported mission format", NULL); return 0; }
    if (!m->name[0]) { seterr(err, errlen, "mission has no name", NULL); return 0; }
    if (missionLevelNumber(m->level) < 0) { seterr(err, errlen, "unknown level", m->level); return 0; }
    for (int i = 0; i < m->npads; i++) {
        if (!m->pads[i].id[0]) { seterr(err, errlen, "new pad without an id", NULL); return 0; }
        for (int j = 0; j < i; j++)
            if (!strcmp(m->pads[i].id, m->pads[j].id)) { seterr(err, errlen, "duplicate pad id", m->pads[i].id); return 0; }
    }
    for (int i = 0; i < m->nzones; i++) {
        if (!m->zones[i].id[0]) { seterr(err, errlen, "zone without an id", NULL); return 0; }
        for (int j = 0; j < i; j++)
            if (!strcmp(m->zones[i].id, m->zones[j].id)) { seterr(err, errlen, "duplicate zone id", m->zones[i].id); return 0; }
    }
    for (int i = 0; i < m->nguards; i++) {
        const MissionGuardOp *g = &m->guards[i];
        snprintf(ctx, sizeof(ctx), "guards[%d]", i);
        if (g->op != MISSION_OP_ADD && g->prop < 0) { seterr(err, errlen, "missing prop", ctx); return 0; }
        if (g->op != MISSION_OP_REMOVE && !padknown(m, &g->pad)) { seterr(err, errlen, "unknown pad", ctx); return 0; }
    }
    for (int i = 0; i < m->nobjects; i++) {
        const MissionObjectOp *o = &m->objects[i];
        snprintf(ctx, sizeof(ctx), "objects[%d]", i);
        if (o->op != MISSION_OP_ADD && o->prop < 0) { seterr(err, errlen, "missing prop", ctx); return 0; }
        if (o->op != MISSION_OP_REMOVE && !padknown(m, &o->pad)) { seterr(err, errlen, "unknown pad", ctx); return 0; }
        if (o->op == MISSION_OP_ADD && o->type < 0) { seterr(err, errlen, "added object without a type", ctx); return 0; }
    }
    for (int i = 0; i < m->nobjectives; i++) {
        if (!m->objectives[i].id[0]) { seterr(err, errlen, "objective without an id", NULL); return 0; }
        for (int j = 0; j < i; j++)
            if (!strcmp(m->objectives[i].id, m->objectives[j].id)) { seterr(err, errlen, "duplicate objective id", m->objectives[i].id); return 0; }
    }
    for (int i = 0; i < m->nscripts; i++) {
        const MissionScript *s = &m->scripts[i];
        snprintf(ctx, sizeof(ctx), "scripts[%d]", i);
        if (!s->use[0]) { seterr(err, errlen, "script without 'use'", ctx); return 0; }
        if (s->kind == MISSION_TGT_PROP && s->prop < 0) { seterr(err, errlen, "missing target prop", ctx); return 0; }
        if (s->kind == MISSION_TGT_PAD && !padknown(m, &s->pad)) { seterr(err, errlen, "unknown target pad", ctx); return 0; }
        if (s->kind == MISSION_TGT_ZONE) {
            int found = 0;
            for (int z = 0; z < m->nzones; z++) if (!strcmp(m->zones[z].id, s->zone)) found = 1;
            if (!found) { seterr(err, errlen, "unknown target zone", ctx); return 0; }
        }
    }
    return 1;
}
