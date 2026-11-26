#include "editor_gl.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

#include <SDL.h>
#include <SDL_vulkan.h>

#include "glad/glad.h"

#include "ge_render.h"
// SHADER_OPT_ALPHA (gfx_cc.h, which needs C++20; the editor is C++17)
static const uint32_t kShaderOptAlpha = 1u << 0;

namespace {

bool isGl() { return ge_render_backend() == GE_RENDER_GL; }

int s_winW = 1, s_winH = 1;   // the window size of the last editorGlClear (the Vulkan scissor-off rectangle)

// The Vulkan clear: one flat-colour rectangle over the whole viewport at the far plane, depth test "always" with the write on, so it
// sets colour and depth like a clear does (the backend's own clear is black, and its rectangle is the size the host last gave
// update_framebuffer_parameters, which is outside the render area for the one frame in which a resize reaches the swapchain).
// The "shade only" combiner every pack has; it is unloaded again so the caller's program bookkeeping holds.
void fillWindow(float r, float g, float b)
{
    GfxRenderingAPI *api = ge_render_api();
    const uint64_t id0 = ge_render_cc_id_shade();
    const uint32_t id1 = kShaderOptAlpha;
    ShaderProgram *prg = api->lookup_shader(id0, id1);
    if (!prg) prg = api->create_and_load_new_shader(id0, id1);
    api->load_shader(prg);
    api->set_depth_mode(true, true, false, false, 0);
    api->set_use_alpha(false, 0);
    static const float x[6] = { -1, 1, -1, -1, 1, 1 }, y[6] = { -1, -1, 1, 1, -1, 1 };
    float v[6 * 8];
    for (int i = 0; i < 6; i++) {
        float *o = v + i * 8;
        o[0] = x[i]; o[1] = y[i]; o[2] = 1; o[3] = 1; o[4] = r; o[5] = g; o[6] = b; o[7] = 1;
    }
    api->draw_triangles(v, 6 * 8, 2);
    api->unload_shader(prg);
}

// The Vulkan read-back: the backend writes the presented window as a PPM. Presents the frame that was just ended.
bool vkReadWindow(std::vector<uint8_t> &rgba, int &w, int &h)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    const std::string path = (fs::temp_directory_path(ec) / "project-dam-editor-dump.ppm").string();
    fs::remove(path, ec);
    uint32_t dw = 0, dh = 0;
    int32_t px = 0, py = 0;
    ge_render_get_dimensions(&dw, &dh, &px, &py);
    if (!ge_render_dump_window(dw, dh, path.c_str())) return false;
    ge_render_present();   // the dump is written in the present
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) return false;
    int pw = 0, ph = 0, mx = 0;
    if (fscanf(f, "P6 %d %d %d", &pw, &ph, &mx) != 3 || pw <= 0 || ph <= 0) { fclose(f); return false; }
    fgetc(f);   // the single whitespace after the header
    std::vector<uint8_t> rgb((size_t)pw * ph * 3);
    const bool ok = fread(rgb.data(), 1, rgb.size(), f) == rgb.size();
    fclose(f);
    fs::remove(path, ec);
    if (!ok) return false;
    rgba.resize((size_t)pw * ph * 4);
    for (size_t i = 0, n = (size_t)pw * ph; i < n; i++) {
        rgba[i * 4 + 0] = rgb[i * 3 + 0]; rgba[i * 4 + 1] = rgb[i * 3 + 1]; rgba[i * 4 + 2] = rgb[i * 3 + 2]; rgba[i * 4 + 3] = 255;
    }
    w = pw; h = ph;
    return true;
}

// A rectangle (x, y from the top left) of a top-down RGBA image of the window.
bool cropRect(const std::vector<uint8_t> &full, int fw, int fh, int x, int y, int w, int h, std::vector<uint8_t> &rgba)
{
    if (x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > fw || y + h > fh) return false;
    rgba.resize((size_t)w * h * 4);
    for (int r = 0; r < h; r++) memcpy(&rgba[(size_t)r * w * 4], &full[((size_t)(y + r) * fw + x) * 4], (size_t)w * 4);
    return true;
}

bool vkReadRect(int x, int y, int w, int h, std::vector<uint8_t> &rgba)
{
    std::vector<uint8_t> full;
    int fw = 0, fh = 0;
    return vkReadWindow(full, fw, fh) && cropRect(full, fw, fh, x, y, w, h, rgba);
}

}   // namespace

void editorDrawableSize(int *w, int *h)
{
    SDL_Window *win = (SDL_Window *)ge_render_window()->get_window_handle();
    *w = *h = 0;
    if (ge_render_backend() == GE_RENDER_VULKAN) SDL_Vulkan_GetDrawableSize(win, w, h);
    else SDL_GL_GetDrawableSize(win, w, h);
}

void editorGlClear(int w, int h, float r, float g, float b)
{
    s_winW = w; s_winH = h;
    if (!isGl()) {
        GfxRenderingAPI *api = ge_render_api();
        api->set_viewport(0, 0, w, h);
        api->set_scissor(0, 0, w, h);
        fillWindow(r, g, b);
        return;
    }
    glViewport(0, 0, w, h);
    glDisable(GL_SCISSOR_TEST);
    glDepthMask(GL_TRUE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(r, g, b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void editorGlScissor(int x, int y, int w, int h)
{
    if (!isGl()) { ge_render_api()->set_viewport(x, y, w, h); ge_render_api()->set_scissor(x, y, w, h); return; }
    glViewport(x, y, w, h);
    glEnable(GL_SCISSOR_TEST);
    glScissor(x, y, w, h);
}

void editorGlScissorOff()
{
    if (!isGl()) { ge_render_api()->set_viewport(0, 0, s_winW, s_winH); ge_render_api()->set_scissor(0, 0, s_winW, s_winH); return; }
    // The backend keeps the scissor test on (the GUI draws with clip rectangles): the whole window, not "off".
    glEnable(GL_SCISSOR_TEST);
    glScissor(0, 0, s_winW, s_winH);
}

bool editorGlReadWindow(int w, int h, std::vector<uint8_t> &rgba)
{
    if (!isGl()) return vkReadRect(0, 0, w, h, rgba);
    std::vector<uint8_t> raw((size_t)w * h * 4);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glReadBuffer(GL_BACK);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, raw.data());
    if (glGetError() != GL_NO_ERROR) return false;
    rgba.resize(raw.size());
    for (int y = 0; y < h; y++)
        memcpy(&rgba[(size_t)y * w * 4], &raw[(size_t)(h - 1 - y) * w * 4], (size_t)w * 4);
    return true;
}

bool editorGlReadRect(int winH, int x, int y, int w, int h, std::vector<uint8_t> &rgba)
{
    if (!isGl()) { (void)winH; return vkReadRect(x, y, w, h, rgba); }
    std::vector<uint8_t> raw((size_t)w * h * 4);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glReadBuffer(GL_BACK);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(x, winH - (y + h), w, h, GL_RGBA, GL_UNSIGNED_BYTE, raw.data());
    if (glGetError() != GL_NO_ERROR) return false;
    rgba.resize(raw.size());
    for (int r = 0; r < h; r++)
        memcpy(&rgba[(size_t)r * w * 4], &raw[(size_t)(h - 1 - r) * w * 4], (size_t)w * 4);
    return true;
}

uint32_t editorGlUploadTexture(int w, int h, const uint8_t *rgba)
{
    GfxRenderingAPI *api = ge_render_api();
    const uint32_t id = api->new_texture();
    if (!id) return 0;
    api->select_texture(0, id, true);
    api->upload_texture(rgba, (uint32_t)w, (uint32_t)h, false);
    api->set_sampler_parameters(0, true, 2 /* G_TX_CLAMP */, 2, false);
    return id;
}

void editorGlDeleteTexture(uint32_t id)
{
    ge_render_api()->delete_texture(id);
}

static GLuint s_offFbo, s_offColor, s_offDepth;
static int s_offW, s_offH;

void editorOffscreenBind(int w, int h)
{
    if (!isGl()) return;
    if (s_offFbo == 0 || w != s_offW || h != s_offH) {
        if (s_offFbo == 0) { glGenFramebuffers(1, &s_offFbo); glGenRenderbuffers(1, &s_offColor); glGenRenderbuffers(1, &s_offDepth); }
        glBindRenderbuffer(GL_RENDERBUFFER, s_offColor);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, w, h);
        glBindRenderbuffer(GL_RENDERBUFFER, s_offDepth);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
        glBindFramebuffer(GL_FRAMEBUFFER, s_offFbo);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, s_offColor);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, s_offDepth);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) fprintf(stderr, "[editor] offscreen framebuffer incomplete\n");
        s_offW = w; s_offH = h;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, s_offFbo);
}

void editorOffscreenPresent(int w, int h)
{
    if (!isGl()) return;
    glDisable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, s_offFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

bool editorGlReadFrame(bool offscreen, int winH, int x, int y, int w, int h, std::vector<uint8_t> &rgba)
{
    if (!isGl()) { (void)offscreen; (void)winH; return vkReadRect(x, y, w, h, rgba); }
    std::vector<uint8_t> raw((size_t)w * h * 4);
    glBindFramebuffer(GL_FRAMEBUFFER, offscreen ? s_offFbo : 0);
    glReadBuffer(offscreen ? GL_COLOR_ATTACHMENT0 : GL_BACK);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(x, winH - (y + h), w, h, GL_RGBA, GL_UNSIGNED_BYTE, raw.data());
    const bool ok = glGetError() == GL_NO_ERROR;
    if (offscreen) glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (!ok) return false;
    rgba.resize(raw.size());
    for (int r = 0; r < h; r++)
        memcpy(&rgba[(size_t)r * w * 4], &raw[(size_t)(h - 1 - r) * w * 4], (size_t)w * 4);
    return true;
}
