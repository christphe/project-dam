#include "doc.h"
#include "launch.h"
#include "exportcache.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "cJSON.h"

// ---------------------------------------------------------------------------------------------
// small helpers

bool readFile(const std::string &path, std::string &out)
{
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    out.resize(n > 0 ? (size_t)n : 0);
    size_t got = n > 0 ? fread(&out[0], 1, (size_t)n, f) : 0;
    fclose(f);
    return got == out.size();
}

static double jnum(const cJSON *o, const char *k, double def)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsNumber(v) ? v->valuedouble : def;
}

static bool jvec3(const cJSON *o, const char *k, Vec3 &out)
{
    const cJSON *a = cJSON_GetObjectItemCaseSensitive(o, k);
    if (!cJSON_IsArray(a) || cJSON_GetArraySize(a) < 3) return false;
    for (int i = 0; i < 3; i++) out[i] = (float)cJSON_GetArrayItem(a, i)->valuedouble;
    return true;
}

static void jpoly(const cJSON *o, std::vector<Vec3> &out)
{
    const cJSON *a = cJSON_GetObjectItemCaseSensitive(o, "v");
    const cJSON *p;
    cJSON_ArrayForEach(p, a) {
        if (!cJSON_IsArray(p) || cJSON_GetArraySize(p) < 3) continue;
        out.push_back({ (float)cJSON_GetArrayItem(p, 0)->valuedouble, (float)cJSON_GetArrayItem(p, 1)->valuedouble,
                        (float)cJSON_GetArrayItem(p, 2)->valuedouble });
    }
}

template <class T> static void arrPush(T *&a, int &n, const T &v)
{
    T *nb = (T *)realloc(a, sizeof(T) * (size_t)(n + 1));
    if (!nb) abort();
    a = nb;
    a[n++] = v;
}

template <class T> static void arrErase(T *a, int &n, int i)
{
    for (int k = i; k + 1 < n; k++) a[k] = a[k + 1];
    n--;
}

static void fillStr(char *dst, size_t n, const std::string &s) { snprintf(dst, n, "%s", s.c_str()); }

// ---------------------------------------------------------------------------------------------
// Level

static void xform(const float *m, const float *p, Vec3 &o)
{
    // (x y z 1) * M, row vectors, M[r*4+c]
    o[0] = p[0] * m[0] + p[1] * m[4] + p[2] * m[8] + m[12];
    o[1] = p[0] * m[1] + p[1] * m[5] + p[2] * m[9] + m[13];
    o[2] = p[0] * m[2] + p[1] * m[6] + p[2] * m[10] + m[14];
}

bool Level::load(const std::string &exportDir, std::string &err)
{
    std::string text;
    if (!readFile(exportDir + "/level.json", text)) { err = "cannot read " + exportDir + "/level.json"; return false; }
    cJSON *root = cJSON_Parse(text.c_str());
    if (!root) { err = "level.json is not valid JSON"; return false; }
    if ((int)jnum(root, "version", 0) != 1) { cJSON_Delete(root); err = "unsupported level.json version"; return false; }

    dir = exportDir;
    const cJSON *s = cJSON_GetObjectItemCaseSensitive(root, "level");
    name = cJSON_IsString(s) ? s->valuestring : "";
    s = cJSON_GetObjectItemCaseSensitive(root, "build");
    build = cJSON_IsString(s) ? s->valuestring : "";

    const cJSON *e;
    cJSON_ArrayForEach(e, cJSON_GetObjectItemCaseSensitive(root, "rooms")) {
        LRoom r; r.index = (int)jnum(e, "index", 0); jvec3(e, "min", r.mn); jvec3(e, "max", r.mx);
        rooms.push_back(r);
    }
    cJSON_ArrayForEach(e, cJSON_GetObjectItemCaseSensitive(root, "stan")) {
        LPoly p; p.room = (int)jnum(e, "room", -1); jpoly(e, p.v);
        stan.push_back(std::move(p));
    }
    cJSON_ArrayForEach(e, cJSON_GetObjectItemCaseSensitive(root, "portals")) {
        LPortal p;
        const cJSON *r = cJSON_GetObjectItemCaseSensitive(e, "rooms");
        if (cJSON_IsArray(r) && cJSON_GetArraySize(r) >= 2) {
            p.a = (int)cJSON_GetArrayItem(r, 0)->valuedouble; p.b = (int)cJSON_GetArrayItem(r, 1)->valuedouble;
        }
        jpoly(e, p.v);
        portals.push_back(std::move(p));
    }
    cJSON_ArrayForEach(e, cJSON_GetObjectItemCaseSensitive(root, "pads")) {
        LPad p; p.num = (int)jnum(e, "num", 0); jvec3(e, "pos", p.pos); jvec3(e, "look", p.look); jvec3(e, "up", p.up);
        p.room = (int)jnum(e, "room", -1);
        padIndex[p.num] = (int)pads.size();
        pads.push_back(p);
    }
    cJSON_ArrayForEach(e, cJSON_GetObjectItemCaseSensitive(root, "props")) {
        LProp p; p.index = (int)jnum(e, "index", 0); p.type = (int)jnum(e, "type", 0); p.pad = (int)jnum(e, "pad", 0);
        p.model = (int)jnum(e, "model", -1); p.item = (int)jnum(e, "item", -1); p.ai = (int)jnum(e, "ai", -1);
        p.owner = (int)jnum(e, "owner", -1);
        props.push_back(p);
    }
    cJSON_ArrayForEach(e, cJSON_GetObjectItemCaseSensitive(root, "ailists")) ailists.push_back((int)e->valuedouble);
    cJSON_Delete(root);

    // level.rec is optional for the editor: without it the level is drawn as markers only.
    std::string recPath = exportDir + "/level.rec";
    FILE *probe = fopen(recPath.c_str(), "rb");
    if (probe) {
        fclose(probe);
        if (!levelrecLoadFile(&rec, recPath.c_str())) { err = "level.rec is incomplete or not a version 1 export"; return false; }
        hasRec = true;
        if (std::string(rec.hdr->level) != name && !name.empty()) {
            err = "level.rec and level.json are of different levels";
            levelrecFree(&rec); hasRec = false;
            return false;
        }
    }

    // bounds
    const float big = 1e30f;
    mn = { big, big, big }; mx = { -big, -big, -big };
    auto grow = [&](const Vec3 &p) { for (int i = 0; i < 3; i++) { mn[i] = std::min(mn[i], p[i]); mx[i] = std::max(mx[i], p[i]); } };
    for (auto &r : rooms) { grow(r.mn); grow(r.mx); }
    for (auto &p : pads) grow(p.pos);
    if (hasRec) {
        owners.assign(rec.hdr->nowners, OwnerInfo());
        for (auto &o : owners) { o.mn = { big, big, big }; o.mx = { -big, -big, -big }; }
        for (uint32_t i = 0; i < rec.hdr->nruns; i++) {
            const LevelRecRun *r = rec.runs[i].r;
            if (r->nverts == 0 || r->nfloats % r->nverts != 0) continue;
            const uint32_t stride = r->nfloats / r->nverts;
            if (stride < 4) continue;
            const LevelRecOwner &ow = rec.owners[r->owner];
            OwnerInfo &oi = owners[r->owner];
            for (uint32_t v = 0; v < r->nverts; v++) {
                const float *vp = rec.runs[i].verts + (size_t)v * stride;
                uint32_t mi = (uint32_t)(vp[3] + 0.5f);
                if (mi >= r->nmtx) mi = 0;
                Vec3 w; xform(rec.runs[i].mtx + mi * 16, vp, w);
                for (int k = 0; k < 3; k++) { oi.mn[k] = std::min(oi.mn[k], w[k]); oi.mx[k] = std::max(oi.mx[k], w[k]); }
                oi.valid = true;
            }
            if (ow.kind == LEVELREC_OWNER_ROOM) {
                for (uint32_t v = 0; v + 2 < r->nverts; v += 3) {
                    LTri t; t.room = ow.id; t.cull = r->cull;
                    Vec3 *dst[3] = { &t.a, &t.b, &t.c };
                    for (int k = 0; k < 3; k++) {
                        const float *vp = rec.runs[i].verts + (size_t)(v + k) * stride;
                        uint32_t mi = (uint32_t)(vp[3] + 0.5f);
                        if (mi >= r->nmtx) mi = 0;
                        xform(rec.runs[i].mtx + mi * 16, vp, *dst[k]);
                    }
                    roomTris.push_back(t);
                }
            }
        }
        for (size_t i = 0; i < owners.size(); i++) {
            OwnerInfo &o = owners[i];
            if (!o.valid) continue;
            o.anchor = { (o.mn[0] + o.mx[0]) * 0.5f, o.mn[1], (o.mn[2] + o.mx[2]) * 0.5f };
            if (rec.owners[i].kind == LEVELREC_OWNER_ROOM) { grow(o.mn); grow(o.mx); }
        }
    }
    if (mn[0] > mx[0]) { mn = { -100, -100, -100 }; mx = { 100, 100, 100 }; }
    return true;
}

const LPad *Level::pad(int num) const
{
    auto it = padIndex.find(num);
    return it == padIndex.end() ? nullptr : &pads[(size_t)it->second];
}

const LProp *Level::prop(int index) const
{
    for (auto &p : props) if (p.index == index) return &p;
    return nullptr;
}

int Level::ownerForModel(int model) const
{
    if (!hasRec) return -1;
    for (uint32_t i = 0; i < rec.hdr->nowners; i++)
        if (rec.owners[i].kind == LEVELREC_OWNER_MODEL && rec.owners[i].id == model) return (int)i;
    return -1;
}

// ---------------------------------------------------------------------------------------------
// files

bool Doc::openLevel(const std::string &dir, std::string &err)
{
    // Level owns the file buffer and is not movable: rebuild the member in place.
    level.~Level();
    new (&level) Level();
    hasLevel = false;
    if (!level.load(dir, err)) return false;
    hasLevel = true;
    levelGen++;
    sel = Selection();
    return true;
}

bool Doc::newMission(const std::string &name, const std::string &levelName, std::string &err)
{
    std::string lvl = levelName.empty() && hasLevel ? level.name : levelName;
    if (missionLevelNumber(lvl.c_str()) < 0) { err = "unknown level '" + lvl + "'"; return false; }
    missionFree(&mission);
    missionInit(&mission);
    mission.env.mood = MISSION_MOOD_KEEP;
    fillStr(mission.name, sizeof mission.name, name.empty() ? "New mission" : name);
    fillStr(mission.level, sizeof mission.level, lvl);
    hasMission = true;
    missionPath.clear();
    if (hasLevel) missionPath = level.dir + "/mission.json";
    undoStack.clear();
    dirty = true;
    sel = Selection();
    return true;
}

bool Doc::openLevelByName(const std::string &lvl, bool reexport, std::string &err)
{
    if (missionLevelNumber(lvl.c_str()) < 0) { err = "unknown level '" + lvl + "'"; return false; }
    std::string dir = exportDirFor(gameExe, lvl);
    if (reexport || !exportPresent(dir)) {
        if (!exportRun(gameExe, lvl, dir, err)) return false;
    }
    return openLevel(dir, err);
}

std::string Doc::missingLevel() const
{
    if (!hasMission || !mission.level[0]) return "";
    if (hasLevel && level.name == mission.level) return "";
    return mission.level;
}

bool Doc::openMission(const std::string &path, std::string &err)
{
    Mission m; missionInit(&m);
    char e[256] = "";
    if (!missionLoadFile(path.c_str(), &m, e, sizeof e)) { err = e[0] ? e : "cannot load mission"; missionFree(&m); return false; }
    missionFree(&mission);
    mission = m;   // takes over the owned pointers; m is dropped without freeing
    hasMission = true;
    missionPath = path;
    undoStack.clear();
    dirty = false;
    sel = Selection();
    return true;
}

bool Doc::saveMission(const std::string &pathIn, std::string &err)
{
    if (!hasMission) { err = "no mission is open"; return false; }
    std::string path = pathIn.empty() ? missionPath : pathIn;
    if (path.empty()) { err = "no path to save to"; return false; }
    char e[256] = "";
    if (!missionSaveFile(path.c_str(), &mission, e, sizeof e)) { err = e[0] ? e : "cannot write " + path; return false; }
    missionPath = path;
    dirty = false;
    return true;
}

// ---------------------------------------------------------------------------------------------
// queries

bool Doc::padPos(const MissionPadRef &r, Vec3 &out) const
{
    if (!r.is_new) {
        const LPad *p = hasLevel ? level.pad(r.num) : nullptr;
        if (!p) return false;
        out = p->pos;
        return true;
    }
    for (int i = 0; i < mission.npads; i++)
        if (strcmp(mission.pads[i].id, r.id) == 0) {
            out = { mission.pads[i].pos[0], mission.pads[i].pos[1], mission.pads[i].pos[2] };
            return true;
        }
    return false;
}

int Doc::roomAt(const Vec3 &p) const
{
    int best = -1; float bestVol = 1e38f;
    if (!hasLevel) return -1;
    for (auto &r : level.rooms) {
        const float eps = 1.0f;
        if (p[0] < r.mn[0] - eps || p[0] > r.mx[0] + eps || p[1] < r.mn[1] - eps || p[1] > r.mx[1] + eps ||
            p[2] < r.mn[2] - eps || p[2] > r.mx[2] + eps) continue;
        float vol = (r.mx[0] - r.mn[0]) * (r.mx[1] - r.mn[1]) * (r.mx[2] - r.mn[2]);
        if (vol < bestVol) { bestVol = vol; best = r.index; }
    }
    return best;
}

std::vector<Item> Doc::items() const
{
    std::vector<Item> out;
    if (hasLevel) {
        for (auto &p : level.props) {
            Item it;
            it.guard = p.type == 9;
            it.idx = p.index;
            it.origPad = p.pad;
            it.pad.is_new = 0; it.pad.num = p.pad;
            it.type = p.type; it.model = p.model; it.item = p.item; it.ai = p.ai; it.owner = p.owner;
            if (hasMission) {
                const MissionGuardOp *ops = it.guard ? mission.guards : nullptr;
                int n = it.guard ? mission.nguards : 0;
                const MissionObjectOp *oops = it.guard ? nullptr : mission.objects;
                int on = it.guard ? 0 : mission.nobjects;
                for (int i = 0; i < n; i++)
                    if (ops[i].op != MISSION_OP_ADD && ops[i].prop == p.index) {
                        if (ops[i].op == MISSION_OP_REMOVE) it.removed = true;
                        else { it.removed = false; it.moved = true; it.pad = ops[i].pad; }
                    }
                for (int i = 0; i < on; i++)
                    if (oops[i].op != MISSION_OP_ADD && oops[i].prop == p.index) {
                        if (oops[i].op == MISSION_OP_REMOVE) it.removed = true;
                        else { it.removed = false; it.moved = true; it.pad = oops[i].pad; }
                    }
            }
            it.hasPos = padPos(it.pad, it.pos);
            out.push_back(it);
        }
    }
    if (hasMission) {
        for (int i = 0; i < mission.nguards; i++) {
            const MissionGuardOp &g = mission.guards[i];
            if (g.op != MISSION_OP_ADD) continue;
            Item it; it.added = true; it.guard = true; it.idx = i; it.pad = g.pad; it.type = 9;
            it.model = g.model; it.ai = g.ai; it.weapon = g.weapon;
            it.owner = hasLevel ? level.ownerForModel(g.model) : -1;
            if (it.owner < 0 && hasLevel) {   // fall back on any guard model the level recorded
                for (auto &p : level.props) if (p.type == 9 && p.owner >= 0) { it.owner = p.owner; break; }
            }
            it.hasPos = padPos(it.pad, it.pos);
            out.push_back(it);
        }
        for (int i = 0; i < mission.nobjects; i++) {
            const MissionObjectOp &o = mission.objects[i];
            if (o.op != MISSION_OP_ADD) continue;
            Item it; it.added = true; it.guard = false; it.idx = i; it.pad = o.pad;
            it.type = o.type; it.model = o.model; it.item = o.item;
            it.owner = hasLevel ? level.ownerForModel(o.model) : -1;
            it.hasPos = padPos(it.pad, it.pos);
            out.push_back(it);
        }
    }
    return out;
}

std::vector<std::string> Doc::validate() const
{
    std::vector<std::string> out;
    if (!hasMission) { out.push_back("no mission is open"); return out; }
    char e[256] = "";
    if (!missionValidate(&mission, e, sizeof e)) out.push_back(e);
    if (!hasLevel) return out;
    char buf[200];
    if (level.name != mission.level) {
        snprintf(buf, sizeof buf, "mission level '%s' differs from the loaded export '%s'", mission.level, level.name.c_str());
        out.push_back(buf);
    }
    auto checkPad = [&](const MissionPadRef &r, const char *what, int i) {
        if (!r.is_new && !level.pad(r.num)) {
            snprintf(buf, sizeof buf, "%s %d: pad 0x%x is not in the level", what, i, r.num);
            out.push_back(buf);
        }
    };
    for (int i = 0; i < mission.nguards; i++) {
        const MissionGuardOp &g = mission.guards[i];
        if (g.op != MISSION_OP_ADD) {
            const LProp *p = level.prop(g.prop);
            if (!p) { snprintf(buf, sizeof buf, "guard op %d: prop %d does not exist", i, g.prop); out.push_back(buf); }
            else if (p->type != 9) { snprintf(buf, sizeof buf, "guard op %d: prop %d is not a guard (type %d)", i, g.prop, p->type); out.push_back(buf); }
        }
        if (g.op != MISSION_OP_REMOVE) checkPad(g.pad, "guard op", i);
    }
    for (int i = 0; i < mission.nobjects; i++) {
        const MissionObjectOp &o = mission.objects[i];
        if (o.op != MISSION_OP_ADD) {
            const LProp *p = level.prop(o.prop);
            if (!p) { snprintf(buf, sizeof buf, "object op %d: prop %d does not exist", i, o.prop); out.push_back(buf); }
            else if (p->type == 9) { snprintf(buf, sizeof buf, "object op %d: prop %d is a guard", i, o.prop); out.push_back(buf); }
        } else if (o.type <= 0 || o.model < 0) {
            snprintf(buf, sizeof buf, "object op %d: add needs a type and a model", i);
            out.push_back(buf);
        }
        if (o.op != MISSION_OP_REMOVE) checkPad(o.pad, "object op", i);
    }
    return out;
}

static void jaddVec(cJSON *o, const char *k, const Vec3 &v)
{
    cJSON *a = cJSON_CreateArray();
    for (int i = 0; i < 3; i++) cJSON_AddItemToArray(a, cJSON_CreateNumber(v[i]));
    cJSON_AddItemToObject(o, k, a);
}

static void jaddPadRef(cJSON *o, const char *k, const MissionPadRef &r)
{
    if (r.is_new) cJSON_AddStringToObject(o, k, r.id); else cJSON_AddNumberToObject(o, k, r.num);
}

std::string Doc::describe(const std::string &detail) const
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "level_loaded", hasLevel);
    if (hasLevel) {
        cJSON_AddStringToObject(root, "level", level.name.c_str());
        cJSON_AddStringToObject(root, "build", level.build.c_str());
        cJSON_AddBoolToObject(root, "geometry", level.hasRec);
        cJSON_AddNumberToObject(root, "room_count", (double)level.rooms.size());
        cJSON_AddNumberToObject(root, "pad_count", (double)level.pads.size());
        cJSON_AddNumberToObject(root, "prop_count", (double)level.props.size());
        int guards = 0;
        for (auto &p : level.props) guards += p.type == 9;
        cJSON_AddNumberToObject(root, "guard_count", guards);
        cJSON_AddNumberToObject(root, "object_count", (double)level.props.size() - guards);
        cJSON *rs = cJSON_AddArrayToObject(root, "rooms");
        for (auto &r : level.rooms) {
            cJSON *o = cJSON_CreateObject();
            cJSON_AddNumberToObject(o, "index", r.index);
            jaddVec(o, "min", r.mn); jaddVec(o, "max", r.mx);
            cJSON_AddItemToArray(rs, o);
        }
        if (detail == "pads") {
            cJSON *ps = cJSON_AddArrayToObject(root, "pads");
            for (auto &p : level.pads) {
                cJSON *o = cJSON_CreateObject();
                cJSON_AddNumberToObject(o, "num", p.num);
                jaddVec(o, "pos", p.pos);
                cJSON_AddNumberToObject(o, "room", p.room);
                cJSON_AddItemToArray(ps, o);
            }
        }
    }
    if (detail == "props" || detail == "items") {
        cJSON *ps = cJSON_AddArrayToObject(root, "items");
        for (auto &it : items()) {
            cJSON *o = cJSON_CreateObject();
            cJSON_AddStringToObject(o, "kind", it.guard ? "guard" : "object");
            if (it.added) cJSON_AddNumberToObject(o, "op", it.idx); else cJSON_AddNumberToObject(o, "prop", it.idx);
            cJSON_AddBoolToObject(o, "added", it.added);
            cJSON_AddBoolToObject(o, "moved", it.moved);
            cJSON_AddBoolToObject(o, "removed", it.removed);
            jaddPadRef(o, "pad", it.pad);
            cJSON_AddNumberToObject(o, "type", it.type);
            cJSON_AddNumberToObject(o, "model", it.model);
            cJSON_AddNumberToObject(o, "item", it.item);
            cJSON_AddNumberToObject(o, "ai", it.ai);
            cJSON_AddItemToArray(ps, o);
        }
    }
    cJSON *m = cJSON_AddObjectToObject(root, "mission");
    cJSON_AddBoolToObject(m, "open", hasMission);
    if (hasMission) {
        cJSON_AddStringToObject(m, "name", mission.name);
        cJSON_AddStringToObject(m, "level", mission.level);
        cJSON_AddStringToObject(m, "path", missionPath.c_str());
        cJSON_AddBoolToObject(m, "dirty", dirty);
        cJSON_AddNumberToObject(m, "new_pads", mission.npads);
        cJSON_AddNumberToObject(m, "guard_ops", mission.nguards);
        cJSON_AddNumberToObject(m, "object_ops", mission.nobjects);
        if (detail == "mission") {
            char *js = missionToJson(&mission);
            if (js) {
                cJSON *parsed = cJSON_Parse(js);
                if (parsed) cJSON_AddItemToObject(m, "json", parsed);
                free(js);
            }
        }
    }
    char *s = cJSON_Print(root);
    std::string out = s ? s : "{}";
    free(s);
    cJSON_Delete(root);
    return out;
}

// ---------------------------------------------------------------------------------------------
// play

std::string Doc::defaultSpawn() const
{
    switch (sel.kind) {
    case Selection::PAD: return std::to_string(sel.num);
    case Selection::MPAD: return sel.id;
    case Selection::ITEM:
        for (auto &it : items())
            if (it.added == sel.item.added && it.guard == sel.item.guard && it.idx == sel.item.idx)
                return it.pad.is_new ? std::string(it.pad.id) : std::to_string(it.pad.num);
        break;
    default: break;
    }
    if (hasLevel && !level.pads.empty()) return std::to_string(level.pads[0].num);
    return "";
}

bool Doc::testRun(const std::string &spawnIn, std::string &err)
{
    if (!hasMission) { err = "no mission is open"; return false; }
    std::vector<std::string> issues = validate();
    if (!issues.empty()) { err = "mission is not valid: " + issues[0]; return false; }
    if (missionPath.empty() && hasLevel) missionPath = level.dir + "/mission.json";
    if (!saveMission("", err)) return false;
    int lvl = missionLevelNumber(mission.level);
    if (lvl < 0) { err = "unknown level"; return false; }
    std::string spawn = spawnIn.empty() ? defaultSpawn() : spawnIn;
    return launchGame(gameExe, lvl, missionPath, spawn, lastPlay, err);
}

bool padSpecFromJson(const cJSON *args, PadSpec &out, std::string &err)
{
    const cJSON *pad = cJSON_GetObjectItemCaseSensitive(args, "pad");
    const cJSON *pos = cJSON_GetObjectItemCaseSensitive(args, "pos");
    if (cJSON_IsNumber(pad)) { out.isNew = false; out.num = (int)pad->valuedouble; return true; }
    if (cJSON_IsString(pad)) {
        // a string is a mission pad id, except "0x2715"/"10005": a number written as text
        char *end = nullptr;
        long v = strtol(pad->valuestring, &end, 0);
        if (end && *end == 0 && end != pad->valuestring) { out.isNew = false; out.num = (int)v; return true; }
        out.isNew = true; out.id = pad->valuestring;
        return true;
    }
    if (cJSON_IsArray(pos) && cJSON_GetArraySize(pos) >= 3) {
        out.atPoint = true;
        for (int i = 0; i < 3; i++) out.pos[i] = (float)cJSON_GetArrayItem(pos, i)->valuedouble;
        const cJSON *room = cJSON_GetObjectItemCaseSensitive(args, "room");
        out.room = cJSON_IsNumber(room) ? (int)room->valuedouble : -1;
        return true;
    }
    err = "give \"pad\" (a pad number or a mission pad id) or \"pos\": [x, y, z]";
    return false;
}
