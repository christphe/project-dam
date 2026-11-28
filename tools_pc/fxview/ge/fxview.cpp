// fxview on ge_render (issue #144): the combat effects drawn through the real backends (deko3d on the Switch, Vulkan or GL on PC),
// the way the game draws them: the game's combiner ids, the PORTFX kind in the id (SHADER_OPT_FX), the pack's effect shader, the
// Lights block. ORIGINAL = the combiner without the kind (what the game drew before the effect shaders), EFFECT = with it.
// The same arguments on PC and Switch; the ImGui panel is ge_imgui (ge_render's ImGui backend, pad navigation on the Switch).
//
//   --mode sprite|decal|cluster     sprite (default): one effect large, original vs effect; decal: the game's bullet holes vs the
//                                   port's cluster on a lit wall; cluster: the procedural #141 repro scene
//   sprite: --kind tracer|laser|watchlaser|wallpuff|bloodpuff|scatter|impact  --impact 0..19  --age TICKS  --angle RAD
//           --tracerlen F  --view side|split|orig|fx  --split 0..1  --backdrop grey|dark|checker|wall  --bgtex N  --zoom F
//           --fc N (frame_count)  --nearest 0|1  --seed N
//   decal:  --mat stone|tile|default|wood|metal|metalobj|dirt|mud|snow|glass  --dtype 0..19  --holes N  --yaw DEG --pitch DEG
//           --dzoom F  --walltex N  --texel F  --wallshade F  --sun 0|1 --sunaz DEG --sunel DEG  --pt 0|1 --ptx --pty --pth --ptr
//   cluster: --dist N --holes N --cmat 0..6 --holesize F --seed N --fov DEG --sun 0|1 --diag coverage|veye|uv|pom|pomalpha|deriv
//   common: --backend gl|vk|deko3d  --w N --h N  --frames N  --ui 0|1  --out DIR  --rom FILE.z64  --shaders DIR
//           textures: --pack ge007.passets or a folder holding it (default: assetpacks/ beside the game, else written from the ROM into --out)  --texpack NAME|PATH  --mod PATH
//           --probeframe N (default 12)  --probe X,Y (repeat)  --dump FILE.ppm  --shot FILE.png  --hold N  --selftest
//
// --probe X,Y (window pixels, top-left origin): "PROBE x y r g b a" in the log. --dump / --shot / --probe take frame 12 (the frame
// count settles first) and quit after it (--hold N: N more frames, for a window screenshot); --frames N quits after N frames;
// without them the viewer runs until Esc / window close / Start on the Switch. Arguments on the Switch: the nxlink argv, else the
// file <out>/fxview.args (one line of the same arguments). Logs and shots: sdmc:/switch/project-dam/fxview/ on the Switch.
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <map>
#include <string>
#include <vector>
#include <chrono>
#include <sys/stat.h>

#ifdef __SWITCH__
#include <switch.h>
#endif

#include "imgui.h"
#include "ge_render.h"
#include "ge_imgui.h"
#include "fxcluster.h"
#include "ge_render_tool.h"   // GPU timers (--bench)
#include "portfx.h"
#include "fx_rom.h"
#include "fx_tex.h"
extern "C" {
#include "ge_png.h"
}
#ifndef __SWITCH__
#include <SDL.h>   // the sea mode's keyboard camera
#endif
#include "sea_tab.h"   // --mode sea|water|sky: the GE+ sky, sea and water (tools_pc/fxview/sea, #145)
#ifdef SEA_HAVE_PACK
extern "C" const unsigned char ge007_sea_pack[], ge007_sea_pack_end[];   // the sky/water VIEWER variants (sea_pack.S)
#endif
#ifndef __SWITCH__
#include "fx_reload.h"
#endif

static FILE *g_log = nullptr;
static void logf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void logf(const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    fprintf(stdout, "%s\n", buf);
    fflush(stdout);
    if (g_log) { fprintf(g_log, "%s\n", buf); fflush(g_log); }
}
static void onLog(int, const char *msg) { logf("%s", msg); }
static volatile int g_quit = 0;
static float g_wheel = 0.0f;
static int g_handheld = 0;   // deko3d sizes its swapchain by this: 1280x720 handheld, 1920x1080 docked (--w <= 1280 asks for the first)
static int onHandheld() { return g_handheld; }
static bool g_f12 = false;
static void onQuit(const char *) { g_quit = 1; }
static void onKey(int sym, int, int) { if (sym == 27) g_quit = 1; if (sym == 1073741893) g_f12 = true; }   // SDLK_F12
static void onWheel(int y) { g_wheel += (float)y; }

static void makeDir(const std::string &p) {
#ifdef _WIN32
    mkdir(p.c_str());
#else
    mkdir(p.c_str(), 0777);
#endif
}

// ---------------------------------------------------------------- arguments
struct Args {
    std::string mode = "sprite", scene, backend, out, diag, rom, pack, texpack, mod, shaders, kind = "wallpuff", view = "side", backdrop = "grey", mat = "stone", dump, shot;
    float dist = 120.0f, holesize = 6.0f, fov = 60.0f, age = 0, angle = 0, tracerlen = 8.0f, zoom = 1.0f, split = 0.5f, dzoom = 1.0f;
    float yaw = 0, pitch = 0, texel = 2.0f, wallshade = 0.6f, sunaz = 135, sunel = 35, ptx = 10, pty = -6, pth = 70, ptr = 260;
    int probeFrame = 12, hold = 0, holes = 3, cmat = 0, seed = 1, w = 1280, h = 720, frames = 0, ui = -1, sun = -1, pt = 1, impact = 0, bgtex = 20, fc = 120, nearest = 0;
    int dtype = -1, walltex = -1, selftest = 0, page = 0;
    float panx = 0, pany = 0;   // decal: middle drag pans the camera across the wall (world units, along the view's right / up)
    int bench = 0, rep = 1;   // cluster: --bench 1 times fxClusterPom on the GPU (full shader vs coverage only), --rep N draws the cluster N times
    // the tabs (fx_tabs.inc): muzzle, tracer, burst, explosion; --t SECONDS freezes the time
    std::string tshow = "side", gun, tritem = "gun", burst = "stone";
    float t = -1, tspeed = 1, tyaw = 0, tpitch = 0, tzoom = 1, mrate = 6, mscale = 0.1f, trrange = 1500, trperiod = 0.25f, bperiod = 1.2f, etail = 0.5f;
    int twall = 1, twalltex = 286, tseed = 1, mvis = 2, explo = 13, play = 1;
    std::vector<std::pair<int, int>> probes;
};

// The options as a table (--name value): the command line and the .ini go through it. persist = saved in the .ini (not the run's own: out, rom, shots).
struct Opt { const char *name; char type; void *p; bool persist; };
static std::vector<Opt> optTable(Args &a) {
    return {
        { "mode", 'S', &a.mode, true }, { "scene", 'S', &a.scene, false }, { "backend", 'S', &a.backend, false }, { "out", 'S', &a.out, false }, { "diag", 'S', &a.diag, false },
        { "rom", 'S', &a.rom, false }, { "pack", 'S', &a.pack, false }, { "texpack", 'S', &a.texpack, true }, { "mod", 'S', &a.mod, false }, { "shaders", 'S', &a.shaders, false }, { "kind", 'S', &a.kind, true }, { "view", 'S', &a.view, true }, { "backdrop", 'S', &a.backdrop, true },
        { "mat", 'S', &a.mat, true }, { "dump", 'S', &a.dump, false }, { "shot", 'S', &a.shot, false },
        { "tshow", 'S', &a.tshow, true }, { "gun", 'S', &a.gun, true }, { "tritem", 'S', &a.tritem, true }, { "burst", 'S', &a.burst, true },
        { "dist", 'F', &a.dist, true }, { "holesize", 'F', &a.holesize, true }, { "fov", 'F', &a.fov, true }, { "age", 'F', &a.age, true }, { "angle", 'F', &a.angle, true },
        { "tracerlen", 'F', &a.tracerlen, true }, { "zoom", 'F', &a.zoom, true }, { "split", 'F', &a.split, true }, { "dzoom", 'F', &a.dzoom, true }, { "yaw", 'F', &a.yaw, true },
        { "pitch", 'F', &a.pitch, true }, { "texel", 'F', &a.texel, true }, { "wallshade", 'F', &a.wallshade, true }, { "sunaz", 'F', &a.sunaz, true }, { "sunel", 'F', &a.sunel, true },
        { "ptx", 'F', &a.ptx, true }, { "pty", 'F', &a.pty, true }, { "pth", 'F', &a.pth, true }, { "ptr", 'F', &a.ptr, true },
        { "t", 'F', &a.t, false }, { "tspeed", 'F', &a.tspeed, true }, { "tyaw", 'F', &a.tyaw, true }, { "tpitch", 'F', &a.tpitch, true }, { "tzoom", 'F', &a.tzoom, true },
        { "mrate", 'F', &a.mrate, true }, { "mscale", 'F', &a.mscale, true }, { "trrange", 'F', &a.trrange, true }, { "trperiod", 'F', &a.trperiod, true },
        { "bperiod", 'F', &a.bperiod, true }, { "etail", 'F', &a.etail, true },
        { "hold", 'I', &a.hold, false }, { "probeframe", 'I', &a.probeFrame, false }, { "holes", 'I', &a.holes, true }, { "cmat", 'I', &a.cmat, true }, { "seed", 'I', &a.seed, true },
        { "w", 'I', &a.w, false }, { "h", 'I', &a.h, false }, { "frames", 'I', &a.frames, false }, { "ui", 'I', &a.ui, false }, { "sun", 'I', &a.sun, true }, { "pt", 'I', &a.pt, true },
        { "impact", 'I', &a.impact, true }, { "bgtex", 'I', &a.bgtex, true }, { "fc", 'I', &a.fc, true }, { "nearest", 'I', &a.nearest, true }, { "dtype", 'I', &a.dtype, true },
        { "walltex", 'I', &a.walltex, true }, { "page", 'I', &a.page, true }, { "twall", 'I', &a.twall, true }, { "twalltex", 'I', &a.twalltex, true }, { "tseed", 'I', &a.tseed, true },
        { "mvis", 'I', &a.mvis, true }, { "explo", 'I', &a.explo, true }, { "play", 'I', &a.play, false },
        { "bench", 'I', &a.bench, false }, { "rep", 'I', &a.rep, false }
    };
}
static bool setOpt(Args &a, const std::string &name, const std::string &val) {
    for (const Opt &o : optTable(a)) {
        if (name != o.name) continue;
        if (o.type == 'S') *(std::string *)o.p = val;
        else if (o.type == 'F') *(float *)o.p = (float)atof(val.c_str());
        else *(int *)o.p = atoi(val.c_str());
        return true;
    }
    return false;
}

// One setting from the command line or the .ini. The sky/sea mode's settings go to its module: its own keys (level, rep, scale, ...),
// and the three keys fxview also has when the value is the mode's (mode sea|water|sky, view overview|pier|..., scene 0|1).
static bool setSetting(Args &a, const std::string &name, const std::string &val) {
    if (name == "mode" && seaTabSetting("mode", val.c_str())) { a.mode = "sea"; return true; }
    if ((name == "view" || name == "scene") && seaTabSetting(name.c_str(), val.c_str())) return true;
    if (setOpt(a, name, val)) return true;
    return seaTabSetting(name.c_str(), val.c_str());
}

static void parseArgs(Args &a, const std::vector<std::string> &v) {
    for (size_t i = 0; i < v.size(); i++) {
        const std::string &k = v[i];
        const bool has = i + 1 < v.size();
        if (k == "--selftest") a.selftest = 1;
        else if (k == "--probe" && has) { int x = 0, y = 0; if (sscanf(v[++i].c_str(), "%d,%d", &x, &y) == 2) a.probes.push_back({ x, y }); }
        else if (k.size() > 2 && k[0] == '-' && k[1] == '-' && has && setSetting(a, k.substr(2), v[i + 1])) i++;
    }
    if (a.scene == "cluster") a.mode = "cluster";
}

// ---------------------------------------------------------------- the .ini (settings kept between runs, as the older viewer's data/fxview.ini)
static void saveIni(Args &a, const std::string &path) {
    FILE *f = fopen(path.c_str(), "w");
    if (!f) return;
    for (const Opt &o : optTable(a)) {
        if (!o.persist) continue;
        if (o.type == 'S') fprintf(f, "%s=%s\n", o.name, ((std::string *)o.p)->c_str());
        else if (o.type == 'F') fprintf(f, "%s=%g\n", o.name, *(float *)o.p);
        else fprintf(f, "%s=%d\n", o.name, *(int *)o.p);
    }
    seaTabWriteSettings(f);   // the sky/sea mode's settings: sea.<key>=value
    fclose(f);
}
static void loadIni(Args &a, const std::string &path) {
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) return;
    char line[512];
    while (fgets(line, sizeof line, f)) {
        std::string s = line;
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
        if (s.compare(0, 4, "sea.") == 0) {   // the sky/sea mode: a name may hold '=' (Bake from (0 = no bake)): the value is after the last one
            const size_t q = s.rfind('=');
            if (q != std::string::npos && q > 4) seaTabSetting(s.substr(4, q - 4).c_str(), s.substr(q + 1).c_str());
            continue;
        }
        const size_t q = s.find('=');
        if (q != std::string::npos) setOpt(a, s.substr(0, q), s.substr(q + 1));
    }
    fclose(f);
}

// ---------------------------------------------------------------- 4x4 (row vectors: clip = (x y z 1) * M)
static bool invert4(const float m[16], float inv[16]) {
    float t[16];
    t[0] = m[5]*m[10]*m[15]-m[5]*m[11]*m[14]-m[9]*m[6]*m[15]+m[9]*m[7]*m[14]+m[13]*m[6]*m[11]-m[13]*m[7]*m[10];
    t[4] = -m[4]*m[10]*m[15]+m[4]*m[11]*m[14]+m[8]*m[6]*m[15]-m[8]*m[7]*m[14]-m[12]*m[6]*m[11]+m[12]*m[7]*m[10];
    t[8] = m[4]*m[9]*m[15]-m[4]*m[11]*m[13]-m[8]*m[5]*m[15]+m[8]*m[7]*m[13]+m[12]*m[5]*m[11]-m[12]*m[7]*m[9];
    t[12] = -m[4]*m[9]*m[14]+m[4]*m[10]*m[13]+m[8]*m[5]*m[14]-m[8]*m[6]*m[13]-m[12]*m[5]*m[10]+m[12]*m[6]*m[9];
    t[1] = -m[1]*m[10]*m[15]+m[1]*m[11]*m[14]+m[9]*m[2]*m[15]-m[9]*m[3]*m[14]-m[13]*m[2]*m[11]+m[13]*m[3]*m[10];
    t[5] = m[0]*m[10]*m[15]-m[0]*m[11]*m[14]-m[8]*m[2]*m[15]+m[8]*m[3]*m[14]+m[12]*m[2]*m[11]-m[12]*m[3]*m[10];
    t[9] = -m[0]*m[9]*m[15]+m[0]*m[11]*m[13]+m[8]*m[1]*m[15]-m[8]*m[3]*m[13]-m[12]*m[1]*m[11]+m[12]*m[3]*m[9];
    t[13] = m[0]*m[9]*m[14]-m[0]*m[10]*m[13]-m[8]*m[1]*m[14]+m[8]*m[2]*m[13]+m[12]*m[1]*m[10]-m[12]*m[2]*m[9];
    t[2] = m[1]*m[6]*m[15]-m[1]*m[7]*m[14]-m[5]*m[2]*m[15]+m[5]*m[3]*m[14]+m[13]*m[2]*m[7]-m[13]*m[3]*m[6];
    t[6] = -m[0]*m[6]*m[15]+m[0]*m[7]*m[14]+m[4]*m[2]*m[15]-m[4]*m[3]*m[14]-m[12]*m[2]*m[7]+m[12]*m[3]*m[6];
    t[10] = m[0]*m[5]*m[15]-m[0]*m[7]*m[13]-m[4]*m[1]*m[15]+m[4]*m[3]*m[13]+m[12]*m[1]*m[7]-m[12]*m[3]*m[5];
    t[14] = -m[0]*m[5]*m[14]+m[0]*m[6]*m[13]+m[4]*m[1]*m[14]-m[4]*m[2]*m[13]-m[12]*m[1]*m[6]+m[12]*m[2]*m[5];
    t[3] = -m[1]*m[6]*m[11]+m[1]*m[7]*m[10]+m[5]*m[2]*m[11]-m[5]*m[3]*m[10]-m[9]*m[2]*m[7]+m[9]*m[3]*m[6];
    t[7] = m[0]*m[6]*m[11]-m[0]*m[7]*m[10]-m[4]*m[2]*m[11]+m[4]*m[3]*m[10]+m[8]*m[2]*m[7]-m[8]*m[3]*m[6];
    t[11] = -m[0]*m[5]*m[11]+m[0]*m[7]*m[9]+m[4]*m[1]*m[11]-m[4]*m[3]*m[9]-m[8]*m[1]*m[7]+m[8]*m[3]*m[5];
    t[15] = m[0]*m[5]*m[10]-m[0]*m[6]*m[9]-m[4]*m[1]*m[10]+m[4]*m[2]*m[9]+m[8]*m[1]*m[6]-m[8]*m[2]*m[5];
    const float det = m[0]*t[0] + m[1]*t[4] + m[2]*t[8] + m[3]*t[12];
    if (fabsf(det) < 1e-30f) return false;
    for (int i = 0; i < 16; i++) inv[i] = t[i] / det;
    return true;
}

// ---------------------------------------------------------------- the camera, as the game's lights see it (gfx_dynlights_sync)
struct Cam { float P[16], A[16], T[16], inv[16]; bool ok; };
static GfxRenderingAPI *api() { return ge_render_api(); }

static Cam makeCam(float fovDeg, float aspect, float farZ = 4000.0f) {
    Cam c = {};
    const float n = 1.0f, f = farZ, t = 1.0f / tanf(fovDeg * 0.5f * 3.14159265f / 180.0f);
    c.P[0] = t / aspect; c.P[5] = t; c.P[10] = (f + n) / (n - f); c.P[11] = -1.0f; c.P[14] = 2.0f * f * n / (n - f);
    const GfxClipParameters cp = api()->get_clip_parameters();
    c.A[0] = 1.0f; c.A[5] = cp.invert_y ? -1.0f : 1.0f;
    c.A[10] = cp.z_is_from_0_to_1 ? 0.5f : 1.0f; c.A[14] = cp.z_is_from_0_to_1 ? 0.5f : 0.0f; c.A[15] = 1.0f;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            c.T[i * 4 + j] = c.P[i * 4 + 0] * c.A[0 * 4 + j] + c.P[i * 4 + 1] * c.A[1 * 4 + j] + c.P[i * 4 + 2] * c.A[2 * 4 + j] + c.P[i * 4 + 3] * c.A[3 * 4 + j];
    c.ok = invert4(c.T, c.inv);
    return c;
}
static void clipOf(const Cam &c, const float e[3], float o[4]) {
    for (int j = 0; j < 4; j++) o[j] = e[0] * c.T[j] + e[1] * c.T[4 + j] + e[2] * c.T[8 + j] + c.T[12 + j];
}

// ---------------------------------------------------------------- drawing through the draw API
static int g_fc = 120;   // frame_count given to the effect shaders
static ShaderProgram *program(uint64_t id0, uint32_t id1) {
    ShaderProgram *p = api()->lookup_shader(id0, id1);
    if (!p) {
        p = api()->create_and_load_new_shader(id0, id1);
#ifndef __SWITCH__
        fxReloadApply(id0, id1);
#endif
    }
    api()->load_shader_with(p, (uint32_t)g_fc, 1.0f, false, false);
    return p;
}
static void draw(const std::vector<float> &v, size_t floatsPerVtx) {
    const size_t per = 255 * 3 * floatsPerVtx;
    for (size_t at = 0; at < v.size(); at += per) {
        const size_t n = v.size() - at < per ? v.size() - at : per;
        api()->draw_triangles(const_cast<float *>(&v[at]), n, n / (3 * floatsPerVtx));
    }
}
static uint32_t makeTex(const unsigned char *rgba, int w, int h, bool mip = false) {
    const uint32_t id = api()->new_texture();
    api()->select_texture(0, id, true);
    api()->upload_texture(rgba, (uint32_t)w, (uint32_t)h, mip);
    api()->set_sampler_parameters(0, true, 2, 2, false);
    return id;
}
static void bindTex(int unit, uint32_t id, bool linear, int cms, int cmt, bool mip = false) {
    api()->select_texture(unit, id, linear);
    api()->set_sampler_parameters(unit, linear, (uint32_t)cms, (uint32_t)cmt, mip);
}

// The textures: a pack texture decoded once (key: number + mip), a 1x1 white, the checker.
struct Tex { uint32_t id = 0; int w = 0, h = 0; std::vector<uint8_t> rgba; };
static std::map<int, Tex> g_tex;
static uint32_t g_white = 0, g_checker = 0;
static void makeBuiltinTextures() {
    const unsigned char white[4] = { 255, 255, 255, 255 };
    g_white = makeTex(white, 1, 1);
    const unsigned char ck[16] = { 150, 150, 150, 255, 100, 100, 100, 255, 100, 100, 100, 255, 150, 150, 150, 255 };
    g_checker = makeTex(ck, 2, 2);
}
static const Tex &romTex(int tn, bool mip) {
    const int key = tn * 2 + (mip ? 1 : 0);
    auto it = g_tex.find(key);
    if (it != g_tex.end()) return it->second;
    Tex t;
    FxTexImage img;
    if (fxTexture(tn, img) && img.w > 0) {
        t.w = img.w; t.h = img.h; t.rgba = img.rgba;
        t.id = makeTex(img.rgba.data(), img.w, img.h, mip);
    } else {
        logf("texture %d not decoded: %s", tn, img.note.c_str());
    }
    return g_tex.emplace(key, t).first->second;
}

// The game's combiners (port/shaders/fx/combiners.txt): TEXEL0 x SHADE with alpha (MODULATEIA), the kind in id1 bits 28..31.
static const uint64_t CC_TEX = 0x01080108ull;
static uint32_t idFor(int kind) { return 1u | ((uint32_t)kind << 28); }   // SHADER_OPT_ALPHA | PORTFX kind

// A quad (two triangles) in clip space with a texture and a vertex colour, in a pane's viewport.
static void quadTex(const float x0, const float y0, const float x1, const float y1, float u0, float v0, float u1, float v1, const float c[4], int kind) {
    program(CC_TEX, idFor(kind));
    const float p[4][4] = { { x0, y0, u0, v0 }, { x1, y0, u1, v0 }, { x1, y1, u1, v1 }, { x0, y1, u0, v1 } };
    static const int idx[6] = { 0, 1, 2, 0, 2, 3 };
    std::vector<float> v;
    for (int i : idx) v.insert(v.end(), { p[i][0], p[i][1], 0.0f, 1.0f, p[i][2], p[i][3], c[0], c[1], c[2], c[3] });
    draw(v, 10);
}
static void quadFlat(float x0, float y0, float x1, float y1, const float c[4]) {
    program(ge_render_cc_id_shade(), 1);
    const float p[4][2] = { { x0, y0 }, { x1, y0 }, { x1, y1 }, { x0, y1 } };
    static const int idx[6] = { 0, 1, 2, 0, 2, 3 };
    std::vector<float> v;
    for (int i : idx) v.insert(v.end(), { p[i][0], p[i][1], 0.0f, 1.0f, c[0], c[1], c[2], c[3] });
    draw(v, 8);
}

// ---------------------------------------------------------------- what is shown: sprites (the game's, from the ROM)
enum { K_TRACER = 0, K_LASER, K_WATCHLASER, K_WALLPUFF, K_BLOODPUFF, K_SCATTER, K_IMPACT, K_COUNT };
static const char *kKindKey[K_COUNT] = { "tracer", "laser", "watchlaser", "wallpuff", "bloodpuff", "scatter", "impact" };
static const char *kKindName[K_COUNT] = { "Tracer (guns, flareimage3)", "Laser tracer (flareimage4)", "Watch laser (flareimage4)", "Wall puff (smoke, type 1)",
                                          "Blood puff (smoke, type 2)", "Scatter puff (type 3)", "Bullet-hole decal (impactimages)" };
static int fxKindOf(int k) { return k <= K_WATCHLASER ? PORTFX_TRACER : k == K_BLOODPUFF ? PORTFX_BLOOD : k == K_IMPACT ? PORTFX_IMPACT : PORTFX_SPARK; }
static int kindIndex(const std::string &s) { for (int i = 0; i < K_COUNT; i++) if (s == kKindKey[i]) return i; return K_WALLPUFF; }
static const uint8_t kBlood[3] = { 0x8C, 0x0A, 0x0A };   // port/src/switch/nxgame.c BLOOD_R/G/B

static void puffParams(int k, const char **table, int *lifetime, float *step) {   // bullet_sparks_init
    if (k == K_SCATTER) { *table = "scattered_explosions"; *lifetime = 9; *step = 0.5f; }
    else { *table = "explosion_smokeimages"; *lifetime = 11; *step = 0.5f; }
}
static int puffFrame(float age, float step, int nframes) { int f = (int)(floorf(age) * step); return f < 0 ? 0 : f >= nframes ? nframes - 1 : f; }

struct Vert { float x, y, u, v, r, g, b, a; };
struct Sprite { std::vector<Vert> v; std::vector<int> idx; const ImgEntry *img = nullptr; std::string label; };

static unsigned lcg(unsigned &s) { s = s * 1103515245u + 12345u; return (s >> 16) & 0x7FFF; }
// src/game/explosion.c g_ImpactTypes[]: half width, half height (world units), apptype (the vertex colour scheme)
struct ImpactType { float w, h; int app; };
static const ImpactType kImpactTypes[20] = { {10,10,1},{6,6,1},{8,8,0},{20,20,1},{6,6,1},{8,8,1},{12,12,1},{6,6,1},{20,20,1},{20,20,1},
                                             {20,20,1},{20,20,1},{20,20,1},{24,24,1},{6,6,1},{6,6,1},{24,24,2},{6,6,1},{8,8,1},{12,12,1} };
static void impactVertexColour(int app, unsigned &seed, float c[3]) {   // explosion.c: light 216..255 grey (1), dark 0..39 (0), random primaries (2)
    if (app == 1) { const float v = (255 - lcg(seed) % 40) / 255.0f; c[0] = c[1] = c[2] = v; }
    else if (app == 0) { const float v = (lcg(seed) % 40) / 255.0f; c[0] = c[1] = c[2] = v; }
    else { c[0] = (lcg(seed) & 1) ? 1.0f : 0.0f; c[2] = (lcg(seed) & 1) ? 1.0f : 0.0f; c[1] = (lcg(seed) & 1) ? 1.0f : 0.0f; }
}

static Sprite buildSprite(const Args &a, int kind) {   // the sprite in game units (x right, y up), the game's corner order and texture coordinates
    Sprite s;
    char lab[200];
    if (kind <= K_WATCHLASER) {   // gunfire.c sub_GAME_7F061E18: a quad from the muzzle (+-right) to the far end (+-0.9 right)
        s.img = fxTableEntry(kind == K_TRACER ? "flareimage3" : "flareimage4", 0);
        const float radius = kind == K_TRACER ? 30.0f : kind == K_LASER ? 50.0f : 10.0f;
        const float L = 2.0f * radius * a.tracerlen;
        const float al = kind == K_WATCHLASER ? 175.0f / 255.0f : 1.0f;   // watch laser: alpha 150..199 at random
        s.v = { { 0, radius, 1, 0, 1, 1, 1, al }, { 0, -radius, 0, 0, 1, 1, 1, al }, { L, 0.9f * radius, 1, 1, 1, 1, 1, al }, { L, -0.9f * radius, 0, 1, 1, 1, 1, al } };
        s.idx = { 0, 2, 3, 0, 3, 1 };
        snprintf(lab, sizeof lab, "%s  tex %u  %dx%d  radius %.0f", kKindName[kind], s.img ? s.img->index : 0, s.img ? s.img->w : 0, s.img ? s.img->h : 0, radius);
    } else if (kind == K_IMPACT) {   // explosion.c: a decal quad, vertex colours by g_ImpactTypes[].apptype
        const int t = a.impact < 0 ? 0 : a.impact > 19 ? 19 : a.impact;
        s.img = fxTableEntry("impactimages", t);
        unsigned seed = (unsigned)a.seed * 7919u + (unsigned)t;
        const float w = kImpactTypes[t].w, h = kImpactTypes[t].h;
        const float pos[4][2] = { { -w, -h }, { -w, h }, { w, h }, { w, -h } }, uv[4][2] = { { 0, 1 }, { 0, 0 }, { 1, 0 }, { 1, 1 } };
        for (int i = 0; i < 4; i++) {
            float c[3];
            impactVertexColour(kImpactTypes[t].app, seed, c);
            s.v.push_back({ pos[i][0], pos[i][1], uv[i][0], uv[i][1], c[0], c[1], c[2], 1.0f });
        }
        s.idx = { 0, 1, 2, 0, 2, 3 };
        snprintf(lab, sizeof lab, "Bullet-hole decal type %d  tex %u  %dx%d  size %.0fx%.0f", t, s.img ? s.img->index : 0, s.img ? s.img->w : 0, s.img ? s.img->h : 0, w, h);
    } else {   // glass2.c bullet_spark_render: a diamond turned by the spark's random angle, frame = age * step
        const char *table; int life; float step;
        puffParams(kind, &table, &life, &step);
        const int nf = std::max(1, fxTableCount(table));
        const int fr = puffFrame(a.age, step, nf);
        s.img = fxTableEntry(table, fr);
        float col[4] = { 1, 1, 1, 1 };
        if (kind == K_WALLPUFF) col[2] = 0xC8 / 255.0f;   // g_BulletSparkColors[1]
        else if (kind == K_BLOODPUFF) for (int k = 0; k < 3; k++) col[k] = kBlood[k] / 255.0f;
        const float cs = cosf(a.angle), sn = sinf(a.angle);
        const float p[4][2] = { { -cs, -sn }, { sn, -cs }, { cs, sn }, { -sn, cs } }, uv[4][2] = { { 1, 0 }, { 0, 0 }, { 0, 1 }, { 1, 1 } };
        for (int i = 0; i < 4; i++) s.v.push_back({ p[i][0], p[i][1], uv[i][0], uv[i][1], col[0], col[1], col[2], col[3] });
        s.idx = { 0, 1, 2, 0, 2, 3 };
        snprintf(lab, sizeof lab, "%s  age %d/%d  frame %d/%d  tex %u", kKindName[kind], (int)floorf(a.age), life - 1, fr, nf - 1, s.img ? s.img->index : 0);
    }
    s.label = lab;
    return s;
}

struct Pane { int x, y, w, h; };   // window pixels, top-left origin

static void paneState(const Pane &p, int H, int clip0, int clip1) {
    const int gy = H - (p.y + p.h);
    api()->set_viewport(p.x, gy, p.w, p.h);
    api()->set_scissor(clip0, gy, clip1 - clip0, p.h);
}

static void drawBackdrop(const Args &a, const Pane &p) {
    api()->set_depth_mode(false, false, false, false, 0);
    api()->set_use_alpha(false, 0);
    const float c[4] = { 1, 1, 1, 1 };
    if (a.backdrop == "dark") { const float d[4] = { 0.10f, 0.10f, 0.11f, 1 }; quadFlat(-1, -1, 1, 1, d); }
    else if (a.backdrop == "checker") { bindTex(0, g_checker, false, 0, 0); quadTex(-1, -1, 1, 1, 0, 0, p.w / 32.0f, p.h / 32.0f, c, 0); }
    else if (a.backdrop == "wall") {
        const Tex &t = romTex(a.bgtex, true);
        if (t.id) { bindTex(0, t.id, true, 0, 0, true); quadTex(-1, -1, 1, 1, 0, 0, p.w / (2.0f * t.w), p.h / (2.0f * t.h), c, 0); }
        else { const float g[4] = { 0.30f, 0.30f, 0.32f, 1 }; quadFlat(-1, -1, 1, 1, g); }
    } else { const float g[4] = { 0.45f, 0.45f, 0.47f, 1 }; quadFlat(-1, -1, 1, 1, g); }
}

// One sprite pane: fx = the effect side (the kind in the combiner id), else the original.
static void drawSpritePane(const Args &a, const Sprite &s, const Pane &p, int H, bool fx, int clip0, int clip1) {
    paneState(p, H, clip0, clip1);
    drawBackdrop(a, p);
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (const Vert &v : s.v) { x0 = fminf(x0, v.x); x1 = fmaxf(x1, v.x); y0 = fminf(y0, v.y); y1 = fmaxf(y1, v.y); }
    const float hx = fmaxf(0.5f * (x1 - x0), 0.01f), hy = fmaxf(0.5f * (y1 - y0), 0.01f), cx = 0.5f * (x0 + x1), cy = 0.5f * (y0 + y1);
    const float sc = a.zoom * 0.9f * fminf(p.w / (2.0f * hx), p.h / (2.0f * hy));
    const GfxClipParameters cp = api()->get_clip_parameters();
    uint32_t tid = g_white;
    int cms = 2, cmt = 2;
    if (s.img) {
        const Tex &t = romTex((int)s.img->index, false);
        if (t.id) { tid = t.id; cms = s.img->flagsS; cmt = s.img->flagsT; }
    }
    program(CC_TEX, idFor(fx ? fxKindOf(kindIndex(a.kind)) : 0));
    api()->set_use_alpha(true, 0);
    bindTex(0, tid, !a.nearest, cms, cmt);
    std::vector<float> v;
    for (int i : s.idx) {
        const Vert &q = s.v[(size_t)i];
        const float X = (q.x - cx) * sc * 2.0f / p.w, Y = (q.y - cy) * sc * 2.0f / p.h * (cp.invert_y ? -1.0f : 1.0f);
        v.insert(v.end(), { X, Y, 0.0f, 1.0f, q.u, q.v, q.r, q.g, q.b, q.a });
    }
    draw(v, 10);
}

// ---------------------------------------------------------------- decal mode: the game's bullet holes vs the port's cluster, on a lit wall
struct DecalMat { const char *key; const char *name; int group; int ntypes; int types[3]; int wallTex; int hit; };   // hit: the images.def hit type (the texture sheet)
// group: the port's FXC_* material (stone/default 0, wood 1, metal 2, dirt/mud 3, snow 4, glass 5, tile 6); wallTex: a real level
// texture of that hit type (NTSC numbering, assets/images.def); types: the game's impact types (src/game/tex.c *_impact_types)
static const DecalMat kMats[] = {
    { "stone", "Stone", 0, 1, { 1 }, 286, FXHIT_STONE }, { "tile", "Tile", 6, 1, { 1 }, 696, FXHIT_TILE }, { "default", "Default (untagged)", 0, 1, { 7 }, 722, FXHIT_DEFAULT },
    { "wood", "Wood", 1, 1, { 1 }, 1203, FXHIT_WOOD }, { "metal", "Metal", 2, 1, { 7 }, 399, FXHIT_METAL }, { "metalobj", "Metal object", 2, 2, { 1, 7 }, 77, FXHIT_METALOBJ },
    { "dirt", "Dirt", 3, 1, { 2 }, 594, FXHIT_DIRT }, { "mud", "Mud", 3, 1, { 2 }, 775, FXHIT_MUD }, { "snow", "Snow", 4, 1, { 1 }, 1157, FXHIT_SNOW }, { "glass", "Glass", 5, 3, { 4, 5, 6 }, 809, FXHIT_GLASS },
};
static const int kNMats = (int)(sizeof kMats / sizeof *kMats);
static int matIndex(const std::string &s) { for (int i = 0; i < kNMats; i++) if (s == kMats[i].key) return i; return 0; }
static int fxcMat(int grp) { return grp == 1 ? FXC_WOOD : grp == 2 ? FXC_METAL : grp == 3 ? FXC_DIRT : grp == 4 ? FXC_SNOW : grp == 5 ? FXC_GLASS : grp == 6 ? FXC_TILE : FXC_STONE; }
static int decalType(const Args &a) { return a.dtype >= 0 && a.dtype < 20 ? a.dtype : kMats[matIndex(a.mat)].types[0]; }
static int wallTexOf(const Args &a) { return a.walltex >= 0 ? a.walltex : kMats[matIndex(a.mat)].wallTex; }
static float holeSizeOf(const Args &a) { const ImpactType &it = kImpactTypes[decalType(a)]; return fmaxf(it.w, it.h); }

struct Hole { float x, y, ang, k; };
static std::vector<Hole> holeLayout(int n, unsigned seed, float size) {   // fxparticles.c decalAdd: 0.85..1.15 scale; the first at the middle
    std::vector<Hole> v;
    unsigned s = seed * 2654435761u + 17u;
    auto fr = [&]() { return lcg(s) / 32767.0f; };
    for (int i = 0; i < n; i++) {
        Hole h;
        h.ang = 0.0f; h.k = 0.85f + 0.3f * fr();   // never turned, like the game's
        if (i == 0) { h.x = h.y = 0; } else { const float an = fr() * 6.2831853f, r = size * (0.9f + 1.1f * fr()); h.x = cosf(an) * r; h.y = sinf(an) * r; }
        v.push_back(h);
    }
    return v;
}

// The camera orbits the middle hole; yaw 0 / pitch 0 = straight at the wall (the wall is z = 0, normal +z, y up). World -> eye space.
struct View { float eye[3], r[3], u[3], f[3]; };
static View makeView(const Args &a) {
    View c;
    const float d = 8.2f * holeSizeOf(a) / fmaxf(a.dzoom, 0.05f), y = a.yaw * 0.0174533f, p = a.pitch * 0.0174533f;
    c.eye[0] = d * cosf(p) * sinf(y); c.eye[1] = d * sinf(p); c.eye[2] = d * cosf(p) * cosf(y);
    const float il = 1.0f / sqrtf(c.eye[0] * c.eye[0] + c.eye[1] * c.eye[1] + c.eye[2] * c.eye[2]);
    for (int i = 0; i < 3; i++) c.f[i] = -c.eye[i] * il;
    c.r[0] = -c.f[2]; c.r[1] = 0; c.r[2] = c.f[0];
    { const float l = 1.0f / sqrtf(c.r[0] * c.r[0] + c.r[2] * c.r[2]); c.r[0] *= l; c.r[2] *= l; }
    c.u[0] = c.r[1] * c.f[2] - c.r[2] * c.f[1]; c.u[1] = c.r[2] * c.f[0] - c.r[0] * c.f[2]; c.u[2] = c.r[0] * c.f[1] - c.r[1] * c.f[0];
    for (int i = 0; i < 3; i++) c.eye[i] += c.r[i] * a.panx + c.u[i] * a.pany;   // the pan moves the eye and what it looks at together
    return c;
}
static void toEye(const View &c, const float w[3], float o[3], bool dir) {
    const float q[3] = { dir ? w[0] : w[0] - c.eye[0], dir ? w[1] : w[1] - c.eye[1], dir ? w[2] : w[2] - c.eye[2] };
    o[0] = c.r[0] * q[0] + c.r[1] * q[1] + c.r[2] * q[2];
    o[1] = c.u[0] * q[0] + c.u[1] * q[1] + c.u[2] * q[2];
    o[2] = -(c.f[0] * q[0] + c.f[1] * q[1] + c.f[2] * q[2]);
}

// The cluster's textures and quad numbers (the decal mode's and the cluster scene's).
struct Cluster { uint32_t col = 0, dep = 0; float x0 = 0, y0 = 0, size = 1, depthMax = 0; bool built = false; std::string key; };

static void texAverage(const Tex &t, float out[3]) {
    double s[3] = { 0, 0, 0 };
    const size_t n = (size_t)t.w * t.h;
    for (size_t i = 0; i < n; i++) for (int k = 0; k < 3; k++) s[k] += t.rgba[i * 4 + (size_t)k] / 255.0;
    for (int k = 0; k < 3; k++) out[k] = n ? (float)(s[k] / (double)n) : 0.5f;
}

// Stamp the holes (x, y, half size, template variant) into one cluster and upload its colour (128^2) and depth (64^2: the game's) textures.
struct StampHole { float x, y, hw, hh; };
static void uploadCluster(Cluster &k, int mat, const std::vector<StampHole> &hs, const float base[3], const unsigned char *glassRgba, int gw, int gh) {
    static FxcTemplate tpl[FXC_MATS][4];
    static bool made[FXC_MATS][4];
    static FxcCluster cl;
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (const StampHole &H : hs) { x0 = fminf(x0, H.x - H.hw); x1 = fmaxf(x1, H.x + H.hw); y0 = fminf(y0, H.y - H.hh); y1 = fmaxf(y1, H.y + H.hh); }
    const float size = fmaxf(x1 - x0, y1 - y0) * 1.02f, cx = 0.5f * (x0 + x1), cy = 0.5f * (y0 + y1);
    fxc_cluster_clear(&cl, cx - size * 0.5f, cy - size * 0.5f, size);
    if (mat == FXC_GLASS && glassRgba) for (int v = 0; v < 4; v++) { fxc_template_glass_from(&tpl[mat][v], glassRgba, gw, gh); made[mat][v] = true; }
    for (size_t i = 0; i < hs.size(); i++) {
        const int var = (int)(i % 4);
        if (!made[mat][var]) { fxc_template(&tpl[mat][var], mat, var); made[mat][var] = true; }
        const FxcHole h = { hs[i].x, hs[i].y, hs[i].hw, hs[i].hh, mat, var, { base[0], base[1], base[2] } };
        fxc_stamp(&cl, &tpl[mat][var], &h);
    }
    std::vector<unsigned char> crgba((size_t)FXC_N * FXC_N * 4), drgba(64 * 64 * 4);
    std::vector<float> cdep((size_t)FXC_N * FXC_N), ddep(64 * 64);
    fxc_cluster_resample(&cl, FXC_N, crgba.data(), cdep.data());
    fxc_cluster_resample(&cl, 64, drgba.data(), ddep.data());   // the game's depth texture: 64, R = depth 0..1
    for (int i = 0; i < 64 * 64; i++) {
        const float d = ddep[(size_t)i];
        drgba[(size_t)i * 4 + 0] = (unsigned char)(d > 1.0f ? 255.0f : d * 255.0f + 0.5f);
        drgba[(size_t)i * 4 + 1] = 0; drgba[(size_t)i * 4 + 2] = 0; drgba[(size_t)i * 4 + 3] = 255;
    }
    if (k.col) { api()->delete_texture(k.col); api()->delete_texture(k.dep); }
    k.col = makeTex(crgba.data(), FXC_N, FXC_N);
    k.dep = makeTex(drgba.data(), 64, 64);
    k.x0 = cl.x0; k.y0 = cl.y0; k.size = cl.size; k.depthMax = cl.depthMax;
    k.built = true;
}

static void buildDecalCluster(Cluster &k, const Args &a, const std::vector<Hole> &hs) {
    char key[96];
    snprintf(key, sizeof key, "d%d/%d/%d/%d/%d/%d", matIndex(a.mat), decalType(a), a.holes, a.seed, wallTexOf(a), (int)(a.wallshade * 100));
    if (k.built && k.key == key) return;
    const ImpactType &it = kImpactTypes[decalType(a)];
    const int mat = fxcMat(kMats[matIndex(a.mat)].group);
    float base[3] = { 0.4f, 0.4f, 0.4f };
    const Tex &wt = romTex(wallTexOf(a), true);
    if (wt.id) { texAverage(wt, base); for (int j = 0; j < 3; j++) base[j] = fminf(1.0f, base[j] * a.wallshade * 1.15f); }   // the wall's average x its baked light, 15 % lighter
    const unsigned char *glass = nullptr;
    int gw = 0, gh = 0;
    if (mat == FXC_GLASS) {   // glass: the game's own glass hole texture, its middle kept empty
        const ImgEntry *gi = fxTableEntry("impactimages", decalType(a));
        if (gi) { const Tex &gt = romTex((int)gi->index, false); if (gt.id) { glass = gt.rgba.data(); gw = gt.w; gh = gt.h; } }
    }
    std::vector<StampHole> sh;
    for (const Hole &H : hs) sh.push_back({ H.x, H.y, it.w * H.k, it.h * H.k });
    uploadCluster(k, mat, sh, base, glass, gw, gh);
    k.key = key;
}

static void setLights(const Cam &cam, const float sunEye[3], bool sunOn, const float ptEye[3], bool ptOn, const Args &a) {
    GfxLights L = {};
    memcpy(L.inv_proj, cam.inv, sizeof L.inv_proj);
    for (int i = 0; i < 3; i++) L.sun_dir[i] = sunEye[i];
    if (sunOn) { L.count[1] = 1; L.sun_col[0] = 1.0f; L.sun_col[1] = 0.96f; L.sun_col[2] = 0.88f; }
    if (ptOn) { L.count[0] = 1; for (int i = 0; i < 3; i++) L.pos[0][i] = ptEye[i]; L.pos[0][3] = a.ptr; L.col[0][0] = 2.2f; L.col[0][1] = 1.5f; L.col[0][2] = 0.7f; }
    if (api()->set_lights) api()->set_lights(&L);
}

static void drawDecalPane(const Args &a, Cluster &cluster, const Pane &p, int H, bool fx, int clip0, int clip1) {
    paneState(p, H, clip0, clip1);
    const float bg[4] = { 0.05f, 0.05f, 0.06f, 1 };
    api()->set_depth_mode(false, false, false, false, 0);
    api()->set_use_alpha(false, 0);
    quadFlat(-1, -1, 1, 1, bg);
    const Cam cam = makeCam(40.0f, (float)p.w / (float)p.h);
    const View vw = makeView(a);
    const bool sunOn = a.sun != 0;   // decal mode: the sun is on unless --sun 0
    float sw[3], se[3];
    { const float az = a.sunaz * 0.0174533f, el = a.sunel * 0.0174533f; const float d[3] = { cosf(el) * cosf(az), cosf(el) * sinf(az), sinf(el) }; memcpy(sw, d, sizeof sw); }
    if (!sunOn) { const float l = sqrtf(0.09f + 0.81f + 0.09f); const float d[3] = { 0.3f / l, 0.9f / l, 0.3f / l }; memcpy(sw, d, sizeof sw); }   // dynlights.c addReliefLight: the relief light, no sun
    toEye(vw, sw, se, true);
    const float pw[3] = { a.ptx, a.pty, a.pth };
    float pe[3];
    toEye(vw, pw, pe, false);
    setLights(cam, se, sunOn, pe, a.pt != 0, a);
    auto clipV = [&](float wx, float wy, float wz, float o[4]) { const float w3[3] = { wx, wy, wz }; float e[3]; toEye(vw, w3, e, false); clipOf(cam, e, o); };
    static const int idx[6] = { 0, 1, 2, 0, 2, 3 };
    {   // the wall: TEXEL0 x SHADE (the room's baked vertex colour), lit by the Lights block; a.texel world units per texel
        const Tex &t = romTex(wallTexOf(a), true);
        const float tw = (float)(t.w > 0 ? t.w : 32), th = (float)(t.h > 0 ? t.h : 32), W = 40.0f * holeSizeOf(a) / fmaxf(a.dzoom, 0.05f), s = a.wallshade;
        const float q[4][4] = { { -W, -W, -W / (a.texel * tw), W / (a.texel * th) }, { W, -W, W / (a.texel * tw), W / (a.texel * th) },
                                { W, W, W / (a.texel * tw), -W / (a.texel * th) }, { -W, W, -W / (a.texel * tw), -W / (a.texel * th) } };
        program(CC_TEX, idFor(0));
        api()->set_use_alpha(false, 0);
        bindTex(0, t.id ? t.id : g_white, true, 0, 0, t.id != 0);
        std::vector<float> v;
        for (int i : idx) { float c4[4]; clipV(q[i][0], q[i][1], 0, c4); v.insert(v.end(), { c4[0], c4[1], c4[2], c4[3], q[i][2], q[i][3], s, s, s, 1.0f }); }
        draw(v, 10);
    }
    std::vector<Hole> hs = holeLayout(a.holes, (unsigned)a.seed, holeSizeOf(a));
    const int type = decalType(a);
    const ImpactType &it = kImpactTypes[type];
    if (fx) {   // the port: the holes fused into one cluster (kind 7), 0.4 off the wall
        buildDecalCluster(cluster, a, hs);
        program(0x180a180aull, 0x70000c01u);
        api()->set_use_alpha(true, 0);
        bindTex(0, cluster.col, true, 2, 2); bindTex(1, cluster.dep, true, 2, 2);
        const int mat = fxcMat(kMats[matIndex(a.mat)].group);
        const float depthUV = cluster.depthMax / cluster.size, vr = fminf(depthUV * 4.0f, 1.0f), lightAmt = (mat == FXC_SNOW || mat == FXC_GLASS) ? 0.0f : 1.0f;
        const float x1 = cluster.x0 + cluster.size, y1 = cluster.y0 + cluster.size;
        const float q[4][4] = { { cluster.x0, cluster.y0, 0, 0 }, { x1, cluster.y0, 1, 0 }, { x1, y1, 1, 1 }, { cluster.x0, y1, 0, 1 } };
        const float cl = (64 - 0.5f) / 64;
        std::vector<float> v;
        for (int i : idx) { float c4[4]; clipV(q[i][0], q[i][1], 0.4f, c4); v.insert(v.end(), { c4[0], c4[1], c4[2], c4[3], q[i][2], q[i][3], q[i][2], q[i][3], cl, cl, vr, lightAmt, 1.0f, 1.0f }); }
        draw(v, 14);
        return;
    }
    // the game's holes: impactimages[type] x the vertex colour (explosion.c apptype), alpha over
    const ImgEntry *img = fxTableEntry("impactimages", type);
    uint32_t hid = g_white;
    if (img) { const Tex &ht = romTex((int)img->index, false); if (ht.id) hid = ht.id; }
    program(CC_TEX, idFor(0));
    api()->set_use_alpha(true, 0);
    bindTex(0, hid, true, img ? img->flagsS : 2, img ? img->flagsT : 2);
    unsigned vseed = (unsigned)a.seed * 7919u + (unsigned)type;
    std::vector<float> v;
    for (const Hole &H : hs) {
        const float hw = it.w * H.k, hh = it.h * H.k;
        static const float cn[4][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
        float vtx[4][10];
        for (int i = 0; i < 4; i++) {
            float col[3] = { 1, 1, 1 };
            impactVertexColour(it.app, vseed, col);
            float c4[4]; clipV(H.x + hw * cn[i][0], H.y + hh * cn[i][1], 0.4f, c4);
            const float tmp[10] = { c4[0], c4[1], c4[2], c4[3], cn[i][0] * 0.5f + 0.5f, cn[i][1] * 0.5f + 0.5f, col[0], col[1], col[2], 1.0f };
            memcpy(vtx[i], tmp, sizeof tmp);
        }
        for (int i : idx) v.insert(v.end(), vtx[i], vtx[i] + 10);
    }
    draw(v, 10);
}

// ---------------------------------------------------------------- the cluster scene (#141 repro): procedural holes at a known distance
static void buildCluster(Cluster &k, const Args &a) {
    char key[96];
    snprintf(key, sizeof key, "c%d/%d/%d/%d", a.cmat, a.holes, a.seed, (int)(a.holesize * 10));
    if (k.built && k.key == key) return;
    const int mat = a.cmat < 0 ? 0 : a.cmat >= FXC_MATS ? FXC_MATS - 1 : a.cmat;
    unsigned s = (unsigned)a.seed * 2654435761u;
    auto rnd = [&]() { s = s * 1664525u + 1013904223u; return (float)(s >> 8) / (float)(1 << 24); };
    std::vector<StampHole> hs;
    for (int i = 0; i < a.holes; i++) {
        const float x = (rnd() - 0.5f) * a.holesize * 2.4f, y = (rnd() - 0.5f) * a.holesize * 2.4f, kk = 0.85f + 0.3f * rnd();
        hs.push_back(i == 0 ? StampHole{ 0, 0, a.holesize, a.holesize } : StampHole{ x, y, a.holesize * kk, a.holesize * kk });
    }
    const float base[3] = { 0.40f, 0.40f, 0.40f };
    uploadCluster(k, mat, hs, base, nullptr, 0, 0);
    k.key = key;
}

static void clusterScene(const Args &a, Cluster &k, const Cam &cam) {
    buildCluster(k, a);
    GfxLights L = {};
    memcpy(L.inv_proj, cam.inv, sizeof L.inv_proj);
    const float sd[3] = { 0.3f, 0.9f, 0.3f }, sn = sqrtf(sd[0] * sd[0] + sd[1] * sd[1] + sd[2] * sd[2]);
    for (int i = 0; i < 3; i++) L.sun_dir[i] = sd[i] / sn;   // the relief light (dynlights.c addReliefLight), as the game sends it with no sun
    if (a.sun > 0) { L.count[1] = 1; L.sun_col[0] = L.sun_col[1] = L.sun_col[2] = 1.0f; }
    if (api()->set_lights) api()->set_lights(&L);
    api()->set_depth_mode(false, false, false, false, 0);
    api()->set_use_alpha(false, 0);
    static const int idx[6] = { 0, 1, 2, 0, 2, 3 };
    {   // the wall: flat grey, shade only (SHADER_OPT_ALPHA)
        program(ge_render_cc_id_shade(), 1);
        const float ext = a.dist * 3.0f, z = -a.dist;
        const float q[4][2] = { { -ext, -ext }, { ext, -ext }, { ext, ext }, { -ext, ext } };
        std::vector<float> v;
        for (int i : idx) {
            const float e[3] = { q[i][0], q[i][1], z };
            float c4[4]; clipOf(cam, e, c4);
            v.insert(v.end(), { c4[0], c4[1], c4[2], c4[3], 0.35f, 0.35f, 0.38f, 1.0f });
        }
        draw(v, 8);
    }
    {   // the cluster: fxClusterPom. TEXEL1 (depth) x TEXEL0 (colour) + shade, kind PORTFX_CLUSTER (7) in id1 (port/shaders/fx/combiners.txt)
        // --diag: the same draw and combiner with a tool-only kind (8 coverage, 9 eye position, 10 uv, 11 pom colour, 12 pom alpha, 13 derivatives)
        const uint32_t d = a.diag == "coverage" ? 0x80000c01u : a.diag == "veye" ? 0x90000c01u : a.diag == "uv" ? 0xa0000c01u : a.diag == "pom" ? 0xb0000c01u
                         : a.diag == "pomalpha" ? 0xc0000c01u : a.diag == "deriv" ? 0xd0000c01u : 0x70000c01u;
        program(0x180a180aull, d);
        api()->set_use_alpha(true, 0);
        bindTex(0, k.col, true, 2, 2); bindTex(1, k.dep, true, 2, 2);
        const float z = -a.dist + 0.4f;
        const float depthUV = k.depthMax / k.size, vr = fminf(depthUV * 4.0f, 1.0f), lightAmt = (a.cmat == FXC_SNOW || a.cmat == FXC_GLASS) ? 0.0f : 1.0f;
        const float x1 = k.x0 + k.size, y1 = k.y0 + k.size;
        const float q[4][4] = { { k.x0, k.y0, 0, 0 }, { x1, k.y0, 1, 0 }, { x1, y1, 1, 1 }, { k.x0, y1, 0, 1 } };
        const float cl = (64 - 0.5f) / 64;   // texture 1's clamp bounds, as gfx_pc writes them
        std::vector<float> v;
        for (int i : idx) {
            const float e[3] = { q[i][0], q[i][1], z };
            float c4[4]; clipOf(cam, e, c4);
            // layout (gfx_pc): position, texture 0 (uv), texture 1 (uv + clamp S, T), input 1 (rgba)
            v.insert(v.end(), { c4[0], c4[1], c4[2], c4[3], q[i][2], q[i][3], q[i][2], q[i][3], cl, cl, vr, lightAmt, 1.0f, 1.0f });
        }
        if (a.bench) ge_render_gpu_timer_begin(1);
        for (int r = 0; r < (a.rep > 0 ? a.rep : 1); r++) draw(v, 14);
        if (a.bench) ge_render_gpu_timer_end(1);
    }
}

#include "fx_tabs.inc"

// ---------------------------------------------------------------- the decal texture sheet: the material's hit-type textures, click one to put it on the wall
struct SheetCell { int x, y, w, h, tex; };
static std::vector<SheetCell> g_sheet;
static const int kSheetCols = 8, kSheetRows = 5;
static std::vector<int> sheetList(const Args &a) {
    std::vector<int> v;
    const std::vector<FxTexName> &n = fxTexNames();
    for (size_t i = 0; i < n.size(); i++) if (n[i].hit == kMats[matIndex(a.mat)].hit) v.push_back((int)i);
    return v;
}
static int sheetPages(const Args &a) { const int per = kSheetCols * kSheetRows; return ((int)sheetList(a).size() + per - 1) / per; }
static void drawSheet(Args &a, const Pane &p, int H) {
    g_sheet.clear();
    const std::vector<int> list = sheetList(a);
    const int per = kSheetCols * kSheetRows, pages = sheetPages(a);
    if (a.page >= pages) a.page = pages > 0 ? pages - 1 : 0;
    if (a.page < 0) a.page = 0;
    const int cw = p.w / kSheetCols, ch = p.h / kSheetRows;
    const GfxClipParameters cp = api()->get_clip_parameters();
    const float sy = cp.invert_y ? -1.0f : 1.0f;
    api()->set_depth_mode(false, false, false, false, 0);
    api()->set_viewport(p.x, 0, p.w, H);
    api()->set_scissor(p.x, 0, p.w, H);
    const float bg[4] = { 0.05f, 0.05f, 0.06f, 1 };
    api()->set_use_alpha(false, 0);
    quadFlat(-1, -1, 1, 1, bg);
    for (int i = 0; i < per && a.page * per + i < (int)list.size(); i++) {
        const int tn = list[(size_t)(a.page * per + i)], cx = p.x + (i % kSheetCols) * cw, cy = p.y + (i / kSheetCols) * ch;
        const Tex &t = romTex(tn, false);
        g_sheet.push_back({ cx, cy, cw, ch, tn });
        if (!t.id) continue;
        const int s = std::min(cw, ch - 18) - 8;
        const float ar = t.w > 0 && t.h > 0 ? (float)t.w / (float)t.h : 1.0f;
        int qw = s, qh = s;
        if (ar > 1) qh = (int)((float)s / ar); else qw = (int)((float)s * ar);
        if (qw < 1 || qh < 1) continue;
        const int gy = H - (cy + 4) - qh;
        api()->set_viewport(cx + 4, gy, qw, qh);
        api()->set_scissor(cx + 4, gy, qw, qh);
        program(CC_TEX, idFor(0));
        bindTex(0, t.id, false, 0, 0);
        const float c[4] = { 1, 1, 1, 1 };   // 2x2 repeats of the texture, aspect kept; upright on either clip orientation
        std::vector<float> v;
        const float cn[4][2] = { { -1, 1 }, { 1, 1 }, { 1, -1 }, { -1, -1 } };
        static const int idx[6] = { 0, 1, 2, 0, 2, 3 };
        for (int k : idx) v.insert(v.end(), { cn[k][0], cn[k][1] * sy, 0.0f, 1.0f, (cn[k][0] + 1.0f), (1.0f - cn[k][1]), c[0], c[1], c[2], c[3] });
        draw(v, 10);
    }
}
static void sheetLabels(const Args &a) {
    ImDrawList *dl = ImGui::GetForegroundDrawList();
    const std::vector<FxTexName> &n = fxTexNames();
    for (const SheetCell &c : g_sheet) {
        char t[96];
        snprintf(t, sizeof t, "%d %s", c.tex, c.tex < (int)n.size() ? n[(size_t)c.tex].name.c_str() : "");
        dl->PushClipRect(ImVec2((float)c.x, (float)c.y), ImVec2((float)(c.x + c.w - 2), (float)(c.y + c.h)), true);   // a long name stays in its cell
        dl->AddText(ImVec2((float)c.x + 4, (float)(c.y + c.h - 17)), c.tex == wallTexOf(a) ? IM_COL32(255, 210, 120, 255) : IM_COL32(220, 220, 220, 255), t);
        dl->PopClipRect();
    }
}

// ---------------------------------------------------------------- selftest (no ROM, no window)
static int selftest(const Args &a) {
    int fail = fx_tex_selftest();
    for (int f = 0; f < 11; f++) {   // the puff frame stepping (bullet_sparks_init: frame = age * 0.5, clamped to the table)
        const int want = std::min(f / 2, 5);
        if (puffFrame((float)f, 0.5f, 6) != want) { fprintf(stderr, "selftest FAIL puffFrame(%d) = %d, want %d\n", f, puffFrame((float)f, 0.5f, 6), want); fail++; }
    }
    if (!a.shaders.empty()) {   // the effect files: each family's function in them
        std::string all;
        for (const char *fn : { "common.glsl", "bullets.glsl", "bursts.glsl", "impacts.glsl" }) {
            FILE *f = fopen((a.shaders + "/" + fn).c_str(), "rb");
            if (!f) { fprintf(stderr, "selftest FAIL cannot read %s/%s\n", a.shaders.c_str(), fn); fail++; continue; }
            char buf[4096]; size_t n;
            while ((n = fread(buf, 1, sizeof buf, f)) > 0) all.append(buf, n);
            fclose(f);
        }
        for (const char *fn : { "float fxHash(", "vec4 fxTracer(", "vec4 fxSpark(", "vec4 fxBlood(", "vec4 fxImpact(", "vec4 fxClusterPom(" })
            if (all.find(fn) == std::string::npos) { fprintf(stderr, "selftest FAIL %s has no %s\n", a.shaders.c_str(), fn); fail++; }
    }
    unsigned s = 1; float c[3];
    impactVertexColour(1, s, c);
    if (c[0] < 216 / 255.0f - 1e-3f || c[0] > 1.0f) { fprintf(stderr, "selftest FAIL light impact colour %f\n", c[0]); fail++; }
    if (matIndex("glass") != 9 || kMats[matIndex("wood")].wallTex != 1203) { fprintf(stderr, "selftest FAIL material table\n"); fail++; }
    {   // the embedded tables (images.def names and hit types, the explosion types) and the .ini round trip
        const std::vector<FxTexName> &n = fxTexNames();
        if (n.size() < 2000 || n[1203].hit != FXHIT_WOOD || n[286].hit != FXHIT_STONE || n[809].hit != FXHIT_GLASS || smoke0() == 0) { fprintf(stderr, "selftest FAIL texture names (%d)\n", (int)n.size()); fail++; }
        if (kNExplo != 21 || burstIndex("blood") != BM_BLOOD || burstIndex("nonsense") != BM_STONE) { fprintf(stderr, "selftest FAIL tab tables\n"); fail++; }
        Args t;
        setOpt(t, "burst", "metal"); setOpt(t, "explo", "7"); setOpt(t, "trrange", "1234"); setOpt(t, "t", "0.5"); setOpt(t, "bogus", "1");
        const std::string ini = a.out + "selftest.ini";
        saveIni(t, ini);
        Args u;
        loadIni(u, ini);
        remove(ini.c_str());
        if (u.burst != "metal" || u.explo != 7 || u.trrange != 1234.0f || u.t != -1.0f) { fprintf(stderr, "selftest FAIL ini round trip (burst %s explo %d t %g)\n", u.burst.c_str(), u.explo, u.t); fail++; }
    }
    fprintf(stderr, fail ? "selftest: %d FAILED\n" : "selftest: ok\n", fail);
    return fail ? 1 : 0;
}

// ---------------------------------------------------------------- ImGui panel
static const char *kViewKey[] = { "side", "split", "orig", "fx", "sheet" };   // sheet: decal mode only
static const char *kBgKey[] = { "grey", "dark", "checker", "wall" };
static const char *kModeKey[] = { "sprite", "decal", "cluster", "muzzle", "tracer", "burst", "explosion", "sea" };
static const char *kTshowKey[] = { "game", "port", "side" };
static std::string g_iniPath;
static const char *kDiagKey[] = { "", "coverage", "veye", "uv", "pom", "pomalpha", "deriv" };
static const float kPanelW = 420.0f;

template <size_t N> static int keyIndex(const char *(&keys)[N], const std::string &s) { for (size_t i = 0; i < N; i++) if (s == keys[i]) return (int)i; return 0; }

static void uiTab(Args &a) {
    if (a.mode == "muzzle") {
        ImGui::SeparatorText("Muzzle flash (guards' weapons)");
        ImGui::TextWrapped("%s", fxGunNote().c_str());
        const std::vector<FxGun> &g = fxGuns();
        if (!g.empty() && ImGui::BeginCombo("weapon", g[(size_t)gunIndex(a)].name.c_str())) {
            for (int i = 0; i < (int)g.size(); i++) if (ImGui::Selectable(g[(size_t)i].name.c_str(), gunIndex(a) == i)) a.gun = g[(size_t)i].name;
            ImGui::EndCombo();
        }
        ImGui::SliderFloat("shots / s", &a.mrate, 1.0f, 15.0f, "%.1f");
        ImGui::SliderInt("ticks shown", &a.mvis, 1, 6);
        ImGui::SliderFloat("model scale", &a.mscale, 0.02f, 0.5f, "%.3f");
        ImGui::TextDisabled("port: the same quad (fast3d gives MUZZLE no effect)\n+ dynlights' muzzle light (2.2,1.5,0.7, r 260)");
    } else if (a.mode == "tracer") {
        ImGui::SeparatorText("Tracer");
        int it = tracerItem(a);
        if (ImGui::Combo("tracer", &it, "gun\0laser\0watch laser\0")) a.tritem = it == 0 ? "gun" : it == 1 ? "laser" : "watchlaser";
        ImGui::SliderFloat("range", &a.trrange, 100.0f, 6000.0f, "%.0f");
        ImGui::SliderFloat("shot every (s)", &a.trperiod, 0.05f, 2.0f, "%.2f");
    } else if (a.mode == "burst") {
        ImGui::SeparatorText("Impact burst");
        int b = burstIndex(a.burst);
        if (ImGui::Combo("material", &b, "metal\0wood\0glass\0water\0snow\0dirt / mud\0stone / tile\0blood\0")) a.burst = kBurstKey[b];
        ImGui::TextDisabled("%s", kBurstName[b]);
        ImGui::SliderFloat("loop (s)", &a.bperiod, 0.2f, 3.0f, "%.2f");
    } else if (a.mode == "explosion") {
        ImGui::SeparatorText("Explosion");
        ImGui::SliderInt("type", &a.explo, 0, kNExplo - 1);
        ImGui::SliderFloat("pause after (s)", &a.etail, 0.0f, 2.0f, "%.2f");
        ImGui::TextDisabled("the smoke the blast leaves is not drawn");
    }
    ImGui::SeparatorText("Playback");
    if (ImGui::Button("Replay")) { g_tTime = 0; g_tPlay = true; }
    ImGui::SameLine();
    ImGui::Checkbox("play", &g_tPlay);
    ImGui::SameLine();
    ImGui::Checkbox("loop", &g_tLoop);
    ImGui::SliderFloat("speed", &g_tSpeed, 0.02f, 2.0f, "%.2f x");
    ImGui::SliderFloat("time (s)", &g_tTime, 0.0f, tabPeriod(a), "%.3f");
    if (ImGui::Button("reroll")) a.tseed++;
    ImGui::SeparatorText("Game vs port");
    int ts = tshowOf(a);
    if (ImGui::Combo("show", &ts, "game (F9 off)\0port\0side by side\0")) a.tshow = kTshowKey[ts];
    ImGui::SeparatorText("Camera");
    ImGui::SliderFloat("yaw", &a.tyaw, -85, 85, "%.0f deg");
    ImGui::SliderFloat("pitch", &a.tpitch, -85, 85, "%.0f deg");
    ImGui::SliderFloat("zoom", &a.tzoom, 0.1f, 8.0f, "%.2f");
    bool wl = a.twall != 0;
    if (ImGui::Checkbox("wall", &wl)) a.twall = wl;
    ImGui::InputInt("wall texture", &a.twalltex);
    if (a.twalltex < 0) a.twalltex = 0;
    bool nn = a.nearest != 0;
    if (ImGui::Checkbox("nearest texels", &nn)) a.nearest = nn;
}

static void uiPanel(Args &a, int w, int h, const char *backendName, std::string &status, bool &shotReq) {
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(kPanelW, (float)h));
    ImGui::Begin("fxview", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);
    ImGui::PushItemWidth(kPanelW * 0.5f);
    ImGui::Text("fxview  %s  %dx%d", backendName, w, h);
    ImGui::TextWrapped("%s", fxRomNote().c_str());
    ImGui::TextWrapped("%s", fxTexNote().c_str());
    int mode = keyIndex(kModeKey, a.mode);
    if (ImGui::Combo("mode", &mode, "sprite\0decal\0cluster\0muzzle\0tracer\0burst\0explosion\0sky / sea\0")) a.mode = kModeKey[mode];
    ImGui::Separator();
    if (a.mode == "sea") {
#ifdef __SWITCH__
        ImGui::TextDisabled("sticks move and look, ZL/ZR down/up, B fast\nL/R load /2 x2, Minus pass size, Y sway, X freeze");
#else
        ImGui::TextDisabled("WASD move, right mouse look, Space/Ctrl up/down,\nShift fast, R/F load x2 /2, M pass size");
#endif
        seaTabUi();
    } else if (a.mode == "sprite") {
        int k = kindIndex(a.kind);
        if (ImGui::Combo("effect", &k, "tracer\0laser\0watch laser\0wall puff\0blood puff\0scatter puff\0bullet hole\0")) a.kind = kKindKey[k];
        if (k == K_IMPACT) ImGui::SliderInt("impact type", &a.impact, 0, 19);
        if (k >= K_WALLPUFF && k <= K_SCATTER) { ImGui::SliderFloat("age", &a.age, 0.0f, 10.0f); ImGui::SliderFloat("angle", &a.angle, 0.0f, 6.28f); }
        if (k <= K_WATCHLASER) ImGui::SliderFloat("tracer length", &a.tracerlen, 1.0f, 20.0f);
        ImGui::SliderFloat("zoom", &a.zoom, 0.2f, 6.0f);
        bool nn = a.nearest != 0;
        if (ImGui::Checkbox("nearest texels", &nn)) a.nearest = nn;
        int bg = keyIndex(kBgKey, a.backdrop);
        if (ImGui::Combo("backdrop", &bg, "grey\0dark\0checker\0wall\0")) a.backdrop = kBgKey[bg];
        if (bg == 3) ImGui::InputInt("wall texture", &a.bgtex);
    } else if (a.mode == "decal") {
        int m = matIndex(a.mat);
        if (ImGui::Combo("material", &m, "stone\0tile\0default\0wood\0metal\0metal object\0dirt\0mud\0snow\0glass\0")) a.mat = kMats[m].key;
        ImGui::SliderInt("holes", &a.holes, 1, 6);
        ImGui::SliderFloat("yaw", &a.yaw, -80.0f, 80.0f); ImGui::SliderFloat("pitch", &a.pitch, -60.0f, 60.0f); ImGui::SliderFloat("zoom", &a.dzoom, 0.4f, 4.0f);
        ImGui::SliderInt("impact type", &a.dtype, -1, 19);
        bool sun = a.sun != 0, pt = a.pt != 0;
        if (ImGui::Checkbox("sun", &sun)) a.sun = sun;
        ImGui::SameLine();
        if (ImGui::Checkbox("point light", &pt)) a.pt = pt;
        ImGui::SliderFloat("sun azimuth", &a.sunaz, -180.0f, 180.0f); ImGui::SliderFloat("sun elevation", &a.sunel, 5.0f, 90.0f);
        ImGui::SliderFloat("light x", &a.ptx, -60.0f, 60.0f); ImGui::SliderFloat("light y", &a.pty, -60.0f, 60.0f); ImGui::SliderFloat("light height", &a.pth, 10.0f, 200.0f);
        ImGui::SliderInt("seed", &a.seed, 1, 99);
        if (a.view == "sheet") {
            ImGui::InputInt("sheet page", &a.page);
            int wt = wallTexOf(a);
            if (ImGui::InputInt("wall texture", &wt)) a.walltex = wt < 0 ? 0 : wt;
        }
    } else if (isTab(a)) {
        uiTab(a);
    } else {
        ImGui::SliderFloat("distance", &a.dist, 20.0f, 600.0f);
        ImGui::SliderInt("holes", &a.holes, 1, 8);
        ImGui::SliderInt("material", &a.cmat, 0, FXC_MATS - 1);
        ImGui::SliderFloat("hole size", &a.holesize, 2.0f, 16.0f);
        ImGui::SliderFloat("fov", &a.fov, 20.0f, 100.0f);
        ImGui::SliderInt("seed", &a.seed, 1, 99);
        int d = keyIndex(kDiagKey, a.diag);
        if (ImGui::Combo("diagnostic", &d, "final colour\0coverage\0eye position\0uv\0pom colour\0pom alpha\0derivatives\0")) a.diag = kDiagKey[d];
    }
    if (a.mode == "sprite" || a.mode == "decal") {
        ImGui::Separator();
        int v = keyIndex(kViewKey, a.view);
        if (a.mode == "decal") { if (ImGui::Combo("view", &v, "side by side\0split\0original\0effect\0texture sheet\0")) a.view = kViewKey[v]; }
        else { if (v > 3) v = 0; if (ImGui::Combo("view", &v, "side by side\0split\0original\0effect\0")) a.view = kViewKey[v]; }
        if (v == 1) ImGui::SliderFloat("split", &a.split, 0.0f, 1.0f);
    }
    ImGui::SliderInt("frame_count", &a.fc, 0, 600);
    if (ImGui::Button("save shot (F12)")) shotReq = true;
    ImGui::SameLine();
    if (ImGui::Button("save settings")) { saveIni(a, g_iniPath); status = "settings saved: " + g_iniPath; }
    ImGui::TextUnformatted("Esc / Start quits");
    if (!status.empty()) { ImGui::Separator(); ImGui::TextWrapped("%s", status.c_str()); }
#ifndef __SWITCH__
    if (fxReloadCount() || fxReloadFailed()) {
        ImGui::Separator();
        if (fxReloadFailed()) ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 110, 110, 255));
        ImGui::TextWrapped("%s", fxReloadStatus().c_str());
        if (fxReloadFailed()) ImGui::PopStyleColor();
    }
#endif
    ImGui::PopItemWidth();
    ImGui::End();
}

static void labelAt(const Pane &p, const char *text, bool bottom) {   // wrapped to the pane: a long caption takes more lines instead of running out of it
    ImDrawList *dl = ImGui::GetForegroundDrawList();
    const float wrap = (float)p.w - 22.0f;
    const ImVec2 sz = ImGui::CalcTextSize(text, nullptr, false, wrap);
    const ImVec2 pos((float)p.x + 8.0f, bottom ? (float)(p.y + p.h) - sz.y - 8.0f : (float)p.y + 6.0f);
    dl->AddRectFilled(ImVec2(pos.x - 3, pos.y - 2), ImVec2(pos.x + sz.x + 3, pos.y + sz.y + 2), IM_COL32(0, 0, 0, 150));
    dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), pos, IM_COL32(255, 255, 255, 255), text, nullptr, wrap);
}

int main(int argc, char **argv) {
    Args a;
    std::vector<std::string> av;
    for (int i = 1; i < argc; i++) av.push_back(argv[i]);
#ifdef __SWITCH__
    const std::string defOut = "sdmc:/switch/project-dam/fxview/";
    makeDir("sdmc:/switch/project-dam");
    makeDir(defOut);
    {   // arguments from the file as well as the nxlink argv
        FILE *f = fopen((defOut + "fxview.args").c_str(), "rb");
        if (f) {
            char line[1024] = {};
            if (fgets(line, sizeof line, f)) {
                for (char *t = strtok(line, " \r\n\t"); t; t = strtok(nullptr, " \r\n\t")) av.push_back(t);
            }
            fclose(f);
        }
    }
    socketInitializeDefault();
    nxlinkStdio();   // the log also goes to stdout over nxlink
#else
    const std::string defOut = ".";
#endif
    parseArgs(a, av);   // the first pass finds --out and whether this is a shot
    if (a.out.empty()) a.out = defOut;
    if (a.out.back() != '/') a.out += "/";
    makeDir(a.out);
    if (a.selftest) return selftest(a);
    g_iniPath = a.out + "fxview.ini";
    const bool headless = !a.probes.empty() || !a.dump.empty() || !a.shot.empty();
    if (!headless) {   // the saved settings, then the arguments again: they win; a shot is reproducible (defaults + arguments only)
        const std::string out = a.out;
        a = Args();
        loadIni(a, g_iniPath);
        parseArgs(a, av);
        a.out = out;
    }
    g_tPlay = a.play != 0;
    if (a.t >= 0) { g_tTime = a.t; g_tPlay = false; }
    g_tSpeed = a.tspeed;
    g_log = fopen((a.out + "fxview.log").c_str(), "w");
    logf("fxview: mode=%s backend=%s", a.mode.c_str(), a.backend.empty() ? "default" : a.backend.c_str());
    std::string rom;
    {
        rom = fxRomFind(a.rom);
        if (rom.empty()) logf("fxview: no ROM found (--rom, FXVIEW_ROM / N64_ROM, or the game's data dir): sprites show as untextured quads");
        else if (!fxRomOpen(rom)) logf("fxview: ROM %s: %s", rom.c_str(), fxRomNote().c_str());
        else logf("fxview: %s (%s)", fxRomNote().c_str(), rom.c_str());
    }
    {   // the textures: ge007.passets (the game's own picture of every texture) under the chosen pack and mod; written from the ROM when absent
        std::string base = fxTexFindBase(a.pack, rom);
        if (base.empty() && !rom.empty()) {
            const std::string made = a.out + "ge007.passets";
            std::string note;
            if (fxTexEnsureBase(rom, made, note)) base = made;
            logf("fxview: %s", note.c_str());
        }
        fxTexOpen(base, a.texpack, a.mod);
        logf("fxview: %s", fxTexNote().c_str());
    }

    GeRenderConfig cfg = ge_render_cfg;
#ifdef __SWITCH__
    cfg.backend = GE_RENDER_DEKO3D;
#else
    cfg.backend = GE_RENDER_VULKAN;
#endif
    if (a.backend == "gl") cfg.backend = GE_RENDER_GL;
    else if (a.backend == "vk") cfg.backend = GE_RENDER_VULKAN;
    else if (a.backend == "deko3d") cfg.backend = GE_RENDER_DEKO3D;
    cfg.window.title = "fxview";
    cfg.window.width = (uint32_t)a.w; cfg.window.height = (uint32_t)a.h; cfg.window.x = 100; cfg.window.y = 100; cfg.window.centered = 1;
    cfg.fog_vertex = 1;
    GeRenderCallbacks cb = {};
    g_handheld = a.w <= 1280 ? 1 : 0;
    cb.is_handheld = onHandheld;
    cb.log = onLog; cb.on_quit = onQuit; cb.on_key_down = onKey; cb.on_mouse_wheel = onWheel;
    ge_render_open(&cfg, &cb);
    static const char *names[] = { "OpenGL", "Vulkan", "deko3d" };
    logf("fxview: backend %s \"%s\"", names[ge_render_backend()], ge_render_renderer_string());
    ge_imgui_init();
    makeBuiltinTextures();
#ifndef __SWITCH__
    if (!a.shaders.empty()) {   // the effect files, watched (hot reload); GL has no effect shaders until they are given to it
        if (fxReloadInit(a.shaders, a.out)) fxReloadPoll(true);
        else logf("fxview: cannot watch %s", a.shaders.c_str());
    }
#endif

    uint32_t w = 0, h = 0; int32_t px = 0, py = 0;
    ge_render_get_dimensions(&w, &h, &px, &py);
    api()->update_framebuffer_parameters(0, w, h, 1, false, true, true, true);
    const int shotFrame = a.probeFrame;
    Cluster ccluster, dcluster;
    bool seaStarted = false, seaLoaded = false;
    SeaTabCam seaCam = {};
    bool shotReq = false;
    int shotN = 0;
    std::string status;
    for (int f = 0; !g_quit; f++) {
        ge_render_poll_events();
        uint32_t nw = 0, nh = 0;
        ge_render_get_dimensions(&nw, &nh, &px, &py);
        if (nw == 0 || nh == 0) continue;
        if (nw != w || nh != h) { w = nw; h = nh; api()->update_framebuffer_parameters(0, w, h, 1, false, true, true, true); api()->on_resize(); }
        const bool showUi = a.ui < 0 ? !headless : a.ui != 0;
        g_fc = a.fc;
        {   // the tabs' clock: real time, except in a shot (--t fixes it)
            static auto last = std::chrono::steady_clock::now();
            const auto now = std::chrono::steady_clock::now();
            const float dt = std::min(std::chrono::duration<float>(now - last).count(), 0.25f);
            last = now;
            if (!headless) tabTick(a, dt);
        }
#ifndef __SWITCH__
        fxReloadPoll(false);
#endif
        ge_render_begin_frame();
        api()->start_draw_to_framebuffer(0, 1.0f);
        api()->set_viewport(0, 0, (int)w, (int)h);
        api()->set_scissor(0, 0, (int)w, (int)h);
        api()->clear_framebuffer(true, true);

        const int H = (int)h;
        const int x0 = showUi ? (int)kPanelW : 0;
        const Pane scene = { x0, 0, (int)w - x0, H };
#ifndef __SWITCH__
        if (!headless && (a.mode == "decal" || isTab(a))) {   // the scene's mouse (outside the panel): left drag orbits, the wheel zooms, middle drag pans and right drag moves the decal's point light
            static int omx = 0, omy = 0, odrag = 0;
            GeRenderInput mi;
            ge_render_poll_input(&mi);
            const bool overUi = showUi && ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureMouse;
            const int btn = mi.mouse_buttons & 7;
            if (!btn) odrag = 0;
            else if (!odrag && mi.mouse_x >= x0 && !overUi) odrag = btn;   // a drag starts in the scene and keeps its button
            const float dx = (float)(mi.mouse_x - omx), dy = (float)(mi.mouse_y - omy);
            omx = mi.mouse_x; omy = mi.mouse_y;
            const bool tab = isTab(a);
            float &yaw = tab ? a.tyaw : a.yaw, &pitch = tab ? a.tpitch : a.pitch, &zoom = tab ? a.tzoom : a.dzoom;
            const float ylim = tab ? 85.0f : 80.0f, plim = tab ? 85.0f : 60.0f;
            if (odrag & 1) {
                yaw = fminf(fmaxf(yaw - dx * 0.3f, -ylim), ylim);
                pitch = fminf(fmaxf(pitch + dy * 0.3f, -plim), plim);
            } else if ((odrag & 4) && !tab) {   // middle drag pans: the wall follows the mouse
                const float k = 8.2f * holeSizeOf(a) / fmaxf(a.dzoom, 0.05f) * 0.0015f;
                a.panx -= dx * k;
                a.pany += dy * k;
            } else if ((odrag & 2) && !tab) {
                a.ptx += dx * 0.1f;
                a.pty -= dy * 0.1f;
            }
            if (g_wheel != 0.0f && mi.mouse_x >= x0 && !overUi) zoom = fminf(fmaxf(zoom * powf(1.1f, g_wheel), 0.4f), 4.0f);
        }
#endif
        std::vector<std::pair<Pane, std::string>> labels;   // drawn by ImGui after the scene
        if (a.mode == "sea") {
            if (!seaStarted) {   // the first time the mode is shown: textures from the ROM, the VIEWER variants, the saved settings
                const void *pack = nullptr; size_t packSize = 0;
#ifdef SEA_HAVE_PACK
                pack = ge007_sea_pack; packSize = (size_t)(ge007_sea_pack_end - ge007_sea_pack);
#endif
                seaLoaded = seaTabInit(pack, packSize, rom.c_str());   // its settings came with fxview.ini and the arguments
                seaTabInitCam(&seaCam);
                seaStarted = true;
                logf("fxview: sky/sea module %s", seaLoaded ? "ready" : "failed (see the log)");
            }
            static auto seaLast = std::chrono::steady_clock::now();
            const auto now = std::chrono::steady_clock::now();
            const float dt = std::min(std::chrono::duration<float>(now - seaLast).count(), 0.25f);
            seaLast = now;
            float fwd = 0, right = 0, up = 0, fast = 1;
            GeRenderInput in;
            ge_render_poll_input(&in);
            auto dz = [](float v) { return v > -0.18f && v < 0.18f ? 0.0f : v; };   // a worn stick drifts
            fwd = dz(in.pad_ly); right = dz(in.pad_lx);
            seaTabLook(&seaCam, dz(in.pad_rx) * 1.5f * dt, -dz(in.pad_ry) * 1.5f * dt);
            if (in.pad_buttons & GE_PAD_B) fast = 10;
            static uint32_t prevPad = 0;
            const uint32_t down = in.pad_buttons & ~prevPad;
            prevPad = in.pad_buttons;
            if (down & GE_PAD_RSHOULDER) seaTabSetRep(seaTabRep() * 2);
            if (down & GE_PAD_LSHOULDER) seaTabSetRep(seaTabRep() / 2);
            if (down & GE_PAD_BACK) seaTabCycleScale();
            if (down & GE_PAD_Y) seaTabToggleSway();
            if (down & GE_PAD_X) seaTabToggleFreeze();
#ifndef __SWITCH__
            if (!ImGui::GetCurrentContext() || !ImGui::GetIO().WantCaptureKeyboard) {
                const Uint8 *k = SDL_GetKeyboardState(nullptr);
                if (k[SDL_SCANCODE_LSHIFT] || k[SDL_SCANCODE_RSHIFT]) fast = 10;
                if (k[SDL_SCANCODE_W]) fwd += 1;
                if (k[SDL_SCANCODE_S]) fwd -= 1;
                if (k[SDL_SCANCODE_D]) right += 1;
                if (k[SDL_SCANCODE_A]) right -= 1;
                if (k[SDL_SCANCODE_SPACE] || k[SDL_SCANCODE_E]) up += 0.5f;
                if (k[SDL_SCANCODE_LCTRL] || k[SDL_SCANCODE_Q]) up -= 0.5f;
                static bool rDown = false, fDown = false, mDown = false;
                if (k[SDL_SCANCODE_R] && !rDown) seaTabSetRep(seaTabRep() * 2);
                if (k[SDL_SCANCODE_F] && !fDown) seaTabSetRep(seaTabRep() / 2);
                if (k[SDL_SCANCODE_M] && !mDown) seaTabCycleScale();
                rDown = k[SDL_SCANCODE_R]; fDown = k[SDL_SCANCODE_F]; mDown = k[SDL_SCANCODE_M];
            }
            {   // right mouse drag looks around (outside the panel)
                static int lmx = 0, lmy = 0;
                if ((in.mouse_buttons & 2) && in.mouse_x >= x0) seaTabLook(&seaCam, (in.mouse_x - lmx) * 0.0025f, (in.mouse_y - lmy) * 0.0025f);
                lmx = in.mouse_x; lmy = in.mouse_y;
            }
#endif
            const float speed = dt * 60.0f * fast;
            seaTabMove(&seaCam, fwd * speed, right * speed, up * speed);
            const int vp[4] = { scene.x, 0, scene.w, H };
            if (seaLoaded) seaTabFrame(headless ? 1.0f / 60.0f : dt, &seaCam, vp);
            {   // the status (variants, load, GPU time) in the log once a second: the console's numbers come back over nxlink
                static auto seaLog = now;
                if (seaLoaded && std::chrono::duration<float>(now - seaLog).count() >= 1.0f) { seaLog = now; logf("sea: %s", seaTabStatus()); }
            }
            api()->set_viewport(0, 0, (int)w, (int)h);
            api()->set_scissor(0, 0, (int)w, (int)h);
            if (!showUi) labels.push_back({ scene, seaTabStatus() });
        } else if (isTab(a)) {
            const int ts = tshowOf(a);
            auto paneDraw = [&](const Pane &p, bool port, int c0, int c1) { drawTabPane(a, p, H, port, c0, c1); };
            if (ts == 2) {
                const int half = scene.w / 2;
                const Pane l = { scene.x, 0, half - 1, H }, r = { scene.x + half + 1, 0, scene.w - half - 1, H };
                paneDraw(l, false, l.x, l.x + l.w); paneDraw(r, true, r.x, r.x + r.w);
                labels.push_back({ l, "GAME (F9 off: the game's own look)" }); labels.push_back({ r, "PORT (effect shaders + lights)" });
            } else {
                paneDraw(scene, ts == 1, scene.x, scene.x + scene.w);
                labels.push_back({ scene, ts == 1 ? "PORT (effect shaders + lights)" : "GAME (F9 off: the game's own look)" });
            }
            labels.push_back({ scene, tabCaption(a) });
        } else if (a.mode == "decal" && a.view == "sheet") {
            drawSheet(a, scene, H);
            char cap[200];
            snprintf(cap, sizeof cap, "%s textures (%s): %d, page %d/%d  -  click one to put it on the wall (or the wall texture field)", kMats[matIndex(a.mat)].name,
                     fxHitName(kMats[matIndex(a.mat)].hit), (int)sheetList(a).size(), a.page + 1, std::max(1, sheetPages(a)));
            labels.push_back({ scene, cap });
        } else if (a.mode == "cluster") {
            const Cam cam = makeCam(a.fov, (float)w / (float)h);
            if (f == 0) {   // #141: what the Lights block carries, and the eye position it rebuilds for some wall points
                const GfxClipParameters cp = api()->get_clip_parameters();
                logf("clip parameters: z_is_from_0_to_1=%d invert_y=%d  T invertible=%d", (int)cp.z_is_from_0_to_1, (int)cp.invert_y, (int)cam.ok);
                for (int r = 0; r < 4; r++) logf("inv_proj[%d] = % .6f % .6f % .6f % .6f", r, cam.inv[r * 4], cam.inv[r * 4 + 1], cam.inv[r * 4 + 2], cam.inv[r * 4 + 3]);
                const float pts[2][3] = { { 0, 0, -a.dist }, { 10, 5, -a.dist } };
                for (const auto &e : pts) {
                    float c4[4]; clipOf(cam, e, c4);
                    float r[4];
                    for (int j = 0; j < 4; j++) r[j] = c4[0] * cam.inv[j] + c4[1] * cam.inv[4 + j] + c4[2] * cam.inv[8 + j] + c4[3] * cam.inv[12 + j];
                    logf("eye (% .2f % .2f % .2f) -> eye' (% .3f % .3f % .3f)", e[0], e[1], e[2], r[0] / r[3], r[1] / r[3], r[2] / r[3]);
                }
            }
            if (a.bench) {   // the cluster fills the screen's height; phases of 120 frames alternate the full shader and coverage only
                static double acc[2]; static int nacc[2];
                const int phase = (f / 120) % 2;
                a.diag = phase ? "coverage" : "";
                buildCluster(ccluster, a);
                const float tf = tanf(a.fov * 0.5f * 3.14159265f / 180.0f);
                a.dist = ccluster.size * 0.5f / tf * 0.95f;
                clusterScene(a, ccluster, cam);
                const double ms = ge_render_gpu_timer_ms(1);
                if (f % 120 >= 20 && ms >= 0) { acc[phase] += ms; nacc[phase]++; }
                if (f % 120 == 119 && nacc[phase] == 0) logf("BENCH %s: no GPU time measured (last %.3f)", phase ? "coverage-only" : "fxClusterPom", ms);
                if (f % 120 == 119 && nacc[phase] > 0) {
                    const float cover = 0.95f * 0.95f * (float)h / (float)w;   // the cluster quad's share of the window
                    logf("BENCH %s rep=%d window=%dx%d cluster=%.0f%% of it: gpu %.3f ms (avg of %d frames)", phase ? "coverage-only" : "fxClusterPom",
                         a.rep, w, h, cover * 100.0f, acc[phase] / nacc[phase], nacc[phase]);
                    acc[phase] = 0; nacc[phase] = 0;
                }
            } else {
                clusterScene(a, ccluster, cam);
            }
        } else {
            const int v = keyIndex(kViewKey, a.view);   // 0 side, 1 split, 2 original, 3 effect
            const bool sprite = a.mode == "sprite";
            const Sprite spr = sprite ? buildSprite(a, kindIndex(a.kind)) : Sprite();
            auto paneDraw = [&](const Pane &p, bool fx, int c0, int c1) { if (sprite) drawSpritePane(a, spr, p, H, fx, c0, c1); else drawDecalPane(a, dcluster, p, H, fx, c0, c1); };
            const char *lo = sprite ? "ORIGINAL (as the game draws it)" : "GAME: the holes as the game draws them";
            const char *lf = sprite ? "EFFECT (the pack's effect shader)" : "PORT: one cluster (fxClusterPom)";
            if (v == 0) {
                const int half = scene.w / 2;
                const Pane l = { scene.x, 0, half - 1, H }, r = { scene.x + half + 1, 0, scene.w - half - 1, H };
                paneDraw(l, false, l.x, l.x + l.w); paneDraw(r, true, r.x, r.x + r.w);
                labels.push_back({ l, lo }); labels.push_back({ r, lf });
            } else if (v == 1) {
                const int sx = scene.x + (int)(scene.w * fminf(fmaxf(a.split, 0.0f), 1.0f));
                paneDraw(scene, false, scene.x, sx); paneDraw(scene, true, sx, scene.x + scene.w);
                api()->set_viewport(0, 0, (int)w, (int)h);
                api()->set_scissor(sx - 1, 0, 2, H);
                const float wc[4] = { 1, 1, 1, 1 };
                quadFlat(-1, -1, 1, 1, wc);
                labels.push_back({ scene, std::string(lo) + "   |   " + lf });
            } else {
                paneDraw(scene, v == 3, scene.x, scene.x + scene.w);
                labels.push_back({ scene, v == 3 ? lf : lo });
            }
            if (sprite) labels.push_back({ scene, spr.label });
        }
        api()->set_viewport(0, 0, (int)w, (int)h);
        api()->set_scissor(0, 0, (int)w, (int)h);

        if (showUi || a.mode != "cluster") {
            if (api()->set_lights) { GfxLights none = {}; api()->set_lights(&none); }   // the panel is drawn unlit (the scene's lights stay out of it)
            ge_imgui_new_frame((int)w, (int)h, g_wheel);
            g_wheel = 0.0f;
            if (showUi) uiPanel(a, (int)w, (int)h, names[ge_render_backend()], status, shotReq);
            for (size_t i = 0; i < labels.size(); i++) labelAt(labels[i].first, labels[i].second.c_str(), (a.mode == "sprite" || isTab(a)) && i + 1 == labels.size());
            if (a.mode == "decal" && a.view == "sheet") {
                sheetLabels(a);
                const ImGuiIO &io = ImGui::GetIO();
                if (showUi && !io.WantCaptureMouse && ImGui::IsMouseClicked(0))
                    for (const SheetCell &c : g_sheet)
                        if (io.MousePos.x >= c.x && io.MousePos.x < c.x + c.w && io.MousePos.y >= c.y && io.MousePos.y < c.y + c.h) { a.walltex = c.tex; a.view = "side"; break; }
            }
            ge_imgui_render();
        }

        ge_render_end_frame();
        // one readback rectangle (the probes' bounding box): a backend takes one read request per present
        int bx0 = 1 << 30, by0 = 1 << 30, bx1 = -1, by1 = -1;
        for (const auto &p : a.probes) { bx0 = std::min(bx0, p.first); by0 = std::min(by0, p.second); bx1 = std::max(bx1, p.first); by1 = std::max(by1, p.second); }
        std::vector<uint8_t> box, shotPix;
        const bool probeNow = headless && f == shotFrame;
        const bool wantShot = (probeNow && !a.shot.empty()) || g_f12 || shotReq;
        std::string shotPath = a.shot;
        if (wantShot && shotPath.empty()) { char nb[64]; snprintf(nb, sizeof nb, "shot_%03d.png", ++shotN); shotPath = a.out + nb; }
        g_f12 = false; shotReq = false;
        if (probeNow && !a.probes.empty()) {
            box.resize((size_t)(bx1 - bx0 + 1) * (by1 - by0 + 1) * 4);
            if (!ge_render_read_pixels(bx0, by0, bx1 - bx0 + 1, by1 - by0 + 1, box.data())) logf("readback: this backend cannot read the window back (%s)", names[ge_render_backend()]);
        }
        if (probeNow && !a.dump.empty()) ge_render_dump_window(w, h, a.dump.c_str());
        if (wantShot && a.probes.empty()) { shotPix.resize((size_t)w * h * 4); if (!ge_render_read_pixels(0, 0, (int)w, (int)h, shotPix.data())) shotPix.clear(); }
        ge_render_present();
        if (wantShot) {
            if (a.probes.empty() && shotPix.empty()) { status = "shot: this backend cannot read the window back"; logf("%s", status.c_str()); }
            else if (!a.probes.empty()) { status = "shot: not with --probe (one readback per frame)"; logf("%s", status.c_str()); }
            else if (gePngWrite(shotPath.c_str(), (int)w, (int)h, shotPix.data(), GE_PNG_RGB)) { status = "shot: " + shotPath; logf("shot written %s", shotPath.c_str()); }
            else { status = "shot: cannot write " + shotPath; logf("%s", status.c_str()); }
        }
        if (probeNow) {
            logf("frame %d read back, window %ux%u", f, w, h);
            for (const auto &p : a.probes) {
                const uint8_t *c = &box[((size_t)(p.second - by0) * (bx1 - bx0 + 1) + (p.first - bx0)) * 4];
                logf("PROBE %d %d %d %d %d %d", p.first, p.second, c[0], c[1], c[2], c[3]);
            }
        }
        if (headless && f >= shotFrame + a.hold) break;
        if (a.frames && f + 1 >= a.frames) break;
        GeRenderInput in;
        ge_render_poll_input(&in);
        if (in.pad_buttons & GE_PAD_START) break;
    }
    if (!headless) saveIni(a, g_iniPath);
    if (seaStarted) {
        seaTabShutdown();
    }
    ge_imgui_shutdown();
    ge_render_park();
    ge_render_close();
    if (g_log) fclose(g_log);
#ifdef __SWITCH__
    socketExit();
#endif
    return 0;
}
