// Starting the game from the editor (the Play button and the test_run MCP tool).
#pragma once

#include <string>

// Runs `exe -level_NN --mission <missionPath> --spawn <spawn>` with GE_COOP=1 GE_PADS=2 in the
// environment, in the exe's directory. Returns at once (the game runs on its own). cmdline gets
// the command line for display.
bool launchGame(const std::string &exe, int levelNumber, const std::string &missionPath, const std::string &spawn,
                std::string &cmdline, std::string &err);

// GE_EDITOR_GAME if set, else the ge007 exe next to the editor (the sibling build output), else "".
std::string defaultGameExe(const std::string &editorDir);
