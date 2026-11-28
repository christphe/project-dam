// The guard domain of the campaign API (#165): Luau bindings. Layout: mission_lua_ext.h; script contract:
// lib/mission.d.luau; host callbacks: mission_host_guard.h (implemented in port/src/missions_host_guard.c).
//
// A guard is addressed by chr number (missionLuaTgtChr: the target's .chr, else its setup prop's chr), so setup guards
// and spawned / cloned ones work alike. A target that is no guard gives the method's neutral value (false, nil, 0),
// as the older prop methods do for a missing prop; a host without the callback logs "<name>: not implemented yet" once.
#include "mission_lua_ext.h"
#include "mission_host_guard.h"

#include "lua.h"
#include "lualib.h"

#include <initializer_list>
#include <string.h>

namespace {

const MissionHostGuard *gh(lua_State *L) { return missionLuaHost(L)->guard_ext; }

// "<name>: not implemented yet", once per name and VM.
void notImplemented(lua_State *L, const char *name)
{
    lua_getfield(L, LUA_REGISTRYINDEX, "ge.guard.nolog");
    if (!lua_istable(L, -1)) { lua_pop(L, 1); return; }
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

// The chr number a method works on, or -1 when there is nothing to do (no such guard, or no host callback: logged once).
template <class F> int enter(lua_State *L, const char *name, F cb)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    if (!cb) { notImplemented(L, name); return -1; }
    return missionLuaTgtChr(L, 1);
}

// A pad argument (number, mission pad id or pad target): missionLuaPadArg. -1: none.
int padAt(lua_State *L, int idx) { return missionLuaPadArg(L, idx); }

int speedAt(lua_State *L, int idx)
{
    if (lua_isnoneornil(L, idx)) return MG_RUN;
    const char *s = luaL_checkstring(L, idx);
    if (!strcmp(s, "walk")) return MG_WALK;
    if (!strcmp(s, "run")) return MG_RUN;
    if (!strcmp(s, "sprint")) return MG_SPRINT;
    luaL_error(L, "speed must be \"walk\", \"run\" or \"sprint\"");
    return MG_RUN;
}

int playerAt(lua_State *L, int idx) { return lua_isnoneornil(L, idx) ? -1 : (int)luaL_checkinteger(L, idx); }

bool optNum(lua_State *L, int tbl, const char *key, double *out)
{
    lua_getfield(L, tbl, key);
    const bool ok = lua_type(L, -1) == LUA_TNUMBER;
    if (ok) *out = lua_tonumber(L, -1);
    lua_pop(L, 1);
    return ok;
}

// A boolean field: -1 absent, else 0 / 1.
int optBool(lua_State *L, int tbl, const char *key)
{
    lua_getfield(L, tbl, key);
    const int r = lua_type(L, -1) == LUA_TBOOLEAN ? (lua_toboolean(L, -1) ? 1 : 0) : -1;
    lua_pop(L, 1);
    return r;
}

// A target for the guard with chr number `chr` (its setup prop when it has one).
void pushGuard(lua_State *L, int chr)
{
    const MissionHost *h = missionLuaHost(L);
    int prop = h->chr_prop ? h->chr_prop(h->ud, chr) : -1;
    missionLuaPushTarget(L, "prop", prop, chr, -1);
}

// ------------------------------------------------------------ presence
int t_exists(lua_State *L)
{
    const MissionHostGuard *g = gh(L);
    int c = enter(L, "exists", g ? g->exists : nullptr);
    lua_pushboolean(L, c >= 0 && g->exists(g->ud, c));
    return 1;
}
int t_is_dying(lua_State *L)
{
    const MissionHostGuard *g = gh(L);
    int c = enter(L, "is_dying", g ? g->is_dying : nullptr);
    lua_pushboolean(L, c >= 0 && g->is_dying(g->ud, c));
    return 1;
}
int t_remove(lua_State *L)
{
    const MissionHostGuard *g = gh(L);
    int c = enter(L, "remove", g ? g->chr_remove : nullptr);
    if (c >= 0) g->chr_remove(g->ud, c);
    return 0;
}
int t_fade_out(lua_State *L)
{
    const MissionHostGuard *g = gh(L);
    int c = enter(L, "fade_out", g ? g->fade_out : nullptr);
    if (c >= 0) g->fade_out(g->ud, c);
    return 0;
}
int t_teleport(lua_State *L)
{
    const MissionHostGuard *g = gh(L);
    int c = enter(L, "teleport", g ? g->teleport : nullptr);
    int pad = padAt(L, 2);
    lua_pushboolean(L, c >= 0 && pad >= 0 && g->teleport(g->ud, c, pad));
    return 1;
}
int t_set_hidden(lua_State *L)
{
    const MissionHostGuard *g = gh(L);
    int c = enter(L, "set_hidden", g ? g->set_hidden : nullptr);
    if (c >= 0) g->set_hidden(g->ud, c, lua_toboolean(L, 2));
    return 0;
}
// set_ai by chr number: the older t_set_ai works on a setup prop only, so a spawned guard (no prop) goes to the host here.
int t_set_ai(lua_State *L)
{
    const MissionHostGuard *g = gh(L);
    luaL_checktype(L, 1, LUA_TTABLE);
    int list = (int)luaL_checkinteger(L, 2);
    int c = g && g->set_ai ? missionLuaTgtChr(L, 1) : -1;
    if (c >= 0 && g->set_ai(g->ud, c, list)) return 0;
    if (missionLuaTgtProp(L, 1) < 0) return 0;  // a chr the game does not have and no setup prop: nothing to do
    lua_pushvalue(L, lua_upvalueindex(1));      // the setup-prop version, for what has no chr
    lua_pushvalue(L, 1);
    lua_pushvalue(L, 2);
    lua_call(L, 2, 0);
    return 0;
}
// is_moving / was_shot_at: the older methods work on a setup prop; a guard with a chr number goes to the host here
// (a spawned guard has no prop; the chr-based check also counts run_to / run_to_player / patrol). The older version
// stays as the upvalue for a target with no chr.
int t_is_moving(lua_State *L)
{
    const MissionHostGuard *g = gh(L);
    luaL_checktype(L, 1, LUA_TTABLE);
    int c = g && g->is_moving ? missionLuaTgtChr(L, 1) : -1;
    if (c >= 0) { lua_pushboolean(L, g->is_moving(g->ud, c)); return 1; }
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_pushvalue(L, 1);
    lua_call(L, 1, 1);
    return 1;
}
int t_was_shot_at(lua_State *L)
{
    const MissionHostGuard *g = gh(L);
    luaL_checktype(L, 1, LUA_TTABLE);
    int c = g && g->was_shot_at ? missionLuaTgtChr(L, 1) : -1;
    if (c >= 0) { lua_pushboolean(L, g->was_shot_at(g->ud, c)); return 1; }
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_pushvalue(L, 1);
    lua_call(L, 1, 1);
    return 1;
}
int t_setup_ai(lua_State *L)
{
    const MissionHostGuard *g = gh(L);
    int c = enter(L, "setup_ai", g ? g->setup_ai : nullptr);
    int v = c >= 0 ? g->setup_ai(g->ud, c) : -1;
    if (v < 0) lua_pushnil(L); else lua_pushnumber(L, v);
    return 1;
}

// ------------------------------------------------------------ settings
int t_set_stats(lua_State *L)
{
    const MissionHostGuard *g = gh(L);
    int c = enter(L, "set_stats", g ? g->set_stats : nullptr);
    luaL_checktype(L, 2, LUA_TTABLE);
    if (c < 0) return 0;
    MissionGuardStats st;
    memset(&st, 0, sizeof st);
    struct F { const char *key; unsigned bit; double *dst; };
    const F fields[] = { { "health", MG_ST_HEALTH, &st.health }, { "max_health", MG_ST_MAX_HEALTH, &st.max_health },
                         { "armour", MG_ST_ARMOUR, &st.armour }, { "speed", MG_ST_SPEED, &st.speed },
                         { "accuracy", MG_ST_ACCURACY, &st.accuracy }, { "reaction", MG_ST_REACTION, &st.reaction },
                         { "grenade_chance", MG_ST_GRENADE, &st.grenade_chance }, { "add_armour", MG_ST_ADD_ARMOUR, &st.add_armour },
                         { "hearing", MG_ST_HEARING, &st.hearing }, { "vision", MG_ST_VISION, &st.vision },
                         { "recovery", MG_ST_RECOVERY, &st.recovery } };
    for (const F &f : fields)
        if (optNum(L, 2, f.key, f.dst)) st.has |= f.bit;
    lua_getfield(L, 2, "flinch");   // false / true, or the chance 0..100
    if (lua_type(L, -1) == LUA_TBOOLEAN) { st.has |= MG_ST_FLINCH; st.flinch = lua_toboolean(L, -1) ? 100 : 0; }
    else if (lua_type(L, -1) == LUA_TNUMBER) {
        const double v = lua_tonumber(L, -1);
        st.has |= MG_ST_FLINCH;
        st.flinch = v < 0 ? 0 : v > 100 ? 100 : (int)(v + 0.5);
    }
    lua_pop(L, 1);
    st.raw = optBool(L, 2, "raw") == 1;
    g->set_stats(g->ud, c, &st);
    return 0;
}

const char *const kFlags[MG_FLAG_COUNT] = { "invincible", "no_autoaim", "can_shoot_guards", "civilian", "dormant",
                                            "no_blood", "fast_sprint", "no_shadow", "sunglasses", "clone_sender" };

int flagAt(lua_State *L, int idx)
{
    const char *s = luaL_checkstring(L, idx);
    for (int i = 0; i < MG_FLAG_COUNT; i++)
        if (!strcmp(s, kFlags[i])) return i;
    luaL_error(L, "unknown guard flag \"%s\"", s);
    return -1;
}

int t_set_flag(lua_State *L)
{
    const MissionHostGuard *g = gh(L);
    int c = enter(L, "set_flag", g ? g->set_flag : nullptr);
    int flag = flagAt(L, 2);
    if (c >= 0) g->set_flag(g->ud, c, flag, lua_toboolean(L, 3));
    return 0;
}
int t_get_flag(lua_State *L)
{
    const MissionHostGuard *g = gh(L);
    int c = enter(L, "get_flag", g ? g->get_flag : nullptr);
    int flag = flagAt(L, 2);
    lua_pushboolean(L, c >= 0 && g->get_flag(g->ud, c, flag));
    return 1;
}
// set_invincible(on) on a guard: the "invincible" flag. The world domain wraps this for objects and players.
int t_set_invincible(lua_State *L)
{
    const MissionHostGuard *g = gh(L);
    int c = enter(L, "set_invincible", g ? g->set_flag : nullptr);
    if (c >= 0) g->set_flag(g->ud, c, MG_FLAG_INVINCIBLE, lua_toboolean(L, 2));
    return 0;
}
int t_give_weapon(lua_State *L)
{
    const MissionHostGuard *g = gh(L);
    int c = enter(L, "give_weapon", g ? g->give_weapon : nullptr);
    int item = (int)luaL_checkinteger(L, 2);
    int model = -1, left = 0, concealed = 0;
    if (lua_istable(L, 3)) {
        double v;
        if (optNum(L, 3, "model", &v)) model = (int)v;
        left = optBool(L, 3, "left_hand") == 1;
        concealed = optBool(L, 3, "concealed") == 1;
    }
    lua_pushboolean(L, c >= 0 && g->give_weapon(g->ud, c, item, model, left, concealed));
    return 1;
}
int t_give_hat(lua_State *L)
{
    const MissionHostGuard *g = gh(L);
    int c = enter(L, "give_hat", g ? g->give_hat : nullptr);
    int model = (int)luaL_checkinteger(L, 2);
    lua_pushboolean(L, c >= 0 && g->give_hat(g->ud, c, model));
    return 1;
}
int t_drop_items(lua_State *L)
{
    const MissionHostGuard *g = gh(L);
    int c = enter(L, "drop_items", g ? g->drop_items : nullptr);
    if (c >= 0) g->drop_items(g->ud, c);
    return 0;
}
int t_remove_held_items(lua_State *L)
{
    const MissionHostGuard *g = gh(L);
    int c = enter(L, "remove_held_items", g ? g->remove_held_items : nullptr);
    if (c >= 0) g->remove_held_items(g->ud, c);
    return 0;
}
int pushPad(lua_State *L, int pad)
{
    if (pad < 0) lua_pushnil(L); else lua_pushnumber(L, pad);
    return 1;
}
int t_preset_pad(lua_State *L)
{
    const MissionHostGuard *g = gh(L);
    int c = enter(L, "preset_pad", g ? g->preset_pad : nullptr);
    return pushPad(L, c >= 0 ? g->preset_pad(g->ud, c) : -1);
}

// ------------------------------------------------------------ movement
int t_patrol(lua_State *L)
{
    const MissionHostGuard *g = gh(L);
    int c = enter(L, "patrol", g ? g->patrol : nullptr);
    int path = (int)luaL_checkinteger(L, 2);
    if (c >= 0) g->patrol(g->ud, c, path);
    return 0;
}
int t_is_patrolling(lua_State *L)
{
    const MissionHostGuard *g = gh(L);
    int c = enter(L, "is_patrolling", g ? g->is_patrolling : nullptr);
    lua_pushboolean(L, c >= 0 && g->is_patrolling(g->ud, c));
    return 1;
}
int t_run_to(lua_State *L)
{
    const MissionHostGuard *g = gh(L);
    int c = enter(L, "run_to", g ? g->run_to : nullptr);
    int pad = padAt(L, 2);
    int speed = speedAt(L, 3);
    lua_pushboolean(L, c >= 0 && pad >= 0 && g->run_to(g->ud, c, pad, speed));
    return 1;
}
int t_run_to_player(lua_State *L)
{
    const MissionHostGuard *g = gh(L);
    int c = enter(L, "run_to_player", g ? g->run_to_player : nullptr);
    int speed = speedAt(L, 2);
    int player = playerAt(L, 3);
    lua_pushboolean(L, c >= 0 && g->run_to_player(g->ud, c, speed, player));
    return 1;
}
int t_face_player(lua_State *L)
{
    const MissionHostGuard *g = gh(L);
    int c = enter(L, "face_player", g ? g->face_player : nullptr);
    if (c >= 0) g->face_player(g->ud, c, -1);
    return 0;
}
int t_face_pad(lua_State *L)
{
    const MissionHostGuard *g = gh(L);
    int c = enter(L, "face_pad", g ? g->face_pad : nullptr);
    int pad = padAt(L, 2);
    if (c >= 0 && pad >= 0) g->face_pad(g->ud, c, pad);
    return 0;
}
int t_halt(lua_State *L)
{
    const MissionHostGuard *g = gh(L);
    int c = enter(L, "halt", g ? g->halt : nullptr);
    if (c >= 0) g->halt(g->ud, c);
    return 0;
}
int t_flee_pad(lua_State *L)
{
    const MissionHostGuard *g = gh(L);
    int c = enter(L, "flee_pad", g ? g->flee_pad : nullptr);
    return pushPad(L, c >= 0 ? g->flee_pad(g->ud, c) : -1);
}
int t_cover_pad(lua_State *L)
{
    const MissionHostGuard *g = gh(L);
    int c = enter(L, "cover_pad", g ? g->cover_pad : nullptr);
    return pushPad(L, c >= 0 ? g->cover_pad(g->ud, c) : -1);
}

// ------------------------------------------------------------ gestures and animation
#define VOID_METHOD(fname, cb)                                                         \
    int fname(lua_State *L)                                                            \
    {                                                                                  \
        const MissionHostGuard *g = gh(L);                                             \
        int c = enter(L, #cb, g ? g->cb : nullptr);                                    \
        if (c >= 0) g->cb(g->ud, c);                                                   \
        return 0;                                                                      \
    }
VOID_METHOD(t_look_around, look_around)
VOID_METHOD(t_point_at_player, point_at_player)
VOID_METHOD(t_raise_arms, raise_arms)
VOID_METHOD(t_kneel, kneel)
VOID_METHOD(t_idle_anim, idle_anim)
VOID_METHOD(t_stop_anim, stop_anim)

int t_anim(lua_State *L)
{
    const MissionHostGuard *g = gh(L);
    int c = enter(L, "anim", g ? g->anim : nullptr);
    int id = -1;
    const char *name = nullptr;
    if (lua_type(L, 2) == LUA_TNUMBER) id = (int)lua_tonumber(L, 2);
    else name = luaL_checkstring(L, 2);
    MissionGuardAnim o;
    o.from = -1; o.to = -1; o.speed = 0; o.hold = o.reverse = o.loop = o.no_move = 0; o.blend = -1;
    o.idle_after = -1; o.sound = -1; o.interruptible = 0;
    if (lua_istable(L, 3)) {
        double v;
        if (optNum(L, 3, "from", &v)) o.from = (int)v;
        if (optNum(L, 3, "to", &v)) o.to = (int)v;
        if (optNum(L, 3, "speed", &v)) o.speed = v;
        if (optNum(L, 3, "blend", &v)) o.blend = v;
        o.hold = optBool(L, 3, "hold") == 1;
        o.reverse = optBool(L, 3, "reverse") == 1;
        o.loop = optBool(L, 3, "loop") == 1;
        o.no_move = optBool(L, 3, "move") == 0;
        o.idle_after = optBool(L, 3, "idle_after");
        if (optNum(L, 3, "sound", &v)) o.sound = (int)v;
        o.interruptible = optBool(L, 3, "interruptible") == 1;
    }
    lua_pushboolean(L, c >= 0 && g->anim(g->ud, c, id, name, &o));
    return 1;
}
int t_anim_done(lua_State *L)
{
    const MissionHostGuard *g = gh(L);
    int c = enter(L, "anim_done", g ? g->anim_done : nullptr);
    lua_pushboolean(L, c >= 0 && g->anim_done(g->ud, c));
    return 1;
}

// ------------------------------------------------------------ combat
int t_fire_at(lua_State *L)
{
    const MissionHostGuard *g = gh(L);
    int c = enter(L, "fire_at", g ? g->fire_at : nullptr);
    int kind = MG_TGT_PLAYER, target = -1;
    if (!lua_isnoneornil(L, 2)) {
        luaL_checktype(L, 2, LUA_TTABLE);
        int p = missionLuaTgtPlayer(L, 2);
        if (p >= 0) { kind = MG_TGT_PLAYER; target = p; }
        else {
            lua_getfield(L, 2, "kind");
            const char *k = lua_type(L, -1) == LUA_TSTRING ? lua_tostring(L, -1) : "";
            const bool isPad = !strcmp(k, "pad"), isBody = !strcmp(k, "body");
            lua_pop(L, 1);
            if (isBody) {   // bond_body(p): aim at that player's chr
                lua_getfield(L, 2, "player");
                kind = MG_TGT_BODY;
                target = lua_type(L, -1) == LUA_TNUMBER ? (int)lua_tonumber(L, -1) : -1;
                lua_pop(L, 1);
            }
            else if (isPad) { kind = MG_TGT_PAD; target = padAt(L, 2); }
            else { kind = MG_TGT_CHR; target = missionLuaTgtChr(L, 2); }
            if (target < 0) { lua_pushboolean(L, 0); return 1; }
        }
    }
    int kneel = 0, aim = 0;
    if (lua_istable(L, 3)) {
        lua_getfield(L, 3, "stance");
        kneel = lua_type(L, -1) == LUA_TSTRING && !strcmp(lua_tostring(L, -1), "kneel");
        lua_pop(L, 1);
        aim = optBool(L, 3, "aim_only") == 1;
    }
    lua_pushboolean(L, c >= 0 && g->fire_at(g->ud, c, kind, target, kneel, aim));
    return 1;
}
int t_combat_move(lua_State *L)
{
    const MissionHostGuard *g = gh(L);
    int c = enter(L, "combat_move", g ? g->combat_move : nullptr);
    const char *k = luaL_checkstring(L, 2);
    static const char *const names[] = { "roll_fire", "walk_fire", "run_fire", "sidestep", "side_hop", "side_run" };
    int move = -1;
    for (int i = 0; i < 6; i++) if (!strcmp(k, names[i])) move = i;
    if (move < 0) luaL_error(L, "unknown combat move \"%s\"", k);
    lua_pushboolean(L, c >= 0 && g->combat_move(g->ud, c, move, -1));
    return 1;
}
int t_throw_grenade(lua_State *L)
{
    const MissionHostGuard *g = gh(L);
    int c = enter(L, "throw_grenade", g ? g->throw_grenade : nullptr);
    lua_pushboolean(L, c >= 0 && g->throw_grenade(g->ud, c, -1));
    return 1;
}
int t_grenade_chance(lua_State *L)
{
    const MissionHostGuard *g = gh(L);
    int c = enter(L, "grenade_chance", g ? g->grenade_chance : nullptr);
    lua_pushnumber(L, c >= 0 ? g->grenade_chance(g->ud, c) : 0);
    return 1;
}
int t_hit(lua_State *L)
{
    const MissionHostGuard *g = gh(L);
    int c = enter(L, "hit", g ? g->hit : nullptr);
    int part = (int)luaL_checkinteger(L, 2), item = (int)luaL_checkinteger(L, 3);
    if (c >= 0) g->hit(g->ud, c, part, item);
    return 0;
}
int t_is_busy(lua_State *L)
{
    const MissionHostGuard *g = gh(L);
    int c = enter(L, "is_busy", g ? g->is_busy : nullptr);
    lua_pushboolean(L, c >= 0 && g->is_busy(g->ud, c));
    return 1;
}
int t_is_idle(lua_State *L)
{
    const MissionHostGuard *g = gh(L);
    int c = enter(L, "is_idle", g ? g->is_idle : nullptr);
    lua_pushboolean(L, c >= 0 && g->is_idle(g->ud, c));
    return 1;
}
int t_clone(lua_State *L)
{
    const MissionHostGuard *g = gh(L);
    int c = enter(L, "clone", g ? g->clone : nullptr);
    int n = c >= 0 ? g->clone(g->ud, c) : -1;
    if (n < 0) lua_pushnil(L); else pushGuard(L, n);
    return 1;
}
int t_press_alarm(lua_State *L)
{
    const MissionHostGuard *g = gh(L);
    int c = enter(L, "press_alarm", g ? g->press_alarm : nullptr);
    int pad = padAt(L, 2);
    lua_pushboolean(L, c >= 0 && pad >= 0 && g->press_alarm(g->ud, c, pad));
    return 1;
}

// ------------------------------------------------------------ perception
int pushPlayer(lua_State *L, int p)
{
    if (p < 0) lua_pushnil(L); else lua_pushnumber(L, p);
    return 1;
}
int t_spotted_player(lua_State *L)
{
    const MissionHostGuard *g = gh(L);
    int c = enter(L, "spotted_player", g ? g->spotted_player : nullptr);
    return pushPlayer(L, c >= 0 ? g->spotted_player(g->ud, c) : -1);
}
int t_line_to_player(lua_State *L)
{
    const MissionHostGuard *g = gh(L);
    int c = enter(L, "line_to_player", g ? g->line_to_player : nullptr);
    return pushPlayer(L, c >= 0 ? g->line_to_player(g->ud, c) : -1);
}
#define BOOL_METHOD(fname, cb)                                                         \
    int fname(lua_State *L)                                                            \
    {                                                                                  \
        const MissionHostGuard *g = gh(L);                                             \
        int c = enter(L, #cb, g ? g->cb : nullptr);                                    \
        lua_pushboolean(L, c >= 0 && g->cb(g->ud, c));                                 \
        return 1;                                                                      \
    }
BOOL_METHOD(t_on_screen, on_screen)
BOOL_METHOD(t_was_seen, was_seen)
BOOL_METHOD(t_is_aimed_at, is_aimed_at)
BOOL_METHOD(t_was_damaged, was_damaged)

int t_alerted_within(lua_State *L)
{
    const MissionHostGuard *g = gh(L);
    int c = enter(L, "alerted_within", g ? g->alerted_within : nullptr);
    double s = luaL_checknumber(L, 2);
    lua_pushboolean(L, c >= 0 && g->alerted_within(g->ud, c, s));
    return 1;
}
int t_heard_shot_within(lua_State *L)
{
    const MissionHostGuard *g = gh(L);
    int c = enter(L, "heard_shot_within", g ? g->heard_shot_within : nullptr);
    double s = luaL_checknumber(L, 2);
    lua_pushboolean(L, c >= 0 && g->heard_shot_within(g->ud, c, s));
    return 1;
}
int t_near_misses(lua_State *L)
{
    const MissionHostGuard *g = gh(L);
    int c = enter(L, "near_misses", g ? g->near_misses : nullptr);
    lua_pushnumber(L, c >= 0 ? g->near_misses(g->ud, c) : 0);
    return 1;
}
int t_witnessed(lua_State *L)
{
    const MissionHostGuard *g = gh(L);
    int c = enter(L, "witnessed", g ? g->witnessed : nullptr);
    int other = -1;
    int what = c >= 0 ? g->witnessed(g->ud, c, &other) : MG_SAW_NONE;
    if (what == MG_SAW_NONE) { lua_pushnil(L); lua_pushnil(L); return 2; }
    lua_pushstring(L, what == MG_SAW_DEATH ? "death" : "hit");
    if (other >= 0) pushGuard(L, other); else lua_pushnil(L);
    return 2;
}
int t_angle_to_player(lua_State *L)
{
    const MissionHostGuard *g = gh(L);
    int c = enter(L, "angle_to_player", g ? g->angle_to_player : nullptr);
    lua_pushnumber(L, c >= 0 ? g->angle_to_player(g->ud, c, -1) : 0.0);
    return 1;
}

// ------------------------------------------------------------ second pass
int t_held_weapon(lua_State *L)
{
    const MissionHostGuard *g = gh(L);
    int c = enter(L, "held_weapon", g ? g->held_weapon : nullptr);
    int item = c >= 0 ? g->held_weapon(g->ud, c) : -1;
    if (item < 0) lua_pushnil(L); else lua_pushnumber(L, item);
    return 1;
}
int t_spot_near_player(lua_State *L)
{
    const MissionHostGuard *g = gh(L);
    int c = enter(L, "spot_near_player", g ? g->spot_near_player : nullptr);
    int player = playerAt(L, 2);
    return pushPad(L, c >= 0 ? g->spot_near_player(g->ud, c, player) : -1);
}
int t_drop_grenade(lua_State *L)
{
    const MissionHostGuard *g = gh(L);
    int c = enter(L, "drop_grenade", g ? g->drop_grenade : nullptr);
    lua_pushboolean(L, c >= 0 && g->drop_grenade(g->ud, c));
    return 1;
}
int t_drop(lua_State *L)
{
    const MissionHostGuard *g = gh(L);
    int c = enter(L, "drop", g ? g->drop_object : nullptr);
    luaL_checktype(L, 2, LUA_TTABLE);
    int prop = missionLuaTgtProp(L, 2);
    lua_pushboolean(L, c >= 0 && prop >= 0 && g->drop_object(g->ud, c, prop));
    return 1;
}
int t_give_object(lua_State *L)
{
    const MissionHostGuard *g = gh(L);
    int c = enter(L, "give_object", g ? g->give_object : nullptr);
    luaL_checktype(L, 2, LUA_TTABLE);
    int prop = missionLuaTgtProp(L, 2);
    lua_pushboolean(L, c >= 0 && prop >= 0 && g->give_object(g->ud, c, prop));
    return 1;
}
int t_set_flag_raw(lua_State *L)
{
    const MissionHostGuard *g = gh(L);
    int c = enter(L, "set_flag_raw", g ? g->set_flag_raw : nullptr);
    const double bits = luaL_checknumber(L, 2);
    if (c >= 0) g->set_flag_raw(g->ud, c, (unsigned)(unsigned long long)bits, lua_toboolean(L, 3));
    return 0;
}

// ------------------------------------------------------------ globals
// spawn_guard{ chr?, pad, body, head?, force?, sunglasses? } -> Target?
int l_spawn_guard(lua_State *L)
{
    const MissionHostGuard *g = gh(L);
    luaL_checktype(L, 1, LUA_TTABLE);
    if (!g || !g->spawn) { notImplemented(L, "spawn_guard"); lua_pushnil(L); return 1; }
    MissionGuardSpawn s;
    double v;
    s.chr = optNum(L, 1, "chr", &v) ? (int)v : -1;
    s.body = optNum(L, 1, "body", &v) ? (int)v : -1;
    s.head = optNum(L, 1, "head", &v) ? (int)v : -1;
    s.force = optBool(L, 1, "force") == 1;
    s.sunglasses = optBool(L, 1, "sunglasses") == 1;
    lua_getfield(L, 1, "pad");
    s.pad = lua_isnil(L, -1) ? -1 : padAt(L, -1);
    lua_pop(L, 1);
    if (s.body < 0 || s.pad < 0) { lua_pushnil(L); return 1; }
    int n = g->spawn(g->ud, &s);
    if (n < 0) lua_pushnil(L); else pushGuard(L, n);
    return 1;
}

// all_guards() -> { Target }: every setup guard spawned on this difficulty, with its prop and chr.
int l_all_guards(lua_State *L)
{
    const MissionHostGuard *g = gh(L);
    lua_createtable(L, 0, 0);
    if (!g || !g->all_guards) { notImplemented(L, "all_guards"); return 1; }
    enum { kMax = 1024 };
    static thread_local int props[kMax], chrs[kMax];
    int n = g->all_guards(g->ud, props, chrs, kMax);
    for (int i = 0; i < n; i++) {
        missionLuaPushTarget(L, "prop", props[i], chrs[i], -1);
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

int l_civilians_killed(lua_State *L)
{
    const MissionHostGuard *g = gh(L);
    if (!g || !g->civilians_killed) { notImplemented(L, "mission.civilians_killed"); lua_pushnumber(L, 0); return 1; }
    lua_pushnumber(L, g->civilians_killed(g->ud));
    return 1;
}

}  // namespace

void missionLuaRegGuard(lua_State *L, int targetMT)
{
    lua_newtable(L);
    lua_setfield(L, LUA_REGISTRYINDEX, "ge.guard.nolog");

    struct Fn { const char *name; lua_CFunction f; };
    static const Fn fns[] = {
        { "exists", t_exists }, { "is_dying", t_is_dying }, { "remove", t_remove }, { "fade_out", t_fade_out },
        { "teleport", t_teleport }, { "set_hidden", t_set_hidden }, { "setup_ai", t_setup_ai },
        { "set_stats", t_set_stats }, { "set_flag", t_set_flag }, { "get_flag", t_get_flag },
        { "set_invincible", t_set_invincible }, { "give_weapon", t_give_weapon }, { "give_hat", t_give_hat },
        { "drop_items", t_drop_items }, { "remove_held_items", t_remove_held_items }, { "preset_pad", t_preset_pad },
        { "patrol", t_patrol }, { "is_patrolling", t_is_patrolling }, { "run_to", t_run_to },
        { "run_to_player", t_run_to_player }, { "face_player", t_face_player }, { "face_pad", t_face_pad },
        { "halt", t_halt }, { "flee_pad", t_flee_pad }, { "cover_pad", t_cover_pad },
        { "look_around", t_look_around }, { "point_at_player", t_point_at_player }, { "raise_arms", t_raise_arms },
        { "kneel", t_kneel }, { "idle_anim", t_idle_anim }, { "anim", t_anim }, { "anim_done", t_anim_done },
        { "stop_anim", t_stop_anim },
        { "fire_at", t_fire_at }, { "combat_move", t_combat_move }, { "throw_grenade", t_throw_grenade },
        { "grenade_chance", t_grenade_chance }, { "hit", t_hit }, { "is_busy", t_is_busy }, { "is_idle", t_is_idle },
        { "clone", t_clone }, { "press_alarm", t_press_alarm },
        { "spotted_player", t_spotted_player }, { "line_to_player", t_line_to_player }, { "on_screen", t_on_screen },
        { "was_seen", t_was_seen }, { "is_aimed_at", t_is_aimed_at }, { "alerted_within", t_alerted_within },
        { "heard_shot_within", t_heard_shot_within }, { "near_misses", t_near_misses }, { "witnessed", t_witnessed },
        { "angle_to_player", t_angle_to_player }, { "was_damaged", t_was_damaged },
        { "held_weapon", t_held_weapon }, { "spot_near_player", t_spot_near_player }, { "drop_grenade", t_drop_grenade },
        { "drop", t_drop }, { "give_object", t_give_object }, { "set_flag_raw", t_set_flag_raw },
    };
    for (const Fn &f : fns) {
        lua_pushcfunction(L, f.f, f.name);
        lua_setfield(L, targetMT, f.name);
    }
    // set_ai: the setup-prop version stays as the fallback (upvalue) for a target with no chr.
    lua_getfield(L, targetMT, "set_ai");
    lua_pushcclosure(L, t_set_ai, "set_ai", 1);
    lua_setfield(L, targetMT, "set_ai");

    for (const Fn &f : { Fn{ "is_moving", t_is_moving }, Fn{ "was_shot_at", t_was_shot_at } }) {
        lua_getfield(L, targetMT, f.name);
        lua_pushcclosure(L, f.f, f.name, 1);
        lua_setfield(L, targetMT, f.name);
    }

    lua_pushcfunction(L, l_spawn_guard, "spawn_guard");
    lua_setglobal(L, "spawn_guard");
    lua_pushcfunction(L, l_all_guards, "all_guards");
    lua_setglobal(L, "all_guards");

    // mission.civilians_killed: added to the existing table (win / fail / difficulty stay)
    lua_getglobal(L, "mission");
    if (lua_istable(L, -1)) {
        lua_pushcfunction(L, l_civilians_killed, "civilians_killed");
        lua_setfield(L, -2, "civilians_killed");
    }
    lua_pop(L, 1);
}
