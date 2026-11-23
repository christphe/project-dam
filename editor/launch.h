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
