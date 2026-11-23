// ui_props.cpp: the Props browser (View > Props browser); see ui_app.h for how the editor UI is split.
//
// The models the open level uses (the export's models table): each one is rendered once, alone, with the same fast3d
// replay as the view (Scene::renderOwner, between two frames), cached as a texture, and shown in a grid. A cell can be
// dragged onto the 3D view: it drops an object (or guard) with that model at the point under the mouse, placed like Ctrl+V.
// Only the level's own models are listed: the game's other models are not in the export.
#include "ui_app.h"

namespace ed {

struct ModelPayload { int guard, model, type, item; };

static const int kThumb = 96;          // rendered size
static const float kCell = 84.0f;      // shown size

void clearOwnerThumbs(App &a)
{
    for (auto &kv : a.ownerTex) if (kv.second) editorGlDeleteTexture(kv.second);
    a.ownerTex.clear();
    a.ownerQueue.clear();
}

void processOwnerQueue(App &a, int winW, int winH)
{
    int n = 0;
    while (!a.ownerQueue.empty() && n < 6) {
        const int owner = a.ownerQueue.front();
        a.ownerQueue.erase(a.ownerQueue.begin());
        if (a.ownerTex.count(owner)) continue;
        std::vector<uint8_t> px;
        uint32_t tex = 0;
        if (a.scene.renderOwner(owner, winW, winH, kThumb, px)) tex = editorGlUploadTexture(kThumb, kThumb, px.data());
        a.ownerTex[owner] = tex;
        n++;
    }
}

// A guard or object with that model at the point (or pad) under the ray, like Ctrl+V.
void placeModelAt(App &a, bool guard, int model, int type, int item, const Vec3 &org, const Vec3 &dir)
{
    std::string err;
    if (!ensureMission(a)) return;
    PadSpec ps;
    if (!padFromHit(a, pickScene(a.doc, org, dir), ps)) { a.say("add %s: drop it on a pad or the floor", guard ? "guard" : "object"); return; }
    ItemRef ref;
    const bool ok = guard ? a.doc.addGuard(ps, model, a.gAi, a.gWeapon, ref, err) : a.doc.addObject(ps, type > 0 ? type : 3, model, item, ref, err);
    if (ok) { selectNewItem(a, ref); a.say("%s added (model %d)", guard ? "guard" : "object", model); }
    else a.say("add %s: %s", guard ? "guard" : "object", err.c_str());
}

static bool matches(const ModelUse &u, const char *filter)
{
    if (!filter[0]) return true;
    char id[32], text[96];
    snprintf(id, sizeof id, "%d", u.model);
    snprintf(text, sizeof text, "%s %d type %d item %d", u.guard ? "guard" : "object", u.model, u.type, u.item);
    std::string f = filter, t = text;
    for (char &c : f) c = (char)tolower((unsigned char)c);
    return t.find(f) != std::string::npos;
}

// While a model is dragged: a transparent window over the 3D view takes the drop (the view is not an ImGui item).
static void viewDropTarget(App &a)
{
    const ImGuiPayload *pl = ImGui::GetDragDropPayload();
    if (!pl || !pl->IsDataType("GE_MODEL")) return;
    ImGui::SetNextWindowPos(ImVec2(a.vx, a.vy));
    ImGui::SetNextWindowSize(ImVec2(a.vw, a.vh));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##viewdrop", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings |
                                        ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove);
    ImGui::InvisibleButton("##drop", ImVec2(a.vw, a.vh));
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload *p = ImGui::AcceptDragDropPayload("GE_MODEL")) {
            const ModelPayload m = *(const ModelPayload *)p->Data;
            Vec3 org, dir;
            const ImVec2 mp = ImGui::GetIO().MousePos;
            a.cam.ray(mp.x - a.vx, mp.y - a.vy, a.vw, a.vh, org, dir);
            placeModelAt(a, m.guard != 0, m.model, m.type, m.item, org, dir);
        }
        ImGui::EndDragDropTarget();
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

void drawPropsWindow(App &a)
{
    viewDropTarget(a);
    if (!a.showPropsWin) return;
    ImGui::SetNextWindowPos(ImVec2(a.vx + 12, a.vy + a.vh - 330), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(470, 320), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Props", &a.showPropsWin)) { ImGui::End(); return; }
    if (!a.doc.hasLevel || !a.doc.level.hasRec) {
        ImGui::TextDisabled("Open a level with a drawing export to see its models.");
        ImGui::End();
        return;
    }
    const std::vector<ModelUse> models = a.doc.levelModels();
    ImGui::SetNextItemWidth(-60);
    ImGui::InputTextWithHint("##pf", "search: model id, guard, object, type", a.propFilter, sizeof a.propFilter);
    ImGui::SameLine();
    ImGui::TextDisabled("%zu", models.size());
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("Drag a model onto the view to place it; click to make it the default of the Guard / Object tool.");
    ImGui::PopTextWrapPos();
    ImGui::Separator();
    ImGui::BeginChild("##propgrid", ImVec2(0, 0), false);
    const float avail = ImGui::GetContentRegionAvail().x;
    const int perRow = std::max(1, (int)(avail / (kCell + ImGui::GetStyle().ItemSpacing.x)));
    int col = 0;
    for (int pass = 0; pass < 2; pass++) {   // objects, then guards
        bool head = false;
        for (const ModelUse &u : models) {
            if (u.guard != (pass == 1) || !matches(u, a.propFilter)) continue;
            if (!head) {
                if (col) { ImGui::NewLine(); col = 0; }
                ImGui::TextColored(ImVec4(0.7f, 0.8f, 1, 1), "%s", pass ? "Guards" : "Objects");
                head = true;
            }
            ImGui::PushID(pass * 100000 + u.model);
            ImGui::BeginGroup();
            auto it = a.ownerTex.find(u.owner);
            const ImVec2 sz(kCell, kCell);
            if (it != a.ownerTex.end() && it->second) ImGui::ImageButton("##m", (ImTextureID)(intptr_t)it->second, sz);
            else {
                ImGui::Button(it != a.ownerTex.end() ? "n/a" : "...", sz);
                if (it == a.ownerTex.end() && ImGui::IsItemVisible() &&
                    std::find(a.ownerQueue.begin(), a.ownerQueue.end(), u.owner) == a.ownerQueue.end()) a.ownerQueue.push_back(u.owner);
            }
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
                if (u.guard) a.gModel = u.model;
                else { a.oModel = u.model; a.oType = u.type; a.oItem = u.item; }
                a.say("%s tool: model %d%s", u.guard ? "Guard" : "Object", u.model, u.guard ? "" : (" (type " + std::to_string(u.type) + ")").c_str());
            }
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                ModelPayload m = { u.guard ? 1 : 0, u.model, u.type, u.item };
                ImGui::SetDragDropPayload("GE_MODEL", &m, sizeof m);
                ImGui::Text("%s %d", u.guard ? "guard" : "object", u.model);
                ImGui::EndDragDropSource();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s model %d%s\nused by %d setup prop(s)", u.guard ? "guard body" : "object", u.model,
                                                          u.guard ? "" : (" (type " + std::to_string(u.type) + ")").c_str(), u.count);
            ImGui::TextDisabled("%d", u.model);
            ImGui::EndGroup();
            ImGui::PopID();
            if (++col < perRow) ImGui::SameLine(); else col = 0;
        }
        if (col) { ImGui::NewLine(); col = 0; }
    }
    ImGui::EndChild();
    ImGui::End();
}

} // namespace ed
