/* level_export.c -- static level export (issue #150): decor file -> glTF 2.0.
 *
 * CLEAN ROOM: written from docs/specs/level-decor.md and display-lists.md
 * only (plus the converter library's file table, rarezip inflate and ROM
 * check). See export.h. Binary output is little-endian (every supported host
 * is little-endian; glTF buffers are little-endian).
 */
#include "export.h"
#include "convert.h"
#include "conv_romcheck.h"

#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#else
#include <sys/types.h>
#endif

/* ------------------------------------------------------------------------ */
/* small helpers                                                            */
/* ------------------------------------------------------------------------ */

static uint32_t be32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
static int16_t be16s(const uint8_t *p) { return (int16_t)(((unsigned)p[0] << 8) | p[1]); }
static float bef32(const uint8_t *p) { uint32_t u = be32(p); float f; memcpy(&f, &u, 4); return f; }

static int grow(void **arr, size_t *cap, size_t need, size_t esz)
{
    if (need <= *cap) return 0;
    size_t nc = *cap ? *cap * 2 : 16;
    while (nc < need) nc *= 2;
    void *p = realloc(*arr, nc * esz);
    if (!p) return -1;
    *arr = p; *cap = nc;
    return 0;
}

static void jp(CvBuf *b, const char *fmt, ...)
{
    char tmp[2048];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if ((size_t)n >= sizeof tmp) n = (int)sizeof tmp - 1;
    cv_buf_put(b, tmp, (size_t)n);
}

static void jstr(CvBuf *b, const char *s)
{
    cv_buf_put(b, "\"", 1);
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') { char e[2] = { '\\', (char)c }; cv_buf_put(b, e, 2); }
        else if (c < 0x20 || c >= 0x7F) { char e[8]; snprintf(e, sizeof e, "\\u%04x", c); cv_buf_put(b, e, 6); }
        else cv_buf_put(b, &c, 1);
    }
    cv_buf_put(b, "\"", 1);
}

static int mkdir_one(const char *p)
{
#ifdef _WIN32
    return _mkdir(p);
#else
    return mkdir(p, 0777);
#endif
}

static int mkdir_p(const char *path)
{
    char buf[1024];
    size_t n = strlen(path);
    if (n == 0 || n >= sizeof buf) return -1;
    memcpy(buf, path, n + 1);
    for (size_t i = 1; i < n; i++) {
        if (buf[i] == '/' || buf[i] == '\\') {
            char c = buf[i];
            if (buf[i - 1] != ':') { buf[i] = 0; mkdir_one(buf); }
            buf[i] = c;
        }
    }
    if (mkdir_one(buf) != 0 && errno != EEXIST) return -1;
    return 0;
}

static int write_file(const char *path, const void *d, size_t n)
{
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    size_t w = n ? fwrite(d, 1, n, f) : 0;
    int rc = fclose(f);
    return (w == n && rc == 0) ? 0 : -1;
}

/* ------------------------------------------------------------------------ */
/* level table (level-decor.md)                                             */
/* ------------------------------------------------------------------------ */

typedef struct { const char *file; const char *names[4]; double scale; } LevelDef;

static const LevelDef k_levels[] = {
    { "bg_dam_all_p",  { "dam" },                            0.23363999 },
    { "bg_ark_all_p",  { "facility" },                       1.20648 },
    { "bg_run_all_p",  { "runway" },                         0.089571431 },
    { "bg_sevx_all_p", { "surface1", "surface2" },           0.45445713 },
    { "bg_sev_all_p",  { "bunker1" },                        0.53931433 },
    { "bg_silo_all_p", { "silo" },                           0.47256002 },
    { "bg_dest_all_p", { "frigate" },                        0.44757429 },
    { "bg_sevb_all_p", { "bunker2" },                        0.53931433 },
    { "bg_stat_all_p", { "statue" },                         0.107202865 },
    { "bg_arch_all_p", { "archives" },                       0.50678575 },
    { "bg_pete_all_p", { "streets" },                        0.34187999 },
    { "bg_depo_all_p", { "depot" },                          0.21847887 },
    { "bg_tra_all_p",  { "train" },                          0.15019713 },
    { "bg_jun_all_p",  { "jungle" },                         0.094662853 },
    { "bg_arec_all_p", { "control" },                        0.49886572 },
    { "bg_cave_all_p", { "caverns" },                        0.26824287 },
    { "bg_crad_all_p", { "cradle" },                         0.23571429 },
    { "bg_azt_all_p",  { "aztec" },                          0.35300568 },
    { "bg_cryp_all_p", { "egypt" },                          0.25608 },
    { "bg_len_all_p",  { "cuba" },                           0.094662853 },
    { "bg_dish_all_p", { "temple" },                         0.47142857 },
    { "bg_ref_all_p",  { "complex" },                        0.94285715 },
    { "bg_cat_all_p",  { "citadel" },                        0.76852286 },
    { "bg_ame_all_p",  { "basement", "stack", "library" },   0.65999997 },
    { "bg_oat_all_p",  { "caves" },                          0.14142857 },
    { "bg_sho_all_p",  { "sho" },                            0.528 },
    { "bg_eld_all_p",  { "eld" },                            0.94285715 },
    { "bg_lue_all_p",  { "lue" },                            0.94285715 },
    { "bg_rit_all_p",  { "rit" },                            0.94285715 },
    { "bg_ear_all_p",  { "ear" },                            0.94285715 },
    { "bg_lee_all_p",  { "lee" },                            0.94285715 },
    { "bg_lip_all_p",  { "lip" },                            0.94285715 },
    { "bg_wax_all_p",  { "wax" },                            0.94285715 },
    { "bg_pam_all_p",  { "pam" },                            0.94285715 },
};
#define N_LEVELDEFS ((int)(sizeof k_levels / sizeof k_levels[0]))

/* ------------------------------------------------------------------------ */
/* export state                                                             */
/* ------------------------------------------------------------------------ */

typedef struct {   /* all uint32 so memcmp is a valid comparison */
    uint32_t textured;      /* 1: baseColorTexture */
    uint32_t texnum;        /* 0xFFFFFFFF: none */
    uint32_t decode_fail;   /* texture wanted but could not be decoded */
    uint32_t list;          /* 0 primary, 1 secondary */
    uint32_t type, smode, tmode, offset, shifts, shiftt, min, second;
    uint32_t comb0, comb1, omh, oml, geom;
    uint32_t env, prim;
    uint32_t ks, kt;        /* raw texture scale values (textured only) */
} MatKey;

#define GEOM_TEXGEN        0x00040000u   /* texture generation: vertex colour bytes are a normal */
#define GEOM_TEXGEN_LINEAR 0x00020000u

/* ---- combine-mode helpers (display-lists.md "Combine mode: the parts that matter") ---- */

/* Does the alpha equation of the last cycle use shade alpha in the way the game rewrites to environment alpha
 * in the secondary list: TEXEL0*SHADE (one cycle), COMBINED*SHADE (cycle 1), or plain SHADE? */
static int uses_env_alpha(const MatKey *k)
{
    if (!k->list) return 0;
    int two = ((k->omh >> 20) & 3) == 1;
    uint32_t a, b, c, d, x;
    if (two) {
        a = (k->comb1 >> 21) & 7; b = (k->comb1 >> 3) & 7; c = (k->comb1 >> 18) & 7; d = k->comb1 & 7; x = 0;
    } else {
        a = (k->comb0 >> 12) & 7; b = (k->comb1 >> 12) & 7; c = (k->comb0 >> 9) & 7; d = (k->comb1 >> 9) & 7; x = 1;
    }
    if (a == x && b == 7 && c == 4 && d == 7) return 1;       /* X * SHADE */
    if (d == 4 && (c == 7 || a == b)) return 1;               /* plain SHADE */
    return 0;
}


typedef struct {
    MatKey k;
    int tw, th, expl;       /* of the decoded texture (textured only) */
    char err[128];
} Material;

typedef struct {
    int texnum;
    int ok;
    ExTexture t;
    char err[128];
} TexEnt;

typedef struct { int16_t v[5]; uint8_t c[4]; } VKey;   /* x y z s t rgba: 14 bytes, no padding */

typedef struct {
    int mat;
    float *pos; float *uv; uint8_t *col; VKey *key;
    size_t nv, capv;
    uint32_t *idx; size_t ni, capi;
    uint32_t *ht; size_t hcap;      /* open addressing, value = vertex index + 1 */
} Prim;

typedef struct {
    int idx;
    float rp[3];
    Prim *prims; size_t np, cap;
    size_t last;
    uint64_t tris;
    uint64_t tail;
    uint64_t bad_tris, bad_vtx;
} Room;

typedef struct {
    const uint8_t *rom; size_t rom_size;
    const char *region;
    ExTexTable tbl; int tbl_ok; const char *tbl_err;

    /* per level */
    Material *mats; size_t nm, capm;
    TexEnt *tex; size_t nt, capt;
    Room *rooms; int nrooms;
    uint64_t census[256];
    uint64_t issued, dropped_tri, dropped_vtx, no_end_lists, tail_total;
    int oom;
} LC;

static const uint8_t k_known[] = { 0x04, 0xBF, 0xB1, 0x06, 0xBB, 0xB7, 0xB6, 0xBA, 0xB9, 0xFC, 0xFA, 0xFB,
                                   0xE7, 0xE6, 0xE8, 0xE9, 0xC0, 0xB8 };
static int is_known(int op)
{
    for (size_t i = 0; i < sizeof k_known; i++) if (k_known[i] == op) return 1;
    return 0;
}

/* ---- texture cache ---- */
static TexEnt *get_tex(LC *lc, int texnum)
{
    for (size_t i = 0; i < lc->nt; i++) if (lc->tex[i].texnum == texnum) return &lc->tex[i];
    if (grow((void **)&lc->tex, &lc->capt, lc->nt + 1, sizeof *lc->tex)) { lc->oom = 1; return NULL; }
    TexEnt *e = &lc->tex[lc->nt++];
    memset(e, 0, sizeof *e);
    e->texnum = texnum;
    if (!lc->tbl_ok) {
        snprintf(e->err, sizeof e->err, "textures unavailable for region %s: %s", lc->region,
                 lc->tbl_err ? lc->tbl_err : "no texture table");
    } else {
        const char *err = NULL;
        if (ex_tex_decode(&lc->tbl, texnum, &e->t, &err) == 0 && e->t.rgba && e->t.width > 0 && e->t.height > 0)
            e->ok = 1;
        else {
            if (e->t.rgba) ex_tex_free(&e->t);
            snprintf(e->err, sizeof e->err, "%s", err ? err : "decode failed");
        }
    }
    return e;
}

/* ---- display-list interpreter ---- */

typedef struct { int16_t x, y, z, s, t; uint8_t c[4]; int valid; } CVtx;

typedef struct {
    uint32_t geom, omh, oml, c0, c1, env, prim;
    int have_tex;
    uint32_t texnum, type, smode, tmode, offset, shifts, shiftt, min, second;
    uint32_t ks, kt;
    CVtx cache[16];
    int mat;   /* -1: dirty */
} DS;

static double scale_of(uint32_t raw) { return raw == 0xFFFFu ? 1.0 : raw / 65536.0; }

static int alpha_mode(uint32_t oml)
{
    if (oml & 0x4000u) return 2;                         /* BLEND */
    if ((oml & 0x1000u) || (oml & 3u) == 1u) return 1;   /* MASK */
    return 0;                                            /* OPAQUE */
}

static int resolve_material(LC *lc, const DS *s, int list)
{
    MatKey k;
    memset(&k, 0, sizeof k);
    k.texnum = 0xFFFFFFFFu;
    k.list = (uint32_t)list;
    k.comb0 = s->c0; k.comb1 = s->c1; k.omh = s->omh; k.oml = s->oml; k.geom = s->geom;
    k.env = s->env; k.prim = s->prim;

    uint32_t a0 = (s->c0 >> 20) & 0xF, c0 = (s->c0 >> 15) & 0x1F, b0 = (s->c1 >> 28) & 0xF, d0 = (s->c1 >> 15) & 7;
    int cand = s->have_tex && (a0 == 1 || c0 == 1 || b0 == 1 || d0 == 1);
    int tw = 0, th = 0, expl = 0;
    char err[128] = "";
    if (cand) {
        TexEnt *te = get_tex(lc, (int)s->texnum);
        if (!te) return -1;
        k.texnum = s->texnum; k.type = s->type; k.smode = s->smode; k.tmode = s->tmode; k.offset = s->offset;
        k.shifts = s->shifts; k.shiftt = s->shiftt; k.min = s->min; k.second = s->second;
        if (te->ok) {
            k.textured = 1; k.ks = s->ks; k.kt = s->kt;
            tw = te->t.width; th = te->t.height; expl = te->t.explicit_lods;
        } else {
            k.decode_fail = 1;
            snprintf(err, sizeof err, "%s", te->err);
        }
    }
    for (size_t i = 0; i < lc->nm; i++)
        if (memcmp(&lc->mats[i].k, &k, sizeof k) == 0) return (int)i;
    if (grow((void **)&lc->mats, &lc->capm, lc->nm + 1, sizeof *lc->mats)) { lc->oom = 1; return -1; }
    Material *m = &lc->mats[lc->nm];
    memset(m, 0, sizeof *m);
    m->k = k; m->tw = tw; m->th = th; m->expl = expl;
    memcpy(m->err, err, sizeof m->err);
    return (int)lc->nm++;
}

static Prim *find_prim(LC *lc, Room *rm, int mat)
{
    if (rm->np && rm->prims[rm->last].mat == mat) return &rm->prims[rm->last];
    for (size_t i = 0; i < rm->np; i++)
        if (rm->prims[i].mat == mat) { rm->last = i; return &rm->prims[i]; }
    if (grow((void **)&rm->prims, &rm->cap, rm->np + 1, sizeof *rm->prims)) { lc->oom = 1; return NULL; }
    Prim *p = &rm->prims[rm->np];
    memset(p, 0, sizeof *p);
    p->mat = mat;
    rm->last = rm->np++;
    return p;
}

static uint32_t vhash(const VKey *k)
{
    const uint8_t *b = (const uint8_t *)k;
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < sizeof *k; i++) { h ^= b[i]; h *= 16777619u; }
    return h;
}

static int prim_rehash(Prim *p, size_t ncap)
{
    uint32_t *nh = (uint32_t *)calloc(ncap, sizeof *nh);
    if (!nh) return -1;
    for (size_t i = 0; i < p->nv; i++) {
        size_t j = vhash(&p->key[i]) & (ncap - 1);
        while (nh[j]) j = (j + 1) & (ncap - 1);
        nh[j] = (uint32_t)i + 1;
    }
    free(p->ht);
    p->ht = nh; p->hcap = ncap;
    return 0;
}

/* returns the vertex index in the primitive, or -1 on OOM */
static long prim_vertex(Prim *p, const CVtx *v, const Material *m, double scale, const DS *s)
{
    VKey k;
    memset(&k, 0, sizeof k);
    k.v[0] = v->x; k.v[1] = v->y; k.v[2] = v->z;
    if (m->k.textured && !(m->k.geom & GEOM_TEXGEN)) { k.v[3] = v->s; k.v[4] = v->t; }
    memcpy(k.c, v->c, 4);
    if ((p->nv + 1) * 2 > p->hcap && prim_rehash(p, p->hcap ? p->hcap * 2 : 64)) return -1;
    size_t j = vhash(&k) & (p->hcap - 1);
    while (p->ht[j]) {
        if (memcmp(&p->key[p->ht[j] - 1], &k, sizeof k) == 0) return (long)p->ht[j] - 1;
        j = (j + 1) & (p->hcap - 1);
    }
    size_t oc = p->capv;
    if (p->nv + 1 > oc) {
        size_t nc = oc ? oc * 2 : 64;
        float *pos = (float *)realloc(p->pos, nc * 3 * sizeof(float));
        if (pos) p->pos = pos;
        float *uv = (float *)realloc(p->uv, nc * 2 * sizeof(float));
        if (uv) p->uv = uv;
        uint8_t *col = (uint8_t *)realloc(p->col, nc * 4);
        if (col) p->col = col;
        VKey *key = (VKey *)realloc(p->key, nc * sizeof(VKey));
        if (key) p->key = key;
        if (!pos || !uv || !col || !key) return -1;
        p->capv = nc;
    }
    size_t i = p->nv++;
    p->key[i] = k;
    p->pos[i * 3 + 0] = (float)(v->x / scale);
    p->pos[i * 3 + 1] = (float)(v->y / scale);
    p->pos[i * 3 + 2] = (float)(v->z / scale);
    if (m->k.textured && (m->k.geom & GEOM_TEXGEN)) {
        /* texture generation: the colour bytes are a normal (signed x, y, z); fixed front-view reflection */
        double nx = (int8_t)v->c[0] / 127.0, ny = (int8_t)v->c[1] / 127.0;
        p->uv[i * 2 + 0] = (float)(0.5 + 0.5 * nx);
        p->uv[i * 2 + 1] = (float)(0.5 - 0.5 * ny);
    } else if (m->k.textured) {
        double o = (m->k.offset == 2 && !m->expl) ? 0.5 : 0.0;
        p->uv[i * 2 + 0] = (float)((v->s / 32.0 * scale_of(s->ks) - o) / m->tw);
        p->uv[i * 2 + 1] = (float)((v->t / 32.0 * scale_of(s->kt) - o) / m->th);
    } else {
        p->uv[i * 2 + 0] = p->uv[i * 2 + 1] = 0.f;
    }
    memcpy(&p->col[i * 4], v->c, 4);
    p->ht[j] = (uint32_t)i + 1;
    return (long)i;
}

static void emit_tri(LC *lc, Room *rm, DS *s, int list, double scale, int a, int b, int c)
{
    lc->issued++;
    if (a > 15 || b > 15 || c > 15 || !s->cache[a].valid || !s->cache[b].valid || !s->cache[c].valid) {
        lc->dropped_tri++; rm->bad_tris++;
        return;
    }
    if (s->mat < 0) {
        s->mat = resolve_material(lc, s, list);
        if (s->mat < 0) return;
    }
    Prim *p = find_prim(lc, rm, s->mat);
    if (!p) return;
    const Material *m = &lc->mats[s->mat];
    long ia = prim_vertex(p, &s->cache[a], m, scale, s);
    long ib = prim_vertex(p, &s->cache[b], m, scale, s);
    long ic = prim_vertex(p, &s->cache[c], m, scale, s);
    if (ia < 0 || ib < 0 || ic < 0) { lc->oom = 1; return; }
    if (grow((void **)&p->idx, &p->capi, p->ni + 3, sizeof *p->idx)) { lc->oom = 1; return; }
    p->idx[p->ni++] = (uint32_t)ia; p->idx[p->ni++] = (uint32_t)ib; p->idx[p->ni++] = (uint32_t)ic;
    rm->tris++;
}

static void run_list(LC *lc, Room *rm, const uint8_t *dl, size_t len, int list, const uint8_t *vtx, size_t vtxlen,
                     double scale)
{
    DS s;
    memset(&s, 0, sizeof s);
    s.ks = s.kt = 0xFFFF;
    s.mat = -1;
    size_t n = len / 8;
    int ended = 0;
    for (size_t i = 0; i < n && !lc->oom; i++) {
        uint32_t w0 = be32(dl + i * 8), w1 = be32(dl + i * 8 + 4);
        unsigned op = w0 >> 24;
        lc->census[op]++;
        switch (op) {
        case 0xB8: {
            uint64_t tail = len - (i + 1) * 8;
            ended = 1;
            rm->tail += tail; lc->tail_total += tail;
            i = n;   /* stop */
            break;
        }
        case 0x04: {
            unsigned count = (w0 & 0xFFFF) / 16, first = (w0 >> 16) & 0xF;
            unsigned seg = w1 >> 24;
            size_t off = (w1 & 0xFFFFFF) / 16;
            if (seg != 0x0E || first + count > 16 || (off + count) * 16 > vtxlen) {
                lc->dropped_vtx++; rm->bad_vtx++;
                break;
            }
            for (unsigned k = 0; k < count; k++) {
                const uint8_t *v = vtx + (off + k) * 16;
                CVtx *c = &s.cache[first + k];
                c->x = be16s(v); c->y = be16s(v + 2); c->z = be16s(v + 4);
                c->s = be16s(v + 8); c->t = be16s(v + 10);
                memcpy(c->c, v + 12, 4);
                c->valid = 1;
            }
            break;
        }
        case 0xBF:
            emit_tri(lc, rm, &s, list, scale, (int)(((w1 >> 16) & 0xFF) / 10), (int)(((w1 >> 8) & 0xFF) / 10),
                     (int)((w1 & 0xFF) / 10));
            break;
        case 0xB1:
            for (int k = 0; k < 4; k++) {
                int a = (int)((w1 >> (8 * k)) & 0xF), b = (int)((w1 >> (8 * k + 4)) & 0xF), c = (int)((w0 >> (4 * k)) & 0xF);
                if (!a && !b && !c) continue;
                emit_tri(lc, rm, &s, list, scale, a, b, c);
            }
            break;
        case 0x06: break;   /* counted in the census, not followed */
        case 0xBB: s.ks = (w1 >> 16) & 0xFFFF; s.kt = w1 & 0xFFFF; s.mat = -1; break;
        case 0xB7: s.geom |= w1; s.mat = -1; break;
        case 0xB6: s.geom &= ~w1; s.mat = -1; break;
        case 0xBA: case 0xB9: {
            unsigned sh = (w0 >> 8) & 0xFF, ln = w0 & 0xFF;
            if (sh < 32 && ln > 0) {
                uint32_t mask = (ln >= 32 ? 0xFFFFFFFFu : ((1u << ln) - 1u)) << sh;   /* w1 is pre-positioned */
                if (op == 0xBA) s.omh = (s.omh & ~mask) | (w1 & mask);
                else            s.oml = (s.oml & ~mask) | (w1 & mask);
            }
            s.mat = -1;
            break;
        }
        case 0xFC: s.c0 = w0 & 0xFFFFFF; s.c1 = w1; s.mat = -1; break;
        case 0xFA: s.prim = w1; s.mat = -1; break;
        case 0xFB: s.env = w1; s.mat = -1; break;
        case 0xC0:
            s.have_tex = 1;
            s.texnum = w1 & 0xFFF; s.second = (w1 >> 12) & 0xFFF; s.min = (w1 >> 24) & 0xFF;
            s.type = w0 & 7; s.smode = (w0 >> 22) & 3; s.tmode = (w0 >> 20) & 3; s.offset = (w0 >> 18) & 3;
            s.shifts = (w0 >> 14) & 0xF; s.shiftt = (w0 >> 10) & 0xF;
            s.mat = -1;
            break;
        default: break;   /* syncs and unknown opcodes: skipped (counted in the census) */
        }
    }
    if (!ended) lc->no_end_lists++;
}

/* ------------------------------------------------------------------------ */
/* decor file                                                               */
/* ------------------------------------------------------------------------ */

static void lc_reset_level(LC *lc)
{
    for (int r = 0; r < lc->nrooms; r++) {
        Room *rm = &lc->rooms[r];
        for (size_t i = 0; i < rm->np; i++) {
            Prim *p = &rm->prims[i];
            free(p->pos); free(p->uv); free(p->col); free(p->key); free(p->idx); free(p->ht);
        }
        free(rm->prims);
    }
    free(lc->rooms); lc->rooms = NULL; lc->nrooms = 0;
    for (size_t i = 0; i < lc->nt; i++) if (lc->tex[i].ok) ex_tex_free(&lc->tex[i].t);
    free(lc->tex); lc->tex = NULL; lc->nt = lc->capt = 0;
    free(lc->mats); lc->mats = NULL; lc->nm = lc->capm = 0;
    memset(lc->census, 0, sizeof lc->census);
    lc->issued = lc->dropped_tri = lc->dropped_vtx = lc->no_end_lists = lc->tail_total = 0;
    lc->oom = 0;
}

/* Inflates the block starting at file offset `off` (to the end of the file).
 * 0 ok, 1 absent (offset 0), -1 failure. */
static int inflate_block(const uint8_t *file, size_t fsize, uint32_t off, CvBuf *out)
{
    memset(out, 0, sizeof *out);
    if (!off) return 1;
    if (off >= fsize) return -1;
    return cv_rz_inflate(file + off, fsize - off, out) == 0 ? 0 : -1;
}

/* ------------------------------------------------------------------------ */
/* glTF writer                                                              */
/* ------------------------------------------------------------------------ */

typedef struct {
    CvBuf bin, views, accs;
    int nviews, naccs;
} GW;

static int gw_view(GW *g, const void *d, size_t n, int target)
{
    while (g->bin.len & 3) { uint8_t z = 0; cv_buf_put(&g->bin, &z, 1); }
    size_t off = g->bin.len;
    cv_buf_put(&g->bin, d, n);
    jp(&g->views, "%s{\"buffer\":0,\"byteOffset\":%zu,\"byteLength\":%zu,\"target\":%d}", g->nviews ? "," : "", off, n, target);
    return g->nviews++;
}

static int gw_acc(GW *g, int view, int ctype, size_t count, const char *type, int norm, const float *mn, const float *mx, int ncomp)
{
    jp(&g->accs, "%s{\"bufferView\":%d,\"componentType\":%d,\"count\":%zu,\"type\":\"%s\"", g->naccs ? "," : "", view, ctype, count, type);
    if (norm) jp(&g->accs, ",\"normalized\":true");
    if (mn) {
        jp(&g->accs, ",\"min\":[");
        for (int i = 0; i < ncomp; i++) jp(&g->accs, "%s%.9g", i ? "," : "", mn[i]);
        jp(&g->accs, "],\"max\":[");
        for (int i = 0; i < ncomp; i++) jp(&g->accs, "%s%.9g", i ? "," : "", mx[i]);
        jp(&g->accs, "]");
    }
    jp(&g->accs, "}");
    return g->naccs++;
}

static const char *alpha_name(int a) { return a == 2 ? "BLEND" : a == 1 ? "MASK" : "OPAQUE"; }
static int wrap_of(uint32_t m) { return m == 1 ? 33071 : m == 2 ? 33648 : 10497; }

typedef struct { int texnum, ws, wt; } GTex;

static void write_rgba(CvBuf *j, uint32_t c) { jp(j, "[%u,%u,%u,%u]", c >> 24, (c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF); }

static void rep(CvBuf *r, const char *fmt, ...)
{
    char tmp[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if ((size_t)n >= sizeof tmp) n = (int)sizeof tmp - 1;
    cv_buf_put(r, tmp, (size_t)n);
}

/* ------------------------------------------------------------------------ */
/* one level                                                                */
/* ------------------------------------------------------------------------ */

typedef struct { int rooms; uint64_t tris, issued; size_t mats, textures; int failed; char msg[1300]; } LevelResult;

static int export_level(LC *lc, const CvFileRow *row, const char *file, const char *name, double scale, int scale_assumed,
                        const char *out_dir, LevelResult *res)
{
    CvBuf rpt, json, comp[3], nodes, meshes, mats, texs, imgs, smps;
    GW g;
    GTex *gt = NULL; size_t ngt = 0, capgt = 0;
    int *imgtex = NULL; size_t nimg = 0, capimg = 0;
    memset(&rpt, 0, sizeof rpt); memset(&json, 0, sizeof json); memset(&g, 0, sizeof g);
    memset(comp, 0, sizeof comp);
    memset(&nodes, 0, sizeof nodes); memset(&meshes, 0, sizeof meshes); memset(&mats, 0, sizeof mats);
    memset(&texs, 0, sizeof texs); memset(&imgs, 0, sizeof imgs); memset(&smps, 0, sizeof smps);
    lc_reset_level(lc);
    int fail = 0, n = 0, nmesh = 0, smap[9], nsmp = 0;
    size_t ntexfiles = 0;
    uint64_t tot_tris = 0;
    char path[1200], dir[1024];
    const uint8_t *fd;
    size_t fsize, maxrec, first_zero = 0;
    uint32_t rt;

#define FAIL(...) do { snprintf(res->msg, sizeof res->msg, __VA_ARGS__); fail = 1; goto done; } while (0)

    for (int i = 0; i < 9; i++) smap[i] = -1;
    snprintf(dir, sizeof dir, "%s/%s", out_dir, name);
    if (row->size < 20 || (size_t)row->off + row->size > lc->rom_size) FAIL("%s: file outside the ROM", file);
    fd = lc->rom + row->off;
    fsize = row->size;

    if (be32(fd) != 0 || be32(fd + 16) != 0) FAIL("%s: header words 0/4 not zero (different layout)", file);
    rt = be32(fd + 4);
    if ((rt >> 24) != 0x0F) FAIL("%s: room table is not a segment 0x0F address", file);
    rt &= 0xFFFFFF;

    /* room records: scan from 1 to the first record with pri == 0 */
    maxrec = (fsize > rt) ? (fsize - rt) / 24 : 0;
    for (size_t i = 1; i < maxrec; i++) {
        if (be32(fd + rt + i * 24 + 4) == 0) { first_zero = i; break; }
    }
    if (!first_zero) FAIL("%s: room table has no terminator", file);
    n = (int)first_zero - 1;              /* records scanned; record n is the end marker */
    if (n < 2) FAIL("%s: no rooms", file);
    lc->nrooms = n - 1;                   /* rooms 1 .. n-1 */
    lc->rooms = (Room *)calloc((size_t)lc->nrooms, sizeof(Room));
    if (!lc->rooms) FAIL("out of memory");

    for (int r = 1; r <= n - 1; r++) {
        Room *rm = &lc->rooms[r - 1];
        const uint8_t *rec = fd + rt + (size_t)r * 24;
        uint32_t vo = be32(rec) ? (be32(rec) & 0xFFFFFF) : 0;
        uint32_t po = be32(rec + 4) ? (be32(rec + 4) & 0xFFFFFF) : 0;
        uint32_t so = be32(rec + 8) ? (be32(rec + 8) & 0xFFFFFF) : 0;
        rm->idx = r;
        for (int k = 0; k < 3; k++) rm->rp[k] = bef32(rec + 12 + 4 * k);
        for (int k = 0; k < 3; k++) cv_buf_free(&comp[k]);
        int rv = inflate_block(fd, fsize, vo, &comp[0]);
        int rp = inflate_block(fd, fsize, po, &comp[1]);
        int rs = inflate_block(fd, fsize, so, &comp[2]);
        if (rv < 0 || rp < 0 || rs < 0)
            FAIL("room %d: a block does not decompress (vtx %d pri %d sec %d)", r, rv, rp, rs);
        if (rp == 0) run_list(lc, rm, comp[1].p, comp[1].len, 0, comp[0].p, comp[0].len, scale);
        if (rs == 0) run_list(lc, rm, comp[2].p, comp[2].len, 1, comp[0].p, comp[0].len, scale);
        if (lc->oom) FAIL("out of memory");
    }

    for (int r = 0; r < lc->nrooms; r++) tot_tris += lc->rooms[r].tris;

    /* ---- textures: PNGs for those used by textured materials ---- */
    snprintf(path, sizeof path, "%s/textures", dir);
    if (mkdir_p(path)) FAIL("cannot create %s", path);
    for (size_t i = 0; i < lc->nt; i++) {
        TexEnt *te = &lc->tex[i];
        int used = 0;
        for (size_t m = 0; m < lc->nm; m++)
            if (lc->mats[m].k.textured && lc->mats[m].k.texnum == (uint32_t)te->texnum) used = 1;
        if (!te->ok || !used) continue;
        snprintf(path, sizeof path, "%s/textures/%d.png", dir, te->texnum);
        if (ex_png_write(path, te->t.width, te->t.height, te->t.rgba)) FAIL("cannot write %s", path);
        ntexfiles++;
    }

    /* ---- glTF: meshes + nodes ---- */
    for (int r = 0; r < lc->nrooms; r++) {
        Room *rm = &lc->rooms[r];
        int mesh = -1;
        if (rm->np) {
            CvBuf pj;
            int nprim = 0;
            memset(&pj, 0, sizeof pj);
            for (size_t pi = 0; pi < rm->np; pi++) {
                Prim *p = &rm->prims[pi];
                if (!p->ni) continue;
                const Material *m = &lc->mats[p->mat];
                float mn[3] = { p->pos[0], p->pos[1], p->pos[2] }, mx[3] = { p->pos[0], p->pos[1], p->pos[2] };
                for (size_t v = 1; v < p->nv; v++)
                    for (int k = 0; k < 3; k++) {
                        float f = p->pos[v * 3 + k];
                        if (f < mn[k]) mn[k] = f;
                        if (f > mx[k]) mx[k] = f;
                    }
                int vp = gw_view(&g, p->pos, p->nv * 12, 34962);
                int ap = gw_acc(&g, vp, 5126, p->nv, "VEC3", 0, mn, mx, 3);
                int at = -1;
                if (m->k.textured) {
                    int vt = gw_view(&g, p->uv, p->nv * 8, 34962);
                    at = gw_acc(&g, vt, 5126, p->nv, "VEC2", 0, NULL, NULL, 0);
                }
                /* COLOR_0 is linear in glTF; the game's colour bytes are sRGB-encoded: convert R,G,B, keep alpha. */
                uint16_t *c16 = (uint16_t *)malloc(p->nv * 8);
                if (!c16) { cv_buf_free(&pj); FAIL("out of memory"); }
                for (size_t k = 0; k < p->nv; k++) {
                    for (int ch = 0; ch < 4; ch++) {
                        unsigned b = p->col[k * 4 + ch];
                        double lin = b / 255.0;
                        if (ch == 3 && uses_env_alpha(&m->k)) lin = 1.0;   /* alpha comes from the env colour */
                        if (ch < 3 && (m->k.geom & GEOM_TEXGEN)) lin = 1.0;   /* texgen: the bytes are a normal, colour is white */
                        else if (ch < 3) lin = lin <= 0.04045 ? lin / 12.92 : pow((lin + 0.055) / 1.055, 2.4);
                        long q = (long)(lin * 65535.0 + 0.5);
                        c16[k * 4 + ch] = (uint16_t)(q < 0 ? 0 : q > 65535 ? 65535 : q);
                    }
                }
                int vc = gw_view(&g, c16, p->nv * 8, 34962);
                free(c16);
                int ac = gw_acc(&g, vc, 5123, p->nv, "VEC4", 1, NULL, NULL, 0);
                int ai;
                if (p->nv - 1 > 65535) {
                    int vi = gw_view(&g, p->idx, p->ni * 4, 34963);   /* little-endian host */
                    ai = gw_acc(&g, vi, 5125, p->ni, "SCALAR", 0, NULL, NULL, 0);
                } else {
                    uint16_t *i16 = (uint16_t *)malloc(p->ni * 2);
                    if (!i16) { cv_buf_free(&pj); FAIL("out of memory"); }
                    for (size_t k = 0; k < p->ni; k++) i16[k] = (uint16_t)p->idx[k];
                    int vi = gw_view(&g, i16, p->ni * 2, 34963);
                    free(i16);
                    ai = gw_acc(&g, vi, 5123, p->ni, "SCALAR", 0, NULL, NULL, 0);
                }
                jp(&pj, "%s{\"attributes\":{\"POSITION\":%d", nprim ? "," : "", ap);
                if (at >= 0) jp(&pj, ",\"TEXCOORD_0\":%d", at);
                jp(&pj, ",\"COLOR_0\":%d},\"indices\":%d,\"material\":%d,\"mode\":4}", ac, ai, p->mat);
                nprim++;
            }
            if (nprim) {
                jp(&meshes, "%s{\"name\":\"room_%d\",\"primitives\":[", nmesh ? "," : "", rm->idx);
                cv_buf_put(&meshes, pj.p, pj.len);
                jp(&meshes, "]}");
                mesh = nmesh++;
            }
            cv_buf_free(&pj);
        }
        jp(&nodes, "%s{\"name\":\"room_%d\"", r ? "," : "", rm->idx);
        if (mesh >= 0) jp(&nodes, ",\"mesh\":%d", mesh);
        jp(&nodes, ",\"translation\":[%.9g,%.9g,%.9g],\"extras\":{\"room\":%d,\"room_position\":[%.9g,%.9g,%.9g]}}",
           (float)(rm->rp[0] / scale), (float)(rm->rp[1] / scale), (float)(rm->rp[2] / scale), rm->idx,
           rm->rp[0], rm->rp[1], rm->rp[2]);
    }

    /* ---- materials, textures, images, samplers ---- */
    for (size_t mi = 0; mi < lc->nm; mi++) {
        const Material *m = &lc->mats[mi];
        const MatKey *k = &m->k;
        int am = alpha_mode(k->oml);
        jp(&mats, "%s{\"name\":\"mat_%zu\",\"pbrMetallicRoughness\":{\"baseColorFactor\":[1,1,1,%.9g],\"metallicFactor\":0,\"roughnessFactor\":1",
           mi ? "," : "", mi, uses_env_alpha(k) ? (double)(k->env & 0xFF) / 255.0 : 1.0);
        if (k->textured) {
            size_t ti;
            uint32_t wsm = k->smode, wtm = k->tmode;
            for (ti = 0; ti < ngt; ti++)
                if (gt[ti].texnum == (int)k->texnum && gt[ti].ws == (int)wsm && gt[ti].wt == (int)wtm) break;
            if (ti == ngt) {
                if (grow((void **)&gt, &capgt, ngt + 1, sizeof *gt)) FAIL("out of memory");
                gt[ngt].texnum = (int)k->texnum; gt[ngt].ws = (int)wsm; gt[ngt].wt = (int)wtm; ngt++;
                size_t ii;
                for (ii = 0; ii < nimg; ii++) if (imgtex[ii] == (int)k->texnum) break;
                if (ii == nimg) {
                    if (grow((void **)&imgtex, &capimg, nimg + 1, sizeof *imgtex)) FAIL("out of memory");
                    imgtex[nimg++] = (int)k->texnum;
                    jp(&imgs, "%s{\"uri\":\"textures/%u.png\"}", ii ? "," : "", k->texnum);
                }
                int sk = (int)(wsm > 2 ? 0 : wsm) * 3 + (int)(wtm > 2 ? 0 : wtm);
                if (smap[sk] < 0) {
                    jp(&smps, "%s{\"magFilter\":9729,\"minFilter\":9987,\"wrapS\":%d,\"wrapT\":%d}", nsmp ? "," : "",
                       wrap_of(wsm), wrap_of(wtm));
                    smap[sk] = nsmp++;
                }
                jp(&texs, "%s{\"sampler\":%d,\"source\":%zu}", ti ? "," : "", smap[sk], ii);
            }
            jp(&mats, ",\"baseColorTexture\":{\"index\":%zu}", ti);
        }
        jp(&mats, "},\"alphaMode\":\"%s\"", alpha_name(am));
        if (am == 1) jp(&mats, ",\"alphaCutoff\":0.5");
        jp(&mats, ",\"doubleSided\":%s", (k->geom & 0x3000u) == 0 ? "true" : "false");
        jp(&mats, ",\"extensions\":{\"KHR_materials_unlit\":{}},\"extras\":{\"texnum\":%d",
           k->texnum == 0xFFFFFFFFu ? -1 : (int)k->texnum);
        if (k->geom & GEOM_TEXGEN) jp(&mats, ",\"texgen\":true%s", (k->geom & GEOM_TEXGEN_LINEAR) ? ",\"texgen_linear\":true" : "");
        if (k->texnum != 0xFFFFFFFFu && k->type == 1)
            jp(&mats, ",\"detail\":{\"texnum\":%u,\"shifts\":%u,\"shiftt\":%u}", k->second, k->shifts, k->shiftt);
        if (k->decode_fail) { jp(&mats, ",\"decode_error\":"); jstr(&mats, m->err); }
        jp(&mats, ",\"n64\":{\"combine\":[%u,%u],\"othermode\":[%u,%u],\"geometry_mode\":%u,\"list\":\"%s\"",
           k->comb0, k->comb1, k->omh, k->oml, k->geom, k->list ? "secondary" : "primary");
        if (k->texnum != 0xFFFFFFFFu)
            jp(&mats, ",\"texref\":{\"type\":%u,\"smode\":%u,\"tmode\":%u,\"offset\":%u,\"shifts\":%u,\"shiftt\":%u,\"min\":%u,\"second\":%u}",
               k->type, k->smode, k->tmode, k->offset, k->shifts, k->shiftt, k->min, k->second);
        if (k->textured) jp(&mats, ",\"texscale\":[%.9g,%.9g]", scale_of(k->ks), scale_of(k->kt));
        jp(&mats, ",\"env\":"); write_rgba(&mats, k->env);
        jp(&mats, ",\"prim\":"); write_rgba(&mats, k->prim);
        jp(&mats, "}");
        if (k->texnum == 1508 || k->texnum == 1511) jp(&mats, ",\"anim\":\"water\"");
        jp(&mats, "}}");
    }

    jp(&json, "{\"asset\":{\"version\":\"2.0\",\"generator\":\"project-dam export\",\"extras\":{\"exporter_version\":%d,\"rom_region\":",
       EX_EXPORTER_VERSION);
    jstr(&json, lc->region);
    jp(&json, ",\"level\":"); jstr(&json, name);
    jp(&json, ",\"decor_file\":"); jstr(&json, file);
    jp(&json, ",\"level_scale\":%.9g", scale);
    jp(&json, ",\"vertex_color_encoding\":\"linear (converted from the game's sRGB bytes)\"");
    if (scale_assumed) jp(&json, ",\"level_scale_note\":\"no level entry for this file; scale 1.0 assumed\"");
    jp(&json, "}},\"extensionsUsed\":[\"KHR_materials_unlit\"],\"scene\":0,\"scenes\":[{\"nodes\":[");
    for (int r = 0; r < lc->nrooms; r++) jp(&json, "%s%d", r ? "," : "", r);
    jp(&json, "]}],\"nodes\":["); cv_buf_put(&json, nodes.p, nodes.len); jp(&json, "]");
    if (nmesh) { jp(&json, ",\"meshes\":["); cv_buf_put(&json, meshes.p, meshes.len); jp(&json, "]"); }
    if (lc->nm) { jp(&json, ",\"materials\":["); cv_buf_put(&json, mats.p, mats.len); jp(&json, "]"); }
    if (ngt) {
        jp(&json, ",\"textures\":["); cv_buf_put(&json, texs.p, texs.len);
        jp(&json, "],\"images\":["); cv_buf_put(&json, imgs.p, imgs.len);
        jp(&json, "],\"samplers\":["); cv_buf_put(&json, smps.p, smps.len); jp(&json, "]");
    }
    if (g.naccs) {
        jp(&json, ",\"accessors\":["); cv_buf_put(&json, g.accs.p, g.accs.len);
        jp(&json, "],\"bufferViews\":["); cv_buf_put(&json, g.views.p, g.views.len); jp(&json, "]");
        jp(&json, ",\"buffers\":[{\"uri\":\"%s.bin\",\"byteLength\":%zu}]", name, g.bin.len);
    }
    jp(&json, "}\n");

    snprintf(path, sizeof path, "%s/%s.gltf", dir, name);
    if (write_file(path, json.p, json.len)) FAIL("cannot write %s", path);
    snprintf(path, sizeof path, "%s/%s.bin", dir, name);
    if (write_file(path, g.bin.p, g.bin.len)) FAIL("cannot write %s", path);

    /* ---- report.txt ---- */
    {
        size_t nfail = 0, opq_sec_m = 0;
        uint64_t opq_sec_tris = 0;
        int unknown = 0;
        rep(&rpt, "level %s  decor file %s  region %s  level_scale %.9g%s\n", name, file, lc->region, scale,
            scale_assumed ? " (assumed, no level entry)" : "");
        rep(&rpt, "rooms %d  triangles %llu  issued-by-lists %llu  materials %zu  textures-written %zu\n\n", lc->nrooms,
            (unsigned long long)tot_tris, (unsigned long long)lc->issued, lc->nm, ntexfiles);
        rep(&rpt, "per room (triangles, primitives, bytes after first end-of-list, dropped triangles, dropped vertex loads):\n");
        for (int r = 0; r < lc->nrooms; r++) {
            Room *rm = &lc->rooms[r];
            rep(&rpt, "  room %d: %llu tris, %zu prims, tail %llu, dropped-tri %llu, dropped-vtx %llu\n", rm->idx,
                (unsigned long long)rm->tris, rm->np, (unsigned long long)rm->tail, (unsigned long long)rm->bad_tris,
                (unsigned long long)rm->bad_vtx);
            for (size_t pi = 0; pi < rm->np; pi++) {
                const MatKey *k = &lc->mats[rm->prims[pi].mat].k;
                if (k->list == 1 && alpha_mode(k->oml) == 0) { opq_sec_m++; opq_sec_tris += rm->prims[pi].ni / 3; }
            }
        }
        rep(&rpt, "\nopcode census (up to each list's first end-of-list):\n");
        for (int op = 0; op < 256; op++)
            if (lc->census[op] && is_known(op)) rep(&rpt, "  0x%02X %llu\n", op, (unsigned long long)lc->census[op]);
        rep(&rpt, "\nunknown opcodes (skipped):\n");
        for (int op = 0; op < 256; op++)
            if (lc->census[op] && !is_known(op)) {
                rep(&rpt, "  0x%02X %llu\n", op, (unsigned long long)lc->census[op]);
                unknown++;
            }
        if (!unknown) rep(&rpt, "  none\n");
        rep(&rpt, "\ncall/jump commands (0x06, not followed): %llu\n", (unsigned long long)lc->census[0x06]);
        rep(&rpt, "bytes after the first end-of-list, all rooms: %llu\n", (unsigned long long)lc->tail_total);
        rep(&rpt, "lists without an end-of-list: %llu\n", (unsigned long long)lc->no_end_lists);
        rep(&rpt, "triangles dropped (empty or out-of-range cache slot): %llu\n", (unsigned long long)lc->dropped_tri);
        rep(&rpt, "vertex loads dropped (bad segment / slot / range): %llu\n", (unsigned long long)lc->dropped_vtx);
        rep(&rpt, "secondary-list groups classified OPAQUE: %zu primitives, %llu triangles\n", opq_sec_m,
            (unsigned long long)opq_sec_tris);
        {
            size_t nenv = 0, ntg = 0;
            for (size_t mi = 0; mi < lc->nm; mi++) { nenv += uses_env_alpha(&lc->mats[mi].k); ntg += (lc->mats[mi].k.geom & GEOM_TEXGEN) != 0; }
            rep(&rpt, "materials with shade alpha replaced by environment alpha: %zu\n", nenv);
            rep(&rpt, "texture-generated materials (geometry mode 0x40000): %zu\n", ntg);
        }
        rep(&rpt, "\ntexture decode failures:\n");
        for (size_t i = 0; i < lc->nt; i++)
            if (!lc->tex[i].ok) { rep(&rpt, "  texture %d: %s\n", lc->tex[i].texnum, lc->tex[i].err); nfail++; }
        if (!nfail) rep(&rpt, "  none\n");
        if (!lc->tbl_ok) rep(&rpt, "texture table unavailable (%s): the level is exported untextured\n", lc->tbl_err ? lc->tbl_err : "?");
        rep(&rpt, "\nspec deviations / choices:\n"
                  "  - other-mode commands 0xB9/0xBA: w1 is taken as already positioned at the shift (mask = ((1<<len)-1)<<shift)\n"
                  "  - texture scale 0xBB: 0xFFFF = 1.0 else value/65536; initial scale 1.0; the 'on' byte is ignored\n"
                  "  - each list starts with fresh state (geometry mode, other modes, combine, colours, texture all zero/none)\n"
                  "  - materials are keyed on the full geometry-mode word; textured materials also on the texture scale\n"
                  "  - 'textured' = a texture reference is current and any of a0,b0,c0,d0 of the cycle-0 colour combine is TEXEL0 (1)\n"
                  "  - untextured materials do not carry a texture reference (texnum -1)\n"
                  "  - secondary list: shade alpha -> env alpha (baseColorFactor[3], COLOR_0 alpha 1.0) when the last-cycle alpha equation is X*SHADE or plain SHADE\n"
                  "  - base texture is always the referenced (main) texture; type-1 detail number/shifts go to extras.detail\n"
                  "  - texture generation (0x40000): COLOR_0 white with the alpha byte, TEXCOORD_0 = 0.5+0.5*nx, 0.5-0.5*ny from the colour bytes as a signed normal\n");
    }
    snprintf(path, sizeof path, "%s/report.txt", dir);
    if (write_file(path, rpt.p, rpt.len)) FAIL("cannot write %s", path);

    res->rooms = lc->nrooms; res->tris = tot_tris; res->issued = lc->issued; res->mats = lc->nm; res->textures = ntexfiles;

done:
    for (int k = 0; k < 3; k++) cv_buf_free(&comp[k]);
    cv_buf_free(&rpt); cv_buf_free(&json); cv_buf_free(&g.bin); cv_buf_free(&g.views); cv_buf_free(&g.accs);
    cv_buf_free(&nodes); cv_buf_free(&meshes); cv_buf_free(&mats); cv_buf_free(&texs); cv_buf_free(&imgs); cv_buf_free(&smps);
    free(gt); free(imgtex);
    res->failed = fail;
    return fail;
#undef FAIL
}

/* ------------------------------------------------------------------------ */
/* all levels                                                               */
/* ------------------------------------------------------------------------ */

static void logf_(ExLogFn log, void *user, const char *fmt, ...)
{
    char tmp[512];
    va_list ap;
    if (!log) return;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    log(user, tmp);
}

static int read_rom(const char *path, uint8_t **out, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0) { fclose(f); return -1; }
    uint8_t *b = (uint8_t *)malloc((size_t)n);
    if (!b || fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); fclose(f); return -1; }
    fclose(f);
    *out = b; *len = (size_t)n;
    return 0;
}

int ex_export_levels(const char *rom_path, const char *out_dir, ExLogFn log, void *user)
{
    uint8_t *rom = NULL;
    size_t rom_size = 0;
    if (read_rom(rom_path, &rom, &rom_size)) { logf_(log, user, "cannot read ROM %s", rom_path); return 1; }
    const char *region = NULL;
    int chk = cv_romcheck(rom, (unsigned long)rom_size, &region);
    if (chk != CV_ROM_OK) { logf_(log, user, "ROM rejected: %s", cv_romcheck_message(chk)); free(rom); return 1; }
    const CvRegionData *rd = NULL;
    for (int i = 0; i < CV_NREGIONS; i++) if (!strcmp(cv_regions[i].region, region)) rd = &cv_regions[i];
    if (!rd) { logf_(log, user, "no file table for region %s", region); free(rom); return 1; }
    if (mkdir_p(out_dir)) { logf_(log, user, "cannot create %s", out_dir); free(rom); return 1; }

    LC *lc = (LC *)calloc(1, sizeof *lc);
    if (!lc) { free(rom); return 1; }
    lc->rom = rom; lc->rom_size = rom_size; lc->region = region;
    lc->tbl_ok = ex_tex_table_open(&lc->tbl, rom, rom_size, region, &lc->tbl_err) == 0;
    if (!lc->tbl_ok)
        logf_(log, user, "textures unavailable for region %s: %s (exporting untextured)", region, lc->tbl_err ? lc->tbl_err : "?");
    else if (strcmp(region, "ntsc-final"))
        logf_(log, user, "region %s: texture support is not verified", region);

    int nfailed = 0, nlevels = 0;
    uint64_t gtris = 0;
    size_t gtex = 0;
    for (int ri = 0; ri < rd->nfiles; ri++) {
        const CvFileRow *row = &rd->files[ri];
        const char *b = row->base;
        size_t bl = strlen(b);
        if (strncmp(b, "bg_", 3) || bl < 10 || strcmp(b + bl - 6, "_all_p")) continue;
        if (cv_find_row(rd, b) != row) continue;   /* duplicate row: last wins */
        if (row->size == 0) { logf_(log, user, "%s: size 0 in this ROM build, skipped", b); continue; }
        const LevelDef *def = NULL;
        for (int d = 0; d < N_LEVELDEFS; d++) if (!strcmp(k_levels[d].file, b)) def = &k_levels[d];
        char shortname[64];
        const char *names[4] = { NULL, NULL, NULL, NULL };
        double scale = 1.0;
        int assumed = 0;
        if (def) {
            for (int k = 0; k < 4; k++) names[k] = def->names[k];
            scale = def->scale;
        } else {
            size_t sl = bl - 9;
            if (sl >= sizeof shortname) sl = sizeof shortname - 1;
            memcpy(shortname, b + 3, sl);
            shortname[sl] = 0;
            names[0] = shortname;
            assumed = 1;
        }
        for (int k = 0; k < 4 && names[k]; k++) {
            LevelResult res;
            memset(&res, 0, sizeof res);
            nlevels++;
            if (export_level(lc, row, b, names[k], scale, assumed, out_dir, &res)) {
                nfailed++;
                logf_(log, user, "%-10s FAILED: %s", names[k], res.msg);
            } else {
                gtris += res.tris; gtex += res.textures;
                logf_(log, user, "%-10s rooms %3d  triangles %7llu (issued %llu)  materials %3zu  textures %3zu", names[k],
                      res.rooms, (unsigned long long)res.tris, (unsigned long long)res.issued, res.mats, res.textures);
            }
        }
    }
    logf_(log, user, "export: %d levels, %d failed, %llu triangles, %zu texture files -> %s", nlevels, nfailed,
          (unsigned long long)gtris, gtex, out_dir);
    lc_reset_level(lc);
    if (lc->tbl_ok) ex_tex_table_close(&lc->tbl);
    free(lc);
    free(rom);
    return nfailed;
}
