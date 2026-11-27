/*
 * setup_export.h -- the stage setup of a level -> setup.json / setup_mp.json (docs/specs/setup.md).
 * Internal to port/export/ (used by level_export.c). CLEAN ROOM like the rest of the directory.
 */
#ifndef PORT_EXPORT_SETUP_H
#define PORT_EXPORT_SETUP_H

#include <stddef.h>
#include <stdint.h>
#include "convert.h"

/* Writes <dir>/setup.json (solo) and <dir>/setup_mp.json (multiplayer) for the level `level` when the ROM has the
 * files; `scale` is the level's level_scale (level-decor.md). One line per file written, or per file that does not
 * exist, is appended to `note` (it goes into report.txt). 0 = ok, -1 = failure (msg says why). */
int ex_setup_export(const uint8_t *rom, size_t rom_size, const CvRegionData *rd, const char *region, const char *level,
                    double scale, const char *dir, CvBuf *note, char *msg, size_t msgn);

#endif
