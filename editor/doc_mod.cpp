// Mod (campaign) folder handling and the mission environment edit of Doc.
// A mod is mods/<mod>/campaign.json plus one folder per mission (mission.json, thumb.png).
#include "doc.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <random>

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

std::string generateUuid()
{
    std::random_device rd;
    std::seed_seq seq{ rd(), rd(), rd(), rd(), rd(), rd(), rd(), rd() };
    std::mt19937_64 gen(seq);
    uint64_t a = gen(), b = gen();
    a = (a & 0xffffffffffff0fffull) | 0x0000000000004000ull;   // version 4
    b = (b & 0x3fffffffffffffffull) | 0x8000000000000000ull;   // variant 10xx
    char s[40];
    snprintf(s, sizeof s, "%08x-%04x-%04x-%04x-%012llx", (unsigned)(a >> 32), (unsigned)((a >> 16) & 0xffff), (unsigned)(a & 0xffff),
             (unsigned)(b >> 48), (unsigned long long)(b & 0xffffffffffffull));
    return s;
}

bool uuidValid(const char *s)
{
    if (!s || strlen(s) != 36) return false;
    for (int i = 0; i < 36; i++) {
        if (i == 8 || i == 13 || i == 18 || i == 23) { if (s[i] != '-') return false; }
        else if (!isxdigit((unsigned char)s[i])) return false;
    }
    return true;
}

bool Doc::modIdOk() const { return uuidValid(camp.id); }

bool Doc::openMod(const std::string &dirIn, std::string &err, bool openFirst)
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
        snprintf(c.id, sizeof c.id, "%s", generateUuid().c_str());
        if (!campaignSaveFile(file.c_str(), &c, e, sizeof e)) { err = e; campaignFree(&c); return false; }
    }
    campaignFree(&camp);
    camp = c;   // takes over the owned pointers
    modNotice = uuidValid(camp.id) ? "" : "warning: campaign.json has no valid id; one is generated on the next save";
    hasMod = true;
    modDir = dir;
    modDirty = false;
    // its first mission (and so its level), unless that would drop unsaved edits; startup's --mission wins after
    if (openFirst && camp.nmissions > 0 && !(hasMission && dirty)) {
        std::string e2;
        if (!modOpenMission(0, e2)) modNotice += (modNotice.empty() ? "" : "; ") + ("first mission: " + e2);
    }
    return true;
}

static std::string slashes(std::string p)
{
    for (auto &c : p) if (c == '\\') c = '/';
    return p;
}

std::string Doc::modOfMission(const std::string &missionPath)
{
    const std::string mp = slashes(missionPath);
    const size_t s1 = mp.find_last_of('/');
    if (s1 == std::string::npos || s1 == 0) return "";
    const size_t s0 = mp.find_last_of('/', s1 - 1);
    if (s0 == std::string::npos) return "";
    const std::string mod = mp.substr(0, s0), folder = mp.substr(s0 + 1, s1 - s0 - 1);
    if (!exists(mod + "/campaign.json")) return "";
    Campaign c; campaignInit(&c);
    char e[256] = "";
    bool listed = false;
    if (campaignLoadFile((mod + "/campaign.json").c_str(), &c, e, sizeof e))
        for (int i = 0; i < c.nmissions; i++) if (folder == c.missions[i]) listed = true;
    campaignFree(&c);
    return listed ? mod : "";
}

int Doc::openModOfOpenMission(std::string &err)
{
    if (!hasMission || missionPath.empty()) return 0;
    const std::string mod = modOfMission(missionPath);
    if (mod.empty()) return 0;
    if (hasMod) {
        std::error_code ec;
        if (slashes(modDir) == mod || fs::equivalent(fs::path(modDir), fs::path(mod), ec)) return 0;
        if (modDirty) { err = "the open mod has unsaved changes: it stays open (save it, then open " + mod + ")"; return -1; }
    }
    return openMod(mod, err, false) ? 1 : -1;
}

std::string Doc::modsRoot() const
{
    std::string d = slashes(gameDataDir());
    while (!d.empty() && d.back() == '/') d.pop_back();
    const size_t s = d.find_last_of('/');
    return (s == std::string::npos ? std::string(".") : d.substr(0, s)) + "/mods";
}

std::string Doc::modCoverPath() const
{
    return hasMod ? modDir + "/cover.png" : "";
}

bool Doc::saveMod(std::string &err)
{
    if (!hasMod) { err = "no mod is open"; return false; }
    char e[256] = "";
    modNotice.clear();
    if (!modIdOk()) {   // a hand-written or older campaign.json: the save key is made now, once
        snprintf(camp.id, sizeof camp.id, "%s", generateUuid().c_str());
        modNotice = std::string("warning: campaign.json had no valid id; generated ") + camp.id;
    }
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

// A free folder name in the open mod for a mission called `name` (or for `folderIn`).
std::string Doc::modMissionFolder(const std::string &name, const std::string &folderIn) const
{
    std::string base = slug(folderIn.empty() ? name : folderIn), folder = base;
    for (int n = 2;; n++) {
        bool used = hasMod && exists(modDir + "/" + folder);
        for (int i = 0; i < camp.nmissions; i++) if (folder == camp.missions[i]) used = true;
        if (!used) break;
        folder = base + "_" + std::to_string(n);
    }
    return folder;
}

static void campaignAppend(Campaign &camp, const std::string &folder)
{
    char (*nm)[MISSION_ID_LEN] = (char (*)[MISSION_ID_LEN])realloc(camp.missions, sizeof(char[MISSION_ID_LEN]) * (size_t)(camp.nmissions + 1));
    if (!nm) abort();
    camp.missions = nm;
    snprintf(camp.missions[camp.nmissions++], MISSION_ID_LEN, "%s", folder.c_str());
}

bool Doc::modAddMission(const std::string &name, const std::string &level, const std::string &folderIn, std::string &outFolder, std::string &err, bool emptied)
{
    if (!hasMod) { err = "no mod is open (mod_open first)"; return false; }
    const std::string folder = modMissionFolder(name, folderIn);
    if (folder.size() >= MISSION_ID_LEN) { err = "folder name too long"; return false; }
    if (!newMission(name, level, err, emptied)) return false;
    std::error_code ec;
    fs::create_directories(fs::path(modDir + "/" + folder), ec);
    if (ec) { err = "cannot create the mission folder"; return false; }
    if (!saveMission(modDir + "/" + folder + "/mission.json", err)) return false;
    campaignAppend(camp, folder);
    outFolder = folder;
    return saveMod(err);
}

// The open mission joins the open mod: saved into <mod>/<folder>/mission.json (its thumb.png copied along), appended to the
// campaign. A mission that already lives in the mod's folders is just listed.
bool Doc::modAddOpenMission(std::string &outFolder, std::string &err)
{
    if (!hasMod) { err = "no mod is open"; return false; }
    if (!hasMission) { err = "no mission is open"; return false; }
    std::string mp = missionPath;
    for (auto &c : mp) if (c == '\\') c = '/';
    std::string md = modDir;
    for (auto &c : md) if (c == '\\') c = '/';
    std::string folder;
    if (!mp.empty() && mp.compare(0, md.size() + 1, md + "/") == 0) {
        const size_t s1 = mp.find_last_of('/');
        const size_t s0 = mp.find_last_of('/', s1 - 1);
        folder = mp.substr(s0 + 1, s1 - s0 - 1);
        if (mp.substr(0, s0) != md) { err = "the mission is nested deeper than one folder in the mod"; return false; }
        for (int i = 0; i < camp.nmissions; i++) if (folder == camp.missions[i]) { err = "the mission is already in the mod"; return false; }
    } else {
        folder = modMissionFolder(mission.name, "");
        if (folder.size() >= MISSION_ID_LEN) { err = "folder name too long"; return false; }
        const std::string oldThumb = mp.empty() ? "" : mp.substr(0, mp.find_last_of('/') + 1) + "thumb.png";
        std::error_code ec;
        fs::create_directories(fs::path(modDir + "/" + folder), ec);
        if (!saveMission(modDir + "/" + folder + "/mission.json", err)) return false;
        if (!oldThumb.empty() && exists(oldThumb)) fs::copy_file(oldThumb, modDir + "/" + folder + "/thumb.png", fs::copy_options::overwrite_existing, ec);
    }
    campaignAppend(camp, folder);
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
