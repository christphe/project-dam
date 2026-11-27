#include "view.h"

#include <algorithm>
#include <cstring>

Mat4 mat4Identity()
{
    Mat4 r; memset(&r, 0, sizeof r);
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1;
    return r;
}

Mat4 mat4Mul(const Mat4 &a, const Mat4 &b)
{
    Mat4 r;
    for (int c = 0; c < 4; c++)
        for (int row = 0; row < 4; row++) {
            float s = 0;
            for (int k = 0; k < 4; k++) s += a.m[k * 4 + row] * b.m[c * 4 + k];
            r.m[c * 4 + row] = s;
        }
    return r;
}

Mat4 mat4Translate(float x, float y, float z)
{
    Mat4 r = mat4Identity();
    r.m[12] = x; r.m[13] = y; r.m[14] = z;
    return r;
}

Mat4 mat4RotY(float yawDeg)
{
    const float r = yawDeg * 0.017453292f, c = cosf(r), s = sinf(r);
    Mat4 m = mat4Identity();
    m.m[0] = c; m.m[2] = -s;     // column 0 (the image of +x)
    m.m[8] = s; m.m[10] = c;     // column 2 (the image of +z)
    return m;
}

Mat4 mat4Perspective(float fovy, float aspect, float zn, float zf)
{
    Mat4 r; memset(&r, 0, sizeof r);
    float f = 1.0f / tanf(fovy * 0.5f);
    r.m[0] = f / aspect;
    r.m[5] = f;
    r.m[10] = (zf + zn) / (zn - zf);
    r.m[11] = -1;
    r.m[14] = 2 * zf * zn / (zn - zf);
    return r;
}

static Vec3 cross(const Vec3 &a, const Vec3 &b)
{
    return { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
}
static Vec3 norm(Vec3 v)
{
    float l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (l > 1e-9f) for (auto &c : v) c /= l;
    return v;
}
static float dot(const Vec3 &a, const Vec3 &b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
static Vec3 sub(const Vec3 &a, const Vec3 &b) { return { a[0] - b[0], a[1] - b[1], a[2] - b[2] }; }
static Vec3 add(const Vec3 &a, const Vec3 &b) { return { a[0] + b[0], a[1] + b[1], a[2] + b[2] }; }
static Vec3 mulf(const Vec3 &a, float s) { return { a[0] * s, a[1] * s, a[2] * s }; }

Mat4 mat4LookAt(const Vec3 &eye, const Vec3 &fwd)
{
    Vec3 f = norm(fwd);
    Vec3 s = norm(cross(f, { 0, 1, 0 }));
    Vec3 u = cross(s, f);
    Mat4 r = mat4Identity();
    r.m[0] = s[0]; r.m[4] = s[1]; r.m[8] = s[2];
    r.m[1] = u[0]; r.m[5] = u[1]; r.m[9] = u[2];
    r.m[2] = -f[0]; r.m[6] = -f[1]; r.m[10] = -f[2];
    r.m[12] = -dot(s, eye); r.m[13] = -dot(u, eye); r.m[14] = dot(f, eye);
    return r;
}

bool projectPoint(const Mat4 &vp, const Vec3 &p, float vx, float vy, float vw, float vh, float out[2])
{
    const float *m = vp.m;
    float x = m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12];
    float y = m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13];
    float w = m[3] * p[0] + m[7] * p[1] + m[11] * p[2] + m[15];
    if (w <= 1e-3f) return false;
    out[0] = vx + (x / w * 0.5f + 0.5f) * vw;
    out[1] = vy + (1.0f - (y / w * 0.5f + 0.5f)) * vh;
    return true;
}

Vec3 Camera::forward() const { return { cosf(pitch) * cosf(yaw), sinf(pitch), cosf(pitch) * sinf(yaw) }; }
Vec3 Camera::right() const { return norm(cross(forward(), { 0, 1, 0 })); }
Mat4 Camera::view() const { return mat4LookAt(pos, forward()); }
Mat4 Camera::proj(float aspect) const { return mat4Perspective(fov, aspect, znear, zfar); }

void Camera::lookAtPoint(const Vec3 &p, float dist)
{
    Vec3 f = forward();
    pos = sub(p, mulf(f, dist));
}

void Camera::frame(const Vec3 &mn, const Vec3 &mx)
{
    Vec3 c = { (mn[0] + mx[0]) * 0.5f, (mn[1] + mx[1]) * 0.5f, (mn[2] + mx[2]) * 0.5f };
    float s = std::max({ mx[0] - mn[0], mx[1] - mn[1], mx[2] - mn[2], 100.0f });
    yaw = 0.8f; pitch = -0.6f;
    lookAtPoint(c, s * 0.9f);
    speed = s * 0.4f;
}

void Camera::ray(float px, float py, float vw, float vh, Vec3 &org, Vec3 &dir) const
{
    float t = tanf(fov * 0.5f), asp = vw / std::max(vh, 1.0f);
    float nx = (px / vw) * 2 - 1, ny = 1 - (py / vh) * 2;
    Vec3 f = forward(), r = right(), u = cross(r, f);
    dir = norm(add(add(f, mulf(r, nx * t * asp)), mulf(u, ny * t)));
    org = pos;
}

// ---- picking ---------------------------------------------------------------------------------

static bool rayAabb(const Vec3 &o, const Vec3 &d, const Vec3 &mn, const Vec3 &mx, float &tOut)
{
    float t0 = 0, t1 = 1e30f;
    for (int i = 0; i < 3; i++) {
        if (fabsf(d[i]) < 1e-9f) { if (o[i] < mn[i] || o[i] > mx[i]) return false; continue; }
        float a = (mn[i] - o[i]) / d[i], b = (mx[i] - o[i]) / d[i];
        if (a > b) std::swap(a, b);
        t0 = std::max(t0, a); t1 = std::min(t1, b);
        if (t0 > t1) return false;
    }
    tOut = t0;
    return true;
}

static bool raySphere(const Vec3 &o, const Vec3 &d, const Vec3 &c, float r, float &tOut)
{
    Vec3 oc = sub(o, c);
    float b = dot(oc, d), cc = dot(oc, oc) - r * r;
    float disc = b * b - cc;
    if (disc < 0) return false;
    float t = -b - sqrtf(disc);
    if (t < 0) t = -b + sqrtf(disc);
    if (t < 0) return false;
    tOut = t;
    return true;
}

// cull: as the game draws it (1 back faces culled, 2 front faces culled, 0 two sided), so a click
// goes through a ceiling that is not drawn from this side.
static bool rayTri(const Vec3 &o, const Vec3 &d, const Vec3 &a, const Vec3 &b, const Vec3 &c, int cull, float &tOut)
{
    Vec3 e1 = sub(b, a), e2 = sub(c, a);
    Vec3 p = cross(d, e2);
    float det = dot(e1, p);
    if (fabsf(det) < 1e-9f) return false;
    // det = -dot(normal, d): > 0 when the face looks at the ray origin (counter-clockwise = front)
    if (cull == 1 && det < 0) return false;
    if (cull == 2 && det > 0) return false;
    float inv = 1.0f / det;
    Vec3 tv = sub(o, a);
    float u = dot(tv, p) * inv;
    if (u < 0 || u > 1) return false;
    Vec3 q = cross(tv, e1);
    float v = dot(d, q) * inv;
    if (v < 0 || u + v > 1) return false;
    float t = dot(e2, q) * inv;
    if (t < 1e-3f) return false;
    tOut = t;
    return true;
}

Vec3 rotYaw(const Vec3 &v, float yawDeg)
{
    const float r = yawDeg * 0.017453292f, c = cosf(r), s = sinf(r);
    return { v[0] * c + v[2] * s, v[1], -v[0] * s + v[2] * c };
}

static const OwnerInfo *ownerOf(const Doc &d, const Item &it)
{
    if (d.hasLevel && it.owner >= 0 && (size_t)it.owner < d.level.owners.size() && d.level.owners[(size_t)it.owner].valid)
        return &d.level.owners[(size_t)it.owner];
    return nullptr;
}

// A door of the level: its model is recorded as the game placed it, scaled and turned to its bound pad's box, so it is drawn
// and picked as recorded; a pad-anchored placement would only lose that. Doors are not movable.
static bool isDoor(const Item &it) { return it.type == 1; }

// A door as the game places it (src/game/prop.c setupDoor): the volume of its BOUND pad, a box in the pad's frame (x along up x look, y up,
// z along look; Item::bas) around the pad position, and the door model scaled per axis to that box: model x to the pad's y extent, model y
// to z, model z to x (the xscale / yscale / zscale of setupDoor). (g_DoorScale is 1 in the levels the editor opens.) A door record's pad is
// a bound pad; see doc.cpp, LProp::pad.
static Vec3 dot3axes(const Item &it, const Vec3 &l)   // pad frame -> world offset
{
    return { it.bas[0][0] * l[0] + it.bas[1][0] * l[1] + it.bas[2][0] * l[2], it.bas[0][1] * l[0] + it.bas[1][1] * l[1] + it.bas[2][1] * l[2],
             it.bas[0][2] * l[0] + it.bas[1][2] * l[1] + it.bas[2][2] * l[2] };
}
static Vec3 toPadFrame(const Item &it, const Vec3 &w)   // world offset -> pad frame
{
    return { w[0] * it.bas[0][0] + w[1] * it.bas[0][1] + w[2] * it.bas[0][2], w[0] * it.bas[1][0] + w[1] * it.bas[1][1] + w[2] * it.bas[1][2],
             w[0] * it.bas[2][0] + w[1] * it.bas[2][1] + w[2] * it.bas[2][2] };
}
static bool padBasis(const LPad &p, Vec3 *b)   // the same frame as Item::bas (doc.cpp setDoorBasis)
{
    Vec3 n = { p.up[1] * p.look[2] - p.up[2] * p.look[1], p.up[2] * p.look[0] - p.up[0] * p.look[2], p.up[0] * p.look[1] - p.up[1] * p.look[0] };
    const float nl = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    if (nl < 1e-6f) return false;
    for (int i = 0; i < 3; i++) n[i] /= nl;
    b[0] = n; b[1] = p.up; b[2] = p.look;
    return true;
}
static bool doorBox(const Doc &d, const Item &it, Vec3 &mn, Vec3 &mx)
{
    if (!it.hasBasis) return false;
    const LPad *p = d.level.pad(it.pad.num);
    if (!p || !p->bound) return false;
    mn = p->bmn; mx = p->bmx;
    return true;
}
// The recorded model of a door is either as the game placed one of its doors (world space) or in model space: told apart by whether its
// bounds sit around some door's box.
static bool doorSourceBox(const Doc &d, const Item &it, Vec3 &c, Vec3 &size)
{
    const OwnerInfo *o = ownerOf(d, it);
    if (!o) return false;
    c = { (o->mn[0] + o->mx[0]) * 0.5f, (o->mn[1] + o->mx[1]) * 0.5f, (o->mn[2] + o->mx[2]) * 0.5f };
    size = { o->mx[0] - o->mn[0], o->mx[1] - o->mn[1], o->mx[2] - o->mn[2] };
    return true;
}

Mat4 itemPlacement(const Doc &d, const Item &it)
{
    const OwnerInfo *o = ownerOf(d, it);
    if (!it.hasPos || !o) return mat4Identity();
    if (isDoor(it)) {
        Vec3 mn, mx, c, sz;
        if (!doorBox(d, it, mn, mx) || !doorSourceBox(d, it, c, sz)) return mat4Identity();
        const Vec3 half = { (mn[0] + mx[0]) * 0.5f, (mn[1] + mx[1]) * 0.5f, (mn[2] + mx[2]) * 0.5f };
        const Vec3 off = dot3axes(it, half);
        const Vec3 c1 = { it.pos[0] + off[0], it.pos[1] + off[1], it.pos[2] + off[2] };   // the box centre in world space
        const Vec3 bs = { mx[0] - mn[0], mx[1] - mn[1], mx[2] - mn[2] };                  // box size in the pad frame
        auto basisMat = [](const Vec3 *b) { Mat4 m = mat4Identity(); for (int k = 0; k < 3; k++) for (int r = 0; r < 3; r++) m.m[k * 4 + r] = b[k][r]; return m; };
        auto transposed = [](Mat4 m) { Mat4 t = m; for (int r = 0; r < 3; r++) for (int k = 0; k < 3; k++) t.m[k * 4 + r] = m.m[r * 4 + k]; return t; };
        // 1. the recorded model is how the game placed ONE of the level's doors of this model (a source pad whose box it sits in): move it
        //    from that box to this one (centre, frame, per-axis size)
        {
            const LPad *best = nullptr; float bestD = 1e30f;
            for (const LProp &p : d.level.props) {
                if (p.type != 1 || p.owner != it.owner) continue;
                const LPad *sp = d.level.pad(p.pad);
                if (!sp || !sp->bound) continue;
                Vec3 sb[3];
                if (!padBasis(*sp, sb)) continue;
                const Vec3 sh = { (sp->bmn[0] + sp->bmx[0]) * 0.5f, (sp->bmn[1] + sp->bmx[1]) * 0.5f, (sp->bmn[2] + sp->bmx[2]) * 0.5f };
                const Vec3 sc = { sp->pos[0] + sb[0][0] * sh[0] + sb[1][0] * sh[1] + sb[2][0] * sh[2], sp->pos[1] + sb[0][1] * sh[0] + sb[1][1] * sh[1] + sb[2][1] * sh[2],
                                  sp->pos[2] + sb[0][2] * sh[0] + sb[1][2] * sh[1] + sb[2][2] * sh[2] };
                const float dd = std::sqrt((c[0] - sc[0]) * (c[0] - sc[0]) + (c[1] - sc[1]) * (c[1] - sc[1]) + (c[2] - sc[2]) * (c[2] - sc[2]));
                if (dd < bestD) { bestD = dd; best = sp; }
            }
            Vec3 sb[3];
            if (best && bestD < 0.6f * std::max({ sz[0], sz[1], sz[2] }) + 10.0f && padBasis(*best, sb)) {
                const Vec3 sh = { (best->bmn[0] + best->bmx[0]) * 0.5f, (best->bmn[1] + best->bmx[1]) * 0.5f, (best->bmn[2] + best->bmx[2]) * 0.5f };
                const Vec3 sc = { best->pos[0] + sb[0][0] * sh[0] + sb[1][0] * sh[1] + sb[2][0] * sh[2], best->pos[1] + sb[0][1] * sh[0] + sb[1][1] * sh[1] + sb[2][1] * sh[2],
                                  best->pos[2] + sb[0][2] * sh[0] + sb[1][2] * sh[1] + sb[2][2] * sh[2] };
                if (it.pad.num == best->num) return mat4Identity();
                const Vec3 ss = { best->bmx[0] - best->bmn[0], best->bmx[1] - best->bmn[1], best->bmx[2] - best->bmn[2] };
                Mat4 S = mat4Identity();
                S.m[0] = ss[0] > 1e-3f ? bs[0] / ss[0] : 1.0f; S.m[5] = ss[1] > 1e-3f ? bs[1] / ss[1] : 1.0f; S.m[10] = ss[2] > 1e-3f ? bs[2] / ss[2] : 1.0f;
                return mat4Mul(mat4Mul(mat4Mul(mat4Mul(mat4Translate(c1[0], c1[1], c1[2]), basisMat(it.bas)), S), transposed(basisMat(sb))),
                               mat4Translate(-sc[0], -sc[1], -sc[2]));
            }
        }
        // 2. the recorded bounds are in model space: model (x, y, z) -> pad (z, x, y), each axis scaled to the box
        const float sx = sz[0] > 1e-3f ? bs[1] / sz[0] : 1.0f, sy = sz[1] > 1e-3f ? bs[2] / sz[1] : 1.0f, szz = sz[2] > 1e-3f ? bs[0] / sz[2] : 1.0f;
        Mat4 M = mat4Identity();
        for (int r = 0; r < 3; r++) {
            M.m[0 * 4 + r] = it.bas[1][r] * sx;      // model x -> pad y
            M.m[1 * 4 + r] = it.bas[2][r] * sy;      // model y -> pad z
            M.m[2 * 4 + r] = it.bas[0][r] * szz;     // model z -> pad x
        }
        return mat4Mul(mat4Mul(mat4Translate(c1[0], c1[1], c1[2]), M), mat4Translate(-c[0], -c[1], -c[2]));
    }
    return mat4Mul(mat4Mul(mat4Translate(it.pos[0], it.pos[1], it.pos[2]), mat4RotY(it.yawDeg)),
                   mat4Translate(-o->anchor[0], -o->anchor[1], -o->anchor[2]));
}

bool itemLocalBox(const Doc &d, const Item &it, Vec3 &mn, Vec3 &mx)
{
    if (!it.hasPos) return false;
    if (isDoor(it)) { Vec3 m0, m1; if (doorBox(d, it, m0, m1)) { mn = m0; mx = m1; return true; } }   // the bound pad's own box, in the pad frame
    if (const OwnerInfo *o = ownerOf(d, it)) {
        for (int i = 0; i < 3; i++) { mn[i] = o->mn[i] - o->anchor[i]; mx[i] = o->mx[i] - o->anchor[i]; }
        return true;
    }
    float r = it.guard ? 25.0f : 20.0f, h = it.guard ? 160.0f : 60.0f;
    mn = { -r, 0, -r };
    mx = { r, h, r };
    return true;
}

bool itemBounds(const Doc &d, const Item &it, Vec3 &mn, Vec3 &mx)
{
    Vec3 lmn, lmx;
    if (!itemLocalBox(d, it, lmn, lmx)) return false;
    mn = { 1e30f, 1e30f, 1e30f }; mx = { -1e30f, -1e30f, -1e30f };
    for (int i = 0; i < 8; i++) {
        const Vec3 c = { (i & 1) ? lmx[0] : lmn[0], (i & 2) ? lmx[1] : lmn[1], (i & 4) ? lmx[2] : lmn[2] };
        const Vec3 w = add(it.hasBasis ? dot3axes(it, c) : rotYaw(c, it.yawDeg), it.pos);
        for (int k = 0; k < 3; k++) { mn[k] = std::min(mn[k], w[k]); mx[k] = std::max(mx[k], w[k]); }
    }
    return true;
}

OccupiedPads occupiedPads(const std::vector<Item> &items)
{
    OccupiedPads o;
    for (const Item &it : items) {
        if (it.removed || it.assigned) continue;     // a carried weapon is not on a pad
        if (it.pad.is_new) o.mission.insert(it.pad.id); else o.level.insert(it.pad.num);
    }
    return o;
}

Hit pickScene(const Doc &d, const Vec3 &org, const Vec3 &dir)
{
    Hit best;
    best.t = 1e30f;
    // room geometry first: it hides what is behind it
    float surf = 1e30f; int surfRoom = -1, surfTex = -1;
    if (d.hasLevel) {
        for (const LTri &t : d.level.roomTris) {
            float tt;
            if (rayTri(org, dir, t.a, t.b, t.c, t.cull, tt) && tt < surf) { surf = tt; surfRoom = t.room; surfTex = t.texnum; }
        }
    }
    if (surf < 1e29f) {
        best.hasSurface = true;
        best.surfacePoint = add(org, mulf(dir, surf));
        best.room = surfRoom;
        best.texnum = surfTex;
    }
    const float limit = surf + 5.0f;

    Hit cand; cand.t = 1e30f;
    auto tryPad = [&](const Vec3 &pos, Hit::Kind k, int num, const std::string &id) {
        float t;
        Vec3 c = { pos[0], pos[1] + 12, pos[2] };
        if (raySphere(org, dir, c, 22.0f, t) && t <= limit && t < cand.t) {
            cand = Hit(); cand.kind = k; cand.num = num; cand.id = id; cand.t = t; cand.point = pos;
        }
    };
    // a pad under a guard or object is the item's, not something to select on its own
    const std::vector<Item> all = d.items();
    const OccupiedPads occ = occupiedPads(all);
    if (d.hasLevel)
        for (auto &p : d.level.pads)
            if (!occ.level.count(p.num)) tryPad(p.pos, Hit::PAD, p.num, "");
    if (d.hasMission)
        for (int i = 0; i < d.mission.npads; i++) {
            const MissionPad &p = d.mission.pads[i];
            if (!occ.mission.count(p.id)) tryPad({ p.pos[0], p.pos[1], p.pos[2] }, Hit::MPAD, -1, p.id);
        }
    // a zone's centre handle (drawn as a ring on the floor with a marker at its centre)
    if (d.hasMission)
        for (int i = 0; i < d.mission.nzones; i++) {
            const MissionZone &z = d.mission.zones[i];
            float t;
            if (raySphere(org, dir, { z.pos[0], z.pos[1], z.pos[2] }, 28.0f, t) && t <= limit && t < cand.t) {
                cand = Hit(); cand.kind = Hit::ZONE; cand.id = z.id; cand.t = t; cand.point = { z.pos[0], z.pos[1], z.pos[2] };
            }
        }
    // items win over a pad at the same spot (they are drawn on it)
    for (const Item &it : d.items()) {
        Vec3 mn, mx; float t;
        if (it.removed || it.assigned || !itemLocalBox(d, it, mn, mx)) continue;
        // the box is turned with the model: test the ray in the item's own frame (a rotation keeps t)
        const Vec3 lo = it.hasBasis ? toPadFrame(it, sub(org, it.pos)) : rotYaw(sub(org, it.pos), -it.yawDeg), ld = it.hasBasis ? toPadFrame(it, dir) : rotYaw(dir, -it.yawDeg);
        if (rayAabb(lo, ld, mn, mx, t) && t <= limit && (cand.kind == Hit::NONE || t <= cand.t + 30.0f)) {
            Hit h; h.kind = Hit::ITEM; h.item.valid = true; h.item.added = it.added; h.item.guard = it.guard; h.item.idx = it.idx;
            h.t = t; h.point = it.pos;
            cand = h;
        }
    }
    if (cand.kind != Hit::NONE) {
        bool hs = best.hasSurface; Vec3 sp = best.surfacePoint; int room = best.room;
        best = cand;
        best.hasSurface = hs; best.surfacePoint = sp; best.room = room;
        return best;
    }
    if (best.hasSurface) { best.kind = Hit::SURFACE; best.t = surf; best.point = best.surfacePoint; }
    return best;
}
