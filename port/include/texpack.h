#ifndef PORT_TEXPACK_H
#define PORT_TEXPACK_H

/*
 * texpack.h -- the game side of asset packs (port/assets/ge_assetstack.h, docs/specs/asset-packs.md): Video.AssetPacks lists the
 * enabled packs in order (<game>/assetpacks/X.passets, or the older <game>/texturepacks/X.ptexpack), and the first one with a
 * textures/<n>.png replaces texture n wherever the game draws it. The active mod's picture always wins.
 *
 * The N64 display lists carry addresses, not texture numbers: texLoad (src/game/image.c, #ifdef PORT) tells this file
 * which texture number it just unpacked at which address, and fast3d's import_texture asks for the address it is about
 * to upload. Game thread: texpackRegister, texpackSetPacks. Render thread: texpackSalt, texpackPixels.
 */
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void texpackInit(void);                    /* after configLoad: opens the packs Video.AssetPacks lists (an older
                                              Video.TexturePack migrates to it) */
/* The enabled packs, file names in order separated by ';' ("" none); also stored in Video.AssetPacks. */
void texpackSetPacks(const char *list);
const char *texpackPacks(void);            /* the list in use */
/* The active mission's mod (its folder or X.pmod; "" none): its textures/<n>.png come before the pack's. Stopgap until the
 * game draws its levels from glTF (port/src/missions.c calls it on activation and deactivation). */
void texpackSetMod(const char *dir);
/* The packs in assetpacks/ and texturepacks/ (sorted file names, display names), for the menu. Returns how many (up to max). */
int texpackListPacks(char (*files)[128], char (*names)[64], int max);

void texpackRegister(const void *data, int texnum, int bytes);   /* texLoad: texture `texnum` unpacked at `data`, `bytes` long */

/* 0 when the pack has no picture for the texture holding `addr`, else a value for fast3d's cache key (changes with the pack
 * and the offset). A load by blocks (fromTile 0) must start at the texture; a load by tiles may start inside it (a strip of
 * a wider image): `offset` is how far in, in bytes. */
unsigned texpackSalt(const void *addr, int fromTile, int *texnum, uint32_t *offset);
/* The pack's picture of `texnum` as RGBA8 for a tile of RDP format `fmt` (G_IM_FMT_*): an opaque picture replacing an
 * intensity texture (I, IA) gets its luminance as alpha, as the RDP reads those. NULL when it cannot be read or is larger
 * than maxSize on a side. Valid until the next call. */
const uint8_t *texpackPixels(int texnum, int fmt, int maxSize, int *w, int *h);

#ifdef __cplusplus
}
#endif
#endif
