#include "launch.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <climits>

#ifdef _WIN32
#include <windows.h>
#else
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

static bool fileExists(const std::string &p)
{
    FILE *f = fopen(p.c_str(), "rb");
    if (!f) return false;
    fclose(f);
    return true;
}

std::string defaultGameExe(const std::string &editorDir)
{
    const char *env = getenv("GE_EDITOR_GAME");
    if (env && *env) return env;
    static const char *names[] = { "ge007.x86_64.exe", "ge007.x86_64", "ge007.exe", "ge007" };
    for (const char *n : names) {
        std::string p = editorDir + n;
        if (fileExists(p)) return p;
    }
    return "";
}

static std::string dirOf(const std::string &p)
{
    size_t s = p.find_last_of("/\\");
    return s == std::string::npos ? "." : p.substr(0, s);
}

static std::string absPath(const std::string &p)
{
    char buf[4096];
#ifdef _WIN32
    if (_fullpath(buf, p.c_str(), sizeof buf)) return buf;
#else
    if (realpath(p.c_str(), buf)) return buf;
#endif
    return p;
}

bool launchGame(const std::string &exeIn, int levelNumber, const std::string &missionIn, const std::string &spawn,
                std::string &cmdline, std::string &err)
{
    // The game starts in its own directory: paths must not depend on ours.
    const std::string exe = exeIn.empty() ? exeIn : absPath(exeIn);
    const std::string missionPath = absPath(missionIn);
    if (exe.empty()) { err = "no game executable: set GE_EDITOR_GAME or build ge007 next to the editor"; return false; }
    if (!fileExists(exe)) { err = "game executable not found: " + exe; return false; }
    char lvl[32];
    snprintf(lvl, sizeof lvl, "-level_%02d", levelNumber);

    // Co-op tests always run with two pads (the game opens only connected pads otherwise).
#ifdef _WIN32
    _putenv_s("GE_COOP", "1");
    _putenv_s("GE_PADS", "2");
#else
    setenv("GE_COOP", "1", 1);
    setenv("GE_PADS", "2", 1);
#endif

    cmdline = "\"" + exe + "\" " + lvl + " --mission \"" + missionPath + "\"";
    if (!spawn.empty()) cmdline += " --spawn " + spawn;

#ifdef _WIN32
    STARTUPINFOA si; PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof si); si.cb = sizeof si;
    memset(&pi, 0, sizeof pi);
    std::string cl = cmdline;
    std::string cwd = dirOf(exe);
    if (!CreateProcessA(nullptr, &cl[0], nullptr, nullptr, FALSE, 0, nullptr, cwd.c_str(), &si, &pi)) {
        err = "cannot start the game (error " + std::to_string((int)GetLastError()) + ")";
        return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
#else
    signal(SIGCHLD, SIG_IGN);   // the game is never waited for
    pid_t pid = fork();
    if (pid < 0) { err = "fork failed"; return false; }
    if (pid == 0) {
        std::string cwd = dirOf(exe);
        if (chdir(cwd.c_str()) != 0) _exit(127);
        std::string arg0 = exe;
        std::string a1 = lvl, a2 = "--mission", a3 = missionPath, a4 = "--spawn";
        if (spawn.empty())
            execl(arg0.c_str(), arg0.c_str(), a1.c_str(), a2.c_str(), a3.c_str(), (char *)nullptr);
        else
            execl(arg0.c_str(), arg0.c_str(), a1.c_str(), a2.c_str(), a3.c_str(), a4.c_str(), spawn.c_str(), (char *)nullptr);
        _exit(127);
    }
    return true;
#endif
}
