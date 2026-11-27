#include "fx_tex.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

extern "C" {
#include "ge_png.h"
#include "ge_tex.h"
#include "export.h"   // ex_ensure_base_pack: writes ge007.ptexpack from the ROM
}

static GeTexSet* g_set = nullptr;
static std::string g_note = "no texture pack";

static bool exists(const std::string& p) { struct stat st; return !p.empty() && stat(p.c_str(), &st) == 0; }
static std::string dirOf(const std::string& p) { const size_t k = p.find_last_of("/\\"); return k == std::string::npos ? std::string(".") : p.substr(0, k); }

std::string fxTexFindBase(const std::string& arg, const std::string& romPath) {
    if (!arg.empty()) return exists(arg) ? arg : std::string();
    if (const char* e = getenv("FXVIEW_PACK")) if (*e && exists(e)) return e;
    std::vector<std::string> dirs;
#ifdef __SWITCH__
    dirs.push_back("sdmc:/switch/project-dam/texturepacks");
#else
    dirs.push_back("texturepacks");
    dirs.push_back("../texturepacks");
    if (!romPath.empty()) dirs.push_back(dirOf(dirOf(romPath)) + "/texturepacks");   // <game>/data/<rom>.z64 -> <game>/texturepacks
#endif
    for (const std::string& d : dirs) {
        const std::string p = d + "/ge007.ptexpack";
        if (exists(p)) return p;
    }
    return std::string();
}

static void logLine(void* u, const char* line) { (void)u; fprintf(stderr, "fxview: pack: %s\n", line); }

bool fxTexEnsureBase(const std::string& romPath, const std::string& packDir, std::string& note) {
    if (romPath.empty()) { note = "no ROM to write ge007.ptexpack from"; return false; }
    const int r = ex_ensure_base_pack(romPath.c_str(), packDir.c_str(), logLine, nullptr);
    if (r < 0) { note = "ge007.ptexpack could not be written from the ROM (see the log)"; return false; }
    note = r == 1 ? "ge007.ptexpack is current" : "ge007.ptexpack written from the ROM";
    return true;
}

bool fxTexOpen(const std::string& base, const std::string& texPack, const std::string& mod) {
    if (!g_set) g_set = geTexSetNew();
    std::string pack;
    if (!texPack.empty() && texPack != "ge007" && texPack != "ge007.ptexpack") {
        pack = exists(texPack) ? texPack : dirOf(base) + "/" + texPack;
    }
    geTexSetOpen(g_set, mod.c_str(), pack.c_str(), base.c_str());
    if (!geTexSetPack(g_set)) { g_note = base.empty() ? "no ge007.ptexpack: sprites show as untextured quads" : "cannot read " + base; return false; }
    char b[1500];
    const TexPackSrc* p = geTexSetPack(g_set);
    snprintf(b, sizeof b, "texture pack: %s, %d textures%s%s%s", texpackSrcRoot(p), texpackSrcCount(p), geTexSetMod(g_set) ? ", mod " : "",
             geTexSetMod(g_set) ? texpackSrcRoot(geTexSetMod(g_set)) : "", geTexSetNote(g_set)[0] ? "; " : "");
    g_note = b;
    if (geTexSetNote(g_set)[0]) g_note += geTexSetNote(g_set);
    if (!mod.empty() && !geTexSetMod(g_set)) g_note += "; mod " + mod + " cannot be read";
    return true;
}

bool fxTexOpened() { return g_set && geTexSetPack(g_set); }
const std::string& fxTexNote() { return g_note; }

bool fxTexture(int texnum, FxTexImage& out) {
    out = FxTexImage();
    if (!fxTexOpened()) { out.note = "no texture pack"; return false; }
    GeTexImage im;
    auto warn = [](void*, const char* msg) { fprintf(stderr, "fxview: texture: %s\n", msg); };
    if (!geTexResolve(g_set, std::to_string(texnum).c_str(), &im, warn, nullptr)) { out.note = "no source has texture " + std::to_string(texnum); return false; }
    out.w = im.w; out.h = im.h;
    out.rgba.assign(im.rgba, im.rgba + (size_t)im.w * im.h * 4);
    return true;
}

int fx_tex_selftest() {
    int fail = 0;
    {   // ge_assets' PNG codec: RGBA written and read back; a texture asked for with no pack open is refused with a reason
        const int W = 5, H = 3;
        std::vector<uint8_t> src((size_t)W * H * 4);
        for (size_t i = 0; i < src.size(); i++) src[i] = (uint8_t)(i * 37 + 11);
        uint8_t* png = nullptr; size_t len = 0; uint8_t* back = nullptr; int w = 0, h = 0;
        if (!gePngEncode(W, H, src.data(), 0, &png, &len) || !gePngDecode(png, len, &w, &h, &back) || w != W || h != H || memcmp(back, src.data(), src.size())) {
            fprintf(stderr, "selftest FAIL png round trip\n"); fail++;
        }
        free(png); free(back);
        FxTexImage img;
        if (fxTexture(1, img) || img.note.empty()) { fprintf(stderr, "selftest FAIL texture without a pack\n"); fail++; }
    }
    fprintf(stderr, fail ? "selftest fx textures: %d failures\n" : "selftest: fx textures (ge_assets) ok\n", fail);
    return fail;
}
