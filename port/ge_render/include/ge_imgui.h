#ifndef GE_IMGUI_H
#define GE_IMGUI_H

/* Dear ImGui renderer backend over ge_render's draw API (textured triangles + scissor), for any tool: it runs on the GL,
 * the Vulkan and the deko3d backend alike, and takes mouse and the first game controller (gamepad navigation, the Switch
 * included) from ge_render_poll_input. Compiled with clean include paths (the game's include/ breaks imgui.h, D66), so this
 * header needs only imgui.h; call everything on the render thread, between ge_render_begin_frame and end_frame, with
 * framebuffer 0 bound and its viewport set (the host's own scene first, the UI on top). */

struct ImDrawData;

/* After ge_render_open: creates the ImGui context and the font atlas texture (the legacy atlas path, GetTexDataAsRGBA32). */
bool ge_imgui_init(void);
void ge_imgui_shutdown(void);
/* Start of a frame: display size, mouse and pad input. wheel: the host's on_mouse_wheel total since the last call. */
void ge_imgui_new_frame(int width, int height, float wheel);
/* End of the UI: ImGui::Render() and draw. */
void ge_imgui_render(void);

#endif
