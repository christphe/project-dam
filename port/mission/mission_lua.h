#ifndef PORT_MISSION_LUA_H
#define PORT_MISSION_LUA_H

/* The Luau runtime of a custom mission (issue #104).
 *
 * One VM per mission. Each entry of mission.json "scripts" runs as a coroutine
 * of its own, in a sandbox (no io / os / debug, a require() that only loads mission modules, read-only globals, a
 * memory cap, and an interrupt that aborts a script that runs over its time
 * budget in one resume). The runtime knows nothing of the game: everything it
 * reads or does goes through the MissionHost callbacks, which port/src/missions.c
 * fills in with the game's own functions. That keeps this file in the clean
 * ge_mission library and lets tests/test_mission_lua.c drive it with a fake host.
 *
 * Script API (globals):
 *   give(item [, player])   take(item [, player])   has(item [, player]) -> bool
 *       item: an ITEM_IDS number; player: 0-based, omitted = every player (has: any player)
 *   message(text)                                   on-screen text (bottom of the screen)
 *   say(who, text)                                  a dialogue line at the top of the screen, shown "who: text" to every player
 *   wait(seconds)                                   sleep
 *   wait(event(name [, filter]))                    sleep until an event matches; returns the event table
 *   clock() -> number                               the mission time in seconds (the runtime's own clock: it stops while the game is paused)
 *   spawn(fn) -> Task                               run fn as a new coroutine of its own (it may wait()); first run at the next
 *                                                   sleeper pass of this tick. Task:cancel(): it never resumes (a finished task: no-op)
 *   on(name, fn [, filter]) -> Connection           fn(event) runs as a NEW coroutine (like spawn: it may wait()) every time a
 *                                                   matching event is raised, on the runtime's tick. An error in fn is logged, not fatal.
 *                                                   Connection:off() unsubscribes (running callbacks finish). wait(event(...)) is unchanged.
 *   require(name) -> value                          the module <name>.luau (mission dir, mission dir/scripts, mod dir/scripts, built-in
 *                                                   library; name = letters, digits, _ and -, so require("jokes_" .. level) works). Run
 *                                                   ONCE per mission in its own environment over the mission globals (not the calling
 *                                                   script target / props), its return value cached (true if it returns nothing).
 *                                                   The module top level must not wait(); its functions called later may.
 *   event(name [, filter]) -> descriptor            filter: table of field = value that must all match
 *   objectives.add(id, text [, difficulty])   objectives.complete(id)
 *   objectives.is_complete(id) -> bool        objectives.all_complete() -> bool   (of the current difficulty)
 *   objectives.reset(id)                      back to incomplete (neither complete nor failed): "keep X alive" can be complete while
 *                                             X stands and incomplete while X is down
 *   objectives.fail(id)                       shown as failed (pause, end screen, and the game's own "Objective a:
 *                                             failed" line); it can no longer be completed
 *   objectives.is_failed(id) -> bool
 *   alarm.trigger()
 *   mission.win()                                   end the level as a success
 *   mission.fail()                                  end the level as a failure (no progress save)
 *   mission.difficulty() -> 0..3                    the difficulty being played: 0 Agent, 1 Secret Agent, 2 00 Agent, 3 007
 *                                                   (-1 when the host has no difficulty)
 *   door(prop) / guard(prop) -> target              a target for a setup prop number
 *   chr(n) -> target                                the guard whose chr number is n (the number the setup and the game's
 *                                                   own scripts use); a target with no prop when there is none
 *   tag(n) -> target                                the object an object tag names (doors, terminals, screens ...)
 *   text(id) -> string | nil                        the game's text `id` in the language played: id a number or 4 hex digits
 *                                                   ("2C04"); the campaign references text by id and never ships it (#165)
 *   player(n) -> target                             player n (0-based) as a target (kind "player"): :say(), :distance(),
 *                                                   :near(), :distance_to() (the prop methods do nothing on it)
 *   target (global of the script)                   the script's own target (kind "prop", "pad", "zone", "mission")
 *   props (global of the script)                    the typed values from mission.json
 *   target methods: :lock() :unlock([force]) :open() :close() :is_open() :is_dead()
 *                   unlock(): gives back the door's own key flags; unlock(true) also removes its native lock (a decoder or key
 *                   door) for the rest of the level, so it stays open to everyone after it closes.
 *                   :set_ai(list) :near(radius) -> bool :distance() -> number (nearest player)
 *                   :distance_to(other) -> number   distance between two targets (prop / guard / zone / a mission pad by id)
 *                                                   1e30 when either has no position (a guard not spawned, an original pad number)
 *                   :near([radius] [, height]) -> bool  a player within radius; with height, radius is measured flat and the
 *                                                   player is at most height above or below (another floor does not count)
 *                   :follow([player]) :stop()       a guard follows the nearest living player (or only `player`, 0-based): when
 *                                                   that player is farther than 200 units, the host orders the guard to the player:
 *                                                   walk, run beyond 350, sprint beyond 1200. The order is renewed (checked every
 *                                                   0.5 s) only when it is out of date (see followTick). :follow() also gives the guard
 *                                                   the passive AI list GAILIST_DEAD_AI (1) so its own AI does not flee, attack or
 *                                                   path over ours. :stop() only stops issuing moves (the AI list is kept).
 *                   :surrender() :stand()           hands up (an armed guard drops its weapons); stand still facing the
 *                                                   nearest living player (hands down). A later :follow() order moves it again.
 *                   :go_to(target, [speed]) -> bool the guard walks ("walk"), runs (default) or sprints to a prop's pad (in front
 *                                                   of it for a wall pad); false: no route or refused. Pause :follow() first
 *                                                   (:stop()), or its next order takes the guard away.
 *                   :is_moving() -> bool            still on its way
 *                   :face(target) -> bool           face a prop square on; within 120 units of the spot go_to aims for, the
 *                                                   guard is first set exactly on that spot. false: someone (a player, a
 *                                                   guard) stands on it, the guard only turns; call again once it is free
 *                   :keep_body()                    once dead, the guard's body stays where it fell (the game fades bodies out)
 *                   :aim_at(other) -> bool          an armed guard keeps another guard covered (actor_aim_at_actor, aim only)
 *                   :shoot(other)                   it fires at the other guard, which takes the hit the game's own
 *                                                   hostage takers deal (Frigate: HitChrWithItem part 8, item 10)
 *                                                   MISSION_SHOT_DELAY later, once the shot has been fired
 *                   :sees_player() -> bool          the guard sees a living player (chrCheckTargetInSight)
 *                   :was_shot_at() -> bool          a player's shot just missed it (chrIfNearMiss)
 *                   :guards_near(radius) -> number  living guards within radius of the target (the target itself left out)
 *                   :hits() -> number               how many times the guard has been hit (chrGetNumArghs)
 *                   :make_usable() :was_used()      an object players can activate (it gets the tag flag the game's own
 *                                                   terminals have); true once per activation since the last call. On a GUARD: a
 *                                                   player within 120 units, facing it, presses use (B), or stands within 90 units
 *                                                   for 1.5 s (missions_host.c guardUseTick), makes was_used() true once
 *                   :play_anim(name [, hold]) -> bool  play a named animation once (missions_host.c kAnims:
 *                                                   keyboard_right_hand_tapping, keyboard_left_hand, keyboard_right_hand2,
 *                                                   kneel_cough, kneel_cough2); hold = true: the guard stays on the last frame until
 *                                                   :stand() or another order
 *                   :health() -> number             a guard health in game units (maxdamage - damage); 0 if not there
 *                   :max_health() -> number         its maxdamage
 *                   :set_health(n)                  damage = maxdamage - n (damage clamped at 0, i.e. at most max_health)
 *                   :set_max_health(n)              maxdamage = n, RAW: not scaled by the 007 health modifier the setup health gets
 *                   :set_revivable([on])            goes down instead of dying (lies on the floor, invincible, no order
 *                                                   reaches it); a player revives it like a co-op partner: B near it,
 *                                                   10 s; up again with full health. :is_down() -> bool while it lies
 *                   :pos() -> x, y, z               the target current world position (a guard, player, prop, pad or zone);
 *                                                   nil, nil, nil when it has none (a guard not spawned, an original pad number)
 *                   :say(who, text [, range])       say(who, text) spoken from the target: a marker over it (a guard or a
 *                                                   player: above its head, followed while it moves, never in that player's
 *                                                   own view; a pad or a zone: its point) in every
 *                                                   player's view, pinned to the edge when out of view; range > 0: players
 *                                                   farther away see neither the line nor the marker. A target with no
 *                                                   position (an original pad number, a guard not spawned) says it plainly
 *                   .prop .kind .zone (fields)
 *   print(...)                                      to the host log
 * Events (name, fields): "start", "end" {win}, "pickup" {item, player}, "death" {prop},
 *   "door" {prop, state = "open" | "closed" | "moving"}, "zone" {id, player},
 *   "objective" {id}, "objective_failed" {id},
 *   "player_death" {player}, "player_hurt" {player, health 0..1, armor 0..1} (health or armor went down),
 *   "player_revived" {player}, "gunfire" {x, y, z, player?, prop?} (a shot starts at x y z; player = 0-based when a player
 *   fired, prop = the setup prop when a setup guard fired; at most one per shooter per 0.25 s), "alarm" {} (off to on),
 *   "guard_hurt" {prop, health} (a setup guard damage went up; health in game units),
 *   "guard_down" {prop} / "guard_revived" {prop} (a revivable guard went down instead of dying / a player revived it).
 * Events are found by diffing the host state every tick (the host raises the player, gunfire, alarm and guard_hurt ones
 * through missionRtRaise); "objective" and "end" are raised by the runtime itself. */

#include <stddef.h>
#include <stdint.h>
#include "mission.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { MH_DOOR_LOCK, MH_DOOR_UNLOCK, MH_DOOR_OPEN, MH_DOOR_CLOSE, MH_DOOR_UNLOCK_FORCE };
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
    void (*guard_pose)(void *ud, int prop, int pose, int player);  /* MH_POSE_*: hands up, or stand facing that player */
    int  (*guard_goto_prop)(void *ud, int prop, int target, int run);  /* to the pad of setup prop `target` (in front of
                                                                          a wall pad); run as guard_goto; MH_GOTO_* */
    int  (*guard_moving)(void *ud, int prop);                      /* 1 while the guard is walking to a position */
    int  (*guard_face_prop)(void *ud, int prop, int target);       /* face setup prop `target` square on, set on the spot in
                                                                      front of it when close; 0: that spot is occupied */
    int  (*guard_anim)(void *ud, int prop, const char *name, int hold);   /* play a named animation once (hold: stay on its last
                                                                              frame); 0: unknown or refused */
    int  (*guard_health)(void *ud, int prop, double *health, double *max);   /* maxdamage - damage, maxdamage; 0: no such guard */
    void (*guard_set_health)(void *ud, int prop, int which, double v);       /* MH_HEALTH_*: v is the health or the raw maxdamage */
    void (*guard_set_revivable)(void *ud, int prop, int on);       /* goes down instead of dying; a player revives it */
    int  (*guard_is_down)(void *ud, int prop);                     /* 1 while a revivable guard lies down */
    void (*prop_usable)(void *ud, int prop);                       /* a player can activate this object (B / use) */
    void (*guard_keep_body)(void *ud, int prop);                   /* the guard's body stays when it dies (no fade-out) */
    int  (*guard_aim_at)(void *ud, int prop, int target, int fire);  /* aim (fire: and shoot) at guard `target`; 0: unarmed */
    void (*guard_hit)(void *ud, int prop);                         /* the hit the game's own hostage AI deals (HitChrWithItem) */
    int  (*guard_sees_player)(void *ud, int prop);                 /* 1 when the guard sees a living player */
    int  (*guard_near_miss)(void *ud, int prop);                   /* 1 when a player's shot just missed the guard */
    int  (*guards_near)(void *ud, const float pos[3], float radius, int except);  /* living guards within radius (not `except`) */
    int  (*guard_hits)(void *ud, int prop);                        /* how many times the guard has been hit (chrGetNumArghs) */
    int  (*prop_used)(void *ud, int prop);                         /* 1 once per activation by a player (clears it) */
    void (*alarm)(void *ud);
    void (*win)(void *ud);
    int  (*difficulty)(void *ud);                      /* 0 Agent .. 3 007; NULL = every objective counts */
    int  (*guard_goto)(void *ud, int prop, int player, int run);   /* send the guard to that player's position: run 0 walk, 1 run,
                                                                       2 sprint; returns an MH_GOTO_* result */
    void (*say)(void *ud, const char *who, const char *text);      /* top-of-screen dialogue line */
    /* The same line, spoken from a place: `kind` MH_SAY_PROP (setup prop `index`, followed while it moves),
     * MH_SAY_PLAYER (player `index`, followed; no marker in its own view) or MH_SAY_POINT (the fixed point pos, a pad
     * or a zone); pos is the speaker's position now in every case. range > 0: players farther away do not get it. */
    void (*say_at)(void *ud, const char *who, const char *text, int kind, int index, const float pos[3], float range);
    void (*fail)(void *ud);                            /* end the level as a failure */
    /* #165 (the Luau campaign): game text by id, and the setup's own ids. */
    int  (*text)(void *ud, int id, char *out, size_t outlen);   /* the game text `id` (bank * 1024 + index) in the language
                                                                   played, without its final line break; 0: no such text */
    int  (*chr_prop)(void *ud, int chrnum);            /* the setup prop of the guard with chr number `chrnum`; -1: none */
    int  (*tag_prop)(void *ud, int tag);               /* the setup prop an object tag `tag` names; -1: none */
    int  (*prop_chr)(void *ud, int prop);              /* the chr number of setup guard `prop`; -1: not a guard */
    int  (*pad_by_id)(void *ud, const char *id);       /* the pad number of a mission pad (its string id); -1: none */
    /* The campaign API's domains (mission_lua_ext.h); NULL: not available. */
    const struct MissionHostGuard  *guard_ext;
    const struct MissionHostWorld  *world_ext;
    const struct MissionHostCinema *cinema_ext;
} MissionHost;

#define MH_HEALTH_CUR  0
#define MH_HEALTH_MAX  1

/* guard:follow(): the passive AI list (GAILIST_DEAD_AI, "yield forever") and the follow timing. */
/* say_at speakers */
#define MH_SAY_POINT   0
#define MH_SAY_PROP    1
#define MH_SAY_PLAYER  2

/* guard_pose poses */
#define MH_POSE_STAND      0   /* stand still facing the player (check_set_actor_standing_still, TARGET_BOND) */
#define MH_POSE_SURRENDER  1   /* hands up (chrTrySurrender; an armed guard throws its weapon) */

/* guard_goto results */
#define MH_GOTO_OK       1    /* the guard is on its way */
#define MH_GOTO_NOROUTE  0    /* no waypoint route between the guard and the player */
#define MH_GOTO_BUSY    -1    /* the game refuses: 10 or more guards have seen a player recently (chrGoToBond) */
#define MH_GOTO_GONE    -2    /* no such guard, not spawned, or dead */

#define MISSION_SHOT_DELAY     0.5     /* seconds from a :shoot() order to its hit (the shooter raises and fires first) */

#define MISSION_FOLLOW_AI      1
#define MISSION_FOLLOW_EVERY   0.5     /* seconds between checks */
#define MISSION_FOLLOW_NEAR    200.0   /* units: no order while the nearest player is this close */
#define MISSION_FOLLOW_RUN     350.0   /* units: run instead of walk beyond this */
#define MISSION_FOLLOW_SPRINT  1200.0  /* units: sprint beyond this */
#define MISSION_FOLLOW_REPLOT  150.0   /* units: re-order when the player is this far from where the guard is heading ... */
#define MISSION_FOLLOW_REPLOT_K 0.25   /* ... or this fraction of the distance between them, whichever is larger */
#define MISSION_FOLLOW_GAP     2.0     /* seconds: at most one order this often, unless a faster speed or another player */
#define MISSION_FOLLOW_STILL   10.0    /* units: a guard that moved less than this in a check has not moved */
#define MISSION_FOLLOW_STALL   3.0     /* seconds: a guard that has not moved for this long is re-ordered (starting from a
                                          standstill can take over a second: turning, accelerating) */

typedef struct MissionRuntime MissionRuntime;

/* Script lookup for "use": <mission_dir>/<use>.luau or <mission_dir>/scripts/<use>.luau, then <mod_dir>/scripts/<use>.luau, then the
 * built-in library. Either dir may be NULL. Returns NULL (and err) if the VM
 * could not be made; a script that fails to load or run is logged and skipped. */
MissionRuntime *missionRtCreate(const Mission *m, const MissionHost *host, const char *mission_dir, const char *mod_dir,
                                char *err, size_t errlen);
void missionRtDestroy(MissionRuntime *rt);

/* An event the host found (player, gunfire, alarm, guard_hurt ...): str != NULL makes the field a string, else num is used. */
typedef struct MissionEvField { const char *key; const char *str; double num; } MissionEvField;
void missionRtRaise(MissionRuntime *rt, const char *name, const MissionEvField *fields, int nfields);

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
int  missionRtObjectiveStatus(const MissionRuntime *rt, int i);   /* OBJECTIVESTATUS: 0 incomplete, 1 complete, 2 failed */
int  missionRtObjectiveQuiet(const MissionRuntime *rt, int i);    /* completed with objectives.complete(id, true): no HUD line */
int  missionRtAllComplete(const MissionRuntime *rt, int difficulty);
/* Marks an objective complete from the host side (e.g. the debug console). */
int  missionRtCompleteObjective(MissionRuntime *rt, const char *id);

/* Counters for tests / logs. */
int  missionRtScriptCount(const MissionRuntime *rt);       /* scripts started */
int  missionRtScriptsAlive(const MissionRuntime *rt);      /* still waiting */

/* The built-in library (generated from port/mission/lib/*.luau): NULL name ends the list. */
typedef struct MissionLibScript { const char *name; const char *source; } MissionLibScript;
extern const MissionLibScript g_missionLibScripts[];

/* The Luau definition file of the whole script API (port/mission/lib/mission.d.luau, NOT a runnable script):
 * globals, target methods, event names and the editor-facing aliases (Item, Objective, Door, ...). For the editor. */
extern const char *const g_missionApiDefs;

#ifdef __cplusplus
}
#endif

#endif
