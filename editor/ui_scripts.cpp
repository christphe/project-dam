// ui_scripts.cpp: see ui_scripts.h. The scripts of a mission: the entries, their typed property form (from the script's
// `export type Props`), the Luau code editor with the type checker's findings, and zones.
#include "ui_scripts.h"

#include <cmath>
#include <functional>

#include "TextEditor.h"
#include "item_names.h"
#include "script_props.h"

namespace ed {

enum PickKind { PK_NONE, PK_TARGET_PROP, PK_TARGET_PAD, PK_FIELD_PROP, PK_FIELD_GUARD, PK_FIELD_PAD };

struct ScriptsUi {
    // code editor
    TextEditor ed;
    TextEditor::Language lang;
    bool langInit = false;
    bool edOpen = false;
    std::string edUse;
    std::string edPushed;           // the text the widget and the document agree on
    bool edChanged = false;         // the widget's (delayed) change callback fired
    bool edReload = true;           // load the document's text into the widget
    int gotoLine = 0;               // 1-based line to show after the next load
    size_t diagKey = 0;
    std::vector<ScriptDiag> diags;  // of the open script
    // picking in the view
    PickKind pick = PK_NONE;
    int pickScript = -1;
    std::string pickField;
    // the Props schemas of the scripts the panels show (parsed once per text)
    std::map<std::string, std::pair<size_t, PropsSchema>> schemas;
    char newName[32] = "my_script";
};

// The widget keeps \n line ends: a text that differs only by \r is the same text.
static bool sameText(const std::string &a, const std::string &b)
{
    size_t i = 0, j = 0;
    while (i < a.size() || j < b.size()) {
        if (i < a.size() && a[i] == '\r') { i++; continue; }
        if (j < b.size() && b[j] == '\r') { j++; continue; }
        if (i >= a.size() || j >= b.size() || a[i] != b[j]) return false;
        i++; j++;
    }
    return true;
}

ScriptsUi &scriptsUi(App &a)
{
    if (!a.scr) a.scr = std::make_shared<ScriptsUi>();
    return *a.scr;
}

// ---- helpers ----

static const PropsSchema &schemaOf(App &a, const std::string &use)
{
    ScriptsUi &S = scriptsUi(a);
    const Doc::ScriptSrc src = a.doc.scriptSource(use);
    const size_t h = std::hash<std::string>()(src.text) ^ (size_t)src.origin;
    auto &e = S.schemas[use];
    if (e.first != h || (!e.second.found && e.second.fields.empty() && e.second.error.empty() && !src.text.empty())) { e.first = h; e.second = parsePropsSchema(src.text); }
    return e.second;
}

static const MissionProp *findProp(const MissionScript &s, const std::string &n)
{
    for (int k = 0; k < s.nprops; k++) if (n == s.props[k].name) return &s.props[k];
    return nullptr;
}

static const char *kindOfUse(const Doc &d, const std::string &use)
{
    for (int i = 0; i < d.mission.nscripts; i++) if (use == d.mission.scripts[i].use) return Doc::targetKindName(d.mission.scripts[i].kind);
    return "mission";
}

static std::string propDesc(const Doc &d, int n)
{
    if (!d.hasLevel) return "(no level loaded)";
    ItemRef ir; int ty = 0;
    if (n >= (int)d.level.props.size()) {
        char m[160]; snprintf(m, sizeof m, "not a prop of the level (it has %d): pick an added prop from the list, it is referenced by uuid", (int)d.level.props.size());
        return m;
    }
    if (!d.propByNumber(n, ir, ty)) return "no such prop";
    char b[160];
    const LProp *p = d.level.prop(n);
    if (p->type == 9) snprintf(b, sizeof b, "guard, body model %d", p->model);
    else if (p->type == 1) snprintf(b, sizeof b, "door, model %d", p->model);
    else if (p->type == 8 && p->item >= 0) snprintf(b, sizeof b, "pickup %s, model %d", itemName(p->item) ? itemName(p->item) : "item", p->model);
    else snprintf(b, sizeof b, "object type %d, model %d", p->type, p->model);
    return b;
}

static Selection scriptSel(int i) { Selection s; s.kind = Selection::SCRIPT; s.num = i; return s; }

void selectScript(App &a, int index)
{
    a.doc.sel = scriptSel(index);
    a.forceTab = 0;
}

bool scriptPicking(const App &a) { return a.scr && a.scr->pick != PK_NONE; }
void cancelScriptPick(App &a) { if (a.scr) a.scr->pick = PK_NONE; }

static void startPick(App &a, PickKind k, int si, const std::string &field)
{
    ScriptsUi &S = scriptsUi(a);
    S.pick = k; S.pickScript = si; S.pickField = field;
}

static void putNum(App &a, int si, const std::string &name, double v)
{
    MissionProp p; memset(&p, 0, sizeof p);
    p.kind = MISSION_VAL_NUMBER; p.num = v;
    std::string err;
    if (!a.doc.setScriptProp(si, name, &p, err)) a.say("edit: %s", err.c_str());
}
static void putStr(App &a, int si, const std::string &name, const std::string &v)
{
    MissionProp p; memset(&p, 0, sizeof p);
    p.kind = MISSION_VAL_STRING; snprintf(p.str, sizeof p.str, "%s", v.c_str());
    std::string err;
    if (!a.doc.setScriptProp(si, name, &p, err)) a.say("edit: %s", err.c_str());
}
static void putBool(App &a, int si, const std::string &name, bool v)
{
    MissionProp p; memset(&p, 0, sizeof p);
    p.kind = MISSION_VAL_BOOL; p.num = v ? 1 : 0;
    std::string err;
    if (!a.doc.setScriptProp(si, name, &p, err)) a.say("edit: %s", err.c_str());
}
static void eraseProp(App &a, int si, const std::string &name)
{
    std::string err;
    if (!a.doc.setScriptProp(si, name, nullptr, err)) a.say("edit: %s", err.c_str());
}

// ---- picking in the view ----

bool scriptPickClick(App &a, const Hit &h)
{
    ScriptsUi &S = scriptsUi(a);
    if (S.pick == PK_NONE) return false;
    Doc &d = a.doc;
    if (S.pickScript < 0 || S.pickScript >= d.mission.nscripts) { S.pick = PK_NONE; return false; }
    if (h.kind == Hit::NONE) return true;
    std::string err;
    const int si = S.pickScript;
    const bool wantProp = S.pick == PK_TARGET_PROP || S.pick == PK_FIELD_PROP || S.pick == PK_FIELD_GUARD;
    if (wantProp) {
        if (h.kind != Hit::ITEM) { a.say("click a door, guard or object of the level"); return true; }
        if (S.pick == PK_FIELD_GUARD && !h.item.guard) { a.say("that is not a guard"); return true; }
        if (h.item.added) {   // an item the mission adds: by its uuid
            const char *uuid = h.item.guard ? d.mission.guards[h.item.idx].uuid : d.mission.objects[h.item.idx].uuid;
            if (S.pick == PK_TARGET_PROP) {
                MissionPadRef none; memset(&none, 0, sizeof none);
                if (!d.setScriptTarget(si, MISSION_TGT_PROP, -1, none, "", err, uuid)) a.say("edit: %s", err.c_str());
            } else putStr(a, si, S.pickField, uuid);
            a.say("picked %s", d.propLabel(h.item).c_str());
            S.pick = PK_NONE;
            return true;
        }
        const int num = h.item.idx;
        if (S.pick == PK_TARGET_PROP) {
            MissionPadRef none; memset(&none, 0, sizeof none);
            if (!d.setScriptTarget(si, MISSION_TGT_PROP, num, none, "", err)) a.say("edit: %s", err.c_str());
        } else putNum(a, si, S.pickField, num);
        a.say("picked prop %d (%s)", num, propDesc(d, num).c_str());
    } else {
        MissionPadRef r; memset(&r, 0, sizeof r); r.num = -1;
        if (h.kind == Hit::PAD) r.num = h.num;
        else if (h.kind == Hit::MPAD) { r.is_new = 1; snprintf(r.id, sizeof r.id, "%s", h.id.c_str()); }
        else if (h.kind == Hit::ITEM) {
            PadSpec ps;
            if (!padFromHit(a, h, ps) || ps.atPoint) { a.say("click a pad"); return true; }
            if (ps.isNew) { r.is_new = 1; snprintf(r.id, sizeof r.id, "%s", ps.id.c_str()); } else r.num = ps.num;
        } else { a.say("click a pad (blue diamond = level, green = the mission's)"); return true; }
        if (S.pick == PK_TARGET_PAD) {
            if (!d.setScriptTarget(si, MISSION_TGT_PAD, -1, r, "", err)) a.say("edit: %s", err.c_str());
        } else if (r.is_new) putStr(a, si, S.pickField, r.id);
        else putNum(a, si, S.pickField, r.num);
        a.say("picked pad %s", padLabel(r).c_str());
    }
    S.pick = PK_NONE;
    return true;
}

// ---- the Props form ----

// A combo of the props the mission adds that fit `filter` (0 any, 1 guard, 2 door, 3 object that is not a guard); true when one was
// chosen (its uuid is in `out`).
static bool addedCombo(App &a, const char *id, const char *preview, int filter, std::string &out)
{
    bool chosen = false;
    if (!ImGui::BeginCombo(id, preview)) return false;
    int n = 0;
    for (const Item &it : a.doc.items()) {
        if (!it.added) continue;
        const bool isDoor = !it.guard && it.type == MISSION_PROPDEF_DOOR;
        if ((filter == 1 && !it.guard) || (filter == 2 && !isDoor) || (filter == 3 && it.guard)) continue;
        ItemRef r; r.valid = true; r.added = true; r.guard = it.guard; r.idx = it.idx;
        ImGui::PushID(it.uuid.c_str());
        if (ImGui::Selectable(a.doc.propLabel(r).c_str(), false)) { out = it.uuid; chosen = true; }
        ImGui::PopID();
        n++;
    }
    if (!n) ImGui::TextDisabled("the mission adds none of this kind");
    ImGui::EndCombo();
    return chosen;
}

static void defaultFor(App &a, int si, const PropField &f)
{
    const std::string &t = f.type;
    const Doc &d = a.doc;
    if (t == "Objective") putStr(a, si, f.name, d.mission.nobjectives ? d.mission.objectives[0].id : "");
    else if (t == "Zone") putStr(a, si, f.name, d.mission.nzones ? d.mission.zones[0].id : "");
    else if (t == "string") putStr(a, si, f.name, "");
    else if (t == "boolean") putBool(a, si, f.name, false);
    else if (t == "Item" || t == "Door" || t == "Guard" || t == "Prop" || t == "Pad" || t == "number") putNum(a, si, f.name, 0);
    else putStr(a, si, f.name, "");
}

static void propRow(App &a, int si, const PropField &f)
{
    Doc &d = a.doc;
    const MissionScript &s0 = d.mission.scripts[si];
    const MissionProp *pv = findProp(s0, f.name);
    const std::string &t = f.type;
    ImGui::PushID(f.name.c_str());
    ImGui::AlignTextToFramePadding();
    if (f.optional) {
        bool on = pv != nullptr;
        if (ImGui::Checkbox("##set", &on)) {
            if (on) defaultFor(a, si, f); else eraseProp(a, si, f.name);
            ImGui::PopID();
            return;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Optional: tick to set it; unticked = the key is left out of mission.json");
        ImGui::SameLine();
    }
    const bool missing = !pv && !f.optional;
    if (missing) ImGui::TextColored(ImVec4(1, 0.4f, 0.35f, 1), "%s", f.name.c_str()); else ImGui::TextUnformatted(f.name.c_str());
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s%s%s", t.c_str(), f.optional ? " (optional)" : "", missing ? " - required, not set" : "");
    ImGui::SameLine(112);
    if (f.optional && !pv) { ImGui::TextDisabled("(not set)"); ImGui::PopID(); return; }

    double num = pv && pv->kind != MISSION_VAL_STRING ? pv->num : 0;
    std::string str = pv && pv->kind == MISSION_VAL_STRING ? pv->str : "";
    const float w = ImGui::GetContentRegionAvail().x;
    if (t == "Item") {
        const int id = (int)num;
        const char *nm = itemName(id);
        char prev[64]; snprintf(prev, sizeof prev, "%s", nm ? nm : "(number)");
        ImGui::SetNextItemWidth(std::max(90.0f, w - 70));
        if (ImGui::BeginCombo("##item", prev)) {
            for (int i = 0; i < kItemNameCount; i++) {
                char l[64]; snprintf(l, sizeof l, "%s (%d)", kItemNames[i].name, kItemNames[i].id);
                if (ImGui::Selectable(l, kItemNames[i].id == id)) putNum(a, si, f.name, kItemNames[i].id);
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        int v = id;
        ImGui::SetNextItemWidth(60);
        ImGui::InputInt("##n", &v, 0, 0);
        if (ImGui::IsItemDeactivatedAfterEdit()) putNum(a, si, f.name, v);
    } else if (t == "Objective") {
        ImGui::SetNextItemWidth(-1);
        std::string prev = str.empty() ? "(none)" : str;
        bool known = false;
        for (int i = 0; i < d.mission.nobjectives; i++) if (str == d.mission.objectives[i].id) known = true;
        if (!str.empty() && !known) prev += " (not an objective of this mission)";
        if (ImGui::BeginCombo("##obj", prev.c_str())) {
            for (int i = 0; i < d.mission.nobjectives; i++) {
                char l[200]; snprintf(l, sizeof l, "%s: %s", d.mission.objectives[i].id, d.mission.objectives[i].text);
                if (ImGui::Selectable(l, str == d.mission.objectives[i].id)) putStr(a, si, f.name, d.mission.objectives[i].id);
            }
            if (d.mission.nobjectives == 0) ImGui::TextDisabled("the mission has no objectives yet (Mission tab)");
            ImGui::EndCombo();
        }
    } else if (t == "Zone") {
        ImGui::SetNextItemWidth(-1);
        std::string prev = str.empty() ? "(none)" : str;
        if (!str.empty() && d.zoneIndex(str) < 0) prev += " (no such zone)";
        if (ImGui::BeginCombo("##zone", prev.c_str())) {
            for (int i = 0; i < d.mission.nzones; i++)
                if (ImGui::Selectable(d.mission.zones[i].id, str == d.mission.zones[i].id)) putStr(a, si, f.name, d.mission.zones[i].id);
            if (d.mission.nzones == 0) ImGui::TextDisabled("the mission has no zones: use the Zone tool");
            ImGui::EndCombo();
        }
    } else if (t == "Door" || t == "Prop" || t == "Guard") {
        const int filter = t == "Guard" ? 1 : t == "Door" ? 2 : 3;
        const bool byUuid = pv && pv->kind == MISSION_VAL_STRING;
        int v = (int)num;
        std::string chosen;
        if (byUuid) {   // a prop the mission adds
            ItemRef ir; int ty = 0;
            const bool known = d.propByUuid(str, ir, ty);
            std::string prev = known ? d.propLabel(ir) : "(no such added prop: " + str.substr(0, 8) + ")";
            ImGui::SetNextItemWidth(std::max(90.0f, w - 120));
            if (addedCombo(a, "##added", prev.c_str(), filter, chosen)) putStr(a, si, f.name, chosen);
            ImGui::SameLine();
            if (ImGui::SmallButton("#")) putNum(a, si, f.name, 0);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Use an original prop's number instead");
        } else {
            ImGui::SetNextItemWidth(70);
            ImGui::InputInt("##prop", &v, 0, 0);
            if (ImGui::IsItemDeactivatedAfterEdit()) putNum(a, si, f.name, v);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(70);
            if (addedCombo(a, "##addedpick", "added...", filter, chosen)) putStr(a, si, f.name, chosen);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("A prop the mission adds: referenced by its uuid");
            ImGui::SameLine();
        }
        if (byUuid) ImGui::SameLine();
        const bool picking = scriptsUi(a).pick != PK_NONE && scriptsUi(a).pickScript == si && scriptsUi(a).pickField == f.name;
        if (picking) { if (ImGui::Button("Cancel")) cancelScriptPick(a); }
        else if (ImGui::Button("Pick")) startPick(a, t == "Guard" ? PK_FIELD_GUARD : PK_FIELD_PROP, si, f.name);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click a %s in the 3D view", t == "Guard" ? "guard" : t == "Door" ? "door" : "prop");
        if (byUuid) ImGui::TextDisabled("    uuid %s", str.c_str());
        else ImGui::TextDisabled("    %s", propDesc(d, v).c_str());
    } else if (t == "Pad") {
        char buf[64];
        if (pv && pv->kind == MISSION_VAL_STRING) snprintf(buf, sizeof buf, "%s", str.c_str()); else snprintf(buf, sizeof buf, "%d", (int)num);
        ImGui::SetNextItemWidth(std::max(80.0f, w - 60));
        ImGui::InputText("##pad", buf, sizeof buf);
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            char *end = nullptr; long v = strtol(buf, &end, 0);
            if (end && !*end && end != buf) putNum(a, si, f.name, (double)v); else putStr(a, si, f.name, buf);
        }
        ImGui::SameLine();
        const bool picking = scriptsUi(a).pick != PK_NONE && scriptsUi(a).pickScript == si && scriptsUi(a).pickField == f.name;
        if (picking) { if (ImGui::Button("Cancel")) cancelScriptPick(a); }
        else if (ImGui::Button("Pick")) startPick(a, PK_FIELD_PAD, si, f.name);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click a pad in the 3D view (a number, or a mission pad id)");
    } else if (t == "boolean") {
        bool v = num != 0;
        if (ImGui::Checkbox("##b", &v)) putBool(a, si, f.name, v);
    } else if (t == "number") {
        double v = num;
        ImGui::SetNextItemWidth(-1);
        ImGui::InputDouble("##num", &v, 0, 0, "%.10g");
        if (ImGui::IsItemDeactivatedAfterEdit()) putNum(a, si, f.name, v);
    } else {   // string, or a type the form does not know: a text field (a number stays a number when it reads as one)
        char buf[MISSION_TEXT_LEN];
        if (pv && pv->kind != MISSION_VAL_STRING) snprintf(buf, sizeof buf, "%.10g", num); else snprintf(buf, sizeof buf, "%s", str.c_str());
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##txt", buf, sizeof buf);
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            if (t != "string") {
                char *end = nullptr; double v = strtod(buf, &end);
                if (end && !*end && end != buf) { putNum(a, si, f.name, v); ImGui::PopID(); return; }
            }
            putStr(a, si, f.name, buf);
        }
    }
    ImGui::PopID();
}

// ---- the Selection tab: one script entry ----

void drawScriptSection(App &a)
{
    Doc &d = a.doc;
    ScriptsUi &S = scriptsUi(a);
    const int si = d.sel.num;
    if (!d.hasMission || si < 0 || si >= d.mission.nscripts) { ImGui::TextDisabled("(script no longer exists)"); return; }
    std::string err;
    ImGui::Text("Script %d of %d", si + 1, d.mission.nscripts);
    ImGui::SameLine();
    if (ImGui::SmallButton("Up") && si > 0) { if (d.moveScript(si, si - 1, err)) d.sel = scriptSel(si - 1); else a.say("edit: %s", err.c_str()); return; }
    ImGui::SameLine();
    if (ImGui::SmallButton("Down") && si + 1 < d.mission.nscripts) { if (d.moveScript(si, si + 1, err)) d.sel = scriptSel(si + 1); else a.say("edit: %s", err.c_str()); return; }
    ImGui::SameLine();
    if (ImGui::SmallButton("Remove")) { if (d.removeScript(si, err)) d.sel = Selection(); else a.say("edit: %s", err.c_str()); return; }

    // use
    const std::string use = d.mission.scripts[si].use;
    const Doc::ScriptSrc src = d.scriptSource(use);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("script");
    ImGui::SameLine(112);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 70);
    if (ImGui::BeginCombo("##use", use.c_str())) {
        const std::vector<std::string> mine = d.missionScriptNames();
        for (const std::string &n : mine)
            if (ImGui::Selectable((n + "  (mission)").c_str(), n == use)) { if (!d.setScriptUse(si, n, err)) a.say("edit: %s", err.c_str()); }
        for (const std::string &n : d.libraryScripts())
            if (std::find(mine.begin(), mine.end(), n) == mine.end() && ImGui::Selectable((n + "  (library)").c_str(), n == use)) { if (!d.setScriptUse(si, n, err)) a.say("edit: %s", err.c_str()); }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Code")) openScriptEditor(a, use);
    if (src.origin == Doc::SRC_NONE) ImGui::TextColored(ImVec4(1, 0.4f, 0.35f, 1), "no script named '%s'", use.c_str());
    else {
        ImGui::TextDisabled("%s", src.origin == Doc::SRC_LIBRARY ? "built-in library script (read-only)" : src.origin == Doc::SRC_MOD ? "the mod's script (read-only here)" : "the mission's own script");
        if (src.origin != Doc::SRC_MISSION) {
            if (ImGui::SmallButton("Copy into the mission")) {
                std::string nu;
                if (d.copyScriptToMission(use, nu, err) && d.setScriptUse(si, nu, err)) { a.say("copied '%s' into the mission as %s", use.c_str(), nu.c_str()); openScriptEditor(a, nu); }
                else a.say("edit: %s", err.c_str());
            }
        }
    }

    // target
    ImGui::Separator();
    ImGui::TextUnformatted("Target");
    {
        const MissionScript &s = d.mission.scripts[si];
        static const char *kinds[4] = { "mission", "prop", "pad", "zone" };
        int k = (int)s.kind;
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("kind");
        ImGui::SameLine(112);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##kind", kinds[k])) {
            for (int i = 0; i < 4; i++)
                if (ImGui::Selectable(kinds[i], i == k) && i != k) {
                    MissionPadRef none; memset(&none, 0, sizeof none); none.num = -1;
                    std::string zone = i == MISSION_TGT_ZONE && d.mission.nzones ? d.mission.zones[0].id : "";
                    if (!d.setScriptTarget(si, (MissionTargetKind)i, -1, none, zone, err)) a.say("edit: %s", err.c_str());
                    cancelScriptPick(a);
                }
            ImGui::EndCombo();
        }
    }
    {
        const MissionScript &s = d.mission.scripts[si];
        const bool picking = S.pick != PK_NONE && S.pickScript == si && S.pickField.empty();
        if (s.kind == MISSION_TGT_PROP) {
            int v = s.prop;
            MissionPadRef none; memset(&none, 0, sizeof none);
            std::string chosen;
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("prop");
            ImGui::SameLine(112);
            if (s.uuid[0]) {   // a prop the mission adds
                ItemRef ir; int ty = 0;
                const bool known = d.propByUuid(s.uuid, ir, ty);
                const std::string prev = known ? d.propLabel(ir) : std::string("(no such added prop)");
                ImGui::SetNextItemWidth(150);
                if (addedCombo(a, "##taddedc", prev.c_str(), 0, chosen) && !d.setScriptTarget(si, MISSION_TGT_PROP, -1, none, "", err, chosen)) a.say("edit: %s", err.c_str());
                ImGui::SameLine();
                if (ImGui::SmallButton("###tnum") && !d.setScriptTarget(si, MISSION_TGT_PROP, 0, none, "", err)) a.say("edit: %s", err.c_str());
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Use an original prop's number instead");
            } else {
                ImGui::SetNextItemWidth(70);
                ImGui::InputInt("##tprop", &v, 0, 0);
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    if (!d.setScriptTarget(si, MISSION_TGT_PROP, v, none, "", err)) a.say("edit: %s", err.c_str());
                }
                ImGui::SameLine();
                ImGui::SetNextItemWidth(70);
                if (addedCombo(a, "##taddedpick", "added...", 0, chosen) && !d.setScriptTarget(si, MISSION_TGT_PROP, -1, none, "", err, chosen)) a.say("edit: %s", err.c_str());
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("A prop the mission adds: referenced by its uuid");
            }
            ImGui::SameLine();
            if (picking) { if (ImGui::Button("Cancel")) cancelScriptPick(a); }
            else if (ImGui::Button("Pick in view")) startPick(a, PK_TARGET_PROP, si, "");
            if (s.uuid[0]) ImGui::TextDisabled("    uuid %s", s.uuid);
            else ImGui::TextDisabled("    %s", s.prop < 0 ? "not set" : propDesc(d, s.prop).c_str());
        } else if (s.kind == MISSION_TGT_PAD) {
            char buf[64];
            if (s.pad.is_new) snprintf(buf, sizeof buf, "%s", s.pad.id); else if (s.pad.num >= 0) snprintf(buf, sizeof buf, "0x%x", s.pad.num); else buf[0] = 0;
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("pad");
            ImGui::SameLine(112);
            ImGui::SetNextItemWidth(90);
            ImGui::InputTextWithHint("##tpad", "0x.. or id", buf, sizeof buf);
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                MissionPadRef r; memset(&r, 0, sizeof r);
                char *end = nullptr; long v = strtol(buf, &end, 0);
                if (end && !*end && end != buf) r.num = (int)v; else { r.is_new = 1; snprintf(r.id, sizeof r.id, "%s", buf); }
                if (!d.setScriptTarget(si, MISSION_TGT_PAD, -1, r, "", err)) a.say("edit: %s", err.c_str());
            }
            ImGui::SameLine();
            if (picking) { if (ImGui::Button("Cancel")) cancelScriptPick(a); }
            else if (ImGui::Button("Pick in view")) startPick(a, PK_TARGET_PAD, si, "");
        } else if (s.kind == MISSION_TGT_ZONE) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("zone");
            ImGui::SameLine(112);
            ImGui::SetNextItemWidth(-1);
            std::string prev = s.zone[0] ? s.zone : "(none)";
            if (s.zone[0] && d.zoneIndex(s.zone) < 0) prev += " (no such zone)";
            if (ImGui::BeginCombo("##tzone", prev.c_str())) {
                for (int i = 0; i < d.mission.nzones; i++)
                    if (ImGui::Selectable(d.mission.zones[i].id, !strcmp(s.zone, d.mission.zones[i].id))) {
                        MissionPadRef none; memset(&none, 0, sizeof none); none.num = -1;
                        if (!d.setScriptTarget(si, MISSION_TGT_ZONE, -1, none, d.mission.zones[i].id, err)) a.say("edit: %s", err.c_str());
                    }
                if (d.mission.nzones == 0) ImGui::TextDisabled("no zones yet: Zone tool in the toolbar");
                ImGui::EndCombo();
            }
        } else ImGui::TextDisabled("the script is about the whole mission");
        if (picking) ImGui::TextColored(ImVec4(1, 0.9f, 0.3f, 1), "Click the target in the 3D view (Esc cancels)");
    }

    // properties
    ImGui::Separator();
    ImGui::TextUnformatted("Properties");
    const PropsSchema ps = schemaOf(a, use);
    if (src.origin == Doc::SRC_NONE) ImGui::TextDisabled("(no script)");
    else if (!ps.found) {
        ImGui::TextWrapped("This script declares no `export type Props = { ... }`, so the editor has nothing to ask for.");
        const MissionScript &s = d.mission.scripts[si];
        for (int k = 0; k < s.nprops; k++) {
            if (s.props[k].kind == MISSION_VAL_STRING) ImGui::TextDisabled("%s = \"%s\"", s.props[k].name, s.props[k].str);
            else ImGui::TextDisabled("%s = %.10g", s.props[k].name, s.props[k].num);
        }
    } else {
        if (ps.fields.empty()) ImGui::TextDisabled("(the script takes no properties)");
        for (const PropField &f : ps.fields) { propRow(a, si, f); if (si >= d.mission.nscripts) return; }
        const MissionScript &s = d.mission.scripts[si];
        for (int k = 0; k < s.nprops; k++) {   // keys that are not (any more) fields of Props
            bool known = false;
            for (const PropField &f : ps.fields) if (f.name == s.props[k].name) known = true;
            if (known) continue;
            ImGui::PushID(1000 + k);
            ImGui::TextColored(ImVec4(1, 0.8f, 0.3f, 1), "%s", s.props[k].name);
            ImGui::SameLine(112);
            ImGui::TextDisabled("not in Props");
            ImGui::SameLine();
            const std::string nm = s.props[k].name;
            if (ImGui::SmallButton("remove")) { eraseProp(a, si, nm); ImGui::PopID(); return; }
            ImGui::PopID();
        }
    }

    // findings of the check (validate, type checker)
    ImGui::Separator();
    bool any = false;
    for (const Issue &i : a.issues) {
        if (!i.hasSel || i.sel.kind != Selection::SCRIPT || i.sel.num != si) continue;
        if (!any) { ImGui::TextUnformatted("Problems"); any = true; }
        ImGui::PushStyleColor(ImGuiCol_Text, i.sev >= 2 ? ImVec4(1, 0.5f, 0.45f, 1) : ImVec4(1, 0.85f, 0.35f, 1));
        ImGui::PushID(&i);
        if (ImGui::Selectable(i.text.c_str(), false, ImGuiSelectableFlags_None) && i.line > 0) openScriptEditor(a, i.script, i.line);
        ImGui::PopID();
        ImGui::PopStyleColor();
    }
    if (!any) ImGui::TextDisabled("No problems found.");
}

// ---- the Scene tree ----

static bool filterMatch(const char *filter, const char *text)
{
    if (!filter[0]) return true;
    std::string h = text, n = filter;
    for (char &c : h) c = (char)tolower((unsigned char)c);
    for (char &c : n) c = (char)tolower((unsigned char)c);
    return h.find(n) != std::string::npos;
}

static void addScriptPopup(App &a)
{
    ScriptsUi &S = scriptsUi(a);
    Doc &d = a.doc;
    std::string err;
    if (ImGui::BeginPopup("addscript")) {
        ImGui::TextDisabled("Library scripts");
        for (const std::string &n : d.libraryScripts())
            if (ImGui::Selectable(n.c_str())) {
                int idx;
                if (d.addScript(n, MISSION_TGT_MISSION, idx, err)) selectScript(a, idx); else a.say("edit: %s", err.c_str());
            }
        const std::vector<std::string> mine = d.missionScriptNames();
        if (!mine.empty()) {
            ImGui::Separator();
            ImGui::TextDisabled("This mission's scripts");
            for (const std::string &n : mine)
                if (ImGui::Selectable(n.c_str())) {
                    int idx;
                    if (d.addScript(n, MISSION_TGT_MISSION, idx, err)) selectScript(a, idx); else a.say("edit: %s", err.c_str());
                }
        }
        ImGui::Separator();
        ImGui::TextDisabled("New script");
        ImGui::SetNextItemWidth(140);
        ImGui::InputText("##newname", S.newName, sizeof S.newName);
        ImGui::SameLine();
        if (ImGui::Button("Create")) {
            std::string name = S.newName;
            int idx;
            if (d.newScript(name, err) && d.addScript(name, MISSION_TGT_MISSION, idx, err)) {
                selectScript(a, idx);
                openScriptEditor(a, name);
                ImGui::CloseCurrentPopup();
            } else a.say("edit: %s", err.c_str());
        }
        ImGui::EndPopup();
    }
}

void drawSceneZonesAndScripts(App &a, const char *filter)
{
    Doc &d = a.doc;
    char head[48];
    snprintf(head, sizeof head, "Zones (%d)###zones", d.hasMission ? d.mission.nzones : 0);
    if (ImGui::CollapsingHeader(head, ImGuiTreeNodeFlags_DefaultOpen) && d.hasMission) {
        for (int i = 0; i < d.mission.nzones; i++) {
            const MissionZone &z = d.mission.zones[i];
            char l[96]; snprintf(l, sizeof l, "%s  r %.0f  room %d", z.id, z.radius, z.room);
            if (!filterMatch(filter, l)) continue;
            const bool sel = d.sel.kind == Selection::ZONE && d.sel.id == z.id;
            ImGui::PushID(i);
            if (ImGui::Selectable(l, sel, ImGuiSelectableFlags_AllowDoubleClick)) {
                d.sel = Selection(); d.sel.kind = Selection::ZONE; d.sel.id = z.id;
                if (ImGui::IsMouseDoubleClicked(0)) focusOn(a, { z.pos[0], z.pos[1], z.pos[2] }, std::max(450.0f, z.radius * 3));
            }
            ImGui::PopID();
        }
        if (d.mission.nzones == 0) ImGui::TextDisabled("none: Zone tool, click the floor");
    }
    snprintf(head, sizeof head, "Scripts (%d)###scripts", d.hasMission ? d.mission.nscripts : 0);
    if (ImGui::CollapsingHeader(head, ImGuiTreeNodeFlags_DefaultOpen) && d.hasMission) {
        for (int i = 0; i < d.mission.nscripts; i++) {
            const MissionScript &s = d.mission.scripts[i];
            char l[128];
            const char *kind = Doc::targetKindName(s.kind);
            if (s.kind == MISSION_TGT_PROP) snprintf(l, sizeof l, "%s > %s %d", s.use, kind, s.prop);
            else if (s.kind == MISSION_TGT_ZONE) snprintf(l, sizeof l, "%s > %s %s", s.use, kind, s.zone);
            else if (s.kind == MISSION_TGT_PAD) snprintf(l, sizeof l, "%s > %s %s", s.use, kind, padLabel(s.pad).c_str());
            else snprintf(l, sizeof l, "%s > %s", s.use, kind);
            if (!filterMatch(filter, l)) continue;
            int bad = 0;
            for (const Issue &is : a.issues) if (is.hasSel && is.sel.kind == Selection::SCRIPT && is.sel.num == i && is.sev >= 2) bad = 1;
            const bool sel = d.sel.kind == Selection::SCRIPT && d.sel.num == i;
            ImGui::PushID(2000 + i);
            if (bad) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.5f, 0.45f, 1));
            if (ImGui::Selectable(l, sel, ImGuiSelectableFlags_AllowDoubleClick)) {
                selectScript(a, i);
                if (ImGui::IsMouseDoubleClicked(0)) openScriptEditor(a, s.use);
            }
            if (bad) ImGui::PopStyleColor();
            ImGui::PopID();
        }
        if (ImGui::SmallButton("Add script...")) ImGui::OpenPopup("addscript");
        addScriptPopup(a);
    }
}

// ---- zones: the Selection tab ----

void drawZoneSection(App &a)
{
    Doc &d = a.doc;
    std::string err;
    const int zi = d.zoneIndex(d.sel.id);
    if (zi < 0) { ImGui::TextDisabled("(zone no longer exists)"); return; }
    const MissionZone z = d.mission.zones[zi];
    ImGui::Text("Zone %s", z.id);
    ImGui::TextDisabled("A sphere: a script sees a player whose position is within the radius of the centre.");
    float p[3] = { z.pos[0], z.pos[1], z.pos[2] };
    if (ImGui::InputFloat3("centre", p, "%.1f", ImGuiInputTextFlags_EnterReturnsTrue)) {
        const Vec3 np = { p[0], p[1], p[2] };
        if (!d.setZone(z.id, &np, nullptr, nullptr, true, err)) a.say("move: %s", err.c_str());
    }
    float r = z.radius;
    ImGui::InputFloat("radius", &r, 10, 50, "%.0f");
    if (ImGui::IsItemDeactivatedAfterEdit()) { if (!d.setZone(z.id, nullptr, &r, nullptr, true, err)) a.say("edit: %s", err.c_str()); }
    int room = z.room;
    ImGui::InputInt("room", &room);
    if (ImGui::IsItemDeactivatedAfterEdit()) { if (!d.setZone(z.id, nullptr, nullptr, &room, true, err)) a.say("edit: %s", err.c_str()); }
    ImGui::TextWrapped("Drag the gizmo in the view, or G to grab; X/Y/Z lock an axis.");
    if (ImGui::Button("Remove")) { const std::string id = z.id; if (d.removeZone(id, err)) d.sel = Selection(); else a.say("remove: %s", err.c_str()); return; }
    ImGui::SameLine();
    if (ImGui::Button("Add a script here")) {
        int idx;
        MissionPadRef none; memset(&none, 0, sizeof none); none.num = -1;
        const std::vector<std::string> lib = d.libraryScripts();
        if (!lib.empty() && d.addScript(lib[0], MISSION_TGT_ZONE, idx, err) && d.setScriptTarget(idx, MISSION_TGT_ZONE, -1, none, z.id, err)) selectScript(a, idx);
        else a.say("edit: %s", err.c_str());
    }
    bool any = false;
    for (int i = 0; i < d.mission.nscripts; i++)
        if (d.mission.scripts[i].kind == MISSION_TGT_ZONE && !strcmp(d.mission.scripts[i].zone, z.id)) {
            if (!any) { ImGui::Separator(); ImGui::TextUnformatted("Scripts on this zone"); any = true; }
            ImGui::PushID(i);
            if (ImGui::Selectable(d.mission.scripts[i].use)) selectScript(a, i);
            ImGui::PopID();
        }
}

bool zoneToolClick(App &a, const Hit &h)
{
    if (a.tool != App::T_ZONE) return false;
    Vec3 p{};
    int room = -1;
    if (h.kind == Hit::SURFACE) { p = h.surfacePoint; room = h.room; }
    else if (h.kind == Hit::PAD || h.kind == Hit::MPAD || h.kind == Hit::ITEM) p = h.point;
    else { a.say("click the floor to place a zone"); return true; }
    if (!ensureMission(a)) return true;
    a.doc.snapPoint(p, a.snapOpts());
    p[1] += 50.0f;   // a player's position is above the floor: the sphere is centred a little above it
    std::string id, err;
    if (a.doc.addZone(p, 150.0f, room, id, err)) {
        a.doc.sel = Selection(); a.doc.sel.kind = Selection::ZONE; a.doc.sel.id = id;
        a.forceTab = 0;
        a.say("zone %s added", id.c_str());
    } else a.say("add zone: %s", err.c_str());
    return true;
}

// ---- overlay ----

static void ring(const Overlay &o, const Vec3 &c, float r, int plane, ImU32 col, float th)
{
    const int N = 64;
    Vec3 prev{};
    for (int i = 0; i <= N; i++) {
        const float an = (float)i * 6.2831853f / (float)N, cs = cosf(an) * r, sn = sinf(an) * r;
        Vec3 q = c;
        if (plane == 1) { q[0] += cs; q[2] += sn; }       // horizontal
        else if (plane == 0) { q[1] += cs; q[2] += sn; }  // vertical, across x
        else { q[0] += cs; q[1] += sn; }                  // vertical, across z
        if (i) o.line(prev, q, col, th);
        prev = q;
    }
}

static void drawZone(const Overlay &o, const MissionZone &z, bool sel, bool target, const Vec3 &cam)
{
    const Vec3 c = { z.pos[0], z.pos[1], z.pos[2] };
    const ImU32 col = target ? IM_COL32(60, 230, 255, 255) : sel ? IM_COL32(255, 220, 40, 255) : IM_COL32(60, 200, 190, 200);
    const ImU32 faint = target ? IM_COL32(60, 230, 255, 120) : sel ? IM_COL32(255, 220, 40, 110) : IM_COL32(60, 200, 190, 70);
    const float th = sel || target ? 2.5f : 1.5f;
    ring(o, c, z.radius, 1, col, th);
    ring(o, c, z.radius, 0, faint, 1.0f);
    ring(o, c, z.radius, 2, faint, 1.0f);
    o.line({ c[0], c[1] - 50, c[2] }, { c[0], c[1] + 50, c[2] }, col, 2.0f);   // the height marker through the centre
    o.diamond(c, sel ? 9.0f : 6.0f, col, true);
    char t[64]; snprintf(t, sizeof t, "zone %s  r %.0f", z.id, z.radius);
    o.text({ c[0], c[1] + 50, c[2] }, t, col, 6, -14);
    (void)cam;
}

static bool targetPos(App &a, const MissionScript &s, Vec3 &p)
{
    if (s.kind == MISSION_TGT_PROP) {
        ItemRef ir; int ty = 0;
        if (a.doc.propByNumber(s.prop, ir, ty))
            for (const Item &it : a.items) if (it.added == ir.added && it.guard == ir.guard && it.idx == ir.idx && it.hasPos) { p = it.pos; return true; }
    } else if (s.kind == MISSION_TGT_PAD) return a.doc.padPos(s.pad, p);
    else if (s.kind == MISSION_TGT_ZONE) {
        const int zi = a.doc.zoneIndex(s.zone);
        if (zi >= 0) { p = { a.doc.mission.zones[zi].pos[0], a.doc.mission.zones[zi].pos[1], a.doc.mission.zones[zi].pos[2] }; return true; }
    }
    return false;
}

static void markPropBox(App &a, const Overlay &o, int prop, ImU32 col, float th, const char *label, const char *uuid = nullptr)
{
    ItemRef ir; int ty = 0;
    if (uuid && uuid[0]) { if (!a.doc.propByUuid(uuid, ir, ty)) return; }
    else if (!a.doc.propByNumber(prop, ir, ty)) return;
    for (const Item &it : a.items) {
        if (it.added != ir.added || it.guard != ir.guard || it.idx != ir.idx || !it.hasPos) continue;
        Vec3 lmn, lmx;
        if (!itemLocalBox(a.doc, it, lmn, lmx)) return;
        const Vec3 grow = { 6, 6, 6 };
        const Vec3 mn = { lmn[0] - grow[0], lmn[1] - grow[1], lmn[2] - grow[2] }, mx = { lmx[0] + grow[0], lmx[1] + grow[1], lmx[2] + grow[2] };
        o.boxItem(it, mn, mx, col, th);
        o.text({ it.pos[0], it.pos[1] + lmx[1] + 20, it.pos[2] }, label, col, -10, -14);
        return;
    }
}

void drawScriptOverlay(App &a, const Overlay &ov)
{
    Doc &d = a.doc;
    if (!d.hasMission) return;
    const bool selScript = d.sel.kind == Selection::SCRIPT && d.sel.num >= 0 && d.sel.num < d.mission.nscripts;
    const MissionScript *ss = selScript ? &d.mission.scripts[d.sel.num] : nullptr;
    for (int i = 0; i < d.mission.nzones; i++) {
        const MissionZone &z = d.mission.zones[i];
        const bool sel = d.sel.kind == Selection::ZONE && d.sel.id == z.id;
        const bool tgt = ss && ss->kind == MISSION_TGT_ZONE && !strcmp(ss->zone, z.id);
        drawZone(ov, z, sel, tgt, a.cam.pos);
    }
    if (ss) {
        const ImU32 cyan = IM_COL32(60, 230, 255, 255), light = IM_COL32(120, 200, 255, 200);
        char t[64];
        snprintf(t, sizeof t, "script %d: %s (target)", d.sel.num + 1, ss->use);
        if (ss->kind == MISSION_TGT_PROP) markPropBox(a, ov, ss->prop, cyan, 3.0f, t, ss->uuid);
        else if (ss->kind == MISSION_TGT_PAD) {
            Vec3 p;
            if (a.doc.padPos(ss->pad, p)) { ov.diamond({ p[0], p[1] + 8, p[2] }, 12.0f, cyan, false); ov.diamond({ p[0], p[1] + 8, p[2] }, 6.0f, cyan, true); ov.text(p, t, cyan); }
        }
        // what its properties point at: lighter
        const PropsSchema &ps = schemaOf(a, ss->use);
        for (const PropField &f : ps.fields) {
            const MissionProp *pv = findProp(*ss, f.name);
            if (!pv) continue;
            if ((f.type == "Door" || f.type == "Prop" || f.type == "Guard") && pv->kind == MISSION_VAL_NUMBER) markPropBox(a, ov, (int)pv->num, light, 1.8f, f.name.c_str());
            else if ((f.type == "Door" || f.type == "Prop" || f.type == "Guard") && pv->kind == MISSION_VAL_STRING) markPropBox(a, ov, -1, light, 1.8f, f.name.c_str(), pv->str);
            else if (f.type == "Pad") {
                MissionPadRef r; memset(&r, 0, sizeof r);
                if (pv->kind == MISSION_VAL_STRING) { r.is_new = 1; snprintf(r.id, sizeof r.id, "%s", pv->str); } else r.num = (int)pv->num;
                Vec3 p;
                if (a.doc.padPos(r, p)) { ov.diamond({ p[0], p[1] + 8, p[2] }, 9.0f, light, false); ov.text(p, f.name.c_str(), light); }
            } else if (f.type == "Zone" && pv->kind == MISSION_VAL_STRING) {
                const int zi = d.zoneIndex(pv->str);
                if (zi >= 0) {
                    const MissionZone &z = d.mission.zones[zi];
                    ring(ov, { z.pos[0], z.pos[1], z.pos[2] }, z.radius, 1, light, 2.0f);
                    ov.text({ z.pos[0], z.pos[1], z.pos[2] }, f.name.c_str(), light, 6, 6);
                }
            }
        }
    }
    if (scriptPicking(a)) {
        const char *what = a.scr->pick == PK_TARGET_PAD || a.scr->pick == PK_FIELD_PAD ? "a pad" : a.scr->pick == PK_FIELD_GUARD ? "a guard" : "a prop (door, guard or object)";
        char t[128]; snprintf(t, sizeof t, "Click %s to pick it - Esc cancels", what);
        ov.dl->AddText(ImVec2(ov.x + 10, ov.y + 8), IM_COL32(255, 255, 120, 255), t);
    }
}

// ---- the code editor ----

static void initLanguage(ScriptsUi &S)
{
    if (S.langInit) return;
    S.langInit = true;
    if (const TextEditor::Language *lua = TextEditor::LookupLanguage("Lua")) S.lang = *lua;
    S.lang.name = "Luau";
    for (const char *k : { "continue", "export", "type" }) S.lang.keywords.insert(k);
    for (const char *k : { "number", "string", "boolean", "any", "unknown", "never" }) S.lang.declarations.insert(k);
    for (const char *k : { "give", "take", "has", "message", "say", "wait", "event", "door", "guard", "objectives", "alarm", "mission", "target", "props" }) S.lang.identifiers.insert(k);
    S.ed.SetLanguage(&S.lang);
    S.ed.SetTabSize(4);
    S.ed.SetInsertSpacesOnTabs(true);
    S.ed.SetShowMiniMapEnabled(false);
    S.ed.SetShowWhitespacesEnabled(false);
    S.ed.SetChangeCallback([&S] { S.edChanged = true; }, 350);   // the type checker re-runs 350 ms after the last key
}

void openScriptEditor(App &a, const std::string &use, int line)
{
    ScriptsUi &S = scriptsUi(a);
    initLanguage(S);
    if (!S.edOpen || S.edUse != use) { S.edUse = use; S.edReload = true; S.edPushed.clear(); S.diagKey = 0; }
    S.edOpen = true;
    S.gotoLine = line;
    if (line > 0 && !S.edReload) { S.ed.ScrollToLine((size_t)(line - 1)); S.ed.SetCursor(TextEditor::DocPos((size_t)(line - 1), 0)); S.gotoLine = 0; }
}

void scriptsFrame(App &a)
{
    ScriptsUi &S = scriptsUi(a);
    if (a.frameNo % 45 == 0) a.doc.scriptCacheClear();
    refreshIssues(a, false);   // the Problems list and the red rows of the Scene tree   // an edit made in another program shows up
    if (S.pick != PK_NONE && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) S.pick = PK_NONE;
    if (!S.edOpen) return;
    initLanguage(S);
    Doc &d = a.doc;
    const Doc::ScriptSrc src = d.scriptSource(S.edUse);
    if (src.origin == Doc::SRC_NONE || !d.hasMission) { S.edOpen = false; return; }
    const bool ro = src.origin != Doc::SRC_MISSION;
    if (S.edReload || !sameText(src.text, S.edPushed)) {   // first show, or the document changed under us (script_write, a copy)
        S.ed.SetText(src.text);
        S.ed.SetReadOnlyEnabled(ro);
        S.edPushed = src.text;
        S.edReload = false;
        S.diagKey = 0;
        if (S.gotoLine > 0) {
            S.ed.ScrollToLine((size_t)(S.gotoLine - 1));
            S.ed.SetCursor(TextEditor::DocPos((size_t)(S.gotoLine - 1), 0));
            S.gotoLine = 0;
        }
    }
    S.ed.SetReadOnlyEnabled(ro);
    if (S.edChanged) {
        S.edChanged = false;
        if (!ro) {
            const std::string t = S.ed.GetText();
            std::string err;
            if (!sameText(t, S.edPushed)) {
                if (d.setScriptText(S.edUse, t, err)) S.edPushed = t; else a.say("edit: %s", err.c_str());
            }
        }
    }
    // the type checker's findings for the text the document holds: line markers and squiggles
    const std::string kind = kindOfUse(d, S.edUse);
    const size_t key = std::hash<std::string>()(src.text) * 31 + std::hash<std::string>()(kind);
    if (key != S.diagKey) {
        S.diagKey = key;
        S.diags = checkScript(src.text, kind, nullptr);
        S.ed.ClearMarkers();
        S.ed.ClearSquiggles();
        for (const ScriptDiag &g : S.diags) {
            const ImU32 c = g.sev >= 2 ? IM_COL32(235, 70, 60, 255) : IM_COL32(235, 190, 50, 255);
            S.ed.AddMarker((size_t)(g.line - 1), c, g.sev >= 2 ? IM_COL32(120, 30, 30, 70) : IM_COL32(120, 100, 20, 60), g.text, g.text);
            size_t sl = (size_t)(g.line - 1), sc = (size_t)(g.col - 1), el = (size_t)(g.endLine - 1), ec = (size_t)(g.endCol - 1);
            if (el != sl || ec <= sc) { el = sl; ec = sc + 1; }
            S.ed.AddSquiggle(TextEditor::DocPos(sl, sc), TextEditor::DocPos(el, ec), 1, c, g.text);
        }
    }
}

void drawScriptEditorWindow(App &a)
{
    ScriptsUi &S = scriptsUi(a);
    if (!S.edOpen) return;
    Doc &d = a.doc;
    const Doc::ScriptSrc src = d.scriptSource(S.edUse);
    if (src.origin == Doc::SRC_NONE) return;
    const bool ro = src.origin != Doc::SRC_MISSION;
    ImGui::SetNextWindowPos(ImVec2(a.vx + 30, a.vy + 20), ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(ImVec2(std::min(860.0f, std::max(400.0f, a.vw - 60)), std::min(600.0f, std::max(300.0f, a.vh - 40))), ImGuiCond_Appearing);
    bool open = true;
    char title[96];
    snprintf(title, sizeof title, "Script: %s%s###scripted", S.edUse.c_str(), d.scriptDirty.count(S.edUse) ? " *" : "");
    if (!ImGui::Begin(title, &open, ImGuiWindowFlags_NoSavedSettings)) { ImGui::End(); if (!open) S.edOpen = false; return; }
    std::string err;
    ImGui::TextDisabled("%s", src.origin == Doc::SRC_LIBRARY ? "built-in library script: read-only" : src.origin == Doc::SRC_MOD ? "the mod's script: read-only here" : shortPath(src.path, 60).c_str());
    if (ro) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Copy into the mission")) {
            std::string nu;
            if (d.copyScriptToMission(S.edUse, nu, err)) {
                for (int i = 0; i < d.mission.nscripts; i++)   // the entry being looked at (or every entry that used it) switches to the copy
                    if (S.edUse == d.mission.scripts[i].use && (d.sel.kind != Selection::SCRIPT || d.sel.num == i)) d.setScriptUse(i, nu, err);
                a.say("copied into the mission as %s (saved next to mission.json with Ctrl+S)", nu.c_str());
                openScriptEditor(a, nu);
            } else a.say("edit: %s", err.c_str());
        }
    } else {
        ImGui::SameLine();
        ImGui::TextDisabled("Ctrl+S saves it with the mission");
    }
    const float diagH = std::min(130.0f, 24.0f + 18.0f * (float)std::max<size_t>(1, S.diags.size()));
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    S.ed.Render("##code", ImVec2(0, std::max(80.0f, avail.y - diagH - 6)));
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
        if (S.edChanged && !ro) { S.edChanged = false; const std::string t = S.ed.GetText(); if (d.setScriptText(S.edUse, t, err)) S.edPushed = t; }
        doSave(a);
    }
    ImGui::BeginChild("##diags", ImVec2(0, 0), ImGuiChildFlags_Borders);
    if (S.diags.empty()) ImGui::TextDisabled("No type errors.");
    for (size_t i = 0; i < S.diags.size(); i++) {
        const ScriptDiag &g = S.diags[i];
        ImGui::PushStyleColor(ImGuiCol_Text, g.sev >= 2 ? ImVec4(1, 0.5f, 0.45f, 1) : ImVec4(1, 0.85f, 0.35f, 1));
        char l[600]; snprintf(l, sizeof l, "line %d: %s", g.line, g.text.c_str());
        ImGui::PushID((int)i);
        if (ImGui::Selectable(l)) { S.ed.ScrollToLine((size_t)(g.line - 1)); S.ed.SetCursor(TextEditor::DocPos((size_t)(g.line - 1), (size_t)(g.col - 1))); }
        ImGui::PopID();
        ImGui::PopStyleColor();
    }
    ImGui::EndChild();
    ImGui::End();
    if (!open) S.edOpen = false;
}

} // namespace ed
