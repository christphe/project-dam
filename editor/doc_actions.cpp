// The mission edits of Doc. Each takes a snapshot of the mission first and pushes it on success,
// so one action is one undo step (also when it creates a pad on the way).
#include "doc.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

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

std::string Doc::snapshot() const
{
    char *js = missionToJson(&mission);
    std::string s = js ? js : "";
    free(js);
    return s;
}

void Doc::commit(const std::string &snap)
{
    undoStack.push_back(snap);
    if (undoStack.size() > 100) undoStack.erase(undoStack.begin());
    dirty = true;
}

bool Doc::undo(std::string &err)
{
    if (undoStack.empty()) { err = "nothing to undo"; return false; }
    Mission m; missionInit(&m);
    char e[256] = "";
    if (!missionParse(undoStack.back().c_str(), &m, e, sizeof e)) { err = e; missionFree(&m); return false; }
    undoStack.pop_back();
    missionFree(&mission);
    mission = m;
    dirty = true;
    sel = Selection();
    return true;
}

std::string Doc::newPadId() const
{
    for (int n = 1;; n++) {
        char id[MISSION_ID_LEN];
        snprintf(id, sizeof id, "p%d", n);
        bool used = false;
        for (int i = 0; i < mission.npads; i++) if (strcmp(mission.pads[i].id, id) == 0) used = true;
        if (!used) return id;
    }
}

std::string Doc::createPadRaw(const Vec3 &pos, int room)
{
    MissionPad p; memset(&p, 0, sizeof p);
    std::string id = newPadId();
    fillStr(p.id, sizeof p.id, id);
    for (int i = 0; i < 3; i++) p.pos[i] = pos[i];
    p.look[0] = 0; p.look[1] = 0; p.look[2] = 1;
    p.room = room >= 0 ? room : roomAt(pos);
    arrPush(mission.pads, mission.npads, p);
    return id;
}

bool Doc::createPad(const Vec3 &pos, int room, std::string &id, std::string &err)
{
    if (!hasMission) { err = "no mission is open"; return false; }
    std::string snap = snapshot();
    id = createPadRaw(pos, room);
    commit(snap);
    return true;
}

// Resolves a pad argument; a point creates a mission pad there (no undo step of its own).
bool Doc::resolvePad(const PadSpec &s, MissionPadRef &out, std::string &err)
{
    memset(&out, 0, sizeof out);
    if (s.atPoint) {
        std::string id = createPadRaw(s.pos, s.room);
        out.is_new = 1; fillStr(out.id, sizeof out.id, id);
        return true;
    }
    if (s.isNew) {
        for (int i = 0; i < mission.npads; i++)
            if (s.id == mission.pads[i].id) { out.is_new = 1; fillStr(out.id, sizeof out.id, s.id); return true; }
        err = "no mission pad '" + s.id + "'";
        return false;
    }
    if (hasLevel && !level.pad(s.num)) {
        char b[80]; snprintf(b, sizeof b, "pad 0x%x is not in the level", s.num);
        err = b;
        return false;
    }
    out.is_new = 0; out.num = s.num;
    return true;
}

bool Doc::addGuard(const PadSpec &pad, int model, int ai, int weapon, ItemRef &out, std::string &err)
{
    if (!hasMission) { err = "no mission is open"; return false; }
    std::string snap = snapshot();
    MissionGuardOp g; memset(&g, 0, sizeof g);
    g.op = MISSION_OP_ADD; g.prop = -1; g.model = model; g.ai = ai;
    if (weapon >= 0) { g.nweapons = 1; g.weapons[0].item = weapon; g.weapons[0].model = -1; }
    if (!resolvePad(pad, g.pad, err)) return false;
    arrPush(mission.guards, mission.nguards, g);
    commit(snap);
    out.valid = true; out.added = true; out.guard = true; out.idx = mission.nguards - 1;
    return true;
}

bool Doc::addObject(const PadSpec &pad, int type, int model, int item, ItemRef &out, std::string &err)
{
    if (!hasMission) { err = "no mission is open"; return false; }
    std::string snap = snapshot();
    MissionObjectOp o; memset(&o, 0, sizeof o);
    o.op = MISSION_OP_ADD; o.prop = -1; o.type = type; o.model = model; o.item = item;
    if (!resolvePad(pad, o.pad, err)) return false;
    arrPush(mission.objects, mission.nobjects, o);
    commit(snap);
    out.valid = true; out.added = true; out.guard = false; out.idx = mission.nobjects - 1;
    return true;
}


bool Doc::findOp(bool guard, int prop, int &opIndex) const
{
    opIndex = -1;
    if (guard) {
        for (int i = 0; i < mission.nguards; i++)
            if (mission.guards[i].op != MISSION_OP_ADD && mission.guards[i].prop == prop) opIndex = i;
    } else {
        for (int i = 0; i < mission.nobjects; i++)
            if (mission.objects[i].op != MISSION_OP_ADD && mission.objects[i].prop == prop) opIndex = i;
    }
    return opIndex >= 0;
}

bool Doc::itemFromProp(int prop, ItemRef &out, std::string &err) const
{
    if (!hasLevel) { err = "no level export is loaded (original props need level.json)"; return false; }
    const LProp *p = level.prop(prop);
    if (!p) { err = "the level has no prop " + std::to_string(prop); return false; }
    out.valid = true; out.added = false; out.guard = p->type == 9; out.idx = prop;
    return true;
}

bool Doc::isAssignedWeapon(const ItemRef &it) const
{
    if (!it.valid || it.added || it.guard || !hasLevel) return false;
    const LProp *p = level.prop(it.idx);
    return p && p->carrier >= 0;
}

bool Doc::itemInRange(const ItemRef &it) const
{
    if (!it.valid || it.idx < 0) return false;
    if (!it.added) return true;
    return it.guard ? it.idx < mission.nguards : it.idx < mission.nobjects;
}

bool Doc::moveItem(const ItemRef &it, const PadSpec &padSpec, std::string &err)
{
    if (!hasMission) { err = "no mission is open"; return false; }
    if (!itemInRange(it)) { err = "no such item"; return false; }
    if (isAssignedWeapon(it)) { err = "a weapon a guard carries goes with its guard"; return false; }
    std::string snap = snapshot();
    MissionPadRef ref;
    if (!resolvePad(padSpec, ref, err)) return false;
    applyMove(it, ref);
    commit(snap);
    return true;
}

// The edit of moveItem without the undo step: an original prop gets/changes its move op, an added
// one its pad.
bool Doc::applyMove(const ItemRef &it, const MissionPadRef &ref)
{
    if (it.added) {
        if (it.guard) mission.guards[it.idx].pad = ref; else mission.objects[it.idx].pad = ref;
        return true;
    }
    int op;
    if (it.guard) {
        if (findOp(true, it.idx, op)) { mission.guards[op].op = MISSION_OP_MOVE; mission.guards[op].pad = ref; }
        else {
            MissionGuardOp g; memset(&g, 0, sizeof g);
            g.op = MISSION_OP_MOVE; g.prop = it.idx; g.pad = ref; g.model = g.ai = -1;
            arrPush(mission.guards, mission.nguards, g);
        }
    } else {
        if (findOp(false, it.idx, op)) { mission.objects[op].op = MISSION_OP_MOVE; mission.objects[op].pad = ref; }
        else {
            MissionObjectOp o; memset(&o, 0, sizeof o);
            o.op = MISSION_OP_MOVE; o.prop = it.idx; o.pad = ref; o.type = o.model = o.item = -1;
            arrPush(mission.objects, mission.nobjects, o);
        }
    }
    return true;
}

bool Doc::removeItem(const ItemRef &it, std::string &err)
{
    if (!hasMission) { err = "no mission is open"; return false; }
    if (!itemInRange(it)) { err = "no such item"; return false; }
    if (isAssignedWeapon(it)) { err = "a weapon a guard carries goes with its guard: remove the guard"; return false; }
    std::string snap = snapshot();
    if (it.added) {
        if (it.guard) arrErase(mission.guards, mission.nguards, it.idx);
        else arrErase(mission.objects, mission.nobjects, it.idx);
        sel = Selection();
        commit(snap);
        return true;
    }
    int op;
    if (it.guard) {
        if (findOp(true, it.idx, op)) mission.guards[op].op = MISSION_OP_REMOVE;
        else {
            MissionGuardOp g; memset(&g, 0, sizeof g);
            g.op = MISSION_OP_REMOVE; g.prop = it.idx; g.model = g.ai = -1;
            arrPush(mission.guards, mission.nguards, g);
        }
    } else {
        if (findOp(false, it.idx, op)) mission.objects[op].op = MISSION_OP_REMOVE;
        else {
            MissionObjectOp o; memset(&o, 0, sizeof o);
            o.op = MISSION_OP_REMOVE; o.prop = it.idx; o.type = o.model = o.item = -1;
            arrPush(mission.objects, mission.nobjects, o);
        }
    }
    commit(snap);
    return true;
}

bool Doc::setAddedFields(const ItemRef &it, int model, int a, int b, std::string &err)
{
    if (!hasMission) { err = "no mission is open"; return false; }
    if (!it.added || !itemInRange(it)) { err = "only items the mission added have fields to edit"; return false; }
    std::string snap = snapshot();
    if (it.guard) {
        MissionGuardOp &g = mission.guards[it.idx];
        g.model = model; g.ai = a;
        if (b >= 0) { if (g.nweapons == 0) { g.nweapons = 1; g.weapons[0].model = -1; memset(&g.weapons[0].f, 0, sizeof g.weapons[0].f); } g.weapons[0].item = b; }
        else if (g.nweapons == 1) g.nweapons = 0;
    }
    else { MissionObjectOp &o = mission.objects[it.idx]; o.model = model; o.type = a; o.item = b; }
    commit(snap);
    return true;
}

static bool findItem(const Doc &d, const ItemRef &r, Item &out)
{
    for (const Item &it : d.items())
        if (it.added == r.added && it.guard == r.guard && it.idx == r.idx) { out = it; return true; }
    return false;
}

bool Doc::setItemField(const ItemRef &it, int field, double value, std::string &err)
{
    if (!hasMission) { err = "no mission is open"; return false; }
    if (!it.added || !itemInRange(it)) { err = "only items the mission added can be edited; copy an original prop to change it"; return false; }
    MissionFieldVals &fv = it.guard ? mission.guards[it.idx].f : mission.objects[it.idx].f;
    const int n = missionRecFields(it.guard ? MISSION_REC_GUARD : mission.objects[it.idx].type, nullptr);
    if (field < 0 || field >= n || field >= MISSION_MAX_FIELDS) { err = "no such field"; return false; }
    std::string snap = snapshot();
    fv.set |= (uint64_t)1 << field;
    fv.v[field] = value;
    commit(snap);
    return true;
}

bool Doc::copyItem(const ItemRef &r, ClipItem &out, std::string &err) const
{
    Item it;
    if (!itemInRange(r) || !findItem(*this, r, it)) { err = "no such item"; return false; }
    if (isAssignedWeapon(r)) { err = "a weapon a guard carries is copied with its guard"; return false; }
    ClipItem c;
    c.valid = true; c.guard = it.guard; c.type = it.type; c.model = it.model; c.item = it.item; c.ai = it.ai;
    c.weapons = it.weapons;                     // a guard's carried weapons, with their record fields
    c.f = it.f;
    c.yawDeg = getTransform([&] { Selection s; s.kind = Selection::ITEM; s.item = r; return s; }()).yawDeg;
    out = c;
    return true;
}

bool Doc::pasteItem(const ClipItem &c, const PadSpec &pad, ItemRef &out, std::string &err)
{
    if (!hasMission) { err = "no mission is open"; return false; }
    if (!c.valid) { err = "nothing copied"; return false; }
    std::string snap = snapshot();
    MissionPadRef ref;
    if (!resolvePad(pad, ref, err)) return false;
    if (c.guard) {
        MissionGuardOp g; memset(&g, 0, sizeof g);
        g.op = MISSION_OP_ADD; g.prop = -1; g.pad = ref; g.model = c.model; g.ai = c.ai; g.f = c.f;
        for (size_t k = 0; k < c.weapons.size() && k < MISSION_MAX_WEAPONS; k++) g.weapons[g.nweapons++] = c.weapons[k];
        arrPush(mission.guards, mission.nguards, g);
        out.guard = true; out.idx = mission.nguards - 1;
    } else {
        MissionObjectOp o; memset(&o, 0, sizeof o);
        o.op = MISSION_OP_ADD; o.prop = -1; o.pad = ref; o.type = c.type; o.model = c.model; o.item = c.item; o.f = c.f;
        arrPush(mission.objects, mission.nobjects, o);
        out.guard = false; out.idx = mission.nobjects - 1;
    }
    out.valid = true; out.added = true;
    if (pad.atPoint) {      // the new pad is the copy's alone: it gets the copied facing
        Selection s; s.kind = Selection::ITEM; s.item = out;
        std::string e;
        setTransform(s, nullptr, &c.yawDeg, false, e);
    }
    commit(snap);
    return true;
}

bool Doc::duplicateItem(const ItemRef &r, const PadSpec *pad, ItemRef &out, std::string &err)
{
    Item it;
    ClipItem c;
    if (!copyItem(r, c, err) || !findItem(*this, r, it)) return false;
    PadSpec ps;
    if (pad) ps = *pad;
    else if (it.pad.is_new) { ps.isNew = true; ps.id = it.pad.id; }
    else { ps.isNew = false; ps.num = it.pad.num; }
    return pasteItem(c, ps, out, err);
}

bool Doc::setMissionLevel(const std::string &lvl, std::string &err)
{
    if (!hasMission) { err = "no mission is open"; return false; }
    if (missionLevelNumber(lvl.c_str()) < 0) { err = "unknown level '" + lvl + "'"; return false; }
    if (lvl == mission.level) return true;
    const std::string snap = snapshot();
    snprintf(mission.level, sizeof mission.level, "%s", lvl.c_str());
    commit(snap);
    return true;
}
