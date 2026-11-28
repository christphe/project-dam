/* model_export.c -- static object-model export (issue #119): the model-number
 * table + model files -> one glTF 2.0 per model.
 *
 * CLEAN ROOM: written from docs/specs/models.md (and the pages it builds on:
 * level-decor.md, textures.md, display-lists.md) only, plus the converter
 * library's file table, rarezip inflate and ROM check. See export.h.
 */
#include "export_common.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------ */
/* the per-region constants of models.md (the only per-region place)        */
/* ------------------------------------------------------------------------ */

static const struct { const char *region; uint32_t cdata_rom, base, table_off; } k_mreg[] = {
    { "ntsc-final", 0x21990u, 0x80020D90u, 0x19498u },
    { "pal-final",  0x1F850u, 0x8001EC50u, 0x16138u },
    { "jpn-final",  0x219D0u, 0x80020DD0u, 0x19488u },
};
#define N_MODELS      340
#define CDATA_MAX_OUT (8u * 1024u * 1024u)

typedef struct {
    int num;
    char file[64];            /* "PdoorstatgateZ" */
    char name[64];            /* "doorstatgate" */
    char dir[64];             /* output directory / file stem */
    float base_scale;
    int nparts, nmatrices, ntextures;
    float radius;
    int njoints, skel_size;   /* skeleton record (extras only) */
    uint16_t (*joints)[3];
    const CvFileRow *row;
} ModelEnt;

static void model_ents_free(ModelEnt *m, int n)
{
    for (int i = 0; i < n; i++) free(m[i].joints);
    free(m);
}

/* ------------------------------------------------------------------------ */
/* the model-number table                                                   */
/* ------------------------------------------------------------------------ */

/* Reads the table out of the decompressed cdata. 0 ok; -1 refused (msg says why). */
static int read_table(const uint8_t *cd, size_t cdlen, uint32_t base, uint32_t toff, const CvRegionData *rd, ModelEnt **out,
                      char *msg, size_t msgsz)
{
    ModelEnt *me = (ModelEnt *)calloc(N_MODELS, sizeof *me);
    long sum_parts = 0, sum_tex = 0, sum_mtx = 0;
    int n_01 = 0, n_10 = 0;
    if (!me) { snprintf(msg, msgsz, "out of memory"); return -1; }
#define REFUSE(...) do { snprintf(msg, msgsz, __VA_ARGS__); model_ents_free(me, N_MODELS); return -1; } while (0)
    for (int i = 0; i <= N_MODELS; i++) {
        size_t eo = (size_t)toff + (size_t)i * 12;
        if (eo + 12 > cdlen) REFUSE("model table runs past the end of cdata at entry %d", i);
        const uint8_t *e = cd + eo;
        uint32_t hp = ex_be32(e), np = ex_be32(e + 4);
        if (i == N_MODELS) {
            if (hp != 0) REFUSE("model table has more than %d entries", N_MODELS);
            break;
        }
        if (hp == 0) REFUSE("model table ends after %d entries (expected %d)", i, N_MODELS);
        ModelEnt *m = &me[i];
        m->num = i;
        if (hp < base || np < base || hp - base + 24 > cdlen || np - base >= cdlen)
            REFUSE("entry %d: header or name pointer outside cdata", i);
        {
            const char *nm = (const char *)cd + (np - base);
            size_t k = 0;
            while (np - base + k < cdlen && nm[k] && k < sizeof m->file - 1) k++;
            if (np - base + k >= cdlen || nm[k]) REFUSE("entry %d: file name not terminated", i);
            memcpy(m->file, nm, k); m->file[k] = 0;
            if (k < 3 || m->file[0] != 'P' || m->file[k - 1] != 'Z') REFUSE("entry %d: name \"%s\" is not P...Z", i, m->file);
            memcpy(m->name, m->file + 1, k - 2); m->name[k - 2] = 0;
            memcpy(m->dir, m->file, k - 1); m->dir[k - 1] = 0;   /* without the trailing Z */
        }
        m->base_scale = ex_bef32(e + 8);
        if (fabsf(m->base_scale - 0.1f) < 1e-6f) n_01++;
        else if (m->base_scale == 1.0f) n_10++;
        m->row = cv_find_row(rd, m->file);
        if (!m->row) REFUSE("entry %d: %s has no row in the ROM file table", i, m->file);
        {
            const uint8_t *h = cd + (hp - base);
            uint32_t sp = ex_be32(h + 4);
            m->nparts = ex_be16s(h + 12); m->nmatrices = ex_be16s(h + 14);
            m->radius = ex_bef32(h + 16); m->ntextures = ex_be16s(h + 22);
            if (m->nparts < 0 || m->nmatrices < 0 || m->ntextures < 0) REFUSE("entry %d: negative count in the header", i);
            sum_parts += m->nparts; sum_tex += m->ntextures; sum_mtx += m->nmatrices;
            if (sp >= base && sp - base + 12 <= cdlen) {
                const uint8_t *sk = cd + (sp - base);
                int nj = ex_be16s(sk);
                uint32_t jp_ = ex_be32(sk + 4);
                m->skel_size = ex_be16s(sk + 8);
                if (nj > 0 && nj < 4096 && jp_ >= base && (size_t)(jp_ - base) + (size_t)nj * 6 <= cdlen) {
                    m->joints = (uint16_t (*)[3])malloc((size_t)nj * sizeof *m->joints);
                    if (!m->joints) REFUSE("out of memory");
                    for (int j = 0; j < nj; j++)
                        for (int c = 0; c < 3; c++) m->joints[j][c] = ex_be16(cd + (jp_ - base) + (size_t)j * 6 + (size_t)c * 2);
                    m->njoints = nj;
                }
            }
        }
    }
    if (strcmp(me[0].file, "Palarm1Z") || strcmp(me[2].file, "PexplosionbitZ") || strcmp(me[339].file, "PbollardZ"))
        REFUSE("entries 0 / 2 / 339 are %s / %s / %s, not Palarm1Z / PexplosionbitZ / PbollardZ", me[0].file, me[2].file,
               me[339].file);
    if (sum_parts != 356 || sum_tex != 1850 || sum_mtx != 412)
        REFUSE("header sums nparts %ld ntextures %ld nmatrices %ld, expected 356 / 1850 / 412", sum_parts, sum_tex, sum_mtx);
    if (n_01 != 275 || n_10 != 65) REFUSE("base scales: %d x 0.1 and %d x 1.0, expected 275 and 65", n_01, n_10);
    *out = me;
    return 0;
#undef REFUSE
}

/* Finds the table in the decompressed cdata of this region. */
static int load_table(const uint8_t *rom, size_t rom_size, const char *region, const CvRegionData *rd, ModelEnt **out,
                      char *msg, size_t msgsz)
{
    size_t r;
    for (r = 0; r < sizeof k_mreg / sizeof k_mreg[0]; r++) if (!strcmp(region, k_mreg[r].region)) break;
    if (r == sizeof k_mreg / sizeof k_mreg[0]) { snprintf(msg, msgsz, "no model table constants for region %s", region); return -1; }
    if (k_mreg[r].cdata_rom >= rom_size) { snprintf(msg, msgsz, "ROM too small"); return -1; }
    CvBuf cd;
    memset(&cd, 0, sizeof cd);
    if (cv_rz_inflate(rom + k_mreg[r].cdata_rom, rom_size - k_mreg[r].cdata_rom, &cd) || cd.len > CDATA_MAX_OUT) {
        cv_buf_free(&cd);
        snprintf(msg, msgsz, "cdata segment does not inflate");
        return -1;
    }
    int rc = read_table(cd.p, cd.len, k_mreg[r].base, k_mreg[r].table_off, rd, out, msg, msgsz);
    cv_buf_free(&cd);
    return rc;
}

/* ------------------------------------------------------------------------ */
/* one model                                                                */
/* ------------------------------------------------------------------------ */

typedef struct {
    char name[56];
    int mesh;
    int has_t; float t[3];
    CvBuf extras;                 /* JSON object body, without braces */
    int *kids; size_t nk, capk;
} GNode;

typedef struct {
    const ModelEnt *me;
    const char *region;
    ExTexCache *tc;

    const uint8_t *f; size_t fsize;
    int nparts, ntex, nmat;
    uint32_t root_off, root_child;
    uint8_t *seen;

    GNode *gn; size_t ngn, capgn;
    int *roots; size_t nroots, caproots;
    int *fars; size_t nfars, capfars;
    CvBuf meshes; int nmesh;
    GW g;
    MatCtx mc;

    uint64_t tri_def, tri_all, mixed_tris, off_anc_tris, dropped_tris, dropped_vtx, bad_ptrs, no_end, calls;
    uint64_t census[256];
    int npos, nswitch, nlod, nbbox, nmuzzle, nstar, nskipped_kinds, nscreens;
    int skipped_kind[32];
    int rtype_seen[8];
    int bbox_a_ok, bbox_b_ok; float bbox_a[6], bbox_b[6];   /* child of the root / child of the root's first child */
    int screen_model;
    int oom;
    CvBuf notes; int nnotes;
} MX;

static const uint8_t k_known_model_ops[] = { 0x01, 0x04, 0xB1, 0xBF, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xBB, 0xC0, 0xE7, 0xFC,
                                             0xFA, 0xFB, 0xE6, 0xE8, 0xE9, 0x06 };
static int op_known(unsigned op)
{
    for (size_t i = 0; i < sizeof k_known_model_ops; i++) if (k_known_model_ops[i] == op) return 1;
    return 0;
}

static void note(MX *x, const char *fmt, ...)
{
    char tmp[400];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if (x->nnotes) cv_buf_put(&x->notes, ",", 1);
    ex_jstr(&x->notes, tmp);
    x->nnotes++;
}

static void jf(CvBuf *b, float f)
{
    if (!isfinite(f)) { ex_jp(b, "0"); return; }
    ex_jp(b, "%.9g", (double)f);
}

/* segment-5 pointer -> file offset of an object of `need` bytes; 0 = none or invalid (invalid is counted) */
static uint32_t pnt(MX *x, uint32_t raw, size_t need)
{
    if (!raw) return 0;
    uint32_t off = raw & 0x00FFFFFFu;
    if ((raw >> 24) != 5 || (size_t)off + need > x->fsize || off < 4u * (uint32_t)x->nparts) { x->bad_ptrs++; return 0; }
    return off;
}

static int add_gnode(MX *x, const char *name)
{
    if (ex_grow((void **)&x->gn, &x->capgn, x->ngn + 1, sizeof *x->gn)) { x->oom = 1; return -1; }
    GNode *n = &x->gn[x->ngn];
    memset(n, 0, sizeof *n);
    snprintf(n->name, sizeof n->name, "%s", name);
    n->mesh = -1;
    return (int)x->ngn++;
}

static void add_to(MX *x, int gparent, int gi, int far_root)
{
    if (gparent >= 0) {
        GNode *p = &x->gn[gparent];
        if (ex_grow((void **)&p->kids, &p->capk, p->nk + 1, sizeof *p->kids)) { x->oom = 1; return; }
        p->kids[p->nk++] = gi;
    } else if (far_root) {
        if (ex_grow((void **)&x->fars, &x->capfars, x->nfars + 1, sizeof *x->fars)) { x->oom = 1; return; }
        x->fars[x->nfars++] = gi;
    } else {
        if (ex_grow((void **)&x->roots, &x->caproots, x->nroots + 1, sizeof *x->roots)) { x->oom = 1; return; }
        x->roots[x->nroots++] = gi;
    }
}

/* the part numbers whose part-table entry points at node `off` */
static int parts_of(const MX *x, uint32_t off, int *out, int max)
{
    int n = 0;
    for (int k = 0; k < x->nparts && n < max; k++)
        if ((ex_be32(x->f + 4 * (size_t)k) & 0x00FFFFFFu) == off && (ex_be32(x->f + 4 * (size_t)k) >> 24) == 5) out[n++] = k;
    return n;
}

static void put_parts(CvBuf *e, const int *parts, int np)
{
    if (!np) return;
    ex_jp(e, ",\"part\":[");
    for (int i = 0; i < np; i++) ex_jp(e, "%s%d", i ? "," : "", parts[i]);
    ex_jp(e, "]");
}

/* ---- the display lists ---- */

/* render type 4 (models.md): two cycles; combine cycle 0 = (TEXEL1 - TEXEL0) * LOD fraction + TEXEL0 (colour and
 * alpha); cycle 1 colour = COMBINED * SHADE, alpha = COMBINED * SHADE + PRIMITIVE alpha. Raw combine words built
 * from the field positions of display-lists.md. */
static void render_type4_state(ExGfxState *s, int list)
{
    uint32_t w0 = (2u << 20)      /* a0 colour = TEXEL1 */
                | (13u << 15)     /* c0 colour = LOD fraction */
                | (2u << 12)      /* a0 alpha = TEXEL1 */
                | (0u << 9)       /* c0 alpha = LOD fraction */
                | (0u << 5)       /* a1 colour = COMBINED */
                | 4u;             /* c1 colour = SHADE */
    uint32_t w1 = (1u << 28)      /* b0 colour = TEXEL0 */
                | (15u << 24)     /* b1 colour = zero */
                | (0u << 21)      /* a1 alpha = COMBINED */
                | (4u << 18)      /* c1 alpha = SHADE */
                | (1u << 15)      /* d0 colour = TEXEL0 */
                | (1u << 12)      /* b0 alpha = TEXEL0 */
                | (1u << 9)       /* d0 alpha = TEXEL0 */
                | (7u << 6)       /* d1 colour = zero */
                | (7u << 3)       /* b1 alpha = zero */
                | 3u;             /* d1 alpha = PRIMITIVE */
    s->omh = 1u << 20;            /* two-cycle */
    s->c0 = w0; s->c1 = w1;
    s->oml = list ? 0xC41049D8u : 0xC4112078u;
}

static void emit_tri(MX *x, PrimSet *ps, ExGfxState *s, int list, int anc_mtx, uint64_t *ntri, int a, int b, int c)
{
    if (a > 15 || b > 15 || c > 15 || !s->cache[a].valid || !s->cache[b].valid || !s->cache[c].valid) {
        x->dropped_tris++;
        return;
    }
    if (s->mat < 0) {
        s->mat = ex_resolve_material(&x->mc, s, list);
        if (s->mat < 0) { x->oom = 1; return; }
    }
    Prim *p = ex_primset_find(ps, s->mat, &x->oom);
    if (!p) return;
    const Material *m = &x->mc.mats[s->mat];
    long ia = ex_prim_vertex(p, &s->cache[a], m, 1.0, s);
    long ib = ex_prim_vertex(p, &s->cache[b], m, 1.0, s);
    long ic = ex_prim_vertex(p, &s->cache[c], m, 1.0, s);
    if (ia < 0 || ib < 0 || ic < 0) { x->oom = 1; return; }
    if (ex_grow((void **)&p->idx, &p->capi, p->ni + 3, sizeof *p->idx)) { x->oom = 1; return; }
    p->idx[p->ni++] = (uint32_t)ia; p->idx[p->ni++] = (uint32_t)ib; p->idx[p->ni++] = (uint32_t)ic;
    (*ntri)++;
    if (s->cache[a].mtx != s->cache[b].mtx || s->cache[a].mtx != s->cache[c].mtx) x->mixed_tris++;
    else if (s->cache[a].mtx != anc_mtx) x->off_anc_tris++;
}

/* Runs one list of a display-list node. kind 24: vertex loads are segment 4 (offset from the node's vertex array);
 * kind 4: segment 5 (offset from the file start). */
static void run_list(MX *x, PrimSet *ps, uint32_t loff, int list, int kind, uint32_t varr, int rtype, int anc_mtx,
                     uint64_t *ntri)
{
    ExGfxState s;
    memset(&s, 0, sizeof s);
    s.ks = s.kt = 0xFFFF;
    s.mat = -1; s.mtx = -1;
    s.geom = 0x2000u;                         /* the game culls back faces before every display-list node */
    if (rtype != 0) render_type4_state(&s, list);   /* render type 0: the list sets its own state */
    size_t maxn = (x->fsize - loff) / 8;
    int ended = 0;
    for (size_t i = 0; i < maxn && !x->oom; i++) {
        const uint8_t *c = x->f + loff + i * 8;
        uint32_t w0 = ex_be32(c), w1 = ex_be32(c + 4);
        unsigned op = w0 >> 24;
        x->census[op]++;
        if (op == 0xB8) { ended = 1; break; }
        switch (op) {
        case 0x01: s.mtx = (int)((w1 & 0xFFFFFFu) / 64u); break;
        case 0x04: {
            unsigned count = (w0 & 0xFFFF) / 16, first = (w0 >> 16) & 0xF, seg = w1 >> 24;
            size_t base, off = w1 & 0xFFFFFFu;
            if (kind == 24) { if (seg != 4) { x->dropped_vtx++; break; } base = varr; }
            else            { if (seg != 5) { x->dropped_vtx++; break; } base = 0; }
            if (first + count > 16 || !(kind == 24 ? varr : 1) || base + off + (size_t)count * 16 > x->fsize) { x->dropped_vtx++; break; }
            for (unsigned k = 0; k < count; k++) {
                const uint8_t *v = x->f + base + off + (size_t)k * 16;
                ExVtx *cv = &s.cache[first + k];
                cv->x = ex_be16s(v); cv->y = ex_be16s(v + 2); cv->z = ex_be16s(v + 4);
                cv->s = ex_be16s(v + 8); cv->t = ex_be16s(v + 10);
                memcpy(cv->c, v + 12, 4);
                cv->valid = 1; cv->mtx = s.mtx;
            }
            break;
        }
        case 0xBF:
            emit_tri(x, ps, &s, list, anc_mtx, ntri, (int)(((w1 >> 16) & 0xFF) / 10), (int)(((w1 >> 8) & 0xFF) / 10),
                     (int)((w1 & 0xFF) / 10));
            break;
        case 0xB1:
            for (int k = 0; k < 4; k++) {
                int a = (int)((w1 >> (8 * k)) & 0xF), b = (int)((w1 >> (8 * k + 4)) & 0xF), cc = (int)((w0 >> (4 * k)) & 0xF);
                if (!a && !b && !cc) continue;
                emit_tri(x, ps, &s, list, anc_mtx, ntri, a, b, cc);
            }
            break;
        case 0x06: x->calls++; break;
        default: ex_dl_state(&s, op, w0, w1); break;
        }
    }
    if (!ended) x->no_end++;
}

static int is_screen_model(const char *name)
{
    static const char *const pre[] = { "tv", "console", "modembox", "doorpanel" };
    for (size_t i = 0; i < sizeof pre / sizeof pre[0]; i++)
        if (!strncmp(name, pre[i], strlen(pre[i]))) return 1;
    return 0;
}

/* a display-list node (kind 4 or 24): builds its mesh, adds the glTF node under `gparent` */
static void do_dl_node(MX *x, uint32_t noff, uint32_t rec, int kind, int gparent, int anc_mtx, int in_far)
{
    const uint8_t *r = x->f + rec;
    uint32_t prim_l = pnt(x, ex_be32(r), 8), sec_l = pnt(x, ex_be32(r + 4), 8);
    uint32_t varr = 0;
    int rtype, vcount, ccount = -1;
    if (kind == 24) {
        varr = pnt(x, ex_be32(r + 8), 0);
        vcount = ex_be16s(r + 12); ccount = ex_be16s(r + 14);
        rtype = ex_be16s(r + 24);
    } else {
        vcount = ex_be16(r + 16);
        rtype = (int8_t)r[18];
    }
    if (rtype >= 0 && rtype < 8) x->rtype_seen[rtype]++;
    PrimSet ps;
    memset(&ps, 0, sizeof ps);
    uint64_t tp = 0, ts = 0;
    if (prim_l) run_list(x, &ps, prim_l, 0, kind, varr, rtype, anc_mtx, &tp);
    if (sec_l && (rtype == 3 || rtype == 4 || rtype > 4 || rtype < 0)) run_list(x, &ps, sec_l, 1, kind, varr, rtype, anc_mtx, &ts);
    else if (sec_l) note(x, "node %06X: secondary list not drawn for render type %d", noff, rtype);
    if (rtype < 0 || rtype > 4) note(x, "node %06X: render type %d unknown, treated like type 4", noff, rtype);
    if (x->oom) { ex_primset_free(&ps); return; }

    x->tri_all += tp + ts;
    if (!in_far) x->tri_def += tp + ts;

    int parts[16];
    int np = parts_of(x, noff, parts, 16);
    char nm[48];
    snprintf(nm, sizeof nm, "dl_%06X", noff);
    int gi = add_gnode(x, nm);
    if (gi < 0) { ex_primset_free(&ps); return; }
    if (tp + ts) {
        CvBuf pj, lists;
        memset(&pj, 0, sizeof pj); memset(&lists, 0, sizeof lists);
        int nprim = ex_emit_prims(&x->g, &pj, &ps, &x->mc);
        if (nprim < 0) { x->oom = 1; cv_buf_free(&pj); ex_primset_free(&ps); return; }
        if (nprim) {
            int first = 1;
            for (size_t pi = 0; pi < ps.np; pi++) {
                if (!ps.prims[pi].ni) continue;
                ex_jp(&lists, "%s\"%s\"", first ? "" : ",", x->mc.mats[ps.prims[pi].mat].k.list ? "secondary" : "primary");
                first = 0;
            }
            ex_jp(&x->meshes, "%s{\"name\":\"%s\",\"primitives\":[", x->nmesh ? "," : "", nm);
            cv_buf_put(&x->meshes, pj.p, pj.len);
            ex_jp(&x->meshes, "],\"extras\":{\"primitive_lists\":[");
            cv_buf_put(&x->meshes, lists.p, lists.len);
            ex_jp(&x->meshes, "]}}");
            x->gn[gi].mesh = x->nmesh++;
        }
        cv_buf_free(&pj); cv_buf_free(&lists);
    }
    CvBuf *e = &x->gn[gi].extras;
    ex_jp(e, "\"kind\":%d,\"render_type\":%d,\"vertex_count\":%d", kind, rtype, vcount);
    if (kind == 24) ex_jp(e, ",\"collision_vertex_count\":%d", ccount);
    ex_jp(e, ",\"triangles\":{\"primary\":%llu,\"secondary\":%llu}", (unsigned long long)tp, (unsigned long long)ts);
    put_parts(e, parts, np);
    if (x->screen_model) {
        int scr = 0;
        for (int i = 0; i < np; i++) if (parts[i] >= 0 && parts[i] <= 3) scr = 1;
        if (scr) { ex_jp(e, ",\"screen\":true"); x->nscreens++; }
    }
    if (in_far) ex_jp(e, ",\"lod_far\":true");
    add_to(x, gparent, gi, 0);
    ex_primset_free(&ps);
}

/* ---- the node walk ---- */

static void walk(MX *x, uint32_t first, int gparent, int has_pos, int anc_mtx, const float acc[3], int in_far, int depth)
{
    uint32_t cur = first;
    int go = 1;   /* the root may sit at offset 0 (no part or texture table) */
    while (go && !x->oom) {
        if (depth > 200) { note(x, "node tree deeper than 200 levels: cut"); return; }
        if ((size_t)cur + 24 > x->fsize) { x->bad_ptrs++; return; }
        if (x->seen[cur]) { note(x, "node %06X reached twice (loop in the tree): cut", cur); return; }
        x->seen[cur] = 1;
        const uint8_t *n = x->f + cur;
        unsigned kw = ex_be16(n), kind = kw & 0xFF;
        if (kw >> 8) note(x, "node %06X: kind word %04X has a non-zero high byte", cur, kw);
        uint32_t rec_raw = ex_be32(n + 4);
        uint32_t parent = ex_be32(n + 8) & 0x00FFFFFFu;
        uint32_t next = pnt(x, ex_be32(n + 12), 24);
        uint32_t child = pnt(x, ex_be32(n + 20), 24);
        int parts[16];
        int np = parts_of(x, cur, parts, 16);
        int sub = 1;   /* walk the children */
        int cg = gparent, chas = has_pos, canc = anc_mtx, cfar = in_far;
        float cacc[3] = { acc[0], acc[1], acc[2] };

        switch (kind) {
        case 1: case 2: case 21: {
            size_t rs = kind == 2 ? 28 : kind == 21 ? 20 : 12;
            uint32_t rec = pnt(x, rec_raw, rs);
            if (!rec) { note(x, "node %06X (kind %u): bad record pointer, subtree skipped", cur, kind); sub = 0; break; }
            const uint8_t *r = x->f + rec;
            float org[3] = { 0, 0, 0 }, radius = 0;
            int matn, joint = -1, anim = -1;
            if (kind == 2) {
                for (int k = 0; k < 3; k++) org[k] = ex_bef32(r + 4 * k);
                joint = ex_be16(r + 12); matn = ex_be16s(r + 14); radius = ex_bef32(r + 24);
            } else if (kind == 21) {
                for (int k = 0; k < 3; k++) org[k] = ex_bef32(r + 4 * k);
                matn = ex_be16s(r + 12); radius = ex_bef32(r + 16);
            } else {
                anim = ex_be16(r); matn = ex_be16s(r + 2);
            }
            char nm[48];
            snprintf(nm, sizeof nm, "joint_%d", matn);
            int gi = add_gnode(x, nm);
            if (gi < 0) return;
            GNode *g = &x->gn[gi];
            int root = !has_pos;
            if (!root && kind != 1) { g->has_t = 1; memcpy(g->t, org, sizeof org); }
            CvBuf *e = &g->extras;
            ex_jp(e, "\"kind\":%u,\"matrix\":%d", kind, matn);
            if (kind != 1) {
                ex_jp(e, ",\"origin\":["); jf(e, org[0]); ex_jp(e, ","); jf(e, org[1]); ex_jp(e, ","); jf(e, org[2]); ex_jp(e, "]");
                ex_jp(e, ",\"radius\":"); jf(e, radius);
            }
            if (joint >= 0) ex_jp(e, ",\"joint\":%d", joint);
            if (anim >= 0) ex_jp(e, ",\"animation_part\":%d", anim);
            if (root) ex_jp(e, ",\"root\":true");
            put_parts(e, parts, np);
            add_to(x, gparent, gi, 0);
            x->npos++;
            if (!root && kind != 1) for (int k = 0; k < 3; k++) cacc[k] += org[k];
            cg = gi; chas = 1; canc = matn;
            break;
        }
        case 4: case 24: {
            size_t rs = kind == 24 ? 32 : 20;
            uint32_t rec = pnt(x, rec_raw, rs);
            if (!rec) { note(x, "node %06X (kind %u): bad record pointer", cur, kind); break; }
            do_dl_node(x, cur, rec, (int)kind, gparent, anc_mtx, in_far);
            break;
        }
        case 8: {
            uint32_t rec = pnt(x, rec_raw, 16);
            if (!rec) { note(x, "node %06X (level of detail): bad record pointer, subtree skipped", cur); sub = 0; break; }
            float nearv = ex_bef32(x->f + rec), farv = ex_bef32(x->f + rec + 4);
            char nm[48];
            snprintf(nm, sizeof nm, "lod_%g_%g", isfinite(nearv) ? (double)nearv : 0.0, isfinite(farv) ? (double)farv : 0.0);
            int gi = add_gnode(x, nm);
            if (gi < 0) return;
            GNode *g = &x->gn[gi];
            CvBuf *e = &g->extras;
            ex_jp(e, "\"kind\":8,\"lod\":{\"near\":"); jf(e, nearv); ex_jp(e, ",\"far\":"); jf(e, farv); ex_jp(e, "}");
            put_parts(e, parts, np);
            x->nlod++;
            if (nearv == 0.0f || in_far) {
                add_to(x, gparent, gi, 0);
            } else {
                /* a coarser level: detached, in scene "all" only, placed by the accumulated rest translation */
                g->has_t = 1; memcpy(g->t, acc, sizeof g->t);
                add_to(x, -1, gi, 1);
                cfar = 1;
            }
            cg = gi;
            break;
        }
        case 9: break;   /* draw-order split: both groups are children; a plain walk draws both */
        case 10: {
            uint32_t rec = pnt(x, rec_raw, 28);
            if (!rec) { note(x, "node %06X (bounding box): bad record pointer", cur); break; }
            const uint8_t *r = x->f + rec;
            int bpart = (int)ex_be32(r);
            float bb[6];
            for (int k = 0; k < 6; k++) bb[k] = ex_bef32(r + 4 + 4 * k);
            char nm[48];
            snprintf(nm, sizeof nm, "bbox_%d", bpart);
            int gi = add_gnode(x, nm);
            if (gi < 0) return;
            CvBuf *e = &x->gn[gi].extras;
            ex_jp(e, "\"kind\":10,\"bbox_part\":%d,\"bbox\":[", bpart);
            for (int k = 0; k < 6; k++) { if (k) ex_jp(e, ","); jf(e, bb[k]); }
            ex_jp(e, "]");
            put_parts(e, parts, np);
            add_to(x, gparent, gi, 0);
            x->nbbox++;
            if (parent == x->root_off && !x->bbox_a_ok) { memcpy(x->bbox_a, bb, sizeof bb); x->bbox_a_ok = 1; }
            else if (x->root_child && parent == x->root_child && !x->bbox_b_ok) { memcpy(x->bbox_b, bb, sizeof bb); x->bbox_b_ok = 1; }
            break;
        }
        case 12: {
            uint32_t rec = pnt(x, rec_raw, 40);
            if (!rec) { note(x, "node %06X (muzzle flash): bad record pointer", cur); break; }
            const uint8_t *r = x->f + rec;
            int gi = add_gnode(x, "muzzle_flash");
            if (gi < 0) return;
            CvBuf *e = &x->gn[gi].extras;
            uint32_t tp = ex_be32(r + 24);
            long tex = -1;
            if ((tp >> 24) == 5) {
                long to = (long)(tp & 0xFFFFFF) - 4L * x->nparts;
                if (to >= 0 && to % 12 == 0 && to / 12 < x->ntex) {
                    const uint8_t *te = x->f + 4 * (size_t)x->nparts + to;
                    if (ex_be32(te) >> 24 == 0) tex = (long)(ex_be32(te) & 0xFFFF);
                }
            }
            ex_jp(e, "\"kind\":12,\"muzzle_flash\":{\"offset\":["); jf(e, ex_bef32(r)); ex_jp(e, ","); jf(e, ex_bef32(r + 4));
            ex_jp(e, ","); jf(e, ex_bef32(r + 8)); ex_jp(e, "],\"size\":["); jf(e, ex_bef32(r + 12)); ex_jp(e, ","); jf(e, ex_bef32(r + 16));
            ex_jp(e, ","); jf(e, ex_bef32(r + 20)); ex_jp(e, "],\"texture\":%ld,\"scale\":", tex); jf(e, ex_bef32(r + 28)); ex_jp(e, "}");
            put_parts(e, parts, np);
            add_to(x, gparent, gi, 0);
            x->nmuzzle++;
            break;
        }
        case 18: {
            uint32_t rec = pnt(x, rec_raw, 8);
            if (!rec) { note(x, "node %06X (switch): bad record pointer, subtree skipped", cur); sub = 0; break; }
            char nm[48];
            if (np) snprintf(nm, sizeof nm, "switch_%d", parts[0]);
            else snprintf(nm, sizeof nm, "switch_o%06X", cur);
            int gi = add_gnode(x, nm);
            if (gi < 0) return;
            ex_jp(&x->gn[gi].extras, "\"kind\":18,\"switch\":true,\"default\":\"visible\"");
            put_parts(&x->gn[gi].extras, parts, np);
            add_to(x, gparent, gi, 0);
            x->nswitch++;
            cg = gi;
            break;
        }
        case 22: {
            uint32_t rec = pnt(x, rec_raw, 16);
            if (!rec) { note(x, "node %06X (star flash): bad record pointer", cur); break; }
            int gi = add_gnode(x, "star_flash");
            if (gi < 0) return;
            ex_jp(&x->gn[gi].extras, "\"kind\":22,\"star_flash\":{\"arms\":%d}", (int)ex_be32(x->f + rec));
            put_parts(&x->gn[gi].extras, parts, np);
            add_to(x, gparent, gi, 0);
            x->nstar++;
            break;
        }
        default:
            if (kind < 32) x->skipped_kind[kind]++;
            x->nskipped_kinds++;
            sub = 0;
            break;
        }
        if (sub && child) walk(x, child, cg, chas, canc, cacc, cfar, depth + 1);
        cur = next; go = next != 0;
    }
}

/* ---- output ---- */

static void node_json(const MX *x, CvBuf *out, int i)
{
    const GNode *g = &x->gn[i];
    ex_jp(out, "%s{\"name\":", i ? "," : "");
    ex_jstr(out, g->name);
    if (g->mesh >= 0) ex_jp(out, ",\"mesh\":%d", g->mesh);
    if (g->has_t) { ex_jp(out, ",\"translation\":["); jf(out, g->t[0]); ex_jp(out, ","); jf(out, g->t[1]); ex_jp(out, ","); jf(out, g->t[2]); ex_jp(out, "]"); }
    if (g->nk) {
        ex_jp(out, ",\"children\":[");
        for (size_t k = 0; k < g->nk; k++) ex_jp(out, "%s%d", k ? "," : "", g->kids[k]);
        ex_jp(out, "]");
    }
    ex_jp(out, ",\"extras\":{");
    cv_buf_put(out, g->extras.p, g->extras.len);
    ex_jp(out, "}}");
}

static void x_free(MX *x)
{
    for (size_t i = 0; i < x->ngn; i++) { cv_buf_free(&x->gn[i].extras); free(x->gn[i].kids); }
    free(x->gn); free(x->roots); free(x->fars); free(x->seen);
    cv_buf_free(&x->meshes); cv_buf_free(&x->notes); gw_free(&x->g);
    ex_matctx_free(&x->mc);
}

typedef struct { int failed; uint64_t tri_def, tri_all; int npos; char msg[1400]; } ModelResult;

static int cmp_int(const void *a, const void *b) { return *(const int *)a - *(const int *)b; }

/* Exports one model; appends its models.json entry to `index`. */
static int export_model(const ModelEnt *me, const uint8_t *rom, size_t rom_size, const char *region, ExTexCache *tc,
                        const char *out_dir, CvBuf *index, ModelResult *res)
{
    MX *x = (MX *)calloc(1, sizeof *x);
    CvBuf fbuf, json, tex_json;
    MatOut mo;
    char dir[1100], path[1200];
    int fail = 0;
    memset(&fbuf, 0, sizeof fbuf); memset(&json, 0, sizeof json); memset(&tex_json, 0, sizeof tex_json);
    ex_matout_init(&mo);
    if (!x) { snprintf(res->msg, sizeof res->msg, "out of memory"); res->failed = 1; return 1; }
    x->me = me; x->region = region; x->tc = tc;
    x->nparts = me->nparts; x->ntex = me->ntextures; x->nmat = me->nmatrices;
    x->mc.tc = tc; x->mc.env_rule = 0; x->mc.normal_mask = GEOM_LIGHTING; x->mc.emit_decal = 1;
    x->screen_model = is_screen_model(me->name);

#define FAIL(...) do { snprintf(res->msg, sizeof res->msg, __VA_ARGS__); fail = 1; goto done; } while (0)

    snprintf(dir, sizeof dir, "%s/%s", out_dir, me->dir);
    if (!me->row || me->row->size < 16 || (size_t)me->row->off + me->row->size > rom_size) FAIL("%s: file outside the ROM", me->file);
    if (cv_rz_inflate(rom + me->row->off, me->row->size, &fbuf)) FAIL("%s: does not decompress", me->file);
    x->f = fbuf.p; x->fsize = fbuf.len;
    x->root_off = 4u * (uint32_t)x->nparts + 12u * (uint32_t)x->ntex;
    if ((size_t)x->root_off + 24 > x->fsize) FAIL("%s: root node outside the file", me->file);
    x->seen = (uint8_t *)calloc(x->fsize + 1, 1);
    if (!x->seen) FAIL("out of memory");
    x->root_child = (ex_be32(x->f + x->root_off + 20) >> 24) == 5 ? (ex_be32(x->f + x->root_off + 20) & 0xFFFFFFu) : 0;

    /* texture table: bank numbers, embedded images */
    int nembedded = 0;
    {
        CvBuf *t = &tex_json;
        for (int i = 0; i < x->ntex; i++) {
            const uint8_t *te = x->f + 4 * (size_t)x->nparts + 12 * (size_t)i;
            if (4 * (size_t)x->nparts + 12 * (size_t)(i + 1) > x->fsize) break;
            uint32_t id = ex_be32(te);
            ex_jp(t, "%s{", i ? "," : "");
            if ((id >> 24) == 0) ex_jp(t, "\"texture\":%u", id & 0xFFFF);
            else if ((id >> 24) == 5) { ex_jp(t, "\"embedded\":%u", id & 0xFFFFFF); nembedded++; }
            else ex_jp(t, "\"id\":%u", id);
            ex_jp(t, ",\"width\":%u,\"height\":%u,\"lods\":%u,\"format\":%u,\"size\":%u,\"wrap_s\":%u,\"wrap_t\":%u}", te[4], te[5], te[6], te[7],
                  te[8], te[9], te[10]);
        }
    }

    {
        float zero[3] = { 0, 0, 0 };
        walk(x, x->root_off, -1, 0, -1, zero, 0, 0);
    }
    if (x->oom) FAIL("out of memory");
    if (!x->npos) note(x, "no position node in the tree (root kind %u): geometry exported without a joint", ex_be16(x->f + x->root_off) & 0xFF);
    if (!x->ngn) FAIL("%s: nothing reached from the root (kind %u)", me->file, ex_be16(x->f + x->root_off) & 0xFF);

    /* bounding box of the model (models.md): first among the root's children, else among its first child's children */
    int have_bbox = x->bbox_a_ok || x->bbox_b_ok;
    const float *bb = x->bbox_a_ok ? x->bbox_a : x->bbox_b;

    /* ---- notes ---- */
    if (nembedded)
        note(x, "front-end model: %d embedded image(s) (texture table entries with segment 5) are not exported; drawn with its own render state, untextured", nembedded);
    if (x->rtype_seen[0]) note(x, "render type 0 (the list sets its own state): %d node(s)", x->rtype_seen[0]);
    for (int k = 1; k < 8; k++) if (k != 4 && x->rtype_seen[k]) note(x, "render type %d: %d node(s) (starting state of type 4 assumed)", k, x->rtype_seen[k]);
    if (x->mixed_tris) note(x, "%llu triangle(s) use vertices loaded under different matrices (attached to the node's ancestor position node)", (unsigned long long)x->mixed_tris);
    if (x->off_anc_tris) note(x, "%llu triangle(s) use a matrix other than their nearest ancestor position node's", (unsigned long long)x->off_anc_tris);
    if (x->dropped_tris) note(x, "%llu triangle(s) dropped (empty or out-of-range vertex slot)", (unsigned long long)x->dropped_tris);
    if (x->dropped_vtx) note(x, "%llu vertex load(s) dropped (bad segment, slot or range)", (unsigned long long)x->dropped_vtx);
    if (x->bad_ptrs) note(x, "%llu pointer(s) outside the file or not segment 5", (unsigned long long)x->bad_ptrs);
    if (x->no_end) note(x, "%llu list(s) without an end-of-list", (unsigned long long)x->no_end);
    if (x->calls) note(x, "%llu call/jump command(s) (0x06) not followed", (unsigned long long)x->calls);
    if (x->nskipped_kinds) {
        for (int k = 0; k < 32; k++) if (x->skipped_kind[k]) note(x, "node kind %d: %d node(s) skipped with their subtree", k, x->skipped_kind[k]);
    }
    for (int op = 0; op < 256; op++)
        if (x->census[op] && !op_known((unsigned)op)) note(x, "unknown display-list opcode 0x%02X x%llu (skipped)", op, (unsigned long long)x->census[op]);
    if (x->nlod) note(x, "%d level-of-detail node(s): coarser levels are in scene \"all\" only", x->nlod);
    if (x->nswitch) note(x, "%d switch node(s), drawn (default visible)", x->nswitch);
    if (x->nmuzzle || x->nstar) note(x, "muzzle flash x%d, star flash x%d: run-time geometry, not generated", x->nmuzzle, x->nstar);
    if (x->nscreens) note(x, "%d screen node(s) (monitor parts 0-3): the file's own list, not a live image", x->nscreens);
    if (x->mc.nm) {
        /* every textured material's texture should be in the model's texture table */
        for (size_t m = 0; m < x->mc.nm; m++) {
            const MatKey *k = &x->mc.mats[m].k;
            if (k->texnum == 0xFFFFFFFFu) continue;
            int found = 0;
            for (int i = 0; i < x->ntex; i++) {
                const uint8_t *te = x->f + 4 * (size_t)x->nparts + 12 * (size_t)i;
                if ((size_t)(4 * x->nparts + 12 * (i + 1)) <= x->fsize && (ex_be32(te) >> 24) == 0 && (ex_be32(te) & 0xFFFF) == k->texnum) found = 1;
            }
            if (!found) { note(x, "texture %u is referenced by a list but not in the texture table", k->texnum); break; }
        }
        for (size_t m = 0; m < x->mc.nm; m++)
            if (x->mc.mats[m].k.decode_fail) { note(x, "texture %u could not be decoded: %s", x->mc.mats[m].k.texnum, x->mc.mats[m].err); break; }
    }

    /* ---- textures + materials ---- */
    mo.pack_depth = 2;   /* <out>/models/<name>/<name>.gltf -> ../../textures (the pack root) */
    if (ex_emit_materials(&mo, &x->mc)) FAIL("out of memory");

    /* ---- the glTF ---- */
    ex_jp(&json, "{\"asset\":{\"version\":\"2.0\",\"generator\":\"project-dam export\",\"extras\":{\"exporter_version\":%d,\"rom_region\":",
          EX_EXPORTER_VERSION);
    ex_jstr(&json, region);
    ex_jp(&json, ",\"model\":%d,\"file\":", me->num); ex_jstr(&json, me->file);
    ex_jp(&json, ",\"name\":"); ex_jstr(&json, me->name);
    ex_jp(&json, ",\"base_scale\":"); jf(&json, me->base_scale);
    ex_jp(&json, ",\"bounding_radius\":"); jf(&json, me->radius);
    ex_jp(&json, ",\"nparts\":%d,\"nmatrices\":%d,\"ntextures\":%d,\"units\":\"model units (world = base_scale x extra scale x model)\"",
          me->nparts, me->nmatrices, me->ntextures);
    ex_jp(&json, ",\"vertex_color_encoding\":\"linear (converted from the game's sRGB bytes)\"");
    ex_jp(&json, ",\"skeleton\":{\"size\":%d,\"joints\":[", me->skel_size);
    for (int j = 0; j < me->njoints; j++) ex_jp(&json, "%s[%u,%u,%u]", j ? "," : "", me->joints[j][0], me->joints[j][1], me->joints[j][2]);
    ex_jp(&json, "]}");
    if (have_bbox) { ex_jp(&json, ",\"bbox\":["); for (int k = 0; k < 6; k++) { if (k) ex_jp(&json, ","); jf(&json, bb[k]); } ex_jp(&json, "]"); }
    ex_jp(&json, ",\"texture_table\":["); cv_buf_put(&json, tex_json.p, tex_json.len); ex_jp(&json, "]");
    ex_jp(&json, ",\"notes\":["); cv_buf_put(&json, x->notes.p, x->notes.len); ex_jp(&json, "]");
    ex_jp(&json, "}},\"extensionsUsed\":[\"KHR_materials_unlit\"],\"scene\":0,\"scenes\":[{\"name\":\"default\",\"nodes\":[");
    for (size_t i = 0; i < x->nroots; i++) ex_jp(&json, "%s%d", i ? "," : "", x->roots[i]);
    ex_jp(&json, "]},{\"name\":\"all\",\"nodes\":[");
    for (size_t i = 0; i < x->nroots; i++) ex_jp(&json, "%s%d", i ? "," : "", x->roots[i]);
    for (size_t i = 0; i < x->nfars; i++) ex_jp(&json, "%s%d", (i || x->nroots) ? "," : "", x->fars[i]);
    ex_jp(&json, "]}],\"nodes\":[");
    for (size_t i = 0; i < x->ngn; i++) node_json(x, &json, (int)i);
    ex_jp(&json, "]");
    if (x->nmesh) { ex_jp(&json, ",\"meshes\":["); cv_buf_put(&json, x->meshes.p, x->meshes.len); ex_jp(&json, "]"); }
    if (x->mc.nm) { ex_jp(&json, ",\"materials\":["); cv_buf_put(&json, mo.mats.p, mo.mats.len); ex_jp(&json, "]"); }
    ex_emit_texture_arrays(&json, &mo);
    if (x->g.naccs) {
        ex_jp(&json, ",\"accessors\":["); cv_buf_put(&json, x->g.accs.p, x->g.accs.len);
        ex_jp(&json, "],\"bufferViews\":["); cv_buf_put(&json, x->g.views.p, x->g.views.len); ex_jp(&json, "]");
        ex_jp(&json, ",\"buffers\":[{\"uri\":\"%s.bin\",\"byteLength\":%zu}]", me->dir, x->g.bin.len);
    }
    ex_jp(&json, "}\n");

    if (ex_mkdir_p(dir)) FAIL("cannot create %s", dir);
    snprintf(path, sizeof path, "%s/%s.gltf", dir, me->dir);
    if (ex_write_file(path, json.p, json.len)) FAIL("cannot write %s", path);
    snprintf(path, sizeof path, "%s/%s.bin", dir, me->dir);
    if (ex_write_file(path, x->g.bin.p, x->g.bin.len)) FAIL("cannot write %s", path);

    /* ---- the models.json entry ---- */
    {
        int *used = NULL; size_t nused = 0, capu = 0;
        for (size_t m = 0; m < x->mc.nm; m++) {
            const MatKey *k = &x->mc.mats[m].k;
            if (!k->textured) continue;
            size_t i;
            for (i = 0; i < nused; i++) if (used[i] == (int)k->texnum) break;
            if (i == nused) {
                if (ex_grow((void **)&used, &capu, nused + 1, sizeof *used)) { free(used); FAIL("out of memory"); }
                used[nused++] = (int)k->texnum;
            }
        }
        if (nused) qsort(used, nused, sizeof *used, cmp_int);
        CvBuf *ix = index;
        ex_jp(ix, "{\"model\":%d,\"file\":", me->num); ex_jstr(ix, me->file);
        ex_jp(ix, ",\"name\":"); ex_jstr(ix, me->name);
        ex_jp(ix, ",\"gltf\":\"%s/%s.gltf\",\"base_scale\":", me->dir, me->dir); jf(ix, me->base_scale);
        ex_jp(ix, ",\"bbox\":");
        if (have_bbox) { ex_jp(ix, "["); for (int k = 0; k < 6; k++) { if (k) ex_jp(ix, ","); jf(ix, bb[k]); } ex_jp(ix, "]"); }
        else ex_jp(ix, "null");
        ex_jp(ix, ",\"bounding_radius\":"); jf(ix, me->radius);
        ex_jp(ix, ",\"nodes\":%d,\"triangles\":%llu,\"triangles_all\":%llu,\"textures\":[", x->npos, (unsigned long long)x->tri_def,
              (unsigned long long)x->tri_all);
        for (size_t i = 0; i < nused; i++) ex_jp(ix, "%s%d", i ? "," : "", used[i]);
        ex_jp(ix, "],\"nmatrices\":%d,\"status\":\"%s\",\"notes\":[", me->nmatrices, nembedded ? "exported_untextured" : "exported");
        cv_buf_put(ix, x->notes.p, x->notes.len);
        ex_jp(ix, "]}");
        free(used);
    }
    res->tri_def = x->tri_def; res->tri_all = x->tri_all; res->npos = x->npos;

done:
    cv_buf_free(&fbuf); cv_buf_free(&json); cv_buf_free(&tex_json);
    ex_matout_free(&mo);
    x_free(x);
    free(x);
    res->failed = fail;
    return fail;
#undef FAIL
}

/* ------------------------------------------------------------------------ */
/* all models                                                               */
/* ------------------------------------------------------------------------ */

int ex_export_models(const char *rom_path, const char *out_dir, ExLogFn log, void *user)
{
    uint8_t *rom = NULL;
    size_t rom_size = 0;
    const char *region = NULL;
    const CvRegionData *rd = NULL;
    char msg[300];
    if (ex_open_rom(rom_path, log, user, &rom, &rom_size, &region, &rd)) return 1;
    if (ex_mkdir_p(out_dir)) { ex_logf(log, user, "cannot create %s", out_dir); free(rom); return 1; }

    ModelEnt *me = NULL;
    if (load_table(rom, rom_size, region, rd, &me, msg, sizeof msg)) {
        ex_logf(log, user, "models refused for region %s: %s", region, msg);
        free(rom);
        return 1000;
    }
    /* the directory name is the file name without the trailing Z; a name that is not unique falls back to model_NNN */
    for (int i = 0; i < N_MODELS; i++)
        for (int j = 0; j < i; j++)
            if (!strcmp(me[i].dir, me[j].dir)) snprintf(me[i].dir, sizeof me[i].dir, "model_%03d", i);

    ExTexCache tc;
    ex_texcache_open(&tc, rom, rom_size, region, log, user);
    CvBuf index;
    memset(&index, 0, sizeof index);
    int nfailed = 0, nuntex = 0;
    uint64_t gtris = 0;
    for (int i = 0; i < N_MODELS; i++) {
        ModelResult res;
        memset(&res, 0, sizeof res);
        if (i) ex_jp(&index, ",\n");
        if (export_model(&me[i], rom, rom_size, region, &tc, out_dir, &index, &res)) {
            nfailed++;
            ex_logf(log, user, "model %3d %-24s FAILED: %s", i, me[i].file, res.msg);
            ex_jp(&index, "{\"model\":%d,\"file\":", i); ex_jstr(&index, me[i].file);
            ex_jp(&index, ",\"status\":\"failed\",\"notes\":["); ex_jstr(&index, res.msg); ex_jp(&index, "]}");
        } else {
            gtris += res.tri_def;
        }
        /* the texture cache is cleared now and then: decoded textures are cheap to redo, but not unbounded */
        if (tc.nt > 600) ex_texcache_clear(&tc);
    }
    (void)nuntex;
    {
        CvBuf full;
        char path[1200];
        memset(&full, 0, sizeof full);
        ex_jp(&full, "{\"exporter_version\":%d,\"rom_region\":", EX_EXPORTER_VERSION);
        ex_jstr(&full, region);
        ex_jp(&full, ",\"models\":[\n");
        cv_buf_put(&full, index.p, index.len);
        ex_jp(&full, "\n]}\n");
        snprintf(path, sizeof path, "%s/models.json", out_dir);
        if (ex_write_file(path, full.p, full.len)) { ex_logf(log, user, "cannot write %s", path); nfailed++; }
        cv_buf_free(&full);
    }
    ex_logf(log, user, "models: %d exported, %d failed, %llu triangles (default scene) -> %s", N_MODELS - nfailed, nfailed,
            (unsigned long long)gtris, out_dir);
    cv_buf_free(&index);
    ex_texcache_close(&tc);
    model_ents_free(me, N_MODELS);
    free(rom);
    return nfailed;
}
