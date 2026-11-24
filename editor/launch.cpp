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

// The process the last launchGame started (only this one may be asked about or stopped).
#ifdef _WIN32
static HANDLE s_proc = nullptr;
#else
static pid_t s_pid = 0;
#endif

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
                bool coop, std::string &cmdline, std::string &err)
{
    // The game starts in its own directory: paths must not depend on ours.
    const std::string exe = exeIn.empty() ? exeIn : absPath(exeIn);
    const std::string missionPath = absPath(missionIn);
    if (exe.empty()) { err = "no game executable: set GE_EDITOR_GAME or build ge007 next to the editor"; return false; }
    if (!fileExists(exe)) { err = "game executable not found: " + exe; return false; }
    char lvl[32];
    snprintf(lvl, sizeof lvl, "-level_%02d", levelNumber);

    // A co-op test runs with two pads (the game opens only connected pads otherwise); a solo one with neither.
#ifdef _WIN32
    _putenv_s("GE_COOP", coop ? "1" : "");
    _putenv_s("GE_PADS", coop ? "2" : "");
#else
    if (coop) { setenv("GE_COOP", "1", 1); setenv("GE_PADS", "2", 1); }
    else { unsetenv("GE_COOP"); unsetenv("GE_PADS"); }
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
    if (s_proc) CloseHandle(s_proc);   // an earlier game is left running; only the newest one is tracked
    s_proc = pi.hProcess;
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
    s_pid = pid;
    return true;
#endif
}

bool launchedGameRunning()
{
#ifdef _WIN32
    return s_proc && WaitForSingleObject(s_proc, 0) == WAIT_TIMEOUT;
#else
    return s_pid > 0 && kill(s_pid, 0) == 0;   // SIGCHLD is ignored: a finished child is reaped at once
#endif
}

bool launchedGameStop()
{
    if (!launchedGameRunning()) return false;
#ifdef _WIN32
    return TerminateProcess(s_proc, 0) != 0;
#else
    return kill(s_pid, SIGTERM) == 0;
#endif
}

std::string gameLogPath(const std::string &exe)
{
    if (exe.empty()) return "";
    return dirOf(absPath(exe)) + "/data/ge007.log";   // the game's $S/ folder: <exe dir>/data (system.c sysResolvePath)
}
