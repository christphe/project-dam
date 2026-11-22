#ifndef RELIGHT_H
#define RELIGHT_H
/* project-dam: PROTOTYPE (GE_RELIGHT=1), port/src/relight.c. GE's painted
 * light removed from the level's outdoor vertex colours (sky ambient + the
 * GE+ sun by orientation, no shadow). */
#include <ultra64.h>

/* Relights the current level once (no-op for the level already done). sunDir: toward the sun, world space. */
void portRelightLevel(const float sunDir[3]);
/* Writes the relit colours into a room GE has just loaded (bg.c bgLoadRoomModelData). */
void portRelightApplyRoom(s32 room);
/* 1 once the current level is relit: the sun is then in the vertex colours, not in the shader. */
int portRelightActive(void);
#endif
