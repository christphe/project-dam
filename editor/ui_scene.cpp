// The left "Scene" panel: a filter box and collapsible groups of everything in the level and the mission. Clicking a
// row selects it (double-click frames it). Carried weapons and pads under an item are not listed, as in the view.
#include "ui_app.h"
#include "ui_scripts.h"

#include <cctype>

namespace ed {

static bool matches(const App &a, const char *text)
{
    if (!a.sceneFilter[0]) return true;
    std::string h = text, n = a.sceneFilter;
    for (char &c : h) c = (char)tolower((unsigned char)c);
    for (char &c : n) c = (char)tolower((unsigned char)c);
    return h.find(n) != std::string::npos;
}

static std::string itemLabel(const Item &it)
{
    char l[96];
    const char *kind = it.guard ? "guard" : "object";
    const char *flag = it.added ? " +added" : it.removed ? " -removed" : it.moved ? " ~moved" : "";
    if (it.added) snprintf(l, sizeof l, "%s +%d  @%s%s", kind, it.idx, padLabel(it.pad).c_str(), flag);
    else snprintf(l, sizeof l, "%s %d  @%s%s", kind, it.idx, padLabel(it.pad).c_str(), flag);
    return l;
}

// A clipped, scrolling list of rows (the Selectable of each row does the selecting).
template <class T, class Row> static void list(const char *id, float height, const std::vector<T> &rows, Row row)
{
    ImGui::BeginChild(id, ImVec2(0, height), ImGuiChildFlags_Borders);
    ImGuiListClipper clip;
    clip.Begin((int)rows.size());
    while (clip.Step())
        for (int i = clip.DisplayStart; i < clip.DisplayEnd; i++) { ImGui::PushID(i); row(rows[(size_t)i]); ImGui::PopID(); }
    ImGui::EndChild();
}

static void itemGroup(App &a, bool guards)
{
    int total = 0;
    std::vector<const Item *> shown;
    for (const Item &it : a.items) {
        if (it.guard != guards || it.assigned) continue;
        total++;
        if (matches(a, itemLabel(it).c_str())) shown.push_back(&it);
    }
    char head[48];
    snprintf(head, sizeof head, "%s (%d)###%s", guards ? "Guards" : "Objects", total, guards ? "g" : "o");
    if (!ImGui::CollapsingHeader(head, guards ? ImGuiTreeNodeFlags_DefaultOpen : 0)) return;
    list(guards ? "guards" : "objects", 220, shown, [&](const Item *it) {
        if (ImGui::Selectable(itemLabel(*it).c_str(), itemIsSel(a, *it))) {
            selectItem(a, *it);
            if (ImGui::IsMouseDoubleClicked(0) && it->hasPos) focusOn(a, it->pos);
        }
    });
}

void drawScenePanel(App &a, float x, float y, float w, float h)
{
    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::Begin("Scene", nullptr, kPanel);
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##scenefilter", "filter", a.sceneFilter, sizeof a.sceneFilter);
    const Doc &d = a.doc;
    if (d.hasLevel || d.hasMission) {
        itemGroup(a, true);
        itemGroup(a, false);
    }

    drawSceneZonesAndScripts(a, a.sceneFilter);   // ui_scripts.cpp

    if (d.hasMission && d.mission.npads > 0) {
        char head[40];
        snprintf(head, sizeof head, "Mission pads (%d)###mpads", d.mission.npads);
        if (ImGui::CollapsingHeader(head, ImGuiTreeNodeFlags_DefaultOpen)) {
            for (int i = 0; i < d.mission.npads; i++) {
                const MissionPad &p = d.mission.pads[i];
                if (!matches(a, p.id)) continue;
                const bool sel = a.doc.sel.kind == Selection::MPAD && a.doc.sel.id == p.id;
                if (ImGui::Selectable(p.id, sel)) {
                    a.doc.sel = Selection(); a.doc.sel.kind = Selection::MPAD; a.doc.sel.id = p.id;
                    if (ImGui::IsMouseDoubleClicked(0)) focusOn(a, { p.pos[0], p.pos[1], p.pos[2] });
                }
            }
        }
    }

    if (d.hasLevel) {
        // free pads: the level's pads that no guard or object stands on
        const OccupiedPads occ = occupiedPads(a.items);
        std::vector<const LPad *> shown;
        int total = 0;
        for (const LPad &p : d.level.pads) {
            if (occ.level.count(p.num)) continue;
            total++;
            char l[48]; snprintf(l, sizeof l, "0x%x  room %d", p.num, p.room);
            if (matches(a, l)) shown.push_back(&p);
        }
        char head[40];
        snprintf(head, sizeof head, "Free pads (%d)###fpads", total);
        if (ImGui::CollapsingHeader(head))
            list("padlist", 200, shown, [&](const LPad *p) {
                char l[48]; snprintf(l, sizeof l, "0x%x  room %d", p->num, p->room);
                const bool sel = a.doc.sel.kind == Selection::PAD && a.doc.sel.num == p->num;
                if (ImGui::Selectable(l, sel)) {
                    a.doc.sel = Selection(); a.doc.sel.kind = Selection::PAD; a.doc.sel.num = p->num;
                    if (ImGui::IsMouseDoubleClicked(0)) focusOn(a, p->pos);
                }
            });

        snprintf(head, sizeof head, "Rooms (%d)###rooms", (int)d.level.rooms.size());
        if (ImGui::CollapsingHeader(head)) {
            for (const LRoom &r : d.level.rooms) {
                char l[48]; snprintf(l, sizeof l, "room %d", r.index);
                if (!matches(a, l)) continue;
                const bool sel = a.doc.sel.kind == Selection::ROOM && a.doc.sel.num == r.index;
                if (ImGui::Selectable(l, sel)) {
                    a.doc.sel = Selection(); a.doc.sel.kind = Selection::ROOM; a.doc.sel.num = r.index;
                    if (ImGui::IsMouseDoubleClicked(0))
                        focusOn(a, { (r.mn[0] + r.mx[0]) / 2, (r.mn[1] + r.mx[1]) / 2, (r.mn[2] + r.mx[2]) / 2 }, 900);
                }
            }
        }
    }
    ImGui::End();
}

} // namespace ed
