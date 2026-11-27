/* stan_export.c -- collision floor ("stan") of a level -> glTF pieces (docs/specs/stan.md).
 *
 * CLEAN ROOM: written from docs/specs/stan.md and level-decor.md only (plus the converter library's file table and
 * rarezip inflate). See export.h. Binary output is little-endian.
 */
#include "stan_export.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t sx_be32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
static int16_t sx_be16s(const uint8_t *p) { return (int16_t)(((unsigned)p[0] << 8) | p[1]); }

static void sx_jp(CvBuf *b, const char *fmt, ...)
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

typedef struct {
    uint32_t off, name;
    int room, kind, color[3], n, extreme[3];
    int16_t pt[10][3];
    uint16_t link[10];
    int nb[10];           /* neighbour tile index per edge, -1: none / unresolved */
} Tile;

typedef struct {
    size_t ntiles, walls, linked, unresolved, trailing;
    uint32_t word0, nentries, first_entry, table[64];
    int table_mismatch;
    char note[300];
} Parsed;

static int grow_tiles(Tile **t, size_t *cap, size_t need)
{
    if (need <= *cap) return 0;
    size_t nc = *cap ? *cap * 2 : 256;
    while (nc < need) nc *= 2;
    Tile *p = (Tile *)realloc(*t, nc * sizeof **t);
    if (!p) return -1;
    *t = p; *cap = nc;
    return 0;
}

/* Parses the decompressed file. 0 ok, -1 invalid (ps->note says why), -2 out of memory. */
static int stan_parse(const uint8_t *d, size_t len, Tile **out, Parsed *ps)
{
    Tile *t = NULL;
    size_t nt = 0, cap = 0, p = 4;
    *out = NULL;
    if (len < 12) { snprintf(ps->note, sizeof ps->note, "stan file too short (%zu bytes)", len); return -1; }
    ps->word0 = sx_be32(d);
    while (p + 4 <= len && sx_be32(d + p) != 0) {
        if (ps->nentries < 64) ps->table[ps->nentries] = sx_be32(d + p);
        ps->nentries++;
        p += 4;
    }
    if (p + 4 > len || !ps->nentries) { snprintf(ps->note, sizeof ps->note, "stan offset table is empty or has no terminator"); return -1; }
    ps->first_entry = ps->table[0];
    p += 4;   /* past the table's 0 word: the first tile */
    if (ps->first_entry != p) ps->table_mismatch = 1;
    while (p + 4 <= len) {
        if (sx_be32(d + p) == 0) break;   /* the 0 word that ends the list */
        if (p + 8 > len) { snprintf(ps->note, sizeof ps->note, "stan tile at %zu truncated", p); break; }
        int n = d[p + 6] >> 4;
        size_t sz;
        if (n > 10) { snprintf(ps->note, sizeof ps->note, "stan tile at %zu has %d points (> 10): list stops there", p, n); break; }
        sz = n <= 3 ? 32 : 8 + 8 * (size_t)n;
        if (p + sz > len) { snprintf(ps->note, sizeof ps->note, "stan tile at %zu runs past the file: list stops there", p); break; }
        if (grow_tiles(&t, &cap, nt + 1)) { free(t); return -2; }
        Tile *s = &t[nt++];
        memset(s, 0, sizeof *s);
        s->off = (uint32_t)p;
        s->name = ((uint32_t)d[p] << 16) | ((uint32_t)d[p + 1] << 8) | d[p + 2];
        s->room = d[p + 3];
        s->kind = d[p + 4] >> 4;
        s->color[0] = d[p + 4] & 15; s->color[1] = d[p + 5] >> 4; s->color[2] = d[p + 5] & 15;
        s->n = n;
        s->extreme[0] = d[p + 6] & 15; s->extreme[1] = d[p + 7] >> 4; s->extreme[2] = d[p + 7] & 15;
        for (int i = 0; i < n; i++) {
            const uint8_t *q = d + p + 8 + (size_t)i * 8;
            s->pt[i][0] = sx_be16s(q); s->pt[i][1] = sx_be16s(q + 2); s->pt[i][2] = sx_be16s(q + 4);
            s->link[i] = (uint16_t)(((unsigned)q[6] << 8) | q[7]);
            s->nb[i] = -1;
        }
        p += sz;
    }
    ps->trailing = len - p;
    for (size_t i = 0; i < nt; i++)
        for (int e = 0; e < t[i].n; e++) {
            unsigned L = t[i].link[e];
            if ((L >> 4) == 0) { ps->walls++; continue; }
            long long target = (long long)ps->first_entry - 0x80 + (long long)L * 8;
            long lo = 0, hi = (long)nt - 1, found = -1;
            while (lo <= hi) {   /* tiles are in ascending offset order */
                long mid = (lo + hi) / 2;
                if ((long long)t[mid].off == target) { found = mid; break; }
                if ((long long)t[mid].off < target) lo = mid + 1; else hi = mid - 1;
            }
            if (found < 0) ps->unresolved++;
            else { t[i].nb[e] = (int)found; ps->linked++; }
        }
    ps->ntiles = nt;
    if (!nt) { snprintf(ps->note, sizeof ps->note, "stan file has no tiles"); free(t); return -1; }
    *out = t;
    return 0;
}

static int sx_align(CvBuf *bin, size_t base)
{
    while (((base + bin->len) & 3) != 0) { uint8_t z = 0; if (cv_buf_put(bin, &z, 1)) return -1; }
    return 0;
}

/* appends a bufferView; returns its index (view_base + n) */
static int sx_view(ExStan *o, size_t bin_base, int view_base, const void *d, size_t n, int target)
{
    sx_align(&o->bin, bin_base);
    size_t off = bin_base + o->bin.len;
    cv_buf_put(&o->bin, d, n);
    sx_jp(&o->views, "%s{\"buffer\":0,\"byteOffset\":%zu,\"byteLength\":%zu,\"target\":%d}", o->nviews ? "," : "", off, n, target);
    return view_base + o->nviews++;
}

static int sx_acc(ExStan *o, int acc_base, int view, int ctype, size_t count, const char *type, const float *mn, const float *mx)
{
    sx_jp(&o->accs, "%s{\"bufferView\":%d,\"componentType\":%d,\"count\":%zu,\"type\":\"%s\"", o->naccs ? "," : "", view, ctype, count, type);
    if (mn) {
        sx_jp(&o->accs, ",\"min\":[");
        for (int i = 0; i < 3; i++) sx_jp(&o->accs, "%s%.9g", i ? "," : "", mn[i]);
        sx_jp(&o->accs, "],\"max\":[");
        for (int i = 0; i < 3; i++) sx_jp(&o->accs, "%s%.9g", i ? "," : "", mx[i]);
        sx_jp(&o->accs, "]");
    }
    sx_jp(&o->accs, "}");
    return acc_base + o->naccs++;
}

const char *ex_stan_material_json(void)
{
    return "{\"name\":\"collision\",\"pbrMetallicRoughness\":{\"baseColorFactor\":[0.6,0.6,0.6,1],\"metallicFactor\":0,\"roughnessFactor\":1},"
           "\"alphaMode\":\"OPAQUE\",\"doubleSided\":true,\"extensions\":{\"KHR_materials_unlit\":{}},\"extras\":{\"texnum\":-1,\"collision\":true}}";
}

void ex_stan_free(ExStan *s)
{
    cv_buf_free(&s->bin); cv_buf_free(&s->views); cv_buf_free(&s->accs); cv_buf_free(&s->meshes);
    cv_buf_free(&s->nodes); cv_buf_free(&s->asset_extras);
}

int ex_stan_build(const uint8_t *rom, size_t rom_size, const CvRegionData *rd, const char *decor_file, double scale,
                  size_t bin_base, int view_base, int acc_base, int mesh_base, int first_node, int material, ExStan *o)
{
    Tile *t = NULL;
    Parsed ps;
    CvBuf raw, children, kidnodes;
    float *pos = NULL;
    uint32_t *idx = NULL;
    int rc = 1, nroom = 0;
    unsigned char present[256];
    memset(o, 0, sizeof *o);
    memset(&ps, 0, sizeof ps);
    memset(&raw, 0, sizeof raw); memset(&children, 0, sizeof children); memset(&kidnodes, 0, sizeof kidnodes);
    memset(present, 0, sizeof present);
    o->root_node = first_node;
    snprintf(o->file, sizeof o->file, "T%s_stanZ", decor_file);
    const CvFileRow *row = cv_find_row(rd, o->file);
    if (!row) { snprintf(o->note, sizeof o->note, "no %s in this ROM's file table", o->file); return 1; }
    if (row->size < 8 || (size_t)row->off + row->size > rom_size) { snprintf(o->note, sizeof o->note, "%s is outside the ROM", o->file); return 1; }
    if (cv_rz_inflate(rom + row->off, row->size, &raw) != 0) {
        snprintf(o->note, sizeof o->note, "%s does not decompress", o->file);
        cv_buf_free(&raw);
        return 1;
    }
    int pr = stan_parse(raw.p, raw.len, &t, &ps);
    cv_buf_free(&raw);
    if (pr == -2) return -1;
    if (pr < 0) { memcpy(o->note, ps.note, sizeof o->note); return 1; }
    memcpy(o->note, ps.note, sizeof o->note);   /* a warning (list stopped early), may be empty */
    o->tiles = ps.ntiles; o->walls = ps.walls; o->linked = ps.linked; o->unresolved = ps.unresolved;
    o->trailing = ps.trailing; o->table_mismatch = ps.table_mismatch;

    for (size_t i = 0; i < ps.ntiles; i++) present[t[i].room] = 1;
    for (int r = 0; r < 256; r++) {
        if (!present[r]) continue;
        size_t nv = 0, ntri = 0;
        for (size_t i = 0; i < ps.ntiles; i++)
            if (t[i].room == r && t[i].n >= 3) { nv += (size_t)t[i].n; ntri += (size_t)t[i].n - 2; }
        if (!ntri) { o->skipped_rooms++; continue; }
        pos = (float *)malloc(nv * 3 * sizeof(float));
        idx = (uint32_t *)malloc(ntri * 3 * sizeof(uint32_t));
        if (!pos || !idx) { rc = -1; goto out; }
        CvBuf tx;
        memset(&tx, 0, sizeof tx);
        size_t v = 0, ti = 0, k = 0;
        int firsttile = 1;
        float mn[3] = { 1e30f, 1e30f, 1e30f }, mx[3] = { -1e30f, -1e30f, -1e30f };
        sx_jp(&tx, "{\"tiles\":[");
        for (size_t i = 0; i < ps.ntiles; i++) {
            const Tile *s = &t[i];
            if (s->room != r) continue;
            size_t base = v, first_tri = ti;
            if (s->n >= 3) {
                for (int j = 0; j < s->n; j++, v++)
                    for (int c = 0; c < 3; c++) {
                        float f = (float)(s->pt[j][c] / scale);
                        pos[v * 3 + (size_t)c] = f;
                        if (f < mn[c]) mn[c] = f;
                        if (f > mx[c]) mx[c] = f;
                    }
                for (int j = 1; j + 1 < s->n; j++, ti++) {   /* fan from point 0 */
                    idx[k++] = (uint32_t)base; idx[k++] = (uint32_t)(base + (size_t)j); idx[k++] = (uint32_t)(base + (size_t)j + 1);
                }
            }
            sx_jp(&tx, "%s{\"index\":%zu,\"name\":%u,\"room\":%d,\"kind\":%d,\"color\":[%d,%d,%d],\"point_count\":%d,"
                       "\"extreme\":[%d,%d,%d],\"first_triangle\":%zu,\"points\":[",
                  firsttile ? "" : ",", i, s->name, s->room, s->kind, s->color[0], s->color[1], s->color[2], s->n,
                  s->extreme[0], s->extreme[1], s->extreme[2], first_tri);
            firsttile = 0;
            for (int j = 0; j < s->n; j++) sx_jp(&tx, "%s[%d,%d,%d]", j ? "," : "", s->pt[j][0], s->pt[j][1], s->pt[j][2]);
            sx_jp(&tx, "],\"links\":[");
            for (int j = 0; j < s->n; j++) sx_jp(&tx, "%s%u", j ? "," : "", s->link[j]);
            sx_jp(&tx, "],\"neighbour\":[");
            for (int j = 0; j < s->n; j++) sx_jp(&tx, "%s%d", j ? "," : "", s->nb[j]);
            sx_jp(&tx, "]}");
        }
        sx_jp(&tx, "]}");
        int vp = sx_view(o, bin_base, view_base, pos, nv * 12, 34962);
        int ap = sx_acc(o, acc_base, vp, 5126, nv, "VEC3", mn, mx);
        int ai;
        if (nv - 1 > 65535) {
            int vi = sx_view(o, bin_base, view_base, idx, ntri * 12, 34963);
            ai = sx_acc(o, acc_base, vi, 5125, ntri * 3, "SCALAR", NULL, NULL);
        } else {
            uint16_t *i16 = (uint16_t *)malloc(ntri * 6);
            if (!i16) { cv_buf_free(&tx); rc = -1; goto out; }
            for (size_t q = 0; q < ntri * 3; q++) i16[q] = (uint16_t)idx[q];
            int vi = sx_view(o, bin_base, view_base, i16, ntri * 6, 34963);
            free(i16);
            ai = sx_acc(o, acc_base, vi, 5123, ntri * 3, "SCALAR", NULL, NULL);
        }
        sx_jp(&o->meshes, "%s{\"name\":\"collision_room_%d\",\"primitives\":[{\"attributes\":{\"POSITION\":%d},\"indices\":%d,"
                          "\"material\":%d,\"mode\":4,\"extras\":",
              o->nmeshes ? "," : "", r, ap, ai, material);
        cv_buf_put(&o->meshes, tx.p, tx.len);
        sx_jp(&o->meshes, "}]}");
        cv_buf_free(&tx);
        sx_jp(&children, "%s%d", nroom ? "," : "", first_node + 1 + nroom);
        sx_jp(&kidnodes, ",{\"name\":\"collision_room_%d\",\"mesh\":%d,\"extras\":{\"room\":%d,\"collision\":true}}", r,
              mesh_base + o->nmeshes, r);
        o->nmeshes++;
        o->tris += ntri;
        nroom++;
        free(pos); free(idx); pos = NULL; idx = NULL;
    }
    if (!nroom) { snprintf(o->note, sizeof o->note, "stan file has no tile with 3 or more points"); rc = 1; goto out; }
    o->rooms = (size_t)nroom;
    sx_jp(&o->nodes, ",{\"name\":\"collision\",\"children\":[");
    cv_buf_put(&o->nodes, children.p, children.len);
    sx_jp(&o->nodes, "],\"extensions\":{\"KHR_node_visibility\":{\"visible\":false}},\"extras\":{\"collision\":true,\"hidden_by_default\":true}}");
    cv_buf_put(&o->nodes, kidnodes.p, kidnodes.len);
    sx_jp(&o->asset_extras, ",\"collision_scene\":1,\"collision\":{\"tiles\":%zu,\"rooms\":%zu,\"file\":\"%s\",\"table_word0\":%u,"
                            "\"table_entries\":%u,\"table\":[",
          o->tiles, o->rooms, o->file, ps.word0, ps.nentries);
    for (uint32_t i = 0; i < ps.nentries && i < 64; i++) sx_jp(&o->asset_extras, "%s%u", i ? "," : "", ps.table[i]);
    sx_jp(&o->asset_extras,
          "],\"unresolved_links\":%zu,\"point_unit\":\"raw s16; world = point / level_scale\","
          "\"tile_index\":\"file order, level-wide; neighbour values index it; first_triangle indexes the room primitive's triangles\"}",
          o->unresolved);
    rc = 0;
out:
    free(pos); free(idx); free(t);
    cv_buf_free(&children); cv_buf_free(&kidnodes);
    return rc;
}
