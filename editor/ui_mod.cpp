// ui_mod.cpp: see ui_app.h for how the editor UI is split.
#include "ui_app.h"


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

// A text field of the campaign: marks the mod as changed.
static void modText(App &a, const char *label, char *buf, size_t n, bool multiline = false)
{
    bool ch;
    if (multiline) ch = ImGui::InputTextMultiline(label, buf, n, ImVec2(-1, 56));
    else { ImGui::SetNextItemWidth(-1); ch = ImGui::InputText(label, buf, n); }
    if (ch) a.doc.modDirty = true;
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
    if (ImGui::Button("Save mod")) doSaveMod(a);
    ImGui::SameLine();
    if (ImGui::Button("Check mod")) doCheckMod(a);
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
    ImGui::InputText("##moddir", a.modDirBuf, sizeof a.modDirBuf);
    ImGui::SameLine();
    if (ImGui::Button("Open/new##mod")) guarded(a, [&a] {
        std::string e2;
        Doc &d2 = a.doc;
        if (d2.openMod(a.modDirBuf, e2)) {
            invalidateThumbs(a);
            a.infoCache.clear();
            a.say("mod '%s' (%d missions)", d2.camp.name, d2.camp.nmissions);
            if (!d2.modNotice.empty()) a.say("%s", d2.modNotice.c_str());
        }
        else a.say("mod: %s", e2.c_str());
    });
    if (!d.hasMod) { ImGui::TextWrapped("A mod is a campaign: mods/<mod>/campaign.json plus one folder per mission. Type a folder above to open or create one."); return; }
    propertiesSection(a);
    missionsSection(a);
}

} // namespace ed
