#ifndef PORT_MISSION_THUMB_H
#define PORT_MISSION_THUMB_H

/* The pixel side of a mission's thumb.png (issue #103): how the game turns the 4:3 colour image of the editor's
 * Capture into the two pictures the player sees, so that the editor can show exactly the same thing.
 *
 *   mission select film strip: a 68x44 I8 texture (grey, the centred 68:44 crop of the thumb, rows stored bottom-up)
 *   briefing polaroid:         a 128x128 white frame holding the centred square of the thumb, 16 greys (I4), stored
 *                              as two 128x64 halves (4 bits per pixel, 64 bytes a row, rows bottom-up per half)
 *
 * Pure C, no game headers: port/src/missions_thumb.c (the game, fast3d's texture hook) and the editor both call it,
 * tests/test_mission_thumb.c checks it. The PNG decoding stays with each caller. */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MTHUMB_W 320            /* the grey master every picture below is cut from (a thumb.png is 320x240) */
#define MTHUMB_H 240
#define MTHUMB_STRIP_W 68
#define MTHUMB_STRIP_H 44
#define MTHUMB_POL_SIZE 128     /* the polaroid, frame included */
#define MTHUMB_POL_HALF_BYTES (64 * 64)   /* one 128x64 I4 half */

/* Grey master: `pix` is w*h pixels of `ch` interleaved 8-bit channels (1 or 2: grey (+alpha); 3 or 4: RGB (+alpha),
 * luma 77/150/29), area-averaged to dw x dh (out). */
void mthumbGrey(const uint8_t *pix, int w, int h, int ch, int dw, int dh, uint8_t *out);

/* Area average of the window (x0, y0, ww, hh) of the MTHUMB_W x MTHUMB_H grey master into dw x dh, row stride `ostride`. */
void mthumbWindow(const uint8_t *grey, int x0, int y0, int ww, int hh, int dw, int dh, uint8_t *out, int ostride);

/* The film strip texture: 68x44 I8, written with row stride `line` (68..72), rows bottom-up as the game stores them. */
void mthumbStrip(const uint8_t *grey, uint8_t *out, int line);

/* The polaroid as one 128x128 picture of 4-bit greys (0..15, top-down): white frame, the centred square inside. */
void mthumbPolaroidImage(const uint8_t *grey, uint8_t *out128);
/* Half 0 or 1 of that picture as the game's 128x64 I4 texture (high nibble first, rows bottom-up within the half). */
void mthumbPolaroidHalf(const uint8_t *image128, int half, uint8_t *out4096);
/* The inverse of the two halves: back to the 128x128 top-down 4-bit greys (what the player sees). */
void mthumbPolaroidDecode(const uint8_t *half0, const uint8_t *half1, uint8_t *out128);

/* Contrast (1 = as is) and brightness (-100..100 grey levels) of an RGBA image, in place:
 * v' = clamp((v - 128) * contrast + 128 + brightness). Alpha is left alone. */
void mthumbAdjustRgba(uint8_t *rgba, int npix, float contrast, float brightness);

#ifdef __cplusplus
}
#endif

#endif
