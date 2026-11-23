#include "filedialog.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>

#include <cstring>

namespace {

// Filters are lists of NUL-separated "name", "pattern" pairs, ended by a second NUL.
const char kMissionFilter[] = "Mission (mission.json)\0mission.json\0JSON\0*.json\0All files\0*.*\0";
const char kProgramFilter[] = "Programs (*.exe)\0*.exe\0All files\0*.*\0";
const char kModFilter[] = "Mod (campaign.json)\0campaign.json\0All files\0*.*\0";
const char kImageFilter[] = "PNG picture (*.png)\0*.png\0All files\0*.*\0";


std::string toWindowsPath(std::string s)
{
    for (char &c : s) if (c == '/') c = '\\';
    return s;
}

std::string toSlashes(std::string s)
{
    for (char &c : s) if (c == '\\') c = '/';
    return s;
}

// A plain Open dialog with its filter and title.
bool openWith(const char *filter, const char *title, const std::string &startDir, std::string &out)
{
    char file[1024] = "";
    const std::string dir = toWindowsPath(startDir);
    OPENFILENAMEA ofn;
    memset(&ofn, 0, sizeof ofn);
    ofn.lStructSize = sizeof ofn;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = file;
    ofn.nMaxFile = sizeof file;
    ofn.lpstrInitialDir = dir.empty() ? nullptr : dir.c_str();
    ofn.lpstrTitle = title;
    ofn.Flags = OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST | OFN_EXPLORER | OFN_FILEMUSTEXIST;
    if (!GetOpenFileNameA(&ofn)) return false;
    out = toSlashes(file);
    return true;
}

bool run(bool save, const std::string &startDir, std::string &out)
{
    char file[1024] = "";
    if (save) strcpy(file, "mission.json");
    const std::string dir = toWindowsPath(startDir);
    OPENFILENAMEA ofn;
    memset(&ofn, 0, sizeof ofn);
    ofn.lStructSize = sizeof ofn;
    ofn.lpstrFilter = kMissionFilter;
    ofn.lpstrFile = file;
    ofn.nMaxFile = sizeof file;
    ofn.lpstrInitialDir = dir.empty() ? nullptr : dir.c_str();
    ofn.lpstrDefExt = "json";
    ofn.lpstrTitle = save ? "Save mission as" : "Open mission";
    ofn.Flags = OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST | OFN_EXPLORER | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    if (!(save ? GetSaveFileNameA(&ofn) : GetOpenFileNameA(&ofn))) return false;
    out = toSlashes(file);
    return true;
}

} // namespace

bool fileDialogAvailable() { return true; }
bool fileDialogOpenMission(const std::string &startDir, std::string &out) { return run(false, startDir, out); }
bool fileDialogSaveMission(const std::string &startDir, std::string &out) { return run(true, startDir, out); }
bool fileDialogOpenMod(const std::string &startDir, std::string &out) { return openWith(kModFilter, "Open mod (its campaign.json)", startDir, out); }
bool fileDialogOpenImage(const std::string &startDir, std::string &out) { return openWith(kImageFilter, "Import picture", startDir, out); }

bool fileDialogPickProgram(const std::string &startPath, std::string &out)
{
    char file[1024] = "";
    std::string dir = toWindowsPath(startPath);
    const size_t s = dir.find_last_of('\\');
    dir = s == std::string::npos ? std::string() : dir.substr(0, s);
    OPENFILENAMEA ofn;
    memset(&ofn, 0, sizeof ofn);
    ofn.lStructSize = sizeof ofn;
    ofn.lpstrFilter = kProgramFilter;
    ofn.lpstrFile = file;
    ofn.nMaxFile = sizeof file;
    ofn.lpstrInitialDir = dir.empty() ? nullptr : dir.c_str();
    ofn.lpstrTitle = "Game executable";
    ofn.Flags = OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST | OFN_EXPLORER | OFN_FILEMUSTEXIST;
    if (!GetOpenFileNameA(&ofn)) return false;
    out = toSlashes(file);
    return true;
}

bool openFolder(const std::string &dir)
{
    return (INT_PTR)ShellExecuteA(nullptr, "open", toWindowsPath(dir).c_str(), nullptr, nullptr, SW_SHOWNORMAL) > 32;
}

#else

bool fileDialogAvailable() { return false; }
bool fileDialogOpenMission(const std::string &, std::string &) { return false; }
bool fileDialogSaveMission(const std::string &, std::string &) { return false; }
bool fileDialogOpenMod(const std::string &, std::string &) { return false; }
bool fileDialogOpenImage(const std::string &, std::string &) { return false; }
bool fileDialogPickProgram(const std::string &, std::string &) { return false; }
bool openFolder(const std::string &) { return false; }

#endif
