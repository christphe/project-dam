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
/* The layout of front.c's (file-local) struct BriefingData: the level's briefing text ids and its objectives' text id and
 * lowest difficulty (OBJECTIVES_MAX = 10 records). */
struct BriefingDataSomething { unsigned short textid, enabled_difficulty; };
struct BriefingData { unsigned short brief[4]; struct BriefingDataSomething objective[10]; };
extern struct BriefingData *ptrbriefingdata;   /* front.c */
int  missionsKeepLevelObjectives(void);   /* missions_ui.c: the mission keeps the level's briefing, objectives and exit */
void missionsHostReset(void);          /* missions_host.c: drop the running scripts */

/* #165, the campaign API's domains (port/mission/mission_lua_ext.h): missions_host_<domain>.c fill these in. */
struct MissionHostGuard; struct MissionHostWorld; struct MissionHostCinema;
const struct MissionHostGuard  *missionsHostGuard(void);
const struct MissionHostWorld  *missionsHostWorld(void);
const struct MissionHostCinema *missionsHostCinema(void);
/* missions_host.c helpers the domains share (setup props of the running level, run with a player current). */
int   missionsHostPropCount(void);
void *missionsHostPropRec(int prop);      /* PropDefHeaderRecord * of setup prop `prop`, NULL: none */
void  missionsHostWithPlayer(int player, void (*fn)(void *), void *arg);
/* Per-tick hooks of the domains, called from the runtime tick (after the scripts ran); NULL-safe to leave empty. */
void  missionsHostGuardTick(void);
void  missionsHostWorldTick(void);
void  missionsHostCinemaTick(void);
/* #165 second pass, shared between the domains */
struct coord3d; struct StandTile;
int   missionsHostCinemaCameraTile(const struct coord3d *pos, const struct coord3d *pos2, struct StandTile **stan,
                                   struct coord3d *walk);   /* missions_host_cinema.c: the director camera's room tile */
int   missionsHostSceneActive(void);       /* missions_host_cinema.c: a cinema.begin scene is running (guards are blind) */
int   missionsHostGuardDown(int prop);     /* missions_host.c: a revivable setup guard lies down (no order reaches it) */
void  missionsHostGuardReset(void);       /* a level starts or the runtime stops: each domain drops its state */
void  missionsHostWorldReset(void);
void  missionsHostCinemaReset(void);

#endif
