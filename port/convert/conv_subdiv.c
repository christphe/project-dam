/*
 * conv_subdiv.c -- character-model subdivision (see conv_subdiv.h).
 *
 * Part 1 is pure mesh code: weld by rest-pose position, smooth normals,
 * crease / boundary / cross-joint edge classes, PN edge midpoints, 1 -> 4
 * triangle split. Part 2 rewrites an N64 model image: each character DL node
 * X becomes   LOD(near 0..D) -> X'(subdivided)   +   LOD(far D..) -> X.
 *
 * Why the rebuilt display lists load their own vertices: the data shows that
 * a GE model DL reads only vertex-cache slots it loaded itself, except a
 * Secondary DL (and the first-person hand) that continues its node's Primary.
 * So a rebuilt DL is self-contained and may re-batch freely; the only
 * addressable slots are 0..25 (G_TRI1 index byte / 10) and a G_VTX starts at
 * slot <= 15 (4-bit field), so a batch is "runs of consecutive array entries,
 * all but the longest start below 16, <= 26 in total".
 */
#include "conv_subdiv.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================== part 1 ==== */

void cv_sd_pn_mid(const float pi[3], const float ni[3], const float pj[3], const float nj[3], float out[3])
{
    float d[3], wij, wji;
    for (int k = 0; k < 3; k++) d[k] = pj[k] - pi[k];
    wij = d[0] * ni[0] + d[1] * ni[1] + d[2] * ni[2];
    wji = -(d[0] * nj[0] + d[1] * nj[1] + d[2] * nj[2]);
    for (int k = 0; k < 3; k++)
        out[k] = (pi[k] + pj[k]) * 0.5f - (wij * ni[k] + wji * nj[k]) * 0.125f;
}

void cv_sd_attr_mid(const CvSdVert *a, const CvSdVert *b, CvSdVert *out)
{
    for (int k = 0; k < 2; k++) out->st[k] = (int16_t)(((int)a->st[k] + (int)b->st[k] + 1) >> 1);
    for (int k = 0; k < 4; k++) out->cn[k] = (uint8_t)(((int)a->cn[k] + (int)b->cn[k] + 1) >> 1);
    out->flag = a->flag;
}

int cv_sd_split_tri(const int v[3], const int m[3], const float pv[3][3], const float pm[3][3], int out[4][3])
{
    int mask = (m[0] >= 0) | ((m[1] >= 0) << 1) | ((m[2] >= 0) << 2);
    int n = 0;
    switch (mask) {
    case 0:
        out[0][0] = v[0]; out[0][1] = v[1]; out[0][2] = v[2];
        return 1;
    case 7:
        out[0][0] = v[0]; out[0][1] = m[0]; out[0][2] = m[2];
        out[1][0] = m[0]; out[1][1] = v[1]; out[1][2] = m[1];
        out[2][0] = m[2]; out[2][1] = m[1]; out[2][2] = v[2];
        out[3][0] = m[0]; out[3][1] = m[1]; out[3][2] = m[2];
        return 4;
    case 1: case 2: case 4: {          /* one split edge e: a-b with midpoint */
        int e = mask == 1 ? 0 : mask == 2 ? 1 : 2;
        int a = v[e], b = v[(e + 1) % 3], c = v[(e + 2) % 3], mm = m[e];
        out[0][0] = a;  out[0][1] = mm; out[0][2] = c;
        out[1][0] = mm; out[1][1] = b;  out[1][2] = c;
        return 2;
    }
    default: {                          /* two split edges, edge u not split */
        int u = mask == 3 ? 2 : mask == 5 ? 1 : 0;
        int r = (u + 1) % 3, r1 = (r + 1) % 3, r2 = (r + 2) % 3;
        int a = v[r], b = v[r1], c = v[r2], mab = m[r], mbc = m[r1];
        float d1 = 0, d2 = 0;
        for (int k = 0; k < 3; k++) {
            float x = pv[r][k] - pm[r1][k], y = pm[r][k] - pv[r2][k];
            d1 += x * x; d2 += y * y;
        }
        out[n][0] = mab; out[n][1] = b; out[n][2] = mbc; n++;      /* corner at b */
        if (d1 <= d2) {
            out[n][0] = a;   out[n][1] = mab; out[n][2] = mbc; n++;
            out[n][0] = a;   out[n][1] = mbc; out[n][2] = c;   n++;
        } else {
            out[n][0] = a;   out[n][1] = mab; out[n][2] = c;   n++;
            out[n][0] = mab; out[n][1] = mbc; out[n][2] = c;   n++;
        }
        return n;
    }
    }
}

/* ---- run helpers ---- */
static int uf_find(int *p, int x)
{
    while (p[x] != x) { p[x] = p[p[x]]; x = p[x]; }
    return x;
}

static void vsub(const float *a, const float *b, float *o) { for (int k = 0; k < 3; k++) o[k] = a[k] - b[k]; }
static void vcross(const float *a, const float *b, float *o)
{
    o[0] = a[1] * b[2] - a[2] * b[1];
    o[1] = a[2] * b[0] - a[0] * b[2];
    o[2] = a[0] * b[1] - a[1] * b[0];
}
static float vdot(const float *a, const float *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
/* returns the old length; leaves a zero vector alone */
static float vnorm(float *a)
{
    float l = sqrtf(vdot(a, a));
    if (l > 1e-12f) { a[0] /= l; a[1] /= l; a[2] /= l; }
    return l;
}

typedef struct { int cx, cy, cz, layer, head; } Cell;
typedef struct { int lo, hi, tri; } EdgeEnt;

static int cmp_edge(const void *a, const void *b)
{
    const EdgeEnt *x = a, *y = b;
    if (x->lo != y->lo) return x->lo < y->lo ? -1 : 1;
    if (x->hi != y->hi) return x->hi < y->hi ? -1 : 1;
    return (x->tri > y->tri) - (x->tri < y->tri);
}

static unsigned cell_hash(int x, int y, int z, int l)
{
    unsigned h = 2166136261u;
    h = (h ^ (unsigned)x) * 16777619u;
    h = (h ^ (unsigned)y) * 16777619u;
    h = (h ^ (unsigned)z) * 16777619u;
    h = (h ^ (unsigned)l) * 16777619u;
    return h ^ (h >> 15);
}

static Cell *cell_find(Cell *tab, unsigned mask, int x, int y, int z, int l, int insert)
{
    unsigned i = cell_hash(x, y, z, l) & mask;
    for (;;) {
        Cell *c = &tab[i];
        if (c->head == -2) {
            if (!insert) return NULL;
            c->cx = x; c->cy = y; c->cz = z; c->layer = l; c->head = -1;
            return c;
        }
        if (c->cx == x && c->cy == y && c->cz == z && c->layer == l) return c;
        i = (i + 1) & mask;
    }
}

typedef struct { uint64_t key; int id; } MidEnt;

static int16_t round16(float x)
{
    long r = (long)floorf(x + 0.5f);
    if (r > 32767) r = 32767;
    if (r < -32768) r = -32768;
    return (int16_t)r;
}

void cv_sd_free_out(CvSdMesh *m)
{
    free(m->otri); free(m->osrc); free(m->mid_ab); free(m->ereason); free(m->mid_pn);
    m->otri = m->osrc = m->mid_ab = NULL; m->ereason = m->mid_pn = NULL;
    m->not_ = m->nmid = 0;
}

#define CV_SD_CREASE_COS 0.5f      /* dihedral above 60 degrees = crease */
#define CV_SD_WELD_DIST2 1.0f      /* rest positions closer than 1 unit weld */

int cv_sd_run(CvSdMesh *m)
{
    int nv = m->nv_in, nt = m->nt, ne = 3 * nt;
    int *uf = NULL, *cls = NULL, *emode = NULL, *nextv = NULL;
    float (*fn)[3] = NULL, (*un)[3] = NULL, (*acc)[3] = NULL;
    EdgeEnt *ee = NULL;
    Cell *tab = NULL;
    MidEnt *mt = NULL;
    unsigned tmask, mmask;
    int rc = -1, cap_out, cap_mid;
    unsigned char *thead = NULL;
    float crease_cos = CV_SD_CREASE_COS, bulge_clamp = CV_SD_DEFAULT_BULGE_CLAMP;

    cv_sd_free_out(m);
    if (m->params) {
        crease_cos = cosf(m->params->crease_deg * 0.017453292519943295f);
        bulge_clamp = m->params->bulge_clamp;
    }
    m->nv = nv; m->nflat = m->npn = 0;
    if (nt == 0) return 0;

    uf = malloc((size_t)nv * sizeof *uf);
    cls = malloc((size_t)nv * sizeof *cls);
    nextv = malloc((size_t)nv * sizeof *nextv);
    fn = calloc((size_t)nt, sizeof *fn);
    un = calloc((size_t)nt, sizeof *un);
    acc = calloc((size_t)nv, sizeof *acc);
    emode = calloc((size_t)ne, sizeof *emode);
    thead = calloc((size_t)nt, 1);
    m->ereason = calloc((size_t)ne, 1);
    ee = malloc((size_t)ne * sizeof *ee);
    {
        unsigned sz = 16;
        while (sz < (unsigned)nv * 2u + 16u) sz <<= 1;
        tmask = sz - 1;
        tab = malloc((size_t)sz * sizeof *tab);
        for (unsigned i = 0; i < sz; i++) tab[i].head = -2;
    }
    if (!uf || !cls || !nextv || !fn || !un || !acc || !emode || !ee || !tab || !thead || !m->ereason) goto done;
    for (int t = 0; t < nt; t++)
        thead[t] = (unsigned char)(!m->head_only || (m->v[m->tri[3 * t]].head && m->v[m->tri[3 * t + 1]].head && m->v[m->tri[3 * t + 2]].head));

    /* 1. weld by rest position, per layer */
    for (int i = 0; i < nv; i++) uf[i] = i;
    for (int i = 0; i < nv; i++) {
        const CvSdVert *a = &m->v[i];
        int cx = (int)floorf(a->rest[0]), cy = (int)floorf(a->rest[1]), cz = (int)floorf(a->rest[2]);
        for (int dx = -1; dx <= 1; dx++) for (int dy = -1; dy <= 1; dy++) for (int dz = -1; dz <= 1; dz++) {
            Cell *c = cell_find(tab, tmask, cx + dx, cy + dy, cz + dz, a->layer, 0);
            if (!c) continue;
            for (int j = c->head; j >= 0; j = nextv[j]) {
                float d[3];
                vsub(a->rest, m->v[j].rest, d);
                if (vdot(d, d) <= CV_SD_WELD_DIST2) {
                    int ra = uf_find(uf, i), rb = uf_find(uf, j);
                    if (ra != rb) uf[ra < rb ? rb : ra] = ra < rb ? ra : rb;
                }
            }
        }
        {
            Cell *c = cell_find(tab, tmask, cx, cy, cz, a->layer, 1);
            nextv[i] = c->head;
            c->head = i;
        }
    }
    for (int i = 0; i < nv; i++) cls[i] = uf_find(uf, i);

    /* 2. face normals (area weighted) and smooth class normals, rest space */
    for (int t = 0; t < nt; t++) {
        const int *v = &m->tri[3 * t];
        float e1[3], e2[3];
        vsub(m->v[v[1]].rest, m->v[v[0]].rest, e1);
        vsub(m->v[v[2]].rest, m->v[v[0]].rest, e2);
        vcross(e1, e2, fn[t]);
        memcpy(un[t], fn[t], sizeof un[t]);
        if (vnorm(un[t]) <= 1e-6f) memset(un[t], 0, sizeof un[t]);
        for (int c = 0; c < 3; c++)
            for (int k = 0; k < 3; k++) acc[cls[v[c]]][k] += fn[t][k];
    }
    for (int i = 0; i < nv; i++) if (cls[i] == i) vnorm(acc[i]);

    /* 3. edge classes: 0 = leave alone, 1 = flat midpoint, 2 = PN midpoint */
    for (int t = 0; t < nt; t++)
        for (int e = 0; e < 3; e++) {
            int a = cls[m->tri[3 * t + e]], b = cls[m->tri[3 * t + (e + 1) % 3]];
            ee[3 * t + e].lo = a < b ? a : b;
            ee[3 * t + e].hi = a < b ? b : a;
            ee[3 * t + e].tri = 3 * t + e;
        }
    qsort(ee, (size_t)ne, sizeof *ee, cmp_edge);
    for (int s = 0; s < ne;) {
        int f = s, ok = 1, mode, reason = 0;
        while (s < ne && ee[s].lo == ee[f].lo && ee[s].hi == ee[f].hi) s++;
        for (int k = f; k < s; k++) {
            int t = ee[k].tri / 3, e = ee[k].tri % 3;
            int va = m->tri[3 * t + e], vb = m->tri[3 * t + (e + 1) % 3];
            if (m->v[va].mtx < 0 || m->v[va].mtx != m->v[vb].mtx || va == vb) ok = 0;
            if (!thead[t]) ok = 0;      /* a body triangle on either side: the edge stays as it is */
        }
        if (ee[f].lo == ee[f].hi) ok = 0;
        if (!ok) { mode = 0; reason = CV_SD_E_JOINT; }
        else if (s - f != 2) { mode = 1; reason = CV_SD_E_BOUNDARY; }   /* boundary / non-manifold */
        else {
            int t1 = ee[f].tri / 3, t2 = ee[f + 1].tri / 3;
            if (t1 == t2 || vdot(un[t1], un[t1]) == 0.f || vdot(un[t2], un[t2]) == 0.f ||
                vdot(un[t1], un[t2]) < crease_cos) { mode = 1; reason = CV_SD_E_CREASE; }   /* crease */
            else mode = 2;
        }
        for (int k = f; k < s; k++) { emode[ee[k].tri] = mode; m->ereason[ee[k].tri] = (uint8_t)reason; }
    }

    /* 4. midpoints + split */
    cap_mid = nt * 3 + 16;
    m->v = realloc(m->v, (size_t)(nv + cap_mid) * sizeof *m->v);
    m->mid_ab = malloc((size_t)cap_mid * 2 * sizeof(int));
    m->mid_pn = calloc((size_t)cap_mid, 1);
    cap_out = nt * 4;
    m->otri = malloc((size_t)cap_out * 3 * sizeof(int));
    m->osrc = malloc((size_t)cap_out * sizeof(int));
    {
        unsigned sz = 16;
        while (sz < (unsigned)cap_mid * 2u) sz <<= 1;
        mmask = sz - 1;
        mt = malloc((size_t)sz * sizeof *mt);
        for (unsigned i = 0; i < sz; i++) mt[i].id = -1;
    }
    if (!m->v || !m->mid_ab || !m->mid_pn || !m->otri || !m->osrc || !mt) goto done;

    for (int t = 0; t < nt; t++) {
        int v[3], mid[3], o[4][3], n;
        float pv[3][3], pm[3][3];
        for (int c = 0; c < 3; c++) {
            v[c] = m->tri[3 * t + c];
            for (int k = 0; k < 3; k++) pv[c][k] = (float)m->v[v[c]].pos[k];
        }
        for (int e = 0; e < 3; e++) {
            int a = v[e], b = v[(e + 1) % 3], lo = a < b ? a : b, hi = a < b ? b : a, id;
            uint64_t key = ((uint64_t)(unsigned)lo << 32) | (unsigned)hi;
            unsigned h;
            mid[e] = -1;
            for (int k = 0; k < 3; k++) pm[e][k] = 0;
            if (emode[3 * t + e] == 0) continue;
            h = (unsigned)((key * 0x9E3779B97F4A7C15ull) >> 40) & mmask;
            while (mt[h].id >= 0 && mt[h].key != key) h = (h + 1) & mmask;
            if (mt[h].id < 0) {
                CvSdVert *nvx = &m->v[m->nv];
                const CvSdVert *A = &m->v[lo], *B = &m->v[hi];
                float pa[3], pb[3], na[3], nb[3], p[3];
                int pn = emode[3 * t + e] == 2;
                for (int k = 0; k < 3; k++) { pa[k] = (float)A->pos[k]; pb[k] = (float)B->pos[k]; }
                memcpy(na, acc[cls[lo]], sizeof na);
                memcpy(nb, acc[cls[hi]], sizeof nb);
                if (pn && (vdot(na, na) < 0.5f || vdot(nb, nb) < 0.5f)) pn = 0;
                if (pn) {
                    float ch[3], d[3], len, dl;
                    cv_sd_pn_mid(pa, na, pb, nb, p);
                    vsub(pb, pa, ch);
                    for (int k = 0; k < 3; k++) d[k] = p[k] - (pa[k] + pb[k]) * 0.5f;
                    len = sqrtf(vdot(ch, ch));
                    dl = sqrtf(vdot(d, d));
                    if (dl > bulge_clamp * len) pn = 0;      /* sanity: a PN bulge is <= ~0.11 of the edge */
                }
                if (!pn) for (int k = 0; k < 3; k++) p[k] = (pa[k] + pb[k]) * 0.5f;
                memset(nvx, 0, sizeof *nvx);
                for (int k = 0; k < 3; k++) nvx->pos[k] = round16(p[k]);
                cv_sd_attr_mid(A, B, nvx);
                nvx->mtx = A->mtx; nvx->layer = A->layer;
                for (int k = 0; k < 3; k++) nvx->rest[k] = (A->rest[k] + B->rest[k]) * 0.5f;
                m->mid_ab[2 * m->nmid] = lo; m->mid_ab[2 * m->nmid + 1] = hi;
                m->mid_pn[m->nmid] = (uint8_t)pn;
                if (pn) m->npn++; else m->nflat++;
                mt[h].key = key; mt[h].id = m->nv;
                m->nv++; m->nmid++;
            }
            id = mt[h].id;
            mid[e] = id;
            for (int k = 0; k < 3; k++) pm[e][k] = (float)m->v[id].pos[k];
        }
        n = cv_sd_split_tri(v, mid, pv, pm, o);
        for (int k = 0; k < n; k++) {
            memcpy(&m->otri[3 * m->not_], o[k], 3 * sizeof(int));
            m->osrc[m->not_++] = t;
        }
    }
    rc = 0;
done:
    free(thead);
    free(uf); free(cls); free(nextv); free(fn); free(un); free(acc); free(emode); free(ee); free(tab); free(mt);
    if (rc) cv_sd_free_out(m);
    return rc;
}

/* ============================================================== part 2 ==== */

typedef struct { uint8_t *p; uint32_t len, cap; } Img;

static uint32_t rb32(const uint8_t *b, uint32_t o) { return ((uint32_t)b[o] << 24) | ((uint32_t)b[o + 1] << 16) | ((uint32_t)b[o + 2] << 8) | b[o + 3]; }
static uint32_t rb16(const uint8_t *b, uint32_t o) { return ((uint32_t)b[o] << 8) | b[o + 1]; }
static void wb32(uint8_t *b, uint32_t o, uint32_t v) { b[o] = (uint8_t)(v >> 24); b[o + 1] = (uint8_t)(v >> 16); b[o + 2] = (uint8_t)(v >> 8); b[o + 3] = (uint8_t)v; }
static void wb16(uint8_t *b, uint32_t o, uint32_t v) { b[o] = (uint8_t)(v >> 8); b[o + 1] = (uint8_t)v; }
static void wbf(uint8_t *b, uint32_t o, float f) { uint32_t u; memcpy(&u, &f, 4); wb32(b, o, u); }
static float rbf(const uint8_t *b, uint32_t o) { uint32_t u = rb32(b, o); float f; memcpy(&f, &u, 4); return f; }
#define SEG5(o) (0x05000000u | (uint32_t)(o))
#define OFF(w) ((w) & 0xFFFFFFu)

/* appends n zero bytes, returns their offset; -1 on OOM */
static int64_t img_alloc(Img *im, uint32_t n)
{
    uint32_t off = im->len;
    if (im->len + n > im->cap) {
        uint32_t cap = im->cap ? im->cap : 4096;
        uint8_t *p;
        while (cap < im->len + n) cap *= 2;
        p = realloc(im->p, cap);
        if (!p) return -1;
        im->p = p; im->cap = cap;
    }
    memset(im->p + off, 0, n);
    im->len += n;
    return off;
}

#define NODE_SZ 0x18u

typedef struct { uint32_t o, op, data; } NInfo;

/* ---- display-list parse ---- */
typedef struct { int kind; uint32_t w0, w1; int t0, t1; } Elem;   /* kind 0 = pass-through, 1 = triangle run */
typedef struct {
    Elem *e; int ne, cape;
    int *tv; int nt, capt;       /* 3 vertex indices per triangle */
    int end_mtx;                 /* matrix at the DL end, -1 none */
    int ok;
} DlP;

typedef struct {
    int slot[32];
    int mtx;
    int *vm;                     /* per vertex: matrix it was loaded under, -1 */
    int conflict;
} Sim;

static int dl_add_elem(DlP *d, int kind, uint32_t w0, uint32_t w1)
{
    if (d->ne == d->cape) {
        d->cape = d->cape ? d->cape * 2 : 64;
        d->e = realloc(d->e, (size_t)d->cape * sizeof *d->e);
        if (!d->e) return -1;
    }
    d->e[d->ne].kind = kind; d->e[d->ne].w0 = w0; d->e[d->ne].w1 = w1;
    d->e[d->ne].t0 = d->e[d->ne].t1 = d->nt;
    d->ne++;
    return 0;
}

static int dl_add_tri(DlP *d, int a, int b, int c)
{
    if (d->nt == d->capt) {
        d->capt = d->capt ? d->capt * 2 : 256;
        d->tv = realloc(d->tv, (size_t)d->capt * 3 * sizeof(int));
        if (!d->tv) return -1;
    }
    d->tv[3 * d->nt] = a; d->tv[3 * d->nt + 1] = b; d->tv[3 * d->nt + 2] = c;
    d->nt++;
    return 0;
}

static void dlp_free(DlP *d) { free(d->e); free(d->tv); memset(d, 0, sizeof *d); }

/* Parses the DL at `start`. op 24: G_VTX seg 4, offsets relative to the node's
 * array; op 4: seg 5, absolute offsets (array at vo). Returns 0 or -1. */
static int parse_dl(const uint8_t *src, uint32_t D, uint32_t start, int op, uint32_t vo, int nv, Sim *sm, DlP *d)
{
    uint32_t p = start;
    memset(d, 0, sizeof *d);
    d->end_mtx = -1;
    for (;;) {
        uint32_t w0, w1, c;
        if (p + 8 > D) return -1;
        w0 = rb32(src, p); w1 = rb32(src, p + 4); c = w0 >> 24;
        p += 8;
        if (c == 0xB8) break;
        if (c == 0x01) {
            if (w0 != 0x01020040u || (w1 >> 24) != 3 || (w1 & 0x3F)) return -1;
            sm->mtx = (int)(OFF(w1) >> 6);
        } else if (c == 0x04) {
            int v0 = (int)((w0 >> 16) & 0xF), n = (int)((w0 & 0xFFFF) >> 4);
            int64_t first;
            if ((w0 & 0xF) || n < 1 || (((n - 1) & 0xF) != (int)((w0 >> 20) & 0xF)) || v0 + n > 32 || sm->mtx < 0) return -1;
            if (op == 24) {
                if ((w1 >> 24) != 4 || (OFF(w1) & 15)) return -1;
                first = OFF(w1) / 16;
            } else {
                if ((w1 >> 24) != 5 || OFF(w1) < vo || ((OFF(w1) - vo) & 15)) return -1;
                first = (OFF(w1) - vo) / 16;
            }
            for (int i = 0; i < n; i++) {
                int64_t vi = first + i;
                if (vi >= nv) return -1;
                sm->slot[v0 + i] = (int)vi;
                if (sm->vm[vi] >= 0 && sm->vm[vi] != sm->mtx) sm->conflict = 1;
                sm->vm[vi] = sm->mtx;
            }
        } else if (c == 0xBF || c == 0xB1) {
            int t[4][3], nt = 0;
            if (c == 0xBF) {
                for (int k = 0; k < 3; k++) {
                    uint32_t b = (w1 >> (16 - 8 * k)) & 0xFF;
                    if (b % 10) return -1;
                    t[0][k] = (int)(b / 10);
                }
                nt = 1;
            } else {
                for (int k = 0; k < 4; k++) {
                    int x = (int)((w1 >> (8 * k)) & 0xF), y = (int)((w1 >> (8 * k + 4)) & 0xF), z = (int)((w0 >> (4 * k)) & 0xF);
                    if (x || y || z) { t[nt][0] = x; t[nt][1] = y; t[nt][2] = z; nt++; }
                }
            }
            if (d->ne == 0 || d->e[d->ne - 1].kind != 1) { if (dl_add_elem(d, 1, 0, 0)) return -1; }
            for (int k = 0; k < nt; k++) {
                int vv[3];
                for (int q = 0; q < 3; q++) {
                    if (t[k][q] >= 32 || sm->slot[t[k][q]] < 0) return -1;
                    vv[q] = sm->slot[t[k][q]];
                }
                if (dl_add_tri(d, vv[0], vv[1], vv[2])) return -1;
                d->e[d->ne - 1].t1 = d->nt;
            }
            continue;
        } else if (c == 0x05 || c == 0x06 || c == 0xB0 || c == 0xB2 || c == 0xB3 || c == 0xB4 || c == 0xB5) {
            return -1;                                  /* geometry commands this pass does not know */
        } else {
            if (dl_add_elem(d, 0, w0, w1)) return -1;
            continue;
        }
        /* MTX / VTX: part of the current triangle run (dropped, rebuilt later) */
        if (d->ne == 0 || d->e[d->ne - 1].kind != 1) { if (dl_add_elem(d, 1, 0, 0)) return -1; }
    }
    d->end_mtx = sm->mtx;
    /* drop empty runs */
    {
        int o = 0;
        for (int i = 0; i < d->ne; i++)
            if (d->e[i].kind == 0 || d->e[i].t1 > d->e[i].t0) d->e[o++] = d->e[i];
        d->ne = o;
    }
    d->ok = 1;
    return 0;
}

/* ---- DL emission ---- */
typedef struct { uint32_t *w; int n, cap; } Cmds;

static int cmd_put(Cmds *c, uint32_t w0, uint32_t w1)
{
    if (c->n == c->cap) {
        c->cap = c->cap ? c->cap * 2 : 128;
        c->w = realloc(c->w, (size_t)c->cap * 2 * sizeof(uint32_t));
        if (!c->w) return -1;
    }
    c->w[2 * c->n] = w0; c->w[2 * c->n + 1] = w1; c->n++;
    return 0;
}

typedef struct { int mtx, vidx; } Inst;
typedef struct { int mtx, start, len; } Run;

static int cmp_inst(const void *a, const void *b)
{
    const Inst *x = a, *y = b;
    if (x->mtx != y->mtx) return x->mtx < y->mtx ? -1 : 1;
    return (x->vidx > y->vidx) - (x->vidx < y->vidx);
}

#define MAX_SLOT_USED 26
#define MAX_START 15

/* Builds the runs of a (sorted) instance set; returns total length, or -1 when infeasible. */
static int plan_runs(const Inst *set, int n, Run *runs, int *nruns)
{
    int nr = 0, longest = 0;
    for (int i = 0; i < n; i++) {
        if (nr && runs[nr - 1].mtx == set[i].mtx && runs[nr - 1].start + runs[nr - 1].len == set[i].vidx) {
            runs[nr - 1].len++;
        } else {
            runs[nr].mtx = set[i].mtx; runs[nr].start = set[i].vidx; runs[nr].len = 1; nr++;
        }
    }
    for (int i = 0; i < nr; i++) if (runs[i].len > longest) longest = runs[i].len;
    *nruns = nr;
    if (n > MAX_SLOT_USED || n - longest > MAX_START) return -1;
    return n;
}

typedef struct { uint32_t vbase; int op; } EmitCtx;   /* vbase: absolute offset of the array (op 4) */

static int emit_group(Cmds *out, const Inst *set_in, int nset, const int (*tris)[3], int ntris, const EmitCtx *ec, int *cur_mtx, int *maxslot)
{
    Inst set[MAX_SLOT_USED + 3];
    Run runs[MAX_SLOT_USED + 3];
    int order[MAX_SLOT_USED + 3], nr = 0, slot_of_run[MAX_SLOT_USED + 3], slot = 0;
    int longest = 0;
    memcpy(set, set_in, (size_t)nset * sizeof(Inst));
    qsort(set, (size_t)nset, sizeof(Inst), cmp_inst);
    if (plan_runs(set, nset, runs, &nr) < 0) return -1;
    for (int i = 0; i < nr; i++) { order[i] = i; if (runs[i].len > runs[longest].len) longest = i; }
    /* longest last (its start must be <= 15); of the rest, the runs under the matrix
     * already loaded go first, so a group does not reload it (fewer G_MTX per DL) */
    {
        int o = 0;
        for (int i = 0; i < nr; i++) if (i != longest && runs[i].mtx == *cur_mtx) order[o++] = i;
        for (int i = 0; i < nr; i++) if (i != longest && runs[i].mtx != *cur_mtx) order[o++] = i;
        order[o++] = longest;
    }
    for (int k = 0; k < nr; k++) {
        const Run *r = &runs[order[k]];
        uint32_t w1;
        if (slot > MAX_START) return -1;
        if (*cur_mtx != r->mtx) {
            if (cmd_put(out, 0x01020040u, 0x03000000u | ((uint32_t)r->mtx << 6))) return -1;
            *cur_mtx = r->mtx;
        }
        w1 = ec->op == 24 ? (0x04000000u | ((uint32_t)r->start << 4)) : SEG5(ec->vbase + ((uint32_t)r->start << 4));
        if (cmd_put(out, 0x04000000u | ((uint32_t)((r->len - 1) & 0xF) << 20) | ((uint32_t)slot << 16) | (uint32_t)(16 * r->len), w1)) return -1;
        slot_of_run[order[k]] = slot;
        slot += r->len;
    }
    if (slot - 1 > *maxslot) *maxslot = slot - 1;
    {
        int pend[4][3], np = 0;
#define SLOT_OF(inst_mtx, inst_v, res) do { \
            res = -1; \
            for (int ri = 0; ri < nr && res < 0; ri++) \
                if (runs[ri].mtx == (inst_mtx) && (inst_v) >= runs[ri].start && (inst_v) < runs[ri].start + runs[ri].len) \
                    res = slot_of_run[ri] + ((inst_v) - runs[ri].start); \
        } while (0)
        for (int t = 0; t <= ntris; t++) {
            int s[3] = {0, 0, 0}, big = 0;
            if (t < ntris) {
                for (int c = 0; c < 3; c++) {
                    int mv = tris[t][c] >> 20, vi = tris[t][c] & 0xFFFFF, r;
                    SLOT_OF(mv, vi, r);
                    if (r < 0) return -1;
                    s[c] = r;
                    if (r > 15) big = 1;
                }
            }
            if (t == ntris || big || np == 4) {
                if (np) {
                    uint32_t w0 = 0xB1000000u, w1 = 0;
                    for (int k = 0; k < np; k++) {
                        w1 |= ((uint32_t)pend[k][0] | ((uint32_t)pend[k][1] << 4)) << (8 * k);
                        w0 |= (uint32_t)pend[k][2] << (4 * k);
                    }
                    if (cmd_put(out, w0, w1)) return -1;
                    np = 0;
                }
            }
            if (t == ntris) break;
            if (big) {
                if (cmd_put(out, 0xBF000000u, ((uint32_t)(s[0] * 10) << 16) | ((uint32_t)(s[1] * 10) << 8) | (uint32_t)(s[2] * 10))) return -1;
            } else {
                pend[np][0] = s[0]; pend[np][1] = s[1]; pend[np][2] = s[2]; np++;
            }
        }
#undef SLOT_OF
    }
    return 0;
}

/* tris carry (mtx << 20 | vidx) per corner. Rebuilds one DL. */
static int rebuild_dl(Cmds *out, const DlP *src_dl, const int *otri, const int *ostart, const int *ocount, int tbase,
                      const int *vmtx, const int *vlocal, const EmitCtx *ec, int *maxslot, int *end_mtx_out)
{
    int cur_mtx = -1;
    for (int ei = 0; ei < src_dl->ne; ei++) {
        const Elem *el = &src_dl->e[ei];
        if (el->kind == 0) { if (cmd_put(out, el->w0, el->w1)) return -1; continue; }
        {
            int (*tl)[3] = NULL, ntl = 0, cap = 0;
            Inst set[MAX_SLOT_USED + 3];
            int nset = 0, start = 0;
            for (int t = el->t0; t < el->t1; t++) {
                for (int k = 0; k < ocount[tbase + t]; k++) {
                    int o = ostart[tbase + t] + k;
                    if (ntl == cap) {
                        cap = cap ? cap * 2 : 64;
                        tl = realloc(tl, (size_t)cap * sizeof *tl);
                        if (!tl) return -1;
                    }
                    for (int c = 0; c < 3; c++) {
                        int g = otri[3 * o + c];
                        tl[ntl][c] = (vmtx[g] << 20) | vlocal[g];
                    }
                    ntl++;
                }
            }
            /* greedy grouping in order */
            for (int t = 0; t < ntl; t++) {
                Inst ns[MAX_SLOT_USED + 3];
                Run tmp[MAX_SLOT_USED + 3];
                int nn = nset, nr;
                memcpy(ns, set, (size_t)nset * sizeof(Inst));
                for (int c = 0; c < 3; c++) {
                    Inst in; int dup = 0;
                    in.mtx = tl[t][c] >> 20; in.vidx = tl[t][c] & 0xFFFFF;
                    for (int q = 0; q < nn; q++) if (ns[q].mtx == in.mtx && ns[q].vidx == in.vidx) dup = 1;
                    if (!dup) { if (nn >= MAX_SLOT_USED) { nn = MAX_SLOT_USED + 1; break; } ns[nn++] = in; }
                }
                {
                    Inst srt[MAX_SLOT_USED + 3];
                    int feas = 0;
                    if (nn <= MAX_SLOT_USED) {
                        memcpy(srt, ns, (size_t)nn * sizeof(Inst));
                        qsort(srt, (size_t)nn, sizeof(Inst), cmp_inst);
                        feas = plan_runs(srt, nn, tmp, &nr) >= 0;
                    }
                    if (feas) { memcpy(set, ns, (size_t)nn * sizeof(Inst)); nset = nn; continue; }
                }
                /* flush [start, t) and begin a new group with t */
                if (t == start) { free(tl); return -1; }
                if (emit_group(out, set, nset, (const int (*)[3])&tl[start], t - start, ec, &cur_mtx, maxslot)) { free(tl); return -1; }
                start = t; nset = 0;
                for (int c = 0; c < 3; c++) {
                    Inst in; int dup = 0;
                    in.mtx = tl[t][c] >> 20; in.vidx = tl[t][c] & 0xFFFFF;
                    for (int q = 0; q < nset; q++) if (set[q].mtx == in.mtx && set[q].vidx == in.vidx) dup = 1;
                    if (!dup) set[nset++] = in;
                }
            }
            if (ntl > start && emit_group(out, set, nset, (const int (*)[3])&tl[start], ntl - start, ec, &cur_mtx, maxslot)) { free(tl); return -1; }
            free(tl);
        }
    }
    if (src_dl->end_mtx >= 0 && cur_mtx != src_dl->end_mtx) {
        if (cmd_put(out, 0x01020040u, 0x03000000u | ((uint32_t)src_dl->end_mtx << 6))) return -1;
    }
    if (end_mtx_out) *end_mtx_out = cur_mtx;
    return cmd_put(out, 0xB8000000u, 0);
}

/* ---- model level ---- */
typedef struct {
    uint32_t o, op, data;
    uint32_t vo; int nv;
    uint32_t prim, sec;
    DlP dl[2];
    int ndl;
    int *vm;
    int layer;
    float encl_max;
    int gbase, tbase[2];
    int hd;                      /* head part: has a triangle whose corners are all under head matrices */
    int collapse;                /* body part under a game LOD pair: keep X, drop the simplified DL */
    int drop;                    /* simplified far-LOD mesh that the tier shift removes */
    uint32_t lf;                 /* tier shift: the far LOD node that takes X back (0 = wrap instead) */
} DNode;

static uint32_t fld(const Img *im, uint32_t o, uint32_t f) { return OFF(rb32(im->p, o + f)); }

/* N64 offsets of node pointers in a node's record, by opcode */
static int rec_node_fields(int op, uint32_t *f)
{
    switch (op) {
    case 1: f[0] = 4; return 1;
    case 2: case 3: f[0] = 0x14; return 1;
    case 8: f[0] = 8; return 1;
    case 9: f[0] = 0x18; f[1] = 0x1C; return 2;
    case 12: f[0] = 0x18; return 1;
    case 13: f[0] = 0x10; f[1] = 0x14; return 2;
    case 18: f[0] = 0; return 1;
    }
    return 0;
}

/* Re-parses a rebuilt DL and checks it draws exactly the expected triangles,
 * each corner under the expected matrix. */
static int verify_dl(const Cmds *c, int op, uint32_t vbase, int nvtot, const int *vmtx_local,
                     const int (*want)[3], int nexp, Sim *sm, int *maxslot)
{
    uint8_t *b = malloc((size_t)c->n * 8 + 8);
    DlP d;
    int rc = -1;
    if (!b) return -1;
    memset(&d, 0, sizeof d);
    for (int i = 0; i < c->n; i++) {
        wb32(b, 8 * (uint32_t)i, c->w[2 * i]);
        wb32(b, 8 * (uint32_t)i + 4, c->w[2 * i + 1]);
    }
    if (parse_dl(b, (uint32_t)c->n * 8, 0, op, vbase, nvtot, sm, &d) == 0 && !sm->conflict) {
        int n = 0, ok = 1;
        for (int e = 0; e < d.ne && ok; e++)
            if (d.e[e].kind == 1)
                for (int t = d.e[e].t0; t < d.e[e].t1 && ok; t++, n++) {
                    if (n >= nexp) { ok = 0; break; }
                    for (int k = 0; k < 3; k++) {
                        int vi = d.tv[3 * t + k];
                        if (vi != (want[n][k] & 0xFFFFF) || sm->vm[vi] != (want[n][k] >> 20) || vmtx_local[vi] != sm->vm[vi]) ok = 0;
                    }
                }
        if (ok && n == nexp) rc = 0;
    }
    for (int i = 0; i < c->n; i++) {
        uint32_t w0 = c->w[2 * i];
        if ((w0 >> 24) == 4) {
            int s = (int)((w0 >> 16) & 0xF) + (int)((w0 & 0xFFFF) / 16) - 1;
            if (s > *maxslot) *maxslot = s;
        }
    }
    dlp_free(&d);
    free(b);
    return rc;
}

int cv_subdiv_model(const uint8_t *src, uint32_t D, int NS, int NT, int head_file, uint8_t **out, uint32_t *outlen, CvSdStats *st)
{
    uint8_t *hmat = NULL;          /* matrices of the head joint (JointID 3) of a body with an integrated head */
    CvSdParams hp;
    uint32_t R0 = 4u * (uint32_t)NS + 12u * (uint32_t)NT;
    NInfo *nodes = NULL; int nn = 0, capn = 0;
    int32_t *nidx = NULL;
    uint32_t *stack = NULL; int sp = 0, capst = 0;
    float (*mat_off)[3] = NULL; uint8_t *mat_ok = NULL;
    DNode *dn = NULL; int nd = 0;
    CvSdMesh mesh;
    uint32_t *xnew = NULL; int *pcol = NULL, *pncv = NULL;
    int *vnode = NULL, *vlocal = NULL, *vmtx = NULL, *ostart = NULL, *ocount = NULL, *nmidc = NULL;
    Img im;
    int rc = -1, ndone = 0;
    CvSdStats loc;
    enum { MAXMAT = 1024 };

    memset(&mesh, 0, sizeof mesh);
    memset(&im, 0, sizeof im);
    memset(&loc, 0, sizeof loc);
    if (!st) st = &loc;
    st->models++;
    *out = NULL; *outlen = 0;
    if (D < R0 + NODE_SZ || D > 0x1000000u) return 0;

    nidx = malloc((size_t)(D + 1) * sizeof *nidx);
    mat_off = calloc((size_t)MAXMAT, sizeof *mat_off);
    mat_ok = calloc((size_t)MAXMAT, 1);
    stack = malloc(64 * sizeof *stack); capst = 64;
    if (!nidx || !mat_off || !mat_ok || !stack) goto done;
    memset(nidx, 0xFF, (size_t)(D + 1) * sizeof *nidx);

    /* 1. node walk */
    stack[sp++] = R0;
    while (sp) {
        uint32_t o = stack[--sp], data, ch;
        int op;
        if (o == 0 || o + NODE_SZ > D || nidx[o] >= 0) continue;
        op = (int)(rb16(src, o) & 0xFF);
        data = OFF(rb32(src, o + 4));
        if (data + 4 > D) continue;
        if (nn == capn) {
            capn = capn ? capn * 2 : 256;
            nodes = realloc(nodes, (size_t)capn * sizeof *nodes);
            if (!nodes) goto done;
        }
        nodes[nn].o = o; nodes[nn].op = (uint32_t)op; nodes[nn].data = data;
        nidx[o] = nn++;
        ch = OFF(rb32(src, o + 0x14));
        if (op == 8 && data + 12 <= D) ch = OFF(rb32(src, data + 8));
        else if (op == 18) ch = OFF(rb32(src, data));
        if (sp + 2 > capst) {
            capst *= 2;
            stack = realloc(stack, (size_t)capst * sizeof *stack);
            if (!stack) goto done;
        }
        if (ch) stack[sp++] = ch;
        if (OFF(rb32(src, o + 0xC))) stack[sp++] = OFF(rb32(src, o + 0xC));
    }

    /* 2. matrix index -> rest-pose offset of its joint (sum of group origins up the tree) */
    for (int i = 0; i < nn; i++) {
        float acc[3] = {0, 0, 0};
        uint32_t o = nodes[i].o;
        int depth = 0;
        if (nodes[i].op != 2 || nodes[i].data + 0x14 > D) continue;
        while (o && depth++ < 64) {
            int j = o < D ? nidx[o] : -1;
            if (j < 0) break;
            if (nodes[j].op == 2 && nodes[j].data + 12 <= D)
                for (int k = 0; k < 3; k++) acc[k] += rbf(src, nodes[j].data + 4 * (uint32_t)k);
            o = OFF(rb32(src, o + 8));
        }
        for (int q = 0; q < 3; q++) {
            int id = (int16_t)rb16(src, nodes[i].data + 0xE + 2 * (uint32_t)q);
            if (id >= 0 && id < MAXMAT && !mat_ok[id]) { memcpy(mat_off[id], acc, sizeof acc); mat_ok[id] = 1; }
        }
    }

    /* head region. A Chead* file is all head. A body whose switch table has no head attachment
     * (Switches[4] == NULL: the files with hasHead == 1 in c_item_entries) has the head in the
     * body: the head joint is the group with JointID 3 (SKEL_NECK, the parent of the head
     * attachment op 23 in every other body, all sharing skeleton `guard`), plus any joint below it. */
    hmat = calloc((size_t)MAXMAT, 1);
    if (!hmat) goto done;
    if (!head_file && NS > 4 && OFF(rb32(src, 16)) == 0) {
        for (int i = 0; i < nn; i++) {
            if (nodes[i].op != 2 || nodes[i].data + 0x14 > D || rb16(src, nodes[i].data + 0xC) != 3) continue;
            for (int q = 0; q < 3; q++) {
                int id = (int16_t)rb16(src, nodes[i].data + 0xE + 2 * (uint32_t)q);
                if (id >= 0 && id < MAXMAT) hmat[id] = 1;
            }
            for (int j = 0; j < nn; j++) {            /* joints below it */
                uint32_t a = OFF(rb32(src, nodes[j].o + 8));
                int depth = 0;
                if (nodes[j].op != 2 || nodes[j].data + 0x14 > D) continue;
                while (a && depth++ < 64) {
                    if (a == nodes[i].o) {
                        for (int q = 0; q < 3; q++) {
                            int id = (int16_t)rb16(src, nodes[j].data + 0xE + 2 * (uint32_t)q);
                            if (id >= 0 && id < MAXMAT) hmat[id] = 1;
                        }
                        break;
                    }
                    a = a < D ? OFF(rb32(src, a + 8)) : 0;
                }
            }
        }
    }

    /* 3. DL nodes: parse (Primary, then the Secondary that continues it) */
    dn = calloc((size_t)nn + 1, sizeof *dn);
    if (!dn) goto done;
    for (int i = 0; i < nn; i++) {
        DNode *x;
        uint32_t d = nodes[i].data;
        Sim sm;
        if (nodes[i].op != 4 && nodes[i].op != 24) continue;
        st->nodes++;
        if (d + 0x14 > D || (nodes[i].op == 24 && d + 0x20 > D)) { st->skipped++; continue; }
        x = &dn[nd];
        memset(x, 0, sizeof *x);
        x->o = nodes[i].o; x->op = nodes[i].op; x->data = d;
        x->prim = OFF(rb32(src, d)); x->sec = OFF(rb32(src, d + 4));
        if (x->op == 24) { x->vo = OFF(rb32(src, d + 8)); x->nv = (int16_t)rb16(src, d + 0xC); }
        else { x->vo = OFF(rb32(src, d + 0xC)); x->nv = (int)rb16(src, d + 0x10); }
        if (!x->prim || !x->vo || x->nv <= 0 || x->vo + 16u * (uint32_t)x->nv > D || x->prim >= D) { st->skipped++; continue; }
        x->vm = malloc((size_t)x->nv * sizeof(int));
        if (!x->vm) goto done;
        for (int k = 0; k < x->nv; k++) x->vm[k] = -1;
        memset(&sm, 0, sizeof sm);
        for (int k = 0; k < 32; k++) sm.slot[k] = -1;
        sm.mtx = -1; sm.vm = x->vm;
        if (parse_dl(src, D, x->prim, (int)x->op, x->vo, x->nv, &sm, &x->dl[0]) || sm.conflict) {
            dlp_free(&x->dl[0]); free(x->vm); x->vm = NULL; st->skipped++; continue;
        }
        x->ndl = 1;
        if (x->sec) {
            if (parse_dl(src, D, x->sec, (int)x->op, x->vo, x->nv, &sm, &x->dl[1]) || sm.conflict) {
                dlp_free(&x->dl[0]); dlp_free(&x->dl[1]); free(x->vm); x->vm = NULL; st->skipped++; continue;
            }
            x->ndl = 2;
        }
        /* LOD layer: 0 none, 1 under a Min==0 game LOD, 2 under a farther one */
        x->encl_max = 1e30f;
        {
            uint32_t a = OFF(rb32(src, x->o + 8));
            int depth = 0;
            while (a && depth++ < 64) {
                int j = a < D ? nidx[a] : -1;
                if (j < 0) break;
                if (nodes[j].op == 8 && nodes[j].data + 8 <= D) {
                    x->layer = rbf(src, nodes[j].data) == 0.0f ? 1 : 2;
                    x->encl_max = rbf(src, nodes[j].data + 4);
                    break;
                }
                a = OFF(rb32(src, a + 8));
            }
        }
        /* a node is a head part when some triangle has all three corners under head matrices */
        {
            const DlP *dp0 = &x->dl[0];
            for (int k = 0; k < x->ndl && !x->hd; k++) {
                const DlP *dp = k ? &x->dl[1] : dp0;
                for (int t = 0; t < dp->nt && !x->hd; t++) {
                    int all = 1;
                    for (int c = 0; c < 3; c++) {
                        int m = x->vm[dp->tv[3 * t + c]];
                        if (!(head_file || (m >= 0 && m < MAXMAT && hmat[m]))) all = 0;
                    }
                    if (all) x->hd = 1;
                }
            }
            if (x->hd) st->head_nodes++; else st->body_nodes++;
        }
        nd++;
    }
    if (nd == 0) { rc = 0; goto done; }

    /* 3b. LOD tier shift. A part with a game LOD pair (near 0..M detailed, far M.. simplified,
     * one DL each) draws the subdivided mesh near and the ORIGINAL detailed mesh far; the
     * simplified mesh is dropped (unreachable, so the converter does not emit it). Only when
     * every collision link stays coherent; otherwise the part is wrapped as below. */
    {
        int32_t *dnidx = malloc((size_t)(D + 1) * sizeof *dnidx);
        int *cy = calloc((size_t)nd + 1, sizeof(int));       /* candidate: index of its far DL, else -1 */
        int *yown = malloc((size_t)(D + 1) * sizeof *yown);
        int changed;
        if (!dnidx || !cy || !yown) { free(dnidx); free(cy); free(yown); goto done; }
        memset(dnidx, 0xFF, (size_t)(D + 1) * sizeof *dnidx);
        for (int i = 0; i < nd; i++) dnidx[dn[i].o] = i;
        for (int i = 0; i < nd; i++) {
            const DNode *x = &dn[i];
            uint32_t ln = OFF(rb32(src, x->o + 8)), lf, y, pp;
            int jl, jf, yi;
            cy[i] = -1;
            if (ln == 0 || ln >= D || (jl = nidx[ln]) < 0 || nodes[jl].op != 8 || nodes[jl].data + 12 > D) continue;
            if (rbf(src, nodes[jl].data) != 0.0f || OFF(rb32(src, x->o + 0xC)) || OFF(rb32(src, x->o + 0x10))) continue;
            if (OFF(rb32(src, ln + 0x14)) != x->o || OFF(rb32(src, ln + 0x10))) continue;
            lf = OFF(rb32(src, ln + 0xC));
            if (lf == 0 || lf >= D || (jf = nidx[lf]) < 0 || nodes[jf].op != 8 || nodes[jf].data + 12 > D) continue;
            pp = OFF(rb32(src, ln + 8));
            if (OFF(rb32(src, lf + 8)) != pp || OFF(rb32(src, lf + 0x10)) != ln || OFF(rb32(src, lf + 0xC))) continue;
            if (pp && pp < D && nidx[pp] >= 0 && nodes[nidx[pp]].op == 9) continue;
            if (rbf(src, nodes[jf].data) != rbf(src, nodes[jl].data + 4)) continue;
            y = OFF(rb32(src, lf + 0x14));
            if (y == 0 || y >= D || (yi = dnidx[y]) < 0 || yi == i || dn[yi].op != x->op) continue;
            if (OFF(rb32(src, y + 8)) != lf || OFF(rb32(src, y + 0xC)) || OFF(rb32(src, y + 0x10))) continue;
            cy[i] = yi;
        }
        do {   /* fixpoint over the collision links */
            changed = 0;
            memset(yown, 0xFF, (size_t)(D + 1) * sizeof *yown);
            for (int i = 0; i < nd; i++) if (cy[i] >= 0) yown[dn[cy[i]].o] = i;
            for (int k = 0; k < nn; k++) {
                uint32_t d = nodes[k].data;
                int ncv, owner = -1, ci;
                if (nodes[k].op != 24 || d + 0x20 > D) continue;
                if (yown[nodes[k].o] >= 0) continue;           /* a dropped node's links do not count */
                ncv = (int16_t)rb16(src, d + 0xE);
                ci = nodes[k].o < D ? dnidx[nodes[k].o] : -1;
                if (ncv <= 0) continue;
                {
                    uint32_t cvo = OFF(rb32(src, d + 0x10));
                    if (!cvo || cvo + 16u * (uint32_t)ncv > D) continue;
                    for (int q = 0; q < ncv; q++) {
                        uint32_t t = OFF(rb32(src, cvo + 16u * (uint32_t)q + 8));
                        if (!t) continue;
                        if (t < D && yown[t] >= 0) { owner = yown[t]; break; }          /* points at a dropped node */
                        if (ci >= 0 && cy[ci] >= 0 && (t >= D || dnidx[t] < 0)) { owner = ci; break; } /* cannot be remapped */
                    }
                }
                if (owner >= 0 && cy[owner] >= 0) { cy[owner] = -1; changed = 1; break; }
            }
        } while (changed);
        for (int i = 0; i < nd; i++) {
            if (cy[i] >= 0) {
                dn[i].lf = OFF(rb32(src, dn[cy[i]].o + 8));
                dn[cy[i]].drop = 1;
                st->dl_dropped++;
                if (dn[i].hd) st->lod_shifted++; else dn[i].collapse = 1;
            } else if (dn[i].layer && !dn[i].drop) {
                /* counted below once the far members are known */
            }
        }
        for (int i = 0; i < nd; i++) if (!dn[i].drop && !dn[i].lf && dn[i].layer && dn[i].hd) st->lod_fallback_dl++;
        free(dnidx); free(cy); free(yown);
    }

    /* 4. one mesh over all DL nodes of the file */
    {
        int nvt = 0, ntt = 0;
        for (int i = 0; i < nd; i++) {
            if (dn[i].drop) { dn[i].gbase = 0; continue; }
            dn[i].gbase = nvt; nvt += dn[i].nv;
            for (int k = 0; k < dn[i].ndl; k++) { dn[i].tbase[k] = ntt; ntt += dn[i].dl[k].nt; }
        }
        mesh.v = calloc((size_t)nvt + 1, sizeof *mesh.v);
        mesh.tri = malloc((size_t)(ntt ? ntt : 1) * 3 * sizeof(int));
        vnode = malloc((size_t)(nvt + 1) * sizeof(int));
        if (!mesh.v || !mesh.tri || !vnode) goto done;
        mesh.nv = mesh.nv_in = nvt; mesh.nt = ntt;
        for (int i = 0; i < nd; i++) {
            const DNode *x = &dn[i];
            if (x->drop) continue;
            for (int k = 0; k < x->nv; k++) {
                CvSdVert *v = &mesh.v[x->gbase + k];
                uint32_t o = x->vo + 16u * (uint32_t)k;
                for (int q = 0; q < 3; q++) v->pos[q] = (int16_t)rb16(src, o + 2u * (uint32_t)q);
                v->flag = (uint16_t)rb16(src, o + 6);
                v->st[0] = (int16_t)rb16(src, o + 8); v->st[1] = (int16_t)rb16(src, o + 10);
                memcpy(v->cn, src + o + 12, 4);
                v->mtx = x->vm[k];
                v->layer = x->layer;
                v->head = head_file || (v->mtx >= 0 && v->mtx < MAXMAT && hmat[v->mtx]);
                for (int q = 0; q < 3; q++) v->rest[q] = (float)v->pos[q];
                if (v->mtx >= 0 && v->mtx < MAXMAT && mat_ok[v->mtx])
                    for (int q = 0; q < 3; q++) v->rest[q] += mat_off[v->mtx][q];
                vnode[x->gbase + k] = i;
            }
            for (int k = 0; k < x->ndl; k++)
                for (int t = 0; t < x->dl[k].nt; t++)
                    for (int c = 0; c < 3; c++)
                        mesh.tri[3 * (x->tbase[k] + t) + c] = x->gbase + x->dl[k].tv[3 * t + c];
        }
    }
    hp.crease_deg = CV_SD_HEAD_CREASE_DEG; hp.bulge_clamp = CV_SD_DEFAULT_BULGE_CLAMP;
    mesh.params = &hp; mesh.head_only = 1;
    if (cv_sd_run(&mesh)) goto done;
    st->mid_pn += mesh.npn; st->mid_flat += mesh.nflat;

    /* 5. per-triangle output ranges, node-local vertex ids, matrices */
    ostart = calloc((size_t)mesh.nt + 1, sizeof(int));
    ocount = calloc((size_t)mesh.nt + 1, sizeof(int));
    vlocal = malloc((size_t)(mesh.nv + 1) * sizeof(int));
    vmtx = malloc((size_t)(mesh.nv + 1) * sizeof(int));
    nmidc = calloc((size_t)nd + 1, sizeof(int));
    if (!ostart || !ocount || !vlocal || !vmtx || !nmidc) goto done;
    for (int o = 0; o < mesh.not_; o++) { int s = mesh.osrc[o]; if (!ocount[s]) ostart[s] = o; ocount[s]++; }
    for (int i = 0; i < nd; i++) if (!dn[i].drop) for (int k = 0; k < dn[i].nv; k++) vlocal[dn[i].gbase + k] = k;
    for (int g = 0; g < mesh.nv; g++) vmtx[g] = mesh.v[g].mtx;
    for (int k = 0; k < mesh.nmid; k++) {
        int g = mesh.nv_in + k, node = vnode[mesh.mid_ab[2 * k]];
        vlocal[g] = dn[node].nv + nmidc[node]++;
    }

    /* 6. image: copy + append the rebuilt nodes */
    im.p = malloc((size_t)D + 4096); im.cap = D + 4096; im.len = D;
    if (!im.p) goto done;
    memcpy(im.p, src, D);

    xnew = calloc((size_t)D + 1, sizeof *xnew);
    pcol = calloc((size_t)nd + 1, sizeof *pcol);
    pncv = calloc((size_t)nd + 1, sizeof *pncv);
    if (!xnew || !pcol || !pncv) goto done;

    for (int i = 0; i < nd; i++) {
        DNode *x = &dn[i];
        int nm = nmidc[i], ntot = x->nv + nm;
        uint32_t saved = im.len;
        Cmds cm[2];
        int *lmtx = NULL, maxslot = -1, fail = 1, nexp[2] = {0, 0};
        int (*want[2])[3];
        int64_t nva = -1, ncol = -1, npu = -1, ndl_off[2] = {-1, -1}, nx = -1, rx = -1, nln = -1, rln = -1, nlf = -1, rlf = -1;
        long tri_in = 0, tri_out = 0;
        EmitCtx ec;
        Sim vs;
        uint32_t d = x->data, orec_sz = x->op == 24 ? 0x20u : 0x14u, xo = x->o;
        int ncv = x->op == 24 ? (int16_t)rb16(src, d + 0xE) : 0;
        uint32_t cvo = x->op == 24 ? OFF(rb32(src, d + 0x10)) : 0, puo = x->op == 24 ? OFF(rb32(src, d + 0x14)) : 0;

        memset(cm, 0, sizeof cm);
        want[0] = want[1] = NULL;
        if (x->drop || !x->hd) continue;
        memset(&vs, 0, sizeof vs);

        if ((x->op == 24 && ntot > 32767) || ntot > 65535) goto node_end;
        if (ncv < 0 || (ncv && (!cvo || cvo + 16u * (uint32_t)ncv > D)) || (puo && puo + 2u * (uint32_t)x->nv > D)) goto node_end;
        nva = img_alloc(&im, 16u * (uint32_t)ntot);
        if (nva < 0) goto done;
        ec.op = (int)x->op; ec.vbase = (uint32_t)nva;
        /* vertices: originals verbatim, then midpoints */
        memcpy(im.p + nva, src + x->vo, 16u * (uint32_t)x->nv);
        for (int k = 0; k < mesh.nmid; k++) {
            int g = mesh.nv_in + k;
            const CvSdVert *v;
            uint8_t *q;
            if (vnode[mesh.mid_ab[2 * k]] != i) continue;
            v = &mesh.v[g];
            q = im.p + nva + 16u * (uint32_t)vlocal[g];
            for (int c = 0; c < 3; c++) wb16(q, 2u * (uint32_t)c, (uint16_t)v->pos[c]);
            wb16(q, 6, v->flag);
            wb16(q, 8, (uint16_t)v->st[0]); wb16(q, 10, (uint16_t)v->st[1]);
            memcpy(q + 12, v->cn, 4);
        }
        lmtx = malloc((size_t)ntot * sizeof(int));
        vs.vm = malloc((size_t)ntot * sizeof(int));
        if (!lmtx || !vs.vm) goto done;
        for (int k = 0; k < x->nv; k++) lmtx[k] = vmtx[x->gbase + k];
        for (int k = 0; k < mesh.nmid; k++)
            if (vnode[mesh.mid_ab[2 * k]] == i) lmtx[vlocal[mesh.nv_in + k]] = vmtx[mesh.nv_in + k];
        for (int k = 0; k < ntot; k++) vs.vm[k] = -1;
        for (int k = 0; k < 32; k++) vs.slot[k] = -1;
        vs.mtx = -1;
        for (int k = 0; k < x->ndl; k++) {
            const DlP *dp = &x->dl[k];
            int nt0 = 0;
            for (int t = 0; t < dp->nt; t++) nt0 += ocount[x->tbase[k] + t];
            want[k] = malloc((size_t)(nt0 ? nt0 : 1) * sizeof *want[k]);
            if (!want[k]) goto done;
            for (int t = 0; t < dp->nt; t++)
                for (int q = 0; q < ocount[x->tbase[k] + t]; q++) {
                    int o = ostart[x->tbase[k] + t] + q;
                    for (int c = 0; c < 3; c++) {
                        int g = mesh.otri[3 * o + c];
                        want[k][nexp[k]][c] = (vmtx[g] << 20) | vlocal[g];
                    }
                    nexp[k]++;
                }
            tri_in += dp->nt; tri_out += nexp[k];
            if (rebuild_dl(&cm[k], dp, mesh.otri, ostart, ocount, x->tbase[k], vmtx, vlocal, &ec, &maxslot, NULL)) goto node_end;
            {
                int nm = 0;
                for (int q = 0; q < cm[k].n; q++) if (cm[k].w[2 * q] == 0x01020040u) nm++;
                if (nm > st->max_mtx) st->max_mtx = nm;
                if (nm >= 32) st->mtx_over++;
                st->dls++;
            }
            if (verify_dl(&cm[k], (int)x->op, ec.vbase, ntot, lmtx, (const int (*)[3])want[k], nexp[k], &vs, &maxslot)) {
                st->verify_fail++;
                goto node_end;
            }
        }

        /* collision array + PointUsage copies (op 24) */
        if (x->op == 24) {
            if (ncv) {
                ncol = img_alloc(&im, 16u * (uint32_t)ncv);
                if (ncol < 0) goto done;
                memcpy(im.p + ncol, src + cvo, 16u * (uint32_t)ncv);
            }
            if (puo) {
                npu = img_alloc(&im, (2u * (uint32_t)ntot + 3u) & ~3u);
                if (npu < 0) goto done;
                memcpy(im.p + npu, src + puo, 2u * (uint32_t)x->nv);
                for (int k = x->nv; k < ntot; k++) wb16(im.p, (uint32_t)npu + 2u * (uint32_t)k, 0xFFFF);
            }
        }
        for (int k = 0; k < x->ndl; k++) {
            ndl_off[k] = img_alloc(&im, 8u * (uint32_t)cm[k].n);
            if (ndl_off[k] < 0) goto done;
            for (int c = 0; c < cm[k].n; c++) {
                wb32(im.p, (uint32_t)ndl_off[k] + 8u * (uint32_t)c, cm[k].w[2 * c]);
                wb32(im.p, (uint32_t)ndl_off[k] + 8u * (uint32_t)c + 4, cm[k].w[2 * c + 1]);
            }
        }
        rx = img_alloc(&im, orec_sz); nx = img_alloc(&im, NODE_SZ);
        if (x->lf) {
            if (rx < 0 || nx < 0) goto done;     /* tier shift: X' replaces X under the game's near LOD */
        } else {
            rln = img_alloc(&im, 0x10);
            /* the file's root node is fixed at R0: when X is the root the near LOD takes that
             * slot and X moves to a fresh node (the head files that are a single DL) */
            if (xo == R0) { nln = R0; xo = (uint32_t)img_alloc(&im, NODE_SZ); }
            else nln = img_alloc(&im, NODE_SZ);
            rlf = img_alloc(&im, 0x10); nlf = img_alloc(&im, NODE_SZ);
            if (rx < 0 || nx < 0 || rln < 0 || nln < 0 || rlf < 0 || nlf < 0 || (int32_t)xo < 0) goto done;
        }

        {
            uint8_t *b = im.p;
            uint8_t *rec = b + rx;
            uint32_t par = fld(&im, x->o, 8), nxt = fld(&im, x->o, 0xC), prv = fld(&im, x->o, 0x10);
            if (xo != x->o) memcpy(b + xo, b + x->o, NODE_SZ);

            /* X' record + node */
            memcpy(rec, b + d, orec_sz);
            wb32(rec, 0, SEG5(ndl_off[0]));
            wb32(rec, 4, x->ndl == 2 ? SEG5(ndl_off[1]) : 0);
            if (x->op == 24) {
                wb32(rec, 8, SEG5(nva));
                wb16(rec, 0xC, (uint32_t)ntot);
                if (ncv) wb32(rec, 0x10, SEG5(ncol));
                if (puo) wb32(rec, 0x14, SEG5(npu));
                wb16(rec, 0x1A, 0);
            } else {
                wb32(rec, 0xC, SEG5(nva));
                wb16(rec, 0x10, (uint32_t)ntot);
            }
            memcpy(b + nx, b + xo, NODE_SZ);
            wb32(b, (uint32_t)nx + 4, SEG5(rx));
            wb32(b, (uint32_t)nx + 8, x->lf ? SEG5(par) : SEG5(nln));
            wb32(b, (uint32_t)nx + 0xC, 0); wb32(b, (uint32_t)nx + 0x10, 0); wb32(b, (uint32_t)nx + 0x14, 0);
            xnew[x->o] = (uint32_t)nx;
            pcol[i] = ncol > 0 ? (uint32_t)ncol : 0; pncv[i] = ncv;
          if (x->lf) {
            /* the game's near LOD now draws X'; its far LOD draws X, the original detailed mesh */
            int jl = nidx[par], jf = nidx[x->lf];
            wb32(b, par + 0x14, SEG5(nx)); wb32(b, nodes[jl].data + 8, SEG5(nx));
            wb32(b, x->lf + 0x14, SEG5(xo)); wb32(b, nodes[jf].data + 8, SEG5(xo));
            wb32(b, xo + 8, SEG5(x->lf)); wb32(b, xo + 0xC, 0); wb32(b, xo + 0x10, 0);
          } else {

            /* LOD records: near 0..D over X', far D..max over X */
            wbf(b, (uint32_t)rln, 0.0f); wbf(b, (uint32_t)rln + 4, CV_SD_NEAR_DIST); wb32(b, (uint32_t)rln + 8, SEG5(nx));
            wbf(b, (uint32_t)rlf, CV_SD_NEAR_DIST); wbf(b, (uint32_t)rlf + 4, x->encl_max); wb32(b, (uint32_t)rlf + 8, SEG5(xo));
            /* LOD nodes take X's place in the sibling chain */
            wb16(b, (uint32_t)nln, 8); wb32(b, (uint32_t)nln + 4, SEG5(rln));
            wb32(b, (uint32_t)nln + 8, par ? SEG5(par) : 0);
            wb32(b, (uint32_t)nln + 0xC, SEG5(nlf));
            wb32(b, (uint32_t)nln + 0x10, prv ? SEG5(prv) : 0);
            wb32(b, (uint32_t)nln + 0x14, SEG5(nx));
            wb16(b, (uint32_t)nlf, 8); wb32(b, (uint32_t)nlf + 4, SEG5(rlf));
            wb32(b, (uint32_t)nlf + 8, par ? SEG5(par) : 0);
            wb32(b, (uint32_t)nlf + 0xC, nxt ? SEG5(nxt) : 0);
            wb32(b, (uint32_t)nlf + 0x10, SEG5(nln));
            wb32(b, (uint32_t)nlf + 0x14, SEG5(xo));
            /* X moves under the far LOD */
            wb32(b, xo + 8, SEG5(nlf)); wb32(b, xo + 0xC, 0); wb32(b, xo + 0x10, 0);
            /* neighbours and the parent now point at the pair */
            if (prv) wb32(b, prv + 0xC, SEG5(nln));
            if (nxt) wb32(b, nxt + 0x10, SEG5(nlf));
            if (par) {
                int j = par < D ? nidx[par] : -1;
                if (fld(&im, par, 0x14) == xo) wb32(b, par + 0x14, SEG5(nln));
                if (j >= 0) {
                    uint32_t ff[2];
                    int nf = rec_node_fields((int)nodes[j].op, ff);
                    for (int q = 0; q < nf; q++)
                        if (nodes[j].data + ff[q] + 4 <= D && fld(&im, nodes[j].data, ff[q]) == xo)
                            wb32(b, nodes[j].data + ff[q], SEG5(nln));
                }
            }
          }
        }
        st->done++; ndone++;
        if (!x->lf) st->dl_wrapped++;
        st->tri_before += tri_in; st->tri_after += tri_out;
        st->vtx_before += x->nv; st->vtx_after += ntot;
        if (maxslot > st->max_slot) st->max_slot = maxslot;
        fail = 0;
node_end:
        if (fail) { im.len = saved; st->skipped++; }
        free(lmtx); free(vs.vm);
        for (int k = 0; k < 2; k++) { free(cm[k].w); free(want[k]); }
    }
    /* body parts under a game LOD pair keep the detailed mesh at every distance: the near LOD
     * reaches as far as the far one did, which is unlinked together with the simplified DL */
    for (int i = 0; i < nd; i++) {
        uint32_t ln, lf;
        int jl, jf;
        if (!dn[i].collapse) continue;
        ln = OFF(rb32(im.p, dn[i].o + 8)); lf = dn[i].lf;
        jl = nidx[ln]; jf = nidx[lf];
        wbf(im.p, nodes[jl].data + 4, rbf(src, nodes[jf].data + 4));
        wb32(im.p, ln + 0xC, 0);
        st->lod_collapsed++; ndone++;
    }
    /* collision links of every X' follow the near branch: related node -> its X' */
    for (int i = 0; i < nd; i++) {
        if (!pcol[i]) continue;
        for (int q = 0; q < pncv[i]; q++) {
            uint32_t o = pcol[i] + 16u * (uint32_t)q + 8, t = OFF(rb32(im.p, o));
            if (t && t < D && xnew[t]) wb32(im.p, o, SEG5(xnew[t]));
        }
    }
    if (ndone) { *out = im.p; *outlen = im.len; im.p = NULL; rc = 1; }
    else rc = 0;
done:
    free(im.p); free(nidx); free(mat_off); free(mat_ok); free(stack); free(nodes);
    if (dn) for (int i = 0; i < nn + 1; i++) { dlp_free(&dn[i].dl[0]); dlp_free(&dn[i].dl[1]); free(dn[i].vm); }
    free(dn);
    free(mesh.v); free(mesh.tri); cv_sd_free_out(&mesh);
    free(xnew); free(pcol); free(pncv); free(hmat);
    free(vnode); free(vlocal); free(vmtx); free(ostart); free(ocount); free(nmidc);
    return rc;
}

/* ============================================================== part 3 ==== */
/* The flat mesh of a model image, for viewers: the same node walk, rest-pose joint offsets and DL
 * parse as cv_subdiv_model (steps 1-4 there), without rewriting anything. */
void cv_sd_model_mesh_free(CvSdModelMesh *m)
{
    cv_sd_free_out(&m->mesh);
    free(m->mesh.v); free(m->mesh.tri); free(m->joint_off); free(m->joint_ok); free(m->tri_marker); free(m->marker);
    memset(m, 0, sizeof *m);
}

int cv_sd_model_mesh(const uint8_t *src, uint32_t D, int NS, int NT, int tier, CvSdModelMesh *out)
{
    uint32_t R0 = 4u * (uint32_t)NS + 12u * (uint32_t)NT;
    NInfo *nodes = NULL; int nn = 0, capn = 0;
    int32_t *nidx = NULL;
    uint32_t *stack = NULL; int sp = 0, capst = 0;
    DNode *dn = NULL; int nd = 0, used = 0, nvt = 0, ntt = 0, rc = -1;
    enum { MAXMAT = 1024 };

    memset(out, 0, sizeof *out);
    if (D < R0 + NODE_SZ || D > 0x1000000u) return 0;
    nidx = malloc((size_t)(D + 1) * sizeof *nidx);
    out->joint_off = calloc((size_t)MAXMAT, sizeof *out->joint_off);
    out->joint_ok = calloc((size_t)MAXMAT, 1);
    stack = malloc(64 * sizeof *stack); capst = 64;
    if (!nidx || !out->joint_off || !out->joint_ok || !stack) goto done;
    memset(nidx, 0xFF, (size_t)(D + 1) * sizeof *nidx);

    stack[sp++] = R0;
    while (sp) {
        uint32_t o = stack[--sp], data, ch;
        int op;
        if (o == 0 || o + NODE_SZ > D || nidx[o] >= 0) continue;
        op = (int)(rb16(src, o) & 0xFF);
        data = OFF(rb32(src, o + 4));
        if (data + 4 > D) continue;
        if (nn == capn) {
            capn = capn ? capn * 2 : 256;
            nodes = realloc(nodes, (size_t)capn * sizeof *nodes);
            if (!nodes) goto done;
        }
        nodes[nn].o = o; nodes[nn].op = (uint32_t)op; nodes[nn].data = data;
        nidx[o] = nn++;
        ch = OFF(rb32(src, o + 0x14));
        if (op == 8 && data + 12 <= D) ch = OFF(rb32(src, data + 8));
        else if (op == 18) ch = OFF(rb32(src, data));
        if (sp + 2 > capst) {
            capst *= 2;
            stack = realloc(stack, (size_t)capst * sizeof *stack);
            if (!stack) goto done;
        }
        if (ch) stack[sp++] = ch;
        if (OFF(rb32(src, o + 0xC))) stack[sp++] = OFF(rb32(src, o + 0xC));
    }
    for (int i = 0; i < nn; i++) {
        float acc[3] = {0, 0, 0};
        uint32_t o = nodes[i].o;
        int depth = 0;
        if (nodes[i].op != 2 || nodes[i].data + 0x14 > D) continue;
        while (o && depth++ < 64) {
            int j = o < D ? nidx[o] : -1;
            if (j < 0) break;
            if (nodes[j].op == 2 && nodes[j].data + 12 <= D)
                for (int k = 0; k < 3; k++) acc[k] += rbf(src, nodes[j].data + 4 * (uint32_t)k);
            o = OFF(rb32(src, o + 8));
        }
        for (int q = 0; q < 3; q++) {
            int id = (int16_t)rb16(src, nodes[i].data + 0xE + 2 * (uint32_t)q);
            if (id >= 0 && id < MAXMAT && !out->joint_ok[id]) { memcpy(out->joint_off[id], acc, sizeof acc); out->joint_ok[id] = 1; }
        }
    }
    dn = calloc((size_t)nn + 1, sizeof *dn);
    if (!dn) goto done;
    for (int i = 0; i < nn; i++) {
        DNode *x;
        uint32_t d = nodes[i].data;
        Sim sm;
        if (nodes[i].op != 4 && nodes[i].op != 24) continue;
        if (d + 0x14 > D || (nodes[i].op == 24 && d + 0x20 > D)) continue;
        x = &dn[nd];
        memset(x, 0, sizeof *x);
        x->o = nodes[i].o; x->op = nodes[i].op; x->data = d;
        x->prim = OFF(rb32(src, d)); x->sec = OFF(rb32(src, d + 4));
        if (x->op == 24) { x->vo = OFF(rb32(src, d + 8)); x->nv = (int16_t)rb16(src, d + 0xC); }
        else { x->vo = OFF(rb32(src, d + 0xC)); x->nv = (int)rb16(src, d + 0x10); }
        if (!x->prim || !x->vo || x->nv <= 0 || x->vo + 16u * (uint32_t)x->nv > D || x->prim >= D) continue;
        x->vm = malloc((size_t)x->nv * sizeof(int));
        if (!x->vm) goto done;
        for (int k = 0; k < x->nv; k++) x->vm[k] = -1;
        memset(&sm, 0, sizeof sm);
        for (int k = 0; k < 32; k++) sm.slot[k] = -1;
        sm.mtx = -1; sm.vm = x->vm;
        if (parse_dl(src, D, x->prim, (int)x->op, x->vo, x->nv, &sm, &x->dl[0]) || sm.conflict) {
            dlp_free(&x->dl[0]); free(x->vm); x->vm = NULL; continue;
        }
        x->ndl = 1;
        if (x->sec) {
            if (parse_dl(src, D, x->sec, (int)x->op, x->vo, x->nv, &sm, &x->dl[1]) || sm.conflict) {
                dlp_free(&x->dl[0]); dlp_free(&x->dl[1]); free(x->vm); x->vm = NULL; continue;
            }
            x->ndl = 2;
        }
        {
            uint32_t a = OFF(rb32(src, x->o + 8));
            int depth = 0;
            while (a && depth++ < 64) {
                int j = a < D ? nidx[a] : -1;
                if (j < 0) break;
                if (nodes[j].op == 8 && nodes[j].data + 8 <= D) { x->layer = rbf(src, nodes[j].data) == 0.0f ? 1 : 2; break; }
                a = OFF(rb32(src, a + 8));
            }
        }
        x->drop = tier == 0 ? x->layer == 2 : x->layer == 1;   /* not part of this tier */
        nd++;
    }
    for (int i = 0; i < nd; i++) {
        if (dn[i].drop) continue;
        dn[i].gbase = nvt; nvt += dn[i].nv;
        for (int k = 0; k < dn[i].ndl; k++) { dn[i].tbase[k] = ntt; ntt += dn[i].dl[k].nt; }
        used++;
    }
    out->ndl = used;
    if (!used) { rc = 0; goto done; }
    out->mesh.v = calloc((size_t)nvt + 1, sizeof *out->mesh.v);
    out->mesh.tri = malloc((size_t)(ntt ? ntt : 1) * 3 * sizeof(int));
    if (!out->mesh.v || !out->mesh.tri) goto done;
    out->mesh.nv = out->mesh.nv_in = nvt; out->mesh.nt = ntt;
    out->tri_marker = malloc((size_t)(ntt ? ntt : 1) * sizeof(int));
    out->marker = malloc(4096 * sizeof *out->marker);
    if (!out->tri_marker || !out->marker) goto done;
    for (int i = 0; i < ntt; i++) out->tri_marker[i] = -1;
    for (int i = 0; i < nd; i++) {
        const DNode *x = &dn[i];
        int cur = -1;
        if (x->drop) continue;
        for (int k = 0; k < x->nv; k++) {
            CvSdVert *v = &out->mesh.v[x->gbase + k];
            uint32_t o = x->vo + 16u * (uint32_t)k;
            for (int q = 0; q < 3; q++) v->pos[q] = (int16_t)rb16(src, o + 2u * (uint32_t)q);
            v->flag = (uint16_t)rb16(src, o + 6);
            v->st[0] = (int16_t)rb16(src, o + 8); v->st[1] = (int16_t)rb16(src, o + 10);
            memcpy(v->cn, src + o + 12, 4);
            v->mtx = x->vm[k];
            v->layer = x->layer;
            for (int q = 0; q < 3; q++) v->rest[q] = (float)v->pos[q];
            if (v->mtx >= 0 && v->mtx < MAXMAT && out->joint_ok[v->mtx])
                for (int q = 0; q < 3; q++) v->rest[q] += out->joint_off[v->mtx][q];
        }
        for (int k = 0; k < x->ndl; k++) {
            for (int t = 0; t < x->dl[k].nt; t++)
                for (int c = 0; c < 3; c++)
                    out->mesh.tri[3 * (x->tbase[k] + t) + c] = x->gbase + x->dl[k].tv[3 * t + c];
            /* the texture marker in force for each triangle run (markers sit between runs in the DL) */
            for (int e = 0; e < x->dl[k].ne; e++) {
                const Elem *el = &x->dl[k].e[e];
                if (el->kind == 0 && (el->w0 >> 24) == 0xC0 && out->nmarker < 4096) {
                    out->marker[out->nmarker][0] = el->w0; out->marker[out->nmarker][1] = el->w1;
                    cur = out->nmarker++;
                } else if (el->kind == 1)
                    for (int t = el->t0; t < el->t1; t++) out->tri_marker[x->tbase[k] + t] = cur;
            }
        }
    }
    rc = 1;
done:
    free(nidx); free(stack); free(nodes);
    if (dn) for (int i = 0; i < nn + 1; i++) { dlp_free(&dn[i].dl[0]); dlp_free(&dn[i].dl[1]); free(dn[i].vm); }
    free(dn);
    if (rc < 0) cv_sd_model_mesh_free(out);
    return rc;
}
