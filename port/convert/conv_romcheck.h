/* conv_romcheck.h -- retail ROM identification by SHA-1 (issue #65).
 * Plain types only and no includes: the game side declares the same
 * prototypes by hand (its TUs see the decomp include path). */
#ifndef CONV_ROMCHECK_H
#define CONV_ROMCHECK_H

#define CV_ROM_OK              0
#define CV_ROM_BYTESWAPPED_V64 1   /* 37 80 40 12 */
#define CV_ROM_BYTESWAPPED_N64 2   /* 40 12 37 80 */
#define CV_ROM_UNKNOWN         3   /* not an unmodified retail image */

/* SHA-1 (FIPS 180-1) of data[0..len). */
void cv_sha1(const unsigned char *data, unsigned long len, unsigned char out[20]);

/* Identify a big-endian (.z64) retail image. On CV_ROM_OK *region_out is
 * "ntsc-final", "pal-final" or "jpn-final" (a static string). */
int cv_romcheck(const unsigned char *rom, unsigned long len, const char **region_out);

/* One short English line for a non-OK code. */
const char *cv_romcheck_message(int code);

#endif
