#ifndef PORT_MISSION_LUA_H
#define PORT_MISSION_LUA_H

/* The Luau runtime of a custom mission (issue #104).
 *
 * One VM per mission. Each entry of mission.json "scripts" runs as a coroutine
 * of its own, in a sandbox (no io / os / debug / require, read-only globals, a
 * memory cap, and an interrupt that aborts a script that runs over its time
 * budget in one resume). The runtime knows nothing of the game: everything it
 * reads or does goes through the MissionHost callbacks, which port/src/missions.c
 * fills in with the game's own functions. That keeps this file in the clean
 * ge_mission library and lets tests/test_mission_lua.c drive it with a fake host.
 *
 * Script API (globals):
 *   give(item [, player])   take(item [, player])   has(item [, player]) -> bool
 *       item: an ITEM_IDS number; player: 0-based, omitted = every player (has: any player)
 *   message(text)                                   on-screen text
 *   wait(seconds)                                   sleep
 *   wait(event(name [, filter]))                    sleep until an event matches; returns the event table
 *   event(name [, filter]) -> descriptor            filter: table of field = value that must all match
 *   objectives.add(id, text [, difficulty])   objectives.complete(id)
 *   objectives.is_complete(id) -> bool        objectives.all_complete() -> bool   (of the current difficulty)
 *   alarm.trigger()
 *   mission.win()                                   end the level as a success
 *   door(prop) / guard(prop) -> target              a target for a setup prop number
 *   target (global of the script)                   the script's own target (kind "prop", "pad", "zone", "mission")
 *   props (global of the script)                    the typed values from mission.json
 *   target methods: :lock() :unlock() :open() :close() :is_open() :is_dead()
 *                   :set_ai(list) :near(radius) -> bool :distance() -> number (nearest player)
 *                   .prop .kind .zone (fields)
 *   print(...)                                      to the host log
 * Events (name, fields): "start", "end" {win}, "pickup" {item, player}, "death" {prop},
 *   "door" {prop, state = "open" | "closed" | "moving"}, "zone" {id, player},
 *   "objective" {id}.
 * Events are found by diffing the host's state every tick; "objective" and "end"
 * are raised by the runtime itself. */

#include <stddef.h>
#include <stdint.h>
#include "mission.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { MH_DOOR_LOCK, MH_DOOR_UNLOCK, MH_DOOR_OPEN, MH_DOOR_CLOSE };
enum { MH_DOOR_NONE = -1, MH_DOOR_CLOSED = 0, MH_DOOR_OPENED = 1, MH_DOOR_MOVING = 2 };

typedef struct MissionHost {
    void *ud;
    void (*log)(void *ud, const char *line);
    void (*message)(void *ud, const char *text);
    void (*give)(void *ud, int player, int item);      /* player -1: every player */
    void (*take)(void *ud, int player, int item);
    int  (*has)(void *ud, int player, int item);
    int  (*player_count)(void *ud);
    int  (*player_pos)(void *ud, int player, float xyz[3]);   /* 1 if the player is alive and placed */
    int  (*prop_count)(void *ud);                      /* setup propDefs records */
    int  (*prop_type)(void *ud, int prop);             /* PROPDEF_* of record `prop` */
    int  (*prop_pos)(void *ud, int prop, float xyz[3]);
    int  (*door_state)(void *ud, int prop);            /* MH_DOOR_*; MH_DOOR_NONE if not a door */
    void (*door_cmd)(void *ud, int prop, int cmd);
    int  (*guard_dead)(void *ud, int prop);            /* 1 once the guard's chr is gone / dead */
    void (*guard_set_ai)(void *ud, int prop, int ailist);
    void (*alarm)(void *ud);
    void (*win)(void *ud);
    int  (*difficulty)(void *ud);                      /* 0 Agent .. 3 007; NULL = every objective counts */
} MissionHost;

typedef struct MissionRuntime MissionRuntime;

/* Script lookup for "use": <mission_dir>/<use>.luau or <mission_dir>/scripts/<use>.luau, then <mod_dir>/scripts/<use>.luau, then the
 * built-in library. Either dir may be NULL. Returns NULL (and err) if the VM
 * could not be made; a script that fails to load or run is logged and skipped. */
MissionRuntime *missionRtCreate(const Mission *m, const MissionHost *host, const char *mission_dir, const char *mod_dir,
                                char *err, size_t errlen);
void missionRtDestroy(MissionRuntime *rt);

/* One game frame: `dt` seconds since the last call. Diffs the host state into events,
 * resumes the scripts that are due. */
void missionRtTick(MissionRuntime *rt, double dt);

/* The level ended (win 1 / 0): raises the "end" event once. */
void missionRtEnd(MissionRuntime *rt, int win);

/* Objectives: the mission's list (+ the ones scripts add). `difficulty` filters like the game's. */
int  missionRtObjectiveCount(const MissionRuntime *rt);
const char *missionRtObjectiveText(const MissionRuntime *rt, int i);
int  missionRtObjectiveDifficulty(const MissionRuntime *rt, int i);
int  missionRtObjectiveComplete(const MissionRuntime *rt, int i);
int  missionRtAllComplete(const MissionRuntime *rt, int difficulty);
/* Marks an objective complete from the host side (e.g. the debug console). */
int  missionRtCompleteObjective(MissionRuntime *rt, const char *id);

/* Counters for tests / logs. */
int  missionRtScriptCount(const MissionRuntime *rt);       /* scripts started */
int  missionRtScriptsAlive(const MissionRuntime *rt);      /* still waiting */

/* The built-in library (generated from port/mission/lib/*.luau): NULL name ends the list. */
typedef struct MissionLibScript { const char *name; const char *source; } MissionLibScript;
extern const MissionLibScript g_missionLibScripts[];

#ifdef __cplusplus
}
#endif

#endif
