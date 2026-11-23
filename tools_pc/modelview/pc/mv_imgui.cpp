// modelview: see mv_imgui.h. Same idea as the game's own ImGui renderer, on plain GL 3.3 core.
#include "mv_imgui.h"
#include "imgui.h"
#include <GL/gl.h>
#include <GL/glext.h>
#include <stddef.h>

// loaded by modelview.cpp
extern PFNGLUSEPROGRAMPROC glUseProgram; extern PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocation; extern PFNGLUNIFORMMATRIX4FVPROC glUniformMatrix4fv;
extern PFNGLUNIFORM1IPROC glUniform1i; extern PFNGLGENVERTEXARRAYSPROC glGenVertexArrays; extern PFNGLBINDVERTEXARRAYPROC glBindVertexArray;
extern PFNGLGENBUFFERSPROC glGenBuffers; extern PFNGLBINDBUFFERPROC glBindBuffer; extern PFNGLBUFFERDATAPROC glBufferData;
extern PFNGLVERTEXATTRIBPOINTERPROC glVertexAttribPointer; extern PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray;
GLuint mv_program(const char* vs, const char* fs, const char* name);

static const char* kVert = R"(#version 330 core
layout(location=0) in vec2 aP; layout(location=1) in vec2 aUV; layout(location=2) in vec4 aC;
uniform mat4 uP; out vec2 vUV; out vec4 vC;
void main() { vUV = aUV; vC = aC; gl_Position = uP * vec4(aP, 0.0, 1.0); }
)";
static const char* kFrag = R"(#version 330 core
in vec2 vUV; in vec4 vC; uniform sampler2D uTex; out vec4 o;
void main() { o = vC * texture(uTex, vUV); }
)";

static GLuint g_prog, g_vao, g_vbo, g_ibo, g_font;
static int g_w = 1, g_h = 1;
static DWORD g_last;

void mvui_init() {
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; io.LogFilename = nullptr;
    ImGui::GetStyle().FontScaleMain = 1.0f;
    ImGui::StyleColorsDark();
    unsigned char* px; int tw, th; io.Fonts->GetTexDataAsRGBA32(&px, &tw, &th);
    glGenTextures(1, &g_font); glBindTexture(GL_TEXTURE_2D, g_font);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, tw, th, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, 0x812F); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, 0x812F);   // clamp to edge
    io.Fonts->SetTexID((ImTextureID)(intptr_t)g_font);
    g_prog = mv_program(kVert, kFrag, "imgui");
    glGenVertexArrays(1, &g_vao); glGenBuffers(1, &g_vbo); glGenBuffers(1, &g_ibo);
    g_last = GetTickCount();
}

static ImGuiKey mapKey(WPARAM w) {
    switch (w) {
    case VK_TAB: return ImGuiKey_Tab; case VK_LEFT: return ImGuiKey_LeftArrow; case VK_RIGHT: return ImGuiKey_RightArrow;
    case VK_UP: return ImGuiKey_UpArrow; case VK_DOWN: return ImGuiKey_DownArrow; case VK_HOME: return ImGuiKey_Home; case VK_END: return ImGuiKey_End;
    case VK_DELETE: return ImGuiKey_Delete; case VK_BACK: return ImGuiKey_Backspace; case VK_RETURN: return ImGuiKey_Enter;
    case VK_ESCAPE: return ImGuiKey_Escape; case VK_SPACE: return ImGuiKey_Space; case 'A': return ImGuiKey_A; case 'C': return ImGuiKey_C;
    case 'V': return ImGuiKey_V; case 'X': return ImGuiKey_X;
    }
    return ImGuiKey_None;
}

bool mvui_wndproc(HWND, UINT m, WPARAM w, LPARAM l) {
    if (!ImGui::GetCurrentContext()) return false;
    ImGuiIO& io = ImGui::GetIO();
    switch (m) {
    case WM_MOUSEMOVE: io.AddMousePosEvent((float)(short)LOWORD(l), (float)(short)HIWORD(l)); break;
    case WM_LBUTTONDOWN: case WM_LBUTTONUP: io.AddMouseButtonEvent(0, m == WM_LBUTTONDOWN); break;
    case WM_RBUTTONDOWN: case WM_RBUTTONUP: io.AddMouseButtonEvent(1, m == WM_RBUTTONDOWN); break;
    case WM_MBUTTONDOWN: case WM_MBUTTONUP: io.AddMouseButtonEvent(2, m == WM_MBUTTONDOWN); break;
    case WM_MOUSEWHEEL: io.AddMouseWheelEvent(0, (short)HIWORD(w) / 120.0f); break;
    case WM_CHAR: if (w >= 32 && w != 127) io.AddInputCharacter((unsigned)w); break;
    case WM_KEYDOWN: case WM_KEYUP: case WM_SYSKEYDOWN: case WM_SYSKEYUP: {
        const bool d = m == WM_KEYDOWN || m == WM_SYSKEYDOWN;
        io.AddKeyEvent(ImGuiMod_Ctrl, (GetKeyState(VK_CONTROL) & 0x8000) != 0); io.AddKeyEvent(ImGuiMod_Shift, (GetKeyState(VK_SHIFT) & 0x8000) != 0);
        const ImGuiKey k = mapKey(w); if (k != ImGuiKey_None) io.AddKeyEvent(k, d);
        break; }
    case WM_KILLFOCUS: io.AddFocusEvent(false); break;
    case WM_SETFOCUS: io.AddFocusEvent(true); break;
    }
    return false;
}

bool mvui_want_mouse() { return ImGui::GetIO().WantCaptureMouse; }

void mvui_begin(int w, int h) {
    ImGuiIO& io = ImGui::GetIO();
    g_w = w; g_h = h; io.DisplaySize = ImVec2((float)w, (float)h);
    const DWORD now = GetTickCount(); float dt = (now - g_last) / 1000.0f; g_last = now;
    io.DeltaTime = dt <= 0 || dt > 0.25f ? 1.0f / 60.0f : dt;
    ImGui::NewFrame();
}

void mvui_draw() {
    ImGui::Render();
    ImDrawData* dd = ImGui::GetDrawData();
    if (!dd || dd->CmdListsCount == 0) return;
    glViewport(0, 0, g_w, g_h);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_CULL_FACE); glDisable(GL_DEPTH_TEST); glEnable(GL_SCISSOR_TEST);
    const float L = 0, R = (float)g_w, T = 0, B = (float)g_h;
    const float proj[16] = { 2 / (R - L), 0, 0, 0, 0, 2 / (T - B), 0, 0, 0, 0, -1, 0, (R + L) / (L - R), (T + B) / (B - T), 0, 1 };
    glUseProgram(g_prog); glUniformMatrix4fv(glGetUniformLocation(g_prog, "uP"), 1, GL_FALSE, proj); glUniform1i(glGetUniformLocation(g_prog, "uTex"), 0);
    glBindVertexArray(g_vao); glBindBuffer(GL_ARRAY_BUFFER, g_vbo); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_ibo);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(ImDrawVert), (const void*)offsetof(ImDrawVert, pos)); glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(ImDrawVert), (const void*)offsetof(ImDrawVert, uv)); glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(ImDrawVert), (const void*)offsetof(ImDrawVert, col)); glEnableVertexAttribArray(2);
    for (int n = 0; n < dd->CmdListsCount; n++) {
        const ImDrawList* cl = dd->CmdLists[n];
        glBufferData(GL_ARRAY_BUFFER, cl->VtxBuffer.Size * sizeof(ImDrawVert), cl->VtxBuffer.Data, GL_STREAM_DRAW);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, cl->IdxBuffer.Size * sizeof(ImDrawIdx), cl->IdxBuffer.Data, GL_STREAM_DRAW);
        for (int i = 0; i < cl->CmdBuffer.Size; i++) {
            const ImDrawCmd& c = cl->CmdBuffer[i];
            if (c.UserCallback) { c.UserCallback(cl, &c); continue; }
            if (c.ClipRect.z <= c.ClipRect.x || c.ClipRect.w <= c.ClipRect.y) continue;
            glScissor((int)c.ClipRect.x, g_h - (int)c.ClipRect.w, (int)(c.ClipRect.z - c.ClipRect.x), (int)(c.ClipRect.w - c.ClipRect.y));
            glBindTexture(GL_TEXTURE_2D, (GLuint)(intptr_t)c.GetTexID());
            glDrawElements(GL_TRIANGLES, (GLsizei)c.ElemCount, sizeof(ImDrawIdx) == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT, (const void*)(intptr_t)(c.IdxOffset * sizeof(ImDrawIdx)));
        }
    }
    glDisable(GL_SCISSOR_TEST); glDisable(GL_BLEND); glEnable(GL_DEPTH_TEST);
}
