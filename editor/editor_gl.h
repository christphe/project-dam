// What the editor needs from the renderer beyond the draw API: clears and scissors, reading pixels back, textures for
// ImGui::Image, the offscreen frame. The drawing itself goes through ge_render (ge_render.h): the editor opens its
// window and its backend (OpenGL or Vulkan) with ge_render_open, Scene draws the level through the backend's draw
// vtable and imgui_render_ge.cpp draws the GUI through it. Kept in its own translation unit because glad's macros
// must not meet anything else's GL loader in one file. On OpenGL these are raw GL calls on the backend's context
// (as the editor always made them); on Vulkan they go through the draw vtable, and a read-back is the backend's
// window dump (the present writes a PPM; one frame is presented for it).
#pragma once

#include <cstdint>
#include <vector>

// The window's drawable size in pixels, from SDL (the GL and Vulkan drawable), not from the backend: after a resize the Vulkan
// backend only learns the new swapchain size in the frame that recreates it, and a clear sized by the old one leaves the
// render area (a validation error on the one frame of the resize).
void editorDrawableSize(int *w, int *h);

// Clears the whole window (color + depth), scissor off.
void editorGlClear(int w, int h, float r, float g, float b);
// Restricts drawing to a window rectangle (x, y from the bottom left).
void editorGlScissor(int x, int y, int w, int h);
void editorGlScissorOff();
// Reads the window's back buffer as top-down RGBA8 rows. Vulkan: this presents the frame (call after ge_render_end_frame).
bool editorGlReadWindow(int w, int h, std::vector<uint8_t> &rgba);

// Reads a rectangle of the window's back buffer (x, y from the top left) as top-down RGBA8 rows.
bool editorGlReadRect(int winH, int x, int y, int w, int h, std::vector<uint8_t> &rgba);

// Reads a rectangle (x, y from the top left) of what was just drawn: the offscreen frame when it is in use, else the window's back buffer.
// Vulkan: this presents the frame (call after ge_render_end_frame).
bool editorGlReadFrame(bool offscreen, int winH, int x, int y, int w, int h, std::vector<uint8_t> &rgba);

// RGBA8 texture for ImGui::Image (mod panel thumbnails): a ge_render texture id, drawn by the ImGui renderer. 0 on failure.
uint32_t editorGlUploadTexture(int w, int h, const uint8_t *rgba);
void editorGlDeleteTexture(uint32_t id);

// Whole-frame offscreen rendering (OpenGL only; on by default, --no-offscreen turns it off): the 3D view and the GUI
// draw into an offscreen framebuffer, copied to the window in one blit just before the swap, so the
// window's own buffer only ever holds whole frames. Windows + NVIDIA presents windowed OpenGL through
// DXGI ("Vulkan/OpenGL present method: Auto") and showed half-drawn frames: the 3D without the GUI.
void editorOffscreenBind(int w, int h);      // creates / resizes it on demand, then binds it
void editorOffscreenPresent(int w, int h);   // blit to the window's back buffer, window framebuffer bound after
