// MCP tools of the mission scripts and zones (issue #105). Included by mcp.cpp, inside its anonymous namespace (uses its
// helpers: ToolResult, fail, okJson, argStr, argInt, argHas). The same Doc functions as the Scripts panel.


cJSON *schemaJson(const std::string &source)
{
    const PropsSchema ps = parsePropsSchema(source);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "found", ps.found);
    cJSON *f = cJSON_AddArrayToObject(o, "fields");
    for (const PropField &p : ps.fields) {
        cJSON *x = cJSON_CreateObject();
        cJSON_AddStringToObject(x, "name", p.name.c_str());
        cJSON_AddStringToObject(x, "type", p.type.c_str());
        cJSON_AddBoolToObject(x, "optional", p.optional);
        cJSON_AddItemToArray(f, x);
    }
    if (!ps.error.empty()) cJSON_AddStringToObject(o, "parse_error", ps.error.c_str());
    return o;
}

const char *originName(Doc::ScriptOrigin o)
{
    return o == Doc::SRC_MISSION ? "mission" : o == Doc::SRC_MOD ? "mod" : o == Doc::SRC_LIBRARY ? "library" : "none";
}

cJSON *diagsJson(const std::vector<ScriptDiag> &ds)
{
    cJSON *a = cJSON_CreateArray();
    for (const ScriptDiag &d : ds) {
        cJSON *x = cJSON_CreateObject();
        cJSON_AddNumberToObject(x, "line", d.line);
        cJSON_AddNumberToObject(x, "column", d.col);
        cJSON_AddStringToObject(x, "severity", d.sev >= 2 ? "error" : "warning");
        cJSON_AddStringToObject(x, "text", d.text.c_str());
        cJSON_AddItemToArray(a, x);
    }
    return a;
}

cJSON *scriptEntryJson(const Doc &d, int i)
{
    const MissionScript &s = d.mission.scripts[i];
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "index", i);
    cJSON_AddStringToObject(o, "use", s.use);
    cJSON *t = cJSON_AddObjectToObject(o, "target");
    cJSON_AddStringToObject(t, "kind", Doc::targetKindName(s.kind));
    if (s.kind == MISSION_TGT_PROP && s.uuid[0]) cJSON_AddStringToObject(t, "uuid", s.uuid);
    else if (s.kind == MISSION_TGT_PROP) cJSON_AddNumberToObject(t, "prop", s.prop);
    else if (s.kind == MISSION_TGT_PAD) { if (s.pad.is_new) cJSON_AddStringToObject(t, "pad", s.pad.id); else cJSON_AddNumberToObject(t, "pad", s.pad.num); }
    else if (s.kind == MISSION_TGT_ZONE) cJSON_AddStringToObject(t, "zone", s.zone);
    cJSON *p = cJSON_AddObjectToObject(o, "props");
    for (int k = 0; k < s.nprops; k++) {
        const MissionProp &v = s.props[k];
        if (v.kind == MISSION_VAL_NUMBER) cJSON_AddNumberToObject(p, v.name, v.num);
        else if (v.kind == MISSION_VAL_BOOL) cJSON_AddBoolToObject(p, v.name, v.num != 0);
        else cJSON_AddStringToObject(p, v.name, v.str);
    }
    const Doc::ScriptSrc src = d.scriptSource(s.use);
    cJSON_AddStringToObject(o, "origin", originName(src.origin));
    if (src.origin != Doc::SRC_NONE) cJSON_AddItemToObject(o, "props_schema", schemaJson(src.text));
    return o;
}

ToolResult toolScriptList(Doc &d, const cJSON *)
{
    cJSON *o = cJSON_CreateObject();
    cJSON *en = cJSON_AddArrayToObject(o, "entries");
    if (d.hasMission) for (int i = 0; i < d.mission.nscripts; i++) cJSON_AddItemToArray(en, scriptEntryJson(d, i));
    cJSON *ap = cJSON_AddArrayToObject(o, "added_props");   // what a Door / Guard / Prop property or a prop target can name by uuid
    if (d.hasMission) {
        for (const Item &it : d.items()) {
            if (!it.added) continue;
            cJSON *x = cJSON_CreateObject();
            cJSON_AddStringToObject(x, "uuid", it.uuid.c_str());
            if (!it.name.empty()) cJSON_AddStringToObject(x, "name", it.name.c_str());
            cJSON_AddStringToObject(x, "kind", it.guard ? "guard" : it.type == MISSION_PROPDEF_DOOR ? "door" : "object");
            cJSON_AddNumberToObject(x, "op", it.idx);
            cJSON_AddItemToArray(ap, x);
        }
    }
    cJSON *lib = cJSON_AddArrayToObject(o, "library");
    for (const std::string &n : d.libraryScripts()) {
        cJSON *x = cJSON_CreateObject();
        cJSON_AddStringToObject(x, "name", n.c_str());
        cJSON_AddItemToObject(x, "props_schema", schemaJson(d.scriptSource(n).origin == Doc::SRC_LIBRARY ? d.scriptSource(n).text : std::string()));
        cJSON_AddItemToArray(lib, x);
    }
    cJSON *ms = cJSON_AddArrayToObject(o, "mission_scripts");
    for (const std::string &n : d.missionScriptNames()) {
        const Doc::ScriptSrc src = d.scriptSource(n);
        if (src.origin != Doc::SRC_MISSION) continue;
        cJSON *x = cJSON_CreateObject();
        cJSON_AddStringToObject(x, "name", n.c_str());
        cJSON_AddStringToObject(x, "path", src.path.c_str());
        cJSON_AddBoolToObject(x, "unsaved", d.scriptDirty.count(n) != 0);
        cJSON_AddItemToObject(x, "props_schema", schemaJson(src.text));
        cJSON_AddItemToArray(ms, x);
    }
    cJSON *zs = cJSON_AddArrayToObject(o, "zones");
    if (d.hasMission) for (int i = 0; i < d.mission.nzones; i++) cJSON_AddItemToArray(zs, cJSON_CreateString(d.mission.zones[i].id));
    return okJson(o);
}

// The kind of attachment a script is checked for: the first entry that uses it, else "mission".
static const char *kindForUse(const Doc &d, const std::string &use)
{
    for (int i = 0; i < d.mission.nscripts; i++) if (use == d.mission.scripts[i].use) return Doc::targetKindName(d.mission.scripts[i].kind);
    return "mission";
}

ToolResult toolScriptRead(Doc &d, const cJSON *a)
{
    const std::string use = argStr(a, "use");
    if (use.empty()) return fail("\"use\" is required: a script name (see script_list)");
    const Doc::ScriptSrc src = d.scriptSource(use);
    if (src.origin == Doc::SRC_NONE) return fail("no script '" + use + "' (library, the mod's, or the mission's own)");
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "use", use.c_str());
    cJSON_AddStringToObject(o, "origin", originName(src.origin));
    cJSON_AddStringToObject(o, "path", src.path.c_str());
    cJSON_AddStringToObject(o, "text", src.text.c_str());
    cJSON_AddItemToObject(o, "props_schema", schemaJson(src.text));
    const Doc::ScriptDiagnostics dg = d.scriptDiagnostics(use, kindForUse(d, use));
    cJSON_AddItemToObject(o, "diagnostics", diagsJson(dg.diags));
    return okJson(o);
}

ToolResult toolScriptWrite(Doc &d, const cJSON *a)
{
    std::string use = argStr(a, "use"), err;
    const cJSON *t = cJSON_GetObjectItemCaseSensitive(a, "text");
    if (use.empty() || !cJSON_IsString(t)) return fail("\"use\" (a script name) and \"text\" (the Luau source) are required");
    const Doc::ScriptSrc cur = d.scriptSource(use);
    if (cur.origin == Doc::SRC_LIBRARY || cur.origin == Doc::SRC_MOD)
        return fail("'" + use + "' is a " + originName(cur.origin) + " script, read-only: script_copy it into the mission first (or write another name)");
    if (!d.setScriptText(use, t->valuestring, err)) return fail(err);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "use", use.c_str());
    cJSON_AddStringToObject(o, "path", d.scriptSource(use).path.c_str());
    cJSON_AddStringToObject(o, "note", "held in the editor until mission_save writes it next to mission.json");
    const Doc::ScriptDiagnostics dg = d.scriptDiagnostics(use, kindForUse(d, use));
    cJSON_AddItemToObject(o, "diagnostics", diagsJson(dg.diags));
    cJSON_AddItemToObject(o, "props_schema", schemaJson(t->valuestring));
    return okJson(o);
}

ToolResult toolScriptCopy(Doc &d, const cJSON *a)
{
    std::string use = argStr(a, "use"), nu, err;
    if (use.empty()) return fail("\"use\" is required: the library or mod script to copy into the mission");
    if (!d.copyScriptToMission(use, nu, err)) return fail(err);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "copied", use.c_str());
    cJSON_AddStringToObject(o, "use", nu.c_str());
    if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(a, "switch")))   // entries that used the original now use the copy
        for (int i = 0; i < d.mission.nscripts; i++) if (use == d.mission.scripts[i].use) { std::string e2; d.setScriptUse(i, nu, e2); }
    return okJson(o);
}

// "target": {kind: prop|pad|zone|mission, prop | uuid, pad, zone}
bool targetFromJson(Doc &d, const cJSON *t, MissionTargetKind &kind, int &prop, MissionPadRef &pad, std::string &zone, std::string &uuid, std::string &err)
{
    (void)d;
    uuid.clear();
    memset(&pad, 0, sizeof pad); pad.num = -1; prop = -1; zone.clear();
    if (!cJSON_IsObject(t)) { err = "\"target\" must be {\"kind\": \"prop\"|\"pad\"|\"zone\"|\"mission\", ...}"; return false; }
    const std::string k = argStr(t, "kind", "mission");
    if (k == "mission") { kind = MISSION_TGT_MISSION; return true; }
    if (k == "prop") {
        if (argHas(t, "uuid")) { kind = MISSION_TGT_PROP; uuid = argStr(t, "uuid"); return true; }
        if (!argHas(t, "prop")) { err = "target kind prop needs \"prop\" (an original prop's number) or \"uuid\" (a prop the mission adds)"; return false; }
        kind = MISSION_TGT_PROP; prop = argInt(t, "prop", -1); return true;
    }
    if (k == "pad") {
        const cJSON *p = cJSON_GetObjectItemCaseSensitive(t, "pad");
        if (!p) { err = "target kind pad needs \"pad\": an original pad number or a mission pad id"; return false; }
        kind = MISSION_TGT_PAD;
        if (cJSON_IsString(p)) {
            char *e = nullptr; long v = strtol(p->valuestring, &e, 0);
            if (e && !*e && e != p->valuestring) { pad.is_new = 0; pad.num = (int)v; }
            else { pad.is_new = 1; snprintf(pad.id, sizeof pad.id, "%s", p->valuestring); }
        } else { pad.is_new = 0; pad.num = (int)p->valuedouble; }
        return true;
    }
    if (k == "zone") {
        if (!argHas(t, "zone")) { err = "target kind zone needs \"zone\": a zone id"; return false; }
        kind = MISSION_TGT_ZONE; zone = argStr(t, "zone"); return true;
    }
    err = "unknown target kind '" + k + "' (prop, pad, zone, mission)";
    return false;
}

ToolResult toolScriptAttach(Doc &d, const cJSON *a)
{
    std::string err;
    const bool update = argHas(a, "index");
    int idx = argInt(a, "index", -1);
    const cJSON *props = cJSON_GetObjectItemCaseSensitive(a, "props");
    const cJSON *tj = cJSON_GetObjectItemCaseSensitive(a, "target");
    if (!d.hasMission) return fail("no mission is open");
    // several Doc edits make one undo step: restore on failure, collapse the steps on success
    const std::string snap = d.snapshot();
    const size_t depth = d.undoStack.size();
    auto bail = [&](const std::string &m) {
        std::string e2; d.restoreSnapshot(snap, e2);
        while (d.undoStack.size() > depth) d.undoStack.pop_back();
        return fail(m);
    };
    MissionTargetKind kind = MISSION_TGT_MISSION; int prop = -1; MissionPadRef pad; std::string zone, uuid;
    if (tj && !targetFromJson(d, tj, kind, prop, pad, zone, uuid, err)) return fail(err);
    if (!update) {
        const std::string use = argStr(a, "use");
        if (use.empty()) return fail("\"use\" is required: a script name (library or the mission's own; see script_list)");
        if (d.scriptSource(use).origin == Doc::SRC_NONE) return fail("no script '" + use + "' (see script_list)");
        if (!d.addScript(use, kind, idx, err)) return bail(err);
        if (tj && !d.setScriptTarget(idx, kind, prop, pad, zone, err, uuid)) return bail(err);
        if (props && !d.setScriptProps(idx, props, true, err)) return bail(err);
    } else {
        if (idx < 0 || idx >= d.mission.nscripts) return fail("no script entry " + std::to_string(idx));
        if (argHas(a, "use") && !d.setScriptUse(idx, argStr(a, "use"), err)) return bail(err);
        if (tj && !d.setScriptTarget(idx, kind, prop, pad, zone, err, uuid)) return bail(err);
        if (props && !d.setScriptProps(idx, props, cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(a, "replace")), err)) return bail(err);
    }
    while (d.undoStack.size() > depth) d.undoStack.pop_back();
    d.undoStack.push_back(snap);
    return okJson(scriptEntryJson(d, idx));
}

ToolResult toolSetProps(Doc &d, const cJSON *a)
{
    std::string err;
    const int idx = argInt(a, "index", -1);
    if (!d.hasMission || idx < 0 || idx >= d.mission.nscripts) return fail("\"index\" is required: the script entry (see script_list)");
    const cJSON *props = cJSON_GetObjectItemCaseSensitive(a, "props");
    const cJSON *unset = cJSON_GetObjectItemCaseSensitive(a, "unset");
    if (!props && !unset) return fail("give \"props\" {name: value} and/or \"unset\" [names]");
    const std::string snap = d.snapshot();
    const size_t depth = d.undoStack.size();
    if (props && !d.setScriptProps(idx, props, cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(a, "replace")), err)) return fail(err);
    if (cJSON_IsArray(unset)) {
        const cJSON *e;
        cJSON_ArrayForEach(e, unset) if (cJSON_IsString(e) && !d.setScriptProp(idx, e->valuestring, nullptr, err)) return fail(err);
    }
    while (d.undoStack.size() > depth) d.undoStack.pop_back();
    d.undoStack.push_back(snap);
    return okJson(scriptEntryJson(d, idx));
}

ToolResult toolScriptRemove(Doc &d, const cJSON *a)
{
    std::string err;
    const int idx = argInt(a, "index", -1);
    if (!d.removeScript(idx, err)) return fail(err);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "removed", idx);
    cJSON_AddNumberToObject(o, "remaining", d.mission.nscripts);
    return okJson(o);
}

cJSON *zoneJson(const Doc &d, int i)
{
    const MissionZone &z = d.mission.zones[i];
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "id", z.id);
    cJSON *p = cJSON_AddArrayToObject(o, "pos");
    for (int k = 0; k < 3; k++) cJSON_AddItemToArray(p, cJSON_CreateNumber(z.pos[k]));
    cJSON_AddNumberToObject(o, "radius", z.radius);
    cJSON_AddNumberToObject(o, "room", z.room);
    return o;
}

bool vec3Arg(const cJSON *a, const char *k, Vec3 &out)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(a, k);
    if (!cJSON_IsArray(v) || cJSON_GetArraySize(v) != 3) return false;
    for (int i = 0; i < 3; i++) out[i] = (float)cJSON_GetArrayItem(v, i)->valuedouble;
    return true;
}

ToolResult toolZoneAdd(Doc &d, const cJSON *a)
{
    std::string err, id = argStr(a, "id");
    Vec3 pos{};
    if (!vec3Arg(a, "pos", pos)) return fail("\"pos\": [x, y, z] is required (level units)");
    const cJSON *r = cJSON_GetObjectItemCaseSensitive(a, "radius");
    const float radius = cJSON_IsNumber(r) ? (float)r->valuedouble : 150.0f;
    if (!d.addZone(pos, radius, argInt(a, "room", -1), id, err)) return fail(err);
    return okJson(zoneJson(d, d.zoneIndex(id)));
}

ToolResult toolZoneSet(Doc &d, const cJSON *a)
{
    std::string err, id = argStr(a, "id");
    if (id.empty()) return fail("\"id\" is required: the zone id");
    Vec3 pos{}; const bool hasPos = vec3Arg(a, "pos", pos);
    const cJSON *r = cJSON_GetObjectItemCaseSensitive(a, "radius");
    float radius = cJSON_IsNumber(r) ? (float)r->valuedouble : 0;
    int room = argInt(a, "room", -1);
    if (!hasPos && !cJSON_IsNumber(r) && !argHas(a, "room")) return fail("give \"pos\" [x,y,z], \"radius\" and/or \"room\"");
    if (!d.setZone(id, hasPos ? &pos : nullptr, cJSON_IsNumber(r) ? &radius : nullptr, argHas(a, "room") ? &room : nullptr, true, err)) return fail(err);
    return okJson(zoneJson(d, d.zoneIndex(id)));
}

ToolResult toolZoneRemove(Doc &d, const cJSON *a)
{
    std::string err, id = argStr(a, "id");
    if (!d.removeZone(id, err)) return fail(err);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "removed", id.c_str());
    return okJson(o);
}
