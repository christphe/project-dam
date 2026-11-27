/* Buffers, region layouts and ROM invariant checks. */
#include <stdlib.h>
#include <string.h>

#include "ge_audio_internal.h"

/* ---- messages ------------------------------------------------------------ */
void ge_seterr(char *err, const char *fmt, ...)
{
    va_list ap;
    if (!err)
        return;
    va_start(ap, fmt);
    vsnprintf(err, GE_AE_ERRLEN, fmt, ap);
    va_end(ap);
}

/* ---- buffer -------------------------------------------------------------- */
void ge_ae_buf_free(ge_ae_buf *b)
{
    free(b->data);
    b->data = NULL;
    b->size = b->cap = 0;
}

int ge_buf_reserve(ge_ae_buf *b, size_t extra)
{
    size_t need, cap;
    uint8_t *p;
    if (extra > (size_t)0x7FFFFFFF - b->size)
        return -1;
    need = b->size + extra;
    if (need <= b->cap)
        return 0;
    cap = b->cap ? b->cap : 4096;
    while (cap < need)
        cap *= 2;
    p = (uint8_t *)realloc(b->data, cap);
    if (!p)
        return -1;
    b->data = p;
    b->cap = cap;
    return 0;
}

int ge_buf_put(ge_ae_buf *b, const void *src, size_t n)
{
    if (ge_buf_reserve(b, n))
        return -1;
    if (n)
        memcpy(b->data + b->size, src, n);
    b->size += n;
    return 0;
}

int ge_buf_u8(ge_ae_buf *b, uint32_t v)
{
    uint8_t c = (uint8_t)v;
    return ge_buf_put(b, &c, 1);
}

int ge_buf_be16(ge_ae_buf *b, uint32_t v)
{
    uint8_t c[2] = {(uint8_t)(v >> 8), (uint8_t)v};
    return ge_buf_put(b, c, 2);
}

int ge_buf_be32(ge_ae_buf *b, uint32_t v)
{
    uint8_t c[4] = {(uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v};
    return ge_buf_put(b, c, 4);
}

int ge_buf_le16(ge_ae_buf *b, uint32_t v)
{
    uint8_t c[2] = {(uint8_t)v, (uint8_t)(v >> 8)};
    return ge_buf_put(b, c, 2);
}

int ge_buf_le32(ge_ae_buf *b, uint32_t v)
{
    uint8_t c[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
    return ge_buf_put(b, c, 4);
}

int ge_buf_fill(ge_ae_buf *b, uint8_t v, size_t n)
{
    if (ge_buf_reserve(b, n))
        return -1;
    memset(b->data + b->size, v, n);
    b->size += n;
    return 0;
}

void ge_buf_patch_le32(ge_ae_buf *b, size_t off, uint32_t v)
{
    if (off + 4 > b->size)
        return;
    b->data[off] = (uint8_t)v;
    b->data[off + 1] = (uint8_t)(v >> 8);
    b->data[off + 2] = (uint8_t)(v >> 16);
    b->data[off + 3] = (uint8_t)(v >> 24);
}

/* ---- region layouts (docs/specs/audio.md, "Where things are") ------------- */
static const ge_ae_layout k_layouts[3] = {
    {"ntsc-final", 0x2EBDE0, 0x5BC0, 0x2F19A0, 0xC2AB0, 0x3B4450, 0x43A0, 0x3B87F0, 0x60FA0, 0x419790},
    {"pal-final", 0x2E2220, 0x5BC0, 0x2E7DE0, 0xC2AB0, 0x3AA890, 0x43A0, 0x3AEC30, 0x60FA0, 0x40FBD0},
    {"jpn-final", 0x2ECB00, 0x5BC0, 0x2F26C0, 0xC2AB0, 0x3B5170, 0x43A0, 0x3B9510, 0x60FA0, 0x41A4B0},
};

int ge_ae_region_from_name(const char *name, ge_ae_region *out)
{
    int i;
    for (i = 0; i < 3; i++) {
        if (name && strcmp(name, k_layouts[i].name) == 0) {
            *out = (ge_ae_region)i;
            return 0;
        }
    }
    return -1;
}

const ge_ae_layout *ge_ae_layout_for(ge_ae_region r)
{
    return &k_layouts[(int)r];
}

/* ---- invariants ------------------------------------------------------------- */
static int range_ok(size_t rom, uint64_t off, uint64_t len)
{
    return off <= rom && len <= rom - off;
}

static int check_ctl(const uint8_t *rom, size_t size, const char *what, uint32_t ctl, uint32_t ctl_size,
                     uint32_t tbl, uint32_t tbl_size, char *err)
{
    static const uint8_t magic[4] = {0x42, 0x31, 0x00, 0x01};
    if (!range_ok(size, ctl, 4) || memcmp(rom + ctl, magic, 4) != 0) {
        ge_seterr(err, "%s ctl at 0x%X does not start with 42 31 00 01", what, (unsigned)ctl);
        return -1;
    }
    if ((uint64_t)ctl + ctl_size != tbl) {
        ge_seterr(err, "%s tbl (0x%X) does not directly follow its ctl (0x%X + 0x%X)", what, (unsigned)tbl,
                  (unsigned)ctl, (unsigned)ctl_size);
        return -1;
    }
    if (!range_ok(size, ctl, (uint64_t)ctl_size) || !range_ok(size, tbl, tbl_size)) {
        ge_seterr(err, "%s ctl/tbl lies beyond the end of the ROM (%zu bytes)", what, size);
        return -1;
    }
    return 0;
}

int ge_ae_check_rom(const uint8_t *rom, size_t size, const ge_ae_layout *lay, int *track_count, char *err)
{
    ge_view v;
    int bad = 0, i, n, first = -1;

    if (size < 0x1000 || !rom) {
        ge_seterr(err, "ROM too small (%zu bytes)", size);
        return -1;
    }
    if (!(rom[0] == 0x80 && rom[1] == 0x37 && rom[2] == 0x12 && rom[3] == 0x40)) {
        if (rom[0] == 0x37 && rom[1] == 0x80)
            ge_seterr(err, "ROM is byte-swapped (.v64); convert to big-endian .z64 first");
        else if (rom[0] == 0x40 && rom[1] == 0x12)
            ge_seterr(err, "ROM is little-endian (.n64); convert to big-endian .z64 first");
        else
            ge_seterr(err, "not an N64 ROM in .z64 (big-endian) order: first bytes %02X %02X %02X %02X", rom[0], rom[1],
                      rom[2], rom[3]);
        return -1;
    }
    if (check_ctl(rom, size, "SFX bank", lay->sfx_ctl, lay->sfx_ctl_size, lay->sfx_tbl, lay->sfx_tbl_size, err))
        return -1;
    if (check_ctl(rom, size, "instrument bank", lay->ins_ctl, lay->ins_ctl_size, lay->ins_tbl, lay->ins_tbl_size, err))
        return -1;

    if (!range_ok(size, lay->music_table, 4)) {
        ge_seterr(err, "music table at 0x%X is outside the ROM", (unsigned)lay->music_table);
        return -1;
    }
    v.p = rom + lay->music_table;
    v.n = size - lay->music_table;
    n = (int)ge_v_u16(v, 0, &bad);
    if (n < 40 || n > 80 || ge_v_u16(v, 2, &bad) != 0 || bad) {
        ge_seterr(err, "music table at 0x%X: bad header (count %d, zero word %u)", (unsigned)lay->music_table, n,
                  (unsigned)ge_v_u16(v, 2, &bad));
        return -1;
    }
    for (i = 0; i < n; i++) {
        uint32_t off = ge_v_u32(v, 4 + 8 * (size_t)i, &bad);
        uint32_t cs = ge_v_u16(v, 4 + 8 * (size_t)i + 6, &bad);
        if (bad) {
            ge_seterr(err, "music table entry %d is outside the ROM", i);
            return -1;
        }
        if (cs == 0)
            continue;
        if (first < 0) {
            first = i;
            if (off != 4 + 8 * (uint32_t)n) {
                ge_seterr(err, "music table: first track with data (%d) starts at 0x%X, expected 0x%X (4 + 8 * %d)", i,
                          (unsigned)off, 4 + 8 * (unsigned)n, n);
                return -1;
            }
        }
        if (!range_ok(v.n, off, cs)) {
            ge_seterr(err, "music table entry %d: data 0x%X + 0x%X is outside the ROM", i, (unsigned)off, (unsigned)cs);
            return -1;
        }
    }
    if (first < 0) {
        ge_seterr(err, "music table has no track with data");
        return -1;
    }
    if (track_count)
        *track_count = n;
    return 0;
}
