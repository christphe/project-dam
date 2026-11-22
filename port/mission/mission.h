#ifndef PORT_MISSION_H
#define PORT_MISSION_H

/* Custom co-op missions (issues #100-#105): the shared mission format.
 *
 * Compiled into the game (port/src/missions*.c applies it) and into the
 * editor (editor/ edits it). No ROM code and no ROM data: a mission is a
 * delta over one solo level's original setup, and every reference into the
 * original level is a number (setup prop index, pad number, AI list id,
 * item id, model id). Only this file and the mission's own Luau scripts are
 * ever shared between people.
 *
 * A mod IS a campaign: an ordered list of missions. The player picks the mod
 * on the game's main screen (port row, D960 pattern; "GoldenEye" = no mod);
 * after that the game's OWN menus are filled with the mod's content: the
 * mission select shows the mod's missions (names, thumb.png images, unlock
 * state) in place of the original ones, then the normal difficulty and
 * briefing screens (the mission's briefing text and objectives). No extra
 * screens. There is no mission outside a campaign. On disk, next to data/
 * (sdmc:/switch/project-dam/mods/ on the Switch):
 *   mods/<mod>/campaign.json          the campaign (format below)
 *   mods/<mod>/scripts/*.luau         the mod's own scripts, usable by all its missions
 *   mods/<mod>/<mission>/mission.json one mission
 *   mods/<mod>/<mission>/thumb.png    its image in the mission grid, 4:3
 *                                     (the editor's Capture button, 320x240)
 * Progress (unlocked missions, difficulties completed) is the player's, not
 * the mod's: <save dir>/mods/<mod>.json, never inside the mod folder.
 *
 * campaign.json, format 1:
 * {
 *   "format": 1,
 *   "name": "Bunker stories",
 *   "author": "display name",
 *   "description": "text for the mod list",
 *   "affects": ["story"],         what the mod replaces: "story" (the solo/co-op mission select)
 *                                 and/or "multiplayer"; shown on the mod chooser
 *   "unlock": "ordered",          "ordered" (as the game: mission n+1 opens when n is completed) | "open"
 *   "missions": [ "bunker_remix", "dam_night" ]   mission folder names, in campaign order
 * }
 *
 * mission.json, format 1:
 * {
 *   "format": 1,
 *   "name": "Bunker 1 remix",                    display name
 *   "level": "bunker1",                          solo level name, as port/src/main.c kSoloLevels (lowercase)
 *   "description": "text for the list",
 *   "briefing": "text for the briefing page",     optional
 *   "pads": [                                     new pads; referenced by their string id
 *     { "id": "p1", "pos": [x, y, z], "look": [x, y, z], "room": 12 }
 *   ],
 *   "zones": [ { "id": "z1", "pos": [x, y, z], "radius": 150, "room": 12 } ],
 *   "guards": [
 *     { "op": "move",   "prop": 17, "pad": 0x2715 },
 *     { "op": "add",    "pad": "p1", "model": 3, "ai": 0x401, "weapon": 17 },
 *     { "op": "remove", "prop": 18 }
 *   ],
 *   "objects": [
 *     { "op": "add",    "type": 4, "pad": 0x2733, "model": 120, "item": 33 },
 *     { "op": "move",   "prop": 40, "pad": "p1" },
 *     { "op": "remove", "prop": 41 }
 *   ],
 *   "objectives": [ { "id": "o1", "text": "Find the key", "difficulty": 0 } ],
 *   "environment": {                              optional; every key optional, absent = the level's own
 *     "mood": "dusk",                             "day" | "dusk" | "night" (sun glow, stars, moon)
 *     "sun": { "azimuth": 120, "elevation": 12, "color": [1, 0.8, 0.6], "glow": 0.6 },   degrees
 *     "sky": [0.55, 0.62, 0.7], "clouds": [0.9, 0.9, 0.95], "cloud_speed": 1.0,
 *     "sea": { "enabled": true, "height": -120, "color": [0.1, 0.3, 0.35],
 *              "swell": 1.0, "chop": 0.5, "speed": 1.0 }
 *   },                                            colours 0..1; applied port-side (skyplus.c and the
 *                                                 level's env record copy); fog distance is not here
 *   "scripts": [
 *     { "use": "key_door", "target": { "kind": "prop", "prop": 52 },
 *       "props": { "key": 33, "message": "Locked" } }
 *   ]
 * }
 *
 * - "pad": an integer is an original setup pad number; a string is the id of
 *   one of this mission's "pads".
 * - "prop": the index of a prop in the level's original setup propDefs list,
 *   in file order (as level.json from --export-level lists them).
 * - "type": the setup prop type number (PROPDEF_*), "model" a model id,
 *   "item" an ITEM_IDS value, "ai" an AI list id.
 * - "difficulty": lowest difficulty the objective applies to (0 Agent,
 *   1 Secret Agent, 2 00 Agent).
 * - script "target.kind": "mission" (no other field), "prop" (+ "prop"),
 *   "pad" (+ "pad"), "zone" (+ "zone": id). "props" values are numbers,
 *   strings or booleans; their meaning comes from the script's typed
 *   property declarations.
 * Unknown keys are kept on load and written back on save ("extra"), so a
 * newer editor's fields survive an older one.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MISSION_FORMAT 1
#define MISSION_ID_LEN 32
#define MISSION_TEXT_LEN 512

typedef struct MissionPadRef {
    int is_new;                     /* 0: original pad number in `num`; 1: new pad id in `id` */
    int num;
    char id[MISSION_ID_LEN];
} MissionPadRef;

typedef struct MissionPad {
    char id[MISSION_ID_LEN];
    float pos[3], look[3];
    int room;
} MissionPad;

typedef struct MissionZone {
    char id[MISSION_ID_LEN];
    float pos[3];
    float radius;
    int room;
} MissionZone;

typedef enum MissionOpKind { MISSION_OP_MOVE, MISSION_OP_ADD, MISSION_OP_REMOVE } MissionOpKind;

typedef struct MissionGuardOp {
    MissionOpKind op;
    int prop;                       /* MOVE/REMOVE: original setup prop index */
    MissionPadRef pad;              /* MOVE/ADD */
    int model, ai, weapon;          /* ADD (-1 = level default) */
} MissionGuardOp;

typedef struct MissionObjectOp {
    MissionOpKind op;
    int prop;                       /* MOVE/REMOVE */
    MissionPadRef pad;              /* MOVE/ADD */
    int type, model, item;          /* ADD (-1 = none) */
} MissionObjectOp;

typedef struct MissionObjective {
    char id[MISSION_ID_LEN];
    char text[MISSION_TEXT_LEN];
    int difficulty;
} MissionObjective;

typedef enum MissionValueKind { MISSION_VAL_NUMBER, MISSION_VAL_STRING, MISSION_VAL_BOOL } MissionValueKind;

typedef struct MissionProp {        /* one script property value */
    char name[MISSION_ID_LEN];
    MissionValueKind kind;
    double num;                     /* NUMBER, BOOL (0/1) */
    char str[MISSION_TEXT_LEN];     /* STRING */
} MissionProp;

typedef enum MissionTargetKind { MISSION_TGT_MISSION, MISSION_TGT_PROP, MISSION_TGT_PAD, MISSION_TGT_ZONE } MissionTargetKind;

typedef struct MissionScript {
    char use[MISSION_ID_LEN];       /* library script name, without .luau */
    MissionTargetKind kind;
    int prop;                       /* PROP */
    MissionPadRef pad;              /* PAD */
    char zone[MISSION_ID_LEN];      /* ZONE */
    MissionProp *props; int nprops;
} MissionScript;

typedef enum MissionMood { MISSION_MOOD_KEEP = -1, MISSION_MOOD_DAY, MISSION_MOOD_DUSK, MISSION_MOOD_NIGHT } MissionMood;

/* environment: each has_* flag says the key was present (0 = keep the level's). */
typedef struct MissionEnv {
    MissionMood mood;
    int has_sun;    float sun_azimuth, sun_elevation, sun_color[3], sun_glow;
    int has_sky;    float sky[3];
    int has_clouds; float clouds[3];
    int has_cloud_speed; float cloud_speed;
    int has_sea;    int sea_enabled;      /* "sea" present: sea_enabled from "enabled" (default 1) */
    int has_sea_height; float sea_height;
    int has_sea_color;  float sea_color[3];
    int has_swell;  float swell;
    int has_chop;   float chop;
    int has_speed;  float speed;
} MissionEnv;

typedef struct Mission {
    int format;
    MissionEnv env;
    char name[MISSION_TEXT_LEN];
    char level[MISSION_ID_LEN];
    char description[MISSION_TEXT_LEN];
    char briefing[2048];
    MissionPad *pads;             int npads;
    MissionZone *zones;           int nzones;
    MissionGuardOp *guards;       int nguards;
    MissionObjectOp *objects;     int nobjects;
    MissionObjective *objectives; int nobjectives;
    MissionScript *scripts;       int nscripts;
    void *extra;                    /* cJSON* of unknown top-level keys (owned) */
} Mission;

/* All return 1 on success; on failure 0 and a one-line message in err. */
void missionInit(Mission *m);
void missionFree(Mission *m);
int  missionParse(const char *json, Mission *out, char *err, size_t errlen);
int  missionLoadFile(const char *path, Mission *out, char *err, size_t errlen);
/* Returns a malloc'd, pretty-printed JSON string (caller frees). */
char *missionToJson(const Mission *m);
int  missionSaveFile(const char *path, const Mission *m, char *err, size_t errlen);
/* Structural checks that need no level data: format, level name known,
 * pad string ids resolve, zone ids resolve, ids unique, ops complete. */
int  missionValidate(const Mission *m, char *err, size_t errlen);

/* Solo level name -> the -level_XX stage number (e.g. "bunker1" -> 9), -1 if unknown. */
int  missionLevelNumber(const char *name);

/* ---- campaign.json (a mod) ---- */
#define CAMPAIGN_FORMAT 1

#define CAMPAIGN_AFFECTS_STORY       1
#define CAMPAIGN_AFFECTS_MULTIPLAYER 2

typedef enum CampaignUnlock { CAMPAIGN_UNLOCK_ORDERED, CAMPAIGN_UNLOCK_OPEN } CampaignUnlock;

typedef struct Campaign {
    int format;
    char name[MISSION_TEXT_LEN];
    char author[MISSION_TEXT_LEN];
    char description[MISSION_TEXT_LEN];
    CampaignUnlock unlock;
    int affects;                        /* CAMPAIGN_AFFECTS_* bits; default story */
    char (*missions)[MISSION_ID_LEN];  /* mission folder names, in order */
    int nmissions;
    void *extra;                        /* cJSON* of unknown top-level keys (owned) */
} Campaign;

void  campaignInit(Campaign *c);
void  campaignFree(Campaign *c);
int   campaignParse(const char *json, Campaign *out, char *err, size_t errlen);
int   campaignLoadFile(const char *path, Campaign *out, char *err, size_t errlen);
char *campaignToJson(const Campaign *c);
int   campaignSaveFile(const char *path, const Campaign *c, char *err, size_t errlen);
/* name set, at least one mission, folder names non-empty, unique, no '/', '\\' or "..". */
int   campaignValidate(const Campaign *c, char *err, size_t errlen);

#ifdef __cplusplus
}
#endif

#endif
