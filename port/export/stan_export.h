/*
 * stan_export.h -- the collision floor ("stan") part of the static level export (docs/specs/stan.md).
 * Internal to port/export/ (used by level_export.c). CLEAN ROOM like the rest of the directory.
 */
#ifndef PORT_EXPORT_STAN_H
#define PORT_EXPORT_STAN_H

#include <stddef.h>
#include <stdint.h>
#include "convert.h"

typedef struct {
    /* what to append to the level's glTF; every index below is already offset by the bases given to ex_stan_build */
    CvBuf bin;          /* binary data to append to the .bin (starts 4-aligned: the caller aligns before appending) */
    CvBuf views;        /* bufferView objects, comma separated, no leading comma */
    CvBuf accs;         /* accessor objects, same */
    CvBuf meshes;       /* mesh objects, same */
    CvBuf nodes;        /* node objects, each with a LEADING comma (they follow the room nodes) */
    CvBuf asset_extras; /* text to put inside asset.extras, with a leading comma */
    int nviews, naccs, nmeshes;
    int root_node;      /* node index of "collision" (= first_node) */
    /* numbers for the log / report */
    size_t tiles, rooms, tris, walls, linked, unresolved, trailing, skipped_rooms;
    int table_mismatch;
    char file[64];
    char note[300];     /* why there is no collision, or a warning */
} ExStan;

/* Builds the collision data of one level. decor_file is the "bg_<name>_all_p" basename. bin_base is the .bin size the
 * caller will append `bin` at (a multiple of 4); view_base / acc_base / mesh_base the counts so far; first_node the
 * number of nodes so far; material the glTF material index of the "collision" material.
 * 0 = ok (out filled), 1 = no collision for this file (out->note says why; nothing to append), -1 = out of memory.
 * Free with ex_stan_free in every case. */
int ex_stan_build(const uint8_t *rom, size_t rom_size, const CvRegionData *rd, const char *decor_file, double scale,
                  size_t bin_base, int view_base, int acc_base, int mesh_base, int first_node, int material, ExStan *out);
void ex_stan_free(ExStan *s);

/* the "collision" material object (JSON, no commas) */
const char *ex_stan_material_json(void);

#endif
