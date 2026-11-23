// Camera, projection and CPU picking. No GL: kept apart from scene.cpp (which talks to fast3d)
// so it stays plain C++.
#pragma once

#include <cmath>
#include <set>
#include <string>

#include "doc.h"

struct Mat4 { float m[16]; };   // column-major, column vectors: the same memory as fast3d's row-vector matrices

Mat4 mat4Identity();
Mat4 mat4Mul(const Mat4 &a, const Mat4 &b);          // a * b
Mat4 mat4Translate(float x, float y, float z);
Mat4 mat4RotY(float yawDeg);                         // turns +z toward (sin yaw, 0, cos yaw): a pad's look
Mat4 mat4Perspective(float fovyRad, float aspect, float zn, float zf);
Mat4 mat4LookAt(const Vec3 &eye, const Vec3 &fwd);   // y up
// World point to window pixels inside the viewport rect (y down); false when behind the camera.
bool projectPoint(const Mat4 &vp, const Vec3 &p, float vx, float vy, float vw, float vh, float out[2]);

struct Camera {
    Vec3 pos{ 0, 500, 0 };
    float yaw = 0, pitch = -0.4f;   // yaw 0 looks along +x
    float fov = 1.0f;               // vertical, radians
    float speed = 600;              // world units per second (cm)
    float znear = 5, zfar = 200000;

    Vec3 forward() const;
    Vec3 right() const;
    Mat4 view() const;
    Mat4 proj(float aspect) const;
    void frame(const Vec3 &mn, const Vec3 &mx);   // look at a box from outside
    void lookAtPoint(const Vec3 &p, float dist);
    // Ray through a pixel of the viewport (px, py relative to its top left).
    void ray(float px, float py, float vw, float vh, Vec3 &org, Vec3 &dir) const;
};

struct Hit {
    enum Kind { NONE, ITEM, PAD, MPAD, SURFACE } kind = NONE;
    ItemRef item;
    int num = -1;           // PAD
    std::string id;         // MPAD
    float t = 0;
    Vec3 point{};           // the picked point (on the surface for SURFACE, the object's pad otherwise)
    Vec3 surfacePoint{};    // where the ray meets room geometry (valid when hasSurface)
    bool hasSurface = false;
    int room = -1;
};

// The item's box in its own frame: relative to its pad position, before the pad's rotation (owner model bounds, or a
// default box). Drawn and picked rotated by Item::yawDeg, like the model.
bool itemLocalBox(const Doc &d, const Item &it, Vec3 &mn, Vec3 &mx);
Vec3 rotYaw(const Vec3 &v, float yawDeg);             // the same rotation applied to a vector
// World AABB of that box once rotated and placed at the pad.
bool itemBounds(const Doc &d, const Item &it, Vec3 &mn, Vec3 &mx);
// Puts the owner's recorded model (facing +z, anchored at its bottom centre) on the pad, turned to the pad's look.
Mat4 itemPlacement(const Doc &d, const Item &it);
Hit pickScene(const Doc &d, const Vec3 &org, const Vec3 &dir);

// The pads a guard or object stands on (not removed ones): drawn and picked as the item, never on their own.
struct OccupiedPads {
    std::set<int> level;            // original pad numbers
    std::set<std::string> mission;  // mission pad ids
    bool has(const MissionPadRef &r) const { return r.is_new ? mission.count(r.id) != 0 : level.count(r.num) != 0; }
};
OccupiedPads occupiedPads(const std::vector<Item> &items);
