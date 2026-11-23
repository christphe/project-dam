// modelview: an original, procedural low-poly mannequin (the phase-1 test model).
// Tubes (shared side vertices, one UV seam), boxes (hard edges), and parts of different joints that
// share seam rings (the elbows, knees, waist and neck), so every edge reason shows up.
#include "mv_mesh.h"
#include <math.h>

namespace {

struct Builder {
    MvMesh m;
    std::vector<MvVec3> jw;   // joint world positions

    int joint(int parent, float x, float y, float z) {
        m.joints.push_back({ parent, { x, y, z } });
        jw = mv_joint_worlds(m);
        return (int)m.joints.size() - 1;
    }
    uint32_t vert(float x, float y, float z, float u, float v, const float* c, float shade, int j) {
        MvVertex w = {};
        w.pos[0] = x - jw[(size_t)j].x; w.pos[1] = y - jw[(size_t)j].y; w.pos[2] = z - jw[(size_t)j].z;
        w.uv[0] = u; w.uv[1] = v;
        w.col[0] = c[0] * shade; w.col[1] = c[1] * shade; w.col[2] = c[2] * shade; w.col[3] = 1;
        w.joint = j;
        m.verts.push_back(w);
        return (uint32_t)m.verts.size() - 1;
    }
    void tri(uint32_t a, uint32_t b, uint32_t c) { m.idx.push_back(a); m.idx.push_back(b); m.idx.push_back(c); }

    // A tube through rings {y, rx, rz} around (cx, cz), n sides. The seam column is duplicated (u = 0 and 1).
    struct Ring { float y, rx, rz; };
    void tube(int j, float cx, float cz, const Ring* r, int nr, int n, const float* col, bool capBottom, bool capTop) {
        std::vector<uint32_t> id((size_t)nr * (n + 1));
        for (int i = 0; i < nr; i++)
            for (int k = 0; k <= n; k++) {
                const float a = 6.2831853f * (k % n) / n;
                id[(size_t)i * (n + 1) + k] = vert(cx + r[i].rx * cosf(a), r[i].y, cz + r[i].rz * sinf(a), (float)k / n, (float)i / (nr - 1), col, 0.7f + 0.3f * i / (nr - 1), j);
            }
        for (int i = 0; i + 1 < nr; i++)
            for (int k = 0; k < n; k++) {
                const uint32_t A = id[(size_t)i * (n + 1) + k], B = id[(size_t)i * (n + 1) + k + 1];
                const uint32_t C = id[(size_t)(i + 1) * (n + 1) + k], D = id[(size_t)(i + 1) * (n + 1) + k + 1];
                tri(A, C, D); tri(A, D, B);
            }
        for (int side = 0; side < 2; side++) {
            if (!(side ? capTop : capBottom)) continue;
            const Ring& q = r[side ? nr - 1 : 0];
            const float sh = side ? 1.0f : 0.7f;
            const uint32_t c = vert(cx, q.y, cz, 0.5f, 0.5f, col, sh, j);
            std::vector<uint32_t> rim((size_t)n);
            for (int k = 0; k < n; k++) {
                const float a = 6.2831853f * k / n;
                rim[(size_t)k] = vert(cx + q.rx * cosf(a), q.y, cz + q.rz * sinf(a), 0.5f + 0.5f * cosf(a), 0.5f + 0.5f * sinf(a), col, sh, j);
            }
            for (int k = 0; k < n; k++) {
                const uint32_t a = rim[(size_t)k], b = rim[(size_t)((k + 1) % n)];
                if (side) tri(c, b, a); else tri(c, a, b);
            }
        }
    }
    // An axis-aligned box: 24 vertices, every edge hard.
    void box(int j, float cx, float cy, float cz, float sx, float sy, float sz, const float* col) {
        static const float ax[6][2][3] = {   // u, v per face; u x v = the face normal
            { { 0, 1, 0 }, { 0, 0, 1 } }, { { 0, 0, 1 }, { 0, 1, 0 } }, { { 0, 0, 1 }, { 1, 0, 0 } },
            { { 1, 0, 0 }, { 0, 0, 1 } }, { { 1, 0, 0 }, { 0, 1, 0 } }, { { 0, 1, 0 }, { 1, 0, 0 } } };
        static const float nm[6][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
        const float h[3] = { sx * 0.5f, sy * 0.5f, sz * 0.5f };
        for (int f = 0; f < 6; f++) {
            uint32_t q[4];
            for (int c = 0; c < 4; c++) {
                const float su = (c == 1 || c == 2) ? 1.f : -1.f, sv = (c >= 2) ? 1.f : -1.f;
                float p[3];
                for (int d = 0; d < 3; d++) p[d] = (d == 0 ? cx : d == 1 ? cy : cz) + nm[f][d] * h[d] + ax[f][0][d] * su * h[d] + ax[f][1][d] * sv * h[d];
                q[c] = vert(p[0], p[1], p[2], su > 0 ? 1.f : 0.f, sv > 0 ? 1.f : 0.f, col, 0.8f + 0.2f * (nm[f][1] > 0 ? 1.f : 0.f), j);
            }
            tri(q[0], q[1], q[2]); tri(q[0], q[2], q[3]);
        }
    }
};

}  // namespace

MvMesh mv_make_mannequin() {
    Builder b;
    b.m.name = "Test mannequin";
    // Rings always run bottom to top (the winding depends on it).
    // Rest offsets are binary fractions: world positions stay exact, so seam rings weld cleanly.
    const int pelvis = b.joint(-1, 0, 0.875f, 0), spine = b.joint(pelvis, 0, 0.25f, 0), head = b.joint(spine, 0, 0.375f, 0);
    const int uaL = b.joint(spine, 0.34375f, 0.3125f, 0), faL = b.joint(uaL, 0, -0.375f, 0);
    const int uaR = b.joint(spine, -0.34375f, 0.3125f, 0), faR = b.joint(uaR, 0, -0.375f, 0);
    const int thL = b.joint(pelvis, 0.125f, 0, 0), shL = b.joint(thL, 0, -0.4375f, 0);
    const int thR = b.joint(pelvis, -0.125f, 0, 0), shR = b.joint(thR, 0, -0.4375f, 0);

    const float cTorso[3] = { 0.20f, 0.60f, 0.65f }, cPelvis[3] = { 0.30f, 0.32f, 0.55f }, cHead[3] = { 0.95f, 0.88f, 0.75f };
    const float cArm[3] = { 0.90f, 0.55f, 0.25f }, cLeg[3] = { 0.45f, 0.32f, 0.65f }, cEnd[3] = { 0.90f, 0.90f, 0.92f }, cCrest[3] = { 0.85f, 0.25f, 0.25f };
    typedef Builder::Ring R;

    const R pel[] = { { 0.8125f, 0.18f, 0.11f }, { 1.125f, 0.19f, 0.11f } };
    b.tube(pelvis, 0, 0, pel, 2, 8, cPelvis, true, false);
    const R tor[] = { { 1.125f, 0.19f, 0.11f }, { 1.3125f, 0.17f, 0.10f }, { 1.5f, 0.25f, 0.13f }, { 1.5625f, 0.22f, 0.12f }, { 1.625f, 0.075f, 0.075f }, { 1.6875f, 0.075f, 0.075f } };
    b.tube(spine, 0, 0, tor, 6, 8, cTorso, false, false);   // the bottom ring is the pelvis' top ring, the top one the head's
    const R hd[] = { { 1.6875f, 0.075f, 0.075f }, { 1.75f, 0.11f, 0.11f }, { 1.875f, 0.125f, 0.135f }, { 1.9375f, 0.09f, 0.10f } };
    b.tube(head, 0, 0, hd, 4, 8, cHead, false, true);
    b.box(head, 0, 1.9375f, 0, 0.05f, 0.08f, 0.22f, cCrest);        // a crest
    b.box(head, 0, 1.8125f, 0.14f, 0.05f, 0.07f, 0.06f, cHead);     // a nose

    for (int s = 0; s < 2; s++) {
        const float sx = s ? -1.f : 1.f, ax = 0.34375f * sx;
        const int ua = s ? uaR : uaL, fa = s ? faR : faL, th = s ? thR : thL, sh = s ? shR : shL;
        const R up[] = { { 1.0625f, 0.05f, 0.05f }, { 1.5f, 0.065f, 0.065f } };
        b.tube(ua, ax, 0, up, 2, 6, cArm, false, true);
        const R lo[] = { { 0.6875f, 0.04f, 0.04f }, { 1.0625f, 0.05f, 0.05f } };
        b.tube(fa, ax, 0, lo, 2, 6, cArm, true, false);
        b.box(fa, ax, 0.625f, 0, 0.08f, 0.12f, 0.05f, cEnd);        // hand
        const float lx = 0.125f * sx;
        const R thg[] = { { 0.4375f, 0.065f, 0.065f }, { 0.9375f, 0.085f, 0.085f } };
        b.tube(th, lx, 0, thg, 2, 6, cLeg, false, true);
        const R shn[] = { { 0.09f, 0.045f, 0.045f }, { 0.4375f, 0.065f, 0.065f } };
        b.tube(sh, lx, 0, shn, 2, 6, cLeg, true, false);
        b.box(sh, lx, 0.045f, 0.06f, 0.1f, 0.09f, 0.22f, cEnd);     // foot
    }
    // model units are mm (the subdivision works on int16-like units, as the game's models do)
    for (auto& j : b.m.joints) for (int k = 0; k < 3; k++) j.offset[k] *= 1000.0f;
    for (auto& v : b.m.verts) for (int k = 0; k < 3; k++) v.pos[k] *= 1000.0f;
    b.m.scale = 0.001f;
    return b.m;
}
