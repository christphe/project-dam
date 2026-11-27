// ui_actions.cpp: see ui_app.h for how the editor UI is split.
#include "ui_app.h"
#include "ui_scripts.h"
#include <cmath>
#include <filesystem>
#include "launch.h"
#include "ge_render.h"
#include "png.h"

namespace ed {

bool itemIsSel(const App &a, const Item &it)
{
    return a.doc.sel.kind == Selection::ITEM && a.doc.sel.item.added == it.added && a.doc.sel.item.guard == it.guard &&
           a.doc.sel.item.idx == it.idx;
}

const Item *selectedItem(const App &a)
{
    if (a.doc.sel.kind != Selection::ITEM) return nullptr;
    for (const Item &it : a.items) if (itemIsSel(a, it)) return &it;
    return nullptr;
}

std::string padLabel(const MissionPadRef &r)
{
    char b[64];
    if (r.is_new) snprintf(b, sizeof b, "%s", r.id); else snprintf(b, sizeof b, "0x%x", r.num);
    return b;
}

void selectItem(App &a, const Item &it)
{
    a.doc.sel = Selection();
    a.doc.sel.kind = Selection::ITEM;
    a.doc.sel.item.valid = true;
    a.doc.sel.item.added = it.added;
    a.doc.sel.item.guard = it.guard;
    a.doc.sel.item.idx = it.idx;
}

void focusOn(App &a, const Vec3 &p, float dist) { a.cam.lookAtPoint(p, dist); }

bool padFromHit(const App &a, const Hit &h, PadSpec &ps)
{
    switch (h.kind) {
    case Hit::PAD: ps.isNew = false; ps.num = h.num; return true;
    case Hit::MPAD: ps.isNew = true; ps.id = h.id; return true;
    case Hit::ITEM:
        for (const Item &it : a.items)
            if (it.added == h.item.added && it.guard == h.item.guard && it.idx == h.item.idx) {
                if (it.pad.is_new) { ps.isNew = true; ps.id = it.pad.id; } else { ps.isNew = false; ps.num = it.pad.num; }
                return true;
            }
        return false;
    case Hit::SURFACE:
        ps.atPoint = true; ps.pos = h.surfacePoint; ps.room = h.room;
        a.doc.snapPoint(ps.pos, a.snapOpts());
        return true;
    default: return false;
    }
}

// Editing needs a mission: the first edit on a bare level starts one (unsaved until Save).
bool ensureMission(App &a)
{
    if (a.doc.hasMission) return true;
    std::string err;
    if (!a.doc.hasLevel) { a.say("open a level or a mission first (File panel)"); return false; }
    const Selection keep = a.doc.sel;   // an original prop or pad stays the same thing in an empty mission
    if (!a.doc.newMission(a.newName, "", err)) { a.say("new mission: %s", err.c_str()); return false; }
    a.doc.sel = keep;
    a.say("started a new mission on %s (not saved yet: Save writes it)", a.doc.mission.level);
    return true;
}

void applyClick(App &a, const Hit &h)
{
    std::string err;
    if (scriptPickClick(a, h)) return;      // picking a script's target / property in the view
    if (zoneToolClick(a, h)) return;        // the Zone tool
    if (a.pickSpawn && (h.kind == Hit::PAD || h.kind == Hit::MPAD || h.kind == Hit::ITEM)) {   // Mission tab > Start and test > pick in the view
        PadSpec ps;
        if (padFromHit(a, h, ps)) {
            if (ps.isNew) a.spawn = ps.id; else a.spawn = std::to_string(ps.num);
            a.pickSpawn = false;
            a.say("Play starts at pad %s", a.spawn.c_str());
            return;
        }
    }
    if (a.tool == App::T_SELECT || h.kind == Hit::NONE) {
        a.doc.sel = Selection();
        if (h.kind == Hit::ITEM) a.doc.sel.kind = Selection::ITEM, a.doc.sel.item = h.item;
        else if (h.kind == Hit::PAD) a.doc.sel.kind = Selection::PAD, a.doc.sel.num = h.num;
        else if (h.kind == Hit::MPAD) a.doc.sel.kind = Selection::MPAD, a.doc.sel.id = h.id;
        else if (h.kind == Hit::ZONE) a.doc.sel.kind = Selection::ZONE, a.doc.sel.id = h.id;
        else if (h.kind == Hit::SURFACE) a.doc.sel.kind = Selection::ROOM, a.doc.sel.num = h.room;
        return;
    }
    if (!ensureMission(a)) return;
    PadSpec ps;
    if (!padFromHit(a, h, ps)) return;
    ItemRef ref;
    if (a.tool == App::T_GUARD) {
        if (a.doc.addGuard(ps, a.gModel, a.gAi, a.gWeapon, ref, err)) {
            a.doc.sel = Selection(); a.doc.sel.kind = Selection::ITEM; a.doc.sel.item = ref;
            a.say("guard added");
        } else a.say("add guard: %s", err.c_str());
    } else if (a.tool == App::T_OBJECT) {
        if (a.doc.addObject(ps, a.oType, a.oModel, a.oItem, ref, err)) {
            a.doc.sel = Selection(); a.doc.sel.kind = Selection::ITEM; a.doc.sel.item = ref;
            a.say("object added");
        } else a.say("add object: %s", err.c_str());
    } else if (a.tool == App::T_MOVE) {
        if (a.doc.sel.kind != Selection::ITEM) { a.say("select a guard or object first"); return; }
        if (a.doc.moveItem(a.doc.sel.item, ps, err)) a.say("moved");
        else a.say("move: %s", err.c_str());
    }
}

void dragBegin(App &a, int hit, const Vec3 &org, const Vec3 &dir);

void selectNewItem(App &a, const ItemRef &ref)
{
    a.doc.sel = Selection(); a.doc.sel.kind = Selection::ITEM; a.doc.sel.item = ref;
}

void copySelected(App &a)
{
    std::string err;
    if (a.doc.sel.kind != Selection::ITEM) { a.say("copy: select a guard or object first"); return; }
    if (a.doc.copyItem(a.doc.sel.item, a.doc.clip, err)) a.say("copied (Ctrl+V pastes it under the cursor)");
    else a.say("copy: %s", err.c_str());
}

void pasteAtMouse(App &a, const Vec3 &org, const Vec3 &dir)
{
    std::string err;
    if (!a.doc.clip.valid) { a.say("paste: nothing copied (Ctrl+C on a guard or object)"); return; }
    if (!ensureMission(a)) return;
    PadSpec ps;
    if (!padFromHit(a, pickScene(a.doc, org, dir), ps)) { a.say("paste: point at a pad or the floor"); return; }
    ItemRef ref;
    if (a.doc.pasteItem(a.doc.clip, ps, ref, err)) { selectNewItem(a, ref); a.say("pasted"); }
    else a.say("paste: %s", err.c_str());
}

void duplicateSelected(App &a, const Vec3 &org, const Vec3 &dir)
{
    std::string err;
    if (a.doc.sel.kind != Selection::ITEM) { a.say("duplicate: select a guard or object first"); return; }
    if (!ensureMission(a)) return;
    ItemRef ref;
    if (!a.doc.duplicateItem(a.doc.sel.item, nullptr, ref, err)) { a.say("duplicate: %s", err.c_str()); return; }
    selectNewItem(a, ref);
    if (a.doc.getTransform(a.doc.sel).movable) {
        dragBegin(a, 4, org, dir);      // follows the mouse until the next click, like G
        a.drag.keymove = true;
    }
    a.say("duplicated");
}

void reloadDecorGpu(App &a)
{
    a.scene.setLevel(a.doc.hasLevel ? &a.doc.level : nullptr);   // the owners are unchanged: the thumbnails and the camera stay
    a.seenDecorGen = a.doc.decorGen;
    if (a.doc.hasLevel) a.say("%s", a.doc.level.decorNote.c_str());
}

void reloadLevelGpu(App &a)
{
    clearOwnerThumbs(a);
    a.scene.setLevel(a.doc.hasLevel ? &a.doc.level : nullptr);
    a.seenLevelGen = a.doc.levelGen;
    a.seenDecorGen = a.doc.decorGen;
    if (a.doc.hasLevel) {
        a.cam.frame(a.doc.level.mn, a.doc.level.mx);
        a.cam.zfar = std::max(50000.0f, 4.0f * (a.doc.level.mx[0] - a.doc.level.mn[0] + a.doc.level.mx[2] - a.doc.level.mn[2]));
        snprintf(a.levelDir, sizeof a.levelDir, "%s", a.doc.level.dir.c_str());
        if (missionLevelNumber(a.doc.level.name.c_str()) >= 0) snprintf(a.pickLevel, sizeof a.pickLevel, "%s", a.doc.level.name.c_str());
        // the mod panel's "Add" creates its mission for the loaded level unless told otherwise
        if (missionLevelNumber(a.doc.level.name.c_str()) >= 0) snprintf(a.modNewLevel, sizeof a.modNewLevel, "%s", a.doc.level.name.c_str());
        a.say("level '%s': %zu rooms, %zu pads, %zu props, %d draw runs", a.doc.level.name.c_str(), a.doc.level.rooms.size(),
              a.doc.level.pads.size(), a.doc.level.props.size(), a.scene.runCount());
    }
}

// A solo level from the export cache next to the game; the game exports it in the background when
// it is missing (or reexport). pollLevel() loads it when the game is done.
void requestLevel(App &a, const std::string &lvl, bool reexport)
{
    std::string err;
    a.doc.gameExe = a.gameExe;
    snprintf(a.pickLevel, sizeof a.pickLevel, "%s", lvl.c_str());
    const std::string dir = exportDirFor(a.gameExe, lvl);
    if (!reexport && exportPresent(dir)) {
        if (!a.doc.openLevel(dir, err)) a.say("open level: %s", err.c_str());
        return;
    }
    if (a.job.running) { a.say("an export is already running (%s)", a.job.level.c_str()); return; }
    if (exportStart(a.job, a.gameExe, lvl, err)) a.say("exporting %s: the game loads it, records it and closes", lvl.c_str());
    else a.say("export %s: %s", lvl.c_str(), err.c_str());
}

void startGltfExport(App &a, bool force)
{
    std::string err;
    if (a.gltfJob.running) return;
    if (!force && a.gltfTried) return;
    if (!a.gameExe[0]) return;   // no game: the view keeps level.rec's rooms
    a.gltfTried = true;
    if (exportStartGltf(a.gltfJob, a.gameExe, err)) a.say("exporting the glTF decor of every level: the game reads the ROM and closes");
    else a.say("glTF export: %s", err.c_str());
}

void pollLevel(App &a)
{
    std::string err;
    // the glTF decor export: once, when the open level has none (or an older exporter's)
    if (a.gltfJob.running) {
        const int r = exportPoll(a.gltfJob, err);
        if (r == 1) {
            a.say("glTF export done");
            if (a.doc.hasLevel) { a.doc.gameExe = a.gameExe; a.doc.reloadDecor(); }
        } else if (r < 0) a.say("glTF export: %s", err.c_str());
    } else if (a.doc.hasLevel && a.doc.levelGen != a.gltfCheckedGen) {
        a.gltfCheckedGen = a.doc.levelGen;
        if (a.doc.level.decorNeedsExport && a.gameExe[0] && !a.gltfTried) startGltfExport(a, false);
    }
    if (a.job.running) {
        int r = exportPoll(a.job, err);
        if (r == 1) {
            if (a.reexportTotal > 0) {   // part of "re-export all": reload only the level that is open
                if (a.doc.hasLevel && a.doc.level.name == a.job.level) { if (!a.doc.openLevel(a.job.dir, err)) a.say("open level: %s", err.c_str()); }
                else a.say("re-exported %s", a.job.level.c_str());
            } else if (!a.doc.openLevel(a.job.dir, err)) a.say("open level: %s", err.c_str());
        } else if (r < 0) a.say("export: %s", err.c_str());
        return;
    }
    if (!a.reexportQueue.empty()) {   // the next level of "re-export all"
        const std::string lvl = a.reexportQueue.front();
        a.reexportQueue.pop_front();
        char keep[sizeof a.pickLevel];
        memcpy(keep, a.pickLevel, sizeof keep);
        requestLevel(a, lvl, true);
        memcpy(a.pickLevel, keep, sizeof keep);
        if (!a.job.running) { a.reexportQueue.clear(); a.reexportTotal = 0; }
        return;
    }
    if (a.reexportTotal > 0) { a.say("re-exported %d level(s)", a.reexportTotal); a.reexportTotal = 0; }
    // an opened mission (or a mod's) brings its level
    const std::string need = a.doc.missingLevel();
    if (!need.empty() && need != a.autoTried) {
        a.autoTried = need;
        snprintf(a.pickLevel, sizeof a.pickLevel, "%s", need.c_str());
        requestLevel(a, need, false);
    }
}

void doPlay(App &a)
{
    std::string err;
    a.doc.gameExe = a.gameExe;
    if (a.doc.testRun(a.spawn, a.playCoop, err)) a.say("started%s: %s", a.playCoop ? " (co-op)" : "", a.doc.lastPlay.c_str());
    else a.say("play: %s", err.c_str());
}

void doSave(App &a)
{
    std::string err;
    if (a.doc.saveMission(a.missionPath, err)) {
        snprintf(a.missionPath, sizeof a.missionPath, "%s", a.doc.missionPath.c_str());
        a.say("saved %s", a.doc.missionPath.c_str());
        a.infoCache.clear();
        if (a.thumbDirty && !a.thumbBase.empty()) {   // contrast / brightness are baked into thumb.png on save
            std::string e2;
            if (thumbWriteBaked(a, a.thumbPath, e2)) a.say("thumbnail written: %s", a.thumbPath);
            else a.say("capture: %s", e2.c_str());
        }
    } else a.say("save: %s", err.c_str());
}

// ---- opening and saving missions ----

std::string missionStartDir(const App &a)
{
    std::string dir;
    if (a.doc.hasMission && !a.doc.missionPath.empty()) {
        const size_t s = a.doc.missionPath.find_last_of("/\\");
        if (s != std::string::npos) dir = a.doc.missionPath.substr(0, s);
    }
    if (dir.empty() || !std::filesystem::exists(dir)) {
        dir = a.doc.gameDataDir() + "/missions";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
    }
    return dir;
}

void doSaveAs(App &a)
{
    std::string path, err;
    if (!a.doc.hasMission) { a.say("no mission to save"); return; }
    if (!fileDialogSaveMission(missionStartDir(a), path)) return;
    if (a.doc.saveMission(path, err)) {
        snprintf(a.missionPath, sizeof a.missionPath, "%s", a.doc.missionPath.c_str());
        a.say("saved %s", a.doc.missionPath.c_str());
        a.infoCache.clear();
    } else a.say("save as: %s", err.c_str());
}

// The mod's state changed (opened, switched): the UI's caches and the Mod tab's folder field follow it.
void modOpened(App &a)
{
    Doc &d = a.doc;
    snprintf(a.modDirBuf, sizeof a.modDirBuf, "%s", d.modDir.c_str());
    invalidateThumbs(a);
    a.infoCache.clear();
    a.say("mod '%s' (%d missions)", d.camp.name, d.camp.nmissions);
    if (!d.modNotice.empty()) a.say("%s", d.modNotice.c_str());
    recentNote(a, d.modDir + "/campaign.json");
}

void doOpenPath(App &a, const std::string &path)
{
    std::string err;
    if (a.doc.openMission(path, err)) {
        snprintf(a.missionPath, sizeof a.missionPath, "%s", path.c_str());
        a.say("opened %s", path.c_str());
        const int m = a.doc.openModOfOpenMission(err);   // a mission of a mod opens its mod too: one open, not two
        if (m > 0) modOpened(a);
        else if (m < 0) a.say("mod: %s", err.c_str());
    } else a.say("open mission: %s", err.c_str());
}

// Opens a mod and its first mission; `dir` is the mod folder, or its campaign.json.
void doOpenModPath(App &a, std::string dir)
{
    for (auto &c : dir) if (c == '\\') c = '/';
    const std::string tail = "/campaign.json";
    if (dir.size() > tail.size() && dir.compare(dir.size() - tail.size(), tail.size(), tail) == 0) dir.resize(dir.size() - tail.size());
    std::string err;
    if (a.doc.hasMod && a.doc.modDirty) { a.say("mod: the open mod has unsaved changes (Mod tab > Save mod first)"); return; }
    if (Doc::isPackageFile(dir)) {   // a package: the modal offers to unpack it into a folder X.pmod/, then opens that
        const size_t s = dir.find_last_of('/');
        a.extractPmod = dir;
        snprintf(a.extractDest, sizeof a.extractDest, "%s", s == std::string::npos ? "." : dir.substr(0, s).c_str());
        a.extractErr.clear();
        a.askExtract = true;
        return;
    }
    if (a.doc.openMod(dir, err)) {
        snprintf(a.missionPath, sizeof a.missionPath, "%s", a.doc.missionPath.c_str());
        modOpened(a);
    } else a.say("mod: %s", err.c_str());
}

void doOpenModDialog(App &a)
{
    std::string path;
    std::error_code ec;
    std::string start = a.doc.modsRoot();
    if (!std::filesystem::exists(start, ec)) start.clear();
    if (fileDialogOpenMod(start, path)) guarded(a, [&a, path] { doOpenModPath(a, path); });
}

// Runs `fn` now, or after the answer of the "Unsaved changes" dialog when the open mission has unsaved edits.
void guarded(App &a, std::function<void()> fn)
{
    if (a.doc.hasMission && a.doc.dirty) { a.pending = std::move(fn); a.askUnsaved = true; }
    else fn();
}

void doOpenDialog(App &a)
{
    std::string path;
    if (fileDialogOpenMission(missionStartDir(a), path)) doOpenPath(a, path);
}

// ---- remembered settings: a small text file next to the editor, "key=value" lines ----

static std::string prefGet(const std::string &text, const char *key)
{
    size_t p = 0;
    const std::string k = std::string(key) + "=";
    while (p < text.size()) {
        size_t e = text.find_first_of("\r\n", p);
        if (e == std::string::npos) e = text.size();
        if (text.compare(p, k.size(), k) == 0) return text.substr(p + k.size(), e - p - k.size());
        p = e + 1;
    }
    return "";
}

void loadPrefs(App &a)
{
    std::string text;
    if (a.prefsFile.empty() || !readFile(a.prefsFile, text)) return;
    a.playCoop = prefGet(text, "coop") == "1";
    if (!prefGet(text, "offscreen").empty()) a.prefOffscreen = prefGet(text, "offscreen") != "0";
    if (!prefGet(text, "renderer").empty()) a.prefRenderer = prefGet(text, "renderer") == "vk" ? 1 : 0;
    if (!prefGet(text, "mcp").empty()) a.prefMcp = prefGet(text, "mcp") != "0";
    const int port = atoi(prefGet(text, "mcpport").c_str());
    if (port > 0 && port < 65536) a.prefMcpPort = port;
}

void savePrefs(App &a)
{
    if (a.prefsFile.empty()) return;
    FILE *f = fopen(a.prefsFile.c_str(), "wb");
    if (!f) return;
    fprintf(f, "coop=%d\noffscreen=%d\nrenderer=%s\nmcp=%d\nmcpport=%d\n", a.playCoop ? 1 : 0, a.prefOffscreen ? 1 : 0, a.prefRenderer ? "vk" : "gl", a.prefMcp ? 1 : 0, a.prefMcpPort);
    fclose(f);
}

// ---- recent missions: a small text file next to the editor, one path per line ----

static std::string normSlash(std::string p)
{
    for (char &c : p) if (c == '\\') c = '/';
    return p;
}

// The same file, whatever the slash direction or the case Windows does not care about.
static bool samePath(const std::string &x, const std::string &y)
{
    if (normSlash(x) == normSlash(y)) return true;
    std::error_code ec;
    const bool eq = std::filesystem::equivalent(x, y, ec);
    return !ec && eq;
}

void recentLoad(App &a)
{
    a.recent.clear();
    std::string text;
    if (a.recentFile.empty() || !readFile(a.recentFile, text)) return;
    size_t p = 0;
    while (p < text.size() && a.recent.size() < 8) {
        size_t e = text.find_first_of("\r\n", p);
        if (e == std::string::npos) e = text.size();
        std::string line = text.substr(p, e - p);
        p = e + 1;
        std::error_code ec;
        if (line.empty() || !std::filesystem::exists(line, ec)) continue;
        bool dup = false;
        for (const std::string &r : a.recent) if (samePath(r, line)) dup = true;
        if (!dup) a.recent.push_back(normSlash(line));
    }
}

void recentNote(App &a, const std::string &path)
{
    std::error_code ec;
    if (path.empty() || !std::filesystem::exists(path, ec)) return;
    for (size_t i = 0; i < a.recent.size();) { if (samePath(a.recent[i], path)) a.recent.erase(a.recent.begin() + (long)i); else i++; }
    a.recent.insert(a.recent.begin(), normSlash(path));
    while (a.recent.size() > 8) a.recent.pop_back();
    if (a.recentFile.empty()) return;
    FILE *f = fopen(a.recentFile.c_str(), "wb");
    if (!f) return;
    for (const std::string &r : a.recent) fprintf(f, "%s\n", r.c_str());
    fclose(f);
}

void recentClear(App &a)
{
    a.recent.clear();
    if (a.recentFile.empty()) return;
    FILE *f = fopen(a.recentFile.c_str(), "wb");
    if (f) fclose(f);
}

// The name and level of a mission file, for lists: read once, kept until a mission is saved. The open mission is live.
const App::MissionInfo &missionInfo(App &a, const std::string &path)
{
    static App::MissionInfo live;
    if (a.doc.hasMission && !a.doc.missionPath.empty() && samePath(a.doc.missionPath, path)) {
        live.name = a.doc.mission.name; live.level = a.doc.mission.level; live.ok = true;
        return live;
    }
    auto it = a.infoCache.find(path);
    if (it != a.infoCache.end()) return it->second;
    App::MissionInfo mi;
    Mission m; missionInit(&m);
    char e[200] = "";
    if (missionLoadFile(path.c_str(), &m, e, sizeof e)) { mi.name = m.name; mi.level = m.level; mi.ok = true; }
    missionFree(&m);
    return a.infoCache[path] = mi;
}

// <folder>/mission.json when the file has no readable name: the folder says more than the file
static std::string recentFallback(const std::string &path)
{
    std::string label = path;
    const size_t s1 = label.find_last_of("/\\");
    if (s1 != std::string::npos && s1 > 0) {
        const size_t s0 = label.find_last_of("/\\", s1 - 1);
        label = label.substr(s0 == std::string::npos ? 0 : s0 + 1);
    }
    return shortPath(label, 42);
}

// The mission's name and level; the file path only in the tooltip.
void recentList(App &a, bool menu)
{
    if (a.recent.empty()) { ImGui::TextDisabled("(none)"); return; }
    for (size_t i = 0; i < a.recent.size(); i++) {
        ImGui::PushID((int)i);
        const std::string path = a.recent[i];
        const bool isMod = path.size() >= 13 && path.compare(path.size() - 13, 13, "campaign.json") == 0;
        std::string label;
        if (isMod) {                                       // a mod: its name, opened with its first mission
            auto it = a.infoCache.find(path);
            if (it == a.infoCache.end()) {
                App::MissionInfo mi;
                Campaign c; campaignInit(&c);
                char e[200] = "";
                if (campaignLoadFile(path.c_str(), &c, e, sizeof e)) { mi.name = c.name; mi.ok = true; }
                campaignFree(&c);
                it = a.infoCache.emplace(path, mi).first;
            }
            label = "Mod: " + (it->second.ok && !it->second.name.empty() ? it->second.name : recentFallback(path));
        } else {
            const App::MissionInfo &mi = missionInfo(a, path);
            label = mi.ok ? mi.name + "  (" + mi.level + ")" : recentFallback(path);
        }
        const bool hit = menu ? ImGui::MenuItem(label.c_str()) : ImGui::Selectable(label.c_str());
        if (hit) guarded(a, [&a, path, isMod] { if (isMod) doOpenModPath(a, path); else doOpenPath(a, path); });
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", path.c_str());
        ImGui::PopID();
    }
}

std::string shortPath(const std::string &p, size_t n)
{
    if (p.size() <= n) return p;
    return "..." + p.substr(p.size() - (n - 3));
}

// Renders the 3D view (no panels, no markers: those are ImGui) and returns a centred 4:3 crop of it, scaled to 320x240 RGBA.
// Runs on the main thread, outside an ImGui frame: the next frame redraws everything.
void invalidateThumbs(App &a);

static bool captureView(App &a, std::vector<uint8_t> &out, std::string &err)
{
    int dw, dh, ww, wh;
    editorDrawableSize(&dw, &dh);
    SDL_GetWindowSize(a.win, &ww, &wh);
    const float sx = (float)dw / (float)std::max(ww, 1), sy = (float)dh / (float)std::max(wh, 1);
    const int vx = (int)(a.vx * sx), vy = (int)(a.vy * sy), vw = (int)(a.vw * sx), vh = (int)(a.vh * sy);
    if (vw < 16 || vh < 16) { err = "the 3D view is too small (window minimized?)"; return false; }
    ge_render_begin_frame();
    a.scene.render(a.doc, a.cam, dw, dh, vx, vy, vw, vh, a.showRooms, a.showProps);
    if (a.scene.offscreen()) editorOffscreenPresent(dw, dh);   // the view is in the offscreen frame: bring it to the window's buffer read below
    ge_render_end_frame();
    int cw = std::min(vw, vh * 4 / 3), ch = cw * 3 / 4;
    const int cx = vx + (vw - cw) / 2, cy = vy + (vh - ch) / 2;
    std::vector<uint8_t> src;
    if (!editorGlReadRect(dh, cx, cy, cw, ch, src)) { err = "reading the framebuffer failed"; return false; }
    out.assign((size_t)320 * 240 * 4, 0);
    for (int y = 0; y < 240; y++)
        for (int x = 0; x < 320; x++) {   // box filter over the source pixels under this one
            int x0 = x * cw / 320, x1 = std::max(x0 + 1, (x + 1) * cw / 320);
            int y0 = y * ch / 240, y1 = std::max(y0 + 1, (y + 1) * ch / 240);
            int acc[3] = { 0, 0, 0 }, n = 0;
            for (int yy = y0; yy < y1 && yy < ch; yy++)
                for (int xx = x0; xx < x1 && xx < cw; xx++) {
                    const uint8_t *p = &src[((size_t)yy * cw + xx) * 4];
                    acc[0] += p[0]; acc[1] += p[1]; acc[2] += p[2]; n++;
                }
            uint8_t *o = &out[((size_t)y * 320 + x) * 4];
            for (int k = 0; k < 3; k++) o[k] = (uint8_t)(n ? acc[k] / n : 0);
            o[3] = 255;
        }
    return true;
}

// The capture becomes the session's base image for this thumb.png; the file is that image with the Mission tab's contrast
// and brightness baked in (ui_thumb.cpp).
bool captureThumb(App &a, const std::string &path, std::string &err)
{
    std::vector<uint8_t> px;
    if (!captureView(a, px, err)) return false;
    a.thumbBase = px;
    a.thumbBaseFor = path;
    a.thumbGen++;
    return thumbWriteBaked(a, path, err);
}

void invalidateThumbs(App &a)
{
    for (auto &kv : a.thumbTex) if (kv.second) editorGlDeleteTexture(kv.second);
    a.thumbTex.clear();
}

uint32_t thumbTexture(App &a, const std::string &path)
{
    auto it = a.thumbTex.find(path);
    if (it != a.thumbTex.end()) return it->second;
    uint32_t tex = 0;
    int w, h;
    std::vector<uint8_t> px;
    if (readPng(path.c_str(), w, h, px)) tex = editorGlUploadTexture(w, h, px.data());
    a.thumbTex[path] = tex;
    return tex;
}

// ---- actions behind the menu, the toolbar and the shortcuts ----

void deleteSelected(App &a)
{
    std::string e;
    if (a.doc.sel.kind == Selection::ITEM && !a.doc.removeItem(a.doc.sel.item, e)) a.say("remove: %s", e.c_str());
    else if (a.doc.sel.kind == Selection::ZONE) { if (a.doc.removeZone(a.doc.sel.id, e)) a.doc.sel = Selection(); else a.say("remove: %s", e.c_str()); }
    else if (a.doc.sel.kind == Selection::SCRIPT) { if (a.doc.removeScript(a.doc.sel.num, e)) a.doc.sel = Selection(); else a.say("remove: %s", e.c_str()); }
}

void undoLast(App &a)
{
    std::string e;
    if (!a.doc.undo(e)) a.say("undo: %s", e.c_str());
}

void viewCenterRay(const App &a, Vec3 &org, Vec3 &dir)
{
    a.cam.ray(a.vw * 0.5f, a.vh * 0.5f, a.vw, a.vh, org, dir);
}

// The mission check, cached for the log panel (a check every 20 frames), or now.
void refreshIssues(App &a, bool force)
{
    if (!force && a.frameNo - a.issuesFrame < 20) return;
    a.issuesFrame = a.frameNo;
    a.issues = a.doc.hasMission ? a.doc.checkIssues() : std::vector<Issue>();
}

// Selects what a finding is about and looks at it.
void selectIssue(App &a, const Issue &i)
{
    if (!i.hasSel) return;
    a.doc.sel = i.sel;
    if (i.sel.kind == Selection::SCRIPT && i.line > 0 && !i.script.empty()) openScriptEditor(a, i.script, i.line);
    Vec3 p{};
    bool have = false;
    if (i.sel.kind == Selection::ITEM) {
        for (const Item &it : a.items)
            if (it.added == i.sel.item.added && it.guard == i.sel.item.guard && it.idx == i.sel.item.idx && it.hasPos) { p = it.pos; have = true; }
    } else if (i.sel.kind == Selection::PAD) {
        if (const LPad *lp = a.doc.hasLevel ? a.doc.level.pad(i.sel.num) : nullptr) { p = lp->pos; have = true; }
    } else if (i.sel.kind == Selection::MPAD) {
        for (int k = 0; k < a.doc.mission.npads; k++)
            if (i.sel.id == a.doc.mission.pads[k].id) { p = { a.doc.mission.pads[k].pos[0], a.doc.mission.pads[k].pos[1], a.doc.mission.pads[k].pos[2] }; have = true; }
    }
    if (have) focusOn(a, p);
    a.forceTab = 0;
}

// Mission > Validate: every finding goes to the log (and the log opens).
void doValidate(App &a)
{
    refreshIssues(a, true);
    if (!a.doc.hasMission) { a.say("invalid: no mission is open"); return; }
    if (a.issues.empty()) a.say("mission is valid");
    for (const Issue &i : a.issues) a.say("invalid: %s", i.text.c_str());
    a.showLog = true;
}

void doCapture(App &a)
{
    std::string e;
    if (a.doc.captureFn && a.doc.captureFn(a.thumbPath, e)) a.say("thumbnail written: %s", a.thumbPath);
    else a.say("capture: %s", e.c_str());
}

// On the level loaded, or the picked one (it is exported then). Asks about unsaved edits, then opens the New mission dialog
// (ui_modals.cpp); createNewMission is its Create button.
void doNewMission(App &a)
{
    guarded(a, [&a] {
        snprintf(a.nmName, sizeof a.nmName, "New mission");
        const char *want = a.doc.hasLevel ? a.doc.level.name.c_str() : a.pickLevel;
        a.nmLevel = 0;
        for (int i = 0; missionLevelNameAt(i); i++) if (strcmp(missionLevelNameAt(i), want) == 0) a.nmLevel = i;
        a.nmStart = 0;
        a.nmAddToMod = true;
        a.askNewMission = true;
    });
}

void createNewMission(App &a)
{
    std::string err, folder;
    const std::string lvl = missionLevelNameAt(a.nmLevel) ? missionLevelNameAt(a.nmLevel) : "";
    const bool emptied = a.nmStart == 1, toMod = a.doc.hasMod && a.nmAddToMod;
    a.doc.gameExe = a.gameExe;
    bool ok;
    if (toMod) ok = a.doc.modAddMission(a.nmName, lvl, "", folder, err, emptied);
    else ok = a.doc.newMission(a.nmName, lvl, err, emptied);
    if (!ok) { a.say("new mission: %s", err.c_str()); return; }
    a.infoCache.clear();
    snprintf(a.missionPath, sizeof a.missionPath, "%s", a.doc.missionPath.c_str());
    if (toMod) a.say("new mission '%s' for %s, added to the mod (saved to %s)", a.doc.mission.name, a.doc.mission.level, a.doc.missionPath.c_str());
    else a.say("new mission '%s' for %s (not saved yet: Save writes %s)", a.doc.mission.name, a.doc.mission.level, a.doc.missionPath.c_str());
    if (emptied) {
        if (a.doc.emptyPending) a.say("emptied level: the original guards and pickups are removed as soon as the level has loaded");
        else a.say("emptied level: %d operations remove the original guards, their weapons and the pickups", a.doc.mission.nguards + a.doc.mission.nobjects);
    }
    // the level: loaded now, or exported first (pollLevel brings it in)
    if (!a.doc.hasLevel || a.doc.level.name != a.doc.mission.level) requestLevel(a, a.doc.mission.level, false);
}

// <game dir>/data/exports: the parent of every cached level export.
std::string exportCacheDir(const App &a)
{
    const std::string one = exportDirFor(a.gameExe, "dam");
    if (one.empty()) return "";
    return std::filesystem::path(one).parent_path().string();
}

void startReexportAll(App &a)
{
    if (a.job.running || !a.reexportQueue.empty()) { a.say("an export is already running"); return; }
    a.doc.gameExe = a.gameExe;
    for (int i = 0; missionLevelNameAt(i); i++) {
        const std::string n = missionLevelNameAt(i);
        if (exportPresent(exportDirFor(a.gameExe, n))) a.reexportQueue.push_back(n);
    }
    a.reexportTotal = (int)a.reexportQueue.size();
    startGltfExport(a, true);   // the decor too
    if (!a.reexportTotal) a.say("no cached level exports to redo");
    else a.say("re-exporting %d cached level(s), one after another", a.reexportTotal);
}

} // namespace ed
