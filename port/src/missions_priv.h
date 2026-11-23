#ifndef PORT_MISSIONS_PRIV_H
#define PORT_MISSIONS_PRIV_H
/* Shared by port/src/missions*.c only. */
#include "mission.h"

void missionsEnsureArgs(void);
int  missionsActivateFile(const char *path, int spawn_pad, const char *mod_dir, int slot);
const char *missionsModDir(void);
const Mission *missionsCurrent(void);
const char *missionsDir(void);
int  missionsSpawnPad(void);
void modsThumbsLoad(const char *modDir, const char (*folders)[MISSION_ID_LEN], int count);   /* missions_thumb.c */
const Campaign *modsCampaign(int i);   /* missions_mods.c: installed mod i */
const char *modsDirOf(int i);
void missionsHostReset(void);          /* missions_host.c: drop the running scripts */

#endif
