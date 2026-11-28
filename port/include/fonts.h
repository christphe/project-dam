#ifndef PORT_FONTS_H
#define PORT_FONTS_H

/*
 * fonts.h -- the game's fonts from the asset packs (docs/specs/fonts.md, docs/specs/asset-packs.md): fonts.json and
 * fonts/<key>.png of the active mod or the enabled packs (texpack.h assetRead). The engine still draws from the data
 * layouts it always had, so this file rebuilds those from the pack: the Latin fonts as the block the loader expects
 * (kerning, glyph records, pixels), the Japanese fonts one pair of glyphs at a time.
 *
 * GE_ROM_FONTS=1 reads the ROM instead (the reader is then never asked). With no font in any pack or mod, nothing is drawn. GE_FONTS_CHECK=1 compares the pack with the ROM at
 * the first font load and logs the counts (dev only).
 */

#ifdef __cplusplus
extern "C" {
#endif

/* ---- the pure part: JSON + 8-bit grey image -> engine data (tests/test_fonts.c) ---- */

/* Latin font `key` of the fonts.json text -> the block laid out as the ROM has it (big-endian: 169 kerning values,
 * 94 glyph records of 24 bytes, pixels with rows padded to 8 columns from 0xB80). Returns its size, 0 when the font is
 * missing, malformed, or the block does not fit in `cap`. */
unsigned fontsBuildLatin(const char *json, unsigned long jlen, const char *key, const unsigned char *grey, int w, int h,
                         unsigned char *dst, unsigned cap);
/* Pair `pair` (glyphs 2k and 2k+1) of the cell font `key`: 12 rows (narrow) or 16 rows (wide) of 8 bytes, 4-bit pixels,
 * the even glyph in bits 0-1 and the odd in bits 2-3. `*bytes` gets the size (0x60 or 0x80). 1 on success. */
int fontsBuildJpnPair(const char *json, unsigned long jlen, const char *key, const unsigned char *grey, int w, int h,
                      unsigned pair, unsigned char *dst, unsigned *bytes);
/* The block of a Latin font with no ink (zero-size glyphs), `size` bytes, the line height `line` kept; returns `size`, 0 when
 * `size` is too small. What the engine gets when no pack or mod has the font. */
unsigned fontsBuildEmptyLatin(unsigned char *dst, unsigned size, int line);
/* Compares two Latin blocks (the ROM's and the rebuilt one, both `n` bytes): glyph records (all but the id), kerning and
 * every pixel byte. *compared counts the 94 glyphs plus the 169 kerning values, *diffs the ones that differ. */
void fontsCompareLatin(const unsigned char *a, const unsigned char *b, unsigned n, unsigned *compared, unsigned *diffs);

/* ---- the game side ---- */

int fontsRomOnly(void);       /* GE_ROM_FONTS=1: read the fonts from the ROM */
int fontsCheckEnabled(void);  /* GE_FONTS_CHECK=1 */
/* Latin font 0 (small) or 1 (large) from the stack into dst. Size of the block, 0 when the stack has no usable font (the
 * caller then uses an empty font, or the ROM with GE_ROM_FONTS=1). Also drops the cached Japanese atlases, so a change of packs or mod is seen at the next load. */
unsigned fontsLatinBlock(int which, unsigned char *dst, unsigned cap);
/* One Japanese pair block (narrow or wide) from the stack: 1 on success, with *bytes the size; 0 when it is not there. */
int fontsJpnPair(int wide, unsigned pair, unsigned char *dst, unsigned *bytes);

#ifdef __cplusplus
}
#endif
#endif
