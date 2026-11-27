/* Private helpers shared by the ge_audio_export sources. */
#ifndef GE_AUDIO_INTERNAL_H
#define GE_AUDIO_INTERNAL_H

#include <stdarg.h>
#include <stdio.h>

#include "ge_audio_export.h"

/* ---- bounds-checked big-endian reads ------------------------------------
 * Every accessor takes a view and sets *bad (never clears it) when the read
 * would leave the view, returning 0 in that case. */
typedef struct {
    const uint8_t *p;
    size_t n;
} ge_view;

static inline uint32_t ge_v_u8(ge_view v, size_t off, int *bad)
{
    if (off >= v.n) {
        *bad = 1;
        return 0;
    }
    return v.p[off];
}

static inline uint32_t ge_v_u16(ge_view v, size_t off, int *bad)
{
    if (off > v.n || v.n - off < 2) {
        *bad = 1;
        return 0;
    }
    return ((uint32_t)v.p[off] << 8) | v.p[off + 1];
}

static inline uint32_t ge_v_u32(ge_view v, size_t off, int *bad)
{
    if (off > v.n || v.n - off < 4) {
        *bad = 1;
        return 0;
    }
    return ((uint32_t)v.p[off] << 24) | ((uint32_t)v.p[off + 1] << 16) | ((uint32_t)v.p[off + 2] << 8) | v.p[off + 3];
}

static inline int32_t ge_v_s32(ge_view v, size_t off, int *bad)
{
    return (int32_t)ge_v_u32(v, off, bad);
}

static inline int16_t ge_v_s16(ge_view v, size_t off, int *bad)
{
    return (int16_t)ge_v_u16(v, off, bad);
}

/* ---- buffer writers ------------------------------------------------------ */
int ge_buf_reserve(ge_ae_buf *b, size_t extra);
int ge_buf_put(ge_ae_buf *b, const void *src, size_t n);
int ge_buf_u8(ge_ae_buf *b, uint32_t v);
int ge_buf_be16(ge_ae_buf *b, uint32_t v);
int ge_buf_be32(ge_ae_buf *b, uint32_t v);
int ge_buf_le16(ge_ae_buf *b, uint32_t v);
int ge_buf_le32(ge_ae_buf *b, uint32_t v);
int ge_buf_fill(ge_ae_buf *b, uint8_t v, size_t n);
void ge_buf_patch_le32(ge_ae_buf *b, size_t off, uint32_t v);

/* printf into an error buffer (GE_AE_ERRLEN) */
void ge_seterr(char *err, const char *fmt, ...);

#endif
