#ifndef PORT_TEXPACK_H
#define PORT_TEXPACK_H

/*
 * texpack.h -- the game side of texture packs (port/mission/texpacks.h has the format): Video.TexturePack names one
 * <game>/texturepacks/X.ptexpack, and its textures/<n>.png replace texture n wherever the game draws it.
 *
 * The N64 display lists carry addresses, not texture numbers: texLoad (src/game/image.c, #ifdef PORT) tells this file
 * which texture number it just unpacked at which address, and fast3d's import_texture asks for the address it is about
 * to upload. Game thread: texpackRegister, texpackSelect. Render thread: texpackSalt, texpackPixels.
 */
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void texpackInit(void);                    /* after configLoad: opens the pack Video.TexturePack names */
void texpackSelect(const char *file);      /* "" none; also stored in Video.TexturePack */
const char *texpackSelected(void);         /* the file name in use ("" none) */
/* The active mission's mod (its folder or X.pmod; "" none): its textures/<n>.png come before the pack's. Stopgap until the
 * game draws its levels from glTF (port/src/missions.c calls it on activation and deactivation). */
void texpackSetMod(const char *dir);
/* The packs in texturepacks/ (sorted file names, display names), for the menu. Returns how many (up to max). */
int texpackListPacks(char (*files)[128], char (*names)[64], int max);

void texpackRegister(const void *data, int texnum);   /* texLoad: texture `texnum` unpacked at `data` */

/* 0 when the pack has no picture for the texture at `addr`, else a value for fast3d's cache key (changes with the pack). */
unsigned texpackSalt(const void *addr, int *texnum);
/* The pack's picture of `texnum` as RGBA8 for a tile of RDP format `fmt` (G_IM_FMT_*): an opaque picture replacing an
 * intensity texture (I, IA) gets its luminance as alpha, as the RDP reads those. NULL when it cannot be read or is larger
 * than maxSize on a side. Valid until the next call. */
const uint8_t *texpackPixels(int texnum, int fmt, int maxSize, int *w, int *h);

#ifdef __cplusplus
}
#endif
#endif
