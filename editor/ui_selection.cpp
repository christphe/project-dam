// ui_selection.cpp: see ui_app.h for how the editor UI is split.
#include "ui_app.h"


namespace ed {

// Numeric position and yaw of the selection. Enter applies (one undo step), snapping is not applied
// to typed numbers.
void transformFields(App &a)
{
    TransformInfo t = a.doc.getTransform(a.doc.sel);
    if (!t.valid) return;
    ImGui::Separator();
    if (!t.movable) {
        ImGui::TextDisabled("Original pads are level data: not movable.");
        return;
    }
    float p[3] = { t.pos[0], t.pos[1], t.pos[2] };
    float yaw = t.yawDeg;
    std::string err;
    if (ImGui::InputFloat3("pos", p, "%.1f", ImGuiInputTextFlags_EnterReturnsTrue)) {
        Vec3 np = { p[0], p[1], p[2] };
        if (!a.doc.setTransform(a.doc.sel, &np, nullptr, true, err)) a.say("move: %s", err.c_str());
    }
    if (ImGui::InputFloat("yaw", &yaw, 0, 0, "%.1f deg", ImGuiInputTextFlags_EnterReturnsTrue)) {
        if (!a.doc.setTransform(a.doc.sel, nullptr, &yaw, true, err)) a.say("turn: %s", err.c_str());
    }
    ImGui::TextWrapped("Drag the gizmo in the view (arrows = axis, square = plane, ring = yaw), or G to grab; X/Y/Z lock an axis while moving.");
}

// All the record fields of the selected guard / object (mission_fields.h). Editable for an item the mission
// added (grey = still the template's value; editing it makes it the item's own); read-only for original props:
// copy one (Ctrl+C, Ctrl+V / Ctrl+D) to change it.
void fieldsPanel(App &a, const Item &it)
{
    const MissionField *f;
    const int type = it.guard ? MISSION_REC_GUARD : it.type;
    const int n = missionRecFields(type, &f);
    if (n <= 0) return;
    ImGui::Separator();
    ImGui::TextWrapped("%s", it.added ? "Fields (grey: the template's value, edit to set)" : "Fields (original, read-only: copy to edit)");
    if (it.guard) ImGui::TextWrapped("body = model (65535 = random); head -1 = random");
    const char *group = missionRecGroup(type);
    bool inGroup = false;
    for (int i = 0; i < n; i++) {
        if (f[i].grouped && !inGroup && group) { ImGui::TextColored(ImVec4(0.7f, 0.8f, 1, 1), "%s", group); inGroup = true; }
        const bool have = (it.f.set >> i) & 1;
        double v = have ? it.f.v[i] : 0;
        const bool own = !it.added || ((it.given >> i) & 1);
        ImGui::PushID(i);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(f[i].name);
        ImGui::SameLine(150);
        ImGui::SetNextItemWidth(120);
        const char *fmt = (f[i].kind == MF_FLOAT || f[i].kind == MF_FIXED) ? "%.10g" : "%.0f";
        if (!it.added) {
            ImGui::TextDisabled((f[i].kind == MF_FLOAT || f[i].kind == MF_FIXED) ? "%.10g" : "%.0f", v);
        } else {
            if (!own) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1));
            ImGui::InputScalar("##v", ImGuiDataType_Double, &v, nullptr, nullptr, fmt);
            if (!own) ImGui::PopStyleColor();
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                std::string err;
                if (!a.doc.setItemField(a.doc.sel.item, i, v, err)) a.say("field: %s", err.c_str());
            }
        }
        ImGui::PopID();
    }
}

// The right panel's Selection tab: what is selected, with its properties (the New items defaults come first).
void drawSelectionTab(App &a)
{
    Selection &sel = a.doc.sel;
    std::string err;
    if (ImGui::CollapsingHeader("New items", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextUnformatted("Guard (tool: Add guard)");
        ImGui::InputInt("model##g", &a.gModel); ImGui::InputInt("ai list##g", &a.gAi); ImGui::InputInt("weapon##g", &a.gWeapon);
        ImGui::TextUnformatted("Object (tool: Add object)");
        ImGui::InputInt("type##o", &a.oType); ImGui::InputInt("model##o", &a.oModel); ImGui::InputInt("item##o", &a.oItem);
        ImGui::TextWrapped("-1 = the level default. Click a pad or the floor in the view.");
    }
    ImGui::Separator();
    switch (sel.kind) {
    case Selection::NONE: ImGui::TextDisabled("Nothing selected."); break;
    case Selection::ROOM: {
        ImGui::Text("Room %d", sel.num);
        for (auto &r : a.doc.level.rooms)
            if (r.index == sel.num) {
                ImGui::Text("min %.0f %.0f %.0f", r.mn[0], r.mn[1], r.mn[2]);
                ImGui::Text("max %.0f %.0f %.0f", r.mx[0], r.mx[1], r.mx[2]);
            }
        break;
    }
    case Selection::PAD: {
        const LPad *p = a.doc.level.pad(sel.num);
        if (!p) break;
        ImGui::Text("Pad 0x%x (original)", p->num);
        ImGui::Text("pos  %.0f %.0f %.0f", p->pos[0], p->pos[1], p->pos[2]);
        ImGui::Text("look %.2f %.2f %.2f", p->look[0], p->look[1], p->look[2]);
        ImGui::Text("room %d", p->room);
        if (ImGui::Button("Set as spawn")) a.spawn = std::to_string(p->num);
        if (a.doc.hasMission) {
            PadSpec ps; ps.num = p->num;
            ItemRef ref;
            if (ImGui::Button("Add guard here")) a.doc.addGuard(ps, a.gModel, a.gAi, a.gWeapon, ref, err), sel.kind = Selection::ITEM, sel.item = ref;
            if (ImGui::Button("Add object here")) a.doc.addObject(ps, a.oType, a.oModel, a.oItem, ref, err), sel.kind = Selection::ITEM, sel.item = ref;
        }
        break;
    }
    case Selection::MPAD: {
        for (int i = 0; i < a.doc.mission.npads; i++) {
            const MissionPad &p = a.doc.mission.pads[i];
            if (sel.id != p.id) continue;
            ImGui::Text("Mission pad %s", p.id);
            ImGui::Text("pos  %.0f %.0f %.0f", p.pos[0], p.pos[1], p.pos[2]);
            ImGui::Text("room %d", p.room);
            if (ImGui::Button("Set as spawn")) a.spawn = p.id;
        }
        transformFields(a);
        break;
    }
    case Selection::ITEM: {
        const Item *it = selectedItem(a);
        if (!it) { ImGui::TextDisabled("(item no longer exists)"); break; }
        if (it->added) ImGui::Text("%s +%d  (added by the mission)", it->guard ? "Guard" : "Object", it->idx);
        else ImGui::Text("%s prop %d  (original)", it->guard ? "Guard" : "Object", it->idx);
        if (it->removed) ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1), "removed by the mission");
        if (it->moved) ImGui::TextColored(ImVec4(1, 0.85f, 0.2f, 1), "moved (was pad 0x%x)", it->origPad);
        ImGui::Text("pad   %s", padLabel(it->pad).c_str());
        if (it->hasPos) ImGui::Text("pos   %.0f %.0f %.0f", it->pos[0], it->pos[1], it->pos[2]);
        ImGui::Text("type  %d", it->type);
        ImGui::Text("model %d", it->model);
        if (it->guard) {
            ImGui::Text("ai    0x%x", it->ai);
            if (it->weapons.empty()) ImGui::TextDisabled("weapons: none");
            for (size_t k = 0; k < it->weapons.size(); k++) ImGui::Text("weapon %zu: item %d  model %d", k + 1, it->weapons[k].item, it->weapons[k].model);
        }
        else ImGui::Text("item  %d", it->item);
        if (it->owner >= 0) ImGui::Text("recorded model: owner %d", it->owner); else ImGui::TextDisabled("no recorded model (marker)");
        ImGui::Separator();
        if (it->added) {
            if (!(a.editFor == sel.item)) {
                a.editFor = sel.item;
                a.editA = it->guard ? it->ai : it->type; a.editB = it->guard ? (it->weapons.empty() ? -1 : it->weapons[0].item) : it->item; a.editC = it->model;
            }
            ImGui::InputInt("model", &a.editC);
            ImGui::InputInt(it->guard ? "ai list" : "type", &a.editA);
            ImGui::InputInt(it->guard ? "weapon 1" : "item", &a.editB);
            if (ImGui::Button("Apply")) {
                if (!a.doc.setAddedFields(sel.item, a.editC, a.editA, a.editB, err)) a.say("edit: %s", err.c_str());
            }
            ImGui::SameLine();
        }
        ImGui::SetNextItemWidth(110);
        ImGui::InputTextWithHint("##pt", "pad 0x.. or id", a.padText, sizeof a.padText);
        ImGui::SameLine();
        if (ImGui::Button("Move to pad")) {
            PadSpec ps;
            char *end = nullptr; long v = strtol(a.padText, &end, 0);
            if (end && !*end && end != a.padText) { ps.isNew = false; ps.num = (int)v; } else { ps.isNew = true; ps.id = a.padText; }
            if (!a.doc.moveItem(sel.item, ps, err)) a.say("move: %s", err.c_str());
        }
        if (ImGui::Button("Remove")) { if (!a.doc.removeItem(sel.item, err)) a.say("remove: %s", err.c_str()); }
        ImGui::TextWrapped("Tool Move: click a pad or the floor to move it there.");
        transformFields(a);
        fieldsPanel(a, *it);
        break;
    }
    }
}

} // namespace ed
