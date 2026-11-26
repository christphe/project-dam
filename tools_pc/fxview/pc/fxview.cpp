// fxview: GoldenEye's combat-effect sprites up close, ORIGINAL (as the game draws them) vs EFFECT (+ fxShade).
// The sprites are decoded from the user's ROM at run time (modelview's decoder, pc/mv_tex.cpp); the effect shaders are
// read from port/shaders/fx/ (common.glsl, bullets.glsl, bursts.glsl, impacts.glsl: the files tools_pc/dkshgen pastes into
// the game's shaders) and hot-reloaded on change.
// UI: Dear ImGui panel on the left. Wheel = zoom, Esc quit, F12 screenshot (data/shot_NNN.png).
// args: --repo DIR --rom FILE.z64 --shader DIR --kind tracer|laser|watchlaser|wallpuff|bloodpuff|scatter|impact
//       --impact N --age F --angle F --view side|split|orig|fx --backdrop grey|dark|checker|wall --bgtex N
//       --zoom F --fc N (frame_count) --nearest 0|1 --shot f.png --w N --h N --selftest
#include <windows.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <map>
#include <string>
#include <vector>
#include <array>
#include "fx_tex.h"
#include "fx_cluster.h"   // the hole templates and clusters (prototype of port/src/fxcluster.c)
#include "mv_png.h"
#include "mv_imgui.h"
#include "imgui.h"

#define GLFUNCS \
 X(PFNGLCREATESHADERPROC,glCreateShader) X(PFNGLSHADERSOURCEPROC,glShaderSource) X(PFNGLCOMPILESHADERPROC,glCompileShader) \
 X(PFNGLGETSHADERIVPROC,glGetShaderiv) X(PFNGLGETSHADERINFOLOGPROC,glGetShaderInfoLog) X(PFNGLCREATEPROGRAMPROC,glCreateProgram) \
 X(PFNGLATTACHSHADERPROC,glAttachShader) X(PFNGLLINKPROGRAMPROC,glLinkProgram) X(PFNGLGETPROGRAMIVPROC,glGetProgramiv) \
 X(PFNGLGETPROGRAMINFOLOGPROC,glGetProgramInfoLog) X(PFNGLUSEPROGRAMPROC,glUseProgram) X(PFNGLGETUNIFORMLOCATIONPROC,glGetUniformLocation) \
 X(PFNGLDELETEPROGRAMPROC,glDeleteProgram) X(PFNGLDELETESHADERPROC,glDeleteShader) \
 X(PFNGLUNIFORMMATRIX4FVPROC,glUniformMatrix4fv) X(PFNGLUNIFORM1IPROC,glUniform1i) X(PFNGLUNIFORM2FPROC,glUniform2f) X(PFNGLUNIFORM1FPROC,glUniform1f) \
 X(PFNGLUNIFORM4FPROC,glUniform4f) X(PFNGLUNIFORM1UIPROC,glUniform1ui) X(PFNGLUNIFORM4FVPROC,glUniform4fv) X(PFNGLUNIFORM4IPROC,glUniform4i) \
 X(PFNGLGENERATEMIPMAPPROC,glGenerateMipmap) \
    X(PFNGLACTIVETEXTUREPROC,glActiveTexture) \
 X(PFNGLGENVERTEXARRAYSPROC,glGenVertexArrays) X(PFNGLBINDVERTEXARRAYPROC,glBindVertexArray) \
 X(PFNGLGENBUFFERSPROC,glGenBuffers) X(PFNGLBINDBUFFERPROC,glBindBuffer) X(PFNGLBUFFERDATAPROC,glBufferData) \
 X(PFNGLVERTEXATTRIBPOINTERPROC,glVertexAttribPointer) X(PFNGLENABLEVERTEXATTRIBARRAYPROC,glEnableVertexAttribArray) \
 X(PFNGLGENFRAMEBUFFERSPROC,glGenFramebuffers) X(PFNGLBINDFRAMEBUFFERPROC,glBindFramebuffer) X(PFNGLDELETEFRAMEBUFFERSPROC,glDeleteFramebuffers) \
 X(PFNGLGENRENDERBUFFERSPROC,glGenRenderbuffers) X(PFNGLBINDRENDERBUFFERPROC,glBindRenderbuffer) X(PFNGLRENDERBUFFERSTORAGEPROC,glRenderbufferStorage) \
 X(PFNGLFRAMEBUFFERRENDERBUFFERPROC,glFramebufferRenderbuffer) X(PFNGLDELETERENDERBUFFERSPROC,glDeleteRenderbuffers) X(PFNGLCHECKFRAMEBUFFERSTATUSPROC,glCheckFramebufferStatus)
#define X(T,N) T N = 0;
GLFUNCS
#undef X
typedef HGLRC (WINAPI *PFNWGLCREATECONTEXTATTRIBSARB)(HDC, HGLRC, const int*);
typedef BOOL (WINAPI *PFNWGLSWAPINTERVAL)(int);

// ---------------------------------------------------------------- small helpers
static bool readFile(const std::string& p, std::string& out) {
    FILE* f = fopen(p.c_str(), "rb"); if (!f) return false;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    out.resize((size_t)(n > 0 ? n : 0)); const bool ok = n <= 0 || fread(&out[0], 1, (size_t)n, f) == (size_t)n; fclose(f); return ok;
}
static uint64_t fileStamp(const std::string& p) {
    WIN32_FILE_ATTRIBUTE_DATA d; if (!GetFileAttributesExA(p.c_str(), GetFileExInfoStandard, &d)) return 0;
    return ((uint64_t)d.ftLastWriteTime.dwHighDateTime << 32) | d.ftLastWriteTime.dwLowDateTime;
}
// "<key> 0x1234" / "<key> = 0x1234" / ".set <key>, 0x1234" in a text: the number after the key, or -1
static long long findHexAfter(const std::string& text, const std::string& key) {
    size_t p = 0;
    while ((p = text.find(key, p)) != std::string::npos) {
        size_t q = p + key.size();
        if (q < text.size() && (isalnum((unsigned char)text[q]) || text[q] == '_')) { p = q; continue; }   // a longer name
        while (q < text.size() && (text[q] == ' ' || text[q] == '\t' || text[q] == '=' || text[q] == ',')) q++;
        return strtoll(text.c_str() + q, nullptr, 0);
    }
    return -1;
}

// ---------------------------------------------------------------- the effect shaders (single source of truth: port/shaders/fx/)
// The files tools_pc/dkshgen pastes into the game's effect shaders, in its order: common.glsl first. Each gets a
// "#line 1 <n>" (n = its index here), so a compile error names the file and its line.
static const char* kFxFiles[4] = { "common.glsl", "bullets.glsl", "bursts.glsl", "impacts.glsl" };
static bool readFxSources(const std::string& dir, std::string& all, std::string& err) {
    all.clear();
    for (int i = 0; i < 4; i++) {
        std::string src;
        if (!readFile(dir + "/" + kFxFiles[i], src)) { err = "cannot read " + dir + "/" + kFxFiles[i]; return false; }
        all += "#line 1 " + std::to_string(i) + "\n" + src + "\n";
    }
    return true;
}
// The viewer's own dispatch by PORTFX kind (the game calls each effect's function directly, dkshgen).
static const char* kFxDispatch = "vec4 fxShade(vec4 c, uint kind, vec2 uv) {\n"
    "    if (kind == 2u) return fxTracer(c);\n    if (kind == 3u) return fxSpark(c);\n"
    "    if (kind == 4u) return fxBlood(c);\n    if (kind == 5u) return fxImpact(c, uv);\n    return c;\n}\n";
// The uber shader's dynLight (the Enhanced per-pixel lights; it lights the wall under the holes): from "vec3 dynLight("
// to the first "}" at the start of a line. Empty when not found.
static std::string extractDynLight(const std::string& src) {
    const size_t b = src.find("vec3 dynLight(");
    if (b == std::string::npos) return "";
    const size_t e = src.find("\n}", b);
    if (e == std::string::npos) return "";
    const int line = 1 + (int)std::count(src.begin(), src.begin() + (long)b, '\n');
    return "#line " + std::to_string(line) + "\n" + src.substr(b, e + 2 - b) + "\n";
}

// ---------------------------------------------------------------- shaders
static const char* kVert = R"(#version 330 core
layout(location=0) in vec2 aP; layout(location=1) in vec2 aUV; layout(location=2) in vec4 aC;
uniform vec4 uRect;   // pane centre (px) and half size (px)
out vec2 vUV; out vec4 vC; out vec2 vPx;
void main() { vUV = aUV; vC = aC; vPx = aP; gl_Position = vec4(aP / uRect.zw, 0.0, 1.0); }
)";
// The game's state for these sprites: G_CC_MODULATEIA (or MODULATEI) = TEXEL0 * SHADE for rgb and alpha, then in the
// port fxShade(texel, kind, vTexCoord0) on the combined colour, then the blender (CLD_SURF / XLU: src alpha over).
static const char* kSpriteHead = R"(#version 330 core
in vec2 vUV; in vec4 vC; in vec2 vPx;
uniform sampler2D uTex; uniform int uFx; uniform int uKind;
uniform int frame_count;
// what fx/impacts.glsl reads (the clusters, not drawn here)
uniform sampler2D uTex0; uniform sampler2D uTex1;
vec2 vTexCoord0; vec3 vEye; ivec4 lcount; vec4 lpos[16]; vec4 lcol[16]; vec4 sun_dir;
out vec4 o;
)";
static const char* kSpriteMain = R"(
void main() {
    vec4 c = texture(uTex, vUV) * vC;
    if (uFx != 0) c = fxShade(c, uint(uKind), vUV);
    o = c;
}
)";
static const char* kFallbackBlock = "vec4 fxShade(vec4 c, uint kind, vec2 uv) { return c; }\n";
static const char* kBackFrag = R"(#version 330 core
in vec2 vUV; in vec4 vC; in vec2 vPx;
uniform int uMode; uniform sampler2D uTex; uniform float uTile;
out vec4 o;
void main() {
    if (uMode == 1) { vec2 q = floor(vPx / 16.0); o = vec4(vec3(mod(q.x + q.y, 2.0) < 1.0 ? 0.62 : 0.38), 1.0); }
    else if (uMode == 2) o = vec4(texture(uTex, vPx / uTile * vec2(1.0, -1.0)).rgb, 1.0);
    else o = vC;
}
)";

// Decal mode: a wall in 3D, then the holes on it. The fragment shader provides what the effect files use from the
// game's shaders, with the same names: vEye (eye space), vTexCoord0, uTex0 / uTex1, the Lights block's lcount / sun_dir /
// sun_col / lpos / lcol (plain uniforms here), frame_count.
// uMode 0 = the wall or the game's own hole (TEXEL0 x SHADE, then the uber shader's dynLight), 4 = a cluster.
static const char* kDecalVert = R"(#version 330 core
layout(location=0) in vec3 aP; layout(location=1) in vec2 aUV; layout(location=2) in vec4 aC;
uniform mat4 uView; uniform mat4 uProj;
out vec2 vTexCoord0; out vec4 vC; out vec3 vEye;
void main() { vec4 e = uView * vec4(aP, 1.0); vEye = e.xyz; vTexCoord0 = aUV; vC = aC; gl_Position = uProj * e; }
)";
static const char* kDecalHead = R"(#version 330 core
in vec2 vTexCoord0; in vec4 vC; in vec3 vEye;
uniform sampler2D uTex0;
uniform sampler2D uTex1;   // clusters: the fused depth
uniform ivec4 lcount; uniform vec4 sun_dir; uniform vec4 sun_col; uniform vec4 lpos[16]; uniform vec4 lcol[16];
uniform uint flags; uniform int fog_pixel; uniform int frame_count;
const vec4 vFog = vec4(0.0); const float vFogW = 0.0;
#define CF_FOG (1u << 1)
vec4 texVal0;
uniform int uMode; uniform int uKind; uniform int uLit;
out vec4 o;
)";
static const char* kDecalMain = R"(
void main() {
    texVal0 = texture(uTex0, vTexCoord0);
    if (uMode == 4) {   // a cluster (fxClusterPom): fused colour and depth, then dynLight like the game
        vec4 d = fxClusterPom(vC);
        if (uLit != 0) d.rgb = min(dynLight(d.rgb, d.rgb), vec3(1.0));
        o = d; return;
    }
    vec4 c = texVal0 * vC;
    if (uLit != 0) c.rgb = min(dynLight(c.rgb, texVal0.rgb), vec3(1.0));
    o = c;
}
)";
static const char* kFallbackDynLight = "vec3 dynLight(vec3 base, vec3 albedo) { return base; }\n";
static const char* kFallbackDecal = "vec4 fxClusterPom(vec4 c) { return texVal0; }\n";

static std::string g_shaderLog;   // the last compile / link log
static GLuint compile(GLenum t, const char* src) {
    GLuint s = glCreateShader(t); glShaderSource(s, 1, &src, 0); glCompileShader(s);
    GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[8192]; glGetShaderInfoLog(s, sizeof log, 0, log); g_shaderLog = log; glDeleteShader(s); return 0; }
    return s;
}
static GLuint link(const char* vs, const char* fs) {
    GLuint a = compile(GL_VERTEX_SHADER, vs); if (!a) return 0;
    GLuint b = compile(GL_FRAGMENT_SHADER, fs); if (!b) { glDeleteShader(a); return 0; }
    GLuint p = glCreateProgram(); glAttachShader(p, a); glAttachShader(p, b); glLinkProgram(p);
    glDeleteShader(a); glDeleteShader(b);
    GLint ok = 0; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { char log[8192]; glGetProgramInfoLog(p, sizeof log, 0, log); g_shaderLog = log; glDeleteProgram(p); return 0; }
    return p;
}
GLuint mv_program(const char* vs, const char* fs, const char* name) {   // for mv_imgui.cpp
    GLuint p = link(vs, fs); if (!p) fprintf(stderr, "SHADER %s:\n%s\n", name, g_shaderLog.c_str()); return p;
}

// ---------------------------------------------------------------- ROM: the global image table and its textures
struct ImgEntry { uint32_t index = 0; int w = 0, h = 0, level = 0, format = 0, depth = 0, flagsS = 0, flagsT = 0; };
static std::vector<uint8_t> g_rom;
static char g_region = 0;                                  // 'u' 'e' 'j'
static std::map<std::string, std::vector<ImgEntry>> g_tables;   // impactimages, explosion_smokeimages, ...
static std::string g_romNote = "no ROM";

static bool romOpen(const std::string& path, const std::string& repo) {
    std::string s; if (!readFile(path, s) || s.size() < 0x400000) { g_romNote = "cannot read the ROM"; return false; }
    g_rom.assign(s.begin(), s.end());
    switch (g_rom[0x3E]) { case 'E': g_region = 'u'; break; case 'P': g_region = 'e'; break; case 'J': g_region = 'j'; break; default: g_romNote = "unrecognised ROM (a big-endian .z64 is expected)"; g_rom.clear(); return false; }
    // the segment's ROM address (port/src/romassets_<r>.s) and the table offsets in it (src/game/image_bank.c)
    std::string as, bank;
    if (!readFile(repo + "/port/src/romassets_" + g_region + ".s", as) || !readFile(repo + "/src/game/image_bank.c", bank)) { g_romNote = "cannot read port/src/romassets_*.s or src/game/image_bank.c"; return false; }
    const long long seg = findHexAfter(as, ".set _GlobalimagetableSegmentRomStart"), segEnd = findHexAfter(as, ".set _GlobalimagetableSegmentRomEnd");
    if (seg < 0 || segEnd <= seg) { g_romNote = "no _GlobalimagetableSegmentRomStart"; return false; }
    const size_t base = (size_t)(seg & 0x0FFFFFFF);   // cartridge address 0x10xxxxxx -> ROM file offset
    // each table runs to the next symbol (image_bank.c lists them in segment order)
    static const char* names[] = { "impactimages", "explosion_smokeimages", "scattered_explosions", "flareimage1", "flareimage2", "flareimage3", "flareimage4", "flareimage5", "ammo9mmimage" };
    for (int i = 0; i + 1 < (int)(sizeof names / sizeof *names); i++) {
        const long long a = findHexAfter(bank, std::string("g_pc_gimg_off_s_") + names[i]), b = findHexAfter(bank, std::string("g_pc_gimg_off_s_") + names[i + 1]);
        if (a < 0 || b <= a) { g_romNote = std::string("no offset for ") + names[i]; return false; }
        std::vector<ImgEntry> t;
        for (long long o = a; o + 12 <= b; o += 12) {
            const uint8_t* e = &g_rom[base + (size_t)o];
            ImgEntry x; x.index = ((uint32_t)e[0] << 24) | ((uint32_t)e[1] << 16) | ((uint32_t)e[2] << 8) | e[3];
            x.w = e[4]; x.h = e[5]; x.level = e[6]; x.format = e[7]; x.depth = e[8]; x.flagsS = e[9]; x.flagsT = e[10];
            t.push_back(x);
        }
        g_tables[names[i]] = t;
    }
    if (!mv_tex_set_imagelist(repo + "/imagelist." + g_region + ".csv") || !fx_tex_set_imagelist(repo + "/imagelist." + g_region + ".csv")) { g_romNote = "no imagelist.<region>.csv"; return false; }
    char b[96]; snprintf(b, sizeof b, "ROM ok (region %c, image table at 0x%X)", g_region, (unsigned)base); g_romNote = b;
    return true;
}
static const ImgEntry* tableEntry(const char* table, int i) {
    auto it = g_tables.find(table); if (it == g_tables.end() || i < 0 || i >= (int)it->second.size()) return nullptr;
    return &it->second[(size_t)i];
}

struct GlTex { GLuint id = 0; int w = 0, h = 0; std::string note; };
static std::map<std::string, GlTex> g_gl;   // key: texture number + wrap
static GLuint g_white = 0;
static bool g_nearest = false;
static GLint wrapOf(int flags) { return flags == 2 ? 0x812F /*clamp to edge*/ : flags == 1 ? 0x8370 /*mirrored repeat*/ : GL_REPEAT; }   // G_TX_CLAMP / G_TX_MIRROR / G_TX_WRAP
static const GlTex& texture(uint32_t tn, int flagsS, int flagsT) {
    char key[64]; snprintf(key, sizeof key, "%u/%d/%d", tn, flagsS, flagsT);
    auto it = g_gl.find(key);
    if (it != g_gl.end()) return it->second;
    GlTex t; MvTexImage img;
    if (!g_rom.empty() && fx_tex_load(g_rom.data(), g_rom.size(), (int)tn, img) && img.w > 0) {
        glGenTextures(1, &t.id); glBindTexture(GL_TEXTURE_2D, t.id);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, img.w, img.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, img.rgba.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapOf(flagsS)); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapOf(flagsT));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);   // no mipmaps
        t.w = img.w; t.h = img.h;
    }
    t.note = img.note;
    if (!t.id) fprintf(stderr, "texture %u not decoded: %s\n", tn, img.note.c_str());
    return g_gl.emplace(key, t).first->second;
}

// ---------------------------------------------------------------- what is shown
enum { K_TRACER = 0, K_LASER, K_WATCHLASER, K_WALLPUFF, K_BLOODPUFF, K_SCATTER, K_IMPACT, K_COUNT };
static const char* kKindKey[K_COUNT] = { "tracer", "laser", "watchlaser", "wallpuff", "bloodpuff", "scatter", "impact" };
static const char* kKindName[K_COUNT] = { "Tracer (guns, flareimage3)", "Laser tracer (flareimage4)", "Watch laser (flareimage4)", "Wall puff (smoke, type 1)",
                                          "Blood puff (smoke, type 2)", "Scatter puff (type 3)", "Bullet-hole decal (impactimages)" };
#include "../../../port/include/portfx.h"   // PORTFX_* (the shared kinds)
static int fxKindOf(int k) { return k <= K_WATCHLASER ? PORTFX_TRACER : k == K_BLOODPUFF ? PORTFX_BLOOD : k == K_IMPACT ? PORTFX_IMPACT : PORTFX_SPARK; }
enum { VIEW_SIDE = 0, VIEW_SPLIT, VIEW_ORIG, VIEW_FX };
static const char* kViewKey[] = { "side", "split", "orig", "fx", "sheet" };   // sheet: decal mode only
enum { BG_GREY = 0, BG_DARK, BG_CHECKER, BG_WALL };
static const char* kBgKey[] = { "grey", "dark", "checker", "wall" };

static int g_kind = K_WALLPUFF, g_impact = 0, g_view = VIEW_SIDE, g_bg = BG_GREY, g_bgTex = 20;   // 20: a grey concrete texture (NTSC numbering)
static float g_age = 0, g_angle = 0, g_zoom = 1.0f, g_split = 0.5f, g_speed = 1.0f, g_tracerLen = 8.0f;
static bool g_play = false, g_animFx = true;
static int g_fc = 0;                                    // frame_count given to fxShade
static uint8_t g_blood[3] = { 0x8C, 0x0A, 0x0A };       // port/src/switch/nxgame.c BLOOD_R/G/B, read at start
static unsigned g_seed = 1;                             // decal vertex colours (the game draws them with randomGetNext)

// the puff's image table, lifetime and frame step (bullet_sparks_init)
static void puffParams(int k, const char** table, int* lifetime, float* step) {
    if (k == K_SCATTER) { *table = "scattered_explosions"; *lifetime = 9; *step = 0.5f; }
    else { *table = "explosion_smokeimages"; *lifetime = 11; *step = 0.5f; }   // type 1 and the default branch (type 2)
}
static int puffFrame(float age, float step, int nframes) { int f = (int)(floorf(age) * step); return f < 0 ? 0 : f >= nframes ? nframes - 1 : f; }

struct Vert { float x, y, u, v, r, g, b, a; };
struct Sprite { std::vector<Vert> v; std::vector<uint16_t> idx; const ImgEntry* img = nullptr; std::string label; };

static unsigned lcg(unsigned& s) { s = s * 1103515245u + 12345u; return (s >> 16) & 0x7FFF; }
// src/game/explosion.c g_ImpactTypes[]: half width, half height (world units), unk1 (the vertex colour scheme)
struct ImpactType { float w, h; int app; };
static const ImpactType kImpactTypes[20] = { {10,10,1},{6,6,1},{8,8,0},{20,20,1},{6,6,1},{8,8,1},{12,12,1},{6,6,1},{20,20,1},{20,20,1},
                                             {20,20,1},{20,20,1},{20,20,1},{24,24,1},{6,6,1},{6,6,1},{24,24,2},{6,6,1},{8,8,1},{12,12,1} };
// explosion.c: the hole's vertex colour by unk1 (randomGetNext): 1 light 216..255 grey, 0 dark 0..39 grey, 2 random primaries
static void impactVertexColour(int app, unsigned& seed, float c[3]) {
    if (app == 1) { const float v = (255 - lcg(seed) % 40) / 255.0f; c[0] = c[1] = c[2] = v; }
    else if (app == 0) { const float v = (lcg(seed) % 40) / 255.0f; c[0] = c[1] = c[2] = v; }
    else { c[0] = (lcg(seed) & 1) ? 1.0f : 0.0f; c[2] = (lcg(seed) & 1) ? 1.0f : 0.0f; c[1] = (lcg(seed) & 1) ? 1.0f : 0.0f; }
}

// The sprite in game units (x right, y up), with the game's corner order and texture coordinates (normalised).
static Sprite buildSprite() {
    Sprite s; char lab[160];
    if (g_kind <= K_WATCHLASER) {   // gunfire.c sub_GAME_7F061E18: a quad from the muzzle (+-right) to the far end (+-0.9 right)
        s.img = tableEntry(g_kind == K_TRACER ? "flareimage3" : "flareimage4", 0);
        const float radius = g_kind == K_TRACER ? 30.0f : g_kind == K_LASER ? 50.0f : 10.0f;
        const float L = 2.0f * radius * g_tracerLen;   // flareoffset = delta * dist * 10: the length depends on the shot
        const float a = g_kind == K_WATCHLASER ? 175.0f / 255.0f : 1.0f;   // watch laser: alpha 150..199 at random
        s.v = { { 0, radius, 1, 0, 1, 1, 1, a }, { 0, -radius, 0, 0, 1, 1, 1, a }, { L, 0.9f * radius, 1, 1, 1, 1, 1, a }, { L, -0.9f * radius, 0, 1, 1, 1, 1, a } };
        s.idx = { 0, 2, 3, 0, 3, 1 };
        snprintf(lab, sizeof lab, "%s  tex %u  %dx%d  radius %.0f  vtx colour %s", kKindName[g_kind], s.img ? s.img->index : 0, s.img ? s.img->w : 0, s.img ? s.img->h : 0, radius, g_kind == K_WATCHLASER ? "white a175" : "white");
    } else if (g_kind == K_IMPACT) {   // explosion.c: a decal quad, vertex colours by g_ImpactTypes[].apptype
        const auto& it = kImpactTypes;
        const int t = g_impact < 0 ? 0 : g_impact > 19 ? 19 : g_impact;
        s.img = tableEntry("impactimages", t);
        unsigned seed = g_seed * 7919u + (unsigned)t;
        const float w = it[t].w, h = it[t].h;
        const float pos[4][2] = { { -w, -h }, { -w, h }, { w, h }, { w, -h } }, uv[4][2] = { { 0, 1 }, { 0, 0 }, { 1, 0 }, { 1, 1 } };
        for (int i = 0; i < 4; i++) {
            float c[3]; impactVertexColour(it[t].app, seed, c);
            s.v.push_back({ pos[i][0], pos[i][1], uv[i][0], uv[i][1], c[0], c[1], c[2], 1.0f });
        }
        s.idx = { 0, 1, 2, 0, 2, 3 };
        static const char* app[3] = { "dark (0..39)", "light (216..255)", "random primaries" };
        snprintf(lab, sizeof lab, "Bullet-hole decal type %d  tex %u  %dx%d  size %.0fx%.0f  vtx colour %s", t, s.img ? s.img->index : 0, s.img ? s.img->w : 0, s.img ? s.img->h : 0, w, h, app[it[t].app]);
    } else {   // glass2.c bullet_spark_render: a diamond turned by the spark's random angle, frame = age * unk08
        const char* table; int life; float step; puffParams(g_kind, &table, &life, &step);
        auto tb = g_tables.find(table); const int nf = tb == g_tables.end() ? 1 : (int)tb->second.size();
        const int fr = puffFrame(g_age, step, nf);
        s.img = tableEntry(table, fr);
        float col[4] = { 1, 1, 1, 1 };
        if (g_kind == K_WALLPUFF) { col[2] = 0xC8 / 255.0f; }   // g_BulletSparkColors[1]
        else if (g_kind == K_BLOODPUFF) { for (int k = 0; k < 3; k++) col[k] = g_blood[k] / 255.0f; }
        const float r = 1.0f, cs = cosf(g_angle) * r, sn = sinf(g_angle) * r;
        // right = (1,0), up = (0,1): s0 = right*cos, s1 = right*sin, s2 = up*cos, s3 = up*sin
        const float p[4][2] = { { -cs, -sn }, { sn, -cs }, { cs, sn }, { -sn, cs } }, uv[4][2] = { { 1, 0 }, { 0, 0 }, { 0, 1 }, { 1, 1 } };
        for (int i = 0; i < 4; i++) s.v.push_back({ p[i][0], p[i][1], uv[i][0], uv[i][1], col[0], col[1], col[2], col[3] });
        s.idx = { 0, 1, 2, 0, 2, 3 };
        snprintf(lab, sizeof lab, "%s  age %d/%d  frame %d/%d  tex %u  vtx colour %02X%02X%02X", kKindName[g_kind], (int)floorf(g_age), life - 1, fr, nf - 1, s.img ? s.img->index : 0,
                 (int)(col[0] * 255 + .5f), (int)(col[1] * 255 + .5f), (int)(col[2] * 255 + .5f));
    }
    s.label = lab;
    return s;
}

// ---------------------------------------------------------------- rendering
static GLuint g_pSprite = 0, g_pBack = 0, g_vao = 0, g_vbo = 0, g_ibo = 0;
static std::string g_shaderPath, g_shaderErr; static uint64_t g_shaderStamp = 0; static int g_reloads = 0;
static std::string g_uberPath;   // dynLight (the Enhanced lights) is the uber shader's; g_shaderPath is the fx/ folder

static GLuint g_pDecal = 0; static std::string g_decalErr; static int g_decalReloads = 0;

// The decal program: the effect files + the uber shader's dynLight.
static void reloadDecal(const std::string& fx, const std::string& dyn, bool readOk, const std::string& readErr) {
    std::string err;
    if (!readOk) err = readErr;
    else {
        const std::string fs = std::string(kDecalHead) + (dyn.empty() ? kFallbackDynLight : dyn.c_str()) + fx + kDecalMain;
        GLuint p = link(kDecalVert, fs.c_str());
        if (p) {
            if (g_pDecal) glDeleteProgram(g_pDecal);
            g_pDecal = p; g_decalErr = dyn.empty() ? "dynLight not found in the shader: the wall is unlit" : ""; g_decalReloads++;
            fprintf(stderr, "impacts loaded (%d)\n", g_decalReloads); return;
        }
        err = "the effect files did not compile (the previous version stays):\n" + g_shaderLog;
    }
    g_decalErr = err; fprintf(stderr, "%s\n", err.c_str());
    if (!g_pDecal) {   // the original side keeps working
        g_pDecal = link(kDecalVert, (std::string(kDecalHead) + kFallbackDynLight + kFallbackDecal + kDecalMain).c_str());
    }
}

static void reloadTab(const std::string& fx, const std::string& dyn, bool readOk, const std::string& readErr);   // fx_tabs.inc
static uint64_t fxStamp() {   // the newest of the effect files
    uint64_t st = 0;
    for (const char* f : kFxFiles) st = std::max(st, fileStamp(g_shaderPath + "/" + f));
    return st;
}
static void reloadShader(bool force) {
    const uint64_t st = fxStamp();
    if (!force && st == g_shaderStamp) return;
    g_shaderStamp = st;
    std::string fx, uber, err;
    const bool readOk = readFxSources(g_shaderPath, fx, err);
    const std::string dyn = readFile(g_uberPath, uber) ? extractDynLight(uber) : "";
    reloadDecal(fx, dyn, readOk, err);
    reloadTab(fx, dyn, readOk, err);
    if (readOk) {
        const std::string fs = std::string(kSpriteHead) + fx + kFxDispatch + kSpriteMain;
        GLuint p = link(kVert, fs.c_str());
        if (p) { if (g_pSprite) glDeleteProgram(g_pSprite); g_pSprite = p; g_shaderErr.clear(); g_reloads++; fprintf(stderr, "effects loaded (%d)\n", g_reloads); return; }
        err = "the effect files did not compile (the previous version stays):\n" + g_shaderLog;
    }
    g_shaderErr = err; fprintf(stderr, "%s\n", err.c_str());
    if (!g_pSprite) g_pSprite = link(kVert, (std::string(kSpriteHead) + kFallbackBlock + kSpriteMain).c_str());   // keep the original side working
}

static void drawVerts(const std::vector<Vert>& v, const std::vector<uint16_t>& idx) {
    glBindVertexArray(g_vao); glBindBuffer(GL_ARRAY_BUFFER, g_vbo); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_ibo);
    glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(Vert), v.data(), GL_STREAM_DRAW);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * 2, idx.data(), GL_STREAM_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vert), (void*)0); glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vert), (void*)8); glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vert), (void*)16); glEnableVertexAttribArray(2);
    glDrawElements(GL_TRIANGLES, (GLsizei)idx.size(), GL_UNSIGNED_SHORT, 0);
}

// One pane (x0,y0 bottom-left in GL pixels, w x h); fx = the effect side. Scissor clips to [clipX0, clipX1).
static void drawPane(const Sprite& s, int x0, int y0, int w, int h, bool fx, int clipX0, int clipX1) {
    glViewport(x0, y0, w, h);
    glEnable(GL_SCISSOR_TEST); glScissor(clipX0, y0, clipX1 - clipX0, h);
    const float hw = w * 0.5f, hh = h * 0.5f;
    // backdrop
    {
        std::vector<Vert> q = { { -hw, -hh, 0, 0, 0, 0, 0, 1 }, { hw, -hh, 0, 0, 0, 0, 0, 1 }, { hw, hh, 0, 0, 0, 0, 0, 1 }, { -hw, hh, 0, 0, 0, 0, 0, 1 } };
        const float g = g_bg == BG_DARK ? 0.07f : 0.45f;
        for (auto& v : q) { v.r = g; v.g = g; v.b = g * 1.03f; }
        int mode = g_bg == BG_CHECKER ? 1 : 0;
        if (g_bg == BG_WALL) {
            const GlTex& t = texture((uint32_t)g_bgTex, 0, 0);
            if (t.id) { mode = 2; glBindTexture(GL_TEXTURE_2D, t.id); }
        }
        glUseProgram(g_pBack);
        glUniform4f(glGetUniformLocation(g_pBack, "uRect"), 0, 0, hw, hh);
        glUniform1i(glGetUniformLocation(g_pBack, "uMode"), mode); glUniform1i(glGetUniformLocation(g_pBack, "uTex"), 0);
        glUniform1f(glGetUniformLocation(g_pBack, "uTile"), 64.0f * 2.0f * g_zoom);   // one wall texel = 2 px at zoom 1
        glDisable(GL_BLEND);
        drawVerts(q, { 0, 1, 2, 0, 2, 3 });
    }
    // sprite: fit its bounding box to the pane, times the zoom
    float mnx = 1e9f, mxx = -1e9f, mny = 1e9f, mxy = -1e9f;
    for (const Vert& v : s.v) { mnx = fminf(mnx, v.x); mxx = fmaxf(mxx, v.x); mny = fminf(mny, v.y); mxy = fmaxf(mxy, v.y); }
    const float cx = (mnx + mxx) * 0.5f, cy = (mny + mxy) * 0.5f;
    const float k = fminf(0.86f * w / fmaxf(mxx - mnx, 1e-3f), 0.72f * h / fmaxf(mxy - mny, 1e-3f)) * g_zoom;
    std::vector<Vert> v = s.v;
    for (Vert& p : v) { p.x = (p.x - cx) * k; p.y = (p.y - cy) * k; }
    GLuint tid = g_white;
    if (s.img) { const GlTex& t = texture(s.img->index, s.img->flagsS, s.img->flagsT); if (t.id) tid = t.id; }
    glBindTexture(GL_TEXTURE_2D, tid);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, g_nearest ? GL_NEAREST : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, g_nearest ? GL_NEAREST : GL_LINEAR);
    glUseProgram(g_pSprite);
    glUniform4f(glGetUniformLocation(g_pSprite, "uRect"), 0, 0, hw, hh);
    glUniform1i(glGetUniformLocation(g_pSprite, "uTex"), 0);
    glUniform1i(glGetUniformLocation(g_pSprite, "uFx"), fx ? 1 : 0);
    glUniform1i(glGetUniformLocation(g_pSprite, "uKind"), fxKindOf(g_kind));
    glUniform1i(glGetUniformLocation(g_pSprite, "frame_count"), g_fc);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    drawVerts(v, s.idx);
    glDisable(GL_BLEND); glDisable(GL_SCISSOR_TEST);
}

struct PaneRect { int x, y, w, h; bool fx; int vx = 0, vy = 0, vw = 0, vh = 0; };   // v*: the 3D viewport (top-left), decal mode
static std::vector<PaneRect> g_panes;   // in top-left pixel coordinates, for the labels
static int g_splitPx = 0;
static float g_cDensity = 0.0f;         // the cluster colour's texels per wall texel (0 = its full resolution)
static float g_cDepthRes = 0.5f;        // the cluster depth's resolution, a fraction of its full one (the shape stays readable)
static int g_mode = 0;                  // 0 sprites, 1 decal (bullet holes on a lit wall: the game's vs the clusters)
static void renderDecal(int x, int y, int w, int h, int H);
static void renderTab(int x, int y, int w, int h, int H);   // fx_tabs.inc: muzzle, tracer, burst, explosion (g_mode >= 2)
static std::string tabCaption();

// The scene into the rectangle (x, y top-left, w x h) of a framebuffer H pixels high.
static void renderScene(int x, int y, int w, int h, int H) {
    if (g_mode == 1) { renderDecal(x, y, w, h, H); return; }
    if (g_mode >= 2) { renderTab(x, y, w, h, H); return; }
    const Sprite s = buildSprite();
    g_panes.clear();
    const int gy = H - y - h;   // GL bottom-left
    if (g_view == VIEW_SIDE) {
        const int hw = w / 2;
        drawPane(s, x, gy, hw, h, false, x, x + hw); g_panes.push_back({ x, y, hw, h, false });
        drawPane(s, x + hw, gy, w - hw, h, true, x + hw, x + w); g_panes.push_back({ x + hw, y, w - hw, h, true });
    } else if (g_view == VIEW_SPLIT) {
        const int sx = x + (int)(w * g_split); g_splitPx = sx;
        drawPane(s, x, gy, w, h, false, x, sx); drawPane(s, x, gy, w, h, true, sx, x + w);
        g_panes.push_back({ x, y, sx - x, h, false }); g_panes.push_back({ sx, y, x + w - sx, h, true });
    } else {
        drawPane(s, x, gy, w, h, g_view == VIEW_FX, x, x + w); g_panes.push_back({ x, y, w, h, g_view == VIEW_FX });
    }
}

// labels over the panes (ImGui foreground list) and the caption
static void drawLabels(const std::string& caption) {
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    for (const PaneRect& p : g_panes) {
        if (g_mode == 1 && g_view == 4) break;   // the texture sheet has its own labels
        const char* t = g_mode == 1 ? (p.fx ? "PORT (game texture + parallax occlusion)" : "GAME (hole texture x vertex colour, alpha over)")
                      : g_mode >= 2 ? (p.fx ? "PORT FX (F9 on)" : "GAME ORIGINAL (F9 off)") : p.fx ? "EFFECT (fxShade)" : "ORIGINAL";
        dl->AddRectFilled(ImVec2((float)p.x + 6, (float)p.y + 6), ImVec2((float)p.x + 14 + ImGui::CalcTextSize(t).x, (float)p.y + 28), IM_COL32(0, 0, 0, 170));
        dl->AddText(ImVec2((float)p.x + 10, (float)p.y + 9), p.fx ? IM_COL32(255, 210, 120, 255) : IM_COL32(220, 220, 220, 255), t);
    }
    if (g_mode < 2 && g_view == VIEW_SPLIT && !g_panes.empty()) dl->AddLine(ImVec2((float)g_splitPx, (float)g_panes[0].y), ImVec2((float)g_splitPx, (float)(g_panes[0].y + g_panes[0].h)), IM_COL32(255, 255, 255, 200), 1.0f);
    if (!g_panes.empty()) {
        const PaneRect& p = g_panes[0]; const float yb = (float)(p.y + p.h) - 26;
        dl->AddRectFilled(ImVec2((float)p.x + 6, yb - 3), ImVec2((float)p.x + 14 + ImGui::CalcTextSize(caption.c_str()).x, yb + 19), IM_COL32(0, 0, 0, 170));
        dl->AddText(ImVec2((float)p.x + 10, yb), IM_COL32(230, 230, 230, 255), caption.c_str());
    }
    const std::string& er = g_mode == 1 ? g_decalErr : g_shaderErr;
    if (!er.empty()) dl->AddText(ImVec2((float)(g_panes.empty() ? 10 : g_panes.back().x + 10), 40), IM_COL32(255, 80, 80, 255), g_mode == 1 ? "impacts error: see the panel" : "effects error: see the panel");
}

// ---------------------------------------------------------------- decal mode: the port's bullet holes on a lit wall
// Materials: the game's hit type (assets/images.def HIT_*, src/game/tex.c *_impact_types) -> the port's decal group
// and the impact types the game picks from.
enum { HIT_DEFAULT = 0, HIT_STONE, HIT_WOOD, HIT_METAL, HIT_GLASS, HIT_WATER, HIT_SNOW, HIT_DIRT, HIT_MUD, HIT_TILE, HIT_METALOBJ, HIT_CHR, HIT_GLASS_XLU };
static const char* kHitNames[] = { "HIT_DEFAULT", "HIT_STONE", "HIT_WOOD", "HIT_METAL", "HIT_GLASS", "HIT_WATER", "HIT_SNOW", "HIT_DIRT", "HIT_MUD", "HIT_TILE", "HIT_METALOBJ", "HIT_CHR", "HIT_GLASS_XLU" };
struct DecalMat { const char* key; const char* name; int hit; int group; int ntypes; int types[3]; int wallTex; };
// wallTex: a real level texture of that hit type (NTSC numbering, assets/images.def), chosen from the texture sheet
static const DecalMat kMats[] = {
    { "stone", "Stone", HIT_STONE, 0, 1, { 1 }, 286 },
    { "tile", "Tile", HIT_TILE, 6, 1, { 1 }, 696 },
    { "default", "Default (untagged)", HIT_DEFAULT, 0, 1, { 7 }, 722 },
    { "wood", "Wood", HIT_WOOD, 1, 1, { 1 }, 1203 },
    { "metal", "Metal", HIT_METAL, 2, 1, { 7 }, 399 },
    { "metalobj", "Metal object", HIT_METALOBJ, 2, 2, { 1, 7 }, 77 },
    { "dirt", "Dirt", HIT_DIRT, 3, 1, { 2 }, 594 },
    { "mud", "Mud", HIT_MUD, 3, 1, { 2 }, 775 },
    { "snow", "Snow", HIT_SNOW, 4, 1, { 1 }, 1157 },
    { "glass", "Glass", HIT_GLASS, 5, 3, { 4, 5, 6 }, 809 },
};
static const int kNMats = (int)(sizeof kMats / sizeof *kMats);
static const char* kGroupNames[] = { "stone/default", "wood", "metal", "dirt/mud", "snow", "glass", "tile" };

// assets/images.def: one IMAGE(NAME, size, hitSound, hitTexture, ...) line per texture number, in order
struct TexInfo { std::string name; int hit = 0; };
static std::vector<TexInfo> g_texInfo;
static int hitByName(const std::string& s) { for (int i = 0; i < (int)(sizeof kHitNames / sizeof *kHitNames); i++) if (s == kHitNames[i]) return i; return -1; }
static bool parseImagesDef(const std::string& text, std::vector<TexInfo>& out) {
    out.clear(); size_t p = 0;
    while ((p = text.find("IMAGE(", p)) != std::string::npos) {
        const size_t e = text.find(')', p); if (e == std::string::npos) break;
        std::string args = text.substr(p + 6, e - p - 6); p = e;
        std::vector<std::string> f; size_t q = 0;
        while (true) {
            const size_t c = args.find(',', q);
            std::string t = args.substr(q, c == std::string::npos ? std::string::npos : c - q);
            while (!t.empty() && t[0] == ' ') t.erase(0, 1);
            while (!t.empty() && t.back() == ' ') t.pop_back();
            f.push_back(t);
            if (c == std::string::npos) break;
            q = c + 1;
        }
        if (f.size() < 4) continue;
        TexInfo ti; ti.name = f[0]; ti.hit = hitByName(f[3]); out.push_back(ti);
    }
    return !out.empty();
}

static int g_mat = 0, g_dtype = -1, g_wallTex = -1, g_holes = 3; static unsigned g_hseed = 1;   // -1 = the material's default
static bool g_sunOn = true, g_ptOn = true, g_wallLit = true;
static float g_sunAz = 135, g_sunEl = 35;                // degrees: azimuth in the wall plane (0 = from the right, 90 = from above), elevation off the wall
static float g_ptX = 10, g_ptY = -6, g_ptH = 70, g_ptR = 260, g_ptCol[3] = { 2.2f, 1.5f, 0.7f };   // dynlights.c MUZZLE_*: world units, linear colour
static float g_yaw = 0, g_pitch = 0, g_dzoom = 1.0f, g_texel = 2.0f, g_wallShade = 0.6f;
static int g_sheetPage = 0;
static const int kSheetCols = 8, kSheetRows = 5;

static const DecalMat& curMat() { return kMats[g_mat < 0 ? 0 : g_mat >= kNMats ? kNMats - 1 : g_mat]; }
static int decalType() { return g_dtype >= 0 && g_dtype < 20 ? g_dtype : curMat().types[0]; }
static int wallTex() { return g_wallTex >= 0 ? g_wallTex : curMat().wallTex; }
static std::vector<int> sheetList() {   // the textures of the material's hit type
    std::vector<int> v; for (int i = 0; i < (int)g_texInfo.size(); i++) if (g_texInfo[i].hit == curMat().hit) v.push_back(i); return v;
}

struct Hole { float x, y, ang, k; };
// fxparticles.c decalAdd: a random rotation, 0.85..1.15 scale; here the first at the middle, the others around it
static std::vector<Hole> holeLayout(int n, unsigned seed, float size) {
    std::vector<Hole> v; unsigned s = seed * 2654435761u + 17u;
    auto fr = [&]() { return lcg(s) / 32767.0f; };
    for (int i = 0; i < n; i++) {
        Hole h; h.ang = fr() * 6.2831853f; h.k = 0.85f + 0.3f * fr();
        if (i == 0) { h.x = h.y = 0; } else { const float a = fr() * 6.2831853f, r = size * (0.9f + 1.1f * fr()); h.x = cosf(a) * r; h.y = sinf(a) * r; }
        v.push_back(h);
    }
    return v;
}

struct V3 { float x, y, z, u, v, r, g, b, a; };
static void drawV3(const std::vector<V3>& v, const std::vector<uint16_t>& idx) {
    glBindVertexArray(g_vao); glBindBuffer(GL_ARRAY_BUFFER, g_vbo); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_ibo);
    glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(V3), v.data(), GL_STREAM_DRAW);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * 2, idx.data(), GL_STREAM_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(V3), (void*)0); glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(V3), (void*)12); glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(V3), (void*)20); glEnableVertexAttribArray(2);
    glDrawElements(GL_TRIANGLES, (GLsizei)idx.size(), GL_UNSIGNED_SHORT, 0);
}

// the camera: orbits the middle hole; yaw 0 / pitch 0 = straight at the wall (the wall is z = 0, normal +z, y up)
struct Cam { float eye[3], r[3], u[3], f[3]; float view[16], proj[16]; float tanh, aspect; };
static float holeSize() { const ImpactType& it = kImpactTypes[decalType()]; return fmaxf(it.w, it.h); }
static Cam makeCam(float aspect) {
    Cam c; const float d = 8.2f * holeSize() / fmaxf(g_dzoom, 0.05f), y = g_yaw * 0.0174533f, p = g_pitch * 0.0174533f;
    c.eye[0] = d * cosf(p) * sinf(y); c.eye[1] = d * sinf(p); c.eye[2] = d * cosf(p) * cosf(y);
    const float il = 1.0f / sqrtf(c.eye[0] * c.eye[0] + c.eye[1] * c.eye[1] + c.eye[2] * c.eye[2]);
    for (int i = 0; i < 3; i++) c.f[i] = -c.eye[i] * il;
    c.r[0] = -c.f[2]; c.r[1] = 0; c.r[2] = c.f[0];   // f x (0,1,0)
    { const float l = 1.0f / sqrtf(c.r[0] * c.r[0] + c.r[2] * c.r[2]); c.r[0] *= l; c.r[2] *= l; }
    c.u[0] = c.r[1] * c.f[2] - c.r[2] * c.f[1]; c.u[1] = c.r[2] * c.f[0] - c.r[0] * c.f[2]; c.u[2] = c.r[0] * c.f[1] - c.r[1] * c.f[0];
    float* m = c.view; memset(m, 0, sizeof c.view);   // column-major
    for (int i = 0; i < 3; i++) { m[i * 4 + 0] = c.r[i]; m[i * 4 + 1] = c.u[i]; m[i * 4 + 2] = -c.f[i]; }
    m[12] = -(c.r[0] * c.eye[0] + c.r[1] * c.eye[1] + c.r[2] * c.eye[2]);
    m[13] = -(c.u[0] * c.eye[0] + c.u[1] * c.eye[1] + c.u[2] * c.eye[2]);
    m[14] = (c.f[0] * c.eye[0] + c.f[1] * c.eye[1] + c.f[2] * c.eye[2]);
    m[15] = 1;
    c.tanh = tanf(20.0f * 0.0174533f); c.aspect = aspect;
    const float n = 0.5f, fa = 20000.0f; float* P = c.proj; memset(P, 0, sizeof c.proj);
    P[0] = 1.0f / (c.tanh * aspect); P[5] = 1.0f / c.tanh; P[10] = -(fa + n) / (fa - n); P[11] = -1; P[14] = -2 * fa * n / (fa - n);
    return c;
}
static void toEye(const Cam& c, const float w[3], float o[3], bool dir) {
    for (int i = 0; i < 3; i++) o[i] = c.view[0 * 4 + i] * w[0] + c.view[1 * 4 + i] * w[1] + c.view[2 * 4 + i] * w[2] + (dir ? 0 : c.view[12 + i]);
}
static void sunDirWorld(float o[3]) {   // towards the sun
    const float a = g_sunAz * 0.0174533f, e = g_sunEl * 0.0174533f;
    o[0] = cosf(e) * cosf(a); o[1] = cosf(e) * sinf(a); o[2] = sinf(e);
}

static void setDecalUniforms(const Cam& c) {
    GLuint p = g_pDecal;
    glUniformMatrix4fv(glGetUniformLocation(p, "uView"), 1, GL_FALSE, c.view);
    glUniformMatrix4fv(glGetUniformLocation(p, "uProj"), 1, GL_FALSE, c.proj);
    glUniform1i(glGetUniformLocation(p, "uTex0"), 0);
    glUniform1i(glGetUniformLocation(p, "uTex1"), 1);
    glUniform4i(glGetUniformLocation(p, "lcount"), g_ptOn ? 1 : 0, g_sunOn ? 1 : 0, 0, 0);
    float sw[3], se[3]; sunDirWorld(sw); toEye(c, sw, se, true);
    glUniform4f(glGetUniformLocation(p, "sun_dir"), se[0], se[1], se[2], 0);
    glUniform4f(glGetUniformLocation(p, "sun_col"), 1.0f, 0.96f, 0.88f, 0);
    float lp[16 * 4] = {}, lc[16 * 4] = {};
    const float pw[3] = { g_ptX, g_ptY, g_ptH }; toEye(c, pw, lp, false); lp[3] = g_ptR;
    lc[0] = g_ptCol[0]; lc[1] = g_ptCol[1]; lc[2] = g_ptCol[2];
    glUniform4fv(glGetUniformLocation(p, "lpos"), 16, lp); glUniform4fv(glGetUniformLocation(p, "lcol"), 16, lc);
    glUniform1ui(glGetUniformLocation(p, "flags"), 0u); glUniform1i(glGetUniformLocation(p, "fog_pixel"), 0);
    glUniform1i(glGetUniformLocation(p, "frame_count"), g_fc);
    glUniform1i(glGetUniformLocation(p, "uLit"), g_wallLit ? 1 : 0);
}

static void bindTex(const GlTex& t, bool mip) {
    glBindTexture(GL_TEXTURE_2D, t.id ? t.id : g_white);
    if (mip && t.id) {
        static std::map<GLuint, bool> mipped; if (!mipped[t.id]) { glGenerateMipmap(GL_TEXTURE_2D); mipped[t.id] = true; }
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, g_nearest ? GL_NEAREST_MIPMAP_LINEAR : GL_LINEAR_MIPMAP_LINEAR);
    } else glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, g_nearest ? GL_NEAREST : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, g_nearest ? GL_NEAREST : GL_LINEAR);
}

// The cluster prototype (fx_cluster.h): the holes stamped into one texture with "the deepest wins", drawn
// as one quad through fxClusterPom (parallax occlusion over the fused depth, the relief lit).
static GLuint g_cCol = 0, g_cDep = 0;
// The wall texture's average colour (read back from its GL texture), for the holes' base colour.
static void wallAverage(GLuint tex, float out[3]) {
    out[0] = out[1] = out[2] = 0.5f;
    if (!tex) return;
    GLint w = 0, h = 0;
    glBindTexture(GL_TEXTURE_2D, tex);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w); glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h);
    if (w <= 0 || h <= 0) return;
    std::vector<uint8_t> px((size_t)w * h * 4);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    double s[3] = { 0, 0, 0 };
    for (size_t i = 0; i < (size_t)w * h; i++) for (int k = 0; k < 3; k++) s[k] += px[i * 4 + k];
    for (int k = 0; k < 3; k++) out[k] = (float)(s[k] / ((double)w * h * 255.0));
}
static void drawCluster(const std::vector<Hole>& hs, const ImpactType& it, int grp) {
    static FxcTemplate tpl[FXC_MATS][4];
    static bool made[FXC_MATS][4];
    const int mat = grp == 1 ? FXC_WOOD : grp == 2 ? FXC_METAL : grp == 3 ? FXC_DIRT : grp == 4 ? FXC_SNOW : grp == 5 ? FXC_GLASS : grp == 6 ? FXC_TILE : FXC_STONE;
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (const Hole& H : hs) {
        const float hw = it.w * H.k, hh = it.h * H.k;
        x0 = fminf(x0, H.x - hw); x1 = fmaxf(x1, H.x + hw); y0 = fminf(y0, H.y - hh); y1 = fmaxf(y1, H.y + hh);
    }
    const float size = fmaxf(x1 - x0, y1 - y0) * 1.02f, cx = 0.5f * (x0 + x1), cy = 0.5f * (y0 + y1);
    static FxcCluster cl;
    fxc_cluster_clear(&cl, cx - size * 0.5f, cy - size * 0.5f, size);
    // the holes' base: the wall texture's average x the wall's baked light (its vertex shade here), 15 % lighter
    float base[3]; wallAverage(texture((uint32_t)wallTex(), 0, 0).id, base);
    for (int k = 0; k < 3; k++) base[k] = fminf(1.0f, base[k] * g_wallShade * 1.15f);
    if (mat == FXC_GLASS) {   // glass: the game's own glass hole texture (read back from its GL texture), its middle kept empty
        const ImgEntry* gi = tableEntry("impactimages", decalType());
        const GlTex& gt = gi ? texture(gi->index, gi->flagsS, gi->flagsT) : GlTex();
        GLint gw = 0, gh = 0;
        if (gt.id) {
            glBindTexture(GL_TEXTURE_2D, gt.id);
            glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &gw); glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &gh);
        }
        if (gw > 0 && gh > 0) {
            std::vector<uint8_t> px((size_t)gw * gh * 4);
            glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
            for (int v = 0; v < 4; v++) { fxc_template_glass_from(&tpl[mat][v], px.data(), gw, gh); made[mat][v] = true; }
        }
    }
    for (size_t i = 0; i < hs.size(); i++) {
        const int var = (int)(i % 4);
        if (!made[mat][var]) { fxc_template(&tpl[mat][var], mat, var); made[mat][var] = true; }
        const FxcHole h = { hs[i].x, hs[i].y, it.w * hs[i].k, it.h * hs[i].k, mat, var, { base[0], base[1], base[2] } };
        fxc_stamp(&cl, &tpl[mat][var], &h);
    }
    if (!g_cCol) { glGenTextures(1, &g_cCol); glGenTextures(1, &g_cDep); }
    // the colour at the wall's texel density (g_texel world units per wall texel; g_cDensity cluster texels per
    // wall texel): as soft as the wall. The depth at its own resolution (g_cDepthRes x full): the shape stays sharp.
    int R = FXC_N;
    if (g_cDensity > 0.0f) R = std::max(4, std::min(FXC_N, (int)lroundf(size / g_texel * g_cDensity)));
    const int RD = std::max(4, std::min(FXC_N, (int)lroundf(FXC_N * g_cDepthRes)));
    std::vector<uint8_t> crgba((size_t)R * R * 4), drgba((size_t)RD * RD * 4);
    std::vector<float> cdep((size_t)R * R), ddep((size_t)RD * RD);
    fxc_cluster_resample(&cl, R, crgba.data(), cdep.data());
    fxc_cluster_resample(&cl, RD, drgba.data(), ddep.data());
    std::vector<uint16_t> d16((size_t)RD * RD);
    for (int i = 0; i < RD * RD; i++) d16[(size_t)i] = (uint16_t)fmaxf(0.0f, fminf(65535.0f, ddep[(size_t)i] * 65535.0f));
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, g_cDep);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R16, RD, RD, 0, GL_RED, GL_UNSIGNED_SHORT, d16.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, g_cCol);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, R, R, 0, GL_RGBA, GL_UNSIGNED_BYTE, crgba.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    const float depthUV = cl.depthMax / size, vr = fminf(depthUV * 4.0f, 1.0f);
    const float qx0 = cl.x0, qy0 = cl.y0, qx1 = cl.x0 + size, qy1 = cl.y0 + size;
    const float lightAmt = (mat == FXC_SNOW || mat == FXC_GLASS) ? 0.0f : 1.0f;   /* the vertex colour's g: how much the relief is lit (snow, glass: none) */
    std::vector<V3> q = { { qx0, qy0, 0.4f, 0, 0, vr, lightAmt, 1, 1 }, { qx1, qy0, 0.4f, 1, 0, vr, lightAmt, 1, 1 },
                          { qx1, qy1, 0.4f, 1, 1, vr, lightAmt, 1, 1 }, { qx0, qy1, 0.4f, 0, 1, vr, lightAmt, 1, 1 } };
    glUniform1i(glGetUniformLocation(g_pDecal, "uMode"), 4);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
    drawV3(q, { 0, 1, 2, 0, 2, 3 });
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDisable(GL_BLEND);
}

// one pane: the wall, then the holes (fx: one cluster, fx/impacts.glsl fxClusterPom; else the game's draw)
static void drawDecalPane(int x0, int y0, int w, int h, bool fx, int clipX0, int clipX1) {
    glViewport(x0, y0, w, h);
    glEnable(GL_SCISSOR_TEST); glScissor(clipX0, y0, clipX1 - clipX0, h);
    glClearColor(0.05f, 0.05f, 0.06f, 1); glClear(GL_COLOR_BUFFER_BIT);
    /* The window's default framebuffer has a depth buffer this view never clears (the shot FBO has
     * none), and the UI may leave culling on: draw order is all the wall and the holes need. */
    glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);
    const Cam c = makeCam((float)w / (float)h);
    glUseProgram(g_pDecal); setDecalUniforms(c);
    // the wall: TEXEL0 x SHADE (the room's baked vertex colour) + dynLight; g_texel world units per texel
    {
        const GlTex& t = texture((uint32_t)wallTex(), 0, 0); bindTex(t, true);
        const float tw = (float)(t.w > 0 ? t.w : 32), th = (float)(t.h > 0 ? t.h : 32), W = 40.0f * holeSize() / fmaxf(g_dzoom, 0.05f), s = g_wallShade;
        std::vector<V3> q = { { -W, -W, 0, -W / (g_texel * tw), W / (g_texel * th), s, s, s, 1 }, { W, -W, 0, W / (g_texel * tw), W / (g_texel * th), s, s, s, 1 },
                              { W, W, 0, W / (g_texel * tw), -W / (g_texel * th), s, s, s, 1 }, { -W, W, 0, -W / (g_texel * tw), -W / (g_texel * th), s, s, s, 1 } };
        glUniform1i(glGetUniformLocation(g_pDecal, "uMode"), 0);
        glDisable(GL_BLEND); drawV3(q, { 0, 1, 2, 0, 2, 3 });
    }
    // the holes: fxparticles.c decalAdd / decalDraw geometry (0.4 off the wall; uv 0..1, t = 0 on the -b side)
    const int type = decalType(); const ImpactType& it = kImpactTypes[type];
    const ImgEntry* img = tableEntry("impactimages", type);
    const GlTex& ht = img ? texture(img->index, img->flagsS, img->flagsT) : GlTex();
    bindTex(ht, false);
    std::vector<V3> v; std::vector<uint16_t> idx;
    std::vector<Hole> hs = holeLayout(g_holes, g_hseed, holeSize());
    for (Hole& hh : hs) hh.ang = 0.0f;   // never turned, like the game's (fxparticles.c decalAdd)
    if (fx) {   // the port: the holes fused into one cluster (port/src/fxclusters.c)
        drawCluster(hs, it, curMat().group);
        glDisable(GL_SCISSOR_TEST);
        return;
    }
    unsigned vseed = g_seed * 7919u + (unsigned)type;
    for (size_t hidx = 0; hidx < hs.size(); hidx++) {
        const Hole& H = hs[hidx];
        const float cs = cosf(H.ang), sn = sinf(H.ang), hw = it.w * H.k, hh = it.h * H.k;
        const float T[2] = { cs * hw, sn * hw }, B[2] = { -sn * hh, cs * hh };
        static const float cn[4][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
        const uint16_t b = (uint16_t)v.size();
        const std::vector<std::array<float, 2>> poly = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
        for (const auto& c : poly) {
            float col[3] = { 1, 1, 1 };
            impactVertexColour(it.app, vseed, col);   // the game's colours
            v.push_back({ H.x + T[0] * c[0] + B[0] * c[1], H.y + T[1] * c[0] + B[1] * c[1], 0.4f, c[0] * 0.5f + 0.5f, c[1] * 0.5f + 0.5f, col[0], col[1], col[2], 1 });
        }
        for (size_t k = 1; k + 1 < poly.size(); k++) for (size_t i : { (size_t)0, k, k + 1 }) idx.push_back((uint16_t)(b + i));
    }
    glEnable(GL_BLEND);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
    glUniform1i(glGetUniformLocation(g_pDecal, "uMode"), 0);   // the game's holes: G_RM_ZB_XLU_DECAL, src alpha over
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    drawV3(v, idx);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDisable(GL_BLEND); glDisable(GL_SCISSOR_TEST);
}

// the texture sheet: the material's textures, click one to put it on the wall
struct SheetCell { int x, y, w, h, tex; };
static std::vector<SheetCell> g_sheet;
static void drawSheet(int x, int y, int w, int h, int H) {
    g_sheet.clear();
    const std::vector<int> list = sheetList();
    const int per = kSheetCols * kSheetRows, pages = ((int)list.size() + per - 1) / per;
    if (g_sheetPage >= pages) g_sheetPage = pages > 0 ? pages - 1 : 0;
    const int cw = w / kSheetCols, ch = h / kSheetRows;
    glUseProgram(g_pDecal);
    const float id[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
    glUniformMatrix4fv(glGetUniformLocation(g_pDecal, "uView"), 1, GL_FALSE, id); glUniformMatrix4fv(glGetUniformLocation(g_pDecal, "uProj"), 1, GL_FALSE, id);
    glUniform1i(glGetUniformLocation(g_pDecal, "uMode"), 0); glUniform1i(glGetUniformLocation(g_pDecal, "uLit"), 0);
    glUniform4i(glGetUniformLocation(g_pDecal, "lcount"), 0, 0, 0, 0);
    glDisable(GL_BLEND);
    for (int i = 0; i < per && g_sheetPage * per + i < (int)list.size(); i++) {
        const int tn = list[(size_t)(g_sheetPage * per + i)], cx = x + (i % kSheetCols) * cw, cy = y + (i / kSheetCols) * ch;
        const GlTex& t = texture((uint32_t)tn, 0, 0); bindTex(t, false);
        const int s = std::min(cw, ch - 18) - 8;
        // 2x2 repeats of the texture, aspect kept
        const float ar = t.w > 0 && t.h > 0 ? (float)t.w / t.h : 1.0f; int qw = s, qh = s; if (ar > 1) qh = (int)(s / ar); else qw = (int)(s * ar);
        glViewport(cx + 4, H - (cy + 4) - qh, qw, qh);
        std::vector<V3> q = { { -1, -1, 0, 0, 2, 1, 1, 1, 1 }, { 1, -1, 0, 2, 2, 1, 1, 1, 1 }, { 1, 1, 0, 2, 0, 1, 1, 1, 1 }, { -1, 1, 0, 0, 0, 1, 1, 1, 1 } };
        drawV3(q, { 0, 1, 2, 0, 2, 3 });
        g_sheet.push_back({ cx, cy, cw, ch, tn });
    }
    g_panes.clear(); PaneRect p{ x, y, w, h, false }; g_panes.push_back(p);
}

static void renderDecal(int x, int y, int w, int h, int H) {
    g_panes.clear();
    if (g_view == 4) { drawSheet(x, y, w, h, H); return; }
    const int gy = H - y - h;
    auto pane = [&](int px, int pw, bool fx, int cx0, int cx1) {
        drawDecalPane(px, gy, pw, h, fx, cx0, cx1);
        PaneRect r{ cx0, y, cx1 - cx0, h, fx }; r.vx = px; r.vy = y; r.vw = pw; r.vh = h; g_panes.push_back(r);
    };
    if (g_view == VIEW_SIDE) { const int hw = w / 2; pane(x, hw, false, x, x + hw); pane(x + hw, w - hw, true, x + hw, x + w); }
    else if (g_view == VIEW_SPLIT) { const int sx = x + (int)(w * g_split); g_splitPx = sx; pane(x, w, false, x, sx); pane(x, w, true, sx, x + w); }
    else pane(x, w, g_view == VIEW_FX, x, x + w);
}

static std::string decalCaption() {
    if (g_view == 4) {
        const std::vector<int> l = sheetList();
        char b[160]; snprintf(b, sizeof b, "%s textures (%s): %d, page %d  -  click one to put it on the wall", curMat().name, kHitNames[curMat().hit], (int)l.size(), g_sheetPage + 1);
        return b;
    }
    const int type = decalType(), wt = wallTex();
    const ImgEntry* img = tableEntry("impactimages", type);
    char b[400];
    snprintf(b, sizeof b, "%s -> group %d (%s) | impact type %d (tex %u, %gx%g) x%d | wall tex %d %s | sun %s az %.0f el %.0f | flash %s (%.0f,%.0f,%.0f) r %.0f",
             curMat().name, curMat().group, kGroupNames[curMat().group], type, img ? img->index : 0, kImpactTypes[type].w * 2, kImpactTypes[type].h * 2, g_holes,
             wt, wt < (int)g_texInfo.size() ? g_texInfo[(size_t)wt].name.c_str() : "?", g_sunOn ? "on" : "off (the relief light: from above)", g_sunAz, g_sunEl,
             g_ptOn ? "on" : "off", g_ptX, g_ptY, g_ptH, g_ptR);
    return b;
}
static void drawSheetLabels() {
    if (!(g_mode == 1 && g_view == 4)) return;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    for (const SheetCell& c : g_sheet) {
        char t[96]; snprintf(t, sizeof t, "%d %s", c.tex, c.tex < (int)g_texInfo.size() ? g_texInfo[(size_t)c.tex].name.c_str() : "");
        dl->AddText(ImVec2((float)c.x + 4, (float)(c.y + c.h - 17)), c.tex == wallTex() ? IM_COL32(255, 210, 120, 255) : IM_COL32(220, 220, 220, 255), t);
    }
}

// a pixel (top-left window coordinates) -> the point on the wall (z = 0) under it; false when it misses
static bool pickWall(int mx, int my, float out[2]) {
    for (const PaneRect& p : g_panes) {
        if (mx < p.x || mx >= p.x + p.w || my < p.y || my >= p.y + p.h || p.vw <= 0) continue;
        const Cam c = makeCam((float)p.vw / (float)p.vh);
        const float nx = (mx - p.vx + 0.5f) / p.vw * 2 - 1, ny = 1 - (my - p.vy + 0.5f) / p.vh * 2;
        const float ex = nx * c.tanh * c.aspect, ey = ny * c.tanh;
        float d[3]; for (int i = 0; i < 3; i++) d[i] = c.r[i] * ex + c.u[i] * ey + c.f[i];
        if (fabsf(d[2]) < 1e-5f) return false;
        const float t = -c.eye[2] / d[2]; if (t <= 0) return false;
        out[0] = c.eye[0] + d[0] * t; out[1] = c.eye[1] + d[1] * t; return true;
    }
    return false;
}

static void uiDecal() {
    ImGui::SeparatorText("Material");
    if (ImGui::BeginCombo("material", curMat().name)) {
        for (int i = 0; i < kNMats; i++) if (ImGui::Selectable(kMats[i].name, g_mat == i)) { g_mat = i; g_dtype = -1; g_wallTex = -1; }
        ImGui::EndCombo();
    }
    ImGui::Text("decal group %d (%s)", curMat().group, kGroupNames[curMat().group]);
    {
        std::string ts; for (int i = 0; i < curMat().ntypes; i++) ts += (i ? ", " : "") + std::to_string(curMat().types[i]);
        ImGui::Text("game impact type(s): %s", ts.c_str());
    }
    int t = decalType();
    if (ImGui::SliderInt("impact type", &t, 0, 19)) g_dtype = t;
    ImGui::SameLine(); if (ImGui::SmallButton("game's")) g_dtype = -1;
    ImGui::SliderInt("holes", &g_holes, 1, 6);
    ImGui::SameLine(); if (ImGui::SmallButton("reroll")) { g_hseed++; g_seed++; }
    ImGui::SeparatorText("Wall");
    int wt = wallTex();
    if (ImGui::InputInt("wall texture #", &wt)) g_wallTex = wt < 0 ? 0 : wt;
    ImGui::SameLine(); if (ImGui::SmallButton("default")) g_wallTex = -1;
    {
        const GlTex& tx = texture((uint32_t)wallTex(), 0, 0);
        const int w2 = wallTex();
        ImGui::TextWrapped("%s, %s, %dx%d %s", w2 < (int)g_texInfo.size() ? g_texInfo[(size_t)w2].name.c_str() : "?", w2 < (int)g_texInfo.size() && g_texInfo[(size_t)w2].hit >= 0 ? kHitNames[g_texInfo[(size_t)w2].hit] : "?",
                           tx.w, tx.h, tx.id ? "" : ("not decodable: " + tx.note).c_str());
    }
    ImGui::SliderFloat("units / texel", &g_texel, 0.25f, 8.0f, "%.2f");
    ImGui::SliderFloat("vertex shade", &g_wallShade, 0.1f, 1.0f, "%.2f");
    ImGui::Checkbox("dynLight on the wall (Enhanced)", &g_wallLit);
    ImGui::SeparatorText("Lights");
    ImGui::Checkbox("sun", &g_sunOn);
    ImGui::SliderFloat("sun azimuth", &g_sunAz, -180, 180, "%.0f deg"); ImGui::SliderFloat("sun elevation", &g_sunEl, 1, 90, "%.0f deg");
    ImGui::Checkbox("point light (muzzle flash)", &g_ptOn);
    ImGui::DragFloat2("on the wall x/y", &g_ptX, 0.25f); ImGui::SliderFloat("off the wall", &g_ptH, 0.5f, 200.0f, "%.1f");
    ImGui::SliderFloat("radius", &g_ptR, 10, 600, "%.0f"); ImGui::DragFloat3("colour", g_ptCol, 0.02f, 0.0f, 4.0f);
    ImGui::TextDisabled("right-drag in the view: move the light");
    ImGui::SeparatorText("Camera");
    ImGui::SliderFloat("yaw", &g_yaw, -85, 85, "%.0f deg"); ImGui::SliderFloat("pitch", &g_pitch, -85, 85, "%.0f deg");
    ImGui::SliderFloat("zoom##d", &g_dzoom, 0.1f, 6.0f, "%.2f");
    ImGui::TextDisabled("left-drag: orbit, wheel: zoom");
    if (ImGui::SmallButton("front")) { g_yaw = 0; g_pitch = 0; } ImGui::SameLine(); if (ImGui::SmallButton("grazing")) { g_yaw = 70; g_pitch = 10; }
    ImGui::SeparatorText("View");
    ImGui::RadioButton("side by side", &g_view, VIEW_SIDE); ImGui::SameLine(); ImGui::RadioButton("split", &g_view, VIEW_SPLIT);
    ImGui::RadioButton("game", &g_view, VIEW_ORIG); ImGui::SameLine(); ImGui::RadioButton("ours", &g_view, VIEW_FX); ImGui::SameLine(); ImGui::RadioButton("texture sheet", &g_view, 4);
    if (g_view == VIEW_SPLIT) ImGui::SliderFloat("split at", &g_split, 0.0f, 1.0f);
    if (g_view == 4) { ImGui::InputInt("page", &g_sheetPage); if (g_sheetPage < 0) g_sheetPage = 0; }
    ImGui::Checkbox("nearest texels (off = bilinear)", &g_nearest);
    ImGui::SeparatorText("port/shaders/fx/");
    ImGui::TextWrapped("%s (reloaded %d x)", g_shaderPath.c_str(), g_decalReloads);
    if (ImGui::Button("reload now")) reloadShader(true);
    if (!g_decalErr.empty()) ImGui::TextColored(ImVec4(1, .4f, .4f, 1), "%s", g_decalErr.c_str());
}

#include "fx_tabs.inc"

// ---------------------------------------------------------------- UI
static const int kPanelW = 320;
static bool g_shotReq = false;
static std::string g_iniPath = "fxview.ini";

static void saveIni() {
    FILE* f = fopen(g_iniPath.c_str(), "w"); if (!f) return;
    fprintf(f, "kind=%s\nimpact=%d\nview=%s\nbackdrop=%s\nbgtex=%d\nzoom=%g\nangle=%g\nsplit=%g\nnearest=%d\ntracerlen=%g\n",
            kKindKey[g_kind], g_impact, kViewKey[g_view], kBgKey[g_bg], g_bgTex, g_zoom, g_angle, g_split, g_nearest ? 1 : 0, g_tracerLen);
    fprintf(f, "mode=%s\nmat=%s\ndtype=%d\nwalltex=%d\nholes=%d\nhseed=%u\nsun=%d\nsunaz=%g\nsunel=%g\npt=%d\nptx=%g\npty=%g\npth=%g\nptr=%g\nptcol=%g,%g,%g\n"
               "yaw=%g\npitch=%g\ndzoom=%g\ntexel=%g\nwallshade=%g\nwalllit=%d\n",
            kModeKey[g_mode >= 0 && g_mode < MODE_COUNT ? g_mode : 0], curMat().key, g_dtype, g_wallTex, g_holes, g_hseed, g_sunOn ? 1 : 0, g_sunAz, g_sunEl, g_ptOn ? 1 : 0, g_ptX, g_ptY, g_ptH, g_ptR,
            g_ptCol[0], g_ptCol[1], g_ptCol[2], g_yaw, g_pitch, g_dzoom, g_texel, g_wallShade, g_wallLit ? 1 : 0);
    fprintf(f, "tspeed=%g\ntshow=%d\ntyaw=%g\ntpitch=%g\ntzoom=%g\ntwall=%d\ntwalltex=%d\ngun=%s\nmrate=%g\nmscale=%g\nmvis=%d\ntritem=%d\ntrrange=%g\ntrperiod=%g\nburst=%s\nbperiod=%g\nexplo=%d\netail=%g\n",
            g_tSpeed, g_tShow, g_tYaw, g_tPitch, g_tZoom, g_tWall ? 1 : 0, g_tWallTex, g_gun < (int)g_guns.size() ? g_guns[(size_t)g_gun].name.c_str() : g_gunWant.c_str(), g_mRate, g_mScale, g_mVis,
            g_trItem, g_trRange, g_trPeriod, kBurstKey[g_burst], g_bPeriod, g_explo, g_eTail);
    fclose(f);
}
static void setKey(const std::string& k, const std::string& v) {
    if (k == "kind") { for (int i = 0; i < K_COUNT; i++) if (v == kKindKey[i]) g_kind = i; }
    else if (k == "impact") g_impact = atoi(v.c_str());
    else if (k == "view") { for (int i = 0; i < 5; i++) if (v == kViewKey[i]) g_view = i; }
    else if (k == "backdrop") { for (int i = 0; i < 4; i++) if (v == kBgKey[i]) g_bg = i; }
    else if (k == "bgtex") g_bgTex = atoi(v.c_str());
    else if (k == "zoom") g_zoom = (float)atof(v.c_str());
    else if (k == "angle") g_angle = (float)atof(v.c_str());
    else if (k == "split") g_split = (float)atof(v.c_str());
    else if (k == "nearest") g_nearest = atoi(v.c_str()) != 0;
    else if (k == "tracerlen") g_tracerLen = (float)atof(v.c_str());
    else if (k == "age") g_age = (float)atof(v.c_str());
    else if (k == "fc") g_fc = atoi(v.c_str());
    else if (k == "seed") g_seed = (unsigned)atoi(v.c_str());
    else if (k == "mode") { g_mode = 0; for (int i = 0; i < MODE_COUNT; i++) if (v == kModeKey[i]) g_mode = i; }
    else if (k == "mat") { for (int i = 0; i < kNMats; i++) if (v == kMats[i].key) g_mat = i; }
    else if (k == "dtype") g_dtype = atoi(v.c_str());
    else if (k == "walltex") g_wallTex = atoi(v.c_str());
    else if (k == "holes") g_holes = std::max(1, std::min(6, atoi(v.c_str())));
    else if (k == "hseed") g_hseed = (unsigned)atoi(v.c_str());
    else if (k == "sun") g_sunOn = atoi(v.c_str()) != 0;
    else if (k == "sunaz") g_sunAz = (float)atof(v.c_str());
    else if (k == "sunel") g_sunEl = (float)atof(v.c_str());
    else if (k == "pt") g_ptOn = atoi(v.c_str()) != 0;
    else if (k == "ptx") g_ptX = (float)atof(v.c_str());
    else if (k == "pty") g_ptY = (float)atof(v.c_str());
    else if (k == "pth") g_ptH = (float)atof(v.c_str());
    else if (k == "ptr") g_ptR = (float)atof(v.c_str());
    else if (k == "ptcol") sscanf(v.c_str(), "%f,%f,%f", &g_ptCol[0], &g_ptCol[1], &g_ptCol[2]);
    else if (k == "yaw") g_yaw = (float)atof(v.c_str());
    else if (k == "pitch") g_pitch = (float)atof(v.c_str());
    else if (k == "dzoom") g_dzoom = (float)atof(v.c_str());
    else if (k == "texel") g_texel = (float)atof(v.c_str());
    else if (k == "wallshade") g_wallShade = (float)atof(v.c_str());
    else if (k == "walllit") g_wallLit = atoi(v.c_str()) != 0;
    else if (k == "cdensity") g_cDensity = (float)atof(v.c_str());
    else if (k == "cdepth") g_cDepthRes = (float)atof(v.c_str());
    else if (k == "page") g_sheetPage = atoi(v.c_str());
    else if (k == "t") { g_tTime = (float)atof(v.c_str()); g_tPlay = false; }
    else if (k == "play") g_tPlay = atoi(v.c_str()) != 0;
    else if (k == "tspeed") g_tSpeed = (float)atof(v.c_str());
    else if (k == "tshow") g_tShow = v == "game" ? 0 : v == "port" ? 1 : v == "side" ? 2 : atoi(v.c_str());
    else if (k == "tyaw") g_tYaw = (float)atof(v.c_str());
    else if (k == "tpitch") g_tPitch = (float)atof(v.c_str());
    else if (k == "tzoom") g_tZoom = (float)atof(v.c_str());
    else if (k == "twall") g_tWall = atoi(v.c_str()) != 0;
    else if (k == "twalltex") g_tWallTex = atoi(v.c_str());
    else if (k == "gun") g_gunWant = v;
    else if (k == "mrate") g_mRate = (float)atof(v.c_str());
    else if (k == "mscale") g_mScale = (float)atof(v.c_str());
    else if (k == "mvis") g_mVis = atoi(v.c_str());
    else if (k == "tritem") g_trItem = v == "gun" ? 0 : v == "laser" ? 1 : v == "watchlaser" ? 2 : atoi(v.c_str());
    else if (k == "trrange") g_trRange = (float)atof(v.c_str());
    else if (k == "trperiod") g_trPeriod = (float)atof(v.c_str());
    else if (k == "burst") { for (int i = 0; i < BM_COUNT; i++) if (v == kBurstKey[i]) g_burst = i; }
    else if (k == "bperiod") g_bPeriod = (float)atof(v.c_str());
    else if (k == "explo") g_explo = atoi(v.c_str());
    else if (k == "etail") g_eTail = (float)atof(v.c_str());
    else if (k == "tseed") g_tSeed = (unsigned)atoi(v.c_str());
}
static void loadIni() {
    std::string s; if (!readFile(g_iniPath, s)) return;
    size_t p = 0;
    while (p < s.size()) {
        size_t e = s.find('\n', p); if (e == std::string::npos) e = s.size();
        std::string line = s.substr(p, e - p); p = e + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        const size_t q = line.find('='); if (q != std::string::npos) setKey(line.substr(0, q), line.substr(q + 1));
    }
}

static void uiPanel(int H) {
    ImGui::SetNextWindowPos(ImVec2(0, 0)); ImGui::SetNextWindowSize(ImVec2((float)kPanelW, (float)H));
    ImGui::Begin("fxview", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
    ImGui::TextWrapped("%s", g_romNote.c_str());
    {   // the tab bar: one tab per effect family (fx_tabs.inc); the selected one follows g_mode (--mode / the .ini) on the first frame
        static bool pending = true;   // until ImGui shows the tab g_mode names (SetSelected can take a few frames)
        static const struct { int mode; const char* name; } tabs[] = { { MODE_DECAL, "Impacts" }, { MODE_MUZZLE, "Muzzle" }, { MODE_TRACER, "Tracers" },
                                                                          { MODE_BURST, "Blood/smoke/debris" }, { MODE_EXPLO, "Explosions" }, { MODE_SPRITE, "Sprites" } };
        if (ImGui::BeginTabBar("tabs", ImGuiTabBarFlags_FittingPolicyResizeDown)) {
            for (const auto& t : tabs)
                if (ImGui::BeginTabItem(t.name, nullptr, pending && g_mode == t.mode ? ImGuiTabItemFlags_SetSelected : 0)) {
                    if (pending) { if (t.mode == g_mode) pending = false; } else g_mode = t.mode;
                    ImGui::EndTabItem();
                }
            ImGui::EndTabBar();
        }
    }
    if (g_mode >= MODE_MUZZLE) {
        uiTab();
        ImGui::Separator();
        if (ImGui::Button("screenshot (F12)")) g_shotReq = true;
        ImGui::SameLine();
        if (ImGui::Button("save settings")) saveIni();
        ImGui::End();
        return;
    }
    if (g_mode == 1) {
        uiDecal();
        ImGui::Separator();
        if (ImGui::Button("screenshot (F12)")) g_shotReq = true;
        ImGui::SameLine();
        if (ImGui::Button("save settings")) saveIni();
        ImGui::End();
        return;
    }
    if (g_view > VIEW_FX) g_view = VIEW_SIDE;
    ImGui::SeparatorText("Effect");
    for (int i = 0; i < K_COUNT; i++) if (ImGui::RadioButton(kKindName[i], g_kind == i)) g_kind = i;
    if (g_kind == K_IMPACT) {
        ImGui::SliderInt("impact type", &g_impact, 0, 19);
        if (ImGui::Button("reroll vertex colours")) g_seed++;
    }
    if (g_kind <= K_WATCHLASER) ImGui::SliderFloat("length / width", &g_tracerLen, 1.0f, 20.0f, "%.1f");
    if (g_kind >= K_WALLPUFF && g_kind <= K_SCATTER) {
        const char* table; int life; float step; puffParams(g_kind, &table, &life, &step);
        ImGui::SliderFloat("age (ticks)", &g_age, 0.0f, (float)life - 0.01f, "%.1f");
        ImGui::Checkbox("play", &g_play); ImGui::SameLine(); ImGui::SliderFloat("speed", &g_speed, 0.05f, 2.0f, "%.2f");
        ImGui::SliderAngle("angle", &g_angle, -180.0f, 180.0f);
    }
    ImGui::SeparatorText("View");
    ImGui::RadioButton("side by side", &g_view, VIEW_SIDE); ImGui::SameLine(); ImGui::RadioButton("split", &g_view, VIEW_SPLIT);
    ImGui::RadioButton("original", &g_view, VIEW_ORIG); ImGui::SameLine(); ImGui::RadioButton("effect", &g_view, VIEW_FX);
    if (g_view == VIEW_SPLIT) ImGui::SliderFloat("split at", &g_split, 0.0f, 1.0f);
    ImGui::SliderFloat("zoom", &g_zoom, 0.1f, 4.0f, "%.2f");
    ImGui::SliderFloat("cluster texels / wall texel", &g_cDensity, 0.0f, 4.0f, g_cDensity <= 0.0f ? "full" : "%.2f");
    ImGui::SliderFloat("cluster depth res (x full)", &g_cDepthRes, 0.125f, 1.0f, "%.3f");
    ImGui::Checkbox("nearest texels (off = bilinear)", &g_nearest);
    ImGui::SeparatorText("Backdrop");
    ImGui::RadioButton("grey", &g_bg, BG_GREY); ImGui::SameLine(); ImGui::RadioButton("dark", &g_bg, BG_DARK); ImGui::SameLine();
    ImGui::RadioButton("checker", &g_bg, BG_CHECKER); ImGui::SameLine(); ImGui::RadioButton("wall", &g_bg, BG_WALL);
    if (g_bg == BG_WALL) {
        ImGui::InputInt("ROM texture #", &g_bgTex); if (g_bgTex < 0) g_bgTex = 0;
        const GlTex& t = texture((uint32_t)g_bgTex, 0, 0);
        if (!t.id) ImGui::TextColored(ImVec4(1, .5f, .4f, 1), "not decodable: %s", t.note.c_str()); else ImGui::Text("%dx%d (%s)", t.w, t.h, t.note.c_str());
    }
    ImGui::SeparatorText("fxShade");
    ImGui::Checkbox("animate frame_count", &g_animFx); ImGui::SameLine(); ImGui::Text("%d", g_fc);
    ImGui::TextWrapped("%s (reloaded %d x; edit the file to tune)", g_shaderPath.c_str(), g_reloads);
    if (ImGui::Button("reload now")) reloadShader(true);
    if (!g_shaderErr.empty()) ImGui::TextColored(ImVec4(1, .4f, .4f, 1), "%s", g_shaderErr.c_str());
    ImGui::Separator();
    if (ImGui::Button("screenshot (F12)")) g_shotReq = true;
    ImGui::SameLine();
    if (ImGui::Button("save settings")) saveIni();
    ImGui::End();
}

static bool shotTo(const std::string& path, int W, int H) {   // offscreen, the scene only (with labels)
    GLuint fbo, rbc;
    glGenFramebuffers(1, &fbo); glGenRenderbuffers(1, &rbc);
    glBindRenderbuffer(GL_RENDERBUFFER, rbc); glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, W, H);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo); glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rbc);
    bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    if (ok) {
        glViewport(0, 0, W, H); glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT);
        renderScene(0, 0, W, H, H);
        mvui_begin(W, H); drawLabels(g_mode == 1 ? decalCaption() : g_mode >= 2 ? tabCaption() : buildSprite().label); drawSheetLabels(); mvui_draw();
        std::vector<uint8_t> px((size_t)W * H * 4), fl((size_t)W * H * 4);
        glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
        for (int y = 0; y < H; y++) memcpy(&fl[(size_t)y * W * 4], &px[(size_t)(H - 1 - y) * W * 4], (size_t)W * 4);
        ok = mvpng::write(path, W, H, fl.data());
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0); glDeleteFramebuffers(1, &fbo); glDeleteRenderbuffers(1, &rbc);
    return ok;
}

static int g_cw = 1400, g_ch = 800;
static LRESULT CALLBACK wndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    mvui_wndproc(h, m, w, l);
    switch (m) {
    case WM_SIZE: g_cw = LOWORD(l); g_ch = HIWORD(l); return 0;
    case WM_MOUSEWHEEL: if (mvui_want_mouse()) return 0;
        if (g_mode >= 2) { g_tZoom = fminf(fmaxf(g_tZoom * powf(1.1f, (short)HIWORD(w) / 120.0f), 0.1f), 8.0f); return 0; }
        if (g_mode == 1) { g_dzoom = fminf(fmaxf(g_dzoom * powf(1.1f, (short)HIWORD(w) / 120.0f), 0.1f), 6.0f); return 0; }
        g_zoom = fminf(fmaxf(g_zoom * powf(1.1f, (short)HIWORD(w) / 120.0f), 0.1f), 8.0f); return 0;
    case WM_KEYDOWN: if (w == VK_F12) g_shotReq = true; else if (w == VK_ESCAPE) PostQuitMessage(0); return 0;
    case WM_CLOSE: PostQuitMessage(0); return 0;
    }
    return DefWindowProcA(h, m, w, l);
}

// ---------------------------------------------------------------- selftest (no ROM, no GL)
static int selftest(const std::string& repo) {
    int fail = mv_tex_selftest() + fx_tex_selftest();
    // the effect files: all four read, each family's function in them
    std::string blk, err;
    if (!repo.empty()) {
        if (!readFxSources(repo + "/port/shaders/fx", blk, err)) { fprintf(stderr, "selftest FAIL %s\n", err.c_str()); fail++; }
        else for (const char* fn : { "float fxHash(", "vec4 fxTracer(", "vec4 fxSpark(", "vec4 fxBlood(", "vec4 fxImpact(", "vec4 fxClusterPom(" })
            if (blk.find(fn) == std::string::npos) { fprintf(stderr, "selftest FAIL port/shaders/fx/ has no %s\n", fn); fail++; }
    }
    // decal mode: dynLight is found in the uber shader, images.def gives the hit types, the layout is stable
    if (!repo.empty()) {
        std::string src, d; std::vector<TexInfo> ti;
        if (!readFile(repo + "/port/shaders/uber.frag.glsl", src) || extractDynLight(src).find("return c;") == std::string::npos) { fprintf(stderr, "selftest FAIL dynLight not found in uber.frag.glsl\n"); fail++; }
        if (!readFile(repo + "/assets/images.def", d) || !parseImagesDef(d, ti)) { fprintf(stderr, "selftest FAIL assets/images.def not parsed\n"); fail++; }
        else {
            if (ti.size() < 2600 || ti[20].name != "GRAVELGREY" || ti[20].hit != HIT_DIRT || ti[33].hit != HIT_WOOD) { fprintf(stderr, "selftest FAIL images.def numbering / hit types\n"); fail++; }
            for (const DecalMat& m : kMats) if (m.wallTex >= (int)ti.size() || ti[(size_t)m.wallTex].hit != m.hit) { fprintf(stderr, "selftest FAIL default wall texture %d of %s is not %s\n", m.wallTex, m.key, kHitNames[m.hit]); fail++; }
        }
    }
    {
        const std::vector<Hole> a = holeLayout(6, 3, 10), b = holeLayout(6, 3, 10);
        bool same = a.size() == 6; for (size_t i = 0; same && i < a.size(); i++) same = a[i].x == b[i].x && a[i].ang == b[i].ang && a[i].k >= 0.85f && a[i].k <= 1.15f;
        if (!same || a[0].x != 0 || a[0].y != 0) { fprintf(stderr, "selftest FAIL hole layout\n"); fail++; }
    }
    // the puff frame stepping (bullet_spark_render: frame = (s32)(age * unk08)), the smoke table has 6 frames over 11 ticks
    const int want[11] = { 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5 };
    for (int a = 0; a < 11; a++) if (puffFrame((float)a, 0.5f, 6) != want[a]) { fprintf(stderr, "selftest FAIL puff frame at age %d\n", a); fail++; }
    if (findHexAfter(".set _GlobalimagetableSegmentRomStart, 0x1029D160\n", ".set _GlobalimagetableSegmentRomStart") != 0x1029D160 ||
        findHexAfter("g_pc_gimg_off_s_flareimage30 = 1, g_pc_gimg_off_s_flareimage3 = 0xC60,", "g_pc_gimg_off_s_flareimage3") != 0xC60) { fprintf(stderr, "selftest FAIL hex lookup\n"); fail++; }
    fprintf(stderr, fail ? "selftest: %d failures\n" : "selftest: ok\n", fail);
    return fail ? 1 : 0;
}

int main(int argc, char** argv) {
    std::string shot, romPath, repo; int W = 1400, H = 800;
    for (int i = 1; i + 1 < argc; i++) if (!strcmp(argv[i], "--repo")) repo = argv[i + 1];
    if (repo.empty()) {
        // Started without fxview.py (the exe opened directly): find the checkout from the exe's folder
        // (tools_pc/fxview/build), then from the working directory, walking up to the shader.
        std::string tried[2] = { argc > 0 ? std::string(argv[0]) : std::string(), std::string(".") + "/x" };
        for (const std::string& from : tried) {
            std::string dir = from;
            for (char& ch : dir) if (ch == '\\') ch = '/';
            for (int up = 0; up < 8 && repo.empty(); up++) {
                const size_t cut = dir.find_last_of('/');
                if (cut == std::string::npos) break;
                dir = dir.substr(0, cut);
                std::string probe;
                if (readFile(dir + "/port/shaders/uber.frag.glsl", probe)) repo = dir;
            }
            if (!repo.empty()) break;
        }
    }
    for (int i = 1; i < argc; i++) if (!strcmp(argv[i], "--selftest")) return selftest(repo);
    g_repo = repo;
    g_shaderPath = repo + "/port/shaders/fx";
    g_uberPath = repo + "/port/shaders/uber.frag.glsl";
    {   // the blood colour the port sets (Game.BloodOnHit)
        std::string s;
        if (readFile(repo + "/port/src/switch/nxgame.c", s)) {
            const long long r = findHexAfter(s, "#define BLOOD_R"), g = findHexAfter(s, "#define BLOOD_G"), b = findHexAfter(s, "#define BLOOD_B");
            if (r >= 0 && g >= 0 && b >= 0) { g_blood[0] = (uint8_t)r; g_blood[1] = (uint8_t)g; g_blood[2] = (uint8_t)b; }
        }
    }
    bool shotMode = false;
    for (int i = 1; i < argc; i++) if (!strcmp(argv[i], "--shot")) shotMode = true;
    if (!shotMode) loadIni();   // shots are reproducible: defaults + the command line only
    for (int i = 1; i < argc; i++) {
        const std::string a = argv[i]; auto nx = [&]() { return std::string(i + 1 < argc ? argv[++i] : "0"); };
        if (a == "--shot") shot = nx(); else if (a == "--w") W = atoi(nx().c_str()); else if (a == "--h") H = atoi(nx().c_str());
        else if (a == "--rom") romPath = nx(); else if (a == "--repo") nx(); else if (a == "--shader") g_shaderPath = nx();
        else if (a.size() > 2 && a[0] == '-' && a[1] == '-') setKey(a.substr(2), nx());
    }
    if (!romPath.empty()) romOpen(romPath, repo);
    { std::string d; if (!readFile(repo + "/assets/images.def", d) || !parseImagesDef(d, g_texInfo)) fprintf(stderr, "no assets/images.def: the texture sheet is empty\n"); }
    fprintf(stderr, "%s\n", g_romNote.c_str());

    if (shotMode) { g_cw = W; g_ch = H; }
    WNDCLASSA wc = {}; wc.lpfnWndProc = wndProc; wc.hInstance = GetModuleHandleA(0); wc.lpszClassName = "fxview"; wc.hCursor = LoadCursor(0, IDC_ARROW); RegisterClassA(&wc);
    RECT r = { 0, 0, g_cw, g_ch }; AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd = CreateWindowA("fxview", "fxview", WS_OVERLAPPEDWINDOW | (shotMode ? 0 : WS_VISIBLE), 40, 40, r.right - r.left, r.bottom - r.top, 0, 0, wc.hInstance, 0);
    HDC dc = GetDC(hwnd);
    PIXELFORMATDESCRIPTOR pf = {}; pf.nSize = sizeof pf; pf.nVersion = 1; pf.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER; pf.iPixelType = PFD_TYPE_RGBA; pf.cColorBits = 32;
    SetPixelFormat(dc, ChoosePixelFormat(dc, &pf), &pf);
    HGLRC tmp = wglCreateContext(dc); wglMakeCurrent(dc, tmp);
    auto cca = (PFNWGLCREATECONTEXTATTRIBSARB)wglGetProcAddress("wglCreateContextAttribsARB");
    int at[] = { 0x2091, 3, 0x2092, 3, 0x9126, 1, 0 };   // GL 3.3 core
    HGLRC rc = cca ? cca(dc, 0, at) : 0; if (!rc) { fprintf(stderr, "no GL 3.3 core context\n"); return 1; }
    wglMakeCurrent(0, 0); wglDeleteContext(tmp); wglMakeCurrent(dc, rc);
#define X(T,N) N = (T)wglGetProcAddress(#N);
    GLFUNCS
#undef X
    auto swapi = (PFNWGLSWAPINTERVAL)wglGetProcAddress("wglSwapIntervalEXT"); if (swapi) swapi(1);

    g_pBack = link(kVert, kBackFrag); if (!g_pBack) { fprintf(stderr, "backdrop shader: %s\n", g_shaderLog.c_str()); return 2; }
    reloadShader(true); if (!g_pSprite) { fprintf(stderr, "sprite shader: %s\n", g_shaderLog.c_str()); return 2; }
    glGenVertexArrays(1, &g_vao); glGenBuffers(1, &g_vbo); glGenBuffers(1, &g_ibo);
    { const uint8_t w4[4] = { 255, 255, 255, 255 }; glGenTextures(1, &g_white); glBindTexture(GL_TEXTURE_2D, g_white); glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, w4); }
    mvui_init();

    if (shotMode) {
        const bool ok = shotTo(shot, W, H);
        fprintf(stderr, "%s %s%s\n", ok ? "saved" : "FAILED to save", shot.c_str(), g_shaderErr.empty() ? "" : " (fxShade did not compile: the effect side is the original)");
        return ok && g_shaderErr.empty() ? 0 : 1;
    }
    MSG msg; bool run = true; DWORD last = GetTickCount(), lastPoll = last; float fcAcc = 0;
    while (run) {
        while (PeekMessageA(&msg, 0, 0, 0, PM_REMOVE)) { if (msg.message == WM_QUIT) run = false; TranslateMessage(&msg); DispatchMessageA(&msg); }
        if (!run) break;
        const DWORD now = GetTickCount(); const float dt = fminf((now - last) / 1000.0f, 0.25f); last = now;
        if (now - lastPoll > 300) { lastPoll = now; reloadShader(false); }   // hot reload
        if (g_mode == 1) {   // decal mode mouse: left-drag orbits, right-drag moves the point light on the wall, a click on the sheet picks
            static bool dragL = false, dragR = false; static POINT last0;
            POINT cp; GetCursorPos(&cp); ScreenToClient(hwnd, &cp);
            const bool fg = GetForegroundWindow() == hwnd, lb = fg && (GetAsyncKeyState(VK_LBUTTON) & 0x8000), rb = fg && (GetAsyncKeyState(VK_RBUTTON) & 0x8000);
            const bool inView = cp.x >= kPanelW && cp.x < g_cw && cp.y >= 0 && cp.y < g_ch && !mvui_want_mouse();
            if (lb && !dragL && inView) {
                dragL = true; last0 = cp;
                if (g_view == 4) for (const SheetCell& c : g_sheet) if (cp.x >= c.x && cp.x < c.x + c.w && cp.y >= c.y && cp.y < c.y + c.h) { g_wallTex = c.tex; g_view = VIEW_SIDE; dragL = false; }
            } else if (lb && dragL && g_view != 4) {
                g_yaw = fminf(fmaxf(g_yaw + (cp.x - last0.x) * 0.3f, -85.0f), 85.0f); g_pitch = fminf(fmaxf(g_pitch + (cp.y - last0.y) * 0.3f, -85.0f), 85.0f); last0 = cp;
            } else if (!lb) dragL = false;
            if (rb && (dragR || inView)) { dragR = true; float wp[2]; if (pickWall(cp.x, cp.y, wp)) { g_ptX = wp[0]; g_ptY = wp[1]; } }
            else if (!rb) dragR = false;
        } else if (g_mode >= 2) {   // the tabs: left-drag orbits
            static bool drag = false; static POINT l0;
            POINT cp; GetCursorPos(&cp); ScreenToClient(hwnd, &cp);
            const bool lb = GetForegroundWindow() == hwnd && (GetAsyncKeyState(VK_LBUTTON) & 0x8000);
            const bool inView = cp.x >= kPanelW && cp.x < g_cw && cp.y >= 0 && cp.y < g_ch && !mvui_want_mouse();
            if (lb && !drag && inView) { drag = true; l0 = cp; }
            else if (lb && drag) { g_tYaw = fminf(fmaxf(g_tYaw + (cp.x - l0.x) * 0.3f, -85.0f), 85.0f); g_tPitch = fminf(fmaxf(g_tPitch + (cp.y - l0.y) * 0.3f, -85.0f), 85.0f); l0 = cp; }
            else if (!lb) drag = false;
        }
        if (g_play && g_kind >= K_WALLPUFF && g_kind <= K_SCATTER) {   // 60 ticks a second (g_ClockTimer units)
            const char* table; int life; float step; puffParams(g_kind, &table, &life, &step);
            g_age += dt * 60.0f * g_speed; if (g_age >= life) g_age = fmodf(g_age, (float)life);
        }
        tabTick(dt);
        if (g_animFx) { fcAcc += dt * 60.0f; g_fc += (int)fcAcc; fcAcc -= floorf(fcAcc); }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, g_cw, g_ch); glClearColor(0.1f, 0.1f, 0.1f, 1); glClear(GL_COLOR_BUFFER_BIT);
        if (g_cw > kPanelW + 16 && g_ch > 16) renderScene(kPanelW, 0, g_cw - kPanelW, g_ch, g_ch);
        mvui_begin(g_cw, g_ch); uiPanel(g_ch); drawLabels(g_mode == 1 ? decalCaption() : g_mode >= 2 ? tabCaption() : buildSprite().label); drawSheetLabels(); mvui_draw();
        if (g_shotReq) {
            g_shotReq = false;
            char fn[64]; for (int n = 0; ; n++) { snprintf(fn, sizeof fn, "shot_%03d.png", n); if (GetFileAttributesA(fn) == INVALID_FILE_ATTRIBUTES) break; }
            fprintf(stderr, "%s %s\n", shotTo(fn, 1400, 700) ? "saved" : "FAILED to save", fn);
        }
        SwapBuffers(dc);
    }
    saveIni();
    return 0;
}
