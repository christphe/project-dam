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

Vec3 itemOffset(const Doc &d, const Item &it)
{
    if (!it.hasPos) return { 0, 0, 0 };
    if (d.hasLevel && it.owner >= 0 && (size_t)it.owner < d.level.owners.size() && d.level.owners[(size_t)it.owner].valid) {
        const Vec3 &a = d.level.owners[(size_t)it.owner].anchor;
        return { it.pos[0] - a[0], it.pos[1] - a[1], it.pos[2] - a[2] };
    }
    return { 0, 0, 0 };
}

bool itemBounds(const Doc &d, const Item &it, Vec3 &mn, Vec3 &mx)
{
    if (!it.hasPos) return false;
    if (d.hasLevel && it.owner >= 0 && (size_t)it.owner < d.level.owners.size() && d.level.owners[(size_t)it.owner].valid) {
        const OwnerInfo &o = d.level.owners[(size_t)it.owner];
        Vec3 off = itemOffset(d, it);
        for (int i = 0; i < 3; i++) { mn[i] = o.mn[i] + off[i]; mx[i] = o.mx[i] + off[i]; }
        return true;
    }
    float r = it.guard ? 25.0f : 20.0f, h = it.guard ? 160.0f : 60.0f;
    mn = { it.pos[0] - r, it.pos[1], it.pos[2] - r };
    mx = { it.pos[0] + r, it.pos[1] + h, it.pos[2] + r };
    return true;
}

Hit pickScene(const Doc &d, const Vec3 &org, const Vec3 &dir)
{
    Hit best;
    best.t = 1e30f;
    // room geometry first: it hides what is behind it
    float surf = 1e30f; int surfRoom = -1;
    if (d.hasLevel) {
        for (const LTri &t : d.level.roomTris) {
            float tt;
            if (rayTri(org, dir, t.a, t.b, t.c, t.cull, tt) && tt < surf) { surf = tt; surfRoom = t.room; }
        }
    }
    if (surf < 1e29f) {
        best.hasSurface = true;
        best.surfacePoint = add(org, mulf(dir, surf));
        best.room = surfRoom;
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
    if (d.hasLevel) for (auto &p : d.level.pads) tryPad(p.pos, Hit::PAD, p.num, "");
    if (d.hasMission)
        for (int i = 0; i < d.mission.npads; i++) {
            const MissionPad &p = d.mission.pads[i];
            tryPad({ p.pos[0], p.pos[1], p.pos[2] }, Hit::MPAD, -1, p.id);
        }
    // items win over a pad at the same spot (they are drawn on it)
    for (const Item &it : d.items()) {
        Vec3 mn, mx; float t;
        if (it.removed || !itemBounds(d, it, mn, mx)) continue;
        if (rayAabb(org, dir, mn, mx, t) && t <= limit && (cand.kind == Hit::NONE || t <= cand.t + 30.0f)) {
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
