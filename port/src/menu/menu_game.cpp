/* menu_game.cpp -- the new menu inside the game: the glue between menu_ui.cpp (Dear ImGui only) and the port.
 *
 * Each frame, while the options overlay is open and GE_NEWMENU=1, it feeds the game's input to ImGui, runs
 * menu::draw and hands the triangles to fast3d (port/fast3d/gfx_ui.h), which draws them through the rendering
 * API (OpenGL, Vulkan, deko3d). The old overlay keeps its logic; only its drawing and navigation are replaced.
 *
 * Buttons are used by label: A confirms and B cancels on every pad. ImGui's "face down" (activate) is fed from
 * the pad's A and "face right" (cancel) from its B, so the confirm button is the bottom one on an Xbox pad and
 * the right one on a Switch pad, as on each console. */
#include <PR/gbi.h>
#include <SDL.h>
#include "../../third_party/imgui/imgui.h"
#include "../../fast3d/gfx_api.h"
#include "../../fast3d/gfx_ui.h"
#include "menu_ui.h"
#include "menu_model.h"
#include "menu_fonts.h"
#include "menu_data.h"
#include "menu_pad.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

extern "C" {
int inputPadButton(int idx, SDL_GameControllerButton b);
short inputPadAxis(int idx, SDL_GameControllerAxis a);
int inputOverlayOwnerPad(void);
int inputOverlayOwnerSeat(void);
int portDevMode(void);
const char* portMenuLanguage(void);
const char* sysResolvePath(const char* path);
int portWheelState(int seat, char names[][24], int max, int* sel, int* kind);
void portSeatRegion(int seat, float* x0, float* y0, float* x1, float* y1);
int portAssignState(char names[][48], int* step, int* count);
void optionsMenuSetSeat(int seat);
int optionsMenuFileSelect(void);
int optionsMenuInStage(void);
int optionsMenuUpdateAvailable(void);
int optionsMenuUpdateState(void);
const char* optionsMenuUpdateTag(void);
const char* optionsMenuUpdateNotes(void);
const char* optionsMenuVersion(void);
int optionsMenuUpdateInstallable(void);
int optionsMenuUpdateBusy(void);
const char* optionsMenuUpdateStatus(void);
int optionsMenuWheel(void);
float portFovHorizDegrees(int pct);
void portCrosshairTint(int* r, int* g, int* b);
int portCrosshairPicture(int style, const unsigned char** px);
int inputCrosshairCursorOn(void);
int optionsMenuProfileAvailable(void);
}

namespace {

SDL_atomic_t s_back;   // Esc pressed while the menu is open
ImGuiContext* s_ctx = nullptr;
std::vector<GfxUiVertex> s_vtx;
std::vector<uint32_t> s_idx;
std::vector<GfxUiCmd> s_cmd;
Uint64 s_lastTicks = 0;

extern "C" void menuBindGame(void);

void init() {
    IMGUI_CHECKVERSION();
    s_ctx = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
    io.MouseDrawCursor = false;   /* the pointer is drawn by menu::drawPointer, only when Input.CrosshairCursor is on */
    menu::init(kMenuBodyTtf, kMenuBodyTtfLen, kMenuHeadTtf, kMenuHeadTtfLen);
#ifdef __SWITCH__
    menu::setPlatform(false);
#else
    menu::setPlatform(true);
#endif
    menu::setDev(portDevMode() != 0);   /* Dev.Mode in the ini */
    {
        std::string err;
        if (!menu::setData(kMenuJson, kMenuJsonLen, &err)) fputs(("menu: " + err + "\n").c_str(), stderr);
    }
    menuBindGame();
    if (const char* lang = portMenuLanguage(); lang && lang[0]) {   /* Menu.Language: a flat table, English text -> translated text */
        const std::string path = std::string("lang/") + lang + ".json";
        if (FILE* f = fopen(sysResolvePath(path.c_str()), "rb")) {
            std::string text;
            char buf[4096];
            size_t n;
            while ((n = fread(buf, 1, sizeof buf, f)) > 0) text.append(buf, n);
            fclose(f);
            std::string err;
            if (!menu::loadTranslations(text.c_str(), text.size(), &err)) fputs(("menu: " + path + ": " + err + "\n").c_str(), stderr);
        }
    }
    if (const char* g = getenv("GE_MENUGOTO")) {   /* tests: "tab,entry,focus" */
        int t = 0, e = 0, f = -1;
        if (sscanf(g, "%d,%d,%d", &t, &e, &f) >= 2) menu::debugGoto(t, e, f);
    }
    const int padRect = io.Fonts->AddCustomRectRegular(kMenuPadW, kMenuPadH);   /* the gamepad drawing, in the same texture */
    unsigned char* px = nullptr;
    int w = 0, h = 0;
    io.Fonts->GetTexDataAsRGBA32(&px, &w, &h);
    if (const ImFontAtlasCustomRect* r = io.Fonts->GetCustomRectByIndex(padRect)) {
        for (int y = 0; y < kMenuPadH; y++)
            for (int x = 0; x < kMenuPadW; x++) {
                unsigned char* d = px + ((size_t)(r->Y + y) * (size_t)w + (size_t)(r->X + x)) * 4;
                d[0] = d[1] = d[2] = 255;
                d[3] = kMenuPad[y * kMenuPadW + x];
            }
        menu::setPadImage((float)r->X / (float)w, (float)r->Y / (float)h, (float)(r->X + kMenuPadW) / (float)w,
                          (float)(r->Y + kMenuPadH) / (float)h, (float)kMenuPadW / (float)kMenuPadH);
    }
    io.Fonts->SetTexID((ImTextureID)(intptr_t)1);
    gfx_ui_set_atlas(px, w, h);
}

void feedMouse(ImGuiIO& io, float W, float H);

void feedInput(ImGuiIO& io, float W, float H) {
    const int pad = inputOverlayOwnerPad();
    auto btn = [&](SDL_GameControllerButton b) { return inputPadButton(pad, b) != 0; };
    io.AddKeyEvent(ImGuiKey_GamepadDpadUp, btn(SDL_CONTROLLER_BUTTON_DPAD_UP));
    io.AddKeyEvent(ImGuiKey_GamepadDpadDown, btn(SDL_CONTROLLER_BUTTON_DPAD_DOWN));
    io.AddKeyEvent(ImGuiKey_GamepadDpadLeft, btn(SDL_CONTROLLER_BUTTON_DPAD_LEFT));
    io.AddKeyEvent(ImGuiKey_GamepadDpadRight, btn(SDL_CONTROLLER_BUTTON_DPAD_RIGHT));
    io.AddKeyEvent(ImGuiKey_GamepadFaceDown, btn(SDL_CONTROLLER_BUTTON_A));    /* confirm: the A label */
    io.AddKeyEvent(ImGuiKey_GamepadFaceRight, btn(SDL_CONTROLLER_BUTTON_B));   /* cancel: the B label */
    io.AddKeyEvent(ImGuiKey_GamepadFaceLeft, btn(SDL_CONTROLLER_BUTTON_X));
    io.AddKeyEvent(ImGuiKey_GamepadFaceUp, btn(SDL_CONTROLLER_BUTTON_Y));
    io.AddKeyEvent(ImGuiKey_GamepadL1, btn(SDL_CONTROLLER_BUTTON_LEFTSHOULDER));
    io.AddKeyEvent(ImGuiKey_GamepadR1, btn(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER));
    const float ax = inputPadAxis(pad, SDL_CONTROLLER_AXIS_LEFTX) / 32767.0f;
    const float ay = inputPadAxis(pad, SDL_CONTROLLER_AXIS_LEFTY) / 32767.0f;
    io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickLeft, ax < -0.3f, ax < -0.3f ? -ax : 0.0f);
    io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickRight, ax > 0.3f, ax > 0.3f ? ax : 0.0f);
    io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickUp, ay < -0.3f, ay < -0.3f ? -ay : 0.0f);
    io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickDown, ay > 0.3f, ay > 0.3f ? ay : 0.0f);

    const Uint8* ks = SDL_GetKeyboardState(nullptr);
    io.AddKeyEvent(ImGuiKey_UpArrow, ks[SDL_SCANCODE_UP] != 0);
    io.AddKeyEvent(ImGuiKey_DownArrow, ks[SDL_SCANCODE_DOWN] != 0);
    io.AddKeyEvent(ImGuiKey_LeftArrow, ks[SDL_SCANCODE_LEFT] != 0);
    io.AddKeyEvent(ImGuiKey_RightArrow, ks[SDL_SCANCODE_RIGHT] != 0);
    io.AddKeyEvent(ImGuiKey_Enter, ks[SDL_SCANCODE_RETURN] != 0 || ks[SDL_SCANCODE_KP_ENTER] != 0);
    {   /* Backspace and Esc go back: Esc arrives as an event (menuPostBack), held for two frames so the menu sees a press */
        static int escPulse = 0;
        if (SDL_AtomicSet(&s_back, 0)) escPulse = 2;
        io.AddKeyEvent(ImGuiKey_Backspace, ks[SDL_SCANCODE_BACKSPACE] != 0 || escPulse > 0);
        if (escPulse > 0) escPulse--;
    }
    io.AddKeyEvent(ImGuiKey_PageUp, ks[SDL_SCANCODE_PAGEUP] != 0);
    io.AddKeyEvent(ImGuiKey_PageDown, ks[SDL_SCANCODE_PAGEDOWN] != 0);

    feedMouse(io, W, H);
}

void feedMouse(ImGuiIO& io, float W, float H) {
    int mx = 0, my = 0;
    const Uint32 mb = SDL_GetMouseState(&mx, &my);
    SDL_Window* win = SDL_GetMouseFocus();
    int ww = 0, wh = 0;
    if (win) {
        SDL_GetWindowSize(win, &ww, &wh);
        if (ww > 0 && wh > 0) io.AddMousePosEvent((float)mx * W / (float)ww, (float)my * H / (float)wh);
    } else {
        io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    }
    io.AddMouseButtonEvent(0, (mb & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0);
}

}  // namespace

extern "C" void menuPostBack(void) { SDL_AtomicSet(&s_back, 1); }

extern "C" int menuNewActive(void) {   /* the menu is this one: the C-era overlay no longer draws or reads input */
    return 1;
}

extern "C" int coopModalActive(void);
extern "C" int coopModalSel(void);
extern "C" void coopModalPointer(int row, int click);

static void runFrame(int mode) {   /* 0 the menu, 1 the wheel, 2 the device assignment, 3 the co-op player count */
    const bool wheel = mode == 1;
    if (!s_ctx) init();
    const float W = (float)gfx_current_dimensions.width, H = (float)gfx_current_dimensions.height;
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(W, H);
    const Uint64 now = SDL_GetTicks64();
    io.DeltaTime = s_lastTicks ? (float)(now - s_lastTicks) / 1000.0f : 1.0f / 60.0f;
    if (io.DeltaTime <= 0.0f || io.DeltaTime > 0.25f) io.DeltaTime = 1.0f / 60.0f;
    s_lastTicks = now;
    if (mode == 3) feedMouse(io, W, H);
    if (mode == 0) {
        menu::setOwner(inputOverlayOwnerSeat());
        optionsMenuSetSeat(inputOverlayOwnerSeat());
        menu::setContext(optionsMenuFileSelect() != 0, optionsMenuInStage() != 0, optionsMenuUpdateAvailable() != 0, optionsMenuProfileAvailable() != 0);
        menu::setUpdate(optionsMenuUpdateState(), optionsMenuUpdateTag(), optionsMenuUpdateNotes(), optionsMenuVersion(),
                        optionsMenuUpdateInstallable() != 0, optionsMenuUpdateBusy() != 0, optionsMenuUpdateStatus());
        feedInput(io, W, H);
        io.AddMouseWheelEvent(0.0f, (float)optionsMenuWheel());   /* the mouse wheel: the lists and the release text scroll */
    }
    ImGui::NewFrame();
    if (mode == 3) {   /* the mouse picks a row too: hovering moves the selection, a click starts */
        int row = -1;
        bool click = false;
        menu::drawCoopCount(W, H, coopModalSel(), &row, &click);
        coopModalPointer(row, click ? 1 : 0);
        menu::drawPointer(inputCrosshairCursorOn() != 0, W, H);
    } else if (mode == 2) {   /* who plays with which device */
        char an[4][48];
        int astep = 0, acount = 0;
        std::vector<std::string> list;
        if (portAssignState(an, &astep, &acount)) {
            for (int i = 0; i < 4; i++) list.push_back(an[i]);
            menu::drawAssign(W, H, list, astep, acount);
        }
    } else if (mode == 0) {
        menu::draw(W, H);
        menu::drawPointer(inputCrosshairCursorOn() != 0, W, H);
    } else {   /* the Modern weapon / gadget wheel, over the running game */
        for (int seat = 0; seat < 4; seat++) {   /* every player with a wheel open, in that player's own picture */
            char names[16][24];
            int sel = -1, kind = 0;
            const int n = portWheelState(seat, names, 16, &sel, &kind);
            if (n <= 0) continue;
            float x0, y0, x1, y1;
            portSeatRegion(seat, &x0, &y0, &x1, &y1);
            std::vector<std::string> list;
            for (int i = 0; i < n; i++) list.push_back(names[i]);
            menu::drawWheel(W, H, seat, x0, y0, x1, y1, kind == 2 ? "Gadgets" : "Weapons", list, sel);
        }
    }
    ImGui::Render();

    s_vtx.clear();
    s_idx.clear();
    s_cmd.clear();
    ImDrawData* dd = ImGui::GetDrawData();
    for (int n = 0; dd && n < dd->CmdListsCount; n++) {
        const ImDrawList* cl = dd->CmdLists[n];
        const uint32_t vbase = (uint32_t)s_vtx.size(), ibase = (uint32_t)s_idx.size();
        for (int i = 0; i < cl->VtxBuffer.Size; i++) {
            const ImDrawVert& v = cl->VtxBuffer[i];
            s_vtx.push_back({ v.pos.x, v.pos.y, v.uv.x, v.uv.y, v.col });
        }
        for (int i = 0; i < cl->IdxBuffer.Size; i++) s_idx.push_back(vbase + cl->IdxBuffer[i]);
        for (int c = 0; c < cl->CmdBuffer.Size; c++) {
            const ImDrawCmd* cmd = &cl->CmdBuffer[c];
            if (cmd->UserCallback) continue;
            s_cmd.push_back({ cmd->ClipRect.x, cmd->ClipRect.y, cmd->ClipRect.z, cmd->ClipRect.w, ibase + cmd->IdxOffset,
                              cmd->ElemCount });
        }
    }
    gfx_ui_submit(s_vtx.data(), (uint32_t)s_vtx.size(), s_idx.data(), (uint32_t)s_idx.size(), s_cmd.data(),
                  (uint32_t)s_cmd.size());
}

extern "C" void menuUiFrame(void) { runFrame(0); }
extern "C" void menuWheelFrame(void) { runFrame(1); }
extern "C" void menuAssignFrame(void) { runFrame(2); }
extern "C" void menuCoopCountFrame(void) { runFrame(3); }

extern "C" {
int portDevMode(void);
int optionsMenuGet(const char* key, double* out);
void optionsMenuSet(const char* key, double value);
int optionsMenuResCount(void);
int optionsMenuAudioCount(void);
const char* optionsMenuAudioLabel(int i);
void optionsMenuSetSeat(int seat);
void optionsMenuResLabel(int i, char* out, int n);
const char* optionsMenuBindText(const char* key, int slot);
void optionsMenuBindBegin(const char* key, int slot);
int optionsMenuBindActive(void);
void optionsMenuAction(const char* key);
}

static bool bindGet(const char* key, double* out) { return optionsMenuGet(key, out) != 0; }
static void bindSet(const char* key, double value) { optionsMenuSet(key, value); }
static std::string keyText(const char* key, int slot) { return optionsMenuBindText(key, slot); }
static void keyBegin(const char* key, int slot) { optionsMenuBindBegin(key, slot); }
static bool keyActive() { return optionsMenuBindActive() != 0; }
static void doAction(const char* key) { optionsMenuAction(key); }

/* "hfov": the horizontal view angle that a vertical one gives at the window's aspect ratio (the picture's, right now) */
static const char* readout(const char* name, double cfg) {
    static char buf[32];
    buf[0] = 0;
    if (!strcmp(name, "hfov")) snprintf(buf, sizeof buf, "(%d deg horizontal)", (int)(portFovHorizDegrees((int)(cfg + 0.5)) + 0.5f));
    return buf;
}

extern "C" void menuBindGame(void) {   /* the menu reads and writes the game's values */
    std::vector<std::string> res;
    for (int i = 0; i < optionsMenuResCount(); i++) {
        char b[32];
        optionsMenuResLabel(i, b, sizeof b);
        res.push_back(b);
    }
    if (!res.empty()) menu::setOptionLabels("res", res);
    std::vector<std::string> dev;
    for (int i = 0; i < optionsMenuAudioCount(); i++) dev.push_back(optionsMenuAudioLabel(i));
    if (!dev.empty()) menu::setOptionLabels("adev", dev);
    menu::setBinding(bindGet, bindSet);
    menu::setKeyBinding(keyText, keyBegin, keyActive);
    menu::setActionHandler(doAction);
    menu::setReadout(readout);
    menu::setTint(portCrosshairTint);
    menu::setPicture(portCrosshairPicture);
}

extern "C" int menuTakeCloseRequest(void) {
    return menu::takeCloseRequest() ? 1 : 0;
}

extern "C" void menuUiClose(void) {
    menu::showRoot();
    gfx_ui_submit(nullptr, 0, nullptr, 0, nullptr, 0);
}
