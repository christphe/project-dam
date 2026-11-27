// ui_overlay.cpp: see ui_app.h for how the editor UI is split.
#include "ui_app.h"
#include <cmath>

namespace ed {

void drawOverlay(App &a, const Overlay &o)
{
    const Doc &d = a.doc;
    const OccupiedPads occ = occupiedPads(a.items);   // pads under a guard / object: drawn as the item
    const ImU32 white = IM_COL32(235, 235, 240, 255), yellow = IM_COL32(255, 220, 40, 255), red = IM_COL32(235, 70, 60, 255),
                orange = IM_COL32(240, 160, 50, 255), green = IM_COL32(80, 220, 110, 255), grey = IM_COL32(130, 130, 140, 150),
                cyan = IM_COL32(70, 200, 230, 160), faint = IM_COL32(90, 200, 120, 90), blue = IM_COL32(120, 170, 255, 255);
    if (d.hasLevel) {
        if (a.showStan && d.level.hasDecor && !d.level.decor.stan.empty()) {
            // the glTF's collision floor: filled translucent tiles, shared edges faint, edges without a neighbour (walls) highlighted
            const ImU32 fillN = IM_COL32(60, 200, 110, 40), fillS = IM_COL32(220, 80, 220, 70), wall = IM_COL32(240, 160, 50, 230);
            for (const DecorStanTile &t : d.level.decor.stan) {
                const size_t n = t.pt.size() / 3;
                ImVec2 s[16];
                bool ok = n <= 16;
                for (size_t i = 0; ok && i < n; i++) ok = o.pt({ t.pt[i * 3], t.pt[i * 3 + 1], t.pt[i * 3 + 2] }, s[i]);
                if (!ok) continue;
                float area = 0;
                for (size_t i = 0; i < n; i++) area += s[i].x * s[(i + 1) % n].y - s[(i + 1) % n].x * s[i].y;
                if (area < 0) for (size_t i = 0; i < n / 2; i++) std::swap(s[i], s[n - 1 - i]);   // ImGui wants one winding
                o.dl->AddConvexPolyFilled(s, (int)n, t.kind ? fillS : fillN);
                for (size_t i = 0; i < n; i++) {
                    const size_t j = (i + 1) % n;
                    const bool isWall = i >= t.nb.size() || t.nb[i] < 0;
                    const Vec3 pa = { t.pt[i * 3], t.pt[i * 3 + 1], t.pt[i * 3 + 2] }, pb = { t.pt[j * 3], t.pt[j * 3 + 1], t.pt[j * 3 + 2] };
                    o.line(pa, pb, isWall ? wall : faint, isWall ? 1.6f : 1.0f);
                }
            }
        } else if (a.showStan)
            for (auto &p : d.level.stan)
                for (size_t i = 0; i < p.v.size(); i++) o.line(p.v[i], p.v[(i + 1) % p.v.size()], faint);
        if (a.showPortals)
            for (auto &p : d.level.portals)
                for (size_t i = 0; i < p.v.size(); i++) o.line(p.v[i], p.v[(i + 1) % p.v.size()], cyan, 1.5f);
        if (d.sel.kind == Selection::ROOM)
            for (auto &r : d.level.rooms)
                if (r.index == d.sel.num) o.box(r.mn, r.mx, yellow, 1.5f);
        if (a.showPads)
            for (auto &p : d.level.pads) {
                bool sel = d.sel.kind == Selection::PAD && d.sel.num == p.num;
                if (!sel && occ.level.count(p.num)) continue;   // a guard's / object's pad: shown as the item
                if (!sel && dist3(p.pos, a.cam.pos) > 12000) continue;
                o.diamond({ p.pos[0], p.pos[1] + 8, p.pos[2] }, sel ? 8.0f : 4.5f, sel ? yellow : blue, true);
                if (sel) {
                    char t[32]; snprintf(t, sizeof t, "pad 0x%x", p.num);
                    o.text(p.pos, t, yellow);
                }
            }
    }
    if (a.showMPads && d.hasMission)
        for (int i = 0; i < d.mission.npads; i++) {
            const MissionPad &p = d.mission.pads[i];
            Vec3 pos = { p.pos[0], p.pos[1], p.pos[2] };
            bool sel = d.sel.kind == Selection::MPAD && d.sel.id == p.id;
            if (!sel && occ.mission.count(p.id)) continue;
            o.diamond({ pos[0], pos[1] + 8, pos[2] }, sel ? 9.0f : 6.0f, sel ? yellow : green, true);
            o.text(pos, p.id, sel ? yellow : green);
        }
    for (const Item &it : a.items) {
        Vec3 mn, mx;
        if (it.assigned || !itemBounds(d, it, mn, mx)) continue;     // a carried weapon is drawn with its guard only
        bool sel = itemIsSel(a, it);
        ImU32 col = it.removed ? grey : it.added ? green : it.moved ? yellow : it.guard ? red : orange;
        if (sel) col = IM_COL32(255, 255, 255, 255);
        Vec3 lmn, lmx;
        itemLocalBox(d, it, lmn, lmx);
        o.boxItem(it, lmn, lmx, col, it.removed && !sel ? 1.0f : sel ? 2.5f : 1.2f);
        if (it.moved && d.hasLevel) {
            const LPad *op = d.level.pad(it.origPad);
            if (op) o.line(op->pos, it.pos, yellow, 1.0f);
        }
        if (sel || dist3(it.pos, a.cam.pos) < 1500) {
            char t[64];
            if (it.added) snprintf(t, sizeof t, "%s +%d", it.guard ? "G" : "O", it.idx);
            else snprintf(t, sizeof t, "%s%s %d", it.guard ? "G" : "O", it.removed ? "x" : "", it.idx);
            o.text({ it.pos[0], mx[1], it.pos[2] }, t, sel ? IM_COL32(255, 255, 255, 255) : col, -10, -14);
        }
    }
}

// ---- transform gizmo ----

bool axisParam(const Vec3 &p0, int axis, const Vec3 &o, const Vec3 &d, float &s)
{
    // closest point of the axis line p0 + s * e_axis to the mouse ray o + t * d
    const Vec3 w = { p0[0] - o[0], p0[1] - o[1], p0[2] - o[2] };
    const float b = d[axis], dw = w[axis], ew = d[0] * w[0] + d[1] * w[1] + d[2] * w[2];
    const float den = 1 - b * b;
    if (fabsf(den) < 1e-4f) return false;
    s = (b * ew - dw) / den;
    return true;
}

bool planeHitY(float y, const Vec3 &o, const Vec3 &d, Vec3 &out)
{
    if (fabsf(d[1]) < 1e-5f) return false;
    float t = (y - o[1]) / d[1];
    if (t < 0) return false;
    out = { o[0] + d[0] * t, y, o[2] + d[2] * t };
    return true;
}

GizmoGeo gizmoGeo(const App &a, const Overlay &ov, const TransformInfo &t)
{
    GizmoGeo g;
    g.len = std::max(dist3(t.pos, a.cam.pos) * 0.11f, 10.0f);
    if (!ov.pt(t.pos, g.c)) return g;
    for (int i = 0; i < 3; i++) {
        Vec3 p = t.pos; p[i] += g.len;
        if (!ov.pt(p, g.tip[i])) return g;
    }
    g.noYaw = a.doc.sel.kind == Selection::ZONE;
    const float yr = t.yawDeg * 0.017453292f;
    Vec3 yp = { t.pos[0] + sinf(yr) * g.len * 1.5f, t.pos[1], t.pos[2] + cosf(yr) * g.len * 1.5f };
    if (!ov.pt(yp, g.yaw)) return g;
    g.ok = true;
    return g;
}

float segDist(ImVec2 p, ImVec2 a, ImVec2 b)
{
    float vx = b.x - a.x, vy = b.y - a.y, wx = p.x - a.x, wy = p.y - a.y;
    float l2 = vx * vx + vy * vy, t = l2 > 0 ? (wx * vx + wy * vy) / l2 : 0;
    t = std::max(0.0f, std::min(1.0f, t));
    float dx = a.x + t * vx - p.x, dy = a.y + t * vy - p.y;
    return sqrtf(dx * dx + dy * dy);
}

// 0 nothing, 1..3 an axis arrow, 4 the centre (plane move), 5 the yaw handle
int gizmoHit(const GizmoGeo &g, ImVec2 m)
{
    auto d2 = [&](ImVec2 q) { return sqrtf((q.x - m.x) * (q.x - m.x) + (q.y - m.y) * (q.y - m.y)); };
    if (!g.noYaw && d2(g.yaw) < 10) return 5;
    if (d2(g.c) < 9) return 4;
    int best = 0; float bd = 7;
    for (int i = 0; i < 3; i++) {
        float d = segDist(m, g.c, g.tip[i]);
        if (d < bd) { bd = d; best = i + 1; }
    }
    return best;
}

std::string ownPadId(const App &a)
{
    if (a.drag.sel.kind == Selection::MPAD) return a.drag.sel.id;
    for (const Item &it : a.items)
        if (it.added == a.drag.sel.item.added && it.guard == a.drag.sel.item.guard && it.idx == a.drag.sel.item.idx && it.pad.is_new)
            return it.pad.id;
    return "";
}

void dragRebase(App &a, const Vec3 &org, const Vec3 &dir)
{
    TransformInfo t = a.doc.getTransform(a.drag.sel);
    a.drag.startPos = t.pos;
    a.drag.grab = t.pos;
    if (a.drag.axis >= 0) { if (!axisParam(t.pos, a.drag.axis, org, dir, a.drag.s0)) a.drag.s0 = 0; }
    else { Vec3 h; if (planeHitY(t.pos[1], org, dir, h)) a.drag.grab = h; }
}

void dragBegin(App &a, int hit, const Vec3 &org, const Vec3 &dir)
{
    if (!ensureMission(a)) return;
    TransformInfo t = a.doc.getTransform(a.doc.sel);
    a.drag = App::Drag();
    a.drag.sel = a.doc.sel;
    a.drag.startPos = t.pos;
    a.drag.snap0 = a.doc.snapshot();
    if (hit == 5) { a.drag.mode = App::Drag::YAW; return; }
    a.drag.mode = App::Drag::MOVE;
    a.drag.axis = hit >= 1 && hit <= 3 ? hit - 1 : -1;
    dragRebase(a, org, dir);
}

void dragEnd(App &a, bool confirm)
{
    std::string e;
    if (a.drag.changed) {
        if (confirm) a.doc.commit(a.drag.snap0);
        else a.doc.restoreSnapshot(a.drag.snap0, e);
    }
    a.drag.mode = App::Drag::NONE;
    a.drag.keymove = false;
}

void dragUpdate(App &a, const Vec3 &org, const Vec3 &dir)
{
    std::string err;
    App::Drag &d = a.drag;
    if (d.mode == App::Drag::MOVE) {
        Vec3 np = d.startPos;
        if (d.axis >= 0) {
            float s;
            if (!axisParam(d.startPos, d.axis, org, dir, s)) return;
            np[d.axis] = d.startPos[d.axis] + (s - d.s0);
        } else {
            Vec3 h;
            if (!planeHitY(d.startPos[1], org, dir, h)) return;
            np[0] = d.startPos[0] + h[0] - d.grab[0];
            np[2] = d.startPos[2] + h[2] - d.grab[2];
        }
        SnapOpts so = a.snapOpts();
        if (d.axis == 1) so.floor = false;   // a height move: the floor must not pull it back
        a.doc.snapPoint(np, so, ownPadId(a));
        // locked: only the axis moves, but the floor snap still sets the height of an X/Z move
        if (d.axis >= 0) for (int i = 0; i < 3; i++) if (i != d.axis && !(i == 1 && so.floor)) np[i] = d.startPos[i];
        if (a.doc.setTransform(d.sel, &np, nullptr, false, err)) d.changed = true;
    } else if (d.mode == App::Drag::YAW) {
        Vec3 h;
        if (!planeHitY(d.startPos[1], org, dir, h)) return;
        float yaw = atan2f(h[0] - d.startPos[0], h[2] - d.startPos[2]) * 57.29578f;
        if (a.snapGrid) yaw = roundf(yaw / 15.0f) * 15.0f;
        if (a.doc.setTransform(d.sel, nullptr, &yaw, false, err)) d.changed = true;
    }
}

void drawGizmo(App &a, const Overlay &ov, const GizmoGeo &g)
{
    if (!g.ok) return;
    static const ImU32 col[3] = { IM_COL32(240, 70, 70, 255), IM_COL32(80, 220, 90, 255), IM_COL32(80, 130, 250, 255) };
    ImDrawList *dl = ov.dl;
    const bool moving = a.drag.mode == App::Drag::MOVE;
    for (int i = 0; i < 3; i++) {
        const bool act = moving && a.drag.axis == i;
        dl->AddLine(g.c, g.tip[i], act ? IM_COL32(255, 255, 80, 255) : col[i], act ? 4.0f : 3.0f);
        dl->AddCircleFilled(g.tip[i], 5.0f, act ? IM_COL32(255, 255, 80, 255) : col[i]);
    }
    dl->AddRectFilled(ImVec2(g.c.x - 5, g.c.y - 5), ImVec2(g.c.x + 5, g.c.y + 5), IM_COL32(240, 240, 240, 255));
    if (!g.noYaw) {
        dl->AddLine(g.c, g.yaw, IM_COL32(255, 170, 40, 255), 2.0f);
        dl->AddCircle(g.yaw, 7.0f, IM_COL32(255, 170, 40, 255), 16, 2.5f);
    }
    if (a.drag.mode != App::Drag::NONE) {
        char t[160];
        if (a.drag.mode == App::Drag::YAW) snprintf(t, sizeof t, "Turning%s - release to confirm, Esc cancels", a.snapGrid ? " (15 deg steps)" : "");
        else snprintf(t, sizeof t, "Moving%s%s - %s, Esc cancels, X/Y/Z lock an axis",
                      a.drag.axis >= 0 ? " locked to " : " in the horizontal plane", a.drag.axis >= 0 ? (a.drag.axis == 0 ? "X" : a.drag.axis == 1 ? "Y" : "Z") : "",
                      a.drag.keymove ? "click confirms" : "release confirms");
        ov.dl->AddText(ImVec2(ov.x + 10, ov.y + 8), IM_COL32(255, 255, 120, 255), t);
        if (moving && a.drag.axis >= 0) {   // guide line through the start along the locked axis
            Vec3 p0 = a.drag.startPos, p1 = a.drag.startPos;
            p0[a.drag.axis] -= g.len * 40; p1[a.drag.axis] += g.len * 40;
            ov.line(p0, p1, IM_COL32(255, 255, 80, 120), 1.0f);
        }
    }
}

// ---- sun marker (environment): a handle on a sphere around the level, dragged to set azimuth/elevation ----

bool sunWorld(const App &a, Vec3 &pos, Vec3 &center, float &R)
{
    if (!a.doc.hasMission || !a.doc.mission.env.has_sun) return false;
    Vec3 mn = { -500, -500, -500 }, mx = { 500, 500, 500 };
    if (a.doc.hasLevel) { mn = a.doc.level.mn; mx = a.doc.level.mx; }
    center = { (mn[0] + mx[0]) / 2, (mn[1] + mx[1]) / 2, (mn[2] + mx[2]) / 2 };
    R = std::max({ mx[0] - mn[0], mx[1] - mn[1], mx[2] - mn[2], 300.0f }) * 0.6f;
    const float az = a.doc.mission.env.sun_azimuth * 0.017453292f, el = a.doc.mission.env.sun_elevation * 0.017453292f;
    pos = { center[0] + sinf(az) * cosf(el) * R, center[1] + sinf(el) * R, center[2] + cosf(az) * cosf(el) * R };
    return true;
}

bool sunHit(const App &a, const Overlay &ov, ImVec2 m)
{
    Vec3 p, c; float R; ImVec2 s;
    if (!sunWorld(a, p, c, R) || !ov.pt(p, s)) return false;
    return sqrtf((s.x - m.x) * (s.x - m.x) + (s.y - m.y) * (s.y - m.y)) < 14;
}

void sunUpdate(App &a, const Vec3 &org, const Vec3 &dir)
{
    Vec3 p, c; float R;
    if (!sunWorld(a, p, c, R)) return;
    const float t = (c[0] - org[0]) * dir[0] + (c[1] - org[1]) * dir[1] + (c[2] - org[2]) * dir[2];
    Vec3 v = { org[0] + dir[0] * t - c[0], org[1] + dir[1] * t - c[1], org[2] + dir[2] * t - c[2] };
    const float l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (l < 1e-3f) return;
    MissionEnv e = a.doc.mission.env;
    float az = atan2f(v[0], v[2]) * 57.29578f;
    if (az < 0) az += 360;
    e.sun_azimuth = az;
    e.sun_elevation = std::max(-10.0f, std::min(90.0f, asinf(v[1] / l) * 57.29578f));
    a.doc.setEnv(e, false);
}

void drawSun(const App &a, const Overlay &ov)
{
    Vec3 p, c; float R; ImVec2 s;
    if (!sunWorld(a, p, c, R)) return;
    const ImU32 col = IM_COL32(255, 210, 60, 255);
    ov.line(c, p, IM_COL32(255, 210, 60, 90), 1.0f);
    if (!ov.pt(p, s)) return;
    ov.dl->AddCircleFilled(s, 8, col);
    for (int i = 0; i < 8; i++) {
        float an = i * 0.7853982f;
        ov.dl->AddLine(ImVec2(s.x + cosf(an) * 12, s.y + sinf(an) * 12), ImVec2(s.x + cosf(an) * 17, s.y + sinf(an) * 17), col, 2.0f);
    }
    char t[64];
    snprintf(t, sizeof t, "sun az %.0f el %.0f%s", a.doc.mission.env.sun_azimuth, a.doc.mission.env.sun_elevation, a.sunDrag ? " (dragging)" : "");
    ov.dl->AddText(ImVec2(s.x + 20, s.y - 8), col, t);
}

} // namespace ed
