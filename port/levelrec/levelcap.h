#ifndef PORT_LEVELCAP_H
#define PORT_LEVELCAP_H

/* Level export (issue #102): the capture side of fast3d (port/levelrec/gfx_levelcap.inc,
 * compiled into port/fast3d/gfx_pc.cpp) and the driver (port/src/levelexport.c).
 *
 * Threads: levelcap* run on the decoder thread (the one that runs gfx_run); the
 * driver is called from there too, at the first room display list of a frame
 * (levelExportOnDl). No game thread ever calls into this. */

#ifdef __cplusplus
extern "C" {
#endif

/* ---- fast3d side (gfx_levelcap.inc) -------------------------------------- */

void levelcapArm(int on);       /* watch the G_DL calls and ask levelExportOnDl */
int  levelcapIsOn(void);        /* a driven display list is being captured */

/* Start: snapshot the decoder state, drop the texture cache so every texture is
 * uploaded (and recorded) again, clear the tables. End: restore the state. */
void levelcapBegin(void);
void levelcapEnd(void);

/* Run a display list with capture on: every triangle is recorded (before the
 * reject / clip, camera independent) for `owner`; nothing is drawn. The decoder
 * state is restored to the one of levelcapBegin first, and after. */
void levelcapRunDl(void *dl, int owner);

int  levelcapAddOwner(int kind, int id, const char *name);

/* Totals, for the report line. */
void levelcapCounts(int *ntex, int *nowners, int *nruns, unsigned long long *ntris);
/* World-space bounds of everything recorded for an owner; 0 if it has no triangle. */
int  levelcapOwnerBounds(int owner, float mn[3], float mx[3]);

/* Writes level.rec (LevelRecHeader ... see levelrec.h). 0 on success. */
int  levelcapWriteRec(const char *path, const char *build, const char *level);

/* ---- driver side (levelexport.c), called by fast3d ----------------------- */

/* A G_DL is about to run `dl`. The driver returns nonzero once it ran the
 * capture (it never recurses: levelcapArm(0) is done first). */
int  levelExportOnDl(const void *dl);
/* 1 when --export-level DIR was given; fast3d asks once, at its first frame, and then arms. */
int  levelExportWanted(void);

#ifdef __cplusplus
}
#endif

#endif
