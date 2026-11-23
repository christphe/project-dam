// modelview: edge classification and the subdivision methods on MvMesh.
//   FLAT: midpoints, linear UV and colour, 1 -> 4 split (0..3 split edges when joint edges stay whole).
//   PN: the converter's own pass (cv_sd_run in port/convert/conv_subdiv.c), so the viewer shows what the game gets.
//   PHONG / BUTTERFLY / LOOP: viewer-only, FLAT topology with moved vertices (LOOP also moves the originals).
#include "mv_subdiv.h"
#include <array>
#include <map>
#include <math.h>
#include <stdlib.h>
#include <string.h>

extern "C" {
#include "conv_subdiv.h"
}

const char* const MV_METHOD_NAMES[MV_METHOD_COUNT] = { "FLAT", "PN", "PHONG", "BUTTERFLY", "LOOP", "HEAD", "GAME (PN130, heads only)" };
bool mv_method_wired(MvMethod) { return true; }
const char* mv_method_note(MvMethod m) {
    switch (m) {
    case MV_PN: return "the converter's PN code on the whole model, crease angle from the slider";
    case MV_GAME: return "the converter's pass (PN, 130 deg crease, head triangles only): what the game gets";
    case MV_PHONG: case MV_BUTTERFLY: return "viewer only, experimental";
    case MV_HEAD: return "Loop with crease rules, for heads: experimental";
    case MV_LOOP: return "moves original vertices: not usable in the converter";
    default: return "";
    }
}

namespace {

typedef MvVec3 V3;
V3 operator+(V3 a, V3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
V3 operator-(V3 a, V3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
V3 operator*(V3 a, float s) { return { a.x * s, a.y * s, a.z * s }; }
float dot3(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 cross3(V3 a, V3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
V3 unit3(V3 a) { float l = sqrtf(dot3(a, a)); return l > 1e-12f ? a * (1.0f / l) : V3{ 0, 0, 0 }; }

struct EdgeInfo {
    int nf = 0;
    V3 n[2] = {};
    int opp[2] = { -1, -1 };   // canonical vertex opposite the edge in each of the first two faces
    int j0 = -1, j1 = -1;      // joints of the two endpoints (low welded id first), first face
    float uv0[2] = {}, uv1[2] = {};
    bool jmix = false, seam = false;
    uint8_t flags = 0;
};

struct Topo {
    std::vector<V3> jw, wp;               // joint worlds, vertex worlds
    std::vector<int> canon;               // vertex -> welded vertex
    std::vector<V3> cpos, cnrm;           // per welded vertex: position, smooth normal
    std::vector<uint8_t> cstraight;       // some incident edge is crease / boundary / joint
    std::vector<std::vector<int>> cnbr;   // welded neighbours
    std::map<std::pair<int, int>, EdgeInfo> em;
    std::pair<int, int> key(uint32_t a, uint32_t b) const { int x = canon[a], y = canon[b]; return x < y ? std::make_pair(x, y) : std::make_pair(y, x); }
};

void buildTopo(const MvMesh& in, const MvParams& p, Topo& t) {
    const size_t nt = in.triCount();
    t.jw = mv_joint_worlds(in);
    t.wp.resize(in.verts.size()); t.canon.resize(in.verts.size());
    std::map<std::array<int, 3>, int> weld;
    for (size_t i = 0; i < in.verts.size(); i++) {
        t.wp[i] = mv_vertex_world(in, t.jw, i);
        std::array<int, 3> k = { (int)floorf(t.wp[i].x * 1000.0f + 0.5f), (int)floorf(t.wp[i].y * 1000.0f + 0.5f), (int)floorf(t.wp[i].z * 1000.0f + 0.5f) };
        auto it = weld.find(k);
        if (it == weld.end()) { const int id = (int)weld.size(); weld[k] = id; t.canon[i] = id; t.cpos.push_back(t.wp[i]); }
        else t.canon[i] = it->second;
    }
    t.cnrm.assign(t.cpos.size(), V3{ 0, 0, 0 });
    for (size_t f = 0; f < nt; f++) {
        const uint32_t* v = &in.idx[3 * f];
        const V3 fn = cross3(t.wp[v[1]] - t.wp[v[0]], t.wp[v[2]] - t.wp[v[0]]);   // area weighted
        for (int k = 0; k < 3; k++) t.cnrm[(size_t)t.canon[v[k]]] = t.cnrm[(size_t)t.canon[v[k]]] + fn;
    }
    for (auto& n : t.cnrm) n = unit3(n);
    for (size_t f = 0; f < nt; f++) {
        const uint32_t* v = &in.idx[3 * f];
        const V3 fn = unit3(cross3(t.wp[v[1]] - t.wp[v[0]], t.wp[v[2]] - t.wp[v[0]]));
        for (int e = 0; e < 3; e++) {
            const uint32_t a = v[e], b = v[(e + 1) % 3], c = v[(e + 2) % 3];
            if (t.canon[a] == t.canon[b]) continue;
            const bool sw = t.canon[a] > t.canon[b];
            const uint32_t lo = sw ? b : a, hi = sw ? a : b;
            EdgeInfo& x = t.em[t.key(a, b)];
            const int jl = in.verts[lo].joint, jh = in.verts[hi].joint;
            if (jl != jh) x.jmix = true;
            if (x.nf == 0) { x.j0 = jl; x.j1 = jh; memcpy(x.uv0, in.verts[lo].uv, 8); memcpy(x.uv1, in.verts[hi].uv, 8); }
            else {
                if (x.j0 != jl || x.j1 != jh) x.jmix = true;
                if (fabsf(x.uv0[0] - in.verts[lo].uv[0]) > 1e-4f || fabsf(x.uv0[1] - in.verts[lo].uv[1]) > 1e-4f ||
                    fabsf(x.uv1[0] - in.verts[hi].uv[0]) > 1e-4f || fabsf(x.uv1[1] - in.verts[hi].uv[1]) > 1e-4f) x.seam = true;
            }
            if (x.nf < 2) { x.n[x.nf] = fn; x.opp[x.nf] = t.canon[c]; }
            x.nf++;
        }
    }
    t.cstraight.assign(t.cpos.size(), 0); t.cnbr.assign(t.cpos.size(), {});
    for (auto& kv : t.em) {
        EdgeInfo& x = kv.second;
        uint8_t f = 0;
        if (x.nf != 2) f |= MV_EDGE_BOUNDARY;
        else {
            const float d = fminf(fmaxf(dot3(x.n[0], x.n[1]), -1.0f), 1.0f);
            if (acosf(d) * 57.29578f > p.crease_deg + 1e-3f) f |= MV_EDGE_CREASE;
        }
        if (x.jmix) f |= MV_EDGE_JOINT;
        if (x.seam) f |= MV_EDGE_UVSEAM;
        x.flags = f;
        const int a = kv.first.first, b = kv.first.second;
        t.cnbr[(size_t)a].push_back(b); t.cnbr[(size_t)b].push_back(a);
        if (f & (MV_EDGE_CREASE | MV_EDGE_BOUNDARY | MV_EDGE_JOINT)) t.cstraight[(size_t)a] = t.cstraight[(size_t)b] = 1;
    }
}

}  // namespace

MvEdgeStats mv_classify_edges(const MvMesh& in, const MvParams& p, std::vector<uint8_t>& flags_out) {
    Topo t; buildTopo(in, p, t);
    MvEdgeStats st = { 0, 0, 0, 0, 0 };
    for (const auto& kv : t.em) {
        const uint8_t f = kv.second.flags;
        st.edges++;
        if (f & MV_EDGE_CREASE) st.crease++;
        if (f & MV_EDGE_BOUNDARY) st.boundary++;
        if (f & MV_EDGE_JOINT) st.joint++;
        if (f & MV_EDGE_UVSEAM) st.seam++;
    }
    flags_out.assign(3 * in.triCount(), 0);
    for (size_t f = 0; f < in.triCount(); f++)
        for (int e = 0; e < 3; e++) {
            const uint32_t a = in.idx[3 * f + e], b = in.idx[3 * f + (e + 1) % 3];
            if (t.canon[a] == t.canon[b]) continue;
            flags_out[3 * f + e] = t.em[t.key(a, b)].flags;
        }
    return st;
}

namespace {

typedef std::vector<std::pair<uint32_t, uint32_t>> Mids;   // new vertex (in.verts.size() + k) = edge (a, b) of the input

// FLAT topology. Every edge is split at its midpoint, except joint edges when split_joint_edges is off.
void flatPass(const MvParams& p, const MvMesh& in, MvMesh& out, std::vector<uint8_t>& oflags, Mids& mids) {
    std::vector<uint8_t> f;
    mv_classify_edges(in, p, f);
    const std::vector<V3> jw = mv_joint_worlds(in);
    out.name = in.name; out.joints = in.joints; out.verts = in.verts; out.scale = in.scale; out.materials = in.materials; out.pose = in.pose; out.headOnly = in.headOnly;
    out.idx.clear(); out.tmat.clear(); oflags.clear(); mids.clear(); size_t curT = 0;
    std::map<std::pair<uint32_t, uint32_t>, uint32_t> mid;
    auto midpoint = [&](uint32_t a, uint32_t b) -> uint32_t {
        if (a > b) { uint32_t t = a; a = b; b = t; }
        auto it = mid.find({ a, b });
        if (it != mid.end()) return it->second;
        const MvVertex &va = in.verts[a], &vb = in.verts[b];
        const V3 wa = mv_vertex_world(in, jw, a), wb = mv_vertex_world(in, jw, b);
        MvVertex m = va;
        m.joint = va.joint;   // the lower vertex index decides: the same for both faces of the edge
        const V3& o = jw[(size_t)m.joint];
        m.pos[0] = (wa.x + wb.x) * 0.5f - o.x; m.pos[1] = (wa.y + wb.y) * 0.5f - o.y; m.pos[2] = (wa.z + wb.z) * 0.5f - o.z;
        for (int i = 0; i < 2; i++) m.uv[i] = (va.uv[i] + vb.uv[i]) * 0.5f;
        for (int i = 0; i < 4; i++) m.col[i] = (va.col[i] + vb.col[i]) * 0.5f;
        out.verts.push_back(m);
        mids.push_back({ a, b });
        return mid[{ a, b }] = (uint32_t)(out.verts.size() - 1);
    };
    auto emit = [&](uint32_t a, uint32_t b, uint32_t c, uint8_t fa, uint8_t fb, uint8_t fc) {
        out.idx.push_back(a); out.idx.push_back(b); out.idx.push_back(c);
        if (!in.tmat.empty()) out.tmat.push_back(in.tmat[curT]);
        oflags.push_back(fa); oflags.push_back(fb); oflags.push_back(fc);
    };
    for (size_t t = 0; t < in.triCount(); t++) {
        curT = t;
        const uint32_t v[3] = { in.idx[3 * t], in.idx[3 * t + 1], in.idx[3 * t + 2] };
        const uint8_t* fl = &f[3 * t];
        int64_t m[3]; int n = 0, un = -1;
        for (int e = 0; e < 3; e++) {
            const bool split = p.split_joint_edges || !(fl[e] & MV_EDGE_JOINT);
            m[e] = split ? (int64_t)midpoint(v[e], v[(e + 1) % 3]) : -1;
            if (split) n++; else un = e;
        }
        if (n == 0) emit(v[0], v[1], v[2], fl[0], fl[1], fl[2]);
        else if (n == 3) {
            emit(v[0], (uint32_t)m[0], (uint32_t)m[2], fl[0], 0, fl[2]);
            emit((uint32_t)m[0], v[1], (uint32_t)m[1], fl[0], fl[1], 0);
            emit((uint32_t)m[2], (uint32_t)m[1], v[2], 0, fl[1], fl[2]);
            emit((uint32_t)m[0], (uint32_t)m[1], (uint32_t)m[2], 0, 0, 0);
        } else if (n == 1) {
            int i = 0; while (m[i] < 0) i++;
            const uint32_t a = v[i], b = v[(i + 1) % 3], c = v[(i + 2) % 3], mi = (uint32_t)m[i];
            emit(a, mi, c, fl[i], 0, fl[(i + 2) % 3]);
            emit(mi, b, c, fl[i], fl[(i + 1) % 3], 0);
        } else {   // two split edges, edge `un` stays whole
            const uint32_t p0 = v[(un + 1) % 3], p1 = v[(un + 2) % 3], p2 = v[un];
            const uint8_t g0 = fl[(un + 1) % 3], g1 = fl[(un + 2) % 3], g2 = fl[un];
            const uint32_t m0 = (uint32_t)m[(un + 1) % 3], m1 = (uint32_t)m[(un + 2) % 3];
            emit(p0, m0, p2, g0, 0, g2);
            emit(m0, p1, m1, g0, g1, 0);
            emit(m0, m1, p2, 0, g1, 0);
        }
    }
}

// Moves the new vertices (and for LOOP the originals) of a FLAT pass. All in world space on welded positions.
void reposition(MvMethod method, const MvParams& p, const MvMesh& in, MvMesh& out, const Mids& mids) {
    Topo t; buildTopo(in, p, t);
    const size_t nin = in.verts.size();
    auto setWorld = [&](size_t vi, V3 w) {
        const V3& o = t.jw[(size_t)out.verts[vi].joint];
        out.verts[vi].pos[0] = w.x - o.x; out.verts[vi].pos[1] = w.y - o.y; out.verts[vi].pos[2] = w.z - o.z;
    };
    const uint8_t kStraight = MV_EDGE_CREASE | MV_EDGE_BOUNDARY | MV_EDGE_JOINT;
    for (size_t k = 0; k < mids.size(); k++) {
        const uint32_t a = mids[k].first, b = mids[k].second;
        if (t.canon[a] == t.canon[b]) continue;
        const int ca = t.canon[a], cb = t.canon[b];
        const EdgeInfo& e = t.em[t.key(a, b)];
        if (e.flags & kStraight) continue;                      // stays at the flat midpoint
        const V3 Pa = t.cpos[(size_t)ca], Pb = t.cpos[(size_t)cb], M = (Pa + Pb) * 0.5f;
        V3 r = M;
        if (method == MV_PHONG) {
            const V3 Na = t.cnrm[(size_t)ca], Nb = t.cnrm[(size_t)cb];
            const V3 pa = M - Na * dot3(M - Pa, Na), pb = M - Nb * dot3(M - Pb, Nb);
            r = M + ((pa + pb) * 0.5f - M) * p.phong_alpha;
        } else if (e.nf == 2 && e.opp[0] >= 0 && e.opp[1] >= 0 && e.opp[0] != e.opp[1]) {
            const int c = e.opp[0], d = e.opp[1];
            // the wing across edge (x, y) of the face whose third vertex is `own`: the other face's opposite vertex
            auto wing = [&](int x, int y, int own) -> int {
                auto it = t.em.find(x < y ? std::make_pair(x, y) : std::make_pair(y, x));
                if (it == t.em.end() || it->second.nf != 2) return -1;
                const EdgeInfo& w = it->second;
                return w.opp[0] == own ? w.opp[1] : w.opp[0];
            };
            if (method == MV_BUTTERFLY) {
                const int w1 = wing(ca, c, cb), w2 = wing(c, cb, ca), w3 = wing(ca, d, cb), w4 = wing(d, cb, ca);
                if (w1 >= 0 && w2 >= 0 && w3 >= 0 && w4 >= 0)
                    r = M + (t.cpos[(size_t)c] + t.cpos[(size_t)d]) * 0.125f
                        - (t.cpos[(size_t)w1] + t.cpos[(size_t)w2] + t.cpos[(size_t)w3] + t.cpos[(size_t)w4]) * 0.0625f;
            } else if (method == MV_LOOP) {
                r = (Pa + Pb) * 0.375f + (t.cpos[(size_t)c] + t.cpos[(size_t)d]) * 0.125f;
            }
        }
        setWorld(nin + k, r);
    }
    if (method == MV_LOOP) {   // the original vertices move too (smooth, interior, not on a straight edge)
        std::vector<V3> np(t.cpos.size());
        std::vector<uint8_t> move(t.cpos.size(), 0);
        for (size_t c = 0; c < t.cpos.size(); c++) {
            const int n = (int)t.cnbr[c].size();
            if (t.cstraight[c] || n < 3) continue;
            const float beta = n == 3 ? 3.0f / 16.0f : 3.0f / (8.0f * n);
            V3 s = { 0, 0, 0 };
            for (int q : t.cnbr[c]) s = s + t.cpos[(size_t)q];
            np[c] = t.cpos[c] * (1.0f - n * beta) + s * beta;
            move[c] = 1;
        }
        for (size_t i = 0; i < nin; i++) if (move[(size_t)t.canon[i]]) setWorld(i, np[(size_t)t.canon[i]]);
    }
}

// The converter's PN pass on an MvMesh: the same cv_sd_run, with the converter's own vertex/edge conventions.
bool pnPass(const MvParams& p, const MvMesh& in, MvMesh& out, std::vector<uint8_t>& oflags, bool game) {
    const std::vector<V3> jw = mv_joint_worlds(in);
    const int nv = (int)in.verts.size(), nt = (int)in.triCount();
    CvSdMesh m; memset(&m, 0, sizeof m);
    m.v = (CvSdVert*)calloc((size_t)nv + 1, sizeof(CvSdVert));
    m.tri = (int*)malloc((size_t)(nt ? nt : 1) * 3 * sizeof(int));
    for (int i = 0; i < nv; i++) {
        const MvVertex& s = in.verts[(size_t)i]; CvSdVert& d = m.v[i];
        for (int k = 0; k < 3; k++) d.pos[k] = (int16_t)lroundf(s.pos[k]);
        d.st[0] = (int16_t)lroundf(s.uv[0] * 1024.0f); d.st[1] = (int16_t)lroundf(s.uv[1] * 1024.0f);
        for (int k = 0; k < 4; k++) d.cn[k] = (uint8_t)lroundf(fminf(fmaxf(s.col[k], 0.f), 1.f) * 255.0f);
        d.mtx = s.joint; d.layer = s.layer; d.head = s.head;
        const V3& o = jw[(size_t)s.joint];
        d.rest[0] = (float)d.pos[0] + o.x; d.rest[1] = (float)d.pos[1] + o.y; d.rest[2] = (float)d.pos[2] + o.z;   // as the converter: pos + joint offsets
    }
    for (int i = 0; i < nt * 3; i++) m.tri[i] = (int)in.idx[(size_t)i];
    m.nv = m.nv_in = nv; m.nt = nt;
    const bool headOnly = game && in.headOnly;   // GAME: the converter (130 degrees, heads only); PN: the whole model
    CvSdParams cp = { headOnly ? CV_SD_HEAD_CREASE_DEG : p.crease_deg, p.pn_clamp };
    m.params = &cp; m.head_only = headOnly ? 1 : 0;
    const int rc = cv_sd_run(&m);
    if (rc) { free(m.v); free(m.tri); cv_sd_free_out(&m); return false; }
    out.name = in.name; out.joints = in.joints; out.scale = in.scale; out.verts = in.verts; out.materials = in.materials; out.pose = in.pose; out.headOnly = in.headOnly;
    // the original vertices are kept as they are (float); the midpoints come from the library (int16, as in the game)
    std::map<std::pair<uint32_t, uint32_t>, uint32_t> midOf;
    for (int k = 0; k < m.nmid; k++) {
        const CvSdVert& s = m.v[m.nv_in + k];
        const MvVertex &A = in.verts[(size_t)m.mid_ab[2 * k]], &B = in.verts[(size_t)m.mid_ab[2 * k + 1]];
        MvVertex d = A;
        for (int q = 0; q < 3; q++) d.pos[q] = (float)s.pos[q];
        d.uv[0] = s.st[0] / 1024.0f; d.uv[1] = s.st[1] / 1024.0f;
        for (int q = 0; q < 4; q++) d.col[q] = (A.col[q] + B.col[q]) * 0.5f;
        d.joint = s.mtx; d.layer = s.layer;
        out.verts.push_back(d);
        midOf[{ (uint32_t)m.mid_ab[2 * k], (uint32_t)m.mid_ab[2 * k + 1] }] = (uint32_t)(m.nv_in + k);
    }
    out.idx.assign((size_t)m.not_ * 3, 0);
    for (int i = 0; i < m.not_ * 3; i++) out.idx[(size_t)i] = (uint32_t)m.otri[i];
    out.tmat.clear();
    if (!in.tmat.empty()) for (int o = 0; o < m.not_; o++) out.tmat.push_back(in.tmat[(size_t)m.osrc[o]]);
    // edge reasons: the library's (crease / boundary / joint) plus the UV seam of the viewer's classifier
    std::vector<uint8_t> mine; mv_classify_edges(in, p, mine);
    std::vector<uint8_t> inFlags((size_t)nt * 3);
    for (int i = 0; i < nt * 3; i++) {
        const uint8_t r = m.ereason[i];
        inFlags[(size_t)i] = (uint8_t)(((r & CV_SD_E_CREASE) ? MV_EDGE_CREASE : 0) | ((r & CV_SD_E_BOUNDARY) ? MV_EDGE_BOUNDARY : 0) |
                                       ((r & CV_SD_E_JOINT) ? MV_EDGE_JOINT : 0) | (mine[(size_t)i] & MV_EDGE_UVSEAM));
    }
    oflags.assign((size_t)m.not_ * 3, 0);
    for (int o = 0; o < m.not_; o++) {
        const int t = m.osrc[o];
        for (int e = 0; e < 3; e++) {
            const uint32_t a = (uint32_t)m.otri[3 * o + e], b = (uint32_t)m.otri[3 * o + (e + 1) % 3];
            for (int se = 0; se < 3; se++) {
                const uint32_t ve = (uint32_t)m.tri[3 * t + se], vf = (uint32_t)m.tri[3 * t + (se + 1) % 3];
                auto it = midOf.find({ ve < vf ? ve : vf, ve < vf ? vf : ve });
                const int64_t me = it == midOf.end() ? -1 : (int64_t)it->second;
                auto is = [&](uint32_t x, uint32_t y) { return (a == x && b == y) || (a == y && b == x); };
                if (is(ve, vf) || (me >= 0 && (is(ve, (uint32_t)me) || is((uint32_t)me, vf)))) { oflags[(size_t)(3 * o + e)] = inFlags[(size_t)(3 * t + se)]; break; }
            }
        }
    }
    free(m.v); free(m.tri); cv_sd_free_out(&m);
    return true;
}

}  // namespace

bool mv_subdivide(MvMethod method, const MvParams& p, const MvMesh& in, MvMesh& out, std::vector<uint8_t>& edge_flags_out) {
    if (method == MV_HEAD) return mv_head_subdivide(p, in, out, edge_flags_out);
    const int passes = p.passes < 1 ? 1 : p.passes > 2 ? 2 : p.passes;
    MvMesh cur = in, nxt;
    std::vector<uint8_t> fl;
    Mids mids;
    for (int i = 0; i < passes; i++) {
        if (method == MV_PN || method == MV_GAME) {
            if (!pnPass(p, cur, nxt, fl, method == MV_GAME)) { flatPass(p, cur, nxt, fl, mids); }
        } else {
            flatPass(p, cur, nxt, fl, mids);
            if (method != MV_FLAT) reposition(method, p, cur, nxt, mids);
        }
        cur = nxt;
    }
    out = cur; edge_flags_out = fl;
    if (method == MV_GAME && out.headOnly) mv_mark_neck(out, edge_flags_out);
    return true;
}

void mv_mark_neck(const MvMesh& m, std::vector<uint8_t>& flags) {
    if (!m.headOnly) return;
    const std::vector<V3> jw = mv_joint_worlds(m);
    std::map<std::array<int, 3>, int> weld;
    std::vector<int> canon(m.verts.size());
    for (size_t i = 0; i < m.verts.size(); i++) {
        const V3 w = mv_vertex_world(m, jw, i);
        const std::array<int, 3> k = { (int)floorf(w.x * 1000.0f + 0.5f), (int)floorf(w.y * 1000.0f + 0.5f), (int)floorf(w.z * 1000.0f + 0.5f) };
        auto it = weld.find(k);
        if (it == weld.end()) { const int id = (int)weld.size(); weld[k] = id; canon[i] = id; } else canon[i] = it->second;
    }
    std::map<std::pair<int, int>, int> seen;   // bit 0: a head triangle, bit 1: a body triangle
    for (size_t t = 0; t < m.triCount(); t++) {
        const int b = mv_tri_is_head(m, t) ? 1 : 2;
        for (int e = 0; e < 3; e++) {
            const int x = canon[m.idx[3 * t + e]], y = canon[m.idx[3 * t + (e + 1) % 3]];
            if (x != y) seen[x < y ? std::make_pair(x, y) : std::make_pair(y, x)] |= b;
        }
    }
    flags.resize(3 * m.triCount(), 0);
    for (size_t t = 0; t < m.triCount(); t++)
        for (int e = 0; e < 3; e++) {
            const int x = canon[m.idx[3 * t + e]], y = canon[m.idx[3 * t + (e + 1) % 3]];
            if (x != y && seen[x < y ? std::make_pair(x, y) : std::make_pair(y, x)] == 3) flags[3 * t + e] |= MV_EDGE_NECK;
        }
}
