// fxview tabs: muzzle flashes, tracers, impact bursts (blood / smoke / debris), explosions. Included by fxview.cpp (after the decal mode:
// it uses its camera, the ROM textures and the draw helpers). The old viewer's tabs, drawn through ge_render the way the game draws them.
//
// Each tab shows GAME (Video.Visuals Enhanced with F9 off: the game's own sprites, no effect kinds, no lights) and PORT (F9 on: what the
// port draws instead, port/src/fxparticles.c, plus the dynamic lights of port/src/dynlights.c: the Lights block) side by side or alone.
// The combiner ids are the game's: TEXEL0 x SHADE (MODULATEIA), SHADE alone for the untextured particles (the PORTFX kind in id1), the
// explosions' two cycles (TEXEL0 x TEXEL1, then x SHADE, TEXEL1 clamped). Blend per kind as fast3d sets it (gfx_pc.cpp: SPARK and TRACER
// additive = blend mode 2, everything else alpha over = mode 0). The scene state is a pure function of the tab's time (re-simulated from 0
// each frame, 1/60 s ticks), so a --shot at --t SECONDS is reproducible.

#include "fxburst.h"

// ---------------------------------------------------------------- shared tab state
static float g_tTime = 0, g_tSpeed = 1.0f;
static bool g_tPlay = true, g_tLoop = true;
static unsigned g_tr = 1;   // xorshift, as fxparticles.c frand (fxburst.h reads it through FxbEmit)

struct Light { float pos[3], r, col[3]; };   // world units (renderScale 1)
struct TV { float x, y, z, u, v, r, g, b, a; };
struct TabCtx { View vw; Cam cam; };

static void tnorm(float v[3]) { const float l = v[0] * v[0] + v[1] * v[1] + v[2] * v[2]; if (l > 1e-8f) { const float k = 1.0f / sqrtf(l); v[0] *= k; v[1] *= k; v[2] *= k; } }

static TabCtx tabCtx(const Args &a, const float tgt[3], float dist, float aspect) {
    TabCtx c;
    const float y = a.tyaw * 0.0174533f, p = a.tpitch * 0.0174533f, d = dist / fmaxf(a.tzoom, 0.05f);
    View &v = c.vw;
    v.eye[0] = tgt[0] + d * cosf(p) * sinf(y); v.eye[1] = tgt[1] + d * sinf(p); v.eye[2] = tgt[2] + d * cosf(p) * cosf(y);
    float f[3] = { tgt[0] - v.eye[0], tgt[1] - v.eye[1], tgt[2] - v.eye[2] };
    tnorm(f);
    for (int i = 0; i < 3; i++) v.f[i] = f[i];
    v.r[0] = -v.f[2]; v.r[1] = 0; v.r[2] = v.f[0];
    { const float l = 1.0f / sqrtf(v.r[0] * v.r[0] + v.r[2] * v.r[2]); v.r[0] *= l; v.r[2] *= l; }
    v.u[0] = v.r[1] * v.f[2] - v.r[2] * v.f[1]; v.u[1] = v.r[2] * v.f[0] - v.r[0] * v.f[2]; v.u[2] = v.r[0] * v.f[1] - v.r[1] * v.f[0];
    c.cam = makeCam(40.0f, aspect, 50000.0f);
    return c;
}

// The Lights block of one pane: the port side only (the game has none).
static void tabLights(const TabCtx &c, const std::vector<Light> &L, bool lit) {
    GfxLights G = {};
    memcpy(G.inv_proj, c.cam.inv, sizeof G.inv_proj);
    const float sd[3] = { 0.3f, 0.9f, 0.3f }, sn = sqrtf(sd[0] * sd[0] + sd[1] * sd[1] + sd[2] * sd[2]);
    for (int i = 0; i < 3; i++) G.sun_dir[i] = sd[i] / sn;   // the relief light, as the game sends it with no sun
    const int n = lit ? std::min((int)L.size(), GFX_LIGHTS_MAX) : 0;
    G.count[0] = n;
    for (int i = 0; i < n; i++) {
        toEye(c.vw, L[(size_t)i].pos, G.pos[i], false);
        G.pos[i][3] = L[(size_t)i].r;
        for (int k = 0; k < 3; k++) G.col[i][k] = L[(size_t)i].col[k];
    }
    if (api()->set_lights) api()->set_lights(&G);
}

static void emitTri(const TabCtx &c, const TV &q, bool tex, std::vector<float> &out) {
    const float w[3] = { q.x, q.y, q.z };
    float e[3], c4[4];
    toEye(c.vw, w, e, false);
    clipOf(c.cam, e, c4);
    out.insert(out.end(), { c4[0], c4[1], c4[2], c4[3] });
    if (tex) out.insert(out.end(), { q.u, q.v });
    out.insert(out.end(), { q.r, q.g, q.b, q.a });
}
static std::vector<float> tabVerts(const TabCtx &c, const std::vector<TV> &v, const std::vector<uint16_t> &idx, bool tex) {
    std::vector<float> out;
    for (uint16_t i : idx) emitTri(c, v[i], tex, out);
    return out;
}

// One textured draw: TEXEL0 x SHADE, the kind in the combiner id (0 = the game's own look), blend 0 alpha over / 2 additive.
static void tabTex(const Args &a, const TabCtx &c, const std::vector<TV> &v, const std::vector<uint16_t> &idx, uint32_t tid, int cms, int cmt, int kind, int blend, bool mip = false) {
    if (v.empty() || idx.empty()) return;
    program(CC_TEX, idFor(kind));
    api()->set_use_alpha(true, blend);
    bindTex(0, tid, !a.nearest, cms, cmt, mip);
    draw(tabVerts(c, v, idx, true), 10);
}
// An untextured draw: SHADE (the particles). Blood keeps the game's id with the depth-only second pass's bit (SHADER_OPT_ZCUT), as fast3d sets it.
static void tabShade(const TabCtx &c, const std::vector<TV> &v, const std::vector<uint16_t> &idx, int kind, int blend) {
    if (v.empty() || idx.empty()) return;
    program(ge_render_cc_id_shade(), idFor(kind) | (kind == PORTFX_BLOOD ? 0x4000u : 0u));
    api()->set_use_alpha(true, blend);
    draw(tabVerts(c, v, idx, false), 8);
}
static void quadIdx(std::vector<uint16_t> &idx, uint16_t b) { for (uint16_t i : { 0, 1, 2, 0, 2, 3 }) idx.push_back((uint16_t)(b + i)); }

// The backdrop wall: the plane through `o` with normal +z, the room's baked shade 0.6, lit on the port side by the Lights block.
static void tabWall(const Args &a, const TabCtx &c, const float o[3], float half) {
    if (!a.twall) return;
    const Tex &t = romTex(a.twalltex, true);
    const float tw = (float)(t.w > 0 ? t.w : 32) * 2.0f, th = (float)(t.h > 0 ? t.h : 32) * 2.0f, s = 0.6f;   // 2 world units per texel
    std::vector<TV> q;
    const float cn[4][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
    for (int k = 0; k < 4; k++) {
        const float x = o[0] + cn[k][0] * half, y = o[1] + cn[k][1] * half;
        q.push_back({ x, y, o[2], x / tw, -y / th, s, s, s, 1 });
    }
    std::vector<uint16_t> idx;
    quadIdx(idx, 0);
    program(CC_TEX, idFor(0));
    api()->set_use_alpha(false, 0);
    bindTex(0, t.id ? t.id : g_white, true, 0, 0, t.id != 0);
    draw(tabVerts(c, q, idx, true), 10);
}

// ---------------------------------------------------------------- muzzle flashes (model.c GUNFIRE node, dynlights.c)
static const FxGun *curGun(const Args &a) {
    const std::vector<FxGun> &g = fxGuns();
    if (g.empty()) return nullptr;
    for (const FxGun &x : g) if (x.name == a.gun) return &x;
    return &g[0];
}
static int gunIndex(const Args &a) { const FxGun *g = curGun(a); return g ? (int)(g - &fxGuns()[0]) : 0; }
#define MUZZLE_R 2.20f
#define MUZZLE_G 1.50f
#define MUZZLE_B 0.70f
#define MUZZLE_RANGE 260.0f
static void sceneMuzzle(const Args &a, const TabCtx &c, bool port) {
    const FxGun *Gp = curGun(a);
    if (!Gp) return;
    const FxGun &G = *Gp;
    const int tick = (int)floorf(g_tTime * 60.0f), period = std::max(1, (int)lroundf(60.0f / fmaxf(a.mrate, 0.5f)));
    const bool visible = tick % period < a.mvis;
    std::vector<Light> L;
    const float k = a.mscale;
    const float centre[3] = { (G.off[0] - G.size[0] * 0.5f) * k, G.off[1] * k, G.off[2] * k };
    if (port && visible) L.push_back({ { centre[0], centre[1], centre[2] }, MUZZLE_RANGE, { MUZZLE_R, MUZZLE_G, MUZZLE_B } });   // dynlights.c addMuzzle
    tabLights(c, L, port);
    { const float o[3] = { 0, 0, -60 }; tabWall(a, c, o, 1200); }
    {   // the barrel, a dark bar from the muzzle back (orientation only, the gun model is not drawn)
        const float x0 = G.off[0] * k, x1 = x0 + 120 * k * 2, y = G.off[1] * k, z = G.off[2] * k, hh = 12 * k;
        std::vector<TV> v = { { x0, y - hh, z, 0, 0, .12f, .12f, .13f, 1 }, { x1, y - hh, z, 0, 0, .12f, .12f, .13f, 1 }, { x1, y + hh, z, 0, 0, .12f, .12f, .13f, 1 }, { x0, y + hh, z, 0, 0, .12f, .12f, .13f, 1 } };
        std::vector<uint16_t> idx;
        quadIdx(idx, 0);
        tabTex(a, c, v, idx, g_white, 2, 2, 0, 0);
    }
    if (!visible) return;
    // model.c GUNFIRE: the eye direction in the model's frame (the model frame = world here, no rotation)
    float e[3] = { c.vw.eye[0] - centre[0], c.vw.eye[1] - centre[1], c.vw.eye[2] - centre[2] };
    tnorm(e);
    const float spec = acosf(fmaxf(-1.0f, fminf(1.0f, e[1])));
    float spf0 = acosf(fmaxf(-1.0f, fminf(1.0f, -e[2] / fmaxf(sinf(spec), 1e-5f))));
    if (-e[0] < 0) spf0 = 6.2831853f - spf0;
    const float spdc = cosf(spf0), spd8 = sinf(spf0), spd4 = cosf(spec), spd0 = sinf(spec);
    unsigned rs = (unsigned)tick * 2654435761u + (unsigned)a.tseed;   // randomGetNext per drawn frame: the scale and the texture's turn
    const float scale = 0.75f + (lcg(rs) % 128) * (1.0f / 256.0f);
    const float S[3] = { G.size[0] * scale, G.size[1] * scale, G.size[2] * scale };
    const float spcc = S[0] * spdc * 0.5f, spc8 = S[2] * spd8 * 0.5f, spc4 = S[1] * spd0 * 0.5f, spc0 = S[0] * spd4 * spd8 * 0.5f, spbc = S[2] * spd4 * spdc * 0.5f;
    const float p90[3] = { G.off[0] - S[0] * 0.5f, G.off[1], G.off[2] };
    const float P[4][3] = { { p90[0] - spcc - spc0, p90[1] - spc4, p90[2] + spc8 - spbc }, { p90[0] - spcc + spc0, p90[1] + spc4, p90[2] + spc8 + spbc },
                            { p90[0] + spcc + spc0, p90[1] + spc4, p90[2] - spc8 + spbc }, { p90[0] + spcc - spc0, p90[1] - spc4, p90[2] - spc8 - spbc } };
    const float ang = (float)((lcg(rs) * 1024) & 0xFFFF) / 65536.0f * 6.2831853f;
    const float cs = cosf(ang) * 0.70703f, sn = sinf(ang) * 0.70703f;   // (coss * width * 0xb5) >> 18, around the centre
    const float uv[4][2] = { { 0.5f - cs, 0.5f - sn }, { 0.5f + sn, 0.5f - cs }, { 0.5f + cs, 0.5f + sn }, { 0.5f - sn, 0.5f + cs } };
    std::vector<TV> v;
    for (int i = 0; i < 4; i++) v.push_back({ P[i][0] * k, P[i][1] * k, P[i][2] * k, uv[i][0], uv[i][1], 1, 1, 1, 1 });   // D_800363E0: white
    const Tex &t = romTex((int)G.tex, false);
    // texSelect mode 4: G_RM_ZB_CLD_SURF (alpha over), MODULATEIA. fast3d keeps the muzzle's look (no effect kind, gfx_pc.cpp)
    tabTex(a, c, v, { 0, 1, 2, 2, 3, 0 }, t.id ? t.id : g_white, G.fs & 3, G.ft & 3, 0, 0);
}

// ---------------------------------------------------------------- tracers (gunfire.c beam, fxparticles.c onShot)
static int tracerItem(const Args &a) { return a.tritem == "laser" || a.tritem == "1" ? 1 : a.tritem == "watchlaser" || a.tritem == "2" ? 2 : 0; }
static void streakQuad(const float h[3], const float d[3], float len, float s, const View &c, const unsigned char rgba[4], int ah, int at, std::vector<TV> &v, std::vector<uint16_t> &idx) {
    // fxparticles.c drawParticle FXP_STREAK: the width across the streak, facing the camera
    const float vd[3] = { -c.f[0], -c.f[1], -c.f[2] };
    float x[3] = { d[1] * vd[2] - d[2] * vd[1], d[2] * vd[0] - d[0] * vd[2], d[0] * vd[1] - d[1] * vd[0] };
    tnorm(x);
    const float t[3] = { h[0] - d[0] * len, h[1] - d[1] * len, h[2] - d[2] * len };
    float q[6][3];
    for (int k = 0; k < 3; k++) { q[0][k] = h[k]; q[1][k] = h[k] + x[k] * s; q[2][k] = h[k] - x[k] * s; q[3][k] = t[k]; q[4][k] = t[k] + x[k] * s; q[5][k] = t[k] - x[k] * s; }
    const float al[6] = { (float)ah, 0, 0, (float)at, 0, 0 };
    const uint16_t b = (uint16_t)v.size();
    for (int k = 0; k < 6; k++) v.push_back({ q[k][0], q[k][1], q[k][2], 0, 0, rgba[0] / 255.0f, rgba[1] / 255.0f, rgba[2] / 255.0f, std::max(0.0f, std::min(255.0f, al[k])) / 255.0f });
    for (uint16_t i : { 1, 0, 3, 1, 3, 4, 0, 2, 5, 0, 5, 3 }) idx.push_back((uint16_t)(b + i));
}
static void sceneTracer(const Args &a, const TabCtx &c, bool port) {
    tabLights(c, {}, false);
    { const float o[3] = { 0, 0, -400 }; tabWall(a, c, o, 6000); }
    const int item = tracerItem(a);
    const float range = a.trrange, period = fmaxf(a.trperiod, 0.05f);
    const float from[3] = { -range * 0.5f, 0, 0 }, dvec[3] = { 1, 0, 0 };
    const float tt = fmodf(g_tTime, period);
    if (!port) {   // gunfire.c CapBeamLengthAndDecideIfRendered + gunAdvanceBeamTimer (once per 1/60 s frame) + sub_GAME_7F061E18
        unsigned rs = (unsigned)a.tseed * 7919u + (unsigned)floorf(g_tTime / period);
        float r = fmaxf(range, 500.0f), u20, u24, u28;
        const float rnd = (lcg(rs) % 32768) / 32768.0f;
        if (item == 1) { u20 = 0.25f * r; u24 = fminf(0.6f * r, 3000.0f); u28 = (-0.1f - rnd * 0.3f) * r; }
        else if (item == 2) { r = fminf(range, 300.0f); r = fmaxf(r, 500.0f); u24 = r; u20 = 2.0f * r; if (r > 3000) u24 = 3000; u28 = 0; }
        else { u20 = 0.2f * r; u24 = u20 > 3000 ? 3000 : u20; u28 = (2.0f * rnd - 1.0f) * u20; }
        const float u1c = item == 2 ? fminf(range, 300.0f) : fminf(range, 10000.0f);
        const int frames = (int)floorf(tt * 60.0f);
        for (int f = 0; f < frames; f++) { u28 += u20; if (u1c <= u28) return; }
        if (u1c <= u28) return;
        float start = u28, dist = u24;
        if (start <= 0) { dist += start; start = 0; }
        if (u1c < start + dist) dist = u1c - start;
        if (dist <= 0) return;
        const float radius = (item == 0 ? 30.0f : item == 1 ? 50.0f : 10.0f) * 0.1f;   // the matrix scales by 0.1
        const float pos[3] = { from[0] + start, from[1], from[2] }, end[3] = { pos[0] + dist, pos[1], pos[2] };
        const View &w = c.vw;
        float rt[3] = { dvec[1] * (w.eye[2] - end[2]) - dvec[2] * (w.eye[1] - end[1]), dvec[2] * (w.eye[0] - end[0]) - dvec[0] * (w.eye[2] - end[2]), dvec[0] * (w.eye[1] - end[1]) - dvec[1] * (w.eye[0] - end[0]) };
        tnorm(rt);
        for (float &x : rt) x *= radius;
        const float al = item == 2 ? 175.0f / 255.0f : 1.0f;
        std::vector<TV> v = { { pos[0] + rt[0], pos[1] + rt[1], pos[2] + rt[2], 1, 0, 1, 1, 1, al }, { pos[0] - rt[0], pos[1] - rt[1], pos[2] - rt[2], 0, 0, 1, 1, 1, al },
                              { end[0] + .9f * rt[0], end[1] + .9f * rt[1], end[2] + .9f * rt[2], 1, 1, 1, 1, 1, al }, { end[0] - .9f * rt[0], end[1] - .9f * rt[1], end[2] - .9f * rt[2], 0, 1, 1, 1, 1, al } };
        const ImgEntry *im = fxTableEntry(item == 0 ? "flareimage3" : "flareimage4", 0);
        const Tex &t = im ? romTex((int)im->index, false) : romTex(0, false);
        tabTex(a, c, v, { 0, 2, 3, 0, 3, 1 }, t.id ? t.id : g_white, im ? im->flagsS : 2, im ? im->flagsT : 2, 0, 0);   // G_RM_ZB_CLD_SURF: alpha over; F9 off: no effect kind
        return;
    }
    // fxparticles.c onShot: a streak from 30 ahead of the muzzle at 9000 / s, its tail never behind the muzzle, + its halo
    const float dist = range;
    if (dist < 31.0f) return;
    const float life = (dist - 30.0f) / 9000.0f;
    if (tt >= life) return;
    const float h[3] = { from[0] + 30.0f + 9000.0f * tt, from[1], from[2] };
    float len = fmaxf(9000.0f * 0.03f, 60.0f);
    len = fminf(len, h[0] - from[0]);
    const unsigned char col[4] = { 255, 214, 150, 255 };
    const View &w = c.vw;
    std::vector<TV> v;
    std::vector<uint16_t> idx;
    for (int pass = 0; pass < 2; pass++) {   // the halo (size x 3.5, a / 4) first, then the core
        const float s = 1.4f * (pass == 0 ? 3.5f : 1.0f);
        const int al = pass == 0 ? 255 / 4 : 255;
        const float t[3] = { h[0] - len, h[1], h[2] };
        const float dh = sqrtf((h[0] - w.eye[0]) * (h[0] - w.eye[0]) + (h[1] - w.eye[1]) * (h[1] - w.eye[1]) + (h[2] - w.eye[2]) * (h[2] - w.eye[2]));
        const float dt = sqrtf((t[0] - w.eye[0]) * (t[0] - w.eye[0]) + (t[1] - w.eye[1]) * (t[1] - w.eye[1]) + (t[2] - w.eye[2]) * (t[2] - w.eye[2]));
        const float kh = fmaxf(0, fminf(1, (dh - 40.0f) / 100.0f)), kt = fmaxf(0, fminf(1, (dt - 40.0f) / 100.0f));   // NEAR_FADE0 / 1
        streakQuad(h, dvec, len, s, w, col, (int)(al * kh), (int)((al / 3) * kt), v, idx);
    }
    tabShade(c, v, idx, PORTFX_TRACER, 2);   // G_CC_SHADE, the TRACER kind, additive (gfx_pc.cpp use_modulate = 2)
}

// ---------------------------------------------------------------- impact bursts (fxparticles.c onImpact / onPuff, glass2.c puffs)
enum { BM_METAL = 0, BM_WOOD, BM_GLASS, BM_WATER, BM_SNOW, BM_DIRT, BM_STONE, BM_BLOOD, BM_COUNT };
static const char *kBurstKey[BM_COUNT] = { "metal", "wood", "glass", "water", "snow", "dirt", "stone", "blood" };
static const char *kBurstName[BM_COUNT] = { "Metal (sparks + dust)", "Wood (splinters + dust)", "Glass (shards)", "Water (splash)", "Snow (spray)",
                                            "Dirt / mud (clods + dust)", "Stone / tile / default (chips, sparks, dust)", "Blood (body hit, puff type 2)" };
static int burstIndex(const std::string &s) { for (int i = 0; i < BM_COUNT; i++) if (s == kBurstKey[i]) return i; return BM_STONE; }
// The recipes are the game's own (port/include/fxburst.h, shared with port/src/fxparticles.c).
typedef FxbPart TPart;
static FxbPart *tAlloc(void *ctx) {
    auto *P = (std::vector<TPart> *)ctx;
    if (P->size() >= 384) return nullptr;   // FXP_MAX
    P->push_back(TPart{});
    return &P->back();
}
static const int kBurstMat[BM_COUNT] = { PMAT_METAL, PMAT_WOOD, PMAT_GLASS, PMAT_WATER, PMAT_SNOW, PMAT_DIRT, PMAT_STONE, PMAT_CHR };
static void burstEmit(std::vector<TPart> &P, int m, const float *pos, const float *N) {
    P.reserve(512);   // the allocator hands out pointers into P: no reallocation while a recipe runs
    const FxbEmit em = { tAlloc, &P, &g_tr };
    if (m == BM_BLOOD) fxb_blood(&em, pos, N);
    else fxb_impact(&em, kBurstMat[m], pos, N);
}
// fxparticles.c drawParticle: streak (6 vertices) or disc / solid (a hexagon, a solid keeps its alpha to the rim)
static void partGeom(const TPart &p, const View &c, std::vector<TV> &v, std::vector<uint16_t> &idx) {
    const float fade = p.life / p.maxlife;
    const int al = (int)(p.rgba[3] * fade);
    if (p.shape == FXB_STREAK || p.shape == FXB_SHARD) {
        float d[3] = { p.vel[0], p.vel[1], p.vel[2] };
        const float sp = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        tnorm(d);
        float len = sp * p.stretch;
        if (len < p.minlen) len = p.minlen;
        const size_t b0 = v.size();
        streakQuad(p.pos, d, len, p.size, c, p.rgba, al, (int)(al * p.tail), v, idx);
        if (p.shape == FXB_SHARD) {   // fxparticles.c: a crisp rim, a pointed tail
            for (int k : { 1, 2 }) v[b0 + (size_t)k].a = v[b0].a;
            for (int k : { 4, 5 }) {
                v[b0 + (size_t)k].a = v[b0 + 3].a;
                for (int j = 0; j < 3; j++) (&v[b0 + (size_t)k].x)[j] = (&v[b0 + 3].x)[j] + ((&v[b0 + (size_t)k].x)[j] - (&v[b0 + 3].x)[j]) * 0.15f;
            }
        }   // the near fade is a no-op at these distances
        return;
    }
    const uint16_t b = (uint16_t)v.size();
    const float rim = p.shape == FXB_SOLID ? (float)al : 0.0f;
    v.push_back({ p.pos[0], p.pos[1], p.pos[2], 0, 0, p.rgba[0] / 255.0f, p.rgba[1] / 255.0f, p.rgba[2] / 255.0f, al / 255.0f });
    for (int i = 0; i < 6; i++) {
        const float ang = i * (6.2831853f / 6.0f), cc = cosf(ang) * p.size, sn = sinf(ang) * p.size;
        v.push_back({ p.pos[0] + c.r[0] * cc + c.u[0] * sn, p.pos[1] + c.r[1] * cc + c.u[1] * sn, p.pos[2] + c.r[2] * cc + c.u[2] * sn, 0, 0,
                      p.rgba[0] / 255.0f, p.rgba[1] / 255.0f, p.rgba[2] / 255.0f, rim / 255.0f });
    }
    for (int i = 0; i < 6; i++) for (int k : { 0, 1 + i, 1 + (i + 1) % 6 }) idx.push_back((uint16_t)(b + k));
}
static void drawParts(const TabCtx &c, const std::vector<TPart> &P) {
    std::vector<TV> v;
    std::vector<uint16_t> idx;
    int cur = -1;
    auto flush = [&]() { if (!v.empty()) tabShade(c, v, idx, cur, (cur == PORTFX_SPARK || cur == PORTFX_TRACER) ? 2 : 0); v.clear(); idx.clear(); };
    for (const TPart &p : P) {   // in array order like fxpDraw; consecutive particles of one kind go in one draw
        if (p.kind != cur) { flush(); cur = p.kind; }
        partGeom(p, c.vw, v, idx);
    }
    flush();
}
static void stepParts(std::vector<TPart> &P, float dt) {   // fxparticles.c fxpStep
    size_t w = 0;
    for (size_t i = 0; i < P.size(); i++) {
        TPart p = P[i];
        if (!fxb_step(&p, dt)) continue;
        P[w++] = p;
    }
    P.resize(w);
}
// glass2.c bullet_spark_render: a diamond facing the camera, frame = (s32)(age * unk08), lifetime 11 (smoke) ticks
static void gamePuff(const Args &a, const TabCtx &c, const float pos[3], int ticks, bool blood) {
    if (ticks >= 11) return;
    const ImgEntry *im = fxTableEntry("explosion_smokeimages", puffFrame((float)ticks, 0.5f, std::max(1, fxTableCount("explosion_smokeimages"))));
    unsigned rs = (unsigned)a.tseed * 31u + 5u;
    const float ang = (lcg(rs) % 32768) / 32768.0f * 6.2831853f, sz = 26.0f * (1.0f + (lcg(rs) % 32768) / 32768.0f * 0.25f) * 1.41421f;   // bullet_spark_create(pos, 1, 26.0f, room)
    const float cs = cosf(ang) * sz, sn = sinf(ang) * sz;
    const float p2[4][2] = { { -cs, -sn }, { sn, -cs }, { cs, sn }, { -sn, cs } }, uv[4][2] = { { 1, 0 }, { 0, 0 }, { 0, 1 }, { 1, 1 } };
    float col[3] = { 1, 1, 0xC8 / 255.0f };   // g_BulletSparkColors[1]
    if (blood) for (int k = 0; k < 3; k++) col[k] = kBlood[k] / 255.0f;
    const View &w = c.vw;
    std::vector<TV> v;
    for (int i = 0; i < 4; i++)
        v.push_back({ pos[0] + w.r[0] * p2[i][0] + w.u[0] * p2[i][1], pos[1] + w.r[1] * p2[i][0] + w.u[1] * p2[i][1], pos[2] + w.r[2] * p2[i][0] + w.u[2] * p2[i][1],
                      uv[i][0], uv[i][1], col[0], col[1], col[2], 1 });
    const Tex &t = im ? romTex((int)im->index, false) : romTex(0, false);
    tabTex(a, c, v, { 0, 1, 2, 0, 2, 3 }, t.id ? t.id : g_white, im ? im->flagsS : 2, im ? im->flagsT : 2, 0, 0);
}
static void sceneBurst(const Args &a, const TabCtx &c, bool port) {
    tabLights(c, {}, false);
    const int bm = burstIndex(a.burst);
    const float pos[3] = { 0, 0, 0 }, N[3] = { 0, 0, 1 };
    { const float o[3] = { 0, 0, bm == BM_BLOOD ? -300.0f : 0.0f }; tabWall(a, c, o, bm == BM_BLOOD ? 3000.0f : 1500.0f); }
    const float tt = fmodf(g_tTime, fmaxf(a.bperiod, 0.1f));
    const int ticks = (int)floorf(tt * 60.0f);
    if (!port) { const float pp[3] = { 0, 0, 0.5f }; gamePuff(a, c, pp, ticks, bm == BM_BLOOD); return; }
    g_tr = 0x2545F491u ^ ((unsigned)a.tseed * 0x9E3779B9u);
    std::vector<TPart> P;
    float n[3] = { N[0], N[1], N[2] };
    if (bm == BM_BLOOD) { n[0] = 1; n[1] = 0.25f; n[2] = 0; tnorm(n); }   // onPuff: along the shot (from the left here), plus a little up
    burstEmit(P, bm, pos, n);
    for (int k = 0; k < ticks; k++) stepParts(P, 1.0f / 60.0f);
    drawParts(c, P);
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
static const int kNExplo = (int)(sizeof kExplo / sizeof *kExplo);
static const int kExploFrameImg[15] = { 0, 2, 7, 8, 9, 10, 11, 12, 13, 14, 1, 3, 4, 5, 6 };   // g_ExplosionDisplayLists[i] -> SMOKE_n / FIRE_n (assets/oddtextures.c)
static const ExploType &curExplo(const Args &a) { return kExplo[std::max(0, std::min(kNExplo - 1, a.explo))]; }
static float exploLife(const Args &a) { const ExploType &T = curExplo(a); return (T.duration + 16.0f * T.flarespd) / 60.0f + a.etail; }
static int smoke0() {   // the texture number of SMOKE_0 (images.def; FIRE_n = SMOKE_n + 1)
    static int s = -2;
    if (s == -2) {
        s = 0;
        const std::vector<FxTexName> &n = fxTexNames();
        for (size_t i = 0; i < n.size(); i++) if (n[i].name == "SMOKE_0") { s = (int)i; break; }
    }
    return s;
}
// the game's two-cycle explosion combiner: TEXEL0 x TEXEL1, then x SHADE, alpha likewise, TEXEL1 clamped (port/shaders/combiners.txt)
static const uint64_t CC_EXPLO = 0x010d010d0a080a08ull;
static const uint32_t ID_EXPLO = 0x00000c11u;
struct EPart { int frame; float pos[3], size, rot; };
static void sceneExplo(const Args &a, const TabCtx &c, bool port) {
    const ExploType &T = curExplo(a);
    const float tt = fmodf(g_tTime, fmaxf(exploLife(a), 0.1f));
    const int ticks = (int)floorf(tt * 60.0f);
    // explosionTick, one tick at a time (lvupdate 1), parts spawned on every other tick (D486: the N64's pace)
    EPart parts[40] = {};
    unsigned rs = (unsigned)a.tseed * 104729u + 3u;
    auto rf = [&]() { return (lcg(rs) % 32768) / 32768.0f; };
    int age = 0;
    for (int k = 0; k <= ticks; k++) {
        if (age >= 8 && age < T.duration && (age & 1) == 0) {
            const float hr = T.hrange + T.hchange * age, vr = T.vrange + T.vchange * age;
            const int n = (int)(((float)T.proprate * (float)age) / (float)T.duration) + 1;
            for (int s = 0; s < n; s++) for (EPart &p : parts) if (p.frame == 0) {
                p.frame = 1; p.pos[0] = (rf() - 0.5f) * hr; p.pos[1] = (rf() - 0.5f) * vr; p.pos[2] = (rf() - 0.5f) * hr;
                const float sz = (rf() * 0.5f + 1.0f) * T.size, an = rf() * 6.2831853f;
                p.size = cosf(an) * sz; p.rot = sinf(an) * sz;
                break;
            }
        }
        if (k == ticks) break;
        age++;
        for (EPart &p : parts) { if (p.frame > 0) p.frame++; if ((int)(T.flarespd * 15.0f) < p.frame) p.frame = 0; }   // the render's reset, folded in
    }
    std::vector<Light> L;
    {   // dynlights.c addExplosions
        const float life = T.duration + 16.0f * T.flarespd, t = (float)age;
        if (T.size >= 10.0f && T.range > 0 && t < life) {
            float k = 1.0f - t / life;
            k *= k;
            if (t < 4.0f) k *= (t + 1.0f) / 5.0f;
            const float g = 1.0f + 0.25f * log2f(T.size / 100.0f);
            k *= (g < 0.8f ? 0.8f : g > 1.8f ? 1.8f : g) * 1.15f;
            if (k > 0.01f) L.push_back({ { 0, T.size * 0.5f, 0 }, T.range * 4.0f, { 2.6f * k, 1.6f * k, 0.7f * k } });
        }
    }
    tabLights(c, L, port);
    { const float o[3] = { 0, 0, -(T.hrange + T.hchange * T.duration) * 0.5f - T.size * 1.5f }; tabWall(a, c, o, 6000); }
    // explosionRenderPropExplosion: frames 14 .. 0, each with its display list (SMOKE x FIRE, 2 cycles), then its parts
    const View &w = c.vw;
    for (int fi = 14; fi >= 0; fi--) {
        const int img = kExploFrameImg[fi];
        std::vector<TV> v;
        std::vector<uint16_t> idx;
        const Tex &t0 = romTex(smoke0() + 2 * img, false);       // tile 0: IA8 56x56, wrap
        const Tex &t1 = romTex(smoke0() + 2 * img + 1, false);   // tile 1: RGBA16, clamp, shift 2
        const float w0 = (float)(t0.w > 0 ? t0.w : 56), h0 = (float)(t0.h > 0 ? t0.h : 56), w1 = (float)(t1.w > 0 ? t1.w : 16), h1 = (float)(t1.h > 0 ? t1.h : 14);
        for (const EPart &p : parts) {
            if (p.frame <= 0 || fi != (int)((float)(p.frame - 1) / T.flarespd)) continue;
            // explosionRenderPart: pulled towards the eye by min(dist / 2, 100), sizes scaled with it
            const float dv[3] = { p.pos[0] - w.eye[0], p.pos[1] - w.eye[1], p.pos[2] - w.eye[2] };
            const float dist = sqrtf(dv[0] * dv[0] + dv[1] * dv[1] + dv[2] * dv[2]), pull = fminf(dist * 0.5f, 100.0f), f2 = dist == 0 ? 0 : (dist - pull) / dist;
            const float s54 = p.size * f2, s50 = p.rot * f2, cx = w.eye[0] + dv[0] * f2, cy = w.eye[1] + dv[1] * f2, cz = w.eye[2] + dv[2] * f2;
            float R54[3], R50[3], U54[3], U50[3];
            for (int k = 0; k < 3; k++) { R54[k] = w.r[k] * s54; R50[k] = w.r[k] * s50; U54[k] = w.u[k] * s54; U50[k] = w.u[k] * s50; }
            const float q[4][3] = { { cx - R54[0] - U50[0], cy - R54[1] - U50[1], cz - R54[2] - U50[2] }, { cx + R50[0] - U54[0], cy + R50[1] - U54[1], cz + R50[2] - U54[2] },
                                    { cx + R54[0] + U50[0], cy + R54[1] + U50[1], cz + R54[2] + U50[2] }, { cx - R50[0] + U54[0], cy - R50[1] + U54[1], cz - R50[2] + U54[2] } };
            const float tc[4][2] = { { 55, 0 }, { 0, 0 }, { 0, 55 }, { 55, 55 } };   // 1760 = 55 << 5
            const uint16_t b = (uint16_t)v.size();
            for (int k = 0; k < 4; k++) v.push_back({ q[k][0], q[k][1], q[k][2], tc[k][0] / w0, tc[k][1] / h0, 1, 1, 1, 1 });   // g_ExplosionRenderPartDefaultVertex: white
            quadIdx(idx, b);
        }
        if (v.empty()) continue;
        const float us = w0 / (4.0f * w1), vs = h0 / (4.0f * h1), cl1s = (w1 - 0.5f) / w1, cl1t = (h1 - 0.5f) / h1;   // tile 1: shift 2, its own size; clamp bounds as gfx_pc writes them
        program(CC_EXPLO, ID_EXPLO);
        api()->set_use_alpha(true, 0);   // G_RM_ZB_CLD_SURF2: alpha over
        bindTex(0, t0.id ? t0.id : g_white, !a.nearest, 0, 0);
        bindTex(1, t1.id ? t1.id : g_white, !a.nearest, 2, 2);
        std::vector<float> out;
        for (uint16_t i : idx) {   // layout (gfx_pc): position, texture 0 (uv), texture 1 (uv + clamp S, T), input 1 (rgba)
            const TV &q = v[i];
            const float wp[3] = { q.x, q.y, q.z };
            float e[3], c4[4];
            toEye(c.vw, wp, e, false);
            clipOf(c.cam, e, c4);
            out.insert(out.end(), { c4[0], c4[1], c4[2], c4[3], q.u, q.v, q.u * us, q.v * vs, cl1s, cl1t, q.r, q.g, q.b, q.a });
        }
        draw(out, 14);
    }
}

// ---------------------------------------------------------------- panes, captions
static float tabPeriod(const Args &a) {
    if (a.mode == "muzzle") return 1.0f;
    if (a.mode == "tracer") return fmaxf(a.trperiod, 0.05f) * 4.0f;
    if (a.mode == "burst") return fmaxf(a.bperiod, 0.1f);
    if (a.mode == "explosion") return exploLife(a);
    return 1.0f;
}
static bool isTab(const Args &a) { return a.mode == "muzzle" || a.mode == "tracer" || a.mode == "burst" || a.mode == "explosion"; }
static void tabTick(const Args &a, float dt) {
    if (!isTab(a) || !g_tPlay) return;
    g_tTime += dt * g_tSpeed;
    const float P = tabPeriod(a);
    if (g_tTime >= P) { if (g_tLoop) g_tTime = fmodf(g_tTime, P); else { g_tTime = P - 1e-4f; g_tPlay = false; } }
}
static int tshowOf(const Args &a) { return a.tshow == "game" || a.tshow == "0" ? 0 : a.tshow == "port" || a.tshow == "1" ? 1 : 2; }
static void drawTabPane(const Args &a, const Pane &p, int H, bool port, int c0, int c1) {
    paneState(p, H, c0, c1);
    const float bg[4] = { 0.05f, 0.05f, 0.06f, 1 };
    api()->set_depth_mode(false, false, false, false, 0);
    api()->set_use_alpha(false, 0);
    quadFlat(-1, -1, 1, 1, bg);
    const float aspect = (float)p.w / (float)p.h;
    if (a.mode == "muzzle") {
        const FxGun *G = curGun(a);
        const float tgt[3] = { G ? (G->off[0] - G->size[0] * 0.5f) * a.mscale : 0, 0, 0 };
        sceneMuzzle(a, tabCtx(a, tgt, G ? fmaxf(G->size[0], G->size[1]) * a.mscale * 3.2f : 100.0f, aspect), port);
    } else if (a.mode == "tracer") { const float tgt[3] = { 0, 0, 0 }; sceneTracer(a, tabCtx(a, tgt, a.trrange * 0.6f, aspect), port); }
    else if (a.mode == "burst") { const float tgt[3] = { 0, 10, 0 }; sceneBurst(a, tabCtx(a, tgt, 260.0f, aspect), port); }
    else { const ExploType &T = curExplo(a); const float tgt[3] = { 0, 0, 0 }; sceneExplo(a, tabCtx(a, tgt, (T.hrange + T.size * 2.5f + T.hchange * T.duration) * 3.0f + 60.0f, aspect), port); }
}
static std::string tabCaption(const Args &a) {
    char b[400];
    const float P = tabPeriod(a);
    if (a.mode == "muzzle") {
        const FxGun *G = curGun(a);
        if (!G) return "muzzle: " + fxGunNote();
        snprintf(b, sizeof b, "Muzzle flash: Pchr%s GUNFIRE  size %.0fx%.0fx%.0f (model units, x%.2f)  tex %u %dx%d  %.1f shots/s, %d ticks each | t %.2f / %.2f s",
                 G->name.c_str(), G->size[0], G->size[1], G->size[2], a.mscale, G->tex, G->w, G->h, a.mrate, a.mvis, g_tTime, P);
    } else if (a.mode == "tracer") {
        static const char *it[3] = { "gun (flareimage3)", "laser (flareimage4)", "watch laser (flareimage4)" };
        snprintf(b, sizeof b, "Tracer: %s  range %.0f | port: streak 9000/s, TRACER kind, additive | t %.3f / %.2f s", it[tracerItem(a)], a.trrange, g_tTime, P);
    } else if (a.mode == "burst") {
        const int bm = burstIndex(a.burst);
        snprintf(b, sizeof b, "Impact burst: %s | game: %s puff (smoke frames, 11 ticks) | t %.2f / %.2f s", kBurstName[bm], bm == BM_BLOOD ? "blood" : "wall", g_tTime, P);
    } else {
        const ExploType &T = curExplo(a);
        snprintf(b, sizeof b, "Explosion type %d: size %.0f, duration %d, flare speed %.0f, range %.0f | port: + dynlights (radius %.0f) | t %.2f / %.2f s",
                 a.explo, T.size, T.duration, T.flarespd, T.range, T.range * 4.0f, g_tTime, P);
    }
    return b;
}
