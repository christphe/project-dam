// ui_props.cpp: the Props browser (View > Props browser); see ui_app.h for how the editor UI is split.
//
// The models the open level uses (the export's models table): each one is rendered once, alone, with the same fast3d
// replay as the view (Scene::renderOwner, between two frames), cached as a texture, and shown in a grid. A cell can be
// dragged onto the 3D view: it drops an object (or guard) with that model at the point under the mouse, placed like Ctrl+V.
// "Level" lists the models the open level uses (an object's thumbnail is its glTF model when the model export has it, else level.rec's);
// "All models" lists every model of the static model export (models.json), placeable like the others.
#include "ui_app.h"

#include <climits>

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
        const bool ok = owner < 0 ? a.scene.renderModel(-owner - 1, winW, winH, kThumb, px) : a.scene.renderOwner(owner, winW, winH, kThumb, px);
        if (ok) tex = editorGlUploadTexture(kThumb, kThumb, px.data());
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
    const ModelLib &lib = a.doc.level.models;
    if (ImGui::RadioButton("Level", a.propCat == 0)) a.propCat = 0;
    ImGui::SameLine();
    if (ImGui::RadioButton("All models", a.propCat == 1)) a.propCat = 1;
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-60);
    ImGui::InputTextWithHint("##pf", a.propCat ? "search: model number or name" : "search: model id, guard, object, type", a.propFilter, sizeof a.propFilter);
    ImGui::SameLine();
    ImGui::TextDisabled("%zu", a.propCat ? lib.entries().size() : models.size());
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("Drag a model onto the view to place it; click to make it the default of the Guard / Object tool.");
    ImGui::PopTextWrapPos();
    ImGui::Separator();
    ImGui::BeginChild("##propgrid", ImVec2(0, 0), false);
    const float avail = ImGui::GetContentRegionAvail().x;
    const int perRow = std::max(1, (int)(avail / (kCell + ImGui::GetStyle().ItemSpacing.x)));
    int col = 0;
    // one cell: its thumbnail key (-1 none), the labels, and what a click or a drag does
    auto cell = [&](int pushId, int thumbKey, bool guard, int model, int type, int item, const char *label, const std::string &tip) {
        ImGui::PushID(pushId);
        ImGui::BeginGroup();
        auto it = a.ownerTex.find(thumbKey);
        const ImVec2 sz(kCell, kCell);
        if (it != a.ownerTex.end() && it->second) ImGui::ImageButton("##m", (ImTextureID)(intptr_t)it->second, sz);
        else {
            ImGui::Button(it != a.ownerTex.end() || thumbKey == INT_MIN ? "n/a" : "...", sz);
            if (thumbKey != INT_MIN && it == a.ownerTex.end() && ImGui::IsItemVisible() &&
                std::find(a.ownerQueue.begin(), a.ownerQueue.end(), thumbKey) == a.ownerQueue.end()) a.ownerQueue.push_back(thumbKey);
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            if (guard) a.gModel = model;
            else { a.oModel = model; a.oType = type > 0 ? type : 3; a.oItem = item; }
            a.say("%s tool: model %d%s", guard ? "Guard" : "Object", model, guard ? "" : (" (type " + std::to_string(a.oType) + ")").c_str());
        }
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
            ModelPayload m = { guard ? 1 : 0, model, type, item };
            ImGui::SetDragDropPayload("GE_MODEL", &m, sizeof m);
            ImGui::Text("%s %d", guard ? "guard" : "object", model);
            ImGui::EndDragDropSource();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip.c_str());
        ImGui::TextDisabled("%s", label);
        ImGui::EndGroup();
        ImGui::PopID();
        if (++col < perRow) ImGui::SameLine(); else col = 0;
    };
    if (a.propCat == 1) {
        if (!lib.isOpen()) ImGui::TextDisabled("No model export found (models/models.json beside the glTF decor): re-export the cached levels in Tools > Settings.");
        std::string f = a.propFilter;
        for (char &c : f) c = (char)tolower((unsigned char)c);
        for (const ModelEntry &e : lib.entries()) {
            if (!f.empty()) {
                std::string t = std::to_string(e.model) + " " + e.name + " " + e.file;
                for (char &c : t) c = (char)tolower((unsigned char)c);
                if (t.find(f) == std::string::npos) continue;
            }
            char label[64];
            snprintf(label, sizeof label, "%d %.9s", e.model, e.name.c_str());
            cell(200000 + e.model, e.exported ? -e.model - 1 : INT_MIN, false, e.model, 0, -1, label,
                 "object model " + std::to_string(e.model) + " (" + e.file + ")\n" + std::to_string(e.triangles) + " triangles" + (e.exported ? "" : ", not exported"));
        }
        if (col) { ImGui::NewLine(); col = 0; }
    } else
    for (int pass = 0; pass < 2; pass++) {   // objects, then guards
        bool head = false;
        for (const ModelUse &u : models) {
            if (u.guard != (pass == 1) || !matches(u, a.propFilter)) continue;
            if (!head) {
                if (col) { ImGui::NewLine(); col = 0; }
                ImGui::TextColored(ImVec4(0.7f, 0.8f, 1, 1), "%s", pass ? "Guards" : "Objects");
                head = true;
            }
            const bool gl = !u.guard && a.doc.level.useModels && lib.get(u.model);
            char label[16];
            snprintf(label, sizeof label, "%d", u.model);
            cell(pass * 100000 + u.model, gl ? -u.model - 1 : u.owner, u.guard, u.model, u.type, u.item, label,
                 std::string(u.guard ? "guard body" : "object") + " model " + std::to_string(u.model) +
                 (u.guard ? "" : " (type " + std::to_string(u.type) + ")") + "\nused by " + std::to_string(u.count) + " setup prop(s)");
        }
        if (col) { ImGui::NewLine(); col = 0; }
    }
    ImGui::EndChild();
    ImGui::End();
}

} // namespace ed
