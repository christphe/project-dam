// The few raw GL calls the editor makes itself (everything else goes through fast3d's
// GfxRenderingAPI or ImGui's backend). Kept in its own translation unit because glad's macros
// must not meet ImGui's GL loader in one file.
#pragma once

#include <cstdint>
#include <vector>

// Clears the whole window (color + depth), scissor off.
void editorGlClear(int w, int h, float r, float g, float b);
// Restricts drawing to a window rectangle (x, y from the bottom left).
void editorGlScissor(int x, int y, int w, int h);
void editorGlScissorOff();
// Reads the window's back buffer as top-down RGBA8 rows.
bool editorGlReadWindow(int w, int h, std::vector<uint8_t> &rgba);

// Reads a rectangle of the window's back buffer (x, y from the top left) as top-down RGBA8 rows.
bool editorGlReadRect(int winH, int x, int y, int w, int h, std::vector<uint8_t> &rgba);

// RGBA8 texture for ImGui::Image (mod panel thumbnails). 0 on failure.
uint32_t editorGlUploadTexture(int w, int h, const uint8_t *rgba);
void editorGlDeleteTexture(uint32_t id);
