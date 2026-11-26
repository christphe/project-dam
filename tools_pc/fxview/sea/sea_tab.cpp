/* The sea and sky tab of fxview: see sea_tab.h. Ported from tools_pc/seaview (the Win32 + OpenGL viewer and the deko3d console
 * viewer, both removed): the same settings table, ini format, level presets, test scene and camera, drawn through ge_render. */
#include "sea_tab.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

#include "ge_render.h"
#include "ge_render_tool.h"
#include "gfx_cc.h"
#include "imgui.h"

#include <ctype.h>
#include "skyplus_core.h"
#include "cloudtex.h"
#include "clouds.h"
#include "img.h"
#include "waterbake.h"
#include "sea_env.h"

namespace {

/* ---- the settings table ------------------------------------------------------------------------------------ */
/* kind 0: shader value PRM(idx) (default = the game's constant); kind 1: a member of the sky block, u[idx] (the capture's value is
 * the default); kind 2: sun elevation, sun azimuth (degrees), eye height; kind 3: the water surface mode (the Level preset sets them) */
struct Par { const char* name; int kind; int idx; float def; };
const Par kPar[] = {
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
    { "Water plane height", 3, 0, 0.0f }, { "Water texture tile (units per repeat)", 3, 1, 256.0f }, { "Patch map size (repeats)", 3, 2, 6.0f },
    { "reflectLit (0..1)", 3, 3, 0.0f }, { "PRIM_LOD_FRAC (texture blend)", 3, 4, 0.5f }, { "2nd texture scale", 3, 5, 0.7f },
    { "Fog end (0 = no fog)", 3, 6, 12000.0f }, { "Water only, no sky (cave) 0/1", 3, 7, 0.0f },
    { "Deep colour R", 3, 8, 0.10f }, { "Deep colour G", 3, 9, 0.20f }, { "Deep colour B", 3, 10, 0.22f },
    { "Light colour R", 3, 11, 0.30f }, { "Light colour G", 3, 12, 0.45f }, { "Light colour B", 3, 13, 0.48f },
    { "Shade R", 3, 14, 0.30f }, { "Shade G", 3, 15, 0.34f }, { "Shade B", 3, 16, 0.36f },
    { "Fog colour R", 3, 17, 0.50f }, { "Fog colour G", 3, 18, 0.58f }, { "Fog colour B", 3, 19, 0.65f },
    { "Tone keep (reflection in the body hue)", 3, 20, 0.8f }, { "Reflection gain", 3, 21, 0.6f },
};
const int NPAR = (int)(sizeof kPar / sizeof *kPar);
const int WATER0 = 38;   /* the first kind-3 entry */
const char* const kTabs[] = { "Sun", "Sky", "Clouds", "Sea", "Fresnel / bake", "Water surface" };
int tabOf(int i) { return kPar[i].kind == 3 ? 5 : i < 7 ? 0 : i < 12 ? 1 : i < 17 ? 2 : i < 29 ? 3 : i < 36 ? 4 : 2; }
const int kColFirst[] = { 3, 7, 19, 46, 49, 52, 55 };   /* the R of each colour triple (G and B follow) */
const char* const kColName[] = { "Sun colour", "Sky colour", "Water colour (tint)", "Deep colour", "Light colour", "Shade (vertex colour)", "Fog colour" };
int colourOf(int i) { for (int k = 0; k < 7; k++) if (i >= kColFirst[k] && i < kColFirst[k] + 3) return k; return -1; }
const char* const kSeaNames[] = { "current", "no medium swell", "1 layer", "2 layers", "3 layers", "bump, 1 layer (sunset light)",
    "bump, 2 layers (sunset light)", "no sky reflection", "all baked (nothing live)", "baked from 600-2500", "baked from 300-1500",
    "no calm patches", "2 layers, reflection, no swell or patches" };
const char* const kSkyNames[] = { "current", "1 cloud layer", "2 cloud layers", "no cloud lighting", "no halo or stars",
    "sky = low-res panorama", "flat sky (cost floor)" };
const char* const kTexNames[] = { "game texture (GE)", "soft fbm", "contrasted fbm", "cumulus", "stretched cirrus", "clear sky" };
const char* const kLevelNames[] = { "dam", "caverns", "complex" };
const char* const kScaleNames[] = { "1/1 (full)", "1/2", "1/4" };

/* Level presets: the level's own water draw as the game would see it (guesses until real captures exist) */
struct Preset { float h, tile, patch, reflectLit, fogEnd, noSky, shade[3], fog[3], tint[3], swell, chop, speed, eyeY; };
const Preset kPreset[3] = {
    /* Dam      */ { 0, 256, 6, 0, 14000, 0, { 0.28f, 0.30f, 0.30f }, { 0.52f, 0.60f, 0.68f }, { 1.0f, 1.0f, 1.0f }, 0.3f, 0.6f, 0.7f, 260 },
    /* Caverns  */ { 0, 200, 6, 0, 3500, 1, { 0.55f, 0.60f, 0.60f }, { 0.11f, 0.14f, 0.15f }, { 0.5f, 0.6f, 0.65f }, 0.3f, 0.6f, 0.7f, 160 },
    /* Complex  */ { 0, 256, 6, 1, 6000, 0, { 0.45f, 0.50f, 0.52f }, { 0.22f, 0.26f, 0.28f }, { 1.0f, 1.0f, 1.0f }, 0.3f, 0.6f, 0.7f, 160 },
};

/* The levels' environment records: the game's own table (src/game/bgfog.c fog_tables / fog_tables2, cut out at build time and compiled
 * with the game's headers: sea_env.h), every level that has one. name: a label for the stage number; eyeY: the tool's camera height
 * (not game data). farFog: the record's far fog (g_ScaledFarFogIntensity in the game: the exact scaling is the game's; the band it
 * feeds is clamped); 3.4e38 for a record without fog. */
struct LevelEnv { std::string name; int stage; float rgb[3]; int clouds; float rep; float cloud[3]; int isWater; float waterRep; float farFog; float eyeY; int waterImageId; };
const char* stageLabel(int stage) {
    switch (stage) { case 26: return "frigate"; case 33: return "dam"; case 39: return "caverns"; case 31: return "complex"; case 36: return "surface"; case 29: return "streets";
                     case 35: return "runway"; case 38: return "temple"; case 41: return "cradle"; default: return nullptr; }
}
std::vector<LevelEnv>& levels() {
    static std::vector<LevelEnv> v;
    if (v.empty()) {
        SeaEnvRec r;
        for (int i = 0; seaEnvGet(i, &r); i++) {
            LevelEnv L = {};
            L.stage = r.id;
            const char* lbl = stageLabel(r.id);
            L.name = lbl ? lbl : "stage" + std::to_string(r.id);
            for (int k = 0; k < 3; k++) { L.rgb[k] = (float)r.rgb[k]; L.cloud[k] = r.cloud[k]; }
            L.clouds = r.clouds; L.rep = r.cloudRepeat; L.isWater = r.isWater; L.waterRep = r.waterRepeat; L.farFog = r.farFog; L.waterImageId = r.waterImageId;
            L.eyeY = r.id == 26 ? 670.0f : (r.id == 39 || r.id == 31) ? 160.0f : 260.0f;
            v.push_back(L);
        }
    }
    return v;
}
int nLevels() { return (int)levels().size(); }
const int IMAGE_CLOUDS_GRAYSCALE_NUM = 2228;   /* the image number of bg/clouds (assets/image_externs.h IMAGEIDS) */

/* ---- vector helpers ---------------------------------------------------------------------------------------- */
struct V3 { float x, y, z; };
V3 add(V3 a, V3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
V3 sub(V3 a, V3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
V3 mul(V3 a, float s) { return { a.x * s, a.y * s, a.z * s }; }
float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
float len(V3 a) { return sqrtf(dot(a, a)); }
V3 nrm(V3 a) { return mul(a, 1.0f / len(a)); }
V3 cross(V3 a, V3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
V3 rotAxis(V3 v, V3 k, float a) { const float c = cosf(a), s = sinf(a); return add(add(mul(v, c), mul(cross(k, v), s)), mul(k, dot(k, v) * (1 - c))); }

/* ---- state ------------------------------------------------------------------------------------------------ */
struct State {
    bool ok = false;
    std::string dataDir = "data", romArg, imgArg, iniPath, wtexArg, view, status;
    int stageIdx = 0;
    std::string romData, imgListUsed;   /* the user's ROM (when it was read) and the image list that matched it */
    int waterImageId = 0;
    bool romOk = false;
    SkyPlusWaterSettings ws;
    float U[64] = {};
    unsigned char cloud[4096] = {}, wave[65536] = {};
    V3 cen = {}, hw = {}, hh = {}, eye0 = {}, sf = {}, sr = {};
    float ysign = 1.0f;
    float val[NPAR] = {}, def[NPAR] = {};
    int mode = 0, level = 0, seaVar = 0, skyVar = 0, texSel = 0, rep = 1, scale = 0;
    int modeArg = -1;
    bool levelArg = false;
    bool scene = true, sway = false, freeze = false, haveCam = false, haveTime = false;
    float cam[5] = {}, fixedTime = 0;
    double T = 0;
    std::string wtexPath;
    bool wtexDirty = true, texDirty = true, viewerOn = false;
    img::Image wimg;
    uint32_t wTex = 0;
    unsigned char wmap[waterbake::MW * waterbake::MH * 4] = {};
    bool wmapOk = false;
    unsigned char cloudTex[4096] = {};
    int texGen = 1, wtexGen = 1, wmapGen = 0;
    bool resetBake = true;
    float prevVal[NPAR] = {};
    int prevVar[3] = { -1, -1, -1 };
    float bakeEye[3] = {};
    std::vector<std::string> sets;   /* --set Name=value */
    PortSkyPlus p;
    double gpuMs = -1;
    char shot[512] = "";
    char iniBuf[260] = "seatab.ini", wtexBuf[260] = "";
    std::vector<float> grid;   /* the water plane's grid, (GN + 1)^2 vertices, x y in -1..1 */
    std::vector<float> vb;
    uint64_t waterId0 = 0, sceneId0 = 0;
};
State g;
const int GN = 96;   /* cells per side of the water plane's grid */

int parIdx(const char* name) { for (int i = 0; i < NPAR; i++) if (!strcmp(kPar[i].name, name)) return i; return -1; }
void setVal(const char* name, float v) { const int i = parIdx(name); if (i >= 0) g.val[i] = v; }

void applyLevel(int lv) {
    if (lv < 0 || lv > 2) return;
    const Preset& p = kPreset[lv];
    g.level = lv;
    setVal("Water plane height", p.h); setVal("Water texture tile (units per repeat)", p.tile); setVal("Patch map size (repeats)", p.patch);
    setVal("Fog end (0 = no fog)", p.fogEnd); setVal("Water only, no sky (cave) 0/1", p.noSky);
    static const char* sfx[3] = { " R", " G", " B" };
    for (int i = 0; i < 3; i++) {
        setVal((std::string("Shade") + sfx[i]).c_str(), p.shade[i]); setVal((std::string("Fog colour") + sfx[i]).c_str(), p.fog[i]);
    }
    setVal("Eye height", p.eyeY);
    g.wtexPath.clear();
    g.wtexDirty = true;
}

void resetAll() { for (int i = 0; i < NPAR; i++) g.val[i] = g.def[i]; g.seaVar = g.skyVar = 0; }

std::string readText(const std::string& p) {
    std::string t;
    FILE* f = fopen(p.c_str(), "rb");
    if (!f) return t;
    char b[4096];
    size_t n;
    while ((n = fread(b, 1, sizeof b, f))) t.append(b, n);
    fclose(f);
    return t;
}

/* the user's ROM: --rom, N64_ROM / FXVIEW_ROM in the environment, N64_ROM in a .env up the directory tree, then the game's data dir (--data) */
std::string findRom() {
    if (!g.romArg.empty()) return g.romArg;
    for (const char* k : { "FXVIEW_ROM", "N64_ROM" }) if (const char* v = getenv(k)) if (*v) return v;
    std::string dir = ".";
    for (int up = 0; up < 8; up++, dir += "/..") {
        const std::string e = readText(dir + "/.env");
        size_t at = e.find("N64_ROM=");
        if (at != std::string::npos && (at == 0 || e[at - 1] == '\n')) {
            size_t end = e.find_first_of("\r\n", at);
            return e.substr(at + 8, end == std::string::npos ? std::string::npos : end - at - 8);
        }
    }
    /* the game's own data dir (--data): the file names romdata.c looks for */
    if (!g.dataDir.empty())
        for (const char* n : { "ge007.ntsc-final.z64", "ge007.pal-final.z64", "ge007.jpn-final.z64", "baserom.u.z64", "baserom.e.z64", "baserom.j.z64" })
            if (FILE* f = fopen((g.dataDir + "/" + n).c_str(), "rb")) { fclose(f); return g.dataDir + "/" + n; }
    return "";
}

/* GE's cloud texture (bg/clouds, IA8 64x64) from the ROM, as skyPlusTexture makes it; else a generated one */
void loadClouds() {
    g.romOk = false;
    const std::string romPath = findRom();
    std::string rom = romPath.empty() ? std::string() : readText(romPath);
    if (rom.size() >= 0x400000) {
        const char reg = rom[0x3E] == 'E' ? 'u' : rom[0x3E] == 'P' ? 'e' : rom[0x3E] == 'J' ? 'j' : 0;
        std::vector<std::string> lists;
        if (!g.imgArg.empty()) lists.push_back(g.imgArg);
        for (const char* d : { ".", "..", "../..", "../../..", "../../../.." }) lists.push_back(std::string(d) + "/imagelist." + (reg ? reg : 'u') + ".csv");
        lists.push_back(g.dataDir + "/imagelist." + (reg ? reg : 'u') + ".csv");
        std::string why = "no imagelist.<region>.csv";
        unsigned char ci[4096];
        for (const std::string& l : lists) {
            if (!readText(l).empty() && cloudTexFromRom((const uint8_t*)rom.data(), rom.size(), l.c_str(), IMAGE_CLOUDS_GRAYSCALE_NUM, ci, why)) {
                memcpy(g.cloud, ci, 4096);
                g.romOk = true;
                g.imgListUsed = l;
                fprintf(stderr, "seatab: cloud texture from %s\n", romPath.c_str());
                break;
            }
        }
        if (g.romOk) g.romData.swap(rom);   /* kept: the level's water texture comes from it too */
        if (!g.romOk) fprintf(stderr, "seatab: the cloud texture could not be read from %s (%s)\n", romPath.c_str(), why.c_str());
    } else {
        fprintf(stderr, "seatab: no ROM (--rom FILE, N64_ROM in .env): a generated cloud texture\n");
    }
    if (!g.romOk) { g.texSel = g.texSel ? g.texSel : 1; unsigned char none[4096] = {}; makeCloudTex(g.texSel, none, g.cloud); }
}

/* the sky block (the layout of the old capture, the settings table's indices) from a PortSkyPlus */
void packU(const PortSkyPlus& p, float* u) {
    memset(u, 0, 256);
    for (int k = 0; k < 3; k++) { u[24 + k] = p.eye[k]; u[28 + k] = p.sky[k]; u[32 + k] = p.cloud[k]; u[44 + k] = p.sunDir[k]; u[48 + k] = p.sunCol[k];
                                  u[52 + k] = p.moonDir[k]; u[56 + k] = p.water[k]; }
    u[7] = p.band; u[27] = p.cloudY; u[31] = p.gain; u[35] = p.texScale;
    u[36] = p.layerOfs[0][0]; u[37] = p.layerOfs[0][1]; u[38] = p.layerOfs[1][0]; u[39] = p.layerOfs[1][1]; u[40] = p.layerOfs[2][0]; u[41] = p.layerOfs[2][1];
    u[42] = p.sunGlow; u[43] = p.night ? 1.0f : 0.0f; u[47] = p.swell; u[51] = p.chop; u[55] = p.speed; u[59] = p.seaY; u[61] = p.sea ? 1.0f : 0.0f;
}

/* the camera's basis: the base view looks along -z with the horizontal half-width 0.95 (16:9 gives a 56 degree vertical field) */
void baseCamera() {
    g.cen = { 0, 0, -1 };
    g.hw = { 0.95f, 0, 0 };
    g.hh = { 0, 1, 0 };
    g.sf = nrm({ g.cen.x, 0, g.cen.z });
    g.sr = nrm({ g.hw.x, 0, g.hw.z });
    const V3 t = cross({ 0, 1, 0 }, g.sf);   /* turning right goes toward the camera's right, whatever the handedness of the axes */
    g.ysign = t.x * g.sr.x + t.z * g.sr.z > 0 ? 1.0f : -1.0f;
}

/* a level: its environment record, its water settings ($S/water/<level>.ini under the data dir) and the sky block the game would make for it */
void selectLevel(int idx) {
    if (idx < 0 || idx >= nLevels()) idx = 0;
    g.stageIdx = idx;
    const LevelEnv& L = levels()[idx];
    g.waterImageId = L.waterImageId;
    skyPlusCoreWaterDefaults(L.stage, &g.ws);
    if (const char* n = skyPlusCoreWaterName(L.stage)) {
        if (skyPlusCoreWaterIni((g.dataDir + "/water/" + n + ".ini").c_str(), &g.ws)) fprintf(stderr, "seatab: %s/water/%s.ini\n", g.dataDir.c_str(), n);
    }
    PortSkyPlus p0;
    memset(&p0, 0, sizeof p0);
    SkyPlusInput in = {};
    in.stage = L.stage;
    in.red = L.rgb[0]; in.green = L.rgb[1]; in.blue = L.rgb[2];
    in.cloudRed = L.cloud[0]; in.cloudGreen = L.cloud[1]; in.cloudBlue = L.cloud[2]; in.cloudRepeat = L.rep;
    in.eye[0] = 0; in.eye[1] = L.eyeY; in.eye[2] = 0;
    in.farFog = L.farFog;
    skyPlusCoreSky(&p0, &in);
    skyPlusCoreWater(&p0, &g.ws, L.isWater, L.waterRep);
    packU(p0, g.U);
    g.eye0 = { 0, L.eyeY, 0 };
    g.mode = L.isWater ? 0 : (skyPlusCoreWaterName(L.stage) ? 1 : 2);
    for (int i = 0; i < NPAR; i++) {
        const Par& q = kPar[i];
        g.def[i] = q.def;
        if (q.kind == 1) g.def[i] = g.U[q.idx];
        else if (q.kind == 2) g.def[i] = q.idx == 0 ? asinf(fminf(fmaxf(g.U[45], -1.0f), 1.0f)) * 57.29578f : q.idx == 1 ? atan2f(g.U[44], g.U[46]) * 57.29578f : g.U[25];
        g.val[i] = g.def[i];
    }
    const int lv = L.stage == 33 ? 0 : L.stage == 39 ? 1 : L.stage == 31 ? 2 : 0;
    applyLevel(lv);   /* the water surface mode's presets (plane, fog, shade); the settings below are the level's own */
    setVal("Eye height", L.eyeY);
    if (L.stage == 39 || !L.clouds) setVal("Water only, no sky (cave) 0/1", 1.0f); else setVal("Water only, no sky (cave) 0/1", 0.0f);
    setVal("reflectLit (0..1)", g.ws.reflectLit); setVal("Patch map size (repeats)", g.ws.patchScale);
    setVal("Tone keep (reflection in the body hue)", g.ws.toneKeep); setVal("Reflection gain", g.ws.reflectGain);
    for (int i = 0; i < NPAR; i++) g.def[i] = g.val[i];
    g.wtexDirty = true;
    g.resetBake = true;
}

void loadWater() {
    img::Image im;
    std::string used;
    if (!g.wtexPath.empty()) {   /* an explicit file (--wtex) wins */
        if (img::loadImage(g.wtexPath, im)) used = g.wtexPath; else fprintf(stderr, "seatab: water texture %s: not readable\n", g.wtexPath.c_str());
    }
    if (used.empty() && g.romOk) {   /* the game's way: skywaterimages[WaterImageId] of the level's environment record (src/game/sky.c) */
        /* a record whose WaterImageId is 0 (Dam, Complex...) has its water draws textured by the level's own room data, which a tool without the
         * level loader cannot reach: the first water image of the table stands in */
        const int id = g.waterImageId > 0 ? g.waterImageId : 1;
        int w = 0, h = 0;
        std::string why;
        if (waterTexFromRom((const uint8_t*)g.romData.data(), g.romData.size(), g.imgListUsed.c_str(), id, &w, &h, im.px, why)) {
            im.w = w; im.h = h;
            used = "ROM skywaterimages[" + std::to_string(id) + "] (" + why + ")" + (g.waterImageId > 0 ? "" : ", the level's own water texture is in its room data: stand-in");
        } else fprintf(stderr, "seatab: the water texture could not be read from the ROM (%s)\n", why.c_str());
    }
    if (used.empty()) {
        im.w = im.h = 64; waterbake::placeholder(g.level + 1, im.px); used = "(generated placeholder: no ROM)";
    }
    std::vector<unsigned char> bsrc = im.px;
    int bw = im.w, bh = im.h;
    while (bw > 128 || bh > 128) {   /* the bake takes up to 128 wide: box-reduce by an integer factor */
        const int nw = bw / 2, nh = bh / 2;
        std::vector<unsigned char> t((size_t)nw * nh * 4);
        for (int y = 0; y < nh; y++) for (int x = 0; x < nw; x++) for (int c = 0; c < 4; c++)
            t[((size_t)y * nw + x) * 4 + c] = (bsrc[((size_t)2 * y * bw + 2 * x) * 4 + c] + bsrc[((size_t)2 * y * bw + 2 * x + 1) * 4 + c] +
                                               bsrc[((size_t)(2 * y + 1) * bw + 2 * x) * 4 + c] + bsrc[((size_t)(2 * y + 1) * bw + 2 * x + 1) * 4 + c]) / 4;
        bsrc.swap(t); bw = nw; bh = nh;
    }
    float deep[3], light[3];
    if (!waterbake::bake(bsrc.data(), bw, bh, g.wmap, deep, light)) { fprintf(stderr, "seatab: water bake: texture %dx%d not usable\n", bw, bh); return; }
    g.wimg = im;
    g.wmapOk = true;
    g.wmapGen++;
    for (int i = 0; i < 3; i++) {   /* loading a texture writes the baked colours into the settings */
        g.val[parIdx("Deep colour R") + i] = deep[i];
        g.val[parIdx("Light colour R") + i] = light[i];
    }
    struct GfxRenderingAPI* api = ge_render_api();
    if (!g.wTex) g.wTex = api->new_texture();
    api->select_texture(0, g.wTex, true);
    api->upload_texture(g.wimg.px.data(), (uint32_t)g.wimg.w, (uint32_t)g.wimg.h, true);
    fprintf(stderr, "seatab: water texture: %s %dx%d; deep %.2f,%.2f,%.2f light %.2f,%.2f,%.2f\n", used.c_str(), im.w, im.h,
            deep[0], deep[1], deep[2], light[0], light[1], light[2]);
}

/* the camera: the capture's centre ray and half-width / half-height vectors turned by the camera */
struct View { V3 eye, cc, hx, hy; };
View makeView(const SeaTabCam& c, float aspectHW, float sway, float roll) {
    V3 hh = mul(nrm(g.hh), len(g.hw) * aspectHW);
    const V3 rax = nrm(g.hw), up0 = { 0, 1, 0 };
    V3 cc = rotAxis(g.cen, rax, c.pitch + sway), hx = g.hw, hy = rotAxis(hh, rax, c.pitch + sway);
    cc = rotAxis(cc, up0, c.yaw); hx = rotAxis(hx, up0, c.yaw); hy = rotAxis(hy, up0, c.yaw);
    if (roll != 0) { const V3 ax = nrm(cc); hx = rotAxis(hx, ax, roll); hy = rotAxis(hy, ax, roll); }
    return { { c.eye[0], c.eye[1], c.eye[2] }, cc, hx, hy };
}

/* the clip position of a world point (the camera's frame as the water vertex shader of the viewers had it; w = depth along cc) */
struct Proj { View v; float N, F; bool z01, flipY; };
void clipOf(const Proj& p, V3 P, float out[4]) {
    const V3 Q = sub(P, p.v.eye);
    const float w = dot(Q, p.v.cc) / dot(p.v.cc, p.v.cc);
    const float cx = dot(Q, p.v.hx) / dot(p.v.hx, p.v.hx), cy = dot(Q, p.v.hy) / dot(p.v.hy, p.v.hy);
    float z = ((p.F + p.N) * w - 2.0f * p.F * p.N) / (p.F - p.N);
    if (p.z01) z = 0.5f * (z + w);
    out[0] = cx; out[1] = p.flipY ? -cy : cy; out[2] = z; out[3] = w;
}

void fillSky(PortSkyPlus& p, const float* u, bool water, bool noSky) {
    memset(&p, 0, sizeof p);
    for (int i = 0; i < 4; i++) for (int k = 0; k < 3; k++) p.corner[i][k] = u[8 + i * 4 + k];
    for (int k = 0; k < 3; k++) { p.eye[k] = u[24 + k]; p.sky[k] = u[28 + k]; p.cloud[k] = u[32 + k]; p.sunDir[k] = u[44 + k]; p.sunCol[k] = u[48 + k];
                                   p.moonDir[k] = u[52 + k]; p.water[k] = u[56 + k]; }
    p.cloudY = u[27]; p.gain = u[31]; p.texScale = u[35];
    p.layerOfs[0][0] = u[36]; p.layerOfs[0][1] = u[37]; p.layerOfs[1][0] = u[38]; p.layerOfs[1][1] = u[39]; p.layerOfs[2][0] = u[40]; p.layerOfs[2][1] = u[41];
    p.sunGlow = u[42]; p.night = u[43] != 0.0f; p.swell = u[47]; p.chop = u[51]; p.speed = u[55]; p.seaY = u[59]; p.time = u[60];
    p.band = u[7];
    p.texGen = g.texGen; p.tex = g.cloudTex;
    p.waterTexGen = g.wtexGen; p.waterTex = g.wave;
    p.sea = (!water && g.mode == 0 && levels()[g.stageIdx].isWater) ? 1 : 0;
    p.bakeReady = 1;
    p.bakeReset = g.resetBake ? 1 : 0;
    for (int k = 0; k < 3; k++) p.bakeEye[k] = g.bakeEye[k];
    if (water) {
        auto W = [&](int k) { return g.val[WATER0 + k]; };
        for (int k = 0; k < 3; k++) { p.deep[k] = W(8 + k); p.light[k] = W(11 + k); }
        p.wmapGen = g.wmapGen;
        p.wmap = g.wmapOk ? g.wmap : nullptr;
        p.reflectLit = W(3); p.patchScale = W(2); p.toneKeep = W(20); p.reflectGain = W(21); p.worldScale = 1.0f;
        p.waterOnly = noSky ? 1 : 0;
        if (noSky) for (int k = 0; k < 3; k++) p.sky[k] = W(17 + k);   /* waterShade mirrors the sky (the horizon colour) when there is no sky */
    }
}

/* ---- drawing ----------------------------------------------------------------------------------------------- */
void drawTris(struct GfxRenderingAPI* api, const float* v, size_t nverts, size_t stride) {
    const size_t per = 255 * 3;
    for (size_t at = 0; at < nverts; at += per) {
        const size_t n = nverts - at < per ? nverts - at : per;
        api->draw_triangles(const_cast<float*>(v + at * stride), n * stride, n / 3);
    }
}

void useProgram(struct GfxRenderingAPI* api, uint64_t id0, uint32_t id1) {
    struct ShaderProgram* prg = api->lookup_shader(id0, id1);
    if (!prg) prg = api->create_and_load_new_shader(id0, id1);
    api->load_shader(prg);
}

/* a water surface: the plane (grid denser near the eye, centred on it) or a rectangle (a pool); 18 floats per vertex */
void waterMesh(std::vector<float>& out, const Proj& pj, bool plane, float cx, float cz, float ea, float eb, V3 ax1, V3 ax2, float y, float tile,
               float scale2, float fogEnd, float lod, const float shade[3], const float fogc[3], float T) {
    const int n = GN + 1;
    std::vector<float> vtx((size_t)n * n * 18);
    for (int j = 0; j < n; j++) for (int i = 0; i < n; i++) {
        float gx = g.grid[((size_t)j * n + i) * 2], gy = g.grid[((size_t)j * n + i) * 2 + 1];
        if (plane) { gx *= fabsf(gx); gy *= fabsf(gy); }
        const float px = cx + ax1.x * (gx * ea) + ax2.x * (gy * eb), pz = cz + ax1.z * (gx * ea) + ax2.z * (gy * eb);
        const V3 P = { px, y, pz };
        float c[4];
        clipOf(pj, P, c);
        float* o = &vtx[((size_t)j * n + i) * 18];
        const float u0 = px / tile, v0 = pz / tile;
        const float fogA = fogEnd > 0 ? fminf(fmaxf(len(sub(P, pj.v.eye)) / fogEnd, 0.0f), 1.0f) : 0.0f;
        o[0] = c[0]; o[1] = c[1]; o[2] = c[2]; o[3] = c[3];
        o[4] = u0; o[5] = v0;
        o[6] = u0 * scale2 + 0.31f + 0.02f * T; o[7] = v0 * scale2 + 0.17f + 0.013f * T;
        o[8] = fogc[0]; o[9] = fogc[1]; o[10] = fogc[2]; o[11] = fogA;
        o[12] = o[13] = o[14] = lod;
        o[15] = shade[0]; o[16] = shade[1]; o[17] = shade[2];
    }
    for (int j = 0; j < GN; j++) for (int i = 0; i < GN; i++) {
        const size_t a = (size_t)j * n + i, b = a + 1, c = a + n, d = c + 1;
        for (size_t k : { a, c, b, b, c, d }) out.insert(out.end(), &vtx[k * 18], &vtx[k * 18] + 18);
    }
}

/* the test scene's boxes in the frame of the capture's eye (a ahead, b to the right): flat-lit triangles, 8 floats per vertex */
void sceneMesh(std::vector<float>& out, const Proj& pj, float h0, V3 L) {
    struct Box { float a0, a1, b0, b1, y0, y1, c[3]; };
    const Box boxes[] = {
        { 300, 1200, 100, 260, h0 - 80, h0 + 60, { 0.55f, 0.55f, 0.50f } },   /* the pier: a slab half in the water */
        { 430, 550, -310, -190, h0 - 100, h0 + 140, { 0.70f, 0.35f, 0.30f } }, { 640, 760, -180, -60, h0 - 100, h0 + 120, { 0.30f, 0.55f, 0.35f } },
        { 900, 1020, -400, -280, h0 - 100, h0 + 160, { 0.65f, 0.60f, 0.30f } }, { 770, 800, -45, -15, h0 - 100, h0 + 320, { 0.9f, 0.9f, 0.9f } },   /* a thin pole */
        { 2500, 2600, 0, 6000, h0 - 300, h0 + 500, { 0.50f, 0.50f, 0.52f } },   /* the shore: a wall the plane ends against */
        { 1092, 1100, -2400, -600, h0 - 20, h0 + 170, { 0.55f, 0.55f, 0.50f } },   /* the raised pool's front rim */
    };
    static const int fc[6][4] = { { 0, 2, 6, 4 }, { 1, 3, 7, 5 }, { 0, 1, 5, 4 }, { 2, 3, 7, 6 }, { 0, 1, 3, 2 }, { 4, 5, 7, 6 } };
    const V3 nr[6] = { { -g.sf.x, 0, -g.sf.z }, { g.sf.x, 0, g.sf.z }, { -g.sr.x, 0, -g.sr.z }, { g.sr.x, 0, g.sr.z }, { 0, -1, 0 }, { 0, 1, 0 } };
    for (const Box& bx : boxes) {
        const float A[2] = { bx.a0, bx.a1 }, B[2] = { bx.b0, bx.b1 }, Y[2] = { bx.y0, bx.y1 };
        for (int f = 0; f < 6; f++) {
            const float lit = 0.35f + 0.65f * fmaxf(dot(nr[f], L), 0.0f);
            for (int t = 0; t < 6; t++) {
                const int c = fc[f][(t < 3) ? t : (t == 3 ? 0 : t - 2)];
                const float a = A[c & 1], b = B[(c >> 1) & 1], y = Y[(c >> 2) & 1];
                const V3 P = { g.eye0.x + g.sf.x * a + g.sr.x * b, y, g.eye0.z + g.sf.z * a + g.sr.z * b };
                float cl[4];
                clipOf(pj, P, cl);
                const float v[8] = { cl[0], cl[1], cl[2], cl[3], bx.c[0] * lit, bx.c[1] * lit, bx.c[2] * lit, 1.0f };
                out.insert(out.end(), v, v + 8);
            }
        }
    }
}

void colourQuad(struct GfxRenderingAPI* api, const Proj& pj, const float rgb[3]) {   /* the whole viewport, at the far plane */
    const float z = pj.z01 ? 1.0f : 1.0f;
    const float v[6][8] = { { -1, -1, z, 1, rgb[0], rgb[1], rgb[2], 1 }, { 1, -1, z, 1, rgb[0], rgb[1], rgb[2], 1 }, { -1, 1, z, 1, rgb[0], rgb[1], rgb[2], 1 },
                            { -1, 1, z, 1, rgb[0], rgb[1], rgb[2], 1 }, { 1, -1, z, 1, rgb[0], rgb[1], rgb[2], 1 }, { 1, 1, z, 1, rgb[0], rgb[1], rgb[2], 1 } };
    useProgram(api, g.sceneId0, SHADER_OPT_ALPHA);
    api->set_depth_mode(false, false, false, false, 0);
    api->set_use_alpha(false, 0);
    drawTris(api, &v[0][0], 6, 8);
}

void viewerBlock(GeRenderViewer* vb) {
    memset(vb, 0, sizeof *vb);
    vb->var[0] = g.seaVar;
    vb->var[1] = g.skyVar;
    for (int i = 0; i < NPAR; i++) if (kPar[i].kind == 0 && kPar[i].idx < 24) vb->prm[kPar[i].idx] = g.val[i];
}

}   // namespace

/* ---- the public API ---------------------------------------------------------------------------------------- */

bool seaTabParseArg(int argc, char** argv, int* i) {
    auto nx = [&]() -> const char* { return *i + 1 < argc ? argv[++*i] : "0"; };
    const std::string a = argv[*i];
    if (a == "--rom") g.romArg = nx();
    else if (a == "--data") g.dataDir = nx();
    else if (a == "--imagelist") g.imgArg = nx();
    else if (a == "--ini") g.iniPath = nx();
    else if (a == "--mode") { const std::string v = nx(); g.modeArg = v == "water" || v == "1" ? 1 : v == "sky" || v == "2" ? 2 : 0; }
    else if (a == "--level") {
        const std::string v = nx();
        g.stageIdx = 0;
        for (int i = 0; i < nLevels(); i++) if (v == levels()[i].name) g.stageIdx = i;
        if (isdigit((unsigned char)v[0])) { const int n = atoi(v.c_str()); g.stageIdx = n % nLevels(); for (int i = 0; i < nLevels(); i++) if (levels()[i].stage == n) g.stageIdx = i; }
        g.levelArg = true;
    }
    else if (a == "--sea") g.seaVar = atoi(nx());
    else if (a == "--sky") g.skyVar = atoi(nx());
    else if (a == "--tex") g.texSel = atoi(nx());
    else if (a == "--wtex") g.wtexArg = nx();
    else if (a == "--scene") g.scene = atoi(nx()) != 0;
    else if (a == "--view") g.view = nx();
    else if (a == "--time") { g.fixedTime = (float)atof(nx()); g.haveTime = true; }
    else if (a == "--sway") g.sway = atoi(nx()) != 0;
    else if (a == "--rep") g.rep = atoi(nx());
    else if (a == "--scale") g.scale = atoi(nx());
    else if (a == "--cam") { g.haveCam = sscanf(nx(), "%f,%f,%f,%f,%f", &g.cam[0], &g.cam[1], &g.cam[2], &g.cam[3], &g.cam[4]) == 5; if (!g.haveCam) fprintf(stderr, "--cam wants x,y,z,yaw,pitch\n"); }
    else if (a == "--set") g.sets.push_back(nx());
    else return false;
    ++*i;
    return true;
}

bool seaTabInit(const void* viewerPack, size_t viewerPackSize) {
    baseCamera();
    skyPlusCoreWaveTexture(g.wave);
    loadClouds();
    memcpy(g.cloudTex, g.cloud, 4096);
    const int mode = g.modeArg, level = g.level, seaVar = g.seaVar, skyVar = g.skyVar, texSel = g.texSel, rep = g.rep, scale = g.scale;
    const bool scene = g.scene, sway = g.sway;
    selectLevel(g.stageIdx);
    if (!g.iniPath.empty()) seaTabLoadIni(g.iniPath.c_str());
    /* the command line wins over the .ini: the arguments again */
    if (mode >= 0) g.mode = mode;
    (void)level;
    if (seaVar) g.seaVar = seaVar;
    if (skyVar) g.skyVar = skyVar;
    if (texSel) { g.texSel = texSel; g.texDirty = true; }
    if (rep > 1) g.rep = rep;
    if (scale) g.scale = scale;
    if (!scene) g.scene = false;
    if (sway) g.sway = true;
    if (!g.wtexArg.empty()) g.wtexPath = g.wtexArg;
    for (const std::string& s : g.sets) {
        const size_t eq = s.rfind('=');   /* the last one: a name may hold '=' (Bake from (0 = no bake)) */
        if (eq == std::string::npos || s.compare(0, 2, "__") == 0) continue;
        const int k = parIdx(s.substr(0, eq).c_str());
        if (k >= 0) g.val[k] = (float)atof(s.c_str() + eq + 1); else fprintf(stderr, "seatab: no setting named %s\n", s.substr(0, eq).c_str());
    }
    g.rep = g.rep < 1 ? 1 : g.rep > 64 ? 64 : g.rep;
    g.scale = g.scale < 0 ? 0 : g.scale > 2 ? 2 : g.scale;
    g.grid.resize((size_t)(GN + 1) * (GN + 1) * 2);
    for (int j = 0; j <= GN; j++) for (int i = 0; i <= GN; i++) { g.grid[((size_t)j * (GN + 1) + i) * 2] = 2.0f * i / GN - 1.0f; g.grid[((size_t)j * (GN + 1) + i) * 2 + 1] = 2.0f * j / GN - 1.0f; }
    {   /* the water combiner (GE's: lerp(TEXEL0, TEXEL1, PRIM_LOD_FRAC) * SHADE, two cycles) and the flat colour one */
        uint8_t c[2][2][4] = {};
        c[0][0][0] = SHADER_TEXEL1; c[0][0][1] = SHADER_TEXEL0; c[0][0][2] = SHADER_INPUT_1; c[0][0][3] = SHADER_TEXEL0;
        c[0][1][3] = SHADER_1;
        c[1][0][0] = SHADER_COMBINED; c[1][0][2] = SHADER_INPUT_2;
        c[1][1][3] = SHADER_COMBINED;
        g.waterId0 = ge_render_cc_id(c);
        g.sceneId0 = ge_render_cc_id_shade();
    }
    g.viewerOn = ge_render_viewer_enable(viewerPack, viewerPackSize);
    if (!g.viewerOn) fprintf(stderr, "seatab: the viewer variants are not available (the game's shaders run; the variants and the PRM values have no effect)\n");
    g.wtexDirty = true;
    g.ok = true;
    return true;
}

void seaTabInitCam(SeaTabCam* cam) {
    cam->eye[0] = g.eye0.x; cam->eye[1] = g.eye0.y; cam->eye[2] = g.eye0.z;
    cam->yaw = cam->pitch = 0;
    if (!g.ok) return;
    if (!g.view.empty()) {   /* a camera in the test scene's frame: eye (a, b, height above the plane) looking at a target (a, b, height) */
        const float h0 = g.val[WATER0];
        struct { const char* n; float e[3], t[3]; } vw[] = {
            { "overview", { -100, 0, 260 }, { 900, -100, 0 } }, { "pier", { 150, 430, 90 }, { 700, 180, 10 } }, { "boxes", { 150, -100, 120 }, { 700, -200, 20 } },
            { "shore", { 1500, 2000, 150 }, { 2500, 2000, 0 } }, { "pool", { 600, -1500, 250 }, { 1700, -1500, 80 } }, { "far", { -100, 0, 40 }, { 4000, 100, 0 } } };
        for (auto& v : vw) if (g.view == v.n) {
            const V3 E = { g.eye0.x + g.sf.x * v.e[0] + g.sr.x * v.e[1], h0 + v.e[2], g.eye0.z + g.sf.z * v.e[0] + g.sr.z * v.e[1] };
            const V3 T = { g.eye0.x + g.sf.x * v.t[0] + g.sr.x * v.t[1], h0 + v.t[2], g.eye0.z + g.sf.z * v.t[0] + g.sr.z * v.t[1] };
            const V3 d = sub(T, E);
            const float hd = sqrtf(d.x * d.x + d.z * d.z);
            const V3 t = { d.x / hd, 0, d.z / hd };
            cam->eye[0] = E.x; cam->eye[1] = E.y; cam->eye[2] = E.z; setVal("Eye height", E.y);
            cam->yaw = atan2f(t.x * g.sf.z - t.z * g.sf.x, t.x * g.sf.x + t.z * g.sf.z);
            cam->pitch = atan2f(d.y, hd);
            g.haveCam = false;
        }
    }
    if (g.haveCam) {
        cam->eye[0] = g.cam[0]; cam->eye[1] = g.cam[1]; cam->eye[2] = g.cam[2]; setVal("Eye height", g.cam[1]);
        cam->yaw += g.cam[3] * 3.14159265f / 180; cam->pitch += g.cam[4] * 3.14159265f / 180;
    }
    g.bakeEye[0] = cam->eye[0]; g.bakeEye[1] = cam->eye[1]; g.bakeEye[2] = cam->eye[2];
}

void seaTabLook(SeaTabCam* cam, float dx, float dy) {
    cam->yaw += g.ysign * dx;
    cam->pitch -= dy;
    if (cam->pitch > 1.5f) cam->pitch = 1.5f;
    if (cam->pitch < -1.5f) cam->pitch = -1.5f;
}

void seaTabMove(SeaTabCam* cam, float fwd, float right, float up) {
    if (!g.ok) return;
    const V3 fw = rotAxis(g.sf, { 0, 1, 0 }, cam->yaw), rt = rotAxis(g.sr, { 0, 1, 0 }, cam->yaw);
    cam->eye[0] += fw.x * fwd + rt.x * right;
    cam->eye[1] += up;
    cam->eye[2] += fw.z * fwd + rt.z * right;
}

void seaTabFrame(float dt, SeaTabCam* cam, const int vp[4]) {
    if (!g.ok || vp[2] <= 0 || vp[3] <= 0) return;
    struct GfxRenderingAPI* api = ge_render_api();
    const GeRenderPasses* passes = ge_render_passes();
    if (g.wtexDirty) { loadWater(); g.wtexDirty = false; }
    if (g.texDirty) { makeCloudTex(g.texSel, g.cloud, g.cloudTex); g.texGen++; g.texDirty = false; }
    if (g.haveTime) g.T = g.fixedTime; else if (!g.freeze) g.T += dt;
    ge_render_cfg.skyplus_half = g.scale;
    ge_render_tool_sky_div(1 << g.scale);   /* deko3d: the divisor of the reduced sky passes (GL and Vulkan read the cfg above) */

    const int ie = parIdx("Eye height");   /* the eye height setting and the camera's y are one value */
    static float lastEye = -1e30f;
    if (g.val[ie] != lastEye) { cam->eye[1] = g.val[ie]; lastEye = g.val[ie]; } else { g.val[ie] = cam->eye[1]; lastEye = g.val[ie]; }

    /* any change of a value or a variant bakes the far sea again (the backend's own bake runs on a sun or colour change only) */
    {
        bool changed = g.prevVar[0] != g.seaVar || g.prevVar[1] != g.skyVar || g.prevVar[2] != g.mode;
        for (int i = 0; i < NPAR; i++) if (i != ie && g.prevVal[i] != g.val[i]) changed = true;
        if (changed) {
            g.resetBake = true;
            g.bakeEye[0] = cam->eye[0]; g.bakeEye[1] = cam->eye[1]; g.bakeEye[2] = cam->eye[2];
        }
        memcpy(g.prevVal, g.val, sizeof g.val);
        g.prevVar[0] = g.seaVar; g.prevVar[1] = g.skyVar; g.prevVar[2] = g.mode;
    }

    const int W = vp[2], H = vp[3];
    const float T = (float)g.T;
    const float swp = g.sway ? 0.0026f * sinf(T * 3.14159f) : 0.f, swr = g.sway ? 0.0012f * sinf(T * 2.1f) : 0.f;
    const View vw = makeView(*cam, (float)H / (float)W, swp, swr);
    float u[64];
    memcpy(u, g.U, 256);
    const V3 cor[4] = { add(sub(vw.cc, vw.hx), vw.hy), add(add(vw.cc, vw.hx), vw.hy), sub(sub(vw.cc, vw.hx), vw.hy), sub(add(vw.cc, vw.hx), vw.hy) };
    for (int i = 0; i < 4; i++) { u[8 + i * 4] = cor[i].x; u[8 + i * 4 + 1] = cor[i].y; u[8 + i * 4 + 2] = cor[i].z; u[8 + i * 4 + 3] = 0; }
    u[24] = cam->eye[0]; u[25] = cam->eye[1]; u[26] = cam->eye[2];
    u[60] = T;
    {
        float el = 0, az = 0;
        for (int i = 0; i < NPAR; i++) {
            const Par& q = kPar[i];
            if (q.kind == 1) u[q.idx] = g.val[i];
            else if (q.kind == 2 && q.idx == 0) el = g.val[i] / 57.29578f;
            else if (q.kind == 2 && q.idx == 1) az = g.val[i] / 57.29578f;
        }
        u[44] = cosf(el) * sinf(az); u[45] = sinf(el); u[46] = cosf(el) * cosf(az);
    }
    const bool water = g.mode == 1;
    const bool noSky = water && g.val[WATER0 + 7] > 0.5f;
    fillSky(g.p, u, water, noSky);
    g.resetBake = false;
    if (g.viewerOn) { GeRenderViewer vb; viewerBlock(&vb); ge_render_viewer_set(&vb); }

    const int vpi[4] = { vp[0], vp[1], W, H };
    api->set_viewport(vp[0], vp[1], W, H);
    api->set_scissor(vp[0], vp[1], W, H);
    const int slot = passes->sky_stage(&g.p, vpi, 1.0f, 0.0f);

    Proj pj;
    pj.v = vw;
    const float cl = len(vw.cc);
    pj.N = 2.0f / cl; pj.F = 600000.0f / cl;
    const GfxClipParameters cp = api->get_clip_parameters();
    pj.z01 = cp.z_is_from_0_to_1; pj.flipY = cp.invert_y;

    ge_render_gpu_timer_begin(0);
    if (noSky) { const float fogc[3] = { g.val[WATER0 + 17], g.val[WATER0 + 18], g.val[WATER0 + 19] }; colourQuad(api, pj, fogc); }
    for (int r = 0; r < g.rep; r++) passes->sky_pass(slot);
    if (water && g.wmapOk) {
        auto W_ = [&](int k) { return g.val[WATER0 + k]; };
        const float h0 = W_(0);
        const V3 L = nrm({ u[44], u[45], u[46] });
        api->set_viewport(vp[0], vp[1], W, H);
        api->set_scissor(vp[0], vp[1], W, H);
        if (g.scene) {
            g.vb.clear();
            sceneMesh(g.vb, pj, h0, L);
            useProgram(api, g.sceneId0, SHADER_OPT_ALPHA);
            api->set_depth_mode(true, true, true, false, 0);
            api->set_use_alpha(false, 0);
            drawTris(api, g.vb.data(), g.vb.size() / 8, 8);
        }
        g.vb.clear();
        const float shade[3] = { W_(14), W_(15), W_(16) }, fogc[3] = { W_(17), W_(18), W_(19) };
        const float tile = W_(1) > 1 ? W_(1) : 1;
        waterMesh(g.vb, pj, true, cam->eye[0], cam->eye[2], 60000.f, 60000.f, { 1, 0, 0 }, { 0, 0, 1 }, h0, tile, W_(5), W_(6), W_(4), shade, fogc, T);
        if (g.scene) {   /* a raised second pool with a rim */
            const V3 c = { g.eye0.x + g.sf.x * 1700 + g.sr.x * -1500, 0, g.eye0.z + g.sf.z * 1700 + g.sr.z * -1500 };
            waterMesh(g.vb, pj, false, c.x, c.z, 600, 900, g.sf, g.sr, h0 + 150, tile, W_(5), W_(6), W_(4), shade, fogc, T);
        }
        useProgram(api, g.waterId0, SHADER_OPT_FOG | SHADER_OPT_2CYC | SHADER_OPT_WATER);
        api->set_depth_mode(true, true, true, false, 0);
        api->set_use_alpha(false, 0);
        api->select_texture(0, g.wTex, true);
        api->set_sampler_parameters(0, true, 0, 0, true);
        api->select_texture(1, g.wTex, true);
        api->set_sampler_parameters(1, true, 0, 0, true);
        for (int r = 0; r < g.rep; r++) drawTris(api, g.vb.data(), g.vb.size() / 18, 18);
    }
    ge_render_gpu_timer_end(0);
    const double ms = ge_render_gpu_timer_ms(0);
    if (ms >= 0) g.gpuMs = ms;
}

const char* seaTabStatus(void) {
    char b[400];
    snprintf(b, sizeof b, "%s | sea: %s | sky: %s | scale %s | load x%d | GPU %.3f ms (%.3f per pass)%s", g.mode == 1 ? "water surface" : g.mode == 2 ? "sky" : "sea", kSeaNames[g.seaVar % 13],
             kSkyNames[g.skyVar % 7], kScaleNames[g.scale % 3], g.rep, g.gpuMs < 0 ? 0.0 : g.gpuMs, g.gpuMs < 0 ? 0.0 : g.gpuMs / g.rep,
             g.viewerOn ? "" : " | game shaders");
    g.status = b;
    return g.status.c_str();
}

int seaTabRep(void) { return g.rep; }
void seaTabSetRep(int n) { g.rep = n < 1 ? 1 : n > 64 ? 64 : n; }
void seaTabCycleScale(void) { g.scale = (g.scale + 1) % 3; }
void seaTabToggleSway(void) { g.sway = !g.sway; }
void seaTabToggleFreeze(void) { g.freeze = !g.freeze; }

void seaTabSaveIni(const char* path) {
    FILE* f = fopen(path, "w");
    if (!f) return;
    for (int i = 0; i < NPAR; i++) fprintf(f, "%s=%g\n", kPar[i].name, g.val[i]);
    fprintf(f, "__sea=%d\n__sky=%d\n__tex=%d\n__mode=%d\n__level=%d\n__wtex=%s\n__scene=%d\n__rep=%d\n__scale=%d\n", g.seaVar, g.skyVar, g.texSel, g.mode, g.level,
            g.wtexPath.c_str(), g.scene, g.rep, g.scale);
    fclose(f);
}

void seaTabLoadIni(const char* path) {
    FILE* f = fopen(path, "r");
    if (!f) { fprintf(stderr, "seatab: no settings file %s\n", path); return; }
    char line[600];
    while (fgets(line, sizeof line, f)) {
        char* eq = strrchr(line, '=');   /* the last one: a name may hold '=' (Bake from (0 = no bake)) */
        if (!eq) continue;
        *eq = 0;
        const float v = (float)atof(eq + 1);
        if (!strcmp(line, "__wtex")) { std::string t = eq + 1; while (!t.empty() && (t.back() == '\n' || t.back() == '\r')) t.pop_back(); g.wtexPath = t; g.wtexDirty = true; continue; }
        if (!strcmp(line, "__mode")) g.mode = (int)v;
        else if (!strcmp(line, "__level")) { g.level = (int)v; g.wtexDirty = true; }
        else if (!strcmp(line, "__sea")) g.seaVar = (int)v;
        else if (!strcmp(line, "__sky")) g.skyVar = (int)v;
        else if (!strcmp(line, "__tex")) { g.texSel = (int)v; g.texDirty = true; }
        else if (!strcmp(line, "__scene")) g.scene = (int)v != 0;
        else if (!strcmp(line, "__rep")) g.rep = (int)v < 1 ? 1 : (int)v > 64 ? 64 : (int)v;
        else if (!strcmp(line, "__scale")) g.scale = (int)v < 0 ? 0 : (int)v > 2 ? 2 : (int)v;
        else { const int k = parIdx(line); if (k >= 0) g.val[k] = v; }
    }
    fclose(f);
}

void seaTabUi(void) {
    if (!g.ok) { ImGui::TextUnformatted("The sea tab did not start."); return; }
    ImGui::TextUnformatted(seaTabStatus());
    {
        std::string names;
        for (int i = 0; i < nLevels(); i++) { names += levels()[i].name.c_str(); names += '\0'; }
        int l = g.stageIdx;
        if (ImGui::Combo("Level", &l, names.c_str())) selectLevel(l);
    }
    ImGui::RadioButton("Sea", &g.mode, 0); ImGui::SameLine(); ImGui::RadioButton("Water surface", &g.mode, 1); ImGui::SameLine(); ImGui::RadioButton("Sky only", &g.mode, 2);
    if (g.mode == 1) {
        static const char* lv[] = { "Dam", "Caverns (no sky)", "Complex" };
        int l = g.level;
        if (ImGui::Combo("Water texture preset", &l, lv, 3)) applyLevel(l);
        ImGui::Checkbox("Test scene (pier, boxes, pool, wall)", &g.scene);
        ImGui::InputText("Water texture file", g.wtexBuf, sizeof g.wtexBuf);
        ImGui::SameLine();
        if (ImGui::Button("Load##wtex")) { g.wtexPath = g.wtexBuf; g.wtexDirty = true; }
    }
    ImGui::Combo("Sea test", &g.seaVar, kSeaNames, 13);
    ImGui::Combo("Sky test", &g.skyVar, kSkyNames, 7);
    if (ImGui::Combo("Cloud texture", &g.texSel, kTexNames, 6)) g.texDirty = true;
    ImGui::SliderInt("Load (passes per frame)", &g.rep, 1, 64);
    ImGui::Combo("Scale", &g.scale, kScaleNames, 3);
    ImGui::Checkbox("Sway", &g.sway); ImGui::SameLine(); ImGui::Checkbox("Freeze time", &g.freeze);
    ImGui::InputText("Settings file", g.iniBuf, sizeof g.iniBuf);
    if (ImGui::Button("Save")) seaTabSaveIni(g.iniBuf);
    ImGui::SameLine();
    if (ImGui::Button("Load##ini")) seaTabLoadIni(g.iniBuf);
    ImGui::SameLine();
    if (ImGui::Button("Reset all")) resetAll();
    if (ImGui::BeginTabBar("seatabs")) {
        for (int t = 0; t < 6; t++) {
            if (!ImGui::BeginTabItem(kTabs[t])) continue;
            for (int i = 0; i < NPAR; i++) {
                if (tabOf(i) != t) continue;
                const int c = colourOf(i);
                ImGui::PushID(i);
                if (c >= 0) {
                    if (i == kColFirst[c]) {
                        ImGui::ColorEdit3(kColName[c], &g.val[i]);
                        ImGui::SameLine();
                        if (ImGui::SmallButton("r")) for (int k = 0; k < 3; k++) g.val[i + k] = g.def[i + k];
                    }
                } else {
                    const float sp = fmaxf(fabsf(g.def[i]) * 0.01f, 0.001f);
                    ImGui::DragFloat(kPar[i].name, &g.val[i], sp);
                    ImGui::SameLine();
                    if (ImGui::SmallButton("r")) g.val[i] = g.def[i];
                }
                ImGui::PopID();
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

void seaTabShutdown(void) {
    ge_render_viewer_disable();
    if (g.wTex) ge_render_api()->delete_texture(g.wTex);
    g.wTex = 0;
    g.ok = false;
}
