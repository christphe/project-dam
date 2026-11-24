// Starting the game from the editor (the Play button and the test_run MCP tool).
#pragma once

#include <string>

// Runs `exe -level_NN --mission <missionPath> [--spawn <spawn>]` in the exe's directory; with `coop`,
// GE_COOP=1 GE_PADS=2 in the environment (two players, two pads opened even if one is connected),
// else a solo run (both removed). Returns at once (the game runs on its own). cmdline gets the
// command line for display.
bool launchGame(const std::string &exe, int levelNumber, const std::string &missionPath, const std::string &spawn,
                bool coop, std::string &cmdline, std::string &err);

// GE_EDITOR_GAME if set, else the ge007 exe next to the editor (the sibling build output), else "".
std::string defaultGameExe(const std::string &editorDir);

// The game process the last launchGame started: still alive? Stop it (only that process; false when it is not running).
bool launchedGameRunning();
bool launchedGameStop();

// Where the game writes its log (ge007.log, rolled to ge007.prev.log at each start): <exe dir>/data/ge007.log.
std::string gameLogPath(const std::string &exe);
