#include "mcp.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>

#include "cJSON.h"

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

cJSON *refJson(const ItemRef &r)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "kind", r.guard ? "guard" : "object");
    cJSON_AddNumberToObject(o, r.added ? "op" : "prop", r.idx);
    return o;
}

// "prop": N (an original prop) or "kind": "guard"|"object" + "op": N (an added one, its index in
// the mission's guards/objects list).
bool itemFromArgs(Doc &d, const cJSON *a, ItemRef &out, std::string &err)
{
    if (argHas(a, "prop")) return d.itemFromProp(argInt(a, "prop", -1), out, err);
    std::string kind = argStr(a, "kind");
    if ((kind == "guard" || kind == "object") && argHas(a, "op")) {
        out.valid = true; out.added = true; out.guard = kind == "guard"; out.idx = argInt(a, "op", -1);
        return true;
    }
    err = "give \"prop\" (an original setup prop index) or \"kind\" (guard|object) with \"op\" (index of an added item)";
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

// ---- the tools ----

ToolResult toolLevelDescribe(Doc &d, const cJSON *a)
{
    ToolResult r; r.text = d.describe(argStr(a, "detail", "summary"));
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
    } else if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(a, "new")) || argHas(a, "name") || argHas(a, "level")) {
        if (!d.newMission(argStr(a, "name", "New mission"), argStr(a, "level"), err)) return fail(err);
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
    ItemRef ref;
    if (!d.addGuard(pad, argInt(a, "model", -1), argInt(a, "ai", -1), argInt(a, "weapon", -1), ref, err)) return fail(err);
    d.sel = Selection(); d.sel.kind = Selection::ITEM; d.sel.item = ref;
    cJSON *o = cJSON_CreateObject();
    cJSON_AddItemToObject(o, "added", refJson(ref));
    cJSON_AddItemToObject(o, "at", padRefJson(d, pad));
    if (pad.atPoint) cJSON_AddStringToObject(o, "new_pad", d.mission.pads[d.mission.npads - 1].id);
    return okJson(o);
}

ToolResult toolPlaceObject(Doc &d, const cJSON *a)
{
    PadSpec pad; std::string err;
    if (!padSpecFromJson(a, pad, err)) return fail(err);
    if (!argHas(a, "type") || !argHas(a, "model")) return fail("\"type\" (setup prop type) and \"model\" are required");
    ItemRef ref;
    if (!d.addObject(pad, argInt(a, "type", -1), argInt(a, "model", -1), argInt(a, "item", -1), ref, err)) return fail(err);
    d.sel = Selection(); d.sel.kind = Selection::ITEM; d.sel.item = ref;
    cJSON *o = cJSON_CreateObject();
    cJSON_AddItemToObject(o, "added", refJson(ref));
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
    cJSON_AddItemToObject(o, "moved", refJson(ref));
    cJSON_AddItemToObject(o, "to", padRefJson(d, pad));
    return okJson(o);
}

ToolResult toolRemove(Doc &d, const cJSON *a)
{
    ItemRef ref; std::string err;
    if (!itemFromArgs(d, a, ref, err)) return fail(err);
    if (!d.removeItem(ref, err)) return fail(err);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddItemToObject(o, "removed", refJson(ref));
    return okJson(o);
}

ToolResult toolDuplicate(Doc &d, const cJSON *a)
{
    ItemRef ref, out; PadSpec pad; std::string err;
    if (!itemFromArgs(d, a, ref, err)) return fail(err);
    const bool hasPad = argHas(a, "pad") || argHas(a, "pos");
    if (hasPad && !padSpecFromJson(a, pad, err)) return fail(err);
    if (!d.duplicateItem(ref, hasPad ? &pad : nullptr, out, err)) return fail(err);
    d.sel = Selection(); d.sel.kind = Selection::ITEM; d.sel.item = out;
    cJSON *o = cJSON_CreateObject();
    cJSON_AddItemToObject(o, "copied", refJson(ref));
    cJSON_AddItemToObject(o, "added", refJson(out));
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
    std::vector<std::string> issues = d.validate();
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "valid", issues.empty());
    cJSON *e = cJSON_AddArrayToObject(o, "errors");
    for (auto &i : issues) cJSON_AddItemToArray(e, cJSON_CreateString(i.c_str()));
    return okJson(o);
}

ToolResult toolTestRun(Doc &d, const cJSON *a)
{
    std::string err;
    std::string spawn = argStr(a, "spawn");
    if (spawn.empty() && cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(a, "spawn"))) spawn = std::to_string(argInt(a, "spawn", 0));
    if (!d.testRun(spawn, cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(a, "coop")), err)) return fail(err);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "started", d.lastPlay.c_str());
    cJSON_AddStringToObject(o, "mission", d.missionPath.c_str());
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
    { "level_describe", "Describe the loaded level export: rooms, pads, setup props, and the open mission. detail = summary | pads | props | mission.",
      "{\"type\":\"object\",\"properties\":{\"detail\":{\"type\":\"string\",\"enum\":[\"summary\",\"pads\",\"props\",\"mission\"]}}}",
      toolLevelDescribe },
    { "level_open", "Load a solo level by name (level, e.g. bunker1): from the export cache next to the game, or the game "
      "exports it first (a few seconds; reexport: true redoes it). Or load an export directory (dir).",
      "{\"type\":\"object\",\"properties\":{\"level\":{\"type\":\"string\",\"description\":\"solo level name, e.g. bunker1\"},"
      "\"reexport\":{\"type\":\"boolean\"},\"dir\":{\"type\":\"string\"}}}",
      toolLevelOpen },
    { "mission_open", "Open a mission.json (path), or start a new mission for a level (new: true, level, name). The mission's level is loaded too (exported by the game when missing); export loads a given export directory instead. Fails while the open mission has unsaved changes unless discard: true is given (mission_save first to keep them).",
      "{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"},\"new\":{\"type\":\"boolean\"},\"name\":{\"type\":\"string\"},"
      "\"level\":{\"type\":\"string\",\"description\":\"solo level name, e.g. bunker1\"},\"export\":{\"type\":\"string\"},\"discard\":{\"type\":\"boolean\",\"description\":\"replace an open mission that has unsaved changes\"}}}",
      toolMissionOpen },
    { "mission_save", "Save the open mission to its file (or to path).",
      "{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"}}}", toolMissionSave },
    { "place_guard", "Add a guard to the mission at a pad (or at a point: a new pad is created there).",
      "{\"type\":\"object\",\"properties\":{ KPAD ,\"model\":{\"type\":\"integer\"},\"ai\":{\"type\":\"integer\",\"description\":\"AI list id\"},"
      "\"weapon\":{\"type\":\"integer\"}}}",
      toolPlaceGuard },
    { "place_object", "Add an object (setup prop type + model, optionally an item id) to the mission at a pad or point.",
      "{\"type\":\"object\",\"properties\":{ KPAD ,\"type\":{\"type\":\"integer\"},\"model\":{\"type\":\"integer\"},\"item\":{\"type\":\"integer\"}},"
      "\"required\":[\"type\",\"model\"]}",
      toolPlaceObject },
    { "move", "Move a guard or object. To a pad (pad), or to a free position (pos [x,y,z], the prop then stands on a mission pad of its own there), "
      "optionally locked to one axis (axis x|y|z: only that component of pos is used) and snapped (snap {grid, floor, pads}). Give prop for an "
      "original setup prop, or kind + op for an item the mission added.",
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
    { "mod_open", "Open a mod folder (mods/<mod>, created with an empty campaign.json when new). Optional name, author, description, affects [story|multiplayer], unlock (ordered|open) edit the campaign; mission (folder or index) opens one of its missions.",
      "{\"type\":\"object\",\"properties\":{\"dir\":{\"type\":\"string\"},\"name\":{\"type\":\"string\"},\"author\":{\"type\":\"string\"},\"description\":{\"type\":\"string\"},"
      "\"affects\":{\"type\":\"array\",\"items\":{\"type\":\"string\",\"enum\":[\"story\",\"multiplayer\"]}},\"unlock\":{\"type\":\"string\",\"enum\":[\"ordered\",\"open\"]},"
      "\"mission\":{\"type\":[\"integer\",\"string\"]}},\"required\":[\"dir\"]}", toolModOpen },
    { "mod_save", "Write the open mod's campaign.json (the open mission is saved with mission_save).", "{\"type\":\"object\",\"properties\":{}}", toolModSave },
    { "mod_add_mission", "Add a new mission for a level to the open mod: creates its folder and mission.json, appends it to the campaign (saved) and opens it.",
      "{\"type\":\"object\",\"properties\":{\"name\":{\"type\":\"string\"},\"level\":{\"type\":\"string\",\"description\":\"solo level name, e.g. bunker1\"},\"folder\":{\"type\":\"string\"}}}", toolModAddMission },
    { "mod_reorder", "Reorder the open mod's missions: order [folder, ...] (a permutation), or mission (folder or index) with to (new index). Save with mod_save.",
      "{\"type\":\"object\",\"properties\":{\"order\":{\"type\":\"array\",\"items\":{\"type\":\"string\"}},\"mission\":{\"type\":[\"integer\",\"string\"]},\"to\":{\"type\":\"integer\"}}}", toolModReorder },
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
    { "duplicate", "Duplicate a guard or object (prop for an original setup prop, or kind + op for an added one) as a NEW item the mission adds, with all its record fields and its facing. Without pad/pos it stands on the original's pad (move it afterwards); with pad or pos [x,y,z] (a new mission pad there, same facing) it is placed there. The original is untouched.",
      "{\"type\":\"object\",\"properties\":{\"prop\":{\"type\":\"integer\"},\"kind\":{\"type\":\"string\",\"enum\":[\"guard\",\"object\"]},"
      "\"op\":{\"type\":\"integer\"}, KPAD }}",
      toolDuplicate },
    { "undo", "Undo the last mission edit.", "{\"type\":\"object\",\"properties\":{}}", toolUndo },
    { "validate", "Check the mission (format, ids, pads and props against the level export).",
      "{\"type\":\"object\",\"properties\":{}}", toolValidate },
    { "test_run", "Save the mission and start the game on it: solo by default, co-op (two players, two pads) with coop: true; at the level's own start unless spawn is given.",
      "{\"type\":\"object\",\"properties\":{\"spawn\":{\"type\":[\"integer\",\"string\"],\"description\":\"pad to start at: a pad number or a mission pad id (default: the level's own start)\"},\"coop\":{\"type\":\"boolean\",\"description\":\"two players (default false: solo)\"}}}",
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
        cJSON_AddStringToObject(si, "name", "ge-editor");
        cJSON_AddStringToObject(si, "version", "0.1.0");
        cJSON_AddStringToObject(r, "instructions",
            "GoldenEye co-op mission editor. level_describe shows the level export (rooms, pads, setup props); "
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

std::string mcpHandleMessage(Doc &doc, const std::string &body)
{
    J root(cJSON_Parse(body.c_str()));
    if (!root.p) {
        J e(rpcError(nullptr, -32700, "parse error"));
        return print(e.p);
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
