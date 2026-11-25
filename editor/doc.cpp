#include "doc.h"
#include "launch.h"
#include "mission_setup.h"
#include "exportcache.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <system_error>
#include <utility>

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

// The room of a point: the stan (walkable floor) tile under it whose height is nearest, -1 when none.
static int roomOfPoint(const Level &l, const Vec3 &p)
{
    int best = -1;
    float bestD = 1e30f;
    for (const LPoly &poly : l.stan) {
        const size_t n = poly.v.size();
        if (n < 3) continue;
        bool in = false;
        for (size_t i = 0, j = n - 1; i < n; j = i++) {
            const Vec3 &a = poly.v[i], &b = poly.v[j];
            if (((a[2] > p[2]) != (b[2] > p[2])) && (p[0] < (b[0] - a[0]) * (p[2] - a[2]) / (b[2] - a[2]) + a[0])) in = !in;
        }
        if (!in) continue;
        const Vec3 &v0 = poly.v[0], &v1 = poly.v[1], &v2 = poly.v[2];
        const Vec3 e1 = { v1[0] - v0[0], v1[1] - v0[1], v1[2] - v0[2] }, e2 = { v2[0] - v0[0], v2[1] - v0[1], v2[2] - v0[2] };
        const float nx = e1[1] * e2[2] - e1[2] * e2[1], ny = e1[2] * e2[0] - e1[0] * e2[2], nz = e1[0] * e2[1] - e1[1] * e2[0];
        const float y = fabsf(ny) > 1e-6f ? v0[1] - (nx * (p[0] - v0[0]) + nz * (p[2] - v0[2])) / ny : v0[1];
        if (fabsf(y - p[1]) < bestD) { bestD = fabsf(y - p[1]); best = poly.room; }
    }
    return best;
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
    // which recorded model owner draws a model (only the renderer knows): guard models and object models are separate ids
    std::map<std::pair<bool, int>, int> &ownerOf = modelOwners;
    ownerOf.clear();
    cJSON_ArrayForEach(e, cJSON_GetObjectItemCaseSensitive(root, "models"))
        ownerOf[{ cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(e, "guard")), (int)jnum(e, "model", -1) }] = (int)jnum(e, "owner", -1);
    const double scale = jnum(root, "scale", 1.0);
    const cJSON *setupName = cJSON_GetObjectItemCaseSensitive(root, "setup");
    const std::string setup = cJSON_IsString(setupName) ? setupName->valuestring : "";
    cJSON_Delete(root);

    // pads, props (with every record field) and AI lists come from the stage setup file the game itself loads
    // (the converted data next to the game, port/mission/mission_setup.h), not from the export
    if (setup.empty()) { err = "level.json has no \"setup\" name: export the level again with this build"; return false; }
    uint8_t *raw = nullptr; uint32_t rawn = 0;
    char em[300] = "";
    if (!missionSetupLoadFile(dataDir.empty() ? "data" : dataDir.c_str(), setup.c_str(), &raw, &rawn, em, sizeof em)) { err = em; return false; }
    SetupData sd;
    const bool dec = missionSetupDecode(raw, rawn, (float)scale, &sd, em, sizeof em);
    free(raw);
    if (!dec) { err = em; return false; }
    for (int i = 0; i < sd.npads; i++) {
        const SetupPad &sp = sd.pads[i];
        LPad p; p.num = sp.num;
        for (int a = 0; a < 3; a++) { p.pos[a] = sp.pos[a]; p.look[a] = sp.look[a]; p.up[a] = sp.up[a]; }
        p.room = roomOfPoint(*this, p.pos);
        if (sp.bound) { p.bound = true; for (int a = 0; a < 3; a++) { p.bmn[a] = sp.bbox[a]; p.bmx[a] = sp.bbox[3 + a]; } }
        padIndex[p.num] = (int)pads.size();
        pads.push_back(p);
    }
    for (int i = 0; i < sd.nprops; i++) {
        const SetupProp &sp = sd.props[i];
        LProp p; p.index = sp.index; p.type = sp.type; p.pad = sp.pad;
        // A door record's pad is ALWAYS a bound-pad index without the +10000 offset (the game reads boundpads[door->pad], prop.c).
        // Every other type uses pad >= 10000 for bound pads. Normalise once here so the rest of the editor resolves one number.
        if (sp.type == 1 && sp.pad >= 0 && sp.pad < 10000) p.pad = sp.pad + 10000;
        p.model = sp.model; p.item = sp.item; p.ai = sp.ai; p.f = sp.f; p.chr = sp.chr; p.carrier = sp.carrier;
        if (sp.model >= 0) {
            auto it = ownerOf.find({ sp.type == 9, sp.model });
            if (it != ownerOf.end()) p.owner = it->second;
        }
        props.push_back(p);
    }
    for (int i = 0; i < sd.nailists; i++) ailists.push_back(sd.ailists[i]);
    missionSetupFree(&sd);

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

int Level::ownerForModel(bool guard, int model) const
{
    if (!hasRec) return -1;
    auto it = modelOwners.find({ guard, model });
    return it == modelOwners.end() ? -1 : it->second;
}

// ---------------------------------------------------------------------------------------------
// files

// The game's data folder (the converted level data lives in data/pccg-<region>/): next to the game executable.
std::string Doc::gameDataDir() const
{
    const char *env = getenv("GE_EDITOR_DATA");
    if (env && *env) return env;
    if (gameExe.empty()) return "data";
    const size_t s = gameExe.find_last_of("/\\");
    return (s == std::string::npos ? std::string(".") : gameExe.substr(0, s)) + "/data";
}

bool Doc::openLevel(const std::string &dir, std::string &err)
{
    // Level owns the file buffer and is not movable: rebuild the member in place.
    level.~Level();
    new (&level) Level();
    hasLevel = false;
    level.dataDir = gameDataDir();
    if (!level.load(dir, err)) return false;
    hasLevel = true;
    levelGen++;
    sel = Selection();
    if (emptyPending && hasMission && mission.level == level.name) {   // File > New mission, "Emptied level", level not loaded yet
        int g = 0, o = 0;
        std::string e2;
        emptyPending = false;
        if (emptyLevel(g, o, e2)) { undoStack.clear(); dirty = true; }
    }
    return true;
}

static std::string missionSlug(const std::string &name)
{
    std::string s;
    for (char c : name) {
        if (isalnum((unsigned char)c)) s += (char)tolower((unsigned char)c);
        else if (!s.empty() && s.back() != '_') s += '_';
    }
    while (!s.empty() && s.back() == '_') s.pop_back();
    return s.empty() ? "mission" : s;
}

std::string Doc::defaultMissionPath(const std::string &name) const
{
    const std::string root = gameDataDir() + "/missions/";
    const std::string base = missionSlug(name);
    std::string folder = base;
    for (int n = 2; n < 1000; n++) {
        std::error_code ec;
        if (!std::filesystem::exists(root + folder, ec)) break;
        folder = base + "_" + std::to_string(n);
    }
    return root + folder + "/mission.json";
}

bool Doc::newMission(const std::string &name, const std::string &levelName, std::string &err, bool emptied)
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
    missionPath = defaultMissionPath(mission.name);
    scriptBuf.clear(); scriptDirty.clear(); scriptCache.clear();
    undoStack.clear();
    dirty = true;
    sel = Selection();
    if (emptied) {
        if (hasLevel && level.name == lvl) {
            int g = 0, o = 0;
            if (!emptyLevel(g, o, err)) return false;
            undoStack.clear();
        } else emptyPending = true;   // the level is exported / loaded next: openLevel does it then
    } else emptyPending = false;
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
    emptyPending = false;
    missionPath = path;
    scriptBuf.clear(); scriptDirty.clear(); scriptCache.clear();
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
    {   // the folders of a new mission's default path
        std::error_code ec;
        const std::filesystem::path parent = std::filesystem::path(path).parent_path();
        if (!parent.empty()) std::filesystem::create_directories(parent, ec);
    }
    char e[256] = "";
    if (!missionSaveFile(path.c_str(), &mission, e, sizeof e)) { err = e[0] ? e : "cannot write " + path; return false; }
    if (!flushScripts(path, err)) return false;
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

// An added item's effective fields: the op's own, the rest from the template record the game clones
// (the level's first one of the type; mission_apply.c).
static void fillAddedFields(const Doc &d, Item &it, const MissionFieldVals &own)
{
    it.given = own.set;
    it.f = own;
    if (!d.hasLevel) return;
    for (const LProp &p : d.level.props)
        if (p.type == it.type) {
            for (int i = 0; i < MISSION_MAX_FIELDS; i++)
                if (!(own.set & ((uint64_t)1 << i)) && (p.f.set & ((uint64_t)1 << i))) { it.f.v[i] = p.f.v[i]; it.f.set |= (uint64_t)1 << i; }
            break;
        }
}

// The facing of a pad reference: level pad look for original pads, mission pad look for new ones.
static float padYaw(const Doc &d, const MissionPadRef &r)
{
    const float *look = nullptr;
    if (r.is_new) {
        for (int i = 0; i < d.mission.npads; i++) if (strcmp(d.mission.pads[i].id, r.id) == 0) look = d.mission.pads[i].look;
    } else if (d.hasLevel) {
        if (const LPad *p = d.level.pad(r.num)) look = p->look.data();
    }
    return look ? atan2f(look[0], look[2]) * 57.29578f : 0.0f;
}

// A door (type 1) on a bound pad: the pad's frame, as the game builds it (prop.c sub_GAME_7F001BD4: x along up x look, y up, z look).
static void setDoorBasis(const Doc &d, Item &it)
{
    if (it.type != 1 || it.pad.is_new || !d.hasLevel) return;
    const LPad *p = d.level.pad(it.pad.num);
    if (!p || !p->bound) return;
    Vec3 u = p->up, l = p->look;
    Vec3 n = { u[1] * l[2] - u[2] * l[1], u[2] * l[0] - u[0] * l[2], u[0] * l[1] - u[1] * l[0] };
    const float nl = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    if (nl < 1e-6f) return;
    for (int i = 0; i < 3; i++) n[i] /= nl;
    it.hasBasis = true; it.bas[0] = n; it.bas[1] = u; it.bas[2] = l;
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
            it.f = p.f; it.chr = p.chr;
            if (hasMission) {
                const MissionGuardOp *ops = it.guard ? mission.guards : nullptr;
                int n = it.guard ? mission.nguards : 0;
                const MissionObjectOp *oops = it.guard ? nullptr : mission.objects;
                int on = it.guard ? 0 : mission.nobjects;
                for (int i = 0; i < n; i++)
                    if (ops[i].op != MISSION_OP_ADD && ops[i].prop == p.index) {
                        if (ops[i].op == MISSION_OP_REMOVE) it.removed = true;
                        else {
                            it.removed = false;
                            if (ops[i].pad.is_new || ops[i].pad.num >= 0) {   // an AI / presence-only op has no pad: the guard stays
                                it.pad = ops[i].pad;
                                it.moved = !(!ops[i].pad.is_new && ops[i].pad.num == p.pad);
                            }
                            if (ops[i].ai >= 0) it.ai = ops[i].ai;
                            it.diffMask = ops[i].diff_mask;
                        }
                    }
                for (int i = 0; i < on; i++)
                    if (oops[i].op != MISSION_OP_ADD && oops[i].prop == p.index) {
                        if (oops[i].op == MISSION_OP_REMOVE) it.removed = true;
                        else {
                            it.removed = false;
                            if (oops[i].pad.is_new || oops[i].pad.num >= 0) { it.moved = true; it.pad = oops[i].pad; }
                            it.diffMask = oops[i].diff_mask;
                        }
                    }
            }
            it.hasPos = padPos(it.pad, it.pos);
            it.yawDeg = padYaw(*this, it.pad);
            setDoorBasis(*this, it);
            out.push_back(it);
        }
        // weapons carried by guards: the record's "pad" field is the carrier's chr number, not a pad. They are
        // drawn with the guard and listed in its properties, and are nothing on their own.
        std::map<int, size_t> guardOfChr;
        for (size_t i = 0; i < out.size(); i++) if (out[i].guard && out[i].chr >= 0) guardOfChr[out[i].chr] = i;
        for (Item &w : out) {
            const LProp *p = w.guard ? nullptr : level.prop(w.idx);
            if (!p || p->carrier < 0) continue;
            w.assigned = true;
            w.hasPos = false;
            w.moved = false;
            auto g = guardOfChr.find(p->carrier);
            if (g == guardOfChr.end()) continue;
            Item &c = out[g->second];
            w.carrierProp = c.idx; w.removed = c.removed; w.pad = c.pad;
            w.hasPos = c.hasPos; w.pos = { c.pos[0], c.pos[1] + 85.0f, c.pos[2] }; w.yawDeg = c.yawDeg;
            MissionWeapon mw; memset(&mw, 0, sizeof mw);
            mw.item = w.item; mw.model = w.model; mw.f = w.f;
            c.weapons.push_back(mw);
        }
    }
    if (hasMission) {
        for (int i = 0; i < mission.nguards; i++) {
            const MissionGuardOp &g = mission.guards[i];
            if (g.op != MISSION_OP_ADD) continue;
            Item it; it.added = true; it.guard = true; it.idx = i; it.pad = g.pad; it.type = 9; it.diffMask = g.diff_mask;
            it.uuid = g.uuid; it.name = g.name;
            it.model = g.model; it.ai = g.ai;
            for (int k = 0; k < g.nweapons && k < MISSION_MAX_WEAPONS; k++) it.weapons.push_back(g.weapons[k]);
            fillAddedFields(*this, it, g.f);
            it.owner = hasLevel ? level.ownerForModel(true, g.model) : -1;
            if (it.owner < 0 && hasLevel) {   // fall back on any guard model the level recorded
                for (auto &p : level.props) if (p.type == 9 && p.owner >= 0) { it.owner = p.owner; break; }
            }
            it.hasPos = padPos(it.pad, it.pos);
            it.yawDeg = padYaw(*this, it.pad);
            out.push_back(it);
        }
        for (int i = 0; i < mission.nobjects; i++) {
            const MissionObjectOp &o = mission.objects[i];
            if (o.op != MISSION_OP_ADD) continue;
            Item it; it.added = true; it.guard = false; it.idx = i; it.pad = o.pad; it.diffMask = o.diff_mask;
            it.uuid = o.uuid; it.name = o.name;
            it.type = o.type; it.model = o.model; it.item = o.item;
            fillAddedFields(*this, it, o.f);
            it.owner = hasLevel ? level.ownerForModel(false, o.model) : -1;
            it.hasPos = padPos(it.pad, it.pos);
            it.yawDeg = padYaw(*this, it.pad);
            out.push_back(it);
        }
    }
    return out;
}

// ---- the mission check ----

// Squared distance of (x, z) to the segment a-b on the ground plane.
static float segDist2(float x, float z, const Vec3 &a, const Vec3 &b)
{
    const float dx = b[0] - a[0], dz = b[2] - a[2], l2 = dx * dx + dz * dz;
    float t = l2 > 1e-9f ? ((x - a[0]) * dx + (z - a[2]) * dz) / l2 : 0.0f;
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    const float px = a[0] + t * dx - x, pz = a[2] + t * dz - z;
    return px * px + pz * pz;
}

bool Doc::floorUnder(const Vec3 &p, float tol, float tolY) const
{
    if (!hasLevel || level.stan.empty()) return true;
    for (const LPoly &poly : level.stan) {
        const size_t n = poly.v.size();
        if (n < 3) continue;
        bool in = false;
        float d2 = 1e30f;
        for (size_t i = 0, j = n - 1; i < n; j = i++) {
            const Vec3 &a = poly.v[i], &b = poly.v[j];
            if (((a[2] > p[2]) != (b[2] > p[2])) && (p[0] < (b[0] - a[0]) * (p[2] - a[2]) / (b[2] - a[2]) + a[0])) in = !in;
            d2 = std::min(d2, segDist2(p[0], p[2], a, b));
        }
        if (!in && d2 > tol * tol) continue;
        const Vec3 &v0 = poly.v[0], &v1 = poly.v[1], &v2 = poly.v[2];
        const Vec3 e1 = { v1[0] - v0[0], v1[1] - v0[1], v1[2] - v0[2] }, e2 = { v2[0] - v0[0], v2[1] - v0[1], v2[2] - v0[2] };
        const float nx = e1[1] * e2[2] - e1[2] * e2[1], ny = e1[2] * e2[0] - e1[0] * e2[2], nz = e1[0] * e2[1] - e1[1] * e2[0];
        const float y = fabsf(ny) > 1e-6f ? v0[1] - (nx * (p[0] - v0[0]) + nz * (p[2] - v0[2])) / ny : v0[1];
        if (fabsf(y - p[1]) <= tolY) return true;
    }
    return false;
}

static std::string padName(const MissionPadRef &r)
{
    char b[48];
    if (r.is_new) snprintf(b, sizeof b, "%s", r.id); else snprintf(b, sizeof b, "0x%x", r.num);
    return b;
}

std::vector<Issue> Doc::checkIssues() const
{
    std::vector<Issue> out;
    if (!hasMission) { Issue i; i.text = "no mission is open"; out.push_back(i); return out; }
    char e[256] = "";
    char buf[400];
    auto add = [&](int sev, const std::string &t) { Issue i; i.sev = sev; i.text = t; out.push_back(i); };
    auto addItem = [&](int sev, const std::string &t, bool added, bool guard, int idx) {
        Issue i; i.sev = sev; i.text = t; i.hasSel = true;
        i.sel.kind = Selection::ITEM; i.sel.item.valid = true; i.sel.item.added = added; i.sel.item.guard = guard; i.sel.item.idx = idx;
        out.push_back(i);
    };
    auto addPad = [&](int sev, const std::string &t, const MissionPadRef &r) {
        Issue i; i.sev = sev; i.text = t; i.hasSel = true;
        if (r.is_new) { i.sel.kind = Selection::MPAD; i.sel.id = r.id; } else { i.sel.kind = Selection::PAD; i.sel.num = r.num; }
        out.push_back(i);
    };
    if (!missionValidate(&mission, e, sizeof e)) add(2, e);
    scriptIssues(out);
    if (!hasLevel) return out;
    if (level.name != mission.level) {
        snprintf(buf, sizeof buf, "mission level '%s' differs from the loaded export '%s'", mission.level, level.name.c_str());
        add(2, buf);
    }
    auto checkPad = [&](const MissionPadRef &r, const char *what, int i, bool added, bool guard, int idx) {
        if (!r.is_new && !level.pad(r.num)) {
            snprintf(buf, sizeof buf, "%s %d: pad 0x%x is not in the level", what, i, r.num);
            addItem(2, buf, added, guard, idx);
            return false;
        }
        return true;
    };
    // ops against the level
    for (int i = 0; i < mission.nguards; i++) {
        const MissionGuardOp &g = mission.guards[i];
        const bool added = g.op == MISSION_OP_ADD;
        const int idx = added ? i : g.prop;
        if (!added) {
            const LProp *p = level.prop(g.prop);
            if (!p) { snprintf(buf, sizeof buf, "guard op %d: prop %d does not exist", i, g.prop); add(2, buf); }
            else if (p->type != 9) { snprintf(buf, sizeof buf, "guard op %d: prop %d is not a guard (type %d)", i, g.prop, p->type); add(2, buf); }
        }
        if (g.op != MISSION_OP_REMOVE && (g.pad.is_new || g.pad.num >= 0) && checkPad(g.pad, "guard op", i, added, true, idx)) {
            Vec3 pp;
            if (padPos(g.pad, pp) && !floorUnder(pp)) {
                snprintf(buf, sizeof buf, "guard op %d stands on pad %s, which is off the walkable floor: it will not spawn", i, padName(g.pad).c_str());
                addItem(2, buf, added, true, idx);
            }
        }
    }
    for (int i = 0; i < mission.nobjects; i++) {
        const MissionObjectOp &o = mission.objects[i];
        const bool added = o.op == MISSION_OP_ADD;
        const int idx = added ? i : o.prop;
        if (!added) {
            const LProp *p = level.prop(o.prop);
            if (!p) { snprintf(buf, sizeof buf, "object op %d: prop %d does not exist", i, o.prop); add(2, buf); }
            else if (p->type == 9) { snprintf(buf, sizeof buf, "object op %d: prop %d is a guard", i, o.prop); add(2, buf); }
            else if (p->type == MISSION_PROPDEF_DOOR && (o.pad.is_new || o.pad.num >= 0) && (o.pad.is_new || o.pad.num < 10000)) {
                snprintf(buf, sizeof buf, "object op %d (door prop %d): %s", i, o.prop, MISSION_DOOR_PAD_MSG);
                addItem(2, buf, false, false, idx);
            }
        } else if (o.type <= 0 || o.model < 0) {
            snprintf(buf, sizeof buf, "object op %d: add needs a type and a model", i);
            addItem(2, buf, true, false, i);
        }
        if (o.op != MISSION_OP_REMOVE && (o.pad.is_new || o.pad.num >= 0) && checkPad(o.pad, "object op", i, added, false, idx)) {
            Vec3 pp;
            if (padPos(o.pad, pp) && !floorUnder(pp, 35.0f, 400.0f)) {
                snprintf(buf, sizeof buf, "object op %d stands on pad %s, which is off the walkable floor", i, padName(o.pad).c_str());
                addItem(1, buf, added, false, idx);
            }
        }
    }
    // the mission's own pads: nobody can walk to one that is off the floor
    for (int i = 0; i < mission.npads; i++) {
        const MissionPad &mp = mission.pads[i];
        const Vec3 pp = { mp.pos[0], mp.pos[1], mp.pos[2] };
        if (!floorUnder(pp)) {
            MissionPadRef r; memset(&r, 0, sizeof r); r.is_new = 1; snprintf(r.id, sizeof r.id, "%s", mp.id);
            snprintf(buf, sizeof buf, "mission pad %s is unreachable: no walkable floor (stan tile) under it", mp.id);
            addPad(1, buf, r);
        }
    }
    // the original pads a script points at
    {
        std::vector<int> seen;
        for (int i = 0; i < mission.nscripts; i++) {
            const MissionScript &s = mission.scripts[i];
            if (s.kind != MISSION_TGT_PAD || s.pad.is_new || std::find(seen.begin(), seen.end(), s.pad.num) != seen.end()) continue;
            seen.push_back(s.pad.num);
            Vec3 pp;
            if (padPos(s.pad, pp) && !floorUnder(pp, 35.0f, 400.0f)) {
                snprintf(buf, sizeof buf, "pad 0x%x (a script's target) is off the walkable floor", s.pad.num);
                addPad(1, buf, s.pad);
            }
        }
    }
    // scripts: target, key, objective
    if (mission.nscripts > 0) {
        const std::vector<Item> its = items();
        auto removedProp = [&](int prop) { for (const Item &it : its) if (!it.added && it.idx == prop) return it.removed; return false; };
        std::vector<int> given;   // item ids that something provides
        for (const Item &it : its) {
            if (it.removed) continue;
            if (it.item >= 0) given.push_back(it.item);
            for (const MissionWeapon &w : it.weapons) if (w.item >= 0) given.push_back(w.item);
        }
        auto propNum = [&](const MissionScript &s, const char *name, double &v) {
            for (int k = 0; k < s.nprops; k++) if (!strcmp(s.props[k].name, name) && s.props[k].kind == MISSION_VAL_NUMBER) { v = s.props[k].num; return true; }
            return false;
        };
        for (int i = 0; i < mission.nscripts; i++) {
            const MissionScript &s = mission.scripts[i];
            double v = 0;
            if (!strcmp(s.use, "give_item") && propNum(s, "item", v)) given.push_back((int)v);
        }
        for (int i = 0; i < mission.nscripts; i++) {
            const MissionScript &s = mission.scripts[i];
            double v = 0;
            if (s.kind == MISSION_TGT_PROP && s.uuid[0]) {
                ItemRef pr; int pty = 0;
                if (!propByUuid(s.uuid, pr, pty)) {
                    snprintf(buf, sizeof buf, "script %d (%s): its target uuid %s is not a prop the mission adds (deleted?)", i, s.use, s.uuid);
                    add(2, buf);
                }
            } else if (s.kind == MISSION_TGT_PROP) {
                ItemRef pr; int pty = 0;
                if (s.prop >= (int)level.props.size() && hasLevel) {
                    snprintf(buf, sizeof buf, "script %d (%s): its target prop %d is not a prop of the level (it has %d); a prop the mission adds is referenced by its uuid, not a number", i, s.use, s.prop, (int)level.props.size());
                    add(2, buf);
                } else if (s.prop >= 0 && !propByNumber(s.prop, pr, pty)) {
                    snprintf(buf, sizeof buf, "script %d (%s): its target prop %d does not exist", i, s.use, s.prop);
                    add(2, buf);
                } else if (s.prop >= 0 && !pr.added && removedProp(s.prop)) {
                    snprintf(buf, sizeof buf, "script %d (%s): its target prop %d is removed by this mission", i, s.use, s.prop);
                    addItem(2, buf, false, pty == 9, s.prop);
                }
            } else if (s.kind == MISSION_TGT_PAD && !s.pad.is_new && !level.pad(s.pad.num)) {
                snprintf(buf, sizeof buf, "script %d (%s): its target pad 0x%x is not in the level", i, s.use, s.pad.num);
                add(2, buf);
            }
            if (!strcmp(s.use, "key_door") && propNum(s, "key", v) && std::find(given.begin(), given.end(), (int)v) == given.end()) {
                snprintf(buf, sizeof buf, "script %d (key_door): key item %d is provided by no object of the mission or the level", i, (int)v);
                add(1, buf);
            }
            for (int k = 0; k < s.nprops; k++)
                if (!strcmp(s.props[k].name, "objective") && s.props[k].kind == MISSION_VAL_STRING) {
                    bool found = false;
                    for (int o = 0; o < mission.nobjectives; o++) if (!strcmp(mission.objectives[o].id, s.props[k].str)) found = true;
                    if (!found) {
                        snprintf(buf, sizeof buf, "script %d (%s): objective '%s' is not in the mission's objectives", i, s.use, s.props[k].str);
                        add(1, buf);
                    }
                }
        }
    }
    return out;
}

std::vector<std::string> Doc::validate() const
{
    std::vector<std::string> out;
    for (const Issue &i : checkIssues()) out.push_back(i.text);
    return out;
}

std::vector<ModelUse> Doc::levelModels() const
{
    std::vector<ModelUse> out;
    if (!hasLevel) return out;
    for (const auto &kv : level.modelOwners) {
        ModelUse u;
        u.guard = kv.first.first; u.model = kv.first.second; u.owner = kv.second;
        bool first = true;
        for (const LProp &p : level.props)
            if (p.model == u.model && (p.type == 9) == u.guard) {
                u.count++;
                if (first) { u.type = p.type; u.item = p.item; first = false; }
            }
        if (u.owner >= 0) out.push_back(u);
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
                jaddVec(o, "look", p.look);
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
            if (it.added) {
                cJSON_AddStringToObject(o, "uuid", it.uuid.c_str());
                if (!it.name.empty()) cJSON_AddStringToObject(o, "name", it.name.c_str());
                ItemRef ir; ir.valid = true; ir.added = true; ir.guard = it.guard; ir.idx = it.idx;
                cJSON_AddNumberToObject(o, "setup_prop_estimate", addedPropNumber(ir));   // display only: scripts use the uuid
            }
            cJSON_AddBoolToObject(o, "moved", it.moved);
            cJSON_AddBoolToObject(o, "removed", it.removed);
            jaddPadRef(o, "pad", it.pad);
            cJSON_AddNumberToObject(o, "type", it.type);
            cJSON_AddNumberToObject(o, "model", it.model);
            cJSON_AddNumberToObject(o, "item", it.item);
            cJSON_AddNumberToObject(o, "ai", it.ai);
            if (it.assigned) cJSON_AddNumberToObject(o, "carried_by", it.carrierProp);
            if (!it.weapons.empty()) {
                cJSON *ws = cJSON_AddArrayToObject(o, "weapons");
                for (const MissionWeapon &w : it.weapons) {
                    cJSON *wo = cJSON_CreateObject();
                    cJSON_AddNumberToObject(wo, "item", w.item); cJSON_AddNumberToObject(wo, "model", w.model);
                    cJSON_AddItemToArray(ws, wo);
                }
            }
            {   // every record field (mission_fields.h), grouped like mission.json: header flat, the type's own in a sub-object
                const MissionField *f;
                const int type = it.guard ? MISSION_REC_GUARD : it.type;
                const int nf = missionRecFields(type, &f);
                cJSON *fo = nullptr, *sub = nullptr;
                for (int i = 0; i < nf && i < MISSION_MAX_FIELDS; i++) {
                    if (!((it.f.set >> i) & 1)) continue;
                    if (!fo) fo = cJSON_AddObjectToObject(o, "fields");
                    if (f[i].grouped) {
                        if (!sub) sub = cJSON_AddObjectToObject(fo, missionRecGroup(type));
                        cJSON_AddNumberToObject(sub, f[i].name, it.f.v[i]);
                    } else cJSON_AddNumberToObject(fo, f[i].name, it.f.v[i]);
                }
            }
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

bool Doc::testRun(const std::string &spawnIn, bool coop, std::string &err)
{
    if (!hasMission) { err = "no mission is open"; return false; }
    for (const Issue &i : checkIssues())      // warnings do not stop a test run
        if (i.sev >= 2) { err = "mission is not valid: " + i.text; return false; }
    if (missionPath.empty() && hasLevel) missionPath = level.dir + "/mission.json";
    if (!saveMission("", err)) return false;
    int lvl = missionLevelNumber(mission.level);
    if (lvl < 0) { err = "unknown level"; return false; }
    // no spawn chosen (Spawn here / MCP spawn): no --spawn, the mission starts at the level's own start
    const std::string spawn = spawnIn;
    // A mission of a mod runs from the packaged mod (<mods>/<mod>.pmod), the way the game loads it for a player.
    std::string run;
    if (hasMod && !modPackForPlay(missionPath, run, err)) { err = "cannot package the mod: " + err; return false; }
    return launchGame(gameExe, lvl, run.empty() ? missionPath : run, spawn, coop, lastPlay, err);
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
