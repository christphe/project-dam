// ui_mission.cpp: see ui_app.h for how the editor UI is split.
#include "ui_app.h"


namespace ed {

// ---- Environment section (save only: no sky or sea is drawn by the editor yet) ----

static void envPanel(App &a)
{
    if (!a.doc.hasMission) return;
    if (!ImGui::CollapsingHeader("Environment")) return;
    ImGui::TextWrapped("Overrides of the level's own sky and sea. The editor draws no sky or sea yet: preview it in the game (Play).");
    Doc &d = a.doc;
    MissionEnv e = d.mission.env;
    // sliders: live while dragged, one undo step per drag
    auto track = [&](bool changed) {
        if (ImGui::IsItemActivated()) a.envSnap = d.snapshot();
        if (changed) d.setEnv(e, false);
        if (ImGui::IsItemDeactivatedAfterEdit()) d.commit(a.envSnap);
    };
    // an override checkbox: ticking it sets the defaults, unticking returns to the level's own
    auto over = [&](const char *label, int &flag) {
        bool b = flag != 0;
        if (ImGui::Checkbox(label, &b)) { flag = b; return true; }
        return false;
    };
    int mood = (int)e.mood + 1;
    ImGui::SetNextItemWidth(130);
    if (ImGui::Combo("mood", &mood, "level's own\0day\0dusk\0night\0")) { e.mood = (MissionMood)(mood - 1); d.setEnv(e, true); }

    if (over("sun##ov", e.has_sun)) {
        if (e.has_sun) { e.sun_azimuth = 0; e.sun_elevation = 30; e.sun_color[0] = e.sun_color[1] = e.sun_color[2] = 1; e.sun_glow = 0.4f; }
        d.setEnv(e, true);
    }
    if (e.has_sun) {
        track(ImGui::SliderFloat("azimuth", &e.sun_azimuth, 0, 360, "%.0f deg"));
        track(ImGui::SliderFloat("elevation", &e.sun_elevation, -10, 90, "%.0f deg"));
        track(ImGui::ColorEdit3("sun colour", e.sun_color));
        track(ImGui::SliderFloat("glow", &e.sun_glow, 0, 2));
        ImGui::TextDisabled("Drag the sun marker in the view too. Azimuth 0 = +z.");
    }
    if (over("sky colour##ov", e.has_sky)) { if (e.has_sky) { e.sky[0] = 0.55f; e.sky[1] = 0.62f; e.sky[2] = 0.7f; } d.setEnv(e, true); }
    if (e.has_sky) track(ImGui::ColorEdit3("sky", e.sky));
    if (over("clouds colour##ov", e.has_clouds)) { if (e.has_clouds) { e.clouds[0] = 0.9f; e.clouds[1] = 0.9f; e.clouds[2] = 0.95f; } d.setEnv(e, true); }
    if (e.has_clouds) track(ImGui::ColorEdit3("clouds", e.clouds));
    if (over("cloud speed##ov", e.has_cloud_speed)) { if (e.has_cloud_speed) e.cloud_speed = 1; d.setEnv(e, true); }
    if (e.has_cloud_speed) track(ImGui::DragFloat("speed##cl", &e.cloud_speed, 0.05f, 0, 20));

    if (over("sea##ov", e.has_sea)) { if (e.has_sea) e.sea_enabled = 1; d.setEnv(e, true); }
    if (e.has_sea) {
        bool en = e.sea_enabled != 0;
        if (ImGui::Checkbox("enabled##sea", &en)) { e.sea_enabled = en; d.setEnv(e, true); }
        if (over("height##ov", e.has_sea_height)) { if (e.has_sea_height) e.sea_height = 0; d.setEnv(e, true); }
        if (e.has_sea_height) track(ImGui::DragFloat("##sh", &e.sea_height, 1.0f));
        if (over("colour##ovs", e.has_sea_color)) { if (e.has_sea_color) { e.sea_color[0] = 0.1f; e.sea_color[1] = 0.3f; e.sea_color[2] = 0.35f; } d.setEnv(e, true); }
        if (e.has_sea_color) track(ImGui::ColorEdit3("##sc", e.sea_color));
        if (over("swell##ov", e.has_swell)) { if (e.has_swell) e.swell = 1; d.setEnv(e, true); }
        if (e.has_swell) track(ImGui::DragFloat("##sw", &e.swell, 0.05f, 0, 10));
        if (over("chop##ov", e.has_chop)) { if (e.has_chop) e.chop = 0.5f; d.setEnv(e, true); }
        if (e.has_chop) track(ImGui::DragFloat("##ch", &e.chop, 0.05f, 0, 10));
        if (over("wave speed##ov", e.has_speed)) { if (e.has_speed) e.speed = 1; d.setEnv(e, true); }
        if (e.has_speed) track(ImGui::DragFloat("##ws", &e.speed, 0.05f, 0, 10));
    }
}


// ---- General ----

// Call right after an input that edits a buffer of the mission itself: one undo step per edit.
static void editDone(App &a)
{
    if (ImGui::IsItemActivated()) a.fieldSnap = a.doc.snapshot();
    if (ImGui::IsItemDeactivatedAfterEdit()) a.doc.commit(a.fieldSnap);
}

static void generalSection(App &a)
{
    Doc &d = a.doc;
    if (!ImGui::CollapsingHeader("General", ImGuiTreeNodeFlags_DefaultOpen)) return;
    ImGui::TextUnformatted("Name");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##mname", d.mission.name, sizeof d.mission.name);
    editDone(a);
    // the mission's own level, in red when the loaded level is another one (Play refuses it then)
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Base level");
    ImGui::SameLine();
    const bool other = d.hasLevel && d.level.name != d.mission.level;
    if (other) ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1), "%s (loaded: %s)", d.mission.level, d.level.name.c_str());
    else ImGui::TextColored(ImVec4(0.95f, 0.95f, 0.6f, 1), "%s", d.mission.level);
    ImGui::SameLine();
    ImGui::TextDisabled("fixed at creation");
    if (other) {
        ImGui::BeginDisabled(a.job.running);
        char b[96];
        snprintf(b, sizeof b, "Load %s", d.mission.level);
        if (ImGui::SmallButton(b)) requestLevel(a, d.mission.level, false);
        ImGui::SameLine();
        snprintf(b, sizeof b, "Move the mission to %s...", d.level.name.c_str());
        if (ImGui::SmallButton(b)) { a.levelSwitchTo = d.level.name; a.askLevelSwitch = true; }
        ImGui::EndDisabled();
    }
    ImGui::TextUnformatted("Briefing");
    ImGui::InputTextMultiline("##brief", d.mission.briefing, sizeof d.mission.briefing, ImVec2(-1, 90));
    editDone(a);
    bool keep = d.mission.keep_level_objectives != 0;
    if (ImGui::Checkbox("Keep the level's objectives", &keep)) {
        const std::string snap = d.snapshot();
        d.mission.keep_level_objectives = keep ? 1 : 0;
        d.commit(snap);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("The level's briefing, objectives and exit stay; the mission's objectives are added after them.");
}

// ---- Start and test (what the toolbar's Play uses) ----

static void startSection(App &a)
{
    if (!ImGui::CollapsingHeader("Start and test", ImGuiTreeNodeFlags_DefaultOpen)) return;
    {
        char label[96];
        char *end = nullptr;
        const long n = a.spawn.empty() ? -1 : strtol(a.spawn.c_str(), &end, 0);
        if (a.spawn.empty()) snprintf(label, sizeof label, "Play starts at: the level start");
        else if (end && !*end) snprintf(label, sizeof label, "Play starts at: pad 0x%lx", n);
        else snprintf(label, sizeof label, "Play starts at: mission pad %s", a.spawn.c_str());
        ImGui::TextUnformatted(label);
    }
    if (ImGui::Button("Level start")) { a.spawn.clear(); a.pickSpawn = false; }
    ImGui::SameLine();
    if (a.pickSpawn) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    if (ImGui::Button(a.pickSpawn ? "Click a pad in the view..." : "A pad (pick in the view)")) a.pickSpawn = !a.pickSpawn;
    if (a.pickSpawn) ImGui::PopStyleColor();
    if (ImGui::Checkbox("Co-op", &a.playCoop)) savePrefs(a);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Play with two players (two pads), else solo");
    ImGui::SameLine();
    if (ImGui::Button("Play")) doPlay(a);
}

// ---- Objectives ----

static void objectivesSection(App &a)
{
    Doc &d = a.doc;
    if (!ImGui::CollapsingHeader("Objectives", ImGuiTreeNodeFlags_DefaultOpen)) return;
    static const char *kDiff[3] = { "Agent", "Secret Agent", "00 Agent" };
    std::string err;
    int up = -1, down = -1, del = -1;
    for (int i = 0; i < d.mission.nobjectives; i++) {
        MissionObjective &o = d.mission.objectives[i];
        ImGui::PushID(i);
        ImGui::AlignTextToFramePadding();
        ImGui::Text("%c.", 'a' + i % 26);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##text", o.text, sizeof o.text);
        editDone(a);
        ImGui::Dummy(ImVec2(18, 0));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120);
        if (ImGui::BeginCombo("##diff", kDiff[o.difficulty < 0 ? 0 : o.difficulty > 2 ? 2 : o.difficulty])) {
            for (int k = 0; k < 3; k++)
                if (ImGui::Selectable(kDiff[k], o.difficulty == k) && o.difficulty != k) {
                    const std::string snap = d.snapshot();
                    o.difficulty = k;
                    d.commit(snap);
                }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("The lowest difficulty this objective applies to");
        ImGui::SameLine();
        ImGui::BeginDisabled(i == 0);
        if (ImGui::SmallButton("Up")) up = i;
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(i + 1 >= d.mission.nobjectives);
        if (ImGui::SmallButton("Dn")) down = i;
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::SmallButton("X")) del = i;
        ImGui::PopID();
    }
    if (up >= 0) d.moveObjective(up, up - 1, err);
    else if (down >= 0) d.moveObjective(down, down + 1, err);
    else if (del >= 0) d.removeObjective(del, err);
    if (ImGui::Button("Add objective")) {
        if (!d.addObjective("New objective", 0, err)) a.say("edit: %s", err.c_str());
    }
    ImGui::TextDisabled("An objective is completed by a script or a zone.");
}

// ---- File: the paths, opening, saving, the level ----

static void fileSection(App &a)
{
    std::string err;
    if (!ImGui::CollapsingHeader("File", a.doc.hasMission ? 0 : ImGuiTreeNodeFlags_DefaultOpen)) return;
    if (a.doc.hasMission) {
        ImGui::TextDisabled("%s", shortPath(a.doc.missionPath, 44).c_str());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", a.doc.missionPath.c_str());
    }
    ImGui::SetNextItemWidth(-60);
    ImGui::InputText("##mpath", a.missionPath, sizeof a.missionPath);
    ImGui::SameLine();
    if (ImGui::Button("Open")) guarded(a, [&a] { doOpenPath(a, a.missionPath); });
    if (fileDialogAvailable()) {
        if (ImGui::Button("Open...")) guarded(a, [&a] { doOpenDialog(a); });
        ImGui::SameLine();
        ImGui::BeginDisabled(!a.doc.hasMission);
        if (ImGui::Button("Save as...")) doSaveAs(a);
        ImGui::EndDisabled();
    }
    if (!a.recent.empty()) {
        ImGui::TextDisabled("recent");
        recentList(a, false);
    }
    ImGui::Spacing();
    ImGui::TextDisabled("level");
    ImGui::SetNextItemWidth(-160);
    if (ImGui::BeginCombo("##lvl", a.pickLevel)) {
        for (int i = 0; missionLevelNameAt(i); i++) {
            const char *n = missionLevelNameAt(i);
            if (ImGui::Selectable(n, strcmp(n, a.pickLevel) == 0)) snprintf(a.pickLevel, sizeof a.pickLevel, "%s", n);
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(a.job.running);
    if (ImGui::Button("Open##lvl")) {
        // a mission made for another level: ask before loading this one under it
        if (a.doc.hasMission && a.doc.mission.level[0] && strcmp(a.doc.mission.level, a.pickLevel) != 0) {
            a.levelSwitchTo = a.pickLevel;
            a.askLevelSwitch = true;
        } else requestLevel(a, a.pickLevel, false);
    }
    ImGui::SameLine();
    if (ImGui::Button("Re-export")) requestLevel(a, a.pickLevel, true);
    ImGui::EndDisabled();
    if (a.job.running) ImGui::TextColored(ImVec4(1.0f, 0.82f, 0.48f, 1.0f), "exporting %s...", a.job.level.c_str());
    else if (!exportPresent(exportDirFor(a.gameExe, a.pickLevel))) ImGui::TextDisabled("not exported yet: Open runs the game");
    if (ImGui::TreeNode("Export folder")) {
        ImGui::SetNextItemWidth(-60);
        ImGui::InputText("##ldir", a.levelDir, sizeof a.levelDir);
        ImGui::SameLine();
        if (ImGui::Button("Load")) {
            if (a.doc.openLevel(a.levelDir, err)) reloadLevelGpu(a); else a.say("open level: %s", err.c_str());
        }
        ImGui::TreePop();
    }
}

// The right panel's Mission tab: the mission's properties, then the file section.
void drawMissionTab(App &a)
{
    if (!a.doc.hasMission) {
        ImGui::TextDisabled("No mission open.");
        if (ImGui::Button("New mission...")) doNewMission(a);
        ImGui::SameLine();
        if (fileDialogAvailable() && ImGui::Button("Open...")) guarded(a, [&a] { doOpenDialog(a); });
        ImGui::Spacing();
        fileSection(a);
        return;
    }
    if (a.doc.dirty) ImGui::TextColored(ImVec4(0.95f, 0.95f, 0.6f, 1), "unsaved changes");
    generalSection(a);
    startSection(a);
    objectivesSection(a);
    envPanel(a);
    thumbSection(a);
    fileSection(a);
}

} // namespace ed
