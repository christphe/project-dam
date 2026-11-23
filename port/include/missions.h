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
const char *modsRowText(void);                    /* "GoldenEye" or the chosen mod's name */
void        modsSelect(int idx);                  /* choose installed mod idx, -1 = the original game */
int         modsCurrent(void);                    /* the chosen mod's index, -1 = the original game */

/* ---- the Mods screen (missions_screen.c): A on the MODS row opens it; front.c ticks it, menu_game.cpp draws it ---- */
void        modsScreenOpen(void);
int         modsScreenActive(void);
int         modsScreenTick(void);                 /* once per frame while active (player 1's pad): 1 = it was just closed */
void        modsScreenPointer(int hover, int click);   /* the mouse over a card (0..n) / button (MODS_HIT_*); click: pressed */
#define MODS_HIT_BACK 1000
#define MODS_HIT_PLAY 1001
/* What it shows. Entry 0 is GoldenEye, entry i the installed mod i - 1. */
int         modsScreenCount(void);
int         modsScreenSel(void);                  /* the highlighted entry */
int         modsScreenFocus(void);                /* 0 the list, 1 the buttons */
int         modsScreenButton(void);               /* the focused button: 0 Back, 1 Play this mod */
int         modsScreenChosen(void);               /* the entry in use now */
const char *modsScreenName(int e);
const char *modsScreenAuthor(int e);
const char *modsScreenDescription(int e);
const char *modsScreenProgress(int e);            /* "1 / 3 missions", "completed", "" */
int         modsScreenAffects(int e);             /* CAMPAIGN_AFFECTS_* bits */
int         modsScreenMissions(int e);
const char *modsScreenMissionName(int e, int m);
int         modsScreenMissionState(int e, int m); /* 0 locked, 1 available, 2 completed */
const char *modsScreenMissionDone(int e, int m);  /* the difficulties completed: "Agent, 00 Agent" */
int         modsScreenThumbId(int e, int m);      /* m = -1: the entry's card picture; -1 = none */
int         modsScreenThumbCount(void);
int         modsScreenThumbRev(void);             /* changes when the set of pictures does */
const unsigned char *modsScreenThumbPixels(int id, int *w, int *h);   /* RGBA, w x h */
int         modsStoryActive(void);                /* the chosen mod replaces the solo / co-op mission select */
int         modsSlotGate(int slot);               /* mission select slot: -1 hidden (no such mission), 2 open campaign, -2 ask the game's save */
void        modsUnbind(void);                     /* give the folder entry its original mission_num back (mission select) */
const char *modsSlotName(int slot);
int         modsSelectSlot(int slot, int *stage); /* activates the slot's mission; 1 = ok, *stage = its level */
int         modsSlotCount(void);                  /* the chosen mod's missions */
int         modsActiveSlot(void);                 /* the active mission's slot, -1 none */


/* ---- briefing and report (front.c) ------------------------------------ */
/* A custom mission is chosen AND it is the level the front end is on (so the next campaign level never shows its text). */
int         missionsObjectivesActive(void);
/* The two header lines of the briefing / difficulty / report screens in place of "Mission 2: Severnaya" and
 * "Part ii: Bunker": "Mission <n>: <mod>" and "<mission>". 1 = filled (newline-terminated), 0 = not a mission. */
int         missionsHeaderLines(char *line1, char *line2, int size);
const char *modsModName(void);
/* Briefing page text for a custom mission: page 1 = name + briefing, others blank; NULL = not a mission. */
const char *missionsBriefingPageText(int page);
/* The objectives list, in place of print_objectives_and_status_to_menu's. */
Gfx        *missionsObjectivesDraw(Gfx *gdl, int ytop, int withStatus);
/* The in-game pause menu (options.c, watch briefing pages) while a mission runs. */
int         missionsPauseActive(void);
int         missionsPauseObjCount(void);
int         missionsPauseObjDifficulty(int i);
const char *missionsPauseObjText(int i);          /* wrapped, newline-terminated, like the game's strings */
int         missionsPauseObjStatus(int i);        /* OBJECTIVESTATUS_*: 1 complete, 0 incomplete */
const char *missionsPausePageText(int page);      /* NULL: use the level's own */
/* frontCompleteAllObjectivesAliveSuccess for a custom mission (1 = every objective of this difficulty done). */
int         missionsObjectivesSuccess(void);

/* ---- pccg: the setup row is rebuilt after the sidecar table is patched ---- */
void        missionsOnPatchTable(void);
void        missionsBeforeSetupLoad(int stage);    /* prop.c: rebuild the mission setup for the chosen difficulty */

#ifdef __cplusplus
}
#endif

#endif
