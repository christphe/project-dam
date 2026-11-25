#ifndef _FILLTRI_H_
#define _FILLTRI_H_

/* G_FILLTRI_EXT (port-only, fast3d gfx_pc.cpp): a filled triangle in screen coordinates, drawn like a fill rectangle
 * (current combiner / prim colour, render mode, HUD scale and aspect) but as one real triangle at the output
 * resolution, so a slanted edge is smooth instead of stepped in whole N64 rows (D397 snaps fill rects to rows).
 * Coordinates are 10.2 fixed point (px * 4), signed 16 bits. Two Gfx words. Used by the dialogue marker's arrow. */

/* No includes: fast3d (C++) only needs the opcode; the macro is for game-side C that already has <ultra64.h>. */
#define G_FILLTRI_EXT 0x4D

#define gDPFillTriangleEXT(pkt, x0q, y0q, x1q, y1q, x2q, y2q)                                           \
    do {                                                                                                  \
        Gfx *_g = (Gfx *)(pkt);                                                                           \
        _g[0].words.w0 = _SHIFTL(G_FILLTRI_EXT, 24, 8);                                                   \
        _g[0].words.w1 = _SHIFTL((u16)(s16)(x0q), 16, 16) | _SHIFTL((u16)(s16)(y0q), 0, 16);            \
        _g[1].words.w0 = _SHIFTL((u16)(s16)(x1q), 16, 16) | _SHIFTL((u16)(s16)(y1q), 0, 16);            \
        _g[1].words.w1 = _SHIFTL((u16)(s16)(x2q), 16, 16) | _SHIFTL((u16)(s16)(y2q), 0, 16);            \
    } while (0)

#endif
