/* char_export.c -- static character export (docs/specs/characters.md): the character table (guard bodies, heads),
 * the shared skeleton and the animation data -> one skinned glTF 2.0 per body, one rigid glTF per head and
 * characters.json.
 *
 * CLEAN ROOM: written from docs/specs/characters.md (and models.md, level-decor.md, textures.md, display-lists.md,
 * which it builds on) only, plus the converter library's file table, rarezip inflate and ROM check. See export.h.
 *
 * Output layout (out_dir = <export dir>/characters):
 *   <out_dir>/<file name without the trailing Z>/<same>.gltf + .bin     (one per body / head; images point into ../../textures/, the pack's own)
 *   <out_dir>/animations/<name>.gltf + .bin     (one animation on the guard rig, shared by every body)
 *   <out_dir>/characters.json
 * Each body carries idle only (its rest pose); set GE007_EXPORT_ALL_ANIMATIONS=1 in the environment to put every
 * animation into every body as well.
 */
#include "export_common.h"
#include "char_labels.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------ */
/* the per-region constants of characters.md (the only per-region place)    */
/* ------------------------------------------------------------------------ */

static const struct {
    const char *region;
    uint32_t cdata_rom, base, ctab, skel, atab;
    uint32_t fr_off, fr_end, hd_off, hd_end;
} k_creg[] = {
    { "ntsc-final", 0x21990u, 0x80020D90u, 0x1D080u, 0x1C670u, 0x8FDCu, 0x124AC0u, 0x28E980u, 0x28E980u, 0x29D160u },
    { "pal-final",  0x1F850u, 0x8001EC50u, 0x18E10u, 0x18540u, 0x666Cu, 0x111A30u, 0x27B8F0u, 0x27B8F0u, 0x28A0D0u },
    { "jpn-final",  0x219D0u, 0x80020DD0u, 0x1D070u, 0x1C660u, 0x8FDCu, 0x1257E0u, 0x28F6A0u, 0x28F6A0u, 0x29DE80u },
};
#define N_CHARS       80
#define N_ANIMS       183
#define N_MATS        64
#define CDATA_MAX_OUT (8u * 1024u * 1024u)
#define PI_           3.14159265358979323846

enum { K_BODY = 0, K_HEAD = 1, K_HAND = 2 };

typedef struct {
    int id, kind;
    char file[64], name[64], dir[64];
    float body_scale, pov;
    int is_male, has_head;
    int nparts, nmatrices, ntextures;
    float radius;
    uint32_t skel;
    const CvFileRow *row;
    const char *label;
} CharEnt;

/* joint ids of the guard skeleton (characters.md §The skeleton) */
static const char *const k_jname[16] = { "root", "pelvis", "spine", "neck", "l_shoulder", "r_shoulder", "l_elbow", "r_elbow",
                                         "l_wrist", "r_wrist", "l_hip", "r_hip", "l_knee", "r_knee", "l_ankle", "r_ankle" };
static const int k_jparent[16] = { -1, 0, 1, 2, 2, 2, 4, 5, 6, 7, 1, 1, 10, 11, 12, 13 };

/* ------------------------------------------------------------------------ */
/* animations                                                               */
/* ------------------------------------------------------------------------ */

typedef struct {
    int used;
    int nframes, width, loop, root_bits, frame_bits;
    uint32_t f0, desc, stream;
    double travel_x, travel_z;
    int fps;
    char name[48];
} AnimEnt;

typedef struct {
    const uint8_t *frames, *heads;
    size_t frames_len, heads_len;
    AnimEnt a[N_ANIMS];
    int skel_a[16];
    int nused;
} AnimData;

#include "anim_names.h"   /* generated from assets/animationtable_data.h: header offset -> name (port/export/CMakeLists.txt) */

/* The name of animation `n` whose header lies at `off` in the headers range: the decompilation's name for that offset,
 * else the few the spec lists by number, else anim_<n>. */
static void anim_name(int n, uint32_t off, char *out, size_t sz)
{
    for (size_t i = 0; i < sizeof k_anim_names / sizeof k_anim_names[0]; i++)
        if (k_anim_names[i].off == off) { snprintf(out, sz, "%s", k_anim_names[i].name); return; }
    static const struct { int n; const char *s; } known[] = {
        { 0, "idle" }, { 1, "fire_standing" }, { 26, "death_forward_face_down" }, { 40, "walking" }, { 41, "sprinting" },
        { 42, "running" }, { 106, "idle_unarmed" }, { 107, "walking_unarmed" }, { 114, "walking_female" },
        { 115, "running_female" }, { 182, "credits_natalya_kissing" },
    };
    for (size_t i = 0; i < sizeof known / sizeof known[0]; i++)
        if (known[i].n == n) { snprintf(out, sz, "%s", known[i].s); return; }
    snprintf(out, sz, "anim_%d", n);
}

static uint32_t getbits(const uint8_t *b, size_t bitpos, int n)
{
    uint32_t v = 0;
    for (int i = 0; i < n; i++) {
        size_t p = bitpos + (size_t)i;
        v = (v << 1) | ((b[p >> 3] >> (7 - (p & 7))) & 1u);
    }
    return v;
}

/* root-motion field k (0 x, 1 y, 2 z, 3 heading) of frame f, as the raw 16-bit value */
static uint32_t root_field(const AnimData *ad, const AnimEnt *a, int k, int f)
{
    const uint8_t *d = ad->heads + a->desc + 6 * (size_t)k;
    unsigned boff = ex_be16(d), cnt = d[2], base = ex_be16(d + 4);
    if (cnt == 0) return base & 0xFFFFu;
    uint32_t raw = getbits(ad->heads + a->stream, (size_t)a->root_bits * (size_t)f + boff, (int)cnt);
    if (cnt < 16 && ((raw >> (cnt - 1)) & 1u)) raw |= ~0u << cnt;
    return (raw + base) & 0xFFFFu;
}

static void anim_root(const AnimData *ad, int n, int f, int *x, int *y, int *z, double *heading)
{
    const AnimEnt *a = &ad->a[n];
    *x = (int16_t)root_field(ad, a, 0, f);
    *y = (int16_t)root_field(ad, a, 1, f);
    *z = (int16_t)root_field(ad, a, 2, f);
    *heading = (double)root_field(ad, a, 3, f) * 2.0 * PI_ / 65536.0;
}

/* rotation angles (radians) of channels 0..44 of frame f */
static void anim_angles(const AnimData *ad, int n, int f, double ang[45])
{
    const AnimEnt *a = &ad->a[n];
    const uint8_t *fp = ad->frames + a->f0 + (size_t)f * (size_t)(a->frame_bits / 8);
    for (int c = 0; c < 45; c++) {
        uint32_t v = getbits(fp, (size_t)c * (size_t)a->width, a->width) << (16 - a->width);
        ang[c] = (double)v * 2.0 * PI_ / 65536.0;
    }
}

/* Reads the animation table and every header. 0 ok, -1 refused (msg says why). */
static int anim_load(AnimData *ad, const uint8_t *rom, size_t rom_size, size_t r, const uint8_t *cd, size_t cdlen, char *msg,
                     size_t msgsz)
{
    memset(ad, 0, sizeof *ad);
    if (k_creg[r].fr_end > rom_size || k_creg[r].hd_end > rom_size || k_creg[r].fr_off >= k_creg[r].fr_end ||
        k_creg[r].hd_off >= k_creg[r].hd_end) {
        snprintf(msg, msgsz, "animation ranges outside the ROM");
        return -1;
    }
    ad->frames = rom + k_creg[r].fr_off; ad->frames_len = k_creg[r].fr_end - k_creg[r].fr_off;
    ad->heads = rom + k_creg[r].hd_off;  ad->heads_len = k_creg[r].hd_end - k_creg[r].hd_off;
    if ((size_t)k_creg[r].atab + 4 * (N_ANIMS + 1) > cdlen) { snprintf(msg, msgsz, "animation table outside cdata"); return -1; }
    const uint8_t *t = cd + k_creg[r].atab;
    if (ex_be32(t + 4 * N_ANIMS) != 0) { snprintf(msg, msgsz, "animation table is not terminated after %d entries", N_ANIMS); return -1; }
    int unused = 0;
    for (int i = 0; i < N_ANIMS; i++) {
        AnimEnt *a = &ad->a[i];
        uint32_t w = ex_be32(t + 4 * (size_t)i);
        anim_name(i, w, a->name, sizeof a->name);
        if (w == 1) { unused++; continue; }
        if ((size_t)w + 20 > ad->heads_len) { snprintf(msg, msgsz, "animation %d: header outside the headers range", i); return -1; }
        const uint8_t *h = ad->heads + w;
        a->f0 = ex_be32(h); a->nframes = ex_be16(h + 4); a->width = h[6]; a->loop = h[7] & 1;
        a->desc = ex_be32(h + 8); a->root_bits = ex_be16(h + 12); a->frame_bits = ex_be16(h + 14); a->stream = ex_be32(h + 16);
        if (a->width < 1 || a->width > 16 || a->frame_bits < 45 * a->width || (a->frame_bits & 7) || a->nframes < 1 ||
            (size_t)a->f0 + (size_t)a->nframes * (size_t)(a->frame_bits / 8) > ad->frames_len ||
            (size_t)a->desc + 24 > ad->heads_len ||
            (size_t)a->stream + ((size_t)a->root_bits * (size_t)a->nframes + 7) / 8 > ad->heads_len) {
            snprintf(msg, msgsz, "animation %d: header fields outside their ranges", i);
            return -1;
        }
        a->used = 1; ad->nused++;
        a->fps = (i == 0 || i == 106) ? 15 : 30;
        for (int f = 0; f < a->nframes; f++) {
            a->travel_x += (int16_t)root_field(ad, a, 0, f);
            a->travel_z += (int16_t)root_field(ad, a, 2, f);
        }
    }
    /* the header at offset 0x1C of the headers range is the idle animation: frame offset 0, 163 frames, 12 bits, 544 bits */
    {
        const uint8_t *h = ad->heads + 0x1C;
        if (ex_be32(t) != 0x1Cu || ex_be32(h) != 0 || ex_be16(h + 4) != 163 || h[6] != 12 || ex_be16(h + 14) != 544) {
            snprintf(msg, msgsz, "animation 0 is not the idle animation (163 frames, 12 bits, 544 bits per frame)");
            return -1;
        }
    }
    if (unused != 13 || ad->nused != 170) {
        snprintf(msg, msgsz, "%d unused and %d used animation slots, expected 13 and 170", unused, ad->nused);
        return -1;
    }
    return 0;
}

/* ---- quaternions (w, x, y, z) ---- */

typedef struct { double w, x, y, z; } Q;
static Q qmul(Q a, Q b)
{
    Q r = { a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z, a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x, a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w };
    return r;
}
static Q qaxis(int ax, double ang)
{
    Q q = { cos(ang / 2), 0, 0, 0 };
    double s = sin(ang / 2);
    if (ax == 0) q.x = s; else if (ax == 1) q.y = s; else q.z = s;
    return q;
}
static Q qnorm(Q q)
{
    double n = sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
    if (n > 0) { q.w /= n; q.x /= n; q.y /= n; q.z /= n; }
    return q;
}
/* L = Rz Ry Rx (Rx applied first) */
static Q euler_q(double rx, double ry, double rz) { return qnorm(qmul(qaxis(2, rz), qmul(qaxis(1, ry), qaxis(0, rx)))); }
/* the rotation halfway from identity along the shortest path */
static Q half_q(Q q)
{
    if (q.w < 0) { q.w = -q.w; q.x = -q.x; q.y = -q.y; q.z = -q.z; }
    Q h = { q.w + 1.0, q.x, q.y, q.z };
    return qnorm(h);
}
static double qdot(Q a, Q b) { return a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z; }
static Q qneg(Q q) { Q r = { -q.w, -q.x, -q.y, -q.z }; return r; }

/* ------------------------------------------------------------------------ */
/* the character table                                                      */
/* ------------------------------------------------------------------------ */

static int read_table(const uint8_t *cd, size_t cdlen, size_t r, const CvRegionData *rd, CharEnt *ce, char *msg, size_t msgsz)
{
    uint32_t base = k_creg[r].base, toff = k_creg[r].ctab, guard_skel = base + k_creg[r].skel;
    int nbody = 0, nhead = 0, nhand = 0, nhh = 0;
#define REFUSE(...) do { snprintf(msg, msgsz, __VA_ARGS__); return -1; } while (0)
    for (int i = 0; i <= N_CHARS; i++) {
        size_t eo = (size_t)toff + (size_t)i * 20;
        if (eo + 20 > cdlen) REFUSE("character table runs past the end of cdata at entry %d", i);
        const uint8_t *e = cd + eo;
        uint32_t hp = ex_be32(e), np = ex_be32(e + 4);
        if (i == N_CHARS) {
            if (hp != 0 || np != 0) REFUSE("character table has more than %d entries", N_CHARS);
            break;
        }
        if (hp == 0) REFUSE("character table ends after %d entries (expected %d)", i, N_CHARS);
        CharEnt *c = &ce[i];
        memset(c, 0, sizeof *c);
        c->id = i;
        if (hp < base || np < base || hp - base + 24 > cdlen || np - base >= cdlen) REFUSE("entry %d: header or name pointer outside cdata", i);
        {
            const char *nm = (const char *)cd + (np - base);
            size_t k = 0;
            while (np - base + k < cdlen && nm[k] && k < sizeof c->file - 1) k++;
            if (np - base + k >= cdlen || nm[k]) REFUSE("entry %d: file name not terminated", i);
            memcpy(c->file, nm, k); c->file[k] = 0;
            if (k < 3 || c->file[0] != 'C' || c->file[k - 1] != 'Z') REFUSE("entry %d: name \"%s\" is not C...Z", i, c->file);
            memcpy(c->name, c->file + 1, k - 2); c->name[k - 2] = 0;
            memcpy(c->dir, c->file, k - 1); c->dir[k - 1] = 0;
        }
        c->body_scale = ex_bef32(e + 8); c->pov = ex_bef32(e + 12);
        c->is_male = e[16]; c->has_head = e[17];
        c->row = cv_find_row(rd, c->file);
        if (!c->row) REFUSE("entry %d: %s has no row in the ROM file table", i, c->file);
        {
            const uint8_t *h = cd + (hp - base);
            c->skel = ex_be32(h + 4);
            c->nparts = ex_be16s(h + 12); c->nmatrices = ex_be16s(h + 14); c->radius = ex_bef32(h + 16); c->ntextures = ex_be16s(h + 22);
            if (c->nparts < 0 || c->nmatrices < 0 || c->ntextures < 0) REFUSE("entry %d: negative count in the header", i);
        }
        if (c->skel == guard_skel) { c->kind = K_BODY; nbody++; }
        else if (c->skel == 0) { c->kind = K_HEAD; nhead++; }
        else { c->kind = K_HAND; nhand++; }
        if (c->has_head && c->kind == K_BODY) nhh++;
        if (!strcmp(c->file, k_char_labels[i].file)) c->label = k_char_labels[i].label;
    }
    if (strcmp(ce[0].file, "CcamguardZ") || strcmp(ce[41].file, "Csuit_lf_handZ") || strcmp(ce[42].file, "CheadkarlZ") ||
        strcmp(ce[78].file, "CheadbrosnanZ") || strcmp(ce[79].file, "CspicebondZ"))
        REFUSE("entries 0 / 41 / 42 / 78 / 79 are %s / %s / %s / %s / %s, not CcamguardZ / Csuit_lf_handZ / CheadkarlZ / CheadbrosnanZ / CspicebondZ",
               ce[0].file, ce[41].file, ce[42].file, ce[78].file, ce[79].file);
    if (nbody != 42 || nhead != 37 || nhand != 1 || ce[41].kind != K_HAND)
        REFUSE("skeleton pointers give %d bodies, %d heads, %d other (expected 42, 37 and the hand, id 41)", nbody, nhead, nhand);
    if (nhh != 14) REFUSE("%d bodies have a built-in head, expected 14", nhh);
    return 0;
#undef REFUSE
}

/* ------------------------------------------------------------------------ */
/* one character file                                                       */
/* ------------------------------------------------------------------------ */

typedef struct {
    char name[56];
    int mesh, skin;
    int has_t; float t[3];
    int has_r; float r[4];            /* x y z w */
    CvBuf extras;
    int *kids; size_t nk, capk;
} GNode;

typedef struct { int16_t v[5]; uint8_t c[4]; int16_t mtx; } CKey;   /* 16 bytes, no padding */

typedef struct {
    int mat;
    float *uv; uint8_t *col; CKey *key;
    size_t nv, capv;
    uint32_t *idx; size_t ni, capi;
    uint32_t *ht; size_t hcap;
} CPrim;

typedef struct { CPrim *p; size_t np, cap, last; } CSet;

typedef struct {
    uint32_t noff;
    int kind, rtype, zone, far, specs, gn;
    CSet set;
    uint64_t tp, ts;
} DLRec;

typedef struct {
    const CharEnt *ce;
    const AnimData *ad;
    int body;
    ExTexCache *tc;

    const uint8_t *f; size_t fsize;
    int nparts, ntex, nmat;
    uint32_t root_off;
    uint8_t *seen;

    GNode *gn; size_t ngn, capgn;
    int *roots; size_t nroots, caproots;
    int *fars; size_t nfars, capfars;
    DLRec *dl; size_t ndl, capdl;
    GW g;
    MatCtx mc;

    /* skeleton */
    int have_j[16], jn[16], jh[16], jpar[16], jm0[16], jm1[16];
    float org[16][3];
    int mat_joint[N_MATS], mat_half[N_MATS];
    int head_attach, nshadow, nplace;

    uint64_t tri_def, tri_all, tri_specs, mixed_tris, dropped_tris, dropped_vtx, bad_ptrs, no_end, calls;
    uint64_t census[256];
    int nlod, nswitch, nbbox, nskipped_kinds, skipped_kind[32];
    int rtype_seen[8];
    int oom;
    CvBuf notes; int nnotes;
} CX;

static void note(CX *x, const char *fmt, ...)
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

static uint32_t pnt(CX *x, uint32_t raw, size_t need)
{
    if (!raw) return 0;
    uint32_t off = raw & 0x00FFFFFFu;
    if ((raw >> 24) != 5 || (size_t)off + need > x->fsize || off < 4u * (uint32_t)x->nparts) { x->bad_ptrs++; return 0; }
    return off;
}

static int add_gnode(CX *x, const char *name)
{
    if (ex_grow((void **)&x->gn, &x->capgn, x->ngn + 1, sizeof *x->gn)) { x->oom = 1; return -1; }
    GNode *n = &x->gn[x->ngn];
    memset(n, 0, sizeof *n);
    snprintf(n->name, sizeof n->name, "%s", name);
    n->mesh = -1; n->skin = -1;
    return (int)x->ngn++;
}

static void add_to(CX *x, int gparent, int gi, int far_root)
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

static int parts_of(const CX *x, uint32_t off, int *out, int max)
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

/* ---- primitives with a matrix in the vertex key ---- */

static CPrim *cset_find(CSet *ps, int mat, int *oom)
{
    if (ps->np && ps->p[ps->last].mat == mat) return &ps->p[ps->last];
    for (size_t i = 0; i < ps->np; i++)
        if (ps->p[i].mat == mat) { ps->last = i; return &ps->p[i]; }
    if (ex_grow((void **)&ps->p, &ps->cap, ps->np + 1, sizeof *ps->p)) { *oom = 1; return NULL; }
    CPrim *p = &ps->p[ps->np];
    memset(p, 0, sizeof *p);
    p->mat = mat;
    ps->last = ps->np++;
    return p;
}

static void cset_free(CSet *ps)
{
    for (size_t i = 0; i < ps->np; i++) {
        CPrim *p = &ps->p[i];
        free(p->uv); free(p->col); free(p->key); free(p->idx); free(p->ht);
    }
    free(ps->p);
    memset(ps, 0, sizeof *ps);
}

static uint32_t chash(const CKey *k)
{
    const uint8_t *b = (const uint8_t *)k;
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < sizeof *k; i++) { h ^= b[i]; h *= 16777619u; }
    return h;
}

static int cprim_rehash(CPrim *p, size_t ncap)
{
    uint32_t *nh = (uint32_t *)calloc(ncap, sizeof *nh);
    if (!nh) return -1;
    for (size_t i = 0; i < p->nv; i++) {
        size_t j = chash(&p->key[i]) & (ncap - 1);
        while (nh[j]) j = (j + 1) & (ncap - 1);
        nh[j] = (uint32_t)i + 1;
    }
    free(p->ht);
    p->ht = nh; p->hcap = ncap;
    return 0;
}

/* vertex index in the primitive (de-duplicated by position, UV, colour AND matrix), -1 on OOM */
static long cprim_vertex(CPrim *p, const ExVtx *v, const Material *m, const ExGfxState *s)
{
    int normals = (m->k.geom & m->normal_mask) != 0;
    int texgen_uv = m->k.textured && (m->k.geom & GEOM_TEXGEN) && normals;
    CKey k;
    memset(&k, 0, sizeof k);
    k.v[0] = v->x; k.v[1] = v->y; k.v[2] = v->z;
    if (m->k.textured && !texgen_uv) { k.v[3] = v->s; k.v[4] = v->t; }
    memcpy(k.c, v->c, 4);
    k.mtx = (int16_t)v->mtx;
    if ((p->nv + 1) * 2 > p->hcap && cprim_rehash(p, p->hcap ? p->hcap * 2 : 64)) return -1;
    size_t j = chash(&k) & (p->hcap - 1);
    while (p->ht[j]) {
        if (memcmp(&p->key[p->ht[j] - 1], &k, sizeof k) == 0) return (long)p->ht[j] - 1;
        j = (j + 1) & (p->hcap - 1);
    }
    if (p->nv + 1 > p->capv) {
        size_t nc = p->capv ? p->capv * 2 : 64;
        float *uv = (float *)realloc(p->uv, nc * 2 * sizeof(float));
        if (uv) p->uv = uv;
        uint8_t *col = (uint8_t *)realloc(p->col, nc * 4);
        if (col) p->col = col;
        CKey *key = (CKey *)realloc(p->key, nc * sizeof(CKey));
        if (key) p->key = key;
        if (!uv || !col || !key) return -1;
        p->capv = nc;
    }
    size_t i = p->nv++;
    p->key[i] = k;
    if (texgen_uv) {
        double nx = (int8_t)v->c[0] / 127.0, ny = (int8_t)v->c[1] / 127.0;
        p->uv[i * 2 + 0] = (float)(0.5 + 0.5 * nx);
        p->uv[i * 2 + 1] = (float)(0.5 - 0.5 * ny);
    } else if (m->k.textured) {
        double o = (m->k.offset == 2 && !m->expl) ? 0.5 : 0.0;
        p->uv[i * 2 + 0] = (float)((v->s / 32.0 * ex_scale_of(s->ks) - o) / m->tw);
        p->uv[i * 2 + 1] = (float)((v->t / 32.0 * ex_scale_of(s->kt) - o) / m->th);
    } else {
        p->uv[i * 2 + 0] = p->uv[i * 2 + 1] = 0.f;
    }
    memcpy(&p->col[i * 4], v->c, 4);
    p->ht[j] = (uint32_t)i + 1;
    return (long)i;
}

/* ---- the display lists ---- */

/* render type 3 (characters.md): the same starting state as models.md's render type 4 (primary opaque, secondary
 * translucent), both lists in one pass */
static void render_type_state(ExGfxState *s, int list)
{
    uint32_t w0 = (2u << 20) | (13u << 15) | (2u << 12) | (0u << 9) | (0u << 5) | 4u;
    uint32_t w1 = (1u << 28) | (15u << 24) | (0u << 21) | (4u << 18) | (1u << 15) | (1u << 12) | (1u << 9) | (7u << 6) | (7u << 3) | 3u;
    s->omh = 1u << 20;
    s->c0 = w0; s->c1 = w1;
    s->oml = list ? 0xC41049D8u : 0xC4112078u;
}

static void emit_tri(CX *x, CSet *ps, ExGfxState *s, int list, uint64_t *ntri, int a, int b, int c)
{
    if (a > 15 || b > 15 || c > 15 || !s->cache[a].valid || !s->cache[b].valid || !s->cache[c].valid ||
        s->cache[a].mtx < 0 || s->cache[a].mtx >= x->nmat || s->cache[b].mtx < 0 || s->cache[b].mtx >= x->nmat ||
        s->cache[c].mtx < 0 || s->cache[c].mtx >= x->nmat) {
        x->dropped_tris++;
        return;
    }
    if (s->mat < 0) {
        s->mat = ex_resolve_material(&x->mc, s, list);
        if (s->mat < 0) { x->oom = 1; return; }
    }
    CPrim *p = cset_find(ps, s->mat, &x->oom);
    if (!p) return;
    const Material *m = &x->mc.mats[s->mat];
    long ia = cprim_vertex(p, &s->cache[a], m, s);
    long ib = cprim_vertex(p, &s->cache[b], m, s);
    long ic = cprim_vertex(p, &s->cache[c], m, s);
    if (ia < 0 || ib < 0 || ic < 0) { x->oom = 1; return; }
    if (ex_grow((void **)&p->idx, &p->capi, p->ni + 3, sizeof *p->idx)) { x->oom = 1; return; }
    p->idx[p->ni++] = (uint32_t)ia; p->idx[p->ni++] = (uint32_t)ib; p->idx[p->ni++] = (uint32_t)ic;
    (*ntri)++;
    if (s->cache[a].mtx != s->cache[b].mtx || s->cache[a].mtx != s->cache[c].mtx) x->mixed_tris++;
}

/* One list. `s` is the state of the whole node: the vertex cache and the matrix in force carry from the primary list
 * into the secondary one (characters.md: "Primary, then Secondary, with one state"); everything else restarts. */
static void run_list(CX *x, CSet *ps, ExGfxState *sp, uint32_t loff, int list, uint32_t varr, int rtype, uint64_t *ntri)
{
    ExGfxState *s = sp;
    s->ks = s->kt = 0xFFFF;
    s->mat = -1;
    s->have_tex = 0;
    s->geom = 0x2000u;                          /* cull back faces */
    if (rtype != 0) render_type_state(s, list);
    size_t maxn = (x->fsize - loff) / 8;
    int ended = 0;
    for (size_t i = 0; i < maxn && !x->oom; i++) {
        const uint8_t *c = x->f + loff + i * 8;
        uint32_t w0 = ex_be32(c), w1 = ex_be32(c + 4);
        unsigned op = w0 >> 24;
        x->census[op]++;
        if (op == 0xB8) { ended = 1; break; }
        switch (op) {
        case 0x01: s->mtx = (int)((w1 & 0xFFFFFFu) / 64u); break;
        case 0x04: {
            unsigned count = (w0 & 0xFFFF) / 16, first = (w0 >> 16) & 0xF, seg = w1 >> 24;
            size_t off = w1 & 0xFFFFFFu;
            if (seg != 4 || !varr || first + count > 16 || (size_t)varr + off + (size_t)count * 16 > x->fsize) { x->dropped_vtx++; break; }
            for (unsigned k = 0; k < count; k++) {
                const uint8_t *v = x->f + varr + off + (size_t)k * 16;
                ExVtx *cv = &s->cache[first + k];
                cv->x = ex_be16s(v); cv->y = ex_be16s(v + 2); cv->z = ex_be16s(v + 4);
                cv->s = ex_be16s(v + 8); cv->t = ex_be16s(v + 10);
                memcpy(cv->c, v + 12, 4);
                cv->valid = 1; cv->mtx = s->mtx;
            }
            break;
        }
        case 0xBF:
            emit_tri(x, ps, s, list, ntri, (int)(((w1 >> 16) & 0xFF) / 10), (int)(((w1 >> 8) & 0xFF) / 10), (int)((w1 & 0xFF) / 10));
            break;
        case 0xB1:
            for (int k = 0; k < 4; k++) {
                int a = (int)((w1 >> (8 * k)) & 0xF), b = (int)((w1 >> (8 * k + 4)) & 0xF), cc = (int)((w0 >> (4 * k)) & 0xF);
                if (!a && !b && !cc) continue;
                emit_tri(x, ps, s, list, ntri, a, b, cc);
            }
            break;
        case 0x06: x->calls++; break;
        default: ex_dl_state(s, op, w0, w1); break;
        }
    }
    if (!ended) x->no_end++;
}

static void do_dl_node(CX *x, uint32_t noff, uint32_t rec, int gparent, int zone, int in_far, int in_specs)
{
    const uint8_t *r = x->f + rec;
    uint32_t prim_l = pnt(x, ex_be32(r), 8), sec_l = pnt(x, ex_be32(r + 4), 8);
    uint32_t varr = pnt(x, ex_be32(r + 8), 0);
    int rtype = ex_be16s(r + 24), vcount = ex_be16s(r + 12), ccount = ex_be16s(r + 14);
    if (rtype >= 0 && rtype < 8) x->rtype_seen[rtype]++;
    if (ex_grow((void **)&x->dl, &x->capdl, x->ndl + 1, sizeof *x->dl)) { x->oom = 1; return; }
    DLRec *d = &x->dl[x->ndl];
    memset(d, 0, sizeof *d);
    d->noff = noff; d->kind = 24; d->rtype = rtype; d->zone = zone; d->far = in_far; d->specs = in_specs; d->gn = -1;
    x->ndl++;
    ExGfxState st;
    memset(&st, 0, sizeof st);
    st.mtx = -1;
    if (prim_l) run_list(x, &d->set, &st, prim_l, 0, varr, rtype, &d->tp);
    if (sec_l && (rtype == 3 || rtype == 4 || rtype > 4 || rtype < 0)) {
        ExGfxState s2;   /* the secondary list starts with the primary's vertex cache and matrix, nothing else */
        memset(&s2, 0, sizeof s2);
        memcpy(s2.cache, st.cache, sizeof s2.cache);
        s2.mtx = st.mtx;
        run_list(x, &d->set, &s2, sec_l, 1, varr, rtype, &d->ts);
    }
    else if (sec_l) note(x, "node %06X: secondary list not drawn for render type %d", noff, rtype);
    x->tri_all += d->tp + d->ts;
    if (!in_far && !in_specs) x->tri_def += d->tp + d->ts;
    if (!in_far && in_specs) x->tri_specs += d->tp + d->ts;
    if (!x->body) {
        int parts[16];
        int np = parts_of(x, noff, parts, 16);
        char nm[48];
        snprintf(nm, sizeof nm, "dl_%06X", noff);
        int gi = add_gnode(x, nm);
        if (gi < 0) return;
        d = &x->dl[x->ndl - 1];
        d->gn = gi;
        CvBuf *e = &x->gn[gi].extras;
        ex_jp(e, "\"kind\":24,\"render_type\":%d,\"vertex_count\":%d,\"collision_vertex_count\":%d", rtype, vcount, ccount);
        ex_jp(e, ",\"triangles\":{\"primary\":%llu,\"secondary\":%llu}", (unsigned long long)d->tp, (unsigned long long)d->ts);
        put_parts(e, parts, np);
        if (in_far) ex_jp(e, ",\"lod_far\":true");
        add_to(x, gparent, gi, 0);
    }
}

/* ---- the node walk ---- */

static void walk(CX *x, uint32_t first, int gparent, int anc, int zone, int in_far, int in_specs, int depth)
{
    uint32_t cur = first;
    int go = 1;
    while (go && !x->oom) {
        if (depth > 200) { note(x, "node tree deeper than 200 levels: cut"); return; }
        if ((size_t)cur + 24 > x->fsize) { x->bad_ptrs++; return; }
        if (x->seen[cur]) { note(x, "node %06X reached twice (loop in the tree): cut", cur); return; }
        x->seen[cur] = 1;
        const uint8_t *n = x->f + cur;
        unsigned kw = ex_be16(n), kind = kw & 0xFF;
        uint32_t rec_raw = ex_be32(n + 4);
        uint32_t next = pnt(x, ex_be32(n + 12), 24);
        uint32_t child = pnt(x, ex_be32(n + 20), 24);
        int parts[16];
        int np = parts_of(x, cur, parts, 16);
        int sub = 1, cg = gparent, canc = anc, cfar = in_far, cspec = in_specs, czone = zone;

        switch (kind) {
        case 1: {
            uint32_t rec = pnt(x, rec_raw, 12);
            if (!x->body || !rec || x->have_j[0]) { note(x, "node %06X (header): unexpected or bad, subtree skipped", cur); sub = 0; break; }
            const uint8_t *r = x->f + rec;
            int matn = ex_be16s(r + 2);
            int gi = add_gnode(x, "j0_root");
            if (gi < 0) return;
            CvBuf *e = &x->gn[gi].extras;
            ex_jp(e, "\"kind\":1,\"joint\":0,\"matrix\":%d,\"animation_part\":%d", matn, ex_be16(r));
            put_parts(e, parts, np);
            x->have_j[0] = 1; x->jn[0] = gi; x->jh[0] = -1; x->jpar[0] = -1; x->jm0[0] = matn; x->jm1[0] = -1;
            add_to(x, gparent, gi, 0);
            cg = gi; canc = 0;
            break;
        }
        case 2: {
            uint32_t rec = pnt(x, rec_raw, 28);
            if (!x->body || !rec) { note(x, "node %06X (position): unexpected or bad record, subtree skipped", cur); sub = 0; break; }
            const uint8_t *r = x->f + rec;
            float org[3];
            for (int k = 0; k < 3; k++) org[k] = ex_bef32(r + 4 * k);
            int joint = ex_be16(r + 12), matn = ex_be16s(r + 14), m1 = ex_be16s(r + 16), half = (kw >> 8) & 1;
            if (joint < 1 || joint > 15 || x->have_j[joint]) {
                note(x, "node %06X (position): joint id %d out of range or repeated, subtree skipped", cur, joint);
                sub = 0;
                break;
            }
            if (kw >> 9) note(x, "node %06X: kind word %04X has unknown high bits", cur, kw);
            if (k_jparent[joint] != anc) note(x, "joint %d hangs under joint %d, expected %d", joint, anc, k_jparent[joint]);
            char nm[48];
            snprintf(nm, sizeof nm, "j%d_%s", joint, k_jname[joint]);
            int gi = add_gnode(x, nm);
            if (gi < 0) return;
            GNode *g = &x->gn[gi];
            g->has_t = 1; memcpy(g->t, org, sizeof org);
            CvBuf *e = &g->extras;
            ex_jp(e, "\"kind\":2,\"joint\":%d,\"matrix\":%d", joint, matn);
            if (half) ex_jp(e, ",\"half_matrix\":%d", m1);
            ex_jp(e, ",\"origin\":["); jf(e, org[0]); ex_jp(e, ","); jf(e, org[1]); ex_jp(e, ","); jf(e, org[2]); ex_jp(e, "]");
            ex_jp(e, ",\"radius\":"); jf(e, ex_bef32(r + 24));
            put_parts(e, parts, np);
            x->have_j[joint] = 1; x->jn[joint] = gi; x->jh[joint] = -1; x->jpar[joint] = anc;
            x->jm0[joint] = matn; x->jm1[joint] = half ? m1 : -1;
            memcpy(x->org[joint], org, sizeof org);
            add_to(x, gparent, gi, 0);
            if (half) {
                if (m1 < 0) note(x, "joint %d: half-rotation flag without a second matrix", joint);
                else {
                    snprintf(nm, sizeof nm, "j%d_half", joint);
                    int hi = add_gnode(x, nm);
                    if (hi < 0) return;
                    GNode *h = &x->gn[hi];
                    h->has_t = 1; memcpy(h->t, org, sizeof org);
                    ex_jp(&h->extras, "\"kind\":2,\"joint\":%d,\"half\":true,\"half_matrix\":%d,\"origin\":[", joint, m1);
                    jf(&h->extras, org[0]); ex_jp(&h->extras, ","); jf(&h->extras, org[1]); ex_jp(&h->extras, ","); jf(&h->extras, org[2]);
                    ex_jp(&h->extras, "]");
                    x->jh[joint] = hi;
                    add_to(x, gparent, hi, 0);
                }
            }
            cg = gi; canc = joint;
            break;
        }
        case 8: {
            uint32_t rec = pnt(x, rec_raw, 16);
            if (!rec) { note(x, "node %06X (level of detail): bad record pointer, subtree skipped", cur); sub = 0; break; }
            float nearv = ex_bef32(x->f + rec), farv = ex_bef32(x->f + rec + 4);
            x->nlod++;
            if (x->body) {
                if (nearv != 0.0f) cfar = 1;
            } else {
                char nm[48];
                snprintf(nm, sizeof nm, "lod_%g_%g", isfinite(nearv) ? (double)nearv : 0.0, isfinite(farv) ? (double)farv : 0.0);
                int gi = add_gnode(x, nm);
                if (gi < 0) return;
                CvBuf *e = &x->gn[gi].extras;
                ex_jp(e, "\"kind\":8,\"lod\":{\"near\":"); jf(e, nearv); ex_jp(e, ",\"far\":"); jf(e, farv); ex_jp(e, "}");
                put_parts(e, parts, np);
                if (nearv == 0.0f || in_far) add_to(x, gparent, gi, 0);
                else { add_to(x, -1, gi, 1); cfar = 1; }
                cg = gi;
            }
            break;
        }
        case 9: break;   /* draw-order split: both groups are children, drawn */
        case 10: {
            uint32_t rec = pnt(x, rec_raw, 28);
            if (!rec) { note(x, "node %06X (bounding box): bad record pointer", cur); break; }
            czone = (int)ex_be32(x->f + rec);   /* the hit zone of the geometry below */
            x->nbbox++;
            break;
        }
        case 13: {
            uint32_t rec = pnt(x, rec_raw, 32);
            if (!rec || !x->body) { note(x, "node %06X (shadow): unexpected or bad record", cur); break; }
            const uint8_t *r = x->f + rec;
            uint32_t tp = ex_be32(r + 16);
            long tex = -1;
            if ((tp >> 24) == 5) {
                long to = (long)(tp & 0xFFFFFF) - 4L * x->nparts;
                if (to >= 0 && to % 12 == 0 && to / 12 < x->ntex) {
                    const uint8_t *te = x->f + 4 * (size_t)x->nparts + to;
                    if (ex_be32(te) >> 24 == 0) tex = (long)(ex_be32(te) & 0xFFFF);
                }
            }
            int gi = add_gnode(x, "shadow");
            if (gi < 0) return;
            CvBuf *e = &x->gn[gi].extras;
            ex_jp(e, "\"kind\":13,\"shadow\":{\"offset\":["); jf(e, ex_bef32(r)); ex_jp(e, ","); jf(e, ex_bef32(r + 4));
            ex_jp(e, "],\"size\":["); jf(e, ex_bef32(r + 8)); ex_jp(e, ","); jf(e, ex_bef32(r + 12));
            ex_jp(e, "],\"texture\":%ld,\"scale\":", tex); jf(e, ex_bef32(r + 24)); ex_jp(e, "}");
            put_parts(e, parts, np);
            add_to(x, gparent, gi, 0);
            x->nshadow++;
            break;
        }
        case 18: {
            uint32_t rec = pnt(x, rec_raw, 8);
            if (!rec) { note(x, "node %06X (switch): bad record pointer, subtree skipped", cur); sub = 0; break; }
            x->nswitch++;
            if (x->body) break;   /* bodies have none: drawn */
            int specs = 0, hair = 0;
            for (int i = 0; i < np; i++) { if (parts[i] == 0) specs = 1; if (parts[i] == 1) hair = 1; }
            char nm[48];
            if (specs) snprintf(nm, sizeof nm, "switch_specs");
            else if (hair) snprintf(nm, sizeof nm, "switch_hair");
            else snprintf(nm, sizeof nm, "switch_o%06X", cur);
            int gi = add_gnode(x, nm);
            if (gi < 0) return;
            CvBuf *e = &x->gn[gi].extras;
            ex_jp(e, "\"kind\":18,\"switch\":true,\"default\":\"%s\"", specs ? "hidden" : "visible");
            put_parts(e, parts, np);
            if (specs) {
                ex_jp(e, ",\"attach\":\"neck\"");
                add_to(x, -1, gi, 1);   /* scene "all" only: the sunglasses are hidden by default */
                cspec = 1;
            } else add_to(x, gparent, gi, 0);
            cg = gi;
            break;
        }
        case 23: {
            if (!x->body) { note(x, "node %06X: head placeholder in a head file", cur); break; }
            int gi = add_gnode(x, "head_attach");
            if (gi < 0) return;
            CvBuf *e = &x->gn[gi].extras;
            ex_jp(e, "\"kind\":23,\"head_placeholder\":true,\"joint\":%d", anc);
            put_parts(e, parts, np);
            add_to(x, gparent, gi, 0);
            x->nplace++;
            x->head_attach = gi;
            if (anc != 3) note(x, "head placeholder hangs under joint %d, not the neck", anc);
            break;
        }
        case 24: {
            uint32_t rec = pnt(x, rec_raw, 32);
            if (!rec) { note(x, "node %06X (display list): bad record pointer", cur); break; }
            do_dl_node(x, cur, rec, gparent, zone, in_far, in_specs);
            break;
        }
        default:
            if (kind < 32) x->skipped_kind[kind]++;
            x->nskipped_kinds++;
            sub = 0;
            break;
        }
        if (sub && child) walk(x, child, cg, canc, czone, cfar, cspec, depth + 1);
        cur = next; go = next != 0;
    }
}

/* ---- output ---- */

static void node_json(const CX *x, CvBuf *out, int i)
{
    const GNode *g = &x->gn[i];
    ex_jp(out, "%s{\"name\":", i ? "," : "");
    ex_jstr(out, g->name);
    if (g->mesh >= 0) ex_jp(out, ",\"mesh\":%d", g->mesh);
    if (g->skin >= 0) ex_jp(out, ",\"skin\":%d", g->skin);
    if (g->has_t) { ex_jp(out, ",\"translation\":["); jf(out, g->t[0]); ex_jp(out, ","); jf(out, g->t[1]); ex_jp(out, ","); jf(out, g->t[2]); ex_jp(out, "]"); }
    if (g->has_r) {
        ex_jp(out, ",\"rotation\":["); jf(out, g->r[0]); ex_jp(out, ","); jf(out, g->r[1]); ex_jp(out, ","); jf(out, g->r[2]);
        ex_jp(out, ","); jf(out, g->r[3]); ex_jp(out, "]");
    }
    if (g->nk) {
        ex_jp(out, ",\"children\":[");
        for (size_t k = 0; k < g->nk; k++) ex_jp(out, "%s%d", k ? "," : "", g->kids[k]);
        ex_jp(out, "]");
    }
    ex_jp(out, ",\"extras\":{");
    cv_buf_put(out, g->extras.p, g->extras.len);
    ex_jp(out, "}}");
}

static void x_free(CX *x)
{
    for (size_t i = 0; i < x->ngn; i++) { cv_buf_free(&x->gn[i].extras); free(x->gn[i].kids); }
    for (size_t i = 0; i < x->ndl; i++) cset_free(&x->dl[i].set);
    free(x->gn); free(x->roots); free(x->fars); free(x->seen); free(x->dl);
    cv_buf_free(&x->notes); gw_free(&x->g);
    ex_matctx_free(&x->mc);
}

static int cw_view(GW *g, const void *d, size_t n)   /* a bufferView without target (animation and skin data) */
{
    while (g->bin.len & 3) { uint8_t z = 0; cv_buf_put(&g->bin, &z, 1); }
    size_t off = g->bin.len;
    cv_buf_put(&g->bin, d, n);
    ex_jp(&g->views, "%s{\"buffer\":0,\"byteOffset\":%zu,\"byteLength\":%zu}", g->nviews ? "," : "", off, n);
    return g->nviews++;
}

/* origin sum from joint 1 to joint j (the bind-pose position of joint j) */
static void bind_sums(const CX *x, float S[16][3])
{
    memset(S, 0, 16 * 3 * sizeof(float));
    for (int pass = 0; pass < 16; pass++)
        for (int j = 1; j < 16; j++) {
            if (!x->have_j[j]) continue;
            int p = x->jpar[j];
            for (int k = 0; k < 3; k++) S[j][k] = (p >= 1 ? S[p][k] : 0.f) + x->org[j][k];
        }
}

/* the primitives of one display-list node into `pj`; returns how many, -1 on OOM */
static int emit_dl_prims(CX *x, CvBuf *pj, int first, const DLRec *d, float S[16][3])
{
    int nprim = 0;
    const CSet *ps = &d->set;
    GW *g = &x->g;
    for (size_t pi = 0; pi < ps->np; pi++) {
        const CPrim *p = &ps->p[pi];
        if (!p->ni) continue;
        const Material *m = &x->mc.mats[p->mat];
        int normals = (m->k.geom & m->normal_mask) != 0;
        float *pos = (float *)malloc(p->nv * 12);
        uint8_t *jt = (uint8_t *)malloc(p->nv * 4);
        float *wt = (float *)malloc(p->nv * 16);
        uint16_t *c16 = (uint16_t *)malloc(p->nv * 8);
        if (!pos || !jt || !wt || !c16) { free(pos); free(jt); free(wt); free(c16); return -1; }
        float mn[3] = { 1e30f, 1e30f, 1e30f }, mx[3] = { -1e30f, -1e30f, -1e30f };
        for (size_t v = 0; v < p->nv; v++) {
            int mtx = p->key[v].mtx, j = x->body && mtx >= 0 && mtx < N_MATS ? x->mat_joint[mtx] : -1;
            for (int k = 0; k < 3; k++) {
                float f = (float)p->key[v].v[k] + (j >= 1 ? S[j][k] : 0.f);
                pos[v * 3 + k] = f;
                if (f < mn[k]) mn[k] = f;
                if (f > mx[k]) mx[k] = f;
            }
            jt[v * 4] = (uint8_t)mtx; jt[v * 4 + 1] = jt[v * 4 + 2] = jt[v * 4 + 3] = 0;
            wt[v * 4] = 1.f; wt[v * 4 + 1] = wt[v * 4 + 2] = wt[v * 4 + 3] = 0.f;
        }
        for (size_t k = 0; k < p->nv; k++)
            for (int ch = 0; ch < 4; ch++) {
                unsigned b = p->col[k * 4 + ch];
                double lin = b / 255.0;
                if (ch < 3 && normals) lin = 1.0;
                else if (ch < 3) lin = lin <= 0.04045 ? lin / 12.92 : pow((lin + 0.055) / 1.055, 2.4);
                long q = (long)(lin * 65535.0 + 0.5);
                c16[k * 4 + ch] = (uint16_t)(q < 0 ? 0 : q > 65535 ? 65535 : q);
            }
        int vp = gw_view(g, pos, p->nv * 12, 34962);
        int ap = gw_acc(g, vp, 5126, p->nv, "VEC3", 0, mn, mx, 3);
        int at = -1;
        if (m->k.textured) {
            int vt = gw_view(g, p->uv, p->nv * 8, 34962);
            at = gw_acc(g, vt, 5126, p->nv, "VEC2", 0, NULL, NULL, 0);
        }
        int vc = gw_view(g, c16, p->nv * 8, 34962);
        int ac = gw_acc(g, vc, 5123, p->nv, "VEC4", 1, NULL, NULL, 0);
        int aj = -1, aw = -1;
        if (x->body) {
            int vj = gw_view(g, jt, p->nv * 4, 34962);
            aj = gw_acc(g, vj, 5121, p->nv, "VEC4", 0, NULL, NULL, 0);
            int vw = gw_view(g, wt, p->nv * 16, 34962);
            aw = gw_acc(g, vw, 5126, p->nv, "VEC4", 0, NULL, NULL, 0);
        }
        int ai;
        if (p->nv - 1 > 65535) {
            int vi = gw_view(g, p->idx, p->ni * 4, 34963);
            ai = gw_acc(g, vi, 5125, p->ni, "SCALAR", 0, NULL, NULL, 0);
        } else {
            uint16_t *i16 = (uint16_t *)malloc(p->ni * 2);
            if (!i16) { free(pos); free(jt); free(wt); free(c16); return -1; }
            for (size_t k = 0; k < p->ni; k++) i16[k] = (uint16_t)p->idx[k];
            int vi = gw_view(g, i16, p->ni * 2, 34963);
            free(i16);
            ai = gw_acc(g, vi, 5123, p->ni, "SCALAR", 0, NULL, NULL, 0);
        }
        free(pos); free(jt); free(wt); free(c16);
        ex_jp(pj, "%s{\"attributes\":{\"POSITION\":%d", (first && !nprim) ? "" : ",", ap);
        if (at >= 0) ex_jp(pj, ",\"TEXCOORD_0\":%d", at);
        ex_jp(pj, ",\"COLOR_0\":%d", ac);
        if (aj >= 0) ex_jp(pj, ",\"JOINTS_0\":%d,\"WEIGHTS_0\":%d", aj, aw);
        ex_jp(pj, "},\"indices\":%d,\"material\":%d,\"mode\":4,\"extras\":{\"list\":\"%s\",\"dl_node\":%u,\"render_type\":%d,\"hit_zone\":%d",
              ai, p->mat, m->k.list ? "secondary" : "primary", d->noff, d->rtype, d->zone);
        if (d->far) ex_jp(pj, ",\"lod_far\":true");
        if (d->specs) ex_jp(pj, ",\"sunglasses\":true");
        ex_jp(pj, "}}");
        nprim++;
    }
    return nprim;
}

static int cmp_int(const void *a, const void *b) { return *(const int *)a - *(const int *)b; }

/* quaternion to a glTF [x y z w] float array */
static void qf(const Q *q, float *o) { o[0] = (float)q->x; o[1] = (float)q->y; o[2] = (float)q->z; o[3] = (float)q->w; }

/* One animation into the buffer; appends its JSON to `aj`. The root's translation is scaled by `pov` (the body's in a
 * body file, 1 in the shared animations.gltf). 0 ok, -1 OOM. */
static int emit_animation(CX *x, CvBuf *aj, int first, const AnimData *ad, int n, double pov)
{
    const AnimEnt *a = &ad->a[n];
    int nf = a->nframes;
    GW *g = &x->g;
    CvBuf samplers, channels;
    memset(&samplers, 0, sizeof samplers); memset(&channels, 0, sizeof channels);
    int nsamp = 0, rc = -1;
    float *times = (float *)malloc((size_t)nf * 4);
    float *buf = (float *)malloc((size_t)nf * 4 * sizeof(float));
    if (!times || !buf) goto out;
    for (int f = 0; f < nf; f++) times[f] = (float)((double)f / (double)a->fps);
    float tmn = 0.f, tmx = times[nf - 1];
    int tacc = gw_acc(g, cw_view(g, times, (size_t)nf * 4), 5126, (size_t)nf, "SCALAR", 0, &tmn, &tmx, 1);

#define ADD_CHAN(node, path, acc) do { \
        ex_jp(&samplers, "%s{\"input\":%d,\"output\":%d,\"interpolation\":\"LINEAR\"}", nsamp ? "," : "", tacc, acc); \
        ex_jp(&channels, "%s{\"sampler\":%d,\"target\":{\"node\":%d,\"path\":\"%s\"}}", nsamp ? "," : "", nsamp, node, path); \
        nsamp++; } while (0)

    /* the joints' rotations, and the half nodes' */
    for (int j = 1; j < 16; j++) {
        if (!x->have_j[j]) continue;
        int ch = ad->skel_a[j];
        Q prev = { 1, 0, 0, 0 }, hprev = { 1, 0, 0, 0 };
        float *hbuf = x->jh[j] >= 0 ? (float *)malloc((size_t)nf * 4 * sizeof(float)) : NULL;
        if (x->jh[j] >= 0 && !hbuf) goto out;
        for (int f = 0; f < nf; f++) {
            double ang[45];
            anim_angles(ad, n, f, ang);
            Q q = euler_q(ang[ch], ang[ch + 1], ang[ch + 2]);
            Q h = half_q(q);
            if (f && qdot(q, prev) < 0) q = qneg(q);
            if (f && qdot(h, hprev) < 0) h = qneg(h);
            prev = q; hprev = h;
            qf(&q, buf + (size_t)f * 4);
            if (hbuf) qf(&h, hbuf + (size_t)f * 4);
        }
        int acc = gw_acc(g, cw_view(g, buf, (size_t)nf * 16), 5126, (size_t)nf, "VEC4", 0, NULL, NULL, 0);
        ADD_CHAN(x->jn[j], "rotation", acc);
        if (hbuf) {
            int ha = gw_acc(g, cw_view(g, hbuf, (size_t)nf * 16), 5126, (size_t)nf, "VEC4", 0, NULL, NULL, 0);
            ADD_CHAN(x->jh[j], "rotation", ha);
            free(hbuf);
        }
    }
    /* the root: accumulated root motion (translation) and heading (rotation) */
    {
        float *tr = (float *)malloc((size_t)nf * 3 * sizeof(float));
        if (!tr) goto out;
        double px = 0, pz = 0, hd = 0;
        Q hprev = { 1, 0, 0, 0 };
        for (int f = 0; f < nf; f++) {
            int rx, ry, rz;
            double h;
            anim_root(ad, n, f, &rx, &ry, &rz, &h);
            if (f == 0) hd = h;
            else {
                px += (rx * cos(hd) + rz * sin(hd)) * pov;
                pz += (-rx * sin(hd) + rz * cos(hd)) * pov;
                hd += h;
            }
            tr[f * 3] = (float)px; tr[f * 3 + 1] = (float)(ry * pov); tr[f * 3 + 2] = (float)pz;
            Q q = qaxis(1, hd);
            if (f && qdot(q, hprev) < 0) q = qneg(q);
            hprev = q;
            qf(&q, buf + (size_t)f * 4);
        }
        int ta = gw_acc(g, cw_view(g, tr, (size_t)nf * 12), 5126, (size_t)nf, "VEC3", 0, NULL, NULL, 0);
        ADD_CHAN(x->jn[0], "translation", ta);
        int ra = gw_acc(g, cw_view(g, buf, (size_t)nf * 16), 5126, (size_t)nf, "VEC4", 0, NULL, NULL, 0);
        ADD_CHAN(x->jn[0], "rotation", ra);
        free(tr);
    }
#undef ADD_CHAN
    ex_jp(aj, "%s{\"name\":", first ? "" : ",");
    ex_jstr(aj, a->name);
    ex_jp(aj, ",\"samplers\":[");
    cv_buf_put(aj, samplers.p, samplers.len);
    ex_jp(aj, "],\"channels\":[");
    cv_buf_put(aj, channels.p, channels.len);
    ex_jp(aj, "],\"extras\":{\"number\":%d,\"frames\":%d,\"loop\":%s,\"fps\":%d}}", n, nf, a->loop ? "true" : "false", a->fps);
    rc = 0;
out:
    free(times); free(buf);
    cv_buf_free(&samplers); cv_buf_free(&channels);
    return rc;
}

typedef struct { int failed; uint64_t tri_def, tri_all; char msg[1400]; } CharResult;

/* Exports one body or head; appends its characters.json entry to `index`. */
static int export_char(const CharEnt *ce, const AnimData *ad, const int *sel, const uint8_t *rom, size_t rom_size, const char *region,
                       ExTexCache *tc, const char *out_dir, CvBuf *index, CharResult *res)
{
    CX *x = (CX *)calloc(1, sizeof *x);
    CvBuf fbuf, json, mesh_pj[2], anims, tex_json;
    MatOut mo;
    char dir[1100], path[1200];
    int fail = 0, nanim = 0, npj[2] = { 0, 0 };
    float S[16][3];
    memset(&fbuf, 0, sizeof fbuf); memset(&json, 0, sizeof json); memset(mesh_pj, 0, sizeof mesh_pj);
    memset(&anims, 0, sizeof anims); memset(&tex_json, 0, sizeof tex_json);
    ex_matout_init(&mo);
    if (!x) { snprintf(res->msg, sizeof res->msg, "out of memory"); res->failed = 1; return 1; }
    x->ce = ce; x->ad = ad; x->tc = tc; x->body = ce->kind == K_BODY;
    x->nparts = ce->nparts; x->ntex = ce->ntextures; x->nmat = ce->nmatrices;
    x->mc.tc = tc; x->mc.env_rule = 0; x->mc.normal_mask = GEOM_LIGHTING; x->mc.emit_decal = 1;
    for (int i = 0; i < 16; i++) { x->jn[i] = x->jh[i] = -1; x->jm0[i] = x->jm1[i] = -1; }
    for (int i = 0; i < N_MATS; i++) x->mat_joint[i] = -1;
    x->head_attach = -1;

#define FAIL(...) do { snprintf(res->msg, sizeof res->msg, __VA_ARGS__); fail = 1; goto done; } while (0)

    snprintf(dir, sizeof dir, "%s/%s", out_dir, ce->dir);
    if (!ce->row || ce->row->size < 16 || (size_t)ce->row->off + ce->row->size > rom_size) FAIL("%s: file outside the ROM", ce->file);
    if (cv_rz_inflate(rom + ce->row->off, ce->row->size, &fbuf)) FAIL("%s: does not decompress", ce->file);
    x->f = fbuf.p; x->fsize = fbuf.len;
    if (x->nmat > N_MATS) FAIL("%s: %d matrices", ce->file, x->nmat);
    x->root_off = 4u * (uint32_t)x->nparts + 12u * (uint32_t)x->ntex;
    if ((size_t)x->root_off + 24 > x->fsize) FAIL("%s: root node outside the file", ce->file);
    x->seen = (uint8_t *)calloc(x->fsize + 1, 1);
    if (!x->seen) FAIL("out of memory");

    /* texture table: bank numbers */
    for (int i = 0; i < x->ntex; i++) {
        const uint8_t *te = x->f + 4 * (size_t)x->nparts + 12 * (size_t)i;
        if (4 * (size_t)x->nparts + 12 * (size_t)(i + 1) > x->fsize) break;
        uint32_t id = ex_be32(te);
        ex_jp(&tex_json, "%s{", i ? "," : "");
        if ((id >> 24) == 0) ex_jp(&tex_json, "\"texture\":%u", id & 0xFFFF);
        else if ((id >> 24) == 5) ex_jp(&tex_json, "\"embedded\":%u", id & 0xFFFFFF);
        else ex_jp(&tex_json, "\"id\":%u", id);
        ex_jp(&tex_json, ",\"width\":%u,\"height\":%u}", te[4], te[5]);
    }

    int head_root = -1;
    if (!x->body) {
        head_root = add_gnode(x, "neck");
        if (head_root < 0) FAIL("out of memory");
        ex_jp(&x->gn[head_root].extras, "\"attach\":\"j3_neck\",\"attach_transform\":\"identity\"");
        add_to(x, -1, head_root, 0);
    }
    walk(x, x->root_off, head_root, -1, 0, 0, 0, 0);
    if (x->oom || x->mc.oom) FAIL("out of memory");
    if (x->body) {
        for (int j = 0; j < 16; j++) if (!x->have_j[j]) FAIL("%s: joint %d is missing from the tree", ce->file, j);
        for (int j = 0; j < 16; j++) {
            if (x->jm0[j] >= 0 && x->jm0[j] < N_MATS) { x->mat_joint[x->jm0[j]] = j; x->mat_half[x->jm0[j]] = 0; }
            if (x->jm1[j] >= 0 && x->jm1[j] < N_MATS) { x->mat_joint[x->jm1[j]] = j; x->mat_half[x->jm1[j]] = 1; }
        }
        for (int m = 0; m < x->nmat; m++) if (x->mat_joint[m] < 0) note(x, "matrix %d belongs to no position node (bound to the root)", m);
        if (x->nplace != (ce->has_head ? 0 : 1)) note(x, "%d head placeholder(s) for has_head = %d", x->nplace, ce->has_head);
        if (!x->nshadow) note(x, "no shadow node");
    } else {
        if (!x->ngn) FAIL("%s: nothing reached from the root", ce->file);
    }
    bind_sums(x, S);

    /* ---- the rest pose: frame 0 of the idle animation ---- */
    if (x->body) {
        double ang[45];
        anim_angles(ad, 0, 0, ang);
        for (int j = 1; j < 16; j++) {
            Q q = euler_q(ang[ad->skel_a[j]], ang[ad->skel_a[j] + 1], ang[ad->skel_a[j] + 2]);
            GNode *g = &x->gn[x->jn[j]];
            g->has_r = 1; qf(&q, g->r);
            if (x->jh[j] >= 0) { Q h = half_q(q); GNode *hg = &x->gn[x->jh[j]]; hg->has_r = 1; qf(&h, hg->r); }
        }
        int rx, ry, rz;
        double hd;
        anim_root(ad, 0, 0, &rx, &ry, &rz, &hd);
        GNode *g = &x->gn[x->jn[0]];
        g->has_t = 1; g->t[0] = 0.f; g->t[1] = (float)(ry * (double)ce->pov); g->t[2] = 0.f;
        Q hq = qaxis(1, hd);
        g->has_r = 1; qf(&hq, g->r);
    }

    /* ---- meshes ---- */
    {
        int nmesh = 0;
        CvBuf meshes;
        memset(&meshes, 0, sizeof meshes);
        for (size_t i = 0; i < x->ndl; i++) {
            DLRec *d = &x->dl[i];
            if (!d->tp && !d->ts) continue;
            if (x->body) {
                int which = d->far ? 1 : 0;
                int n = emit_dl_prims(x, &mesh_pj[which], npj[which] == 0, d, S);
                if (n < 0) { cv_buf_free(&meshes); FAIL("out of memory"); }
                npj[which] += n;
            } else {
                CvBuf pj;
                memset(&pj, 0, sizeof pj);
                int n = emit_dl_prims(x, &pj, 1, d, S);
                if (n < 0) { cv_buf_free(&pj); cv_buf_free(&meshes); FAIL("out of memory"); }
                if (n) {
                    char nm[48];
                    snprintf(nm, sizeof nm, "dl_%06X", d->noff);
                    ex_jp(&meshes, "%s{\"name\":\"%s\",\"primitives\":[", nmesh ? "," : "", nm);
                    cv_buf_put(&meshes, pj.p, pj.len);
                    ex_jp(&meshes, "]}");
                    x->gn[d->gn].mesh = nmesh++;
                }
                cv_buf_free(&pj);
            }
        }
        if (x->body) {
            int ids[2] = { -1, -1 };
            for (int w = 0; w < 2; w++) {
                if (!npj[w]) continue;
                ex_jp(&meshes, "%s{\"name\":\"%s\",\"primitives\":[", nmesh ? "," : "", w ? "body_far" : "body");
                cv_buf_put(&meshes, mesh_pj[w].p, mesh_pj[w].len);
                ex_jp(&meshes, "]}");
                int gi = add_gnode(x, w ? "body_far" : "body");
                if (gi < 0) { cv_buf_free(&meshes); FAIL("out of memory"); }
                x->gn[gi].mesh = nmesh++; x->gn[gi].skin = 0;
                ids[w] = gi;
            }
            if (ids[0] >= 0) add_to(x, -1, ids[0], 0);
            if (ids[1] >= 0) add_to(x, -1, ids[1], 1);
        }
        /* the materials are complete now */
        mo.pack_depth = 2;   /* <out>/characters/<name>/<name>.gltf -> ../../textures (the pack root) */
        if (ex_emit_materials(&mo, &x->mc)) { cv_buf_free(&meshes); FAIL("out of memory"); }

        /* skin + animations (bodies) */
        CvBuf skins;
        memset(&skins, 0, sizeof skins);
        if (x->body) {
            float *ibm = (float *)calloc((size_t)x->nmat * 16, sizeof(float));
            if (!ibm) { cv_buf_free(&meshes); FAIL("out of memory"); }
            ex_jp(&skins, "{\"joints\":[");
            for (int m = 0; m < x->nmat; m++) {
                int j = x->mat_joint[m];
                int node = j < 0 ? x->jn[0] : (x->mat_half[m] && x->jh[j] >= 0 ? x->jh[j] : x->jn[j]);
                float *o = ibm + (size_t)m * 16;
                o[0] = o[5] = o[10] = o[15] = 1.f;
                if (j >= 1) { o[12] = -S[j][0]; o[13] = -S[j][1]; o[14] = -S[j][2]; }
                ex_jp(&skins, "%s%d", m ? "," : "", node);
            }
            int acc = gw_acc(&x->g, cw_view(&x->g, ibm, (size_t)x->nmat * 64), 5126, (size_t)x->nmat, "MAT4", 0, NULL, NULL, 0);
            free(ibm);
            ex_jp(&skins, "],\"inverseBindMatrices\":%d,\"skeleton\":%d,\"name\":\"rig\"}", acc, x->jn[0]);
            for (int n = 0; n < N_ANIMS; n++) {
                if (!sel[n] || !ad->a[n].used) continue;
                if (emit_animation(x, &anims, nanim == 0, ad, n, (double)ce->pov)) { cv_buf_free(&meshes); cv_buf_free(&skins); FAIL("out of memory"); }
                nanim++;
            }
        }

        /* ---- the glTF ---- */
        ex_jp(&json, "{\"asset\":{\"version\":\"2.0\",\"generator\":\"project-dam export\",\"extras\":{\"exporter_version\":%d,\"rom_region\":",
              EX_EXPORTER_VERSION);
        ex_jstr(&json, region);
        ex_jp(&json, ",\"character\":%d,\"file\":", ce->id); ex_jstr(&json, ce->file);
        ex_jp(&json, ",\"name\":"); ex_jstr(&json, ce->name);
        ex_jp(&json, ",\"label\":"); if (ce->label) ex_jstr(&json, ce->label); else ex_jp(&json, "null");
        ex_jp(&json, ",\"kind\":\"%s\",\"body_scale\":", x->body ? "body" : "head"); jf(&json, ce->body_scale);
        ex_jp(&json, ",\"model_scale\":"); jf(&json, 0.1f * ce->body_scale);
        ex_jp(&json, ",\"pov\":"); jf(&json, ce->pov);
        ex_jp(&json, ",\"is_male\":%d,\"has_head\":%d,\"bounding_radius\":", ce->is_male, ce->has_head); jf(&json, ce->radius);
        ex_jp(&json, ",\"nparts\":%d,\"nmatrices\":%d,\"ntextures\":%d", ce->nparts, ce->nmatrices, ce->ntextures);
        ex_jp(&json, ",\"units\":\"model units (world = model_scale x model; placement per characters.md)\"");
        ex_jp(&json, ",\"vertex_color_encoding\":\"linear (converted from the game's sRGB bytes)\"");
        if (x->body)
            ex_jp(&json, ",\"rest_pose\":\"frame 0 of the idle animation (animation 0), root height included\",\"bind\":\"identity rotations; "
                         "mesh positions are stored + the sum of joint origins from joint 1; skin = matrix number\"");
        else
            ex_jp(&json, ",\"attach\":\"child of j3_neck with an identity transform\"");
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
        if (nmesh) { ex_jp(&json, ",\"meshes\":["); cv_buf_put(&json, meshes.p, meshes.len); ex_jp(&json, "]"); }
        if (x->body) { ex_jp(&json, ",\"skins\":["); cv_buf_put(&json, skins.p, skins.len); ex_jp(&json, "]"); }
        if (x->mc.nm) { ex_jp(&json, ",\"materials\":["); cv_buf_put(&json, mo.mats.p, mo.mats.len); ex_jp(&json, "]"); }
        ex_emit_texture_arrays(&json, &mo);
        if (nanim) { ex_jp(&json, ",\"animations\":["); cv_buf_put(&json, anims.p, anims.len); ex_jp(&json, "]"); }
        if (x->g.naccs) {
            ex_jp(&json, ",\"accessors\":["); cv_buf_put(&json, x->g.accs.p, x->g.accs.len);
            ex_jp(&json, "],\"bufferViews\":["); cv_buf_put(&json, x->g.views.p, x->g.views.len); ex_jp(&json, "]");
            ex_jp(&json, ",\"buffers\":[{\"uri\":\"%s.bin\",\"byteLength\":%zu}]", ce->dir, x->g.bin.len);
        }
        ex_jp(&json, "}\n");
        cv_buf_free(&meshes); cv_buf_free(&skins);
    }

    if (ex_mkdir_p(dir)) FAIL("cannot create %s", dir);
    snprintf(path, sizeof path, "%s/%s.gltf", dir, ce->dir);
    if (ex_write_file(path, json.p, json.len)) FAIL("cannot write %s", path);
    snprintf(path, sizeof path, "%s/%s.bin", dir, ce->dir);
    if (ex_write_file(path, x->g.bin.p, x->g.bin.len)) FAIL("cannot write %s", path);

    /* ---- the characters.json entry ---- */
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
        ex_jp(ix, "{\"id\":%d,\"file\":", ce->id); ex_jstr(ix, ce->file);
        ex_jp(ix, ",\"name\":"); ex_jstr(ix, ce->name);
        ex_jp(ix, ",\"label\":"); if (ce->label) ex_jstr(ix, ce->label); else ex_jp(ix, "null");
        ex_jp(ix, ",\"kind\":\"%s\",\"gltf\":\"%s/%s.gltf\",\"body_scale\":", x->body ? "body" : "head", ce->dir, ce->dir); jf(ix, ce->body_scale);
        ex_jp(ix, ",\"model_scale\":"); jf(ix, 0.1f * ce->body_scale);
        ex_jp(ix, ",\"pov\":"); jf(ix, ce->pov);
        ex_jp(ix, ",\"is_male\":%d,\"has_head\":%d,\"joints\":%d,\"triangles\":%llu", ce->is_male, ce->has_head, x->body ? x->nmat : 1,
              (unsigned long long)x->tri_def);
        if (!x->body) ex_jp(ix, ",\"triangles_specs\":%llu", (unsigned long long)x->tri_specs);
        ex_jp(ix, ",\"triangles_all\":%llu,\"textures\":[", (unsigned long long)(x->tri_all));
        for (size_t i = 0; i < nused; i++) ex_jp(ix, "%s%d", i ? "," : "", used[i]);
        ex_jp(ix, "],\"animations\":[");
        {
            int first = 1;
            if (x->body)
                for (int n = 0; n < N_ANIMS; n++)
                    if (sel[n] && ad->a[n].used) { ex_jp(ix, "%s\"%s\"", first ? "" : ",", ad->a[n].name); first = 0; }
        }
        ex_jp(ix, "],\"attach_joint\":%s,\"notes\":[", x->body ? "null" : "\"j3_neck\"");
        cv_buf_put(ix, x->notes.p, x->notes.len);
        ex_jp(ix, "]}");
        free(used);
    }
    res->tri_def = x->tri_def; res->tri_all = x->tri_all;

done:
    cv_buf_free(&fbuf); cv_buf_free(&json); cv_buf_free(&tex_json); cv_buf_free(&anims);
    cv_buf_free(&mesh_pj[0]); cv_buf_free(&mesh_pj[1]);
    ex_matout_free(&mo);
    x_free(x);
    free(x);
    res->failed = fail;
    return fail;
#undef FAIL
}

/* animations/<name>.gltf: one animation on the guard rig (the nodes of one body, `ce`, without its meshes), the root's
 * translation in model units without any body's pov (a consumer multiplies it by the body's pov). Every body shares
 * the skeleton and names its nodes the same, so a consumer retargets by node name; one file per animation, so a
 * consumer can replace or add one by its name. 0 ok, else 1 (msg says why). */
static int export_anim(const CharEnt *ce, const AnimData *ad, int n, const uint8_t *rom, size_t rom_size, const char *region,
                       ExTexCache *tc, const char *anim_dir, char *msg, size_t msgsz)
{
    CX *x = (CX *)calloc(1, sizeof *x);
    CvBuf fbuf, json, anims;
    char path[1200];
    int fail = 0;
    const char *name = ad->a[n].name;
    memset(&fbuf, 0, sizeof fbuf); memset(&json, 0, sizeof json); memset(&anims, 0, sizeof anims);
    if (!x) { snprintf(msg, msgsz, "out of memory"); return 1; }
    x->ce = ce; x->ad = ad; x->tc = tc; x->body = 1;
    x->nparts = ce->nparts; x->ntex = ce->ntextures; x->nmat = ce->nmatrices;
    x->mc.tc = tc; x->mc.env_rule = 0; x->mc.normal_mask = GEOM_LIGHTING; x->mc.emit_decal = 1;
    for (int i = 0; i < 16; i++) { x->jn[i] = x->jh[i] = -1; x->jm0[i] = x->jm1[i] = -1; }
    for (int i = 0; i < N_MATS; i++) x->mat_joint[i] = -1;
    x->head_attach = -1;
#define AFAIL(...) do { snprintf(msg, msgsz, __VA_ARGS__); fail = 1; goto done; } while (0)
    if (ce->kind != K_BODY || !ce->row || ce->row->size < 16 || (size_t)ce->row->off + ce->row->size > rom_size) AFAIL("%s: not a body in the ROM", ce->file);
    if (cv_rz_inflate(rom + ce->row->off, ce->row->size, &fbuf)) AFAIL("%s: does not decompress", ce->file);
    x->f = fbuf.p; x->fsize = fbuf.len;
    if (x->nmat > N_MATS) AFAIL("%s: %d matrices", ce->file, x->nmat);
    x->root_off = 4u * (uint32_t)x->nparts + 12u * (uint32_t)x->ntex;
    if ((size_t)x->root_off + 24 > x->fsize) AFAIL("%s: root node outside the file", ce->file);
    x->seen = (uint8_t *)calloc(x->fsize + 1, 1);
    if (!x->seen) AFAIL("out of memory");
    walk(x, x->root_off, -1, -1, 0, 0, 0, 0);
    if (x->oom || x->mc.oom) AFAIL("out of memory");
    for (int j = 0; j < 16; j++) if (!x->have_j[j]) AFAIL("%s: joint %d is missing from the tree", ce->file, j);
    {   /* the rest pose (frame 0 of idle) on the nodes, the root's height without pov */
        double ang[45];
        int rx, ry, rz;
        double hd;
        anim_angles(ad, 0, 0, ang);
        for (int j = 1; j < 16; j++) {
            Q q = euler_q(ang[ad->skel_a[j]], ang[ad->skel_a[j] + 1], ang[ad->skel_a[j] + 2]);
            GNode *g = &x->gn[x->jn[j]];
            g->has_r = 1; qf(&q, g->r);
            if (x->jh[j] >= 0) { Q h = half_q(q); GNode *hg = &x->gn[x->jh[j]]; hg->has_r = 1; qf(&h, hg->r); }
        }
        anim_root(ad, 0, 0, &rx, &ry, &rz, &hd);
        GNode *g = &x->gn[x->jn[0]];
        g->has_t = 1; g->t[0] = 0.f; g->t[1] = (float)ry; g->t[2] = 0.f;
        Q hq = qaxis(1, hd);
        g->has_r = 1; qf(&hq, g->r);
    }
    if (emit_animation(x, &anims, 1, ad, n, 1.0)) AFAIL("out of memory");
    ex_jp(&json, "{\"asset\":{\"version\":\"2.0\",\"generator\":\"project-dam export\",\"extras\":{\"exporter_version\":%d,\"rom_region\":",
          EX_EXPORTER_VERSION);
    ex_jstr(&json, region);
    ex_jp(&json, ",\"rig\":"); ex_jstr(&json, ce->file);
    ex_jp(&json, ",\"animation\":%d,\"units\":\"model units\",\"root_translation\":\"without pov: multiply by the body's pov "
                 "(characters.json)\",\"retarget\":\"by node name (j<id>_<name>, j<id>_half): every body has the same skeleton\"}}", n);
    ex_jp(&json, ",\"scene\":0,\"scenes\":[{\"name\":\"rig\",\"nodes\":[");
    for (size_t i = 0; i < x->nroots; i++) ex_jp(&json, "%s%d", i ? "," : "", x->roots[i]);
    ex_jp(&json, "]}],\"nodes\":[");
    for (size_t i = 0; i < x->ngn; i++) node_json(x, &json, (int)i);
    ex_jp(&json, "],\"animations\":["); cv_buf_put(&json, anims.p, anims.len); ex_jp(&json, "]");
    ex_jp(&json, ",\"accessors\":["); cv_buf_put(&json, x->g.accs.p, x->g.accs.len);
    ex_jp(&json, "],\"bufferViews\":["); cv_buf_put(&json, x->g.views.p, x->g.views.len); ex_jp(&json, "]");
    ex_jp(&json, ",\"buffers\":[{\"uri\":\"%s.bin\",\"byteLength\":%zu}]}\n", name, x->g.bin.len);
    snprintf(path, sizeof path, "%s/%s.gltf", anim_dir, name);
    if (ex_write_file(path, json.p, json.len)) AFAIL("cannot write %s.gltf", name);
    snprintf(path, sizeof path, "%s/%s.bin", anim_dir, name);
    if (ex_write_file(path, x->g.bin.p, x->g.bin.len)) AFAIL("cannot write %s.bin", name);
done:
    cv_buf_free(&fbuf); cv_buf_free(&json); cv_buf_free(&anims);
    x_free(x);
    free(x);
    return fail;
#undef AFAIL
}

/* ------------------------------------------------------------------------ */
/* all characters                                                           */
/* ------------------------------------------------------------------------ */

int ex_export_characters(const char *rom_path, const char *out_dir, ExLogFn log, void *user)
{
    uint8_t *rom = NULL;
    size_t rom_size = 0;
    const char *region = NULL;
    const CvRegionData *rd = NULL;
    char msg[600];
    CvBuf cd;
    CharEnt *ce = (CharEnt *)calloc(N_CHARS, sizeof *ce);
    AnimData *ad = (AnimData *)calloc(1, sizeof *ad);
    memset(&cd, 0, sizeof cd);
    if (!ce || !ad) { free(ce); free(ad); return 1; }
    if (ex_open_rom(rom_path, log, user, &rom, &rom_size, &region, &rd)) { free(ce); free(ad); return 1; }
    if (ex_mkdir_p(out_dir)) { ex_logf(log, user, "cannot create %s", out_dir); free(rom); free(ce); free(ad); return 1; }

    size_t r;
    for (r = 0; r < sizeof k_creg / sizeof k_creg[0]; r++) if (!strcmp(region, k_creg[r].region)) break;
    if (r == sizeof k_creg / sizeof k_creg[0]) { ex_logf(log, user, "characters refused: no constants for region %s", region); goto refused; }
    if (k_creg[r].cdata_rom >= rom_size || cv_rz_inflate(rom + k_creg[r].cdata_rom, rom_size - k_creg[r].cdata_rom, &cd) ||
        cd.len > CDATA_MAX_OUT) {
        ex_logf(log, user, "characters refused for region %s: cdata segment does not inflate", region);
        goto refused;
    }
    if (read_table(cd.p, cd.len, r, rd, ce, msg, sizeof msg)) { ex_logf(log, user, "characters refused for region %s: %s", region, msg); goto refused; }
    if (anim_load(ad, rom, rom_size, r, cd.p, cd.len, msg, sizeof msg)) { ex_logf(log, user, "characters refused for region %s: %s", region, msg); goto refused; }
    {
        /* the guard skeleton record: 16 joints, size 45, rotation channel A per joint */
        const uint8_t *sk = cd.p + k_creg[r].skel;
        uint32_t jp_ = ex_be32(sk + 4);
        if ((size_t)k_creg[r].skel + 12 > cd.len || ex_be16s(sk) != 16 || ex_be16s(sk + 8) != 45 || jp_ < k_creg[r].base ||
            (size_t)(jp_ - k_creg[r].base) + 16 * 6 > cd.len) {
            ex_logf(log, user, "characters refused for region %s: the guard skeleton record does not have 16 joints and size 45", region);
            goto refused;
        }
        for (int j = 1; j < 16; j++) {
            ad->skel_a[j] = ex_be16(cd.p + (jp_ - k_creg[r].base) + (size_t)j * 6 + 2);
            if (ad->skel_a[j] != 3 * (j - 1)) {
                ex_logf(log, user, "characters refused for region %s: joint %d reads rotation channel %d, expected %d", region, j,
                        ad->skel_a[j], 3 * (j - 1));
                goto refused;
            }
        }
    }
    {
        int sel[N_ANIMS];
        const char *allv = getenv("GE007_EXPORT_ALL_ANIMATIONS");
        int all = allv && allv[0] && allv[0] != '0';
        memset(sel, 0, sizeof sel);
        if (all) for (int i = 0; i < N_ANIMS; i++) sel[i] = 1;
        else sel[0] = 1;   /* idle; every animation is in the shared animations.gltf */

        ExTexCache tc;
        ex_texcache_open(&tc, rom, rom_size, region, log, user);
        CvBuf index;
        memset(&index, 0, sizeof index);
        int nfailed = 0, nbody = 0, nhead = 0;
        uint64_t gtris = 0;
        for (int i = 0; i < N_CHARS; i++) {
            CharResult res;
            memset(&res, 0, sizeof res);
            if (i) ex_jp(&index, ",\n");
            if (ce[i].kind == K_HAND) {
                ex_jp(&index, "{\"id\":%d,\"file\":", i); ex_jstr(&index, ce[i].file);
                ex_jp(&index, ",\"name\":"); ex_jstr(&index, ce[i].name);
                ex_jp(&index, ",\"label\":"); if (ce[i].label) ex_jstr(&index, ce[i].label); else ex_jp(&index, "null");
                ex_jp(&index, ",\"kind\":\"hand\",\"gltf\":null,\"body_scale\":"); jf(&index, ce[i].body_scale);
                ex_jp(&index, ",\"pov\":"); jf(&index, ce[i].pov);
                ex_jp(&index, ",\"joints\":%d,\"notes\":[\"first-person hand: its own skeleton, switches and matrices set by the watch code; not exported\"]}", ce[i].nmatrices);
                continue;
            }
            if (export_char(&ce[i], ad, sel, rom, rom_size, region, &tc, out_dir, &index, &res)) {
                nfailed++;
                ex_logf(log, user, "character %2d %-20s FAILED: %s", i, ce[i].file, res.msg);
                ex_jp(&index, "{\"id\":%d,\"file\":", i); ex_jstr(&index, ce[i].file);
                ex_jp(&index, ",\"kind\":\"failed\",\"notes\":["); ex_jstr(&index, res.msg); ex_jp(&index, "]}");
            } else {
                gtris += res.tri_def;
                if (ce[i].kind == K_BODY) nbody++; else nhead++;
            }
            if (tc.nt > 600) ex_texcache_clear(&tc);
        }
        /* every animation into animations/<name>.gltf, on the camguard rig */
        int nshared = 0, aok[N_ANIMS];
        memset(aok, 0, sizeof aok);
        {
            char adir[1100];
            snprintf(adir, sizeof adir, "%s/animations", out_dir);
            if (ex_mkdir_p(adir)) { ex_logf(log, user, "cannot create %s", adir); nfailed++; }
            else
                for (int n = 0; n < N_ANIMS; n++) {
                    char amsg[600] = "";
                    if (!ad->a[n].used) continue;
                    if (export_anim(&ce[0], ad, n, rom, rom_size, region, &tc, adir, amsg, sizeof amsg)) {
                        nfailed++;
                        ex_logf(log, user, "animation %3d %-28s FAILED: %s", n, ad->a[n].name, amsg);
                    } else { aok[n] = 1; nshared++; }
                }
        }
        {
            CvBuf full;
            char path[1200];
            memset(&full, 0, sizeof full);
            ex_jp(&full, "{\"exporter_version\":%d,\"rom_region\":", EX_EXPORTER_VERSION);
            ex_jstr(&full, region);
            if (nshared) ex_jp(&full, ",\"animations_dir\":\"animations\",\"animations_rig\":\"%s\"", ce[0].file);
            ex_jp(&full, ",\"animations\":[\n");
            {
                int first = 1;
                for (int n = 0; n < N_ANIMS; n++) {
                    const AnimEnt *a = &ad->a[n];
                    if (!a->used) continue;
                    ex_jp(&full, "%s{\"number\":%d,\"name\":", first ? "" : ",\n", n);
                    ex_jstr(&full, a->name);
                    ex_jp(&full, ",\"frames\":%d,\"loop\":%s,\"fps\":%d,\"travel_x\":%.0f,\"travel_z\":%.0f,\"exported\":%s", a->nframes,
                          a->loop ? "true" : "false", a->fps, a->travel_x, a->travel_z, sel[n] ? "true" : "false");
                    if (aok[n]) { ex_jp(&full, ",\"gltf\":\"animations/"); cv_buf_put(&full, a->name, strlen(a->name)); ex_jp(&full, ".gltf\""); }
                    ex_jp(&full, "}");
                    first = 0;
                }
            }
            ex_jp(&full, "\n],\"characters\":[\n");
            cv_buf_put(&full, index.p, index.len);
            ex_jp(&full, "\n]}\n");
            snprintf(path, sizeof path, "%s/characters.json", out_dir);
            if (ex_write_file(path, full.p, full.len)) { ex_logf(log, user, "cannot write %s", path); nfailed++; }
            cv_buf_free(&full);
        }
        ex_logf(log, user, "characters: %d bodies, %d heads exported, %d failed, %llu triangles (default scenes), %d animations in animations/ -> %s",
                nbody, nhead, nfailed, (unsigned long long)gtris, nshared, out_dir);
        cv_buf_free(&index);
        ex_texcache_close(&tc);
        cv_buf_free(&cd);
        free(rom); free(ce); free(ad);
        return nfailed;
    }
refused:
    cv_buf_free(&cd);
    free(rom); free(ce); free(ad);
    return 1000;
}
