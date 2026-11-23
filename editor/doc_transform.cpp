// Transforms and snapping of Doc: moving a mission pad, or a guard/object, to a free position, and
// turning it. Moving an original prop never edits level data: the prop gets a mission pad of its
// own at the new spot (or reuses the one it already has).
#include "doc.h"

#include <cmath>
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

bool Doc::restoreSnapshot(const std::string &snap, std::string &err)
{
    Mission m; missionInit(&m);
    char e[256] = "";
    if (!missionParse(snap.c_str(), &m, e, sizeof e)) { err = e; missionFree(&m); return false; }
    missionFree(&mission);
    mission = m;
    return true;
}

static MissionPad *findMPad(Mission &m, const char *id)
{
    for (int i = 0; i < m.npads; i++) if (strcmp(m.pads[i].id, id) == 0) return &m.pads[i];
    return nullptr;
}

static float yawOfLook(const float *look) { return atan2f(look[0], look[2]) * 57.29578f; }

int Doc::padRefCount(const char *id) const
{
    int n = 0;
    for (int i = 0; i < mission.nguards; i++)
        if (mission.guards[i].op != MISSION_OP_REMOVE && mission.guards[i].pad.is_new && strcmp(mission.guards[i].pad.id, id) == 0) n++;
    for (int i = 0; i < mission.nobjects; i++)
        if (mission.objects[i].op != MISSION_OP_REMOVE && mission.objects[i].pad.is_new && strcmp(mission.objects[i].pad.id, id) == 0) n++;
    return n;
}

TransformInfo Doc::getTransform(const Selection &s) const
{
    TransformInfo t;
    if (s.kind == Selection::PAD && hasLevel) {
        const LPad *p = level.pad(s.num);
        if (p) { t.valid = true; t.movable = false; t.pos = p->pos; t.yawDeg = yawOfLook(p->look.data()); }
    } else if (s.kind == Selection::MPAD) {
        for (int i = 0; i < mission.npads; i++)
            if (s.id == mission.pads[i].id) {
                const MissionPad &p = mission.pads[i];
                t.valid = t.movable = true;
                t.pos = { p.pos[0], p.pos[1], p.pos[2] };
                t.yawDeg = yawOfLook(p.look);
            }
    } else if (s.kind == Selection::ITEM) {
        for (const Item &it : items()) {
            if (it.added != s.item.added || it.guard != s.item.guard || it.idx != s.item.idx || !it.hasPos) continue;
            t.valid = true; t.movable = !it.removed && !it.assigned; t.pos = it.pos;
            if (it.pad.is_new) {
                for (int i = 0; i < mission.npads; i++)
                    if (strcmp(mission.pads[i].id, it.pad.id) == 0) t.yawDeg = yawOfLook(mission.pads[i].look);
            } else if (hasLevel) {
                if (const LPad *p = level.pad(it.pad.num)) t.yawDeg = yawOfLook(p->look.data());
            }
        }
    }
    return t;
}

bool Doc::setTransform(const Selection &s, const Vec3 *pos, const float *yawDeg, bool commitUndo, std::string &err)
{
    if (!hasMission) { err = "no mission is open"; return false; }
    TransformInfo cur = getTransform(s);
    if (!cur.valid) { err = "nothing selected that has a position"; return false; }
    if (!cur.movable) { err = "original pads are level data and cannot be moved; place a mission pad instead"; return false; }
    if (!pos && !yawDeg) return true;
    std::string snap = snapshot();
    const float yaw = yawDeg ? *yawDeg : cur.yawDeg;
    const float look[3] = { sinf(yaw * 0.017453292f), 0.0f, cosf(yaw * 0.017453292f) };
    auto apply = [&](MissionPad &p) {
        if (pos) for (int i = 0; i < 3; i++) p.pos[i] = (*pos)[i];
        if (yawDeg) for (int i = 0; i < 3; i++) p.look[i] = look[i];
        if (pos) { int r = roomAt({ p.pos[0], p.pos[1], p.pos[2] }); if (r >= 0) p.room = r; }
    };
    if (s.kind == Selection::MPAD) {
        MissionPad *p = findMPad(mission, s.id.c_str());
        if (!p) { err = "no such mission pad"; return false; }
        apply(*p);
    } else if (s.kind == Selection::ITEM) {
        std::vector<Item> all = items();
        const Item *found = nullptr;
        for (const Item &it : all)
            if (it.added == s.item.added && it.guard == s.item.guard && it.idx == s.item.idx) found = &it;
        if (!found) { err = "no such item"; return false; }
        MissionPad *own = found->pad.is_new && padRefCount(found->pad.id) == 1 ? findMPad(mission, found->pad.id) : nullptr;
        if (own) apply(*own);
        else {
            MissionPad np; memset(&np, 0, sizeof np);
            std::string id = newPadId();
            snprintf(np.id, sizeof np.id, "%s", id.c_str());
            const Vec3 at = pos ? *pos : found->pos;
            for (int i = 0; i < 3; i++) np.pos[i] = at[i];
            np.look[0] = look[0]; np.look[1] = look[1]; np.look[2] = look[2];
            np.room = roomAt(at);
            if (np.room < 0 && !found->pad.is_new && hasLevel)
                if (const LPad *lp = level.pad(found->pad.num)) np.room = lp->room;
            arrPush(mission.pads, mission.npads, np);
            MissionPadRef ref; memset(&ref, 0, sizeof ref);
            ref.is_new = 1; snprintf(ref.id, sizeof ref.id, "%s", id.c_str());
            applyMove(s.item, ref);
        }
    } else { err = "select a mission pad, guard or object"; return false; }
    if (commitUndo) commit(snap);
    return true;
}

void Doc::snapPoint(Vec3 &p, const SnapOpts &o, const std::string &ignoreMPad) const
{
    if (o.grid > 0) {
        for (int i = 0; i < 3; i++) {
            if (i == 1 && o.floor) continue;   // the floor decides the height
            p[i] = roundf(p[i] / o.grid) * o.grid;
        }
    }
    if (o.floor && hasLevel) {
        bool have = false; float bestY = 0, bestD = 1e30f;
        for (const LPoly &poly : level.stan) {
            const size_t n = poly.v.size();
            if (n < 3) continue;
            bool in = false;
            for (size_t i = 0, j = n - 1; i < n; j = i++) {
                const Vec3 &a = poly.v[i], &b = poly.v[j];
                if (((a[2] > p[2]) != (b[2] > p[2])) && (p[0] < (b[0] - a[0]) * (p[2] - a[2]) / (b[2] - a[2]) + a[0])) in = !in;
            }
            if (!in) continue;
            const Vec3 &v0 = poly.v[0], &v1 = poly.v[1], &v2 = poly.v[2];
            Vec3 e1 = { v1[0] - v0[0], v1[1] - v0[1], v1[2] - v0[2] }, e2 = { v2[0] - v0[0], v2[1] - v0[1], v2[2] - v0[2] };
            float nx = e1[1] * e2[2] - e1[2] * e2[1], ny = e1[2] * e2[0] - e1[0] * e2[2], nz = e1[0] * e2[1] - e1[1] * e2[0];
            float y = fabsf(ny) > 1e-6f ? v0[1] - (nx * (p[0] - v0[0]) + nz * (p[2] - v0[2])) / ny : v0[1];
            if (fabsf(y - p[1]) < bestD) { bestD = fabsf(y - p[1]); bestY = y; have = true; }
        }
        if (have) p[1] = bestY;
    }
    if (o.pads) {
        float best = 40.0f; bool have = false; Vec3 q{};
        auto consider = [&](const Vec3 &c) {
            float dx = c[0] - p[0], dy = c[1] - p[1], dz = c[2] - p[2];
            float d = sqrtf(dx * dx + dy * dy + dz * dz);
            if (d < best) { best = d; q = c; have = true; }
        };
        if (hasLevel) for (const LPad &lp : level.pads) consider(lp.pos);
        for (int i = 0; i < mission.npads; i++)
            if (ignoreMPad != mission.pads[i].id) consider({ mission.pads[i].pos[0], mission.pads[i].pos[1], mission.pads[i].pos[2] });
        if (have) p = q;
    }
}
