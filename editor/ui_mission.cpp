// ui_mission.cpp: see ui_app.h for how the editor UI is split.
#include "ui_app.h"


namespace ed {

// ---- environment panel (save only: no sky or sea is drawn by the editor yet) ----

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

// The right panel's Mission tab (for now what the old File panel held: the mission and its level, files, the
// environment, the thumbnail). Step 3 rebuilds it into the mission properties.
void drawMissionTab(App &a)
{
    std::string err;
    if (a.doc.hasMission) {
        ImGui::TextColored(ImVec4(0.95f, 0.95f, 0.6f, 1), "%s%s", a.doc.mission.name, a.doc.dirty ? " *" : "");
        // the mission's own level, in red when the loaded level is another one (Play refuses it then)
        const bool other = a.doc.hasLevel && a.doc.level.name != a.doc.mission.level;
        ImGui::SameLine();
        if (other) ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1), "(%s, loaded: %s)", a.doc.mission.level, a.doc.level.name.c_str());
        else ImGui::TextDisabled("(%s)", a.doc.mission.level);
        ImGui::TextDisabled("%s", shortPath(a.doc.missionPath, 38).c_str());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", a.doc.missionPath.c_str());
    } else ImGui::TextDisabled("no mission open");
    ImGui::Separator();

    // the level: a solo level, exported by the game the first time and then kept next to it
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
    else if (!exportPresent(exportDirFor(a.gameExe, a.pickLevel))) ImGui::TextDisabled("not exported yet: Open runs the game once");
    if (ImGui::TreeNode("Export folder")) {
        ImGui::SetNextItemWidth(-60);
        ImGui::InputText("##ldir", a.levelDir, sizeof a.levelDir);
        ImGui::SameLine();
        if (ImGui::Button("Load")) {
            if (a.doc.openLevel(a.levelDir, err)) reloadLevelGpu(a); else a.say("open level: %s", err.c_str());
        }
        ImGui::TreePop();
    }

    ImGui::TextUnformatted("mission file");
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
    ImGui::SetNextItemWidth(-110);
    ImGui::InputText("##nname", a.newName, sizeof a.newName);
    ImGui::SameLine();
    if (ImGui::Button("New mission")) doNewMission(a);

    ImGui::SetNextItemWidth(-60);
    ImGui::InputText("##thumb", a.thumbPath, sizeof a.thumbPath);
    ImGui::SameLine();
    ImGui::TextUnformatted("thumb");
    if (ImGui::Button("Capture thumbnail")) doCapture(a);
    ImGui::SameLine();
    ImGui::TextDisabled("plain crop");
    ImGui::Separator();
    envPanel(a);
}

} // namespace ed
