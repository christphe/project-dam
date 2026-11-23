#include "filedialog.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>

#include <cstring>

namespace {

// "Mission (mission.json)\0mission.json\0JSON\0*.json\0\0"
const char kFilter[] = "Mission (mission.json)\0mission.json\0JSON\0*.json\0All files\0*.*\0";

std::string toWindowsPath(std::string s)
{
    for (char &c : s) if (c == '/') c = '\\';
    return s;
}

bool run(bool save, const std::string &startDir, std::string &out)
{
    char file[1024] = "";
    if (save) strcpy(file, "mission.json");
    const std::string dir = toWindowsPath(startDir);
    OPENFILENAMEA ofn;
    memset(&ofn, 0, sizeof ofn);
    ofn.lStructSize = sizeof ofn;
    ofn.lpstrFilter = kFilter;
    ofn.lpstrFile = file;
    ofn.nMaxFile = sizeof file;
    ofn.lpstrInitialDir = dir.empty() ? nullptr : dir.c_str();
    ofn.lpstrDefExt = "json";
    ofn.lpstrTitle = save ? "Save mission as" : "Open mission";
    ofn.Flags = OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST | OFN_EXPLORER | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    if (!(save ? GetSaveFileNameA(&ofn) : GetOpenFileNameA(&ofn))) return false;
    out = file;
    for (char &c : out) if (c == '\\') c = '/';
    return true;
}

} // namespace

bool fileDialogAvailable() { return true; }
bool fileDialogOpenMission(const std::string &startDir, std::string &out) { return run(false, startDir, out); }
bool fileDialogSaveMission(const std::string &startDir, std::string &out) { return run(true, startDir, out); }

#else

bool fileDialogAvailable() { return false; }
bool fileDialogOpenMission(const std::string &, std::string &) { return false; }
bool fileDialogSaveMission(const std::string &, std::string &) { return false; }

#endif
