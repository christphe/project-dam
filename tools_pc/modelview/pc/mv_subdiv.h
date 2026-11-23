// modelview: the subdivision interface.
//   FLAT      midpoints, linear UV and colour (viewer).
//   PN        the converter's own pass (port/convert/conv_subdiv.c, cv_sd_run): what the game gets.
//   PHONG, BUTTERFLY, LOOP   viewer-only experiments on MvMesh.
#pragma once
#include "mv_mesh.h"

enum MvMethod { MV_FLAT = 0, MV_PN, MV_PHONG, MV_BUTTERFLY, MV_LOOP, MV_HEAD, MV_GAME, MV_METHOD_COUNT };
extern const char* const MV_METHOD_NAMES[MV_METHOD_COUNT];
bool mv_method_wired(MvMethod m);   // false = mv_subdivide falls back to FLAT for it (all are wired now)
const char* mv_method_note(MvMethod m);   // "" or a short label for the UI

struct MvParams {
    float crease_deg = 60.0f;        // an edge whose two faces differ by more than this stays straight
    bool split_joint_edges = false;   // false: an edge across joints is not split (0..3 split edges per triangle)
    int passes = 1;                  // 1..2
    float phong_alpha = 0.5f;        // PHONG blend, 0..1
    bool head_tex_features = false;  // HEAD: UV-island borders and texture changes are creases too
    bool neck_pin = true;            // HEAD: the open boundary keeps its exact positions
    int taubin_iters = 0;            // HEAD: Taubin smoothing after the subdivision (0 = off)
    float pn_clamp = 0.15f;          // PN: a bulge above this fraction of the edge length falls back to flat
};

// Why an edge stayed straight (a bit set). Edge e of triangle t runs idx[3t+e] -> idx[3t+(e+1)%3].
enum {
    MV_EDGE_CREASE = 1,     // the two faces differ by more than crease_deg
    MV_EDGE_BOUNDARY = 2,   // one face only (or more than two: non-manifold)
    MV_EDGE_JOINT = 4,      // the edge touches vertices of different joints
    MV_EDGE_UVSEAM = 8,     // same position, different UV on the two sides (informational)
    MV_EDGE_NECK = 32,      // GAME: a head triangle meets a body triangle (the edge stays whole)
    MV_EDGE_TEXFEAT = 16,   // HEAD: a crease because of the texture (UV-island border, or two textures meet)
};

struct MvEdgeStats { int edges, crease, boundary, joint, seam; };   // unique edges, welded by position

// Classify the edges of a mesh: flags_out has 3 entries per triangle.
MvEdgeStats mv_classify_edges(const MvMesh& in, const MvParams& p, std::vector<uint8_t>& flags_out);

// Subdivide. edge_flags_out has 3 entries per OUTPUT triangle: the reason of the original edge a
// triangle edge came from (halves of a split edge inherit it; edges inside a split triangle are 0).
// Returns true if the requested method ran, false if it fell back to FLAT.
bool mv_subdivide(MvMethod method, const MvParams& p, const MvMesh& in, MvMesh& out, std::vector<uint8_t>& edge_flags_out);

void mv_mark_neck(const MvMesh& m, std::vector<uint8_t>& flags);   // sets MV_EDGE_NECK where a head triangle meets a body one

// HEAD (mv_head.cpp): Loop subdivision with piecewise-smooth (Hoppe) rules, passes 1..3.
struct MvHeadStats {
    int creaseEdges = 0, texEdges = 0, boundaryEdges = 0, jointEdges = 0;   // edges of the original mesh by class
    float maxDisp = 0;       // largest move of an original vertex, model units
    float neckGap = 0;       // largest move of a boundary (neck) vertex: 0 when the neck is pinned
    bool multiMatrix = false, valid = false;
};
const MvHeadStats& mv_head_stats();
bool mv_head_subdivide(const MvParams& p, const MvMesh& in, MvMesh& out, std::vector<uint8_t>& edge_flags_out);
void mv_head_classify(const MvMesh& in, const MvParams& p, std::vector<uint8_t>& flags_out);   // the original mesh's edge classes as HEAD sees them
int mv_head_selftest();
