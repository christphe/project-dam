// Mod (campaign) folder handling and the mission environment edit of Doc.
// A mod is mods/<mod>/campaign.json plus one folder per mission (mission.json, thumb.png).
#include "doc.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>

namespace fs = std::filesystem;

static bool exists(const std::string &p)
{
    std::error_code ec;
    return fs::exists(fs::path(p), ec);
}

static std::string baseName(std::string p)
{
    while (!p.empty() && (p.back() == '/' || p.back() == '\\')) p.pop_back();
    size_t s = p.find_last_of("/\\");
    return s == std::string::npos ? p : p.substr(s + 1);
}

bool Doc::openMod(const std::string &dirIn, std::string &err)
{
    std::string dir = dirIn;
    while (dir.size() > 1 && (dir.back() == '/' || dir.back() == '\\')) dir.pop_back();
    if (dir.empty()) { err = "no mod folder given"; return false; }
    const std::string file = dir + "/campaign.json";
    Campaign c; campaignInit(&c);
    char e[256] = "";
    if (exists(file)) {
        if (!campaignLoadFile(file.c_str(), &c, e, sizeof e)) { err = e[0] ? e : "cannot load campaign.json"; campaignFree(&c); return false; }
    } else {
        std::error_code ec;
        fs::create_directories(fs::path(dir), ec);
        if (ec) { err = "cannot create " + dir; campaignFree(&c); return false; }
        snprintf(c.name, sizeof c.name, "%s", baseName(dir).c_str());
        c.affects = CAMPAIGN_AFFECTS_STORY;
        if (!campaignSaveFile(file.c_str(), &c, e, sizeof e)) { err = e; campaignFree(&c); return false; }
    }
    campaignFree(&camp);
    camp = c;   // takes over the owned pointers
    hasMod = true;
    modDir = dir;
    modDirty = false;
    return true;
}

bool Doc::saveMod(std::string &err)
{
    if (!hasMod) { err = "no mod is open"; return false; }
    char e[256] = "";
    if (!campaignSaveFile((modDir + "/campaign.json").c_str(), &camp, e, sizeof e)) { err = e[0] ? e : "cannot write campaign.json"; return false; }
    modDirty = false;
    return true;
}

std::vector<std::string> Doc::validateMod() const
{
    std::vector<std::string> out;
    if (!hasMod) { out.push_back("no mod is open"); return out; }
    char e[256] = "";
    if (!campaignValidate(&camp, e, sizeof e)) out.push_back(e);
    for (int i = 0; i < camp.nmissions; i++)
        if (!exists(modMissionPath(i))) out.push_back(std::string("mission folder '") + camp.missions[i] + "' has no mission.json");
    return out;
}

std::string Doc::modMissionPath(int i) const
{
    if (!hasMod || i < 0 || i >= camp.nmissions) return "";
    return modDir + "/" + camp.missions[i] + "/mission.json";
}

std::string Doc::defaultThumbPath() const
{
    if (!hasMod || !hasMission || missionPath.empty()) return "";
    std::string mp = missionPath;
    for (auto &c : mp) if (c == '\\') c = '/';
    std::string md = modDir;
    for (auto &c : md) if (c == '\\') c = '/';
    if (mp.compare(0, md.size() + 1, md + "/") != 0) return "";
    size_t s = mp.find_last_of('/');
    return mp.substr(0, s) + "/thumb.png";
}

static std::string slug(const std::string &s)
{
    std::string o;
    for (char ch : s) {
        if (isalnum((unsigned char)ch)) o += (char)tolower((unsigned char)ch);
        else if (ch == ' ' || ch == '_' || ch == '-') o += '_';
    }
    return o.empty() ? "mission" : o;
}

bool Doc::modAddMission(const std::string &name, const std::string &level, const std::string &folderIn, std::string &outFolder, std::string &err)
{
    if (!hasMod) { err = "no mod is open (mod_open first)"; return false; }
    std::string base = slug(folderIn.empty() ? name : folderIn), folder = base;
    for (int n = 2;; n++) {
        bool used = exists(modDir + "/" + folder);
        for (int i = 0; i < camp.nmissions; i++) if (folder == camp.missions[i]) used = true;
        if (!used) break;
        folder = base + "_" + std::to_string(n);
    }
    if (folder.size() >= MISSION_ID_LEN) { err = "folder name too long"; return false; }
    if (!newMission(name, level, err)) return false;
    std::error_code ec;
    fs::create_directories(fs::path(modDir + "/" + folder), ec);
    if (ec) { err = "cannot create the mission folder"; return false; }
    if (!saveMission(modDir + "/" + folder + "/mission.json", err)) return false;
    char (*nm)[MISSION_ID_LEN] = (char (*)[MISSION_ID_LEN])realloc(camp.missions, sizeof(char[MISSION_ID_LEN]) * (size_t)(camp.nmissions + 1));
    if (!nm) abort();
    camp.missions = nm;
    snprintf(camp.missions[camp.nmissions++], MISSION_ID_LEN, "%s", folder.c_str());
    outFolder = folder;
    return saveMod(err);
}

bool Doc::modRemoveMission(int i, std::string &err)
{
    if (!hasMod || i < 0 || i >= camp.nmissions) { err = "no such mission in the mod"; return false; }
    for (int k = i; k + 1 < camp.nmissions; k++) memcpy(camp.missions[k], camp.missions[k + 1], MISSION_ID_LEN);
    camp.nmissions--;
    modDirty = true;
    return true;
}

bool Doc::modMoveMission(int i, int to, std::string &err)
{
    if (!hasMod || i < 0 || i >= camp.nmissions) { err = "no such mission in the mod"; return false; }
    to = std::max(0, std::min(camp.nmissions - 1, to));
    char tmp[MISSION_ID_LEN];
    memcpy(tmp, camp.missions[i], MISSION_ID_LEN);
    if (to > i) for (int k = i; k < to; k++) memcpy(camp.missions[k], camp.missions[k + 1], MISSION_ID_LEN);
    else for (int k = i; k > to; k--) memcpy(camp.missions[k], camp.missions[k - 1], MISSION_ID_LEN);
    memcpy(camp.missions[to], tmp, MISSION_ID_LEN);
    modDirty = true;
    return true;
}

bool Doc::modOpenMission(int i, std::string &err)
{
    std::string p = modMissionPath(i);
    if (p.empty()) { err = "no such mission in the mod"; return false; }
    return openMission(p, err);
}

bool Doc::setEnv(const MissionEnv &e, bool commitUndo)
{
    if (!hasMission) return false;
    std::string snap = snapshot();
    mission.env = e;
    if (commitUndo) commit(snap);
    return true;
}
