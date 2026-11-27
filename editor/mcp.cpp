#include "mcp.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <system_error>
#include <thread>
#include <vector>

#include "cJSON.h"
#include "item_names.h"
#include "launch.h"
#include "mission_apply.h"
#include "mission_setup.h"
#include "model_names.h"
#include "script_props.h"
#include "ge_texpacks.h"

// ---------------------------------------------------------------------------------------------
// JSON helpers

namespace {

struct J {   // owning cJSON handle
    cJSON *p;
    explicit J(cJSON *x = nullptr) : p(x) {}
    ~J() { if (p) cJSON_Delete(p); }
    J(const J &) = delete;
    J &operator=(const J &) = delete;
    cJSON *release() { cJSON *x = p; p = nullptr; return x; }
};

std::string print(const cJSON *j)
{
    char *s = cJSON_PrintUnformatted(j);
    std::string out = s ? s : "null";
    free(s);
    return out;
}

cJSON *rpcResult(const cJSON *id, cJSON *result)
{
    cJSON *r = cJSON_CreateObject();
    cJSON_AddStringToObject(r, "jsonrpc", "2.0");
    cJSON_AddItemToObject(r, "id", id ? cJSON_Duplicate(id, 1) : cJSON_CreateNull());
    cJSON_AddItemToObject(r, "result", result);
    return r;
}

cJSON *rpcError(const cJSON *id, int code, const std::string &msg)
{
    cJSON *r = cJSON_CreateObject();
    cJSON_AddStringToObject(r, "jsonrpc", "2.0");
    cJSON_AddItemToObject(r, "id", id ? cJSON_Duplicate(id, 1) : cJSON_CreateNull());
    cJSON *e = cJSON_AddObjectToObject(r, "error");
    cJSON_AddNumberToObject(e, "code", code);
    cJSON_AddStringToObject(e, "message", msg.c_str());
    return r;
}

// ---- tool plumbing ----

struct ToolResult {
    bool error = false;
    std::string text;
};

ToolResult fail(const std::string &msg)
{
    ToolResult r; r.error = true; r.text = msg;
    return r;
}

ToolResult okJson(cJSON *obj)   // takes ownership
{
    ToolResult r; r.text = print(obj);
    cJSON_Delete(obj);
    return r;
}

int argInt(const cJSON *a, const char *k, int def)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(a, k);
    if (cJSON_IsNumber(v)) return (int)v->valuedouble;
    if (cJSON_IsString(v)) { char *e = nullptr; long x = strtol(v->valuestring, &e, 0); if (e && !*e && e != v->valuestring) return (int)x; }
    return def;
}

std::string argStr(const cJSON *a, const char *k, const char *def = "")
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(a, k);
    return cJSON_IsString(v) ? v->valuestring : def;
}

bool argHas(const cJSON *a, const char *k) { return cJSON_GetObjectItemCaseSensitive(a, k) != nullptr; }

// An item as a result: kind + prop (original) or op (added); an added one also names its uuid and display name (the id scripts use).
cJSON *refJson(const ItemRef &r, const Doc *d = nullptr)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "kind", r.guard ? "guard" : "object");
    cJSON_AddNumberToObject(o, r.added ? "op" : "prop", r.idx);
    if (d && r.added && r.valid && d->hasMission && r.idx >= 0 && r.idx < (r.guard ? d->mission.nguards : d->mission.nobjects)) {
        cJSON_AddStringToObject(o, "uuid", r.guard ? d->mission.guards[r.idx].uuid : d->mission.objects[r.idx].uuid);
        const char *nm = r.guard ? d->mission.guards[r.idx].name : d->mission.objects[r.idx].name;
        if (nm[0]) cJSON_AddStringToObject(o, "name", nm);
    }
    return o;
}

// "prop": N (an original prop), "uuid": the uuid of an added item, or "kind": "guard"|"object" + "op": N (an added one,
// its index in the mission's guards/objects list).
// `uuidKey` is the argument that names an added item by its uuid ("uuid"; duplicate uses "from_uuid" because its "uuid" is the new copy's).
bool itemFromArgs(Doc &d, const cJSON *a, ItemRef &out, std::string &err, const char *uuidKey = "uuid")
{
    if (argHas(a, "prop")) return d.itemFromProp(argInt(a, "prop", -1), out, err);
    if (uuidKey && argHas(a, uuidKey)) {
        int ty = 0;
        if (!d.propByUuid(argStr(a, uuidKey), out, ty)) { err = "no prop the mission adds has uuid '" + argStr(a, uuidKey) + "'"; return false; }
        return true;
    }
    std::string kind = argStr(a, "kind");
    if ((kind == "guard" || kind == "object") && argHas(a, "op")) {
        out.valid = true; out.added = true; out.guard = kind == "guard"; out.idx = argInt(a, "op", -1);
        return true;
    }
    err = "give \"prop\" (an original setup prop index), \"uuid\" (an added item), or \"kind\" (guard|object) with \"op\" (index of an added item)";
    return false;
}

cJSON *padRefJson(const Doc &d, const PadSpec &s)
{
    cJSON *o = cJSON_CreateObject();
    if (s.atPoint) cJSON_AddStringToObject(o, "pad", "(new pad at the point)");
    else if (s.isNew) cJSON_AddStringToObject(o, "pad", s.id.c_str());
    else cJSON_AddNumberToObject(o, "pad", s.num);
    (void)d;
    return o;
}

#include "mcp_tools_mission.inc"

// ---- the tools ----

ToolResult toolLevelDescribe(Doc &d, const cJSON *a)
{
    const std::string detail = argStr(a, "detail", "summary");
    if (detail == "links") return toolDescribeLinks(d, a);
    if (detail == "names") return toolDescribeNames(d, a);
    ToolResult r; r.text = d.describe(detail);
    if (detail == "props" || detail == "items") r.text = msAnnotateGuards(r.text);
    return r;
}

ToolResult toolLevelOpen(Doc &d, const cJSON *a)
{
    std::string dir = argStr(a, "dir"), lvl = argStr(a, "level"), err;
    if (!lvl.empty()) {
        if (!d.openLevelByName(lvl, cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(a, "reexport")), err)) return fail(err);
    } else {
        if (dir.empty()) return fail("give \"level\" (a solo level name, e.g. bunker1) or \"dir\" (an export directory)");
        if (!d.openLevel(dir, err)) return fail(err);
    }
    ToolResult r; r.text = d.describe("summary");
    return r;
}

ToolResult toolMissionOpen(Doc &d, const cJSON *a)
{
    std::string err;
    if (d.hasMission && d.dirty && (argHas(a, "path") || argHas(a, "new") || argHas(a, "name") || argHas(a, "level")) &&
        !cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(a, "discard")))
        return fail("the open mission has unsaved changes: mission_save first, or pass \"discard\": true to replace it");
    if (argHas(a, "export")) {
        if (!d.openLevel(argStr(a, "export"), err)) return fail(err);
    }
    if (argHas(a, "path")) {
        if (!d.openMission(argStr(a, "path"), err)) return fail(err);
        std::string e2;
        if (d.openModOfOpenMission(e2) < 0) d.modNotice = e2;   // a mission of a mod opens its mod too
    } else if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(a, "new")) || argHas(a, "name") || argHas(a, "level")) {
        const bool emptied = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(a, "emptied"));
        if (!d.newMission(argStr(a, "name", "New mission"), argStr(a, "level"), err, emptied)) return fail(err);
    } else {
        return fail("give \"path\" to open a mission.json, or \"new\": true with \"level\" (e.g. bunker1) and \"name\"");
    }
    // the mission's level, from the export cache (exported by the game when missing)
    const std::string need = d.missingLevel();
    if (!need.empty() && !d.openLevelByName(need, false, err)) return fail("mission opened, but its level: " + err);
    ToolResult r; r.text = d.describe("summary");
    return r;
}

ToolResult toolMissionSave(Doc &d, const cJSON *a)
{
    std::string err;
    std::vector<std::string> issues = d.validate();
    if (!d.saveMission(argStr(a, "path"), err)) return fail(err);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "saved", d.missionPath.c_str());
    cJSON *w = cJSON_AddArrayToObject(o, "warnings");
    for (auto &i : issues) cJSON_AddItemToArray(w, cJSON_CreateString(i.c_str()));
    return okJson(o);
}

ToolResult toolPlaceGuard(Doc &d, const cJSON *a)
{
    PadSpec pad; std::string err;
    if (!padSpecFromJson(a, pad, err)) return fail(err);
    RecExtras ex;
    if (!recParsePlace(a, true, ex, err)) return fail(err);
    ItemRef ref;
    if (!d.addGuard(pad, argInt(a, "model", -1), argInt(a, "ai", -1), argInt(a, "weapon", -1), ref, err, argStr(a, "uuid"), argStr(a, "name"))) return fail(err);
    recApplyPlace(d, ref, true, ex);   // the record fields, weapons and difficulties: part of the add's undo step
    d.sel = Selection(); d.sel.kind = Selection::ITEM; d.sel.item = ref;
    cJSON *o = cJSON_CreateObject();
    cJSON_AddItemToObject(o, "added", refJson(ref, &d));
    cJSON_AddStringToObject(o, "uuid", d.mission.guards[ref.idx].uuid);
    cJSON_AddItemToObject(o, "at", padRefJson(d, pad));
    if (pad.atPoint) cJSON_AddStringToObject(o, "new_pad", d.mission.pads[d.mission.npads - 1].id);
    return okJson(o);
}

ToolResult toolPlaceObject(Doc &d, const cJSON *a)
{
    PadSpec pad; std::string err;
    if (!padSpecFromJson(a, pad, err)) return fail(err);
    if (!argHas(a, "type") || !argHas(a, "model")) return fail("\"type\" (setup prop type) and \"model\" are required");
    RecExtras ex;
    if (!recParsePlace(a, false, ex, err)) return fail(err);
    ItemRef ref;
    if (!d.addObject(pad, argInt(a, "type", -1), argInt(a, "model", -1), argInt(a, "item", -1), ref, err, argStr(a, "uuid"), argStr(a, "name"))) return fail(err);
    recApplyPlace(d, ref, false, ex);
    d.sel = Selection(); d.sel.kind = Selection::ITEM; d.sel.item = ref;
    cJSON *o = cJSON_CreateObject();
    cJSON_AddItemToObject(o, "added", refJson(ref, &d));
    cJSON_AddStringToObject(o, "uuid", d.mission.objects[ref.idx].uuid);
    cJSON_AddItemToObject(o, "at", padRefJson(d, pad));
    if (pad.atPoint) cJSON_AddStringToObject(o, "new_pad", d.mission.pads[d.mission.npads - 1].id);
    return okJson(o);
}

SnapOpts snapFromArgs(const cJSON *a)
{
    SnapOpts s;
    const cJSON *sn = cJSON_GetObjectItemCaseSensitive(a, "snap");
    if (cJSON_IsObject(sn)) {
        const cJSON *g = cJSON_GetObjectItemCaseSensitive(sn, "grid");
        if (cJSON_IsNumber(g)) s.grid = (float)g->valuedouble;
        s.floor = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(sn, "floor"));
        s.pads = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(sn, "pads"));
    }
    return s;
}

// The thing to transform: "prop" / "kind"+"op" (a guard or object), or "mission_pad": id.
bool selectionFromArgs(Doc &d, const cJSON *a, Selection &out, std::string &err)
{
    out = Selection();
    if (argHas(a, "mission_pad")) { out.kind = Selection::MPAD; out.id = argStr(a, "mission_pad"); return true; }
    ItemRef ref;
    if (!itemFromArgs(d, a, ref, err)) return false;
    out.kind = Selection::ITEM; out.item = ref;
    return true;
}

// A free position: pos [x,y,z] (with axis "x"|"y"|"z" only that component is taken and the others keep
// their value), snapped as asked. Applied as one undo step.
ToolResult freeTransform(Doc &d, const cJSON *a, bool needSomething)
{
    Selection sel; std::string err;
    if (!selectionFromArgs(d, a, sel, err)) return fail(err);
    TransformInfo cur = d.getTransform(sel);
    if (!cur.valid) return fail("the target has no position (is a level export loaded?)");
    const cJSON *p = cJSON_GetObjectItemCaseSensitive(a, "pos");
    const bool hasPos = cJSON_IsArray(p) && cJSON_GetArraySize(p) >= 3;
    const bool hasYaw = cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(a, "yaw"));
    if (needSomething && !hasPos && !hasYaw) return fail("give \"pos\": [x, y, z] and/or \"yaw\" (degrees)");
    Vec3 np = cur.pos;
    if (hasPos) {
        std::string axis = argStr(a, "axis");
        for (int i = 0; i < 3; i++) np[i] = (float)cJSON_GetArrayItem(p, i)->valuedouble;
        int lock = axis == "x" ? 0 : axis == "y" ? 1 : axis == "z" ? 2 : -1;
        if (!axis.empty() && lock < 0) return fail("\"axis\" must be x, y or z");
        d.snapPoint(np, snapFromArgs(a), sel.kind == Selection::MPAD ? sel.id : "");
        if (lock >= 0) for (int i = 0; i < 3; i++) if (i != lock) np[i] = cur.pos[i];
    }
    float yaw = hasYaw ? (float)cJSON_GetObjectItemCaseSensitive(a, "yaw")->valuedouble : 0;
    if (!d.setTransform(sel, hasPos ? &np : nullptr, hasYaw ? &yaw : nullptr, true, err)) return fail(err);
    TransformInfo now = d.getTransform(sel);
    cJSON *o = cJSON_CreateObject();
    cJSON *pa = cJSON_AddArrayToObject(o, "pos");
    for (int i = 0; i < 3; i++) cJSON_AddItemToArray(pa, cJSON_CreateNumber(now.pos[i]));
    cJSON_AddNumberToObject(o, "yaw", now.yawDeg);
    if (sel.kind == Selection::ITEM) {
        for (const Item &it : d.items())
            if (it.added == sel.item.added && it.guard == sel.item.guard && it.idx == sel.item.idx) {
                if (it.pad.is_new) cJSON_AddItemToObject(o, "pad_id", cJSON_CreateString(it.pad.id));
                else cJSON_AddNumberToObject(o, "pad_num", it.pad.num);
            }
    }
    return okJson(o);
}

#include "mcp_tools_mod.inc"
#include "mcp_tools_script.inc"

ToolResult toolSetTransform(Doc &d, const cJSON *a) { return freeTransform(d, a, true); }

ToolResult toolCaptureThumb(Doc &d, const cJSON *a)
{
    std::string path = argStr(a, "path"), err;
    if (path.empty()) path = d.defaultThumbPath();   // mods/<mod>/<mission>/thumb.png when a mod mission is open
    if (path.empty()) return fail("\"path\" is required (a .png file) unless a mission of an open mod is being edited");
    if (!d.captureFn) return fail("capture_thumb needs the editor window: it is not available with --mcp-stdio / --mcp-http");
    if (!d.captureFn(path, err)) return fail(err);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "written", path.c_str());
    cJSON_AddNumberToObject(o, "width", 320);
    cJSON_AddNumberToObject(o, "height", 240);
    return okJson(o);
}

ToolResult toolMove(Doc &d, const cJSON *a)
{
    // a position (free move; the prop gets a mission pad of its own there) instead of a pad
    if (argHas(a, "pos") && !argHas(a, "pad")) return freeTransform(d, a, true);
    ItemRef ref; PadSpec pad; std::string err;
    if (!itemFromArgs(d, a, ref, err) || !padSpecFromJson(a, pad, err)) return fail(err);
    if (!d.moveItem(ref, pad, err)) return fail(err);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddItemToObject(o, "moved", refJson(ref, &d));
    cJSON_AddItemToObject(o, "to", padRefJson(d, pad));
    return okJson(o);
}

ToolResult toolRemove(Doc &d, const cJSON *a)
{
    ItemRef ref; std::string err;
    if (!itemFromArgs(d, a, ref, err)) return fail(err);
    if (!d.removeItem(ref, err)) return fail(err);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddItemToObject(o, "removed", refJson(ref));   // (deleted: no uuid to show)
    return okJson(o);
}

ToolResult toolDuplicate(Doc &d, const cJSON *a)
{
    ItemRef ref, out; PadSpec pad; std::string err;
    if (!itemFromArgs(d, a, ref, err, "from_uuid")) return fail(err);
    const bool hasPad = argHas(a, "pad") || argHas(a, "pos");
    if (hasPad && !padSpecFromJson(a, pad, err)) return fail(err);
    if (!d.duplicateItem(ref, hasPad ? &pad : nullptr, out, err, argStr(a, "uuid"), argStr(a, "name"))) return fail(err);
    d.sel = Selection(); d.sel.kind = Selection::ITEM; d.sel.item = out;
    cJSON *o = cJSON_CreateObject();
    cJSON_AddItemToObject(o, "copied", refJson(ref, &d));
    cJSON_AddItemToObject(o, "added", refJson(out, &d));
    cJSON_AddStringToObject(o, "uuid", out.guard ? d.mission.guards[out.idx].uuid : d.mission.objects[out.idx].uuid);
    if (hasPad && pad.atPoint) cJSON_AddStringToObject(o, "new_pad", d.mission.pads[d.mission.npads - 1].id);
    return okJson(o);
}

ToolResult toolUndo(Doc &d, const cJSON *)
{
    std::string err;
    if (!d.undo(err)) return fail(err);
    ToolResult r; r.text = d.describe("summary");
    return r;
}

ToolResult toolValidate(Doc &d, const cJSON *)
{
    const std::vector<Issue> issues = d.checkIssues();
    cJSON *o = cJSON_CreateObject();
    bool anyError = false;
    for (const Issue &i : issues) if (i.sev >= 2) anyError = true;
    cJSON_AddBoolToObject(o, "valid", !anyError);             // warnings do not make a mission invalid
    cJSON *e = cJSON_AddArrayToObject(o, "errors");          // the texts of every finding, as before
    cJSON *det = cJSON_AddArrayToObject(o, "issues");        // with severity and what each is about
    for (const Issue &i : issues) {
        cJSON_AddItemToArray(e, cJSON_CreateString(i.text.c_str()));
        cJSON *x = cJSON_CreateObject();
        cJSON_AddStringToObject(x, "severity", i.sev >= 2 ? "error" : "warning");
        cJSON_AddStringToObject(x, "text", i.text.c_str());
        if (!i.script.empty()) cJSON_AddStringToObject(x, "script", i.script.c_str());
        if (i.line > 0) cJSON_AddNumberToObject(x, "line", i.line);
        if (i.hasSel) {
            if (i.sel.kind == Selection::ITEM) {
                cJSON_AddItemToObject(x, "item", refJson(i.sel.item, &d));
            } else if (i.sel.kind == Selection::PAD) cJSON_AddNumberToObject(x, "pad", i.sel.num);
            else if (i.sel.kind == Selection::MPAD) cJSON_AddStringToObject(x, "mission_pad", i.sel.id.c_str());
            else if (i.sel.kind == Selection::SCRIPT) cJSON_AddNumberToObject(x, "script_index", i.sel.num);
        }
        cJSON_AddItemToArray(det, x);
    }
    return okJson(o);
}

// The difficulties a guard / object is present on: difficulties [0..3] (0 Agent, 1 Secret Agent, 2 00 Agent, 3 007); all four = everywhere.
ToolResult toolSetPresence(Doc &d, const cJSON *a)
{
    ItemRef ref; std::string err;
    if (!itemFromArgs(d, a, ref, err)) return fail(err);
    const cJSON *arr = cJSON_GetObjectItemCaseSensitive(a, "difficulties");
    if (!cJSON_IsArray(arr)) return fail("\"difficulties\": an array of 0..3 (0 Agent, 1 Secret Agent, 2 00 Agent, 3 007) is required");
    int mask = 0;
    const cJSON *e;
    cJSON_ArrayForEach(e, arr) {
        if (!cJSON_IsNumber(e) || e->valuedouble < 0 || e->valuedouble > 3) return fail("difficulties must be numbers 0..3");
        mask |= 1 << (int)e->valuedouble;
    }
    if (!mask) return fail("a prop must be present on at least one difficulty (use remove to take it out)");
    if (!d.setPresence(ref, mask, err)) return fail(err);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddItemToObject(o, "item", refJson(ref, &d));
    cJSON *r = cJSON_AddArrayToObject(o, "difficulties");
    for (int k = 0; k < 4; k++) if (mask & (1 << k)) cJSON_AddItemToArray(r, cJSON_CreateNumber(k));
    return okJson(o);
}

// The guard's AI list: an original guard gets a move op that keeps its pad and carries "ai"; an added guard's own "ai" changes.
ToolResult toolSetGuardBehavior(Doc &d, const cJSON *a)
{
    ItemRef ref; std::string err;
    if (!itemFromArgs(d, a, ref, err)) return fail(err);
    if (!ref.guard) return fail("that is not a guard (prop of a guard, or kind guard + op)");
    if (!argHas(a, "ai")) return fail("\"ai\" (an AI list id, e.g. 1025 or \"0x401\") is required");
    const int ai = argInt(a, "ai", -1);
    if (!d.setGuardAi(ref, ai, err)) return fail(err);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddItemToObject(o, "guard", refJson(ref, &d));
    cJSON_AddNumberToObject(o, "ai", ai);
    const bool known = d.hasLevel && std::find(d.level.ailists.begin(), d.level.ailists.end(), ai) != d.level.ailists.end();
    cJSON_AddBoolToObject(o, "level_own_list", known);
    if (!known) cJSON_AddStringToObject(o, "note", "not one of this level's own AI lists: fine for a list the game shares between levels, else the guard stands idle");
    return okJson(o);
}

ToolResult toolTestRun(Doc &d, const cJSON *a)
{
    std::string err;
    std::string spawn = argStr(a, "spawn");
    if (spawn.empty() && cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(a, "spawn"))) spawn = std::to_string(argInt(a, "spawn", 0));
    const RunMark mark = testRunMark();
    if (!d.testRun(spawn, cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(a, "coop")), err)) return fail(err);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "started", d.lastPlay.c_str());
    cJSON_AddStringToObject(o, "mission", d.missionPath.c_str());
    testRunReport(d, a, mark, o);   // wait_seconds / stop: the game's log lines since the launch
    return okJson(o);
}

// ---- tool table with JSON schemas ----

struct Tool {
    const char *name, *description, *schema;
    ToolResult (*fn)(Doc &, const cJSON *);
};

const char *kPadProps =
    "\"pad\":{\"description\":\"an original pad number (e.g. 10005) or the id of one of the mission's own pads (e.g. \\\"p1\\\")\","
    "\"type\":[\"integer\",\"string\"]},"
    "\"pos\":{\"description\":\"instead of pad: create a new mission pad at [x, y, z] (level units)\",\"type\":\"array\","
    "\"items\":{\"type\":\"number\"},\"minItems\":3,\"maxItems\":3}";

const Tool kTools[] = {
    { "level_describe", "Describe the loaded level export: rooms, pads, setup props, and the open mission. detail = summary | pads | props | mission | links | names. links: the switch pairs (terminal prop <-> door prop), dual-weapon links, tag records (tag id -> prop) and every door's bound pad, terminals and tags. names: body / head ids with their model file names, and the item ids with names. props also names each guard's body and head.",
      "{\"type\":\"object\",\"properties\":{\"detail\":{\"type\":\"string\",\"enum\":[\"summary\",\"pads\",\"props\",\"mission\",\"links\",\"names\"]}}}",
      toolLevelDescribe },
    { "level_open", "Load a solo level by name (level, e.g. bunker1): from the export cache next to the game, or the game "
      "exports it first (a few seconds; reexport: true redoes it). Or load an export directory (dir).",
      "{\"type\":\"object\",\"properties\":{\"level\":{\"type\":\"string\",\"description\":\"solo level name, e.g. bunker1\"},"
      "\"reexport\":{\"type\":\"boolean\"},\"dir\":{\"type\":\"string\"}}}",
      toolLevelOpen },
    { "mission_open", "Open a mission.json (path), or start a new mission for a level (new: true, level, name). The mission's level is loaded too (exported by the game when missing); export loads a given export directory instead. Fails while the open mission has unsaved changes unless discard: true is given (mission_save first to keep them).",
      "{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"},\"new\":{\"type\":\"boolean\"},\"name\":{\"type\":\"string\"},"
      "\"level\":{\"type\":\"string\",\"description\":\"solo level name, e.g. bunker1\"},\"emptied\":{\"type\":\"boolean\",\"description\":\"with new: remove every original guard, the weapons they carry and every pickup (doors, glass, scenery stay)\"},\"export\":{\"type\":\"string\"},\"discard\":{\"type\":\"boolean\",\"description\":\"replace an open mission that has unsaved changes\"}}}",
      toolMissionOpen },
    { "mission_save", "Save the open mission to its file (or to path).",
      "{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"}}}", toolMissionSave },
    { "place_guard", "Add a guard to the mission at a pad (or at a point: a new pad is created there). It gets a uuid (given as uuid, else made and returned in the result): scripts reference an added prop by it, never by a number. model = body id (65535 random; level_describe detail names). "
      "Any record field of the add op is accepted by name (mission_fields.h: head (-1 random), health, reaction_time, bitflags (1 = sunglasses always, 2 = one time in two; the head must have them), preset, chrpreset1, extrascale, state; unknown names are refused with the list), "
      "weapons = a list (at most 2) of {item, model?, flags?, collectable: {timer...}} instead of weapon, difficulties = [0..3] (0 Agent, 1 Secret Agent, 2 00 Agent, 3 007; all four = everywhere).",
      "{\"type\":\"object\",\"properties\":{ KPAD ,\"uuid\":{\"type\":\"string\",\"description\":\"lowercase RFC 4122 UUID the scripts reference this prop by; omitted = the editor makes one and returns it\"},\"name\":{\"type\":\"string\",\"description\":\"display name (optional)\"},\"model\":{\"type\":\"integer\"},\"ai\":{\"type\":\"integer\",\"description\":\"AI list id\"},"
      "\"weapon\":{\"type\":\"integer\"},\"weapons\":{\"type\":\"array\",\"maxItems\":2,\"items\":{\"type\":\"object\"}},"
      "\"difficulties\":{\"type\":\"array\",\"items\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":3}},"
      "\"head\":{\"type\":\"integer\"},\"health\":{\"type\":\"integer\"},\"reaction_time\":{\"type\":\"integer\"},\"bitflags\":{\"type\":\"integer\"},"
      "\"preset\":{\"type\":\"integer\"},\"chrpreset1\":{\"type\":\"integer\"},\"extrascale\":{\"type\":\"integer\"},\"state\":{\"type\":\"integer\"}}}",
      toolPlaceGuard },
    { "place_object", "Add an object (setup prop type + model, optionally an item id) to the mission at a pad or point. It gets a uuid (given as uuid, else made and returned in the result) that scripts reference it by. Any record field of the add op is accepted by name: "
      "the header fields flat (flags, flags2, maxdamage, damage, extrascale, state) and the type's own in a sub-object named for it, e.g. {\"type\":1,\"door\":{\"max_speed\":0.1}} or {\"monitor\":{...}} "
      "(mission_fields.h; unknown names are refused with the list). difficulties = [0..3] as place_guard.",
      "{\"type\":\"object\",\"properties\":{ KPAD ,\"uuid\":{\"type\":\"string\",\"description\":\"lowercase RFC 4122 UUID the scripts reference this prop by; omitted = the editor makes one and returns it\"},\"name\":{\"type\":\"string\",\"description\":\"display name (optional)\"},\"type\":{\"type\":\"integer\"},\"model\":{\"type\":\"integer\"},\"item\":{\"type\":\"integer\"},"
      "\"difficulties\":{\"type\":\"array\",\"items\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":3}},"
      "\"flags\":{\"type\":\"integer\"},\"flags2\":{\"type\":\"integer\"},\"maxdamage\":{\"type\":\"number\"},\"damage\":{\"type\":\"number\"},"
      "\"extrascale\":{\"type\":\"integer\"},\"state\":{\"type\":\"integer\"}},"
      "\"additionalProperties\":{\"description\":\"the type's own record fields: a sub-object named for the type (door, key, cctv, magazine, collectable, monitor, multi_monitor, autogun, ammo, armour, vehicle, aircraft, tank, tinted_glass)\"},"
      "\"required\":[\"type\",\"model\"]}",
      toolPlaceObject },
    { "move", "Move a guard or object. To a pad (pad), or to a free position (pos [x,y,z], the prop then stands on a mission pad of its own there), "
      "optionally locked to one axis (axis x|y|z: only that component of pos is used) and snapped (snap {grid, floor, pads}). Give prop for an "
      "original setup prop, uuid for an item the mission added, or kind + op.",
      "{\"type\":\"object\",\"properties\":{\"prop\":{\"type\":\"integer\"},\"kind\":{\"type\":\"string\",\"enum\":[\"guard\",\"object\"]},"
      "\"op\":{\"type\":\"integer\"}, KPAD ,\"axis\":{\"type\":\"string\",\"enum\":[\"x\",\"y\",\"z\"]}, KSNAP }}",
      toolMove },
    { "set_transform", "Set the position and/or facing of a mission pad (mission_pad: id) or of a guard/object (prop, or kind + op). pos [x,y,z] in level "
      "units, yaw in degrees (0 = +z), axis x|y|z locks the move to that axis, snap {grid: step, floor: bool, pads: bool}. Original pads cannot be moved.",
      "{\"type\":\"object\",\"properties\":{\"prop\":{\"type\":\"integer\"},\"kind\":{\"type\":\"string\",\"enum\":[\"guard\",\"object\"]},"
      "\"op\":{\"type\":\"integer\"},\"mission_pad\":{\"type\":\"string\"},\"pos\":{\"type\":\"array\",\"items\":{\"type\":\"number\"},\"minItems\":3,\"maxItems\":3},"
      "\"yaw\":{\"type\":\"number\"},\"axis\":{\"type\":\"string\",\"enum\":[\"x\",\"y\",\"z\"]}, KSNAP }}",
      toolSetTransform },
    { "capture_thumb", "Render the editor's current 3D view to a 320x240 PNG (a plain 4:3 crop of the view, no markers or panels). Needs the editor window.",
      "{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\",\"description\":\"default: thumb.png in the open mission's folder inside the open mod\"}}}", toolCaptureThumb },
    { "mod_open", "Open a mod folder (mods/<mod>, created with an empty campaign.json when new; a .pmod package file is refused: mod_extract it first). Optional name, author, description, affects [story|multiplayer], unlock (ordered|open) edit the campaign; mission (folder or index) opens one of its missions.",
      "{\"type\":\"object\",\"properties\":{\"dir\":{\"type\":\"string\"},\"name\":{\"type\":\"string\"},\"author\":{\"type\":\"string\"},\"description\":{\"type\":\"string\"},"
      "\"affects\":{\"type\":\"array\",\"items\":{\"type\":\"string\",\"enum\":[\"story\",\"multiplayer\"]}},\"unlock\":{\"type\":\"string\",\"enum\":[\"ordered\",\"open\"]},"
      "\"mission\":{\"type\":[\"integer\",\"string\"]}},\"required\":[\"dir\"]}", toolModOpen },
    { "mod_extract", "Unpack a package file X.pmod into a folder X.pmod/ to edit it (the game plays that folder like the file); then mod_open the returned dir. "
      "Without dest it is done in place: the file is renamed X.pmod.old (refused when that exists). With dest: into <dest>/X.pmod/, the file kept (refused when that folder exists). "
      "Refused when the package holds no campaign.json. Returns dir (and old).",
      "{\"type\":\"object\",\"properties\":{\"pmod\":{\"type\":\"string\"},\"dest\":{\"type\":\"string\",\"description\":\"the folder that gets X.pmod/; default: in place\"}},\"required\":[\"pmod\"]}", toolModExtract },
    { "mod_save", "Write the open mod's campaign.json (the open mission is saved with mission_save).", "{\"type\":\"object\",\"properties\":{}}", toolModSave },
    { "mod_publish", "Save the open mod, validate it and its missions (an error refuses), and write it as <out_dir>/<mod>.pmod (a tar of the mod folder; default out_dir <mod>/dist). Returns path and bytes.",
      "{\"type\":\"object\",\"properties\":{\"out_dir\":{\"type\":\"string\"}}}", toolModPublish },
    { "mod_deploy", "Like mod_publish, but writes <mods folder>/<mod>.pmod, where the game finds it. Returns path and bytes.", "{\"type\":\"object\",\"properties\":{}}", toolModDeploy },
    { "mod_add_mission", "Add a new mission for a level to the open mod: creates its folder and mission.json, appends it to the campaign (saved) and opens it.",
      "{\"type\":\"object\",\"properties\":{\"name\":{\"type\":\"string\"},\"level\":{\"type\":\"string\",\"description\":\"solo level name, e.g. bunker1\"},\"folder\":{\"type\":\"string\"}}}", toolModAddMission },
    { "mod_reorder", "Reorder the open mod's missions: order [folder, ...] (a permutation), or mission (folder or index) with to (new index). Save with mod_save.",
      "{\"type\":\"object\",\"properties\":{\"order\":{\"type\":\"array\",\"items\":{\"type\":\"string\"}},\"mission\":{\"type\":[\"integer\",\"string\"]},\"to\":{\"type\":\"integer\"}}}", toolModReorder },
    { "textures", "The open level's textures (glTF decor): texnum, size, where the drawn picture comes from (level, mod, pack) and the rooms using it; "
      "the open mod's pictures (textures/<n>.png), the chosen texture pack and the packs in the game's texturepacks folder (usable, or not and why).",
      "{\"type\":\"object\",\"properties\":{}}", toolTextures },
    { "mod_set_texture", "Replace texture texnum in the open mod with the PNG png (copied as <mod>/textures/<texnum>.png; 8-bit, not interlaced, any size: "
      "the texture coordinates are 0..1 of the picture). Pictures of your own only, never the game's. remove: true takes the replacement out. "
      "The decor is drawn again with it; returns what textures returns.",
      "{\"type\":\"object\",\"properties\":{\"texnum\":{\"type\":\"integer\"},\"png\":{\"type\":\"string\"},\"remove\":{\"type\":\"boolean\"}},\"required\":[\"texnum\"]}", toolModSetTexture },
    { "set_texture_pack", "Draw the level with a texture pack of the game's texturepacks folder (file: \"hd.ptexpack\"; \"\" or \"ge007.ptexpack\" the game's own, the default), under the mod's own textures: "
      "exactly one pack is always chosen, and an incomplete pack (missing textures of ge007) is refused. "
      "The editor's choice only (the game's is Video > Texture pack). Returns what textures returns.",
      "{\"type\":\"object\",\"properties\":{\"file\":{\"type\":\"string\"}}}", toolSetTexturePack },
    { "set_environment", "Set the mission's environment overrides (save only: the editor draws no sky or sea yet, preview in the game). mood day|dusk|night|keep; sun {azimuth, elevation (deg), color [r,g,b], glow}; sky, clouds [r,g,b]; cloud_speed; sea {enabled, height, color, swell, chop, speed}; clear: [mood|sun|sky|clouds|cloud_speed|sea] or \"all\" returns to the level's own.",
      "{\"type\":\"object\",\"properties\":{\"mood\":{\"type\":\"string\",\"enum\":[\"day\",\"dusk\",\"night\",\"keep\"]},"
      "\"sun\":{\"type\":\"object\",\"properties\":{\"azimuth\":{\"type\":\"number\"},\"elevation\":{\"type\":\"number\"},\"color\":{\"type\":\"array\",\"items\":{\"type\":\"number\"}},\"glow\":{\"type\":\"number\"}}},"
      "\"sky\":{\"type\":\"array\",\"items\":{\"type\":\"number\"}},\"clouds\":{\"type\":\"array\",\"items\":{\"type\":\"number\"}},\"cloud_speed\":{\"type\":\"number\"},"
      "\"sea\":{\"type\":\"object\",\"properties\":{\"enabled\":{\"type\":\"boolean\"},\"height\":{\"type\":\"number\"},\"color\":{\"type\":\"array\",\"items\":{\"type\":\"number\"}},"
      "\"swell\":{\"type\":\"number\"},\"chop\":{\"type\":\"number\"},\"speed\":{\"type\":\"number\"}}},\"clear\":{\"type\":[\"array\",\"string\"]}}}", toolSetEnvironment },
    { "remove", "Remove an original prop from the mission (prop), or delete an item the mission added (kind + op).",
      "{\"type\":\"object\",\"properties\":{\"prop\":{\"type\":\"integer\"},\"kind\":{\"type\":\"string\",\"enum\":[\"guard\",\"object\"]},"
      "\"op\":{\"type\":\"integer\"}}}",
      toolRemove },
    { "duplicate", "Duplicate a guard or object (prop for an original setup prop, or kind + op / from_uuid for an added one) as a NEW item the mission adds (a fresh uuid unless uuid is given, returned in the result), with all its record fields and its facing. Without pad/pos it stands on the original's pad (move it afterwards); with pad or pos [x,y,z] (a new mission pad there, same facing) it is placed there. The original is untouched.",
      "{\"type\":\"object\",\"properties\":{\"prop\":{\"type\":\"integer\"},\"kind\":{\"type\":\"string\",\"enum\":[\"guard\",\"object\"]},"
      "\"op\":{\"type\":\"integer\"},\"from_uuid\":{\"type\":\"string\",\"description\":\"the added item to copy, by uuid\"}, KPAD ,\"uuid\":{\"type\":\"string\",\"description\":\"lowercase RFC 4122 UUID the scripts reference this prop by; omitted = the editor makes one and returns it\"},\"name\":{\"type\":\"string\",\"description\":\"display name (optional)\"}}}",
      toolDuplicate },
    { "set_presence", "Make a guard or object present only on some difficulties: difficulties = a list of 0 Agent, 1 Secret Agent, 2 00 Agent, 3 007 (all four = everywhere). prop = an original prop (it gets a move op without a pad), or kind + op = one the mission added. An object gets the game's per-difficulty do-not-load flags; a guard is sunk like a removed one on the other difficulties.",
      "{\"type\":\"object\",\"properties\":{\"prop\":{\"type\":\"integer\"},\"kind\":{\"type\":\"string\",\"enum\":[\"guard\",\"object\"]},\"op\":{\"type\":\"integer\"},"
      "\"difficulties\":{\"type\":\"array\",\"items\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":3}}},\"required\":[\"difficulties\"]}", toolSetPresence },
    { "set_guard_behavior", "Reassign a guard's AI list (what it does: patrol, guard, flee...). prop = an original guard (it gets a move op that keeps its pad and carries the new list), or kind guard + op = a guard the mission added. ai = the AI list id (number or \"0x401\"); level_describe props lists each guard's current ai.",
      "{\"type\":\"object\",\"properties\":{\"prop\":{\"type\":\"integer\"},\"kind\":{\"type\":\"string\",\"enum\":[\"guard\"]},\"op\":{\"type\":\"integer\"},"
      "\"ai\":{\"type\":[\"integer\",\"string\"],\"description\":\"AI list id\"}},\"required\":[\"ai\"]}", toolSetGuardBehavior },
    { "script_list", "The mission's script entries (use, target, props, the script's Props schema), the built-in library scripts and the mission's own scripts with their Props schema (field name, type alias: Item / Objective / Door / Guard / Prop / Zone / Pad / number / string / boolean, optional), and the zone ids.",
      "{\"type\":\"object\",\"properties\":{}}", toolScriptList },
    { "script_read", "A script's source (use = library name, the mod's, or the mission's own), its Props schema and the type checker's findings.",
      "{\"type\":\"object\",\"properties\":{\"use\":{\"type\":\"string\"}},\"required\":[\"use\"]}", toolScriptRead },
    { "script_write", "Write the source of a mission script (created when new; library and mod scripts are read-only: script_copy first). Held in the editor and saved next to mission.json by mission_save. Returns the type checker's findings (line, severity, text) against mission.d.luau, with props typed as the script's Props.",
      "{\"type\":\"object\",\"properties\":{\"use\":{\"type\":\"string\",\"description\":\"script name: letters, digits, _ and -\"},\"text\":{\"type\":\"string\"}},\"required\":[\"use\",\"text\"]}", toolScriptWrite },
    { "script_copy", "Copy a library or mod script into the mission as its own editable script (<mission dir>/scripts/<name>.luau on save). switch: true also points the entries that used it at the copy.",
      "{\"type\":\"object\",\"properties\":{\"use\":{\"type\":\"string\"},\"switch\":{\"type\":\"boolean\"}},\"required\":[\"use\"]}", toolScriptCopy },
    { "script_attach", "Add a script entry (use + target + props), or with index update one (use, target and/or props; props are merged unless replace: true). target = {kind: prop, prop: N (an original prop)} | {kind: prop, uuid: <uuid> (a prop the mission adds)} | {kind: pad, pad: N or id} | {kind: zone, zone: id} | {kind: mission}. props = {name: number | string | boolean}; a Door / Guard / Prop property is an original prop's number or an added prop's uuid string (a number at or past the level's own prop count is an error); names and meanings come from the script's Props schema (script_list).",
      "{\"type\":\"object\",\"properties\":{\"index\":{\"type\":\"integer\"},\"use\":{\"type\":\"string\"},\"target\":{\"type\":\"object\",\"properties\":{\"kind\":{\"type\":\"string\",\"enum\":[\"prop\",\"pad\",\"zone\",\"mission\"]},\"prop\":{\"type\":\"integer\"},\"uuid\":{\"type\":\"string\"},\"pad\":{\"type\":[\"integer\",\"string\"]},\"zone\":{\"type\":\"string\"}}},\"props\":{\"type\":\"object\"},\"replace\":{\"type\":\"boolean\"}}}", toolScriptAttach },
    { "set_props", "Update the property values of a script entry (index): props {name: value} merged (replace: true replaces them all); unset [names] removes keys (an optional property left empty).",
      "{\"type\":\"object\",\"properties\":{\"index\":{\"type\":\"integer\"},\"props\":{\"type\":\"object\"},\"unset\":{\"type\":\"array\",\"items\":{\"type\":\"string\"}},\"replace\":{\"type\":\"boolean\"}},\"required\":[\"index\"]}", toolSetProps },
    { "script_remove", "Remove a script entry (index). The script file stays.",
      "{\"type\":\"object\",\"properties\":{\"index\":{\"type\":\"integer\"}},\"required\":[\"index\"]}", toolScriptRemove },
    { "zone_add", "Add a zone (a sphere scripts can test: a player within radius): pos [x,y,z], radius (default 150), room (default: the room at pos), id (default z1, z2, ...).",
      "{\"type\":\"object\",\"properties\":{\"pos\":{\"type\":\"array\",\"items\":{\"type\":\"number\"},\"minItems\":3,\"maxItems\":3},\"radius\":{\"type\":\"number\"},\"room\":{\"type\":\"integer\"},\"id\":{\"type\":\"string\"}},\"required\":[\"pos\"]}", toolZoneAdd },
    { "zone_set", "Change a zone: pos [x,y,z], radius and/or room.",
      "{\"type\":\"object\",\"properties\":{\"id\":{\"type\":\"string\"},\"pos\":{\"type\":\"array\",\"items\":{\"type\":\"number\"},\"minItems\":3,\"maxItems\":3},\"radius\":{\"type\":\"number\"},\"room\":{\"type\":\"integer\"}},\"required\":[\"id\"]}", toolZoneSet },
    { "zone_remove", "Remove a zone (scripts that target it are reported by validate).",
      "{\"type\":\"object\",\"properties\":{\"id\":{\"type\":\"string\"}},\"required\":[\"id\"]}", toolZoneRemove },
    { "mission_set", "Set the open mission's name, briefing, description and/or keep_level_objectives (true: the level keeps its own briefing, objectives and exit, and the mission's objectives are added after them; one undo step).",
      "{\"type\":\"object\",\"properties\":{\"name\":{\"type\":\"string\"},\"briefing\":{\"type\":\"string\"},\"description\":{\"type\":\"string\"},\"keep_level_objectives\":{\"type\":\"boolean\"}}}", toolMissionSet },
    { "objective_add", "Add an objective: id (letters, digits, _ -; unique; scripts name the objective by it), text, difficulty = the lowest difficulty it applies to (0 Agent, 1 Secret Agent, 2 00 Agent; default 0). An objective is completed by a script or a zone.",
      "{\"type\":\"object\",\"properties\":{\"id\":{\"type\":\"string\"},\"text\":{\"type\":\"string\"},\"difficulty\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":2}},\"required\":[\"id\",\"text\"]}", toolObjectiveAdd },
    { "objective_set", "Change an objective (by id): text, difficulty, new_id (scripts that name the old id are then reported by validate), index (move it to that position in the list).",
      "{\"type\":\"object\",\"properties\":{\"id\":{\"type\":\"string\"},\"text\":{\"type\":\"string\"},\"difficulty\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":2},\"new_id\":{\"type\":\"string\"},\"index\":{\"type\":\"integer\",\"minimum\":0}},\"required\":[\"id\"]}", toolObjectiveSet },
    { "objective_remove", "Remove an objective by id.",
      "{\"type\":\"object\",\"properties\":{\"id\":{\"type\":\"string\"}},\"required\":[\"id\"]}", toolObjectiveRemove },
    { "script_check", "Type-check a DRAFT Luau source against the script API without writing it anywhere: text, target_kind (mission|prop|pad|zone; default: the kind of the first entry that uses `use`, else mission), use (optional name). Returns the diagnostics (line, column, severity, text) and the source's Props schema.",
      "{\"type\":\"object\",\"properties\":{\"text\":{\"type\":\"string\"},\"target_kind\":{\"type\":\"string\",\"enum\":[\"mission\",\"prop\",\"pad\",\"zone\"]},\"use\":{\"type\":\"string\"}},\"required\":[\"text\"]}", toolScriptCheck },
    { "undo", "Undo the last mission edit.", "{\"type\":\"object\",\"properties\":{}}", toolUndo },
    { "validate", "Check the mission (format, ids, pads and props against the level export).",
      "{\"type\":\"object\",\"properties\":{}}", toolValidate },
    { "test_run", "Save the mission and start the game on it (a mission of a mod runs from the mod packed into <mods folder>/<mod>.pmod): solo by default, co-op (two players, two pads) with coop: true; at the level's own start unless spawn is given. wait_seconds (cap 120; default 0 = return at once) waits that long (ends early if the game exits) and returns the game log's \"missions:\" lines since the launch (objective completions, script errors and loads) in missions, [ERROR] lines in errors, and running; the editor window is busy while it waits. stop: true then ends the game process this editor started (never another).",
      "{\"type\":\"object\",\"properties\":{\"wait_seconds\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":120},\"stop\":{\"type\":\"boolean\"},\"spawn\":{\"type\":[\"integer\",\"string\"],\"description\":\"pad to start at: a pad number or a mission pad id (default: the level's own start)\"},\"coop\":{\"type\":\"boolean\",\"description\":\"two players (default false: solo)\"}}}",
      toolTestRun },
};

cJSON *toolsList()
{
    cJSON *arr = cJSON_CreateArray();
    for (const Tool &t : kTools) {
        std::string schema = t.schema;
        size_t k = schema.find("KPAD");
        if (k != std::string::npos) schema.replace(k, 4, kPadProps);
        k = schema.find("KSNAP");
        if (k != std::string::npos) schema.replace(k, 5,
            "\"snap\":{\"type\":\"object\",\"properties\":{\"grid\":{\"type\":\"number\"},\"floor\":{\"type\":\"boolean\"},\"pads\":{\"type\":\"boolean\"}}}");
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "name", t.name);
        cJSON_AddStringToObject(o, "description", t.description);
        cJSON *s = cJSON_Parse(schema.c_str());
        cJSON_AddItemToObject(o, "inputSchema", s ? s : cJSON_CreateObject());
        cJSON_AddItemToArray(arr, o);
    }
    return arr;
}

cJSON *toolsCall(Doc &d, const cJSON *params)
{
    std::string name = argStr(params, "name");
    const cJSON *args = cJSON_GetObjectItemCaseSensitive(params, "arguments");
    J empty(cJSON_CreateObject());
    if (!cJSON_IsObject(args)) args = empty.p;
    ToolResult res = fail("unknown tool '" + name + "'");
    for (const Tool &t : kTools)
        if (name == t.name) { res = t.fn(d, args); break; }
    cJSON *r = cJSON_CreateObject();
    cJSON *content = cJSON_AddArrayToObject(r, "content");
    cJSON *c = cJSON_CreateObject();
    cJSON_AddStringToObject(c, "type", "text");
    cJSON_AddStringToObject(c, "text", res.text.c_str());
    cJSON_AddItemToArray(content, c);
    cJSON_AddBoolToObject(r, "isError", res.error);
    return r;
}

// One request object. Returns the response (owned) or nullptr for a notification.
cJSON *handleOne(Doc &d, const cJSON *req)
{
    if (!cJSON_IsObject(req)) return rpcError(nullptr, -32600, "invalid request");
    const cJSON *id = cJSON_GetObjectItemCaseSensitive(req, "id");
    const cJSON *method = cJSON_GetObjectItemCaseSensitive(req, "method");
    const cJSON *params = cJSON_GetObjectItemCaseSensitive(req, "params");
    if (!cJSON_IsString(method)) {
        if (id) return rpcError(id, -32600, "invalid request: no method");
        return nullptr;   // a response from the client: nothing to do
    }
    const std::string m = method->valuestring;
    if (!id) return nullptr;   // notification (notifications/initialized, notifications/cancelled, ...)

    if (m == "initialize") {
        std::string want = argStr(params, "protocolVersion");
        static const char *supported[] = { "2025-06-18", "2025-03-26", "2024-11-05" };
        std::string use = supported[0];
        for (const char *s : supported) if (want == s) use = s;
        cJSON *r = cJSON_CreateObject();
        cJSON_AddStringToObject(r, "protocolVersion", use.c_str());
        cJSON *caps = cJSON_AddObjectToObject(r, "capabilities");
        cJSON *tools = cJSON_AddObjectToObject(caps, "tools");
        cJSON_AddBoolToObject(tools, "listChanged", false);
        cJSON *si = cJSON_AddObjectToObject(r, "serverInfo");
        cJSON_AddStringToObject(si, "name", "project-dam-editor");
        cJSON_AddStringToObject(si, "version", "0.1.0");
        cJSON_AddStringToObject(r, "instructions",
            "Project Dam co-op mission editor. level_describe shows the level export (rooms, pads, setup props); "
            "mission_open starts or loads a mission; place_guard/place_object/move/remove edit it as a delta over the "
            "solo level; validate, mission_save and test_run finish the loop. Pads are original pad numbers or the "
            "ids of mission pads; a pos [x,y,z] creates a mission pad.");
        return rpcResult(id, r);
    }
    if (m == "ping") return rpcResult(id, cJSON_CreateObject());
    if (m == "tools/list") {
        cJSON *r = cJSON_CreateObject();
        cJSON_AddItemToObject(r, "tools", toolsList());
        return rpcResult(id, r);
    }
    if (m == "tools/call") {
        if (!cJSON_IsObject(params) || !cJSON_IsString(cJSON_GetObjectItemCaseSensitive(params, "name")))
            return rpcError(id, -32602, "tools/call needs params.name");
        return rpcResult(id, toolsCall(d, params));
    }
    return rpcError(id, -32601, "method not found: " + m);
}

} // namespace

// Completes a tools/call response (result.content[0].text is the tool's JSON) with a deferred tail.
static void applyDeferred(std::function<void(cJSON *)> tail, std::string &resp)
{
    J r(cJSON_Parse(resp.c_str()));
    cJSON *res = r.p ? cJSON_GetObjectItemCaseSensitive(r.p, "result") : nullptr;
    cJSON *content = res ? cJSON_GetObjectItemCaseSensitive(res, "content") : nullptr;
    cJSON *c0 = cJSON_IsArray(content) ? cJSON_GetArrayItem(content, 0) : nullptr;
    cJSON *text = c0 ? cJSON_GetObjectItemCaseSensitive(c0, "text") : nullptr;
    if (!cJSON_IsString(text)) return;
    J inner(cJSON_Parse(text->valuestring));
    if (!inner.p) return;
    tail(inner.p);
    const std::string s = print(inner.p);
    cJSON_ReplaceItemInObjectCaseSensitive(c0, "text", cJSON_CreateString(s.c_str()));
    resp = print(r.p);
}

std::string mcpHandleMessage(Doc &doc, const std::string &body, std::function<void(std::string &)> *post)
{
    J root(cJSON_Parse(body.c_str()));
    if (!root.p) {
        J e(rpcError(nullptr, -32700, "parse error"));
        return print(e.p);
    }
    if (post && cJSON_IsObject(root.p)) {
        gAllowDefer = true; gDeferred = nullptr;
        J r(handleOne(doc, root.p));
        gAllowDefer = false;
        std::string out = r.p ? print(r.p) : std::string();
        if (gDeferred) { auto tail = gDeferred; gDeferred = nullptr; *post = [tail](std::string &s) { applyDeferred(tail, s); }; }
        return out;
    }
    if (cJSON_IsArray(root.p)) {
        J out(cJSON_CreateArray());
        const cJSON *el;
        cJSON_ArrayForEach(el, root.p) {
            cJSON *r = handleOne(doc, el);
            if (r) cJSON_AddItemToArray(out.p, r);
        }
        return cJSON_GetArraySize(out.p) ? print(out.p) : std::string();
    }
    J r(handleOne(doc, root.p));
    return r.p ? print(r.p) : std::string();
}

int mcpStdioLoop(Doc &doc)
{
    std::string line;
    while (std::getline(std::cin, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        std::string out = mcpHandleMessage(doc, line);
        if (!out.empty()) { std::cout << out << "\n"; std::cout.flush(); }
    }
    return 0;
}

bool mcpOriginAllowed(const std::string &origin)
{
    if (origin.empty()) return true;   // not a browser
    // scheme://host[:port]
    size_t s = origin.find("://");
    std::string host = s == std::string::npos ? origin : origin.substr(s + 3);
    size_t slash = host.find('/');
    if (slash != std::string::npos) host.resize(slash);
    if (!host.empty() && host[0] == '[') { size_t e = host.find(']'); return host.substr(0, e + 1) == "[::1]"; }
    size_t colon = host.find(':');
    if (colon != std::string::npos) host.resize(colon);
    return host == "localhost" || host == "127.0.0.1";
}
