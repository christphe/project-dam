#ifndef PORT_MISSIONS_H
#define PORT_MISSIONS_H

/* Custom co-op missions in the game (issues #101, #103, #104). The mission format
 * and the setup rebuild are port/mission/ (ge_mission); this is the game side:
 * command line, the mission list ("More..." in the mission select), the briefing
 * text, the objective status and the Luau runtime's window onto the game.
 * port/src/missions.c, missions_menu.c, missions_host.c.
 *
 * Command line:  --mission FILE   mission.json to play (its level must be the -level_XX boot level)
 *                --spawn PAD      start at that setup pad (best effort)
 * Menu:          missions/<name>/mission.json next to data/ (sdmc:/switch/project-dam/missions) */

#include <ultra64.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- the active mission ---------------------------------------------- */
int         missionsActive(void);                 /* a mission is chosen (menu or command line) */
const char *missionsName(void);
const char *missionsBriefing(void);
void        missionsDeactivate(void);             /* a normal level was chosen: back to the campaign */
int         missionsStage(void);                  /* the level's stage number, -1 if none */

/* ---- frame (needs one call per game frame, see port/mission/hooks) ---- */
void        missionsFrameTick(void);
/* ---- mods (front.c): the chooser on the mode select, then the game's own mission select ---- */
int         modsCount(void);                      /* installed mods (mods/<mod>/campaign.json) */
void        modsCycle(void);                      /* GoldenEye -> mod 1 -> ... -> GoldenEye */
const char *modsRowText(void);                    /* "GoldenEye" or "<name> (Story, Multiplayer)" */
int         modsStoryActive(void);                /* the chosen mod replaces the solo / co-op mission select */
int         modsSlotDifficulty(int slot);         /* get_highest_unlocked_difficulty_for_level for a mod slot; -1 = hidden */
const char *modsSlotName(int slot);
int         modsSelectSlot(int slot, int *stage); /* activates the slot's mission; 1 = ok, *stage = its level */
int         modsActiveSlot(void);                 /* the active mission's slot, -1 none */


/* ---- briefing and report (front.c) ------------------------------------ */
/* A custom mission is chosen AND it is the level the front end is on (so the next campaign level never shows its text). */
int         missionsObjectivesActive(void);
/* Briefing page text for a custom mission: page 1 = name + briefing, others blank; NULL = not a mission. */
const char *missionsBriefingPageText(int page);
/* The objectives list, in place of print_objectives_and_status_to_menu's. */
Gfx        *missionsObjectivesDraw(Gfx *gdl, int ytop, int withStatus);
/* frontCompleteAllObjectivesAliveSuccess for a custom mission (1 = every objective of this difficulty done). */
int         missionsObjectivesSuccess(void);

/* ---- pccg: the setup row is rebuilt after the sidecar table is patched ---- */
void        missionsOnPatchTable(void);

#ifdef __cplusplus
}
#endif

#endif
