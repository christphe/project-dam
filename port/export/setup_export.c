/* setup_export.c -- stage setup of a level -> setup.json / setup_mp.json (docs/specs/setup.md).
 *
 * CLEAN ROOM: written from docs/specs/setup.md (with level-decor.md for the level names) only, plus the converter
 * library's file table, rarezip inflate and SHA-1, and the mission system's field NAMES and grouping (the offsets
 * used here are the spec's N64 offsets). See export.h. All ROM values are big-endian.
 */
#include "export_common.h"
#include "conv_romcheck.h"
#include "setup_export.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#define jp ex_jp

/* ------------------------------------------------------------------------ */
/* level table (setup.md §Which setup belongs to which level)               */
/* ------------------------------------------------------------------------ */

typedef struct { const char *level; int stage; const char *mid; } SetupDef;   /* Usetup<mid>Z / Ump_setup<mid>Z */

static const SetupDef k_setups[] = {
    { "dam", 33, "dam" },         { "facility", 34, "ark" },    { "runway", 35, "run" },     { "surface1", 36, "sevx" },
    { "bunker1", 9, "sevbunker" }, { "silo", 20, "silo" },       { "frigate", 26, "dest" },   { "surface2", 43, "sevxb" },
    { "bunker2", 27, "sevb" },    { "statue", 22, "statue" },   { "archives", 24, "arch" },  { "streets", 29, "pete" },
    { "depot", 30, "depo" },      { "train", 25, "tra" },       { "jungle", 37, "jun" },     { "control", 23, "control" },
    { "caverns", 39, "cave" },    { "cradle", 41, "crad" },     { "aztec", 28, "azt" },      { "egypt", 32, "cryp" },
    { "cuba", 54, "len" },        { "temple", 38, "dish" },     { "complex", 31, "ref" },    { "caves", 50, "oat" },
    { "basement", 45, "imp" },    { "stack", 46, "ash" },       { "library", 48, "ame" },    { "citadel", 40, "cat" },
};
#define N_SETUPS ((int)(sizeof k_setups / sizeof k_setups[0]))

/* ------------------------------------------------------------------------ */
/* reader                                                                   */
/* ------------------------------------------------------------------------ */

typedef struct {
    const uint8_t *d;
    size_t n;
    char why[200];   /* first problem found; "" = none */
    CvBuf o;         /* the JSON */
    /* propDef index (filled by the walk) */
    size_t nprops;
    uint32_t *poff;
    uint8_t *ptype;
    /* waypoints (patrol paths resolve their pads through them) */
    int nwp;
    int *wp_pad;
    int *tmp;        /* scratch for index lists */
} S;

#define TMPCAP 16384

static void bad(S *s, const char *fmt, ...)
{
    if (s->why[0]) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s->why, sizeof s->why, fmt, ap);
    va_end(ap);
}

static uint32_t r32(S *s, size_t off)
{
    if (off + 4 > s->n) { bad(s, "read past the end of the file at %zu", off); return 0; }
    return ex_be32(s->d + off);
}
static int32_t r32s(S *s, size_t off) { return (int32_t)r32(s, off); }
static float rf(S *s, size_t off)
{
    if (off + 4 > s->n) { bad(s, "read past the end of the file at %zu", off); return 0; }
    return ex_bef32(s->d + off);
}

/* ---- number printing ---- */

static void pfl(CvBuf *o, float v)   /* shortest text that reads back to the same float32 */
{
    char b[48];
    if (v != v || v > 3.4e38f || v < -3.4e38f) { cv_buf_put(o, "0", 1); return; }
    for (int p = 1; p <= 9; p++) {
        snprintf(b, sizeof b, "%.*g", p, (double)v);
        if (strtof(b, NULL) == v) break;
    }
    cv_buf_put(o, b, strlen(b));
}

static void pdb(CvBuf *o, double v)  /* the text mission.json uses for fixed values */
{
    char b[48];
    snprintf(b, sizeof b, "%.15g", v);
    if (strtod(b, NULL) != v) snprintf(b, sizeof b, "%.17g", v);
    cv_buf_put(o, b, strlen(b));
}

static void pf3(CvBuf *o, const float *v)
{
    cv_buf_put(o, "[", 1);
    for (int i = 0; i < 3; i++) { if (i) cv_buf_put(o, ",", 1); pfl(o, v[i]); }
    cv_buf_put(o, "]", 1);
}

static void phex(CvBuf *o, const uint8_t *p, size_t n)
{
    static const char hx[] = "0123456789abcdef";
    cv_buf_put(o, "\"", 1);
    for (size_t i = 0; i < n; i++) {
        char c[2] = { hx[p[i] >> 4], hx[p[i] & 15] };
        cv_buf_put(o, c, 2);
    }
    cv_buf_put(o, "\"", 1);
}

static void pnull_or_int(CvBuf *o, long v) { if (v < 0) cv_buf_put(o, "null", 4); else jp(o, "%ld", v); }

/* zero-terminated string at a file offset; NULL (and a note) when outside the file */
static const char *str_at(S *s, uint32_t off)
{
    if (off >= s->n) { bad(s, "string offset %u outside the file", off); return NULL; }
    const uint8_t *z = memchr(s->d + off, 0, s->n - off);
    if (!z) { bad(s, "unterminated string at %u", off); return NULL; }
    return (const char *)(s->d + off);
}

/* array of s32 at `off`, ending at the first negative value; into s->tmp, returns the count (-1 on error) */
static int ilist(S *s, uint32_t off)
{
    int n = 0;
    if (off == 0 || off >= s->n) { bad(s, "list offset %u outside the file", off); return -1; }
    for (;;) {
        if ((size_t)off + 4 * (size_t)(n + 1) > s->n) { bad(s, "unterminated list at %u", off); return -1; }
        int32_t v = (int32_t)ex_be32(s->d + off + 4 * (size_t)n);
        if (v < 0) break;
        if (n >= TMPCAP) { bad(s, "list at %u too long", off); return -1; }
        s->tmp[n++] = v;
    }
    return n;
}

static void put_ilist(S *s, uint32_t off)
{
    int n = ilist(s, off);
    cv_buf_put(&s->o, "[", 1);
    for (int i = 0; i < n; i++) jp(&s->o, "%s%d", i ? "," : "", s->tmp[i]);
    cv_buf_put(&s->o, "]", 1);
}

/* ------------------------------------------------------------------------ */
/* tile names (setup.md §Tile name)                                         */
/* ------------------------------------------------------------------------ */

static uint32_t pack_tile(const char *s)   /* 0xFFFFFF = invalid */
{
    if (s[0] != 'p' && s[0] != 'q') return 0xFFFFFFu;
    const char *p = s + 1;
    unsigned num = 0;
    int nd = 0;
    while (*p >= '0' && *p <= '9') {
        num = num * 10 + (unsigned)(*p - '0');
        if (num > 32767) return 0xFFFFFFu;
        p++; nd++;
    }
    if (!nd || *p < 'a' || *p > 'z') return 0xFFFFFFu;
    unsigned lo = (unsigned)(*p - 'a') << 3;
    p++;
    if (*p) {
        if (*p < '1' || *p > '7') return 0xFFFFFFu;
        lo |= (unsigned)(*p - '0');
        p++;
        if (*p) return 0xFFFFFFu;
    }
    return ((((unsigned)(s[0] - 'p') << 15) | num) << 8) | lo;
}

/* ------------------------------------------------------------------------ */
/* pads (setup.md §Pads and bound pads)                                     */
/* ------------------------------------------------------------------------ */

static void emit_pad(S *s, size_t off, int num, int bound, float inv)
{
    CvBuf *o = &s->o;
    float pos[3], wpos[3], up[3], look[3];
    for (int i = 0; i < 3; i++) {
        pos[i] = rf(s, off + 4 * (size_t)i);
        up[i] = rf(s, off + 12 + 4 * (size_t)i);
        look[i] = rf(s, off + 24 + 4 * (size_t)i);
        wpos[i] = pos[i] * inv;
    }
    const char *nm = str_at(s, r32(s, off + 36));
    if (!nm) nm = "";
    jp(o, "{\"num\":%d,\"pos\":", num); pf3(o, wpos);
    jp(o, ",\"file_pos\":"); pf3(o, pos);
    jp(o, ",\"up\":"); pf3(o, up);
    jp(o, ",\"look\":"); pf3(o, look);
    jp(o, ",\"tile\":"); ex_jstr(o, nm);
    if (!nm[0]) jp(o, ",\"stan_id\":null");
    else jp(o, ",\"stan_id\":%u", pack_tile(nm));
    if (bound) {
        float fb[6], wb[6];
        for (int i = 0; i < 6; i++) { fb[i] = rf(s, off + 44 + 4 * (size_t)i); wb[i] = fb[i] * inv; }
        /* file order: xmin xmax ymin ymax zmin zmax */
        jp(o, ",\"bbox\":{\"min\":[");
        pfl(o, wb[0]); jp(o, ","); pfl(o, wb[2]); jp(o, ","); pfl(o, wb[4]);
        jp(o, "],\"max\":[");
        pfl(o, wb[1]); jp(o, ","); pfl(o, wb[3]); jp(o, ","); pfl(o, wb[5]);
        jp(o, "]},\"file_bbox\":[");
        for (int i = 0; i < 6; i++) { if (i) jp(o, ","); pfl(o, fb[i]); }
        jp(o, "]");
    }
    jp(o, "}");
}

/* ------------------------------------------------------------------------ */
/* propDefs (setup.md §PropDefs)                                            */
/* ------------------------------------------------------------------------ */

typedef struct { const char *name; short off; char size; char kind; } Fd;   /* kind: u s x(fixed) c(hundredths) */

#define FU(n, o, z) { n, o, z, 'u' }
#define FS(n, o, z) { n, o, z, 's' }
#define FX(n, o) { n, o, 4, 'x' }
#define FC(n, o) { n, o, 4, 'c' }
#define SL(k) FU("slot" #k "_model", 128 + 4 * k, 2), FU("slot" #k "_qty", 130 + 4 * k, 2)

static const Fd f_guard[] = { FU("extrascale", 0, 2), FU("state", 2, 1), FU("preset", 12, 2), FU("chrpreset1", 14, 2),
                              FU("health", 16, 2), FU("reaction_time", 18, 2), FU("bitflags", 20, 2), FS("head", 22, 2) };
static const Fd f_common[] = { FU("extrascale", 0, 2), FU("state", 2, 1), FU("flags", 8, 4), FU("flags2", 12, 4),
                               FX("maxdamage", 112), FX("damage", 116) };
static const Fd f_door[] = { FS("linked_door_offset", 128, 4), FX("max_frac", 132), FX("perim_frac", 136), FX("accel", 140),
                             FX("decel", 144), FX("max_speed", 148), FU("door_flags", 152, 2), FU("door_type", 154, 2),
                             FU("keyflags", 156, 4), FU("auto_close_frames", 160, 4), FU("open_sound", 164, 4) };
static const Fd f_key[] = { FU("keyflags", 128, 4) };
static const Fd f_cctv[] = { FS("lookpad", 128, 4) };
static const Fd f_magazine[] = { FS("ammo_type", 128, 4) };
static const Fd f_collectable[] = { FS("weaponnum", 128, 1), FS("linked_weapon_type", 129, 1), FS("timer", 130, 2) };
static const Fd f_monitor[] = { FS("owner_offset", 244, 4), FS("owner_part", 248, 4), FS("image_num", 252, 4) };
static const Fd f_multi_monitor[] = { FU("image_num0", 592, 1), FU("image_num1", 593, 1), FU("image_num2", 594, 1),
                                      FU("image_num3", 595, 1) };
static const Fd f_autogun[] = { FS("pad_id", 128, 4), FS("rot_related", 132, 4), FS("unk88", 136, 4), FS("unk8c", 140, 4),
                                FS("speed", 164, 4), FS("aimdist", 168, 4), FS("unkac", 172, 4), FS("unkb8", 184, 4),
                                FS("unkbc", 188, 4), FS("unkc0", 192, 4), FS("is_active", 208, 4) };
static const Fd f_ammo[] = { SL(0), SL(1), SL(2), SL(3), SL(4), SL(5), SL(6), SL(7), SL(8), SL(9), SL(10), SL(11), SL(12) };
static const Fd f_armour[] = { FX("initial_amount", 128) };
static const Fd f_vehicle[] = { FU("ai_offset", 132, 2), FS("ai_return_list", 134, 2), FU("ailist", 128, 4) };
static const Fd f_tank[] = { FS("unka4", 164, 4), FS("unka8", 168, 4), FS("unkac", 172, 4), FS("unkb0", 176, 4),
                             FS("unkb4", 180, 4), FS("unkb8", 184, 4), FS("unkbc", 188, 4), FS("unkc0", 192, 4) };
static const Fd f_tinted[] = { FS("tint_dist", 128, 4), FS("cull_dist", 132, 4), FS("unk90", 144, 4) };

static const Fd f_door_scale[] = { FX("scale", 4) };
static const Fd f_link[] = { FS("offset1", 4, 4), FS("offset2", 8, 4) };
static const Fd f_gattr[] = { FS("chrnum", 4, 4), FU("word", 8, 4), FU("grenade_probability", 11, 1) };
static const Fd f_switch[] = { FS("offset1", 4, 4), FS("offset2", 8, 4) };
static const Fd f_tag[] = { FU("tag_id", 4, 2), FS("offset", 6, 2) };
static const Fd f_obj_start[] = { FS("number", 4, 4), FU("text", 8, 4), FS("min_difficulty", 12, 4) };
static const Fd f_tagid[] = { FS("tag_id", 4, 4) };
static const Fd f_flags[] = { FU("stage_flags", 4, 4) };
static const Fd f_enter_room[] = { FS("pad", 4, 4) };
static const Fd f_deposit_room[] = { FS("tag_id", 4, 4), FS("pad", 8, 4) };
static const Fd f_briefing[] = { FS("page", 4, 4), FU("text", 8, 4) };
static const Fd f_rename[] = { FS("offset", 4, 4), FS("weapon", 8, 4), FU("title1", 12, 4), FU("title2", 16, 4),
                               FU("short_text", 20, 4), FU("long_text", 24, 4), FU("pickup_text", 28, 4) };
static const Fd f_lock[] = { FS("offset1", 4, 4), FS("offset2", 8, 4) };
static const Fd f_safe_item[] = { FS("offset_item", 4, 4), FS("offset_safe", 8, 4), FS("offset_door", 12, 4) };
static const Fd f_camera[] = { FC("x", 4), FC("y", 8), FC("z", 12), FX("theta", 16), FX("verta", 20), FS("pad", 24, 4) };

typedef struct {
    int type;
    const char *kind;
    int size;
    int obj;
    const char *group;   /* object types: the group name of the type's own fields (NULL: none) */
    const Fd *fd;
    int nfd;
} TypeInfo;

#define TI(t, k, sz, ob, grp, tbl) { t, k, sz, ob, grp, tbl, (int)(sizeof tbl / sizeof *tbl) }
#define TN(t, k, sz, ob, grp) { t, k, sz, ob, grp, NULL, 0 }

static const TypeInfo k_types[] = {
    TI(1, "door", 256, 1, "door", f_door),
    TI(2, "door_scale", 8, 0, NULL, f_door_scale),
    TN(3, "prop", 128, 1, NULL),
    TI(4, "key", 132, 1, "key", f_key),
    TN(5, "alarm", 128, 1, NULL),
    TI(6, "cctv", 236, 1, "cctv", f_cctv),
    TI(7, "magazine", 132, 1, "magazine", f_magazine),
    TI(8, "collectable", 136, 1, "collectable", f_collectable),
    TI(9, "guard", 28, 0, NULL, f_guard),
    TI(10, "monitor", 256, 1, "monitor", f_monitor),
    TI(11, "multi_monitor", 596, 1, "multi_monitor", f_multi_monitor),
    TN(12, "rack", 128, 1, NULL),
    TI(13, "autogun", 216, 1, "autogun", f_autogun),
    TI(14, "link", 12, 0, NULL, f_link),
    TN(17, "hat", 128, 1, NULL),
    TI(18, "guard_attribute", 12, 0, NULL, f_gattr),
    TI(19, "switch", 16, 0, NULL, f_switch),
    TI(20, "ammo_box", 180, 1, "ammo", f_ammo),
    TI(21, "body_armour", 136, 1, "armour", f_armour),
    TI(22, "tag", 16, 0, NULL, f_tag),
    TI(23, "objective_start", 16, 0, NULL, f_obj_start),
    TN(24, "objective_end", 4, 0, NULL),
    TI(25, "objective_destroy", 8, 0, NULL, f_tagid),
    TI(26, "objective_complete_when", 8, 0, NULL, f_flags),
    TI(27, "objective_fail_when", 8, 0, NULL, f_flags),
    TI(28, "objective_collect", 8, 0, NULL, f_tagid),
    TI(29, "objective_deposit", 8, 0, NULL, f_tagid),
    TI(30, "objective_photograph", 16, 0, NULL, f_tagid),
    TN(31, "objective_null", 4, 0, NULL),
    TI(32, "objective_enter_room", 16, 0, NULL, f_enter_room),
    TI(33, "objective_deposit_in_room", 20, 0, NULL, f_deposit_room),
    TI(34, "objective_copy_item", 12, 0, NULL, f_tagid),
    TI(35, "briefing_text", 16, 0, NULL, f_briefing),
    TN(36, "gas_releasing", 128, 1, NULL),
    TI(37, "rename", 40, 0, NULL, f_rename),
    TI(38, "lock_door", 16, 0, NULL, f_lock),
    TI(39, "vehicle", 176, 1, "vehicle", f_vehicle),
    TI(40, "aircraft", 180, 1, "aircraft", f_vehicle),
    TN(42, "glass", 128, 1, NULL),
    TN(43, "safe", 128, 1, NULL),
    TI(44, "safe_item", 20, 0, NULL, f_safe_item),
    TI(45, "tank", 224, 1, "tank", f_tank),
    TI(46, "camera_position", 28, 0, NULL, f_camera),
    TI(47, "tinted_glass", 148, 1, "tinted_glass", f_tinted),
};
#define TYPE_END 48

static const TypeInfo *type_info(int t)
{
    for (size_t i = 0; i < sizeof k_types / sizeof *k_types; i++) if (k_types[i].type == t) return &k_types[i];
    return NULL;
}

static double fval(const uint8_t *r, const Fd *f)
{
    uint32_t u = 0;
    for (int i = 0; i < f->size; i++) u = (u << 8) | r[f->off + i];
    switch (f->kind) {
    case 'x': return (int32_t)u / 65536.0;
    case 'c': return (int32_t)u / 100.0;
    case 's':
        if (f->size == 1) return (int8_t)u;
        if (f->size == 2) return (int16_t)u;
        return (int32_t)u;
    default: return u;
    }
}

static void put_fd(CvBuf *o, const Fd *f, const uint8_t *r, int comma)
{
    double v = fval(r, f);
    jp(o, "%s\"%s\":", comma ? "," : "", f->name);
    if (f->kind == 'x' || f->kind == 'c') pdb(o, v); else jp(o, "%.0f", v);
}

static void put_fds(CvBuf *o, const Fd *f, int n, const uint8_t *r, int comma)
{
    for (int i = 0; i < n; i++) put_fd(o, &f[i], r, comma || i);
}

/* index + relative offset -> absolute index, or -1 when outside 0..count-1 */
static long resolve(const S *s, size_t idx, long rel)
{
    long t = (long)idx + rel;
    return (t < 0 || (size_t)t >= s->nprops) ? -1 : t;
}

static void put_ref(S *s, const char *name, size_t idx, long rel, int *first)
{
    jp(&s->o, "%s\"%s\":", *first ? "" : ",", name);
    *first = 0;
    pnull_or_int(&s->o, resolve(s, idx, rel));
}

static void emit_prop(S *s, size_t idx)
{
    CvBuf *o = &s->o;
    uint32_t off = s->poff[idx];
    int type = s->ptype[idx];
    const TypeInfo *ti = type_info(type);
    const uint8_t *r = s->d + off;
    jp(o, "{\"prop\":%zu,\"type\":%d,\"kind\":\"%s\",\"offset\":%u", idx, type, ti->kind, off);

    if (type == 9) {
        jp(o, ",\"chr\":%u,\"pad\":%u,\"body\":%u,\"ailist\":%u,\"ai\":%u", ex_be16(r + 4), ex_be16(r + 6), ex_be16(r + 8),
           ex_be16(r + 10), ex_be16(r + 10));
        jp(o, ",\"fields\":{"); put_fds(o, ti->fd, ti->nfd, r, 0); jp(o, "}");
    } else if (ti->obj) {
        uint32_t flags = ex_be32(r + 8), flags2 = ex_be32(r + 12);
        int pad_raw = ex_be16s(r + 6);
        int inside = (flags & 0x8000u) != 0, carried = !inside && (flags & 0x4000u) != 0;
        jp(o, ",\"model\":%d", ex_be16s(r + 4));
        if (inside) jp(o, ",\"pad\":null");
        else if (carried) jp(o, ",\"pad\":null,\"carrier\":%d", pad_raw);
        else jp(o, ",\"pad\":%d", type == 1 ? pad_raw + 10000 : pad_raw);
        jp(o, ",\"pad_raw\":%d", pad_raw);
        if (type == 8) {
            int wn = (int8_t)r[128];
            jp(o, ",\"item\":%d", wn);
            if ((uint8_t)wn >= 0xF0 && (uint8_t)wn <= 0xF7) jp(o, ",\"mp_weapon_slot\":%d", (uint8_t)wn - 0xF0);
        } else if (type == 4) {
            jp(o, ",\"item\":%u", ex_be32(r + 128));
        }
        if (type == 39 || type == 40) jp(o, ",\"ai\":%u", ex_be32(r + 128));
        static const struct { const char *n; uint32_t bit; } lo[] = {
            { "agent", 0x10 }, { "secret", 0x20 }, { "00", 0x40 }, { "007", 0x80 },
            { "multi", 0x08 }, { "2p", 0x400000 }, { "3p", 0x800000 }, { "4p", 0x1000000 } };
        jp(o, ",\"load_on\":{");
        for (int i = 0; i < 8; i++) jp(o, "%s\"%s\":%s", i ? "," : "", lo[i].n, (flags2 & lo[i].bit) ? "false" : "true");
        jp(o, "},\"fields\":{");
        put_fds(o, f_common, 6, r, 0);
        if (ti->group && ti->nfd) {
            jp(o, ",\"%s\":{", ti->group);
            put_fds(o, ti->fd, ti->nfd, r, 0);
            jp(o, "}");
        }
        jp(o, "}");
        int first = 1;
        if (inside || type == 1) {
            jp(o, ",\"refs\":{");
            if (inside) put_ref(s, "inside", idx, pad_raw, &first);
            if (type == 1) {
                long rel = ex_be32(r + 128) ? (int32_t)ex_be32(r + 128) : 0;
                if (rel) put_ref(s, "linked_door", idx, rel, &first);
                else { jp(o, "%s\"linked_door\":null", first ? "" : ","); first = 0; }
            }
            jp(o, "}");
        }
    } else {
        jp(o, ",\"fields\":{"); put_fds(o, ti->fd, ti->nfd, r, 0); jp(o, "}");
        int first = 1;
        const char *n1 = NULL, *n2 = NULL, *n3 = NULL;
        long r1 = 0, r2 = 0, r3 = 0;
        switch (type) {
        case 14: n1 = "first"; r1 = (int32_t)ex_be32(r + 4); n2 = "second"; r2 = (int32_t)ex_be32(r + 8); break;
        case 19: n1 = "switch"; r1 = (int32_t)ex_be32(r + 4); n2 = "door"; r2 = (int32_t)ex_be32(r + 8); break;
        case 22: n1 = "tagged"; r1 = ex_be16s(r + 6); break;
        case 37: n1 = "renamed"; r1 = (int32_t)ex_be32(r + 4); break;
        case 38: n1 = "door"; r1 = (int32_t)ex_be32(r + 4); n2 = "lock"; r2 = (int32_t)ex_be32(r + 8); break;
        case 44: n1 = "item"; r1 = (int32_t)ex_be32(r + 4); n2 = "safe"; r2 = (int32_t)ex_be32(r + 8);
                 n3 = "door"; r3 = (int32_t)ex_be32(r + 12); break;
        default: break;
        }
        if (n1) {
            jp(o, ",\"refs\":{");
            put_ref(s, n1, idx, r1, &first);
            if (n2) put_ref(s, n2, idx, r2, &first);
            if (n3) put_ref(s, n3, idx, r3, &first);
            jp(o, "}");
        }
    }
    jp(o, ",\"raw\":"); phex(o, r, (size_t)ti->size);
    jp(o, "}");
}

/* the object a tag id names: the first tag record (22) with that id, resolved; -1 when none / outside */
static long tag_object(const S *s, long tag_id)
{
    for (size_t i = 0; i < s->nprops; i++) {
        if (s->ptype[i] != 22) continue;
        const uint8_t *r = s->d + s->poff[i];
        if (ex_be16(r + 4) == tag_id) return resolve(s, i, ex_be16s(r + 6));
    }
    return -1;
}

static void emit_objectives(S *s)
{
    CvBuf *o = &s->o;
    int ngroups = 0, open = 0, nc = 0;
    jp(o, ",\"objectives\":[");
    for (size_t i = 0; i < s->nprops; i++) {
        int t = s->ptype[i];
        const uint8_t *r = s->d + s->poff[i];
        if (t == 23) {
            if (open) jp(o, "]}");
            jp(o, "%s{\"prop\":%zu,\"number\":%d,\"text\":%u,\"min_difficulty\":%d,\"conditions\":[", ngroups ? "," : "", i,
               (int32_t)ex_be32(r + 4), ex_be32(r + 8), (int32_t)ex_be32(r + 12));
            ngroups++; open = 1; nc = 0;
        } else if (t == 24) {
            if (open) jp(o, "]}");
            open = 0;
        } else if (t >= 25 && t <= 34 && open) {
            const TypeInfo *ti = type_info(t);
            jp(o, "%s{\"prop\":%zu,\"type\":%d,\"kind\":\"%s\"", nc ? "," : "", i, t, ti->kind);
            put_fds(o, ti->fd, ti->nfd, r, 1);
            if (t == 25 || t == 28 || t == 29 || t == 30 || t == 33) {
                jp(o, ",\"object\":");
                pnull_or_int(o, tag_object(s, (int32_t)ex_be32(r + 4)));
            }
            jp(o, "}");
            nc++;
        }
    }
    if (open) jp(o, "]}");
    jp(o, "]");
}

static void emit_briefing(S *s)
{
    int n = 0;
    jp(&s->o, ",\"briefing\":[");
    for (size_t i = 0; i < s->nprops; i++) {
        if (s->ptype[i] != 35) continue;
        const uint8_t *r = s->d + s->poff[i];
        jp(&s->o, "%s{\"prop\":%zu,\"page\":%d,\"text\":%u}", n++ ? "," : "", i, (int32_t)ex_be32(r + 4), ex_be32(r + 8));
    }
    jp(&s->o, "]");
}

/* ------------------------------------------------------------------------ */
/* AI lists (setup.md §AI lists)                                            */
/* ------------------------------------------------------------------------ */

#define ROW16(a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p) a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p
static const uint8_t k_ai_len[256] = {   /* 0 = opcode 0xAD (string) */
    ROW16(2, 2, 2, 1, 1, 4, 3, 1, 1, 1, 9, 2, 1, 1, 2, 2),
    ROW16(2, 2, 2, 2, 6, 6, 6, 6, 4, 4, 2, 5, 3, 1, 3, 3),
    ROW16(2, 1, 1, 2, 4, 1, 1, 2, 2, 2, 2, 2, 3, 3, 3, 2),
    ROW16(3, 3, 2, 1, 3, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2),
    ROW16(3, 2, 2, 2, 4, 2, 2, 3, 3, 3, 3, 4, 4, 7, 7, 5),
    ROW16(5, 4, 6, 6, 5, 4, 3, 3, 4, 3, 3, 3, 3, 3, 2, 2),
    ROW16(2, 2, 2, 2, 3, 4, 2, 2, 4, 3, 3, 3, 4, 3, 3, 3),
    ROW16(3, 3, 4, 4, 4, 4, 3, 3, 3, 3, 3, 3, 4, 4, 3, 3),
    ROW16(3, 2, 2, 2, 3, 2, 2, 2, 2, 3, 2, 3, 2, 2, 2, 3),
    ROW16(3, 2, 2, 2, 2, 2, 3, 3, 3, 4, 5, 5, 6, 5, 5, 6),
    ROW16(6, 6, 7, 6, 6, 7, 6, 6, 7, 2, 3, 3, 4, 0, 1, 1),
    ROW16(1, 1, 2, 5, 5, 1, 1, 3, 1, 1, 2, 4, 4, 12, 11, 9),
    ROW16(8, 5, 3, 3, 4, 5, 6, 6, 6, 2, 5, 2, 5, 5, 2, 2),
    ROW16(4, 2, 1, 1, 3, 6, 4, 2, 1, 5, 1, 1, 2, 1, 1, 2),
    ROW16(3, 3, 4, 2, 2, 3, 5, 2, 2, 1, 1, 2, 1, 1, 13, 1),
    ROW16(2, 2, 3, 2, 4, 2, 1, 3, 3, 1, 1, 1, 2, 1, 1, 1),   /* 0xFD..0xFF: undefined, counted as 1 */
};

/* size in bytes of the list at `off`, up to and including its 0x04; -1 when it runs off the file */
static long ai_size(const S *s, size_t off)
{
    size_t p = off;
    for (;;) {
        if (p >= s->n) return -1;
        unsigned op = s->d[p];
        size_t len = k_ai_len[op];
        if (op == 0xAD) {
            const uint8_t *z = memchr(s->d + p + 1, 0, s->n - p - 1);
            if (!z) return -1;
            len = (size_t)(z - (s->d + p)) + 1;
        }
        p += len;
        if (p > s->n) return -1;
        if (op == 0x04) break;
    }
    return (long)(p - off);
}

static void emit_ailists(S *s, uint32_t tab)
{
    int n = 0;
    jp(&s->o, ",\"ailists\":[");
    for (size_t p = tab; s->why[0] == 0; p += 8) {
        uint32_t lo = r32(s, p);
        if (!lo) break;
        int32_t id = r32s(s, p + 4);
        long sz = lo < s->n ? ai_size(s, lo) : -1;
        if (sz < 0) { bad(s, "AI list %d at %u does not end inside the file", (int)id, lo); break; }
        jp(&s->o, "%s{\"id\":%d,\"class\":\"%s\",\"offset\":%u,\"size\":%ld,\"raw\":", n++ ? "," : "", id,
           id <= 1024 ? "global" : id < 4096 ? "chr" : "bg", lo, sz);
        phex(&s->o, s->d + lo, (size_t)sz);
        jp(&s->o, "}");
    }
    jp(&s->o, "]");
}

/* ------------------------------------------------------------------------ */
/* navigation graph, paths, intro                                           */
/* ------------------------------------------------------------------------ */

static void emit_waypoints(S *s, uint32_t off)
{
    int n = 0;
    jp(&s->o, ",\"waypoints\":[");
    for (size_t p = off; s->why[0] == 0; p += 16) {
        int32_t pad = r32s(s, p);
        if (pad < 0) break;
        if (n >= TMPCAP) { bad(s, "too many waypoints"); break; }
        s->wp_pad[n] = pad;
        jp(&s->o, "%s{\"pad\":%d,\"group\":%d,\"neighbours\":", n ? "," : "", pad, r32s(s, p + 8));
        put_ilist(s, r32(s, p + 4));
        jp(&s->o, "}");
        n++;
    }
    s->nwp = n;
    jp(&s->o, "]");
}

static void emit_groups(S *s, uint32_t off)
{
    int n = 0;
    jp(&s->o, ",\"waypoint_groups\":[");
    for (size_t p = off; s->why[0] == 0; p += 12) {
        uint32_t nb = r32(s, p);
        if (!nb) break;
        jp(&s->o, "%s{\"waypoints\":", n++ ? "," : "");
        put_ilist(s, r32(s, p + 4));
        jp(&s->o, ",\"neighbours\":");
        put_ilist(s, nb);
        jp(&s->o, "}");
    }
    jp(&s->o, "]");
}

static void emit_paths(S *s, uint32_t off)
{
    int n = 0;
    jp(&s->o, ",\"patrol_paths\":[");
    for (size_t p = off; s->why[0] == 0; p += 8) {
        uint32_t lo = r32(s, p);
        if (!lo) break;
        if (p + 8 > s->n) { bad(s, "patrol path outside the file"); break; }
        jp(&s->o, "%s{\"id\":%u,\"loop\":%s,\"waypoints\":", n++ ? "," : "", s->d[p + 4], s->d[p + 5] ? "true" : "false");
        int cnt = ilist(s, lo);
        cv_buf_put(&s->o, "[", 1);
        for (int i = 0; i < cnt; i++) jp(&s->o, "%s%d", i ? "," : "", s->tmp[i]);
        jp(&s->o, "],\"pads\":[");
        for (int i = 0; i < cnt; i++) {
            if (i) jp(&s->o, ",");
            if (s->tmp[i] < s->nwp) jp(&s->o, "%d", s->wp_pad[s->tmp[i]]); else jp(&s->o, "null");
        }
        jp(&s->o, "]}");
    }
    jp(&s->o, "]");
}

static const struct { const char *kind; int size; } k_intro[] = {
    { "spawn", 12 }, { "item", 16 }, { "ammo", 16 }, { "swirl", 32 }, { "animation", 8 },
    { "cuff", 8 }, { "camera", 40 }, { "watch_time", 12 }, { "credits", 8 }, { "end", 4 } };

static void emit_intro(S *s, uint32_t off, long *credits_off)
{
    CvBuf *o = &s->o;
    int n = 0;
    *credits_off = -1;
    jp(o, ",\"intro\":[");
    for (size_t p = off; s->why[0] == 0;) {
        uint32_t t = r32(s, p);
        if (t > 9) { bad(s, "intro record type %u at %zu", t, p); break; }
        size_t sz = (size_t)k_intro[t].size;
        if (p + sz > s->n) { bad(s, "intro record at %zu runs past the file", p); break; }
        const uint8_t *r = s->d + p;
#define W(i) ((int32_t)ex_be32(r + 4 * (i)))
#define WU(i) ex_be32(r + 4 * (i))
        jp(o, "%s{\"type\":%u,\"kind\":\"%s\",\"offset\":%zu", n++ ? "," : "", t, k_intro[t].kind, p);
        switch (t) {
        case 0: jp(o, ",\"pad\":%d,\"slot\":%d", W(1), W(2)); break;
        case 1: jp(o, ",\"right\":%d,\"left\":%d,\"slot\":%d", W(1), W(2), W(3)); break;
        case 2: jp(o, ",\"ammo_type\":%d,\"amount\":%d,\"slot\":%d", W(1), W(2), W(3)); break;
        case 3:
            jp(o, ",\"flags\":%u", WU(1));
            jp(o, ",\"x\":"); pdb(o, W(2) / 65536.0);
            jp(o, ",\"y\":"); pdb(o, W(3) / 65536.0);
            jp(o, ",\"z\":"); pdb(o, W(4) / 65536.0);
            jp(o, ",\"scale\":"); pdb(o, W(5) / 65536.0);
            jp(o, ",\"duration\":"); pdb(o, W(6) / 65536.0);
            jp(o, ",\"pad\":%d", W(7));
            break;
        case 4: jp(o, ",\"anim\":%d", W(1)); break;
        case 5: jp(o, ",\"cuff\":%d", W(1)); break;
        case 6:
            jp(o, ",\"x\":"); pdb(o, W(1) / 100.0);
            jp(o, ",\"y\":"); pdb(o, W(2) / 100.0);
            jp(o, ",\"z\":"); pdb(o, W(3) / 100.0);
            jp(o, ",\"yaw\":"); pdb(o, W(4) / 65536.0);
            jp(o, ",\"pitch\":"); pdb(o, W(5) / 65536.0);
            jp(o, ",\"pad\":%d,\"text1\":%u,\"text2\":%d", W(6), WU(7), W(8));
            break;
        case 7: jp(o, ",\"hours\":%d,\"minutes\":%d", W(1), W(2)); break;
        case 8: jp(o, ",\"credits_offset\":%u", WU(1)); *credits_off = (long)WU(1); break;
        default: break;
        }
#undef W
#undef WU
        jp(o, ",\"raw\":"); phex(o, r, sz);
        jp(o, "}");
        p += sz;
        if (t == 9) break;
    }
    jp(o, "]");
}

static void emit_credits(S *s, long off)
{
    CvBuf *o = &s->o;
    if (off < 0) { jp(o, ",\"credits\":null"); return; }
    jp(o, ",\"credits\":[");
    int n = 0;
    for (size_t p = (size_t)off; s->why[0] == 0; p += 12) {
        if (p + 12 > s->n) { bad(s, "credits block runs past the file"); break; }
        const uint8_t *r = s->d + p;
        if (!ex_be16(r) && !ex_be16(r + 2)) break;
        jp(o, "%s{\"text1\":%u,\"text2\":%u,\"position1\":%d,\"alignment1\":%u,\"position2\":%d,\"alignment2\":%u}",
           n++ ? "," : "", ex_be16(r), ex_be16(r + 2), ex_be16s(r + 4), ex_be16(r + 6), ex_be16s(r + 8), ex_be16(r + 10));
    }
    jp(o, "]");
}

static void emit_names(S *s, const char *key, uint32_t off)
{
    jp(&s->o, ",\"%s\":", key);
    if (!off) { jp(&s->o, "null"); return; }
    jp(&s->o, "[");
    int n = 0;
    for (size_t p = off; s->why[0] == 0; p += 4) {
        uint32_t so = r32(s, p);
        if (!so) break;
        const char *nm = str_at(s, so);
        if (!nm) break;
        if (n++) jp(&s->o, ",");
        ex_jstr(&s->o, nm);
    }
    jp(&s->o, "]");
}

/* ------------------------------------------------------------------------ */
/* one setup file                                                           */
/* ------------------------------------------------------------------------ */

static int build_json(S *s, const char *level, int stage, const char *mode, const char *setup, const char *region,
                      const CvFileRow *row, double scale)
{
    CvBuf *o = &s->o;
    uint32_t h[10];
    unsigned char sha[20];
    char hex[41];
    float lscale = (float)scale, inv = 1.0f / lscale;
    if (s->n < 40) { bad(s, "file shorter than its header"); return -1; }
    for (int i = 0; i < 10; i++) h[i] = ex_be32(s->d + 4 * i);
    cv_sha1(s->d, (unsigned long)s->n, sha);
    for (int i = 0; i < 20; i++) snprintf(hex + 2 * i, 3, "%02x", sha[i]);

    /* ---- the propDef walk: sizes, types, refuse the unused types ---- */
    {
        size_t cap = 0, p = h[3];
        if (!h[3] || h[3] >= s->n) { bad(s, "propDef offset outside the file"); return -1; }
        for (;;) {
            if (p + 4 > s->n) { bad(s, "propDef stream runs past the file"); return -1; }
            int t = s->d[p + 3];
            if (t == TYPE_END) break;
            const TypeInfo *ti = type_info(t);
            if (!ti) { bad(s, "propDef type %d at %zu is not a known type (refusing the file)", t, p); return -1; }
            if (p + (size_t)ti->size > s->n) { bad(s, "propDef at %zu runs past the file", p); return -1; }
            if (ex_grow((void **)&s->poff, &cap, s->nprops + 1, sizeof *s->poff)) { bad(s, "out of memory"); return -1; }
            s->poff[s->nprops++] = (uint32_t)p;
            p += (size_t)ti->size;
        }
        s->ptype = (uint8_t *)malloc(s->nprops ? s->nprops : 1);
        if (!s->ptype) { bad(s, "out of memory"); return -1; }
        for (size_t i = 0; i < s->nprops; i++) s->ptype[i] = s->d[s->poff[i] + 3];
    }

    jp(o, "{\n  \"format\": \"ge007-setup\", \"version\": 1,\n  \"level\": "); ex_jstr(o, level);
    jp(o, ", \"stage\": %d, \"mode\": \"%s\",\n  \"setup\": ", stage, mode); ex_jstr(o, setup);
    jp(o, ", \"region\": "); ex_jstr(o, region);
    jp(o, ",\n  \"file\": { \"rom_offset\": %u, \"compressed_size\": %u, \"size\": %zu, \"sha1\": \"%s\" },\n", row->off,
       row->size, s->n, hex);
    jp(o, "  \"level_scale\": "); pfl(o, lscale);
    jp(o, ", \"scale\": "); pfl(o, inv);
    jp(o, ",\n  \"header\": [");
    for (int i = 0; i < 10; i++) jp(o, "%s%u", i ? ", " : "", h[i]);
    jp(o, "]");

    /* pads */
    jp(o, ",\n  \"pads\": [");
    {
        int n = 0;
        for (size_t p = h[6]; s->why[0] == 0; p += 44, n++) {
            if (r32(s, p + 36) == 0) break;
            jp(o, "%s\n    ", n ? "," : "");
            emit_pad(s, p, n, 0, inv);
        }
    }
    jp(o, "\n  ],\n  \"bound_pads\": [");
    {
        int n = 0;
        for (size_t p = h[7]; s->why[0] == 0; p += 68, n++) {
            if (r32(s, p + 36) == 0) break;
            jp(o, "%s\n    ", n ? "," : "");
            emit_pad(s, p, 10000 + n, 1, inv);
        }
    }
    jp(o, "\n  ]");
    emit_names(s, "waypoint_names", h[8]);
    emit_names(s, "group_names", h[9]);

    jp(o, ",\n  \"props\": [");
    for (size_t i = 0; i < s->nprops && s->why[0] == 0; i++) {
        jp(o, "%s\n    ", i ? "," : "");
        emit_prop(s, i);
    }
    jp(o, "\n  ]");
    emit_objectives(s);
    emit_briefing(s);
    emit_ailists(s, h[5]);
    emit_waypoints(s, h[0]);
    emit_groups(s, h[1]);
    emit_paths(s, h[4]);
    long credits = -1;
    emit_intro(s, h[2], &credits);
    emit_credits(s, credits);
    jp(o, "\n}\n");
    return s->why[0] ? -1 : 0;
}

static void note(CvBuf *n, const char *fmt, ...)
{
    char tmp[600];
    va_list ap;
    va_start(ap, fmt);
    int k = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if (k < 0) return;
    if ((size_t)k >= sizeof tmp) k = (int)sizeof tmp - 1;
    cv_buf_put(n, tmp, (size_t)k);
}

int ex_setup_export(const uint8_t *rom, size_t rom_size, const CvRegionData *rd, const char *region, const char *level,
                    double scale, const char *dir, CvBuf *rpt, char *msg, size_t msgn)
{
    const SetupDef *def = NULL;
    for (int i = 0; i < N_SETUPS; i++) if (!strcmp(k_setups[i].level, level)) def = &k_setups[i];
    if (!def) { note(rpt, "\nsetup: none (no setup entry for level %s)\n", level); return 0; }
    for (int mode = 0; mode < 2; mode++) {
        char fname[96], path[1200];
        snprintf(fname, sizeof fname, mode ? "Ump_setup%sZ" : "Usetup%sZ", def->mid);
        const char *json = mode ? "setup_mp.json" : "setup.json";
        const CvFileRow *row = cv_find_row(rd, fname);
        if (!row || row->size == 0) { note(rpt, "\n%s: none (the ROM has no %s)\n", json, fname); continue; }
        if (row->size < 8 || (size_t)row->off + row->size > rom_size) {
            snprintf(msg, msgn, "%s is outside the ROM", fname);
            return -1;
        }
        S s;
        CvBuf raw;
        memset(&s, 0, sizeof s);
        memset(&raw, 0, sizeof raw);
        if (cv_rz_inflate(rom + row->off, row->size, &raw) != 0) {
            snprintf(msg, msgn, "%s does not decompress", fname);
            cv_buf_free(&raw);
            return -1;
        }
        s.d = raw.p; s.n = raw.len;
        s.tmp = (int *)malloc(TMPCAP * sizeof(int));
        s.wp_pad = (int *)malloc(TMPCAP * sizeof(int));
        int rc = -1;
        if (!s.tmp || !s.wp_pad) bad(&s, "out of memory");
        else rc = build_json(&s, level, def->stage, mode ? "multi" : "solo", fname, region, row, scale);
        if (rc == 0) {
            snprintf(path, sizeof path, "%s/%s", dir, json);
            if (ex_write_file(path, s.o.p, s.o.len)) { snprintf(s.why, sizeof s.why, "cannot write %s", path); rc = -1; }
            else note(rpt, "\n%s: %s, %zu bytes, %zu propDefs\n", json, fname, s.n, s.nprops);
        }
        if (rc) snprintf(msg, msgn, "%s: %s", fname, s.why[0] ? s.why : "failed");
        cv_buf_free(&raw); cv_buf_free(&s.o);
        free(s.poff); free(s.ptype); free(s.tmp); free(s.wp_pad);
        if (rc) return -1;
    }
    return 0;
}
