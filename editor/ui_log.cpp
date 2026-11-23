// The bottom strip: one status line (the last message, coloured by severity) that a click expands into the log
// panel (validation result + every message). View > Show log does the same.
#include "ui_app.h"

namespace ed {

static ImVec4 sevColor(int sev)
{
    return sev >= 2 ? ImVec4(1.0f, 0.45f, 0.4f, 1) : sev == 1 ? ImVec4(1.0f, 0.82f, 0.48f, 1) : ImVec4(0.8f, 0.85f, 0.9f, 1);
}

void drawStatusBar(App &a, float x, float y, float w, float h)
{
    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::Begin("Log", nullptr, kPanel | ImGuiWindowFlags_NoTitleBar | (a.showLog ? 0 : ImGuiWindowFlags_NoScrollbar));
    if (!a.showLog) {
        const std::string last = a.log.empty() ? std::string("ready") : a.log.back().text;
        const int sev = a.log.empty() ? 0 : a.log.back().sev;
        ImGui::PushStyleColor(ImGuiCol_Text, sevColor(sev));
        if (ImGui::Selectable(last.c_str(), false, 0, ImVec2(a.mcpPort ? -90 : 0, 0))) a.showLog = true;
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click to open the log");
        if (a.mcpPort) { ImGui::SameLine(); ImGui::TextDisabled("MCP :%d", a.mcpPort); }
        ImGui::End();
        return;
    }
    if (ImGui::SmallButton("Hide log")) a.showLog = false;
    ImGui::SameLine();
    auto issues = a.doc.validate();
    if (a.doc.hasMission && issues.empty()) ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1), "mission valid");
    else if (!issues.empty()) ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "%zu issue(s)", issues.size());
    if (a.mcpPort) { ImGui::SameLine(); ImGui::TextDisabled("MCP :%d", a.mcpPort); }
    for (auto &s : issues) ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "! %s", s.c_str());
    ImGui::Separator();
    for (auto it = a.log.rbegin(); it != a.log.rend(); ++it) ImGui::TextColored(sevColor(it->sev), "%s", it->text.c_str());
    ImGui::End();
}

} // namespace ed
