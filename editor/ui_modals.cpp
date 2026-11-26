// ui_modals.cpp: see ui_app.h for how the editor UI is split.
#include "ui_app.h"

#include <filesystem>


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

// File > New mission: the name, the base level, and where the mission starts from: the level as it is, or emptied (every
// original guard, the weapons they carry and every pickup removed, the doors, glass, scenery and objective objects kept).
void newMissionModal(App &a)
{
    if (a.askNewMission) { ImGui::OpenPopup("New mission"); a.askNewMission = false; }
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.3f), ImGuiCond_Appearing, ImVec2(0.5f, 0.0f));
    if (!ImGui::BeginPopupModal("New mission", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    a.modalOpen = true;
    Doc &d = a.doc;
    const char *lvl = missionLevelNameAt(a.nmLevel) ? missionLevelNameAt(a.nmLevel) : "";
    ImGui::TextUnformatted("Name");
    ImGui::SetNextItemWidth(360);
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ImGui::InputText("##nmname", a.nmName, sizeof a.nmName);
    ImGui::TextUnformatted("Base level");
    ImGui::SetNextItemWidth(200);
    if (ImGui::BeginCombo("##nmlevel", lvl)) {
        for (int i = 0; missionLevelNameAt(i); i++)
            if (ImGui::Selectable(missionLevelNameAt(i), a.nmLevel == i)) a.nmLevel = i;
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("fixed once the mission exists");
    ImGui::Spacing();
    ImGui::TextUnformatted("Starting point");
    ImGui::RadioButton("Level as is", &a.nmStart, 0);
    ImGui::TextDisabled("    all the original guards and objects are there");
    ImGui::RadioButton("Emptied level", &a.nmStart, 1);
    ImGui::TextDisabled("    the original guards, their weapons and every pickup (weapons, ammo,\n    body armour, keys and other collectables) are removed; doors, glass,\n    scenery and the objective objects stay");
    if (d.hasMod) {
        ImGui::Spacing();
        char label[200];
        snprintf(label, sizeof label, "Add to the open mod's campaign (%s)", d.camp.name[0] ? d.camp.name : d.modDir.c_str());
        ImGui::Checkbox(label, &a.nmAddToMod);
    }
    ImGui::Spacing();
    ImGui::Separator();
    {   // where it ends up, and what happens to the level
        std::string where;
        const bool toMod = d.hasMod && a.nmAddToMod;
        if (toMod) {
            const std::string mod = d.modDir.substr(d.modDir.find_last_of("/\\") == std::string::npos ? 0 : d.modDir.find_last_of("/\\") + 1);
            where = "mods/" + mod + "/" + d.modMissionFolder(a.nmName) + "/mission.json";
        } else {
            where = d.defaultMissionPath(a.nmName);
            const std::string data = d.gameDataDir();
            if (where.compare(0, data.size(), data) == 0) where = "<game data>" + where.substr(data.size());
        }
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 420);
        ImGui::TextDisabled("Saved to %s", where.c_str());
        const bool loaded = d.hasLevel && d.level.name == lvl;
        ImGui::TextDisabled("%s", loaded ? "The level is loaded." : exportPresent(exportDirFor(a.gameExe, lvl)) ? "The level loads from the export cache."
                                                                                                           : "The level is exported by the game the first time (a few seconds).");
        ImGui::PopTextWrapPos();
    }
    ImGui::Spacing();
    const bool okName = a.nmName[0] != 0;
    ImGui::BeginDisabled(!okName);
    if (ImGui::Button("Create", ImVec2(100, 0)) || (okName && ImGui::IsKeyPressed(ImGuiKey_Enter, false) && !ImGui::IsAnyItemActive())) {
        createNewMission(a);
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(100, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

// Mod > Publish...: the mod as a .pmod file. Publish writes <folder>/<mod>.pmod (to share); Deploy writes it into the game's
// mods folder (to play). Both save the mod first and refuse when it does not validate.
void publishModal(App &a)
{
    if (a.askPublish) { ImGui::OpenPopup("Publish mod"); a.askPublish = false; }
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.3f), ImGuiCond_Appearing, ImVec2(0.5f, 0.0f));
    if (!ImGui::BeginPopupModal("Publish mod", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    a.modalOpen = true;
    Doc &d = a.doc;
    ImGui::Text("%s.pmod", Doc::modFolderName(d.modDir).c_str());
    ImGui::TextUnformatted("Output folder");
    ImGui::SetNextItemWidth(420);
    ImGui::InputText("##pubdir", a.pubDir, sizeof a.pubDir);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 460);
    ImGui::TextDisabled("Deploy writes %s/%s.pmod, where the game finds it.", d.modsRoot().c_str(), Doc::modFolderName(d.modDir).c_str());
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    auto report = [&](bool ok, const std::string &path, uint64_t bytes, const std::string &err) {
        a.pubFailed = !ok;
        if (ok) { char b[1200]; snprintf(b, sizeof b, "wrote %s (%llu bytes)", path.c_str(), (unsigned long long)bytes); a.pubResult = b; }
        else a.pubResult = "refused: " + err;
        a.say("publish: %s", a.pubResult.c_str());
    };
    std::string path, err;
    uint64_t bytes = 0;
    if (ImGui::Button("Publish", ImVec2(100, 0))) {
        const bool ok = d.publishMod(a.pubDir, path, bytes, err);
        report(ok, path, bytes, err);
    }
    ImGui::SameLine();
    if (ImGui::Button("Deploy", ImVec2(100, 0))) {
        const bool ok = d.deployMod(path, bytes, err);
        report(ok, path, bytes, err);
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(100, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) ImGui::CloseCurrentPopup();
    if (!a.pubResult.empty()) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 460);
        ImGui::TextColored(a.pubFailed ? ImVec4(1.0f, 0.5f, 0.45f, 1) : ImVec4(0.55f, 0.95f, 0.55f, 1), "%s", a.pubResult.c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::EndPopup();
}

// Open mod... on a package file X.pmod: unpack it into a folder X.pmod/ (the game reads it like the file), then open that.
// In its own folder the file is renamed X.pmod.old; into another folder the file stays.
void extractModal(App &a)
{
    if (a.askExtract) { ImGui::OpenPopup("Extract package"); a.askExtract = false; }
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.3f), ImGuiCond_Appearing, ImVec2(0.5f, 0.0f));
    if (!ImGui::BeginPopupModal("Extract package", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    a.modalOpen = true;
    const std::string pm = a.extractPmod;
    const size_t s = pm.find_last_of('/');
    const std::string name = s == std::string::npos ? pm : pm.substr(s + 1), parent = s == std::string::npos ? "." : pm.substr(0, s);
    std::string dest = a.extractDest;
    for (char &c : dest) if (c == '\\') c = '/';
    while (dest.size() > 1 && dest.back() == '/') dest.pop_back();
    std::error_code ec;
    const bool inPlace = dest.empty() || (std::filesystem::exists(dest, ec) && std::filesystem::equivalent(dest, parent, ec));
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 460);
    ImGui::Text("%s is a package. To edit it, the editor unpacks it into a folder.", name.c_str());
    ImGui::PopTextWrapPos();
    ImGui::TextUnformatted("Into the folder");
    ImGui::SetNextItemWidth(420);
    ImGui::InputText("##exdest", a.extractDest, sizeof a.extractDest);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 460);
    if (inPlace) ImGui::TextDisabled("Makes the folder %s/ in place of the file; the file is kept as %s.old. The game plays the folder as it plays the file.", name.c_str(), name.c_str());
    else ImGui::TextDisabled("Makes %s/%s/; the package file stays where it is.", dest.c_str(), name.c_str());
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    if (ImGui::Button("Extract and open", ImVec2(140, 0))) {
        std::string out, err;
        if (Doc::extractMod(pm, inPlace ? std::string() : dest, out, err)) {
            a.say("mod: unpacked %s into %s", pm.c_str(), out.c_str());
            ImGui::CloseCurrentPopup();
            doOpenModPath(a, out);
        } else {
            a.extractErr = "refused: " + err;
            a.say("mod: %s", a.extractErr.c_str());
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(100, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) ImGui::CloseCurrentPopup();
    if (!a.extractErr.empty()) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 460);
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.45f, 1), "%s", a.extractErr.c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::EndPopup();
}

// The popups, drawn last in the frame: unsaved changes, mission level, New mission, Publish, Extract package, Help > Controls.
void drawModals(App &a)
{
    unsavedModal(a);
    levelSwitchModal(a);
    newMissionModal(a);
    publishModal(a);
    extractModal(a);
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
