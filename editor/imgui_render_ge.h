// The editor's Dear ImGui renderer: draws ImDrawData through ge_render's draw vtable (gfx_rendering_api.h), so the GUI
// shows on every ge_render backend (OpenGL, Vulkan) with one code path. Replaces imgui_impl_opengl3.
//
// Each ImGui vertex becomes one vertex of the combiner "texel 0 x shade" (ge_render_cc_id_texel0_shade):
// x y z w, u v, r g b a. Clip rectangles are scissors, textures are ge_render texture ids (ImTextureID),
// the font atlas follows ImGui's texture requests (ImGuiBackendFlags_RendererHasTextures).
#pragma once

struct ImDrawData;

bool imguiGeInit();
void imguiGeShutdown();
// Inside a ge_render frame (after ge_render_begin_frame), after ImGui::Render(). The viewport and scissor are left at the window.
void imguiGeRender(ImDrawData *dd);
