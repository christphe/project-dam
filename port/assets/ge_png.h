/*
 * ge_png.h -- the one PNG reader and writer of the game and its tools (ge_assets, issue #172).
 *
 * Reader: 8-bit grey / grey+alpha / RGB / RGBA / palette (with tRNS), not interlaced, up to 8192 on a side; always RGBA8, top
 * row first. Writer: 8-bit RGBA, or RGB with GE_PNG_RGB. Plain C, libc + zlib, no GL, no game headers. Thread-safe (no state).
 */
#ifndef GE_PNG_H
#define GE_PNG_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GE_PNG_MAX_SIDE 8192

enum {
    GE_PNG_RGB  = 1,   /* encode: drop the alpha channel (RGB8) */
    GE_PNG_BEST = 2    /* encode: the smallest-sum row filter per row and zlib level 9 (screenshots, thumbnails); else filter 0, level 6 */
};

/* PNG file bytes -> RGBA8. 1 on success; *rgba is malloc'd (free() it), w*h*4 bytes. 0 otherwise, with *rgba NULL. */
int gePngDecode(const uint8_t *file, size_t len, int *w, int *h, uint8_t **rgba);
/* The same from a file path (a plain file; for one inside a package read it with pkgReadFile and use gePngDecode). */
int gePngRead(const char *path, int *w, int *h, uint8_t **rgba);

/* RGBA8 (w*h*4, top row first) -> PNG bytes: *out malloc'd (free() it). 1 on success, 0 on bad arguments or out of memory. */
int gePngEncode(int w, int h, const uint8_t *rgba, unsigned flags, uint8_t **out, size_t *outLen);
/* The same into a file. 1 on success. */
int gePngWrite(const char *path, int w, int h, const uint8_t *rgba, unsigned flags);

#ifdef __cplusplus
}
#endif
#endif
