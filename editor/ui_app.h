// The editor GUI's shared state (App) and the helpers every panel file uses. The UI is split by panel so
// that work on one panel does not touch another's file:
//   ui_menubar.cpp   main menu bar          ui_toolbar.cpp   tools / Play / snapping
//   ui_scene.cpp     left tree (Scene)      ui_selection.cpp right panel, Selection tab
//   ui_mission.cpp   right, Mission tab     ui_mod.cpp       right, Mod tab
//   ui_settings.cpp  Settings window        ui_modals.cpp    unsaved / mission-level / Controls popups
//   ui_log.cpp       status line + log      ui_overlay.cpp   3D overlay, gizmo, sun marker
//   ui_frame.cpp     one frame: layout, the right panel's tab bar, camera, view input, shortcuts
//   ui_actions.cpp   actions shared by menus, buttons and shortcuts (open / save / copy / level / prefs ...)
// main.cpp keeps main(), the command line and the test aids.
#pragma once

#include <SDL.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "imgui.h"

#include "doc.h"
#include "editor_gl.h"
#include "exportcache.h"
#include "filedialog.h"
#include "mcp.h"
#include "scene.h"
#include "view.h"

namespace ed {

struct LogLine { std::string text; int sev = 0; };   // sev: 0 info, 1 warning, 2 error

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
    char pickLevel[32] = "bunker1";   // Mission tab > level
    ExportJob job;                    // the game exporting a level for us
    std::string autoTried;
    ImVec2 gizC{}, gizTip[3]{};       // last frame's gizmo on screen (the --mouse-drag test aid aims at it)
    bool gizOk = false;
    std::deque<LogLine> log;
    int seenLevelGen = 0;
    McpHttp http;
    int mcpPort = 0;
    bool looking = false, lmbDown = false, panning = false;
    // opening / saving: the unsaved-changes guard, recent missions
    std::function<void()> pending;     // what waits for the answer of the "Unsaved changes" dialog
    bool askUnsaved = false, modalOpen = false, wantQuit = false;
    bool askLevelSwitch = false;       // level Open while the mission is for another level
    std::string levelSwitchTo;
    std::vector<std::string> recent;   // last opened / saved missions, newest first
    std::string recentFile, lastRecorded, lastTitle;
    std::string prefsFile;             // ge-editor-prefs.txt next to the exe: remembered settings (key=value lines)
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

    // ---- the shell (menu bar, tabs, status line, Settings) ----
    bool showLog = false;              // the log panel is open (else one status line)
    bool showSettings = false, showHelp = false;
    int forceTab = -1;                 // right panel: select this tab next frame (0 Selection, 1 Mission, 2 Mod), -1 none
    char sceneFilter[64] = "";         // Scene panel filter
    // persisted in ge-editor-prefs.txt (next to coop): applied at the next start
    bool prefOffscreen = true;         // draw through a whole offscreen frame (editor_gl.h)
    bool prefMcp = true;               // start the MCP HTTP server
    int prefMcpPort = 8765;
    // Settings > re-export all cached levels: the queue runs one export after another in the background
    std::deque<std::string> reexportQueue;
    int reexportTotal = 0;
    // test aids (main.cpp): open a menu / window / tab by name
    std::map<std::string, ImVec2> menuPos;   // centre of each top-level menu title, last frame

    SnapOpts snapOpts() const
    {
        SnapOpts s;
        s.grid = snapGrid ? gridStep : 0;
        s.floor = snapFloor; s.pads = snapPads;
        return s;
    }

    // Severity from the wording: the messages are "<what>: <reason>" for failures.
    static int severityOf(const char *m)
    {
        static const char *errs[] = { "save: ", "save as: ", "open mission: ", "open level: ", "new mission: ", "play: ", "edit: ",
                                      "field: ", "paste: ", "copy: ", "duplicate: ", "move: ", "remove: ", "turn: ", "undo: ", "capture: ",
                                      "export: ", "export ", "mod: ", "save mod: ", "add mission: ", "open: ", "mission level: ",
                                      "startup: ", "renderer: ", "MCP HTTP server: ", "settings: " };
        for (const char *e : errs) if (strncmp(m, e, strlen(e)) == 0) return 2;
        if (strncmp(m, "invalid: ", 9) == 0 || strncmp(m, "mod: ", 5) == 0 || strstr(m, "unsaved") || strncmp(m, "an export is already", 20) == 0) return 1;
        return 0;
    }

    void say(const char *fmt, ...)
    {
        char b[512];
        va_list ap; va_start(ap, fmt);
        vsnprintf(b, sizeof b, fmt, ap);
        va_end(ap);
        log.push_back({ b, severityOf(b) });
        if (log.size() > 200) log.pop_front();
        fprintf(stderr, "[editor] %s\n", b);
    }
};

// ---- the 3D overlay: drawn with the ImGui background list over the view ----
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

inline float dist3(const Vec3 &a, const Vec3 &b)
{
    float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

struct GizmoGeo {
    bool ok = false;
    ImVec2 c, tip[3], yaw;
    float len = 0;
};

const ImGuiWindowFlags kPanel = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                                ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;

// ---- ui_actions.cpp ----
bool itemIsSel(const App &a, const Item &it);
const Item *selectedItem(const App &a);
std::string padLabel(const MissionPadRef &r);
void selectItem(App &a, const Item &it);
void focusOn(App &a, const Vec3 &p, float dist = 450);
bool padFromHit(const App &a, const Hit &h, PadSpec &ps);
bool ensureMission(App &a);
void applyClick(App &a, const Hit &h);
void selectNewItem(App &a, const ItemRef &ref);
void copySelected(App &a);
void pasteAtMouse(App &a, const Vec3 &org, const Vec3 &dir);
void duplicateSelected(App &a, const Vec3 &org, const Vec3 &dir);
void deleteSelected(App &a);
void undoLast(App &a);
void viewCenterRay(const App &a, Vec3 &org, Vec3 &dir);      // the ray through the middle of the 3D view (menu Paste / Duplicate)
void reloadLevelGpu(App &a);
void requestLevel(App &a, const std::string &lvl, bool reexport);
void pollLevel(App &a);
void doPlay(App &a);
void doValidate(App &a);
void doSave(App &a);
void doSaveAs(App &a);
void doOpenPath(App &a, const std::string &path);
void doOpenDialog(App &a);
void doNewMission(App &a);                                    // the New mission action (File menu, Mission tab)
void doCapture(App &a);
void guarded(App &a, std::function<void()> fn);
std::string missionStartDir(const App &a);
std::string shortPath(const std::string &p, size_t n);
void loadPrefs(App &a);
void savePrefs(App &a);
void recentLoad(App &a);
void recentNote(App &a, const std::string &path);
void recentClear(App &a);
void recentList(App &a, bool menu);                           // the recent missions as menu items or selectables
bool captureThumb(App &a, const std::string &path, std::string &err);
void invalidateThumbs(App &a);
uint32_t thumbTexture(App &a, const std::string &path);
std::string exportCacheDir(const App &a);                     // the folder holding the cached level exports
void startReexportAll(App &a);

// ---- panels ----
void drawMenuBar(App &a);
void drawToolbar(App &a, float y, float h);
void drawScenePanel(App &a, float x, float y, float w, float h);
void drawRightPanel(App &a, float x, float y, float w, float h);   // the tab bar; the tabs are drawn by:
void drawSelectionTab(App &a);
void drawMissionTab(App &a);
void drawModTab(App &a);
void doSaveMod(App &a);
void doCheckMod(App &a);
void drawStatusBar(App &a, float x, float y, float w, float h);
void drawSettingsWindow(App &a);
void drawModals(App &a);
void drawHelpWindow(App &a);

// ---- ui_overlay.cpp ----
void drawOverlay(App &a, const Overlay &o);
GizmoGeo gizmoGeo(const App &a, const Overlay &ov, const TransformInfo &t);
int gizmoHit(const GizmoGeo &g, ImVec2 m);
void drawGizmo(App &a, const Overlay &ov, const GizmoGeo &g);
void dragRebase(App &a, const Vec3 &org, const Vec3 &dir);
void dragBegin(App &a, int hit, const Vec3 &org, const Vec3 &dir);
void dragEnd(App &a, bool confirm);
void dragUpdate(App &a, const Vec3 &org, const Vec3 &dir);
bool sunHit(const App &a, const Overlay &ov, ImVec2 m);
void sunUpdate(App &a, const Vec3 &org, const Vec3 &dir);
void drawSun(const App &a, const Overlay &ov);

// ---- ui_frame.cpp ----
void cameraInput(App &a, bool viewHovered);
void frameUi(App &a, int winW, int winH, bool &quit);

} // namespace ed
