/* export_common.c -- code shared by the level and model exporters (see export_common.h).
 * CLEAN ROOM: written from docs/specs/ only. */
#include "export_common.h"
#include "conv_romcheck.h"

#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#else
#include <sys/types.h>
#endif

/* ------------------------------------------------------------------------ */
/* helpers                                                                  */
/* ------------------------------------------------------------------------ */

int ex_grow(void **arr, size_t *cap, size_t need, size_t esz)
{
    if (need <= *cap) return 0;
    size_t nc = *cap ? *cap * 2 : 16;
    while (nc < need) nc *= 2;
    void *p = realloc(*arr, nc * esz);
    if (!p) return -1;
    *arr = p; *cap = nc;
    return 0;
}

void ex_jp(CvBuf *b, const char *fmt, ...)
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

void ex_jstr(CvBuf *b, const char *s)
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

int ex_mkdir_p(const char *path)
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

int ex_write_file(const char *path, const void *d, size_t n)
{
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    size_t w = n ? fwrite(d, 1, n, f) : 0;
    int rc = fclose(f);
    return (w == n && rc == 0) ? 0 : -1;
}

int ex_read_rom(const char *path, uint8_t **out, size_t *len)
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

void ex_logf(ExLogFn log, void *user, const char *fmt, ...)
{
    char tmp[512];
    va_list ap;
    if (!log) return;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    log(user, tmp);
}

int ex_open_rom(const char *rom_path, ExLogFn log, void *user, uint8_t **rom, size_t *rom_size, const char **region,
                const CvRegionData **rd)
{
    uint8_t *r = NULL;
    size_t n = 0;
    if (ex_read_rom(rom_path, &r, &n)) { ex_logf(log, user, "cannot read ROM %s", rom_path); return 1; }
    const char *reg = NULL;
    int chk = cv_romcheck(r, (unsigned long)n, &reg);
    if (chk != CV_ROM_OK) { ex_logf(log, user, "ROM rejected: %s", cv_romcheck_message(chk)); free(r); return 1; }
    const CvRegionData *d = NULL;
    for (int i = 0; i < CV_NREGIONS; i++) if (!strcmp(cv_regions[i].region, reg)) d = &cv_regions[i];
    if (!d) { ex_logf(log, user, "no file table for region %s", reg); free(r); return 1; }
    *rom = r; *rom_size = n; *region = reg; *rd = d;
    return 0;
}

/* ------------------------------------------------------------------------ */
/* materials                                                                */
/* ------------------------------------------------------------------------ */

double ex_scale_of(uint32_t raw) { return raw == 0xFFFFu ? 1.0 : raw / 65536.0; }

int ex_alpha_mode(uint32_t oml)
{
    if (oml & 0x4000u) return 2;                         /* BLEND */
    if ((oml & 0x1000u) || (oml & 3u) == 1u) return 1;   /* MASK */
    return 0;                                            /* OPAQUE */
}

/* Does the alpha equation of the last cycle use shade alpha in the way the game rewrites to environment alpha
 * in the secondary list: TEXEL0*SHADE (one cycle), COMBINED*SHADE (cycle 1), or plain SHADE? (rooms only) */
int ex_uses_env_alpha(const Material *m)
{
    const MatKey *k = &m->k;
    if (!m->env_rule || !k->list) return 0;
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

/* ---- texture cache ---- */

void ex_texcache_open(ExTexCache *tc, const uint8_t *rom, size_t rom_size, const char *region, ExLogFn log, void *user)
{
    memset(tc, 0, sizeof *tc);
    tc->rom = rom; tc->rom_size = rom_size; tc->region = region;
    tc->tbl_ok = ex_tex_table_open(&tc->tbl, rom, rom_size, region, &tc->tbl_err) == 0;
    if (!tc->tbl_ok)
        ex_logf(log, user, "textures unavailable for region %s: %s (exporting untextured)", region,
                tc->tbl_err ? tc->tbl_err : "?");
    else if (strcmp(region, "ntsc-final"))
        ex_logf(log, user, "region %s: texture support is not verified", region);
}

void ex_texcache_clear(ExTexCache *tc)
{
    for (size_t i = 0; i < tc->nt; i++) if (tc->tex[i].ok) ex_tex_free(&tc->tex[i].t);
    free(tc->tex); tc->tex = NULL; tc->nt = tc->capt = 0;
}

void ex_texcache_close(ExTexCache *tc)
{
    ex_texcache_clear(tc);
    if (tc->tbl_ok) ex_tex_table_close(&tc->tbl);
    tc->tbl_ok = 0;
}

TexEnt *ex_texcache_get(ExTexCache *tc, int texnum)
{
    for (size_t i = 0; i < tc->nt; i++) if (tc->tex[i].texnum == texnum) return &tc->tex[i];
    if (ex_grow((void **)&tc->tex, &tc->capt, tc->nt + 1, sizeof *tc->tex)) { tc->oom = 1; return NULL; }
    TexEnt *e = &tc->tex[tc->nt++];
    memset(e, 0, sizeof *e);
    e->texnum = texnum;
    if (!tc->tbl_ok) {
        snprintf(e->err, sizeof e->err, "textures unavailable for region %s: %s", tc->region,
                 tc->tbl_err ? tc->tbl_err : "no texture table");
    } else {
        const char *err = NULL;
        if (ex_tex_decode(&tc->tbl, texnum, &e->t, &err) == 0 && e->t.rgba && e->t.width > 0 && e->t.height > 0)
            e->ok = 1;
        else {
            if (e->t.rgba) ex_tex_free(&e->t);
            snprintf(e->err, sizeof e->err, "%s", err ? err : "decode failed");
        }
    }
    return e;
}

/* ---- display-list state ---- */

int ex_dl_state(ExGfxState *s, unsigned op, uint32_t w0, uint32_t w1)
{
    switch (op) {
    case 0xBB: s->ks = (w1 >> 16) & 0xFFFF; s->kt = w1 & 0xFFFF; s->mat = -1; return 1;
    case 0xB7: s->geom |= w1; s->mat = -1; return 1;
    case 0xB6: s->geom &= ~w1; s->mat = -1; return 1;
    case 0xBA: case 0xB9: {
        unsigned sh = (w0 >> 8) & 0xFF, ln = w0 & 0xFF;
        if (sh < 32 && ln > 0) {
            uint32_t mask = (ln >= 32 ? 0xFFFFFFFFu : ((1u << ln) - 1u)) << sh;   /* w1 is pre-positioned */
            if (op == 0xBA) s->omh = (s->omh & ~mask) | (w1 & mask);
            else            s->oml = (s->oml & ~mask) | (w1 & mask);
        }
        s->mat = -1;
        return 1;
    }
    case 0xFC: s->c0 = w0 & 0xFFFFFF; s->c1 = w1; s->mat = -1; return 1;
    case 0xFA: s->prim = w1; s->mat = -1; return 1;
    case 0xFB: s->env = w1; s->mat = -1; return 1;
    case 0xC0:
        s->have_tex = 1;
        s->texnum = w1 & 0xFFF; s->second = (w1 >> 12) & 0xFFF; s->min = (w1 >> 24) & 0xFF;
        s->type = w0 & 7; s->smode = (w0 >> 22) & 3; s->tmode = (w0 >> 20) & 3; s->offset = (w0 >> 18) & 3;
        s->shifts = (w0 >> 14) & 0xF; s->shiftt = (w0 >> 10) & 0xF;
        s->mat = -1;
        return 1;
    default: return 0;
    }
}

/* ---- material resolution ---- */

void ex_matctx_reset(MatCtx *mc)
{
    free(mc->mats); mc->mats = NULL; mc->nm = mc->capm = 0;
    mc->oom = 0;
}

void ex_matctx_free(MatCtx *mc) { ex_matctx_reset(mc); }

int ex_resolve_material(MatCtx *mc, const ExGfxState *s, int list)
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
        TexEnt *te = ex_texcache_get(mc->tc, (int)s->texnum);
        if (!te) { mc->oom = 1; return -1; }
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
    for (size_t i = 0; i < mc->nm; i++)
        if (memcmp(&mc->mats[i].k, &k, sizeof k) == 0) return (int)i;
    if (ex_grow((void **)&mc->mats, &mc->capm, mc->nm + 1, sizeof *mc->mats)) { mc->oom = 1; return -1; }
    Material *m = &mc->mats[mc->nm];
    memset(m, 0, sizeof *m);
    m->k = k; m->tw = tw; m->th = th; m->expl = expl;
    memcpy(m->err, err, sizeof m->err);
    m->env_rule = mc->env_rule; m->normal_mask = mc->normal_mask; m->emit_decal = mc->emit_decal;
    return (int)mc->nm++;
}

/* ------------------------------------------------------------------------ */
/* primitives                                                               */
/* ------------------------------------------------------------------------ */

Prim *ex_primset_find(PrimSet *ps, int mat, int *oom)
{
    if (ps->np && ps->prims[ps->last].mat == mat) return &ps->prims[ps->last];
    for (size_t i = 0; i < ps->np; i++)
        if (ps->prims[i].mat == mat) { ps->last = i; return &ps->prims[i]; }
    if (ex_grow((void **)&ps->prims, &ps->cap, ps->np + 1, sizeof *ps->prims)) { *oom = 1; return NULL; }
    Prim *p = &ps->prims[ps->np];
    memset(p, 0, sizeof *p);
    p->mat = mat;
    ps->last = ps->np++;
    return p;
}

void ex_primset_free(PrimSet *ps)
{
    for (size_t i = 0; i < ps->np; i++) {
        Prim *p = &ps->prims[i];
        free(p->pos); free(p->uv); free(p->col); free(p->key); free(p->idx); free(p->ht);
    }
    free(ps->prims);
    memset(ps, 0, sizeof *ps);
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

long ex_prim_vertex(Prim *p, const ExVtx *v, const Material *m, double scale, const ExGfxState *s)
{
    int normals = (m->k.geom & m->normal_mask) != 0;                     /* colour bytes hold a normal */
    int texgen_uv = m->k.textured && (m->k.geom & GEOM_TEXGEN) && normals;
    VKey k;
    memset(&k, 0, sizeof k);
    k.v[0] = v->x; k.v[1] = v->y; k.v[2] = v->z;
    if (m->k.textured && !texgen_uv) { k.v[3] = v->s; k.v[4] = v->t; }
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
    if (texgen_uv) {
        /* texture generation: the colour bytes are a normal (signed x, y, z); fixed front-view reflection */
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

/* ------------------------------------------------------------------------ */
/* glTF writer                                                              */
/* ------------------------------------------------------------------------ */

int gw_view(GW *g, const void *d, size_t n, int target)
{
    while (g->bin.len & 3) { uint8_t z = 0; cv_buf_put(&g->bin, &z, 1); }
    size_t off = g->bin.len;
    cv_buf_put(&g->bin, d, n);
    ex_jp(&g->views, "%s{\"buffer\":0,\"byteOffset\":%zu,\"byteLength\":%zu,\"target\":%d}", g->nviews ? "," : "", off, n, target);
    return g->nviews++;
}

int gw_acc(GW *g, int view, int ctype, size_t count, const char *type, int norm, const float *mn, const float *mx, int ncomp)
{
    ex_jp(&g->accs, "%s{\"bufferView\":%d,\"componentType\":%d,\"count\":%zu,\"type\":\"%s\"", g->naccs ? "," : "", view, ctype, count, type);
    if (norm) ex_jp(&g->accs, ",\"normalized\":true");
    if (mn) {
        ex_jp(&g->accs, ",\"min\":[");
        for (int i = 0; i < ncomp; i++) ex_jp(&g->accs, "%s%.9g", i ? "," : "", mn[i]);
        ex_jp(&g->accs, "],\"max\":[");
        for (int i = 0; i < ncomp; i++) ex_jp(&g->accs, "%s%.9g", i ? "," : "", mx[i]);
        ex_jp(&g->accs, "]");
    }
    ex_jp(&g->accs, "}");
    return g->naccs++;
}

void gw_free(GW *g)
{
    cv_buf_free(&g->bin); cv_buf_free(&g->views); cv_buf_free(&g->accs);
    memset(g, 0, sizeof *g);
}

int ex_emit_prims(GW *g, CvBuf *pj, const PrimSet *ps, const MatCtx *mc)
{
    int nprim = 0;
    for (size_t pi = 0; pi < ps->np; pi++) {
        const Prim *p = &ps->prims[pi];
        if (!p->ni) continue;
        const Material *m = &mc->mats[p->mat];
        int normals = (m->k.geom & m->normal_mask) != 0;
        float mn[3] = { p->pos[0], p->pos[1], p->pos[2] }, mx[3] = { p->pos[0], p->pos[1], p->pos[2] };
        for (size_t v = 1; v < p->nv; v++)
            for (int k = 0; k < 3; k++) {
                float f = p->pos[v * 3 + k];
                if (f < mn[k]) mn[k] = f;
                if (f > mx[k]) mx[k] = f;
            }
        int vp = gw_view(g, p->pos, p->nv * 12, 34962);
        int ap = gw_acc(g, vp, 5126, p->nv, "VEC3", 0, mn, mx, 3);
        int at = -1;
        if (m->k.textured) {
            int vt = gw_view(g, p->uv, p->nv * 8, 34962);
            at = gw_acc(g, vt, 5126, p->nv, "VEC2", 0, NULL, NULL, 0);
        }
        /* COLOR_0 is linear in glTF; the game's colour bytes are sRGB-encoded: convert R,G,B, keep alpha. */
        uint16_t *c16 = (uint16_t *)malloc(p->nv * 8);
        if (!c16) return -1;
        for (size_t k = 0; k < p->nv; k++) {
            for (int ch = 0; ch < 4; ch++) {
                unsigned b = p->col[k * 4 + ch];
                double lin = b / 255.0;
                if (ch == 3 && ex_uses_env_alpha(m)) lin = 1.0;   /* alpha comes from the env colour */
                if (ch < 3 && normals) lin = 1.0;                 /* the bytes are a normal, colour is white */
                else if (ch < 3) lin = lin <= 0.04045 ? lin / 12.92 : pow((lin + 0.055) / 1.055, 2.4);
                long q = (long)(lin * 65535.0 + 0.5);
                c16[k * 4 + ch] = (uint16_t)(q < 0 ? 0 : q > 65535 ? 65535 : q);
            }
        }
        int vc = gw_view(g, c16, p->nv * 8, 34962);
        free(c16);
        int ac = gw_acc(g, vc, 5123, p->nv, "VEC4", 1, NULL, NULL, 0);
        int ai;
        if (p->nv - 1 > 65535) {
            int vi = gw_view(g, p->idx, p->ni * 4, 34963);   /* little-endian host */
            ai = gw_acc(g, vi, 5125, p->ni, "SCALAR", 0, NULL, NULL, 0);
        } else {
            uint16_t *i16 = (uint16_t *)malloc(p->ni * 2);
            if (!i16) return -1;
            for (size_t k = 0; k < p->ni; k++) i16[k] = (uint16_t)p->idx[k];
            int vi = gw_view(g, i16, p->ni * 2, 34963);
            free(i16);
            ai = gw_acc(g, vi, 5123, p->ni, "SCALAR", 0, NULL, NULL, 0);
        }
        ex_jp(pj, "%s{\"attributes\":{\"POSITION\":%d", nprim ? "," : "", ap);
        if (at >= 0) ex_jp(pj, ",\"TEXCOORD_0\":%d", at);
        ex_jp(pj, ",\"COLOR_0\":%d},\"indices\":%d,\"material\":%d,\"mode\":4}", ac, ai, p->mat);
        nprim++;
    }
    return nprim;
}

/* ------------------------------------------------------------------------ */
/* materials JSON                                                           */
/* ------------------------------------------------------------------------ */

static const char *alpha_name(int a) { return a == 2 ? "BLEND" : a == 1 ? "MASK" : "OPAQUE"; }
static int wrap_of(uint32_t m) { return m == 1 ? 33071 : m == 2 ? 33648 : 10497; }
static void write_rgba(CvBuf *j, uint32_t c) { ex_jp(j, "[%u,%u,%u,%u]", c >> 24, (c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF); }

void ex_matout_init(MatOut *mo)
{
    memset(mo, 0, sizeof *mo);
    mo->pack_depth = 1;
    for (int i = 0; i < 9; i++) mo->smap[i] = -1;
}

void ex_matout_free(MatOut *mo)
{
    cv_buf_free(&mo->mats); cv_buf_free(&mo->texs); cv_buf_free(&mo->imgs); cv_buf_free(&mo->smps);
    free(mo->gt); free(mo->imgtex);
    memset(mo, 0, sizeof *mo);
}

int ex_emit_materials(MatOut *mo, const MatCtx *mc)
{
    for (size_t mi = 0; mi < mc->nm; mi++) {
        const Material *m = &mc->mats[mi];
        const MatKey *k = &m->k;
        int am = ex_alpha_mode(k->oml);
        ex_jp(&mo->mats, "%s{\"name\":\"mat_%zu\",\"pbrMetallicRoughness\":{\"baseColorFactor\":[1,1,1,%.9g],\"metallicFactor\":0,\"roughnessFactor\":1",
              mi ? "," : "", mi, ex_uses_env_alpha(m) ? (double)(k->env & 0xFF) / 255.0 : 1.0);
        if (k->textured) {
            size_t ti;
            uint32_t wsm = k->smode, wtm = k->tmode;
            for (ti = 0; ti < mo->ngt; ti++)
                if (mo->gt[ti].texnum == (int)k->texnum && mo->gt[ti].ws == (int)wsm && mo->gt[ti].wt == (int)wtm) break;
            if (ti == mo->ngt) {
                if (ex_grow((void **)&mo->gt, &mo->capgt, mo->ngt + 1, sizeof *mo->gt)) return -1;
                mo->gt[mo->ngt].texnum = (int)k->texnum; mo->gt[mo->ngt].ws = (int)wsm; mo->gt[mo->ngt].wt = (int)wtm; mo->ngt++;
                size_t ii;
                for (ii = 0; ii < mo->nimg; ii++) if (mo->imgtex[ii] == (int)k->texnum) break;
                if (ii == mo->nimg) {
                    if (ex_grow((void **)&mo->imgtex, &mo->capimg, mo->nimg + 1, sizeof *mo->imgtex)) return -1;
                    mo->imgtex[mo->nimg++] = (int)k->texnum;
                    ex_jp(&mo->imgs, "%s{\"uri\":\"%s/ge007.ptexpack/textures/%u.png\"}", ii ? "," : "",
                          mo->pack_depth >= 2 ? "../.." : "..", k->texnum);
                }
                int sk = (int)(wsm > 2 ? 0 : wsm) * 3 + (int)(wtm > 2 ? 0 : wtm);
                if (mo->smap[sk] < 0) {
                    ex_jp(&mo->smps, "%s{\"magFilter\":9729,\"minFilter\":9987,\"wrapS\":%d,\"wrapT\":%d}", mo->nsmp ? "," : "",
                          wrap_of(wsm), wrap_of(wtm));
                    mo->smap[sk] = mo->nsmp++;
                }
                ex_jp(&mo->texs, "%s{\"sampler\":%d,\"source\":%zu}", ti ? "," : "", mo->smap[sk], ii);
            }
            ex_jp(&mo->mats, ",\"baseColorTexture\":{\"index\":%zu}", ti);
        }
        ex_jp(&mo->mats, "},\"alphaMode\":\"%s\"", alpha_name(am));
        if (am == 1) ex_jp(&mo->mats, ",\"alphaCutoff\":0.5");
        ex_jp(&mo->mats, ",\"doubleSided\":%s", (k->geom & 0x3000u) == 0 ? "true" : "false");
        ex_jp(&mo->mats, ",\"extensions\":{\"KHR_materials_unlit\":{}},\"extras\":{\"texnum\":%d",
              k->texnum == 0xFFFFFFFFu ? -1 : (int)k->texnum);
        if (k->textured) ex_jp(&mo->mats, ",\"texture\":\"%u\"", k->texnum);   /* the texture key in the base pack (texture-packs.md) */
        if (k->geom & GEOM_TEXGEN) ex_jp(&mo->mats, ",\"texgen\":true%s", (k->geom & GEOM_TEXGEN_LINEAR) ? ",\"texgen_linear\":true" : "");
        if (m->emit_decal && (k->oml & 0x0C00u) == 0x0C00u) ex_jp(&mo->mats, ",\"decal\":true");
        if (k->texnum != 0xFFFFFFFFu && k->type == 1)
            ex_jp(&mo->mats, ",\"detail\":{\"texnum\":%u,\"shifts\":%u,\"shiftt\":%u}", k->second, k->shifts, k->shiftt);
        if (k->decode_fail) { ex_jp(&mo->mats, ",\"decode_error\":"); ex_jstr(&mo->mats, m->err); }
        ex_jp(&mo->mats, ",\"n64\":{\"combine\":[%u,%u],\"othermode\":[%u,%u],\"geometry_mode\":%u,\"list\":\"%s\"",
              k->comb0, k->comb1, k->omh, k->oml, k->geom, k->list ? "secondary" : "primary");
        if (k->texnum != 0xFFFFFFFFu)
            ex_jp(&mo->mats, ",\"texref\":{\"type\":%u,\"smode\":%u,\"tmode\":%u,\"offset\":%u,\"shifts\":%u,\"shiftt\":%u,\"min\":%u,\"second\":%u}",
                  k->type, k->smode, k->tmode, k->offset, k->shifts, k->shiftt, k->min, k->second);
        if (k->textured) ex_jp(&mo->mats, ",\"texscale\":[%.9g,%.9g]", ex_scale_of(k->ks), ex_scale_of(k->kt));
        ex_jp(&mo->mats, ",\"env\":"); write_rgba(&mo->mats, k->env);
        ex_jp(&mo->mats, ",\"prim\":"); write_rgba(&mo->mats, k->prim);
        ex_jp(&mo->mats, "}");
        if (k->texnum == 1508 || k->texnum == 1511) ex_jp(&mo->mats, ",\"anim\":\"water\"");
        ex_jp(&mo->mats, "}}");
    }
    return 0;
}

void ex_emit_texture_arrays(CvBuf *json, const MatOut *mo)
{
    if (!mo->ngt) return;
    ex_jp(json, ",\"textures\":["); cv_buf_put(json, mo->texs.p, mo->texs.len);
    ex_jp(json, "],\"images\":["); cv_buf_put(json, mo->imgs.p, mo->imgs.len);
    ex_jp(json, "],\"samplers\":["); cv_buf_put(json, mo->smps.p, mo->smps.len); ex_jp(json, "]");
}
