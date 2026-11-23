// ge-editor: the GoldenEye co-op mission editor (issue #102).
//
//   ge-editor [--export DIR] [--mission FILE] [--port N | --no-mcp] [--game EXE]
//   ge-editor --mcp-stdio [--export DIR] [--mission FILE]     headless MCP over stdin/stdout
//
// GUI: SDL2 window, Dear ImGui panels around a 3D view of the level export replayed through
// fast3d's OpenGL backend (scene.cpp). Every edit goes through Doc (doc.h), as the MCP tools do.
// The panels live in ui_*.cpp (ui_app.h lists them); this file has main(), the command line and the test aids.
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
#include "png.h"
#include "scene.h"
#include "view.h"
#include "ui_app.h"

extern "C" void editorStubsSetArgs(int argc, char **argv);   // fast3d_stubs.cpp

using namespace ed;

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
    bool portGiven = false;               // --port was on the command line (else the saved setting is used)
    std::string openMenu, openWindow, openTab;   // test aids: --open-menu File, --open-window settings|help|log, --open-tab selection|mission|mod
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
         "    --open-menu NAME                  test aid: open a menu of the menu bar (File, Edit, View, Mission, Mod, Tools, Help)\n"
         "    --open-window NAME                test aid: open the settings, help or log window / panel\n"
         "    --open-tab NAME                   test aid: select the right panel tab (selection, mission, mod)\n"
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
        else if (s == "--port" && i + 1 < argc) { o.port = atoi(argv[++i]); o.portGiven = true; }
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
        } else if (s == "--open-menu") { next(o.openMenu);
        } else if (s == "--open-window") { next(o.openWindow);
        } else if (s == "--open-tab") { next(o.openTab);
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
    app.prefsFile = exeDir + "ge-editor-prefs.txt";
    app.recentFile = exeDir + "ge-editor-recent.txt";
    loadPrefs(app);
    recentLoad(app);
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
    app.scene.setOffscreen(!opt.noOffscreen && app.prefOffscreen);

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

    app.doc.captureFn = [&app](const std::string &path, std::string &e) { return captureThumb(app, path, e); };
    // thumb.png next to the mission, by default
    if (!app.doc.missionPath.empty()) {
        std::string mp = app.doc.missionPath;
        size_t s = mp.find_last_of("/\\");
        snprintf(app.thumbPath, sizeof app.thumbPath, "%s", ((s == std::string::npos ? std::string() : mp.substr(0, s + 1)) + "thumb.png").c_str());
    }

    if (!opt.noMcp && app.prefMcp) {
        if (app.http.start(opt.portGiven ? opt.port : app.prefMcpPort, runOnMain, app.doc, err)) {
            app.mcpPort = app.http.port();
            app.say("MCP: claude mcp add --transport http ge-editor http://127.0.0.1:%d/mcp", app.mcpPort);
        } else app.say("MCP HTTP server: %s", err.c_str());
    }

    bool quit = false;
    int frame = 0;
    app.logCam = opt.logCam;
    if (opt.openWindow == "settings") app.showSettings = true;
    else if (opt.openWindow == "help") app.showHelp = true;
    else if (opt.openWindow == "log") app.showLog = true;
    app.forceTab = opt.openTab == "selection" ? 0 : opt.openTab == "mission" ? 1 : opt.openTab == "mod" ? 2 : -1;
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
        if (!opt.openMenu.empty() && frame >= 5) {   // test aid: click the title of a menu, then keep the cursor on it
            auto mp = app.menuPos.find(opt.openMenu);
            if (mp != app.menuPos.end()) ImGui::GetIO().AddMousePosEvent(mp->second.x, mp->second.y);
            if (frame == 7) ImGui::GetIO().AddMouseButtonEvent(0, true);
            if (frame == 8) ImGui::GetIO().AddMouseButtonEvent(0, false);
        }
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
