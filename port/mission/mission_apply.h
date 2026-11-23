#ifndef PORT_MISSION_APPLY_H
#define PORT_MISSION_APPLY_H

/* Applying a mission to a solo level's stage setup (issue #101).
 *
 * Pure: it works on the DECOMPRESSED PC-layout setup file (Usetup<name>Z as
 * port/convert/conv_d88.c writes it: little-endian, 64-bit pointer slots, the
 * propDefs stream with the PD_PCB record sizes) and returns a rebuilt copy.
 * No game headers, no ROM data of its own: it only moves, clones and
 * appends what the original file already holds.
 *
 * The file is offset-relative (every internal reference is a byte offset from
 * the start), so the rebuild APPENDS the grown tables and re-points the
 * header at them; nothing else in the file moves:
 *
 *   [ original file, untouched except the edits below ]
 *   [ pads table: the original pads, then the mission's new pads            ]
 *   [ propDefs stream: the original records edited in place in the copy,    ]
 *   [   then the added guards / objects, then the END record                ]
 *
 *  - move guard / object: the pad number (u16 / s16 at +6 of the record).
 *  - add guard: a clone of the level's first guard with a new chrnum, pad,
 *    body (model), AI list; a weapon adds a collectable assigned to that chr.
 *  - add object: a clone of the level's first record of that type (a
 *    collectable not assigned to a guard) with pad, model and item.
 *  - remove guard: moved to a pad outside every stan tile, where the game
 *    refuses to spawn it. remove object: its flags2 "do not load" bits.
 *  Prop numbers keep their meaning: records are appended, never inserted.
 *
 * `spawn_pad` >= 0 re-points the first INTROTYPE_SPAWN record at that pad; <= -2 means the
 * mission's own pad number (-2 - spawn_pad) in m->pads (its index in the file is only known here);
 * -1 keeps the level's start.
 * Pad positions in the mission are world units; the file holds them divided
 * by `room_scale` (the game's room_data_float2 = 1 / levelscale, ~1.85 in Bunker 1: the loader multiplies stored pads by it). */

#include <stddef.h>
#include <stdint.h>
#include "mission.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MissionApplyResult {
    uint8_t *data;                  /* malloc'd rebuilt file (missionApplyFree) */
    uint32_t size;
    int guards_moved, guards_added, guards_removed;
    int objects_moved, objects_added, objects_removed;
    int pads_added;
    int skipped;                    /* ops that could not be applied (see log) */
    char log[4096];                 /* one line per op, '\n' separated */
} MissionApplyResult;

/* 1 on success (even when some ops were skipped, see .skipped and .log), 0 on a
 * file that is not a setup this code understands (err says why). */
int  missionApplySetup(const Mission *m, const uint8_t *in, uint32_t insize, int spawn_pad,
                       float room_scale, MissionApplyResult *out, char *err, size_t errlen);
/* The same for a chosen difficulty (0 Agent .. 3 007; -1 = unknown, every prop is present). The game loads every guard at
 * every difficulty, so a guard op with "difficulties" that leaves `difficulty` out is sunk like a "remove" (its carried
 * weapons not loaded) and an added guard of that kind is not added; objects get the game's per-difficulty do-not-load
 * flags2 bits whatever `difficulty` is. The setup depends on `difficulty`: rebuild it when the player picks another. */
int  missionApplySetupEx(const Mission *m, const uint8_t *in, uint32_t insize, int spawn_pad, float room_scale, int difficulty,
                         MissionApplyResult *out, char *err, size_t errlen);
void missionApplyFree(MissionApplyResult *r);

/* The stage setup file name for a solo level number ("UsetupsevbunkerZ"), from the
 * game's own table; implemented game-side. Declared here for the callers. */

/* Weapon item id -> the guard weapon model (obj) the setups use for it; -1 unknown. */
int  missionGuardWeaponModel(int item);

/* Layout helpers (also used by the tests). */
#define MISSION_SETUP_HDR_SIZE 0x50
#define MISSION_SETUP_PAD_SIZE 56
#define MISSION_PROPDEF_END    48

/* PC byte size of a propDefs record of this type, 0 for an unknown type. */
unsigned missionPropdefSize(int type);

#ifdef __cplusplus
}
#endif

#endif
