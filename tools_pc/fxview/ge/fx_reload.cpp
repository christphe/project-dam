#include <map>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <time.h>
#ifdef _WIN32
#include <direct.h>
#endif

#define main dkshgen_main   // the packs' GLSL generator, used as it is (the game's pack and this reload come from the same code)
#include "../../dkshgen/dkshgen.cpp"
#undef main

#include "fx_reload.h"
#include "ge_render.h"

namespace {
std::string g_dir, g_work, g_status = "no reload yet", g_repoList, g_toolList;
bool g_failed = false;
int g_count = 0;
uint64_t g_stamp = 0;
double g_lastPoll = 0;
std::map<std::pair<uint64_t, uint32_t>, std::vector<uint32_t>> g_spv;   // the reloaded fragment shaders, applied to programs created later
const char* const kFiles[4] = { "common.glsl", "bullets.glsl", "bursts.glsl", "impacts.glsl" };

double nowSec() { return (double)clock() / CLOCKS_PER_SEC; }

uint64_t stampOf() {
    uint64_t m = 0;
    std::vector<std::string> f;
    for (const char* n : kFiles) f.push_back(g_dir + "/" + n);
    f.push_back(g_toolList.substr(0, g_toolList.find_last_of("/\\") + 1) + "shaders/diag.glsl");
    for (const std::string& p : f) {
        struct stat st;
        if (stat(p.c_str(), &st) == 0 && (uint64_t)st.st_mtime > m) m = (uint64_t)st.st_mtime;
    }
    return m;
}

bool readTxt(const std::string& p, std::string& out) {
    FILE* f = fopen(p.c_str(), "rb");
    if (!f) return false;
    char buf[4096];
    size_t n;
    out.clear();
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
    fclose(f);
    return true;
}

std::string run(const std::string& cmd, int* rc) {
    std::string out;
#ifdef _WIN32
    FILE* p = _popen(cmd.c_str(), "r");
#else
    FILE* p = popen(cmd.c_str(), "r");
#endif
    if (!p) { *rc = -1; return "cannot start: " + cmd; }
    char buf[1024];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, p)) > 0) out.append(buf, n);
#ifdef _WIN32
    *rc = _pclose(p);
#else
    *rc = pclose(p);
#endif
    return out;
}

void fail(const std::string& why) {
    g_failed = true;
    g_status = "reload FAILED (the previous shader stays): " + why;
    fprintf(stdout, "RELOAD %s\n", g_status.c_str());
    fflush(stdout);
}

bool reloadGl() {
    std::string s[4];
    for (int i = 0; i < 4; i++) if (!readTxt(g_dir + "/" + kFiles[i], s[i])) { fail(std::string("cannot read ") + g_dir + "/" + kFiles[i]); return false; }
    char log[4096] = "";
    const bool ok = ge_render_fx_reload_gl(s[0].c_str(), s[1].c_str(), s[2].c_str(), s[3].c_str(), log, sizeof log);
    if (!ok) { fail(std::string(log) + "\n(file N in N(line): 0 common, 1 bullets, 2 bursts, 3 impacts)"); return false; }
    g_failed = false;
    g_count++;
    g_status = "reload #" + std::to_string(g_count) + " " + log;
    fprintf(stdout, "RELOAD %s\n", g_status.c_str());
    fflush(stdout);
    return true;
}

bool reloadVk() {
    const char* gl = getenv("GLSLANG");
    const std::string glslang = gl && *gl ? gl : "glslangValidator";
    const std::string gdir = g_work + "glsl";
    char* av[5];
    std::string a0 = "dkshgen", a1 = "glsl", a2 = g_repoList, a3 = gdir, a4 = g_toolList;
    av[0] = &a0[0]; av[1] = &a1[0]; av[2] = &a2[0]; av[3] = &a3[0]; av[4] = &a4[0];
#ifdef _WIN32
    _mkdir(g_work.c_str()); _mkdir(gdir.c_str());
#else
    mkdir(g_work.c_str(), 0777); mkdir(gdir.c_str(), 0777);
#endif
    if (dkshgen_main(5, av) != 0) { fail("dkshgen could not generate the shaders (see the log)"); return false; }
    std::string list;
    if (!readTxt(gdir + "/list.txt", list)) { fail("no list.txt from dkshgen"); return false; }
    std::map<std::pair<uint64_t, uint32_t>, std::vector<uint32_t>> fresh;
    size_t at = 0;
    while (at < list.size()) {
        size_t eol = list.find('\n', at);
        if (eol == std::string::npos) eol = list.size();
        const std::string ln = list.substr(at, eol - at);
        at = eol + 1;
        unsigned long long id0 = 0;
        unsigned id1 = 0;
        char base[256];
        if (sscanf(ln.c_str(), "%llx %x %255s", &id0, &id1, base) != 3) continue;
        if ((id1 & SHADER_OPT_FX_MASK) == 0) continue;   // the effects only: the game's combiners are the pack's
        const std::string b = base;
#ifdef _WIN32
        const std::string cd = "cd /d \"" + gdir + "\" && ";
#else
        const std::string cd = "cd \"" + gdir + "\" && ";
#endif
        int rc = 0;
        const std::string out = run(cd + "\"" + glslang + "\" -V --target-env vulkan1.3 --sub 4 -S frag -o " + b + ".frag.spv " + b + ".frag.glsl 2>&1", &rc);
        if (rc != 0) {
            fail(b + ".frag.glsl: glslangValidator (" + glslang + ") failed:\n" + out +
                 "\n(file N in N:line: the generated source, #line 1 N = 0 common, 1 bullets, 2 bursts, 3 impacts; set GLSLANG to a glslangValidator)");
            return false;
        }
        std::string spv;
        if (!readTxt(gdir + "/" + b + ".frag.spv", spv) || spv.size() < 8 || spv.size() % 4) { fail("no SPIR-V for " + b); return false; }
        std::vector<uint32_t> w(spv.size() / 4);
        memcpy(w.data(), spv.data(), spv.size());
        fresh[{ (uint64_t)id0, (uint32_t)id1 }] = w;
    }
    int applied = 0;
    for (auto& kv : fresh) {
        char log[256];
        if (ge_render_fx_replace_spirv(kv.first.first, kv.first.second, kv.second.data(), kv.second.size(), log, sizeof log)) applied++;
    }
    g_spv = fresh;
    g_failed = false;
    g_count++;
    g_status = "reload #" + std::to_string(g_count) + " ok: " + std::to_string(fresh.size()) + " effect shaders compiled, " + std::to_string(applied) + " live";
    fprintf(stdout, "RELOAD %s\n", g_status.c_str());
    fflush(stdout);
    return true;
}
}  // namespace

bool fxReloadInit(const std::string& dir, const std::string& work) {
    g_dir = dir;
    while (!g_dir.empty() && (g_dir.back() == '/' || g_dir.back() == '\\')) g_dir.pop_back();
    g_work = work;
    if (!g_work.empty() && g_work.back() != '/') g_work += "/";
    g_work += "fxreload/";
    const size_t cut = g_dir.find_last_of("/\\");   // <repo>/port/shaders/fx -> <repo>/port/shaders/combiners.txt, <repo>/tools_pc/fxview/combiners.txt
    if (cut == std::string::npos) return false;
    const std::string shaders = g_dir.substr(0, cut);
    g_repoList = shaders + "/combiners.txt";
    g_toolList = shaders + "/../../tools_pc/fxview/combiners.txt";
    g_stamp = stampOf();
    return g_stamp != 0;
}

void fxReloadPoll(bool force) {
    if (g_dir.empty()) return;
    const double t = nowSec();
    if (!force && t - g_lastPoll < 0.3) return;
    g_lastPoll = t;
    const uint64_t s = stampOf();
    if (!force && s == g_stamp) return;
    g_stamp = s;
    if (ge_render_backend() == GE_RENDER_GL) reloadGl();
    else if (ge_render_backend() == GE_RENDER_VULKAN) { if (!force) reloadVk(); }   // Vulkan: the pack is current at start
}

void fxReloadApply(uint64_t id0, uint32_t id1) {
    auto it = g_spv.find({ id0, id1 });
    if (it == g_spv.end()) return;
    char log[256];
    ge_render_fx_replace_spirv(id0, id1, it->second.data(), it->second.size(), log, sizeof log);
}

const std::string& fxReloadStatus() { return g_status; }
bool fxReloadFailed() { return g_failed; }
int fxReloadCount() { return g_count; }
