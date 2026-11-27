// modelview: the mesh data model, close to what a GE model provides.
// A vertex is joint-local; its world position is pos + the summed rest offsets up the joint chain.
// Triangles are vertex indices (3 per triangle). Vertices are NOT welded: a UV seam or a hard edge
// is two vertices at one position (the tools weld by position when they need topology).
#pragma once
#include <stdint.h>
#include <string>
#include <vector>

struct MvJoint {
    int parent;        // -1 = root
    float offset[3];   // rest offset from the parent joint
};

struct MvVertex {
    float pos[3];      // joint-local
    float uv[2];       // 0..1
    float col[4];      // RGBA 0..1
    int joint;         // index into MvMesh::joints
    int layer = 0;     // vertices weld only within one layer (PN: the converter's LOD layer)
    bool head = false; // in the head region (the converter: CvSdVert.head); only head triangles are subdivided by GAME
};

// A texture + wrap modes (0 wrap, 1 clamp, 2 mirror): its texture key and number, and the glTF's own PNG (image).
struct MvMaterial { int texnum = -1; int wrapS = 0, wrapT = 0; std::string image; std::string key; };   // key: the texture key (extras.texture), the picture is resolved through the mod and the chosen pack; image: the glTF's own URI, the fallback

struct MvMesh {
    std::string name;
    std::vector<MvJoint> joints;
    std::vector<MvVertex> verts;
    std::vector<uint32_t> idx;   // 3 per triangle, counter-clockwise seen from outside
    float scale = 1.0f;          // model units -> viewer units (the subdivision works in model units)
    bool headOnly = false;       // GAME: only triangles whose three corners are head are split (a loaded game model)
    std::vector<MvMaterial> materials;   // triangles pick one by tmat (empty = no textures)
    std::vector<int> tmat;               // per triangle: index into materials, -1 none
    // Optional pose: per joint a 3x4 (row vectors: p' = p.x*r0 + p.y*r1 + p.z*r2 + t), 12 floats. Empty = rest (offsets only).
    // The subdivision always works in the rest pose; the pose only moves the vertices when drawing (skinning per joint).
    std::vector<float> pose;
    size_t triCount() const { return idx.size() / 3; }
};

struct MvVec3 { float x, y, z; };

// World position of every joint (rest pose, translation only).
static inline std::vector<MvVec3> mv_joint_worlds(const MvMesh& m) {
    std::vector<MvVec3> w(m.joints.size());
    for (size_t i = 0; i < m.joints.size(); i++) {   // parents come before children
        const MvJoint& j = m.joints[i];
        MvVec3 p = j.parent >= 0 ? w[(size_t)j.parent] : MvVec3{ 0, 0, 0 };
        w[i] = { p.x + j.offset[0], p.y + j.offset[1], p.z + j.offset[2] };
    }
    return w;
}

static inline MvVec3 mv_vertex_world(const MvMesh& m, const std::vector<MvVec3>& jw, size_t vi) {
    const MvVertex& v = m.verts[vi];
    const MvVec3& o = jw[(size_t)v.joint];
    return { v.pos[0] + o.x, v.pos[1] + o.y, v.pos[2] + o.z };
}

// Where a vertex is drawn: skinned by its joint's pose matrix when there is one, else the rest position.
static inline MvVec3 mv_vertex_posed(const MvMesh& m, const std::vector<MvVec3>& jw, size_t vi) {
    if (m.pose.empty()) return mv_vertex_world(m, jw, vi);
    const MvVertex& v = m.verts[vi]; const float* p = &m.pose[12 * (size_t)v.joint];
    return { v.pos[0] * p[0] + v.pos[1] * p[3] + v.pos[2] * p[6] + p[9], v.pos[0] * p[1] + v.pos[1] * p[4] + v.pos[2] * p[7] + p[10],
             v.pos[0] * p[2] + v.pos[1] * p[5] + v.pos[2] * p[8] + p[11] };
}

// A head triangle: all three corners in the head region (only meaningful when the mesh is headOnly).
static inline bool mv_tri_is_head(const MvMesh& m, size_t t) {
    return m.verts[m.idx[3 * t]].head && m.verts[m.idx[3 * t + 1]].head && m.verts[m.idx[3 * t + 2]].head;
}

MvMesh mv_make_mannequin();   // mv_mannequin.cpp: the procedural test model
