// Tools > Settings...: the game, the export cache, rendering and MCP options, recent missions. The remembered ones are
// written to project-dam-editor-prefs.txt next to the editor (key=value lines, with the co-op toggle).
#include "ui_app.h"
#include "ge_render.h"

namespace ed {

void drawSettingsWindow(App &a)
{
    if (!a.showSettings) return;
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.3f), ImGuiCond_Appearing, ImVec2(0.5f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(560, 0), ImGuiCond_Appearing);
    if (!ImGui::Begin("Settings", &a.showSettings, ImGuiWindowFlags_AlwaysAutoResize)) { ImGui::End(); return; }

    ImGui::SeparatorText("Game");
    ImGui::TextUnformatted("game executable (Play, level exports)");
    ImGui::SetNextItemWidth(fileDialogAvailable() ? -90 : -1);
    if (ImGui::InputText("##game", a.gameExe, sizeof a.gameExe)) a.doc.gameExe = a.gameExe;
    if (fileDialogAvailable()) {
        ImGui::SameLine();
        if (ImGui::Button("Browse...")) {
            std::string path;
            if (fileDialogPickProgram(a.gameExe, path)) { snprintf(a.gameExe, sizeof a.gameExe, "%s", path.c_str()); a.doc.gameExe = a.gameExe; }
        }
    }

    ImGui::SeparatorText("Export cache");
    const std::string cache = exportCacheDir(a);
    ImGui::TextUnformatted("level exports are kept in");
    char cb[512];
    snprintf(cb, sizeof cb, "%s", cache.empty() ? "(no game executable)" : cache.c_str());
    ImGui::SetNextItemWidth(fileDialogAvailable() ? -110 : -1);
    ImGui::InputText("##cache", cb, sizeof cb, ImGuiInputTextFlags_ReadOnly);
    if (fileDialogAvailable()) {
        ImGui::SameLine();
        ImGui::BeginDisabled(cache.empty());
        if (ImGui::Button("Open folder")) { if (!openFolder(cache)) a.say("settings: cannot open %s", cache.c_str()); }
        ImGui::EndDisabled();
    }
    {
        const bool busy = a.job.running || !a.reexportQueue.empty();
        ImGui::BeginDisabled(busy || cache.empty());
        if (ImGui::Button("Re-export all cached levels")) startReexportAll(a);
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (a.reexportTotal > 0) ImGui::TextDisabled("%d of %d done%s%s", a.reexportTotal - (int)a.reexportQueue.size() - (a.job.running ? 1 : 0), a.reexportTotal,
                                                     a.job.running ? ", exporting " : "", a.job.running ? a.job.level.c_str() : "");
        else ImGui::TextDisabled("runs the game once per level, in the background");
    }

    ImGui::SeparatorText("Rendering");
    {
        static const char *names[] = { "OpenGL", "Vulkan" };
        ImGui::SetNextItemWidth(160);
        if (ImGui::Combo("renderer", &a.prefRenderer, names, 2)) savePrefs(a);
        ImGui::TextDisabled("Running on %s. A change applies at the next start (--renderer gl|vk wins over it).",
                            ge_render_backend() == GE_RENDER_VULKAN ? "Vulkan" : "OpenGL");
    }
    ImGui::BeginDisabled(ge_render_backend() != GE_RENDER_GL);
    if (ImGui::Checkbox("draw through an offscreen frame (OpenGL)", &a.prefOffscreen)) savePrefs(a);
    ImGui::EndDisabled();
    ImGui::TextDisabled("Avoids half-drawn frames on some drivers. Applies at the next start.");

    ImGui::SeparatorText("MCP server");
    bool changed = ImGui::Checkbox("enable the MCP server", &a.prefMcp);
    ImGui::SetNextItemWidth(100);
    changed |= ImGui::InputInt("port", &a.prefMcpPort, 0, 0);
    if (a.prefMcpPort < 1) a.prefMcpPort = 1;
    if (a.prefMcpPort > 65535) a.prefMcpPort = 65535;
    if (changed) savePrefs(a);
    if (a.mcpPort) ImGui::TextDisabled("running on 127.0.0.1:%d. Changes apply at the next start.", a.mcpPort);
    else ImGui::TextDisabled("not running. Changes apply at the next start.");

    ImGui::SeparatorText("Recent missions");
    {
        char l[48]; snprintf(l, sizeof l, "Clear the recent list (%d)", (int)a.recent.size());
        ImGui::BeginDisabled(a.recent.empty());
        if (ImGui::Button(l)) recentClear(a);
        ImGui::EndDisabled();
    }
    ImGui::End();
}

} // namespace ed
