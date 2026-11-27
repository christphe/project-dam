/* text_export.c -- static export of the game's text banks and name tables (docs/specs/text.md).
 *
 * Writes <out_dir>/text_<lang>.json for every language file set the ROM carries (E / P / J) and
 * <out_dir>/names.json (items, mission folders, briefings, multiplayer stages and characters).
 *
 * CLEAN ROOM: written from docs/specs/text.md only (see export.h). The output is ROM data: it goes
 * only to the directory given.
 */
#include "export_common.h"
#include "conv_romcheck.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------ */
/* constants of text.md                                                     */
/* ------------------------------------------------------------------------ */

static const struct {
    const char *region;
    uint32_t cdata_rom, base, items, folders, mpstages, mpchars;
    char game_suffix;
} k_treg[] = {
    { "ntsc-final", 0x21990u, 0x80020D90u, 0x12B94u, 0x9E54u, 0xA2E4u, 0xA408u, 'E' },
    { "pal-final",  0x1F850u, 0x8001EC50u, 0x10224u, 0x74E4u, 0x7974u, 0x7A98u, 'P' },
    { "jpn-final",  0x219D0u, 0x80020DD0u, 0x12B94u, 0x9E54u, 0xA2E4u, 0xA408u, 'J' },
};

#define NBANKS 44
#define CDATA_MAX_OUT (8u * 1024u * 1024u)
#define N_ITEMS 89
#define N_FOLDERS 29
#define N_MPSTAGES 12
#define N_MPCHARS 64

static const struct { const char *name, *stem; } k_bank[NBANKS + 1] = {
    { "LNULL", "" },
    { "LAME", "Lame" }, { "LARCH", "Larch" }, { "LARK", "Lark" }, { "LASH", "Lash" }, { "LAZT", "Lazt" },
    { "LCAT", "Lcat" }, { "LCAVE", "Lcave" }, { "LAREC", "Larec" }, { "LCRAD", "Lcrad" }, { "LCRYP", "Lcryp" },
    { "LDAM", "Ldam" }, { "LDEPO", "Ldepo" }, { "LDEST", "Ldest" }, { "LDISH", "Ldish" }, { "LEAR", "Lear" },
    { "LELD", "Leld" }, { "LIMP", "Limp" }, { "LJUN", "Ljun" }, { "LLEE", "Llee" }, { "LLEN", "Llen" },
    { "LLIP", "Llip" }, { "LLUE", "Llue" }, { "LOAT", "Loat" }, { "LPAM", "Lpam" }, { "LPETE", "Lpete" },
    { "LREF", "Lref" }, { "LRIT", "Lrit" }, { "LRUN", "Lrun" }, { "LSEVB", "Lsevb" }, { "LSEV", "Lsev" },
    { "LSEVX", "Lsevx" }, { "LSEVXB", "Lsevxb" }, { "LSHO", "Lsho" }, { "LSILO", "Lsilo" }, { "LSTAT", "Lstat" },
    { "LTRA", "Ltra" }, { "LWAX", "Lwax" }, { "LGUN", "Lgun" }, { "LTITLE", "Ltitle" }, { "LMPMENU", "Lmpmenu" },
    { "LPROPOBJ", "Lpropobj" }, { "LMPWEAPONS", "Lmpweapons" }, { "LOPTIONS", "Loptions" }, { "LMISC", "Lmisc" },
};

static const struct { int level; const char *stage; int bank; } k_levelbank[] = {
    { 9, "Bunker 1", 30 },  { 20, "Silo", 34 },     { 22, "Statue", 35 },   { 23, "Control", 8 },
    { 24, "Archives", 2 },  { 25, "Train", 36 },    { 26, "Frigate", 13 },  { 27, "Bunker 2", 29 },
    { 28, "Aztec", 5 },     { 29, "Streets", 25 },  { 30, "Depot", 12 },    { 31, "Complex", 26 },
    { 32, "Egyptian", 10 }, { 33, "Dam", 11 },      { 34, "Facility", 3 },  { 35, "Runway", 28 },
    { 36, "Surface 1", 31 }, { 37, "Jungle", 18 },  { 38, "Temple", 14 },   { 39, "Caverns", 7 },
    { 41, "Cradle", 9 },    { 43, "Surface 2", 32 }, { 45, "Basement", 17 }, { 46, "Stack", 4 },
    { 48, "Library", 1 },   { 50, "Caves", 23 },    { 54, "Cuba", 20 },
};
#define N_LEVELBANK ((int)(sizeof k_levelbank / sizeof k_levelbank[0]))

static int level_bank(int level)
{
    for (int i = 0; i < N_LEVELBANK; i++) if (k_levelbank[i].level == level) return k_levelbank[i].bank;
    return -1;
}

static const struct { char suffix; const char *lang; } k_lang[] = { { 'E', "en" }, { 'P', "en-GB" }, { 'J', "ja" } };

/* ------------------------------------------------------------------------ */
/* banks                                                                    */
/* ------------------------------------------------------------------------ */

typedef struct {
    int present;          /* the ROM has the file and it inflated */
    CvBuf d;
    uint32_t slots;
    uint32_t nstr;
} Bank;

typedef struct {
    char suffix;
    int present;          /* at least one bank file exists */
    int game;             /* the game loads this set */
    Bank b[NBANKS + 1];
    uint32_t total;
    char sha1[41];
} TextSet;

static void set_free(TextSet *s)
{
    for (int i = 0; i <= NBANKS; i++) cv_buf_free(&s->b[i].d);
}

/* The string of an id: pointer into the bank and its length (without the terminator); 0 = no string. */
static int get_string(const TextSet *s, unsigned id, const uint8_t **p, size_t *n)
{
    unsigned bank = id >> 10, idx = id & 0x3FFu;
    if (!id || bank < 1 || bank > NBANKS || !s || !s->b[bank].present) return 0;
    const Bank *b = &s->b[bank];
    if (idx >= b->slots) return 0;
    uint32_t off = ex_be32(b->d.p + (size_t)idx * 4);
    if (!off || off >= b->d.len) return 0;
    const uint8_t *z = memchr(b->d.p + off, 0, b->d.len - off);
    if (!z) return 0;
    *p = b->d.p + off;
    *n = (size_t)(z - (b->d.p + off));
    return 1;
}


/* Reads one set's banks. Returns the number of bank-level failures. */
static int load_set(const uint8_t *rom, size_t rom_size, const CvRegionData *rd, TextSet *s, char suffix, ExLogFn log,
                    void *user)
{
    int failed = 0;
    CvBuf all;
    memset(&all, 0, sizeof all);
    s->suffix = suffix;
    for (int bk = 1; bk <= NBANKS; bk++) {
        char fn[32];
        Bank *b = &s->b[bk];
        snprintf(fn, sizeof fn, "%s%c", k_bank[bk].stem, suffix);
        const CvFileRow *row = cv_find_row(rd, fn);
        if (!row) continue;
        s->present = 1;
        if ((size_t)row->off >= rom_size || (size_t)row->off + row->size > rom_size) {
            ex_logf(log, user, "%s: file outside the ROM", fn); failed++; continue;
        }
        if (cv_rz_inflate(rom + row->off, row->size, &b->d) || b->d.len < 16 || (b->d.len & 15u)) {
            ex_logf(log, user, "%s: does not decompress to a multiple of 16 bytes", fn);
            cv_buf_free(&b->d); failed++; continue;
        }
        /* slots = (smallest non-zero entry) / 4; an all-zero table has length / 4 slots */
        uint32_t min = (uint32_t)b->d.len;
        for (size_t i = 0; i * 4 < min && i * 4 + 4 <= b->d.len; i++) {
            uint32_t v = ex_be32(b->d.p + i * 4);
            if (v && v < min) min = v;
        }
        b->slots = min / 4;
        uint32_t prev = 0;
        int bad = 0;
        b->nstr = 0;
        for (uint32_t i = 0; i < b->slots; i++) {
            uint32_t v = ex_be32(b->d.p + (size_t)i * 4);
            if (!v) continue;
            if ((v & 3u) || v >= b->d.len || v <= prev || !memchr(b->d.p + v, 0, b->d.len - v)) { bad = 1; break; }
            prev = v;
            b->nstr++;
        }
        if (bad) {
            ex_logf(log, user, "%s: offset table breaks the layout rules (4-aligned, inside, increasing, terminated)", fn);
            cv_buf_free(&b->d); b->nstr = 0; b->slots = 0; failed++; continue;
        }
        b->present = 1;
        s->total += b->nstr;
        cv_buf_put(&all, b->d.p, b->d.len);
    }
    if (s->present) {
        unsigned char h[20];
        cv_sha1(all.p ? all.p : (const unsigned char *)"", (unsigned long)all.len, h);
        for (int i = 0; i < 20; i++) snprintf(s->sha1 + i * 2, 3, "%02x", h[i]);
    }
    cv_buf_free(&all);
    return failed;
}

/* ------------------------------------------------------------------------ */
/* text -> JSON                                                             */
/* ------------------------------------------------------------------------ */

/* Appends the JSON string of text bytes (with its quotes). japanese: bytes >= 0x80 are two-byte glyph codes. */
static void jtext(CvBuf *o, const uint8_t *s, size_t n, int japanese, int strip_nl)
{
    char e[40];
    if (strip_nl && n && s[n - 1] == 0x0A) n--;
    cv_buf_put(o, "\"", 1);
    for (size_t i = 0; i < n; i++) {
        unsigned c = s[i];
        if (c == '"' || c == '\\') { e[0] = '\\'; e[1] = (char)c; cv_buf_put(o, e, 2); }
        else if (c == 0x0A) cv_buf_put(o, "\\n", 2);
        else if (c < 0x20) { snprintf(e, sizeof e, "\\u%04x", c); cv_buf_put(o, e, 6); }
        else if (c < 0x80) { char ch = (char)c; cv_buf_put(o, &ch, 1); }
        else if (japanese && i + 1 < n) {
            unsigned code = ((c & 0x7Fu) << 7) | (s[i + 1] & 0x7Fu);
            int w = snprintf(e, sizeof e, (code & 0x2000u) ? "{jw:%u}" : "{j:%u}", (code & 0x2000u) ? (code & 0x1FFFu) : code);
            cv_buf_put(o, e, (size_t)w);
            i++;
        } else {   /* a byte >= 0x80 in English text, or a lone lead byte: keep it as an escape */
            int w = snprintf(e, sizeof e, "{x:%02X}", c);
            cv_buf_put(o, e, (size_t)w);
        }
    }
    cv_buf_put(o, "\"", 1);
}

static void jhex(CvBuf *o, const uint8_t *s, size_t n)
{
    cv_buf_put(o, "\"", 1);
    for (size_t i = 0; i < n; i++) { char e[3]; snprintf(e, sizeof e, "%02x", s[i]); cv_buf_put(o, e, 2); }
    cv_buf_put(o, "\"", 1);
}

static int write_set(const char *dir, const char *region, const TextSet *s, int japanese, const char *lang, ExLogFn log,
                     void *user)
{
    CvBuf o;
    char path[1200];
    memset(&o, 0, sizeof o);
    ex_jp(&o, "{\n\"format\":\"ge007-text/1\",\n\"region\":"); ex_jstr(&o, region);
    ex_jp(&o, ",\n\"suffix\":\"%c\",\n\"language\":\"%s\",\n\"game_language\":%s,\n\"glyph_table\":false,\n\"strings_total\":%u,\n\"sha1\":\"%s\",\n",
          s->suffix, lang, s->game ? "true" : "false", (unsigned)s->total, s->sha1);
    ex_jp(&o, "\"banks\":[\n");
    for (int bk = 1; bk <= NBANKS; bk++) {
        const Bank *b = &s->b[bk];
        ex_jp(&o, "%s{\"bank\":%d,\"name\":\"%s\",\"file\":\"%s%c\",\"slots\":%u,\"strings\":%u%s}", bk > 1 ? ",\n" : "", bk,
              k_bank[bk].name, k_bank[bk].stem, s->suffix, (unsigned)b->slots, (unsigned)b->nstr, b->present ? "" : ",\"missing\":true");
    }
    ex_jp(&o, "\n],\n\"strings\":{\n");
    int first = 1;
    for (int bk = 1; bk <= NBANKS; bk++) {
        const Bank *b = &s->b[bk];
        if (!b->present) continue;
        for (uint32_t i = 0; i < b->slots; i++) {
            const uint8_t *p; size_t n;
            unsigned id = (unsigned)bk * 1024u + i;
            if (!get_string(s, id, &p, &n)) continue;
            ex_jp(&o, "%s\"%04X\":{\"bank\":\"%s\",\"index\":%u,\"text\":", first ? "" : ",\n", id, k_bank[bk].name, (unsigned)i);
            first = 0;
            jtext(&o, p, n, japanese, 0);
            if (japanese) { ex_jp(&o, ",\"raw\":"); jhex(&o, p, n); }
            ex_jp(&o, "}");
        }
    }
    ex_jp(&o, "\n}\n}\n");
    snprintf(path, sizeof path, "%s/text_%s.json", dir, lang);
    int rc = ex_write_file(path, o.p, o.len);
    if (rc) ex_logf(log, user, "cannot write %s", path);
    else ex_logf(log, user, "text %c (%s)%s: %u strings, sha1 %s -> %s", s->suffix, lang, s->game ? " [game language]" : "",
                 (unsigned)s->total, s->sha1, path);
    cv_buf_free(&o);
    return rc ? 1 : 0;
}

/* ------------------------------------------------------------------------ */
/* names.json                                                               */
/* ------------------------------------------------------------------------ */

typedef struct {
    const uint8_t *cd; size_t len;
    uint32_t base;
    const TextSet *gs;      /* the game-language set (may be NULL) */
    int japanese;
    char err[200];
} NamesCtx;

/* A zero-terminated ASCII name in cdata behind a pointer; NULL when the pointer is 0 or outside. */
static const char *cstr_at(const NamesCtx *c, uint32_t ptr)
{
    if (!ptr || ptr < c->base || ptr - c->base >= c->len) return NULL;
    const char *s = (const char *)c->cd + (ptr - c->base);
    size_t room = c->len - (ptr - c->base);
    const char *z = memchr(s, 0, room);
    if (!z || z - s > 63) return NULL;
    for (const char *q = s; q < z; q++) if ((unsigned char)*q < 0x20 || (unsigned char)*q > 0x7E) return NULL;
    return s;
}

static void vid(CvBuf *o, unsigned id)
{
    if (id) ex_jp(o, "\"%04X\"", id); else ex_jp(o, "null");
}

static void vtext(CvBuf *o, const NamesCtx *c, unsigned id)
{
    const uint8_t *p; size_t n;
    if (c->gs && get_string(c->gs, id, &p, &n)) jtext(o, p, n, c->japanese, 1); else ex_jp(o, "null");
}

/* ,"key":"HHHH","key_text":"..." */
static void kid(CvBuf *o, const NamesCtx *c, const char *key, unsigned id)
{
    ex_jp(o, ",\"%s\":", key); vid(o, id);
    ex_jp(o, ",\"%s_text\":", key); vtext(o, c, id);
}

static void kstr(CvBuf *o, const char *key, const char *s)
{
    ex_jp(o, ",\"%s\":", key);
    if (s) ex_jstr(o, s); else ex_jp(o, "null");
}

#define REFUSE(...) do { snprintf(c->err, sizeof c->err, __VA_ARGS__); return -1; } while (0)

static int names_items(CvBuf *o, NamesCtx *c, uint32_t off)
{
    if ((size_t)off + (size_t)N_ITEMS * 56 > c->len) REFUSE("item table runs past the end of cdata");
    ex_jp(o, "\"items\":[\n");
    for (int i = 0; i < N_ITEMS; i++) {
        const uint8_t *r = c->cd + off + (size_t)i * 56;
        unsigned ids[4] = { ex_be16(r + 16), ex_be16(r + 18), ex_be16(r + 40), ex_be16(r + 42) };
        for (int k = 0; k < 4; k++)
            if ((ids[k] >> 10) != 38) REFUSE("item %d: string id %04X is not in bank 38", i, ids[k]);
        const char *file = cstr_at(c, ex_be32(r + 4));
        if (i == 0 && (ex_be32(r) || ex_be32(r + 4) || ex_be32(r + 8) != 1 || ex_be32(r + 12) ||
                       ids[0] != 0x9883 || ids[1] != 0x9811 || ids[2] != 0x9811 || ids[3] != 0x9811))
            REFUSE("item 0 does not have the documented invariant");
        if (i == 1 && (!file || strcmp(file, "GfistZ"))) REFUSE("item 1 is not GfistZ");
        ex_jp(o, "%s{\"item\":%d", i ? ",\n" : "", i);
        kstr(o, "file", file);
        ex_jp(o, ",\"no_first_person_model\":%d", (int)ex_be32(r + 8));
        ex_jp(o, ",\"watch\":["); vid(o, ids[0]); ex_jp(o, ","); vid(o, ids[1]);
        ex_jp(o, "],\"watch_text\":["); vtext(o, c, ids[0]); ex_jp(o, ","); vtext(o, c, ids[1]); ex_jp(o, "]");
        kid(o, c, "long", ids[2]);
        kid(o, c, "short", ids[3]);
        ex_jp(o, "}");
    }
    ex_jp(o, "\n]");
    return 0;
}

typedef struct { int level; const char *file; } FolderLevel;

static int names_folders(CvBuf *o, NamesCtx *c, uint32_t off, FolderLevel *lv, int *nlv)
{
    if ((size_t)off + (size_t)(N_FOLDERS + 1) * 28 > c->len) REFUSE("folder table runs past the end of cdata");
    const uint8_t *t = c->cd + off;
    int nh = 0, nl = 0;
    ex_jp(o, "\"folders\":[\n");
    for (int i = 0; i < N_FOLDERS; i++) {
        const uint8_t *r = t + (size_t)i * 28;
        const char *label = cstr_at(c, ex_be32(r));
        const char *brief = cstr_at(c, ex_be32(r + 24));
        int level = (int)ex_be32(r + 8), hdr = (int)ex_be32(r + 16);
        if (!label) REFUSE("folder %d: label pointer does not resolve", i);
        if ((ex_be16(r + 4) >> 10) != 39) REFUSE("folder %d: name id is not in bank 39", i);
        if (hdr == 1) nh++;
        else if (hdr == 0) {
            if (nl >= 20 || !brief) REFUSE("folder %d: too many levels or no briefing file", i);
            lv[nl].level = level; lv[nl].file = brief; nl++;
        } else REFUSE("folder %d: header flag %d", i, hdr);
        ex_jp(o, "%s{\"folder\":%d", i ? ",\n" : "", i);
        kstr(o, "label", label);
        kid(o, c, "name", ex_be16(r + 4));
        kid(o, c, "short", ex_be16(r + 6));
        ex_jp(o, ",\"level\":%d,\"unknown_12\":%d,\"header\":%s,\"stage\":%d", level, (int)ex_be32(r + 12), hdr ? "true" : "false",
              (int)ex_be32(r + 20));
        kstr(o, "briefing_file", brief);
        ex_jp(o, "}");
    }
    ex_jp(o, "\n]");
    const uint8_t *tm = t + (size_t)N_FOLDERS * 28;
    if (ex_be32(tm) || ex_be16(tm + 4) || ex_be16(tm + 6) || (int)ex_be32(tm + 8) != -1 || (int)ex_be32(tm + 20) != -1)
        REFUSE("folder table terminator is not where expected");
    if (nh != 9 || nl != 20) REFUSE("folder table has %d headers and %d levels", nh, nl);
    {
        const uint8_t *r0 = t, *r1 = t + 28;
        const char *l0 = cstr_at(c, ex_be32(r0)), *b1 = cstr_at(c, ex_be32(r1 + 24));
        if (!l0 || strcmp(l0, "1") || ex_be16(r0 + 4) != 0x9C78 || (int)ex_be32(r1 + 8) != 33 || ex_be16(r1 + 4) != 0x9C79 ||
            !b1 || strcmp(b1, "UbriefdamZ"))
            REFUSE("folder records 0 and 1 do not have the documented invariant");
    }
    *nlv = nl;
    return 0;
}

static int names_briefings(CvBuf *o, NamesCtx *c, const uint8_t *rom, size_t rom_size, const CvRegionData *rd, const FolderLevel *lv,
                           int nlv, int *nobj)
{
    *nobj = 0;
    ex_jp(o, "\"briefings\":[\n");
    for (int i = 0; i < nlv; i++) {
        CvBuf b;
        memset(&b, 0, sizeof b);
        const CvFileRow *row = cv_find_row(rd, lv[i].file);
        if (!row || (size_t)row->off + row->size > rom_size) REFUSE("briefing file %s is not in the ROM", lv[i].file);
        if (cv_rz_inflate(rom + row->off, row->size, &b) || b.len != 48) {
            cv_buf_free(&b);
            REFUSE("briefing file %s does not decompress to 48 bytes", lv[i].file);
        }
        int bank = level_bank(lv[i].level);
        ex_jp(o, "%s{\"level\":%d,\"file\":", i ? ",\n" : "", lv[i].level); ex_jstr(o, lv[i].file);
        ex_jp(o, ",\"bank\":%d,\"pages\":[", bank);
        for (int k = 0; k < 4; k++) { if (k) ex_jp(o, ","); vid(o, ex_be16(b.p + k * 2)); }
        ex_jp(o, "],\"pages_text\":[");
        for (int k = 0; k < 4; k++) { if (k) ex_jp(o, ","); vtext(o, c, ex_be16(b.p + k * 2)); }
        ex_jp(o, "],\"objectives\":[");
        int first = 1;
        for (int k = 0; k < 10; k++) {
            unsigned id = ex_be16(b.p + 8 + k * 4), diff = ex_be16(b.p + 10 + k * 4);
            if (!id) continue;
            if (diff > 2) { cv_buf_free(&b); REFUSE("briefing %s: difficulty %u", lv[i].file, diff); }
            ex_jp(o, "%s{\"id\":\"%04X\",\"difficulty\":%u,\"text\":", first ? "" : ",", id, diff);
            vtext(o, c, id);
            ex_jp(o, "}");
            first = 0;
            (*nobj)++;
        }
        ex_jp(o, "]}");
        cv_buf_free(&b);
    }
    ex_jp(o, "\n]");
    return 0;
}

static int names_mpstages(CvBuf *o, NamesCtx *c, uint32_t off)
{
    if ((size_t)off + (size_t)N_MPSTAGES * 24 > c->len) REFUSE("multiplayer stage table runs past the end of cdata");
    if (ex_be16(c->cd + off) != 0x9C9A || ex_be16(c->cd + off + 2) != 0x9C9B) REFUSE("multiplayer stage 0 does not have the documented ids");
    ex_jp(o, "\"mp_stages\":[\n");
    for (int i = 0; i < N_MPSTAGES; i++) {
        const uint8_t *r = c->cd + off + (size_t)i * 24;
        ex_jp(o, "%s{\"stage\":%d", i ? ",\n" : "", i);
        kid(o, c, "name", ex_be16(r));
        kid(o, c, "select", ex_be16(r + 2));
        ex_jp(o, ",\"photo\":%d,\"level\":%d,\"unlock_solo_stage\":%d,\"min_players\":%d,\"max_players\":%d}", (int)ex_be32(r + 4),
              (int)ex_be32(r + 8), (int)ex_be32(r + 12), (int)ex_be32(r + 16), (int)ex_be32(r + 20));
    }
    ex_jp(o, "\n]");
    return 0;
}

static int names_mpchars(CvBuf *o, NamesCtx *c, uint32_t off)
{
    if ((size_t)off + (size_t)N_MPCHARS * 12 > c->len) REFUSE("multiplayer character table runs past the end of cdata");
    if (ex_be16(c->cd + off) != 0x9CB8 || ex_be16s(c->cd + off + 4) != 5) REFUSE("multiplayer character 0 does not have the documented values");
    ex_jp(o, "\"mp_characters\":[\n");
    for (int i = 0; i < N_MPCHARS; i++) {
        const uint8_t *r = c->cd + off + (size_t)i * 12;
        float sc = ex_bef32(r + 8);
        ex_jp(o, "%s{\"character\":%d", i ? ",\n" : "", i);
        kid(o, c, "name", ex_be16(r));
        ex_jp(o, ",\"gender\":\"%s\",\"photo\":%d,\"body\":%d,\"head\":%d,\"scale\":%.9g}", r[2] == 1 ? "male" : "female", r[3],
              (int)ex_be16s(r + 4), (int)ex_be16s(r + 6), isfinite(sc) ? (double)sc : 0.0);
    }
    ex_jp(o, "\n]");
    return 0;
}

/* 0 ok, else the number of failures */
static int write_names(const char *dir, const uint8_t *rom, size_t rom_size, const char *region, const CvRegionData *rd, int ri,
                       const TextSet *sets, ExLogFn log, void *user)
{
    CvBuf cd, o;
    NamesCtx c;
    FolderLevel lv[20];
    int nlv = 0, nobj = 0, rc = 0;
    char path[1200];
    memset(&cd, 0, sizeof cd); memset(&o, 0, sizeof o); memset(&c, 0, sizeof c);
    if ((size_t)k_treg[ri].cdata_rom >= rom_size || cv_rz_inflate(rom + k_treg[ri].cdata_rom, rom_size - k_treg[ri].cdata_rom, &cd) ||
        cd.len > CDATA_MAX_OUT) {
        ex_logf(log, user, "names: cdata segment does not inflate");
        cv_buf_free(&cd);
        return 1;
    }
    c.cd = cd.p; c.len = cd.len; c.base = k_treg[ri].base;
    for (int k = 0; k < 3; k++)
        if (k_lang[k].suffix == k_treg[ri].game_suffix && sets[k].present) { c.gs = &sets[k]; c.japanese = (k_lang[k].suffix == 'J'); }
    if (!c.gs) { ex_logf(log, user, "names: the game-language text set is not in the ROM"); cv_buf_free(&cd); return 1; }
    memset(lv, 0, sizeof lv);

    ex_jp(&o, "{\n\"format\":\"ge007-names/1\",\n\"region\":"); ex_jstr(&o, region);
    ex_jp(&o, ",\n\"game_language\":\"%s\",\n\"text_note\":\"*_text is the game-language text without the final line break; other languages: text_<lang>.json by id\",\n",
          c.japanese ? "ja" : (c.gs->suffix == 'P' ? "en-GB" : "en"));
    if (names_items(&o, &c, k_treg[ri].items)) goto fail;
    ex_jp(&o, ",\n");
    if (names_folders(&o, &c, k_treg[ri].folders, lv, &nlv)) goto fail;
    ex_jp(&o, ",\n");
    if (names_briefings(&o, &c, rom, rom_size, rd, lv, nlv, &nobj)) goto fail;
    ex_jp(&o, ",\n");
    if (names_mpstages(&o, &c, k_treg[ri].mpstages)) goto fail;
    ex_jp(&o, ",\n");
    if (names_mpchars(&o, &c, k_treg[ri].mpchars)) goto fail;
    ex_jp(&o, ",\n\"level_banks\":[\n");
    for (int i = 0; i < N_LEVELBANK; i++)
        ex_jp(&o, "%s{\"level\":%d,\"stage\":\"%s\",\"bank\":%d,\"bank_name\":\"%s\"}", i ? ",\n" : "", k_levelbank[i].level,
              k_levelbank[i].stage, k_levelbank[i].bank, k_bank[k_levelbank[i].bank].name);
    ex_jp(&o, "\n]\n}\n");
    snprintf(path, sizeof path, "%s/names.json", dir);
    if (ex_write_file(path, o.p, o.len)) { ex_logf(log, user, "cannot write %s", path); rc = 1; }
    else ex_logf(log, user, "names: %d items, %d folders, %d briefings (%d objectives), %d stages, %d characters -> %s", N_ITEMS, N_FOLDERS, nlv,
                 nobj, N_MPSTAGES, N_MPCHARS, path);
    cv_buf_free(&o); cv_buf_free(&cd);
    return rc;
fail:
    ex_logf(log, user, "names refused: %s", c.err);
    cv_buf_free(&o); cv_buf_free(&cd);
    return 1;
}

/* ------------------------------------------------------------------------ */
/* entry                                                                    */
/* ------------------------------------------------------------------------ */

int ex_export_text(const char *rom_path, const char *out_dir, ExLogFn log, void *user)
{
    uint8_t *rom = NULL;
    size_t rom_size = 0;
    const char *region = NULL;
    const CvRegionData *rd = NULL;
    int failed = 0, ri;
    static TextSet sets[3];
    if (ex_open_rom(rom_path, log, user, &rom, &rom_size, &region, &rd)) return 1;
    for (ri = 0; ri < 3; ri++) if (!strcmp(region, k_treg[ri].region)) break;
    if (ri == 3) { ex_logf(log, user, "text: no constants for region %s", region); free(rom); return 1; }
    if (ex_mkdir_p(out_dir)) { ex_logf(log, user, "cannot create %s", out_dir); free(rom); return 1; }
    memset(sets, 0, sizeof sets);
    for (int k = 0; k < 3; k++) {
        failed += load_set(rom, rom_size, rd, &sets[k], k_lang[k].suffix, log, user);
        sets[k].game = (k_lang[k].suffix == k_treg[ri].game_suffix);
        if (sets[k].present) failed += write_set(out_dir, region, &sets[k], k_lang[k].suffix == 'J', k_lang[k].lang, log, user);
    }
    failed += write_names(out_dir, rom, rom_size, region, rd, ri, sets, log, user);
    for (int k = 0; k < 3; k++) set_free(&sets[k]);
    free(rom);
    return failed;
}
