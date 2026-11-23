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

static void modPanel(App &a)
{
    Doc &d = a.doc;
    std::string err;
    ImGui::SetNextItemWidth(-90);
    ImGui::InputText("##moddir", a.modDirBuf, sizeof a.modDirBuf);
    ImGui::SameLine();
    if (ImGui::Button("Open/new##mod")) guarded(a, [&a] {
        std::string e2;
        Doc &d2 = a.doc;
        if (d2.openMod(a.modDirBuf, e2)) {
            invalidateThumbs(a);
            a.say("mod '%s' (%d missions)", d2.camp.name, d2.camp.nmissions);
            if (!d2.modNotice.empty()) a.say("%s", d2.modNotice.c_str());
        }
        else a.say("mod: %s", e2.c_str());
    });
    if (!d.hasMod) { ImGui::TextDisabled("A mod is a campaign: mods/<mod>/campaign.json + one folder per mission."); return; }
    Campaign &c = d.camp;
    {   // the id keys the mod's save: made once, read-only
        char idbuf[CAMPAIGN_ID_LEN];
        snprintf(idbuf, sizeof idbuf, "%s", d.modIdOk() ? c.id : "(none: made on save)");
        ImGui::SetNextItemWidth(-60);
        ImGui::InputText("id##c", idbuf, sizeof idbuf, ImGuiInputTextFlags_ReadOnly);
    }
    if (ImGui::InputText("name##c", c.name, sizeof c.name)) d.modDirty = true;
    if (ImGui::InputText("author##c", c.author, sizeof c.author)) d.modDirty = true;
    if (ImGui::InputTextMultiline("##cdesc", c.description, sizeof c.description, ImVec2(-1, 48))) d.modDirty = true;
    bool story = c.affects & CAMPAIGN_AFFECTS_STORY, mp = c.affects & CAMPAIGN_AFFECTS_MULTIPLAYER;
    ImGui::TextUnformatted("affects");
    ImGui::SameLine();
    if (ImGui::Checkbox("Story", &story)) { c.affects = (c.affects & ~CAMPAIGN_AFFECTS_STORY) | (story ? CAMPAIGN_AFFECTS_STORY : 0); d.modDirty = true; }
    ImGui::SameLine();
    if (ImGui::Checkbox("Multiplayer", &mp)) { c.affects = (c.affects & ~CAMPAIGN_AFFECTS_MULTIPLAYER) | (mp ? CAMPAIGN_AFFECTS_MULTIPLAYER : 0); d.modDirty = true; }
    int un = c.unlock == CAMPAIGN_UNLOCK_OPEN ? 1 : 0;
    ImGui::SetNextItemWidth(110);
    if (ImGui::Combo("unlock", &un, "ordered\0open\0")) { c.unlock = un ? CAMPAIGN_UNLOCK_OPEN : CAMPAIGN_UNLOCK_ORDERED; d.modDirty = true; }
    if (ImGui::Button("Save mod")) doSaveMod(a);
    ImGui::SameLine();
    if (ImGui::Button("Check mod")) doCheckMod(a);
    if (d.modDirty) { ImGui::SameLine(); ImGui::TextDisabled("(unsaved)"); }
    ImGui::Separator();
    int act = -1, from = -1, to = -1;
    for (int i = 0; i < c.nmissions; i++) {
        ImGui::PushID(i);
        const std::string thumb = d.modDir + "/" + c.missions[i] + "/thumb.png";
        uint32_t tex = thumbTexture(a, thumb);
        if (tex) ImGui::Image((ImTextureID)(intptr_t)tex, ImVec2(64, 48));
        else { ImGui::Dummy(ImVec2(64, 48)); ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), IM_COL32(90, 90, 100, 255)); }
        ImGui::SameLine();
        ImGui::BeginGroup();
        const bool cur = d.hasMission && d.missionPath == d.modMissionPath(i);
        ImGui::Text("%d. %s%s", i + 1, c.missions[i], cur ? "  <" : "");
        if (ImGui::SmallButton("Open")) act = i;
        ImGui::SameLine();
        if (ImGui::SmallButton("Up") && i > 0) { from = i; to = i - 1; }
        ImGui::SameLine();
        if (ImGui::SmallButton("Dn") && i + 1 < c.nmissions) { from = i; to = i + 1; }
        ImGui::SameLine();
        if (ImGui::SmallButton("X")) { d.modRemoveMission(i, err); ImGui::EndGroup(); ImGui::PopID(); break; }
        ImGui::EndGroup();
        ImGui::PopID();
    }
    if (act >= 0) guarded(a, [&a, act] {
        std::string e2;
        if (a.doc.modOpenMission(act, e2)) { snprintf(a.missionPath, sizeof a.missionPath, "%s", a.doc.missionPath.c_str()); a.say("opened %s", a.doc.missionPath.c_str()); }
        else a.say("open: %s", e2.c_str());
    });
    if (from >= 0) d.modMoveMission(from, to, err);
    ImGui::Separator();
    ImGui::SetNextItemWidth(130);
    ImGui::InputText("##mnm", a.modNewName, sizeof a.modNewName);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    ImGui::InputText("##mlv", a.modNewLevel, sizeof a.modNewLevel);
    ImGui::SameLine();
    if (ImGui::Button("Add")) guarded(a, [&a] {
        std::string folder, e2;
        if (a.doc.modAddMission(a.modNewName, a.modNewLevel, "", folder, e2)) {
            snprintf(a.missionPath, sizeof a.missionPath, "%s", a.doc.missionPath.c_str());
            a.say("added mission '%s'", folder.c_str());
        } else a.say("add mission: %s", e2.c_str());
    });
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("Capture thumbnail (Mission tab) writes thumb.png into the open mission's folder.");
    ImGui::PopTextWrapPos();
}

// The right panel's Mod tab: today's mod panel, unchanged. Step 4 rebuilds it into the mod properties.
void drawModTab(App &a) { modPanel(a); }

} // namespace ed
