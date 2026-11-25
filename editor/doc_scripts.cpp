// Scripts and zones of Doc: the mission's script entries (mission.json "scripts"), the Luau sources they use, and the
// mission's zones. Entries and zones are mission edits (one undo step each); the script text is held in scriptBuf and written
// next to mission.json when the mission is saved.
#include "doc.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "cJSON.h"
extern "C" {
#include "mission_lua.h"
}
#include "script_props.h"

namespace fs = std::filesystem;

template <class T> static void arrPush(T *&a, int &n, const T &v)
{
    T *nb = (T *)realloc(a, sizeof(T) * (size_t)(n + 1));
    if (!nb) abort();
    a = nb;
    a[n++] = v;
}
template <class T> static void arrErase(T *a, int &n, int i)
{
    for (int k = i; k + 1 < n; k++) a[k] = a[k + 1];
    n--;
}
static void fillStr(char *dst, size_t n, const std::string &s) { snprintf(dst, n, "%s", s.c_str()); }

static bool useOk(const std::string &u)
{
    if (u.empty() || u.size() >= MISSION_ID_LEN) return false;
    for (char c : u) if (!(isalnum((unsigned char)c) || c == '_' || c == '-')) return false;
    return true;
}

static bool fileExists(const std::string &p) { std::error_code ec; return fs::is_regular_file(fs::path(p), ec); }

const char *Doc::targetKindName(MissionTargetKind k)
{
    return k == MISSION_TGT_PROP ? "prop" : k == MISSION_TGT_PAD ? "pad" : k == MISSION_TGT_ZONE ? "zone" : "mission";
}

// ---------------------------------------------------------------------------------------------
// sources

std::string Doc::missionDir() const
{
    if (missionPath.empty()) return "";
    return fs::path(missionPath).parent_path().string();
}

std::string Doc::newScriptPath(const std::string &use) const
{
    const std::string dir = missionDir();
    if (dir.empty() || !useOk(use)) return "";
    // an existing <dir>/<use>.luau stays where it is (the runtime looks there first)
    const std::string flat = (fs::path(dir) / (use + ".luau")).string();
    if (fileExists(flat)) return flat;
    return (fs::path(dir) / "scripts" / (use + ".luau")).string();
}

std::vector<std::string> Doc::libraryScripts() const
{
    std::vector<std::string> out;
    for (const MissionLibScript *s = g_missionLibScripts; s->name; s++) out.push_back(s->name);
    return out;
}

std::vector<std::string> Doc::missionScriptNames() const
{
    std::vector<std::string> out;
    const std::string dir = missionDir();
    for (const std::string &sub : { std::string(), std::string("scripts") }) {
        if (dir.empty()) break;
        std::error_code ec;
        const fs::path d = sub.empty() ? fs::path(dir) : fs::path(dir) / sub;
        for (fs::directory_iterator it(d, ec), end; !ec && it != end; it.increment(ec))
            if (it->is_regular_file(ec) && it->path().extension() == ".luau") out.push_back(it->path().stem().string());
    }
    for (const auto &kv : scriptBuf) out.push_back(kv.first);
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

Doc::ScriptSrc Doc::scriptSource(const std::string &use) const
{
    ScriptSrc r;
    if (!useOk(use)) return r;
    const std::string dir = missionDir();
    auto b = scriptBuf.find(use);
    if (b != scriptBuf.end()) { r.origin = SRC_MISSION; r.text = b->second; r.path = newScriptPath(use); return r; }
    auto c = scriptCache.find(use);
    if (c != scriptCache.end()) return c->second;
    r = scriptSourceUncached(use, dir);
    scriptCache[use] = r;
    return r;
}

Doc::ScriptSrc Doc::scriptSourceUncached(const std::string &use, const std::string &dir) const
{
    ScriptSrc r;
    if (!dir.empty()) {
        for (const std::string &p : { (fs::path(dir) / (use + ".luau")).string(), (fs::path(dir) / "scripts" / (use + ".luau")).string() })
            if (readFile(p, r.text)) { r.origin = SRC_MISSION; r.path = p; return r; }
        const std::string mod = hasMod ? modDir : modOfMission(missionPath);
        if (!mod.empty()) {
            const std::string p = (fs::path(mod) / "scripts" / (use + ".luau")).string();
            if (readFile(p, r.text)) { r.origin = SRC_MOD; r.path = p; return r; }
        }
    }
    for (const MissionLibScript *s = g_missionLibScripts; s->name; s++)
        if (use == s->name) { r.origin = SRC_LIBRARY; r.text = s->source; r.path = std::string("(library) ") + s->name; return r; }
    return r;
}

bool Doc::setScriptText(const std::string &use, const std::string &text, std::string &err)
{
    if (!hasMission) { err = "no mission is open"; return false; }
    if (!useOk(use)) { err = "a script name is letters, digits, _ and - (at most 31)"; return false; }
    if (missionDir().empty()) { err = "save the mission first: its scripts live next to mission.json"; return false; }
    auto b = scriptBuf.find(use);
    if (b != scriptBuf.end() && b->second == text) return true;
    scriptBuf[use] = text;
    scriptDirty.insert(use);
    dirty = true;
    return true;
}

bool Doc::newScript(std::string &use, std::string &err)
{
    if (!hasMission) { err = "no mission is open"; return false; }
    std::string base = use.empty() ? "my_script" : use;
    for (char &c : base) if (!(isalnum((unsigned char)c) || c == '_' || c == '-')) c = '_';
    if (base.size() > 24) base.resize(24);
    const std::vector<std::string> have = missionScriptNames();
    std::string name = base;
    for (int n = 2; std::find(have.begin(), have.end(), name) != have.end() || scriptSource(name).origin != SRC_NONE; n++) name = base + "_" + std::to_string(n);
    const std::string text =
        "-- " + name + ": what this script does.\n"
        "-- target: what it attaches to (a door prop, a pad, a zone, or the mission).\n"
        "-- props: the values the editor asks for are the fields of Props below.\n"
        "export type Props = {}\n\n"
        "wait(event(\"start\"))\n";
    if (!setScriptText(name, text, err)) return false;
    use = name;
    return true;
}

bool Doc::copyScriptToMission(const std::string &use, std::string &newUse, std::string &err)
{
    const ScriptSrc s = scriptSource(use);
    if (s.origin == SRC_NONE) { err = "no script '" + use + "'"; return false; }
    if (s.origin == SRC_MISSION) { err = "'" + use + "' is already the mission's own script"; return false; }
    std::string name = use;
    const std::vector<std::string> have = missionScriptNames();
    for (int n = 2; std::find(have.begin(), have.end(), name) != have.end(); n++) name = use + "_" + std::to_string(n);
    if (!setScriptText(name, s.text, err)) return false;
    newUse = name;
    return true;
}

bool Doc::flushScripts(const std::string &missionFile, std::string &err)
{
    const std::string newDir = fs::path(missionFile).parent_path().string();
    const std::string curDir = missionDir();
    std::error_code ec;
    std::map<std::string, std::string> toWrite;
    for (const std::string &u : scriptDirty) { auto b = scriptBuf.find(u); if (b != scriptBuf.end()) toWrite[u] = b->second; }
    if (newDir != curDir && !curDir.empty() && !fs::equivalent(fs::path(newDir), fs::path(curDir), ec)) {
        // Save as: the mission's own scripts follow it
        for (const std::string &u : missionScriptNames()) {
            if (toWrite.count(u)) continue;
            ScriptSrc s = scriptSource(u);
            if (s.origin == SRC_MISSION) toWrite[u] = s.text;
        }
    }
    for (const auto &kv : toWrite) {
        const std::string flat = (fs::path(newDir) / (kv.first + ".luau")).string();
        const std::string p = fileExists(flat) ? flat : (fs::path(newDir) / "scripts" / (kv.first + ".luau")).string();
        fs::create_directories(fs::path(p).parent_path(), ec);
        std::ofstream f(p, std::ios::binary);
        if (!f) { err = "cannot write " + p; return false; }
        f << kv.second;
        if (!f) { err = "cannot write " + p; return false; }
    }
    scriptDirty.clear();
    scriptCache.clear();
    return true;
}

Doc::ScriptDiagnostics Doc::scriptDiagnostics(const std::string &use, const std::string &targetKind) const
{
    ScriptDiagnostics d;
    const ScriptSrc s = scriptSource(use);
    if (s.origin == SRC_NONE) return d;
    d.found = true; d.origin = s.origin;
    d.hasProps = parsePropsSchema(s.text).found;
    d.diags = checkScript(s.text, targetKind, &d.defsError);
    return d;
}

// ---------------------------------------------------------------------------------------------
// entries

bool Doc::addScript(const std::string &use, MissionTargetKind kind, int &outIndex, std::string &err)
{
    if (!hasMission) { err = "no mission is open"; return false; }
    if (!useOk(use)) { err = "a script name is letters, digits, _ and - (at most 31)"; return false; }
    const std::string snap = snapshot();
    MissionScript s; memset(&s, 0, sizeof s);
    fillStr(s.use, sizeof s.use, use);
    s.kind = kind; s.prop = -1; s.pad.is_new = 0; s.pad.num = -1;
    arrPush(mission.scripts, mission.nscripts, s);
    outIndex = mission.nscripts - 1;
    commit(snap);
    return true;
}

static bool scriptIdx(const Doc &d, int i, std::string &err)
{
    if (!d.hasMission) { err = "no mission is open"; return false; }
    if (i < 0 || i >= d.mission.nscripts) { err = "no script " + std::to_string(i) + " (the mission has " + std::to_string(d.mission.nscripts) + ")"; return false; }
    return true;
}

bool Doc::removeScript(int index, std::string &err)
{
    if (!scriptIdx(*this, index, err)) return false;
    const std::string snap = snapshot();
    free(mission.scripts[index].props);
    arrErase(mission.scripts, mission.nscripts, index);
    commit(snap);
    return true;
}

bool Doc::moveScript(int index, int newIndex, std::string &err)
{
    if (!scriptIdx(*this, index, err)) return false;
    if (newIndex < 0 || newIndex >= mission.nscripts) { err = "no such position"; return false; }
    if (index == newIndex) return true;
    const std::string snap = snapshot();
    const MissionScript s = mission.scripts[index];
    if (index < newIndex) for (int k = index; k < newIndex; k++) mission.scripts[k] = mission.scripts[k + 1];
    else for (int k = index; k > newIndex; k--) mission.scripts[k] = mission.scripts[k - 1];
    mission.scripts[newIndex] = s;
    commit(snap);
    return true;
}

bool Doc::setScriptUse(int index, const std::string &use, std::string &err)
{
    if (!scriptIdx(*this, index, err)) return false;
    if (!useOk(use)) { err = "a script name is letters, digits, _ and - (at most 31)"; return false; }
    const std::string snap = snapshot();
    fillStr(mission.scripts[index].use, sizeof mission.scripts[index].use, use);
    commit(snap);
    return true;
}

bool Doc::setScriptTarget(int index, MissionTargetKind kind, int prop, const MissionPadRef &pad, const std::string &zone, std::string &err, const std::string &uuid)
{
    if (!scriptIdx(*this, index, err)) return false;
    if (kind == MISSION_TGT_PROP && !uuid.empty() && !missionFindAdded(&mission, uuid.c_str(), nullptr)) { err = "no added prop has uuid " + uuid; return false; }
    const std::string snap = snapshot();
    MissionScript &s = mission.scripts[index];
    s.kind = kind;
    s.prop = kind == MISSION_TGT_PROP && uuid.empty() ? prop : -1;
    s.uuid[0] = 0;
    if (kind == MISSION_TGT_PROP && !uuid.empty()) fillStr(s.uuid, sizeof s.uuid, uuid);
    memset(&s.pad, 0, sizeof s.pad); s.pad.num = -1;
    if (kind == MISSION_TGT_PAD) s.pad = pad;
    s.zone[0] = 0;
    if (kind == MISSION_TGT_ZONE) fillStr(s.zone, sizeof s.zone, zone);
    commit(snap);
    return true;
}

static void putProp(MissionScript &s, const std::string &name, const MissionProp *v)
{
    int at = -1;
    for (int k = 0; k < s.nprops; k++) if (name == s.props[k].name) at = k;
    if (!v) {
        if (at >= 0) arrErase(s.props, s.nprops, at);
        return;
    }
    MissionProp p = *v;
    fillStr(p.name, sizeof p.name, name);
    if (at >= 0) s.props[at] = p; else arrPush(s.props, s.nprops, p);
}

bool Doc::setScriptProp(int index, const std::string &name, const MissionProp *value, std::string &err)
{
    if (!scriptIdx(*this, index, err)) return false;
    if (name.empty() || name.size() >= MISSION_ID_LEN) { err = "a property name is 1..31 characters"; return false; }
    const std::string snap = snapshot();
    putProp(mission.scripts[index], name, value);
    commit(snap);
    return true;
}

bool Doc::setScriptProps(int index, const cJSON *obj, bool replace, std::string &err)
{
    if (!scriptIdx(*this, index, err)) return false;
    if (!cJSON_IsObject(obj)) { err = "\"props\" must be an object of name: number | string | boolean"; return false; }
    const cJSON *e;
    cJSON_ArrayForEach(e, obj)
        if (!cJSON_IsNumber(e) && !cJSON_IsString(e) && !cJSON_IsBool(e)) { err = std::string("property '") + (e->string ? e->string : "") + "' must be a number, string or boolean"; return false; }
    const std::string snap = snapshot();
    MissionScript &s = mission.scripts[index];
    if (replace) { free(s.props); s.props = nullptr; s.nprops = 0; }
    cJSON_ArrayForEach(e, obj) {
        MissionProp p; memset(&p, 0, sizeof p);
        if (cJSON_IsNumber(e)) { p.kind = MISSION_VAL_NUMBER; p.num = e->valuedouble; }
        else if (cJSON_IsBool(e)) { p.kind = MISSION_VAL_BOOL; p.num = cJSON_IsTrue(e) ? 1 : 0; }
        else { p.kind = MISSION_VAL_STRING; fillStr(p.str, sizeof p.str, e->valuestring); }
        putProp(s, e->string, &p);
    }
    commit(snap);
    return true;
}

// ---------------------------------------------------------------------------------------------
// zones

int Doc::zoneIndex(const std::string &id) const
{
    for (int i = 0; i < mission.nzones; i++) if (id == mission.zones[i].id) return i;
    return -1;
}

bool Doc::addZone(const Vec3 &pos, float radius, int room, std::string &id, std::string &err)
{
    if (!hasMission) { err = "no mission is open"; return false; }
    if (!(radius > 0)) { err = "a zone's radius must be positive"; return false; }
    if (id.empty()) {
        for (int n = 1;; n++) { id = "z" + std::to_string(n); if (zoneIndex(id) < 0) break; }
    } else if (id.size() >= MISSION_ID_LEN) { err = "a zone id is at most 31 characters"; return false; }
    else if (zoneIndex(id) >= 0) { err = "there is already a zone '" + id + "'"; return false; }
    const std::string snap = snapshot();
    MissionZone z; memset(&z, 0, sizeof z);
    fillStr(z.id, sizeof z.id, id);
    for (int i = 0; i < 3; i++) z.pos[i] = pos[i];
    z.radius = radius;
    z.room = room >= 0 ? room : roomAt(pos);
    arrPush(mission.zones, mission.nzones, z);
    commit(snap);
    return true;
}

bool Doc::setZone(const std::string &id, const Vec3 *pos, const float *radius, const int *room, bool commitUndo, std::string &err)
{
    if (!hasMission) { err = "no mission is open"; return false; }
    const int i = zoneIndex(id);
    if (i < 0) { err = "no zone '" + id + "'"; return false; }
    if (radius && !(*radius > 0)) { err = "a zone's radius must be positive"; return false; }
    const std::string snap = snapshot();
    MissionZone &z = mission.zones[i];
    if (pos) {
        for (int k = 0; k < 3; k++) z.pos[k] = (*pos)[k];
        if (!room) { const int r = roomAt(*pos); if (r >= 0) z.room = r; }
    }
    if (radius) z.radius = *radius;
    if (room) z.room = *room;
    if (commitUndo) commit(snap);
    return true;
}

bool Doc::removeZone(const std::string &id, std::string &err)
{
    if (!hasMission) { err = "no mission is open"; return false; }
    const int i = zoneIndex(id);
    if (i < 0) { err = "no zone '" + id + "'"; return false; }
    const std::string snap = snapshot();
    arrErase(mission.zones, mission.nzones, i);
    commit(snap);
    return true;
}

// ---------------------------------------------------------------------------------------------
// checks

void Doc::scriptIssues(std::vector<Issue> &out) const
{
    char buf[512];
    std::set<std::string> checked;   // (use, kind) already type-checked: several entries of one script report once
    bool defsReported = false;
    for (int i = 0; i < mission.nscripts; i++) {
        const MissionScript &s = mission.scripts[i];
        auto add = [&](int sev, const std::string &t, int line = 0) {
            Issue is; is.sev = sev; is.text = t; is.hasSel = true; is.sel.kind = Selection::SCRIPT; is.sel.num = i;
            is.script = s.use; is.line = line;
            out.push_back(is);
        };
        const ScriptSrc src = scriptSource(s.use);
        if (src.origin == SRC_NONE) {
            snprintf(buf, sizeof buf, "script %d: there is no script named '%s' (not in the mission's folder, its mod or the library)", i, s.use);
            add(2, buf);
            continue;
        }
        if (s.kind == MISSION_TGT_PROP && s.prop < 0 && !s.uuid[0]) { snprintf(buf, sizeof buf, "script %d (%s): its target prop is not set", i, s.use); add(2, buf); }
        if (s.kind == MISSION_TGT_PAD && !s.pad.is_new && s.pad.num < 0) { snprintf(buf, sizeof buf, "script %d (%s): its target pad is not set", i, s.use); add(2, buf); }
        if (s.kind == MISSION_TGT_ZONE && !s.zone[0]) { snprintf(buf, sizeof buf, "script %d (%s): its target zone is not set", i, s.use); add(2, buf); }

        const PropsSchema ps = parsePropsSchema(src.text);
        auto findProp = [&](const std::string &n) -> const MissionProp * {
            for (int k = 0; k < s.nprops; k++) if (n == s.props[k].name) return &s.props[k];
            return nullptr;
        };
        if (ps.found) {
            for (const PropField &f : ps.fields) {
                const MissionProp *p = findProp(f.name);
                if (!p) {
                    if (!f.optional) { snprintf(buf, sizeof buf, "script %d (%s): property '%s' (%s) is not set", i, s.use, f.name.c_str(), f.type.c_str()); add(2, buf); }
                    continue;
                }
                const std::string &t = f.type;
                const bool refT = t == "Door" || t == "Guard" || t == "Prop";   // an original prop's number, or an added prop's uuid string
                const bool numT = t == "Item" || t == "number";
                const bool strT = t == "Objective" || t == "Zone" || t == "string";
                const bool boolT = t == "boolean";
                const bool padT = t == "Pad";
                const bool isNum = p->kind == MISSION_VAL_NUMBER, isStr = p->kind == MISSION_VAL_STRING, isBool = p->kind == MISSION_VAL_BOOL;
                if ((numT && !isNum) || (strT && !isStr) || (boolT && !isBool) || (padT && !isNum && !isStr) || (refT && !isNum && !isStr)) {
                    snprintf(buf, sizeof buf, "script %d (%s): property '%s' is a %s but Props says %s", i, s.use, f.name.c_str(),
                             isNum ? "number" : isStr ? "string" : "boolean", t.c_str());
                    add(2, buf);
                    continue;
                }
                if (t == "Zone" && zoneIndex(p->str) < 0) { snprintf(buf, sizeof buf, "script %d (%s): property '%s': there is no zone '%s'", i, s.use, f.name.c_str(), p->str); add(2, buf); }
                if (t == "Objective" && f.name != "objective") {   // 'objective' is already reported by the older check
                    bool found = false;
                    for (int o = 0; o < mission.nobjectives; o++) if (!strcmp(mission.objectives[o].id, p->str)) found = true;
                    if (!found) { snprintf(buf, sizeof buf, "script %d (%s): property '%s': objective '%s' is not in the mission's objectives", i, s.use, f.name.c_str(), p->str); add(1, buf); }
                }
                if (refT && isStr) {   // an added prop, by uuid
                    ItemRef ir; int ty = 0;
                    if (!propByUuid(p->str, ir, ty)) { snprintf(buf, sizeof buf, "script %d (%s): property '%s': no prop the mission adds has uuid '%s' (deleted?)", i, s.use, f.name.c_str(), p->str); add(2, buf); }
                    else if (t == "Door" && ty != 1) { snprintf(buf, sizeof buf, "script %d (%s): property '%s': %s is not a door (type %d)", i, s.use, f.name.c_str(), propLabel(ir).c_str(), ty); add(2, buf); }
                    else if (t == "Guard" && ty != 9) { snprintf(buf, sizeof buf, "script %d (%s): property '%s': %s is not a guard (type %d)", i, s.use, f.name.c_str(), propLabel(ir).c_str(), ty); add(2, buf); }
                    else if (t == "Prop" && ty == 9) { snprintf(buf, sizeof buf, "script %d (%s): property '%s': %s is a guard, not an object", i, s.use, f.name.c_str(), propLabel(ir).c_str()); add(2, buf); }
                } else if (refT && hasLevel) {   // an original prop, by number
                    ItemRef ir; int ty = 0;
                    if ((int)p->num >= (int)level.props.size()) { snprintf(buf, sizeof buf, "script %d (%s): property '%s': %d is not a prop of the level (it has %d); a prop the mission adds is referenced by its uuid, not a number", i, s.use, f.name.c_str(), (int)p->num, (int)level.props.size()); add(2, buf); }
                    else if (!propByNumber((int)p->num, ir, ty)) { snprintf(buf, sizeof buf, "script %d (%s): property '%s': there is no prop %d", i, s.use, f.name.c_str(), (int)p->num); add(2, buf); }
                    else if (t == "Door" && ty != 1) { snprintf(buf, sizeof buf, "script %d (%s): property '%s': prop %d is not a door (type %d)", i, s.use, f.name.c_str(), (int)p->num, ty); add(1, buf); }
                    else if (t == "Guard" && ty != 9) { snprintf(buf, sizeof buf, "script %d (%s): property '%s': prop %d is not a guard (type %d)", i, s.use, f.name.c_str(), (int)p->num, ty); add(1, buf); }
                }
                if (t == "Pad") {
                    bool ok = true;
                    if (isStr) { ok = false; for (int k = 0; k < mission.npads; k++) if (!strcmp(mission.pads[k].id, p->str)) ok = true; }
                    else if (hasLevel) ok = level.pad((int)p->num) != nullptr;
                    if (!ok) { snprintf(buf, sizeof buf, "script %d (%s): property '%s': that pad does not exist", i, s.use, f.name.c_str()); add(2, buf); }
                }
            }
            for (int k = 0; k < s.nprops; k++) {
                bool known = false;
                for (const PropField &f : ps.fields) if (f.name == s.props[k].name) known = true;
                if (!known) { snprintf(buf, sizeof buf, "script %d (%s): '%s' is not a field of its Props", i, s.use, s.props[k].name); add(1, buf); }
            }
        }

        const std::string kind = targetKindName(s.kind);
        if (!checked.insert(std::string(s.use) + ":" + kind).second) continue;
        const ScriptDiagnostics dg = scriptDiagnostics(s.use, kind);
        if (!dg.defsError.empty() && !defsReported) {
            defsReported = true;
            Issue is; is.sev = 1; is.text = "script type checker: " + dg.defsError;
            out.push_back(is);
        }
        for (const ScriptDiag &d : dg.diags) {
            snprintf(buf, sizeof buf, "script %s line %d: %s", s.use, d.line, d.text.c_str());
            add(d.sev, buf, d.line);
        }
    }
}

// ---------------------------------------------------------------------------------------------
// prop numbers

int Doc::propTotal() const
{
    int n = hasLevel ? (int)level.props.size() : 0;
    for (int i = 0; i < mission.nguards; i++) if (mission.guards[i].op == MISSION_OP_ADD) n += 1 + std::min(mission.guards[i].nweapons, (int)MISSION_MAX_WEAPONS);
    for (int i = 0; i < mission.nobjects; i++) if (mission.objects[i].op == MISSION_OP_ADD) n++;
    return n;
}

int Doc::addedPropNumber(const ItemRef &r) const
{
    if (!r.valid) return -1;
    if (!r.added) return r.idx;
    int n = hasLevel ? (int)level.props.size() : 0;
    for (int i = 0; i < mission.nguards; i++) {
        if (mission.guards[i].op != MISSION_OP_ADD) continue;
        if (r.guard && r.idx == i) return n;
        n += 1 + std::min(mission.guards[i].nweapons, (int)MISSION_MAX_WEAPONS);
    }
    for (int i = 0; i < mission.nobjects; i++) {
        if (mission.objects[i].op != MISSION_OP_ADD) continue;
        if (!r.guard && r.idx == i) return n;
        n++;
    }
    return -1;
}

bool Doc::propByUuid(const std::string &uuid, ItemRef &out, int &type) const
{
    out = ItemRef();
    int idx = -1;
    const int k = missionFindAdded(&mission, uuid.c_str(), &idx);
    if (!k) return false;
    out.valid = true; out.added = true; out.guard = k == 1; out.idx = idx;
    type = k == 1 ? 9 : mission.objects[idx].type;
    return true;
}

std::string Doc::propLabel(const ItemRef &r) const
{
    if (!r.valid) return "?";
    if (!r.added) {
        const LProp *p = hasLevel ? level.prop(r.idx) : nullptr;
        return "#" + std::to_string(r.idx) + (p ? (p->type == 9 ? " guard" : p->type == 1 ? " door" : " object") : "");
    }
    const std::string name = r.guard ? mission.guards[r.idx].name : mission.objects[r.idx].name;
    const std::string uuid = r.guard ? mission.guards[r.idx].uuid : mission.objects[r.idx].uuid;
    const char *kind = r.guard ? "guard" : mission.objects[r.idx].type == MISSION_PROPDEF_DOOR ? "door" : "object";
    return (name.empty() ? uuid.substr(0, 8) : name) + " (added " + kind + ")";
}

bool Doc::propByNumber(int num, ItemRef &out, int &type) const
{
    out = ItemRef();
    if (num < 0) return false;
    const int nOrig = hasLevel ? (int)level.props.size() : 0;
    if (num < nOrig) {
        const LProp *p = level.prop(num);
        if (!p) return false;
        out.valid = true; out.added = false; out.guard = p->type == 9; out.idx = num;
        type = p->type;
        return true;
    }
    int n = nOrig;
    for (int i = 0; i < mission.nguards; i++) {
        if (mission.guards[i].op != MISSION_OP_ADD) continue;
        const int w = std::min(mission.guards[i].nweapons, (int)MISSION_MAX_WEAPONS);
        if (num == n) { out.valid = true; out.added = true; out.guard = true; out.idx = i; type = 9; return true; }
        if (num > n && num <= n + w) return false;   // a weapon it carries
        n += 1 + w;
    }
    for (int i = 0; i < mission.nobjects; i++) {
        if (mission.objects[i].op != MISSION_OP_ADD) continue;
        if (num == n) { out.valid = true; out.added = true; out.guard = false; out.idx = i; type = mission.objects[i].type; return true; }
        n++;
    }
    return false;
}
