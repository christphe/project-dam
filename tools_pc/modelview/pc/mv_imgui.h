// modelview: Dear ImGui without backends: a GL 3.3 core renderer for ImDrawData and Win32 input feeding.
#pragma once
#include <windows.h>

// The GL entry points must be loaded before mvui_init (modelview.cpp does it).
void mvui_init();
bool mvui_wndproc(HWND h, UINT m, WPARAM w, LPARAM l);   // feed an input message
void mvui_begin(int w, int h);                           // ImGui::NewFrame with the display size and delta time
void mvui_draw();                                        // ImGui::Render and draw into the bound framebuffer (w x h as in mvui_begin)
bool mvui_want_mouse();
