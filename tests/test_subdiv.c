/* Character-model subdivision (port/convert/conv_subdiv.c): pure, no core. */
#include "ge_test.h"
#include "../port/convert/conv_subdiv.h"

#include <math.h>
#include <stdlib.h>

static CvSdVert mkv(int x, int y, int z, int s, int t, int mtx)
{
    CvSdVert v;
    memset(&v, 0, sizeof v);
    v.pos[0] = (int16_t)x; v.pos[1] = (int16_t)y; v.pos[2] = (int16_t)z;
    v.st[0] = (int16_t)s; v.st[1] = (int16_t)t;
    v.mtx = mtx;
    v.rest[0] = (float)x; v.rest[1] = (float)y; v.rest[2] = (float)z;
    return v;
}

static int near_f(float a, float b, float eps) { return fabsf(a - b) <= eps; }

/* signed area (x/y plane, counter-clockwise > 0) of a triangle of mesh vertices */
static float area2(const CvSdMesh *m, const int *t)
{
    const CvSdVert *a = &m->v[t[0]], *b = &m->v[t[1]], *c = &m->v[t[2]];
    return (float)(b->pos[0] - a->pos[0]) * (c->pos[1] - a->pos[1]) - (float)(c->pos[0] - a->pos[0]) * (b->pos[1] - a->pos[1]);
}

static void mesh_init(CvSdMesh *m, int nv, int nt)
{
    memset(m, 0, sizeof *m);
    m->v = calloc((size_t)nv, sizeof *m->v);
    m->tri = calloc((size_t)nt * 3, sizeof(int));
    m->nv = m->nv_in = nv;
    m->nt = nt;
}

static void mesh_free(CvSdMesh *m)
{
    cv_sd_free_out(m);
    free(m->v); free(m->tri);
}

static void test_pn_mid(void)
{
    /* hand computed: wij = 8*0.6 = 4.8 = wji; mid = (4,0,0) - (4.8*Ni + 4.8*Nj)/8 = (4, 0, -0.96) */
    const float pi[3] = {0, 0, 0}, pj[3] = {8, 0, 0};
    const float ni[3] = {0.6f, 0, 0.8f}, nj[3] = {-0.6f, 0, 0.8f};
    float a[3], b[3];
    cv_sd_pn_mid(pi, ni, pj, nj, a);
    GE_CHECK(near_f(a[0], 4.0f, 1e-5f));
    GE_CHECK(near_f(a[1], 0.0f, 1e-5f));
    GE_CHECK(near_f(a[2], -0.96f, 1e-5f));
    cv_sd_pn_mid(pj, nj, pi, ni, b);            /* symmetric: swapped ends give the same bits */
    GE_CHECK(memcmp(a, b, sizeof a) == 0);
    {   /* flat normals perpendicular to the chord: no bulge */
        const float n0[3] = {0, 0, 1};
        cv_sd_pn_mid(pi, n0, pj, n0, a);
        GE_CHECK(near_f(a[0], 4.0f, 1e-6f) && near_f(a[2], 0.0f, 1e-6f));
    }
}

static void test_attr_mid(void)
{
    CvSdVert a = mkv(0, 0, 0, 10, 20, 0), b = mkv(0, 0, 0, 30, -20, 0), o;
    a.cn[0] = 0; a.cn[1] = 100; a.cn[2] = 255; a.cn[3] = 10;
    b.cn[0] = 200; b.cn[1] = 50; b.cn[2] = 255; b.cn[3] = 11;
    memset(&o, 0, sizeof o);
    cv_sd_attr_mid(&a, &b, &o);
    GE_CHECK_EQ_INT(o.st[0], 20);
    GE_CHECK_EQ_INT(o.st[1], 0);
    GE_CHECK_EQ_INT(o.cn[0], 100);
    GE_CHECK_EQ_INT(o.cn[1], 75);
    GE_CHECK_EQ_INT(o.cn[2], 255);
    GE_CHECK_EQ_INT(o.cn[3], 11);
    {   /* symmetric */
        CvSdVert p;
        memset(&p, 0, sizeof p);
        cv_sd_attr_mid(&b, &a, &p);
        GE_CHECK(p.st[0] == o.st[0] && p.st[1] == o.st[1] && memcmp(p.cn, o.cn, 4) == 0);
    }
}

static void test_split(void)
{
    /* CCW triangle (0,0) (10,0) (0,10), midpoints 3,4,5 of edges 0,1,2 */
    CvSdMesh m;
    static const int want_n[8] = {1, 2, 2, 3, 2, 3, 3, 4};
    mesh_init(&m, 6, 0);
    m.v[0] = mkv(0, 0, 0, 0, 0, 0); m.v[1] = mkv(10, 0, 0, 0, 0, 0); m.v[2] = mkv(0, 10, 0, 0, 0, 0);
    m.v[3] = mkv(5, 0, 0, 0, 0, 0); m.v[4] = mkv(5, 5, 0, 0, 0, 0); m.v[5] = mkv(0, 5, 0, 0, 0, 0);
    for (int mask = 0; mask < 8; mask++) {
        int v[3] = {0, 1, 2}, mid[3], out[4][3], n;
        float pv[3][3], pm[3][3], total = 0;
        int ok = 1;
        for (int e = 0; e < 3; e++) mid[e] = (mask >> e) & 1 ? 3 + e : -1;
        for (int c = 0; c < 3; c++) for (int k = 0; k < 3; k++) pv[c][k] = (float)m.v[v[c]].pos[k];
        for (int e = 0; e < 3; e++) for (int k = 0; k < 3; k++) pm[e][k] = mid[e] >= 0 ? (float)m.v[mid[e]].pos[k] : 0.f;
        n = cv_sd_split_tri(v, mid, pv, pm, out);
        GE_CHECK_EQ_INT(n, want_n[mask]);
        for (int k = 0; k < n; k++) {
            float a = area2(&m, out[k]);
            if (a <= 0.f) ok = 0;               /* winding kept (CCW stays CCW) */
            total += a;
        }
        GE_CHECK(ok);
        GE_CHECK(near_f(total, 100.f, 1e-3f));  /* the pieces cover the triangle exactly */
    }
    mesh_free(&m);
}

static void test_boundary_and_cross_joint(void)
{
    CvSdMesh m;
    /* one triangle: every edge is a boundary -> flat midpoints, 1 -> 4 */
    mesh_init(&m, 3, 1);
    m.v[0] = mkv(0, 0, 0, 0, 0, 7); m.v[1] = mkv(100, 0, 40, 64, 0, 7); m.v[2] = mkv(0, 100, 0, 0, 64, 7);
    m.tri[0] = 0; m.tri[1] = 1; m.tri[2] = 2;
    GE_CHECK_EQ_INT(cv_sd_run(&m), 0);
    GE_CHECK_EQ_INT(m.not_, 4);
    GE_CHECK_EQ_INT(m.nmid, 3);
    GE_CHECK_EQ_INT(m.npn, 0);
    GE_CHECK_EQ_INT(m.nflat, 3);
    {
        int found = 0;
        for (int k = 0; k < m.nmid; k++) {
            const CvSdVert *v = &m.v[m.nv_in + k];
            if (m.mid_ab[2 * k] == 0 && m.mid_ab[2 * k + 1] == 1) {   /* flat: (0,0,0)+(100,0,40) / 2 */
                found = v->pos[0] == 50 && v->pos[1] == 0 && v->pos[2] == 20 && v->st[0] == 32 && v->st[1] == 0;
            }
        }
        GE_CHECK(found);
    }
    mesh_free(&m);

    /* vertex 1 sits under another matrix: both edges touching it are not split */
    mesh_init(&m, 3, 1);
    m.v[0] = mkv(0, 0, 0, 0, 0, 7); m.v[1] = mkv(100, 0, 0, 0, 0, 8); m.v[2] = mkv(0, 100, 0, 0, 0, 7);
    m.tri[0] = 0; m.tri[1] = 1; m.tri[2] = 2;
    GE_CHECK_EQ_INT(cv_sd_run(&m), 0);
    GE_CHECK_EQ_INT(m.nmid, 1);
    GE_CHECK_EQ_INT(m.not_, 2);
    mesh_free(&m);
}

static void test_crease_and_pn(void)
{
    CvSdMesh m;
    /* roof: two triangles at 90 degrees along the edge (0,0,0)-(100,0,0): a crease -> flat */
    mesh_init(&m, 4, 2);
    m.v[0] = mkv(0, 0, 0, 0, 0, 1); m.v[1] = mkv(100, 0, 0, 0, 0, 1);
    m.v[2] = mkv(0, 100, 0, 0, 0, 1); m.v[3] = mkv(0, 0, 100, 0, 0, 1);
    m.tri[0] = 0; m.tri[1] = 1; m.tri[2] = 2;
    m.tri[3] = 1; m.tri[4] = 0; m.tri[5] = 3;
    GE_CHECK_EQ_INT(cv_sd_run(&m), 0);
    GE_CHECK_EQ_INT(m.npn, 0);
    GE_CHECK_EQ_INT(m.nmid, 5);              /* the shared edge has one midpoint */
    for (int k = 0; k < m.nmid; k++)
        if (m.mid_ab[2 * k] == 0 && m.mid_ab[2 * k + 1] == 1) {
            const CvSdVert *v = &m.v[m.nv_in + k];
            GE_CHECK(v->pos[0] == 50 && v->pos[1] == 0 && v->pos[2] == 0);
        }
    mesh_free(&m);

    /* flat shallow pyramid (open base): apex (0,0,300), rim (+-1000,0,0) (0,+-1000,0).
     * The spoke apex->(1000,0,0) joins two faces ~32 degrees apart: smooth -> PN. */
    mesh_init(&m, 5, 4);
    m.v[0] = mkv(0, 0, 300, 0, 0, 1);
    m.v[1] = mkv(1000, 0, 0, 0, 0, 1); m.v[2] = mkv(0, 1000, 0, 0, 0, 1);
    m.v[3] = mkv(-1000, 0, 0, 0, 0, 1); m.v[4] = mkv(0, -1000, 0, 0, 0, 1);
    { static const int t[12] = {1, 2, 0, 2, 3, 0, 3, 4, 0, 4, 1, 0}; memcpy(m.tri, t, sizeof t); }
    GE_CHECK_EQ_INT(cv_sd_run(&m), 0);
    GE_CHECK_EQ_INT(m.not_, 16);
    GE_CHECK_EQ_INT(m.npn, 4);                /* the 4 spokes */
    GE_CHECK_EQ_INT(m.nflat, 4);              /* the 4 open rim edges */
    for (int k = 0; k < m.nmid; k++) {
        int a = m.mid_ab[2 * k], b = m.mid_ab[2 * k + 1];
        const CvSdVert *v = &m.v[m.nv_in + k];
        if (a == 0 && b == 1) {
            /* hand: Ni=(0,0,1), Nj=(0.6,0,2)/|.|, wij=-300, wji~0 -> z = 150 + 300/8 = 187.5 */
            GE_CHECK(v->pos[0] == 500 && v->pos[1] == 0);
            GE_CHECK(v->pos[2] >= 187 && v->pos[2] <= 188);
        }
        if (a == 1 && b == 2) GE_CHECK(v->pos[0] == 500 && v->pos[1] == 500 && v->pos[2] == 0);
    }
    mesh_free(&m);
}

static void test_shared_edge_and_seam(void)
{
    CvSdMesh m;
    int ia = -1, ib = -1;
    /* two coplanar triangles sharing the edge v1-v2: one midpoint, referenced by both */
    mesh_init(&m, 4, 2);
    m.v[0] = mkv(0, 0, 0, 0, 0, 3); m.v[1] = mkv(100, 0, 0, 100, 0, 3);
    m.v[2] = mkv(0, 100, 0, 0, 100, 3); m.v[3] = mkv(100, 100, 0, 100, 100, 3);
    m.tri[0] = 0; m.tri[1] = 1; m.tri[2] = 2;
    m.tri[3] = 1; m.tri[4] = 3; m.tri[5] = 2;
    GE_CHECK_EQ_INT(cv_sd_run(&m), 0);
    GE_CHECK_EQ_INT(m.nmid, 5);
    GE_CHECK_EQ_INT(m.not_, 8);
    for (int k = 0; k < m.nmid; k++)
        if (m.mid_ab[2 * k] == 1 && m.mid_ab[2 * k + 1] == 2) ia = m.nv_in + k;
    GE_CHECK(ia >= 0);
    {   /* both source triangles reference that one id */
        int in0 = 0, in1 = 0;
        for (int o = 0; o < m.not_; o++)
            for (int c = 0; c < 3; c++)
                if (m.otri[3 * o + c] == ia) { if (m.osrc[o] == 0) in0 = 1; else in1 = 1; }
        GE_CHECK(in0 && in1);
        GE_CHECK(m.v[ia].pos[0] == 50 && m.v[ia].pos[1] == 50);
    }
    for (int o = 0; o < m.not_; o++) GE_CHECK(area2(&m, &m.otri[3 * o]) > 0.f);   /* winding */
    mesh_free(&m);

    /* UV seam: the two triangles use separate copies of v1/v2 (same positions, other UVs).
     * They weld, so the shared edge is split on both sides; the midpoints agree in
     * position but each takes its own UVs. */
    mesh_init(&m, 6, 2);
    m.v[0] = mkv(0, 0, 0, 0, 0, 3); m.v[1] = mkv(100, 0, 0, 100, 0, 3); m.v[2] = mkv(0, 100, 0, 0, 100, 3);
    m.v[3] = mkv(100, 0, 0, 500, 40, 3); m.v[4] = mkv(100, 100, 0, 700, 40, 3); m.v[5] = mkv(0, 100, 0, 500, 200, 3);
    m.tri[0] = 0; m.tri[1] = 1; m.tri[2] = 2;
    m.tri[3] = 3; m.tri[4] = 4; m.tri[5] = 5;
    GE_CHECK_EQ_INT(cv_sd_run(&m), 0);
    GE_CHECK_EQ_INT(m.nmid, 6);               /* no sharing: the copies are different vertices */
    GE_CHECK_EQ_INT(m.npn, 2);                /* welded: the seam edge has two faces, coplanar -> PN (no bulge), both copies */
    GE_CHECK_EQ_INT(m.nflat, 4);              /* the 4 open edges */
    for (int k = 0; k < m.nmid; k++) {
        int a = m.mid_ab[2 * k], b = m.mid_ab[2 * k + 1];
        if (a == 1 && b == 2) ia = m.nv_in + k;
        if (a == 3 && b == 5) ib = m.nv_in + k;
    }
    GE_CHECK(ia >= 0 && ib >= 0);
    GE_CHECK(m.v[ia].pos[0] == m.v[ib].pos[0] && m.v[ia].pos[1] == m.v[ib].pos[1] && m.v[ia].pos[2] == m.v[ib].pos[2]);
    GE_CHECK(m.v[ia].pos[0] == 50 && m.v[ia].pos[1] == 50);
    GE_CHECK_EQ_INT(m.v[ia].st[0], 50);       /* (100 + 0) / 2 */
    GE_CHECK_EQ_INT(m.v[ia].st[1], 50);       /* (0 + 100) / 2 */
    GE_CHECK_EQ_INT(m.v[ib].st[0], 500);      /* (500 + 500) / 2 */
    GE_CHECK_EQ_INT(m.v[ib].st[1], 120);      /* (40 + 200) / 2 */
    mesh_free(&m);
}

static CvSdVert mkh(int x, int y, int z, int mtx)
{
    CvSdVert v = mkv(x, y, z, 0, 0, mtx);
    v.head = 1;
    return v;
}

/* head region: crease limit 130 degrees; body region: nothing is split; neck edge: not split */
static void test_head_body(void)
{
    static const CvSdParams head_params = {130.0f, 0.15f};
    CvSdMesh m;
    /* two triangles on the edge (0,0,0)-(100,0,0), the second one folded by 100 / 140 degrees */
    static const int fold[2][3] = {{0, 17, 98}, {0, 77, 64}};
    for (int k = 0; k < 2; k++) {
        mesh_init(&m, 4, 2);
        m.params = &head_params; m.head_only = 1;
        m.v[0] = mkh(0, 0, 0, 1); m.v[1] = mkh(100, 0, 0, 1); m.v[2] = mkh(0, 100, 0, 1);
        m.v[3] = mkh(fold[k][0], fold[k][1], fold[k][2], 1);
        m.tri[0] = 0; m.tri[1] = 1; m.tri[2] = 2;
        m.tri[3] = 1; m.tri[4] = 0; m.tri[5] = 3;
        GE_CHECK_EQ_INT(cv_sd_run(&m), 0);
        GE_CHECK_EQ_INT(m.nmid, 5);
        GE_CHECK_EQ_INT(m.npn, k == 0 ? 1 : 0);      /* 100 degrees: PN; 140 degrees: crease, flat */
        mesh_free(&m);
    }

    /* body region: no triangle is head, nothing is split */
    mesh_init(&m, 4, 2);
    m.params = &head_params; m.head_only = 1;
    m.v[0] = mkv(0, 0, 0, 0, 0, 1); m.v[1] = mkv(100, 0, 0, 0, 0, 1);
    m.v[2] = mkv(0, 100, 0, 0, 0, 1); m.v[3] = mkv(100, 100, 0, 0, 0, 1);
    m.tri[0] = 0; m.tri[1] = 1; m.tri[2] = 2;
    m.tri[3] = 1; m.tri[4] = 3; m.tri[5] = 2;
    GE_CHECK_EQ_INT(cv_sd_run(&m), 0);
    GE_CHECK_EQ_INT(m.nmid, 0);
    GE_CHECK_EQ_INT(m.not_, 2);
    mesh_free(&m);

    /* neck: head triangle (0,1,2) shares the edge 1-2 with a body triangle (1,3,2); vertices 1 and 2
     * are head vertices but the body triangle has the body vertex 3. The shared edge stays whole,
     * the head triangle's two free edges are split. */
    mesh_init(&m, 4, 2);
    m.params = &head_params; m.head_only = 1;
    m.v[0] = mkh(0, 0, 0, 1); m.v[1] = mkh(100, 0, 0, 1); m.v[2] = mkh(0, 100, 0, 1);
    m.v[3] = mkv(100, 100, 0, 0, 0, 1);
    m.tri[0] = 0; m.tri[1] = 1; m.tri[2] = 2;
    m.tri[3] = 1; m.tri[4] = 3; m.tri[5] = 2;
    GE_CHECK_EQ_INT(cv_sd_run(&m), 0);
    GE_CHECK_EQ_INT(m.nmid, 2);
    GE_CHECK_EQ_INT(m.not_, 4);                      /* head triangle -> 3, body triangle -> 1 */
    for (int k = 0; k < m.nmid; k++)
        GE_CHECK(!(m.mid_ab[2 * k] == 1 && m.mid_ab[2 * k + 1] == 2));
    for (int o = 0; o < m.not_; o++) GE_CHECK(area2(&m, &m.otri[3 * o]) > 0.f);
    mesh_free(&m);
}

int main(void)
{
    GE_TEST_BEGIN("subdiv");
    test_pn_mid();
    test_attr_mid();
    test_split();
    test_boundary_and_cross_joint();
    test_crease_and_pn();
    test_shared_edge_and_seam();
    test_head_body();
    return GE_TEST_END();
}
