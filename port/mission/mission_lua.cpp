// The Luau runtime of a custom mission: see mission_lua.h.
#include "mission_lua.h"

#include <chrono>
#include <cstdarg>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "lua.h"
#include "lualib.h"
#include "luacode.h"

namespace {

using Clock = std::chrono::steady_clock;

constexpr size_t kMemCap = 16u * 1024u * 1024u;   // the VM never holds more than this
constexpr double kResumeBudget = 0.004;           // seconds one script may run per resume
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
};

struct Objective {
    std::string id, text;
    int difficulty = 0;
    bool done = false;
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
    double time = 0, lastScan = -1;
    bool first = true, ended = false, scanned = false;
    Clock::time_point deadline;
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
    MissionRuntime *rt = rtOf(L);
    if (rt && Clock::now() > rt->deadline) {
        lua_checkstack(L, 1);
        luaL_error(L, "script ran over its time budget");
    }
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

bool matches(const Script &s, const Event &e)
{
    if (s.evName != e.name) return false;
    for (const Field &want : s.filter) {
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
    if (!o->done) {
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
    if (!completeObjective(rt, id)) luaL_error(L, "no objective '%s'", id);
    return 0;
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

int l_win(lua_State *L)
{
    MissionRuntime *rt = rtOf(L);
    rt->log("mission.win()");
    if (rt->host.win) rt->host.win(rt->host.ud);
    /* the host call may take long (it ends the level): it is not the script's time */
    rt->deadline = Clock::now() + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(kResumeBudget));
    return 0;
}

// ------------------------------------------------------------ targets
const char kTargetMT[] = "ge.target";

int tgtProp(lua_State *L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_getfield(L, 1, "prop");
    int p = lua_isnumber(L, -1) ? (int)lua_tonumber(L, -1) : -1;
    lua_pop(L, 1);
    return p;
}

int t_door(lua_State *L, int cmd)
{
    MissionRuntime *rt = rtOf(L);
    int p = tgtProp(L);
    if (p < 0) luaL_error(L, "this target is not a prop");
    if (rt->host.door_cmd) rt->host.door_cmd(rt->host.ud, p, cmd);
    return 0;
}
int t_lock(lua_State *L) { return t_door(L, MH_DOOR_LOCK); }
int t_unlock(lua_State *L) { return t_door(L, MH_DOOR_UNLOCK); }
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

bool targetPos(lua_State *L, MissionRuntime *rt, float xyz[3], float *radius)
{
    *radius = 0;
    lua_getfield(L, 1, "kind");
    std::string kind = lua_isstring(L, -1) ? lua_tostring(L, -1) : "";
    lua_pop(L, 1);
    if (kind == "prop") {
        int p = tgtProp(L);
        return p >= 0 && rt->host.prop_pos && rt->host.prop_pos(rt->host.ud, p, xyz);
    }
    if (kind == "zone") {
        const char *keys[3] = { "x", "y", "z" };
        for (int i = 0; i < 3; i++) {
            lua_getfield(L, 1, keys[i]);
            xyz[i] = (float)lua_tonumber(L, -1);
            lua_pop(L, 1);
        }
        lua_getfield(L, 1, "radius");
        *radius = (float)lua_tonumber(L, -1);
        lua_pop(L, 1);
        return true;
    }
    return false;
}

double nearestPlayer(MissionRuntime *rt, const float c[3])
{
    double best = 1e30;
    int n = rt->host.player_count ? rt->host.player_count(rt->host.ud) : 0;
    for (int p = 0; p < n && p < kMaxPlayers; p++) {
        float q[3];
        if (!rt->host.player_pos || !rt->host.player_pos(rt->host.ud, p, q)) continue;
        double dx = q[0] - c[0], dy = q[1] - c[1], dz = q[2] - c[2];
        double d = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (d < best) best = d;
    }
    return best;
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
    if (!targetPos(L, rt, c, &r)) { lua_pushboolean(L, 0); return 1; }
    if (radius <= 0) radius = r > 0 ? r : 150;
    lua_pushboolean(L, nearestPlayer(rt, c) <= radius);
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
        FILE *f = fopen(cands[i].c_str(), "rb");
        if (f) {
            char buf[4096];
            size_t n;
            out.clear();
            while ((n = fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
            fclose(f);
            origin = cands[i];
            return true;
        }
    }
    for (const MissionLibScript *s = g_missionLibScripts; s->name; s++) {
        if (use == s->name) { out = s->source; origin = std::string("library/") + use; return true; }
    }
    return false;
}

void pushProps(lua_State *T, const MissionScript &ms)
{
    lua_createtable(T, 0, ms.nprops);
    for (int i = 0; i < ms.nprops; i++) {
        const MissionProp &p = ms.props[i];
        if (p.kind == MISSION_VAL_STRING) lua_pushstring(T, p.str);
        else if (p.kind == MISSION_VAL_BOOL) lua_pushboolean(T, p.num != 0);
        else lua_pushnumber(T, p.num);
        lua_setfield(T, -2, p.name);
    }
}

void pushScriptTarget(MissionRuntime *rt, lua_State *T, const MissionScript &ms)
{
    switch (ms.kind) {
    case MISSION_TGT_PROP:
        pushTarget(T, "prop", ms.prop);
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
    pushProps(T, ms);
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
    if (s.st == St::Done) return;
    int nargs = 0;
    if (ev) { pushEvent(s.T, *ev); nargs = 1; }
    s.st = St::Run;
    rt->deadline = Clock::now() + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(kResumeBudget));
    int r = lua_resume(s.T, nullptr, nargs);
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
}

void dispatch(MissionRuntime *rt)
{
    for (int guard = 0; !rt->queue.empty() && guard < 256; guard++) {
        Event e = std::move(rt->queue.front());
        rt->queue.erase(rt->queue.begin());
        for (auto &sp : rt->scripts)
            if (sp->st == St::Event && matches(*sp, e)) resumeScript(rt, *sp, &e);
    }
    rt->queue.clear();
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
    for (const char *drop : { "os", "io", "debug", "require", "dofile", "loadfile", "collectgarbage" }) {
        lua_pushnil(L);
        lua_setglobal(L, drop);
    }
    struct Fn { const char *name; lua_CFunction f; };
    for (const Fn &f : { Fn{ "print", l_print }, Fn{ "message", l_message }, Fn{ "give", l_give }, Fn{ "take", l_take },
                         Fn{ "has", l_has }, Fn{ "wait", l_wait }, Fn{ "event", l_event }, Fn{ "door", l_door }, Fn{ "guard", l_door } }) {
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
                          Fn{ "all_complete", l_obj_all } });
    table("alarm", { Fn{ "trigger", l_alarm_trigger } });
    table("mission", { Fn{ "win", l_win } });
    luaL_newmetatable(L, kTargetMT);
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    for (const Fn &f : { Fn{ "lock", t_lock }, Fn{ "unlock", t_unlock }, Fn{ "open", t_open }, Fn{ "close", t_close },
                         Fn{ "is_open", t_is_open }, Fn{ "is_dead", t_is_dead }, Fn{ "set_ai", t_set_ai },
                         Fn{ "near", t_near }, Fn{ "distance", t_distance } }) {
        lua_pushcfunction(L, f.f, f.name);
        lua_setfield(L, -2, f.name);
    }
    lua_pop(L, 1);
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
    if (rt->first) {
        rt->first = false;
        for (auto &sp : rt->scripts) resumeScript(rt, *sp, nullptr);   // run each up to its first wait
        scan(rt);                                                      // the baseline: no events from it
        rt->lastScan = rt->time;
        raise(rt, Event{ "start", {} });
    } else if (rt->time - rt->lastScan >= kScanEvery) {
        rt->lastScan = rt->time;
        scan(rt);
    }
    dispatch(rt);
    for (auto &sp : rt->scripts)
        if (sp->st == St::Sleep && rt->time >= sp->wake) resumeScript(rt, *sp, nullptr);
    dispatch(rt);
    lua_gc(rt->L, LUA_GCSTEP, 4);
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
            if (sp->st != St::Done) n++;
    return n;
}
