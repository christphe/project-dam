// The Luau runtime of a custom mission: see mission_lua.h.
#include "mission_lua.h"
#include "mission_pkg.h"

#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "lua.h"
#include "lualib.h"
#include "luacode.h"

namespace {

using Clock = std::chrono::steady_clock;

constexpr size_t kMemCap = 16u * 1024u * 1024u;   // the VM never holds more than this
// Time budgets. Nothing is cut (a rough first version): going over one logs a warning, the script goes on.
constexpr double kFrameBudget = 0.002;            // seconds all the scripts may run per frame, together
constexpr double kModuleBudget = 10.0;            // seconds a require()d module's top level may run (once: building its tables)
constexpr double kLoadBudget = 10.0;              // seconds a script's load (its first run, up to its first wait()) and its
                                                  // start() may each run: both happen while the level loads
constexpr double kStuckWarn = 1.0;                // a frame-time resume still running after this: "still running" (a loop
                                                  // that never ends freezes the game; the log names the script)
constexpr double kFrameWarnEvery = 5.0;           // seconds between two "over the frame budget" warnings
constexpr double kScanEvery = 0.1;                // seconds between state scans (inventory, doors, guards, zones)
constexpr int kItemScan = 128;                    // ITEM_IDS 0..127 are diffed for pickups
constexpr int kMaxPlayers = 4;

struct Field {
    std::string key;
    bool isStr = false;
    double num = 0;
    std::string str;
};
struct Event {
    std::string name;
    std::vector<Field> f;
};

struct Follow {
    int prop = -1;
    int player = -1;   // -1: the nearest living player
    // The last order: re-plotting resets the guard's speed to 0 (plot_course_for_actor), so an order is only re-sent
    // when it is out of date (see followTick).
    bool ordered = false;
    int who = -1, speed = 0;
    float tgt[3] = { 0, 0, 0 };    // where the player was
    float last[3] = { 0, 0, 0 };   // where the guard was at the previous tick
    double since = 0;              // time of the order
    int lastResult = 99;           // the previous order's MH_GOTO_* result (a repeated failure is logged once)
    double movedAt = 0;            // the last time the guard was seen moving
};

enum class St { Run, Sleep, Event, Done };

struct Script {
    lua_State *T = nullptr;
    int ref = 0;
    St st = St::Run;
    double wake = 0;
    std::string evName;
    std::vector<Field> filter;
    std::string name;
    bool started = false;
    int id = 0;               // spawn() / on() tasks: a handle for :cancel()
    bool task = false;        // a spawned coroutine (reaped once done); the mission's own scripts stay
    bool cancelled = false;   // Task:cancel(): never resumes again
    bool loaded = false;      // its first resume is done (a mission script: its load; a start() task: start() itself)
    bool isStart = false;     // the task that runs the script's start() (kLoadBudget for its first resume)
};

struct Sub {                  // on(name, fn, filter)
    int id = 0;
    std::string name;
    std::vector<Field> filter;
    int fnref = 0;            // registry reference of the callback
    bool active = true;
};

struct Objective {
    std::string id, text;
    int difficulty = 0;
    bool done = false;
    bool failed = false;   // objectives.fail(): shown as failed; it can no longer be completed
    bool quiet = false;    // objectives.complete(id, true): completing it never shows a HUD line (a protect objective)
};

struct Alloc {
    size_t used = 0;
};

}  // namespace

struct MissionRuntime {
    const Mission *m = nullptr;
    MissionHost host{};
    std::string dir, modDir;
    lua_State *L = nullptr;
    Alloc alloc;
    std::vector<std::unique_ptr<Script>> scripts;
    std::vector<Objective> objs;
    std::vector<Event> queue;
    std::vector<Follow> follows;
    std::vector<Sub> subs;
    std::map<std::string, int> modules;   // require(): name -> registry ref of its result (0 while it loads)
    int nextId = 1;
    struct PendingHit { int target; double at; };
    std::vector<PendingHit> hits;   // :shoot(): the hit lands once the shooter has had time to raise and fire
    double time = 0, lastScan = -1, lastFollow = -1;
    bool first = true, ended = false, scanned = false;
    Clock::time_point deadline;         // the running resume's budget: past it, one warning (onInterrupt)
    std::string budgetWhat;             // what that budget is for, in the warning ("script x#0: its load")
    double budgetSecs = 0;
    bool overWarned = false;            // the running resume has already been warned about
    double frameUsed = 0, frameTop = 0, lastFrameWarn = -100;   // this frame's script time, the longest resume in it
    std::string frameTopName;
    // state diffed into events
    bool had[kMaxPlayers][kItemScan] = {};
    std::vector<signed char> propType;
    std::vector<signed char> doorPrev, deadPrev;
    std::vector<unsigned char> inZone;   // [zone * kMaxPlayers + player]
    int started = 0;

    void log(const char *fmt, ...) const;
};

void MissionRuntime::log(const char *fmt, ...) const
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (host.log) host.log(host.ud, buf);
}

namespace {

MissionRuntime *rtOf(lua_State *L) { return static_cast<MissionRuntime *>(lua_callbacks(L)->userdata); }

void *allocFn(void *ud, void *ptr, size_t osize, size_t nsize)
{
    Alloc *a = static_cast<Alloc *>(ud);
    if (nsize == 0) {
        if (ptr) a->used -= osize;
        free(ptr);
        return nullptr;
    }
    size_t old = ptr ? osize : 0;
    if (a->used - old + nsize > kMemCap) return nullptr;
    void *p = realloc(ptr, nsize);
    if (p) a->used = a->used - old + nsize;
    return p;
}

void onInterrupt(lua_State *L, int gc)
{
    if (gc >= 0) return;
    // Luau calls this at every call and loop back-edge: reading the clock each time made scripts ~5x slower on the
    // Switch (Ryubing: a 100k-iteration loop 13.3 ms against 2.4 ms). Once every 64 checks is plenty for a 4 ms budget.
    static thread_local unsigned s_checks = 0;
    if ((++s_checks & 63u) != 0) return;
    MissionRuntime *rt = rtOf(L);
    if (rt && !rt->overWarned && Clock::now() > rt->deadline) {
        rt->overWarned = true;   // once per budget; nothing is cut
        rt->log("%s: still running after %.3g s (over its budget; nothing is cut)", rt->budgetWhat.c_str(), rt->budgetSecs);
    }
}

void setBudget(MissionRuntime *rt, const std::string &what, double secs)
{
    rt->budgetWhat = what;
    rt->budgetSecs = secs;
    rt->overWarned = false;
    rt->deadline = Clock::now() + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(secs));
}

// ---------------------------------------------------------------- helpers
Script *scriptOf(lua_State *L)
{
    return static_cast<Script *>(lua_getthreaddata(L));
}

int optPlayer(lua_State *L, int idx, int def)
{
    if (lua_isnoneornil(L, idx)) return def;
    return (int)luaL_checkinteger(L, idx);
}

void readFilter(lua_State *L, int idx, std::vector<Field> &out)
{
    out.clear();
    if (!lua_istable(L, idx)) return;
    lua_pushnil(L);
    while (lua_next(L, idx) != 0) {   // key at -2, value at -1
        if (lua_type(L, -2) == LUA_TSTRING) {
            Field f;
            f.key = lua_tostring(L, -2);
            if (lua_type(L, -1) == LUA_TNUMBER) f.num = lua_tonumber(L, -1);
            else if (lua_type(L, -1) == LUA_TBOOLEAN) f.num = lua_toboolean(L, -1) ? 1 : 0;
            else if (lua_type(L, -1) == LUA_TSTRING) { f.isStr = true; f.str = lua_tostring(L, -1); }
            else { lua_pop(L, 1); continue; }
            out.push_back(std::move(f));
        }
        lua_pop(L, 1);
    }
}

bool matchFilter(const std::string &name, const std::vector<Field> &filter, const Event &e)
{
    if (name != e.name) return false;
    for (const Field &want : filter) {
        bool ok = false;
        for (const Field &have : e.f) {
            if (have.key != want.key) continue;
            ok = have.isStr == want.isStr && (want.isStr ? have.str == want.str : have.num == want.num);
            break;
        }
        if (!ok) return false;
    }
    return true;
}

bool matches(const Script &s, const Event &e) { return matchFilter(s.evName, s.filter, e); }

void pushEvent(lua_State *T, const Event &e)
{
    lua_createtable(T, 0, (int)e.f.size() + 1);
    lua_pushstring(T, e.name.c_str());
    lua_setfield(T, -2, "name");
    for (const Field &f : e.f) {
        if (f.isStr) lua_pushstring(T, f.str.c_str());
        else lua_pushnumber(T, f.num);
        lua_setfield(T, -2, f.key.c_str());
    }
}

Field num(const char *k, double v) { Field f; f.key = k; f.num = v; return f; }
Field str(const char *k, const char *v) { Field f; f.key = k; f.isStr = true; f.str = v; return f; }

void raise(MissionRuntime *rt, Event e) { rt->queue.push_back(std::move(e)); }

// ----------------------------------------------------------- Lua functions
int l_print(lua_State *L)
{
    std::string s;
    int n = lua_gettop(L);
    for (int i = 1; i <= n; i++) {
        size_t len;
        const char *p = luaL_tolstring(L, i, &len);
        if (i > 1) s += "\t";
        s.append(p, len);
        lua_pop(L, 1);
    }
    MissionRuntime *rt = rtOf(L);
    Script *sc = scriptOf(L);
    rt->log("script %s: %s", sc ? sc->name.c_str() : "?", s.c_str());
    return 0;
}

int l_message(lua_State *L)
{
    MissionRuntime *rt = rtOf(L);
    const char *t = luaL_checkstring(L, 1);
    if (rt->host.message) rt->host.message(rt->host.ud, t);
    return 0;
}

int l_say(lua_State *L)
{
    MissionRuntime *rt = rtOf(L);
    const char *who = luaL_checkstring(L, 1);
    const char *t = luaL_checkstring(L, 2);
    if (rt->host.say) rt->host.say(rt->host.ud, who, t);
    return 0;
}

int l_give(lua_State *L)
{
    MissionRuntime *rt = rtOf(L);
    int item = (int)luaL_checkinteger(L, 1);
    if (rt->host.give) rt->host.give(rt->host.ud, optPlayer(L, 2, -1), item);
    return 0;
}

int l_take(lua_State *L)
{
    MissionRuntime *rt = rtOf(L);
    int item = (int)luaL_checkinteger(L, 1);
    if (rt->host.take) rt->host.take(rt->host.ud, optPlayer(L, 2, -1), item);
    return 0;
}

bool hasItem(MissionRuntime *rt, int player, int item)
{
    if (!rt->host.has) return false;
    if (player >= 0) return rt->host.has(rt->host.ud, player, item) != 0;
    int n = rt->host.player_count ? rt->host.player_count(rt->host.ud) : 1;
    for (int p = 0; p < n && p < kMaxPlayers; p++)
        if (rt->host.has(rt->host.ud, p, item)) return true;
    return false;
}

int l_has(lua_State *L)
{
    MissionRuntime *rt = rtOf(L);
    int item = (int)luaL_checkinteger(L, 1);
    lua_pushboolean(L, hasItem(rt, optPlayer(L, 2, -1), item));
    return 1;
}

int l_event(lua_State *L)
{
    const char *name = luaL_checkstring(L, 1);
    const bool hasFilter = lua_istable(L, 2) != 0;
    lua_createtable(L, 0, 2);
    lua_pushstring(L, name);
    lua_setfield(L, -2, "__event");
    if (hasFilter) lua_pushvalue(L, 2); else lua_createtable(L, 0, 0);
    lua_setfield(L, -2, "filter");
    return 1;
}

int l_wait(lua_State *L)
{
    MissionRuntime *rt = rtOf(L);
    Script *s = scriptOf(L);
    if (!s) luaL_error(L, "wait outside a script");
    if (lua_isnumber(L, 1)) {
        double sec = lua_tonumber(L, 1);
        s->st = St::Sleep;
        s->wake = rt->time + (sec > 0 ? sec : 0);
    } else if (lua_istable(L, 1)) {
        lua_getfield(L, 1, "__event");
        if (lua_type(L, -1) != LUA_TSTRING) luaL_error(L, "wait expects seconds or event(...)");
        s->evName = lua_tostring(L, -1);
        lua_pop(L, 1);
        lua_getfield(L, 1, "filter");
        readFilter(L, lua_gettop(L), s->filter);
        lua_pop(L, 1);
        s->st = St::Event;
    } else {
        luaL_error(L, "wait expects seconds or event(...)");
    }
    return lua_yield(L, 0);
}

Objective *findObj(MissionRuntime *rt, const char *id)
{
    for (Objective &o : rt->objs)
        if (o.id == id) return &o;
    return nullptr;
}

int l_obj_add(lua_State *L)
{
    MissionRuntime *rt = rtOf(L);
    const char *id = luaL_checkstring(L, 1);
    const char *text = luaL_optstring(L, 2, id);
    if (findObj(rt, id)) return 0;
    Objective o;
    o.id = id;
    o.text = text;
    o.difficulty = (int)luaL_optinteger(L, 3, 0);
    rt->objs.push_back(std::move(o));
    return 0;
}

bool completeObjective(MissionRuntime *rt, const char *id)
{
    Objective *o = findObj(rt, id);
    if (!o) return false;
    if (!o->done && !o->failed) {
        o->done = true;
        rt->log("objective complete: %s", id);
        raise(rt, Event{ "objective", { str("id", id) } });
    }
    return true;
}

int l_obj_complete(lua_State *L)
{
    MissionRuntime *rt = rtOf(L);
    const char *id = luaL_checkstring(L, 1);
    if (lua_toboolean(L, 2)) {
        Objective *o = findObj(rt, id);
        if (o) o->quiet = true;
    }
    if (!completeObjective(rt, id)) luaL_error(L, "no objective '%s'", id);
    return 0;
}

int l_obj_fail(lua_State *L)
{
    MissionRuntime *rt = rtOf(L);
    const char *id = luaL_checkstring(L, 1);
    Objective *o = findObj(rt, id);
    if (!o) luaL_error(L, "no objective '%s'", id);
    if (!o->done && !o->failed) {
        o->failed = true;
        rt->log("objective failed: %s", id);
        raise(rt, Event{ "objective_failed", { str("id", id) } });
    }
    return 0;
}

// objectives.reset(id): back to incomplete (neither complete nor failed), e.g. "keep X alive" while X is down.
int l_obj_reset(lua_State *L)
{
    MissionRuntime *rt = rtOf(L);
    const char *id = luaL_checkstring(L, 1);
    Objective *o = findObj(rt, id);
    if (!o) luaL_error(L, "no objective '%s'", id);
    if (o->done || o->failed) {
        o->done = false;
        o->failed = false;
        rt->log("objective reset: %s", id);
    }
    return 0;
}

int l_obj_is_failed(lua_State *L)
{
    MissionRuntime *rt = rtOf(L);
    Objective *o = findObj(rt, luaL_checkstring(L, 1));
    lua_pushboolean(L, o && o->failed);
    return 1;
}

int l_obj_is_complete(lua_State *L)
{
    MissionRuntime *rt = rtOf(L);
    Objective *o = findObj(rt, luaL_checkstring(L, 1));
    lua_pushboolean(L, o && o->done);
    return 1;
}

int l_obj_all(lua_State *L)
{
    MissionRuntime *rt = rtOf(L);
    int diff = rt->host.difficulty ? rt->host.difficulty(rt->host.ud) : 99;
    bool all = true;
    for (const Objective &o : rt->objs)
        if (o.difficulty <= diff) all = all && o.done;
    lua_pushboolean(L, all);
    return 1;
}

int l_alarm_trigger(lua_State *L)
{
    MissionRuntime *rt = rtOf(L);
    if (rt->host.alarm) rt->host.alarm(rt->host.ud);
    return 0;
}

// mission.difficulty() -> 0 Agent .. 3 007; -1 when the host has no difficulty
int l_difficulty(lua_State *L)
{
    MissionRuntime *rt = rtOf(L);
    lua_pushinteger(L, rt->host.difficulty ? rt->host.difficulty(rt->host.ud) : -1);
    return 1;
}

int l_win(lua_State *L)
{
    MissionRuntime *rt = rtOf(L);
    rt->log("mission.win()");
    if (rt->host.win) rt->host.win(rt->host.ud);
    return 0;
}

int l_fail(lua_State *L)
{
    MissionRuntime *rt = rtOf(L);
    rt->log("mission.fail()");
    if (rt->host.fail) rt->host.fail(rt->host.ud);
    return 0;
}

// ------------------------------------------------- clock, spawn, on, require
const char kTaskMT[] = "ge.task";
const char kConnMT[] = "ge.connection";

Script *newTask(MissionRuntime *rt, const std::string &name);
void resumeScript(MissionRuntime *rt, Script &s, const Event *ev);
bool validName(const char *n);
bool loadSource(MissionRuntime *rt, const std::string &use, std::string &out, std::string &origin);

int l_clock(lua_State *L)
{
    lua_pushnumber(L, rtOf(L)->time);
    return 1;
}

// A handle table { id = n } with a metatable whose __index holds the method.
void pushHandle(lua_State *L, const char *mt, int id)
{
    lua_createtable(L, 0, 1);
    lua_pushnumber(L, id);
    lua_setfield(L, -2, "id");
    luaL_getmetatable(L, mt);
    lua_setmetatable(L, -2);
}

int handleId(lua_State *L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_getfield(L, 1, "id");
    int id = lua_isnumber(L, -1) ? (int)lua_tonumber(L, -1) : -1;
    lua_pop(L, 1);
    return id;
}

// spawn(fn) -> Task: fn runs as a coroutine of its own (it may wait()), first at this tick's sleeper pass.
int l_spawn(lua_State *L)
{
    MissionRuntime *rt = rtOf(L);
    luaL_checktype(L, 1, LUA_TFUNCTION);
    Script *s = newTask(rt, "task");
    lua_pushvalue(L, 1);
    lua_xmove(L, s->T, 1);
    s->st = St::Sleep;
    s->wake = rt->time;
    pushHandle(L, kTaskMT, s->id);
    return 1;
}

int task_cancel(lua_State *L)
{
    MissionRuntime *rt = rtOf(L);
    const int id = handleId(L);
    for (auto &sp : rt->scripts)
        if (sp->task && sp->id == id) {
            sp->cancelled = true;
            sp->st = St::Done;
        }
    return 0;
}

// on(name, fn [, filter]) -> Connection: fn(event) runs as a new coroutine every time a matching event is raised.
int l_on(lua_State *L)
{
    MissionRuntime *rt = rtOf(L);
    Sub sub;
    sub.name = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    readFilter(L, 3, sub.filter);
    sub.id = rt->nextId++;
    sub.fnref = lua_ref(L, 2);
    const int id = sub.id;
    rt->subs.push_back(std::move(sub));
    pushHandle(L, kConnMT, id);
    return 1;
}

int conn_off(lua_State *L)
{
    MissionRuntime *rt = rtOf(L);
    const int id = handleId(L);
    for (Sub &s : rt->subs)
        if (s.id == id && s.active) {
            s.active = false;
            lua_unref(L, s.fnref);
            s.fnref = 0;
        }
    return 0;
}

// require(name): a module <name>.luau (same lookup as the scripts), run once, its return value cached.
int l_require(lua_State *L)
{
    MissionRuntime *rt = rtOf(L);
    const char *name = luaL_checkstring(L, 1);
    char err[512] = "";
    if (!validName(name)) luaL_error(L, "require: invalid module name");
    {
        auto it = rt->modules.find(name);
        if (it != rt->modules.end()) {
            if (it->second == 0) luaL_error(L, "require: module '%s' requires itself", name);
            lua_getref(L, it->second);
            return 1;
        }
    }
    lua_State *T = lua_newthread(L);   // anchored on L's stack while the module runs
    int ok = 0;
    // The caller's budget is its own: a require (reading, compiling and running the module) does not spend it. Its
    // deadline is put back afterwards, moved on by the time the require took.
    const Clock::time_point callerDeadline = rt->deadline, requireStart = Clock::now();
    const std::string callerWhat = rt->budgetWhat;
    const double callerSecs = rt->budgetSecs;
    const bool callerWarned = rt->overWarned;
    {
        std::string src, origin, chunk;
        if (!loadSource(rt, name, src, origin)) {
            snprintf(err, sizeof err, "require: module '%s' not found", name);
        } else {
            lua_CompileOptions opts = {};
            opts.optimizationLevel = 1;
            opts.debugLevel = 1;
            size_t bsz = 0;
            char *bc = luau_compile(src.data(), src.size(), &opts, &bsz);
            luaL_sandboxthread(T);   // its own globals table over the mission's
            chunk = "=" + origin;
            if (!bc || luau_load(T, chunk.c_str(), bc, bsz, 0) != 0) {
                snprintf(err, sizeof err, "require: %s: %s", name, lua_isstring(T, -1) ? lua_tostring(T, -1) : "compile error");
            } else {
                rt->modules[name] = 0;
                // Reading the module (from a .pmod, on the Switch from the romfs) and compiling it is the host's work,
                // not the script's. Its top level runs once (a data module builds all its tables there): a budget of
                // its own.
                setBudget(rt, std::string("require('") + name + "')", kModuleBudget);
                const int rc = lua_pcall(T, 0, 1, 0);
                rt->deadline = callerDeadline + (Clock::now() - requireStart);
                rt->budgetWhat = callerWhat;
                rt->budgetSecs = callerSecs;
                rt->overWarned = callerWarned;
                if (rc != LUA_OK) {
                    snprintf(err, sizeof err, "require: %s: %s", name, lua_isstring(T, -1) ? lua_tostring(T, -1) : "error");
                    rt->modules.erase(name);
                } else {
                    if (lua_isnil(T, -1)) { lua_pop(T, 1); lua_pushboolean(T, 1); }
                    lua_xmove(T, L, 1);
                    rt->modules[name] = lua_ref(L, -1);
                    rt->log("require: module %s loaded from %s", name, origin.c_str());
                    ok = 1;
                }
            }
            free(bc);
        }
    }
    if (!ok) luaL_error(L, "%s", err);
    return 1;
}

// ------------------------------------------------------------ targets
const char kTargetMT[] = "ge.target";

int tgtPropAt(lua_State *L, int idx)
{
    luaL_checktype(L, idx, LUA_TTABLE);
    lua_getfield(L, idx, "prop");
    int p = lua_isnumber(L, -1) ? (int)lua_tonumber(L, -1) : -1;
    lua_pop(L, 1);
    return p;
}

int tgtProp(lua_State *L) { return tgtPropAt(L, 1); }

int t_door(lua_State *L, int cmd)
{
    MissionRuntime *rt = rtOf(L);
    int p = tgtProp(L);
    if (p < 0) luaL_error(L, "this target is not a prop");
    if (rt->host.door_cmd) rt->host.door_cmd(rt->host.ud, p, cmd);
    return 0;
}
int t_lock(lua_State *L) { return t_door(L, MH_DOOR_LOCK); }
int t_unlock(lua_State *L) { return t_door(L, lua_toboolean(L, 2) ? MH_DOOR_UNLOCK_FORCE : MH_DOOR_UNLOCK); }
int t_open(lua_State *L) { return t_door(L, MH_DOOR_OPEN); }
int t_close(lua_State *L) { return t_door(L, MH_DOOR_CLOSE); }

int t_is_open(lua_State *L)
{
    MissionRuntime *rt = rtOf(L);
    int p = tgtProp(L);
    lua_pushboolean(L, p >= 0 && rt->host.door_state && rt->host.door_state(rt->host.ud, p) == MH_DOOR_OPENED);
    return 1;
}

int t_is_dead(lua_State *L)
{
    MissionRuntime *rt = rtOf(L);
    int p = tgtProp(L);
    lua_pushboolean(L, p >= 0 && rt->host.guard_dead && rt->host.guard_dead(rt->host.ud, p));
    return 1;
}

int t_set_ai(lua_State *L)
{
    MissionRuntime *rt = rtOf(L);
    int p = tgtProp(L);
    int ai = (int)luaL_checkinteger(L, 2);
    if (p < 0) luaL_error(L, "this target is not a prop");
    if (rt->host.guard_set_ai) rt->host.guard_set_ai(rt->host.ud, p, ai);
    return 0;
}

double nearestPlayerIdx(MissionRuntime *rt, const float c[3], int only, int *which);

static int poseCmd(lua_State *L, int pose)
{
    MissionRuntime *rt = rtOf(L);
    int p = tgtProp(L);
    if (p < 0) luaL_error(L, "this target is not a prop");
    float g[3];
    int who = 0;
    if (rt->host.prop_pos && rt->host.prop_pos(rt->host.ud, p, g)) nearestPlayerIdx(rt, g, -1, &who);
    if (who < 0) who = 0;
    if (rt->host.guard_pose) rt->host.guard_pose(rt->host.ud, p, pose, who);
    rt->log("guard %d: %s", p, pose == MH_POSE_SURRENDER ? "surrender" : "stand");
    return 0;
}
int t_surrender(lua_State *L) { return poseCmd(L, MH_POSE_SURRENDER); }

static const char *gotoResult(int res)
{
    return res == MH_GOTO_OK ? "ok" : res == MH_GOTO_NOROUTE ? "NO ROUTE" : res == MH_GOTO_BUSY ? "REFUSED (10+ guards saw a player)" : "GONE";
}

int t_go_to(lua_State *L)
{
    MissionRuntime *rt = rtOf(L);
    int p = tgtProp(L), t = tgtPropAt(L, 2);
    const char *sp = luaL_optstring(L, 3, "run");
    if (p < 0 || t < 0) luaL_error(L, "go_to: the guard and the target must be props");
    const int run = !strcmp(sp, "walk") ? 0 : !strcmp(sp, "sprint") ? 2 : 1;
    const int res = rt->host.guard_goto_prop ? rt->host.guard_goto_prop(rt->host.ud, p, t, run) : MH_GOTO_GONE;
    rt->log("guard %d: go_to prop %d (%s): %s", p, t, sp, gotoResult(res));
    lua_pushboolean(L, res == MH_GOTO_OK);
    return 1;
}

int t_is_moving(lua_State *L)
{
    MissionRuntime *rt = rtOf(L);
    int p = tgtProp(L);
    lua_pushboolean(L, p >= 0 && rt->host.guard_moving && rt->host.guard_moving(rt->host.ud, p));
    return 1;
}

int t_face(lua_State *L)
{
    MissionRuntime *rt = rtOf(L);
    int p = tgtProp(L), t = tgtPropAt(L, 2);
    if (p < 0 || t < 0) luaL_error(L, "face: the guard and the target must be props");
    const int ok = !rt->host.guard_face_prop || rt->host.guard_face_prop(rt->host.ud, p, t);
    lua_pushboolean(L, ok);
    return 1;
}

int t_aim_at(lua_State *L)
{
    MissionRuntime *rt = rtOf(L);
    int p = tgtProp(L), t = tgtPropAt(L, 2);
    if (p < 0 || t < 0) luaL_error(L, "aim_at: the guard and the target must be props");
    lua_pushboolean(L, rt->host.guard_aim_at && rt->host.guard_aim_at(rt->host.ud, p, t, 0));
    return 1;
}

int t_shoot(lua_State *L)
{
    MissionRuntime *rt = rtOf(L);
    int p = tgtProp(L), t = tgtPropAt(L, 2);
    if (p < 0 || t < 0) luaL_error(L, "shoot: the guard and the target must be props");
    if (rt->host.guard_aim_at) rt->host.guard_aim_at(rt->host.ud, p, t, 1);
    // Frigate's hostage takers fire, wait five AI ticks, then deal the hit: the shot is seen before the target falls.
    rt->hits.push_back({ t, rt->time + MISSION_SHOT_DELAY });
    rt->log("guard %d: shoots guard %d", p, t);
    return 0;
}

int t_sees_player(lua_State *L)
{
    MissionRuntime *rt = rtOf(L);
    int p = tgtProp(L);
    lua_pushboolean(L, p >= 0 && rt->host.guard_sees_player && rt->host.guard_sees_player(rt->host.ud, p));
    return 1;
}

int t_was_shot_at(lua_State *L)
{
    MissionRuntime *rt = rtOf(L);
    int p = tgtProp(L);
    lua_pushboolean(L, p >= 0 && rt->host.guard_near_miss && rt->host.guard_near_miss(rt->host.ud, p));
    return 1;
}

bool targetPos(lua_State *L, MissionRuntime *rt, float xyz[3], float *radius);

int t_guards_near(lua_State *L)
{
    MissionRuntime *rt = rtOf(L);
    float c[3], r;
    const double radius = luaL_checknumber(L, 2);
    if (!targetPos(L, rt, c, &r) || !rt->host.guards_near) { lua_pushnumber(L, 0); return 1; }
    lua_pushnumber(L, rt->host.guards_near(rt->host.ud, c, (float)radius, tgtProp(L)));
    return 1;
}

int t_hits(lua_State *L)
{
    MissionRuntime *rt = rtOf(L);
    int p = tgtProp(L);
    lua_pushnumber(L, (p >= 0 && rt->host.guard_hits) ? rt->host.guard_hits(rt->host.ud, p) : 0);
    return 1;
}

int t_keep_body(lua_State *L)
{
    MissionRuntime *rt = rtOf(L);
    int p = tgtProp(L);
    if (p < 0) luaL_error(L, "keep_body: this target is not a prop");
    if (rt->host.guard_keep_body) rt->host.guard_keep_body(rt->host.ud, p);
    return 0;
}

int t_make_usable(lua_State *L)
{
    MissionRuntime *rt = rtOf(L);
    int p = tgtProp(L);
    if (p < 0) luaL_error(L, "make_usable: this target is not a prop");
    if (rt->host.prop_usable) rt->host.prop_usable(rt->host.ud, p);
    return 0;
}

int t_was_used(lua_State *L)
{
    MissionRuntime *rt = rtOf(L);
    int p = tgtProp(L);
    const int used = p >= 0 && rt->host.prop_used && rt->host.prop_used(rt->host.ud, p);
    if (used) rt->log("prop %d: used by a player", p);
    lua_pushboolean(L, used);
    return 1;
}

int t_play_anim(lua_State *L)
{
    MissionRuntime *rt = rtOf(L);
    int p = tgtProp(L);
    const char *name = luaL_checkstring(L, 2);
    const int hold = lua_toboolean(L, 3);
    const int ok = p >= 0 && rt->host.guard_anim && rt->host.guard_anim(rt->host.ud, p, name, hold);
    rt->log("guard %d: play_anim %s%s: %s", p, name, hold ? " (hold)" : "", ok ? "ok" : "unknown animation or refused");
    lua_pushboolean(L, ok);
    return 1;
}
int t_stand(lua_State *L) { return poseCmd(L, MH_POSE_STAND); }

// guard:health() / :max_health(): game units (maxdamage - damage, maxdamage); 0 for a guard that is not there.
int healthField(lua_State *L, int which)
{
    MissionRuntime *rt = rtOf(L);
    const int p = tgtProp(L);
    double h = 0, m = 0;
    if (p >= 0 && rt->host.guard_health && !rt->host.guard_health(rt->host.ud, p, &h, &m)) h = m = 0;
    lua_pushnumber(L, which ? m : h);
    return 1;
}
int t_health(lua_State *L) { return healthField(L, 0); }
int t_max_health(lua_State *L) { return healthField(L, 1); }

int setHealthField(lua_State *L, int which)
{
    MissionRuntime *rt = rtOf(L);
    const int p = tgtProp(L);
    const double v = luaL_checknumber(L, 2);
    if (p < 0) luaL_error(L, "this target is not a prop");
    if (rt->host.guard_set_health) rt->host.guard_set_health(rt->host.ud, p, which, v);
    return 0;
}
int t_set_health(lua_State *L) { return setHealthField(L, MH_HEALTH_CUR); }
int t_set_max_health(lua_State *L) { return setHealthField(L, MH_HEALTH_MAX); }

// guard:set_revivable([on]): down instead of dead, revived by a player (missions_host.c reviveTick). :is_down().
int t_set_revivable(lua_State *L)
{
    MissionRuntime *rt = rtOf(L);
    const int p = tgtProp(L);
    const int on = lua_isnoneornil(L, 2) ? 1 : lua_toboolean(L, 2);
    if (p < 0) luaL_error(L, "set_revivable: this target is not a prop");
    if (rt->host.guard_set_revivable) rt->host.guard_set_revivable(rt->host.ud, p, on);
    rt->log("guard %d: revivable %s", p, on ? "on" : "off");
    return 0;
}

int t_is_down(lua_State *L)
{
    MissionRuntime *rt = rtOf(L);
    const int p = tgtProp(L);
    lua_pushboolean(L, p >= 0 && rt->host.guard_is_down && rt->host.guard_is_down(rt->host.ud, p));
    return 1;
}

// player(n)'s index (the "player" field), -1 if none.
int tgtPlayer(lua_State *L, int idx)
{
    lua_getfield(L, idx, "player");
    int p = lua_isnumber(L, -1) ? (int)lua_tonumber(L, -1) : -1;
    lua_pop(L, 1);
    return p;
}

// The position of the target table at `idx`: a prop (guard / object / door), a player, a zone, or a mission pad (by id).
// An original pad number has no position the runtime knows: false.
bool targetPosAt(lua_State *L, int idx, MissionRuntime *rt, float xyz[3], float *radius)
{
    *radius = 0;
    lua_getfield(L, idx, "kind");
    std::string kind = lua_isstring(L, -1) ? lua_tostring(L, -1) : "";
    lua_pop(L, 1);
    if (kind == "prop") {
        int p = tgtPropAt(L, idx);
        return p >= 0 && rt->host.prop_pos && rt->host.prop_pos(rt->host.ud, p, xyz);
    }
    if (kind == "player") {
        int p = tgtPlayer(L, idx);
        return p >= 0 && rt->host.player_pos && rt->host.player_pos(rt->host.ud, p, xyz);
    }
    if (kind == "pad") {
        lua_getfield(L, idx, "pad");
        std::string id = lua_type(L, -1) == LUA_TSTRING ? lua_tostring(L, -1) : "";
        lua_pop(L, 1);
        for (int i = 0; !id.empty() && i < rt->m->npads; i++) {
            if (id != rt->m->pads[i].id) continue;
            for (int a = 0; a < 3; a++) xyz[a] = rt->m->pads[i].pos[a];
            return true;
        }
        return false;
    }
    if (kind == "zone") {
        const char *keys[3] = { "x", "y", "z" };
        for (int i = 0; i < 3; i++) {
            lua_getfield(L, idx, keys[i]);
            xyz[i] = (float)lua_tonumber(L, -1);
            lua_pop(L, 1);
        }
        lua_getfield(L, idx, "radius");
        *radius = (float)lua_tonumber(L, -1);
        lua_pop(L, 1);
        return true;
    }
    return false;
}

bool targetPos(lua_State *L, MissionRuntime *rt, float xyz[3], float *radius) { return targetPosAt(L, 1, rt, xyz, radius); }

// The nearest living player to `c` (only `only` when >= 0); its index in *which (-1 if none) and its distance.
double nearestPlayerIdx(MissionRuntime *rt, const float c[3], int only, int *which)
{
    double best = 1e30;
    int n = rt->host.player_count ? rt->host.player_count(rt->host.ud) : 0;
    if (which) *which = -1;
    for (int p = 0; p < n && p < kMaxPlayers; p++) {
        float q[3];
        if (only >= 0 && p != only) continue;
        if (!rt->host.player_pos || !rt->host.player_pos(rt->host.ud, p, q)) continue;
        double dx = q[0] - c[0], dy = q[1] - c[1], dz = q[2] - c[2];
        double d = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (d < best) { best = d; if (which) *which = p; }
    }
    return best;
}

double nearestPlayer(MissionRuntime *rt, const float c[3]) { return nearestPlayerIdx(rt, c, -1, nullptr); }

// target:pos() -> x, y, z (nil, nil, nil when the target has no position)
int t_pos(lua_State *L)
{
    MissionRuntime *rt = rtOf(L);
    float c[3], r;
    if (!targetPos(L, rt, c, &r)) {
        for (int i = 0; i < 3; i++) lua_pushnil(L);
        return 3;
    }
    for (int i = 0; i < 3; i++) lua_pushnumber(L, c[i]);
    return 3;
}

// target:say(who, text [, range]): say() from the target's place (missions_host.c hSayAt). A prop is followed while
// the line shows; a pad or a zone is its point; no position: a plain say().
int t_say(lua_State *L)
{
    MissionRuntime *rt = rtOf(L);
    const char *who = luaL_checkstring(L, 2);
    const char *t = luaL_checkstring(L, 3);
    float range = (float)luaL_optnumber(L, 4, 0.0);
    float xyz[3], r;
    lua_getfield(L, 1, "kind");
    std::string kind = lua_isstring(L, -1) ? lua_tostring(L, -1) : "";
    lua_pop(L, 1);
    if (rt->host.say_at && targetPos(L, rt, xyz, &r)) {
        int k = kind == "prop" ? MH_SAY_PROP : kind == "player" ? MH_SAY_PLAYER : MH_SAY_POINT;
        int index = k == MH_SAY_PROP ? tgtProp(L) : k == MH_SAY_PLAYER ? tgtPlayer(L, 1) : -1;
        rt->host.say_at(rt->host.ud, who, t, k, index, xyz, range < 0 ? 0 : range);
        return 0;
    }
    if (rt->host.say) rt->host.say(rt->host.ud, who, t);
    return 0;
}

int t_distance_to(lua_State *L)
{
    MissionRuntime *rt = rtOf(L);
    float a[3], b[3], r;
    luaL_checktype(L, 2, LUA_TTABLE);
    if (!targetPosAt(L, 1, rt, a, &r) || !targetPosAt(L, 2, rt, b, &r)) { lua_pushnumber(L, 1e30); return 1; }
    double dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    lua_pushnumber(L, std::sqrt(dx * dx + dy * dy + dz * dz));
    return 1;
}

// guard:follow([player]): see mission_lua.h
int t_follow(lua_State *L)
{
    MissionRuntime *rt = rtOf(L);
    int p = tgtProp(L);
    int player = optPlayer(L, 2, -1);
    if (p < 0) luaL_error(L, "this target is not a prop");
    bool found = false;
    for (Follow &f : rt->follows)
        if (f.prop == p) { f.player = player; f.ordered = false; found = true; }
    if (!found) {
        Follow f;
        f.prop = p;
        f.player = player;
        rt->follows.push_back(f);
        if (rt->host.guard_set_ai) rt->host.guard_set_ai(rt->host.ud, p, MISSION_FOLLOW_AI);
    }
    rt->lastFollow = -1;   // the first order at the next tick
    return 0;
}

int t_stop(lua_State *L)
{
    MissionRuntime *rt = rtOf(L);
    int p = tgtProp(L);
    for (size_t i = 0; i < rt->follows.size(); i++)
        if (rt->follows[i].prop == p) { rt->follows.erase(rt->follows.begin() + (long)i); break; }
    return 0;
}

static double dist3(const float a[3], const float b[3])
{
    const double dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// Every MISSION_FOLLOW_EVERY: order each following guard to its player when that player is far enough. Each order
// restarts the guard from a standstill (plot_course_for_actor sets act_gopos.speed to 0), so an order is sent only when
// the previous one is out of date: a new player to follow, a faster speed needed, the player moved MISSION_FOLLOW_REPLOT
// from where the guard is heading, or the guard has not moved for a tick since an order at least MISSION_FOLLOW_STALL old.
void followTick(MissionRuntime *rt)
{
    const MissionHost &h = rt->host;
    for (size_t i = 0; i < rt->follows.size();) {
        Follow &f = rt->follows[i];
        float g[3], q[3];
        if (h.guard_dead && h.guard_dead(h.ud, f.prop)) { rt->follows.erase(rt->follows.begin() + (long)i); continue; }
        i++;
        // a revivable guard lying down: no orders; it follows again once revived
        if (h.guard_is_down && h.guard_is_down(h.ud, f.prop)) { f.ordered = false; continue; }
        if (!h.prop_pos || !h.prop_pos(h.ud, f.prop, g)) continue;
        int who;
        const double d = nearestPlayerIdx(rt, g, f.player, &who);
        if (dist3(g, f.last) >= MISSION_FOLLOW_STILL) f.movedAt = rt->time;
        memcpy(f.last, g, sizeof f.last);
        if (who < 0 || d <= MISSION_FOLLOW_NEAR || !h.guard_goto || !h.player_pos || !h.player_pos(h.ud, who, q)) {
            f.ordered = false;   // close enough: the guard stops where it was sent
            continue;
        }
        const int speed = d > MISSION_FOLLOW_SPRINT ? 2 : d > MISSION_FOLLOW_RUN ? 1 : 0;
        // Every order restarts the guard from a standstill, so: an urgent one (none yet, another player, faster) at once;
        // otherwise at most one per MISSION_FOLLOW_GAP, and only when the player has drifted far from where the guard is
        // heading (relative to the distance: from far away, last position is good enough) or the guard has not moved
        // for MISSION_FOLLOW_STALL.
        const bool urgent = !f.ordered || who != f.who || speed > f.speed;
        const double drift = dist3(q, f.tgt), driftMax = std::max(MISSION_FOLLOW_REPLOT, d * MISSION_FOLLOW_REPLOT_K);
        const bool stalled = rt->time - f.movedAt >= MISSION_FOLLOW_STALL && rt->time - f.since >= MISSION_FOLLOW_STALL;
        const bool stale = urgent || (rt->time - f.since >= MISSION_FOLLOW_GAP && (drift > driftMax || stalled));
        if (!stale) continue;
        const char *why = !f.ordered ? "start" : who != f.who ? "other player" : speed > f.speed ? "faster"
                        : drift > driftMax ? "player moved" : "stalled";
        const int res = h.guard_goto(h.ud, f.prop, who, speed);
        static const char *const kSpeed[] = { "walk", "run", "sprint" };
        const char *rs = res == MH_GOTO_OK ? "ok" : res == MH_GOTO_NOROUTE ? "NO ROUTE" : res == MH_GOTO_BUSY ? "REFUSED (10+ guards saw a player)" : "GONE";
        if (res == MH_GOTO_OK || res != f.lastResult)
            rt->log("follow: prop %d at %.0f %.0f %.0f -> player %d at %.0f %.0f %.0f, %.0f away, %s (%s): %s", f.prop, g[0], g[1], g[2],
                    who, q[0], q[1], q[2], d, kSpeed[speed], why, rs);
        f.lastResult = res;
        if (res != MH_GOTO_OK) { f.ordered = false; continue; }   // retried at the next check
        f.ordered = true; f.who = who; f.speed = speed; f.since = rt->time; f.movedAt = rt->time;
        memcpy(f.tgt, q, sizeof f.tgt);
    }
}

int t_distance(lua_State *L)
{
    MissionRuntime *rt = rtOf(L);
    float c[3], r;
    if (!targetPos(L, rt, c, &r)) { lua_pushnumber(L, 1e30); return 1; }
    lua_pushnumber(L, nearestPlayer(rt, c));
    return 1;
}

int t_near(lua_State *L)
{
    MissionRuntime *rt = rtOf(L);
    float c[3], r;
    double radius = luaL_optnumber(L, 2, 0);
    const double height = luaL_optnumber(L, 3, -1);
    if (!targetPos(L, rt, c, &r)) { lua_pushboolean(L, 0); return 1; }
    if (radius <= 0) radius = r > 0 ? r : 150;
    if (height < 0) {
        lua_pushboolean(L, nearestPlayer(rt, c) <= radius);
        return 1;
    }
    // with a height: horizontal distance within radius and vertical gap within height (a floor above does not count)
    const int n = rt->host.player_count ? rt->host.player_count(rt->host.ud) : 0;
    bool any = false;
    for (int p = 0; p < n && p < kMaxPlayers && !any; p++) {
        float q[3];
        if (!rt->host.player_pos || !rt->host.player_pos(rt->host.ud, p, q)) continue;
        const double dx = q[0] - c[0], dz = q[2] - c[2];
        any = dx * dx + dz * dz <= radius * radius && std::fabs(q[1] - c[1]) <= height;
    }
    lua_pushboolean(L, any);
    return 1;
}

void pushTarget(lua_State *L, const char *kind, int prop)
{
    lua_createtable(L, 0, 4);
    lua_pushstring(L, kind);
    lua_setfield(L, -2, "kind");
    if (prop >= 0) { lua_pushnumber(L, prop); lua_setfield(L, -2, "prop"); }
    luaL_getmetatable(L, kTargetMT);
    lua_setmetatable(L, -2);
}

int l_door(lua_State *L) { pushTarget(L, "prop", (int)luaL_checkinteger(L, 1)); return 1; }

// player(n): player n (0-based) as a target.
int l_player(lua_State *L)
{
    int n = (int)luaL_checkinteger(L, 1);
    pushTarget(L, "player", -1);
    lua_pushnumber(L, n);
    lua_setfield(L, -2, "player");
    return 1;
}

// -------------------------------------------------------------- scripts
bool validName(const char *n)
{
    if (!n || !*n) return false;
    for (const char *p = n; *p; p++)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '_' || *p == '-')) return false;
    return true;
}

bool loadSource(MissionRuntime *rt, const std::string &use, std::string &out, std::string &origin)
{
    if (!validName(use.c_str())) return false;
    std::string cands[3];
    int nc = 0;
    if (!rt->dir.empty()) {
        cands[nc++] = rt->dir + "/" + use + ".luau";
        cands[nc++] = rt->dir + "/scripts/" + use + ".luau";
    }
    if (!rt->modDir.empty()) cands[nc++] = rt->modDir + "/scripts/" + use + ".luau";
    for (int i = 0; i < nc; i++) {
        uint8_t *data = nullptr;   // through the package layer: a mod's scripts may live inside its .pmod
        size_t n = 0;
        if (pkgReadFile(cands[i].c_str(), &data, &n)) {
            out.assign((const char *)data, n);
            pkgFree(data);
            origin = cands[i];
            return true;
        }
    }
    for (const MissionLibScript *s = g_missionLibScripts; s->name; s++) {
        if (use == s->name) { out = s->source; origin = std::string("library/") + use; return true; }
    }
    return false;
}

// The setup prop number of the added prop `uuid` names (-1: not present on this difficulty); logs each resolution once per use.
int resolveUuid(MissionRuntime *rt, const char *uuid, const char *what)
{
    int idx = -1;
    int kind = missionFindAdded(rt->m, uuid, &idx);
    if (!kind) return -2;
    const char *name = kind == 1 ? rt->m->guards[idx].name : rt->m->objects[idx].name;
    int prop = kind == 1 ? rt->m->guards[idx].setup_prop : rt->m->objects[idx].setup_prop;
    rt->log("%s: \"%s\" %s -> setup prop %d%s", what, name, uuid, prop, prop < 0 ? " (not present on this difficulty)" : "");
    return prop;
}

// The level's own prop count: the first added prop's number, else every record is the level's.
int originalPropCount(MissionRuntime *rt)
{
    int n = rt->host.prop_count ? rt->host.prop_count(rt->host.ud) : 0, first = -1;
    for (int i = 0; i < rt->m->nguards; i++)
        if (rt->m->guards[i].op == MISSION_OP_ADD && rt->m->guards[i].setup_prop >= 0 && (first < 0 || rt->m->guards[i].setup_prop < first)) first = rt->m->guards[i].setup_prop;
    for (int i = 0; i < rt->m->nobjects; i++)
        if (rt->m->objects[i].op == MISSION_OP_ADD && rt->m->objects[i].setup_prop >= 0 && (first < 0 || rt->m->objects[i].setup_prop < first)) first = rt->m->objects[i].setup_prop;
    return first >= 0 ? first : n;
}

void pushProps(MissionRuntime *rt, lua_State *T, const MissionScript &ms)
{
    lua_createtable(T, 0, ms.nprops);
    for (int i = 0; i < ms.nprops; i++) {
        const MissionProp &p = ms.props[i];
        if (p.kind == MISSION_VAL_STRING && missionUuidValid(p.str) && missionFindAdded(rt->m, p.str, nullptr)) {
            std::string what = std::string("script ") + ms.use + " prop " + p.name;
            lua_pushnumber(T, resolveUuid(rt, p.str, what.c_str()));       // an added prop: its setup prop number
        }
        else if (p.kind == MISSION_VAL_STRING) lua_pushstring(T, p.str);
        else if (p.kind == MISSION_VAL_BOOL) lua_pushboolean(T, p.num != 0);
        else lua_pushnumber(T, p.num);
        lua_setfield(T, -2, p.name);
    }
}

void pushScriptTarget(MissionRuntime *rt, lua_State *T, const MissionScript &ms)
{
    switch (ms.kind) {
    case MISSION_TGT_PROP:
        if (ms.uuid[0]) {
            std::string what = std::string("script ") + ms.use + " target";
            int p = resolveUuid(rt, ms.uuid, what.c_str());
            if (p == -2) rt->log("script %s: target uuid %s is not an added prop: treated as missing", ms.use, ms.uuid);
            pushTarget(T, "prop", p < 0 ? -1 : p);        // no "prop" field = a missing prop (the host checks every number)
        } else {
            const int orig = originalPropCount(rt);
            const bool added = orig > 0 && ms.prop >= orig;
            if (added) rt->log("script %s: target prop %d is an added prop: reference it by its uuid (treated as missing)", ms.use, ms.prop);
            pushTarget(T, "prop", added ? -1 : ms.prop);
        }
        break;
    case MISSION_TGT_PAD:
        pushTarget(T, "pad", -1);
        if (ms.pad.is_new) lua_pushstring(T, ms.pad.id); else lua_pushnumber(T, ms.pad.num);
        lua_setfield(T, -2, "pad");
        break;
    case MISSION_TGT_ZONE:
        pushTarget(T, "zone", -1);
        lua_pushstring(T, ms.zone);
        lua_setfield(T, -2, "zone");
        for (int i = 0; i < rt->m->nzones; i++) {
            const MissionZone &z = rt->m->zones[i];
            if (strcmp(z.id, ms.zone)) continue;
            lua_pushnumber(T, z.pos[0]); lua_setfield(T, -2, "x");
            lua_pushnumber(T, z.pos[1]); lua_setfield(T, -2, "y");
            lua_pushnumber(T, z.pos[2]); lua_setfield(T, -2, "z");
            lua_pushnumber(T, z.radius); lua_setfield(T, -2, "radius");
        }
        break;
    default:
        pushTarget(T, "mission", -1);
        break;
    }
}

bool startScript(MissionRuntime *rt, const MissionScript &ms, int index)
{
    std::string src, origin;
    if (!loadSource(rt, ms.use, src, origin)) {
        rt->log("script %s: not found (no library script and no scripts/%s.luau)", ms.use, ms.use);
        return false;
    }
    lua_CompileOptions opts = {};
    opts.optimizationLevel = 1;
    opts.debugLevel = 1;
    size_t bsz = 0;
    char *bc = luau_compile(src.data(), src.size(), &opts, &bsz);
    if (!bc) return false;

    auto sc = std::make_unique<Script>();
    sc->name = std::string(ms.use) + "#" + std::to_string(index);
    lua_State *T = lua_newthread(rt->L);
    sc->ref = lua_ref(rt->L, -1);
    lua_pop(rt->L, 1);
    sc->T = T;
    luaL_sandboxthread(T);
    std::string chunk = "=" + origin;
    int lr = luau_load(T, chunk.c_str(), bc, bsz, 0);
    free(bc);
    if (lr != 0) {
        rt->log("script %s: %s", sc->name.c_str(), lua_tostring(T, -1));
        lua_unref(rt->L, sc->ref);
        return false;
    }
    // props and target as globals of this script's own environment
    pushProps(rt, T, ms);
    lua_setglobal(T, "props");
    pushScriptTarget(rt, T, ms);
    lua_setglobal(T, "target");
    lua_setthreaddata(T, sc.get());
    rt->log("script %s loaded from %s", sc->name.c_str(), origin.c_str());
    rt->scripts.push_back(std::move(sc));
    rt->started++;
    return true;
}

void resumeScript(MissionRuntime *rt, Script &s, const Event *ev)
{
    if (s.st == St::Done || s.cancelled) return;
    int nargs = 0;
    if (ev) { pushEvent(s.T, *ev); nargs = 1; }
    s.st = St::Run;
    // The first resume of a mission script is its load (requires, its tables, up to its first wait) and the first one
    // of its start() task is start(): both run while the level loads, with kLoadBudget each. The rest is frame time:
    // all the scripts share kFrameBudget per frame (missionRtTick warns), and a resume still running after kStuckWarn
    // is named in the log. Nothing is cut.
    const bool loading = !s.loaded && (!s.task || s.isStart);
    s.loaded = true;
    if (loading) setBudget(rt, "script " + s.name + (s.isStart ? "" : ": its load"), kLoadBudget);
    else setBudget(rt, "script " + s.name, kStuckWarn);
    const Clock::time_point t0 = Clock::now();
    int r = lua_resume(s.T, nullptr, nargs);
    const double took = std::chrono::duration<double>(Clock::now() - t0).count();
    if (loading) {
        if (took > kLoadBudget) rt->log("%s took %.3g s (budget %.3g s)", rt->budgetWhat.c_str(), took, kLoadBudget);
    } else {
        rt->frameUsed += took;
        if (took > rt->frameTop) { rt->frameTop = took; rt->frameTopName = s.name; }
    }
    if (r == LUA_YIELD) {
        lua_settop(s.T, 0);
        if (s.st == St::Run) { s.st = St::Sleep; s.wake = rt->time; }   // a bare yield: next tick
    } else if (r == LUA_OK) {
        s.st = St::Done;
        rt->log("script %s finished", s.name.c_str());
    } else {
        const char *msg = lua_tostring(s.T, -1);
        rt->log("script %s: error: %s", s.name.c_str(), msg ? msg : "?");
        s.st = St::Done;
    }
    if (s.cancelled) s.st = St::Done;   // a task that cancelled itself
}

Script *newTask(MissionRuntime *rt, const std::string &name)
{
    auto sc = std::make_unique<Script>();
    sc->name = name;
    sc->task = true;
    sc->id = rt->nextId++;
    lua_State *T = lua_newthread(rt->L);
    sc->ref = lua_ref(rt->L, -1);
    lua_pop(rt->L, 1);
    sc->T = T;
    lua_setthreaddata(T, sc.get());
    sc->name += "#" + std::to_string(sc->id);
    Script *raw = sc.get();
    rt->scripts.push_back(std::move(sc));
    return raw;
}

// Callbacks of on(): each match runs the function as a task of its own, with the event as its argument.
// Indexed loops throughout: a callback may spawn tasks, subscribe or unsubscribe.
void dispatch(MissionRuntime *rt)
{
    for (int guard = 0; !rt->queue.empty() && guard < 256; guard++) {
        Event e = std::move(rt->queue.front());
        rt->queue.erase(rt->queue.begin());
        for (size_t i = 0; i < rt->scripts.size(); i++) {
            Script *sp = rt->scripts[i].get();
            if (sp->st == St::Event && matches(*sp, e)) resumeScript(rt, *sp, &e);
        }
        for (size_t i = 0; i < rt->subs.size(); i++) {
            if (!rt->subs[i].active || !matchFilter(rt->subs[i].name, rt->subs[i].filter, e)) continue;
            const int ref = rt->subs[i].fnref;
            Script *t = newTask(rt, "on:" + e.name);
            lua_getref(t->T, ref);
            resumeScript(rt, *t, &e);
        }
    }
    rt->queue.clear();
}

// Drop finished tasks and unsubscribed callbacks (never inside a resume).
void reap(MissionRuntime *rt)
{
    for (size_t i = rt->scripts.size(); i-- > 0;) {
        Script &s = *rt->scripts[i];
        if (!s.task || s.st != St::Done) continue;
        lua_unref(rt->L, s.ref);
        rt->scripts.erase(rt->scripts.begin() + (long)i);
    }
    for (size_t i = rt->subs.size(); i-- > 0;)
        if (!rt->subs[i].active) rt->subs.erase(rt->subs.begin() + (long)i);
}

void scan(MissionRuntime *rt)
{
    const MissionHost &h = rt->host;
    int np = h.player_count ? h.player_count(h.ud) : 0;
    if (np > kMaxPlayers) np = kMaxPlayers;
    bool base = !rt->scanned;

    // pickups: an item a player did not have a moment ago
    if (h.has) {
        for (int p = 0; p < np; p++) {
            for (int it = 1; it < kItemScan; it++) {
                bool now = h.has(h.ud, p, it) != 0;
                if (now && !rt->had[p][it] && !base) raise(rt, Event{ "pickup", { num("item", it), num("player", p) } });
                rt->had[p][it] = now;
            }
        }
    }

    // props: doors and guards
    int n = h.prop_count ? h.prop_count(h.ud) : 0;
    if ((int)rt->propType.size() != n) {
        rt->propType.assign(n, 0);
        rt->doorPrev.assign(n, -1);
        rt->deadPrev.assign(n, 0);
        for (int i = 0; i < n; i++) rt->propType[i] = (signed char)(h.prop_type ? h.prop_type(h.ud, i) : 0);
    }
    for (int i = 0; i < n; i++) {
        if (rt->propType[i] == 1 && h.door_state) {          // PROPDEF_DOOR
            int s = h.door_state(h.ud, i);
            if (s != rt->doorPrev[i]) {
                if (!base && rt->doorPrev[i] != -1)
                    raise(rt, Event{ "door", { num("prop", i), str("state", s == MH_DOOR_OPENED ? "open" : s == MH_DOOR_CLOSED ? "closed" : "moving") } });
                rt->doorPrev[i] = (signed char)s;
            }
        } else if (rt->propType[i] == 9 && h.guard_dead) {   // PROPDEF_GUARD
            int d = h.guard_dead(h.ud, i) ? 1 : 0;
            if (d && !rt->deadPrev[i] && !base) raise(rt, Event{ "death", { num("prop", i) } });
            rt->deadPrev[i] = (signed char)d;
        }
    }

    // zones
    int nz = rt->m->nzones;
    if ((int)rt->inZone.size() != nz * kMaxPlayers) rt->inZone.assign((size_t)nz * kMaxPlayers, 0);
    for (int z = 0; z < nz; z++) {
        const MissionZone &zn = rt->m->zones[z];
        for (int p = 0; p < np; p++) {
            float q[3];
            bool in = false;
            if (h.player_pos && h.player_pos(h.ud, p, q)) {
                double dx = q[0] - zn.pos[0], dy = q[1] - zn.pos[1], dz = q[2] - zn.pos[2];
                in = std::sqrt(dx * dx + dy * dy + dz * dz) <= zn.radius;
            }
            unsigned char &was = rt->inZone[(size_t)z * kMaxPlayers + p];
            if (in && !was) raise(rt, Event{ "zone", { str("id", zn.id), num("player", p) } });
            was = in ? 1 : 0;
        }
    }
    rt->scanned = true;
}

}  // namespace

// =================================================================== API
MissionRuntime *missionRtCreate(const Mission *m, const MissionHost *host, const char *mission_dir, const char *mod_dir, char *err, size_t errlen)
{
    auto rt = std::make_unique<MissionRuntime>();
    rt->m = m;
    rt->host = *host;
    if (mission_dir) rt->dir = mission_dir;
    if (mod_dir) rt->modDir = mod_dir;
    for (int i = 0; i < m->nobjectives; i++) {
        Objective o;
        o.id = m->objectives[i].id;
        o.text = m->objectives[i].text;
        o.difficulty = m->objectives[i].difficulty;
        rt->objs.push_back(std::move(o));
    }
    rt->L = lua_newstate(allocFn, &rt->alloc);
    if (!rt->L) {
        snprintf(err, errlen, "could not create the Luau VM");
        return nullptr;
    }
    lua_State *L = rt->L;
    lua_callbacks(L)->userdata = rt.get();
    lua_callbacks(L)->interrupt = onInterrupt;
    luaL_openlibs(L);
    {   // math.random differs from one run to the next (Luau seeds it with a constant)
        lua_getglobal(L, "math");
        lua_getfield(L, -1, "randomseed");
        lua_pushnumber(L, (double)(std::chrono::steady_clock::now().time_since_epoch().count() & 0x7fffffff));
        lua_call(L, 1, 0);
        lua_pop(L, 1);
    }
    for (const char *drop : { "os", "io", "debug", "dofile", "loadfile", "collectgarbage" }) {
        lua_pushnil(L);
        lua_setglobal(L, drop);
    }
    struct Fn { const char *name; lua_CFunction f; };
    for (const Fn &f : { Fn{ "print", l_print }, Fn{ "message", l_message }, Fn{ "say", l_say }, Fn{ "give", l_give }, Fn{ "take", l_take },
                         Fn{ "has", l_has }, Fn{ "wait", l_wait }, Fn{ "event", l_event }, Fn{ "door", l_door }, Fn{ "guard", l_door },
                         Fn{ "player", l_player }, Fn{ "clock", l_clock }, Fn{ "spawn", l_spawn }, Fn{ "on", l_on },
                         Fn{ "require", l_require } }) {
        lua_pushcfunction(L, f.f, f.name);
        lua_setglobal(L, f.name);
    }
    auto table = [&](const char *name, std::initializer_list<Fn> fns) {
        lua_createtable(L, 0, (int)fns.size());
        for (const Fn &f : fns) {
            lua_pushcfunction(L, f.f, f.name);
            lua_setfield(L, -2, f.name);
        }
        lua_setglobal(L, name);
    };
    table("objectives", { Fn{ "add", l_obj_add }, Fn{ "complete", l_obj_complete }, Fn{ "is_complete", l_obj_is_complete },
                          Fn{ "fail", l_obj_fail }, Fn{ "is_failed", l_obj_is_failed }, Fn{ "all_complete", l_obj_all },
                          Fn{ "reset", l_obj_reset } });
    table("alarm", { Fn{ "trigger", l_alarm_trigger } });
    table("mission", { Fn{ "win", l_win }, Fn{ "fail", l_fail }, Fn{ "difficulty", l_difficulty } });
    luaL_newmetatable(L, kTargetMT);
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    for (const Fn &f : { Fn{ "lock", t_lock }, Fn{ "unlock", t_unlock }, Fn{ "open", t_open }, Fn{ "close", t_close },
                         Fn{ "is_open", t_is_open }, Fn{ "is_dead", t_is_dead }, Fn{ "set_ai", t_set_ai },
                         Fn{ "near", t_near }, Fn{ "distance", t_distance }, Fn{ "distance_to", t_distance_to },
                         Fn{ "follow", t_follow }, Fn{ "stop", t_stop }, Fn{ "surrender", t_surrender }, Fn{ "stand", t_stand },
                         Fn{ "go_to", t_go_to }, Fn{ "is_moving", t_is_moving }, Fn{ "face", t_face }, Fn{ "play_anim", t_play_anim },
                         Fn{ "make_usable", t_make_usable }, Fn{ "was_used", t_was_used }, Fn{ "keep_body", t_keep_body },
                         Fn{ "aim_at", t_aim_at }, Fn{ "shoot", t_shoot }, Fn{ "sees_player", t_sees_player },
                         Fn{ "was_shot_at", t_was_shot_at }, Fn{ "guards_near", t_guards_near }, Fn{ "hits", t_hits },
                         Fn{ "say", t_say }, Fn{ "pos", t_pos }, Fn{ "health", t_health }, Fn{ "max_health", t_max_health },
                         Fn{ "set_health", t_set_health }, Fn{ "set_max_health", t_set_max_health },
                         Fn{ "set_revivable", t_set_revivable }, Fn{ "is_down", t_is_down } }) {
        lua_pushcfunction(L, f.f, f.name);
        lua_setfield(L, -2, f.name);
    }
    lua_pop(L, 1);
    for (const auto &mt : { std::make_pair(kTaskMT, Fn{ "cancel", task_cancel }), std::make_pair(kConnMT, Fn{ "off", conn_off }) }) {
        luaL_newmetatable(L, mt.first);
        lua_pushvalue(L, -1);
        lua_setfield(L, -2, "__index");
        lua_pushcfunction(L, mt.second.f, mt.second.name);
        lua_setfield(L, -2, mt.second.name);
        lua_pop(L, 1);
    }
    luaL_sandbox(L);

    for (int i = 0; i < m->nscripts; i++) startScript(rt.get(), m->scripts[i], i);
    return rt.release();
}

void missionRtDestroy(MissionRuntime *rt)
{
    if (!rt) return;
    if (rt->L) lua_close(rt->L);
    delete rt;
}

void missionRtTick(MissionRuntime *rt, double dt)
{
    if (!rt) return;
    rt->time += dt > 0 ? dt : 0;
    rt->frameUsed = 0;
    rt->frameTop = 0;
    if (rt->first) {
        rt->first = false;
        const size_t n = rt->scripts.size();
        for (size_t i = 0; i < n; i++) resumeScript(rt, *rt->scripts[i], nullptr);   // each one's load: up to its first wait
        // Then each script's start(), if it has one (BepInEx / Unity's Start: every script is loaded, the level's guards
        // and objects exist). As a task of its own: start() may wait().
        for (size_t i = 0; i < n; i++) {
            Script *sp = rt->scripts[i].get();
            if (sp->task) continue;
            lua_getglobal(sp->T, "start");
            if (!lua_isfunction(sp->T, -1)) { lua_pop(sp->T, 1); continue; }
            Script *t = newTask(rt, sp->name + ":start()");
            t->isStart = true;
            lua_xmove(sp->T, t->T, 1);
            resumeScript(rt, *t, nullptr);
        }
        rt->frameUsed = 0;                                             // the load is not frame time
        rt->frameTop = 0;
        scan(rt);                                                      // the baseline: no events from it
        rt->lastScan = rt->time;
        raise(rt, Event{ "start", {} });
    } else if (rt->time - rt->lastScan >= kScanEvery) {
        rt->lastScan = rt->time;
        scan(rt);
    }
    dispatch(rt);
    for (size_t i = 0; i < rt->scripts.size(); i++) {
        Script *sp = rt->scripts[i].get();
        if (sp->st == St::Sleep && rt->time >= sp->wake) resumeScript(rt, *sp, nullptr);
    }
    dispatch(rt);
    for (size_t i = 0; i < rt->hits.size();) {
        if (rt->time < rt->hits[i].at) { i++; continue; }
        if (rt->host.guard_hit) rt->host.guard_hit(rt->host.ud, rt->hits[i].target);
        rt->hits.erase(rt->hits.begin() + (long)i);
    }
    if (!rt->follows.empty() && rt->time - rt->lastFollow >= MISSION_FOLLOW_EVERY) {
        rt->lastFollow = rt->time;
        followTick(rt);
    }
    reap(rt);
    lua_gc(rt->L, LUA_GCSTEP, 4);
    if (rt->frameUsed > kFrameBudget && rt->time - rt->lastFrameWarn >= kFrameWarnEvery) {
        rt->lastFrameWarn = rt->time;
        rt->log("scripts: %.2f ms this frame (budget %.2f ms; the longest: %s, %.2f ms)", rt->frameUsed * 1000.0,
                kFrameBudget * 1000.0, rt->frameTopName.c_str(), rt->frameTop * 1000.0);
    }
}

void missionRtRaise(MissionRuntime *rt, const char *name, const MissionEvField *f, int n)
{
    if (!rt || !name) return;
    Event e;
    e.name = name;
    for (int i = 0; i < n; i++) {
        if (f[i].str) e.f.push_back(str(f[i].key, f[i].str));
        else e.f.push_back(num(f[i].key, f[i].num));
    }
    raise(rt, std::move(e));
}

void missionRtEnd(MissionRuntime *rt, int win)
{
    if (!rt || rt->ended) return;
    rt->ended = true;
    raise(rt, Event{ "end", { num("win", win ? 1 : 0) } });
    dispatch(rt);
}

int missionRtObjectiveCount(const MissionRuntime *rt) { return rt ? (int)rt->objs.size() : 0; }
const char *missionRtObjectiveText(const MissionRuntime *rt, int i) { return (rt && i >= 0 && i < (int)rt->objs.size()) ? rt->objs[i].text.c_str() : ""; }
int missionRtObjectiveDifficulty(const MissionRuntime *rt, int i) { return (rt && i >= 0 && i < (int)rt->objs.size()) ? rt->objs[i].difficulty : 0; }
int missionRtObjectiveComplete(const MissionRuntime *rt, int i) { return (rt && i >= 0 && i < (int)rt->objs.size()) ? rt->objs[i].done : 0; }

int missionRtObjectiveStatus(const MissionRuntime *rt, int i)
{
    if (!rt || i < 0 || i >= (int)rt->objs.size()) return 0;
    return rt->objs[i].done ? 1 : rt->objs[i].failed ? 2 : 0;
}

int missionRtObjectiveQuiet(const MissionRuntime *rt, int i)
{
    return rt && i >= 0 && i < (int)rt->objs.size() && rt->objs[i].quiet;
}

int missionRtAllComplete(const MissionRuntime *rt, int difficulty)
{
    if (!rt) return 0;
    for (const Objective &o : rt->objs)
        if (o.difficulty <= difficulty && !o.done) return 0;
    return 1;
}

int missionRtCompleteObjective(MissionRuntime *rt, const char *id)
{
    if (!rt) return 0;
    bool ok = completeObjective(rt, id);
    dispatch(rt);
    return ok;
}

int missionRtScriptCount(const MissionRuntime *rt) { return rt ? rt->started : 0; }

int missionRtScriptsAlive(const MissionRuntime *rt)
{
    int n = 0;
    if (rt)
        for (const auto &sp : rt->scripts)
            if (!sp->task && sp->st != St::Done) n++;
    return n;
}
