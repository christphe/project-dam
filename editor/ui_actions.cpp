// ui_actions.cpp: see ui_app.h for how the editor UI is split.
#include "ui_app.h"
#include <cmath>
#include <filesystem>
#include "launch.h"
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
    if (a.tool == App::T_SELECT || h.kind == Hit::NONE) {
        a.doc.sel = Selection();
        if (h.kind == Hit::ITEM) a.doc.sel.kind = Selection::ITEM, a.doc.sel.item = h.item;
        else if (h.kind == Hit::PAD) a.doc.sel.kind = Selection::PAD, a.doc.sel.num = h.num;
        else if (h.kind == Hit::MPAD) a.doc.sel.kind = Selection::MPAD, a.doc.sel.id = h.id;
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

void reloadLevelGpu(App &a)
{
    a.scene.setLevel(a.doc.hasLevel ? &a.doc.level : nullptr);
    a.seenLevelGen = a.doc.levelGen;
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

void pollLevel(App &a)
{
    std::string err;
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
    } else a.say("save as: %s", err.c_str());
}

void doOpenPath(App &a, const std::string &path)
{
    std::string err;
    if (a.doc.openMission(path, err)) {
        snprintf(a.missionPath, sizeof a.missionPath, "%s", path.c_str());
        a.say("opened %s", path.c_str());
    } else a.say("open mission: %s", err.c_str());
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
    if (!prefGet(text, "mcp").empty()) a.prefMcp = prefGet(text, "mcp") != "0";
    const int port = atoi(prefGet(text, "mcpport").c_str());
    if (port > 0 && port < 65536) a.prefMcpPort = port;
}

void savePrefs(App &a)
{
    if (a.prefsFile.empty()) return;
    FILE *f = fopen(a.prefsFile.c_str(), "wb");
    if (!f) return;
    fprintf(f, "coop=%d\noffscreen=%d\nmcp=%d\nmcpport=%d\n", a.playCoop ? 1 : 0, a.prefOffscreen ? 1 : 0, a.prefMcp ? 1 : 0, a.prefMcpPort);
    fclose(f);
}

// ---- recent missions: a small text file next to the editor, one path per line ----

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
        if (!line.empty() && std::filesystem::exists(line, ec)) a.recent.push_back(line);
    }
}

void recentNote(App &a, const std::string &path)
{
    std::error_code ec;
    if (path.empty() || !std::filesystem::exists(path, ec)) return;
    for (size_t i = 0; i < a.recent.size();) { if (a.recent[i] == path) a.recent.erase(a.recent.begin() + (long)i); else i++; }
    a.recent.insert(a.recent.begin(), path);
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

// <folder>/mission.json: the folder says more than the file
static std::string recentLabel(const std::string &path)
{
    std::string label = path;
    const size_t s1 = label.find_last_of("/\\");
    if (s1 != std::string::npos && s1 > 0) {
        const size_t s0 = label.find_last_of("/\\", s1 - 1);
        label = label.substr(s0 == std::string::npos ? 0 : s0 + 1);
    }
    return shortPath(label, 42);
}

void recentList(App &a, bool menu)
{
    if (a.recent.empty()) { ImGui::TextDisabled("(none)"); return; }
    for (size_t i = 0; i < a.recent.size(); i++) {
        ImGui::PushID((int)i);
        const std::string path = a.recent[i];
        const bool hit = menu ? ImGui::MenuItem(recentLabel(path).c_str()) : ImGui::Selectable(recentLabel(path).c_str());
        if (hit) guarded(a, [&a, path] { doOpenPath(a, path); });
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", path.c_str());
        ImGui::PopID();
    }
}

std::string shortPath(const std::string &p, size_t n)
{
    if (p.size() <= n) return p;
    return "..." + p.substr(p.size() - (n - 3));
}

// Renders the 3D view (no panels, no markers: those are ImGui) and saves a centred 4:3 crop of it,
// scaled to 320x240. Runs on the main thread, outside an ImGui frame: the next frame redraws everything.
void invalidateThumbs(App &a);

bool captureThumb(App &a, const std::string &path, std::string &err)
{
    int dw, dh, ww, wh;
    SDL_GL_GetDrawableSize(a.win, &dw, &dh);
    SDL_GetWindowSize(a.win, &ww, &wh);
    const float sx = (float)dw / (float)std::max(ww, 1), sy = (float)dh / (float)std::max(wh, 1);
    const int vx = (int)(a.vx * sx), vy = (int)(a.vy * sy), vw = (int)(a.vw * sx), vh = (int)(a.vh * sy);
    if (vw < 16 || vh < 16) { err = "the 3D view is too small (window minimized?)"; return false; }
    a.scene.render(a.doc, a.cam, dw, dh, vx, vy, vw, vh, a.showRooms, a.showProps);
    if (a.scene.offscreen()) editorOffscreenPresent(dw, dh);   // the view is in the offscreen frame: bring it to the window's buffer read below
    int cw = std::min(vw, vh * 4 / 3), ch = cw * 3 / 4;
    const int cx = vx + (vw - cw) / 2, cy = vy + (vh - ch) / 2;
    std::vector<uint8_t> src;
    if (!editorGlReadRect(dh, cx, cy, cw, ch, src)) { err = "reading the framebuffer failed"; return false; }
    std::vector<uint8_t> out(320 * 240 * 4);
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
    std::error_code ec;
    std::filesystem::path p(path);
    if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path(), ec);
    if (!writePng(path.c_str(), 320, 240, out.data())) { err = "cannot write " + path; return false; }
    invalidateThumbs(a);
    return true;
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

void doValidate(App &a)
{
    auto v = a.doc.validate();
    if (v.empty()) a.say("mission is valid");
    for (auto &s : v) a.say("invalid: %s", s.c_str());
}

void doCapture(App &a)
{
    std::string e;
    if (a.doc.captureFn && a.doc.captureFn(a.thumbPath, e)) a.say("thumbnail written: %s", a.thumbPath);
    else a.say("capture: %s", e.c_str());
}

// On the level loaded, or the picked one (it is exported then); the name from the Mission tab.
void doNewMission(App &a)
{
    guarded(a, [&a] {
        std::string e2;
        if (a.doc.newMission(a.newName, a.doc.hasLevel ? "" : a.pickLevel, e2)) {
            snprintf(a.missionPath, sizeof a.missionPath, "%s", a.doc.missionPath.c_str());
            a.say("new mission for %s (saved to %s)", a.doc.mission.level, a.doc.missionPath.c_str());
        } else a.say("new mission: %s", e2.c_str());
    });
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
    if (!a.reexportTotal) a.say("no cached level exports to redo");
    else a.say("re-exporting %d cached level(s), one after another", a.reexportTotal);
}

} // namespace ed
