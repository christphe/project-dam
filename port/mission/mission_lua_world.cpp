// The world domain of the campaign API (#165): Luau bindings. Layout: mission_lua_ext.h; script contract:
// lib/mission.d.luau; host callbacks: mission_host_world.h (implemented in port/src/missions_host_world.c).
//
// The Target methods of this domain work on a setup prop number (an object by tag(n) or a door), a player or a pad.
// Every verb is registered even when the host does not provide it (the tests' fake host, an unfinished verb): it then
// logs "<name>: not implemented yet" once per VM and returns its neutral value (false, nil, 0).
#include "mission_lua_ext.h"
#include "mission_host_world.h"

#include <cstring>
#include <initializer_list>

#include "lua.h"
#include "lualib.h"

namespace {

const char kLogged[] = "ge.world.logged";    // registry: { [name] = true } of the stubs already logged
const char kObjAdded[] = "ge.world.objadded"; // registry: { [id] = difficulty } of the objectives scripts added

const MissionHostWorld *W(lua_State *L) { return missionLuaHost(L)->world_ext; }

// The verb is not available: log it once, the caller returns its neutral value.
void stub(lua_State *L, const char *name)
{
    lua_getfield(L, LUA_REGISTRYINDEX, kLogged);
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setfield(L, LUA_REGISTRYINDEX, kLogged);
    }
    lua_getfield(L, -1, name);
    const bool seen = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    if (!seen) {
        lua_pushboolean(L, 1);
        lua_setfield(L, -2, name);
        missionLuaLog(L, "%s: not implemented yet", name);
    }
    lua_pop(L, 1);
}

// A pad argument: an original pad number, the id of a mission pad or a pad target (missionLuaPadArg, mission_lua_ext.h).
bool padArg(lua_State *L, int idx, int *pad)
{
    *pad = missionLuaPadArg(L, idx);
    return *pad >= 0;
}

// ------------------------------------------------------------------ in_room_of (every target kind)
// The guard domain may have registered one first: it is the upvalue; anything that is not ours goes to it.
int callEarlier(lua_State *L)
{
    if (!lua_isfunction(L, lua_upvalueindex(1))) return -1;
    const int n = lua_gettop(L);
    lua_pushvalue(L, lua_upvalueindex(1));
    for (int i = 1; i <= n; i++) lua_pushvalue(L, i);
    lua_call(L, n, LUA_MULTRET);
    return lua_gettop(L) - n;
}

// What a target is for the room checks: MHW_ROOM_* kind and its index, -1 when it is none of them.
void roomSubject(lua_State *L, int idx, int *kind, int *index)
{
    const int player = missionLuaTgtPlayer(L, idx), chr = missionLuaTgtChr(L, idx), prop = missionLuaTgtProp(L, idx), tpad = missionLuaTgtPad(L, idx);
    *kind = -1;
    *index = -1;
    if (player >= 0) { *kind = MHW_ROOM_PLAYER; *index = player; }
    else if (chr >= 0) { *kind = MHW_ROOM_CHR; *index = chr; }
    else if (prop >= 0) { *kind = MHW_ROOM_PROP; *index = prop; }
    else if (tpad >= 0) { *kind = MHW_ROOM_PAD; *index = tpad; }
}

int t_in_room_of(lua_State *L)
{
    const MissionHostWorld *w = W(L);
    int kind, index;
    roomSubject(L, 1, &kind, &index);
    if (kind < 0 || !w || !w->in_room) {
        const int r = callEarlier(L);
        if (r >= 0) return r;
        if (kind >= 0) stub(L, "Target:in_room_of");
        lua_pushboolean(L, 0);
        return 1;
    }
    int pad = -1;
    if (!padArg(L, 2, &pad)) { lua_pushboolean(L, 0); return 1; }
    lua_pushboolean(L, w->in_room(w->ud, kind, index, pad) != 0);
    return 1;
}

// same_room_as(other): any two targets (guards, objects, players, pads).
int t_same_room_as(lua_State *L)
{
    const MissionHostWorld *w = W(L);
    int ka, ia, kb = -1, ib = -1;
    roomSubject(L, 1, &ka, &ia);
    if (lua_istable(L, 2)) roomSubject(L, 2, &kb, &ib);
    if (!w || !w->same_room) { stub(L, "Target:same_room_as"); lua_pushboolean(L, 0); return 1; }
    lua_pushboolean(L, ka >= 0 && kb >= 0 && w->same_room(w->ud, ka, ia, kb, ib) != 0);
    return 1;
}

// set_invincible: an object (prop, no chr) or a player is ours; a guard goes to the earlier function.
int t_set_invincible(lua_State *L)
{
    const MissionHostWorld *w = W(L);
    const int on = lua_toboolean(L, 2);
    const int player = missionLuaTgtPlayer(L, 1), chr = missionLuaTgtChr(L, 1), prop = missionLuaTgtProp(L, 1);
    if (player >= 0) {
        if (!w || !w->player_set_invincible) { stub(L, "Target:set_invincible (player)"); return 0; }
        w->player_set_invincible(w->ud, player, on);
        return 0;
    }
    if (chr < 0 && prop >= 0) {
        if (!w || !w->obj_set_invincible) { stub(L, "Target:set_invincible (object)"); return 0; }
        if (w->obj_set_invincible(w->ud, prop, on)) return 0;
    }
    const int r = callEarlier(L);
    return r >= 0 ? r : 0;
}

// ------------------------------------------------------------------ objects
// Every prop verb below does nothing on a target with no prop (a removed or absent object).
int t_is_destroyed(lua_State *L)
{
    const MissionHostWorld *w = W(L);
    const int p = missionLuaTgtProp(L, 1);
    if (!w || !w->obj_destroyed) { stub(L, "Target:is_destroyed"); lua_pushboolean(L, 0); return 1; }
    // the host counts an object that is not there (not spawned, gone) as destroyed, as the game's own "object not
    // destroyed" test (AI_IFObjectNotDestroyed: obj && obj->prop && healthy) fails for it
    lua_pushboolean(L, p >= 0 && w->obj_destroyed(w->ud, p) != 0);
    return 1;
}

int t_destroy(lua_State *L)
{
    const MissionHostWorld *w = W(L);
    const int p = missionLuaTgtProp(L, 1);
    if (!w || !w->obj_destroy) { stub(L, "Target:destroy"); return 0; }
    if (p >= 0) w->obj_destroy(w->ud, p);
    return 0;
}

int t_explode(lua_State *L)
{
    const MissionHostWorld *w = W(L);
    const int p = missionLuaTgtProp(L, 1);
    if (!w || !w->obj_explode) { stub(L, "Target:explode"); return 0; }
    if (p >= 0) w->obj_explode(w->ud, p);
    return 0;
}

int t_set_collectable(lua_State *L)
{
    const MissionHostWorld *w = W(L);
    const int p = missionLuaTgtProp(L, 1), on = lua_toboolean(L, 2);
    if (!w || !w->obj_set_collectable) { stub(L, "Target:set_collectable"); return 0; }
    if (p >= 0) w->obj_set_collectable(w->ud, p, on);
    return 0;
}

int t_move_to(lua_State *L)
{
    const MissionHostWorld *w = W(L);
    const int p = missionLuaTgtProp(L, 1);
    int pad = -1;
    if (!w || !w->obj_move_to) { stub(L, "Target:move_to"); return 0; }
    if (padArg(L, 2, &pad) && p >= 0) w->obj_move_to(w->ud, p, pad);
    return 0;
}

int t_activate(lua_State *L)
{
    const MissionHostWorld *w = W(L);
    const int p = missionLuaTgtProp(L, 1);
    if (!w || !w->obj_activate) { stub(L, "Target:activate"); return 0; }
    if (p >= 0) w->obj_activate(w->ud, p);
    return 0;
}

int t_show_screen(lua_State *L)
{
    const MissionHostWorld *w = W(L);
    const int p = missionLuaTgtProp(L, 1), bank = (int)luaL_checkinteger(L, 2), screen = (int)luaL_optinteger(L, 3, 0);
    if (!w || !w->obj_show_screen) { stub(L, "Target:show_screen"); return 0; }
    if (p >= 0) w->obj_show_screen(w->ud, p, bank, screen);
    return 0;
}

int t_held_by(lua_State *L)
{
    const MissionHostWorld *w = W(L);
    const int p = missionLuaTgtProp(L, 1), player = lua_isnoneornil(L, 2) ? -1 : (int)luaL_checkinteger(L, 2);
    if (!w || !w->obj_held_by) { stub(L, "Target:held_by"); lua_pushboolean(L, 0); return 1; }
    lua_pushboolean(L, p >= 0 && w->obj_held_by(w->ud, p, player) != 0);
    return 1;
}

// speed is in the setup's own rotor units (AI_AircraftRotorSpeed's ROTOR_SPEED), ramp in seconds.
int t_set_rotor(lua_State *L)
{
    const MissionHostWorld *w = W(L);
    const int p = missionLuaTgtProp(L, 1);
    const double speed = luaL_checknumber(L, 2), ramp = luaL_optnumber(L, 3, 0.0);
    if (!w || !w->obj_set_rotor) { stub(L, "Target:set_rotor"); return 0; }
    if (p >= 0) w->obj_set_rotor(w->ud, p, speed, ramp < 0 ? 0 : ramp);
    return 0;
}

int t_drive(lua_State *L)
{
    const MissionHostWorld *w = W(L);
    const int p = missionLuaTgtProp(L, 1), path = (int)luaL_checkinteger(L, 2);
    const double speed = luaL_checknumber(L, 3), ramp = luaL_optnumber(L, 4, 0.0);
    if (!w || !w->obj_drive) { stub(L, "Target:drive"); return 0; }
    if (p >= 0) w->obj_drive(w->ud, p, path, speed, ramp < 0 ? 0 : ramp);
    return 0;
}

// set_active(on): a turret / drone gun / camera on or off.
int t_set_active(lua_State *L)
{
    const MissionHostWorld *w = W(L);
    const int p = missionLuaTgtProp(L, 1), on = lua_toboolean(L, 2);
    if (!w || !w->obj_set_active) { stub(L, "Target:set_active"); return 0; }
    if (p >= 0) w->obj_set_active(w->ud, p, on);
    return 0;
}

// set_damage("all" | "explosions" | "none")
int t_set_damage(lua_State *L)
{
    const MissionHostWorld *w = W(L);
    const int p = missionLuaTgtProp(L, 1);
    const char *k = luaL_checkstring(L, 2);
    int kind;
    if (!strcmp(k, "all")) kind = MHW_DMG_ALL;
    else if (!strcmp(k, "explosions")) kind = MHW_DMG_EXPLOSIONS;
    else if (!strcmp(k, "none")) kind = MHW_DMG_NONE;
    else { luaL_error(L, "set_damage: \"%s\" is not \"all\", \"explosions\" or \"none\"", k); return 0; }
    if (!w || !w->obj_set_damage) { stub(L, "Target:set_damage"); return 0; }
    if (p >= 0) w->obj_set_damage(w->ud, p, kind);
    return 0;
}

// set_hidden / anim / health wrap the guard domain's: a guard (chr) goes to it, an object (and a player for health) is ours.
int t_set_hidden(lua_State *L)
{
    const MissionHostWorld *w = W(L);
    const int chr = missionLuaTgtChr(L, 1), prop = missionLuaTgtProp(L, 1);
    if (chr < 0 && prop >= 0) {
        if (!w || !w->obj_set_hidden) { stub(L, "Target:set_hidden (object)"); return 0; }
        if (w->obj_set_hidden(w->ud, prop, lua_toboolean(L, 2))) return 0;
    }
    const int r = callEarlier(L);
    return r >= 0 ? r : 0;
}

int t_anim(lua_State *L)
{
    const MissionHostWorld *w = W(L);
    const int chr = missionLuaTgtChr(L, 1), prop = missionLuaTgtProp(L, 1);
    if (chr < 0 && prop >= 0 && lua_type(L, 2) == LUA_TNUMBER) {
        int from = -1, to = -1;
        double speed = 1.0, blend = -1.0, v;
        if (lua_istable(L, 3)) {
            lua_getfield(L, 3, "from");  if (lua_isnumber(L, -1)) from = (int)lua_tonumber(L, -1);  lua_pop(L, 1);
            lua_getfield(L, 3, "to");    if (lua_isnumber(L, -1)) to = (int)lua_tonumber(L, -1);    lua_pop(L, 1);
            lua_getfield(L, 3, "speed"); if (lua_isnumber(L, -1) && (v = lua_tonumber(L, -1)) > 0) speed = v; lua_pop(L, 1);
            lua_getfield(L, 3, "blend"); if (lua_isnumber(L, -1)) blend = lua_tonumber(L, -1);      lua_pop(L, 1);
        }
        if (!w || !w->obj_anim) { stub(L, "Target:anim (object)"); lua_pushboolean(L, 0); return 1; }
        if (w->obj_anim(w->ud, prop, (int)lua_tonumber(L, 2), from, to, speed, blend)) { lua_pushboolean(L, 1); return 1; }
    }
    const int r = callEarlier(L);
    if (r >= 0) return r;
    lua_pushboolean(L, 0);
    return 1;
}

// ------------------------------------------------------------------ doors
int t_state(lua_State *L)
{
    static const char *const kNames[] = { "closed", "opening", "open", "closing" };
    const MissionHostWorld *w = W(L);
    const int p = missionLuaTgtProp(L, 1);
    int s = MHW_DOOR_NONE;
    if (!w || !w->door_phase) { stub(L, "Target:state"); lua_pushstring(L, "closed"); return 1; }
    if (p >= 0) s = w->door_phase(w->ud, p);
    lua_pushstring(L, s >= 0 && s < 4 ? kNames[s] : "closed");   // not a door: the neutral "closed"
    return 1;
}

int t_open_now(lua_State *L)
{
    const MissionHostWorld *w = W(L);
    const int p = missionLuaTgtProp(L, 1);
    if (!w || !w->door_open_now) { stub(L, "Target:open_now"); return 0; }
    if (p >= 0) w->door_open_now(w->ud, p);
    return 0;
}

int t_set_passable(lua_State *L)
{
    const MissionHostWorld *w = W(L);
    const int p = missionLuaTgtProp(L, 1), on = lua_toboolean(L, 2);
    if (!w || !w->door_set_passable) { stub(L, "Target:set_passable"); return 0; }
    if (p >= 0) w->door_set_passable(w->ud, p, on);
    return 0;
}

int t_set_usable_by_guards(lua_State *L)
{
    const MissionHostWorld *w = W(L);
    const int p = missionLuaTgtProp(L, 1), on = lua_toboolean(L, 2);
    if (!w || !w->door_set_usable_by_guards) { stub(L, "Target:set_usable_by_guards"); return 0; }
    if (p >= 0) w->door_set_usable_by_guards(w->ud, p, on);
    return 0;
}

// ------------------------------------------------------------------ players
int t_armour(lua_State *L)
{
    const MissionHostWorld *w = W(L);
    const int pl = missionLuaTgtPlayer(L, 1);
    double a = 0;
    if (!w || !w->player_armour) { stub(L, "Target:armour"); lua_pushnumber(L, 0); return 1; }
    if (pl >= 0 && !w->player_armour(w->ud, pl, &a)) a = 0;
    lua_pushnumber(L, a);
    return 1;
}

// health() on a player: 0..1; anything else goes to the setup-guard health() (game units).
int t_health(lua_State *L)
{
    const MissionHostWorld *w = W(L);
    const int pl = missionLuaTgtPlayer(L, 1);
    double h = 0;
    if (pl < 0) {
        const int r = callEarlier(L);
        if (r >= 0) return r;
        lua_pushnumber(L, 0);
        return 1;
    }
    if (!w || !w->player_health) { stub(L, "Target:health (player)"); lua_pushnumber(L, 0); return 1; }
    if (!w->player_health(w->ud, pl, &h)) h = 0;
    lua_pushnumber(L, h);
    return 1;
}

int t_kill(lua_State *L)
{
    const MissionHostWorld *w = W(L);
    const int pl = missionLuaTgtPlayer(L, 1);
    if (!w || !w->player_kill) { stub(L, "Target:kill"); return 0; }
    if (pl >= 0) w->player_kill(w->ud, pl);
    return 0;
}

int t_held_item(lua_State *L)
{
    const MissionHostWorld *w = W(L);
    const int pl = missionLuaTgtPlayer(L, 1);
    if (!w || !w->player_held_item) { stub(L, "Target:held_item"); lua_pushnumber(L, 0); return 1; }
    lua_pushnumber(L, pl >= 0 ? w->player_held_item(w->ud, pl) : 0);
    return 1;
}

int t_holds(lua_State *L)
{
    const MissionHostWorld *w = W(L);
    const int pl = missionLuaTgtPlayer(L, 1), item = (int)luaL_checkinteger(L, 2);
    if (!w || !w->player_holds) { stub(L, "Target:holds"); lua_pushboolean(L, 0); return 1; }
    lua_pushboolean(L, pl >= 0 && w->player_holds(w->ud, pl, item) != 0);
    return 1;
}

int t_force_weapon(lua_State *L)
{
    const MissionHostWorld *w = W(L);
    const int pl = missionLuaTgtPlayer(L, 1), item = (int)luaL_checkinteger(L, 2);
    if (!w || !w->player_force_weapon) { stub(L, "Target:force_weapon"); return 0; }
    if (pl >= 0) w->player_force_weapon(w->ud, pl, item);
    return 0;
}

int t_push(lua_State *L)
{
    const MissionHostWorld *w = W(L);
    const int pl = missionLuaTgtPlayer(L, 1);
    const double dx = luaL_checknumber(L, 2), dz = luaL_checknumber(L, 3);
    if (!w || !w->player_push) { stub(L, "Target:push"); return 0; }
    if (pl >= 0) w->player_push(w->ud, pl, dx, dz);
    return 0;
}

// facing(): a player or a guard, the camera API's yaw in degrees (nil-free: 0 when it is neither or not placed).
int t_facing(lua_State *L)
{
    const MissionHostWorld *w = W(L);
    const int pl = missionLuaTgtPlayer(L, 1), chr = missionLuaTgtChr(L, 1);
    double deg = 0;
    if (!w || !w->facing) { stub(L, "Target:facing"); lua_pushnumber(L, 0); return 1; }
    if (pl >= 0) { if (!w->facing(w->ud, MHW_ROOM_PLAYER, pl, &deg)) deg = 0; }
    else if (chr >= 0) { if (!w->facing(w->ud, MHW_ROOM_CHR, chr, &deg)) deg = 0; }
    lua_pushnumber(L, deg);
    return 1;
}

// eye(): x, y, z of a player's eye point; nil, nil, nil when it is not a player or not placed.
int t_eye(lua_State *L)
{
    const MissionHostWorld *w = W(L);
    const int pl = missionLuaTgtPlayer(L, 1);
    double p[3] = { 0, 0, 0 };
    if (!w || !w->player_eye) { stub(L, "Target:eye"); }
    else if (pl >= 0 && w->player_eye(w->ud, pl, p)) {
        lua_pushnumber(L, p[0]);
        lua_pushnumber(L, p[1]);
        lua_pushnumber(L, p[2]);
        return 3;
    }
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnil(L);
    return 3;
}

// ------------------------------------------------------------------ globals
// give_object(object, player?): a tagged object into a player's inventory (every player when omitted).
int l_give_object(lua_State *L)
{
    const MissionHostWorld *w = W(L);
    const int p = lua_istable(L, 1) ? missionLuaTgtProp(L, 1) : -1;
    const int player = lua_isnoneornil(L, 2) ? -1 : (int)luaL_checkinteger(L, 2);
    if (!w || !w->give_object) { stub(L, "give_object"); lua_pushboolean(L, 0); return 1; }
    lua_pushboolean(L, p >= 0 && w->give_object(w->ud, p, player) != 0);
    return 1;
}

int l_item_in_room(lua_State *L)
{
    const MissionHostWorld *w = W(L);
    const int item = (int)luaL_checkinteger(L, 1);
    int pad = -1;
    if (!w || !w->item_in_room) { stub(L, "item_in_room"); lua_pushboolean(L, 0); return 1; }
    lua_pushboolean(L, padArg(L, 2, &pad) && w->item_in_room(w->ud, item, pad) != 0);
    return 1;
}

int l_explosions_around_players(lua_State *L)
{
    const MissionHostWorld *w = W(L);
    const int on = lua_toboolean(L, 1);
    if (!w || !w->explosions_around_players) { stub(L, "explosions_around_players"); return 0; }
    w->explosions_around_players(w->ud, on);
    return 0;
}

// give(item, player, { silent = true }): the runtime's give (upvalue 1) already adds the item without a pickup message
// (bondinvAddInvItem prints none), so `silent` only has to be accepted: the original is called with its two arguments.
int l_give_wrap(lua_State *L)
{
    const bool hasPlayer = !lua_isnoneornil(L, 2);
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_pushvalue(L, 1);
    if (hasPlayer) lua_pushvalue(L, 2);
    lua_call(L, hasPlayer ? 2 : 1, 0);
    return 0;
}

int l_pad(lua_State *L)
{
    if (lua_type(L, 1) == LUA_TSTRING) {
        missionLuaPushTarget(L, "pad", -1, -1, -1);
        lua_pushvalue(L, 1);
        lua_setfield(L, -2, "pad");
    } else {
        missionLuaPushTarget(L, "pad", -1, -1, (int)luaL_checkinteger(L, 1));
    }
    return 1;
}

int l_player_in_room(lua_State *L)
{
    const MissionHostWorld *w = W(L);
    int pad = -1;
    if (!w || !w->player_in_room) { stub(L, "player_in_room"); lua_pushnil(L); return 1; }
    const int p = padArg(L, 1, &pad) ? w->player_in_room(w->ud, pad) : -1;
    if (p >= 0) lua_pushnumber(L, p); else lua_pushnil(L);
    return 1;
}

int l_item_landed(lua_State *L)
{
    const MissionHostWorld *w = W(L);
    const int item = (int)luaL_checkinteger(L, 1);
    int tag = -1, r = MHW_LANDED_NONE;
    if (!w || !w->item_landed) { stub(L, "item_landed"); lua_pushnil(L); lua_pushnil(L); return 2; }
    r = w->item_landed(w->ud, item, &tag);
    if (r == MHW_LANDED_STUCK) {
        lua_pushstring(L, "stuck");
        if (tag >= 0) lua_pushnumber(L, tag); else lua_pushnil(L);
    } else if (r == MHW_LANDED_LOOSE) {
        lua_pushstring(L, "loose");
        lua_pushnil(L);
    } else {
        lua_pushnil(L);
        lua_pushnil(L);
    }
    return 2;
}

// hud
int hud_top(lua_State *L)
{
    const MissionHostWorld *w = W(L);
    const char *t = luaL_checkstring(L, 1);
    if (!w || !w->hud_top) { stub(L, "hud.top"); return 0; }
    w->hud_top(w->ud, t);
    return 0;
}

int hud_bottom(lua_State *L)   // the bottom line is message()
{
    const MissionHost *h = missionLuaHost(L);
    const char *t = luaL_checkstring(L, 1);
    if (h->message) h->message(h->ud, t);
    return 0;
}

int hud_hide(lua_State *L)
{
    const MissionHostWorld *w = W(L);
    const int on = lua_toboolean(L, 1);
    if (!w || !w->hud_hide) { stub(L, "hud.hide"); return 0; }
    w->hud_hide(w->ud, on);
    return 0;
}

// countdown
// countdown.set on a running countdown keeps it running: the game's countdownTimerSetValue only writes the clock value.
int cd_set(lua_State *L)
{
    const MissionHostWorld *w = W(L);
    const double s = luaL_checknumber(L, 1);
    if (!w || !w->countdown_set) { stub(L, "countdown.set"); return 0; }
    w->countdown_set(w->ud, s < 0 ? 0 : s);
    return 0;
}

int cd_start(lua_State *L)
{
    const MissionHostWorld *w = W(L);
    if (!w || !w->countdown_run) { stub(L, "countdown.start"); return 0; }
    w->countdown_run(w->ud, 1);
    return 0;
}

int cd_stop(lua_State *L)
{
    const MissionHostWorld *w = W(L);
    if (!w || !w->countdown_run) { stub(L, "countdown.stop"); return 0; }
    w->countdown_run(w->ud, 0);
    return 0;
}

int cd_show(lua_State *L)
{
    const MissionHostWorld *w = W(L);
    const int on = lua_toboolean(L, 1);
    if (!w || !w->countdown_show) { stub(L, "countdown.show"); return 0; }
    w->countdown_show(w->ud, on);
    return 0;
}

int cd_left(lua_State *L)
{
    const MissionHostWorld *w = W(L);
    if (!w || !w->countdown_left) { stub(L, "countdown.left"); lua_pushnumber(L, 0); return 1; }
    lua_pushnumber(L, w->countdown_left(w->ud));
    return 1;
}

// sound: play(id, { from = Target, channel = n }) -> channel
int snd_play(lua_State *L)
{
    const MissionHostWorld *w = W(L);
    const int id = (int)luaL_checkinteger(L, 1);
    int channel = -1, kind = MHW_FROM_NONE, index = -1;
    if (lua_istable(L, 2)) {
        lua_getfield(L, 2, "channel");
        if (lua_isnumber(L, -1)) channel = (int)lua_tonumber(L, -1);
        lua_pop(L, 1);
        lua_getfield(L, 2, "from");
        if (lua_istable(L, -1)) {
            const int top = lua_gettop(L), pad = missionLuaTgtPad(L, top), prop = missionLuaTgtProp(L, top);
            if (pad >= 0) { kind = MHW_FROM_PAD; index = pad; }
            else if (prop >= 0) { kind = MHW_FROM_PROP; index = prop; }
        }
        lua_pop(L, 1);
    }
    if (!w || !w->sound_play) { stub(L, "sound.play"); lua_pushnumber(L, 0); return 1; }
    lua_pushnumber(L, w->sound_play(w->ud, id, channel, kind, index));
    return 1;
}

int snd_stop(lua_State *L)
{
    const MissionHostWorld *w = W(L);
    const int ch = (int)luaL_checkinteger(L, 1);
    if (!w || !w->sound_stop) { stub(L, "sound.stop"); return 0; }
    w->sound_stop(w->ud, ch);
    return 0;
}

int snd_volume(lua_State *L)
{
    const MissionHostWorld *w = W(L);
    const int ch = (int)luaL_checkinteger(L, 1);
    double v = luaL_checknumber(L, 2);
    const double fade = luaL_optnumber(L, 3, 0.0);
    if (!w || !w->sound_volume) { stub(L, "sound.volume"); return 0; }
    if (v < 0) v = 0;
    if (v > 1) v = 1;
    w->sound_volume(w->ud, ch, v, fade < 0 ? 0 : fade);
    return 0;
}

// music
int mus_play(lua_State *L)
{
    const MissionHostWorld *w = W(L);
    const int slot = (int)luaL_checkinteger(L, 1);
    int stop_after = 0, total = 0;
    double fade = 0;
    if (lua_istable(L, 2)) {
        lua_getfield(L, 2, "stop_after"); if (lua_isnumber(L, -1)) stop_after = (int)lua_tonumber(L, -1); lua_pop(L, 1);
        lua_getfield(L, 2, "total");      if (lua_isnumber(L, -1)) total = (int)lua_tonumber(L, -1);      lua_pop(L, 1);
        lua_getfield(L, 2, "fade");       if (lua_isnumber(L, -1)) fade = lua_tonumber(L, -1);            lua_pop(L, 1);
    }
    if (!w || !w->music_play) { stub(L, "music.play_extra"); return 0; }
    w->music_play(w->ud, slot, stop_after, total, fade);
    return 0;
}

int mus_stop(lua_State *L)
{
    const MissionHostWorld *w = W(L);
    const int slot = (int)luaL_checkinteger(L, 1);
    if (!w || !w->music_stop) { stub(L, "music.stop_extra"); return 0; }
    w->music_stop(w->ud, slot);
    return 0;
}

int fog_next(lua_State *L)
{
    const MissionHostWorld *w = W(L);
    const int slow = lua_toboolean(L, 1);
    if (!w || !w->fog_next) { stub(L, "fog.next"); return 0; }
    w->fog_next(w->ud, slow);
    return 0;
}

// alarm.is_on / alarm.stop, mission.finish / mission.exiting
int alarm_is_on(lua_State *L)
{
    const MissionHostWorld *w = W(L);
    if (!w || !w->alarm_is_on) { stub(L, "alarm.is_on"); lua_pushboolean(L, 0); return 1; }
    lua_pushboolean(L, w->alarm_is_on(w->ud) != 0);
    return 1;
}

int alarm_stop(lua_State *L)
{
    const MissionHostWorld *w = W(L);
    if (!w || !w->alarm_stop) { stub(L, "alarm.stop"); return 0; }
    w->alarm_stop(w->ud);
    return 0;
}

// mission.finish(): the level ends now, won when every objective of the difficulty is complete and none failed (the
// game's own end-of-level rule, objectiveIsAllComplete), else lost. The runtime's own mission.win / mission.fail host
// callbacks do the ending (they end the level once).
int mis_finish(lua_State *L)
{
    const MissionHost *h = missionLuaHost(L);
    MissionRuntime *rt = static_cast<MissionRuntime *>(lua_callbacks(L)->userdata);
    const int diff = h->difficulty ? h->difficulty(h->ud) : 99;
    bool won = true;
    for (int i = 0, n = missionRtObjectiveCount(rt); i < n; i++)
        if (missionRtObjectiveDifficulty(rt, i) <= diff && missionRtObjectiveStatus(rt, i) != 1) won = false;   // OBJECTIVESTATUS_COMPLETE
    missionLuaLog(L, "mission.finish(): %s", won ? "won" : "lost");
    if (won) { if (h->win) h->win(h->ud); }
    else if (h->fail) h->fail(h->ud);
    return 0;
}

int mis_players(lua_State *L)
{
    const MissionHost *h = missionLuaHost(L);
    int n = h->player_count ? h->player_count(h->ud) : 1;
    lua_pushnumber(L, n < 1 ? 1 : n);
    return 1;
}

// mission.killed_in_action(): the game's KIA mark (the debrief says "killed in action"), then the level is lost.
int mis_kia(lua_State *L)
{
    const MissionHost *h = missionLuaHost(L);
    const MissionHostWorld *w = W(L);
    if (!w || !w->killed_in_action) { stub(L, "mission.killed_in_action"); return 0; }
    w->killed_in_action(w->ud);
    missionLuaLog(L, "mission.killed_in_action()");
    if (h->fail) h->fail(h->ud);
    return 0;
}

int mis_exiting(lua_State *L)
{
    const MissionHostWorld *w = W(L);
    if (!w || !w->exiting) { stub(L, "mission.exiting"); lua_pushboolean(L, 0); return 1; }
    lua_pushboolean(L, w->exiting(w->ud) != 0);
    return 1;
}

// ------------------------------------------------------------------ objectives
// upvalues: 1 objectives.fail, 2 objectives.reset, 3 objectives.is_failed
int obj_set_failed(lua_State *L)
{
    luaL_checkstring(L, 1);
    const int on = lua_toboolean(L, 2);
    if (on) {
        lua_pushvalue(L, lua_upvalueindex(1));
        lua_pushvalue(L, 1);
        lua_call(L, 1, 0);
        return 0;
    }
    lua_pushvalue(L, lua_upvalueindex(3));
    lua_pushvalue(L, 1);
    lua_call(L, 1, 1);
    const bool failed = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    if (failed) {   // only a failed objective: a completed one cannot be failed at the same time, so reset lifts just the failure
        lua_pushvalue(L, lua_upvalueindex(2));
        lua_pushvalue(L, 1);
        lua_call(L, 1, 0);
    }
    return 0;
}

// upvalues: 1 objectives.fail. The mission's own objectives come from the host (the runtime keeps their ids to itself),
// the ones scripts added from the wrapper of objectives.add below.
int obj_fail_all(lua_State *L)
{
    const MissionHost *h = missionLuaHost(L);
    const MissionHostWorld *w = W(L);
    const int diff = h->difficulty ? h->difficulty(h->ud) : 99;
    if (w && w->objective_id) {
        char id[64];
        int d = 0;
        for (int i = 0; i < 256 && w->objective_id(w->ud, i, id, sizeof id, &d); i++) {
            if (d > diff) continue;
            lua_pushvalue(L, lua_upvalueindex(1));
            lua_pushstring(L, id);
            lua_call(L, 1, 0);
        }
    } else {
        stub(L, "objectives.fail_all (the mission's own objectives)");
    }
    lua_getfield(L, LUA_REGISTRYINDEX, kObjAdded);
    if (lua_istable(L, -1)) {
        lua_pushnil(L);
        while (lua_next(L, -2) != 0) {   // key = id, value = difficulty
            if (lua_isstring(L, -2) && (int)lua_tonumber(L, -1) <= diff) {
                lua_pushvalue(L, lua_upvalueindex(1));
                lua_pushvalue(L, -3);
                lua_call(L, 1, 0);
            }
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);
    return 0;
}

// objectives.add wrapper (upvalue 1: the runtime's add): the same call, and the id is remembered for fail_all.
int obj_add(lua_State *L)
{
    const int n = lua_gettop(L);
    const char *id = luaL_checkstring(L, 1);
    const int diff = (int)luaL_optinteger(L, 3, 0);
    lua_pushvalue(L, lua_upvalueindex(1));
    for (int i = 1; i <= n; i++) lua_pushvalue(L, i);
    lua_call(L, n, 0);
    lua_getfield(L, LUA_REGISTRYINDEX, kObjAdded);
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setfield(L, LUA_REGISTRYINDEX, kObjAdded);
    }
    lua_getfield(L, -1, id);
    if (lua_isnil(L, -1)) { lua_pushnumber(L, diff); lua_setfield(L, -3, id); }
    lua_pop(L, 2);
    return 0;
}

}  // namespace

void missionLuaRegWorld(lua_State *L, int targetMT)
{
    struct Fn { const char *name; lua_CFunction f; };

    // Target methods. in_room_of and set_invincible wrap the guard domain's, when it registered one.
    for (const Fn &f : { Fn{ "in_room_of", t_in_room_of }, Fn{ "set_invincible", t_set_invincible },
                         Fn{ "set_hidden", t_set_hidden }, Fn{ "anim", t_anim }, Fn{ "health", t_health } }) {
        lua_getfield(L, targetMT, f.name);   // the earlier function or nil: the closure's upvalue
        lua_pushcclosure(L, f.f, f.name, 1);
        lua_setfield(L, targetMT, f.name);
    }
    for (const Fn &f : { Fn{ "is_destroyed", t_is_destroyed }, Fn{ "destroy", t_destroy }, Fn{ "explode", t_explode },
                         Fn{ "set_collectable", t_set_collectable }, Fn{ "move_to", t_move_to }, Fn{ "activate", t_activate },
                         Fn{ "show_screen", t_show_screen }, Fn{ "held_by", t_held_by }, Fn{ "set_rotor", t_set_rotor },
                         Fn{ "drive", t_drive }, Fn{ "state", t_state }, Fn{ "open_now", t_open_now },
                         Fn{ "set_passable", t_set_passable }, Fn{ "armour", t_armour }, Fn{ "held_item", t_held_item },
                         Fn{ "holds", t_holds }, Fn{ "force_weapon", t_force_weapon }, Fn{ "push", t_push },
                         Fn{ "set_active", t_set_active }, Fn{ "set_damage", t_set_damage }, Fn{ "same_room_as", t_same_room_as },
                         Fn{ "set_usable_by_guards", t_set_usable_by_guards }, Fn{ "kill", t_kill },
                         Fn{ "facing", t_facing }, Fn{ "eye", t_eye } }) {
        lua_pushcfunction(L, f.f, f.name);
        lua_setfield(L, targetMT, f.name);
    }

    // globals
    for (const Fn &f : { Fn{ "pad", l_pad }, Fn{ "player_in_room", l_player_in_room }, Fn{ "item_landed", l_item_landed },
                         Fn{ "give_object", l_give_object }, Fn{ "item_in_room", l_item_in_room },
                         Fn{ "explosions_around_players", l_explosions_around_players } }) {
        lua_pushcfunction(L, f.f, f.name);
        lua_setglobal(L, f.name);
    }
    lua_getglobal(L, "give");   // wraps the runtime's give (a third table argument is accepted)
    if (lua_isfunction(L, -1)) {
        lua_pushcclosure(L, l_give_wrap, "give", 1);
        lua_setglobal(L, "give");
    } else {
        lua_pop(L, 1);
    }
    auto table = [&](const char *name, std::initializer_list<Fn> fns) {
        lua_createtable(L, 0, (int)fns.size());
        for (const Fn &f : fns) {
            lua_pushcfunction(L, f.f, f.name);
            lua_setfield(L, -2, f.name);
        }
        lua_setglobal(L, name);
    };
    table("hud", { Fn{ "top", hud_top }, Fn{ "bottom", hud_bottom }, Fn{ "hide", hud_hide } });
    table("countdown", { Fn{ "set", cd_set }, Fn{ "start", cd_start }, Fn{ "stop", cd_stop }, Fn{ "show", cd_show }, Fn{ "left", cd_left } });
    table("sound", { Fn{ "play", snd_play }, Fn{ "stop", snd_stop }, Fn{ "volume", snd_volume } });
    table("music", { Fn{ "play_extra", mus_play }, Fn{ "stop_extra", mus_stop } });
    table("fog", { Fn{ "next", fog_next } });

    // fields added to the runtime's own tables (they exist by now; the sandbox freezes them after registration)
    const struct { const char *table; Fn fns[4]; int n; } adds[] = {
        { "alarm", { Fn{ "is_on", alarm_is_on }, Fn{ "stop", alarm_stop } }, 2 },
        { "mission", { Fn{ "finish", mis_finish }, Fn{ "exiting", mis_exiting }, Fn{ "players", mis_players }, Fn{ "killed_in_action", mis_kia } }, 4 },
    };
    for (const auto &a : adds) {
        lua_getglobal(L, a.table);
        if (!lua_istable(L, -1)) { lua_pop(L, 1); continue; }
        for (int i = 0; i < a.n; i++) {
            lua_pushcfunction(L, a.fns[i].f, a.fns[i].name);
            lua_setfield(L, -2, a.fns[i].name);
        }
        lua_pop(L, 1);
    }

    lua_getglobal(L, "objectives");
    if (lua_istable(L, -1)) {
        const int ot = lua_gettop(L);
        lua_getfield(L, ot, "fail");
        lua_getfield(L, ot, "reset");
        lua_getfield(L, ot, "is_failed");
        lua_pushcclosure(L, obj_set_failed, "set_failed", 3);
        lua_setfield(L, ot, "set_failed");
        lua_getfield(L, ot, "fail");
        lua_pushcclosure(L, obj_fail_all, "fail_all", 1);
        lua_setfield(L, ot, "fail_all");
        lua_getfield(L, ot, "add");
        lua_pushcclosure(L, obj_add, "add", 1);
        lua_setfield(L, ot, "add");
    }
    lua_settop(L, targetMT);
}
