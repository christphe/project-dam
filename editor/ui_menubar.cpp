// The main menu bar. Every item is an action that also exists elsewhere (toolbar, shortcut, panel): this file only
// lists them. Paste / Duplicate from the menu act at the middle of the 3D view, since there is no cursor in it.
#include "ui_app.h"

namespace ed {

void drawMenuBar(App &a)
{
    if (!ImGui::BeginMainMenuBar()) return;
    const bool hasM = a.doc.hasMission;
    const bool dlg = fileDialogAvailable();
    // test aids (--open-menu) click a title at the position it had last frame
    auto title = [&](const char *name) {
        const bool open = ImGui::BeginMenu(name);
        const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
        a.menuPos[name] = ImVec2((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f);
        return open;
    };

    if (title("File")) {
        if (ImGui::MenuItem("New mission...")) doNewMission(a);
        if (ImGui::MenuItem("Open...", "Ctrl+O", false, dlg)) guarded(a, [&a] { doOpenDialog(a); });
        if (ImGui::BeginMenu("Recent")) { recentList(a, true); ImGui::EndMenu(); }
        ImGui::Separator();
        if (ImGui::MenuItem("Save", "Ctrl+S", false, hasM)) doSave(a);
        if (ImGui::MenuItem("Save as...", "Ctrl+Shift+S", false, hasM && dlg)) doSaveAs(a);
        ImGui::Separator();
        if (ImGui::MenuItem("Quit")) guarded(a, [&a] { a.wantQuit = true; });
        ImGui::EndMenu();
    }
    if (title("Edit")) {
        const bool item = a.doc.sel.kind == Selection::ITEM;
        if (ImGui::MenuItem("Undo", "Ctrl+Z", false, hasM)) undoLast(a);
        ImGui::Separator();
        if (ImGui::MenuItem("Copy", "Ctrl+C", false, item)) copySelected(a);
        if (ImGui::MenuItem("Paste", "Ctrl+V", false, a.doc.clip.valid)) { Vec3 o, d; viewCenterRay(a, o, d); pasteAtMouse(a, o, d); }
        if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, item)) { Vec3 o, d; viewCenterRay(a, o, d); duplicateSelected(a, o, d); }
        if (ImGui::MenuItem("Delete", "Del", false, item)) deleteSelected(a);
        ImGui::EndMenu();
    }
    if (title("View")) {
        ImGui::MenuItem("Rooms", nullptr, &a.showRooms);
        ImGui::MenuItem("Props", nullptr, &a.showProps);
        ImGui::MenuItem("Pads", nullptr, &a.showPads);
        ImGui::MenuItem("Portals", nullptr, &a.showPortals);
        ImGui::MenuItem("Stan tiles", nullptr, &a.showStan);
        ImGui::Separator();
        ImGui::MenuItem("Props browser", nullptr, &a.showPropsWin);
        ImGui::MenuItem("Show log", nullptr, &a.showLog);
        ImGui::EndMenu();
    }
    if (title("Mission")) {
        if (ImGui::MenuItem("Validate", nullptr, false, hasM)) doValidate(a);
        if (ImGui::MenuItem("Play", nullptr, false, hasM)) doPlay(a);
        if (ImGui::MenuItem("Co-op", nullptr, a.playCoop)) { a.playCoop = !a.playCoop; savePrefs(a); }
        ImGui::Separator();
        if (ImGui::MenuItem("Capture thumbnail", nullptr, false, hasM)) doCapture(a);
        ImGui::EndMenu();
    }
    if (title("Mod")) {
        if (ImGui::MenuItem("Open/new mod...")) a.forceTab = 2;      // the path field lives in the Mod tab
        if (ImGui::MenuItem("Save mod", nullptr, false, a.doc.hasMod)) doSaveMod(a);
        if (ImGui::MenuItem("Check mod", nullptr, false, a.doc.hasMod)) doCheckMod(a);
        ImGui::EndMenu();
    }
    if (title("Tools")) {
        if (ImGui::MenuItem("Settings...")) a.showSettings = true;
        if (ImGui::MenuItem("Re-export level", nullptr, false, !a.job.running)) requestLevel(a, a.pickLevel, true);
        ImGui::EndMenu();
    }
    if (title("Help")) {
        if (ImGui::MenuItem("Controls")) a.showHelp = true;
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
}

} // namespace ed
