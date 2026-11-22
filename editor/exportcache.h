// Level exports made by the editor: it runs `ge007 -level_NN --export-level DIR` itself and keeps the
// result next to the game, in <game dir>/data/exports/<level>/ (level.rec + level.json). An export is
// ROM-derived data: it stays in the user's game folder, never in a mod or in the repo.
#pragma once

#include <string>

// <game dir>/data/exports/<level>, absolute. "" when there is no game exe.
std::string exportDirFor(const std::string &gameExe, const std::string &level);
// Both files of a finished export are there.
bool exportPresent(const std::string &dir);

// One export run of the game, started in the background.
struct ExportJob {
    bool running = false;
    std::string level, dir;
#ifdef _WIN32
    void *proc = nullptr;
#else
    int pid = -1;
#endif
};

// Starts the game on `level` (a solo level name) exporting into exportDirFor(). Any older export of
// that level is removed first, so exportPresent() means this run finished.
bool exportStart(ExportJob &j, const std::string &gameExe, const std::string &level, std::string &err);
// 0 still running, 1 done (the export is present), -1 failed (err says why).
int exportPoll(ExportJob &j, std::string &err);
// Starts and waits up to timeoutSec. dir gets the export folder.
bool exportRun(const std::string &gameExe, const std::string &level, std::string &dir, std::string &err,
               int timeoutSec = 180);
