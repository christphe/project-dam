/* Dear ImGui over ge_render's draw API (ge_imgui.h). Clean include paths: see ge_imgui.h. */
#include <stdint.h>
#include <string.h>
#include <vector>

#include "imgui.h"
#include "ge_render.h"
#include "ge_imgui.h"

static uint32_t s_atlas = 0;
static uint64_t s_id0 = 0;
static const uint32_t kId1 = 1;   /* SHADER_OPT_ALPHA */
static struct ShaderProgram *s_prg = nullptr;
static ImGuiContext *s_ctx = nullptr;

bool ge_imgui_init(void) {
    IMGUI_CHECKVERSION();
    s_ctx = ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad | ImGuiConfigFlags_NavEnableKeyboard;
    unsigned char *px = nullptr;
    int w = 0, h = 0;
    io.Fonts->GetTexDataAsRGBA32(&px, &w, &h);
    GfxRenderingAPI *api = ge_render_api();
    s_atlas = api->new_texture();
    api->select_texture(0, s_atlas, true);
    api->upload_texture(px, (uint32_t)w, (uint32_t)h, false);
    api->set_sampler_parameters(0, true, 2, 2, false);
    io.Fonts->SetTexID((ImTextureID)(intptr_t)s_atlas);
    s_id0 = ge_render_cc_id_texel0_shade();
    return true;
}

void ge_imgui_shutdown(void) {
    if (s_ctx) ImGui::DestroyContext(s_ctx);
    s_ctx = nullptr;
}

void ge_imgui_new_frame(int width, int height, float wheel) {
    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)width, (float)height);
    io.DeltaTime = 1.0f / 60.0f;
    GeRenderInput in;
    ge_render_poll_input(&in);
    io.AddMousePosEvent((float)in.mouse_x, (float)in.mouse_y);
    io.AddMouseButtonEvent(0, (in.mouse_buttons & 1) != 0);
    io.AddMouseButtonEvent(1, (in.mouse_buttons & 2) != 0);
    if (wheel != 0.0f) io.AddMouseWheelEvent(0.0f, wheel);
    static const struct { uint32_t bit; ImGuiKey key; } map[] = {
        { GE_PAD_A, ImGuiKey_GamepadFaceDown }, { GE_PAD_B, ImGuiKey_GamepadFaceRight }, { GE_PAD_X, ImGuiKey_GamepadFaceLeft },
        { GE_PAD_Y, ImGuiKey_GamepadFaceUp }, { GE_PAD_UP, ImGuiKey_GamepadDpadUp }, { GE_PAD_DOWN, ImGuiKey_GamepadDpadDown },
        { GE_PAD_LEFT, ImGuiKey_GamepadDpadLeft }, { GE_PAD_RIGHT, ImGuiKey_GamepadDpadRight },
        { GE_PAD_LSHOULDER, ImGuiKey_GamepadL1 }, { GE_PAD_RSHOULDER, ImGuiKey_GamepadR1 },
    };
    for (const auto &m : map) io.AddKeyEvent(m.key, (in.pad_buttons & m.bit) != 0);
    io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickLeft, in.pad_lx < -0.2f, in.pad_lx < 0 ? -in.pad_lx : 0.0f);
    io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickRight, in.pad_lx > 0.2f, in.pad_lx > 0 ? in.pad_lx : 0.0f);
    io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickUp, in.pad_ly < -0.2f, in.pad_ly < 0 ? -in.pad_ly : 0.0f);
    io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickDown, in.pad_ly > 0.2f, in.pad_ly > 0 ? in.pad_ly : 0.0f);
    ImGui::NewFrame();
}

void ge_imgui_render(void) {
    ImGui::Render();
    ImDrawData *dd = ImGui::GetDrawData();
    if (!dd || dd->DisplaySize.x <= 0 || dd->DisplaySize.y <= 0) return;
    GfxRenderingAPI *api = ge_render_api();
    const int W = (int)dd->DisplaySize.x, H = (int)dd->DisplaySize.y;
    const bool flip = api->get_clip_parameters().invert_y;
    if (!s_prg) {
        s_prg = api->lookup_shader(s_id0, kId1);
        if (!s_prg) s_prg = api->create_and_load_new_shader(s_id0, kId1);
    }
    api->load_shader(s_prg);
    api->set_depth_mode(false, false, false, false, 0);
    api->set_use_alpha(true, 0);
    api->set_viewport(0, 0, W, H);
    std::vector<float> v;
    for (int n = 0; n < dd->CmdListsCount; n++) {
        const ImDrawList *dl = dd->CmdLists[n];
        for (const ImDrawCmd &c : dl->CmdBuffer) {
            if (c.UserCallback || c.ElemCount == 0) continue;
            const float x0 = c.ClipRect.x, y0 = c.ClipRect.y, x1 = c.ClipRect.z, y1 = c.ClipRect.w;
            if (x1 <= x0 || y1 <= y0) continue;
            api->set_scissor((int)x0, H - (int)y1, (int)(x1 - x0), (int)(y1 - y0));   /* bottom-up pixels */
            api->select_texture(0, (uint32_t)(intptr_t)c.GetTexID(), true);
            api->set_sampler_parameters(0, true, 2, 2, false);
            v.clear();
            for (unsigned i = 0; i < c.ElemCount; i++) {
                const ImDrawVert &d = dl->VtxBuffer[dl->IdxBuffer[c.IdxOffset + i] + c.VtxOffset];
                v.push_back(2.0f * d.pos.x / W - 1.0f);
                const float y = 1.0f - 2.0f * d.pos.y / H;
                v.push_back(flip ? -y : y);
                v.push_back(0.0f);
                v.push_back(1.0f);
                v.push_back(d.uv.x);
                v.push_back(d.uv.y);
                v.push_back((d.col & 0xff) / 255.0f);
                v.push_back(((d.col >> 8) & 0xff) / 255.0f);
                v.push_back(((d.col >> 16) & 0xff) / 255.0f);
                v.push_back(((d.col >> 24) & 0xff) / 255.0f);
            }
            const size_t per = 255 * 3 * 10;   /* the translator's batch size */
            for (size_t at = 0; at < v.size(); at += per) {
                const size_t cnt = v.size() - at < per ? v.size() - at : per;
                api->draw_triangles(&v[at], cnt, cnt / 30);
            }
        }
    }
    api->set_scissor(0, 0, W, H);
}
