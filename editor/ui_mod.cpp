// ui_mod.cpp: see ui_app.h for how the editor UI is split.
#include "ui_app.h"

#include <filesystem>

#include "png.h"


namespace ed {

void doSaveMod(App &a)
{
    std::string err;
    if (a.doc.saveMod(err)) { a.say("saved %s/campaign.json", a.doc.modDir.c_str()); if (!a.doc.modNotice.empty()) a.say("%s", a.doc.modNotice.c_str()); }
    else a.say("save mod: %s", err.c_str());
}

void doCheckMod(App &a)
{
    auto v = a.doc.validateMod();
    if (v.empty()) a.say("mod is valid");
    for (auto &s : v) a.say("mod: %s", s.c_str());
}

void doPublishMod(App &a)
{
    if (!a.doc.hasMod) { a.say("publish: no mod is open"); return; }
    snprintf(a.pubDir, sizeof a.pubDir, "%s", (a.doc.modDir + "/dist").c_str());
    a.pubResult.clear();
    a.pubFailed = false;
    a.askPublish = true;
}

// A text field of the campaign: marks the mod as changed.
static void modText(App &a, const char *label, char *buf, size_t n, bool multiline = false)
{
    bool ch;
    if (multiline) ch = ImGui::InputTextMultiline(label, buf, n, ImVec2(-1, 56));
    else { ImGui::SetNextItemWidth(-1); ch = ImGui::InputText(label, buf, n); }
    if (ch) a.doc.modDirty = true;
}

// The picture of the mod on the game's Mods screen: <mod>/cover.png when there is one, else its first mission's thumb.png.
static void coverRow(App &a)
{
    Doc &d = a.doc;
    const std::string cover = d.modCoverPath();
    std::error_code ec;
    const bool own = std::filesystem::exists(cover, ec);
    std::string shown = own ? cover : "";
    for (int i = 0; !own && i < d.camp.nmissions && shown.empty(); i++) {
        const std::string t = d.modDir + "/" + d.camp.missions[i] + "/thumb.png";
        if (std::filesystem::exists(t, ec)) shown = t;
    }
    ImGui::TextUnformatted("Picture (on the Mods screen)");
    const uint32_t tex = shown.empty() ? 0 : thumbTexture(a, shown);
    if (tex) ImGui::Image((ImTextureID)(intptr_t)tex, ImVec2(96, 72));
    else ImGui::Dummy(ImVec2(96, 72));
    ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), IM_COL32(90, 90, 100, 255));
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::TextDisabled("%s", own ? "cover.png" : shown.empty() ? "none yet" : "the first mission's thumbnail");
    ImGui::BeginDisabled(!fileDialogAvailable());
    if (ImGui::SmallButton("Choose picture...")) {
        std::string src, err;
        std::vector<uint8_t> px;
        if (fileDialogOpenImage("", src)) {
            if (!importPicture(src, 320, 240, px, err)) a.say("picture: %s", err.c_str());
            else if (!writePng(cover.c_str(), 320, 240, px.data())) a.say("picture: cannot write %s", cover.c_str());
            else { invalidateThumbs(a); a.say("mod picture: %s", cover.c_str()); }
        }
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("A PNG of your own: its centre 4:3 part, saved as cover.png (320x240)");
    if (own) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Remove##cover")) {
            std::filesystem::remove(cover, ec);
            invalidateThumbs(a);
            a.say("mod picture removed: the first mission's thumbnail is used");
        }
    }
    ImGui::EndGroup();
}

// The picture of a mod: cover.png, else its first mission's thumb.png that exists ("" if none).
static std::string modPicture(const std::string &dir, const Campaign &c)
{
    std::error_code ec;
    if (std::filesystem::exists(dir + "/cover.png", ec)) return dir + "/cover.png";
    for (int i = 0; i < c.nmissions; i++) {
        const std::string t = dir + "/" + c.missions[i] + "/thumb.png";
        if (std::filesystem::exists(t, ec)) return t;
    }
    return "";
}

// No mod open: the mods opened lately (their source folders, from the recent list) as cards (picture, name, missions and
// author); a click opens one with its first mission. The game's mods folder holds packages (.pmod), not sources.
static void modList(App &a)
{
    Doc &d = a.doc;
    const std::string root = d.modsRoot();
    (void)d;
    struct Card { std::string dir, name, sub, pic; };
    static std::string lastRoot;              // the folder is read again every half second at most, not every frame
    static double lastScan = -1;
    static std::vector<Card> mods;
    if (root != lastRoot || ImGui::GetTime() - lastScan > 0.5) {
        lastRoot = root; lastScan = ImGui::GetTime();
        mods.clear();
        std::error_code ec;
        for (const std::string &r : a.recent) {
            const std::filesystem::path rp(r);
            if (rp.filename() != "campaign.json") continue;
            const std::string dir = rp.parent_path().generic_string(), file = dir + "/campaign.json";
            if (!std::filesystem::exists(file, ec)) continue;
            Card k{ dir, rp.parent_path().filename().string(), "(campaign.json unreadable)", "" };
            Campaign c; campaignInit(&c);
            char e[200] = "";
            if (campaignLoadFile(file.c_str(), &c, e, sizeof e)) {
                if (c.name[0]) k.name = c.name;
                char s[600];
                snprintf(s, sizeof s, "%d mission%s%s%s", c.nmissions, c.nmissions == 1 ? "" : "s", c.author[0] ? " - " : "", c.author);
                k.sub = s;
                k.pic = modPicture(dir, c);
            }
            campaignFree(&c);
            mods.push_back(k);
        }
        std::sort(mods.begin(), mods.end(), [](const Card &x, const Card &y) { return x.name < y.name; });
    }
    ImGui::TextUnformatted("Recent mods");
    ImGui::SameLine();
    ImGui::TextDisabled("(%d)", (int)mods.size());
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Mods opened lately; the game plays the packages in %s", root.c_str());
    if (mods.empty()) ImGui::TextDisabled("No mod opened lately: open one, or type a folder above to create one.");
    const float rowH = 52.0f;
    for (size_t i = 0; i < mods.size(); i++) {
        ImGui::PushID((int)i);
        const Card k = mods[i];
        const ImVec2 p = ImGui::GetCursorScreenPos();
        if (ImGui::Selectable("##card", false, 0, ImVec2(0, rowH))) guarded(a, [&a, k] { doOpenModPath(a, k.dir); });
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", k.dir.c_str());
        ImDrawList *dl = ImGui::GetWindowDrawList();
        const ImVec2 pic0(p.x + 2, p.y + 2), pic1(p.x + 2 + 64, p.y + 2 + 48);
        const uint32_t tex = k.pic.empty() ? 0 : thumbTexture(a, k.pic);
        if (tex) dl->AddImage((ImTextureID)(intptr_t)tex, pic0, pic1);
        dl->AddRect(pic0, pic1, IM_COL32(90, 90, 100, 255));
        const float tx = pic1.x + 8, maxX = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
        const ImVec4 clip(tx, p.y, maxX, p.y + rowH);
        dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(tx, p.y + 8), ImGui::GetColorU32(ImGuiCol_Text), k.name.c_str(), nullptr, 0, &clip);
        dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(tx, p.y + 8 + ImGui::GetTextLineHeightWithSpacing()), ImGui::GetColorU32(ImGuiCol_TextDisabled),
                    k.sub.c_str(), nullptr, 0, &clip);
        ImGui::PopID();
    }
}
static void propertiesSection(App &a)
{
    Doc &d = a.doc;
    Campaign &c = d.camp;
    if (!ImGui::CollapsingHeader("Mod properties", ImGuiTreeNodeFlags_DefaultOpen)) return;
    {   // the id keys the mod's save: made once, read-only
        char idbuf[CAMPAIGN_ID_LEN];
        snprintf(idbuf, sizeof idbuf, "%s", d.modIdOk() ? c.id : "(none: made on save)");
        ImGui::TextDisabled("id (keys the mod's save)");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##cid", idbuf, sizeof idbuf, ImGuiInputTextFlags_ReadOnly);
    }
    ImGui::TextUnformatted("Name");
    modText(a, "##cname", c.name, sizeof c.name);
    ImGui::TextUnformatted("Author");
    modText(a, "##cauthor", c.author, sizeof c.author);
    ImGui::TextUnformatted("Description (shown on the mod chooser)");
    modText(a, "##cdesc", c.description, sizeof c.description, true);
    bool story = c.affects & CAMPAIGN_AFFECTS_STORY, mp = c.affects & CAMPAIGN_AFFECTS_MULTIPLAYER;
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Affects");
    ImGui::SameLine();
    if (ImGui::Checkbox("Story", &story)) { c.affects = (c.affects & ~CAMPAIGN_AFFECTS_STORY) | (story ? CAMPAIGN_AFFECTS_STORY : 0); d.modDirty = true; }
    ImGui::SameLine();
    if (ImGui::Checkbox("Multiplayer", &mp)) { c.affects = (c.affects & ~CAMPAIGN_AFFECTS_MULTIPLAYER) | (mp ? CAMPAIGN_AFFECTS_MULTIPLAYER : 0); d.modDirty = true; }
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Unlock order");
    ImGui::SameLine();
    int un = c.unlock == CAMPAIGN_UNLOCK_OPEN ? 1 : 0;
    if (ImGui::RadioButton("Ordered", &un, 0)) { c.unlock = CAMPAIGN_UNLOCK_ORDERED; d.modDirty = true; }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("As the game: a mission opens when the one before it is completed");
    ImGui::SameLine();
    if (ImGui::RadioButton("Open", &un, 1)) { c.unlock = CAMPAIGN_UNLOCK_OPEN; d.modDirty = true; }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Every mission is available from the start");
    coverRow(a);
    if (ImGui::Button("Save mod")) doSaveMod(a);
    ImGui::SameLine();
    if (ImGui::Button("Check mod")) doCheckMod(a);
    ImGui::SameLine();
    if (ImGui::Button("Publish...")) doPublishMod(a);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Packs the mod into a .pmod file (to share) or into the game's mods folder (to play)");
    if (d.modDirty) { ImGui::SameLine(); ImGui::TextDisabled("(unsaved)"); }
}

static void missionsSection(App &a)
{
    Doc &d = a.doc;
    Campaign &c = d.camp;
    std::string err;
    char head[64];
    snprintf(head, sizeof head, "Missions (%d)###modmissions", c.nmissions);
    if (!ImGui::CollapsingHeader(head, ImGuiTreeNodeFlags_DefaultOpen)) return;
    int act = -1, from = -1, to = -1, del = -1;
    for (int i = 0; i < c.nmissions; i++) {
        ImGui::PushID(i);
        const std::string path = d.modMissionPath(i);
        const std::string thumb = d.modDir + "/" + c.missions[i] + "/thumb.png";
        const uint32_t tex = thumbTexture(a, thumb);
        if (tex) ImGui::Image((ImTextureID)(intptr_t)tex, ImVec2(64, 48));
        else { ImGui::Dummy(ImVec2(64, 48)); ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), IM_COL32(90, 90, 100, 255)); }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tex ? thumb.c_str() : "no thumb.png yet: capture the view in the Mission tab");
        ImGui::SameLine();
        ImGui::BeginGroup();
        const App::MissionInfo &mi = missionInfo(a, path);
        const bool cur = d.hasMission && d.missionPath == path;
        ImGui::Text("%d. %s%s", i + 1, mi.ok ? mi.name.c_str() : c.missions[i], cur ? "  <" : "");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", path.c_str());
        ImGui::TextDisabled("%s", mi.ok ? mi.level.c_str() : "(no mission.json)");
        if (ImGui::SmallButton("Open")) act = i;
        ImGui::SameLine();
        ImGui::BeginDisabled(i == 0);
        if (ImGui::SmallButton("Up")) { from = i; to = i - 1; }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(i + 1 >= c.nmissions);
        if (ImGui::SmallButton("Dn")) { from = i; to = i + 1; }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::SmallButton("Remove")) del = i;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Takes it out of the campaign; the folder stays on disk");
        ImGui::EndGroup();
        ImGui::PopID();
    }
    if (act >= 0) guarded(a, [&a, act] {
        std::string e2;
        if (a.doc.modOpenMission(act, e2)) { snprintf(a.missionPath, sizeof a.missionPath, "%s", a.doc.missionPath.c_str()); a.say("opened %s", a.doc.missionPath.c_str()); }
        else a.say("open: %s", e2.c_str());
    });
    if (from >= 0) d.modMoveMission(from, to, err);
    if (del >= 0 && !d.modRemoveMission(del, err)) a.say("mod: %s", err.c_str());
    if (c.nmissions == 0) ImGui::TextDisabled("No missions yet.");
    ImGui::Spacing();
    if (ImGui::Button("New mission...")) doNewMission(a);
    ImGui::SameLine();
    ImGui::BeginDisabled(!d.hasMission);
    if (ImGui::Button("Add the open mission")) {
        std::string folder;
        if (d.modAddOpenMission(folder, err)) {
            snprintf(a.missionPath, sizeof a.missionPath, "%s", d.missionPath.c_str());
            a.infoCache.clear();
            a.say("added mission '%s' to the mod", folder.c_str());
        } else a.say("add mission: %s", err.c_str());
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Saves the open mission into the mod's folder and lists it");
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("The picture is the mission's thumb.png (Mission tab > Thumbnail).");
    ImGui::PopTextWrapPos();
}

// The right panel's Mod tab: the mod's properties and its missions.
void drawModTab(App &a)
{
    Doc &d = a.doc;
    ImGui::SetNextItemWidth(-90);
    ImGui::InputTextWithHint("##moddir", "mod folder (its own repo)", a.modDirBuf, sizeof a.modDirBuf);
    ImGui::SameLine();
    if (ImGui::Button("Open/new##mod")) guarded(a, [&a] { doOpenModPath(a, a.modDirBuf); });
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Opens the mod in this folder, or creates it when the folder has no campaign.json");
    if (fileDialogAvailable()) {
        if (ImGui::Button("Open mod...")) doOpenModDialog(a);
        if (d.hasMod) {
            ImGui::SameLine();
            if (ImGui::Button("Other mods")) {   // back to the list of mods, the mission stays open
                if (d.modDirty) a.say("mod: unsaved changes (Save mod first)");
                else { campaignFree(&d.camp); campaignInit(&d.camp); d.hasMod = false; d.modDir.clear(); }
            }
        }
    }
    if (!d.hasMod) {
        ImGui::PushTextWrapPos(0);
        ImGui::TextDisabled("A mod is a campaign: a folder of its own (keep it in its own git repo) with campaign.json plus one folder per mission. Play and Publish... pack it into a .pmod. Opening one of its missions opens it too.");
        ImGui::PopTextWrapPos();
        ImGui::Separator();
        modList(a);
        return;
    }
    propertiesSection(a);
    missionsSection(a);
}

} // namespace ed
