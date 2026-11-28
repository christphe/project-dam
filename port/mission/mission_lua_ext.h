#ifndef PORT_MISSION_LUA_EXT_H
#define PORT_MISSION_LUA_EXT_H

/* The campaign API (#165), split by domain so each lives in files of its own:
 *
 *   domain   host callbacks             bindings                 host implementation
 *   guard    mission_host_guard.h       mission_lua_guard.cpp    port/src/missions_host_guard.c
 *   world    mission_host_world.h       mission_lua_world.cpp    port/src/missions_host_world.c
 *   cinema   mission_host_cinema.h      mission_lua_cinema.cpp   port/src/missions_host_cinema.c
 *
 * The declarations scripts see are port/mission/lib/mission.d.luau. A domain's bindings reach the game only through
 * its host struct (MissionHost.guard_ext / world_ext / cinema_ext); a NULL struct or a NULL callback means "not
 * available here" (the tests' fake host): a binding then returns its neutral value (false, nil, 0) and logs once.
 *
 * Who owns what (the mission.d.luau #165 section):
 *   guard   every guard method of the "presence", "settings", "movement", "gestures and animation", "combat" and
 *           "perception" groups; set_invincible on a guard; spawn_guard(); mission.civilians_killed(); the events
 *           "anim_end" and "guard_damaged".
 *   world   in_room_of (any target); the "objects" and "doors" groups; set_invincible on an object or a player;
 *           the player queries armour / held_item / holds / force_weapon / push; pad(), player_in_room(),
 *           item_landed(); hud, countdown, sound, music, fog; alarm.is_on / stop; mission.finish / exiting;
 *           objectives.set_failed / fail_all; the events "destroyed", "activated", "gadget_used", "picked_up",
 *           "door_refused"; "text:XXXX" references in mission.json (name, description, briefing, objectives).
 *   cinema  cinema, camera, screen, credits; bond_body() and its methods (teleport, anim, anim_done, hold_item,
 *           show, set_shadow on kind "body"); the player methods set_frozen / hide_weapon / in_cutscene / show;
 *           the event "intro_done".
 * Registration runs guard, then world, then cinema. A later domain that shares a method name with an earlier one
 * (anim / anim_done / teleport on a "body", set_invincible on an object) WRAPS it: it reads the earlier function
 * from the metatable, installs its own, handles its own target kinds and calls the earlier one for the rest. */

#include "mission_lua.h"

#ifdef __cplusplus
struct lua_State;

/* Helpers of the runtime (mission_lua.cpp) for the domain bindings. */
const MissionHost *missionLuaHost(lua_State *L);
void missionLuaLog(lua_State *L, const char *fmt, ...);
int  missionLuaTgtProp(lua_State *L, int idx);   /* the target's setup prop, -1: none */
int  missionLuaTgtChr(lua_State *L, int idx);    /* the target's chr number (a guard; from its prop when it has no .chr),
                                                    -1: none */
int  missionLuaTgtPlayer(lua_State *L, int idx); /* a player target's 0-based player, -1: not a player */
int  missionLuaTgtPad(lua_State *L, int idx);    /* a pad target's original pad number, -1: none */
/* A Pad argument at `idx`, whatever its form: a number, a mission pad's string id, or a pad target (pad(n) /
 * pad("p1")); -1: none. Every domain takes pads through this, so mission pads work everywhere (#165). */
int  missionLuaPadArg(lua_State *L, int idx);
/* Push a target: kind "prop" with prop (-1 none) and, for a guard, chr (-1 none); kind "pad" / "player" / "body"
 * with `index` as the pad / player number. */
void missionLuaPushTarget(lua_State *L, const char *kind, int prop, int chr, int index);
/* Raise an event from a binding (same as missionRtRaise). */
void missionLuaRaise(lua_State *L, const char *name, const MissionEvField *fields, int nfields);

/* Registration: called with the Target metatable at stack index `targetMT`. Each adds its Target methods to that
 * table and its globals / namespace tables with lua_setglobal. */
void missionLuaRegGuard(lua_State *L, int targetMT);
void missionLuaRegWorld(lua_State *L, int targetMT);
void missionLuaRegCinema(lua_State *L, int targetMT);
#endif

#endif
