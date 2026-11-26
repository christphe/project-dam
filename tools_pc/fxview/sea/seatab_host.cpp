/* seatab_host: a small ge_render host that runs the sea and sky tab (sea_tab.h) on its own, with a fly camera. A test host: fxview
 * hosts the same module with its ImGui panel; this one has the arguments only (no ImGui backend).
 *
 *   seatab_host [--backend gl|vk|deko3d] [--size WxH] [--frames N] [--shot out.ppm [--shotframe N]] [--log FILE] [tab arguments]
 *
 * The tab arguments are listed in sea_tab.h (--rom, --data, --ini, --mode, --level, --sea, --sky, --rep, --set "Name=value", ...).
 * PC: WASD / ZQSD move, Shift fast, Space or E up, Ctrl or Q down, hold the right mouse button to look, R / F load x2 / x1/2, M scale,
 * Esc quits. Switch (an NRO, deko3d): left stick moves, right stick looks, ZR / ZL up / down, B held fast, L / R load /2 x2,
 * Minus scale, Y sway, X freeze time, + quits; arguments come from sdmc:/switch/project-dam/fxview/seatab.args when it exists
 * (whitespace-separated, as on a command line), the ROM and the water ini files are in the game's data dir, sdmc:/switch/project-dam/data, and imagelist.u.csv (a repo file) next to the NRO. The GPU time
 * per second goes to the log (stderr, and the log file: sdmc:/switch/project-dam/fxview/seatab.log on the Switch). */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

#include "ge_render.h"
#include "ge_render_tool.h"
#include "sea_tab.h"

#ifdef __SWITCH__
#include <switch.h>
#include <sys/stat.h>
#define FXDIR "sdmc:/switch/project-dam/fxview/"
#else
#include <SDL.h>
#endif

#ifdef SEA_HAVE_PACK
extern "C" const unsigned char ge007_sea_pack[], ge007_sea_pack_end[];
#endif

static void dbg(const char* m);
static volatile int s_quit = 0;
static FILE* s_log = nullptr;

static void onLog(int level, const char* msg) {
    static const char* tags[] = { "ERROR", "WARN ", "NOTE ", "INFO ", "DEBUG" };
    dbg(msg);
    fprintf(stderr, "[%s] %s\n", tags[level < 0 ? 0 : level > 4 ? 4 : level], msg);
    fflush(stderr);
#ifdef __SWITCH__
    printf("[%s] %s\n", tags[level < 0 ? 0 : level > 4 ? 4 : level], msg);   /* stdout is nxlink's socket (stderr is the log file) */
    fflush(stdout);
#endif
    if (s_log) { fprintf(s_log, "[%s] %s\n", tags[level < 0 ? 0 : level > 4 ? 4 : level], msg); fflush(s_log); }
}
static void dbg(const char* m) {   // the emulator shows svcOutputDebugString as a guest log line; also a file (stderr goes nowhere)
#ifdef __SWITCH__
    svcOutputDebugString(m, strlen(m));
#else
    (void)m;
#endif
}
static void logf(const char* fmt, const char* a) {
    char tmp[512];
    snprintf(tmp, sizeof tmp, fmt, a);
    dbg(tmp);
    fprintf(stderr, fmt, a);
    fflush(stderr);
#ifdef __SWITCH__
    printf(fmt, a);
    fflush(stdout);
#endif
    if (s_log) { fprintf(s_log, fmt, a); fflush(s_log); }
}
static void onQuit(const char* why) { logf("quit: %s\n", why); s_quit = 1; }
static void onKey(int sym, int mod, int repeat) {
    (void)mod; (void)repeat;
    if (sym == 27 /* SDLK_ESCAPE */) s_quit = 1;
}

int main(int argc, char** argv) {
#ifdef __SWITCH__
    if (R_SUCCEEDED(socketInitializeDefault())) nxlinkStdio();   /* the log on nxlink's stdout too (a no-op without nxlink) */
    mkdir("sdmc:/switch/project-dam/fxview", 0777);
    freopen(FXDIR "seatab.log", "w", stderr);   /* the log file: the tab's own messages go to stderr */
#endif
    dbg("seatab_host: main");
    GeRenderConfig cfg = ge_render_cfg;
    int frames = 0, width = 1280, height = 720, shotFrame = 30;
    const char* shot = nullptr;
    const char* logPath = nullptr;
    bool noViewer = false;   /* run the game's own shaders (the tool pack not loaded) */
    /* an emulator's homebrew loader can pass argc == 0: argv + 1 .. argv + argc would be a huge range (std::length_error, terminate) */
    std::vector<std::string> args;
    for (int i = 1; i < argc; i++) args.push_back(argv[i]);
#ifdef __SWITCH__
    cfg.backend = GE_RENDER_DEKO3D;
    {   /* the argument file: an emulator passes no arguments */
        FILE* f = fopen(FXDIR "seatab.args", "r");
        if (f) {
            std::string cur;
            int c;
            bool quoted = false;
            while ((c = fgetc(f)) != EOF) {
                if (c == '"') { quoted = !quoted; continue; }
                if (!quoted && (c == ' ' || c == '\n' || c == '\r' || c == '\t')) { if (!cur.empty()) args.push_back(cur); cur.clear(); continue; }
                cur.push_back((char)c);
            }
            if (!cur.empty()) args.push_back(cur);
            fclose(f);
        }
    }
    for (const char* v : { "sdmc:/switch/project-dam/data", "--data", FXDIR "imagelist.u.csv", "--imagelist" }) args.insert(args.begin(), v);   /* the game's data dir (ROM, water ini files); the image list next to the NRO */
#else
    cfg.backend = GE_RENDER_VULKAN;
#endif
    std::vector<char*> av;
    av.push_back((char*)"seatab_host");
    for (std::string& a : args) av.push_back((char*)a.c_str());
    const int ac = (int)av.size();
    for (int i = 1; i < ac;) {
        if (seaTabParseArg(ac, av.data(), &i)) continue;
        const std::string a = av[i];
        const char* v = i + 1 < ac ? av[i + 1] : "";
        if (a == "--backend") { cfg.backend = !strcmp(v, "gl") ? GE_RENDER_GL : !strcmp(v, "vk") ? GE_RENDER_VULKAN : GE_RENDER_DEKO3D; i += 2; }
        else if (a == "--frames") { frames = atoi(v); i += 2; }
        else if (a == "--size") { sscanf(v, "%dx%d", &width, &height); i += 2; }
        else if (a == "--shot") { shot = v; i += 2; }
        else if (a == "--shotframe") { shotFrame = atoi(v); i += 2; }
        else if (a == "--log") { logPath = v; i += 2; }
        else if (a == "--noviewer") { noViewer = true; i++; }
        else { fprintf(stderr, "seatab_host: unknown argument %s\n", a.c_str()); i++; }
    }
    dbg("args parsed");
    if (logPath && !s_log) s_log = fopen(logPath, "w");
    cfg.window.title = "seatab_host";
    cfg.window.width = (uint32_t)width;
    cfg.window.height = (uint32_t)height;
    cfg.window.x = 100;
    cfg.window.y = 100;
    cfg.window.centered = true;
    cfg.fog_vertex = 1;   /* the water's fog is per vertex, as the plane's grid carries it */

    GeRenderCallbacks cb = {};
    cb.log = onLog;
    cb.on_quit = onQuit;
    cb.on_key_down = onKey;
    dbg("opening");
    ge_render_open(&cfg, &cb);
    dbg("opened");
    static const char* names[] = { "OpenGL", "Vulkan", "deko3d" };
    char line[256];
    snprintf(line, sizeof line, "seatab_host: backend %s, \"%s\"\n", names[ge_render_backend()], ge_render_renderer_string());
    logf("%s", line);

    const void* pack = nullptr;
    size_t packSize = 0;
#ifdef SEA_HAVE_PACK
    pack = ge007_sea_pack;
    packSize = (size_t)(ge007_sea_pack_end - ge007_sea_pack);
#endif
    if (noViewer) { pack = nullptr; packSize = 0; }
    struct GfxRenderingAPI* api = ge_render_api();
    uint32_t w = 0, h = 0;
    int32_t px = 0, py = 0;
    ge_render_get_dimensions(&w, &h, &px, &py);
    api->update_framebuffer_parameters(0, w, h, 1, false, true, true, true);   /* framebuffer 0 is the window */
    const bool ok = seaTabInit(pack, packSize);
    SeaTabCam cam = {};
    seaTabInitCam(&cam);
    if (!ok) { logf("%s", "seatab_host: no capture, nothing to show\n"); }

#ifdef __SWITCH__
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);
#endif
    uint64_t last = 0, lastLog = 0;
    for (int f = 0; !s_quit && (frames == 0 || f < frames); f++) {
#ifdef __SWITCH__
        const uint64_t now = armTicksToNs(armGetSystemTick());
#else
        const uint64_t now = SDL_GetTicks64() * 1000000ull;
#endif
        const float dt = last ? (float)((now - last) * 1e-9) : 1.0f / 60.0f;
        last = now;
        ge_render_poll_events();
        uint32_t nw = 0, nh = 0;
        ge_render_get_dimensions(&nw, &nh, &px, &py);
        if (nw == 0 || nh == 0) continue;
        if (nw != w || nh != h) {
            w = nw; h = nh;
            api->update_framebuffer_parameters(0, w, h, 1, false, true, true, true);
            api->on_resize();
        }
        /* the camera */
        float fwd = 0, right = 0, up = 0, fast = 1;
#ifdef __SWITCH__
        padUpdate(&pad);
        const u64 down = padGetButtonsDown(&pad), held = padGetButtons(&pad);
        const HidAnalogStickState l = padGetStickPos(&pad, 0), r = padGetStickPos(&pad, 1);
        if (held & HidNpadButton_B) fast = 10;
        auto dz = [](float v) { v /= 32767.0f; return v > -0.18f && v < 0.18f ? 0.0f : v; };   /* a dead zone: a worn stick drifts the camera */
        fwd = dz((float)l.y); right = dz((float)l.x);
        if (held & HidNpadButton_ZR) up += 0.5f;
        if (held & HidNpadButton_ZL) up -= 0.5f;
        seaTabLook(&cam, dz((float)r.x) * 1.5f * dt, -dz((float)r.y) * 1.5f * dt);
        if (down & HidNpadButton_R) seaTabSetRep(seaTabRep() * 2);
        if (down & HidNpadButton_L) seaTabSetRep(seaTabRep() / 2);
        if (down & HidNpadButton_Minus) seaTabCycleScale();
        if (down & HidNpadButton_Y) seaTabToggleSway();
        if (down & HidNpadButton_X) seaTabToggleFreeze();
        if (down & HidNpadButton_Plus) s_quit = 1;
#else
        const Uint8* k = SDL_GetKeyboardState(nullptr);
        if (k[SDL_SCANCODE_LSHIFT] || k[SDL_SCANCODE_RSHIFT]) fast = 10;
        if (k[SDL_SCANCODE_W]) fwd += 1;
        if (k[SDL_SCANCODE_S]) fwd -= 1;
        if (k[SDL_SCANCODE_D]) right += 1;
        if (k[SDL_SCANCODE_A]) right -= 1;
        if (k[SDL_SCANCODE_SPACE] || k[SDL_SCANCODE_E]) up += 0.5f;
        if (k[SDL_SCANCODE_LCTRL] || k[SDL_SCANCODE_Q]) up -= 0.5f;
        int mx = 0, my = 0;
        const Uint32 mb = SDL_GetRelativeMouseState(&mx, &my);
        if (mb & SDL_BUTTON_RMASK) seaTabLook(&cam, mx * 0.0025f, my * 0.0025f);
        static bool rDown = false, fDown = false, mDown = false;
        if (k[SDL_SCANCODE_R] && !rDown) seaTabSetRep(seaTabRep() * 2);
        if (k[SDL_SCANCODE_F] && !fDown) seaTabSetRep(seaTabRep() / 2);
        if (k[SDL_SCANCODE_M] && !mDown) seaTabCycleScale();
        rDown = k[SDL_SCANCODE_R]; fDown = k[SDL_SCANCODE_F]; mDown = k[SDL_SCANCODE_M];
#endif
        const float speed = dt * 60.0f * fast;
        seaTabMove(&cam, fwd * speed, right * speed, up * speed);

        ge_render_begin_frame();
        api->start_draw_to_framebuffer(0, 1.0f);
        api->set_viewport(0, 0, (int)w, (int)h);
        api->set_scissor(0, 0, (int)w, (int)h);
        api->clear_framebuffer(true, true);
        const int vp[4] = { 0, 0, (int)w, (int)h };
        if (ok) seaTabFrame(dt, &cam, vp);
        ge_render_end_frame();
        if (shot && f == shotFrame) ge_render_dump_window(w, h, shot);
        ge_render_present();
        if (ok && now - lastLog > 1000000000ull) {
            lastLog = now;
            snprintf(line, sizeof line, "%s\n", seaTabStatus());
            logf("%s", line);
        }
        if (shot && f == shotFrame + 3) break;   /* the Vulkan dump is written by the present of its frame */
    }

    seaTabShutdown();
    ge_render_park();
    ge_render_close();
    if (s_log) fclose(s_log);
#ifdef __SWITCH__
    socketExit();
#endif
    return 0;
}
