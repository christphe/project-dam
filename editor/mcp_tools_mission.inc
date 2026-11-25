// MCP tools that author a whole mission: its metadata and objectives, the record fields of placed guards / objects, the level's
// links / tags / model names, a draft script check and the test_run log. Included by mcp.cpp, inside its anonymous namespace
// and BEFORE the tool functions (place_guard / place_object / test_run / level_describe call the helpers below); uses the
// helpers above it (ToolResult, fail, okJson, argStr, argInt, argHas). The same Doc actions as the Mission tab.
//
// Names here carry an "ms" / "Rec" / "draft" prefix: they share the namespace with mcp_tools_script.inc.

// ---------------------------------------------------------------------------------------------
// mission metadata and objectives

static bool msIdOk(const std::string &id)
{
    if (id.empty() || id.size() >= MISSION_ID_LEN) return false;
    for (char c : id) if (!(isalnum((unsigned char)c) || c == '_' || c == '-')) return false;
    return true;
}

static cJSON *msObjectivesJson(const Doc &d)
{
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < d.mission.nobjectives; i++) {
        const MissionObjective &o = d.mission.objectives[i];
        cJSON *x = cJSON_CreateObject();
        cJSON_AddNumberToObject(x, "index", i);
        cJSON_AddStringToObject(x, "id", o.id);
        cJSON_AddStringToObject(x, "text", o.text);
        cJSON_AddNumberToObject(x, "difficulty", o.difficulty);
        cJSON_AddItemToArray(arr, x);
    }
    return arr;
}

static int msFindObjective(const Doc &d, const std::string &id)
{
    for (int i = 0; i < d.mission.nobjectives; i++) if (id == d.mission.objectives[i].id) return i;
    return -1;
}

ToolResult toolMissionSet(Doc &d, const cJSON *a)
{
    if (!d.hasMission) return fail("no mission is open");
    struct F { const char *key; char *dst; size_t cap; } fields[] = {
        { "name", d.mission.name, sizeof d.mission.name },
        { "briefing", d.mission.briefing, sizeof d.mission.briefing },
        { "description", d.mission.description, sizeof d.mission.description },
    };
    bool any = false;
    for (const F &f : fields) {
        const cJSON *v = cJSON_GetObjectItemCaseSensitive(a, f.key);
        if (!v) continue;
        any = true;
        if (!cJSON_IsString(v)) return fail(std::string("\"") + f.key + "\" must be a string");
        if (strlen(v->valuestring) >= f.cap) return fail(std::string("\"") + f.key + "\" is too long (at most " + std::to_string(f.cap - 1) + " bytes)");
    }
    if (!any) return fail("give \"name\", \"briefing\" and/or \"description\"");
    const std::string snap = d.snapshot();
    bool changed = false;
    for (const F &f : fields) {
        const cJSON *v = cJSON_GetObjectItemCaseSensitive(a, f.key);
        if (!v || strcmp(f.dst, v->valuestring) == 0) continue;
        snprintf(f.dst, f.cap, "%s", v->valuestring);
        changed = true;
    }
    if (changed) d.commit(snap);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "name", d.mission.name);
    cJSON_AddStringToObject(o, "briefing", d.mission.briefing);
    cJSON_AddStringToObject(o, "description", d.mission.description);
    return okJson(o);
}

static bool msDifficulty(const cJSON *a, int &out, std::string &err)
{
    out = 0;
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(a, "difficulty");
    if (!v) return true;
    if (!cJSON_IsNumber(v) || v->valuedouble < 0 || v->valuedouble > 2 || v->valuedouble != (int)v->valuedouble) {
        err = "\"difficulty\" is the lowest difficulty the objective applies to: 0 Agent, 1 Secret Agent, 2 00 Agent";
        return false;
    }
    out = (int)v->valuedouble;
    return true;
}

ToolResult toolObjectiveAdd(Doc &d, const cJSON *a)
{
    if (!d.hasMission) return fail("no mission is open");
    const std::string id = argStr(a, "id"), text = argStr(a, "text");
    std::string err;
    if (!msIdOk(id)) return fail("\"id\" is required: letters, digits, _ and - (at most " + std::to_string(MISSION_ID_LEN - 1) + "); scripts name the objective by it");
    if (!cJSON_IsString(cJSON_GetObjectItemCaseSensitive(a, "text"))) return fail("\"text\" is required: what the briefing shows");
    if (text.size() >= MISSION_TEXT_LEN) return fail("\"text\" is too long (at most " + std::to_string(MISSION_TEXT_LEN - 1) + " bytes)");
    if (msFindObjective(d, id) >= 0) return fail("there is already an objective '" + id + "'");
    int diff;
    if (!msDifficulty(a, diff, err)) return fail(err);
    if (!d.addObjective(text, diff, err)) return fail(err);
    snprintf(d.mission.objectives[d.mission.nobjectives - 1].id, MISSION_ID_LEN, "%s", id.c_str());   // part of the same undo step
    cJSON *o = cJSON_CreateObject();
    cJSON_AddItemToObject(o, "objectives", msObjectivesJson(d));
    return okJson(o);
}

ToolResult toolObjectiveSet(Doc &d, const cJSON *a)
{
    if (!d.hasMission) return fail("no mission is open");
    const std::string id = argStr(a, "id"), newId = argStr(a, "new_id");
    const int at = msFindObjective(d, id);
    if (at < 0) return fail("no objective '" + id + "' (objective ids: see level_describe detail mission)");
    const bool hasText = argHas(a, "text"), hasDiff = argHas(a, "difficulty"), hasNew = argHas(a, "new_id"), hasIndex = argHas(a, "index");
    if (!hasText && !hasDiff && !hasNew && !hasIndex) return fail("give \"text\", \"difficulty\", \"new_id\" and/or \"index\" (a new position)");
    std::string err;
    if (hasText) {
        if (!cJSON_IsString(cJSON_GetObjectItemCaseSensitive(a, "text"))) return fail("\"text\" must be a string");
        if (argStr(a, "text").size() >= MISSION_TEXT_LEN) return fail("\"text\" is too long (at most " + std::to_string(MISSION_TEXT_LEN - 1) + " bytes)");
    }
    int diff = 0;
    if (hasDiff && !msDifficulty(a, diff, err)) return fail(err);
    if (hasNew) {
        if (!msIdOk(newId)) return fail("\"new_id\": letters, digits, _ and - (at most " + std::to_string(MISSION_ID_LEN - 1) + ")");
        if (newId != id && msFindObjective(d, newId) >= 0) return fail("there is already an objective '" + newId + "'");
    }
    int to = at;
    if (hasIndex) {
        to = argInt(a, "index", -1);
        if (!cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(a, "index")) || to < 0 || to >= d.mission.nobjectives)
            return fail("\"index\" must be 0.." + std::to_string(d.mission.nobjectives - 1));
    }
    const std::string snap = d.snapshot();
    MissionObjective &o = d.mission.objectives[at];
    if (hasText) snprintf(o.text, sizeof o.text, "%s", argStr(a, "text").c_str());
    if (hasDiff) o.difficulty = diff;
    if (hasNew) snprintf(o.id, sizeof o.id, "%s", newId.c_str());
    if (to != at) {
        MissionObjective *arr = d.mission.objectives;
        if (to < at) std::rotate(arr + to, arr + at, arr + at + 1);
        else std::rotate(arr + at, arr + at + 1, arr + to + 1);
    }
    d.commit(snap);   // one undo step for the whole edit
    cJSON *r = cJSON_CreateObject();
    cJSON_AddItemToObject(r, "objectives", msObjectivesJson(d));
    return okJson(r);
}

ToolResult toolObjectiveRemove(Doc &d, const cJSON *a)
{
    if (!d.hasMission) return fail("no mission is open");
    const std::string id = argStr(a, "id");
    const int at = msFindObjective(d, id);
    if (at < 0) return fail("no objective '" + id + "'");
    std::string err;
    if (!d.removeObjective(at, err)) return fail(err);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "removed", id.c_str());
    cJSON_AddItemToObject(o, "objectives", msObjectivesJson(d));
    return okJson(o);
}

// ---------------------------------------------------------------------------------------------
// record fields of placed guards / objects (mission_fields.h)

struct RecExtras {
    MissionFieldVals f;
    bool hasDiff = false;
    int diffMask = 0;
    bool hasWeapons = false;
    std::vector<MissionWeapon> weapons;
};

// The names the record type takes: flat fields, then "group": { grouped fields }.
static std::string recFieldNames(int type)
{
    const MissionField *f;
    const int n = missionRecFields(type, &f);
    std::string flat, grouped;
    for (int i = 0; i < n && i < MISSION_MAX_FIELDS; i++) {
        std::string &dst = f[i].grouped ? grouped : flat;
        if (!dst.empty()) dst += ", ";
        dst += f[i].name;
    }
    std::string s = flat;
    const char *g = missionRecGroup(type);
    if (g && !grouped.empty()) s += std::string(s.empty() ? "" : "; ") + "\"" + g + "\": { " + grouped + " }";
    return s.empty() ? "(none)" : s;
}

static bool recNumber(const MissionField &f, const cJSON *v, double &out, std::string &err)
{
    if (cJSON_IsBool(v)) { out = cJSON_IsTrue(v) ? 1 : 0; return true; }
    if (!cJSON_IsNumber(v)) { err = std::string("\"") + f.name + "\" must be a number"; return false; }
    const double x = v->valuedouble;
    double lo, hi;
    const double bits = (double)(8 * f.size);
    if (f.kind == MF_FLOAT) { out = x; return true; }
    if (f.kind == MF_FIXED) { lo = -32768.0; hi = 32767.99998; }
    else {
        if (x != std::floor(x)) { err = std::string("\"") + f.name + "\" must be a whole number"; return false; }
        if (f.kind == MF_SINT) { lo = -std::ldexp(1.0, (int)bits - 1); hi = std::ldexp(1.0, (int)bits - 1) - 1; }
        else { lo = -std::ldexp(1.0, (int)bits - 1); hi = std::ldexp(1.0, (int)bits) - 1; }   // an unsigned field also takes its two's complement
    }
    if (x < lo || x > hi) {
        char b[160]; snprintf(b, sizeof b, "\"%s\" = %g is out of range (%g .. %g)", f.name, x, lo, hi);
        err = b;
        return false;
    }
    out = x;
    return true;
}

// The record fields among the keys of `src` (an op's keys / a weapon's keys), refusing every key that is neither one of `reserved`
// (the op's own keys, handled by the caller) nor a field of the record type. `what` names the thing in the message.
static bool recParseFields(const cJSON *src, int type, const char *const *reserved, size_t nreserved, const std::string &what,
                           MissionFieldVals &out, std::string &err)
{
    const MissionField *f;
    const int n = missionRecFields(type, &f);
    const char *group = missionRecGroup(type);
    memset(&out, 0, sizeof out);
    std::string unknown;
    for (const cJSON *k = src ? src->child : nullptr; k; k = k->next) {
        const std::string key = k->string ? k->string : "";
        bool known = false;
        for (size_t r = 0; r < nreserved; r++) if (key == reserved[r]) known = true;
        if (known) continue;
        for (int i = 0; i < n && i < MISSION_MAX_FIELDS; i++) {
            if (f[i].grouped || key != f[i].name) continue;
            double x;
            if (!recNumber(f[i], k, x, err)) { err = what + ": " + err; return false; }
            out.set |= (uint64_t)1 << i; out.v[i] = x;
            known = true;
            break;
        }
        if (known) continue;
        if (group && key == group) {
            if (!cJSON_IsObject(k)) { err = what + ": \"" + key + "\" must be an object { field: number }"; return false; }
            for (const cJSON *s = k->child; s; s = s->next) {
                bool sk = false;
                for (int i = 0; i < n && i < MISSION_MAX_FIELDS; i++) {
                    if (!f[i].grouped || !s->string || strcmp(s->string, f[i].name)) continue;
                    double x;
                    if (!recNumber(f[i], s, x, err)) { err = what + ": " + err; return false; }
                    out.set |= (uint64_t)1 << i; out.v[i] = x;
                    sk = true;
                    break;
                }
                if (!sk) unknown += (unknown.empty() ? "" : ", ") + key + "." + (s->string ? s->string : "?");
            }
            continue;
        }
        unknown += (unknown.empty() ? "" : ", ") + key;
    }
    if (!unknown.empty()) {
        err = what + ": unknown field(s) " + unknown + ". Valid: ";
        for (size_t r = 0; r < nreserved; r++) err += std::string(r ? ", " : "") + reserved[r];
        err += "; record fields: " + recFieldNames(type);
        return false;
    }
    return true;
}

static bool recDifficulties(const cJSON *a, RecExtras &ex, std::string &err)
{
    const cJSON *arr = cJSON_GetObjectItemCaseSensitive(a, "difficulties");
    if (!arr) return true;
    if (!cJSON_IsArray(arr)) { err = "\"difficulties\": an array of 0..3 (0 Agent, 1 Secret Agent, 2 00 Agent, 3 007)"; return false; }
    int mask = 0;
    const cJSON *e;
    cJSON_ArrayForEach(e, arr) {
        if (!cJSON_IsNumber(e) || e->valuedouble < 0 || e->valuedouble > 3 || e->valuedouble != (int)e->valuedouble) { err = "difficulties must be whole numbers 0..3"; return false; }
        mask |= 1 << (int)e->valuedouble;
    }
    if (!mask) { err = "a prop must be present on at least one difficulty"; return false; }
    ex.hasDiff = true;
    ex.diffMask = mask == MISSION_DIFF_ALL ? 0 : mask;   // all four = everywhere = no key
    return true;
}

// place_guard / place_object: the extras of the arguments, validated before anything is added.
static bool recParsePlace(const cJSON *a, bool guard, RecExtras &ex, std::string &err)
{
    memset(&ex.f, 0, sizeof ex.f);
    if (guard) {
        static const char *kRes[] = { "pad", "pos", "room", "model", "ai", "weapon", "weapons", "difficulties", "uuid", "name" };
        if (!recParseFields(a, MISSION_REC_GUARD, kRes, sizeof kRes / sizeof *kRes, "place_guard", ex.f, err)) return false;
        if (!recDifficulties(a, ex, err)) return false;
        const cJSON *ws = cJSON_GetObjectItemCaseSensitive(a, "weapons");
        if (ws) {
            if (!cJSON_IsArray(ws)) { err = "\"weapons\" must be a list (at most 2, one per hand) of { \"item\": id, \"model\": id, ...weapon record fields }"; return false; }
            if (argHas(a, "weapon")) { err = "give \"weapon\" (one item) or \"weapons\" (a list), not both"; return false; }
            if (cJSON_GetArraySize(ws) > MISSION_MAX_WEAPONS) { err = "a guard carries at most 2 weapons (one per hand)"; return false; }
            ex.hasWeapons = true;
            int k = 0;
            const cJSON *w;
            cJSON_ArrayForEach(w, ws) {
                if (!cJSON_IsObject(w)) { err = "\"weapons\"[" + std::to_string(k) + "] must be an object { \"item\": id, ... }"; return false; }
                static const char *kWRes[] = { "item", "model" };
                MissionWeapon mw; memset(&mw, 0, sizeof mw);
                mw.item = argInt(w, "item", -1); mw.model = argInt(w, "model", -1);
                if (!recParseFields(w, 8, kWRes, 2, "weapons[" + std::to_string(k) + "]", mw.f, err)) return false;
                ex.weapons.push_back(mw);
                k++;
            }
        }
    } else {
        const int type = argInt(a, "type", -1);
        static const char *kRes[] = { "pad", "pos", "room", "type", "model", "item", "difficulties", "uuid", "name" };
        std::vector<const char *> res(kRes, kRes + sizeof kRes / sizeof *kRes);
        if (!recParseFields(a, type, res.data(), res.size(), "place_object (type " + std::to_string(type) + ")", ex.f, err)) return false;
        if (!recDifficulties(a, ex, err)) return false;
    }
    return true;
}

static void recApplyPlace(Doc &d, const ItemRef &ref, bool guard, const RecExtras &ex)
{
    if (!ref.valid || !ref.added) return;
    if (guard) {
        MissionGuardOp &g = d.mission.guards[ref.idx];
        g.f = ex.f;
        if (ex.hasDiff) g.diff_mask = ex.diffMask;
        if (ex.hasWeapons) {
            g.nweapons = (int)ex.weapons.size();
            for (size_t i = 0; i < ex.weapons.size(); i++) g.weapons[i] = ex.weapons[i];
        }
    } else {
        MissionObjectOp &o = d.mission.objects[ref.idx];
        o.f = ex.f;
        if (ex.hasDiff) o.diff_mask = ex.diffMask;
    }
}

// ---------------------------------------------------------------------------------------------
// level_describe: links, names, and the model names on the guards of detail "props"

static cJSON *msModelJson(const ModelName &m)
{
    cJSON *x = cJSON_CreateObject();
    cJSON_AddNumberToObject(x, "id", m.id);
    cJSON_AddStringToObject(x, "file", m.file);
    cJSON_AddStringToObject(x, "label", m.label);
    return x;
}

ToolResult toolDescribeNames(Doc &, const cJSON *)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "note", "A guard's \"model\" is a body id, its \"head\" field a head id (-1 = random head); body 65535 = random body. "
        "Both index c_item_entries (the file is the converted model). Item ids are ITEM_IDS values (-1 = none).");
    cJSON *b = cJSON_AddArrayToObject(o, "bodies"), *h = cJSON_AddArrayToObject(o, "heads");
    for (int i = 0; i < kModelNameCount; i++) cJSON_AddItemToArray(i < kFirstHeadId ? b : h, msModelJson(kModelNames[i]));
    cJSON *it = cJSON_AddArrayToObject(o, "items");
    for (int i = 0; i < kItemNameCount; i++) {
        cJSON *x = cJSON_CreateObject();
        cJSON_AddNumberToObject(x, "id", kItemNames[i].id);
        cJSON_AddStringToObject(x, "name", kItemNames[i].name);
        cJSON_AddItemToArray(it, x);
    }
    return okJson(o);
}

// The guards of a describe("props") text get body_name / body_label (the model file and its enum label) and head_name / head_label.
static std::string msAnnotateGuards(const std::string &text)
{
    cJSON *root = cJSON_Parse(text.c_str());
    if (!root) return text;
    cJSON *e;
    cJSON_ArrayForEach(e, cJSON_GetObjectItemCaseSensitive(root, "items")) {
        if (argStr(e, "kind") != "guard") continue;
        const cJSON *mv = cJSON_GetObjectItemCaseSensitive(e, "model");
        if (cJSON_IsNumber(mv)) {
            if (mv->valuedouble == 65535) { cJSON_AddStringToObject(e, "body_name", "random"); }
            else if (const ModelName *m = modelName((int)mv->valuedouble)) { cJSON_AddStringToObject(e, "body_name", m->file); cJSON_AddStringToObject(e, "body_label", m->label); }
        }
        const cJSON *hv = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(e, "fields"), "head");
        if (cJSON_IsNumber(hv)) {
            if (hv->valuedouble < 0) cJSON_AddStringToObject(e, "head_name", "random");
            else if (const ModelName *m = modelName((int)hv->valuedouble)) { cJSON_AddStringToObject(e, "head_name", m->file); cJSON_AddStringToObject(e, "head_label", m->label); }
        }
    }
    char *s = cJSON_Print(root);
    std::string out = s ? s : text;
    free(s);
    cJSON_Delete(root);
    return out;
}

static uint32_t msRd32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static int msRd16u(const uint8_t *p) { return p[0] | (p[1] << 8); }
static int msRd16s(const uint8_t *p) { return (int16_t)(p[0] | (p[1] << 8)); }

// The level's links and tags, read from the same setup file the editor loaded the props from (the propDefs stream in file order:
// a prop number is the record's position). PROPDEF_SWITCH (19) records pair a terminal/console with a door, PROPDEF_LINK (14)
// two dual-wield weapons, PROPDEF_TAG (22) gives an object the tag id AI lists and objectives name. Indices in the file are
// relative to the record itself (src/game/prop.c).
ToolResult toolDescribeLinks(Doc &d, const cJSON *)
{
    if (!d.hasLevel) return fail("no level export is loaded");
    std::string text, err;
    if (!readFile(d.level.dir + "/level.json", text)) return fail("cannot read " + d.level.dir + "/level.json");
    cJSON *lj = cJSON_Parse(text.c_str());
    const std::string setup = lj ? argStr(lj, "setup") : "";
    if (lj) cJSON_Delete(lj);
    if (setup.empty()) return fail("level.json has no \"setup\" name: export the level again");
    uint8_t *raw = nullptr; uint32_t n = 0;
    char em[300] = "";
    if (!missionSetupLoadFile(d.level.dataDir.empty() ? "data" : d.level.dataDir.c_str(), setup.c_str(), &raw, &n, em, sizeof em)) return fail(em);
    struct Rec { int type; int a = -1, b = -1; int id = -1; };
    std::vector<Rec> recs;
    if (n >= 32) {
        uint32_t o = msRd32(raw + 24);   // header slot 3: the propDefs
        while (o + 4 <= n) {
            const int t = raw[o + 3];
            const unsigned sz = (t >= 1 && t <= 48) ? missionPropdefSize(t) : 0;
            if (t == MISSION_PROPDEF_END || !sz || o + sz > n) break;
            Rec r; r.type = t;
            const int idx = (int)recs.size();
            if (t == 19 || t == 14) { r.a = idx + (int)(int32_t)msRd32(raw + o + 8); r.b = idx + (int)(int32_t)msRd32(raw + o + 16); }
            else if (t == 22) { r.id = msRd16u(raw + o + 4); r.a = idx + msRd16s(raw + o + 6); }
            recs.push_back(r);
            o += sz;
        }
    }
    free(raw);
    auto typeOf = [&](int i) { return i >= 0 && i < (int)recs.size() ? recs[(size_t)i].type : -1; };

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "level", d.level.name.c_str());
    cJSON_AddStringToObject(root, "note", "prop numbers are setup record positions (the \"prop\" of level_describe props; scripts see them too). A door's "
        "bound_pad is the original bound pad it stands on (pad number, 10000 + the door record's bound-pad index).");
    cJSON *links = cJSON_AddArrayToObject(root, "links");
    cJSON *tags = cJSON_AddArrayToObject(root, "tags");
    std::map<int, std::vector<int>> termsOfDoor, tagsOfProp;
    for (size_t i = 0; i < recs.size(); i++) {
        const Rec &r = recs[i];
        if (r.type == 19) {
            cJSON *x = cJSON_CreateObject();
            cJSON_AddStringToObject(x, "kind", "switch");
            cJSON_AddNumberToObject(x, "record", (double)i);
            cJSON_AddNumberToObject(x, "terminal", r.a); cJSON_AddNumberToObject(x, "terminal_type", typeOf(r.a));
            cJSON_AddNumberToObject(x, "door", r.b); cJSON_AddNumberToObject(x, "door_type", typeOf(r.b));
            cJSON_AddItemToArray(links, x);
            termsOfDoor[r.b].push_back(r.a);
        } else if (r.type == 14) {
            cJSON *x = cJSON_CreateObject();
            cJSON_AddStringToObject(x, "kind", "dual_weapon");
            cJSON_AddNumberToObject(x, "record", (double)i);
            cJSON_AddNumberToObject(x, "first", r.a); cJSON_AddNumberToObject(x, "second", r.b);
            cJSON_AddItemToArray(links, x);
        } else if (r.type == 22) {
            cJSON *x = cJSON_CreateObject();
            cJSON_AddNumberToObject(x, "record", (double)i);
            cJSON_AddNumberToObject(x, "tag", r.id);
            cJSON_AddNumberToObject(x, "prop", r.a);
            cJSON_AddNumberToObject(x, "prop_type", typeOf(r.a));
            cJSON_AddItemToArray(tags, x);
            tagsOfProp[r.a].push_back(r.id);
        }
    }
    cJSON *doors = cJSON_AddArrayToObject(root, "doors");
    for (const LProp &p : d.level.props) {
        if (p.type != MISSION_PROPDEF_DOOR) continue;
        cJSON *x = cJSON_CreateObject();
        cJSON_AddNumberToObject(x, "prop", p.index);
        cJSON_AddNumberToObject(x, "bound_pad", p.pad);
        cJSON_AddNumberToObject(x, "bound_pad_index", p.pad >= 10000 ? p.pad - 10000 : p.pad);
        cJSON *ts = cJSON_AddArrayToObject(x, "terminals");
        for (int t : termsOfDoor[p.index]) cJSON_AddItemToArray(ts, cJSON_CreateNumber(t));
        cJSON *tg = cJSON_AddArrayToObject(x, "tags");
        for (int t : tagsOfProp[p.index]) cJSON_AddItemToArray(tg, cJSON_CreateNumber(t));
        cJSON_AddItemToArray(doors, x);
    }
    char *s = cJSON_Print(root);
    ToolResult r; r.text = s ? s : "{}";
    free(s);
    cJSON_Delete(root);
    return r;
}

// ---------------------------------------------------------------------------------------------
// script_check: a draft source, nothing written

ToolResult toolScriptCheck(Doc &d, const cJSON *a)
{
    const cJSON *t = cJSON_GetObjectItemCaseSensitive(a, "text");
    if (!cJSON_IsString(t)) return fail("\"text\" (the Luau source to check) is required");
    const std::string use = argStr(a, "use");
    std::string kind = argStr(a, "target_kind");
    if (kind.empty()) {   // as script_read: the kind of the first entry that uses this script, else mission
        kind = "mission";
        if (d.hasMission) for (int i = 0; i < d.mission.nscripts; i++) if (!use.empty() && use == d.mission.scripts[i].use) { kind = Doc::targetKindName(d.mission.scripts[i].kind); break; }
    }
    if (kind != "mission" && kind != "prop" && kind != "pad" && kind != "zone") return fail("\"target_kind\" is mission, prop, pad or zone");
    std::string defsErr;
    const std::vector<ScriptDiag> diags = checkScript(t->valuestring, kind, &defsErr);
    cJSON *o = cJSON_CreateObject();
    if (!use.empty()) cJSON_AddStringToObject(o, "use", use.c_str());
    cJSON_AddStringToObject(o, "target_kind", kind.c_str());
    int errors = 0;
    cJSON *arr = cJSON_AddArrayToObject(o, "diagnostics");
    for (const ScriptDiag &g : diags) {
        cJSON *x = cJSON_CreateObject();
        cJSON_AddNumberToObject(x, "line", g.line);
        cJSON_AddNumberToObject(x, "column", g.col);
        cJSON_AddStringToObject(x, "severity", g.sev >= 2 ? "error" : "warning");
        cJSON_AddStringToObject(x, "text", g.text.c_str());
        cJSON_AddItemToArray(arr, x);
        if (g.sev >= 2) errors++;
    }
    cJSON_AddNumberToObject(o, "errors", errors);
    if (!defsErr.empty()) cJSON_AddStringToObject(o, "defs_error", defsErr.c_str());
    const PropsSchema ps = parsePropsSchema(t->valuestring);
    cJSON *sc = cJSON_AddObjectToObject(o, "props_schema");
    cJSON_AddBoolToObject(sc, "found", ps.found);
    cJSON *fs = cJSON_AddArrayToObject(sc, "fields");
    for (const PropField &p : ps.fields) {
        cJSON *x = cJSON_CreateObject();
        cJSON_AddStringToObject(x, "name", p.name.c_str());
        cJSON_AddStringToObject(x, "type", p.type.c_str());
        cJSON_AddBoolToObject(x, "optional", p.optional);
        cJSON_AddItemToArray(fs, x);
    }
    if (!ps.error.empty()) cJSON_AddStringToObject(sc, "parse_error", ps.error.c_str());
    return okJson(o);
}

// ---------------------------------------------------------------------------------------------
// test_run: wait, read the game's log, stop

typedef std::filesystem::file_time_type RunMark;

static RunMark testRunMark() { return std::filesystem::file_time_type::clock::now(); }

// After a launch: with wait_seconds > 0 waits up to that long (cap 120; ends early when the game exits), then adds the game's log
// lines since the launch to `o`: "missions:" lines (objective completions, script errors and loads) and errors. stop: true then
// ends the game process this editor started (never another one).
// The HTTP transport asks (gAllowDefer) for the wait to run off the Doc's thread: the tail then lands in gDeferred, which
// mcpHandleMessage hands out as *post. It uses no Doc (only the log path, copied).
static bool gAllowDefer = false;
static std::function<void(cJSON *)> gDeferred;

static void testRunTail(int wait, bool stop, const std::string &log, const RunMark &mark, cJSON *o)
{
    if (wait > 0) {
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(wait);
        while (std::chrono::steady_clock::now() < until && launchedGameRunning())
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        cJSON_AddStringToObject(o, "log", log.c_str());
        cJSON *ms = cJSON_AddArrayToObject(o, "missions");
        cJSON *es = cJSON_AddArrayToObject(o, "errors");
        std::error_code ec;
        bool fresh = false;
        if (!log.empty()) { const auto mt = std::filesystem::last_write_time(log, ec); fresh = !ec && mt >= mark; }
        std::string text;
        if (fresh && readFile(log, text)) {   // the game rolls the previous log away at its start: a log older than the launch is not this run's
            std::vector<std::string> mLines, eLines;
            size_t p = 0;
            while (p < text.size()) {
                size_t e = text.find('\n', p);
                if (e == std::string::npos) e = text.size();
                std::string line = text.substr(p, e - p);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.find("missions:") != std::string::npos) mLines.push_back(line);
                else if (line.rfind("[ERROR]", 0) == 0 || line.rfind("[FATAL]", 0) == 0) eLines.push_back(line);
                p = e + 1;
            }
            const size_t cap = 300;
            for (size_t i = mLines.size() > cap ? mLines.size() - cap : 0; i < mLines.size(); i++) cJSON_AddItemToArray(ms, cJSON_CreateString(mLines[i].c_str()));
            for (size_t i = eLines.size() > 50 ? eLines.size() - 50 : 0; i < eLines.size(); i++) cJSON_AddItemToArray(es, cJSON_CreateString(eLines[i].c_str()));
        } else {
            cJSON_AddStringToObject(o, "note", "no log of this run yet (the game has not written ge007.log since the launch)");
        }
        cJSON_AddNumberToObject(o, "waited", wait);
    }
    cJSON_AddBoolToObject(o, "running", launchedGameRunning());
    if (stop) {
        const bool was = launchedGameStop();
        cJSON_AddBoolToObject(o, "stopped", was);
        if (was) {   // let the process go
            for (int i = 0; i < 20 && launchedGameRunning(); i++) std::this_thread::sleep_for(std::chrono::milliseconds(100));
            cJSON_ReplaceItemInObject(o, "running", cJSON_CreateBool(launchedGameRunning()));
        }
    }
}

static void testRunReport(Doc &d, const cJSON *a, const RunMark &mark, cJSON *o)
{
    int wait = argInt(a, "wait_seconds", 0);
    wait = wait < 0 ? 0 : wait > 120 ? 120 : wait;
    const bool stop = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(a, "stop"));
    const std::string log = gameLogPath(d.gameExe);
    if (wait > 0 && gAllowDefer) {
        gDeferred = [wait, stop, log, mark](cJSON *res) { testRunTail(wait, stop, log, mark, res); };
        return;
    }
    testRunTail(wait, stop, log, mark, o);
}
