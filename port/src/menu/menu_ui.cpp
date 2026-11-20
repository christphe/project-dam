/* menu_ui.cpp -- the fullscreen menu, written against Dear ImGui only (see menu_ui.h).
 *
 * The content comes from menu.json through menu_model: tabs along the top, a list of entries on the left, the
 * form of the selected entry on the right, a footer of button prompts. The custom look is drawn with ImDrawList;
 * ImGui provides the hit testing and the mouse. Gamepad and keyboard navigation is the menu's own: the list on
 * the left has the focus until A or right enters the form, up / down move between controls, left / right change
 * the value, B leaves the form, B on the list asks the game to close the menu. */
#include "menu_ui.h"
#include "menu_model.h"
#include "../../third_party/imgui/imgui.h"

#include <cstdio>
#include <cstring>

namespace menu {

namespace {

const ImU32 kGold = IM_COL32(230, 194, 90, 255);
const ImU32 kGoldFill = IM_COL32(230, 194, 90, 48);
const ImU32 kGoldSoft = IM_COL32(230, 194, 90, 24);
const ImU32 kGoldDim = IM_COL32(141, 122, 61, 255);
const ImU32 kRule = IM_COL32(108, 106, 94, 255);
const ImU32 kRow = IM_COL32(18, 19, 15, 255);
const ImU32 kInk = IM_COL32(243, 238, 224, 255);
const ImU32 kMut = IM_COL32(169, 165, 147, 255);
const ImU32 kLine = IM_COL32(63, 62, 55, 255);
const ImU32 kDisabled = IM_COL32(110, 108, 98, 255);

/* the game defines its own toupper (src/str.c) and shadows <math.h>: plain helpers only */
inline char upc(char c) { return (c >= 'a' && c <= 'z') ? (char)(c - 32) : c; }
inline float maxf(float a, float b) { return a > b ? a : b; }
inline float minf(float a, float b) { return a < b ? a : b; }

ImFont* g_body = nullptr;
ImFont* g_head = nullptr;

Model g_model;
State g_state;
std::string g_error;

int g_tab = 0;
int g_entry = 0;          /* index among the visible entries of the tab */
int g_focus = -1;         /* index among the visible controls of the entry, or -1 while the list has the focus */
bool g_closeReq = false;
float g_scroll = 0, g_scrollTarget = 0;
std::string g_actionFired;   /* the id of the last action control activated (the game's adapter reads it) */
std::string g_selId;         /* the select whose list is open, or "" */
int g_ownerSeat = -1;
float g_padUv[4] = { 0, 0, 0, 0 }, g_padAspect = 0;
bool g_root = true;      /* the first menu: Resume / Options / Restart / Quit */
int g_rootSel = 0, g_rootArmed = -1;
std::string (*g_keyText)(const char*, int) = nullptr;
void (*g_keyBegin)(const char*, int) = nullptr;
bool (*g_keyActive)() = nullptr;
void (*g_actionHandler)(const char*) = nullptr;
std::string g_capId;
int g_capSlot = 0, g_slotSel = 0;
bool g_capturing = false;   /* a key or button is being captured: the menu keeps still */
bool (*g_bindGet)(const char*, double*) = nullptr;
void (*g_bindSet)(const char*, double) = nullptr;
std::map<std::string, double> g_synced;   /* control id -> the value last agreed with the game */
int g_selCursor = 0, g_selScroll = 0, g_selOpenFrame = -1;
ImVec2 g_selAnchor(0, 0);
float g_selW = 0, g_selH = 0;
bool g_blockHits = false;    /* while a list is open it takes every click: nothing under it is hit-tested */

struct Vis {   /* what is visible on the current tab, rebuilt every frame */
    std::vector<int> entries;                        /* item indexes of the visible entries */
    std::vector<const Control*> controls;           /* the visible controls of the selected entry */
};

std::string upper(const std::string& s) {
    std::string o = s;
    for (char& c : o) c = upc(c);
    return o;
}

ImVec2 textSize(ImFont* f, float px, const char* s) { return f->CalcTextSizeA(px, 1e9f, 0.0f, s); }
void text(ImDrawList* dl, ImFont* f, float px, ImVec2 p, ImU32 col, const char* s) { dl->AddText(f, px, p, col, s); }
void textCenter(ImDrawList* dl, ImFont* f, float px, ImVec2 a, ImVec2 b, ImU32 col, const char* s) {
    const ImVec2 sz = textSize(f, px, s);
    text(dl, f, px, ImVec2(a.x + (b.x - a.x - sz.x) * 0.5f, a.y + (b.y - a.y - sz.y) * 0.5f), col, s);
}
void box(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 fill, ImU32 border) {
    dl->AddRectFilled(a, b, fill);
    dl->AddRect(a, b, border, 0.0f, 0, 1.0f);
}
bool hit(const char* id, ImVec2 a, ImVec2 size) {
    if (g_blockHits) return false;
    ImGui::SetCursorScreenPos(a);
    return ImGui::InvisibleButton(id, size);
}

/* an action control: "__ResetEntry" puts every control of its entry back to its default, anything else is the game's */
void fireAction(const Control& c) {
    g_actionFired = c.id;
    if (c.key == "__ResetEntry") {
        for (const Tab& t : g_model.tabs)
            for (const Item& it : t.items) {
                bool mine = false;
                for (const Control& k : it.controls) if (&k == &c) mine = true;
                if (!mine) continue;
                for (const Control& k : it.controls)
                    if (k.type != CT_ACTION && k.type != CT_BIND) g_state.v[k.id] = k.def[g_state.pc ? 1 : 0];
            }
        return;
    }
    if (g_actionHandler) g_actionHandler(c.key.c_str());
}

void changeValue(const Control& c, int d) {
    double& v = g_state.v[c.id];
    if (c.type == CT_CHOICE || c.type == CT_SELECT) {
        const int n = (int)c.options.size();
        int k = (int)v;
        for (int tries = 0; tries < n; tries++) {   /* the next option that is offered */
            k = (k + d + n) % n;
            if (optionVisible(c, k, g_model, g_state)) break;
        }
        v = (double)k;
    } else if (c.type == CT_TOGGLE) {
        v = v != 0 ? 0 : 1;
    } else if (c.type == CT_SLIDER) {
        v += d * c.step;
        if (v < c.vmin) v = c.vmin;
        if (v > c.vmax) v = c.vmax;
    } else {
        fireAction(c);
    }
}

bool pressed(ImGuiKey a, ImGuiKey b, ImGuiKey c, bool repeat) {
    return ImGui::IsKeyPressed(a, repeat) || ImGui::IsKeyPressed(b, repeat) || (c != ImGuiKey_None && ImGui::IsKeyPressed(c, repeat));
}

Vis visibleOf(const Tab& tab) {
    Vis v;
    for (size_t i = 0; i < tab.items.size(); i++)
        if (!tab.items[i].group && itemVisible(tab.items[i], g_model, g_state)) v.entries.push_back((int)i);
    return v;
}

/* ---- the controls ---- */
struct Slot { float x, y, w; };

/* the buttons of a choice, wrapped over several lines when they do not fit */
float layoutChoice(const Control& c, float x0, float availW, float u, std::vector<Slot>* out) {
    const float bh = 3.1f * u, gap = 0.8f * u;
    float x = 0, y = 0;
    for (size_t oi = 0; oi < c.options.size(); oi++) {
        const Option& o = c.options[oi];
        if (!optionVisible(c, (int)oi, g_model, g_state)) { out->push_back({ x0, 0, 0 }); continue; }   /* keeps the slot index */
        const ImVec2 sz = textSize(g_head, 1.55f * u, tr(o.label).c_str());
        const float bw = maxf(6.5f * u, sz.x + 2.6f * u);
        if (x > 0 && x + bw > availW) { x = 0; y += bh + 0.6f * u; }
        out->push_back({ x0 + x, y, bw });
        x += bw + gap;
    }
    return y + bh;
}

void drawChoice(ImDrawList* dl, const Control& c, int fi, float x0, float y0, float availW, float u, bool disabled) {
    std::vector<Slot> slots;
    layoutChoice(c, x0, availW, u, &slots);
    const int cur = (int)g_state.get(c.id);
    const float bh = 3.1f * u;
    for (size_t i = 0; i < slots.size(); i++) {
        if (slots[i].w <= 0) continue;   /* an option that is not offered */
        char id[64];
        snprintf(id, sizeof id, "##%s_%d", c.id.c_str(), (int)i);
        const ImVec2 a(slots[i].x, y0 + slots[i].y), b(slots[i].x + slots[i].w, y0 + slots[i].y + bh);
        if (hit(id, a, ImVec2(slots[i].w, bh)) && !disabled) { g_state.v[c.id] = (double)i; g_focus = fi; }
        const bool on = (int)i == cur;
        box(dl, a, b, on ? (disabled ? kGoldSoft : kGoldFill) : IM_COL32(21, 22, 15, 255), on ? (disabled ? kRule : kGold) : kRule);
        if (on && !disabled) dl->AddRectFilled(ImVec2(a.x, b.y - 0.22f * u), b, kGold);
        textCenter(dl, g_head, 1.55f * u, a, b, disabled ? kDisabled : kInk, tr(c.options[i].label).c_str());
    }
}

void drawToggle(ImDrawList* dl, const Control& c, int fi, float x, float y, float u, bool disabled) {
    const float s = 2.1f * u;
    char id[64];
    snprintf(id, sizeof id, "##%s", c.id.c_str());
    if (hit(id, ImVec2(x, y), ImVec2(s + 6.0f * u, s)) && !disabled) { changeValue(c, 1); g_focus = fi; }
    const bool on = g_state.get(c.id) != 0;
    box(dl, ImVec2(x, y), ImVec2(x + s, y + s), IM_COL32(13, 14, 12, 255), on && !disabled ? kGold : kRule);
    if (on) {
        const float k = s * 0.18f;
        const ImU32 col = disabled ? kDisabled : kGold;
        dl->AddLine(ImVec2(x + s * 0.2f, y + s * 0.52f), ImVec2(x + s * 0.42f, y + s * 0.74f), col, k);
        dl->AddLine(ImVec2(x + s * 0.42f, y + s * 0.74f), ImVec2(x + s * 0.82f, y + s * 0.26f), col, k);
    }
    text(dl, g_body, 1.6f * u, ImVec2(x + s + 0.9f * u, y + (s - 1.6f * u) * 0.5f - 0.1f * u), disabled ? kDisabled : kInk, on ? tr("ON").c_str() : tr("OFF").c_str());
}

void drawSlider(ImDrawList* dl, const Control& c, int fi, float x, float y, float u, bool disabled) {
    const float w = 22.0f * u, h = 2.6f * u;
    char id[64];
    snprintf(id, sizeof id, "##%s", c.id.c_str());
    const bool submitted = !g_blockHits;
    if (submitted) hit(id, ImVec2(x, y), ImVec2(w, h));
    if (submitted && ImGui::IsItemActive() && !disabled) {
        const float f = minf(1.0f, maxf(0.0f, (ImGui::GetIO().MousePos.x - x) / w));
        int v = (int)((c.vmin + f * (c.vmax - c.vmin)) / c.step + 0.5f) * c.step;
        if (v < c.vmin) v = c.vmin;
        if (v > c.vmax) v = c.vmax;
        g_state.v[c.id] = v;
        g_focus = fi;
    }
    const double val = g_state.get(c.id);
    float f = (float)(val - c.vmin) / (float)(c.vmax - c.vmin);
    f = f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f);   /* a value outside the range still draws inside the track */
    const float ty = y + h * 0.5f;
    dl->AddRectFilled(ImVec2(x, ty - 0.2f * u), ImVec2(x + w, ty + 0.2f * u), IM_COL32(58, 59, 51, 255));
    dl->AddRectFilled(ImVec2(x, ty - 0.2f * u), ImVec2(x + 0.45f * u + (w - 0.9f * u) * f, ty + 0.2f * u), disabled ? kDisabled : kGold);
    if (!c.options.empty() && c.options.size() > 1)   /* a notch per option */
        for (size_t i = 0; i < c.options.size(); i++) {
            const float nx = x + w * (float)i / (float)(c.options.size() - 1);
            dl->AddRectFilled(ImVec2(nx - 0.12f * u, ty - 0.7f * u), ImVec2(nx + 0.12f * u, ty + 0.7f * u), disabled ? kDisabled : kMut);
        }
    const float kx = x + 0.45f * u + (w - 0.9f * u) * f;
    box(dl, ImVec2(kx - 0.45f * u, ty - 0.95f * u), ImVec2(kx + 0.45f * u, ty + 0.95f * u), disabled ? kDisabled : kInk, IM_COL32(13, 14, 12, 255));
    text(dl, g_head, 1.7f * u, ImVec2(x + w + 1.4f * u, ty - 0.85f * u), disabled ? kDisabled : kGold, sliderText(c, val).c_str());
}

void drawAction(ImDrawList* dl, const Control& c, int fi, float x, float y, float u, bool disabled) {
    const float bh = 3.1f * u;
    const ImVec2 sz = textSize(g_head, 1.55f * u, tr(c.label).c_str());
    const float bw = maxf(14.0f * u, sz.x + 3.0f * u);
    char id[64];
    snprintf(id, sizeof id, "##%s", c.id.c_str());
    if (hit(id, ImVec2(x, y), ImVec2(bw, bh)) && !disabled) { fireAction(c); g_focus = fi; }
    box(dl, ImVec2(x, y), ImVec2(x + bw, y + bh), IM_COL32(21, 22, 15, 255), kRule);
    textCenter(dl, g_head, 1.55f * u, ImVec2(x, y), ImVec2(x + bw, y + bh), disabled ? kDisabled : kInk, tr(c.label).c_str());
}

void startCapture(const Control& c, int slot) {
    g_capId = c.id;
    g_capSlot = slot;
    g_slotSel = slot;
    if (g_keyBegin) g_keyBegin(c.key.c_str(), slot);
}

void drawBind(ImDrawList* dl, const Control& c, int fi, float x, float y, float u, bool disabled) {
    const float bw = 15.0f * u, bh = 3.1f * u, gap = 1.0f * u;
    for (int slot = 0; slot < 2; slot++) {
        const ImVec2 a(x + slot * (bw + gap), y), b(a.x + bw, y + bh);
        char id[64];
        snprintf(id, sizeof id, "##%s_k%d", c.id.c_str(), slot);
        if (hit(id, a, ImVec2(bw, bh)) && !disabled && !g_capturing) { startCapture(c, slot); g_focus = fi; }
        const bool cap = g_capturing && g_capId == c.id && g_capSlot == slot;
        const bool sel = g_focus == fi && g_slotSel == slot;
        std::string t = cap ? tr("Press a key") : (g_keyText ? g_keyText(c.key.c_str(), slot) : std::string());
        if (t.empty()) t = "-";
        box(dl, a, b, cap ? kGoldFill : IM_COL32(21, 22, 15, 255), cap || sel ? kGold : kRule);
        textCenter(dl, g_head, 1.55f * u, a, b, disabled ? kDisabled : (cap ? kGold : kInk), t.c_str());
    }
}

/* the gamepad drawn with what each button does in the chosen layout: the left half's labels on the left, the right half's on the right */
void drawDiagram(ImDrawList* dl, const Control& c, float x, float y, float w, float u) {
    auto fit = g_model.byId.find(c.follows);
    if (fit == g_model.byId.end()) return;
    const Control& f = *fit->second;
    const int k = (int)g_state.get(f.id);
    if (k < 0 || k >= (int)f.options.size()) return;
    auto lit = c.layouts.find(f.options[k].id);
    if (lit == c.layouts.end()) return;
    const auto& L = lit->second;
    const float pw = 30.0f * u, ph = g_padAspect > 0 ? pw / g_padAspect : 19.0f * u, px = x + (w - pw) * 0.5f + 3.0f * u, py = y + 3.5f * u;
    const float fs = 1.35f * u;
    const ImU32 ink = kInk, line = kRule;
    if (g_padAspect > 0)   /* the outline (a mask: its lines take this colour) */
        dl->AddImage((ImTextureID)(intptr_t)1, ImVec2(px, py), ImVec2(px + pw, py + ph), ImVec2(g_padUv[0], g_padUv[1]), ImVec2(g_padUv[2], g_padUv[3]), IM_COL32(150, 150, 135, 255));
    else
        box(dl, ImVec2(px, py), ImVec2(px + pw, py + ph), IM_COL32(21, 22, 15, 255), kRule);
    /* where each element is on the drawing, as fractions of it */
    auto at = [&](float fx, float fy) { return ImVec2(px + fx * pw, py + fy * ph); };
    const ImVec2 ls = at(0.333f, 0.62f), rs = at(0.656f, 0.62f), dp = at(0.24f, 0.33f);
    /* the labels: one row each, a leader line to the element */
    struct Lab { const char* slot; const char* name; bool left; ImVec2 to; };
    const Lab labs[] = {
        { "lt", "LT", true, at(0.27f, 0.0f) }, { "lb", "LB", true, at(0.30f, 0.07f) },
        { "lstick", "Left stick", true, ls }, { "ls", "L3", true, at(0.333f, 0.70f) }, { "dpad", "D-pad", true, dp },
        { "rt", "RT", false, at(0.73f, 0.0f) }, { "rb", "RB", false, at(0.70f, 0.07f) },
        { "y", "Y", false, at(0.733f, 0.222f) }, { "x", "X", false, at(0.656f, 0.333f) },
        { "b", "B", false, at(0.808f, 0.333f) }, { "a", "A", false, at(0.733f, 0.438f) },
        { "rstick", "Right stick", false, rs }, { "rs", "R3", false, at(0.656f, 0.70f) },
    };
    int rowL = 0, rowR = 0;
    for (const Lab& lb : labs) {
        auto it = L.find(lb.slot);
        if (it == L.end() || it->second.empty()) continue;
        const std::string t = std::string(lb.name) + ": " + tr(it->second);
        const float ry = y + 0.6f * u + (lb.left ? rowL++ : rowR++) * 2.9f * u;
        if (lb.left) {
            const ImVec2 ts = textSize(g_body, fs, t.c_str());
            const float lx = px - 3.0f * u;
            text(dl, g_body, fs, ImVec2(lx - ts.x, ry), ink, t.c_str());
            dl->AddLine(ImVec2(lx + 0.4f * u, ry + fs * 0.55f), lb.to, line, 1.0f);
        } else {
            const float lx = px + pw + 3.0f * u;
            text(dl, g_body, fs, ImVec2(lx, ry), ink, t.c_str());
            dl->AddLine(ImVec2(lx - 0.4f * u, ry + fs * 0.55f), lb.to, line, 1.0f);
        }
    }
}

void openSelect(const Control& c) {
    g_selId = c.id;
    g_selCursor = (int)g_state.get(c.id);
    g_selScroll = 0;
    g_selOpenFrame = ImGui::GetFrameCount();
}

void drawSelect(ImDrawList* dl, const Control& c, int fi, float x, float y, float u, bool disabled) {
    const float bh = 3.1f * u, bw = 24.0f * u;
    char id[64];
    snprintf(id, sizeof id, "##%s", c.id.c_str());
    if (hit(id, ImVec2(x, y), ImVec2(bw, bh)) && !disabled) { openSelect(c); g_focus = fi; }
    const bool open = g_selId == c.id;
    box(dl, ImVec2(x, y), ImVec2(x + bw, y + bh), IM_COL32(21, 22, 15, 255), open ? kGold : kRule);
    const int cur = (int)g_state.get(c.id);
    const std::string label = cur >= 0 && cur < (int)c.options.size() ? tr(c.options[cur].label) : std::string();
    text(dl, g_head, 1.55f * u, ImVec2(x + 1.2f * u, y + (bh - 1.55f * u) * 0.5f), disabled ? kDisabled : kInk, label.c_str());
    const ImU32 ac = disabled ? kDisabled : kInk;   /* the little down arrow */
    dl->AddTriangleFilled(ImVec2(x + bw - 2.6f * u, y + bh * 0.4f), ImVec2(x + bw - 1.2f * u, y + bh * 0.4f), ImVec2(x + bw - 1.9f * u, y + bh * 0.64f), ac);
    if (open) { g_selAnchor = ImVec2(x, y); g_selW = bw; g_selH = bh; }
}

}  // namespace

void init(const void* bodyTtf, size_t bodyLen, const void* headTtf, size_t headLen) {
    ImGuiIO& io = ImGui::GetIO();
    ImFontConfig cfg;
    cfg.FontDataOwnedByAtlas = false;   /* the caller keeps the TTF bytes */
    cfg.OversampleH = 2;
    cfg.OversampleV = 2;
    g_body = io.Fonts->AddFontFromMemoryTTF(const_cast<void*>(bodyTtf), (int)bodyLen, 48.0f, &cfg);
    g_head = io.Fonts->AddFontFromMemoryTTF(const_cast<void*>(headTtf), (int)headLen, 72.0f, &cfg);
    io.ConfigFlags &= ~(ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad);   /* the menu has its own focus model */
}

bool setData(const char* json, size_t len, std::string* err) {
    Model m;
    std::string e;
    if (!loadModel(json, len, &m, &e)) {
        g_error = e;
        if (err) *err = e;
        return false;
    }
    g_model = std::move(m);
    /* rebuild the id index against the moved model */
    g_model.byId.clear();
    for (const Tab& t : g_model.tabs)
        for (const Item& it : t.items)
            for (const Control& c : it.controls) g_model.byId[c.id] = &c;
    State fresh;
    fresh.pc = g_state.pc;
    fresh.dev = g_state.dev;
    fresh.reset(g_model);
    for (auto& kv : fresh.v) {   /* keep what was set */
        auto old = g_state.v.find(kv.first);
        if (old != g_state.v.end()) kv.second = old->second;
    }
    g_state = std::move(fresh);
    g_error.clear();
    g_tab = g_tab < (int)g_model.tabs.size() ? g_tab : 0;
    g_entry = 0;
    g_focus = -1;
    return true;
}

void setOwner(int seat) { g_ownerSeat = seat; }

void setPadImage(float u0, float v0, float u1, float v1, float aspect) {
    g_padUv[0] = u0; g_padUv[1] = v0; g_padUv[2] = u1; g_padUv[3] = v1;
    g_padAspect = aspect;
}

void setContext(bool fileSelect, bool inStage, bool updateAvailable, bool profile) {
    g_state.profile = profile;
    g_state.updateAvailable = updateAvailable;
    g_state.fileSelect = fileSelect;
    g_state.inStage = inStage;
}

void showRoot() {
    g_root = true;
    g_rootSel = 0;
    g_rootArmed = -1;
    g_selId.clear();
}

void debugGoto(int tab, int entry, int focus) {
    g_root = false;
    if (tab >= 0 && tab < (int)g_model.tabs.size()) g_tab = tab;
    g_entry = entry < 0 ? 0 : entry;
    g_focus = focus;
}

void setKeyBinding(std::string (*text)(const char*, int), void (*begin)(const char*, int), bool (*active)()) {
    g_keyText = text;
    g_keyBegin = begin;
    g_keyActive = active;
}

void setActionHandler(void (*handler)(const char*)) { g_actionHandler = handler; }

void setBinding(bool (*get)(const char*, double*), void (*set)(const char*, double)) {
    g_bindGet = get;
    g_bindSet = set;
    g_synced.clear();
}

void setOptionLabels(const char* id, const std::vector<std::string>& labels) {
    auto it = g_model.byId.find(id);
    if (it == g_model.byId.end()) return;
    Control* c = const_cast<Control*>(it->second);
    c->options.clear();
    for (size_t i = 0; i < labels.size(); i++) {
        Option o;
        o.id = std::to_string(i);
        o.label = labels[i];
        o.value = (double)i;
        c->options.push_back(o);
    }
}

void setPlatform(bool pc) {
    if (g_state.pc == pc) return;
    g_state.pc = pc;
    g_state.reset(g_model);
    g_entry = 0;
    g_focus = -1;
}
void setDev(bool dev) { g_state.dev = dev; g_entry = 0; g_focus = -1; }
bool isPc() { return g_state.pc; }
bool devOn() { return g_state.dev; }

void drawFooter(ImDrawList* dl, float W, float H, float footH, float u, const char* (*hints)[2], int n) {
    dl->AddRectFilled(ImVec2(0, H - footH), ImVec2(W, H), IM_COL32(13, 14, 12, 208));
    dl->AddLine(ImVec2(0, H - footH), ImVec2(W, H - footH), kGoldDim, 1.0f);
    const float px = 1.75f * u, cy = H - footH * 0.5f - px * 0.5f;
    float x = 2.4f * u;
    for (int i = 0; i < n; i++) {
        const char* const* h = hints[i];
        const ImVec2 ks = textSize(g_head, px, h[0]);
        const float bw = ks.x + 1.2f * u;
        dl->AddRectFilled(ImVec2(x, cy - 0.3f * u), ImVec2(x + bw, cy + px + 0.3f * u), kInk, 0.0f);
        text(dl, g_head, px, ImVec2(x + 0.6f * u, cy), IM_COL32(21, 22, 15, 255), h[0]);
        x += bw + 0.9f * u;
        const std::string label = tr(h[1]);
        text(dl, g_body, px, ImVec2(x, cy), kInk, label.c_str());
        x += textSize(g_body, px, label.c_str()).x + 3.0f * u;
    }
}

/* the first menu: what a pause menu offers, then the options */
void drawRoot(ImDrawList* dl, float W, float H, float u, float hh) {
    static const char* const ids[5] = { "Resume", "Options", "Controllers", "Restart", "Quit" };
    static const char* const actions[5] = { "", "", "__AssignDevices", "__RestartGame", "__QuitToDesktop" };
    std::vector<int> shown;   /* no self-relaunch on the Switch: no Restart there */
    for (int i = 0; i < 5; i++)
        if (i != 3 || g_state.pc) shown.push_back(i);
    const int n = (int)shown.size();
    const float footH = 5.0f * u, bw = 30.0f * u, bh = 5.4f * u, gap = 1.2f * u;
    const bool up = !g_capturing && pressed(ImGuiKey_UpArrow, ImGuiKey_GamepadDpadUp, ImGuiKey_GamepadLStickUp, true);
    const bool down = !g_capturing && pressed(ImGuiKey_DownArrow, ImGuiKey_GamepadDpadDown, ImGuiKey_GamepadLStickDown, true);
    const bool ok = pressed(ImGuiKey_Enter, ImGuiKey_GamepadFaceDown, ImGuiKey_None, false);
    const bool back = pressed(ImGuiKey_Backspace, ImGuiKey_GamepadFaceRight, ImGuiKey_None, false);
    if (g_rootSel >= n) g_rootSel = n - 1;
    if (up && g_rootSel > 0) { g_rootSel--; g_rootArmed = -1; }
    if (down && g_rootSel < n - 1) { g_rootSel++; g_rootArmed = -1; }
    if (back) { g_closeReq = true; return; }   /* B resumes */
    const float total = n * bh + (n - 1) * gap, y0 = hh + (H - hh - footH - total) * 0.45f, x0 = (W - bw) * 0.5f;
    int fire = ok ? g_rootSel : -1;
    for (int k = 0; k < n; k++) {
        const int i = shown[k];
        const ImVec2 a(x0, y0 + k * (bh + gap)), b(a.x + bw, a.y + bh);
        char id[24];
        snprintf(id, sizeof id, "##root%d", i);
        if (hit(id, a, ImVec2(bw, bh))) { g_rootSel = k; fire = k; }
        const bool on = k == g_rootSel, armed = i == g_rootArmed;
        box(dl, a, b, on ? kGoldFill : IM_COL32(21, 22, 15, 255), on ? kGold : kRule);
        if (on) dl->AddRectFilled(ImVec2(a.x, a.y), ImVec2(a.x + 0.4f * u, b.y), kGold);
        std::string label = upper(tr(ids[i]));
        if (armed) label += "?  " + upper(tr("Press again"));
        textCenter(dl, g_head, 2.3f * u, a, b, armed ? kGold : kInk, label.c_str());
    }
    if (fire >= 0) {
        const int i = shown[fire];
        if (i == 0) { g_closeReq = true; return; }
        if (i == 1) { g_root = false; g_rootArmed = -1; return; }
        if (i == 2) { if (g_actionHandler) g_actionHandler(actions[i]); return; }   /* the device screen replaces the menu */
        if (g_rootArmed == i) { g_rootArmed = -1; if (g_actionHandler) g_actionHandler(actions[i]); }   /* a second press confirms */
        else g_rootArmed = i;
    }
}

/* sine and cosine without libm (the game's math.h shadows the system one here) */
void sincosApprox(float a, float* s, float* c) {
    const float pi = 3.14159265f;
    while (a > pi) a -= 2 * pi;
    while (a < -pi) a += 2 * pi;
    const float a2 = a * a;
    *s = a * (1 - a2 / 6 * (1 - a2 / 20 * (1 - a2 / 42 * (1 - a2 / 72))));
    *c = 1 - a2 / 2 * (1 - a2 / 12 * (1 - a2 / 30 * (1 - a2 / 56 * (1 - a2 / 90))));
}

void draw(float W, float H) {
    const float u = W / 100.0f;   /* 1 unit = 1% of the width: the design is laid out in these */
    ImGuiStyle& st = ImGui::GetStyle();
    st.WindowPadding = ImVec2(0, 0);
    st.WindowBorderSize = 0.0f;

    /* the values live in the game: a change made here is stored, then every control reads back what the game holds */
    if (g_bindGet && g_bindSet) {
        for (const Tab& t : g_model.tabs)
            for (const Item& it : t.items)
                for (const Control& c : it.controls) {
                    if (c.key.empty() || c.type == CT_ACTION || c.type == CT_BIND || c.type == CT_DIAGRAM) continue;
                    auto sy = g_synced.find(c.id);
                    const double cur = g_state.get(c.id);
                    if (sy != g_synced.end() && sy->second != cur) g_bindSet(c.key.c_str(), controlToConfig(c, cur));
                    double cfg, ctl;
                    if (g_bindGet(c.key.c_str(), &cfg) && controlFromConfig(c, cfg, &ctl)) g_state.v[c.id] = ctl;
                    g_synced[c.id] = g_state.get(c.id);
                }
    }

    /* a value whose option is no longer offered (the display mode changed) moves to the first one that is */
    for (const Tab& t : g_model.tabs)
        for (const Item& it : t.items)
            for (const Control& c : it.controls) {
                if (c.options.empty() || c.type == CT_SLIDER) continue;
                if (optionVisible(c, (int)g_state.get(c.id), g_model, g_state)) continue;
                const std::vector<int> offered = visibleOptions(c, g_model, g_state);
                if (!offered.empty()) g_state.v[c.id] = offered[0];
            }

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(W, H));
    ImGui::Begin("##menu", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    g_blockHits = !g_selId.empty();

    dl->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(W, H), IM_COL32(40, 43, 34, 235), IM_COL32(26, 28, 22, 240),
                                IM_COL32(16, 17, 14, 245), IM_COL32(26, 28, 22, 240));

    /* ---- header ---- */
    const float hh = 5.6f * u;
    dl->AddRectFilled(ImVec2(0, 0), ImVec2(W, hh), IM_COL32(13, 14, 12, 208));
    dl->AddLine(ImVec2(0, hh), ImVec2(W, hh), kGoldDim, 1.0f);
    {
        const float px = 2.4f * u;
        text(dl, g_head, px, ImVec2(2.4f * u, (hh - px) * 0.5f), kInk, "PROJECT");
        text(dl, g_head, px, ImVec2(2.4f * u + textSize(g_head, px, "PROJECT ").x, (hh - px) * 0.5f), kGold, "DAM");
    }
    const int nTabs = (int)g_model.tabs.size();
    if (nTabs == 0) {   /* the content did not load: say why */
        text(dl, g_body, 1.8f * u, ImVec2(4.0f * u, hh + 3.0f * u), kInk, g_error.empty() ? "No menu content." : g_error.c_str());
        ImGui::End();
        return;
    }
    if (g_tab >= nTabs) g_tab = 0;
    if (!g_root && !g_capturing && (ImGui::IsKeyPressed(ImGuiKey_GamepadL1) || ImGui::IsKeyPressed(ImGuiKey_PageUp))) g_tab = (g_tab + nTabs - 1) % nTabs, g_entry = 0, g_focus = -1;
    if (!g_root && !g_capturing && (ImGui::IsKeyPressed(ImGuiKey_GamepadR1) || ImGui::IsKeyPressed(ImGuiKey_PageDown))) g_tab = (g_tab + 1) % nTabs, g_entry = 0, g_focus = -1;
    {
        const float tw = 12.0f * u, th = 3.6f * u, gap = 0.9f * u;
        float x = (W - (nTabs * tw + (nTabs - 1) * gap)) * 0.5f;
        for (int i = 0; i < (g_root ? 0 : nTabs); i++) {
            char id[16];
            snprintf(id, sizeof id, "##tab%d", i);
            const ImVec2 a(x, (hh - th) * 0.5f), b(x + tw, (hh - th) * 0.5f + th);
            if (hit(id, a, ImVec2(tw, th))) g_tab = i, g_entry = 0, g_focus = -1;
            const bool on = i == g_tab;
            box(dl, a, b, on ? kGoldFill : IM_COL32(21, 22, 15, 255), on ? kGold : kRule);
            if (on) dl->AddRectFilled(ImVec2(a.x, b.y - 0.25f * u), b, kGold);
            textCenter(dl, g_head, 1.9f * u, a, b, kInk, upper(tr(g_model.tabs[i].name)).c_str());
            x += tw + gap;
        }
        const std::string paused = upper(tr("Paused"));
        const ImVec2 ps = textSize(g_head, 1.9f * u, paused.c_str());
        text(dl, g_head, 1.9f * u, ImVec2(W - 2.4f * u - ps.x, (hh - ps.y) * 0.5f), kInk, paused.c_str());
        float rx = W - 2.4f * u - ps.x;   /* left edge of what the header has drawn so far */
        if (g_ownerSeat >= 0) {   /* whose pad drives the menu */
            const std::string who = upper(tr("Player")) + " " + std::to_string(g_ownerSeat + 1);
            const float bw = textSize(g_head, 1.5f * u, who.c_str()).x + 2.4f * u, bx = rx - 1.2f * u - bw;
            box(dl, ImVec2(bx, (hh - 2.6f * u) * 0.5f), ImVec2(bx + bw, (hh + 2.6f * u) * 0.5f), kGoldFill, kGold);
            textCenter(dl, g_head, 1.5f * u, ImVec2(bx, (hh - 2.6f * u) * 0.5f), ImVec2(bx + bw, (hh + 2.6f * u) * 0.5f), kInk, who.c_str());
            rx = bx;
        }
        if (g_state.dev) {   /* a reminder that rows beyond the everyday menu are showing */
            const float bx = rx - 1.2f * u - 5.0f * u;
            box(dl, ImVec2(bx, (hh - 2.6f * u) * 0.5f), ImVec2(bx + 5.0f * u, (hh + 2.6f * u) * 0.5f), IM_COL32(60, 40, 20, 255), kGold);
            textCenter(dl, g_head, 1.5f * u, ImVec2(bx, (hh - 2.6f * u) * 0.5f), ImVec2(bx + 5.0f * u, (hh + 2.6f * u) * 0.5f), kGold, "DEV");
        }
    }

    if (g_root) {
        drawRoot(dl, W, H, u, hh);
        static const char* rootHints[2][2] = { { "A", "Select" }, { "B", "Resume" } };
        drawFooter(dl, W, H, 5.0f * u, u, rootHints, 2);
        ImGui::End();
        return;
    }

    /* ---- what is visible, and the navigation ---- */
    const Tab& tab = g_model.tabs[g_tab];
    Vis vis = visibleOf(tab);
    if (vis.entries.empty()) {
        text(dl, g_body, 1.8f * u, ImVec2(4.0f * u, hh + 3.0f * u), kMut, tr("Nothing to show on this tab.").c_str());
        ImGui::End();
        return;
    }
    if (g_entry >= (int)vis.entries.size()) g_entry = (int)vis.entries.size() - 1;
    auto controlsOf = [&](int visEntry) {
        std::vector<const Control*> cs;
        for (const Control& c : tab.items[vis.entries[visEntry]].controls)
            if (controlVisible(c, g_model, g_state)) cs.push_back(&c);
        return cs;
    };
    vis.controls = controlsOf(g_entry);
    if (g_focus >= (int)vis.controls.size()) g_focus = (int)vis.controls.size() - 1;
    {
        g_capturing = g_keyActive && g_keyActive();
        bool up = pressed(ImGuiKey_UpArrow, ImGuiKey_GamepadDpadUp, ImGuiKey_GamepadLStickUp, true);
        bool down = pressed(ImGuiKey_DownArrow, ImGuiKey_GamepadDpadDown, ImGuiKey_GamepadLStickDown, true);
        bool left = pressed(ImGuiKey_LeftArrow, ImGuiKey_GamepadDpadLeft, ImGuiKey_GamepadLStickLeft, true);
        bool right = pressed(ImGuiKey_RightArrow, ImGuiKey_GamepadDpadRight, ImGuiKey_GamepadLStickRight, true);
        bool ok = pressed(ImGuiKey_Enter, ImGuiKey_GamepadFaceDown, ImGuiKey_None, false);
        bool back = pressed(ImGuiKey_Backspace, ImGuiKey_GamepadFaceRight, ImGuiKey_None, false);
        if (g_capturing) up = down = left = right = ok = back = false;
        if (!g_selId.empty()) {   /* an open list takes the keys: up / down move in it, A picks, B closes it */
            auto sit = g_model.byId.find(g_selId);
            const std::vector<int> offered = sit == g_model.byId.end() ? std::vector<int>() : visibleOptions(*sit->second, g_model, g_state);
            const int n = (int)offered.size();
            int pos = 0;
            for (int k = 0; k < n; k++) if (offered[k] == g_selCursor) pos = k;
            if (up && pos > 0) g_selCursor = offered[pos - 1];
            if (down && pos < n - 1) g_selCursor = offered[pos + 1];
            if (ok && n > 0) { g_state.v[g_selId] = g_selCursor; g_selId.clear(); }
            else if (back) g_selId.clear();
        } else if (g_focus < 0) {
            if (up && g_entry > 0) { g_entry--; g_scrollTarget = 0; }
            if (down && g_entry < (int)vis.entries.size() - 1) { g_entry++; g_scrollTarget = 0; }
            if ((right || ok) && !controlsOf(g_entry).empty()) g_focus = 0;
            if (back) { g_root = true; g_rootSel = 1; g_rootArmed = -1; }
            vis.controls = controlsOf(g_entry);
        } else {
            if (up && g_focus > 0) g_focus--;
            if (down && g_focus < (int)vis.controls.size() - 1) g_focus++;
            const Control& c = *vis.controls[g_focus];
            const bool dis = controlDisabled(c, g_model, g_state);
            if (!dis && c.type == CT_BIND) {
                if (left) g_slotSel = 0;
                if (right) g_slotSel = 1;
                if (ok) startCapture(c, g_slotSel);
            } else if (!dis) {
                if (left) changeValue(c, -1);
                if (right) changeValue(c, +1);
                if (ok && c.type == CT_SELECT) openSelect(c);
                else if (ok && c.type != CT_SLIDER) changeValue(c, +1);
            }
            if (back && g_selId.empty()) g_focus = -1;
        }
    }

    const float footH = 5.0f * u, bodyTop = hh, bodyBot = H - footH;
    const float leftX = 3.1f * u, leftW = 28.0f * u, divX = leftX + leftW + 1.8f * u;
    dl->AddLine(ImVec2(divX, bodyTop + 1.2f * u), ImVec2(divX, bodyBot - 1.2f * u), kGoldDim, 1.0f);

    /* ---- the entries, with their group headings ---- */
    {
        float y = bodyTop + 2.2f * u;
        const float eh = 3.4f * u;
        int visIdx = 0;
        for (size_t i = 0; i < tab.items.size(); i++) {
            const Item& it = tab.items[i];
            if (it.group) {
                bool anyBelow = false;   /* a heading with no visible entry under it is not drawn */
                for (size_t j = i + 1; j < tab.items.size() && !tab.items[j].group; j++)
                    if (itemVisible(tab.items[j], g_model, g_state)) { anyBelow = true; break; }
                if (anyBelow && itemVisible(it, g_model, g_state)) {
                    text(dl, g_head, 1.3f * u, ImVec2(leftX, y + 0.4f * u), kMut, upper(tr(it.name)).c_str());
                    y += 2.4f * u;
                }
                continue;
            }
            if (!itemVisible(it, g_model, g_state)) continue;
            const float ind = it.sub ? 2.2f * u : 0.0f;
            const float h = it.sub ? 3.1f * u : eh;
            char id[32];
            snprintf(id, sizeof id, "##ent%d", (int)i);
            if (hit(id, ImVec2(leftX + ind, y), ImVec2(leftW - ind, h))) { g_entry = visIdx; g_focus = -1; g_scrollTarget = 0; }
            const bool on = visIdx == g_entry;
            if (it.sub) dl->AddLine(ImVec2(leftX + ind - 1.3f * u, y + h * 0.5f), ImVec2(leftX + ind - 0.3f * u, y + h * 0.5f), kRule, 1.0f);
            box(dl, ImVec2(leftX + ind, y), ImVec2(leftX + leftW, y + h), on ? (g_focus >= 0 ? kGoldSoft : kGoldFill) : kRow, on ? kGold : kRule);
            if (on) dl->AddRectFilled(ImVec2(leftX + ind, y), ImVec2(leftX + ind + 0.45f * u, y + h), kGold);
            const float px = it.sub ? 1.6f * u : 1.75f * u;
            text(dl, g_body, px, ImVec2(leftX + ind + 1.6f * u, y + (h - px) * 0.5f - 0.15f * u), kInk, tr(it.name).c_str());
            y += h + 0.5f * u;
            visIdx++;
        }
    }

    /* ---- the form of the selected entry, scrolled to keep the focused control in view ---- */
    {
        const Item& e = tab.items[vis.entries[g_entry]];
        const float fx = divX + 2.6f * u, fw = W - fx - 2.6f * u;
        float y = bodyTop + 2.2f * u;
        text(dl, g_head, 2.9f * u, ImVec2(fx, y), kInk, upper(tr(e.name)).c_str());
        y += 3.6f * u;
        dl->AddLine(ImVec2(fx, y), ImVec2(fx + fw, y), kGold, 1.0f);
        y += 1.0f * u;
        const float top = y, bottom = bodyBot - 1.4f * u;

        const float cx0 = fx + 20.5f * u, availW = fx + fw - cx0 - 1.0f * u;
        struct Row { float y, h, helpH; };
        std::vector<Row> rows;
        float cy = 0;
        for (const Control* c : vis.controls) {
            const std::string help = tr(helpFor(*c, g_state.get(c->id)));
            float rowH = 4.6f * u;
            if (c->type == CT_DIAGRAM) rowH = 27.0f * u;
            if (c->type == CT_CHOICE) {
                std::vector<Slot> slots;
                const float ch = layoutChoice(*c, 0, availW, u, &slots);
                rowH = maxf(rowH, ch + 1.5f * u);
            }
            const float helpH = help.empty() ? 0.0f : g_body->CalcTextSizeA(1.4f * u, 1e9f, fw - 2.4f * u, help.c_str()).y + 0.6f * u;   /* wrapped */
            const float total = rowH + helpH;
            rows.push_back({ cy, total, helpH });
            cy += total + 0.4f * u;
        }
        if (g_focus >= 0 && g_focus < (int)rows.size()) {   /* keep the focused row in view */
            const float rt = rows[g_focus].y, rb = rt + rows[g_focus].h;
            if (rt < g_scrollTarget) g_scrollTarget = rt;
            if (rb > g_scrollTarget + (bottom - top)) g_scrollTarget = rb - (bottom - top);
        }
        const float maxScroll = maxf(0.0f, cy - (bottom - top));
        ImGuiIO& io = ImGui::GetIO();
        if (io.MouseWheel != 0 && io.MousePos.x > divX && io.MousePos.y > top && io.MousePos.y < bottom) g_scrollTarget -= io.MouseWheel * 6.0f * u;
        if (g_scrollTarget > maxScroll) g_scrollTarget = maxScroll;
        if (g_scrollTarget < 0) g_scrollTarget = 0;
        g_scroll += (g_scrollTarget - g_scroll) * 0.35f;
        if ((g_scrollTarget - g_scroll) * (g_scrollTarget - g_scroll) < 0.25f) g_scroll = g_scrollTarget;

        ImGui::PushClipRect(ImVec2(fx, top), ImVec2(fx + fw, bottom), true);
        for (size_t i = 0; i < vis.controls.size(); i++) {
            const Control& c = *vis.controls[i];
            const int fi = (int)i;
            const bool dis = controlDisabled(c, g_model, g_state);
            const float y0 = top + rows[i].y - g_scroll, rowH = rows[i].h;
            if (y0 + rowH < top || y0 > bottom) continue;
            const std::string help = tr(helpFor(c, g_state.get(c.id)));
            const float baseH = rowH - rows[i].helpH;
            if (g_focus == fi) dl->AddRectFilled(ImVec2(fx, y0), ImVec2(fx + fw, y0 + rowH), kGoldSoft);
            const std::string lab = (c.type == CT_ACTION || c.type == CT_DIAGRAM) ? std::string() : tr(c.label);
            text(dl, g_head, 1.7f * u, ImVec2(fx + 1.2f * u, y0 + (4.6f * u - 1.7f * u) * 0.5f), dis ? kDisabled : kInk, lab.c_str());
            std::string tagText = c.tag.empty() ? std::string() : tr(c.tag);
            if (c.restart) tagText += (tagText.empty() ? "" : " ") + tr("(restart)");
            if (dis && !c.disabledTag.empty()) tagText += (tagText.empty() ? "" : " ") + tr(c.disabledTag);
            if (!tagText.empty())
                text(dl, g_body, 1.15f * u, ImVec2(fx + 1.2f * u + textSize(g_head, 1.7f * u, lab.c_str()).x + 0.7f * u, y0 + (4.6f * u - 1.15f * u) * 0.5f), kMut, tagText.c_str());
            const float cy2 = y0 + (4.6f * u - 3.1f * u) * 0.5f;
            if (c.type == CT_CHOICE) drawChoice(dl, c, fi, cx0, cy2, availW, u, dis);
            else if (c.type == CT_SELECT) drawSelect(dl, c, fi, cx0, cy2, u, dis);
            else if (c.type == CT_TOGGLE) drawToggle(dl, c, fi, cx0, y0 + (4.6f * u - 2.1f * u) * 0.5f, u, dis);
            else if (c.type == CT_SLIDER) drawSlider(dl, c, fi, cx0, y0 + (4.6f * u - 2.6f * u) * 0.5f, u, dis);
            else if (c.type == CT_BIND) drawBind(dl, c, fi, cx0, cy2, u, dis);
            else if (c.type == CT_DIAGRAM) drawDiagram(dl, c, fx + 1.0f * u, y0, fw - 2.0f * u, u);   /* the whole width of the form */
            else drawAction(dl, c, fi, cx0, cy2, u, dis);
            if (!help.empty()) dl->AddText(g_body, 1.4f * u, ImVec2(fx + 1.2f * u, y0 + baseH - 0.2f * u), kMut, help.c_str(), nullptr, fw - 2.4f * u);
            dl->AddLine(ImVec2(fx, y0 + rowH + 0.2f * u), ImVec2(fx + fw, y0 + rowH + 0.2f * u), kLine, 1.0f);
        }
        ImGui::PopClipRect();
        if (maxScroll > 0.0f) {   /* a thin scrollbar */
            const float sh = (bottom - top) * (bottom - top) / (cy > 0 ? cy : 1), sy = top + (bottom - top - sh) * (g_scroll / maxScroll);
            dl->AddRectFilled(ImVec2(fx + fw + 0.6f * u, top), ImVec2(fx + fw + 0.9f * u, bottom), IM_COL32(58, 59, 51, 255));
            dl->AddRectFilled(ImVec2(fx + fw + 0.6f * u, sy), ImVec2(fx + fw + 0.9f * u, sy + sh), kGold);
        }
    }

    /* ---- footer ---- */
    {
        static const char* hints[4][2] = { { "A", "Select" }, { "<  >", "Change" }, { "L / R", "Tabs" }, { "B", "Back" } };
        drawFooter(dl, W, H, footH, u, hints, 4);
    }
    if (!g_selId.empty()) {   /* the open list, drawn last so it sits over everything */
        auto sit = g_model.byId.find(g_selId);
        if (sit == g_model.byId.end() || g_selW <= 0) {
            g_selId.clear();
        } else {
            const Control& c = *sit->second;
            const std::vector<int> offered = visibleOptions(c, g_model, g_state);
            const int n = (int)offered.size();
            const float ih = 3.1f * u;
            /* the list opens where there is more room, and shows as many rows as fit between the header and the footer */
            const float below = (H - footH) - (g_selAnchor.y + g_selH + 0.3f * u), above = g_selAnchor.y - hh - 0.3f * u;
            const bool up = (n * ih + 0.6f * u) > below && above > below;
            const float room = (up ? above : below) - 0.6f * u;
            int vis = n < 8 ? n : 8;
            const int fit = (int)(room / ih);
            if (vis > fit) vis = fit < 3 ? (n < 3 ? n : 3) : fit;
            const float ph = vis * ih + 0.6f * u;
            float py = up ? g_selAnchor.y - ph - 0.3f * u : g_selAnchor.y + g_selH + 0.3f * u;
            if (py < hh + 0.2f * u) py = hh + 0.2f * u;
            if (py + ph > H - footH - 0.2f * u) py = H - footH - 0.2f * u - ph;
            const ImVec2 a(g_selAnchor.x, py), b(g_selAnchor.x + g_selW, py + ph);
            dl->AddRectFilled(a, b, IM_COL32(13, 14, 12, 250));
            dl->AddRect(a, b, kGold, 0.0f, 0, 1.0f);
            int curPos = 0;   /* the cursor and the scroll count offered options, not list positions */
            for (int k = 0; k < n; k++) if (offered[k] == g_selCursor) curPos = k;
            if (curPos < g_selScroll) g_selScroll = curPos;
            if (curPos >= g_selScroll + vis) g_selScroll = curPos - vis + 1;
            const float wheel = ImGui::GetIO().MouseWheel;
            if (wheel > 0 && g_selScroll > 0) g_selScroll--;
            if (wheel < 0 && g_selScroll < n - vis) g_selScroll++;
            const int curVal = (int)g_state.get(c.id);
            for (int k = 0; k < vis; k++) {
                const int i = offered[g_selScroll + k];
                const ImVec2 ia(a.x + 0.3f * u, a.y + 0.3f * u + k * ih), ib(b.x - 0.3f * u, ia.y + ih);
                char id[64];
                snprintf(id, sizeof id, "##opt_%d", i);
                ImGui::SetCursorScreenPos(ia);
                if (ImGui::InvisibleButton(id, ImVec2(ib.x - ia.x, ih))) { g_state.v[c.id] = i; g_selId.clear(); }
                if (ImGui::IsItemHovered() && ImGui::GetIO().MouseDelta.x * ImGui::GetIO().MouseDelta.x + ImGui::GetIO().MouseDelta.y * ImGui::GetIO().MouseDelta.y > 0) g_selCursor = i;
                if (i == g_selCursor) dl->AddRectFilled(ia, ib, kGoldFill);
                text(dl, g_head, 1.55f * u, ImVec2(ia.x + 0.9f * u, ia.y + (ih - 1.55f * u) * 0.5f), i == curVal ? kGold : kInk, tr(c.options[i].label).c_str());
            }
            if (n > vis) {   /* a thin scrollbar on the list */
                const float sh = (ph - 0.6f * u) * vis / n, sy = a.y + 0.3f * u + (ph - 0.6f * u - sh) * (g_selScroll / (float)(n - vis));
                dl->AddRectFilled(ImVec2(b.x - 0.5f * u, sy), ImVec2(b.x - 0.25f * u, sy + sh), kGold);
            }
            const ImVec2 m = ImGui::GetIO().MousePos;
            if (ImGui::IsMouseClicked(0) && ImGui::GetFrameCount() != g_selOpenFrame && !(m.x >= a.x && m.x <= b.x && m.y >= a.y && m.y <= b.y))
                g_selId.clear();   /* a click outside closes it */
        }
    }
    ImGui::End();
}

bool takeCloseRequest() {
    const bool r = g_closeReq;
    g_closeReq = false;
    return r;
}

const char* debugState() {
    static char buf[192];
    if (g_model.tabs.empty()) { snprintf(buf, sizeof buf, "no content: %s", g_error.c_str()); return buf; }
    const Tab& tab = g_model.tabs[g_tab < (int)g_model.tabs.size() ? g_tab : 0];
    Vis vis = visibleOf(tab);
    if (vis.entries.empty()) { snprintf(buf, sizeof buf, "tab=%s (empty)", tab.name.c_str()); return buf; }
    const int ei = g_entry < (int)vis.entries.size() ? g_entry : 0;
    const Item& e = tab.items[vis.entries[ei]];
    std::vector<const Control*> cs;
    for (const Control& c : e.controls)
        if (controlVisible(c, g_model, g_state)) cs.push_back(&c);
    std::string fv = "-";
    if (g_focus >= 0 && g_focus < (int)cs.size()) {
        const Control& c = *cs[g_focus];
        fv = c.id + "=" + (hasOptions(c) ? c.options[(int)g_state.get(c.id)].id : std::to_string((int)g_state.get(c.id)));
        if (!g_selId.empty()) fv += " (list open, cursor " + std::to_string(g_selCursor) + ")";
    }
    snprintf(buf, sizeof buf, "tab=%s entry=%s controls=%d focus=%s close=%d", tab.name.c_str(), e.name.c_str(), (int)cs.size(), fv.c_str(), g_closeReq ? 1 : 0);
    return buf;
}

}  // namespace menu

namespace menu {

void drawWheel(float W, float H, int seat, float x0, float y0, float x1, float y1, const char* title, const std::vector<std::string>& names, int sel) {
    if (names.empty() || !g_head) return;
    const float pi = 3.14159265f;
    const float rw = (x1 - x0) * W, rh = (y1 - y0) * H, u = (rw < rh ? rw : rh) / 60.0f;   /* sized to the player's own picture */
    char wid[16];
    snprintf(wid, sizeof wid, "##wheel%d", seat);
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(W, H));
    ImGui::Begin(wid, nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoInputs |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 c(x0 * W + rw * 0.5f, y0 * H + rh * 0.5f);   /* the middle of that picture */
    const float R = 24.0f * u, r = 9.0f * u, mid = (R + r) * 0.5f, thick = R - r;
    const int n = (int)names.size();
    dl->AddCircleFilled(c, R + 1.2f * u, IM_COL32(13, 14, 12, 150), 64);
    if (n == 1) {   /* one item: a full ring, there is nothing to pick between */
        dl->AddCircle(c, mid, sel == 0 ? kGoldFill : IM_COL32(21, 22, 15, 235), 72, thick);
        dl->AddCircle(c, R, sel == 0 ? kGold : kRule, 72, 1.0f);
        dl->AddCircle(c, r, sel == 0 ? kGold : kRule, 72, 1.0f);
    }
    for (int i = 0; i < n; i++) {
        const float th = 2 * pi * i / n, half = pi / n - 0.02f;   /* from the top, clockwise */
        const bool on = i == sel;
        if (n > 1) {
            dl->PathArcTo(c, mid, th - half - pi * 0.5f, th + half - pi * 0.5f, 24);
            dl->PathStroke(on ? kGoldFill : IM_COL32(21, 22, 15, 235), 0, thick);
            dl->PathArcTo(c, R, th - half - pi * 0.5f, th + half - pi * 0.5f, 24);
            dl->PathStroke(on ? kGold : kRule, 0, 1.0f);
        }
        float s, co;
        sincosApprox(th, &s, &co);
        const std::string lab = tr(names[i]);
        const ImVec2 ts = textSize(g_head, 1.6f * u, lab.c_str());
        text(dl, g_head, 1.6f * u, ImVec2(c.x + mid * s - ts.x * 0.5f, c.y - mid * co - ts.y * 0.5f), on ? kInk : kMut, lab.c_str());
    }
    const std::string t = upper(tr(title));
    const ImVec2 tz = textSize(g_head, 1.9f * u, t.c_str());
    text(dl, g_head, 1.9f * u, ImVec2(c.x - tz.x * 0.5f, c.y - tz.y * 0.5f), kGold, t.c_str());
    ImGui::End();
}

void drawAssign(float W, float H, const std::vector<std::string>& names, int step, int count) {
    if (!g_head) return;
    const float u = W / 100.0f;
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(W, H));
    ImGui::Begin("##assign", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoInputs);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), IM_COL32(13, 14, 12, 235));
    const float pw = 52.0f * u, rowH = 5.4f * u, gap = 1.0f * u;
    const float ph = 9.0f * u + count * (rowH + gap), x0 = (W - pw) * 0.5f, y0 = (H - ph) * 0.5f;
    box(dl, ImVec2(x0, y0), ImVec2(x0 + pw, y0 + ph), IM_COL32(21, 22, 15, 255), kGold);
    const std::string head = upper(tr("Who plays with what"));
    text(dl, g_head, 2.4f * u, ImVec2(x0 + 2.0f * u, y0 + 1.6f * u), kInk, head.c_str());
    for (int i = 0; i < count; i++) {
        const ImVec2 a(x0 + 2.0f * u, y0 + 6.0f * u + i * (rowH + gap)), b(x0 + pw - 2.0f * u, a.y + rowH);
        const bool now = i == step, done = i < step;
        box(dl, a, b, now ? kGoldFill : IM_COL32(13, 14, 12, 255), now ? kGold : kRule);
        const std::string who = upper(tr("Player")) + " " + std::to_string(i + 1);
        text(dl, g_head, 1.9f * u, ImVec2(a.x + 1.4f * u, a.y + (rowH - 1.9f * u) * 0.5f), kInk, who.c_str());
        const std::string what = done && i < (int)names.size() ? names[i] : (now ? tr("Press a button") : std::string("-"));
        const ImVec2 ws = textSize(g_body, 1.7f * u, what.c_str());
        text(dl, g_body, 1.7f * u, ImVec2(b.x - 1.4f * u - ws.x, a.y + (rowH - ws.y) * 0.5f), done ? kGold : kMut, what.c_str());
    }
    const std::string hint = tr(step == 0 ? "A button takes that controller, any key takes the keyboard and mouse. Esc skips." : "Press a button on the next controller. Esc skips.");
    text(dl, g_body, 1.4f * u, ImVec2(x0 + 2.0f * u, y0 + ph - 2.6f * u), kMut, hint.c_str());
    ImGui::End();
}

}  // namespace menu
