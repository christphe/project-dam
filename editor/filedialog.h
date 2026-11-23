// The standard Open / Save file dialogs of the platform: Windows only (comdlg32, already part of the system);
// elsewhere fileDialogAvailable() is false and the editor hides the buttons (the typed path field stays).
#pragma once

#include <string>

bool fileDialogAvailable();
// Mission open dialog; startDir "" = the dialog's own default. True and `out` set when the user picked a file.
bool fileDialogOpenMission(const std::string &startDir, std::string &out);
// Mission save dialog (filter mission.json / *.json, default name mission.json).
bool fileDialogSaveMission(const std::string &startDir, std::string &out);
// Open a mod: pick its campaign.json.
bool fileDialogOpenMod(const std::string &startDir, std::string &out);
// Pick a picture to import (filter *.png).
bool fileDialogOpenImage(const std::string &startDir, std::string &out);
// Settings: pick the game executable (filter *.exe on Windows), and open a folder in the system file manager.
bool fileDialogPickProgram(const std::string &startPath, std::string &out);
bool openFolder(const std::string &dir);
