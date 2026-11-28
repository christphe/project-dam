#include "exportcache.h"

extern "C" {
#include "mission.h"
#include "export.h"
#include "ge_pkg.h"
#include "cJSON.h"
}

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

std::string exportDirFor(const std::string &gameExe, const std::string &level)
{
    if (gameExe.empty() || level.empty()) return "";
    std::error_code ec;
    fs::path exe = fs::absolute(fs::path(gameExe), ec);
    return (exe.parent_path() / "data" / "exports" / level).string();
}

std::string gltfDirFor(const std::string &gameExe)
{
    if (gameExe.empty()) return "";
    std::error_code ec;
    fs::path exe = fs::absolute(fs::path(gameExe), ec);
    return (exe.parent_path() / "data" / "exports" / "gltf").string();
}

int exportGltfVersion() { return EX_EXPORTER_VERSION; }

std::string assetPackIn(const std::string &root)
{
    if (root.empty()) return "";
    std::error_code ec;
    const size_t k = root.size();
    if (k > 8 && root.compare(k - 8, 8, ".passets") == 0) return fs::exists(fs::path(root), ec) ? root : "";
    const fs::path p = fs::path(root) / "ge007.passets";
    return fs::exists(p, ec) ? p.generic_string() : "";
}

bool gltfCurrent(const std::string &dir, const std::string &level)
{
    (void)level;
    const std::string pack = assetPackIn(dir);
    if (pack.empty()) return false;
    uint8_t *buf = nullptr;
    size_t len = 0;
    if (!pkgReadFile((pack + "/pack.json").c_str(), &buf, &len)) return false;   // a tar or a folder alike
    int ver = -1;
    if (cJSON *j = cJSON_Parse((const char *)buf)) {
        const cJSON *v = cJSON_GetObjectItemCaseSensitive(j, "exporter_version");
        if (cJSON_IsNumber(v)) ver = (int)v->valuedouble;
        cJSON_Delete(j);
    }
    pkgFree(buf);
    return ver == EX_EXPORTER_VERSION;
}

bool exportPresent(const std::string &dir)
{
    std::error_code ec;
    return !dir.empty() && fs::is_regular_file(fs::path(dir) / "level.json", ec) &&
           fs::is_regular_file(fs::path(dir) / "level.rec", ec);
}

bool exportStart(ExportJob &j, const std::string &gameExe, const std::string &level, std::string &err)
{
    if (j.running) { err = "an export is already running (" + j.level + ")"; return false; }
    const int num = missionLevelNumber(level.c_str());
    if (num < 0) { err = "unknown level '" + level + "'"; return false; }
    if (gameExe.empty()) { err = "no game executable: set it in File > game, or GE_EDITOR_GAME"; return false; }
    std::error_code ec;
    fs::path exe = fs::absolute(fs::path(gameExe), ec);
    if (!fs::is_regular_file(exe, ec)) { err = "game executable not found: " + exe.string(); return false; }

    const std::string dir = exportDirFor(exe.string(), level);
    fs::create_directories(dir, ec);
    if (ec) { err = "cannot create " + dir + ": " + ec.message(); return false; }
    fs::remove(fs::path(dir) / "level.json", ec);   // a present export must come from this run
    fs::remove(fs::path(dir) / "level.rec", ec);

    char lvl[32];
    snprintf(lvl, sizeof lvl, "-level_%02d", num);
    const std::string cwd = exe.parent_path().string();

    // A Play before may have left GE_COOP/GE_PADS in our environment: the export is a solo load.
#ifdef _WIN32
    _putenv_s("GE_COOP", "");
    _putenv_s("GE_PADS", "");
    std::string cl = "\"" + exe.string() + "\" " + lvl + " --export-level \"" + dir + "\"";
    STARTUPINFOA si; PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof si); si.cb = sizeof si;
    memset(&pi, 0, sizeof pi);
    if (!CreateProcessA(nullptr, &cl[0], nullptr, nullptr, FALSE, 0, nullptr, cwd.c_str(), &si, &pi)) {
        err = "cannot start the game (error " + std::to_string((int)GetLastError()) + ")";
        return false;
    }
    CloseHandle(pi.hThread);
    j.proc = pi.hProcess;
#else
    unsetenv("GE_COOP");
    unsetenv("GE_PADS");
    const std::string exeS = exe.string();
    pid_t pid = fork();
    if (pid < 0) { err = "fork failed"; return false; }
    if (pid == 0) {
        if (chdir(cwd.c_str()) != 0) _exit(127);
        execl(exeS.c_str(), exeS.c_str(), lvl, "--export-level", dir.c_str(), (char *)nullptr);
        _exit(127);
    }
    j.pid = (int)pid;
#endif
    j.running = true;
    j.gltf = false;
    j.level = level;
    j.dir = dir;
    return true;
}

bool exportStartGltf(ExportJob &j, const std::string &gameExe, std::string &err)
{
    if (j.running) { err = "an export is already running (" + j.level + ")"; return false; }
    if (gameExe.empty()) { err = "no game executable: set it in Tools > Settings, or GE_EDITOR_GAME"; return false; }
    std::error_code ec;
    fs::path exe = fs::absolute(fs::path(gameExe), ec);
    if (!fs::is_regular_file(exe, ec)) { err = "game executable not found: " + exe.string(); return false; }
    const std::string dir = gltfDirFor(exe.string());
    fs::create_directories(dir, ec);
    if (ec) { err = "cannot create " + dir + ": " + ec.message(); return false; }
    const std::string cwd = exe.parent_path().string();
#ifdef _WIN32
    std::string cl = "\"" + exe.string() + "\" --export-levels \"" + dir + "\"";
    STARTUPINFOA si; PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof si); si.cb = sizeof si;
    memset(&pi, 0, sizeof pi);
    if (!CreateProcessA(nullptr, &cl[0], nullptr, nullptr, FALSE, 0, nullptr, cwd.c_str(), &si, &pi)) {
        err = "cannot start the game (error " + std::to_string((int)GetLastError()) + ")";
        return false;
    }
    CloseHandle(pi.hThread);
    j.proc = pi.hProcess;
#else
    const std::string exeS = exe.string();
    pid_t pid = fork();
    if (pid < 0) { err = "fork failed"; return false; }
    if (pid == 0) {
        if (chdir(cwd.c_str()) != 0) _exit(127);
        execl(exeS.c_str(), exeS.c_str(), "--export-levels", dir.c_str(), (char *)nullptr);
        _exit(127);
    }
    j.pid = (int)pid;
#endif
    j.running = true;
    j.gltf = true;
    j.level = "gltf";
    j.dir = dir;
    return true;
}

int exportPoll(ExportJob &j, std::string &err)
{
    if (!j.running) { err = "no export running"; return -1; }
    int code = 0;
#ifdef _WIN32
    if (WaitForSingleObject((HANDLE)j.proc, 0) == WAIT_TIMEOUT) return 0;
    DWORD c = 0;
    GetExitCodeProcess((HANDLE)j.proc, &c);
    CloseHandle((HANDLE)j.proc);
    j.proc = nullptr;
    code = (int)c;
#else
    int st = 0;
    pid_t r = waitpid((pid_t)j.pid, &st, WNOHANG);
    if (r == 0) return 0;
    if (r < 0) {
        // SIGCHLD ignored (Play sets it): the child is reaped for us, so only its existence tells.
        if (kill((pid_t)j.pid, 0) == 0) return 0;
    } else {
        code = WIFEXITED(st) ? WEXITSTATUS(st) : 128;
    }
    j.pid = -1;
#endif
    j.running = false;
    if (j.gltf ? gltfCurrent(j.dir, "bunker1") : exportPresent(j.dir)) return 1;
    if (j.gltf) {
        err = "the game exited (code " + std::to_string(code) + ") without writing the glTF export; see data/ge007.log next to the game";
        return -1;
    }
    err = "the game exited (code " + std::to_string(code) + ") without writing the export of " + j.level +
          "; see data/ge007.log next to the game";
    return -1;
}

bool exportRun(const std::string &gameExe, const std::string &level, std::string &dir, std::string &err, int timeoutSec)
{
    ExportJob j;
    if (!exportStart(j, gameExe, level, err)) return false;
    dir = j.dir;
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(timeoutSec);
    for (;;) {
        int r = exportPoll(j, err);
        if (r == 1) return true;
        if (r < 0) return false;
        if (std::chrono::steady_clock::now() > end) {
            err = "the export of " + level + " did not finish in " + std::to_string(timeoutSec) + " s";
            return false;   // the game keeps running; it quits on its own once the export is written
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
}
