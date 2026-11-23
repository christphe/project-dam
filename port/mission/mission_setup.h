#ifndef PORT_MISSION_SETUP_H
#define PORT_MISSION_SETUP_H

/* A solo level's stage setup, read for the mission editor (issue #101): the pads, bound pads, AI list ids and
 * every guard / object record with all its fields, straight from the PC-layout setup file the game itself loads
 * (the "Usetup<name>Z" row of the pccg sidecar, data/pccg-<region>/pccg.bin + manifest.csv, written by
 * port/convert from the user's ROM). The values are therefore exactly the file form mission_apply edits.
 * Plain host C (needs zlib): no game headers, no game state. */

#include <stddef.h>
#include <stdint.h>
#include "mission_fields.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SetupPad {
    int num;                    /* pad number: index, or 10000 + index for a bound pad */
    int bound;
    float pos[3], up[3], look[3];
    float bbox[6];              /* bound pads: xmin ymin zmin xmax ymax zmax */
} SetupPad;

typedef struct SetupProp {
    int index;                  /* position in the propDefs stream (level.json / mission.json "prop") */
    int type;                   /* PROPDEF_* */
    int pad, model, item, ai;   /* -1 when the record has none */
    int chr;                    /* a guard's chrnum (else -1) */
    int carrier;                /* a weapon assigned to a guard (flags & 0x4000): the carrier's chrnum, which is what its pad field holds; else -1 */
    MissionFieldVals f;         /* missionRecFields(type); empty for a record that is not a guard / object */
} SetupProp;

typedef struct SetupData {
    SetupPad *pads; int npads;
    SetupProp *props; int nprops;
    int *ailists; int nailists;
} SetupData;

/* The inflated setup file of row `name` (e.g. "UsetupsevbunkerZ") from <dataDir>/pccg-<region>/. malloc'd;
 * 0 and a one-line err when the sidecar is missing (the game was never launched with the ROM). */
int  missionSetupLoadFile(const char *dataDir, const char *name, uint8_t **out, uint32_t *outn, char *err, size_t errlen);
/* Decodes a setup file. `scale` is the level's room_data_float2: the game multiplies the stored pad
 * positions (and bound boxes) by it when it loads a level. */
int  missionSetupDecode(const uint8_t *raw, uint32_t n, float scale, SetupData *out, char *err, size_t errlen);
void missionSetupFree(SetupData *d);
/* Inflates an RZ block ("0x11 0x72" + raw deflate, the format of the game's compressed files).
 * Returns a malloc'd buffer (caller frees) and its size in *outn, NULL if it is not RZ or broken.
 * The one copy used by the editor and by the game's mission loader (port/src/missions.c). */
uint8_t *missionRzInflate(const uint8_t *rz, uint32_t n, uint32_t *outn);

#ifdef __cplusplus
}
#endif

#endif
