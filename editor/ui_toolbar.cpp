// The short toolbar under the menu bar: tools, Play (+ Co-op, spawn label) and snapping. The view toggles are in the
// View menu; Save / Undo / Validate / Capture are in the menus.
#include "ui_app.h"

namespace ed {

void drawToolbar(App &a, float y, float h)
{
    ImGui::SetNextWindowPos(ImVec2(0, y));
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetIO().DisplaySize.x, h));
    ImGui::Begin("toolbar", nullptr, kPanel | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar);
    auto toolBtn = [&](const char *label, App::Tool t) {
        bool on = a.tool == t;
        if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::Button(label)) a.tool = t;
        if (on) ImGui::PopStyleColor();
        ImGui::SameLine();
    };
    auto futureBtn = [&](const char *label, const char *tip) {   // a tool that comes with a later step
        ImGui::BeginDisabled();
        ImGui::Button(label);
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", tip);
        ImGui::SameLine();
    };
    toolBtn("Select", App::T_SELECT);
    toolBtn("Move", App::T_MOVE);
    toolBtn("Guard", App::T_GUARD);
    toolBtn("Object", App::T_OBJECT);
    futureBtn("Zone", "#105");
    futureBtn("Geometry", "#105");
    ImGui::TextUnformatted("|"); ImGui::SameLine();
    if (ImGui::Button("Play")) doPlay(a);
    ImGui::SameLine();
    if (ImGui::Checkbox("Co-op", &a.playCoop)) savePrefs(a);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Play with two players (two pads), else solo");
    ImGui::SameLine();
    {   // what Play will use: the level's own start unless a pad was chosen with "Set as spawn"
        char label[96];
        char *end = nullptr;
        const long n = a.spawn.empty() ? -1 : strtol(a.spawn.c_str(), &end, 0);
        if (a.spawn.empty()) snprintf(label, sizeof label, "spawn: level start");
        else if (end && !*end) snprintf(label, sizeof label, "spawn: pad 0x%lx", n);
        else snprintf(label, sizeof label, "spawn: %s", a.spawn.c_str());
        ImGui::TextUnformatted(label);
        if (!a.spawn.empty()) {
            ImGui::SameLine();
            if (ImGui::SmallButton("x##spawn")) a.spawn.clear();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Back to the level's own start");
        }
    }
    ImGui::SameLine();
    ImGui::TextUnformatted("|"); ImGui::SameLine();
    ImGui::TextUnformatted("snap"); ImGui::SameLine();
    ImGui::Checkbox("Grid", &a.snapGrid); ImGui::SameLine();
    ImGui::SetNextItemWidth(60);
    ImGui::InputFloat("##gs", &a.gridStep, 0, 0, "%.0f"); ImGui::SameLine();
    if (a.gridStep < 1) a.gridStep = 1;
    ImGui::Checkbox("Floor", &a.snapFloor); ImGui::SameLine();
    ImGui::Checkbox("Pads##snap", &a.snapPads);
    ImGui::End();
}

} // namespace ed
