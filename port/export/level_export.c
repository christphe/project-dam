/* level_export.c -- static level export (issue #150): decor file -> glTF 2.0.
 *
 * CLEAN ROOM: written from docs/specs/level-decor.md and display-lists.md
 * only (plus the converter library's file table, rarezip inflate and ROM
 * check). See export.h. Binary output is little-endian (every supported host
 * is little-endian; glTF buffers are little-endian).
 */
#include "export_common.h"
#include "conv_romcheck.h"
#include "stan_export.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define be32 ex_be32
#define be16s ex_be16s
#define bef32 ex_bef32
#define jp ex_jp
#define jstr ex_jstr
#define mkdir_p ex_mkdir_p
#define write_file ex_write_file

/* ------------------------------------------------------------------------ */
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

typedef struct {
    int idx;
    float rp[3];
    PrimSet ps;
    uint64_t tris;
    uint64_t tail;
    uint64_t bad_tris, bad_vtx;
} Room;

typedef struct {
    const uint8_t *rom; size_t rom_size;
    const char *region;
    const CvRegionData *rd;
    ExTexCache tc;
    MatCtx mc;

    /* per level */
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

/* ---- display-list interpreter ---- */

static void emit_tri(LC *lc, Room *rm, ExGfxState *s, int list, double scale, int a, int b, int c)
{
    lc->issued++;
    if (a > 15 || b > 15 || c > 15 || !s->cache[a].valid || !s->cache[b].valid || !s->cache[c].valid) {
        lc->dropped_tri++; rm->bad_tris++;
        return;
    }
    if (s->mat < 0) {
        s->mat = ex_resolve_material(&lc->mc, s, list);
        if (s->mat < 0) { lc->oom = 1; return; }
    }
    Prim *p = ex_primset_find(&rm->ps, s->mat, &lc->oom);
    if (!p) return;
    const Material *m = &lc->mc.mats[s->mat];
    long ia = ex_prim_vertex(p, &s->cache[a], m, scale, s);
    long ib = ex_prim_vertex(p, &s->cache[b], m, scale, s);
    long ic = ex_prim_vertex(p, &s->cache[c], m, scale, s);
    if (ia < 0 || ib < 0 || ic < 0) { lc->oom = 1; return; }
    if (ex_grow((void **)&p->idx, &p->capi, p->ni + 3, sizeof *p->idx)) { lc->oom = 1; return; }
    p->idx[p->ni++] = (uint32_t)ia; p->idx[p->ni++] = (uint32_t)ib; p->idx[p->ni++] = (uint32_t)ic;
    rm->tris++;
}

static void run_list(LC *lc, Room *rm, const uint8_t *dl, size_t len, int list, const uint8_t *vtx, size_t vtxlen,
                     double scale)
{
    ExGfxState s;
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
                ExVtx *c = &s.cache[first + k];
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
        default: ex_dl_state(&s, op, w0, w1); break;   /* state commands; syncs and unknown opcodes are skipped (counted) */
        }
    }
    if (!ended) lc->no_end_lists++;
}

/* ------------------------------------------------------------------------ */
/* decor file                                                               */
/* ------------------------------------------------------------------------ */

static void lc_reset_level(LC *lc)
{
    for (int r = 0; r < lc->nrooms; r++) ex_primset_free(&lc->rooms[r].ps);
    free(lc->rooms); lc->rooms = NULL; lc->nrooms = 0;
    ex_texcache_clear(&lc->tc);
    ex_matctx_reset(&lc->mc);
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

typedef struct { int rooms; uint64_t tris, issued; size_t mats, textures; int failed; char msg[1300]; size_t stan_tiles, stan_rooms; } LevelResult;

static int export_level(LC *lc, const CvFileRow *row, const char *file, const char *name, double scale, int scale_assumed,
                        const char *out_dir, LevelResult *res)
{
    CvBuf rpt, json, comp[3], nodes, meshes;
    GW g;
    MatOut mo;
    ExStan stan;
    int stan_rc = 1;
    memset(&rpt, 0, sizeof rpt); memset(&json, 0, sizeof json); memset(&g, 0, sizeof g);
    memset(comp, 0, sizeof comp);
    memset(&nodes, 0, sizeof nodes); memset(&meshes, 0, sizeof meshes);
    ex_matout_init(&mo);
    memset(&stan, 0, sizeof stan);
    lc_reset_level(lc);
    int fail = 0, n = 0, nmesh = 0;
    size_t ntexfiles = 0;
    uint64_t tot_tris = 0;
    char path[1200], dir[1024];
    const uint8_t *fd;
    size_t fsize, maxrec, first_zero = 0;
    uint32_t rt;

#define FAIL(...) do { snprintf(res->msg, sizeof res->msg, __VA_ARGS__); fail = 1; goto done; } while (0)

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
    if (ex_write_textures(&lc->tc, lc->mc.mats, lc->mc.nm, dir, &ntexfiles)) FAIL("cannot write textures into %s", dir);

    /* ---- glTF: meshes + nodes ---- */
    for (int r = 0; r < lc->nrooms; r++) {
        Room *rm = &lc->rooms[r];
        int mesh = -1;
        if (rm->ps.np) {
            CvBuf pj;
            memset(&pj, 0, sizeof pj);
            int nprim = ex_emit_prims(&g, &pj, &rm->ps, &lc->mc);
            if (nprim < 0) { cv_buf_free(&pj); FAIL("out of memory"); }
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

    /* ---- collision floor (stan.md): an extra "collision" node tree, in its own scene ---- */
    while (g.bin.len & 3) { uint8_t z = 0; cv_buf_put(&g.bin, &z, 1); }
    stan_rc = ex_stan_build(lc->rom, lc->rom_size, lc->rd, file, scale, g.bin.len, g.nviews, g.naccs, nmesh, lc->nrooms,
                            (int)lc->mc.nm, &stan);
    if (stan_rc < 0) FAIL("out of memory");
    if (stan_rc == 0) {
        if (g.nviews) cv_buf_put(&g.views, ",", 1);
        cv_buf_put(&g.views, stan.views.p, stan.views.len); g.nviews += stan.nviews;
        if (g.naccs) cv_buf_put(&g.accs, ",", 1);
        cv_buf_put(&g.accs, stan.accs.p, stan.accs.len); g.naccs += stan.naccs;
        cv_buf_put(&g.bin, stan.bin.p, stan.bin.len);
        if (nmesh) cv_buf_put(&meshes, ",", 1);
        cv_buf_put(&meshes, stan.meshes.p, stan.meshes.len); nmesh += stan.nmeshes;
        cv_buf_put(&nodes, stan.nodes.p, stan.nodes.len);
        res->stan_tiles = stan.tiles; res->stan_rooms = stan.rooms;
    }

    /* ---- materials, textures, images, samplers ---- */
    if (ex_emit_materials(&mo, &lc->mc)) FAIL("out of memory");

    jp(&json, "{\"asset\":{\"version\":\"2.0\",\"generator\":\"project-dam export\",\"extras\":{\"exporter_version\":%d,\"rom_region\":",
       EX_EXPORTER_VERSION);
    jstr(&json, lc->region);
    jp(&json, ",\"level\":"); jstr(&json, name);
    jp(&json, ",\"decor_file\":"); jstr(&json, file);
    jp(&json, ",\"level_scale\":%.9g", scale);
    jp(&json, ",\"vertex_color_encoding\":\"linear (converted from the game's sRGB bytes)\"");
    if (scale_assumed) jp(&json, ",\"level_scale_note\":\"no level entry for this file; scale 1.0 assumed\"");
    if (stan_rc == 0) cv_buf_put(&json, stan.asset_extras.p, stan.asset_extras.len);
    jp(&json, "}},\"extensionsUsed\":[\"KHR_materials_unlit\"%s],\"scene\":0,\"scenes\":[{\"nodes\":[",
       stan_rc == 0 ? ",\"KHR_node_visibility\"" : "");
    for (int r = 0; r < lc->nrooms; r++) jp(&json, "%s%d", r ? "," : "", r);
    jp(&json, "]}");
    if (stan_rc == 0) jp(&json, ",{\"name\":\"collision\",\"nodes\":[%d]}", stan.root_node);
    jp(&json, "],\"nodes\":["); cv_buf_put(&json, nodes.p, nodes.len); jp(&json, "]");
    if (nmesh) { jp(&json, ",\"meshes\":["); cv_buf_put(&json, meshes.p, meshes.len); jp(&json, "]"); }
    if (stan_rc == 0) {   /* the "collision" material is the last one: index lc->mc.nm */
        if (lc->mc.nm) cv_buf_put(&mo.mats, ",", 1);
        cv_buf_put(&mo.mats, ex_stan_material_json(), strlen(ex_stan_material_json()));
    }
    if (lc->mc.nm || stan_rc == 0) { jp(&json, ",\"materials\":["); cv_buf_put(&json, mo.mats.p, mo.mats.len); jp(&json, "]"); }
    ex_emit_texture_arrays(&json, &mo);
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
            (unsigned long long)tot_tris, (unsigned long long)lc->issued, lc->mc.nm, ntexfiles);
        rep(&rpt, "per room (triangles, primitives, bytes after first end-of-list, dropped triangles, dropped vertex loads):\n");
        for (int r = 0; r < lc->nrooms; r++) {
            Room *rm = &lc->rooms[r];
            rep(&rpt, "  room %d: %llu tris, %zu prims, tail %llu, dropped-tri %llu, dropped-vtx %llu\n", rm->idx,
                (unsigned long long)rm->tris, rm->ps.np, (unsigned long long)rm->tail, (unsigned long long)rm->bad_tris,
                (unsigned long long)rm->bad_vtx);
            for (size_t pi = 0; pi < rm->ps.np; pi++) {
                const MatKey *k = &lc->mc.mats[rm->ps.prims[pi].mat].k;
                if (k->list == 1 && ex_alpha_mode(k->oml) == 0) { opq_sec_m++; opq_sec_tris += rm->ps.prims[pi].ni / 3; }
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
            for (size_t mi = 0; mi < lc->mc.nm; mi++) { nenv += ex_uses_env_alpha(&lc->mc.mats[mi]); ntg += (lc->mc.mats[mi].k.geom & GEOM_TEXGEN) != 0; }
            rep(&rpt, "materials with shade alpha replaced by environment alpha: %zu\n", nenv);
            rep(&rpt, "texture-generated materials (geometry mode 0x40000): %zu\n", ntg);
        }
        if (stan_rc == 0)
            rep(&rpt, "\ncollision floor %s: %zu tiles in %zu rooms, %zu triangles, edges: %zu walls, %zu linked, %zu unresolved links, "
                      "%zu trailing bytes%s%s%s\n", stan.file, stan.tiles, stan.rooms, stan.tris, stan.walls, stan.linked, stan.unresolved,
                stan.trailing, stan.table_mismatch ? ", first table entry != end of table (tiles walked from the end of the table)" : "",
                stan.note[0] ? ", note: " : "", stan.note);
        else
            rep(&rpt, "\ncollision floor: none (%s)\n", stan.note);
        rep(&rpt, "\ntexture decode failures:\n");
        for (size_t i = 0; i < lc->tc.nt; i++)
            if (!lc->tc.tex[i].ok) { rep(&rpt, "  texture %d: %s\n", lc->tc.tex[i].texnum, lc->tc.tex[i].err); nfail++; }
        if (!nfail) rep(&rpt, "  none\n");
        if (!lc->tc.tbl_ok) rep(&rpt, "texture table unavailable (%s): the level is exported untextured\n", lc->tc.tbl_err ? lc->tc.tbl_err : "?");
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

    res->rooms = lc->nrooms; res->tris = tot_tris; res->issued = lc->issued; res->mats = lc->mc.nm; res->textures = ntexfiles;

done:
    for (int k = 0; k < 3; k++) cv_buf_free(&comp[k]);
    cv_buf_free(&rpt); cv_buf_free(&json); gw_free(&g);
    cv_buf_free(&nodes); cv_buf_free(&meshes);
    ex_matout_free(&mo);
    ex_stan_free(&stan);
    res->failed = fail;
    return fail;
#undef FAIL
}

/* ------------------------------------------------------------------------ */
/* all levels                                                               */
/* ------------------------------------------------------------------------ */

#define logf_ ex_logf

int ex_export_levels(const char *rom_path, const char *out_dir, ExLogFn log, void *user)
{
    uint8_t *rom = NULL;
    size_t rom_size = 0;
    const char *region = NULL;
    const CvRegionData *rd = NULL;
    if (ex_open_rom(rom_path, log, user, &rom, &rom_size, &region, &rd)) return 1;
    if (mkdir_p(out_dir)) { logf_(log, user, "cannot create %s", out_dir); free(rom); return 1; }

    LC *lc = (LC *)calloc(1, sizeof *lc);
    if (!lc) { free(rom); return 1; }
    lc->rom = rom; lc->rom_size = rom_size; lc->region = region; lc->rd = rd;
    ex_texcache_open(&lc->tc, rom, rom_size, region, log, user);
    lc->mc.tc = &lc->tc; lc->mc.env_rule = 1; lc->mc.normal_mask = GEOM_TEXGEN; lc->mc.emit_decal = 0;

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
                logf_(log, user, "%-10s rooms %3d  triangles %7llu (issued %llu)  materials %3zu  textures %3zu  stan tiles %5zu rooms %3zu", names[k],
                      res.rooms, (unsigned long long)res.tris, (unsigned long long)res.issued, res.mats, res.textures,
                      res.stan_tiles, res.stan_rooms);
            }
        }
    }
    logf_(log, user, "export: %d levels, %d failed, %llu triangles, %zu texture files -> %s", nlevels, nfailed,
          (unsigned long long)gtris, gtex, out_dir);
    lc_reset_level(lc);
    ex_texcache_close(&lc->tc);
    free(lc);
    free(rom);
    return nfailed;
}
