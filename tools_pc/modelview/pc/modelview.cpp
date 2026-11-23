// modelview: compare mesh subdivision methods on a model (original | subdivided).
// UI: Dear ImGui panel on the left. Mouse (outside the panel): left-drag orbit, right- or middle-drag pan, wheel zoom. Keys: R / Home reset camera, F12 screenshot, Esc quit.
// args: --shot f.png --w n --h n --method FLAT|PN|PHONG|BUTTERFLY|LOOP --passes 1|2 --crease deg --view side|split|sub|orig
//       --wire 0|1 --edges 0|1 --checker 0|1 --normals 0|1 --light 0|1 --smooth 0|1   (the command line wins over data/modelview.ini)
#include <windows.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <string>
#include <vector>
#include "mv_mesh.h"
#include "mv_subdiv.h"
#include "mv_png.h"
#include "mv_rom.h"
#include "mv_anim.h"
#include "mv_tex.h"
#include <map>
#include "mv_imgui.h"
#include "imgui.h"

#define GLFUNCS \
 X(PFNGLCREATESHADERPROC,glCreateShader) X(PFNGLSHADERSOURCEPROC,glShaderSource) X(PFNGLCOMPILESHADERPROC,glCompileShader) \
 X(PFNGLGETSHADERIVPROC,glGetShaderiv) X(PFNGLGETSHADERINFOLOGPROC,glGetShaderInfoLog) X(PFNGLCREATEPROGRAMPROC,glCreateProgram) \
 X(PFNGLATTACHSHADERPROC,glAttachShader) X(PFNGLLINKPROGRAMPROC,glLinkProgram) X(PFNGLGETPROGRAMIVPROC,glGetProgramiv) \
 X(PFNGLGETPROGRAMINFOLOGPROC,glGetProgramInfoLog) X(PFNGLUSEPROGRAMPROC,glUseProgram) X(PFNGLGETUNIFORMLOCATIONPROC,glGetUniformLocation) \
 X(PFNGLUNIFORMMATRIX4FVPROC,glUniformMatrix4fv) X(PFNGLUNIFORM1IPROC,glUniform1i) X(PFNGLUNIFORM2FPROC,glUniform2f) X(PFNGLUNIFORM1FPROC,glUniform1f) \
 X(PFNGLGENVERTEXARRAYSPROC,glGenVertexArrays) X(PFNGLBINDVERTEXARRAYPROC,glBindVertexArray) X(PFNGLDELETEVERTEXARRAYSPROC,glDeleteVertexArrays) \
 X(PFNGLGENBUFFERSPROC,glGenBuffers) X(PFNGLBINDBUFFERPROC,glBindBuffer) X(PFNGLBUFFERDATAPROC,glBufferData) X(PFNGLDELETEBUFFERSPROC,glDeleteBuffers) \
 X(PFNGLVERTEXATTRIBPOINTERPROC,glVertexAttribPointer) X(PFNGLENABLEVERTEXATTRIBARRAYPROC,glEnableVertexAttribArray) \
 X(PFNGLGENFRAMEBUFFERSPROC,glGenFramebuffers) X(PFNGLBINDFRAMEBUFFERPROC,glBindFramebuffer) X(PFNGLDELETEFRAMEBUFFERSPROC,glDeleteFramebuffers) \
 X(PFNGLGENRENDERBUFFERSPROC,glGenRenderbuffers) X(PFNGLBINDRENDERBUFFERPROC,glBindRenderbuffer) X(PFNGLRENDERBUFFERSTORAGEPROC,glRenderbufferStorage) \
 X(PFNGLFRAMEBUFFERRENDERBUFFERPROC,glFramebufferRenderbuffer) X(PFNGLDELETERENDERBUFFERSPROC,glDeleteRenderbuffers) X(PFNGLCHECKFRAMEBUFFERSTATUSPROC,glCheckFramebufferStatus)
#define X(T,N) T N = 0;
GLFUNCS
#undef X
typedef HGLRC (WINAPI *PFNWGLCREATECONTEXTATTRIBSARB)(HDC, HGLRC, const int*);
typedef BOOL (WINAPI *PFNWGLSWAPINTERVAL)(int);

// ---------------------------------------------------------------- shaders
static const char* kMeshVert = R"(#version 330 core
layout(location=0) in vec3 aP; layout(location=1) in vec3 aN; layout(location=2) in vec2 aUV; layout(location=3) in vec4 aC;
uniform mat4 uVP;
out vec3 vW; out vec3 vN; out vec2 vUV; out vec4 vC;
void main() { vW = aP; vN = aN; vUV = aUV; vC = aC; gl_Position = uVP * vec4(aP, 1.0); }
)";
static const char* kMeshFrag = R"(#version 330 core
in vec3 vW; in vec3 vN; in vec2 vUV; in vec4 vC;
uniform sampler2D uTex; uniform int uMode, uLight, uSmooth, uVCol, uHilite; uniform vec2 uTexSize;
out vec4 o;
void main() {
    vec3 base = uVCol != 0 ? vC.rgb : vec3(0.75);
    if (uMode == 1) base = texture(uTex, vUV).rgb;   // checker: 1 UV unit = 32 texels
    else if (uMode == 2) { vec4 t = texture(uTex, vUV * 32.0 / uTexSize); if (t.a < 0.1) discard; base = t.rgb * (uVCol != 0 ? vC.rgb : vec3(1.0)); }
    if (uHilite != 0 && vC.a < 0.75) base = mix(base, vec3(1.0, 0.35, 0.2), 0.45);   // a head triangle (alpha 0.5)
    float shade = 1.0;
    if (uLight != 0) {
        vec3 n = uSmooth != 0 ? normalize(vN) * (gl_FrontFacing ? 1.0 : -1.0) : normalize(cross(dFdx(vW), dFdy(vW)));
        shade = 0.35 + 0.65 * max(dot(n, normalize(vec3(0.4, 0.8, 0.5))), 0.0);
    }
    o = vec4(base * shade, 1.0);
}
)";
static const char* kLineVert = R"(#version 330 core
layout(location=0) in vec3 aP; layout(location=1) in vec3 aC;
uniform mat4 uVP; out vec3 vC;
void main() { vC = aC; gl_Position = uVP * vec4(aP, 1.0); }
)";
static const char* kLineFrag = R"(#version 330 core
in vec3 vC; out vec4 o;
void main() { o = vec4(vC, 1.0); }
)";

static GLuint compile(GLenum t, const char* src, const char* name) {
    GLuint s = glCreateShader(t); glShaderSource(s, 1, &src, 0); glCompileShader(s);
    GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[4096]; glGetShaderInfoLog(s, sizeof log, 0, log); fprintf(stderr, "SHADER %s:\n%s\n", name, log); return 0; }
    return s;
}
GLuint mv_program(const char* vs, const char* fs, const char* name) {
    GLuint a = compile(GL_VERTEX_SHADER, vs, name), b = compile(GL_FRAGMENT_SHADER, fs, name); if (!a || !b) return 0;
    GLuint p = glCreateProgram(); glAttachShader(p, a); glAttachShader(p, b); glLinkProgram(p);
    GLint ok = 0; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { char log[4096]; glGetProgramInfoLog(p, sizeof log, 0, log); fprintf(stderr, "LINK %s:\n%s\n", name, log); return 0; }
    return p;
}

// ---------------------------------------------------------------- math (column-major 4x4)
struct V3 { float x, y, z; };
static V3 sub(V3 a, V3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
static V3 cross(V3 a, V3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
static float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static V3 nrm(V3 a) { float l = sqrtf(dot(a, a)); return l > 1e-12f ? V3{ a.x / l, a.y / l, a.z / l } : V3{ 0, 0, 0 }; }
static void mul4(const float* a, const float* b, float* o) {   // o = a * b
    for (int c = 0; c < 4; c++) for (int r = 0; r < 4; r++) { float s = 0; for (int k = 0; k < 4; k++) s += a[k * 4 + r] * b[c * 4 + k]; o[c * 4 + r] = s; }
}
static void perspective(float fovy, float asp, float n, float f, float* m) {
    const float t = 1.0f / tanf(fovy * 0.5f); memset(m, 0, 64);
    m[0] = t / asp; m[5] = t; m[10] = (f + n) / (n - f); m[11] = -1; m[14] = 2 * f * n / (n - f);
}
static void lookAt(V3 e, V3 c, V3 up, float* m) {
    V3 f = nrm(sub(c, e)), s = nrm(cross(f, up)), u = cross(s, f);
    float r[16] = { s.x, u.x, -f.x, 0, s.y, u.y, -f.y, 0, s.z, u.z, -f.z, 0, -dot(s, e), -dot(u, e), dot(f, e), 1 };
    memcpy(m, r, 64);
}

// ---------------------------------------------------------------- state
enum { VIEW_SIDE = 0, VIEW_SPLIT, VIEW_SUB, VIEW_ORIG };
static MvMethod g_method = MV_GAME;
static MvParams g_par;
static int g_view = VIEW_SIDE;
static float g_split = 0.5f;
static bool g_hilite = false;   // tint the head triangles
static bool g_vcol = false;   // vertex colours: shown on untextured triangles, and multiplied into textures (off = texture only)
static bool g_wire = true, g_edgeCol = true, g_checker = false, g_normals = false, g_light = true, g_smooth = false;
static float g_yaw = 25, g_pitch = 12, g_dist = 3.2f, g_tgt[3] = { 0, 1.0f, 0 };
static bool g_dirtyMesh = true, g_dirtyGpu = true;   // subdivide again / rebuild the GPU buffers
static int g_cw = 1280, g_ch = 720;                   // client size
static bool g_shotReq = false;
static int g_menuH = 19;                              // the ImGui menu bar (measured in menuBar(); main sets a first guess)
static const int g_panelW = 330;                      // the ImGui panel on the left; the scene uses the rest

static MvMesh g_orig, g_sub;
static std::vector<uint8_t> g_origFlags, g_subFlags;
static MvEdgeStats g_stats = {};
static bool g_wired = true;

struct Draw { int first, count, mat; };
struct Gpu { GLuint vaoT = 0, vboT = 0, vaoL = 0, vboL = 0, vaoN = 0, vboN = 0; int nT = 0, nL = 0, nN = 0; std::vector<Draw> draws; };
static Gpu g_gOrig, g_gSub;
static GLuint g_pMesh = 0, g_pLine = 0, g_tChecker = 0;

static std::string g_modelName;   // "" = the test mannequin, else a C* file of the ROM
static int g_tier = 0;            // 0 near (detailed), 1 far (simplified)
static std::string g_loadErr, g_animNote, g_texInfo;
static bool g_tex = true;                                  // draw the model's textures
static int g_animIdx = -1; static bool g_animOn = true, g_play = false; static float g_frame = 0, g_speed = 1; static std::string g_animName = "idle";
static std::map<int, std::pair<GLuint, std::pair<int, int>>> g_texGL;   // texnum -> GL texture, size (0 = not decodable)
static void frameModel() {   // the camera target and distance from the model's bounding box
    const std::vector<MvVec3> jw = mv_joint_worlds(g_orig);
    float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
    for (uint32_t i : g_orig.idx) {
        const MvVec3 w = mv_vertex_posed(g_orig, jw, i); const float c[3] = { w.x, w.y, w.z };
        for (int k = 0; k < 3; k++) { if (c[k] < lo[k]) lo[k] = c[k]; if (c[k] > hi[k]) hi[k] = c[k]; }
    }
    if (lo[0] > hi[0]) return;
    float ext = 0;
    for (int k = 0; k < 3; k++) { g_tgt[k] = (lo[k] + hi[k]) * 0.5f * g_orig.scale; if ((hi[k] - lo[k]) * g_orig.scale > ext) ext = (hi[k] - lo[k]) * g_orig.scale; }
    g_dist = ext * 0.62f / tanf(0.35f) * 0.9f + ext * 0.5f;
}
static void resetCamera() { g_yaw = 25; g_pitch = 12; frameModel(); }
static void applyPose() {   // skin the model with the chosen animation frame (the subdivision stays in the rest pose)
    if (g_modelName.empty() || !mv_rom_ready()) { g_orig.pose.clear(); g_sub.pose.clear(); g_dirtyGpu = true; return; }   // animations: not enabled in this build step
    int ns = 0, nt = 0; const std::vector<uint8_t>& img = mv_rom_model_image(&ns, &nt);
    std::vector<float> pose;
    mv_anim_pose(img, ns, nt, g_modelName, g_animOn ? g_animIdx : -1, (int)g_frame, (int)g_orig.joints.size(), pose, g_animNote);
    g_orig.pose = pose;
    if (g_sub.joints.size() == g_orig.joints.size()) g_sub.pose = pose;
    g_dirtyGpu = true;
}

// ---------------------------------------------------------------- GPU upload
static void edgeColour(uint8_t f, bool byReason, float* c) {
    c[0] = c[1] = c[2] = 0.06f;
    if (!byReason) return;
    if (f & MV_EDGE_NECK) { c[0] = 1.0f; c[1] = 1.0f; c[2] = 1.0f; }
    else if (f & MV_EDGE_BOUNDARY) { c[0] = 1.0f; c[1] = 0.15f; c[2] = 0.15f; }
    else if (f & MV_EDGE_JOINT) { c[0] = 0.1f; c[1] = 0.95f; c[2] = 0.2f; }
    else if (f & MV_EDGE_CREASE) { c[0] = 1.0f; c[1] = 0.8f; c[2] = 0.05f; }
    else if (f & MV_EDGE_TEXFEAT) { c[0] = 0.1f; c[1] = 0.85f; c[2] = 1.0f; }
    else if (f & MV_EDGE_UVSEAM) { c[0] = 0.85f; c[1] = 0.3f; c[2] = 1.0f; }
}
static void vattr(GLuint& vao, GLuint& vbo, const std::vector<float>& d, int stride, int n0, int n1, int n2, int n3) {
    if (!vao) { glGenVertexArrays(1, &vao); glGenBuffers(1, &vbo); }
    glBindVertexArray(vao); glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, d.size() * 4, d.empty() ? 0 : d.data(), GL_STATIC_DRAW);
    const int ns[4] = { n0, n1, n2, n3 }; int off = 0;
    for (int i = 0; i < 4; i++) { if (!ns[i]) break; glVertexAttribPointer(i, ns[i], GL_FLOAT, GL_FALSE, stride * 4, (const void*)(size_t)(off * 4)); glEnableVertexAttribArray(i); off += ns[i]; }
}
static void upload(const MvMesh& m, const std::vector<uint8_t>& flags, Gpu& g) {
    const std::vector<MvVec3> jw = mv_joint_worlds(m);
    const size_t nv = m.verts.size();
    std::vector<MvVec3> wp(nv);
    std::vector<V3> vn(nv, V3{ 0, 0, 0 });
    for (size_t i = 0; i < nv; i++) { wp[i] = mv_vertex_posed(m, jw, i); wp[i].x *= m.scale; wp[i].y *= m.scale; wp[i].z *= m.scale; }
    for (size_t t = 0; t < m.triCount(); t++) {   // area-weighted vertex normals (per vertex, not welded: hard edges stay hard)
        const uint32_t* v = &m.idx[3 * t];
        const V3 a = { wp[v[0]].x, wp[v[0]].y, wp[v[0]].z }, b = { wp[v[1]].x, wp[v[1]].y, wp[v[1]].z }, c = { wp[v[2]].x, wp[v[2]].y, wp[v[2]].z };
        const V3 n = cross(sub(b, a), sub(c, a));
        for (int k = 0; k < 3; k++) { vn[v[k]].x += n.x; vn[v[k]].y += n.y; vn[v[k]].z += n.z; }
    }
    for (size_t i = 0; i < nv; i++) vn[i] = nrm(vn[i]);
    std::vector<float> tri, line, nl;
    g.draws.clear();
    {   // triangles grouped by material (one draw per texture), the lines in triangle order
        std::vector<std::vector<size_t>> by(m.materials.size() + 1);
        for (size_t t = 0; t < m.triCount(); t++) by[(m.tmat.empty() || m.tmat[t] < 0) ? 0 : (size_t)m.tmat[t] + 1].push_back(t);
        for (size_t mi = 0; mi < by.size(); mi++) {
            if (by[mi].empty()) continue;
            g.draws.push_back({ (int)(tri.size() / 12), (int)by[mi].size() * 3, (int)mi - 1 });
            for (size_t t : by[mi])
                for (int e = 0; e < 3; e++) {
                    const uint32_t a = m.idx[3 * t + e]; const MvVertex& vv = m.verts[a];
                    tri.insert(tri.end(), { wp[a].x, wp[a].y, wp[a].z, vn[a].x, vn[a].y, vn[a].z, vv.uv[0], vv.uv[1], vv.col[0], vv.col[1], vv.col[2], (m.headOnly && mv_tri_is_head(m, t)) ? 0.5f : 1.0f });
                }
        }
    }
    for (size_t t = 0; t < m.triCount(); t++)
        for (int e = 0; e < 3; e++) {
            const uint32_t a = m.idx[3 * t + e], b = m.idx[3 * t + (e + 1) % 3];
            float c[3]; edgeColour(flags.size() > 3 * t + e ? flags[3 * t + e] : 0, g_edgeCol, c);
            line.insert(line.end(), { wp[a].x, wp[a].y, wp[a].z, c[0], c[1], c[2], wp[b].x, wp[b].y, wp[b].z, c[0], c[1], c[2] });
        }
    for (size_t i = 0; i < nv; i++)
        nl.insert(nl.end(), { wp[i].x, wp[i].y, wp[i].z, 0.2f, 0.9f, 1.0f, wp[i].x + vn[i].x * 0.05f, wp[i].y + vn[i].y * 0.05f, wp[i].z + vn[i].z * 0.05f, 0.2f, 0.9f, 1.0f });
    vattr(g.vaoT, g.vboT, tri, 12, 3, 3, 2, 4); g.nT = (int)(tri.size() / 12);
    vattr(g.vaoL, g.vboL, line, 6, 3, 3, 0, 0); g.nL = (int)(line.size() / 6);
    vattr(g.vaoN, g.vboN, nl, 6, 3, 3, 0, 0); g.nN = (int)(nl.size() / 6);
}

static std::string g_methodNote;
static bool loadModel(const std::string& name) {   // "" = the mannequin; keeps the old model on failure
    MvMesh m; std::string err;
    if (name.empty()) m = mv_make_mannequin();
    else if (!mv_rom_load(name, g_tier, m, err)) { g_loadErr = name + ": " + err; fprintf(stderr, "model %s\n", g_loadErr.c_str()); return false; }
    g_loadErr.clear(); g_orig = m; g_modelName = name; g_dirtyMesh = true;
    g_sub = MvMesh();
    g_animIdx = g_animName.empty() ? -1 : mv_anim_find(g_animName);
    applyPose(); frameModel();
    {   // textures: how many decode (the GL textures are made at draw time)
        int ok = 0, bad = 0; std::string why;
        for (const MvMaterial& mt : g_orig.materials) { MvTexImage ti; if (mv_rom_texture(mt.texnum, ti)) ok++; else { bad++; if (why.size() < 90) why += (why.empty() ? "" : "; ") + std::to_string(mt.texnum) + ": " + ti.note; } }
        g_texInfo = std::to_string(ok) + " of " + std::to_string(ok + bad) + " textures decoded" + (why.empty() ? "" : " (" + why + ")");
    }
    fprintf(stderr, "model %s: %d vertices, %d triangles\n", m.name.c_str(), (int)m.verts.size(), (int)m.triCount());
    return true;
}
static void rebuild() {
    if (g_dirtyMesh) {
        g_stats = mv_classify_edges(g_orig, g_par, g_origFlags);
        if (g_method == MV_HEAD) mv_head_classify(g_orig, g_par, g_origFlags);
        if (g_method == MV_GAME && g_orig.headOnly) { MvParams gp = g_par; gp.crease_deg = 130.0f; mv_classify_edges(g_orig, gp, g_origFlags); mv_mark_neck(g_orig, g_origFlags); }
        g_wired = mv_subdivide(g_method, g_par, g_orig, g_sub, g_subFlags);
        if (g_method == MV_HEAD) { const MvHeadStats& hs = mv_head_stats(); fprintf(stderr, "HEAD %s: edges crease %d texture %d boundary %d joint %d, max move %.3f, neck gap %.4f, %d -> %d triangles\n", g_orig.name.c_str(), hs.creaseEdges, hs.texEdges, hs.boundaryEdges, hs.jointEdges, hs.maxDisp, hs.neckGap, (int)g_orig.triCount(), (int)g_sub.triCount()); }
        g_methodNote = std::string(MV_METHOD_NAMES[g_method]) + (mv_method_note(g_method)[0] ? std::string(": ") + mv_method_note(g_method) : std::string());
        g_dirtyMesh = false; g_dirtyGpu = true;
    }
    if (g_dirtyGpu) { upload(g_orig, g_origFlags, g_gOrig); upload(g_sub, g_subFlags, g_gSub); g_dirtyGpu = false; }
}

// ---------------------------------------------------------------- drawing
static void viewProj(float aspect, float* vp) {
    const float yw = g_yaw * 0.0174533f, pt = g_pitch * 0.0174533f;
    const V3 tg = { g_tgt[0], g_tgt[1], g_tgt[2] };
    const V3 eye = { tg.x + g_dist * cosf(pt) * sinf(yw), tg.y + g_dist * sinf(pt), tg.z + g_dist * cosf(pt) * cosf(yw) };
    float p[16], v[16]; perspective(0.7f, aspect, 0.05f, 100.0f, p); lookAt(eye, tg, V3{ 0, 1, 0 }, v); mul4(p, v, vp);
}
static void drawMesh(const Gpu& g, const float* vp) {
    const MvMesh& mesh = (&g == &g_gOrig) ? g_orig : g_sub;
    glUseProgram(g_pMesh);
    glUniformMatrix4fv(glGetUniformLocation(g_pMesh, "uVP"), 1, GL_FALSE, vp);
    glUniform1i(glGetUniformLocation(g_pMesh, "uLight"), g_light); glUniform1i(glGetUniformLocation(g_pMesh, "uHilite"), g_hilite);
    glUniform1i(glGetUniformLocation(g_pMesh, "uVCol"), g_vcol || mesh.materials.empty());
    glUniform1i(glGetUniformLocation(g_pMesh, "uSmooth"), g_smooth); glUniform1i(glGetUniformLocation(g_pMesh, "uTex"), 0);
    glBindVertexArray(g.vaoT);
    glEnable(GL_POLYGON_OFFSET_FILL); glPolygonOffset(1.0f, 1.0f);
    for (const Draw& d : g.draws) {
        int mode = g_checker ? 1 : 0; float tw = 1, th = 1;
        if (d.mat >= 0 && d.mat < (int)mesh.materials.size() && !g_checker && g_tex) {
            const MvMaterial& mt = mesh.materials[(size_t)d.mat];
            auto it = g_texGL.find(mt.texnum);
            if (it == g_texGL.end()) {   // decode once, upload once; a texture that cannot be decoded stays untextured
                MvTexImage ti; GLuint id = 0;
                if (mv_rom_texture(mt.texnum, ti)) {
                    glGenTextures(1, &id); glBindTexture(GL_TEXTURE_2D, id);
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, ti.w, ti.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, ti.rgba.data());
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                }
                it = g_texGL.emplace(mt.texnum, std::make_pair(id, std::make_pair(ti.w, ti.h))).first;
            }
            if (it->second.first) {
                static const GLint wrap[3] = { GL_REPEAT, GL_CLAMP_TO_EDGE, GL_MIRRORED_REPEAT };
                glBindTexture(GL_TEXTURE_2D, it->second.first);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap[mt.wrapS]); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap[mt.wrapT]);
                mode = 2; tw = (float)it->second.second.first; th = (float)it->second.second.second;
            }
        }
        if (mode == 1) glBindTexture(GL_TEXTURE_2D, g_tChecker);
        glUniform1i(glGetUniformLocation(g_pMesh, "uMode"), mode); glUniform2f(glGetUniformLocation(g_pMesh, "uTexSize"), tw, th);
        glDrawArrays(GL_TRIANGLES, d.first, d.count);
    }
    glDisable(GL_POLYGON_OFFSET_FILL);
    if (g_wire || g_normals) {
        glUseProgram(g_pLine); glUniformMatrix4fv(glGetUniformLocation(g_pLine, "uVP"), 1, GL_FALSE, vp);
        if (g_wire) { glBindVertexArray(g.vaoL); glDrawArrays(GL_LINES, 0, g.nL); }
        if (g_normals) { glBindVertexArray(g.vaoN); glDrawArrays(GL_LINES, 0, g.nN); }
    }
}
static void renderScene(int FW, int FH) {
    const int X0 = g_panelW, W = FW - g_panelW, H = FH - g_menuH;   // the scene area: right of the panel, under the menu bar
    glViewport(0, 0, FW, FH); glDisable(GL_SCISSOR_TEST);
    glClearColor(0.20f, 0.22f, 0.26f, 1); glClearDepth(1); glDepthMask(GL_TRUE); glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    float vp[16];
    auto pane = [&](int x, int w, const Gpu& g) {   // a pane with its own aspect
        glEnable(GL_SCISSOR_TEST); glScissor(X0 + x, 0, w, H); glViewport(X0 + x, 0, w, H);
        viewProj((float)w / (float)H, vp); drawMesh(g, vp);
    };
    auto bar = [&](int x) { x += X0; glEnable(GL_SCISSOR_TEST); glScissor(x - 1, 0, 2, H); glClearColor(0.9f, 0.9f, 0.9f, 1); glClear(GL_COLOR_BUFFER_BIT); };
    if (g_view == VIEW_SIDE) { const int h = W / 2; pane(0, h, g_gOrig); pane(h, W - h, g_gSub); bar(h); }
    else if (g_view == VIEW_SPLIT) {
        const int sx = (int)(g_split * W);
        glViewport(X0, 0, W, H); viewProj((float)W / (float)H, vp);
        glEnable(GL_SCISSOR_TEST); glScissor(X0, 0, sx, H); drawMesh(g_gOrig, vp);
        glScissor(X0 + sx, 0, W - sx, H); drawMesh(g_gSub, vp);
        bar(sx);
    }
    else if (g_view == VIEW_SUB) pane(0, W, g_gSub);
    else pane(0, W, g_gOrig);
    glDisable(GL_SCISSOR_TEST);
}
static void uiFrame(int W, int H);
static void uiWarm(int W, int H);   // one frame without drawing: auto-sized windows show from the second frame
static bool shotTo(const std::string& path, int W, int H) {   // through an offscreen target: an overlapped window cannot spoil it
    GLuint fbo, rbc, rbd;
    glGenFramebuffers(1, &fbo); glGenRenderbuffers(1, &rbc); glGenRenderbuffers(1, &rbd);
    glBindRenderbuffer(GL_RENDERBUFFER, rbc); glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, W, H);
    glBindRenderbuffer(GL_RENDERBUFFER, rbd); glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, W, H);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rbc);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rbd);
    bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    if (ok) {
        rebuild(); uiWarm(W, H); renderScene(W, H); uiFrame(W, H);
        std::vector<uint8_t> px((size_t)W * H * 4), fl((size_t)W * H * 4);
        glReadBuffer(GL_COLOR_ATTACHMENT0); glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
        for (int y = 0; y < H; y++) memcpy(&fl[(size_t)y * W * 4], &px[(size_t)(H - 1 - y) * W * 4], (size_t)W * 4);
        ok = mvpng::write(path, W, H, fl.data());
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0); glDeleteFramebuffers(1, &fbo); glDeleteRenderbuffers(1, &rbc); glDeleteRenderbuffers(1, &rbd);
    return ok;
}

// ---------------------------------------------------------------- settings + ini
static void saveIni(const char* path) {
    FILE* f = fopen(path, "w"); if (!f) return;
    fprintf(f, "method=%s\ncrease=%g\nsplit_joint_edges=%d\npasses=%d\nphong_alpha=%g\nview=%d\nsplit=%g\nwire=%d\nedge_colours=%d\nchecker=%d\nnormals=%d\nlight=%d\nsmooth=%d\n"
               "vcol=%d\nhilite=%d\nyaw=%g\npitch=%g\ndist=%g\ntarget=%g,%g,%g\nmodel=%s\ntier=%d\npn_clamp=%g\nanim=%s\nframe=%g\ntextures=%d\ntexfeat=%d\nneckpin=%d\ntaubin=%d\n",
            MV_METHOD_NAMES[g_method], g_par.crease_deg, g_par.split_joint_edges, g_par.passes, g_par.phong_alpha, g_view, g_split, g_wire, g_edgeCol, g_checker, g_normals, g_light, g_smooth,
            g_vcol, g_hilite, g_yaw, g_pitch, g_dist, g_tgt[0], g_tgt[1], g_tgt[2], g_modelName.c_str(), g_tier, g_par.pn_clamp, g_animName.c_str(), g_frame, g_tex, (int)g_par.head_tex_features, (int)g_par.neck_pin, g_par.taubin_iters);
    fclose(f);
}
static int methodByName(const char* s) { if (!_strnicmp(s, "GAME", 4)) return MV_GAME; for (int i = 0; i < MV_METHOD_COUNT; i++) if (!_stricmp(s, MV_METHOD_NAMES[i])) return i; return atoi(s) % MV_METHOD_COUNT; }
static void setKey(const char* k, const char* v) {
    const float f = (float)atof(v); const int i = atoi(v);
    if (!strcmp(k, "method")) g_method = (MvMethod)methodByName(v);
    else if (!strcmp(k, "crease")) g_par.crease_deg = fminf(fmaxf(f, 0), 180);
    else if (!strcmp(k, "split_joint_edges")) g_par.split_joint_edges = i != 0;
    else if (!strcmp(k, "passes")) g_par.passes = i < 2 ? 1 : 2;
    else if (!strcmp(k, "phong_alpha")) g_par.phong_alpha = fminf(fmaxf(f, 0), 1);
    else if (!strcmp(k, "view")) { if (!_stricmp(v, "side")) g_view = VIEW_SIDE; else if (!_stricmp(v, "split")) g_view = VIEW_SPLIT; else if (!_stricmp(v, "sub")) g_view = VIEW_SUB; else if (!_stricmp(v, "orig")) g_view = VIEW_ORIG; else g_view = i & 3; }
    else if (!strcmp(k, "split")) g_split = fminf(fmaxf(f, 0), 1);
    else if (!strcmp(k, "wire")) g_wire = i != 0;
    else if (!strcmp(k, "edge_colours") || !strcmp(k, "edges")) g_edgeCol = i != 0;
    else if (!strcmp(k, "checker")) g_checker = i != 0;
    else if (!strcmp(k, "normals")) g_normals = i != 0;
    else if (!strcmp(k, "light")) g_light = i != 0;
    else if (!strcmp(k, "smooth")) g_smooth = i != 0;
    else if (!strcmp(k, "yaw")) g_yaw = f; else if (!strcmp(k, "pitch")) g_pitch = f; else if (!strcmp(k, "dist")) g_dist = f;
    else if (!strcmp(k, "model")) g_modelName = v;
    else if (!strcmp(k, "anim")) { g_animName = v; g_animOn = true; if (v[0] >= '0' && v[0] <= '9' && mv_anim_list().size() > (size_t)i) g_animName = mv_anim_list()[(size_t)i].name; if (!strcmp(v, "none")) g_animName.clear(); }
    else if (!strcmp(k, "frame")) g_frame = f < 0 ? 0 : f;
    else if (!strcmp(k, "textures")) g_tex = i != 0;
    else if (!strcmp(k, "vcol")) g_vcol = i != 0;
    else if (!strcmp(k, "hilite")) g_hilite = i != 0;
    else if (!strcmp(k, "texfeat")) g_par.head_tex_features = i != 0;
    else if (!strcmp(k, "neckpin")) g_par.neck_pin = i != 0;
    else if (!strcmp(k, "taubin")) g_par.taubin_iters = i < 0 ? 0 : i > 50 ? 50 : i;
    else if (!strcmp(k, "clean") && i) { g_vcol = g_wire = g_edgeCol = g_normals = false; }
    else if (!strcmp(k, "tier")) g_tier = i ? 1 : 0;
    else if (!strcmp(k, "pn_clamp")) g_par.pn_clamp = fminf(fmaxf(f, 0), 1);
    else if (!strcmp(k, "target")) sscanf(v, "%f,%f,%f", &g_tgt[0], &g_tgt[1], &g_tgt[2]);
    g_dirtyMesh = g_dirtyGpu = true;
}
static void loadIni(const char* path) {
    FILE* f = fopen(path, "r"); if (!f) return;
    char line[256];
    while (fgets(line, sizeof line, f)) {
        char* eq = strchr(line, '='); if (!eq) continue; *eq = 0;
        char* v = eq + 1; for (char* e = v + strlen(v); e > v && (e[-1] == '\n' || e[-1] == '\r'); ) *--e = 0;
        setKey(line, v);
    }
    fclose(f);
}

// ---------------------------------------------------------------- the ImGui panel
static void menuBar() {
    if (!ImGui::BeginMainMenuBar()) return;
    g_menuH = (int)ceilf(ImGui::GetWindowHeight());   // the real bar height (1.92 rounds the font size, so a hand-made sum is 1 px off)
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Save ini")) saveIni("modelview.ini");
        if (ImGui::MenuItem("Load ini")) loadIni("modelview.ini");
        if (ImGui::MenuItem("Screenshot", "F12")) g_shotReq = true;
        if (ImGui::MenuItem("Reset camera", "R")) resetCamera();
        ImGui::Separator();
        if (ImGui::MenuItem("Quit", "Esc")) PostQuitMessage(0);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Model")) {
        if (ImGui::MenuItem("Test mannequin", nullptr, g_modelName.empty())) loadModel("");
        if (!mv_rom_ready()) ImGui::MenuItem("Characters (no ROM: --rom or MODELVIEW_ROM)", nullptr, false, false);
        for (int pass = 0; pass < 2 && mv_rom_ready(); pass++) {
            if (!ImGui::BeginMenu(pass ? "Heads" : "Bodies")) continue;
            for (const MvModelEntry& e : mv_rom_models())
                if (e.head == (pass == 1) && ImGui::MenuItem(e.name.c_str(), nullptr, g_modelName == e.name)) loadModel(e.name);
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        if (ImGui::MenuItem("Side by side", nullptr, g_view == VIEW_SIDE)) g_view = VIEW_SIDE;
        if (ImGui::MenuItem("Split slider", nullptr, g_view == VIEW_SPLIT)) g_view = VIEW_SPLIT;
        if (ImGui::MenuItem("Subdivided", nullptr, g_view == VIEW_SUB)) g_view = VIEW_SUB;
        if (ImGui::MenuItem("Original", nullptr, g_view == VIEW_ORIG)) g_view = VIEW_ORIG;
        ImGui::Separator();
        if (ImGui::MenuItem("Clean view (no overlays)")) { g_vcol = g_wire = g_edgeCol = g_normals = false; g_dirtyGpu = true; }
        ImGui::Separator();
        ImGui::MenuItem("Textures (model)", nullptr, &g_tex);
        ImGui::MenuItem("Vertex colours (off = texture only)", nullptr, &g_vcol);
        ImGui::MenuItem("Highlight head region", nullptr, &g_hilite);
        ImGui::MenuItem("Wireframe overlay", nullptr, &g_wire);
        if (ImGui::MenuItem("Colour straight edges by reason", nullptr, &g_edgeCol)) g_dirtyGpu = true;
        ImGui::MenuItem("Checker texture (judge UVs)", nullptr, &g_checker);
        ImGui::MenuItem("Show normals", nullptr, &g_normals);
        ImGui::MenuItem("Directional light", nullptr, &g_light);
        ImGui::MenuItem("Smooth shading (vertex normals)", nullptr, &g_smooth);
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
}
static void panel(int H) {
    ImGui::SetNextWindowPos(ImVec2(0, (float)g_menuH)); ImGui::SetNextWindowSize(ImVec2((float)g_panelW, (float)(H - g_menuH)));
    ImGui::Begin("modelview", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings);
    ImGui::PushItemWidth(-110);
    ImGui::Text("Model: %s", g_modelName.empty() ? "Test mannequin" : g_modelName.c_str());
    if (!g_loadErr.empty()) ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", g_loadErr.c_str());
    if (!g_modelName.empty()) {
        ImGui::Text("Tier"); ImGui::SameLine();
        if (ImGui::RadioButton("near", g_tier == 0)) { g_tier = 0; loadModel(g_modelName); } ImGui::SameLine();
        if (ImGui::RadioButton("far (simplified)", g_tier == 1)) { g_tier = 1; loadModel(g_modelName); }
    }
    if (!g_modelName.empty()) ImGui::TextWrapped("%s", g_texInfo.c_str());
    if (!g_modelName.empty() && !mv_anim_list().empty() && ImGui::CollapsingHeader("Animation", ImGuiTreeNodeFlags_DefaultOpen)) {
        bool ch = ImGui::Checkbox("Pose with an animation", &g_animOn);
        const std::vector<MvAnimInfo>& al = mv_anim_list();
        if (g_animIdx >= 0 && g_animIdx < (int)al.size()) {
            const int last = al[(size_t)g_animIdx].frames - 1;
            if (ImGui::SliderFloat("Frame", &g_frame, 0.0f, (float)(last > 0 ? last : 0), "%.0f")) ch = true;
            ImGui::Checkbox("Play", &g_play); ImGui::SameLine(); ImGui::SliderFloat("Speed", &g_speed, 0.1f, 3.0f, "%.1fx");
        }
        if (ImGui::BeginListBox("##anims", ImVec2(-1, 130))) {
            for (int i = 0; i < (int)al.size(); i++)
                if (ImGui::Selectable(al[(size_t)i].name.c_str(), i == g_animIdx)) { g_animIdx = i; g_animName = al[(size_t)i].name; g_frame = 0; ch = true; }
            ImGui::EndListBox();
        }
        if (!g_animNote.empty()) ImGui::TextColored(ImVec4(1, 0.7f, 0.3f, 1), "%s", g_animNote.c_str());
        if (ch) applyPose();
    }
    if (ImGui::CollapsingHeader("Method", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (int i = 0; i < MV_METHOD_COUNT; i++) {
            std::string l = MV_METHOD_NAMES[i]; if (i != MV_FLAT && i != MV_GAME) l += "  (viewer only)";
            if (ImGui::RadioButton(l.c_str(), g_method == i)) { g_method = (MvMethod)i; g_dirtyMesh = true; }
        }
        ImGui::TextWrapped("Showing: %s", g_methodNote.c_str());
    }
    if (ImGui::CollapsingHeader("Edges", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::SliderFloat("Crease angle", &g_par.crease_deg, 0.0f, 180.0f, "%.0f deg")) g_dirtyMesh = true;
        if (ImGui::Checkbox((g_method == MV_PN || g_method == MV_GAME) ? "Split joint edges (not for PN)" : "Split joint edges", &g_par.split_joint_edges)) g_dirtyMesh = true;
        ImGui::Text("Passes"); ImGui::SameLine();
        if (ImGui::RadioButton("1", g_par.passes == 1)) { g_par.passes = 1; g_dirtyMesh = true; } ImGui::SameLine();
        if (ImGui::RadioButton("2", g_par.passes == 2)) { g_par.passes = 2; g_dirtyMesh = true; } ImGui::SameLine();
        if (ImGui::RadioButton("3 (HEAD)", g_par.passes == 3)) { g_par.passes = 3; g_dirtyMesh = true; }
        if (g_method == MV_HEAD) {
            if (ImGui::Checkbox("Texture features are creases", &g_par.head_tex_features)) g_dirtyMesh = true;
            if (ImGui::Checkbox("Pin the neck (boundary)", &g_par.neck_pin)) g_dirtyMesh = true;
            if (ImGui::SliderInt("Taubin smoothing", &g_par.taubin_iters, 0, 20, g_par.taubin_iters ? "%d iterations" : "off")) g_dirtyMesh = true;
            if (mv_head_stats().multiMatrix) ImGui::TextColored(ImVec4(1, 0.7f, 0.3f, 1), "HEAD is meant for single-matrix meshes: edges across matrices are creases");
        }
        if (g_method == MV_PHONG && ImGui::SliderFloat("Phong alpha", &g_par.phong_alpha, 0.0f, 1.0f, "%.2f")) g_dirtyMesh = true;
        if ((g_method == MV_PN || g_method == MV_GAME) && ImGui::SliderFloat("PN bulge clamp", &g_par.pn_clamp, 0.0f, 0.5f, "%.2f")) g_dirtyMesh = true;
        if (g_view == VIEW_SPLIT) ImGui::SliderFloat("Split", &g_split, 0.0f, 1.0f, "%.2f");
    }
    if (ImGui::CollapsingHeader("Stats", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("Original:    %d verts, %d tris", (int)g_orig.verts.size(), (int)g_orig.triCount());
        ImGui::Text("Subdivided:  %d verts, %d tris", (int)g_sub.verts.size(), (int)g_sub.triCount());
        if (g_orig.headOnly) {
            int ho = 0, hs = 0; for (size_t t = 0; t < g_orig.triCount(); t++) if (mv_tri_is_head(g_orig, t)) ho++;
            for (size_t t = 0; t < g_sub.triCount(); t++) if (mv_tri_is_head(g_sub, t)) hs++;
            ImGui::Text("Head tris: %d -> %d, body tris: %d", ho, hs, (int)g_orig.triCount() - ho);
        }
        ImGui::Text("Edges: %d (welded by position)", g_stats.edges);
        ImGui::Text("  crease %d, boundary %d", g_stats.crease, g_stats.boundary);
        ImGui::Text("  joint %d, UV seam %d", g_stats.joint, g_stats.seam);
        if (g_method == MV_HEAD && mv_head_stats().valid) {
            const MvHeadStats& hs = mv_head_stats();
            ImGui::Text("HEAD edges: crease %d, texture %d, boundary %d, joint %d", hs.creaseEdges, hs.texEdges, hs.boundaryEdges, hs.jointEdges);
            ImGui::Text("Max move of original vertices: %.2f", hs.maxDisp);
            ImGui::Text("Neck gap (boundary vertices): %.3f%s", hs.neckGap, g_par.neck_pin ? " (pinned)" : "");
        }
    }
    ImGui::Separator();
    ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1), "red: boundary"); ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.4f, 1), "green: joint");
    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1), "yellow: crease"); ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.9f, 0.4f, 1.0f, 1), "magenta: UV seam");
    ImGui::TextColored(ImVec4(0.1f, 0.85f, 1.0f, 1), "cyan: texture feature (HEAD)"); ImGui::TextColored(ImVec4(1, 1, 1, 1), "white: neck edge (head meets body)");
    ImGui::PopItemWidth(); ImGui::End();
}
static void uiWarm(int W, int H) { mvui_begin(W, H); menuBar(); panel(H); ImGui::EndFrame(); }
static void uiFrame(int W, int H) { mvui_begin(W, H); menuBar(); panel(H); mvui_draw(); }

// ---------------------------------------------------------------- main window
static bool g_drag = false, g_pan = false; static int g_lx, g_ly;
static LRESULT CALLBACK wndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    mvui_wndproc(h, m, w, l);
    switch (m) {
    case WM_SIZE: g_cw = LOWORD(l) ? LOWORD(l) : 1; g_ch = HIWORD(l) ? HIWORD(l) : 1; return 0;
    case WM_LBUTTONDOWN: case WM_RBUTTONDOWN: case WM_MBUTTONDOWN:
        if (mvui_want_mouse() || (short)LOWORD(l) < g_panelW) return 0;   // the panel owns it
        g_drag = true; g_pan = m != WM_LBUTTONDOWN; g_lx = (short)LOWORD(l); g_ly = (short)HIWORD(l); SetCapture(h); SetFocus(h); return 0;
    case WM_LBUTTONUP: case WM_RBUTTONUP: case WM_MBUTTONUP: if (g_drag) { g_drag = false; ReleaseCapture(); } return 0;
    case WM_MOUSEMOVE:
        if (g_drag) {
            const int x = (short)LOWORD(l), y = (short)HIWORD(l), dx = x - g_lx, dy = y - g_ly; g_lx = x; g_ly = y;
            if (!g_pan) { g_yaw -= dx * 0.4f; g_pitch += dy * 0.4f; g_pitch = fminf(fmaxf(g_pitch, -89.0f), 89.0f); }
            else {   // move the target in the camera's plane
                const float yw = g_yaw * 0.0174533f, pt = g_pitch * 0.0174533f, k = g_dist * 0.0016f;
                const V3 right = { cosf(yw), 0, -sinf(yw) }, up = { -sinf(pt) * sinf(yw), cosf(pt), -sinf(pt) * cosf(yw) };
                g_tgt[0] += (-right.x * dx + up.x * dy) * k; g_tgt[1] += (-right.y * dx + up.y * dy) * k; g_tgt[2] += (-right.z * dx + up.z * dy) * k;
            }
        }
        return 0;
    case WM_MOUSEWHEEL: if (mvui_want_mouse()) return 0; g_dist = fminf(fmaxf(g_dist * powf(0.9f, (short)HIWORD(w) / 120.0f), 0.3f), 40.0f); return 0;
    case WM_KEYDOWN:
        if (w == 'R' || w == VK_HOME) resetCamera(); else if (w == VK_F12) g_shotReq = true; else if (w == VK_ESCAPE) PostQuitMessage(0);
        return 0;
    case WM_CLOSE: PostQuitMessage(0); return 0;
    }
    return DefWindowProcA(h, m, w, l);
}

int main(int argc, char** argv) {
    std::string shot, romPath, repo; bool selftest = false; int W = 1280, H = 720;
    for (int i = 1; i + 1 < argc; i++) if (!strcmp(argv[i], "--repo")) repo = argv[i + 1];
    for (int i = 1; i < argc; i++) if (!strcmp(argv[i], "--selftest")) selftest = true;
    if (selftest) { const int r1 = mv_tex_selftest(), r2 = mv_anim_selftest(), r3 = mv_head_selftest(); return r1 | r2 | r3; }
    if (!repo.empty() && !mv_anim_init(repo)) fprintf(stderr, "no animation data under %s/assets\n", repo.c_str());
    g_orig = mv_make_mannequin();
    {   // the mannequin must be closed and outward-facing: its signed volume is positive
        const std::vector<MvVec3> jw = mv_joint_worlds(g_orig); double vol = 0;
        for (size_t t = 0; t < g_orig.triCount(); t++) {
            const MvVec3 a = mv_vertex_world(g_orig, jw, g_orig.idx[3 * t]), b = mv_vertex_world(g_orig, jw, g_orig.idx[3 * t + 1]), c = mv_vertex_world(g_orig, jw, g_orig.idx[3 * t + 2]);
            vol += ((double)a.x * (b.y * c.z - b.z * c.y) + (double)a.y * (b.z * c.x - b.x * c.z) + (double)a.z * (b.x * c.y - b.y * c.x)) / 6.0;
        }
        fprintf(stderr, "mannequin: %d vertices, %d triangles, volume %.4f (must be > 0)\n", (int)g_orig.verts.size(), (int)g_orig.triCount(), vol);
    }
    // defaults, then the .ini, then the command line
    loadIni("modelview.ini");
    for (int i = 1; i < argc; i++) {
        const std::string a = argv[i]; auto nx = [&]() { return i + 1 < argc ? argv[++i] : (char*)"0"; };
        if (a == "--shot") shot = nx(); else if (a == "--w") W = atoi(nx()); else if (a == "--h") H = atoi(nx());
        else if (a == "--method") setKey("method", nx()); else if (a == "--passes") setKey("passes", nx()); else if (a == "--crease") setKey("crease", nx());
        else if (a == "--view") setKey("view", nx()); else if (a == "--wire") setKey("wire", nx()); else if (a == "--edges") setKey("edges", nx());
        else if (a == "--checker") setKey("checker", nx()); else if (a == "--normals") setKey("normals", nx()); else if (a == "--light") setKey("light", nx());
        else if (a == "--smooth") setKey("smooth", nx()); else if (a == "--vcol") setKey("vcol", nx()); else if (a == "--hilite") setKey("hilite", nx()); else if (a == "--texfeat") setKey("texfeat", nx()); else if (a == "--neckpin") setKey("neckpin", nx()); else if (a == "--taubin") setKey("taubin", nx()); else if (a == "--clean") setKey("clean", nx());
        else if (a == "--rom") romPath = nx(); else if (a == "--model") setKey("model", nx()); else if (a == "--tier") setKey("tier", nx());
        else if (a == "--repo") repo = nx(); else if (a == "--selftest") selftest = true;
        else if (a == "--anim") setKey("anim", nx()); else if (a == "--frame") setKey("frame", nx()); else if (a == "--texture") setKey("textures", nx());
        else if (a == "--clamp") setKey("pn_clamp", nx()); else if (a == "--splitjoint") setKey("split_joint_edges", nx());
    }

    if (!romPath.empty()) { std::string err; if (!mv_rom_open(romPath, err)) fprintf(stderr, "ROM: %s\n", err.c_str()); }
    if (mv_rom_ready() && !repo.empty()) {
        const std::string il = repo + "/imagelist." + mv_rom_region_letter() + ".csv";
        if (!mv_tex_set_imagelist(il)) fprintf(stderr, "no image list %s: models stay untextured\n", il.c_str());
    }
    if (!g_modelName.empty() && !loadModel(g_modelName)) g_modelName.clear();
    WNDCLASSA wc = {}; wc.lpfnWndProc = wndProc; wc.hInstance = GetModuleHandleA(0); wc.lpszClassName = "modelview"; wc.hCursor = LoadCursor(0, IDC_ARROW); RegisterClassA(&wc);
    RECT r = { 0, 0, W, H }; AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, TRUE);
    HWND hwnd = CreateWindowA("modelview", "modelview", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 40, 40, r.right - r.left, r.bottom - r.top, 0, 0, wc.hInstance, 0);

    HDC dc = GetDC(hwnd);
    PIXELFORMATDESCRIPTOR pf = {}; pf.nSize = sizeof pf; pf.nVersion = 1; pf.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER; pf.iPixelType = PFD_TYPE_RGBA; pf.cColorBits = 32; pf.cDepthBits = 24;
    SetPixelFormat(dc, ChoosePixelFormat(dc, &pf), &pf);
    HGLRC tmp = wglCreateContext(dc); wglMakeCurrent(dc, tmp);
    auto cca = (PFNWGLCREATECONTEXTATTRIBSARB)wglGetProcAddress("wglCreateContextAttribsARB");
    int at[] = { 0x2091, 3, 0x2092, 3, 0x9126, 1, 0 };   // GL 3.3 core
    HGLRC rc = cca ? cca(dc, 0, at) : 0; if (!rc) { fprintf(stderr, "no GL 3.3 core context\n"); return 1; }
    wglMakeCurrent(0, 0); wglDeleteContext(tmp); wglMakeCurrent(dc, rc);
#define X(T,N) N = (T)wglGetProcAddress(#N);
    GLFUNCS
#undef X
    auto swapi = (PFNWGLSWAPINTERVAL)wglGetProcAddress("wglSwapIntervalEXT"); if (swapi) swapi(shot.empty() ? 1 : 0);
    fprintf(stderr, "GL %s | %s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));

    g_pMesh = mv_program(kMeshVert, kMeshFrag, "mesh"); g_pLine = mv_program(kLineVert, kLineFrag, "line");
    if (!g_pMesh || !g_pLine) return 2;
    {   // 8x8-cell checker (64x64), cells tinted by position so a stretched or mirrored UV shows
        std::vector<uint8_t> px(64 * 64 * 4);
        for (int y = 0; y < 64; y++) for (int x = 0; x < 64; x++) {
            const bool d = ((x / 8) + (y / 8)) & 1; uint8_t* p = &px[(y * 64 + x) * 4];
            p[0] = d ? 60 : (uint8_t)(150 + x * 1.6); p[1] = d ? 60 : (uint8_t)(150 + y * 1.6); p[2] = d ? 60 : 200; p[3] = 255;
        }
        glGenTextures(1, &g_tChecker); glBindTexture(GL_TEXTURE_2D, g_tChecker);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    }
    mvui_init(); g_menuH = (int)ceilf(ImGui::GetIO().Fonts->Fonts[0]->LegacySize * ImGui::GetStyle().FontScaleMain + ImGui::GetStyle().FramePadding.y * 2);
    rebuild();

    if (!shot.empty()) {
        const bool ok = shotTo(shot, W, H);
        fprintf(stderr, "%s %s\n", ok ? "saved" : "FAILED to save", shot.c_str());
        return ok ? 0 : 1;
    }
    MSG msg; bool run = true;
    while (run) {
        while (PeekMessageA(&msg, 0, 0, 0, PM_REMOVE)) { if (msg.message == WM_QUIT) run = false; TranslateMessage(&msg); DispatchMessageA(&msg); }
        if (!run) break;
        {   // animation playback: 30 frames per second at speed 1
            static DWORD last = GetTickCount(); const DWORD now = GetTickCount(); const float dt = (now - last) / 1000.0f; last = now;
            if (g_play && g_animIdx >= 0 && g_animIdx < (int)mv_anim_list().size()) {
                const float n = (float)mv_anim_list()[(size_t)g_animIdx].frames;
                g_frame += dt * 30.0f * g_speed; if (n > 0 && g_frame >= n) g_frame = fmodf(g_frame, n);
                applyPose();
            }
        }
        rebuild();
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        renderScene(g_cw, g_ch);
        uiFrame(g_cw, g_ch);
        if (g_shotReq) {
            g_shotReq = false;
            char fn[64]; for (int n = 0; ; n++) { snprintf(fn, sizeof fn, "shot_%03d.png", n); if (GetFileAttributesA(fn) == INVALID_FILE_ATTRIBUTES) break; }
            fprintf(stderr, "%s %s\n", shotTo(fn, g_cw, g_ch) ? "saved" : "FAILED to save", fn);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }
        SwapBuffers(dc);
    }
    return 0;
}
