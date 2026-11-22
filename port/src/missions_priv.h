#ifndef PORT_MISSIONS_PRIV_H
#define PORT_MISSIONS_PRIV_H
/* Shared by port/src/missions*.c only. */
#include "mission.h"

void missionsEnsureArgs(void);
int  missionsActivateFile(const char *path, int spawn_pad, const char *mod_dir, int slot);
const char *missionsModDir(void);
void missionsRecordComplete(int difficulty);
const Mission *missionsCurrent(void);
const char *missionsDir(void);
int  missionsSpawnPad(void);
void missionsHostReset(void);          /* missions_host.c: drop the running scripts */

#endif
