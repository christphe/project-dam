// ge-editor: the GoldenEye co-op mission editor (issue #102).
//
//   ge-editor [--export DIR] [--mission FILE] [--port N | --no-mcp] [--game EXE]
//   ge-editor --mcp-stdio [--export DIR] [--mission FILE]     headless MCP over stdin/stdout
//
// GUI: SDL2 window, Dear ImGui panels around a 3D view of the level export replayed through
// fast3d's OpenGL backend (scene.cpp). Every edit goes through Doc (doc.h), as the MCP tools do.
#define SDL_MAIN_HANDLED
#include <SDL.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl2.h"

#include "doc.h"
#include "filedialog.h"
#include "editor_gl.h"
#include "launch.h"
#include "exportcache.h"
#include "mcp.h"
#include "photo.h"
#include "png.h"
#include "scene.h"
#include "view.h"

extern "C" void editorStubsSetArgs(int argc, char **argv);   // fast3d_stubs.cpp

namespace {

struct Options {
    bool mcpStdio = false, mcpHttp = false, noMcp = false, help = false;
    int port = 8765;
    std::string exportDir, missionFile, game, screenshot, modDir, level;
    int frames = 30;
    bool hasCam = false, hasClick = false, hasDrag = false;
    float drag[4] = { 0, 0, 0, 0 };
    int dragAxis = -1;
    bool noOffscreen = false;   // --no-offscreen: draw straight into the window (see editor_gl.h)
    bool hasMouseDrag = false, mouseClickMode = false, mouseMiddle = false;   // --mouse-middle: the drag uses the middle button (pan)   // --mouse-click: click the handle, move, click to confirm
    float mouseDrag[2] = { 0, 0 };   // --mouse-drag DX,DY: real SDL mouse events on the gizmo (--drag-axis picks the arrow)
    float cam[5] = { 0, 0, 0, 0, 0 };
    float click[2] = { 0, 0 };
    std::string tool;
    struct KeyAid { int frame, len; std::string spec; };
    std::vector<KeyAid> keys;       // --key FRAME[xLEN]:SPEC, e.g. 8:ctrl+c or 10x30:rmb+ctrl (test aid)
    bool hasMouseAt = false, logCam = false;
    float mouseAt[2] = { 0, 0 };    // --mouse-at X,Y: the cursor rests there from frame 4
    float mouseThen[3] = { -1, 0, 0 };   // --mouse-then FRAME,X,Y: from that frame it rests there instead
};

std::string dirOfExe(const char *argv0)
{
    std::string p = argv0 ? argv0 : "";
    size_t s = p.find_last_of("/\\");
    return s == std::string::npos ? std::string("./") : p.substr(0, s + 1);
}

// ---- main-thread jobs (the HTTP transport runs MCP calls here) ----
struct Job {
    std::function<void()> fn;
    std::mutex m;
    std::condition_variable cv;
    bool done = false, abandoned = false;
};
std::mutex gJobsM;
std::deque<std::shared_ptr<Job>> gJobs;

void runOnMain(std::function<void()> fn)
{
    auto j = std::make_shared<Job>();
    j->fn = std::move(fn);
    { std::lock_guard<std::mutex> l(gJobsM); gJobs.push_back(j); }
    std::unique_lock<std::mutex> lk(j->m);
    if (!j->cv.wait_for(lk, std::chrono::seconds(30), [&] { return j->done; })) j->abandoned = true;
}

void drainJobs()
{
    std::deque<std::shared_ptr<Job>> local;
    { std::lock_guard<std::mutex> l(gJobsM); local.swap(gJobs); }
    for (auto &j : local) {
        { std::lock_guard<std::mutex> l(j->m); if (j->abandoned) continue; }
        j->fn();
        { std::lock_guard<std::mutex> l(j->m); j->done = true; }
        j->cv.notify_all();
    }
}

// ---- application state ----
struct App {
    Doc doc;
    Scene scene;
    Camera cam;
    SDL_Window *win = nullptr;
    SDL_GLContext gl = nullptr;

    enum Tool { T_SELECT, T_GUARD, T_OBJECT, T_MOVE } tool = T_SELECT;
    bool showRooms = true, showProps = true, showPads = true, showMPads = true, showPortals = false, showStan = false;
    char levelDir[512] = "", missionPath[512] = "", newName[128] = "New mission";
    char gameExe[512] = "", padText[64] = "", padFilter[32] = "";
    int gModel = -1, gAi = -1, gWeapon = -1;
    int oType = 3, oModel = -1, oItem = -1;
    int editA = 0, editB = 0, editC = 0;
    ItemRef editFor;
    std::string spawn;
    char pickLevel[32] = "bunker1";   // File > level
    ExportJob job;                    // the game exporting a level for us
    std::string autoTried;
    ImVec2 gizC{}, gizTip[3]{};       // last frame's gizmo on screen (the --mouse-drag test aid aims at it)
    bool gizOk = false;            // the mission level last fetched on its own (once per level)
    std::deque<std::string> log;
    int seenLevelGen = 0;
    McpHttp http;
    int mcpPort = 0;
    bool looking = false, lmbDown = false, panning = false;
    // opening / saving: the unsaved-changes guard, recent missions
    std::function<void()> pending;     // what waits for the answer of the "Unsaved changes" dialog
    bool askUnsaved = false, modalOpen = false, wantQuit = false;
    bool askLevelSwitch = false;       // File > level Open while the mission is for another level
    std::string levelSwitchTo;
    std::vector<std::string> recent;   // last opened / saved missions, newest first
    std::string recentFile, lastRecorded, lastTitle;
    std::string prefsFile;             // ge-editor-prefs.txt next to the exe: remembered toggles
    bool playCoop = false;             // Play: two players (GE_COOP=1 GE_PADS=2) or solo
    bool logCam = false;               // --log-cam: print camera moves and wheel-zoom pixels to stderr (test aid)
    float panDist = 0;                 // pan: distance of the grabbed point
    float lmbX = 0, lmbY = 0;
    float vx = 0, vy = 0, vw = 1, vh = 1;     // the 3D view rectangle (window pixels, y down)
    std::vector<Item> items;

    // snapping (toolbar) and the transform gizmo
    bool snapGrid = false, snapFloor = false, snapPads = false;
    float gridStep = 25.0f;
    char thumbPath[512] = "thumb.png";
    float thumbIntensity = 1.0f;   // old-photo look of Capture, 0 = plain
    std::string lastAutoThumb;
    char modDirBuf[512] = "mods/mymod", modNewName[64] = "New mission", modNewLevel[32] = "bunker1";
    std::map<std::string, uint32_t> thumbTex;   // thumb.png path -> GL texture (0: missing)
    std::string envSnap;
    bool sunDrag = false;
    std::string sunSnap;
    struct Drag {
        enum Mode { NONE, MOVE, YAW } mode = NONE;
        int axis = -1;             // 0 x, 1 y, 2 z locked; -1 free (the horizontal plane through the start)
        bool keymove = false;      // started with G, or a click on a handle: follows the mouse until a click
        ImVec2 pressAt{};          // where the handle was pressed
        Vec3 startPos{};
        Vec3 grab{};               // free move: where the mouse ray met the plane at the start
        float s0 = 0;              // axis move: the mouse's position along the axis at the start
        std::string snap0;         // mission before the drag: one undo step, or cancel
        bool changed = false;
        Selection sel;
    } drag;

    SnapOpts snapOpts() const
    {
        SnapOpts s;
        s.grid = snapGrid ? gridStep : 0;
        s.floor = snapFloor; s.pads = snapPads;
        return s;
    }

    void say(const char *fmt, ...)
    {
        char b[512];
        va_list ap; va_start(ap, fmt);
        vsnprintf(b, sizeof b, fmt, ap);
        va_end(ap);
        log.push_back(b);
        if (log.size() > 200) log.pop_front();
        fprintf(stderr, "[editor] %s\n", b);
    }
};

// ---- helpers ----

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

void focusOn(App &a, const Vec3 &p, float dist = 450) { a.cam.lookAtPoint(p, dist); }

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
            if (!a.doc.openLevel(a.job.dir, err)) a.say("open level: %s", err.c_str());
        } else if (r < 0) a.say("export: %s", err.c_str());
        return;
    }
    // an opened mission (or a mod's) brings its level
    const std::string need = a.doc.missingLevel();
    if (!need.empty() && need != a.autoTried) {
        a.autoTried = need;
        snprintf(a.pickLevel, sizeof a.pickLevel, "%s", need.c_str());
        requestLevel(a, need, false);
    }
}

// ---- overlay (drawn with the ImGui background list over the 3D view) ----

struct Overlay {
    ImDrawList *dl;
    Mat4 vp;
    float x, y, w, h;
    bool pt(const Vec3 &p, ImVec2 &o) const
    {
        float s[2];
        if (!projectPoint(vp, p, x, y, w, h, s)) return false;
        o = ImVec2(s[0], s[1]);
        return true;
    }
    void line(const Vec3 &a, const Vec3 &b, ImU32 col, float th = 1.0f) const
    {
        ImVec2 pa, pb;
        if (pt(a, pa) && pt(b, pb)) dl->AddLine(pa, pb, col, th);
    }
    void box(const Vec3 &mn, const Vec3 &mx, ImU32 col, float th = 1.0f) const
    {
        Vec3 c[8];
        for (int i = 0; i < 8; i++) c[i] = { (i & 1) ? mx[0] : mn[0], (i & 2) ? mx[1] : mn[1], (i & 4) ? mx[2] : mn[2] };
        static const int e[12][2] = { { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 }, { 0, 2 }, { 1, 3 }, { 4, 6 }, { 5, 7 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
        for (auto &k : e) line(c[k[0]], c[k[1]], col, th);
    }
    // a box in an item's own frame, turned by the pad's yaw and put at pos
    void boxYaw(const Vec3 &pos, const Vec3 &mn, const Vec3 &mx, float yawDeg, ImU32 col, float th = 1.0f) const
    {
        Vec3 c[8];
        for (int i = 0; i < 8; i++) {
            const Vec3 l = { (i & 1) ? mx[0] : mn[0], (i & 2) ? mx[1] : mn[1], (i & 4) ? mx[2] : mn[2] };
            const Vec3 r = rotYaw(l, yawDeg);
            c[i] = { pos[0] + r[0], pos[1] + r[1], pos[2] + r[2] };
        }
        static const int e[12][2] = { { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 }, { 0, 2 }, { 1, 3 }, { 4, 6 }, { 5, 7 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
        for (auto &k : e) line(c[k[0]], c[k[1]], col, th);
        // the front (+z of the item) is the facing: a line from the middle of the front face outwards
        const Vec3 f0 = rotYaw({ (mn[0] + mx[0]) * 0.5f, (mn[1] + mx[1]) * 0.5f, mx[2] }, yawDeg);
        const Vec3 f1 = rotYaw({ (mn[0] + mx[0]) * 0.5f, (mn[1] + mx[1]) * 0.5f, mx[2] + 40.0f }, yawDeg);
        line({ pos[0] + f0[0], pos[1] + f0[1], pos[2] + f0[2] }, { pos[0] + f1[0], pos[1] + f1[1], pos[2] + f1[2] }, col, th);
    }
    void diamond(const Vec3 &p, float r, ImU32 col, bool fill) const
    {
        ImVec2 s;
        if (!pt(p, s)) return;
        ImVec2 q[4] = { ImVec2(s.x, s.y - r), ImVec2(s.x + r, s.y), ImVec2(s.x, s.y + r), ImVec2(s.x - r, s.y) };
        if (fill) dl->AddConvexPolyFilled(q, 4, col); else dl->AddPolyline(q, 4, col, ImDrawFlags_Closed, 1.5f);
    }
    void text(const Vec3 &p, const char *t, ImU32 col, float dx = 6, float dy = -16) const
    {
        ImVec2 s;
        if (pt(p, s)) dl->AddText(ImVec2(s.x + dx, s.y + dy), col, t);
    }
};

float dist3(const Vec3 &a, const Vec3 &b)
{
    float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

void drawOverlay(App &a, const Overlay &o)
{
    const Doc &d = a.doc;
    const OccupiedPads occ = occupiedPads(a.items);   // pads under a guard / object: drawn as the item
    const ImU32 white = IM_COL32(235, 235, 240, 255), yellow = IM_COL32(255, 220, 40, 255), red = IM_COL32(235, 70, 60, 255),
                orange = IM_COL32(240, 160, 50, 255), green = IM_COL32(80, 220, 110, 255), grey = IM_COL32(130, 130, 140, 150),
                cyan = IM_COL32(70, 200, 230, 160), faint = IM_COL32(90, 200, 120, 90), blue = IM_COL32(120, 170, 255, 255);
    if (d.hasLevel) {
        if (a.showStan)
            for (auto &p : d.level.stan)
                for (size_t i = 0; i < p.v.size(); i++) o.line(p.v[i], p.v[(i + 1) % p.v.size()], faint);
        if (a.showPortals)
            for (auto &p : d.level.portals)
                for (size_t i = 0; i < p.v.size(); i++) o.line(p.v[i], p.v[(i + 1) % p.v.size()], cyan, 1.5f);
        if (d.sel.kind == Selection::ROOM)
            for (auto &r : d.level.rooms)
                if (r.index == d.sel.num) o.box(r.mn, r.mx, yellow, 1.5f);
        if (a.showPads)
            for (auto &p : d.level.pads) {
                bool sel = d.sel.kind == Selection::PAD && d.sel.num == p.num;
                if (!sel && occ.level.count(p.num)) continue;   // a guard's / object's pad: shown as the item
                if (!sel && dist3(p.pos, a.cam.pos) > 12000) continue;
                o.diamond({ p.pos[0], p.pos[1] + 8, p.pos[2] }, sel ? 8.0f : 4.5f, sel ? yellow : blue, true);
                if (sel) {
                    char t[32]; snprintf(t, sizeof t, "pad 0x%x", p.num);
                    o.text(p.pos, t, yellow);
                }
            }
    }
    if (a.showMPads && d.hasMission)
        for (int i = 0; i < d.mission.npads; i++) {
            const MissionPad &p = d.mission.pads[i];
            Vec3 pos = { p.pos[0], p.pos[1], p.pos[2] };
            bool sel = d.sel.kind == Selection::MPAD && d.sel.id == p.id;
            if (!sel && occ.mission.count(p.id)) continue;
            o.diamond({ pos[0], pos[1] + 8, pos[2] }, sel ? 9.0f : 6.0f, sel ? yellow : green, true);
            o.text(pos, p.id, sel ? yellow : green);
        }
    for (const Item &it : a.items) {
        Vec3 mn, mx;
        if (it.assigned || !itemBounds(d, it, mn, mx)) continue;     // a carried weapon is drawn with its guard only
        bool sel = itemIsSel(a, it);
        ImU32 col = it.removed ? grey : it.added ? green : it.moved ? yellow : it.guard ? red : orange;
        if (sel) col = IM_COL32(255, 255, 255, 255);
        Vec3 lmn, lmx;
        itemLocalBox(d, it, lmn, lmx);
        o.boxYaw(it.pos, lmn, lmx, it.yawDeg, col, it.removed && !sel ? 1.0f : sel ? 2.5f : 1.2f);
        if (it.moved && d.hasLevel) {
            const LPad *op = d.level.pad(it.origPad);
            if (op) o.line(op->pos, it.pos, yellow, 1.0f);
        }
        if (sel || dist3(it.pos, a.cam.pos) < 1500) {
            char t[64];
            if (it.added) snprintf(t, sizeof t, "%s +%d", it.guard ? "G" : "O", it.idx);
            else snprintf(t, sizeof t, "%s%s %d", it.guard ? "G" : "O", it.removed ? "x" : "", it.idx);
            o.text({ it.pos[0], mx[1], it.pos[2] }, t, sel ? IM_COL32(255, 255, 255, 255) : col, -10, -14);
        }
    }
}

// ---- panels ----

const ImGuiWindowFlags kPanel = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                                ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;

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

// File > level Open with a mission made for another level: move the mission to the new level (its pads
// and items keep their positions, now read against that level), or cancel. Loading the level under the
// mission without changing it is what used to happen silently, and Play then refused the mission.
void levelSwitchModal(App &a)
{
    if (a.askLevelSwitch) { ImGui::OpenPopup("Mission level"); a.askLevelSwitch = false; }
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.4f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal("Mission level", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    a.modalOpen = true;
    ImGui::Text("The open mission \"%s\" is for %s.", a.doc.mission.name, a.doc.mission.level);
    ImGui::Text("Open %s:", a.levelSwitchTo.c_str());
    ImGui::Separator();
    char move[96];
    snprintf(move, sizeof move, "Move the mission to %s", a.levelSwitchTo.c_str());
    if (ImGui::Button(move)) {
        std::string err;
        if (a.doc.setMissionLevel(a.levelSwitchTo, err)) {
            a.say("the mission is now for %s (not saved yet)", a.levelSwitchTo.c_str());
            requestLevel(a, a.levelSwitchTo, false);
        } else a.say("mission level: %s", err.c_str());
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(100, 0))) {
        snprintf(a.pickLevel, sizeof a.pickLevel, "%s", a.doc.hasLevel ? a.doc.level.name.c_str() : a.doc.mission.level);
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void unsavedModal(App &a)
{
    if (a.askUnsaved) { ImGui::OpenPopup("Unsaved changes"); a.askUnsaved = false; }
    a.modalOpen = false;
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.4f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal("Unsaved changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    a.modalOpen = true;
    ImGui::Text("Unsaved changes in %s.", a.doc.mission.name);
    ImGui::TextDisabled("%s", a.doc.missionPath.c_str());
    ImGui::Separator();
    auto finish = [&](bool run) {
        std::function<void()> fn = std::move(a.pending);
        a.pending = nullptr;
        ImGui::CloseCurrentPopup();
        if (run && fn) fn();
    };
    if (ImGui::Button("Save", ImVec2(100, 0))) {
        doSave(a);
        if (!a.doc.dirty) finish(true);      // a failed save keeps the dialog (and the mission) open
    }
    ImGui::SameLine();
    if (ImGui::Button("Discard", ImVec2(100, 0))) { a.doc.dirty = false; finish(true); }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(100, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) finish(false);
    ImGui::EndPopup();
}

// ---- remembered toggles: a small text file next to the editor, "key=value" lines ----

void loadPrefs(App &a)
{
    std::string text;
    if (a.prefsFile.empty() || !readFile(a.prefsFile, text)) return;
    a.playCoop = text.find("coop=1") != std::string::npos;
}

void savePrefs(App &a)
{
    if (a.prefsFile.empty()) return;
    FILE *f = fopen(a.prefsFile.c_str(), "wb");
    if (!f) return;
    fprintf(f, "coop=%d\n", a.playCoop ? 1 : 0);
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

std::string shortPath(const std::string &p, size_t n)
{
    if (p.size() <= n) return p;
    return "..." + p.substr(p.size() - (n - 3));
}

// Renders the 3D view (no panels, no markers: those are ImGui) and saves a centred 4:3 crop of it,
// scaled to 320x240. Runs on the main thread, outside an ImGui frame: the next frame redraws everything.
void invalidateThumbs(App &a);

bool captureThumb(App &a, const std::string &path, float intensity, std::string &err)
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
    applyOldPhoto(out, 320, 240, intensity);
    std::error_code ec;
    std::filesystem::path p(path);
    if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path(), ec);
    if (!writePng(path.c_str(), 320, 240, out.data())) { err = "cannot write " + path; return false; }
    invalidateThumbs(a);
    return true;
}

void toolbar(App &a, float W, float topH)
{
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(W, topH));
    ImGui::Begin("toolbar", nullptr, kPanel | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar);
    auto toolBtn = [&](const char *label, App::Tool t) {
        bool on = a.tool == t;
        if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::Button(label)) a.tool = t;
        if (on) ImGui::PopStyleColor();
        ImGui::SameLine();
    };
    toolBtn("Select", App::T_SELECT);
    toolBtn("Add guard", App::T_GUARD);
    toolBtn("Add object", App::T_OBJECT);
    toolBtn("Move", App::T_MOVE);
    ImGui::TextUnformatted("|"); ImGui::SameLine();
    if (ImGui::Button("Undo")) { std::string e; if (!a.doc.undo(e)) a.say("undo: %s", e.c_str()); }
    ImGui::SameLine();
    if (ImGui::Button("Delete")) {
        std::string e;
        if (a.doc.sel.kind == Selection::ITEM) { if (!a.doc.removeItem(a.doc.sel.item, e)) a.say("remove: %s", e.c_str()); }
    }
    ImGui::SameLine();
    if (ImGui::Button("Save")) doSave(a);
    ImGui::SameLine();
    if (ImGui::Button("Validate")) {
        auto v = a.doc.validate();
        if (v.empty()) a.say("mission is valid");
        for (auto &s : v) a.say("invalid: %s", s.c_str());
    }
    ImGui::SameLine();
    if (ImGui::Button("Play")) doPlay(a);
    ImGui::SameLine();
    if (ImGui::Checkbox("Co-op", &a.playCoop)) savePrefs(a);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Play with two players (two pads), else solo");
    ImGui::SameLine();
    {   // what Play will use: the level's own start unless a pad was chosen with "Set as spawn"
        char label[96];
        char *end = nullptr;
        const long n = a.spawn.empty() ? -1 : strtol(a.spawn.c_str(), &end, 0);
        if (a.spawn.empty()) snprintf(label, sizeof label, "spawn: level start");
        else if (end && !*end) snprintf(label, sizeof label, "spawn: pad 0x%lx", n);
        else snprintf(label, sizeof label, "spawn: %s", a.spawn.c_str());
        ImGui::TextUnformatted(label);
        if (!a.spawn.empty()) {
            ImGui::SameLine();
            if (ImGui::SmallButton("x##spawn")) a.spawn.clear();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Back to the level's own start");
        }
    }
    ImGui::SameLine();
    ImGui::TextUnformatted("|"); ImGui::SameLine();
    ImGui::Checkbox("Rooms", &a.showRooms); ImGui::SameLine();
    ImGui::Checkbox("Props", &a.showProps); ImGui::SameLine();
    ImGui::Checkbox("Pads", &a.showPads); ImGui::SameLine();
    ImGui::Checkbox("Portals", &a.showPortals); ImGui::SameLine();
    ImGui::Checkbox("Tiles", &a.showStan); ImGui::SameLine();
    ImGui::TextUnformatted("snap"); ImGui::SameLine();
    ImGui::Checkbox("Grid", &a.snapGrid); ImGui::SameLine();
    ImGui::SetNextItemWidth(60);
    ImGui::InputFloat("##gs", &a.gridStep, 0, 0, "%.0f"); ImGui::SameLine();
    if (a.gridStep < 1) a.gridStep = 1;
    ImGui::Checkbox("Floor", &a.snapFloor); ImGui::SameLine();
    ImGui::Checkbox("Pads##snap", &a.snapPads); ImGui::SameLine();
    if (ImGui::Button("Capture")) {
        std::string e;
        if (a.doc.captureFn && a.doc.captureFn(a.thumbPath, a.thumbIntensity, e)) a.say("thumbnail written: %s", a.thumbPath);
        else a.say("capture: %s", e.c_str());
    }
    ImGui::SameLine();
    if (a.mcpPort) ImGui::Text("| MCP :%d", a.mcpPort);
    ImGui::End();
}

// ---- mod (campaign) panel ----

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

void modPanel(App &a)
{
    Doc &d = a.doc;
    std::string err;
    ImGui::SetNextItemWidth(-90);
    ImGui::InputText("##moddir", a.modDirBuf, sizeof a.modDirBuf);
    ImGui::SameLine();
    if (ImGui::Button("Open/new##mod")) guarded(a, [&a] {
        std::string e2;
        Doc &d2 = a.doc;
        if (d2.openMod(a.modDirBuf, e2)) {
            invalidateThumbs(a);
            a.say("mod '%s' (%d missions)", d2.camp.name, d2.camp.nmissions);
            if (!d2.modNotice.empty()) a.say("%s", d2.modNotice.c_str());
        }
        else a.say("mod: %s", e2.c_str());
    });
    if (!d.hasMod) { ImGui::TextDisabled("A mod is a campaign: mods/<mod>/campaign.json + one folder per mission."); return; }
    Campaign &c = d.camp;
    {   // the id keys the mod's save: made once, read-only
        char idbuf[CAMPAIGN_ID_LEN];
        snprintf(idbuf, sizeof idbuf, "%s", d.modIdOk() ? c.id : "(none: made on save)");
        ImGui::SetNextItemWidth(-60);
        ImGui::InputText("id##c", idbuf, sizeof idbuf, ImGuiInputTextFlags_ReadOnly);
    }
    if (ImGui::InputText("name##c", c.name, sizeof c.name)) d.modDirty = true;
    if (ImGui::InputText("author##c", c.author, sizeof c.author)) d.modDirty = true;
    if (ImGui::InputTextMultiline("##cdesc", c.description, sizeof c.description, ImVec2(-1, 48))) d.modDirty = true;
    bool story = c.affects & CAMPAIGN_AFFECTS_STORY, mp = c.affects & CAMPAIGN_AFFECTS_MULTIPLAYER;
    ImGui::TextUnformatted("affects");
    ImGui::SameLine();
    if (ImGui::Checkbox("Story", &story)) { c.affects = (c.affects & ~CAMPAIGN_AFFECTS_STORY) | (story ? CAMPAIGN_AFFECTS_STORY : 0); d.modDirty = true; }
    ImGui::SameLine();
    if (ImGui::Checkbox("Multiplayer", &mp)) { c.affects = (c.affects & ~CAMPAIGN_AFFECTS_MULTIPLAYER) | (mp ? CAMPAIGN_AFFECTS_MULTIPLAYER : 0); d.modDirty = true; }
    int un = c.unlock == CAMPAIGN_UNLOCK_OPEN ? 1 : 0;
    ImGui::SetNextItemWidth(110);
    if (ImGui::Combo("unlock", &un, "ordered\0open\0")) { c.unlock = un ? CAMPAIGN_UNLOCK_OPEN : CAMPAIGN_UNLOCK_ORDERED; d.modDirty = true; }
    if (ImGui::Button("Save mod")) {
        if (d.saveMod(err)) { a.say("saved %s/campaign.json", d.modDir.c_str()); if (!d.modNotice.empty()) a.say("%s", d.modNotice.c_str()); }
        else a.say("save mod: %s", err.c_str());
    }
    ImGui::SameLine();
    if (ImGui::Button("Check mod")) {
        auto v = d.validateMod();
        if (v.empty()) a.say("mod is valid");
        for (auto &s : v) a.say("mod: %s", s.c_str());
    }
    if (d.modDirty) { ImGui::SameLine(); ImGui::TextDisabled("(unsaved)"); }
    ImGui::Separator();
    int act = -1, from = -1, to = -1;
    for (int i = 0; i < c.nmissions; i++) {
        ImGui::PushID(i);
        const std::string thumb = d.modDir + "/" + c.missions[i] + "/thumb.png";
        uint32_t tex = thumbTexture(a, thumb);
        if (tex) ImGui::Image((ImTextureID)(intptr_t)tex, ImVec2(64, 48));
        else { ImGui::Dummy(ImVec2(64, 48)); ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), IM_COL32(90, 90, 100, 255)); }
        ImGui::SameLine();
        ImGui::BeginGroup();
        const bool cur = d.hasMission && d.missionPath == d.modMissionPath(i);
        ImGui::Text("%d. %s%s", i + 1, c.missions[i], cur ? "  <" : "");
        if (ImGui::SmallButton("Open")) act = i;
        ImGui::SameLine();
        if (ImGui::SmallButton("Up") && i > 0) { from = i; to = i - 1; }
        ImGui::SameLine();
        if (ImGui::SmallButton("Dn") && i + 1 < c.nmissions) { from = i; to = i + 1; }
        ImGui::SameLine();
        if (ImGui::SmallButton("X")) { d.modRemoveMission(i, err); ImGui::EndGroup(); ImGui::PopID(); break; }
        ImGui::EndGroup();
        ImGui::PopID();
    }
    if (act >= 0) guarded(a, [&a, act] {
        std::string e2;
        if (a.doc.modOpenMission(act, e2)) { snprintf(a.missionPath, sizeof a.missionPath, "%s", a.doc.missionPath.c_str()); a.say("opened %s", a.doc.missionPath.c_str()); }
        else a.say("open: %s", e2.c_str());
    });
    if (from >= 0) d.modMoveMission(from, to, err);
    ImGui::Separator();
    ImGui::SetNextItemWidth(130);
    ImGui::InputText("##mnm", a.modNewName, sizeof a.modNewName);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    ImGui::InputText("##mlv", a.modNewLevel, sizeof a.modNewLevel);
    ImGui::SameLine();
    if (ImGui::Button("Add")) guarded(a, [&a] {
        std::string folder, e2;
        if (a.doc.modAddMission(a.modNewName, a.modNewLevel, "", folder, e2)) {
            snprintf(a.missionPath, sizeof a.missionPath, "%s", a.doc.missionPath.c_str());
            a.say("added mission '%s'", folder.c_str());
        } else a.say("add mission: %s", e2.c_str());
    });
    ImGui::TextDisabled("Capture (toolbar) writes thumb.png into the open mission's folder.");
}

void leftPanel(App &a, float x, float y, float w, float h)
{
    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::Begin("Hierarchy", nullptr, kPanel);
    std::string err;
    if (ImGui::CollapsingHeader("File", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (a.doc.hasMission) {
            ImGui::TextColored(ImVec4(0.95f, 0.95f, 0.6f, 1), "%s%s", a.doc.mission.name, a.doc.dirty ? " *" : "");
            // the mission's own level, in red when the loaded level is another one (Play refuses it then)
            const bool other = a.doc.hasLevel && a.doc.level.name != a.doc.mission.level;
            ImGui::SameLine();
            if (other) ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1), "(%s, loaded: %s)", a.doc.mission.level, a.doc.level.name.c_str());
            else ImGui::TextDisabled("(%s)", a.doc.mission.level);
            ImGui::TextDisabled("%s", shortPath(a.doc.missionPath, 33).c_str());
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", a.doc.missionPath.c_str());
        } else ImGui::TextDisabled("no mission open");
        ImGui::Separator();
        // the level: a solo level, exported by the game the first time and then kept next to it
        ImGui::SetNextItemWidth(-160);
        if (ImGui::BeginCombo("##lvl", a.pickLevel)) {
            for (int i = 0; missionLevelNameAt(i); i++) {
                const char *n = missionLevelNameAt(i);
                if (ImGui::Selectable(n, strcmp(n, a.pickLevel) == 0)) snprintf(a.pickLevel, sizeof a.pickLevel, "%s", n);
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(a.job.running);
        if (ImGui::Button("Open##lvl")) {
            // a mission made for another level: ask before loading this one under it
            if (a.doc.hasMission && a.doc.mission.level[0] && strcmp(a.doc.mission.level, a.pickLevel) != 0) {
                a.levelSwitchTo = a.pickLevel;
                a.askLevelSwitch = true;
            } else requestLevel(a, a.pickLevel, false);
        }
        ImGui::SameLine();
        if (ImGui::Button("Re-export")) requestLevel(a, a.pickLevel, true);
        ImGui::EndDisabled();
        if (a.job.running) ImGui::TextColored(ImVec4(1.0f, 0.82f, 0.48f, 1.0f), "exporting %s...", a.job.level.c_str());
        else if (!exportPresent(exportDirFor(a.gameExe, a.pickLevel))) ImGui::TextDisabled("not exported yet: Open runs the game once");
        if (ImGui::TreeNode("Export folder")) {
            ImGui::SetNextItemWidth(-60);
            ImGui::InputText("##ldir", a.levelDir, sizeof a.levelDir);
            ImGui::SameLine();
            if (ImGui::Button("Load")) {
                if (a.doc.openLevel(a.levelDir, err)) reloadLevelGpu(a); else a.say("open level: %s", err.c_str());
            }
            ImGui::TreePop();
        }
        ImGui::TextUnformatted("mission file");
        ImGui::SetNextItemWidth(-60);
        ImGui::InputText("##mpath", a.missionPath, sizeof a.missionPath);
        ImGui::SameLine();
        if (ImGui::Button("Open")) guarded(a, [&a] { doOpenPath(a, a.missionPath); });
        if (fileDialogAvailable()) {
            if (ImGui::Button("Open...")) guarded(a, [&a] { doOpenDialog(a); });
            ImGui::SameLine();
            ImGui::BeginDisabled(!a.doc.hasMission);
            if (ImGui::Button("Save as...")) doSaveAs(a);
            ImGui::EndDisabled();
        }
        if (!a.recent.empty()) {
            ImGui::TextDisabled("recent");
            for (size_t i = 0; i < a.recent.size(); i++) {
                ImGui::PushID((int)i);
                const std::string path = a.recent[i];
                std::string label = path;
                const size_t s1 = label.find_last_of("/\\");
                if (s1 != std::string::npos) {   // <folder>/mission.json: the folder says more than the file
                    const size_t s0 = label.find_last_of("/\\", s1 - 1);
                    label = label.substr(s0 == std::string::npos ? 0 : s0 + 1);
                }
                if (ImGui::Selectable(shortPath(label, 42).c_str())) guarded(a, [&a, path] { doOpenPath(a, path); });
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", path.c_str());
                ImGui::PopID();
            }
        }
        ImGui::SetNextItemWidth(-110);
        ImGui::InputText("##nname", a.newName, sizeof a.newName);
        ImGui::SameLine();
        if (ImGui::Button("New mission")) {   // on the level above (loaded, or picked: it is exported then)
            guarded(a, [&a] {
                std::string e2;
                if (a.doc.newMission(a.newName, a.doc.hasLevel ? "" : a.pickLevel, e2)) {
                    snprintf(a.missionPath, sizeof a.missionPath, "%s", a.doc.missionPath.c_str());
                    a.say("new mission for %s (saved to %s)", a.doc.mission.level, a.doc.missionPath.c_str());
                } else a.say("new mission: %s", e2.c_str());
            });
        }
        ImGui::SetNextItemWidth(-60);
        ImGui::InputText("##game", a.gameExe, sizeof a.gameExe);
        ImGui::SameLine();
        ImGui::TextUnformatted("game");
        ImGui::SetNextItemWidth(-60);
        ImGui::InputText("##thumb", a.thumbPath, sizeof a.thumbPath);
        ImGui::SameLine();
        ImGui::TextUnformatted("thumb");
        ImGui::SetNextItemWidth(-90);
        ImGui::SliderFloat("old photo", &a.thumbIntensity, 0.0f, 1.5f, "%.2f");
    }
    if (ImGui::CollapsingHeader("Mod (campaign)", ImGuiTreeNodeFlags_DefaultOpen)) modPanel(a);
    if (a.doc.hasLevel && ImGui::CollapsingHeader("Rooms")) {
        for (auto &r : a.doc.level.rooms) {
            char l[48]; snprintf(l, sizeof l, "room %d", r.index);
            bool sel = a.doc.sel.kind == Selection::ROOM && a.doc.sel.num == r.index;
            if (ImGui::Selectable(l, sel)) {
                a.doc.sel = Selection(); a.doc.sel.kind = Selection::ROOM; a.doc.sel.num = r.index;
                if (ImGui::IsMouseDoubleClicked(0))
                    focusOn(a, { (r.mn[0] + r.mx[0]) / 2, (r.mn[1] + r.mx[1]) / 2, (r.mn[2] + r.mx[2]) / 2 }, 900);
            }
        }
    }
    if (a.doc.hasLevel && ImGui::CollapsingHeader("Pads")) {
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##pf", "filter (hex)", a.padFilter, sizeof a.padFilter);
        ImGui::BeginChild("padlist", ImVec2(0, 200), ImGuiChildFlags_Borders);
        std::vector<const LPad *> shown;
        for (auto &p : a.doc.level.pads) {
            char l[32]; snprintf(l, sizeof l, "%x", p.num);
            if (a.padFilter[0] && !strstr(l, a.padFilter)) continue;
            shown.push_back(&p);
        }
        ImGuiListClipper clip;
        clip.Begin((int)shown.size());
        while (clip.Step())
            for (int i = clip.DisplayStart; i < clip.DisplayEnd; i++) {
                const LPad &p = *shown[(size_t)i];
                char l[48]; snprintf(l, sizeof l, "0x%x  room %d", p.num, p.room);
                bool sel = a.doc.sel.kind == Selection::PAD && a.doc.sel.num == p.num;
                if (ImGui::Selectable(l, sel)) {
                    a.doc.sel = Selection(); a.doc.sel.kind = Selection::PAD; a.doc.sel.num = p.num;
                    if (ImGui::IsMouseDoubleClicked(0)) focusOn(a, p.pos);
                }
            }
        ImGui::EndChild();
    }
    if (a.doc.hasMission && a.doc.mission.npads > 0 && ImGui::CollapsingHeader("Mission pads", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (int i = 0; i < a.doc.mission.npads; i++) {
            const MissionPad &p = a.doc.mission.pads[i];
            bool sel = a.doc.sel.kind == Selection::MPAD && a.doc.sel.id == p.id;
            if (ImGui::Selectable(p.id, sel)) {
                a.doc.sel = Selection(); a.doc.sel.kind = Selection::MPAD; a.doc.sel.id = p.id;
                if (ImGui::IsMouseDoubleClicked(0)) focusOn(a, { p.pos[0], p.pos[1], p.pos[2] });
            }
        }
    }
    for (int pass = 0; pass < 2; pass++) {
        const bool guards = pass == 0;
        if (!a.doc.hasLevel && !a.doc.hasMission) break;
        if (!ImGui::CollapsingHeader(guards ? "Guards" : "Objects", guards ? ImGuiTreeNodeFlags_DefaultOpen : 0)) continue;
        ImGui::PushID(pass);
        ImGui::BeginChild("items", ImVec2(0, 220), ImGuiChildFlags_Borders);
        std::vector<const Item *> shown;
        for (const Item &it : a.items) if (it.guard == guards && !it.assigned) shown.push_back(&it);
        ImGuiListClipper clip;
        clip.Begin((int)shown.size());
        while (clip.Step())
            for (int i = clip.DisplayStart; i < clip.DisplayEnd; i++) {
                const Item &it = *shown[(size_t)i];
                char l[96];
                const char *flag = it.added ? " +added" : it.removed ? " -removed" : it.moved ? " ~moved" : "";
                if (it.added) snprintf(l, sizeof l, "%s +%d  @%s%s", guards ? "guard" : "object", it.idx, padLabel(it.pad).c_str(), flag);
                else snprintf(l, sizeof l, "%s %d  @%s%s", guards ? "guard" : "object", it.idx, padLabel(it.pad).c_str(), flag);
                ImGui::PushID(i);
                if (ImGui::Selectable(l, itemIsSel(a, it))) {
                    selectItem(a, it);
                    if (ImGui::IsMouseDoubleClicked(0) && it.hasPos) focusOn(a, it.pos);
                }
                ImGui::PopID();
            }
        ImGui::EndChild();
        ImGui::PopID();
    }
    ImGui::End();
}

// Numeric position and yaw of the selection. Enter applies (one undo step), snapping is not applied
// to typed numbers.
void transformFields(App &a)
{
    TransformInfo t = a.doc.getTransform(a.doc.sel);
    if (!t.valid) return;
    ImGui::Separator();
    if (!t.movable) {
        ImGui::TextDisabled("Original pads are level data: not movable.");
        return;
    }
    float p[3] = { t.pos[0], t.pos[1], t.pos[2] };
    float yaw = t.yawDeg;
    std::string err;
    if (ImGui::InputFloat3("pos", p, "%.1f", ImGuiInputTextFlags_EnterReturnsTrue)) {
        Vec3 np = { p[0], p[1], p[2] };
        if (!a.doc.setTransform(a.doc.sel, &np, nullptr, true, err)) a.say("move: %s", err.c_str());
    }
    if (ImGui::InputFloat("yaw", &yaw, 0, 0, "%.1f deg", ImGuiInputTextFlags_EnterReturnsTrue)) {
        if (!a.doc.setTransform(a.doc.sel, nullptr, &yaw, true, err)) a.say("turn: %s", err.c_str());
    }
    ImGui::TextWrapped("Drag the gizmo in the view (arrows = axis, square = plane, ring = yaw), or G to grab; X/Y/Z lock an axis while moving.");
}

// ---- environment panel (save only: no sky or sea is drawn by the editor yet) ----

void envPanel(App &a)
{
    if (!a.doc.hasMission) return;
    if (!ImGui::CollapsingHeader("Environment")) return;
    ImGui::TextWrapped("Overrides of the level's own sky and sea. The editor draws no sky or sea yet: preview it in the game (Play).");
    Doc &d = a.doc;
    MissionEnv e = d.mission.env;
    // sliders: live while dragged, one undo step per drag
    auto track = [&](bool changed) {
        if (ImGui::IsItemActivated()) a.envSnap = d.snapshot();
        if (changed) d.setEnv(e, false);
        if (ImGui::IsItemDeactivatedAfterEdit()) d.commit(a.envSnap);
    };
    // an override checkbox: ticking it sets the defaults, unticking returns to the level's own
    auto over = [&](const char *label, int &flag) {
        bool b = flag != 0;
        if (ImGui::Checkbox(label, &b)) { flag = b; return true; }
        return false;
    };
    int mood = (int)e.mood + 1;
    ImGui::SetNextItemWidth(130);
    if (ImGui::Combo("mood", &mood, "level's own\0day\0dusk\0night\0")) { e.mood = (MissionMood)(mood - 1); d.setEnv(e, true); }

    if (over("sun##ov", e.has_sun)) {
        if (e.has_sun) { e.sun_azimuth = 0; e.sun_elevation = 30; e.sun_color[0] = e.sun_color[1] = e.sun_color[2] = 1; e.sun_glow = 0.4f; }
        d.setEnv(e, true);
    }
    if (e.has_sun) {
        track(ImGui::SliderFloat("azimuth", &e.sun_azimuth, 0, 360, "%.0f deg"));
        track(ImGui::SliderFloat("elevation", &e.sun_elevation, -10, 90, "%.0f deg"));
        track(ImGui::ColorEdit3("sun colour", e.sun_color));
        track(ImGui::SliderFloat("glow", &e.sun_glow, 0, 2));
        ImGui::TextDisabled("Drag the sun marker in the view too. Azimuth 0 = +z.");
    }
    if (over("sky colour##ov", e.has_sky)) { if (e.has_sky) { e.sky[0] = 0.55f; e.sky[1] = 0.62f; e.sky[2] = 0.7f; } d.setEnv(e, true); }
    if (e.has_sky) track(ImGui::ColorEdit3("sky", e.sky));
    if (over("clouds colour##ov", e.has_clouds)) { if (e.has_clouds) { e.clouds[0] = 0.9f; e.clouds[1] = 0.9f; e.clouds[2] = 0.95f; } d.setEnv(e, true); }
    if (e.has_clouds) track(ImGui::ColorEdit3("clouds", e.clouds));
    if (over("cloud speed##ov", e.has_cloud_speed)) { if (e.has_cloud_speed) e.cloud_speed = 1; d.setEnv(e, true); }
    if (e.has_cloud_speed) track(ImGui::DragFloat("speed##cl", &e.cloud_speed, 0.05f, 0, 20));

    if (over("sea##ov", e.has_sea)) { if (e.has_sea) e.sea_enabled = 1; d.setEnv(e, true); }
    if (e.has_sea) {
        bool en = e.sea_enabled != 0;
        if (ImGui::Checkbox("enabled##sea", &en)) { e.sea_enabled = en; d.setEnv(e, true); }
        if (over("height##ov", e.has_sea_height)) { if (e.has_sea_height) e.sea_height = 0; d.setEnv(e, true); }
        if (e.has_sea_height) track(ImGui::DragFloat("##sh", &e.sea_height, 1.0f));
        if (over("colour##ovs", e.has_sea_color)) { if (e.has_sea_color) { e.sea_color[0] = 0.1f; e.sea_color[1] = 0.3f; e.sea_color[2] = 0.35f; } d.setEnv(e, true); }
        if (e.has_sea_color) track(ImGui::ColorEdit3("##sc", e.sea_color));
        if (over("swell##ov", e.has_swell)) { if (e.has_swell) e.swell = 1; d.setEnv(e, true); }
        if (e.has_swell) track(ImGui::DragFloat("##sw", &e.swell, 0.05f, 0, 10));
        if (over("chop##ov", e.has_chop)) { if (e.has_chop) e.chop = 0.5f; d.setEnv(e, true); }
        if (e.has_chop) track(ImGui::DragFloat("##ch", &e.chop, 0.05f, 0, 10));
        if (over("wave speed##ov", e.has_speed)) { if (e.has_speed) e.speed = 1; d.setEnv(e, true); }
        if (e.has_speed) track(ImGui::DragFloat("##ws", &e.speed, 0.05f, 0, 10));
    }
}

// All the record fields of the selected guard / object (mission_fields.h). Editable for an item the mission
// added (grey = still the template's value; editing it makes it the item's own); read-only for original props:
// copy one (Ctrl+C, Ctrl+V / Ctrl+D) to change it.
void fieldsPanel(App &a, const Item &it)
{
    const MissionField *f;
    const int type = it.guard ? MISSION_REC_GUARD : it.type;
    const int n = missionRecFields(type, &f);
    if (n <= 0) return;
    ImGui::Separator();
    ImGui::TextWrapped("%s", it.added ? "Fields (grey: the template's value, edit to set)" : "Fields (original, read-only: copy to edit)");
    if (it.guard) ImGui::TextWrapped("body = model (65535 = random); head -1 = random");
    const char *group = missionRecGroup(type);
    bool inGroup = false;
    for (int i = 0; i < n; i++) {
        if (f[i].grouped && !inGroup && group) { ImGui::TextColored(ImVec4(0.7f, 0.8f, 1, 1), "%s", group); inGroup = true; }
        const bool have = (it.f.set >> i) & 1;
        double v = have ? it.f.v[i] : 0;
        const bool own = !it.added || ((it.given >> i) & 1);
        ImGui::PushID(i);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(f[i].name);
        ImGui::SameLine(150);
        ImGui::SetNextItemWidth(120);
        const char *fmt = (f[i].kind == MF_FLOAT || f[i].kind == MF_FIXED) ? "%.10g" : "%.0f";
        if (!it.added) {
            ImGui::TextDisabled((f[i].kind == MF_FLOAT || f[i].kind == MF_FIXED) ? "%.10g" : "%.0f", v);
        } else {
            if (!own) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1));
            ImGui::InputScalar("##v", ImGuiDataType_Double, &v, nullptr, nullptr, fmt);
            if (!own) ImGui::PopStyleColor();
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                std::string err;
                if (!a.doc.setItemField(a.doc.sel.item, i, v, err)) a.say("field: %s", err.c_str());
            }
        }
        ImGui::PopID();
    }
}

void rightPanel(App &a, float x, float y, float w, float h)
{
    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::Begin("Properties", nullptr, kPanel);
    Selection &sel = a.doc.sel;
    std::string err;
    if (ImGui::CollapsingHeader("New items", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextUnformatted("Guard (tool: Add guard)");
        ImGui::InputInt("model##g", &a.gModel); ImGui::InputInt("ai list##g", &a.gAi); ImGui::InputInt("weapon##g", &a.gWeapon);
        ImGui::TextUnformatted("Object (tool: Add object)");
        ImGui::InputInt("type##o", &a.oType); ImGui::InputInt("model##o", &a.oModel); ImGui::InputInt("item##o", &a.oItem);
        ImGui::TextWrapped("-1 = the level default. Click a pad or the floor in the view.");
    }
    envPanel(a);
    ImGui::Separator();
    switch (sel.kind) {
    case Selection::NONE: ImGui::TextDisabled("Nothing selected."); break;
    case Selection::ROOM: {
        ImGui::Text("Room %d", sel.num);
        for (auto &r : a.doc.level.rooms)
            if (r.index == sel.num) {
                ImGui::Text("min %.0f %.0f %.0f", r.mn[0], r.mn[1], r.mn[2]);
                ImGui::Text("max %.0f %.0f %.0f", r.mx[0], r.mx[1], r.mx[2]);
            }
        break;
    }
    case Selection::PAD: {
        const LPad *p = a.doc.level.pad(sel.num);
        if (!p) break;
        ImGui::Text("Pad 0x%x (original)", p->num);
        ImGui::Text("pos  %.0f %.0f %.0f", p->pos[0], p->pos[1], p->pos[2]);
        ImGui::Text("look %.2f %.2f %.2f", p->look[0], p->look[1], p->look[2]);
        ImGui::Text("room %d", p->room);
        if (ImGui::Button("Set as spawn")) a.spawn = std::to_string(p->num);
        if (a.doc.hasMission) {
            PadSpec ps; ps.num = p->num;
            ItemRef ref;
            if (ImGui::Button("Add guard here")) a.doc.addGuard(ps, a.gModel, a.gAi, a.gWeapon, ref, err), sel.kind = Selection::ITEM, sel.item = ref;
            if (ImGui::Button("Add object here")) a.doc.addObject(ps, a.oType, a.oModel, a.oItem, ref, err), sel.kind = Selection::ITEM, sel.item = ref;
        }
        break;
    }
    case Selection::MPAD: {
        for (int i = 0; i < a.doc.mission.npads; i++) {
            const MissionPad &p = a.doc.mission.pads[i];
            if (sel.id != p.id) continue;
            ImGui::Text("Mission pad %s", p.id);
            ImGui::Text("pos  %.0f %.0f %.0f", p.pos[0], p.pos[1], p.pos[2]);
            ImGui::Text("room %d", p.room);
            if (ImGui::Button("Set as spawn")) a.spawn = p.id;
        }
        transformFields(a);
        break;
    }
    case Selection::ITEM: {
        const Item *it = selectedItem(a);
        if (!it) { ImGui::TextDisabled("(item no longer exists)"); break; }
        if (it->added) ImGui::Text("%s +%d  (added by the mission)", it->guard ? "Guard" : "Object", it->idx);
        else ImGui::Text("%s prop %d  (original)", it->guard ? "Guard" : "Object", it->idx);
        if (it->removed) ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1), "removed by the mission");
        if (it->moved) ImGui::TextColored(ImVec4(1, 0.85f, 0.2f, 1), "moved (was pad 0x%x)", it->origPad);
        ImGui::Text("pad   %s", padLabel(it->pad).c_str());
        if (it->hasPos) ImGui::Text("pos   %.0f %.0f %.0f", it->pos[0], it->pos[1], it->pos[2]);
        ImGui::Text("type  %d", it->type);
        ImGui::Text("model %d", it->model);
        if (it->guard) {
            ImGui::Text("ai    0x%x", it->ai);
            if (it->weapons.empty()) ImGui::TextDisabled("weapons: none");
            for (size_t k = 0; k < it->weapons.size(); k++) ImGui::Text("weapon %zu: item %d  model %d", k + 1, it->weapons[k].item, it->weapons[k].model);
        }
        else ImGui::Text("item  %d", it->item);
        if (it->owner >= 0) ImGui::Text("recorded model: owner %d", it->owner); else ImGui::TextDisabled("no recorded model (marker)");
        ImGui::Separator();
        if (it->added) {
            if (!(a.editFor == sel.item)) {
                a.editFor = sel.item;
                a.editA = it->guard ? it->ai : it->type; a.editB = it->guard ? (it->weapons.empty() ? -1 : it->weapons[0].item) : it->item; a.editC = it->model;
            }
            ImGui::InputInt("model", &a.editC);
            ImGui::InputInt(it->guard ? "ai list" : "type", &a.editA);
            ImGui::InputInt(it->guard ? "weapon 1" : "item", &a.editB);
            if (ImGui::Button("Apply")) {
                if (!a.doc.setAddedFields(sel.item, a.editC, a.editA, a.editB, err)) a.say("edit: %s", err.c_str());
            }
            ImGui::SameLine();
        }
        ImGui::SetNextItemWidth(110);
        ImGui::InputTextWithHint("##pt", "pad 0x.. or id", a.padText, sizeof a.padText);
        ImGui::SameLine();
        if (ImGui::Button("Move to pad")) {
            PadSpec ps;
            char *end = nullptr; long v = strtol(a.padText, &end, 0);
            if (end && !*end && end != a.padText) { ps.isNew = false; ps.num = (int)v; } else { ps.isNew = true; ps.id = a.padText; }
            if (!a.doc.moveItem(sel.item, ps, err)) a.say("move: %s", err.c_str());
        }
        if (ImGui::Button("Remove")) { if (!a.doc.removeItem(sel.item, err)) a.say("remove: %s", err.c_str()); }
        ImGui::TextWrapped("Tool Move: click a pad or the floor to move it there.");
        transformFields(a);
        fieldsPanel(a, *it);
        break;
    }
    }
    ImGui::End();
}

void bottomPanel(App &a, float x, float y, float w, float h)
{
    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::Begin("Log", nullptr, kPanel);
    auto issues = a.doc.validate();
    if (a.doc.hasMission) {
        if (issues.empty()) ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1), "mission valid");
        for (auto &s : issues) ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "! %s", s.c_str());
    }
    ImGui::Separator();
    for (auto it = a.log.rbegin(); it != a.log.rend(); ++it) ImGui::TextUnformatted(it->c_str());
    ImGui::End();
}

// ---- transform gizmo ----

bool axisParam(const Vec3 &p0, int axis, const Vec3 &o, const Vec3 &d, float &s)
{
    // closest point of the axis line p0 + s * e_axis to the mouse ray o + t * d
    const Vec3 w = { p0[0] - o[0], p0[1] - o[1], p0[2] - o[2] };
    const float b = d[axis], dw = w[axis], ew = d[0] * w[0] + d[1] * w[1] + d[2] * w[2];
    const float den = 1 - b * b;
    if (fabsf(den) < 1e-4f) return false;
    s = (b * ew - dw) / den;
    return true;
}

bool planeHitY(float y, const Vec3 &o, const Vec3 &d, Vec3 &out)
{
    if (fabsf(d[1]) < 1e-5f) return false;
    float t = (y - o[1]) / d[1];
    if (t < 0) return false;
    out = { o[0] + d[0] * t, y, o[2] + d[2] * t };
    return true;
}

struct GizmoGeo {
    bool ok = false;
    ImVec2 c, tip[3], yaw;
    float len = 0;
};

GizmoGeo gizmoGeo(const App &a, const Overlay &ov, const TransformInfo &t)
{
    GizmoGeo g;
    g.len = std::max(dist3(t.pos, a.cam.pos) * 0.11f, 10.0f);
    if (!ov.pt(t.pos, g.c)) return g;
    for (int i = 0; i < 3; i++) {
        Vec3 p = t.pos; p[i] += g.len;
        if (!ov.pt(p, g.tip[i])) return g;
    }
    const float yr = t.yawDeg * 0.017453292f;
    Vec3 yp = { t.pos[0] + sinf(yr) * g.len * 1.5f, t.pos[1], t.pos[2] + cosf(yr) * g.len * 1.5f };
    if (!ov.pt(yp, g.yaw)) return g;
    g.ok = true;
    return g;
}

float segDist(ImVec2 p, ImVec2 a, ImVec2 b)
{
    float vx = b.x - a.x, vy = b.y - a.y, wx = p.x - a.x, wy = p.y - a.y;
    float l2 = vx * vx + vy * vy, t = l2 > 0 ? (wx * vx + wy * vy) / l2 : 0;
    t = std::max(0.0f, std::min(1.0f, t));
    float dx = a.x + t * vx - p.x, dy = a.y + t * vy - p.y;
    return sqrtf(dx * dx + dy * dy);
}

// 0 nothing, 1..3 an axis arrow, 4 the centre (plane move), 5 the yaw handle
int gizmoHit(const GizmoGeo &g, ImVec2 m)
{
    auto d2 = [&](ImVec2 q) { return sqrtf((q.x - m.x) * (q.x - m.x) + (q.y - m.y) * (q.y - m.y)); };
    if (d2(g.yaw) < 10) return 5;
    if (d2(g.c) < 9) return 4;
    int best = 0; float bd = 7;
    for (int i = 0; i < 3; i++) {
        float d = segDist(m, g.c, g.tip[i]);
        if (d < bd) { bd = d; best = i + 1; }
    }
    return best;
}

std::string ownPadId(const App &a)
{
    if (a.drag.sel.kind == Selection::MPAD) return a.drag.sel.id;
    for (const Item &it : a.items)
        if (it.added == a.drag.sel.item.added && it.guard == a.drag.sel.item.guard && it.idx == a.drag.sel.item.idx && it.pad.is_new)
            return it.pad.id;
    return "";
}

void dragRebase(App &a, const Vec3 &org, const Vec3 &dir)
{
    TransformInfo t = a.doc.getTransform(a.drag.sel);
    a.drag.startPos = t.pos;
    a.drag.grab = t.pos;
    if (a.drag.axis >= 0) { if (!axisParam(t.pos, a.drag.axis, org, dir, a.drag.s0)) a.drag.s0 = 0; }
    else { Vec3 h; if (planeHitY(t.pos[1], org, dir, h)) a.drag.grab = h; }
}

void dragBegin(App &a, int hit, const Vec3 &org, const Vec3 &dir)
{
    if (!ensureMission(a)) return;
    TransformInfo t = a.doc.getTransform(a.doc.sel);
    a.drag = App::Drag();
    a.drag.sel = a.doc.sel;
    a.drag.startPos = t.pos;
    a.drag.snap0 = a.doc.snapshot();
    if (hit == 5) { a.drag.mode = App::Drag::YAW; return; }
    a.drag.mode = App::Drag::MOVE;
    a.drag.axis = hit >= 1 && hit <= 3 ? hit - 1 : -1;
    dragRebase(a, org, dir);
}

void dragEnd(App &a, bool confirm)
{
    std::string e;
    if (a.drag.changed) {
        if (confirm) a.doc.commit(a.drag.snap0);
        else a.doc.restoreSnapshot(a.drag.snap0, e);
    }
    a.drag.mode = App::Drag::NONE;
    a.drag.keymove = false;
}

void dragUpdate(App &a, const Vec3 &org, const Vec3 &dir)
{
    std::string err;
    App::Drag &d = a.drag;
    if (d.mode == App::Drag::MOVE) {
        Vec3 np = d.startPos;
        if (d.axis >= 0) {
            float s;
            if (!axisParam(d.startPos, d.axis, org, dir, s)) return;
            np[d.axis] = d.startPos[d.axis] + (s - d.s0);
        } else {
            Vec3 h;
            if (!planeHitY(d.startPos[1], org, dir, h)) return;
            np[0] = d.startPos[0] + h[0] - d.grab[0];
            np[2] = d.startPos[2] + h[2] - d.grab[2];
        }
        SnapOpts so = a.snapOpts();
        if (d.axis == 1) so.floor = false;   // a height move: the floor must not pull it back
        a.doc.snapPoint(np, so, ownPadId(a));
        // locked: only the axis moves, but the floor snap still sets the height of an X/Z move
        if (d.axis >= 0) for (int i = 0; i < 3; i++) if (i != d.axis && !(i == 1 && so.floor)) np[i] = d.startPos[i];
        if (a.doc.setTransform(d.sel, &np, nullptr, false, err)) d.changed = true;
    } else if (d.mode == App::Drag::YAW) {
        Vec3 h;
        if (!planeHitY(d.startPos[1], org, dir, h)) return;
        float yaw = atan2f(h[0] - d.startPos[0], h[2] - d.startPos[2]) * 57.29578f;
        if (a.snapGrid) yaw = roundf(yaw / 15.0f) * 15.0f;
        if (a.doc.setTransform(d.sel, nullptr, &yaw, false, err)) d.changed = true;
    }
}

void drawGizmo(App &a, const Overlay &ov, const GizmoGeo &g)
{
    if (!g.ok) return;
    static const ImU32 col[3] = { IM_COL32(240, 70, 70, 255), IM_COL32(80, 220, 90, 255), IM_COL32(80, 130, 250, 255) };
    ImDrawList *dl = ov.dl;
    const bool moving = a.drag.mode == App::Drag::MOVE;
    for (int i = 0; i < 3; i++) {
        const bool act = moving && a.drag.axis == i;
        dl->AddLine(g.c, g.tip[i], act ? IM_COL32(255, 255, 80, 255) : col[i], act ? 4.0f : 3.0f);
        dl->AddCircleFilled(g.tip[i], 5.0f, act ? IM_COL32(255, 255, 80, 255) : col[i]);
    }
    dl->AddRectFilled(ImVec2(g.c.x - 5, g.c.y - 5), ImVec2(g.c.x + 5, g.c.y + 5), IM_COL32(240, 240, 240, 255));
    dl->AddLine(g.c, g.yaw, IM_COL32(255, 170, 40, 255), 2.0f);
    dl->AddCircle(g.yaw, 7.0f, IM_COL32(255, 170, 40, 255), 16, 2.5f);
    if (a.drag.mode != App::Drag::NONE) {
        char t[160];
        if (a.drag.mode == App::Drag::YAW) snprintf(t, sizeof t, "Turning%s - release to confirm, Esc cancels", a.snapGrid ? " (15 deg steps)" : "");
        else snprintf(t, sizeof t, "Moving%s%s - %s, Esc cancels, X/Y/Z lock an axis",
                      a.drag.axis >= 0 ? " locked to " : " in the horizontal plane", a.drag.axis >= 0 ? (a.drag.axis == 0 ? "X" : a.drag.axis == 1 ? "Y" : "Z") : "",
                      a.drag.keymove ? "click confirms" : "release confirms");
        ov.dl->AddText(ImVec2(ov.x + 10, ov.y + 8), IM_COL32(255, 255, 120, 255), t);
        if (moving && a.drag.axis >= 0) {   // guide line through the start along the locked axis
            Vec3 p0 = a.drag.startPos, p1 = a.drag.startPos;
            p0[a.drag.axis] -= g.len * 40; p1[a.drag.axis] += g.len * 40;
            ov.line(p0, p1, IM_COL32(255, 255, 80, 120), 1.0f);
        }
    }
}

// ---- sun marker (environment): a handle on a sphere around the level, dragged to set azimuth/elevation ----

bool sunWorld(const App &a, Vec3 &pos, Vec3 &center, float &R)
{
    if (!a.doc.hasMission || !a.doc.mission.env.has_sun) return false;
    Vec3 mn = { -500, -500, -500 }, mx = { 500, 500, 500 };
    if (a.doc.hasLevel) { mn = a.doc.level.mn; mx = a.doc.level.mx; }
    center = { (mn[0] + mx[0]) / 2, (mn[1] + mx[1]) / 2, (mn[2] + mx[2]) / 2 };
    R = std::max({ mx[0] - mn[0], mx[1] - mn[1], mx[2] - mn[2], 300.0f }) * 0.6f;
    const float az = a.doc.mission.env.sun_azimuth * 0.017453292f, el = a.doc.mission.env.sun_elevation * 0.017453292f;
    pos = { center[0] + sinf(az) * cosf(el) * R, center[1] + sinf(el) * R, center[2] + cosf(az) * cosf(el) * R };
    return true;
}

bool sunHit(const App &a, const Overlay &ov, ImVec2 m)
{
    Vec3 p, c; float R; ImVec2 s;
    if (!sunWorld(a, p, c, R) || !ov.pt(p, s)) return false;
    return sqrtf((s.x - m.x) * (s.x - m.x) + (s.y - m.y) * (s.y - m.y)) < 14;
}

void sunUpdate(App &a, const Vec3 &org, const Vec3 &dir)
{
    Vec3 p, c; float R;
    if (!sunWorld(a, p, c, R)) return;
    const float t = (c[0] - org[0]) * dir[0] + (c[1] - org[1]) * dir[1] + (c[2] - org[2]) * dir[2];
    Vec3 v = { org[0] + dir[0] * t - c[0], org[1] + dir[1] * t - c[1], org[2] + dir[2] * t - c[2] };
    const float l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (l < 1e-3f) return;
    MissionEnv e = a.doc.mission.env;
    float az = atan2f(v[0], v[2]) * 57.29578f;
    if (az < 0) az += 360;
    e.sun_azimuth = az;
    e.sun_elevation = std::max(-10.0f, std::min(90.0f, asinf(v[1] / l) * 57.29578f));
    a.doc.setEnv(e, false);
}

void drawSun(const App &a, const Overlay &ov)
{
    Vec3 p, c; float R; ImVec2 s;
    if (!sunWorld(a, p, c, R)) return;
    const ImU32 col = IM_COL32(255, 210, 60, 255);
    ov.line(c, p, IM_COL32(255, 210, 60, 90), 1.0f);
    if (!ov.pt(p, s)) return;
    ov.dl->AddCircleFilled(s, 8, col);
    for (int i = 0; i < 8; i++) {
        float an = i * 0.7853982f;
        ov.dl->AddLine(ImVec2(s.x + cosf(an) * 12, s.y + sinf(an) * 12), ImVec2(s.x + cosf(an) * 17, s.y + sinf(an) * 17), col, 2.0f);
    }
    char t[64];
    snprintf(t, sizeof t, "sun az %.0f el %.0f%s", a.doc.mission.env.sun_azimuth, a.doc.mission.env.sun_elevation, a.sunDrag ? " (dragging)" : "");
    ov.dl->AddText(ImVec2(s.x + 20, s.y - 8), col, t);
}

// ---- camera input ----

void cameraInput(App &a, bool viewHovered)
{
    ImGuiIO &io = ImGui::GetIO();
    if (a.drag.mode != App::Drag::NONE || a.sunDrag) { a.looking = false; return; }
    if (viewHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) a.looking = true;
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Right)) a.looking = false;
    if (a.looking) {
        a.cam.yaw += io.MouseDelta.x * 0.0035f;
        a.cam.pitch -= io.MouseDelta.y * 0.0035f;
        a.cam.pitch = std::max(-1.55f, std::min(1.55f, a.cam.pitch));
    }
    // middle button: pan, the point under the cursor follows the mouse
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle)) a.panning = false;
    // moves of earlier frames only: the press frame's delta is not part of the drag
    if (a.panning && (io.MouseDelta.x != 0 || io.MouseDelta.y != 0)) {
        const float k = 2.0f * tanf(a.cam.fov * 0.5f) * a.panDist / std::max(a.vh, 1.0f);
        const Vec3 f = a.cam.forward(), r = a.cam.right();
        const Vec3 u = { r[1] * f[2] - r[2] * f[1], r[2] * f[0] - r[0] * f[2], r[0] * f[1] - r[1] * f[0] };
        for (int i = 0; i < 3; i++) a.cam.pos[i] += (-r[i] * io.MouseDelta.x + u[i] * io.MouseDelta.y) * k;
    }
    if (viewHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
        Vec3 o, d;
        a.cam.ray(io.MousePos.x - a.vx, io.MousePos.y - a.vy, a.vw, a.vh, o, d);
        Hit h = pickScene(a.doc, o, d);
        if (h.kind != Hit::NONE && h.t > 1.0f) a.panDist = h.t;
        else if (a.doc.hasLevel) {   // nothing under the cursor: the depth of the level's centre
            Vec3 c, f = a.cam.forward();
            for (int i = 0; i < 3; i++) c[i] = (a.doc.level.mn[i] + a.doc.level.mx[i]) * 0.5f - a.cam.pos[i];
            a.panDist = std::max(100.0f, c[0] * f[0] + c[1] * f[1] + c[2] * f[2]);
        } else a.panDist = 1000.0f;
        a.panning = true;
    }
    if (viewHovered && io.MouseWheel != 0) {
        // zoom toward the cursor: along the cursor ray, 15% of the distance to the scene point under it per notch
        // (wheel up = toward, down = away), so the point stays under the cursor and is never reached or passed
        Vec3 o, d;
        a.cam.ray(io.MousePos.x - a.vx, io.MousePos.y - a.vy, a.vw, a.vh, o, d);
        const Hit h = pickScene(a.doc, o, d);
        float step;
        if (h.kind != Hit::NONE && h.t > 1.0f) step = h.t * (1.0f - powf(0.85f, io.MouseWheel));
        else step = a.cam.speed * 0.25f * io.MouseWheel;     // nothing under the cursor: the current step, still along the cursor ray
        const Mat4 vpBefore = mat4Mul(a.cam.proj(a.vw / a.vh), a.cam.view());
        for (int i = 0; i < 3; i++) a.cam.pos[i] += d[i] * step;
        if (a.logCam && h.kind != Hit::NONE) {
            const Vec3 pt = { o[0] + d[0] * h.t, o[1] + d[1] * h.t, o[2] + d[2] * h.t };
            float b[2] = { 0, 0 }, n[2] = { 0, 0 };
            const Mat4 vpAfter = mat4Mul(a.cam.proj(a.vw / a.vh), a.cam.view());
            projectPoint(vpBefore, pt, 0, 0, a.vw, a.vh, b);
            projectPoint(vpAfter, pt, 0, 0, a.vw, a.vh, n);
            fprintf(stderr, "zoom: wheel %.1f, point %.1f %.1f %.1f at distance %.1f -> %.1f; its pixel before %.2f,%.2f after %.2f,%.2f (cursor %.1f,%.1f)\n",
                    io.MouseWheel, pt[0], pt[1], pt[2], h.t, h.t - step, b[0], b[1], n[0], n[1], io.MousePos.x - a.vx, io.MousePos.y - a.vy);
        }
    }
    if (io.WantTextInput) return;
    if (!a.looking) return;     // the movement keys fly only while the right button is held: Ctrl+C / G / X never move the camera
    float s = a.cam.speed * io.DeltaTime * (io.KeyShift ? 4.0f : 1.0f);
    Vec3 f = a.cam.forward(), r = a.cam.right();
    float fw = 0, rt = 0, up = 0;
    if (ImGui::IsKeyDown(ImGuiKey_W) || ImGui::IsKeyDown(ImGuiKey_Z) || ImGui::IsKeyDown(ImGuiKey_UpArrow)) fw += 1;
    if (ImGui::IsKeyDown(ImGuiKey_S) || ImGui::IsKeyDown(ImGuiKey_DownArrow)) fw -= 1;
    if (ImGui::IsKeyDown(ImGuiKey_D) || ImGui::IsKeyDown(ImGuiKey_RightArrow)) rt += 1;
    if (ImGui::IsKeyDown(ImGuiKey_A) || ImGui::IsKeyDown(ImGuiKey_Q) || ImGui::IsKeyDown(ImGuiKey_LeftArrow)) rt -= 1;
    if (ImGui::IsKeyDown(ImGuiKey_Space)) up += 1;
    if (io.KeyCtrl && !io.KeyShift) up -= 1;
    for (int i = 0; i < 3; i++) a.cam.pos[i] += (f[i] * fw + r[i] * rt) * s;
    a.cam.pos[1] += up * s;
}

// ---- one frame of UI ----

void frameUi(App &a, int winW, int winH, bool &quit)
{
    ImGuiIO &io = ImGui::GetIO();
    const float W = (float)winW, H = (float)winH;
    const float topH = 40, leftW = 310, rightW = 330, botH = 130;
    a.vx = leftW; a.vy = topH; a.vw = std::max(W - leftW - rightW, 1.0f); a.vh = std::max(H - topH - botH, 1.0f);
    a.items = a.doc.items();
    {   // the thumbnail goes next to the open mission when it is a mission of the open mod
        std::string t = a.doc.defaultThumbPath();
        if (!t.empty() && t != a.lastAutoThumb) { a.lastAutoThumb = t; snprintf(a.thumbPath, sizeof a.thumbPath, "%s", t.c_str()); }
    }

    toolbar(a, W, topH);
    leftPanel(a, 0, topH, leftW, H - topH);
    rightPanel(a, W - rightW, topH, rightW, H - topH - botH);
    bottomPanel(a, leftW, H - botH, W - leftW, botH);
    unsavedModal(a);
    levelSwitchModal(a);

    const ImVec2 mp = io.MousePos;
    const bool inView = mp.x >= a.vx && mp.x < a.vx + a.vw && mp.y >= a.vy && mp.y < a.vy + a.vh;
    const bool viewHovered = inView && !io.WantCaptureMouse;
    cameraInput(a, viewHovered);

    Overlay ov;
    ov.dl = ImGui::GetBackgroundDrawList();
    ov.vp = mat4Mul(a.cam.proj(a.vw / a.vh), a.cam.view());
    ov.x = a.vx; ov.y = a.vy; ov.w = a.vw; ov.h = a.vh;

    // transform gizmo of the selection (pads of the level are fixed: no gizmo)
    const TransformInfo ti = a.doc.getTransform(a.doc.sel);
    GizmoGeo gg;
    if (ti.valid && ti.movable) gg = gizmoGeo(a, ov, ti);
    a.gizOk = gg.ok; a.gizC = gg.c;
    for (int i = 0; i < 3; i++) a.gizTip[i] = gg.tip[i];
    Vec3 mOrg, mDir;
    a.cam.ray(mp.x - a.vx, mp.y - a.vy, a.vw, a.vh, mOrg, mDir);

    if (a.sunDrag) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) { std::string e; a.doc.restoreSnapshot(a.sunSnap, e); a.sunDrag = false; }
        else {
            sunUpdate(a, mOrg, mDir);
            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) { a.doc.commit(a.sunSnap); a.sunDrag = false; }
        }
    } else if (a.drag.mode != App::Drag::NONE) {
        if (!io.WantTextInput && a.drag.mode == App::Drag::MOVE) {
            int k = ImGui::IsKeyPressed(ImGuiKey_X, false) ? 0 : ImGui::IsKeyPressed(ImGuiKey_Y, false) ? 1 : ImGui::IsKeyPressed(ImGuiKey_Z, false) ? 2 : -1;
            if (k >= 0) { a.drag.axis = a.drag.axis == k ? -1 : k; dragRebase(a, mOrg, mDir); }
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            dragEnd(a, false);
            a.say("move cancelled");
        } else {
            dragUpdate(a, mOrg, mDir);
            if (!a.drag.keymove && ImGui::IsMouseReleased(ImGuiMouseButton_Left) &&
                fabsf(mp.x - a.drag.pressAt.x) < 4 && fabsf(mp.y - a.drag.pressAt.y) < 4) {
                a.drag.keymove = true;   // a click on a handle (no drag): it follows the mouse until the next click
            } else {
                const bool confirm = a.drag.keymove ? ImGui::IsMouseClicked(ImGuiMouseButton_Left) : ImGui::IsMouseReleased(ImGuiMouseButton_Left);
                if (confirm) dragEnd(a, true);
            }
        }
    } else {
        if (viewHovered && ti.valid && ti.movable && !io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_G, false)) {
            dragBegin(a, 4, mOrg, mDir);
            a.drag.keymove = true;
        } else if (viewHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && sunHit(a, ov, mp)) {
            a.sunDrag = true;
            a.sunSnap = a.doc.snapshot();
        } else if (viewHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            int hit = gg.ok ? gizmoHit(gg, mp) : 0;
            if (hit) { dragBegin(a, hit, mOrg, mDir); a.drag.pressAt = mp; }
            else { a.lmbDown = true; a.lmbX = mp.x; a.lmbY = mp.y; }
        }
        // click = press and release in the view without dragging
        if (a.lmbDown && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            a.lmbDown = false;
            if (inView && fabsf(mp.x - a.lmbX) < 4 && fabsf(mp.y - a.lmbY) < 4)
                applyClick(a, pickScene(a.doc, mOrg, mDir));
        }
    }

    // shortcuts
    if (!io.WantTextInput) {
        if (viewHovered && io.KeyCtrl && !io.KeyShift && !a.looking && a.drag.mode == App::Drag::NONE) {
            // Ctrl+C copies the selected guard / object, Ctrl+V pastes a new one under the cursor, Ctrl+D duplicates
            // it in place and starts a move that follows the mouse until the next click
            if (ImGui::IsKeyPressed(ImGuiKey_C, false)) copySelected(a);
            if (ImGui::IsKeyPressed(ImGuiKey_V, false)) pasteAtMouse(a, mOrg, mDir);
            if (ImGui::IsKeyPressed(ImGuiKey_D, false)) duplicateSelected(a, mOrg, mDir);
        }
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) { std::string e; a.doc.undo(e); }
        if (!a.modalOpen) {
            if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_S, false)) doSave(a);
            if (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_S, false) && fileDialogAvailable()) doSaveAs(a);
            if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_O, false) && fileDialogAvailable()) guarded(a, [&a] { doOpenDialog(a); });
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) && a.doc.sel.kind == Selection::ITEM) { std::string e; a.doc.removeItem(a.doc.sel.item, e); }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) a.tool = App::T_SELECT;
        if (ImGui::IsKeyPressed(ImGuiKey_F, false) && !io.KeyCtrl) {
            if (const Item *it = selectedItem(a)) { if (it->hasPos) focusOn(a, it->pos); }
            else if (a.doc.sel.kind == Selection::PAD) { if (const LPad *p = a.doc.level.pad(a.doc.sel.num)) focusOn(a, p->pos); }
        }
    }
    (void)quit;

    ov.dl->PushClipRect(ImVec2(a.vx, a.vy), ImVec2(a.vx + a.vw, a.vy + a.vh), true);
    drawOverlay(a, ov);
    drawSun(a, ov);
    {   // the gizmo follows the selection live while it is dragged
        TransformInfo t2 = a.doc.getTransform(a.doc.sel);
        if (t2.valid && t2.movable) drawGizmo(a, ov, gizmoGeo(a, ov, t2));
    }
    ov.dl->PopClipRect();
}

void usage()
{
    puts("ge-editor: GoldenEye co-op mission editor\n"
         "  ge-editor [options]\n"
         "    --level NAME      solo level to open (e.g. bunker1): from the export cache next to the game, exported first if missing\n"
         "    --export DIR      level export folder (level.rec + level.json from `ge007 -level_XX --export-level DIR`)\n"
         "    --mod DIR         mod folder (mods/<mod>) to open\n"
         "    --mission FILE    mission.json to open (a new one is started if the file does not exist)\n"
         "    --port N          MCP HTTP port on 127.0.0.1 (default 8765; the next free one is used)\n"
         "    --no-mcp          do not start the MCP HTTP server\n"
         "    --no-offscreen    draw straight into the window, not into a whole offscreen frame copied at the end\n"
         "                      (the default avoids half-drawn frames on NVIDIA's DXGI present path)\n"
         "    --mcp-stdio       headless: MCP over stdin/stdout (newline-delimited JSON-RPC), no window\n"
         "    --mcp-http        headless: only the HTTP MCP server, no window (until killed)\n"
         "    --game EXE        game executable for Play (default: GE_EDITOR_GAME, else ge007 next to the editor)\n"
         "    --screenshot F.png [--frames N]   render N frames, save the window to a PNG and exit\n"
         "    --cam x,y,z,yaw,pitch             start camera (radians)\n"
         "    --click X,Y [--tool select|guard|object|move]   test aid: click the view at window pixel X,Y on frame 3\n"
         "    --key FRAME[xLEN]:SPEC            test aid: key / button / wheel events, SPEC = ctrl+c, ctrl+d, rmb+ctrl, w, wheel1, wheel-2, quit ...\n"
         "    --mouse-at X,Y                    test aid: the cursor rests at window pixel X,Y from frame 4\n"
         "    --mouse-then FRAME,X,Y            test aid: from that frame the cursor rests at X,Y instead\n"
         "    --log-cam                         test aid: print camera moves and the wheel-zoom pixel check to stderr\n"
         "  Camera: hold the right mouse button to fly: mouse = look, WASD/ZQSD/arrows = move, Space = up, Ctrl = down, Shift = fast.\n"
         "          The movement keys do nothing without the right button, so shortcuts (Ctrl+C/V/D, G, X/Y/Z) never move the camera.\n"
         "          Middle button = pan, mouse wheel = zoom toward the cursor (15% of the distance to the point under it per notch).\n"
         "  Edit: Ctrl+C copies the selected guard or object, Ctrl+V pastes it under the cursor, Ctrl+D duplicates it and moves the copy\n"
         "        with the mouse until the next click; G grabs, X/Y/Z lock an axis, Ctrl+Z undoes (the view must be hovered).");
}

bool parseArgs(int argc, char **argv, Options &o)
{
    for (int i = 1; i < argc; i++) {
        std::string s = argv[i];
        auto next = [&](std::string &dst) { if (i + 1 < argc) dst = argv[++i]; };
        if (s == "--mcp-stdio") o.mcpStdio = true;
        else if (s == "--mcp-http") o.mcpHttp = true;
        else if (s == "--no-mcp") o.noMcp = true;
        else if (s == "--no-offscreen") o.noOffscreen = true;
        else if (s == "--export") next(o.exportDir);
        else if (s == "--level") next(o.level);
        else if (s == "--mission") next(o.missionFile);
        else if (s == "--game") next(o.game);
        else if (s == "--mod") next(o.modDir);
        else if (s == "--screenshot") next(o.screenshot);
        else if (s == "--port" && i + 1 < argc) o.port = atoi(argv[++i]);
        else if (s == "--frames" && i + 1 < argc) o.frames = atoi(argv[++i]);
        else if (s == "--cam" && i + 1 < argc) {
            o.hasCam = sscanf(argv[++i], "%f,%f,%f,%f,%f", &o.cam[0], &o.cam[1], &o.cam[2], &o.cam[3], &o.cam[4]) == 5;
        } else if (s == "--click" && i + 1 < argc) {
            o.hasClick = sscanf(argv[++i], "%f,%f", &o.click[0], &o.click[1]) == 2;
        } else if (s == "--drag" && i + 1 < argc) {
            o.hasDrag = sscanf(argv[++i], "%f,%f,%f,%f", &o.drag[0], &o.drag[1], &o.drag[2], &o.drag[3]) == 4;
        } else if (s == "--key" && i + 1 < argc) {
            int fr = 0, len = 2, used = 0;
            const char *v = argv[++i];
            if (sscanf(v, "%d%n", &fr, &used) >= 1) {
                if (v[used] == 'x') { int l = 0, u2 = 0; if (sscanf(v + used + 1, "%d%n", &l, &u2) >= 1) { len = l; used += 1 + u2; } }
                if (v[used] == ':') o.keys.push_back({ fr, len, std::string(v + used + 1) });
            }
        } else if (s == "--log-cam") { o.logCam = true;
        } else if (s == "--mouse-then" && i + 1 < argc) {
            sscanf(argv[++i], "%f,%f,%f", &o.mouseThen[0], &o.mouseThen[1], &o.mouseThen[2]);
        } else if (s == "--mouse-at" && i + 1 < argc) {
            o.hasMouseAt = sscanf(argv[++i], "%f,%f", &o.mouseAt[0], &o.mouseAt[1]) == 2;
        } else if (s == "--mouse-click") { o.mouseClickMode = true;
        } else if (s == "--mouse-middle") { o.mouseMiddle = true;
        } else if (s == "--mouse-drag" && i + 1 < argc) {
            o.hasMouseDrag = sscanf(argv[++i], "%f,%f", &o.mouseDrag[0], &o.mouseDrag[1]) == 2;
        } else if (s == "--drag-axis" && i + 1 < argc) {
            std::string v = argv[++i]; o.dragAxis = v == "x" ? 0 : v == "y" ? 1 : v == "z" ? 2 : -1;
        } else if (s == "--tool") next(o.tool);
        else if (s == "--help" || s == "-h") o.help = true;
        else { fprintf(stderr, "unknown argument: %s\n", s.c_str()); return false; }
    }
    return true;
}

// blocking: the headless modes export a missing level here; the window does it in the background (pollLevel).
bool loadStartup(Doc &doc, const Options &o, std::string &err, bool blocking)
{
    if (!o.exportDir.empty() && !doc.openLevel(o.exportDir, err)) return false;
    if (!o.modDir.empty() && !doc.openMod(o.modDir, err)) return false;
    if (!o.missionFile.empty()) {
        FILE *f = fopen(o.missionFile.c_str(), "rb");
        if (f) {
            fclose(f);
            if (!doc.openMission(o.missionFile, err)) return false;
        } else {
            if (!doc.newMission("New mission", "", err)) return false;
            doc.missionPath = o.missionFile;
        }
    }
    if (blocking) {
        std::string lvl = !o.level.empty() && !doc.hasLevel ? o.level : doc.missingLevel();
        if (!lvl.empty() && !doc.openLevelByName(lvl, false, err)) return false;
    }
    return true;
}

} // namespace

int main(int argc, char **argv)
{
    Options opt;
    if (!parseArgs(argc, argv, opt)) return 2;
    if (opt.help) { usage(); return 0; }
    editorStubsSetArgs(argc, argv);
    const std::string exeDir = dirOfExe(argv[0]);

    if (opt.mcpStdio) {
#ifdef _WIN32
        _setmode(_fileno(stdout), _O_BINARY);
        _setmode(_fileno(stdin), _O_BINARY);
#endif
        Doc doc;
        doc.gameExe = opt.game.empty() ? defaultGameExe(exeDir) : opt.game;
        std::string err;
        if (!loadStartup(doc, opt, err, true)) { fprintf(stderr, "ge-editor: %s\n", err.c_str()); return 1; }
        return mcpStdioLoop(doc);
    }

    if (opt.mcpHttp) {
        Doc doc;
        doc.gameExe = opt.game.empty() ? defaultGameExe(exeDir) : opt.game;
        std::string err;
        if (!loadStartup(doc, opt, err, true)) { fprintf(stderr, "ge-editor: %s\n", err.c_str()); return 1; }
        std::mutex docLock;
        McpHttp http;
        if (!http.start(opt.port, [&](std::function<void()> fn) { std::lock_guard<std::mutex> l(docLock); fn(); }, doc, err)) {
            fprintf(stderr, "ge-editor: %s\n", err.c_str());
            return 1;
        }
        printf("MCP http://127.0.0.1:%d/mcp\n", http.port());
        fflush(stdout);
        for (;;) std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    App app;
    app.doc.gameExe = opt.game.empty() ? defaultGameExe(exeDir) : opt.game;
    snprintf(app.gameExe, sizeof app.gameExe, "%s", app.doc.gameExe.c_str());

    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    app.win = SDL_CreateWindow("GoldenEye mission editor", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1500, 920,
                               SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!app.win) { fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return 1; }
    app.gl = SDL_GL_CreateContext(app.win);
    if (!app.gl) { fprintf(stderr, "no OpenGL 3.3 core context: %s\n", SDL_GetError()); return 1; }
    SDL_GL_SetSwapInterval(1);
    {   // what the driver really gave (a single-buffered window would show half-drawn frames)
        int dbl = -1, depth = -1, maj = -1, min = -1;
        SDL_GL_GetAttribute(SDL_GL_DOUBLEBUFFER, &dbl);
        SDL_GL_GetAttribute(SDL_GL_DEPTH_SIZE, &depth);
        SDL_GL_GetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, &maj);
        SDL_GL_GetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, &min);
        fprintf(stderr, "[editor] GL context %d.%d: doublebuffer %d, depth %d bits, swap interval %d, offscreen frame %s\n",
                maj, min, dbl, depth, SDL_GL_GetSwapInterval(), opt.noOffscreen ? "off (--no-offscreen)" : "on");
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    ImGui::GetStyle().WindowRounding = 0;
    io.FontGlobalScale = 1.15f;
    ImGui_ImplSDL2_InitForOpenGL(app.win, app.gl);
    ImGui_ImplOpenGL3_Init("#version 330");

    int dw, dh;
    SDL_GL_GetDrawableSize(app.win, &dw, &dh);
    std::string err;
    if (!app.scene.init(dw, dh, err)) { fprintf(stderr, "renderer: %s\n", err.c_str()); return 1; }
    app.scene.setOffscreen(!opt.noOffscreen);

    if (!loadStartup(app.doc, opt, err, false)) app.say("startup: %s", err.c_str());
    if (!opt.level.empty() && !app.doc.hasLevel) requestLevel(app, opt.level, false);
    if (!app.doc.hasLevel) app.cam.pos = { 0, 800, 0 };
    reloadLevelGpu(app);
    if (opt.hasCam) {
        app.cam.pos = { opt.cam[0], opt.cam[1], opt.cam[2] };
        app.cam.yaw = opt.cam[3]; app.cam.pitch = opt.cam[4];
    }
    snprintf(app.missionPath, sizeof app.missionPath, "%s", app.doc.missionPath.c_str());
    if (app.doc.hasMod) snprintf(app.modDirBuf, sizeof app.modDirBuf, "%s", app.doc.modDir.c_str());

    app.doc.captureFn = [&app](const std::string &path, float intensity, std::string &e) { return captureThumb(app, path, intensity, e); };
    // thumb.png next to the mission, by default
    if (!app.doc.missionPath.empty()) {
        std::string mp = app.doc.missionPath;
        size_t s = mp.find_last_of("/\\");
        snprintf(app.thumbPath, sizeof app.thumbPath, "%s", ((s == std::string::npos ? std::string() : mp.substr(0, s + 1)) + "thumb.png").c_str());
    }

    if (!opt.noMcp) {
        if (app.http.start(opt.port, runOnMain, app.doc, err)) {
            app.mcpPort = app.http.port();
            app.say("MCP: claude mcp add --transport http ge-editor http://127.0.0.1:%d/mcp", app.mcpPort);
        } else app.say("MCP HTTP server: %s", err.c_str());
    }

    bool quit = false;
    int frame = 0;
    app.logCam = opt.logCam;
    app.recentFile = exeDir + "ge-editor-recent.txt";
    app.prefsFile = exeDir + "ge-editor-prefs.txt";
    loadPrefs(app);
    recentLoad(app);
    Vec3 lastCam = app.cam.pos;
    while (!quit) {
        SDL_Event e;
        static ImVec2 mdStart;
        if (opt.hasMouseDrag && frame >= 4 && frame <= 20) {   // test aid: press on the gizmo, move, release, as a hand does
            if (frame == 4) {
                // the mouse "enters" the window: else the SDL backend reads the real cursor while the window has focus
                SDL_Event en; memset(&en, 0, sizeof en);
                en.type = SDL_WINDOWEVENT; en.window.windowID = SDL_GetWindowID(app.win); en.window.event = SDL_WINDOWEVENT_ENTER;
                SDL_PushEvent(&en);
                mdStart = app.gizC;
                if (opt.dragAxis >= 0) mdStart = ImVec2((app.gizC.x + app.gizTip[opt.dragAxis].x) * 0.5f, (app.gizC.y + app.gizTip[opt.dragAxis].y) * 0.5f);
                TransformInfo t0 = app.doc.getTransform(app.doc.sel);
                fprintf(stderr, "mouse-drag: camera at %.0f %.0f %.0f\n", app.cam.pos[0], app.cam.pos[1], app.cam.pos[2]);
                fprintf(stderr, "mouse-drag: gizmo %s, press at %.0f,%.0f, item at %.0f %.0f %.0f\n", app.gizOk ? "shown" : "NOT shown",
                        mdStart.x, mdStart.y, t0.pos[0], t0.pos[1], t0.pos[2]);
            }
            const int k = frame - (opt.mouseClickMode ? 7 : 6);
            ImVec2 m = mdStart;
            if (k > 0) { float f = std::min(k, 10) / 10.0f; m.x += opt.mouseDrag[0] * f; m.y += opt.mouseDrag[1] * f; }
            SDL_Event ev; memset(&ev, 0, sizeof ev);
            ev.type = SDL_MOUSEMOTION; ev.motion.windowID = SDL_GetWindowID(app.win); ev.motion.x = (int)m.x; ev.motion.y = (int)m.y;
            SDL_PushEvent(&ev);
            const bool down = frame == 6 || (opt.mouseClickMode && frame == 17);
            const bool up = frame == 18 || (opt.mouseClickMode && frame == 7);
            if (down || up) {
                memset(&ev, 0, sizeof ev);
                ev.type = down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
                ev.button.windowID = SDL_GetWindowID(app.win); ev.button.button = opt.mouseMiddle ? SDL_BUTTON_MIDDLE : SDL_BUTTON_LEFT;
                ev.button.state = down ? SDL_PRESSED : SDL_RELEASED; ev.button.clicks = 1;
                ev.button.x = (int)m.x; ev.button.y = (int)m.y;
                SDL_PushEvent(&ev);
            }
            if (frame == 12) fprintf(stderr, "mouse-drag: mid-drag mode %d axis %d\n", (int)app.drag.mode, app.drag.axis);
            if (frame == 20) {
                TransformInfo t1 = app.doc.getTransform(app.doc.sel);
                fprintf(stderr, "mouse-drag: camera at %.0f %.0f %.0f\n", app.cam.pos[0], app.cam.pos[1], app.cam.pos[2]);
                fprintf(stderr, "mouse-drag: after release item at %.0f %.0f %.0f, undo steps %zu\n", t1.pos[0], t1.pos[1], t1.pos[2], app.doc.undoStack.size());
            }
        }
        if (opt.hasMouseAt && frame >= 4 && !opt.hasMouseDrag) {   // test aid: the cursor rests at --mouse-at
            if (frame == 4) {
                SDL_Event en; memset(&en, 0, sizeof en);
                en.type = SDL_WINDOWEVENT; en.window.windowID = SDL_GetWindowID(app.win); en.window.event = SDL_WINDOWEVENT_ENTER;
                SDL_PushEvent(&en);
            }
            SDL_Event ev; memset(&ev, 0, sizeof ev);
            ev.type = SDL_MOUSEMOTION; ev.motion.windowID = SDL_GetWindowID(app.win); ev.motion.x = (int)opt.mouseAt[0]; ev.motion.y = (int)opt.mouseAt[1];  // (the ImGui event below wins)
            SDL_PushEvent(&ev);
        }
        for (const Options::KeyAid &k : opt.keys) {   // test aid: key / button / wheel events through ImGui's input queue
            const bool on = frame >= k.frame && frame < k.frame + k.len;
            const bool edge = frame == k.frame + k.len;      // release one frame after
            if (!on && !edge) continue;
            ImGuiIO &kio = ImGui::GetIO();
            std::string t = k.spec;
            for (size_t p0 = 0; p0 <= t.size();) {
                size_t p1 = t.find('+', p0);
                if (p1 == std::string::npos) p1 = t.size();
                const std::string tok = t.substr(p0, p1 - p0);
                p0 = p1 + 1;
                if (tok == "ctrl") kio.AddKeyEvent(ImGuiMod_Ctrl, on);
                else if (tok == "shift") kio.AddKeyEvent(ImGuiMod_Shift, on);
                else if (tok == "rmb") kio.AddMouseButtonEvent(1, on);
                else if (tok == "space") kio.AddKeyEvent(ImGuiKey_Space, on);
                else if (tok == "quit") { if (frame == k.frame) { SDL_Event q; memset(&q, 0, sizeof q); q.type = SDL_QUIT; SDL_PushEvent(&q); } }
                else if (tok.compare(0, 5, "wheel") == 0) { if (frame == k.frame) kio.AddMouseWheelEvent(0, (float)atof(tok.c_str() + 5)); }
                else if (tok.size() == 1 && tok[0] >= 'a' && tok[0] <= 'z') kio.AddKeyEvent((ImGuiKey)(ImGuiKey_A + (tok[0] - 'a')), on);
            }
        }
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL2_ProcessEvent(&e);
            if (e.type == SDL_QUIT || (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_CLOSE)) {
                if (app.doc.hasMission && app.doc.dirty && !app.modalOpen) guarded(app, [&app] { app.wantQuit = true; });   // waits for the answer
                else if (!(app.doc.hasMission && app.doc.dirty)) quit = true;
            }
        }
        drainJobs();
        pollLevel(app);
        if (app.doc.levelGen != app.seenLevelGen) reloadLevelGpu(app);

        SDL_GL_GetDrawableSize(app.win, &dw, &dh);
        int ww, wh;
        SDL_GetWindowSize(app.win, &ww, &wh);
        if (SDL_GetWindowFlags(app.win) & SDL_WINDOW_MINIMIZED) { SDL_Delay(50); continue; }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        if (opt.hasMouseAt && frame >= 4 && !opt.hasMouseDrag) { const bool then = opt.mouseThen[0] >= 0 && frame >= (int)opt.mouseThen[0]; ImGui::GetIO().AddMousePosEvent(then ? opt.mouseThen[1] : opt.mouseAt[0], then ? opt.mouseThen[2] : opt.mouseAt[1]); }   // over the real cursor
        ImGui::NewFrame();
        frameUi(app, ww, wh, quit);
        if (app.wantQuit) quit = true;
        {   // the window title: "<mission>[*] - <level> - GoldenEye mission editor", only when it changes
            std::string title = "GoldenEye mission editor";
            if (app.doc.hasMission) title = std::string(app.doc.mission.name) + (app.doc.dirty ? "*" : "") + " - " + app.doc.mission.level + " - " + title;
            if (title != app.lastTitle) { app.lastTitle = title; SDL_SetWindowTitle(app.win, title.c_str()); }
        }
        // a mission that is saved (or was just opened) goes to the recent list
        if (app.doc.hasMission && !app.doc.dirty && app.doc.missionPath != app.lastRecorded) {
            app.lastRecorded = app.doc.missionPath;
            recentNote(app, app.doc.missionPath);
        }
        ImGui::Render();

        // 3D view in drawable pixels
        const float sx = (float)dw / (float)std::max(ww, 1), sy = (float)dh / (float)std::max(wh, 1);
        app.scene.render(app.doc, app.cam, dw, dh, (int)(app.vx * sx), (int)(app.vy * sy), (int)(app.vw * sx), (int)(app.vh * sy),
                         app.showRooms, app.showProps);
        if (app.scene.offscreen()) editorOffscreenBind(dw, dh);   // the GUI goes into the same offscreen frame
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        if (app.scene.offscreen()) editorOffscreenPresent(dw, dh);

        frame++;
        if (opt.logCam && (app.cam.pos[0] != lastCam[0] || app.cam.pos[1] != lastCam[1] || app.cam.pos[2] != lastCam[2])) {
            fprintf(stderr, "cam: frame %d pos %.1f %.1f %.1f\n", frame, app.cam.pos[0], app.cam.pos[1], app.cam.pos[2]);
            lastCam = app.cam.pos;
        }
        if (opt.hasClick && frame == 3) {
            if (opt.tool == "guard") app.tool = App::T_GUARD;
            else if (opt.tool == "object") app.tool = App::T_OBJECT;
            else if (opt.tool == "move") app.tool = App::T_MOVE;
            Vec3 org, dir;
            app.cam.ray(opt.click[0] - app.vx, opt.click[1] - app.vy, app.vw, app.vh, org, dir);
            Hit h = pickScene(app.doc, org, dir);
            fprintf(stderr, "click: hit kind %d (0 none, 1 item, 2 pad, 3 mission pad, 4 floor) at %.0f %.0f %.0f room %d\n", (int)h.kind,
                    h.point[0], h.point[1], h.point[2], h.room);
            applyClick(app, h);
            fprintf(stderr, "click: selection kind %d, mission has %d guard ops, %d objects, %d pads\n", (int)app.doc.sel.kind,
                    app.doc.mission.nguards, app.doc.mission.nobjects, app.doc.mission.npads);
        }
        if (opt.hasDrag && frame == 4) {   // test aid: a gizmo drag of the selection, like the mouse does it
            TransformInfo t0 = app.doc.getTransform(app.doc.sel);
            Vec3 o1, d1, o2, d2;
            app.cam.ray(opt.drag[0] - app.vx, opt.drag[1] - app.vy, app.vw, app.vh, o1, d1);
            app.cam.ray(opt.drag[2] - app.vx, opt.drag[3] - app.vy, app.vw, app.vh, o2, d2);
            dragBegin(app, opt.dragAxis >= 0 ? opt.dragAxis + 1 : 4, o1, d1);
            dragUpdate(app, o2, d2);
            dragEnd(app, true);
            TransformInfo t1 = app.doc.getTransform(app.doc.sel);
            fprintf(stderr, "drag: %.0f %.0f %.0f -> %.0f %.0f %.0f, undo steps %zu, pads %d\n", t0.pos[0], t0.pos[1], t0.pos[2],
                    t1.pos[0], t1.pos[1], t1.pos[2], app.doc.undoStack.size(), app.doc.mission.npads);
        }
        if (!opt.screenshot.empty() && frame >= opt.frames) {
            std::vector<uint8_t> px;
            if (editorGlReadWindow(dw, dh, px) && writePng(opt.screenshot.c_str(), dw, dh, px.data()))
                fprintf(stderr, "screenshot: %s (%dx%d, %d runs drawn)\n", opt.screenshot.c_str(), dw, dh, app.scene.drawn());
            else fprintf(stderr, "screenshot failed\n");
            quit = true;
        }
        SDL_GL_SwapWindow(app.win);
    }

    app.http.stop();
    app.scene.setLevel(nullptr);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(app.gl);
    SDL_DestroyWindow(app.win);
    SDL_Quit();
    return 0;
}
