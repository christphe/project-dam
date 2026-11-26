#include "imgui_render_ge.h"

#include <cstdint>
#include <cstring>
#include <vector>

#include "imgui.h"

#include "ge_render.h"
// SHADER_OPT_ALPHA (gfx_cc.h, which needs C++20; the editor is C++17)
static const uint32_t kShaderOptAlpha = 1u << 0;

namespace {

ShaderProgram *s_prg = nullptr;

void destroyTexture(ImTextureData *tex)
{
    if (tex->TexID != ImTextureID_Invalid) ge_render_api()->delete_texture((uint32_t)tex->TexID);
    tex->SetTexID(ImTextureID_Invalid);
    tex->SetStatus(ImTextureStatus_Destroyed);
}

void updateTexture(ImTextureData *tex)
{
    GfxRenderingAPI *api = ge_render_api();
    if (tex->Status == ImTextureStatus_WantCreate || tex->Status == ImTextureStatus_WantUpdates) {
        // The whole image again on an update: the atlas is small and changes rarely.
        const int w = tex->Width, h = tex->Height;
        std::vector<uint8_t> rgba;
        const uint8_t *src = (const uint8_t *)tex->GetPixels();
        if (tex->Format == ImTextureFormat_Alpha8) {
            rgba.resize((size_t)w * h * 4);
            for (size_t i = 0, n = (size_t)w * h; i < n; i++) { rgba[i * 4] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = 255; rgba[i * 4 + 3] = src[i]; }
            src = rgba.data();
        }
        uint32_t id = (uint32_t)tex->TexID;
        if (tex->Status == ImTextureStatus_WantCreate || tex->TexID == ImTextureID_Invalid) id = api->new_texture();
        api->select_texture(0, id, true);
        api->upload_texture(src, (uint32_t)w, (uint32_t)h, false);
        api->set_sampler_parameters(0, true, 2 /* G_TX_CLAMP */, 2, false);
        tex->SetTexID((ImTextureID)id);
        tex->SetStatus(ImTextureStatus_OK);
    } else if (tex->Status == ImTextureStatus_WantDestroy && tex->UnusedFrames > 0) {
        destroyTexture(tex);
    }
}

}   // namespace

bool imguiGeInit()
{
    ImGuiIO &io = ImGui::GetIO();
    io.BackendRendererName = "ge_render";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures;
    return true;
}

void imguiGeShutdown()
{
    for (ImTextureData *tex : ImGui::GetPlatformIO().Textures)
        if (tex->RefCount == 1) destroyTexture(tex);
    s_prg = nullptr;
    ImGuiIO &io = ImGui::GetIO();
    io.BackendRendererName = nullptr;
    io.BackendFlags &= ~(ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures);
}

void imguiGeRender(ImDrawData *dd)
{
    if (!dd) return;
    GfxRenderingAPI *api = ge_render_api();
    if (dd->Textures)
        for (ImTextureData *tex : *dd->Textures)
            if (tex->Status != ImTextureStatus_OK) updateTexture(tex);
    const int fbW = (int)(dd->DisplaySize.x * dd->FramebufferScale.x), fbH = (int)(dd->DisplaySize.y * dd->FramebufferScale.y);
    if (fbW <= 0 || fbH <= 0 || dd->CmdListsCount == 0) return;

    const uint64_t id0 = ge_render_cc_id_texel0_shade();
    const uint32_t id1 = kShaderOptAlpha;
    s_prg = api->lookup_shader(id0, id1);
    if (!s_prg) s_prg = api->create_and_load_new_shader(id0, id1);
    api->load_shader(s_prg);
    api->set_depth_mode(false, false, false, false, 0);
    api->set_use_alpha(true, 0);
    api->set_viewport(0, 0, fbW, fbH);

    const ImVec2 clipOff = dd->DisplayPos, clipScale = dd->FramebufferScale;
    const float sx = 2.0f / dd->DisplaySize.x, sy = 2.0f / dd->DisplaySize.y;
    static std::vector<float> buf;
    const size_t perBatch = 255 * 3;   // the translator's batch size, which every backend's vertex ring is sized for
    constexpr int kFloats = 10;        // x y z w u v r g b a

    for (const ImDrawList *cl : dd->CmdLists) {
        for (const ImDrawCmd &cmd : cl->CmdBuffer) {
            if (cmd.UserCallback) continue;   // none in the editor
            const float x0 = (cmd.ClipRect.x - clipOff.x) * clipScale.x, y0 = (cmd.ClipRect.y - clipOff.y) * clipScale.y;
            const float x1 = (cmd.ClipRect.z - clipOff.x) * clipScale.x, y1 = (cmd.ClipRect.w - clipOff.y) * clipScale.y;
            const int cx0 = x0 < 0 ? 0 : (int)x0, cy0 = y0 < 0 ? 0 : (int)y0;
            const int cx1 = x1 > fbW ? fbW : (int)x1, cy1 = y1 > fbH ? fbH : (int)y1;
            if (cx1 <= cx0 || cy1 <= cy0) continue;
            api->set_scissor(cx0, fbH - cy1, cx1 - cx0, cy1 - cy0);   // bottom-up window pixels
            api->select_texture(0, (uint32_t)cmd.GetTexID(), true);
            api->set_sampler_parameters(0, true, 2 /* G_TX_CLAMP */, 2, false);

            buf.clear();
            const ImDrawVert *vtx = cl->VtxBuffer.Data + cmd.VtxOffset;
            const ImDrawIdx *idx = cl->IdxBuffer.Data + cmd.IdxOffset;
            for (unsigned i = 0; i < cmd.ElemCount; i++) {
                const ImDrawVert &v = vtx[idx[i]];
                const float f[kFloats] = {
                    (v.pos.x - dd->DisplayPos.x) * sx - 1.0f, 1.0f - (v.pos.y - dd->DisplayPos.y) * sy, 0.0f, 1.0f,
                    v.uv.x, v.uv.y,
                    (float)((v.col >> IM_COL32_R_SHIFT) & 255) / 255.0f, (float)((v.col >> IM_COL32_G_SHIFT) & 255) / 255.0f,
                    (float)((v.col >> IM_COL32_B_SHIFT) & 255) / 255.0f, (float)((v.col >> IM_COL32_A_SHIFT) & 255) / 255.0f };
                buf.insert(buf.end(), f, f + kFloats);
            }
            const size_t nverts = buf.size() / kFloats;
            for (size_t at = 0; at + 3 <= nverts; at += perBatch) {
                size_t n = nverts - at < perBatch ? nverts - at : perBatch;
                n -= n % 3;
                api->draw_triangles(&buf[at * kFloats], n * kFloats, n / 3);
            }
        }
    }
    api->unload_shader(s_prg);
    api->set_scissor(0, 0, fbW, fbH);
}
