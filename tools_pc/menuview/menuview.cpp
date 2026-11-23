// menuview: the game's menu (port/src/menu/menu_ui.cpp) on its own, in a window, to iterate on its look and
// feel without starting the game. Win32 + OpenGL 3 with the official Dear ImGui backends; the menu code is the
// very file the game compiles. Keyboard (arrows, Enter, Esc, PageUp/PageDown for the tabs), mouse and any
// XInput pad work.
//   menuview.exe [--fonts DIR] [--menu menu.json] [--lang translations.json] [--w N] [--h N]
// Keys: F5 reloads the menu file (and the translations), F2 switches Switch / PC, F3 turns dev mode on or off.
#include <windows.h>
#include <GL/gl.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "imgui.h"
#include "menu_pad.h"
#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_win32.h"
#include "menu_ui.h"
#include "menu_model.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static int g_w = 1280, g_h = 720;
static bool g_reload = false, g_flipPlatform = false, g_flipDev = false;

static std::vector<char> readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return std::vector<char>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

static LRESULT CALLBACK wndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (ImGui_ImplWin32_WndProcHandler(h, m, w, l)) return true;
    switch (m) {
    case WM_SIZE:
        if (w != SIZE_MINIMIZED) { g_w = LOWORD(l); g_h = HIWORD(l); }
        return 0;
    case WM_KEYDOWN:
        if (w == VK_F5) g_reload = true;
        if (w == VK_F2) g_flipPlatform = true;
        if (w == VK_F3) g_flipDev = true;
        if (w == VK_ESCAPE && (GetKeyState(VK_SHIFT) & 0x8000)) { PostQuitMessage(0); return 0; }   // Shift+Esc quits
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

int main(int argc, char** argv) {
    struct { int t = -1, e = 0, f = -1; } gotoArg;   /* --goto tab,entry,focus */
    std::string fonts = "port/third_party/fonts", script, menuFile = "port/src/menu/menu.json", langFile;
    for (int i = 1; i + 1 < argc; i++) {
        if (!strcmp(argv[i], "--fonts")) fonts = argv[i + 1];
        if (!strcmp(argv[i], "--script")) script = argv[i + 1];
        if (!strcmp(argv[i], "--player")) menu::setOwner(atoi(argv[i + 1]) - 1);
        if (!strcmp(argv[i], "--goto")) { int t = 0, e = 0, f = -1; if (sscanf(argv[i + 1], "%d,%d,%d", &t, &e, &f) >= 2) { gotoArg.t = t; gotoArg.e = e; gotoArg.f = f; } }
        if (!strcmp(argv[i], "--menu")) menuFile = argv[i + 1];
        if (!strcmp(argv[i], "--lang")) langFile = argv[i + 1];
        if (!strcmp(argv[i], "--w")) g_w = atoi(argv[i + 1]);
        if (!strcmp(argv[i], "--h")) g_h = atoi(argv[i + 1]);
    }
    std::vector<char> body = readFile(fonts + "/FiraSans-Regular.ttf");
    std::vector<char> head = readFile(fonts + "/BarlowCondensed-SemiBold.ttf");
    if (body.empty() || head.empty()) {
        fprintf(stderr, "menuview: fonts not found in '%s' (run from the repo root or pass --fonts DIR)\n", fonts.c_str());
        return 1;
    }

    WNDCLASSEXW wc = { sizeof wc, CS_OWNDC, wndProc, 0, 0, GetModuleHandleW(nullptr), nullptr, LoadCursor(nullptr, IDC_ARROW),
                       nullptr, nullptr, L"menuview", nullptr };
    RegisterClassExW(&wc);
    RECT r = { 0, 0, g_w, g_h };
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd = CreateWindowW(L"menuview", L"menuview - Project Dam menu", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 60,
                              r.right - r.left, r.bottom - r.top, nullptr, nullptr, wc.hInstance, nullptr);
    HDC dc = GetDC(hwnd);
    PIXELFORMATDESCRIPTOR pfd = {};
    pfd.nSize = sizeof pfd;
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    SetPixelFormat(dc, ChoosePixelFormat(dc, &pfd), &pfd);
    HGLRC rc = wglCreateContext(dc);
    wglMakeCurrent(dc, rc);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui_ImplWin32_InitForOpenGL(hwnd);
    ImGui_ImplOpenGL3_Init("#version 130");
    menu::init(body.data(), body.size(), head.data(), head.size());
    /* The gamepad drawing, packed in the font atlas like the game does. With 1.92's texture-owning backend the atlas
     * can be rebuilt or grown at any time: ImGui copies every packed rectangle (custom ones too) into the new texture,
     * but the UVs move, so they are read again each frame (padUv). The pixels are written once, right after
     * AddCustomRect and before the backend first creates the texture, so no upload has to be queued by hand. */
    ImFontAtlas* atlas = ImGui::GetIO().Fonts;
    ImFontAtlasRectId padRect = ImFontAtlasRectId_Invalid;
    {
        atlas->TexPixelsUseColors = true;   /* RGBA pixels: keeps the backend's texture RGBA32 */
        ImFontAtlasRect rr;
        padRect = atlas->AddCustomRect(kMenuPadW, kMenuPadH, &rr);
        ImTextureData* tex = atlas->TexData;
        if (padRect == ImFontAtlasRectId_Invalid || !tex || !tex->Pixels || tex->Format != ImTextureFormat_RGBA32)
            fprintf(stderr, "menuview: cannot place the gamepad drawing in the atlas\n");
        else
            for (int y = 0; y < kMenuPadH; y++)
                for (int x = 0; x < kMenuPadW; x++) {
                    unsigned char* d = (unsigned char*)tex->GetPixelsAt(rr.x + x, rr.y + y);
                    d[0] = d[1] = d[2] = 255;
                    d[3] = kMenuPad[y * kMenuPadW + x];
                }
    }
    auto padUv = [&]() {
        ImFontAtlasRect r;
        if (padRect != ImFontAtlasRectId_Invalid && atlas->GetCustomRect(padRect, &r))
            menu::setPadImage(r.uv0.x, r.uv0.y, r.uv1.x, r.uv1.y, (float)kMenuPadW / (float)kMenuPadH);
    };
    auto loadContent = [&]() {
        std::vector<char> js = readFile(menuFile);
        std::string err;
        if (js.empty()) { fprintf(stderr, "menuview: cannot read '%s'\n", menuFile.c_str()); return; }
        if (!menu::setData(js.data(), js.size(), &err)) fprintf(stderr, "menuview: %s\n", err.c_str());
        else fprintf(stderr, "menuview: loaded %s\n", menuFile.c_str());
        if (!langFile.empty()) {
            std::vector<char> tr = readFile(langFile);
            if (!menu::loadTranslations(tr.data(), tr.size(), &err)) fprintf(stderr, "menuview: %s\n", err.c_str());
        }
    };
    loadContent();
    if (gotoArg.t >= 0) menu::debugGoto(gotoArg.t, gotoArg.e, gotoArg.f);
    auto title = [&]() {
        std::wstring t = std::wstring(L"menuview - ") + (menu::isPc() ? L"PC" : L"Switch") + (menu::devOn() ? L" - dev mode" : L"") +
                         L"   (F5 reload, F2 platform, F3 dev)";
        SetWindowTextW(hwnd, t.c_str());
    };
    title();

    size_t scriptPos = 0;
    int frame = 0;
    for (bool run = true; run;) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) run = false;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!run) break;
        if (g_reload) { g_reload = false; loadContent(); }
        if (g_flipPlatform) { g_flipPlatform = false; menu::setPlatform(!menu::isPc()); title(); }
        if (g_flipDev) { g_flipDev = false; menu::setDev(!menu::devOn()); title(); }
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplWin32_NewFrame();
        if (!script.empty()) {   /* self-test: U D L R = d-pad, A B = face buttons, T Y = L1 R1; one key per 12 frames */
            static ImGuiKey held = ImGuiKey_None;
            ImGuiIO& sio = ImGui::GetIO();
            if (held != ImGuiKey_None) { sio.AddKeyEvent(held, false); held = ImGuiKey_None; }
            if (frame > 10 && frame % 12 == 0) {
                while (scriptPos < script.size() && script[scriptPos] == ',') scriptPos++;
                if (scriptPos < script.size()) {
                    const char k = script[scriptPos++];
                    if (k == 'P') { menu::setPlatform(!menu::isPc()); }   /* self-test: P flips the platform, V dev mode */
                    else if (k == 'V') { menu::setDev(!menu::devOn()); }
                    else if (k == 'K') held = ImGuiKey_GamepadBack;
                    else held = k == 'U' ? ImGuiKey_GamepadDpadUp : k == 'D' ? ImGuiKey_GamepadDpadDown : k == 'L' ? ImGuiKey_GamepadDpadLeft
                         : k == 'R' ? ImGuiKey_GamepadDpadRight : k == 'A' ? ImGuiKey_GamepadFaceDown : k == 'B' ? ImGuiKey_GamepadFaceRight
                         : k == 'T' ? ImGuiKey_GamepadL1 : ImGuiKey_GamepadR1;
                    if (held != ImGuiKey_None) sio.AddKeyEvent(held, true);
                } else {
                    printf("%s\n", menu::debugState());
                    fflush(stdout);
                    break;
                }
            }
            if (frame > 10 && frame % 12 == 6) { printf("%c -> %s\n", scriptPos > 0 ? script[scriptPos - 1] : '?', menu::debugState()); fflush(stdout); }
        }
        frame++;
        ImGui::NewFrame();
        padUv();
        if (ImGui::IsKeyPressed(ImGuiKey_GamepadBack, false)) { menu::setPlatform(!menu::isPc()); title(); }   // Back (Select): Switch <-> PC
        menu::draw((float)g_w, (float)g_h);
        {   // the mode we are previewing, drawn over everything like an FPS counter (not part of the menu)
            char tag[40];
            snprintf(tag, sizeof tag, "%s%s", menu::isPc() ? "PC" : "SWITCH", menu::devOn() ? "  DEV" : "");
            ImDrawList* fg = ImGui::GetForegroundDrawList();
            const float px = 15.0f;
            const ImVec2 sz = ImGui::GetFont()->CalcTextSizeA(px, 1e9f, 0.0f, tag);
            const ImVec2 p((float)g_w - sz.x - 12.0f, 3.0f);
            fg->AddRectFilled(ImVec2(p.x - 5.0f, p.y - 1.0f), ImVec2(p.x + sz.x + 5.0f, p.y + sz.y + 1.0f), IM_COL32(0, 0, 0, 150));
            fg->AddText(ImGui::GetFont(), px, p, IM_COL32(64, 255, 96, 255), tag);
        }
        ImGui::Render();
        glViewport(0, 0, g_w, g_h);
        glClearColor(0.10f, 0.12f, 0.09f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SwapBuffers(dc);
    }
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(rc);
    return 0;
}
