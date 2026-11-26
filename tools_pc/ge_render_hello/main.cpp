/* ge_render_hello: the smallest host of the renderer library (issue #143). Opens a window on the chosen backend, clears it
 * and draws a few debug primitives every frame, through ge_render.h only. No game code, no N64 code.
 *
 *   ge_render_hello [--backend gl|vk|deko3d] [--frames N] [--size WxH] [--dump file.ppm]
 *
 * Default backend: deko3d on the Switch (an NRO, run from the homebrew menu: 600 frames, then it exits), Vulkan on a PC
 * (with GL as the fallback). Esc or closing the window quits. The output is a window that changes colour once a second:
 * the clear is drawn over by the backdrop so the picture shows the whole path (clear, draw_triangles with the
 * "shade only" combiner, present), and the log line names the backend and the GPU. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cmath>

#include "ge_render.h"

static volatile int s_quit = 0;

static void onLog(int level, const char *msg) {
    static const char *tags[] = { "ERROR", "WARN ", "NOTE ", "INFO ", "DEBUG" };
    fprintf(stderr, "[%s] %s\n", tags[level < 0 ? 0 : level > 4 ? 4 : level], msg);
    fflush(stderr);
}
static void onQuit(const char *why) { fprintf(stderr, "quit: %s\n", why); s_quit = 1; }
static void onKey(int sym, int mod, int repeat) { (void)mod; (void)repeat; if (sym == 27 /* SDLK_ESCAPE */) s_quit = 1; }

static void ortho(float m[16], float w, float h) {   /* pixels (y down) -> clip, row vectors */
    memset(m, 0, 16 * sizeof(float));
    m[0] = 2.0f / w;  m[5] = -2.0f / h;  m[10] = 1.0f;  m[12] = -1.0f;  m[13] = 1.0f;  m[15] = 1.0f;
}

int main(int argc, char **argv) {
    GeRenderConfig cfg = ge_render_cfg;
    int frames = 0, width = 1280, height = 720;
    const char *dump = NULL;   /* --dump: write frame 30 as a PPM (GL reads the back buffer, Vulkan writes it in the present) */
#ifdef __SWITCH__
    cfg.backend = GE_RENDER_DEKO3D;
    frames = 600;
#else
    cfg.backend = GE_RENDER_VULKAN;
#endif
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--backend") && i + 1 < argc) {
            const char *b = argv[++i];
            cfg.backend = !strcmp(b, "gl") ? GE_RENDER_GL : !strcmp(b, "vk") ? GE_RENDER_VULKAN : GE_RENDER_DEKO3D;
        } else if (!strcmp(argv[i], "--frames") && i + 1 < argc) {
            frames = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--dump") && i + 1 < argc) {
            dump = argv[++i];
        } else if (!strcmp(argv[i], "--size") && i + 1 < argc) {
            sscanf(argv[++i], "%dx%d", &width, &height);
        }
    }
    cfg.window.title = "ge_render_hello";
    cfg.window.width = (uint32_t)width;
    cfg.window.height = (uint32_t)height;
    cfg.window.x = 100;
    cfg.window.y = 100;
    cfg.window.centered = true;
    cfg.fog_vertex = 1;   /* nothing here uses fog */

    GeRenderCallbacks cb = {};
    cb.log = onLog;
    cb.on_quit = onQuit;
    cb.on_key_down = onKey;
    ge_render_open(&cfg, &cb);
    static const char *names[] = { "OpenGL", "Vulkan", "deko3d" };
    fprintf(stderr, "ge_render_hello: backend %s, \"%s\"\n", names[ge_render_backend()], ge_render_renderer_string());

    struct GfxRenderingAPI *api = ge_render_api();
    uint32_t w = 0, h = 0;
    int32_t px = 0, py = 0;
    ge_render_get_dimensions(&w, &h, &px, &py);
    api->update_framebuffer_parameters(0, w, h, 1, false, true, true, true);   /* framebuffer 0 is the window */

    for (int f = 0; !s_quit && (frames == 0 || f < frames); f++) {
        ge_render_poll_events();
        uint32_t nw = 0, nh = 0;
        ge_render_get_dimensions(&nw, &nh, &px, &py);
        if (nw == 0 || nh == 0) continue;
        if (nw != w || nh != h) {
            w = nw; h = nh;
            api->update_framebuffer_parameters(0, w, h, 1, false, true, true, true);
            api->on_resize();
        }
        ge_render_begin_frame();
        api->start_draw_to_framebuffer(0, 1.0f);
        api->set_viewport(0, 0, (int)w, (int)h);
        api->set_scissor(0, 0, (int)w, (int)h);
        api->clear_framebuffer(true, true);

        const float t = (float)f / 60.0f;
        const float backdrop[4] = { 0.5f + 0.5f * std::sin(t), 0.5f + 0.5f * std::sin(t + 2.1f), 0.5f + 0.5f * std::sin(t + 4.2f), 1.0f };
        float m[16];
        ortho(m, (float)w, (float)h);
        ge_debug_begin(m, (int)w, (int)h, false);
        const float c[3] = { w * 0.5f, h * 0.5f, 0.0f };
        ge_debug_point(c, backdrop, (float)(w > h ? w : h) * 1.5f);   /* the backdrop: a point so big it covers the window */
        const float white[4] = { 1, 1, 1, 1 }, yellow[4] = { 1, 0.9f, 0.1f, 1 };
        for (int i = 0; i < 12; i++) {
            const float a = t + (float)i * 0.5236f;
            const float p0[3] = { c[0], c[1], 0.0f };
            const float p1[3] = { c[0] + std::cos(a) * h * 0.4f, c[1] + std::sin(a) * h * 0.4f, 0.0f };
            ge_debug_line(p0, p1, i & 1 ? white : yellow, 4.0f);
            ge_debug_point(p1, white, 14.0f);
        }
        ge_debug_end();

        ge_render_end_frame();
        if (dump && f == 30) ge_render_dump_window(w, h, dump);
        ge_render_present();
    }

    ge_render_park();
    ge_render_close();
    return 0;
}
