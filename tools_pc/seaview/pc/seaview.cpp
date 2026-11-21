// seaview: Frigate's sea + sky alone, the game's own shaders (skycommon/skyplus/skylut), free camera, playground.
// keys: WASD move (shift fast), Q/E height, right-mouse or arrows look, B sway on/off, T time freeze, H half-width divisor 1/2/4,
// 0-9 mode (uWave4.w), Z/X select playground param (uPlay.xyzw), ,/. change it, R reload shaders, P screenshot, Esc quit.
// args: --shot f.tga|f.png --yaw d --pitch d --time t --mode n|sea|water --half n --sway 1 --play a,b,c,d --dump file --shaders dir --w n --h n
//       --level dam|caverns|complex --cam x,y,z,yaw,pitch --wtex file.png|tga (see README: Water surface mode)
// F12: screenshot (png, numbered). Menus Mode / Level / Water: the "Water surface" mode draws a level-water plane with water.frag.glsl.
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <string>
#include <vector>
#include "../common/img.h"
#include "../common/waterbake.h"

#define GLFUNCS \
 X(PFNGLCREATESHADERPROC,glCreateShader) X(PFNGLSHADERSOURCEPROC,glShaderSource) X(PFNGLCOMPILESHADERPROC,glCompileShader) \
 X(PFNGLGETSHADERIVPROC,glGetShaderiv) X(PFNGLGETSHADERINFOLOGPROC,glGetShaderInfoLog) X(PFNGLCREATEPROGRAMPROC,glCreateProgram) \
 X(PFNGLATTACHSHADERPROC,glAttachShader) X(PFNGLLINKPROGRAMPROC,glLinkProgram) X(PFNGLGETPROGRAMIVPROC,glGetProgramiv) \
 X(PFNGLGETPROGRAMINFOLOGPROC,glGetProgramInfoLog) X(PFNGLUSEPROGRAMPROC,glUseProgram) X(PFNGLGETUNIFORMLOCATIONPROC,glGetUniformLocation) \
 X(PFNGLUNIFORM4FPROC,glUniform4f) X(PFNGLGENVERTEXARRAYSPROC,glGenVertexArrays) X(PFNGLBINDVERTEXARRAYPROC,glBindVertexArray) \
 X(PFNGLGENBUFFERSPROC,glGenBuffers) X(PFNGLBINDBUFFERPROC,glBindBuffer) X(PFNGLBUFFERDATAPROC,glBufferData) \
 X(PFNGLBUFFERSUBDATAPROC,glBufferSubData) X(PFNGLBINDBUFFERBASEPROC,glBindBufferBase) X(PFNGLACTIVETEXTUREPROC,glActiveTexture) \
 X(PFNGLGENERATEMIPMAPPROC,glGenerateMipmap) X(PFNGLGENFRAMEBUFFERSPROC,glGenFramebuffers) X(PFNGLBINDFRAMEBUFFERPROC,glBindFramebuffer) \
 X(PFNGLFRAMEBUFFERTEXTURE2DPROC,glFramebufferTexture2D) X(PFNGLDELETEPROGRAMPROC,glDeleteProgram) X(PFNGLDELETESHADERPROC,glDeleteShader) \
 X(PFNGLGENQUERIESPROC,glGenQueries) X(PFNGLBEGINQUERYPROC,glBeginQuery) X(PFNGLENDQUERYPROC,glEndQuery) \
 X(PFNGLGETQUERYOBJECTUI64VPROC,glGetQueryObjectui64v) X(PFNGLBLITFRAMEBUFFERPROC,glBlitFramebuffer) X(PFNGLUNIFORM1IPROC,glUniform1i) X(PFNGLUNIFORM1FVPROC,glUniform1fv) \
 X(PFNGLGENRENDERBUFFERSPROC,glGenRenderbuffers) X(PFNGLBINDRENDERBUFFERPROC,glBindRenderbuffer) X(PFNGLRENDERBUFFERSTORAGEPROC,glRenderbufferStorage) X(PFNGLFRAMEBUFFERRENDERBUFFERPROC,glFramebufferRenderbuffer) \
 X(PFNGLVERTEXATTRIBPOINTERPROC,glVertexAttribPointer) X(PFNGLENABLEVERTEXATTRIBARRAYPROC,glEnableVertexAttribArray)
#define X(T,N) static T N = 0;
GLFUNCS
#undef X
typedef HGLRC (WINAPI *PFNWGLCREATECONTEXTATTRIBSARB)(HDC, HGLRC, const int*);
typedef BOOL (WINAPI *PFNWGLSWAPINTERVAL)(int);

static std::string readfile(const std::string& p) { FILE* f = fopen(p.c_str(), "rb"); if (!f) return ""; std::string s; char b[4096]; size_t n; while ((n = fread(b, 1, sizeof b, f))) s.append(b, n); fclose(f); return s; }
static std::string gShDir = "port/switch/shaders/";   /* --shaders <repo>/port/switch/shaders/ (seaview.py passes it) */
static GLuint compile(GLenum t, const std::string& src, const char* name) {
    GLuint s = glCreateShader(t); const char* c = src.c_str(); glShaderSource(s, 1, &c, 0); glCompileShader(s);
    GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[8192]; glGetShaderInfoLog(s, sizeof log, 0, log); fprintf(stderr, "SHADER %s:\n%s\n", name, log); glDeleteShader(s); return 0; }
    return s;
}
static const char* kResolve = R"(#version 460
layout(location=0) in vec2 vUV;
layout(binding=0) uniform sampler2D uCur;
layout(binding=1) uniform sampler2D uHist;
uniform vec4 uC, uHX, uHY, uPC, uPHX, uPHY, uP;
layout(location=0) out vec4 o;
void main() {
    ivec2 sz = textureSize(uCur, 0); ivec2 ip = ivec2(gl_FragCoord.xy);
    vec3 cur = texelFetch(uCur, ip, 0).rgb, m1 = vec3(0.0), m2 = vec3(0.0);
    for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
        vec3 c = texelFetch(uCur, clamp(ip + ivec2(dx, dy), ivec2(0), sz - 1), 0).rgb; m1 += c; m2 += c * c; }
    vec3 mu = m1 / 9.0, sd = sqrt(max(m2 / 9.0 - mu * mu, 0.0));
    float u = vUV.x, v = 1.0 - vUV.y;
    vec3 d = uC.xyz + uHX.xyz * (2.0 * u - 1.0) + uHY.xyz * (1.0 - 2.0 * v);
    float t = dot(uPC.xyz, uPC.xyz) / dot(d, uPC.xyz);
    vec3 q = d * t - uPC.xyz;
    float a = dot(q, uPHX.xyz) / dot(uPHX.xyz, uPHX.xyz), b = dot(q, uPHY.xyz) / dot(uPHY.xyz, uPHY.xyz);
    vec2 huv = vec2((a + 1.0) * 0.5, (b + 1.0) * 0.5);
    vec3 h = texture(uHist, huv).rgb;
    float ok = (uP.y > 0.5 && dot(d, uPC.xyz) > 0.0 && huv.x > 0.0 && huv.x < 1.0 && huv.y > 0.0 && huv.y < 1.0) ? 1.0 : 0.0;
    h = clamp(h, mu - uP.z * sd, mu + uP.z * sd);
    o = vec4(mix(cur, mix(cur, h, 1.0 - uP.x), ok), 1.0);
})";
static GLuint buildSrc(const char* vs, const std::string& fsrc) {
    std::string v = readfile(gShDir + vs);
    GLuint a = compile(GL_VERTEX_SHADER, v, vs), b = compile(GL_FRAGMENT_SHADER, fsrc, "resolve"); if (!a || !b) return 0;
    GLuint p = glCreateProgram(); glAttachShader(p, a); glAttachShader(p, b); glLinkProgram(p);
    GLint ok = 0; glGetProgramiv(p, GL_LINK_STATUS, &ok); return ok ? p : 0;
}
static GLuint build(const char* vs, const char* fs) {
    std::string common = readfile(gShDir + "skycommon.glsl");
    std::string v = readfile(gShDir + vs), f = readfile(gShDir + fs);
    size_t k = f.find("#pragma skycommon"); if (k != std::string::npos) f.replace(k, 17, common);
    for (size_t y; (y = f.find("vec2(f.x, 1.0 - f.y)")) != std::string::npos;) f.replace(y, 20, "vec2(f.x, f.y)");
    for (size_t y; (y = f.find("vec2(hf.x, 1.0 - hf.y)")) != std::string::npos;) f.replace(y, 22, "vec2(hf.x, hf.y)");
    for (size_t y; (y = f.find("vec2(pu, 1.0 - pv)")) != std::string::npos;) f.replace(y, 18, "vec2(pu, pv)");   // GL render targets are y-up (deko3d's are not)
    size_t nl = f.find('\n'); f.insert(nl + 1, "#define VIEWER\nuniform vec4 uPlay;\nlayout(std140, binding = 1) uniform Viewer { ivec4 uVarv; vec4 uPrv[6]; };\n#define uSeaVar uVarv.x\n#define uSkyVar uVarv.y\n");
    GLuint a = compile(GL_VERTEX_SHADER, v, vs), b = compile(GL_FRAGMENT_SHADER, f, fs); if (!a || !b) return 0;
    GLuint p = glCreateProgram(); glAttachShader(p, a); glAttachShader(p, b); glLinkProgram(p);
    GLint ok = 0; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { char log[8192]; glGetProgramInfoLog(p, sizeof log, 0, log); fprintf(stderr, "LINK %s:\n%s\n", fs, log); return 0; }
    return p;
}
/* The water plane's vertex shader: stands in for uber.vert.glsl (the game's draws arrive transformed; here the camera of the sky's
 * corner rays does it) and provides the locations water.frag.glsl reads: vTexCoord0/1, vFog, vFogW, vInput1 = PRIM_LOD_FRAC,
 * vInput2 = SHADE, vClip. A grid denser near the eye (the fog is per vertex). */
static const char* kWaterVert = R"(#version 460
layout(location = 0) in vec2 aG;
uniform vec4 uEyeV, uCC, uHX, uHY, uPl, uGrid, uShade, uFogC, uExt, uAx;
layout(location = 1) out vec2 vTexCoord0;
layout(location = 2) out vec2 vTexCoord1;
layout(location = 7) out vec4 vFog;
layout(location = 16) out float vFogW;
layout(location = 9) out vec4 vInput1;
layout(location = 10) out vec4 vInput2;
layout(location = 17) out vec3 vClip;
void main() {
    /* uExt.z = 0: the big plane (denser near the centre); 1: a rectangle of half sizes uExt.xy along the axes uAx.xy / uAx.zw */
    vec2 g = uExt.z > 0.5 ? aG : aG * abs(aG);
    vec2 pxz = uGrid.xy + uAx.xy * (g.x * uExt.x) + uAx.zw * (g.y * uExt.y);
    vec3 P = vec3(pxz.x, uPl.x, pxz.y);
    vec3 Q = P - uEyeV.xyz;
    float w = dot(Q, uCC.xyz) / dot(uCC.xyz, uCC.xyz);
    float cx = dot(Q, uHX.xyz) / dot(uHX.xyz, uHX.xyz), cy = dot(Q, uHY.xyz) / dot(uHY.xyz, uHY.xyz);
    float N = uEyeV.w, F = uCC.w;   /* near / far in w units: |cc| is not 1 (the dump's corner rays) */
    gl_Position = vec4(cx, cy, ((F + N) * w - 2.0 * F * N) / (F - N), w);
    vec2 uv = P.xz / uPl.y;
    vTexCoord0 = uv;
    vTexCoord1 = uv * uPl.z + vec2(0.31, 0.17) + vec2(0.02, 0.013) * uGrid.w;
    float fogA = uPl.w > 0.0 ? clamp(length(Q) / uPl.w, 0.0, 1.0) : 0.0;
    vFog = vec4(uFogC.rgb, fogA); vFogW = fogA;
    vInput1 = vec4(uFogC.w); vInput2 = uShade;
    vClip = vec3(cx, cy, w);
}
)";
/* The test scene's boxes: world position, normal, colour; the same projection as the water's vertex shader. */
static const char* kSceneVert = R"(#version 460
layout(location = 0) in vec3 aP;
layout(location = 1) in vec3 aN;
layout(location = 2) in vec3 aC;
uniform vec4 uEyeV, uCC, uHX, uHY;
out vec3 vN; out vec3 vC;
void main() {
    vec3 Q = aP - uEyeV.xyz;
    float w = dot(Q, uCC.xyz) / dot(uCC.xyz, uCC.xyz);
    float cx = dot(Q, uHX.xyz) / dot(uHX.xyz, uHX.xyz), cy = dot(Q, uHY.xyz) / dot(uHY.xyz, uHY.xyz);
    float N = uEyeV.w, F = uCC.w;
    gl_Position = vec4(cx, cy, ((F + N) * w - 2.0 * F * N) / (F - N), w);
    vN = aN; vC = aC;
}
)";
static const char* kSceneFrag = R"(#version 460
in vec3 vN; in vec3 vC;
layout(location = 0) out vec4 o;
void main() { float l = 0.35 + 0.65 * max(dot(normalize(vN), normalize(vec3(0.4, 0.8, 0.3))), 0.0); o = vec4(vC * l, 0.0); }
)";
/* Debug view 1: the half-size FX target as the composite reads it (colour / coverage; dark magenta = no coverage) */
static const char* kHalfDbg = R"(#version 460
layout(location = 0) in vec2 vUV;
layout(binding = 0) uniform sampler2D uHalf;
layout(location = 0) out vec4 o;
void main() { vec4 h = texture(uHalf, vUV); o = vec4(h.a > 0.05 ? h.rgb / h.a : vec3(0.08, 0.0, 0.08), 1.0); }
)";
/* water.frag.glsl with skycommon pasted in and the viewer's variant block (binding 5: water.frag owns 0..4).
 * variant 0: the file as is = the full shading (one full-res pass, and the FX pass of the half-res pipeline),
 * variant 1: #define WATER_BODY = the cheap body (step 1 of the half-res pipeline); the game's own define. */
static GLuint buildWater(int variant = 0) {
    std::string common = readfile(gShDir + "skycommon.glsl"), f = readfile(gShDir + "water.frag.glsl");
    size_t k = f.find("#pragma skycommon"); if (k != std::string::npos) f.replace(k, 17, common);
    std::string hdr = "#define VIEWER\nuniform vec4 uPlay;\nlayout(std140, binding = 5) uniform Viewer { ivec4 uVarv; vec4 uPrv[6]; };\n#define uSeaVar uVarv.x\n#define uSkyVar uVarv.y\n";
    if (variant == 1) { if (f.find("WATER_BODY") == std::string::npos) { fprintf(stderr, "water.frag.glsl has no WATER_BODY variant\n"); return 0; } hdr += "#define WATER_BODY\n"; }
    size_t nl = f.find('\n'); f.insert(nl + 1, hdr);
    GLuint a = compile(GL_VERTEX_SHADER, kWaterVert, "water vertex"), b = compile(GL_FRAGMENT_SHADER, f, variant == 1 ? "water.frag.glsl (WATER_BODY)" : "water.frag.glsl"); if (!a || !b) return 0;
    GLuint p = glCreateProgram(); glAttachShader(p, a); glAttachShader(p, b); glLinkProgram(p);
    GLint ok = 0; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { char log[8192]; glGetProgramInfoLog(p, sizeof log, 0, log); fprintf(stderr, "LINK water:\n%s\n", log); return 0; }
    return p;
}
/* watercomp.frag.glsl (the game's step-3 composite) on the viewer's water vertex shader. mask: debug view 2, the file patched so that a
 * pixel that passes the depth test is green where the half-size colour is used and red where the shader discards (the cheap body stays) */
static GLuint buildComp(bool mask) {
    std::string f = readfile(gShDir + "watercomp.frag.glsl"); if (f.empty()) { fprintf(stderr, "no watercomp.frag.glsl\n"); return 0; }
    if (mask) {
        auto patch = [&](const char* a, const char* b) { size_t q = f.find(a); if (q == std::string::npos) { fprintf(stderr, "mask patch: %s not found\n", a); return; } f.replace(q, strlen(a), b); };
        patch("if (a < 0.05) discard;", "if (a < 0.05) { outColor = vec4(1.0, 0.0, 0.0, 1.0); return; }");
        patch("outColor = vec4(clamp(h.rgb / a, 0.0, 1.0), smoothstep(0.3, 0.85, a));", "outColor = vec4(0.0, 1.0, 0.0, smoothstep(0.3, 0.85, a));");
    }
    GLuint a = compile(GL_VERTEX_SHADER, kWaterVert, "water vertex"), b = compile(GL_FRAGMENT_SHADER, f, "watercomp.frag.glsl"); if (!a || !b) return 0;
    GLuint p = glCreateProgram(); glAttachShader(p, a); glAttachShader(p, b); glLinkProgram(p);
    GLint ok = 0; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { char log[8192]; glGetProgramInfoLog(p, sizeof log, 0, log); fprintf(stderr, "LINK watercomp:\n%s\n", log); return 0; }
    return p;
}
static GLuint tex2d(int w, int h, const void* px, bool mips, bool repeatY, float aniso) {
    GLuint t; glGenTextures(1, &t); glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    if (mips) glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mips ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, repeatY ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, repeatY ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    if (aniso > 1) glTexParameterf(GL_TEXTURE_2D, 0x84FE, aniso);
    return t;
}
struct V3 { float x, y, z; };
static V3 add(V3 a, V3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
static V3 sub(V3 a, V3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
static V3 mul(V3 a, float s) { return { a.x * s, a.y * s, a.z * s }; }
static float len(V3 a) { return sqrtf(a.x * a.x + a.y * a.y + a.z * a.z); }
static V3 nrm(V3 a) { return mul(a, 1.0f / len(a)); }
static V3 cross(V3 a, V3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
static V3 rotAxis(V3 v, V3 k, float a) { float c = cosf(a), s = sinf(a); return add(add(mul(v, c), mul(cross(k, v), s)), mul(k, (k.x * v.x + k.y * v.y + k.z * v.z) * (1 - c))); }

static bool keys[256]; static bool kdown[256];
static int g_seaVar = 0, g_skyVar = 0;
static const char* kSeaNames[] = { "current", "no medium swell", "1 layer", "2 layers", "3 layers", "bump, 1 layer (sunset light)",
    "bump, 2 layers (sunset light)", "no sky reflection", "all baked (nothing live)", "baked from 600-2500", "baked from 300-1500",
    "no calm patches", "2 layers, reflection, no swell or patches" };
static const char* kSkyNames[] = { "current", "1 cloud layer", "2 cloud layers", "no cloud lighting", "no halo or stars",
    "sky = low-res panorama", "flat sky (cost floor)" };
static HMENU g_seaMenu, g_skyMenu, g_modeMenu, g_levelMenu, g_waterMenu;
static int g_mode = 0;    /* 0 = Sea (the Frigate view), 1 = Water surface (a level's water plane under the sky) */
static int g_level = 0;   /* 0 Dam, 1 Caverns, 2 Complex */
static int g_half = 0;    /* water: 0 = one full-resolution water.frag pass, 1 = the game's 3 steps (cheap body, half-res FX, composite) */
static int g_scene = 1;   /* the edge test scene: a pier, boxes, a second pool and a wall, drawn with depth */
static int g_dbg = 0;     /* debug view: 0 final, 1 half-res FX target, 2 depth-key mismatch mask, 3 cheap body only */
static HMENU g_dbgMenu;
static const char* kDbgNames[] = { "Final", "Half-res FX target", "Depth-key mask (green = replaced, red = water nearby, no key matched)", "Cheap body only (step 1)" };
static std::string g_wtexPath;    /* the water texture file (empty: data/water_<level>.png|.tga, else the placeholder) */
static bool g_wtexDirty = true;   /* (re)load and re-bake the water texture */
static bool g_haveCam = false; static float g_cam[5];   /* --cam x,y,z,yaw,pitch */
static const char* kLevelNames[] = { "dam", "caverns", "complex" };
static bool g_mlook = false;
static int g_rep = 1, g_scale = 1; static HMENU g_repMenu, g_scaleMenu; static char g_gpuName[128] = "";
#ifdef SEAVIEW_HP   /* ask hybrid laptops for the discrete GPU */
extern "C" { __declspec(dllexport) DWORD NvOptimusEnablement = 1; __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1; }
#endif
/* settings window. kind 0: shader value uPr[idx] (PRM in the shaders, default = the game's constant);
 * kind 1: a member of the sky block, u[idx]; kind 2: sun elevation, sun azimuth (degrees), eye height */
struct Par { const char* name; int kind; int idx; float def; };
static const Par kPar[] = {
    { "Sun elevation (deg)", 2, 0, 0 }, { "Sun azimuth (deg)", 2, 1, 0 }, { "Sun glow", 1, 42, 0 },
    { "Sun colour R", 1, 48, 0 }, { "Sun colour G", 1, 49, 0 }, { "Sun colour B", 1, 50, 0 },
    { "Night (0/1)", 1, 43, 0 },
    { "Sky colour R", 1, 28, 0 }, { "Sky colour G", 1, 29, 0 }, { "Sky colour B", 1, 30, 0 },
    { "Sky zenith (x colour)", 0, 13, 0.82f }, { "Horizon band (sky)", 1, 7, 0 },
    { "Cloud height", 1, 27, 0 }, { "Cloud texture scale", 1, 35, 0 }, { "Cloud gain", 1, 31, 0 },
    { "Cloud opacity (x)", 0, 8, 1.0f }, { "Dome curvature (radius)", 0, 0, 100000.0f },
    { "Eye height", 2, 2, 0 }, { "Sea level", 1, 59, 0 },
    { "Water colour R", 1, 56, 0 }, { "Water colour G", 1, 57, 0 }, { "Water colour B", 1, 58, 0 },
    { "Swell", 1, 47, 0 }, { "Medium swell (gain)", 0, 11, 5.0f }, { "Chop", 1, 51, 0 }, { "Wave speed", 1, 55, 0 },
    { "Calm patches min", 0, 6, 0.35f }, { "Calm patches max", 0, 7, 1.45f }, { "Flatten far off (x)", 0, 5, 0.35f },
    { "Fresnel power", 0, 1, 5.0f }, { "Fresnel gain", 0, 2, 0.9f }, { "Fresnel base", 0, 3, 0.08f },
    { "Far reflection dimming", 0, 4, 0.5f }, { "Horizon fade", 0, 12, 0.006f },
    { "Bake from (0 = no bake)", 0, 9, 600.0f }, { "Fully baked at", 0, 10, 6000.0f },
    { "Cloud light min (low sun)", 0, 14, 0.3f }, { "Cloud light, sun side", 0, 15, 0.8f },
    /* kind 3: the water surface mode (viewer only; the Level menu presets them). idx = position in the water block */
    { "Water plane height", 3, 0, 0.0f }, { "Water texture tile (units per repeat)", 3, 1, 256.0f }, { "Patch map size (repeats)", 3, 2, 6.0f },
    { "reflectLit (0..1)", 3, 3, 0.0f }, { "PRIM_LOD_FRAC (texture blend)", 3, 4, 0.5f }, { "2nd texture scale", 3, 5, 0.7f },
    { "Fog end (0 = no fog)", 3, 6, 12000.0f }, { "Water only, no sky (cave) 0/1", 3, 7, 0.0f },
    { "Deep colour R", 3, 8, 0.10f }, { "Deep colour G", 3, 9, 0.20f }, { "Deep colour B", 3, 10, 0.22f },
    { "Light colour R", 3, 11, 0.30f }, { "Light colour G", 3, 12, 0.45f }, { "Light colour B", 3, 13, 0.48f },
    { "Shade R", 3, 14, 0.30f }, { "Shade G", 3, 15, 0.34f }, { "Shade B", 3, 16, 0.36f },
    { "Fog colour R", 3, 17, 0.50f }, { "Fog colour G", 3, 18, 0.58f }, { "Fog colour B", 3, 19, 0.65f },
    { "Tone keep (reflection in the body hue)", 3, 20, 0.8f }, { "Reflection gain", 3, 21, 0.6f },
};
#define NPAR ((int)(sizeof kPar / sizeof *kPar))
#define WATER0 38   /* index of the first kind-3 entry of kPar */
static float g_val[NPAR], g_def[NPAR];
static const char* kTabs[] = { "Sun", "Sky", "Clouds", "Sea", "Fresnel / bake", "Water surface" };
static int tabOf(int i) { return kPar[i].kind == 3 ? 5 : i < 7 ? 0 : i < 12 ? 1 : i < 17 ? 2 : i < 29 ? 3 : i < 36 ? 4 : 2; }
static int parIdx(const char* name) { for (int i = 0; i < NPAR; i++) if (!strcmp(kPar[i].name, name)) return i; fprintf(stderr, "no parameter %s\n", name); return 0; }
static HWND g_edit[NPAR], g_lbl[NPAR], g_rst[NPAR], g_tab;
static const char* kTex[] = { "game texture (GE)", "soft fbm", "contrasted fbm", "cumulus", "stretched cirrus", "clear sky" };
static int g_texSel = 0; static bool g_texDirty = false;
#define NCOL 7
static HWND g_colBtn[NCOL], g_colLbl[NCOL], g_colRst[NCOL]; static const int kColTab[NCOL] = { 0, 1, 3, 5, 5, 5, 5 };
static const int kColFirst[NCOL] = { 3, 7, 19, 46, 49, 52, 55 };   /* the R of each colour (G and B follow) */
static const char* kColName[NCOL] = { "Sun colour", "Sky colour", "Water colour (tint)", "Deep colour", "Light colour", "Shade (vertex colour)", "Fog colour" };
static int colourOf(int i) { for (int k = 0; k < NCOL; k++) if (i >= kColFirst[k] && i < kColFirst[k] + 3) return k; return -1; }   /* the R G B values of a colour */
static void showTab(int t) {
    for (int k = 0; k < NCOL; k++) if (g_colBtn[k]) { int sw = kColTab[k] == t ? SW_SHOW : SW_HIDE; ShowWindow(g_colBtn[k], sw); ShowWindow(g_colLbl[k], sw); ShowWindow(g_colRst[k], sw); }
    for (int i = 0; i < NPAR; i++) { int sw = (tabOf(i) == t && colourOf(i) < 0) ? SW_SHOW : SW_HIDE; ShowWindow(g_edit[i], sw); ShowWindow(g_lbl[i], sw); ShowWindow(g_rst[i], sw); }
}
static void repaintSwatches() { for (int k = 0; k < NCOL; k++) if (g_colBtn[k]) InvalidateRect(g_colBtn[k], nullptr, TRUE); }
static void setVal(int i, float v) { char b[64]; snprintf(b, sizeof b, "%g", v); SetWindowTextA(g_edit[i], b); g_val[i] = v; }
static void saveIni(const char* path) {
    FILE* f = fopen(path, "w"); if (!f) return;
    for (int i = 0; i < NPAR; i++) fprintf(f, "%s=%g\n", kPar[i].name, g_val[i]);
    fprintf(f, "__sea=%d\n__sky=%d\n__tex=%d\n__mode=%d\n__level=%d\n__wtex=%s\n__half=%d\n__scene=%d\n__dbg=%d\n", g_seaVar, g_skyVar, g_texSel, g_mode, g_level, g_wtexPath.c_str(), g_half, g_scene, g_dbg); fclose(f);
}
static void loadIni(const char* path) {
    FILE* f = fopen(path, "r"); if (!f) return;
    char line[600];
    while (fgets(line, sizeof line, f)) {
        char* eq = strchr(line, '='); if (!eq) continue; *eq = 0; float v = (float)atof(eq + 1);
        if (!strcmp(line, "__wtex")) { std::string t = eq + 1; while (!t.empty() && (t.back() == '\n' || t.back() == '\r')) t.pop_back(); g_wtexPath = t; g_wtexDirty = true; continue; }
        if (!strcmp(line, "__half")) { g_half = (int)v; CheckMenuItem(g_waterMenu, 8203, MF_BYCOMMAND | (g_half ? MF_CHECKED : MF_UNCHECKED)); continue; }
        if (!strcmp(line, "__scene")) { g_scene = (int)v; CheckMenuItem(g_waterMenu, 8204, MF_BYCOMMAND | (g_scene ? MF_CHECKED : MF_UNCHECKED)); continue; }
        if (!strcmp(line, "__dbg")) { g_dbg = (int)v; CheckMenuRadioItem(g_dbgMenu, 9000, 9003, 9000 + g_dbg, MF_BYCOMMAND); continue; }
        if (!strcmp(line, "__mode")) { g_mode = (int)v; CheckMenuRadioItem(g_modeMenu, 8000, 8001, 8000 + g_mode, MF_BYCOMMAND); continue; }
        if (!strcmp(line, "__level")) { g_level = (int)v; CheckMenuRadioItem(g_levelMenu, 8100, 8102, 8100 + g_level, MF_BYCOMMAND); g_wtexDirty = true; continue; }
        if (!strcmp(line, "__sea")) { g_seaVar = (int)v; CheckMenuRadioItem(g_seaMenu, 1000, 1099, 1000 + g_seaVar, MF_BYCOMMAND); continue; }
        if (!strcmp(line, "__sky")) { g_skyVar = (int)v; CheckMenuRadioItem(g_skyMenu, 2000, 2099, 2000 + g_skyVar, MF_BYCOMMAND); continue; }
        if (!strcmp(line, "__tex")) { g_texSel = (int)v; g_texDirty = true; continue; }
        for (int i = 0; i < NPAR; i++) if (!strcmp(line, kPar[i].name)) setVal(i, v);
    }
    fclose(f);
}
static HWND g_texCombo;
static void pickColour(HWND owner, int k) {
    static COLORREF custom[16];
    const int f = kColFirst[k];
    auto c8 = [](float v) { int i = (int)(fminf(fmaxf(v, 0.f), 1.f) * 255.f + 0.5f); return i; };
    CHOOSECOLORA cc = {}; cc.lStructSize = sizeof cc; cc.hwndOwner = owner; cc.lpCustColors = custom;
    cc.rgbResult = RGB(c8(g_val[f]), c8(g_val[f + 1]), c8(g_val[f + 2])); cc.Flags = CC_FULLOPEN | CC_RGBINIT;
    if (ChooseColorA(&cc)) { setVal(f, GetRValue(cc.rgbResult) / 255.f); setVal(f + 1, GetGValue(cc.rgbResult) / 255.f); setVal(f + 2, GetBValue(cc.rgbResult) / 255.f); repaintSwatches(); }
}
static bool fileDialog(HWND owner, bool save, char* path, int n, const char* filter = "Settings (*.ini)\0*.ini\0All\0*.*\0", const char* ext = "ini") {
    OPENFILENAMEA o = {}; o.lStructSize = sizeof o; o.hwndOwner = owner; o.lpstrFilter = filter; o.lpstrFile = path; o.nMaxFile = n;
    o.lpstrDefExt = ext; o.Flags = save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST;
    return save ? GetSaveFileNameA(&o) != 0 : GetOpenFileNameA(&o) != 0;
}
static LRESULT CALLBACK pp(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_COMMAND) {
        const int id = LOWORD(w), code = HIWORD(w);
        if (code == EN_CHANGE && id >= 3000 && id < 3000 + NPAR) { char b[64]; GetWindowTextA((HWND)l, b, sizeof b); g_val[id - 3000] = (float)atof(b); }
        else if (code == BN_CLICKED && id >= 4000 && id < 4000 + NPAR) setVal(id - 4000, g_def[id - 4000]);
        else if (code == BN_CLICKED && id == 5001) { static char p[MAX_PATH] = "seaview.ini"; if (fileDialog(h, true, p, MAX_PATH)) saveIni(p); }
        else if (code == BN_CLICKED && id == 5002) { static char p[MAX_PATH] = "seaview.ini"; if (fileDialog(h, false, p, MAX_PATH)) { loadIni(p); SendMessageA(g_texCombo, CB_SETCURSEL, g_texSel, 0); repaintSwatches(); } }
        else if (code == BN_CLICKED && id >= 5101 && id < 5101 + NCOL) pickColour(h, id - 5101);
        else if (code == BN_CLICKED && id >= 5201 && id < 5201 + NCOL) { const int f = kColFirst[id - 5201]; for (int j = 0; j < 3; j++) setVal(f + j, g_def[f + j]); repaintSwatches(); }
        else if (code == BN_CLICKED && id == 5003) { for (int i = 0; i < NPAR; i++) setVal(i, g_def[i]); repaintSwatches(); }
        else if (code == CBN_SELCHANGE && id == 5004) { g_texSel = (int)SendMessageA((HWND)l, CB_GETCURSEL, 0, 0); g_texDirty = true; }
        return 0;
    }
    if (m == WM_NOTIFY && ((NMHDR*)l)->code == TCN_SELCHANGE) { showTab(TabCtrl_GetCurSel(g_tab)); return 0; }
    if (m == WM_DRAWITEM) {   /* a colour swatch: the colour itself, framed */
        const DRAWITEMSTRUCT* di = (const DRAWITEMSTRUCT*)l;
        const int k = (int)di->CtlID - 5101;
        if (k >= 0 && k < NCOL) {
            const int f = kColFirst[k];
            auto c8 = [](float v) { return (int)(fminf(fmaxf(v, 0.f), 1.f) * 255.f + 0.5f); };
            HBRUSH br = CreateSolidBrush(RGB(c8(g_val[f]), c8(g_val[f + 1]), c8(g_val[f + 2])));
            FillRect(di->hDC, &di->rcItem, br); DeleteObject(br);
            FrameRect(di->hDC, &di->rcItem, (HBRUSH)GetStockObject(BLACK_BRUSH));
            return TRUE;
        }
    }
    if (m == WM_CLOSE) return 0;
    return DefWindowProcA(h, m, w, l);
}
/* the cloud textures to try (64x64, tiling): GE's, or periodic noises */
static float vnoiseP(float x, float y, int px, int py, int seed) {
    int ix = (int)floorf(x), iy = (int)floorf(y); float fx = x - ix, fy = y - iy;
    auto hsh = [&](int a, int b) { a = ((a % px) + px) % px; b = ((b % py) + py) % py; unsigned h = (unsigned)(a * 374761393 + b * 668265263 + seed * 2246822519u); h = (h ^ (h >> 13)) * 1274126177u; return (float)(h & 0xffff) / 65535.0f; };
    float u = fx * fx * (3 - 2 * fx), v = fy * fy * (3 - 2 * fy);
    float a = hsh(ix, iy), b = hsh(ix + 1, iy), c = hsh(ix, iy + 1), d = hsh(ix + 1, iy + 1);
    return a + (b - a) * u + (c - a) * v + (a - b - c + d) * u * v;
}
static void makeCloudTex(int sel, const unsigned char* ge, unsigned char* out) {
    for (int y = 0; y < 64; y++) for (int x = 0; x < 64; x++) {
        float v = 0;
        if (sel == 0) { v = ge[y * 64 + x] / 255.0f; }
        else {
            const float ax = sel == 4 ? 1.0f : 4.0f, ay = sel == 4 ? 8.0f : 4.0f;
            float amp = 0.5f, sum = 0, norm = 0;
            for (int o = 0; o < 5; o++) {
                const int fx = (int)(ax * (1 << o)), fy = (int)(ay * (1 << o));
                sum += amp * vnoiseP(x / 64.0f * fx, y / 64.0f * fy, fx, fy, o + sel * 17); norm += amp; amp *= 0.5f;
            }
            v = sum / norm;
            auto sstep = [](float a, float b, float t) { t = fminf(fmaxf((t - a) / (b - a), 0.f), 1.f); return t * t * (3 - 2 * t); };
            if (sel == 2) v = sstep(0.42f, 0.62f, v);
            else if (sel == 3) v = sstep(0.5f, 0.6f, v) * (0.6f + 0.4f * v);
            else if (sel == 5) v = sstep(0.6f, 0.78f, v);
        }
        out[y * 64 + x] = (unsigned char)(fminf(fmaxf(v, 0.f), 1.f) * 255.0f);
    }
}
/* Level presets (menu Level): the level's own water draw as the game would see it. Guesses until real captures exist (Dam: dark-lit
 * water, reflectLit 0; Caverns: no sky, mirrors the fog colour, reflectLit 0; Complex: lit pits, reflectLit 1). */
struct Preset { float h, tile, patch, reflectLit, fogEnd, noSky, shade[3], fog[3], tint[3], swell, chop, speed, eyeY; };
static const Preset kPreset[3] = {
    /* Dam      */ { 0, 256, 6, 0, 14000, 0, { 0.28f, 0.30f, 0.30f }, { 0.52f, 0.60f, 0.68f }, { 1.0f, 1.0f, 1.0f }, 0.3f, 0.6f, 0.7f, 260 },
    /* Caverns  */ { 0, 200, 6, 0, 3500, 1, { 0.55f, 0.60f, 0.60f }, { 0.11f, 0.14f, 0.15f }, { 0.5f, 0.6f, 0.65f }, 0.3f, 0.6f, 0.7f, 160 },
    /* Complex  */ { 0, 256, 6, 1, 6000, 0, { 0.45f, 0.50f, 0.52f }, { 0.22f, 0.26f, 0.28f }, { 1.0f, 1.0f, 1.0f }, 0.3f, 0.6f, 0.7f, 160 },
};
static void applyLevel(int lv) {
    if (lv < 0 || lv > 2) return;
    const Preset& p = kPreset[lv]; g_level = lv;
    CheckMenuRadioItem(g_levelMenu, 8100, 8102, 8100 + lv, MF_BYCOMMAND);
    setVal(parIdx("Water plane height"), p.h); setVal(parIdx("Water texture tile (units per repeat)"), p.tile); setVal(parIdx("Patch map size (repeats)"), p.patch);
    setVal(parIdx("reflectLit (0..1)"), p.reflectLit); setVal(parIdx("Fog end (0 = no fog)"), p.fogEnd); setVal(parIdx("Water only, no sky (cave) 0/1"), p.noSky);
    static const char* sfx[3] = { " R", " G", " B" };
    for (int i = 0; i < 3; i++) {
        setVal(parIdx((std::string("Shade") + sfx[i]).c_str()), p.shade[i]); setVal(parIdx((std::string("Fog colour") + sfx[i]).c_str()), p.fog[i]);
        setVal(parIdx((std::string("Water colour") + sfx[i]).c_str()), p.tint[i]);
    }
    setVal(parIdx("Swell"), p.swell); setVal(parIdx("Chop"), p.chop); setVal(parIdx("Wave speed"), p.speed); setVal(parIdx("Eye height"), p.eyeY);
    g_wtexPath.clear(); g_wtexDirty = true; repaintSwatches();
}
static LRESULT CALLBACK wp(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_COMMAND) {
        int id = LOWORD(w);
        if (id >= 8000 && id <= 8001) { g_mode = id - 8000; CheckMenuRadioItem(g_modeMenu, 8000, 8001, id, MF_BYCOMMAND); }
        if (id >= 8100 && id <= 8102) applyLevel(id - 8100);
        if (id == 8200) { static char p[MAX_PATH] = ""; if (fileDialog(h, false, p, MAX_PATH, "Images (*.png;*.tga)\0*.png;*.tga\0All\0*.*\0", "png")) { g_wtexPath = p; g_wtexDirty = true; } }
        if (id == 8201) g_wtexDirty = true;
        if (id == 8202) kdown[VK_F12] = true;
        if (id == 8203) { g_half ^= 1; CheckMenuItem(g_waterMenu, 8203, MF_BYCOMMAND | (g_half ? MF_CHECKED : MF_UNCHECKED)); }
        if (id == 8204) { g_scene ^= 1; CheckMenuItem(g_waterMenu, 8204, MF_BYCOMMAND | (g_scene ? MF_CHECKED : MF_UNCHECKED)); }
        if (id >= 9000 && id <= 9003) { g_dbg = id - 9000; CheckMenuRadioItem(g_dbgMenu, 9000, 9003, id, MF_BYCOMMAND); }
        if (id >= 1000 && id < 1100) { g_seaVar = id - 1000; CheckMenuRadioItem(g_seaMenu, 1000, 1099, id, MF_BYCOMMAND); }
        if (id >= 7000 && id < 7010) { g_scale = id - 7000; CheckMenuRadioItem(g_scaleMenu, 7001, 7004, id, MF_BYCOMMAND); }
        if (id >= 6000 && id < 6100) { g_rep = id - 6000; CheckMenuRadioItem(g_repMenu, 6001, 6064, id, MF_BYCOMMAND); }
        if (id >= 2000 && id < 2100) { g_skyVar = id - 2000; CheckMenuRadioItem(g_skyMenu, 2000, 2099, id, MF_BYCOMMAND); }
        return 0;
    }
    if (m == WM_LBUTTONDOWN && !g_mlook) { g_mlook = true; ShowCursor(FALSE); SetFocus(h); return 0; }
    if (m == WM_KEYDOWN) { keys[w & 255] = true; kdown[w & 255] = true; }
    else if (m == WM_KEYUP) keys[w & 255] = false;
    else if (m == WM_CLOSE) { PostQuitMessage(0); return 0; }
    return DefWindowProcA(h, m, w, l);
}

int main(int argc, char** argv) {
    int cliLevel = -1, cliMode = -1, cliHalf = -1, cliScene = -1, cliDbg = -1; std::string cliWtex, cliView;
    std::string shot, dump = "sea_dump.bin"; int W = 1280, H = 720, mode = 0, hdiv = 2;
    bool noRep = false; float shx = 0, shz = 0, bakeDistArg = 400.f; float yawd = 0, pitchd = 0, ftime = -1, aniso = 2, fdt = 0, taaA = 0.1f, taaK = 1.25f; int taa = 0, seqN = 0; std::string outp = "seq"; bool sway = false; float play[4] = { 0, 0, 0, 0 };
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i]; auto nx = [&]() { return i + 1 < argc ? argv[++i] : (char*)"0"; };
        if (a == "--shot") shot = nx(); else if (a == "--yaw") yawd = (float)atof(nx()); else if (a == "--pitch") pitchd = (float)atof(nx());
        else if (a == "--time") ftime = (float)atof(nx()); else if (a == "--mode") { std::string v = nx(); if (v == "water") cliMode = 1; else if (v == "sea") cliMode = 0; else mode = atoi(v.c_str()); }
        else if (a == "--level") { std::string v = nx(); cliLevel = v == "dam" ? 0 : v == "caverns" ? 1 : v == "complex" ? 2 : atoi(v.c_str()); }
        else if (a == "--cam") { g_haveCam = sscanf(nx(), "%f,%f,%f,%f,%f", &g_cam[0], &g_cam[1], &g_cam[2], &g_cam[3], &g_cam[4]) == 5; if (!g_haveCam) fprintf(stderr, "--cam wants x,y,z,yaw,pitch\n"); }
        else if (a == "--wtex") cliWtex = nx(); else if (a == "--seahalf") hdiv = atoi(nx());   /* the sea target's width divisor (was --half) */
        else if (a == "--half") cliHalf = atoi(nx()); else if (a == "--scene") cliScene = atoi(nx()); else if (a == "--view") cliView = nx();
        else if (a == "--dbgview") { std::string v = nx(); cliDbg = v == "final" ? 0 : v == "fx" ? 1 : v == "mask" ? 2 : v == "cheap" ? 3 : atoi(v.c_str()); }
        else if (a == "--sway") sway = atoi(nx()) != 0; else if (a == "--dump") dump = nx(); else if (a == "--shaders") gShDir = nx();
        else if (a == "--scale") g_scale = atoi(nx()); else if (a == "--norep") noRep = true; else if (a == "--shift") sscanf(nx(), "%f,%f", &shx, &shz); else if (a == "--bakedist") bakeDistArg = (float)atof(nx()); else if (a == "--taa") taa = atoi(nx()); else if (a == "--taaa") taaA = (float)atof(nx()); else if (a == "--taak") taaK = (float)atof(nx()); else if (a == "--seq") seqN = atoi(nx()); else if (a == "--dt") fdt = (float)atof(nx()); else if (a == "--out") outp = nx(); else if (a == "--aniso") aniso = (float)atof(nx()); else if (a == "--w") W = atoi(nx()); else if (a == "--h") H = atoi(nx());
        else if (a == "--play") sscanf(nx(), "%f,%f,%f,%f", &play[0], &play[1], &play[2], &play[3]);
    }
    FILE* df = fopen(dump.c_str(), "rb"); if (!df) { fprintf(stderr, "no %s\n", dump.c_str()); return 1; }
    float U[64]; unsigned char cloud[4096], wave[65536];
    if (fread(U, 4, 64, df) != 64 || fread(cloud, 1, 4096, df) != 4096 || fread(wave, 1, 65536, df) != 65536) { fprintf(stderr, "short dump\n"); return 1; }
    fclose(df);
    // the camera from the dumped corner rays
    V3 C[4]; for (int i = 0; i < 4; i++) C[i] = { U[(2 + i) * 4], U[(2 + i) * 4 + 1], U[(2 + i) * 4 + 2] };
    V3 cen = mul(add(add(C[0], C[1]), add(C[2], C[3])), 0.25f);
    V3 hw = mul(add(sub(C[1], C[0]), sub(C[3], C[2])), 0.25f), hh = mul(add(sub(C[0], C[2]), sub(C[1], C[3])), 0.25f);
    hh = mul(nrm(hh), len(hw) * (float)H / (float)W);
    V3 eye = { U[24], U[25], U[26] };
    /* the test scene's frame: origin = the capture's eye, a = the capture's heading, b = to its right (yaw 0) */
    const V3 sf = nrm({ cen.x, 0, cen.z }), sr = nrm({ hw.x, 0, hw.z }), so = eye;
    float yaw = yawd * 3.14159265f / 180, pitch = pitchd * 3.14159265f / 180;

    WNDCLASSA wc = {}; wc.lpfnWndProc = wp; wc.hInstance = GetModuleHandleA(0); wc.lpszClassName = "seaview"; wc.hCursor = LoadCursor(0, IDC_ARROW); RegisterClassA(&wc);
    RECT r = { 0, 0, W, H }; AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, TRUE);
    HWND hwnd = CreateWindowA("seaview", "seaview", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 40, 40, r.right - r.left, r.bottom - r.top, 0, 0, wc.hInstance, 0);
    {
        HMENU bar = CreateMenu(); g_seaMenu = CreatePopupMenu(); g_skyMenu = CreatePopupMenu();
        for (int i = 0; i < (int)(sizeof kSeaNames / sizeof *kSeaNames); i++) AppendMenuA(g_seaMenu, MF_STRING, 1000 + i, kSeaNames[i]);
        for (int i = 0; i < (int)(sizeof kSkyNames / sizeof *kSkyNames); i++) AppendMenuA(g_skyMenu, MF_STRING, 2000 + i, kSkyNames[i]);
        CheckMenuRadioItem(g_seaMenu, 1000, 1099, 1000, MF_BYCOMMAND); CheckMenuRadioItem(g_skyMenu, 2000, 2099, 2000, MF_BYCOMMAND);
        AppendMenuA(bar, MF_POPUP, (UINT_PTR)g_seaMenu, "Sea tests"); AppendMenuA(bar, MF_POPUP, (UINT_PTR)g_skyMenu, "Sky tests");
        g_scaleMenu = CreatePopupMenu();
        AppendMenuA(g_scaleMenu, MF_STRING, 7001, "1/1 (full)"); AppendMenuA(g_scaleMenu, MF_STRING, 7002, "1/2"); AppendMenuA(g_scaleMenu, MF_STRING, 7004, "1/4");
        CheckMenuRadioItem(g_scaleMenu, 7001, 7004, 7001, MF_BYCOMMAND);
        AppendMenuA(bar, MF_POPUP, (UINT_PTR)g_scaleMenu, "Scale");
        g_repMenu = CreatePopupMenu();
        AppendMenuA(g_repMenu, MF_STRING, 6001, "x1 (normal)"); AppendMenuA(g_repMenu, MF_STRING, 6004, "x4"); AppendMenuA(g_repMenu, MF_STRING, 6016, "x16"); AppendMenuA(g_repMenu, MF_STRING, 6064, "x64");
        CheckMenuRadioItem(g_repMenu, 6001, 6064, 6001, MF_BYCOMMAND);
        AppendMenuA(bar, MF_POPUP, (UINT_PTR)g_repMenu, "Load");
        g_modeMenu = CreatePopupMenu(); AppendMenuA(g_modeMenu, MF_STRING, 8000, "Sea (Frigate view)"); AppendMenuA(g_modeMenu, MF_STRING, 8001, "Water surface (level water)");
        CheckMenuRadioItem(g_modeMenu, 8000, 8001, 8000, MF_BYCOMMAND); AppendMenuA(bar, MF_POPUP, (UINT_PTR)g_modeMenu, "Mode");
        g_levelMenu = CreatePopupMenu(); AppendMenuA(g_levelMenu, MF_STRING, 8100, "Dam"); AppendMenuA(g_levelMenu, MF_STRING, 8101, "Caverns (no sky)"); AppendMenuA(g_levelMenu, MF_STRING, 8102, "Complex");
        CheckMenuRadioItem(g_levelMenu, 8100, 8102, 8100, MF_BYCOMMAND); AppendMenuA(bar, MF_POPUP, (UINT_PTR)g_levelMenu, "Level");
        g_waterMenu = CreatePopupMenu(); AppendMenuA(g_waterMenu, MF_STRING, 8200, "Water texture..."); AppendMenuA(g_waterMenu, MF_STRING, 8201, "Reload texture, re-bake, reset deep/light");
        AppendMenuA(g_waterMenu, MF_STRING, 8202, "Screenshot (F12)");
        AppendMenuA(g_waterMenu, MF_SEPARATOR, 0, 0); AppendMenuA(g_waterMenu, MF_STRING, 8203, "Half resolution (the game's 3 steps)"); AppendMenuA(g_waterMenu, MF_STRING, 8204, "Test scene (pier, boxes, pool, wall)");
        CheckMenuItem(g_waterMenu, 8204, MF_BYCOMMAND | MF_CHECKED);
        AppendMenuA(bar, MF_POPUP, (UINT_PTR)g_waterMenu, "Water");
        g_dbgMenu = CreatePopupMenu(); for (int i = 0; i < 4; i++) AppendMenuA(g_dbgMenu, MF_STRING, 9000 + i, kDbgNames[i]);
        CheckMenuRadioItem(g_dbgMenu, 9000, 9003, 9000, MF_BYCOMMAND); AppendMenuA(bar, MF_POPUP, (UINT_PTR)g_dbgMenu, "Debug view");
        SetMenu(hwnd, bar);
    }
    {
        for (int i = 0; i < NPAR; i++) {
            const Par& q = kPar[i];
            if (q.kind == 0 || q.kind == 3) g_val[i] = q.def;
            else if (q.kind == 1) g_val[i] = U[q.idx];
            else if (q.idx == 0) g_val[i] = asinf(fmaxf(-1.f, fminf(1.f, U[45]))) * 57.29578f;
            else if (q.idx == 1) g_val[i] = atan2f(U[44], U[46]) * 57.29578f;
            else g_val[i] = U[25];
        }
        for (int i = 0; i < NPAR; i++) g_def[i] = g_val[i];
        INITCOMMONCONTROLSEX ic = { sizeof ic, ICC_TAB_CLASSES }; InitCommonControlsEx(&ic);
        WNDCLASSA pc = {}; pc.lpfnWndProc = pp; pc.hInstance = wc.hInstance; pc.lpszClassName = "seaparams"; pc.hCursor = LoadCursor(0, IDC_ARROW);
        pc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); RegisterClassA(&pc);
        RECT mr; GetWindowRect(hwnd, &mr);
        const int PW = 430, PH = 630;
        HWND pw = CreateWindowExA(WS_EX_TOOLWINDOW, "seaparams", "Settings", WS_CAPTION | WS_VISIBLE | WS_POPUPWINDOW, mr.right + 8, mr.top, PW, PH, hwnd, 0, wc.hInstance, 0);
        g_tab = CreateWindowA(WC_TABCONTROLA, "", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 4, 4, PW - 16, 480, pw, (HMENU)5005, wc.hInstance, 0);
        for (int t = 0; t < 6; t++) { TCITEMA ti = {}; ti.mask = TCIF_TEXT; ti.pszText = (LPSTR)kTabs[t]; SendMessageA(g_tab, TCM_INSERTITEMA, t, (LPARAM)&ti); }
        int row[6] = { 0, 0, 0, 0, 0, 0 };
        for (int i = 0; i < NPAR; i++) {
            const int t = tabOf(i), ck = colourOf(i);
            if (ck >= 0 && i != kColFirst[ck]) {   /* G and B: hidden fields, the swatch's row is the R one */
                char b[64]; snprintf(b, sizeof b, "%g", g_val[i]);
                g_edit[i] = CreateWindowA("EDIT", b, WS_CHILD, 0, 0, 10, 10, pw, (HMENU)(INT_PTR)(3000 + i), wc.hInstance, 0);
                g_lbl[i] = CreateWindowA("STATIC", "", WS_CHILD, 0, 0, 1, 1, pw, 0, wc.hInstance, 0); g_rst[i] = CreateWindowA("STATIC", "", WS_CHILD, 0, 0, 1, 1, pw, 0, wc.hInstance, 0);
                continue;
            }
            const int y = 36 + row[t]++ * 26;
            if (ck >= 0) {
                g_colLbl[ck] = CreateWindowA("STATIC", kColName[ck], WS_CHILD, 14, y + 2, 230, 20, pw, 0, wc.hInstance, 0);
                g_colBtn[ck] = CreateWindowA("BUTTON", "", WS_CHILD | BS_OWNERDRAW, 248, y, 110, 22, pw, (HMENU)(INT_PTR)(5101 + ck), wc.hInstance, 0);
                g_colRst[ck] = CreateWindowA("BUTTON", "reset", WS_CHILD | BS_PUSHBUTTON, 362, y, 48, 22, pw, (HMENU)(INT_PTR)(5201 + ck), wc.hInstance, 0);
                for (HWND hw2 : { g_colLbl[ck], g_colBtn[ck], g_colRst[ck] }) SetWindowPos(hw2, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
                char b[64]; snprintf(b, sizeof b, "%g", g_val[i]);
                g_edit[i] = CreateWindowA("EDIT", b, WS_CHILD, 0, 0, 10, 10, pw, (HMENU)(INT_PTR)(3000 + i), wc.hInstance, 0);
                g_lbl[i] = CreateWindowA("STATIC", "", WS_CHILD, 0, 0, 1, 1, pw, 0, wc.hInstance, 0); g_rst[i] = CreateWindowA("STATIC", "", WS_CHILD, 0, 0, 1, 1, pw, 0, wc.hInstance, 0);
                continue;
            }
            g_lbl[i] = CreateWindowA("STATIC", kPar[i].name, WS_CHILD, 14, y + 2, 230, 20, pw, 0, wc.hInstance, 0);
            char b[64]; snprintf(b, sizeof b, "%g", g_val[i]);
            g_edit[i] = CreateWindowA("EDIT", b, WS_CHILD | WS_BORDER | ES_AUTOHSCROLL, 248, y, 110, 22, pw, (HMENU)(INT_PTR)(3000 + i), wc.hInstance, 0);
            g_rst[i] = CreateWindowA("BUTTON", "reset", WS_CHILD | BS_PUSHBUTTON, 362, y, 48, 22, pw, (HMENU)(INT_PTR)(4000 + i), wc.hInstance, 0);
            SetWindowPos(g_lbl[i], HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE); SetWindowPos(g_edit[i], HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE); SetWindowPos(g_rst[i], HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        }
        CreateWindowA("STATIC", "Cloud texture", WS_CHILD | WS_VISIBLE, 14, 496, 120, 20, pw, 0, wc.hInstance, 0);
        g_texCombo = CreateWindowA("COMBOBOX", "", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, 140, 492, 270, 200, pw, (HMENU)5004, wc.hInstance, 0);
        for (int i = 0; i < (int)(sizeof kTex / sizeof *kTex); i++) SendMessageA(g_texCombo, CB_ADDSTRING, 0, (LPARAM)kTex[i]);
        SendMessageA(g_texCombo, CB_SETCURSEL, 0, 0);
        CreateWindowA("BUTTON", "Save", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 14, 530, 120, 28, pw, (HMENU)5001, wc.hInstance, 0);
        CreateWindowA("BUTTON", "Load", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 144, 530, 120, 28, pw, (HMENU)5002, wc.hInstance, 0);
        CreateWindowA("BUTTON", "Reset all", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 274, 530, 136, 28, pw, (HMENU)5003, wc.hInstance, 0);
        CreateWindowA("STATIC", "Click in the view: mouse look (Escape releases). WASD/ZQSD, Shift = fast, Space/E up, Ctrl down.", WS_CHILD | WS_VISIBLE, 14, 566, 400, 34, pw, 0, wc.hInstance, 0);
        showTab(0);
        loadIni("seaview.ini");
        /* the command line wins over the .ini */
        if (cliLevel >= 0) applyLevel(cliLevel);
        if (cliMode >= 0) { g_mode = cliMode; CheckMenuRadioItem(g_modeMenu, 8000, 8001, 8000 + g_mode, MF_BYCOMMAND); }
        if (!cliWtex.empty()) { g_wtexPath = cliWtex; g_wtexDirty = true; }
        if (cliHalf >= 0) { g_half = cliHalf; CheckMenuItem(g_waterMenu, 8203, MF_BYCOMMAND | (g_half ? MF_CHECKED : MF_UNCHECKED)); }
        if (cliScene >= 0) { g_scene = cliScene; CheckMenuItem(g_waterMenu, 8204, MF_BYCOMMAND | (g_scene ? MF_CHECKED : MF_UNCHECKED)); }
        if (cliDbg >= 0) { g_dbg = cliDbg; CheckMenuRadioItem(g_dbgMenu, 9000, 9003, 9000 + g_dbg, MF_BYCOMMAND); }
        if (!cliView.empty()) {   /* a camera in the test scene's frame: eye (a, b, height above the plane) looking at a target (a, b, height) */
            const float h0 = g_val[WATER0];
            struct { const char* n; float e[3], t[3]; } vw[] = {
                { "overview", { -100, 0, 260 }, { 900, -100, 0 } }, { "pier", { 150, 430, 90 }, { 700, 180, 10 } }, { "boxes", { 150, -100, 120 }, { 700, -200, 20 } },
                { "shore", { 1500, 2000, 150 }, { 2500, 2000, 0 } }, { "pool", { 600, -1500, 250 }, { 1700, -1500, 80 } }, { "far", { -100, 0, 40 }, { 4000, 100, 0 } } };
            for (auto& v : vw) if (cliView == v.n) {
                const V3 E = { so.x + sf.x * v.e[0] + sr.x * v.e[1], h0 + v.e[2], so.z + sf.z * v.e[0] + sr.z * v.e[1] };
                const V3 T = { so.x + sf.x * v.t[0] + sr.x * v.t[1], h0 + v.t[2], so.z + sf.z * v.t[0] + sr.z * v.t[1] };
                const V3 d = sub(T, E); const float hd = sqrtf(d.x * d.x + d.z * d.z); const V3 t = { d.x / hd, 0, d.z / hd };
                eye.x = E.x; eye.z = E.z; setVal(parIdx("Eye height"), E.y);
                yaw = atan2f(t.x * sf.z - t.z * sf.x, t.x * sf.x + t.z * sf.z); pitch = atan2f(d.y, hd); g_haveCam = false;
                fprintf(stderr, "view %s: eye %.1f %.1f %.1f\n", v.n, E.x, E.y, E.z);
            }
        }
        if (g_haveCam) { eye.x = g_cam[0]; eye.z = g_cam[2]; setVal(parIdx("Eye height"), g_cam[1]); yaw += g_cam[3] * 3.14159265f / 180; pitch += g_cam[4] * 3.14159265f / 180; }
    }
    HDC dc = GetDC(hwnd);
    PIXELFORMATDESCRIPTOR pf = {}; pf.nSize = sizeof pf; pf.nVersion = 1; pf.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER; pf.iPixelType = PFD_TYPE_RGBA; pf.cColorBits = 32;
    SetPixelFormat(dc, ChoosePixelFormat(dc, &pf), &pf);
    HGLRC tmp = wglCreateContext(dc); wglMakeCurrent(dc, tmp);
    auto cca = (PFNWGLCREATECONTEXTATTRIBSARB)wglGetProcAddress("wglCreateContextAttribsARB");
    int at[] = { 0x2091, 4, 0x2092, 6, 0 };
    HGLRC rc = cca(dc, 0, at); if (!rc) { fprintf(stderr, "no GL 4.6\n"); return 1; }
    wglMakeCurrent(0, 0); wglDeleteContext(tmp); wglMakeCurrent(dc, rc);
#define X(T,N) N = (T)wglGetProcAddress(#N);
    GLFUNCS
#undef X
    auto swapi = (PFNWGLSWAPINTERVAL)wglGetProcAddress("wglSwapIntervalEXT"); if (swapi) swapi(shot.empty() ? 1 : 0);
    fprintf(stderr, "GL %s | %s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));
    snprintf(g_gpuName, sizeof g_gpuName, "%s", (const char*)glGetString(GL_RENDERER));

    std::vector<unsigned char> c4(64 * 64 * 4);
    for (int i = 0; i < 4096; i++) c4[i * 4] = c4[i * 4 + 1] = c4[i * 4 + 2] = c4[i * 4 + 3] = cloud[i];
    GLuint tCloud = tex2d(64, 64, c4.data(), true, true, 1), tWave = tex2d(128, 128, wave, true, true, aniso);
    unsigned char z4[4] = { 0, 0, 0, 255 }; GLuint tDummy = tex2d(1, 1, z4, false, true, 1);
    GLuint tLut = tex2d(128, 64, 0, false, false, 1), fLut; glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glGenFramebuffers(1, &fLut); glBindFramebuffer(GL_FRAMEBUFFER, fLut); glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tLut, 0);
    GLuint tHalf = 0, fHalf = 0; int hwid = 0, hhei = 0;
    GLuint tSkyR = 0, fSkyR = 0; int skyrW = 0, skyrH = 0;   /* the reduced final pass (Scale) */
    GLuint tPano = tex2d(4096, 512, 0, false, false, 1), fPano; glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glGenFramebuffers(1, &fPano); glBindFramebuffer(GL_FRAMEBUFFER, fPano); glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tPano, 0);
    bool baked = false; bool shifted = false; V3 beye = {0, 0, 0}; float bplay[4] = {0, 0, 0, 0}; int bmode = 0, nbake = 0, bsea = 0; float bpar[NPAR] = {0}; float bakeDist = bakeDistArg;
    GLuint ubo; glGenBuffers(1, &ubo); glBindBuffer(GL_UNIFORM_BUFFER, ubo); glBufferData(GL_UNIFORM_BUFFER, 256, U, GL_DYNAMIC_DRAW);
    GLuint vubo; glGenBuffers(1, &vubo); glBindBuffer(GL_UNIFORM_BUFFER, vubo); glBufferData(GL_UNIFORM_BUFFER, 112, 0, GL_DYNAMIC_DRAW);   /* block Viewer */
    GLuint vao; glGenVertexArrays(1, &vao); glBindVertexArray(vao);
    GLuint tScene = tex2d(W, H, 0, false, false, 1), fScene; glGenFramebuffers(1, &fScene); glBindFramebuffer(GL_FRAMEBUFFER, fScene); glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tScene, 0);
    GLuint tHist[2], fHist[2]; for (int i = 0; i < 2; i++) { tHist[i] = tex2d(W, H, 0, false, false, 1); glGenFramebuffers(1, &fHist[i]); glBindFramebuffer(GL_FRAMEBUFFER, fHist[i]); glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tHist[i], 0); }
    int hcur = 0; bool histOk = false; V3 pcc[4] = {}, peye = {0,0,0};
    GLuint tHH[2] = {0,0}, fHH[2] = {0,0}; GLuint taaUbo; glGenBuffers(1, &taaUbo); glBindBuffer(GL_UNIFORM_BUFFER, taaUbo); glBufferData(GL_UNIFORM_BUFFER, 96, 0, GL_DYNAMIC_DRAW);
    GLuint pRes = buildSrc("fullscreen.vert.glsl", kResolve); if (!pRes) return 3;
    GLuint q[2]; glGenQueries(2, q); bool qv[2] = { false, false }; double gpums = 0;
    GLuint pSky = build("fullscreen.vert.glsl", "skyplus.frag.glsl"), pLut = build("fullscreen.vert.glsl", "skylut.frag.glsl");
    if (!pSky || !pLut) return 2;
    /* ---- the water surface mode: program, grid, uniform blocks (Draw 0, Comb 1, Sky 2 + the 4 vec4s of the level water), textures ---- */
    GLuint pWater = buildWater(0), pWBody = buildWater(1), pComp = buildComp(false), pMask = buildComp(true); if (!pWater || !pWBody || !pComp || !pMask) fprintf(stderr, "water program failed: Water surface mode disabled\n");
    const int GN = 256;   /* cells per side of the plane's grid */
    GLuint vaoW, vboW, eboW; glGenVertexArrays(1, &vaoW); glBindVertexArray(vaoW);
    {
        std::vector<float> gv; for (int j = 0; j <= GN; j++) for (int i = 0; i <= GN; i++) { gv.push_back(2.0f * i / GN - 1.0f); gv.push_back(2.0f * j / GN - 1.0f); }
        std::vector<unsigned> gi; for (int j = 0; j < GN; j++) for (int i = 0; i < GN; i++) { unsigned a = j * (GN + 1) + i, b = a + 1, c = a + GN + 1, d = c + 1; unsigned t[6] = { a, c, b, b, c, d }; gi.insert(gi.end(), t, t + 6); }
        glGenBuffers(1, &vboW); glBindBuffer(GL_ARRAY_BUFFER, vboW); glBufferData(GL_ARRAY_BUFFER, gv.size() * 4, gv.data(), GL_STATIC_DRAW);
        glGenBuffers(1, &eboW); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, eboW); glBufferData(GL_ELEMENT_ARRAY_BUFFER, gi.size() * 4, gi.data(), GL_STATIC_DRAW);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 8, 0); glEnableVertexAttribArray(0);
    }
    glBindVertexArray(vao);
    GLuint uboW, dDraw, dComb; glGenBuffers(1, &uboW); glBindBuffer(GL_UNIFORM_BUFFER, uboW); glBufferData(GL_UNIFORM_BUFFER, 320, 0, GL_DYNAMIC_DRAW);
    glGenBuffers(1, &dDraw); glBindBuffer(GL_UNIFORM_BUFFER, dDraw); glBufferData(GL_UNIFORM_BUFFER, 32, 0, GL_DYNAMIC_DRAW);
    glGenBuffers(1, &dComb); glBindBuffer(GL_UNIFORM_BUFFER, dComb); glBufferData(GL_UNIFORM_BUFFER, 32, 0, GL_DYNAMIC_DRAW);
    GLuint tWSrc = 0, tWMap = 0;
    /* the water pipeline's targets (as in gfx_deko3d.cpp wfx_prepare): the scene gets a depth texture; the FX target is half size
     * (each axis), RGBA8, linear + clamp, cleared to 0 and with its own depth */
    GLuint tDepth, tHalfFx, fHalfFx, rbHalf, uboC;
    glGenTextures(1, &tDepth); glBindTexture(GL_TEXTURE_2D, tDepth); glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, W, H, 0, GL_DEPTH_COMPONENT, GL_FLOAT, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, fScene); glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, tDepth, 0);
    tHalfFx = tex2d(W / 2, H / 2, 0, false, false, 1);   /* linear, clamp */
    glGenFramebuffers(1, &fHalfFx); glBindFramebuffer(GL_FRAMEBUFFER, fHalfFx); glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tHalfFx, 0);
    glGenRenderbuffers(1, &rbHalf); glBindRenderbuffer(GL_RENDERBUFFER, rbHalf); glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT32F, W / 2, H / 2); glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rbHalf);
    glGenBuffers(1, &uboC); glBindBuffer(GL_UNIFORM_BUFFER, uboC); glBufferData(GL_UNIFORM_BUFFER, 32, 0, GL_DYNAMIC_DRAW);   /* block Comp of watercomp.frag.glsl */
    GLuint pScene, pHalfDbg;
    {   /* the test scene's and the debug view's programs */
        GLuint a = compile(GL_VERTEX_SHADER, kSceneVert, "scene vert"), b = compile(GL_FRAGMENT_SHADER, kSceneFrag, "scene frag"); pScene = glCreateProgram(); glAttachShader(pScene, a); glAttachShader(pScene, b); glLinkProgram(pScene);
        pHalfDbg = buildSrc("fullscreen.vert.glsl", kHalfDbg);
    }
    GLuint vaoS, vboS; glGenVertexArrays(1, &vaoS); glBindVertexArray(vaoS); glGenBuffers(1, &vboS); glBindBuffer(GL_ARRAY_BUFFER, vboS);
    for (int i = 0; i < 3; i++) { glVertexAttribPointer(i, 3, GL_FLOAT, GL_FALSE, 36, (const void*)(size_t)(i * 12)); glEnableVertexAttribArray(i); }
    glBindVertexArray(vao);
    /* the level's water texture: from the file (--wtex, menu Water, .ini __wtex, else data/water_<level>.png|.tga), else the generated
     * placeholder; the bake (common/waterbake.h) gives the patch map and the deep / light colours (which land in the pickers) */
    auto loadWater = [&]() {
        img::Image im; std::string used;
        if (!g_wtexPath.empty()) { if (img::loadImage(g_wtexPath, im)) used = g_wtexPath; else fprintf(stderr, "water texture %s: not readable\n", g_wtexPath.c_str()); }
        else for (const char* ext : { ".png", ".tga" }) { std::string f = std::string("water_") + kLevelNames[g_level] + ext; if (img::loadImage(f, im)) { used = f; break; } }
        if (used.empty()) { im.w = im.h = 64; waterbake::placeholder(g_level + 1, im.px); used = "(generated placeholder)"; }
        /* the bake takes up to 128 wide: box-reduce a bigger texture by an integer factor */
        std::vector<unsigned char> bsrc = im.px; int bw = im.w, bh = im.h;
        while (bw > 128 || bh > 128) { const int nw = bw / 2, nh = bh / 2; std::vector<unsigned char> t((size_t)nw * nh * 4);
            for (int y = 0; y < nh; y++) for (int x = 0; x < nw; x++) for (int c = 0; c < 4; c++) t[((size_t)y * nw + x) * 4 + c] = (bsrc[((size_t)2 * y * bw + 2 * x) * 4 + c] + bsrc[((size_t)2 * y * bw + 2 * x + 1) * 4 + c] + bsrc[((size_t)(2 * y + 1) * bw + 2 * x) * 4 + c] + bsrc[((size_t)(2 * y + 1) * bw + 2 * x + 1) * 4 + c]) / 4;
            bsrc.swap(t); bw = nw; bh = nh; }
        unsigned char map[waterbake::MW * waterbake::MH * 4]; float deep[3], light[3];
        if (!waterbake::bake(bsrc.data(), bw, bh, map, deep, light)) { fprintf(stderr, "water bake: texture %dx%d not usable\n", bw, bh); return; }
        if (tWSrc) glDeleteTextures(1, &tWSrc);
        if (tWMap) glDeleteTextures(1, &tWMap);
        tWSrc = tex2d(im.w, im.h, im.px.data(), true, true, 8); tWMap = tex2d(waterbake::MW, waterbake::MH, map, true, true, 1);
        static const char* sfx[3] = { " R", " G", " B" };
        for (int i = 0; i < 3; i++) { setVal(parIdx((std::string("Deep colour") + sfx[i]).c_str()), deep[i]); setVal(parIdx((std::string("Light colour") + sfx[i]).c_str()), light[i]); }
        repaintSwatches();
        fprintf(stderr, "water texture: %s %dx%d; deep %.2f,%.2f,%.2f light %.2f,%.2f,%.2f\n", used.c_str(), im.w, im.h, deep[0], deep[1], deep[2], light[0], light[1], light[2]);
    };

    double t0 = (double)GetTickCount64() / 1000.0, tPrev = t0, tFrozen = 0; bool frozen = ftime >= 0; if (frozen) tFrozen = ftime;
    int sel = 0; POINT lastm; GetCursorPos(&lastm); int frame = 0; double fpsT = t0; int fpsN = 0, fpsShown = 0;
    MSG msg; bool run = true;
    while (run) {
        while (PeekMessageA(&msg, 0, 0, 0, PM_REMOVE)) { if (msg.message == WM_QUIT) run = false; TranslateMessage(&msg); DispatchMessageA(&msg); }
        if (!shifted && (nbake >= 1 || bakeDist <= 0.f)) { eye.x += shx; eye.z += shz; shifted = true; }
        double now = (double)GetTickCount64() / 1000.0, dt = now - tPrev; tPrev = now;
        double T = fdt > 0 ? (ftime < 0 ? 0 : ftime) + frame * fdt : (frozen ? tFrozen : (now - t0));
        if (kdown[VK_ESCAPE] && g_mlook) { g_mlook = false; ShowCursor(TRUE); }
        if (kdown['B']) sway = !sway;
        if (kdown['T']) { frozen = !frozen; tFrozen = T; }
        if (kdown['H']) hdiv = hdiv == 1 ? 2 : (hdiv == 2 ? 4 : 1);
        for (int d = 0; d < 10; d++) if (kdown['0' + d]) mode = d;
        if (kdown['X']) sel = (sel + 1) & 3;
        if (kdown['R']) { GLuint a = build("fullscreen.vert.glsl", "skyplus.frag.glsl"), b = build("fullscreen.vert.glsl", "skylut.frag.glsl"), c = buildWater(0); if (a && b) { pSky = a; pLut = b; fprintf(stderr, "reloaded\n"); } if (c) { pWater = c; GLuint c1 = buildWater(1), c2 = buildComp(false), c3 = buildComp(true); if (c1) pWBody = c1; if (c2) pComp = c2; if (c3) pMask = c3; } }
        if (g_wtexDirty) { loadWater(); g_wtexDirty = false; }
        float step = (float)dt * (keys[VK_SHIFT] ? 5.f : 1.f);
        if (keys[VK_OEM_COMMA]) play[sel] -= step;
        if (keys[VK_OEM_PERIOD]) play[sel] += step;
        /* turning right goes toward the camera's right, whatever the handedness of the game's axes */
        const float ysign = [&]() { V3 f0 = nrm({ cen.x, 0, cen.z }), r0 = nrm({ hw.x, 0, hw.z }), t = cross({ 0, 1, 0 }, f0); return t.x * r0.x + t.z * r0.z > 0 ? 1.0f : -1.0f; }();
        bool rmb = (GetKeyState(VK_RBUTTON) & 0x8000) != 0; POINT m; GetCursorPos(&m);
        if (g_mlook && GetForegroundWindow() == hwnd) {
            RECT cr; GetClientRect(hwnd, &cr); POINT c = { (cr.right - cr.left) / 2, (cr.bottom - cr.top) / 2 }; ClientToScreen(hwnd, &c);
            yaw += ysign * (m.x - c.x) * 0.0025f; pitch -= (m.y - c.y) * 0.0025f; SetCursorPos(c.x, c.y); m = c;
        } else if (rmb) { yaw += ysign * (m.x - lastm.x) * 0.003f; pitch -= (m.y - lastm.y) * 0.003f; }
        if (pitch > 1.5f) pitch = 1.5f; if (pitch < -1.5f) pitch = -1.5f;
        lastm = m;
        if (keys[VK_LEFT]) yaw -= ysign * (float)dt; if (keys[VK_RIGHT]) yaw += ysign * (float)dt;
        if (keys[VK_UP]) pitch += (float)dt * 0.5f; if (keys[VK_DOWN]) pitch -= (float)dt * 0.5f;
        float sp = (float)dt * 60.f * (keys[VK_SHIFT] ? 10.f : 1.f);
        V3 fw0 = nrm({ cen.x, 0, cen.z });
        /* the camera's own frame: forward and right turn with the same rotation as the view (rotAxis about +y) */
        const V3 rt0 = nrm({ hw.x, 0, hw.z });
        V3 fw = rotAxis(fw0, { 0, 1, 0 }, yaw);
        V3 rt = rotAxis(rt0, { 0, 1, 0 }, yaw);
        if (keys['W'] || keys['Z']) eye = add(eye, mul(fw, sp));
        if (keys['S']) eye = sub(eye, mul(fw, sp));
        if (keys['D']) eye = add(eye, mul(rt, sp));
        if (keys['A'] || keys['Q']) eye = sub(eye, mul(rt, sp));
        if (keys[VK_CONTROL]) eye.y -= sp * 0.5f;
        if (keys['E'] || keys[VK_SPACE]) eye.y += sp * 0.5f;
        // view: base basis, pitch about its right axis, yaw about world Y, then breathing sway (~0.15 deg tilt over 2 s + tiny roll)
        float swp = sway ? 0.0026f * sinf((float)T * 3.14159f) : 0.f;
        float swr = sway ? 0.0012f * sinf((float)T * 2.1f) : 0.f;
        V3 up0 = { 0, 1, 0 }, rax = nrm(hw);
        V3 cc = rotAxis(cen, rax, pitch + swp), hx = hw, hy = rotAxis(hh, rax, pitch + swp);
        cc = rotAxis(cc, up0, yaw); hx = rotAxis(hx, up0, yaw); hy = rotAxis(hy, up0, yaw);
        if (swr != 0) { V3 ax = nrm(cc); hx = rotAxis(hx, ax, swr); hy = rotAxis(hy, ax, swr); }
        V3 ccU = cc, hxU = hx, hyU = hy;
        V3 cor[4] = { add(sub(cc, hx), hy), add(add(cc, hx), hy), sub(sub(cc, hx), hy), sub(add(cc, hx), hy) };
        float u[64]; memcpy(u, U, 256);
        u[0] = 0; u[1] = 0; u[2] = (float)W; u[3] = (float)H; u[4] = 1; u[5] = 0; u[6] = 0;
        for (int i = 0; i < 4; i++) { u[(2 + i) * 4] = cor[i].x; u[(2 + i) * 4 + 1] = cor[i].y; u[(2 + i) * 4 + 2] = cor[i].z; u[(2 + i) * 4 + 3] = 0; }
        u[24] = eye.x; u[25] = eye.y; u[26] = eye.z;
        u[60] = (float)T; u[63] = (float)mode;
        float upr[24] = {0};
        {
            static float lastEye = -1e30f;
            float el = 0, az = 0;
            for (int i = 0; i < NPAR; i++) {
                const Par& q = kPar[i];
                if (q.kind == 0) upr[q.idx] = g_val[i];
                else if (q.kind == 1) u[q.idx] = g_val[i];
                else if (q.kind == 3) continue;   /* the water surface block: read where the plane is drawn */
                else if (q.idx == 0) el = g_val[i] / 57.29578f;
                else if (q.idx == 1) az = g_val[i] / 57.29578f;
                else if (g_val[i] != lastEye) { lastEye = g_val[i]; eye.y = g_val[i]; u[25] = eye.y; }
            }
            u[44] = cosf(el) * sinf(az); u[45] = sinf(el); u[46] = cosf(el) * cosf(az);
            if (g_mode == 1) u[61] = 0;   /* the water surface mode: no Frigate sea, the sky only */
        }
        {   /* block Viewer (binding 1): the menus' variants and the shaders' editable values (PRM) */
            struct { int var[4]; float pr[24]; } vb = { { g_seaVar, g_skyVar, 0, 0 }, {} };
            memcpy(vb.pr, upr, sizeof vb.pr);
            glBindBuffer(GL_UNIFORM_BUFFER, vubo); glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof vb, &vb); glBindBufferBase(GL_UNIFORM_BUFFER, 1, vubo);
        }
        glBindBuffer(GL_UNIFORM_BUFFER, ubo); glBufferSubData(GL_UNIFORM_BUFFER, 0, 256, u); glBindBufferBase(GL_UNIFORM_BUFFER, 0, ubo);
        auto bindTex = [&](int unit, GLuint t) { glActiveTexture(GL_TEXTURE0 + unit); glBindTexture(GL_TEXTURE_2D, t); };
        if (g_texDirty) { unsigned char t64[4096]; makeCloudTex(g_texSel, cloud, t64); for (int i = 0; i < 4096; i++) c4[i * 4] = c4[i * 4 + 1] = c4[i * 4 + 2] = c4[i * 4 + 3] = t64[i];
            glBindTexture(GL_TEXTURE_2D, tCloud); glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, c4.data()); glGenerateMipmap(GL_TEXTURE_2D); g_texDirty = false; }
        bindTex(0, tCloud); bindTex(1, tWave); bindTex(3, tDummy); bindTex(4, tDummy);
        int qi = frame & 1; glBeginQuery(GL_TIME_ELAPSED, q[qi]);
        glBindFramebuffer(GL_FRAMEBUFFER, fLut); glViewport(0, 0, 128, 64);
        glUseProgram(pLut); glUniform4f(glGetUniformLocation(pLut, "uPlay"), play[0], play[1], play[2], play[3]); glUniform1i(glGetUniformLocation(pLut, "uSeaVar"), g_seaVar); glUniform1i(glGetUniformLocation(pLut, "uSkyVar"), g_skyVar); glUniform1fv(glGetUniformLocation(pLut, "uPr"), 24, upr); glDrawArrays(GL_TRIANGLES, 0, 3);
        bindTex(2, tLut);
        // the far sea, baked into the panorama (pass 4) when the view of the sea changes
        if (kdown['K'] || !baked || play[0] != bplay[0] || play[1] != bplay[1] || play[2] != bplay[2] || play[3] != bplay[3] || mode != bmode || g_seaVar != bsea || memcmp(bpar, g_val, sizeof g_val) != 0) {
            glBindFramebuffer(GL_FRAMEBUFFER, fPano); glViewport(0, 0, 4096, 512);
            u[11] = 4; glBufferSubData(GL_UNIFORM_BUFFER, 0, 256, u);
            glUseProgram(pSky); glUniform4f(glGetUniformLocation(pSky, "uPlay"), play[0], play[1], play[2], play[3]); glUniform1i(glGetUniformLocation(pSky, "uSeaVar"), g_seaVar); glUniform1i(glGetUniformLocation(pSky, "uSkyVar"), g_skyVar); glUniform1fv(glGetUniformLocation(pSky, "uPr"), 24, upr); glDrawArrays(GL_TRIANGLES, 0, 3);
            baked = true; beye = eye; for (int i = 0; i < 4; i++) bplay[i] = play[i]; bmode = mode; bsea = g_seaVar; memcpy(bpar, g_val, sizeof g_val); nbake++;
        }
        u[15] = beye.x; u[19] = beye.z; u[23] = noRep ? 0.f : beye.y - u[59];   // where the panorama was baked from (uBake*)
        bindTex(4, tPano);
        bool half = hdiv >= 2 && W / hdiv >= 16;
        /* the render scale (menu Scale): the sea target and the final pass are drawn at 1/g_scale of the screen on each axis */
        const int SW = W / g_scale, SH = H / g_scale;
        if (half) {
            int ww = SW / hdiv;
            if (ww != hwid || SH != hhei) {
                hwid = ww; hhei = SH; if (tHalf) glDeleteTextures(1, &tHalf);
                tHalf = tex2d(ww, SH, 0, false, false, 1);
                if (!fHalf) glGenFramebuffers(1, &fHalf);
                glBindFramebuffer(GL_FRAMEBUFFER, fHalf); glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tHalf, 0);
            }
            glBindFramebuffer(GL_FRAMEBUFFER, fHalf); glViewport(0, 0, ww, SH);
            u[11] = 1; glBufferSubData(GL_UNIFORM_BUFFER, 0, 256, u);
            glUseProgram(pSky); glUniform4f(glGetUniformLocation(pSky, "uPlay"), play[0], play[1], play[2], play[3]); glUniform1i(glGetUniformLocation(pSky, "uSeaVar"), g_seaVar); glUniform1i(glGetUniformLocation(pSky, "uSkyVar"), g_skyVar); glUniform1fv(glGetUniformLocation(pSky, "uPr"), 24, upr); for (int rr = 0; rr < g_rep; rr++) glDrawArrays(GL_TRIANGLES, 0, 3);
            u[11] = 2; glBufferSubData(GL_UNIFORM_BUFFER, 0, 256, u);
            bindTex(3, tHalf);
        } else { u[11] = 0; glBufferSubData(GL_UNIFORM_BUFFER, 0, 256, u); }
        if (g_scale > 1) {   /* the final pass into a reduced target, enlarged (bilinear) onto the screen */
            if (SW != skyrW || SH != skyrH) {
                skyrW = SW; skyrH = SH; if (tSkyR) glDeleteTextures(1, &tSkyR);
                tSkyR = tex2d(SW, SH, 0, false, false, 1);
                if (!fSkyR) glGenFramebuffers(1, &fSkyR);
                glBindFramebuffer(GL_FRAMEBUFFER, fSkyR); glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tSkyR, 0);
            }
            glBindFramebuffer(GL_FRAMEBUFFER, fSkyR); glViewport(0, 0, SW, SH);
        } else {
            glBindFramebuffer(GL_FRAMEBUFFER, fScene); glViewport(0, 0, W, H);
        }
        glUseProgram(pSky); glUniform4f(glGetUniformLocation(pSky, "uPlay"), play[0], play[1], play[2], play[3]); glUniform1i(glGetUniformLocation(pSky, "uSeaVar"), g_seaVar); glUniform1i(glGetUniformLocation(pSky, "uSkyVar"), g_skyVar); glUniform1fv(glGetUniformLocation(pSky, "uPr"), 24, upr); for (int rr = 0; rr < g_rep; rr++) glDrawArrays(GL_TRIANGLES, 0, 3);
        if (g_scale > 1) {
            glBindFramebuffer(GL_READ_FRAMEBUFFER, fSkyR); glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fScene);
            glBlitFramebuffer(0, 0, SW, SH, 0, 0, W, H, GL_COLOR_BUFFER_BIT, GL_LINEAR);
        }
        GLuint fShow = fScene;   /* what is shown and saved */
        if (g_mode == 1 && pWater && tWSrc) {   /* the level's water over the sky, drawn with water.frag.glsl */
            auto W_ = [&](int k) { return g_val[WATER0 + k]; };   /* the kind-3 block of kPar, in order */
            const bool noSky = W_(7) > 0.5f, half = g_half && g_dbg != 3 && pComp && pMask && pWBody;
            glBindFramebuffer(GL_FRAMEBUFFER, fScene); glViewport(0, 0, W, H);
            if (noSky) { glClearColor(W_(17), W_(18), W_(19), 1); glClear(GL_COLOR_BUFFER_BIT); }   /* a cave: no sky, the fog colour is the horizon */
            glDepthMask(GL_TRUE); glClear(GL_DEPTH_BUFFER_BIT); glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS);
            float uw[80] = {}; memcpy(uw, u, 256);
            if (noSky) { uw[28] = W_(17); uw[29] = W_(18); uw[30] = W_(19); }   /* waterShade mirrors uSky (the horizon colour) when there is no sky */
            uw[62] = 1;   /* uWave4.z: GE+ water on */
            /* the level-water members of block Sky (vec4 16..19) */
            uw[64] = W_(8); uw[65] = W_(9); uw[66] = W_(10); uw[67] = W_(2);          /* uWX0: deep colour, patch size in repeats */
            uw[68] = W_(11); uw[69] = W_(12); uw[70] = W_(13); uw[71] = W_(3);        /* uWX1: light colour, reflectLit */
            uw[72] = 1; uw[73] = noSky ? 1.f : 0.f;                                   /* uWX2: map ready, water only */
            uw[76] = W_(20); uw[77] = W_(21); uw[78] = 1.0f;   /* uWX3: toneKeep, reflectGain, 1 / scale (the viewer's plane is in world units) */
            glBindBuffer(GL_UNIFORM_BUFFER, uboW); glBufferSubData(GL_UNIFORM_BUFFER, 0, 320, uw);
            const int dr[8] = { 0, 0, 0, 0, 0, 0, 0, 0 }; glBindBuffer(GL_UNIFORM_BUFFER, dDraw); glBufferSubData(GL_UNIFORM_BUFFER, 0, 32, dr);
            const unsigned cb[8] = { 0, 0, 0, 0, W_(6) > 0 ? 2u : 0u, 0, 0, 0 }; glBindBuffer(GL_UNIFORM_BUFFER, dComb); glBufferSubData(GL_UNIFORM_BUFFER, 0, 32, cb);   /* flags: CF_FOG */
            glBindBufferBase(GL_UNIFORM_BUFFER, 0, dDraw); glBindBufferBase(GL_UNIFORM_BUFFER, 1, dComb); glBindBufferBase(GL_UNIFORM_BUFFER, 2, uboW); glBindBufferBase(GL_UNIFORM_BUFFER, 5, vubo);
            const float cl = len(ccU), nearW = 2.0f / cl, farW = 600000.f / cl, h0 = W_(0);
            /* the test scene (world = origin so, axes sf / sr): boxes, and the water surfaces other than the big plane */
            struct Box { float a0, a1, b0, b1, y0, y1, c[3]; };
            std::vector<Box> boxes; struct Rect { float a, b, ea, eb, y; }; std::vector<Rect> pools;
            if (g_scene) {
                boxes = { { 300, 1200, 100, 260, h0 - 80, h0 + 60, { 0.55f, 0.55f, 0.50f } },        /* the pier: a slab half in the water */
                          { 430, 550, -310, -190, h0 - 100, h0 + 140, { 0.70f, 0.35f, 0.30f } }, { 640, 760, -180, -60, h0 - 100, h0 + 120, { 0.30f, 0.55f, 0.35f } },
                          { 900, 1020, -400, -280, h0 - 100, h0 + 160, { 0.65f, 0.60f, 0.30f } }, { 770, 800, -45, -15, h0 - 100, h0 + 320, { 0.9f, 0.9f, 0.9f } },   /* a thin pole */
                          { 2500, 2600, 0, 6000, h0 - 300, h0 + 500, { 0.50f, 0.50f, 0.52f } } };    /* the shore: a wall the plane ends against */
                pools = { { 1700, -1500, 600, 900, h0 + 150 } };
                boxes.push_back({ 1092, 1100, -2400, -600, h0 - 20, h0 + 170, { 0.55f, 0.55f, 0.50f } });   /* its front rim */   /* a raised pool: another water height */
            }
            std::vector<float> sv;
            for (const Box& bx : boxes) {
                const float A[2] = { bx.a0, bx.a1 }, B[2] = { bx.b0, bx.b1 }, Y[2] = { bx.y0, bx.y1 };
                static const int fc[6][4] = { { 0, 2, 6, 4 }, { 1, 3, 7, 5 }, { 0, 1, 5, 4 }, { 2, 3, 7, 6 }, { 0, 1, 3, 2 }, { 4, 5, 7, 6 } };
                const V3 nr[6] = { { -sf.x, 0, -sf.z }, { sf.x, 0, sf.z }, { -sr.x, 0, -sr.z }, { sr.x, 0, sr.z }, { 0, -1, 0 }, { 0, 1, 0 } };
                for (int f = 0; f < 6; f++) for (int t = 0; t < 6; t++) {
                    const int c = fc[f][(t < 3) ? t : (t == 3 ? 0 : t - 2)];   /* 0 1 2, 0 2 3 */
                    const float a = A[c & 1], b = B[(c >> 1) & 1], y = Y[(c >> 2) & 1];
                    const float v[9] = { so.x + sf.x * a + sr.x * b, y, so.z + sf.z * a + sr.z * b, nr[f].x, nr[f].y, nr[f].z, bx.c[0], bx.c[1], bx.c[2] };
                    sv.insert(sv.end(), v, v + 9);
                }
            }
            auto setView = [&](GLuint prog) {
                auto U4 = [&](const char* n, float a, float b, float c, float d) { GLint l = glGetUniformLocation(prog, n); if (l >= 0) glUniform4f(l, a, b, c, d); };
                U4("uEyeV", eye.x, eye.y, eye.z, nearW); U4("uCC", ccU.x, ccU.y, ccU.z, farW); U4("uHX", hxU.x, hxU.y, hxU.z, 0); U4("uHY", hyU.x, hyU.y, hyU.z, 0);
            };
            if (!sv.empty()) {
                glUseProgram(pScene); setView(pScene);
                glBindVertexArray(vaoS); glBindBuffer(GL_ARRAY_BUFFER, vboS); glBufferData(GL_ARRAY_BUFFER, sv.size() * 4, sv.data(), GL_STREAM_DRAW);
                glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(sv.size() / 9)); glBindVertexArray(vao);
            }
            bindTex(0, tWSrc); bindTex(1, tWSrc); bindTex(2, tLut); bindTex(3, tWave); bindTex(4, tWMap);
            /* every water draw of the frame: the big plane, then the pools (the same list is replayed at half size in step 2) */
            auto drawWater = [&](GLuint prog) {
                glUseProgram(prog); setView(prog);
                auto U4 = [&](const char* n, float a, float b, float c, float d) { glUniform4f(glGetUniformLocation(prog, n), a, b, c, d); };
                U4("uShade", W_(14), W_(15), W_(16), 1); U4("uFogC", W_(17), W_(18), W_(19), W_(4));
                glUniform4f(glGetUniformLocation(prog, "uPlay"), play[0], play[1], play[2], play[3]);
                glBindVertexArray(vaoW);
                U4("uPl", h0, W_(1) > 1 ? W_(1) : 1, W_(5), W_(6)); U4("uGrid", eye.x, eye.z, 0, (float)T); U4("uExt", 60000.f, 60000.f, 0, 0); U4("uAx", 1, 0, 0, 1);
                glDrawElements(GL_TRIANGLES, GN * GN * 6, GL_UNSIGNED_INT, 0);
                for (const Rect& r : pools) {
                    U4("uPl", r.y, W_(1) > 1 ? W_(1) : 1, W_(5), W_(6)); U4("uGrid", so.x + sf.x * r.a + sr.x * r.b, so.z + sf.z * r.a + sr.z * r.b, 0, (float)T);
                    U4("uExt", r.ea, r.eb, 1, 0); U4("uAx", sf.x, sf.z, sr.x, sr.z);
                    glDrawElements(GL_TRIANGLES, GN * GN * 6, GL_UNSIGNED_INT, 0);
                }
                glBindVertexArray(vao);
            };
            drawWater((half || g_dbg == 3) ? pWBody : pWater);   /* step 1 (or the whole thing at full res) */
            if (half) {
                const int Wh = W / 2, Hh = H / 2;
                glBindFramebuffer(GL_FRAMEBUFFER, fHalfFx); glViewport(0, 0, Wh, Hh);   /* step 2: the water draws again at half size with the full water.frag */
                glClearColor(0, 0, 0, 0); glDepthMask(GL_TRUE); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                drawWater(pWater);
                glBindFramebuffer(GL_FRAMEBUFFER, fScene); glViewport(0, 0, W, H);
                if (g_dbg == 1) {   /* debug: the half-size target itself, full screen */
                    glDisable(GL_DEPTH_TEST); bindTex(0, tHalfFx); glUseProgram(pHalfDbg); glDrawArrays(GL_TRIANGLES, 0, 3);
                } else {   /* step 3: the water draws once more at full size, depth LEQUAL against the scene (writes off), watercomp.frag over the body colour */
                    const float comp[8] = { 0, (float)Hh, (float)Wh, -(float)Hh, (float)Wh, (float)Hh, 0, 0 };   /* GL targets are y-up: the shader's rows from the top are flipped here */
                    glBindBuffer(GL_UNIFORM_BUFFER, uboC); glBufferSubData(GL_UNIFORM_BUFFER, 0, 32, comp); glBindBufferBase(GL_UNIFORM_BUFFER, 0, uboC);
                    bindTex(0, tHalfFx);
                    glDepthFunc(GL_LEQUAL); glDepthMask(GL_FALSE);
                    if (g_dbg == 2) glDisable(GL_BLEND); else { glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); }
                    drawWater(g_dbg == 2 ? pMask : pComp);
                    glDisable(GL_BLEND);
                }
            }
            glDisable(GL_DEPTH_TEST);
            bindTex(0, tCloud); bindTex(1, tWave); bindTex(3, tDummy); bindTex(4, tDummy);
        }
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fShow); glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glBlitFramebuffer(0, 0, W, H, 0, 0, W, H, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glEndQuery(GL_TIME_ELAPSED);
        if (qv[qi ^ 1]) { GLuint64 ns = 0; glGetQueryObjectui64v(q[qi ^ 1], GL_QUERY_RESULT, &ns); gpums = frame < 8 ? ns / 1e6 : gpums * 0.9 + (ns / 1e6) * 0.1; }
        qv[qi] = true;
        SwapBuffers(dc); frame++; fpsN++;
        if (now - fpsT > 0.5) {
            fpsShown = (int)(fpsN / (now - fpsT)); fpsN = 0; fpsT = now;
            char t[256]; snprintf(t, sizeof t, "%s | %.2f ms/frame (scale 1/%d, load x%d = ~%.3f ms per pass) | sea: %s | sky: %s", g_gpuName, gpums, g_scale, g_rep, gpums / g_rep, kSeaNames[g_seaVar], kSkyNames[g_skyVar]);
            SetWindowTextA(hwnd, t);
        }
        if (seqN > 0 && frame > 16 && frame <= 16 + seqN) {
            std::vector<unsigned char> px((size_t)W * H * 4); glReadBuffer(GL_BACK); glReadPixels(0, 0, W, H, GL_BGRA, GL_UNSIGNED_BYTE, px.data());
            char fn[256]; snprintf(fn, sizeof fn, "%s_%03d.tga", outp.c_str(), frame - 17); FILE* f = fopen(fn, "wb");
            unsigned char hd[18] = { 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, (unsigned char)(W & 255), (unsigned char)(W >> 8), (unsigned char)(H & 255), (unsigned char)(H >> 8), 32, 8 };
            fwrite(hd, 1, 18, f); fwrite(px.data(), 1, px.size(), f); fclose(f);
            if (frame == 16 + seqN) { fprintf(stderr, "seq done gpu %.2f ms\n", gpums); break; }
        }
        if (kdown['P'] || kdown[VK_F12] || (!shot.empty() && frame == 6)) {   /* the scene target (before the swap), flipped to top-row-first */
            std::vector<unsigned char> px((size_t)W * H * 4), fl((size_t)W * H * 4);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, fShow); glReadBuffer(GL_COLOR_ATTACHMENT0); glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
            for (int y = 0; y < H; y++) memcpy(&fl[(size_t)y * W * 4], &px[(size_t)(H - 1 - y) * W * 4], (size_t)W * 4);
            std::string fn = !shot.empty() ? shot : kdown['P'] ? std::string("shot.tga") : std::string();
            if (fn.empty()) for (int n = 0; ; n++) { char b[64]; snprintf(b, sizeof b, "shot_%03d.png", n); if (GetFileAttributesA(b) == INVALID_FILE_ATTRIBUTES) { fn = b; break; } }
            const bool okw = img::saveImage(fn, W, H, fl.data());
            fprintf(stderr, "gpu %.2f ms\n%s %s\n", gpums, okw ? "saved" : "FAILED to save", fn.c_str()); if (!shot.empty()) break;
        }
        memset(kdown, 0, sizeof kdown);
    }
    return 0;
}
