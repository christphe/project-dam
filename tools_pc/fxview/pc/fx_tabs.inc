// fxview tabs: muzzle flashes, tracers, impact bursts (blood / smoke / debris), explosions. Included by fxview.cpp
// (after the decal mode: it uses its camera, the ROM tables and the GL helpers).
//
// Each tab shows GAME (Video.Visuals Enhanced with F9 off: the game's own sprites) and PORT (F9 on: what the port
// draws instead, port/src/fxparticles.c, plus the dynamic lights of port/src/dynlights.c), side by side or alone.
// One fragment shader draws everything, with the uber shader's order of operations (uber.frag.glsl main): the
// combiner (texture x shade, or the explosions' TEXEL0 x TEXEL1 x SHADE), then dynLight, then fxShade with the
// PORTFX kind (the FXSHADE block and dynLight are read from the shader file and hot-reloaded). Blend per kind as
// fast3d sets it (gfx_pc.cpp: SPARK and TRACER additive = blend mode 2, everything else alpha over = mode 0).
// The scene state is a pure function of the tab's time (re-simulated from 0 each frame, 1/60 s ticks), so a
// --shot at --t SECONDS is reproducible.

enum { MODE_SPRITE = 0, MODE_DECAL = 1, MODE_MUZZLE, MODE_TRACER, MODE_BURST, MODE_EXPLO, MODE_COUNT };
static const char* kModeKey[MODE_COUNT] = { "sprite", "decal", "muzzle", "tracer", "burst", "explosion" };
enum { PFX_NONE = 0, PFX_MUZZLE = 1, PFX_TRACER = 2, PFX_SPARK = 3, PFX_BLOOD = 4 };   // port/include/portfx.h

// ---------------------------------------------------------------- the tab program
static GLuint g_pTab = 0; static std::string g_tabErr; static int g_tabReloads = 0;
static const char* kTabMain = R"(
uniform int uTexMode;    // 0 untextured (G_CC_SHADE), 1 TEXEL0 x SHADE (MODULATEIA), 2 TEXEL0 x TEXEL1 x SHADE (explosions)
uniform vec2 uUV1Scale;  // explosions: TEXEL1's uv = TEXEL0's uv x this (tile 1: shift 2, its own width)
uniform int uFx;         // the PORTFX kind given to fxShade (0 = none)
void main() {
    texVal0 = texture(uTex0, vTexCoord0);
    vec4 c = vC;
    if (uTexMode == 1) c = texVal0 * vC;
    else if (uTexMode == 2) c = texVal0 * texture(uTex1, vTexCoord0 * uUV1Scale) * vC;
    if (uLit != 0) c.rgb = min(dynLight(c.rgb, uTexMode != 0 ? texVal0.rgb : vec3(0.6)), vec3(1.0));
    if (uFx != 0) c = fxShade(c, uint(uFx), vTexCoord0);
    o = c;
}
)";
static void reloadTab(const std::string& src, bool readOk) {
    std::string shade, err;
    const std::string dyn = readOk ? extractDynLight(src) : "";
    if (!readOk) err = "cannot read " + g_shaderPath;
    else if (extractBlock(src, shade, err)) {
        const std::string fs = std::string(kDecalHead) + (dyn.empty() ? kFallbackDynLight : dyn.c_str()) + shade + kTabMain;
        GLuint p = link(kDecalVert, fs.c_str());
        if (p) { if (g_pTab) glDeleteProgram(g_pTab); g_pTab = p; g_tabErr = dyn.empty() ? "dynLight not found: no lights" : ""; g_tabReloads++; return; }
        err = "fxShade / dynLight did not compile (the previous version stays):\n" + g_shaderLog;
    }
    g_tabErr = err; fprintf(stderr, "%s\n", err.c_str());
    if (!g_pTab) g_pTab = link(kDecalVert, (std::string(kDecalHead) + kFallbackDynLight + kFallbackBlock + kTabMain).c_str());
}

// ---------------------------------------------------------------- shared tab state
static float g_tTime = 0, g_tSpeed = 1.0f; static bool g_tPlay = true, g_tLoop = true;
static int g_tShow = 2;                         // 0 game, 1 port, 2 side by side
static float g_tYaw = 0, g_tPitch = 0, g_tZoom = 1.0f;
static bool g_tWall = true;                     // a lit wall behind (the lights show on it)
static int g_tWallTex = 286;                    // stone (assets/images.def)
static unsigned g_tSeed = 1;

struct Light { float pos[3], r, col[3]; };   // world units (renderScale 1)
struct TabScene { std::vector<V3> v; std::vector<uint16_t> idx; };

static unsigned g_tr = 1;                       // xorshift, as fxparticles.c frand
static float tfrand() { g_tr ^= g_tr << 13; g_tr ^= g_tr >> 17; g_tr ^= g_tr << 5; return (float)(g_tr & 0xFFFFFF) / (float)0x1000000; }
static float tfrange(float a, float b) { return a + (b - a) * tfrand(); }
static void tnorm(float v[3]) { const float l = v[0] * v[0] + v[1] * v[1] + v[2] * v[2]; if (l > 1e-8f) { const float k = 1.0f / sqrtf(l); v[0] *= k; v[1] *= k; v[2] *= k; } }

static Cam tabCam(const float tgt[3], float dist, float aspect) {
    Cam c; const float y = g_tYaw * 0.0174533f, p = g_tPitch * 0.0174533f, d = dist / fmaxf(g_tZoom, 0.05f);
    c.eye[0] = tgt[0] + d * cosf(p) * sinf(y); c.eye[1] = tgt[1] + d * sinf(p); c.eye[2] = tgt[2] + d * cosf(p) * cosf(y);
    float f[3] = { tgt[0] - c.eye[0], tgt[1] - c.eye[1], tgt[2] - c.eye[2] }; tnorm(f);
    for (int i = 0; i < 3; i++) c.f[i] = f[i];
    c.r[0] = -c.f[2]; c.r[1] = 0; c.r[2] = c.f[0];
    { const float l = 1.0f / sqrtf(c.r[0] * c.r[0] + c.r[2] * c.r[2]); c.r[0] *= l; c.r[2] *= l; }
    c.u[0] = c.r[1] * c.f[2] - c.r[2] * c.f[1]; c.u[1] = c.r[2] * c.f[0] - c.r[0] * c.f[2]; c.u[2] = c.r[0] * c.f[1] - c.r[1] * c.f[0];
    float* m = c.view; memset(m, 0, sizeof c.view);
    for (int i = 0; i < 3; i++) { m[i * 4 + 0] = c.r[i]; m[i * 4 + 1] = c.u[i]; m[i * 4 + 2] = -c.f[i]; }
    m[12] = -(c.r[0] * c.eye[0] + c.r[1] * c.eye[1] + c.r[2] * c.eye[2]);
    m[13] = -(c.u[0] * c.eye[0] + c.u[1] * c.eye[1] + c.u[2] * c.eye[2]);
    m[14] = (c.f[0] * c.eye[0] + c.f[1] * c.eye[1] + c.f[2] * c.eye[2]);
    m[15] = 1;
    c.tanh = tanf(20.0f * 0.0174533f); c.aspect = aspect;
    const float n = 1.0f, fa = 50000.0f; float* P = c.proj; memset(P, 0, sizeof c.proj);
    P[0] = 1.0f / (c.tanh * aspect); P[5] = 1.0f / c.tanh; P[10] = -(fa + n) / (fa - n); P[11] = -1; P[14] = -2 * fa * n / (fa - n);
    return c;
}

// uniforms for one pane: camera and the lights (port side only: the game has none)
static void tabUniforms(const Cam& c, const std::vector<Light>& L, bool lit) {
    GLuint p = g_pTab; glUseProgram(p);
    glUniformMatrix4fv(glGetUniformLocation(p, "uView"), 1, GL_FALSE, c.view);
    glUniformMatrix4fv(glGetUniformLocation(p, "uProj"), 1, GL_FALSE, c.proj);
    glUniform1i(glGetUniformLocation(p, "uTex0"), 0); glUniform1i(glGetUniformLocation(p, "uTex1"), 1);
    const int n = lit ? std::min((int)L.size(), 16) : 0;
    glUniform4i(glGetUniformLocation(p, "lcount"), n, 0, 0, 0);
    float lp[16 * 4] = {}, lc[16 * 4] = {};
    for (int i = 0; i < n; i++) { toEye(c, L[(size_t)i].pos, &lp[i * 4], false); lp[i * 4 + 3] = L[(size_t)i].r; for (int k = 0; k < 3; k++) lc[i * 4 + k] = L[(size_t)i].col[k]; }
    glUniform4fv(glGetUniformLocation(p, "lpos"), 16, lp); glUniform4fv(glGetUniformLocation(p, "lcol"), 16, lc);
    glUniform4f(glGetUniformLocation(p, "sun_dir"), 0, 1, 0, 0); glUniform4f(glGetUniformLocation(p, "sun_col"), 1, 1, 1, 0);
    glUniform1ui(glGetUniformLocation(p, "flags"), 0u); glUniform1i(glGetUniformLocation(p, "fog_pixel"), 0);
    glUniform1i(glGetUniformLocation(p, "frame_count"), g_fc);
    glUniform1i(glGetUniformLocation(p, "uLit"), n > 0 ? 1 : 0);
}
// one draw: texture mode, fx kind, blend (0 alpha over, 2 additive: fast3d's modes)
static void tabDraw(const std::vector<V3>& v, const std::vector<uint16_t>& idx, int texMode, int fx, int blend) {
    if (v.empty() || idx.empty()) return;
    glUniform1i(glGetUniformLocation(g_pTab, "uTexMode"), texMode);
    glUniform1i(glGetUniformLocation(g_pTab, "uFx"), fx);
    glEnable(GL_BLEND);
    if (blend == 2) glBlendFunc(GL_SRC_ALPHA, GL_ONE); else glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
    drawV3(v, idx);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
}
static void quadIdx(std::vector<uint16_t>& idx, uint16_t b) { for (uint16_t i : { 0, 1, 2, 0, 2, 3 }) idx.push_back((uint16_t)(b + i)); }

// the backdrop wall: the plane through `o` with normal +z (or +y for a floor), the room's baked shade 0.6, lit on the port side
static void tabWall(const float o[3], float half, bool floor) {
    const GlTex& t = texture((uint32_t)g_tWallTex, 0, 0); bindTex(t, true);
    const float tw = (float)(t.w > 0 ? t.w : 32) * 2.0f, th = (float)(t.h > 0 ? t.h : 32) * 2.0f, s = 0.6f;   // 2 world units per texel
    std::vector<V3> q;
    const float c[4][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
    for (int k = 0; k < 4; k++) {
        const float a = c[k][0] * half, b = c[k][1] * half;
        V3 p = floor ? V3{ o[0] + a, o[1], o[2] + b, (o[0] + a) / tw, (o[2] + b) / th, s, s, s, 1 } : V3{ o[0] + a, o[1] + b, o[2], (o[0] + a) / tw, -(o[1] + b) / th, s, s, s, 1 };
        q.push_back(p);
    }
    glDisable(GL_BLEND);
    glUniform1i(glGetUniformLocation(g_pTab, "uTexMode"), 1); glUniform1i(glGetUniformLocation(g_pTab, "uFx"), 0);
    drawV3(q, { 0, 1, 2, 0, 2, 3 });
}

// ---------------------------------------------------------------- muzzle flashes (model.c GUNFIRE node, dynlights.c)
// The guards' held weapons (Pchr*Z model files) carry a GUNFIRE node (opcode 12): Offset, Size and a texture config.
// They are read from the ROM's file table (scripts/filelist.<region>.csv): nothing is stored.
struct GunFlash { std::string name; float off[3], size[3]; uint32_t tex = 0; int w = 0, h = 0, fs = 0, ft = 0; };
static std::vector<GunFlash> g_guns; static bool g_gunsScanned = false; static std::string g_gunNote, g_repo, g_gunWant;
static int g_gun = 0;
static float rdF(const uint8_t* p) { uint32_t u = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; float f; memcpy(&f, &u, 4); return f; }
static uint32_t rdU(const uint8_t* p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
static void scanGuns() {
    if (g_gunsScanned) return;
    g_gunsScanned = true;
    if (g_rom.empty()) { g_gunNote = "no ROM"; return; }
    std::string csv; if (!readFile(g_repo + "/scripts/filelist." + g_region + ".csv", csv)) { g_gunNote = "no scripts/filelist.<region>.csv"; return; }
    size_t p = 0;
    while (p < csv.size()) {
        size_t e = csv.find('\n', p); if (e == std::string::npos) e = csv.size();
        const std::string line = csv.substr(p, e - p); p = e + 1;
        const size_t c1 = line.find(','), c2 = c1 == std::string::npos ? c1 : line.find(',', c1 + 1);
        if (c2 == std::string::npos) continue;
        const std::string path = line.substr(c2 + 1, line.find(',', c2 + 1) - c2 - 1);
        const size_t sl = path.find_last_of('/'); std::string name = path.substr(sl == std::string::npos ? 0 : sl + 1);
        if (name.size() > 4 && name.compare(name.size() - 4, 4, ".bin") == 0) name.resize(name.size() - 4);
        if (name.compare(0, 4, "Pchr") != 0) continue;
        const size_t a = (size_t)strtoull(line.c_str(), nullptr, 10), n = (size_t)strtoull(line.c_str() + c1 + 1, nullptr, 10);
        if (a + n > g_rom.size() || n < 4) continue;
        std::vector<uint8_t> d; if (!mv_inflate_raw(&g_rom[a + 2], n - 2, d)) continue;   // the 1172 header, then raw deflate
        const size_t D = d.size();
        for (size_t o = 0; o + 0x18 <= D; o += 4) {   // a node: u16 opcode, pad, then the record (segment 5 = the file)
            if (d[o] != 0 || d[o + 1] != 12 || d[o + 2] || d[o + 3]) continue;
            const uint32_t r = rdU(&d[o + 4]); if (r >> 24 != 5 || (r & 0xFFFFFF) + 0x28 > D) continue;
            const uint8_t* R = &d[r & 0xFFFFFF];
            GunFlash g; g.name = name.substr(4, name.size() - 5);
            for (int k = 0; k < 3; k++) { g.off[k] = rdF(R + 4 * k); g.size[k] = rdF(R + 12 + 4 * k); }
            const uint32_t img = rdU(R + 0x18);
            if (img >> 24 == 5 && (img & 0xFFFFFF) + 12 <= D) { const uint8_t* T = &d[img & 0xFFFFFF]; g.tex = rdU(T); g.w = T[4]; g.h = T[5]; g.fs = T[9]; g.ft = T[10]; }
            bool ok = g.size[0] > 0 && g.tex > 0; for (int k = 0; k < 3; k++) ok = ok && fabsf(g.off[k]) < 1e4f && fabsf(g.size[k]) < 1e4f;
            if (ok) { g_guns.push_back(g); break; }
        }
    }
    char b[96]; snprintf(b, sizeof b, "%d guards' weapons with a GUNFIRE node", (int)g_guns.size()); g_gunNote = b;
    for (int i = 0; i < (int)g_guns.size(); i++) if (g_guns[(size_t)i].name == g_gunWant) g_gun = i;
}
static float g_mRate = 6.0f, g_mScale = 0.1f; static int g_mVis = 2;   // shots / s, model units -> world, ticks shown
#define MUZZLE_R 2.20f
#define MUZZLE_G 1.50f
#define MUZZLE_B 0.70f
#define MUZZLE_RANGE 260.0f
static void sceneMuzzle(const Cam& c, bool port, int pw, int ph) {
    (void)pw; (void)ph;
    scanGuns();
    if (g_guns.empty()) return;
    const GunFlash& G = g_guns[(size_t)std::max(0, std::min((int)g_guns.size() - 1, g_gun))];
    const int tick = (int)floorf(g_tTime * 60.0f), period = std::max(1, (int)lroundf(60.0f / fmaxf(g_mRate, 0.5f)));
    const bool visible = tick % period < g_mVis;
    std::vector<Light> L;
    const float k = g_mScale;
    const float centre[3] = { (G.off[0] - G.size[0] * 0.5f) * k, G.off[1] * k, G.off[2] * k };
    if (port && visible) L.push_back({ { centre[0], centre[1], centre[2] }, MUZZLE_RANGE, { MUZZLE_R, MUZZLE_G, MUZZLE_B } });   // dynlights.c addMuzzle
    tabUniforms(c, L, port);
    if (g_tWall) { const float o[3] = { 0, 0, -60 }; tabWall(o, 1200, false); }
    {   // the barrel, a dark bar from the muzzle back (orientation only, the gun model is not drawn)
        std::vector<V3> v; const float x0 = G.off[0] * k, x1 = x0 + 120 * k * 2, y = G.off[1] * k, z = G.off[2] * k, hh = 12 * k;
        v = { { x0, y - hh, z, 0, 0, .12f, .12f, .13f, 1 }, { x1, y - hh, z, 0, 0, .12f, .12f, .13f, 1 }, { x1, y + hh, z, 0, 0, .12f, .12f, .13f, 1 }, { x0, y + hh, z, 0, 0, .12f, .12f, .13f, 1 } };
        glBindTexture(GL_TEXTURE_2D, g_white); tabDraw(v, { 0, 1, 2, 0, 2, 3 }, 0, 0, 0);
    }
    if (!visible) return;
    // model.c GUNFIRE: the eye direction in the model's frame (the model frame = world here, no rotation)
    float e[3] = { c.eye[0] - centre[0], c.eye[1] - centre[1], c.eye[2] - centre[2] }; tnorm(e);
    const float spec = acosf(fmaxf(-1.0f, fminf(1.0f, e[1])));
    float spf0 = acosf(fmaxf(-1.0f, fminf(1.0f, -e[2] / fmaxf(sinf(spec), 1e-5f))));
    if (-e[0] < 0) spf0 = 6.2831853f - spf0;
    const float spdc = cosf(spf0), spd8 = sinf(spf0), spd4 = cosf(spec), spd0 = sinf(spec);
    unsigned rs = (unsigned)tick * 2654435761u + g_tSeed;   // randomGetNext per drawn frame: the scale and the texture's turn
    const float scale = 0.75f + (lcg(rs) % 128) * (1.0f / 256.0f);
    const float S[3] = { G.size[0] * scale, G.size[1] * scale, G.size[2] * scale };
    const float spcc = S[0] * spdc * 0.5f, spc8 = S[2] * spd8 * 0.5f, spc4 = S[1] * spd0 * 0.5f, spc0 = S[0] * spd4 * spd8 * 0.5f, spbc = S[2] * spd4 * spdc * 0.5f;
    const float p90[3] = { G.off[0] - S[0] * 0.5f, G.off[1], G.off[2] };
    const float P[4][3] = { { p90[0] - spcc - spc0, p90[1] - spc4, p90[2] + spc8 - spbc }, { p90[0] - spcc + spc0, p90[1] + spc4, p90[2] + spc8 + spbc },
                            { p90[0] + spcc + spc0, p90[1] + spc4, p90[2] - spc8 + spbc }, { p90[0] + spcc - spc0, p90[1] - spc4, p90[2] - spc8 - spbc } };
    const float ang = (float)((lcg(rs) * 1024) & 0xFFFF) / 65536.0f * 6.2831853f;
    const float cs = cosf(ang) * 0.70703f, sn = sinf(ang) * 0.70703f;   // (coss * width * 0xb5) >> 18, around the centre
    const float uv[4][2] = { { 0.5f - cs, 0.5f - sn }, { 0.5f + sn, 0.5f - cs }, { 0.5f + cs, 0.5f + sn }, { 0.5f - sn, 0.5f + cs } };
    std::vector<V3> v;
    for (int i = 0; i < 4; i++) v.push_back({ P[i][0] * k, P[i][1] * k, P[i][2] * k, uv[i][0], uv[i][1], 1, 1, 1, 1 });   // D_800363E0: white
    const GlTex& t = texture(G.tex, G.fs & 3, G.ft & 3); bindTex(t, false);
    // texSelect mode 4: G_RM_ZB_CLD_SURF (alpha over), MODULATEIA. fast3d keeps the muzzle's look (no fxShade, gfx_pc.cpp)
    tabDraw(v, { 0, 1, 2, 2, 3, 0 }, 1, 0, 0);
}

// ---------------------------------------------------------------- tracers (gunfire.c beam, fxparticles.c onShot)
static int g_trItem = 0; static float g_trRange = 1500.0f, g_trPeriod = 0.25f;   // 0 gun, 1 laser, 2 watch laser
static void streakQuad(const float h[3], const float d[3], float len, float s, const Cam& c, const unsigned char rgba[4], int ah, int at, std::vector<V3>& v, std::vector<uint16_t>& idx) {
    // fxparticles.c drawParticle FXP_STREAK: the width across the streak, facing the camera
    const float vd[3] = { -c.f[0], -c.f[1], -c.f[2] };
    float x[3] = { d[1] * vd[2] - d[2] * vd[1], d[2] * vd[0] - d[0] * vd[2], d[0] * vd[1] - d[1] * vd[0] }; tnorm(x);
    const float t[3] = { h[0] - d[0] * len, h[1] - d[1] * len, h[2] - d[2] * len };
    float q[6][3];
    for (int k = 0; k < 3; k++) { q[0][k] = h[k]; q[1][k] = h[k] + x[k] * s; q[2][k] = h[k] - x[k] * s; q[3][k] = t[k]; q[4][k] = t[k] + x[k] * s; q[5][k] = t[k] - x[k] * s; }
    const float al[6] = { (float)ah, 0, 0, (float)at, 0, 0 };
    const uint16_t b = (uint16_t)v.size();
    for (int k = 0; k < 6; k++) v.push_back({ q[k][0], q[k][1], q[k][2], 0, 0, rgba[0] / 255.0f, rgba[1] / 255.0f, rgba[2] / 255.0f, std::max(0.0f, std::min(255.0f, al[k])) / 255.0f });
    for (uint16_t i : { 1, 0, 3, 1, 3, 4, 0, 2, 5, 0, 5, 3 }) idx.push_back((uint16_t)(b + i));
}
static void sceneTracer(const Cam& c, bool port) {
    tabUniforms(c, {}, port);
    if (g_tWall) { const float o[3] = { 0, 0, -400 }; tabWall(o, 6000, false); }
    const float from[3] = { -g_trRange * 0.5f, 0, 0 }, dvec[3] = { 1, 0, 0 };
    const float tt = fmodf(g_tTime, fmaxf(g_trPeriod, 0.05f));
    if (!port) {   // gunfire.c CapBeamLengthAndDecideIfRendered + gunAdvanceBeamTimer (once per 1/60 s frame) + sub_GAME_7F061E18
        unsigned rs = g_tSeed * 7919u + (unsigned)floorf(g_tTime / fmaxf(g_trPeriod, 0.05f));
        const float range = g_trRange; float r = fmaxf(range, 500.0f), u20, u24, u28;
        const float rnd = (lcg(rs) % 32768) / 32768.0f;
        if (g_trItem == 1) { u20 = 0.25f * r; u24 = fminf(0.6f * r, 3000.0f); u28 = (-0.1f - rnd * 0.3f) * r; }
        else if (g_trItem == 2) { r = fminf(range, 300.0f); r = fmaxf(r, 500.0f); u24 = r; u20 = 2.0f * r; if (r > 3000) u24 = 3000; u28 = 0; }
        else { u20 = 0.2f * r; u24 = u20 > 3000 ? 3000 : u20; u28 = (2.0f * rnd - 1.0f) * u20; }
        const float u1c = g_trItem == 2 ? fminf(range, 300.0f) : fminf(range, 10000.0f);
        const int frames = (int)floorf(tt * 60.0f);
        for (int f = 0; f < frames; f++) { u28 += u20; if (u1c <= u28) return; }
        if (u1c <= u28) return;
        float start = u28, dist = u24;
        if (start <= 0) { dist += start; start = 0; }
        if (u1c < start + dist) dist = u1c - start;
        if (dist <= 0) return;
        const float radius = (g_trItem == 0 ? 30.0f : g_trItem == 1 ? 50.0f : 10.0f) * 0.1f;   // the matrix scales by 0.1
        const float pos[3] = { from[0] + start, from[1], from[2] }, end[3] = { pos[0] + dist, pos[1], pos[2] };
        float rt[3] = { dvec[1] * (c.eye[2] - end[2]) - dvec[2] * (c.eye[1] - end[1]), dvec[2] * (c.eye[0] - end[0]) - dvec[0] * (c.eye[2] - end[2]), dvec[0] * (c.eye[1] - end[1]) - dvec[1] * (c.eye[0] - end[0]) };
        tnorm(rt); for (float& x : rt) x *= radius;
        const float a = g_trItem == 2 ? 175.0f / 255.0f : 1.0f;
        std::vector<V3> v = { { pos[0] + rt[0], pos[1] + rt[1], pos[2] + rt[2], 1, 0, 1, 1, 1, a }, { pos[0] - rt[0], pos[1] - rt[1], pos[2] - rt[2], 0, 0, 1, 1, 1, a },
                              { end[0] + .9f * rt[0], end[1] + .9f * rt[1], end[2] + .9f * rt[2], 1, 1, 1, 1, 1, a }, { end[0] - .9f * rt[0], end[1] - .9f * rt[1], end[2] - .9f * rt[2], 0, 1, 1, 1, 1, a } };
        const ImgEntry* im = tableEntry(g_trItem == 0 ? "flareimage3" : "flareimage4", 0);
        const GlTex& t = im ? texture(im->index, im->flagsS, im->flagsT) : GlTex(); bindTex(t, false);
        tabDraw(v, { 0, 2, 3, 0, 3, 1 }, 1, 0, 0);   // G_RM_ZB_CLD_SURF: alpha over; F9 off: no fxShade
        return;
    }
    // fxparticles.c onShot: a streak from 30 ahead of the muzzle at 9000 / s, its tail never behind the muzzle, + its halo
    const float dist = g_trRange; if (dist < 31.0f) return;
    const float life = (dist - 30.0f) / 9000.0f;
    if (tt >= life) return;
    const float h[3] = { from[0] + 30.0f + 9000.0f * tt, from[1], from[2] };
    float len = fmaxf(9000.0f * 0.03f, 60.0f); len = fminf(len, h[0] - from[0]);
    const unsigned char col[4] = { 255, 214, 150, 255 };
    std::vector<V3> v; std::vector<uint16_t> idx;
    for (int pass = 0; pass < 2; pass++) {   // the halo (size x 3.5, a / 4) first, then the core
        const float s = 1.4f * (pass == 0 ? 3.5f : 1.0f); int a = pass == 0 ? 255 / 4 : 255;
        const float t[3] = { h[0] - len, h[1], h[2] };
        const float dh = sqrtf((h[0] - c.eye[0]) * (h[0] - c.eye[0]) + (h[1] - c.eye[1]) * (h[1] - c.eye[1]) + (h[2] - c.eye[2]) * (h[2] - c.eye[2]));
        const float dt = sqrtf((t[0] - c.eye[0]) * (t[0] - c.eye[0]) + (t[1] - c.eye[1]) * (t[1] - c.eye[1]) + (t[2] - c.eye[2]) * (t[2] - c.eye[2]));
        const float kh = fmaxf(0, fminf(1, (dh - 40.0f) / 100.0f)), kt = fmaxf(0, fminf(1, (dt - 40.0f) / 100.0f));   // NEAR_FADE0 / 1
        streakQuad(h, dvec, len, s, c, col, (int)(a * kh), (int)((a / 3) * kt), v, idx);
    }
    glBindTexture(GL_TEXTURE_2D, g_white);
    tabDraw(v, idx, 0, PFX_TRACER, 2);   // G_CC_SHADE, fxShade TRACER, additive (gfx_pc.cpp use_modulate = 2)
}

// ---------------------------------------------------------------- impact bursts (fxparticles.c onImpact / onPuff, glass2.c puffs)
enum { BM_METAL = 0, BM_WOOD, BM_GLASS, BM_WATER, BM_SNOW, BM_DIRT, BM_STONE, BM_BLOOD, BM_COUNT };
static const char* kBurstKey[BM_COUNT] = { "metal", "wood", "glass", "water", "snow", "dirt", "stone", "blood" };
static const char* kBurstName[BM_COUNT] = { "Metal (sparks + dust)", "Wood (splinters + dust)", "Glass (shards)", "Water (splash)", "Snow (spray)",
                                            "Dirt / mud (clods + dust)", "Stone / tile / default (chips, sparks, dust)", "Blood (body hit, puff type 2)" };
static int g_burst = BM_STONE; static float g_bPeriod = 1.2f;
// The recipes are the game's own (port/include/fxburst.h, shared with port/src/fxparticles.c): tune them here.
#include "../../../port/include/fxburst.h"
#define FXP_STREAK FXB_STREAK
#define FXP_DISC FXB_DISC
#define FXP_SOLID FXB_SOLID
typedef FxbPart TPart;
static FxbPart* tAlloc(void* ctx) {
    auto* P = (std::vector<TPart>*)ctx;
    if (P->size() >= 384) return nullptr;   // FXP_MAX
    P->push_back(TPart{});
    return &P->back();
}
static const int kBurstMat[BM_COUNT] = { PMAT_METAL, PMAT_WOOD, PMAT_GLASS, PMAT_WATER, PMAT_SNOW, PMAT_DIRT, PMAT_STONE, PMAT_CHR };
static void burstEmit(std::vector<TPart>& P, int m, const float* pos, const float* N) {
    P.reserve(512);   // the allocator hands out pointers into P: no reallocation while a recipe runs
    const FxbEmit em = { tAlloc, &P, &g_tr };
    if (m == BM_BLOOD) fxb_blood(&em, pos, N);
    else fxb_impact(&em, kBurstMat[m], pos, N);
}
// fxparticles.c drawParticle: streak (6 vertices) or disc / solid (a hexagon, a solid keeps its alpha to the rim)
static void partGeom(const TPart& p, const Cam& c, std::vector<V3>& v, std::vector<uint16_t>& idx) {
    const float fade = p.life / p.maxlife; const int a = (int)(p.rgba[3] * fade);
    if (p.shape == FXP_STREAK || p.shape == FXB_SHARD) {
        float d[3] = { p.vel[0], p.vel[1], p.vel[2] }; const float sp = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]); tnorm(d);
        float len = sp * p.stretch; if (len < p.minlen) len = p.minlen;
        const size_t b0 = v.size();
        streakQuad(p.pos, d, len, p.size, c, p.rgba, a, (int)(a * p.tail), v, idx);
        if (p.shape == FXB_SHARD) {   // fxparticles.c: a crisp rim, a pointed tail
            for (int k : { 1, 2 }) v[b0 + k].a = v[b0].a;
            for (int k : { 4, 5 }) {
                v[b0 + k].a = v[b0 + 3].a;
                for (int j = 0; j < 3; j++) (&v[b0 + k].x)[j] = (&v[b0 + 3].x)[j] + ((&v[b0 + k].x)[j] - (&v[b0 + 3].x)[j]) * 0.15f;
            }
        }   // the near fade is a no-op at these distances
        return;
    }
    const uint16_t b = (uint16_t)v.size(); const float rim = p.shape == FXP_SOLID ? a : 0;
    v.push_back({ p.pos[0], p.pos[1], p.pos[2], 0, 0, p.rgba[0] / 255.0f, p.rgba[1] / 255.0f, p.rgba[2] / 255.0f, a / 255.0f });
    for (int i = 0; i < 6; i++) {
        const float ang = i * (6.2831853f / 6.0f), cc = cosf(ang) * p.size, sn = sinf(ang) * p.size;
        v.push_back({ p.pos[0] + c.r[0] * cc + c.u[0] * sn, p.pos[1] + c.r[1] * cc + c.u[1] * sn, p.pos[2] + c.r[2] * cc + c.u[2] * sn, 0, 0,
                      p.rgba[0] / 255.0f, p.rgba[1] / 255.0f, p.rgba[2] / 255.0f, rim / 255.0f });
    }
    for (int i = 0; i < 6; i++) for (int k : { 0, 1 + i, 1 + (i + 1) % 6 }) idx.push_back((uint16_t)(b + k));
}
static void drawParts(const std::vector<TPart>& P, const Cam& c) {
    glBindTexture(GL_TEXTURE_2D, g_white);
    for (const TPart& p : P) {   // in array order like fxpDraw; the kind decides fxShade and the blend
        std::vector<V3> v; std::vector<uint16_t> idx; partGeom(p, c, v, idx);
        tabDraw(v, idx, 0, p.kind, (p.kind == PFX_SPARK || p.kind == PFX_TRACER) ? 2 : 0);
    }
}
static void stepParts(std::vector<TPart>& P, float dt) {   // fxparticles.c fxpStep
    size_t w = 0;
    for (size_t i = 0; i < P.size(); i++) {
        TPart p = P[i]; if (!fxb_step(&p, dt)) continue; P[w++] = p;
    }
    P.resize(w);
}
// glass2.c bullet_spark_render: a diamond facing the camera, frame = (s32)(age * unk08), lifetime 11 (smoke) ticks
static void gamePuff(const Cam& c, const float pos[3], int ticks, bool blood) {
    if (ticks >= 11) return;
    const ImgEntry* im = tableEntry("explosion_smokeimages", puffFrame((float)ticks, 0.5f, (int)(g_tables.count("explosion_smokeimages") ? g_tables["explosion_smokeimages"].size() : 1)));
    unsigned rs = g_tSeed * 31u + 5u;
    const float ang = (lcg(rs) % 32768) / 32768.0f * 6.2831853f, sz = 26.0f * (1.0f + (lcg(rs) % 32768) / 32768.0f * 0.25f) * 1.41421f;   // bullet_spark_create(pos, 1, 26.0f, room)
    const float cs = cosf(ang) * sz, sn = sinf(ang) * sz;
    const float p2[4][2] = { { -cs, -sn }, { sn, -cs }, { cs, sn }, { -sn, cs } }, uv[4][2] = { { 1, 0 }, { 0, 0 }, { 0, 1 }, { 1, 1 } };
    float col[3] = { 1, 1, 0xC8 / 255.0f };   // g_BulletSparkColors[1]
    if (blood) for (int k = 0; k < 3; k++) col[k] = g_blood[k] / 255.0f;
    std::vector<V3> v;
    for (int i = 0; i < 4; i++) v.push_back({ pos[0] + c.r[0] * p2[i][0] + c.u[0] * p2[i][1], pos[1] + c.r[1] * p2[i][0] + c.u[1] * p2[i][1], pos[2] + c.r[2] * p2[i][0] + c.u[2] * p2[i][1],
                                              uv[i][0], uv[i][1], col[0], col[1], col[2], 1 });
    const GlTex& t = im ? texture(im->index, im->flagsS, im->flagsT) : GlTex(); bindTex(t, false);
    tabDraw(v, { 0, 1, 2, 0, 2, 3 }, 1, 0, 0);
}
static void sceneBurst(const Cam& c, bool port) {
    tabUniforms(c, {}, port);
    const float pos[3] = { 0, 0, 0 }, N[3] = { 0, 0, 1 };
    if (g_tWall && g_burst != BM_BLOOD) { const float o[3] = { 0, 0, 0 }; tabWall(o, 1500, false); }
    if (g_tWall && g_burst == BM_BLOOD) { const float o[3] = { 0, 0, -300 }; tabWall(o, 3000, false); }
    const float tt = fmodf(g_tTime, fmaxf(g_bPeriod, 0.1f));
    const int ticks = (int)floorf(tt * 60.0f);
    if (!port) { const float pp[3] = { 0, 0, 0.5f }; gamePuff(c, pp, ticks, g_burst == BM_BLOOD); return; }
    g_tr = 0x2545F491u ^ (g_tSeed * 0x9E3779B9u);
    std::vector<TPart> P;
    float n[3] = { N[0], N[1], N[2] };
    if (g_burst == BM_BLOOD) { n[0] = 1; n[1] = 0.25f; n[2] = 0; tnorm(n); }   // onPuff: along the shot (from the left here), plus a little up
    burstEmit(P, g_burst, pos, n);
    for (int k = 0; k < ticks; k++) stepParts(P, 1.0f / 60.0f);
    drawParts(P, c);
}

// ---------------------------------------------------------------- explosions (explosion.c explosionTick / explosionRenderPart, dynlights.c)
struct ExploType { float hrange, vrange, hchange, vchange, size, range, dmgrange; int duration, proprate; float flarespd; };
static const ExploType kExplo[] = {   // g_ExplosionTypes (NTSC): hrange .. flareanimspeed
    { 0.1f, 0.1f, 0, 0, 0.1f, 0, 0, 1, 1, 1.0f }, { 1, 1, 0, 0, 1, 0, 0, 30, 1, 1.0f }, { 20, 20, 0, 0, 30, 50, 50, 80, 1, 3.0f }, { 50, 50, 0, 0, 50, 100, 100, 90, 1, 4.0f },
    { 60, 80, 1.0f, 0.3f, 100, 150, 280, 120, 2, 5.0f }, { 60, 120, 1.0f, 0.3f, 150, 200, 310, 120, 2, 5.0f }, { 20, 20, 0, 0, 22, 40, 40, 80, 1, 3.0f }, { 35, 40, 0, 0, 35, 70, 70, 90, 1, 4.0f },
    { 50, 80, 1.0f, 0.3f, 50, 100, 220, 120, 2, 5.0f }, { 60, 120, 1.0f, 0.3f, 50, 130, 230, 120, 2, 5.0f }, { 40, 40, 0.4f, 0.2f, 70, 100, 180, 170, 4, 5.0f },
    { 50, 50, 0.6f, 0.4f, 100, 150, 260, 180, 1, 4.0f }, { 70, 60, 1.0f, 0.6f, 150, 225, 320, 180, 2, 5.0f }, { 80, 60, 2.0f, 0.7f, 200, 300, 480, 180, 2, 5.0f },
    { 50, 50, 0, 0, 120, 200, 400, 150, 4, 4.0f }, { 1, 1, 0, 0, 1, 0, 0, 1, 1, 1.0f }, { 1, 1, 0, 0, 1, 0, 0, 1, 1, 1.0f }, { 80, 60, 15.0f, 5.0f, 1500, 2200, 3600, 300, 1, 2.0f },
    { 80, 60, 3.0f, 1.0f, 300, 450, 640, 60, 1, 2.0f }, { 90, 75, 2.5f, 0.87f, 250, 375, 600, 180, 2, 5.0f }, { 160, 120, 6.0f, 2.0f, 600, 450, 640, 60, 1, 2.0f },
};
static int g_explo = 13;   // 13: grenades and mines
static float g_eTail = 0.5f;
static const int kExploFrameImg[15] = { 0, 2, 7, 8, 9, 10, 11, 12, 13, 14, 1, 3, 4, 5, 6 };   // g_ExplosionDisplayLists[i] -> SMOKE_n / FIRE_n (assets/oddtextures.c)
static int g_smoke0 = -1;   // the texture number of SMOKE_0 (images.def; FIRE_n = SMOKE_n + 1)
static const ExploType& curExplo() { return kExplo[std::max(0, std::min((int)(sizeof kExplo / sizeof *kExplo) - 1, g_explo))]; }
static float exploLife() { const ExploType& T = curExplo(); return (T.duration + 16.0f * T.flarespd) / 60.0f + g_eTail; }
struct EPart { int frame; float pos[3], size, rot; };
static void sceneExplo(const Cam& c, bool port) {
    if (g_smoke0 < 0) { for (int i = 0; i < (int)g_texInfo.size(); i++) if (g_texInfo[(size_t)i].name == "SMOKE_0") { g_smoke0 = i; break; } if (g_smoke0 < 0) g_smoke0 = 0; }
    const ExploType& T = curExplo();
    const float tt = fmodf(g_tTime, fmaxf(exploLife(), 0.1f));
    const int ticks = (int)floorf(tt * 60.0f);
    // explosionTick, one tick at a time (lvupdate 1), parts spawned on every other tick (D486: the N64's pace)
    EPart parts[40] = {}; unsigned rs = g_tSeed * 104729u + 3u; auto rf = [&]() { return (lcg(rs) % 32768) / 32768.0f; };
    int age = 0;
    for (int k = 0; k <= ticks; k++) {
        if (age >= 8 && age < T.duration && (age & 1) == 0) {
            const float hr = T.hrange + T.hchange * age, vr = T.vrange + T.vchange * age;
            const int n = (int)(((float)T.proprate * (float)age) / (float)T.duration) + 1;
            for (int s = 0; s < n; s++) for (EPart& p : parts) if (p.frame == 0) {
                p.frame = 1; p.pos[0] = (rf() - 0.5f) * hr; p.pos[1] = (rf() - 0.5f) * vr; p.pos[2] = (rf() - 0.5f) * hr;
                const float sz = (rf() * 0.5f + 1.0f) * T.size, a = rf() * 6.2831853f; p.size = cosf(a) * sz; p.rot = sinf(a) * sz; break;
            }
        }
        if (k == ticks) break;
        age++;
        for (EPart& p : parts) { if (p.frame > 0) p.frame++; if ((int)(T.flarespd * 15.0f) < p.frame) p.frame = 0; }   // the render's reset, folded in
    }
    std::vector<Light> L;
    {   // dynlights.c addExplosions
        const float life = T.duration + 16.0f * T.flarespd, t = (float)age;
        if (T.size >= 10.0f && T.range > 0 && t < life) {
            float k = 1.0f - t / life; k *= k; if (t < 4.0f) k *= (t + 1.0f) / 5.0f;
            float g = 1.0f + 0.25f * log2f(T.size / 100.0f); k *= (g < 0.8f ? 0.8f : g > 1.8f ? 1.8f : g) * 1.15f;
            if (k > 0.01f) L.push_back({ { 0, T.size * 0.5f, 0 }, T.range * 4.0f, { 2.6f * k, 1.6f * k, 0.7f * k } });
        }
    }
    tabUniforms(c, L, port);
    if (g_tWall) { const float o[3] = { 0, 0, -(T.hrange + T.hchange * T.duration) * 0.5f - T.size * 1.5f }; tabWall(o, 6000, false); }
    // explosionRenderPropExplosion: frames 14 .. 0, each with its display list (SMOKE x FIRE, 2 cycles), then its parts
    for (int fi = 14; fi >= 0; fi--) {
        const int img = kExploFrameImg[fi];
        const GlTex& t0 = texture((uint32_t)(g_smoke0 + 2 * img), 0, 0);       // tile 0: IA8 56x56, wrap
        const GlTex& t1 = texture((uint32_t)(g_smoke0 + 2 * img + 1), 2, 2);   // tile 1: RGBA16, clamp, shift 2
        const float w0 = (float)(t0.w > 0 ? t0.w : 56), h0 = (float)(t0.h > 0 ? t0.h : 56), w1 = (float)(t1.w > 0 ? t1.w : 16), h1 = (float)(t1.h > 0 ? t1.h : 14);
        std::vector<V3> v; std::vector<uint16_t> idx;
        for (const EPart& p : parts) {
            if (p.frame <= 0 || fi != (int)((float)(p.frame - 1) / T.flarespd)) continue;
            // explosionRenderPart: pulled towards the eye by min(dist / 2, 100), sizes scaled with it
            const float dv[3] = { p.pos[0] - c.eye[0], p.pos[1] - c.eye[1], p.pos[2] - c.eye[2] };
            const float dist = sqrtf(dv[0] * dv[0] + dv[1] * dv[1] + dv[2] * dv[2]), pull = fminf(dist * 0.5f, 100.0f), f2 = dist == 0 ? 0 : (dist - pull) / dist;
            const float s54 = p.size * f2, s50 = p.rot * f2, cx = c.eye[0] + dv[0] * f2, cy = c.eye[1] + dv[1] * f2, cz = c.eye[2] + dv[2] * f2;
            float R54[3], R50[3], U54[3], U50[3];
            for (int k = 0; k < 3; k++) { R54[k] = c.r[k] * s54; R50[k] = c.r[k] * s50; U54[k] = c.u[k] * s54; U50[k] = c.u[k] * s50; }
            const float q[4][3] = { { cx - R54[0] - U50[0], cy - R54[1] - U50[1], cz - R54[2] - U50[2] }, { cx + R50[0] - U54[0], cy + R50[1] - U54[1], cz + R50[2] - U54[2] },
                                    { cx + R54[0] + U50[0], cy + R54[1] + U50[1], cz + R54[2] + U50[2] }, { cx - R50[0] + U54[0], cy - R50[1] + U54[1], cz - R50[2] + U54[2] } };
            const float tc[4][2] = { { 55, 0 }, { 0, 0 }, { 0, 55 }, { 55, 55 } };   // 1760 = 55 << 5
            const uint16_t b = (uint16_t)v.size();
            for (int k = 0; k < 4; k++) v.push_back({ q[k][0], q[k][1], q[k][2], tc[k][0] / w0, tc[k][1] / h0, 1, 1, 1, 1 });   // g_ExplosionRenderPartDefaultVertex: white
            quadIdx(idx, b);
        }
        if (v.empty()) continue;
        glActiveTexture(GL_TEXTURE1); bindTex(t1, false); glActiveTexture(GL_TEXTURE0); bindTex(t0, false);
        glUniform2f(glGetUniformLocation(g_pTab, "uUV1Scale"), w0 / (4.0f * w1), h0 / (4.0f * h1));
        tabDraw(v, idx, 2, 0, 0);   // G_CC_INTERFERENCE then MODULATEIA2, G_RM_ZB_CLD_SURF2: alpha over
    }
}

// ---------------------------------------------------------------- panes, captions, UI
static float tabPeriod() {
    switch (g_mode) { case MODE_MUZZLE: return 1.0f; case MODE_TRACER: return fmaxf(g_trPeriod, 0.05f) * 4.0f; case MODE_BURST: return fmaxf(g_bPeriod, 0.1f); case MODE_EXPLO: return exploLife(); }
    return 1.0f;
}
static void tabTick(float dt) {
    if (g_mode < MODE_MUZZLE || !g_tPlay) return;
    g_tTime += dt * g_tSpeed;
    const float P = tabPeriod();
    if (g_tTime >= P) { if (g_tLoop) g_tTime = fmodf(g_tTime, P); else { g_tTime = P - 1e-4f; g_tPlay = false; } }
}
static void drawTabPane(int x0, int y0, int w, int h, bool port, int clipX0, int clipX1) {
    glViewport(x0, y0, w, h);
    glEnable(GL_SCISSOR_TEST); glScissor(clipX0, y0, clipX1 - clipX0, h);
    glClearColor(0.05f, 0.05f, 0.06f, 1); glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);
    const float aspect = (float)w / (float)h;
    if (g_mode == MODE_MUZZLE) {
        scanGuns();
        const GunFlash* G = g_guns.empty() ? nullptr : &g_guns[(size_t)std::max(0, std::min((int)g_guns.size() - 1, g_gun))];
        const float tgt[3] = { G ? (G->off[0] - G->size[0] * 0.5f) * g_mScale : 0, 0, 0 };
        sceneMuzzle(tabCam(tgt, G ? fmaxf(G->size[0], G->size[1]) * g_mScale * 3.2f : 100.0f, aspect), port, w, h);
    } else if (g_mode == MODE_TRACER) { const float tgt[3] = { 0, 0, 0 }; sceneTracer(tabCam(tgt, g_trRange * 0.6f, aspect), port); }
    else if (g_mode == MODE_BURST) { const float tgt[3] = { 0, 10, 0 }; sceneBurst(tabCam(tgt, 260.0f, aspect), port); }
    else { const ExploType& T = curExplo(); const float tgt[3] = { 0, 0, 0 }; sceneExplo(tabCam(tgt, (T.hrange + T.size * 2.5f + T.hchange * T.duration) * 3.0f + 60.0f, aspect), port); }
    glDisable(GL_BLEND); glDisable(GL_SCISSOR_TEST);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, 0); glActiveTexture(GL_TEXTURE0);
}
static void renderTab(int x, int y, int w, int h, int H) {
    g_panes.clear();
    const int gy = H - y - h;
    auto pane = [&](int px, int pw, bool port) { drawTabPane(px, gy, pw, h, port, px, px + pw); PaneRect r{ px, y, pw, h, port }; g_panes.push_back(r); };
    if (g_tShow == 2) { const int hw = w / 2; pane(x, hw, false); pane(x + hw, w - hw, true); }
    else pane(x, w, g_tShow == 1);
}
static std::string tabCaption() {
    char b[400]; const float P = tabPeriod();
    if (g_mode == MODE_MUZZLE) {
        scanGuns();
        if (g_guns.empty()) return "muzzle: " + g_gunNote;
        const GunFlash& G = g_guns[(size_t)std::max(0, std::min((int)g_guns.size() - 1, g_gun))];
        snprintf(b, sizeof b, "Muzzle flash: Pchr%s GUNFIRE  size %.0fx%.0fx%.0f (model units, x%.2f)  tex %u %dx%d  %.1f shots/s, %d ticks each | t %.2f / %.2f s",
                 G.name.c_str(), G.size[0], G.size[1], G.size[2], g_mScale, G.tex, G.w, G.h, g_mRate, g_mVis, g_tTime, P);
    } else if (g_mode == MODE_TRACER) {
        static const char* it[3] = { "gun (flareimage3)", "laser (flareimage4)", "watch laser (flareimage4)" };
        snprintf(b, sizeof b, "Tracer: %s  range %.0f | port: streak 9000/s, fxShade TRACER, additive | t %.3f / %.2f s", it[g_trItem], g_trRange, g_tTime, P);
    } else if (g_mode == MODE_BURST) {
        snprintf(b, sizeof b, "Impact burst: %s | game: %s puff (smoke frames, 11 ticks) | t %.2f / %.2f s", kBurstName[g_burst], g_burst == BM_BLOOD ? "blood" : "wall", g_tTime, P);
    } else {
        const ExploType& T = curExplo();
        snprintf(b, sizeof b, "Explosion type %d: size %.0f, duration %d, flare speed %.0f, range %.0f | port: + dynlights (radius %.0f) | t %.2f / %.2f s",
                 g_explo, T.size, T.duration, T.flarespd, T.range, T.range * 4.0f, g_tTime, P);
    }
    return b;
}
static void uiTabCommon() {
    ImGui::SeparatorText("Playback");
    if (ImGui::Button("Replay")) { g_tTime = 0; g_tPlay = true; }
    ImGui::SameLine(); ImGui::Checkbox("play", &g_tPlay); ImGui::SameLine(); ImGui::Checkbox("loop", &g_tLoop);
    ImGui::SliderFloat("speed", &g_tSpeed, 0.02f, 2.0f, "%.2f x");
    float P = tabPeriod(); ImGui::SliderFloat("time (s)", &g_tTime, 0.0f, P, "%.3f");
    ImGui::SameLine(); if (ImGui::SmallButton("reroll")) g_tSeed++;
    ImGui::SeparatorText("Game original vs port FX (F9)");
    ImGui::RadioButton("side by side", &g_tShow, 2); ImGui::SameLine(); ImGui::RadioButton("game (F9 off)", &g_tShow, 0); ImGui::SameLine(); ImGui::RadioButton("port", &g_tShow, 1);
    ImGui::SeparatorText("Camera");
    ImGui::SliderFloat("yaw##t", &g_tYaw, -85, 85, "%.0f deg"); ImGui::SliderFloat("pitch##t", &g_tPitch, -85, 85, "%.0f deg"); ImGui::SliderFloat("zoom##t", &g_tZoom, 0.1f, 8.0f, "%.2f");
    ImGui::Checkbox("wall / floor", &g_tWall); ImGui::SameLine(); ImGui::SetNextItemWidth(90); ImGui::InputInt("tex #", &g_tWallTex); if (g_tWallTex < 0) g_tWallTex = 0;
    ImGui::Checkbox("nearest texels", &g_nearest);
    ImGui::SeparatorText("Shader");
    ImGui::TextWrapped("fxShade + dynLight from %s (reloaded %d x)", g_shaderPath.c_str(), g_tabReloads);
    if (!g_tabErr.empty()) ImGui::TextColored(ImVec4(1, .4f, .4f, 1), "%s", g_tabErr.c_str());
}
static void uiTab() {
    if (g_mode == MODE_MUZZLE) {
        ImGui::SeparatorText("Muzzle flash (guards' weapons)");
        scanGuns();
        ImGui::TextWrapped("%s", g_gunNote.c_str());
        if (!g_guns.empty() && ImGui::BeginCombo("weapon", g_guns[(size_t)std::max(0, std::min((int)g_guns.size() - 1, g_gun))].name.c_str())) {
            for (int i = 0; i < (int)g_guns.size(); i++) if (ImGui::Selectable(g_guns[(size_t)i].name.c_str(), g_gun == i)) g_gun = i;
            ImGui::EndCombo();
        }
        ImGui::SliderFloat("shots / s", &g_mRate, 1.0f, 15.0f, "%.1f"); ImGui::SliderInt("ticks shown", &g_mVis, 1, 6);
        ImGui::SliderFloat("model scale", &g_mScale, 0.02f, 0.5f, "%.3f");
        ImGui::TextDisabled("port: the same quad (fast3d gives MUZZLE no fxShade)\n+ dynlights' muzzle light (2.2,1.5,0.7, r 260)");
    } else if (g_mode == MODE_TRACER) {
        ImGui::SeparatorText("Tracer");
        ImGui::RadioButton("gun", &g_trItem, 0); ImGui::SameLine(); ImGui::RadioButton("laser", &g_trItem, 1); ImGui::SameLine(); ImGui::RadioButton("watch laser", &g_trItem, 2);
        ImGui::SliderFloat("range", &g_trRange, 100.0f, 6000.0f, "%.0f"); ImGui::SliderFloat("shot every (s)", &g_trPeriod, 0.05f, 2.0f, "%.2f");
    } else if (g_mode == MODE_BURST) {
        ImGui::SeparatorText("Impact burst");
        for (int i = 0; i < BM_COUNT; i++) if (ImGui::RadioButton(kBurstName[i], g_burst == i)) g_burst = i;
        ImGui::SliderFloat("loop (s)", &g_bPeriod, 0.2f, 3.0f, "%.2f");
    } else if (g_mode == MODE_EXPLO) {
        ImGui::SeparatorText("Explosion");
        ImGui::SliderInt("type", &g_explo, 0, (int)(sizeof kExplo / sizeof *kExplo) - 1);
        ImGui::SliderFloat("pause after (s)", &g_eTail, 0.0f, 2.0f, "%.2f");
        ImGui::TextDisabled("the smoke the blast leaves (explosionCreateSmoke)\nis not drawn");
    }
    uiTabCommon();
}
