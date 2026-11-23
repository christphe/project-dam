// ui_modals.cpp: see ui_app.h for how the editor UI is split.
#include "ui_app.h"


namespace ed {

// File > level Open with a mission made for another level: move the mission to the new level (its pads
// and items keep their positions, now read against that level), or cancel. Loading the level under the
// mission without changing it is what used to happen silently, and Play then refused the mission.
void levelSwitchModal(App &a)
{
    if (a.askLevelSwitch) { ImGui::OpenPopup("Mission level"); a.askLevelSwitch = false; }
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.4f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal("Mission level", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    a.modalOpen = true;
    ImGui::Text("The open mission \"%s\" is for %s.", a.doc.mission.name, a.doc.mission.level);
    ImGui::Text("Open %s:", a.levelSwitchTo.c_str());
    ImGui::Separator();
    char move[96];
    snprintf(move, sizeof move, "Move the mission to %s", a.levelSwitchTo.c_str());
    if (ImGui::Button(move)) {
        std::string err;
        if (a.doc.setMissionLevel(a.levelSwitchTo, err)) {
            a.say("the mission is now for %s (not saved yet)", a.levelSwitchTo.c_str());
            requestLevel(a, a.levelSwitchTo, false);
        } else a.say("mission level: %s", err.c_str());
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(100, 0))) {
        snprintf(a.pickLevel, sizeof a.pickLevel, "%s", a.doc.hasLevel ? a.doc.level.name.c_str() : a.doc.mission.level);
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void unsavedModal(App &a)
{
    if (a.askUnsaved) { ImGui::OpenPopup("Unsaved changes"); a.askUnsaved = false; }
    a.modalOpen = false;
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.4f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal("Unsaved changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    a.modalOpen = true;
    ImGui::Text("Unsaved changes in %s.", a.doc.mission.name);
    ImGui::TextDisabled("%s", a.doc.missionPath.c_str());
    ImGui::Separator();
    auto finish = [&](bool run) {
        std::function<void()> fn = std::move(a.pending);
        a.pending = nullptr;
        ImGui::CloseCurrentPopup();
        if (run && fn) fn();
    };
    if (ImGui::Button("Save", ImVec2(100, 0))) {
        doSave(a);
        if (!a.doc.dirty) finish(true);      // a failed save keeps the dialog (and the mission) open
    }
    ImGui::SameLine();
    if (ImGui::Button("Discard", ImVec2(100, 0))) { a.doc.dirty = false; finish(true); }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(100, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) finish(false);
    ImGui::EndPopup();
}

// The popups, drawn last in the frame: unsaved changes, mission level, and Help > Controls.
void drawModals(App &a)
{
    unsavedModal(a);
    levelSwitchModal(a);
    drawHelpWindow(a);
}

void drawHelpWindow(App &a)
{
    if (!a.showHelp) return;
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.3f), ImGuiCond_Appearing, ImVec2(0.5f, 0.0f));
    if (!ImGui::Begin("Controls", &a.showHelp, ImGuiWindowFlags_AlwaysAutoResize)) { ImGui::End(); return; }
    auto row = [](const char *keys, const char *what) { ImGui::TextColored(ImVec4(0.95f, 0.95f, 0.6f, 1), "%s", keys); ImGui::SameLine(190); ImGui::TextUnformatted(what); };
    ImGui::SeparatorText("Camera");
    row("hold right button", "fly: mouse looks around");
    row("W A S D / Z Q S D / arrows", "move (only while the right button is held)");
    row("Space / Ctrl / Shift", "up / down / fast (while flying)");
    row("middle button", "pan: the point under the cursor follows");
    row("mouse wheel", "zoom toward the cursor");
    row("F", "frame the selection");
    ImGui::SeparatorText("Editing");
    row("left click", "select (or place, with the Guard / Object tool)");
    row("drag the gizmo", "arrows = axis, square = plane, ring = yaw");
    row("G, then X / Y / Z", "grab and move; lock an axis; click confirms, Esc cancels");
    row("Ctrl+C / Ctrl+V / Ctrl+D", "copy / paste under the cursor / duplicate (view hovered)");
    row("Delete", "remove the selected guard or object");
    row("Ctrl+Z", "undo");
    row("Esc", "back to the Select tool");
    ImGui::SeparatorText("Files");
    row("Ctrl+S / Ctrl+Shift+S / Ctrl+O", "save / save as / open");
    ImGui::End();
}

} // namespace ed
