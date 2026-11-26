/* ge_render: the library's front door (ge_render.h). Backend choice, window per backend, the host callbacks
 * and their defaults, the config, the frame calls. Everything the game or a tool needs from the renderer goes
 * through here; the backends and the window are reached through the two vtables they already had. */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#include "ge_internal.h"
#include "ge_window.h"
#include "gfx_opengl.h"
#ifdef GE_VULKAN
#include "gfx_vulkan.h"
#endif
#ifdef __SWITCH__
#include "gfx_deko3d.h"
#endif

/* ---- the backend in use ---------------------------------------------------------------------------------- */

static GeRenderBackend s_backend = GE_RENDER_GL;
static struct GfxRenderingAPI *s_rapi = &gfx_opengl_api;
static struct GfxWindowManagerAPI *s_wapi = &ge_window_sdl;
static bool s_open = false;
static GeRenderPasses s_passes = {
    gfx_opengl_fxaa_pass, gfx_opengl_skyplus_stage, gfx_opengl_skyplus_pass, gfx_opengl_interp_store,
    gfx_opengl_interp_show, gfx_opengl_interp_sync, gfx_opengl_frame_count_rewind, gfx_opengl_perf_report,
    false,
};

extern "C" {

bool ge_render_deko3d_wanted(void) {
#ifdef __SWITCH__
    return gfx_deko3d_wanted();
#else
    return false;
#endif
}

bool ge_render_backend_usable(GeRenderBackend b) {
    switch (b) {
        case GE_RENDER_GL: return true;
        case GE_RENDER_VULKAN:
#ifdef GE_VULKAN
            return gfx_vulkan_probe();
#else
            return false;
#endif
        case GE_RENDER_DEKO3D:
#ifdef __SWITCH__
            return true;
#else
            return false;
#endif
    }
    return false;
}

int ge_render_gl_usable(void) {
#ifdef GE_VULKAN
    return ge_window_gl_usable();
#else
    return 0;
#endif
}

bool ge_render_open(const GeRenderConfig *cfg, const GeRenderCallbacks *cb) {
    ge_core_set_callbacks(cb);
    GeRenderBackend want = GE_RENDER_GL;
    struct GfxWindowInitSettings win = {};
    if (cfg) {
        want = cfg->backend;
        win = cfg->window;
        ge_render_cfg.backend = cfg->backend;
        ge_render_cfg.window = cfg->window;
        ge_render_cfg.fxaa_on = cfg->fxaa_on;
        ge_render_cfg.per_pixel_fog = cfg->per_pixel_fog;
        ge_render_cfg.display_fixes = cfg->display_fixes;
        ge_render_cfg.skyplus_half = cfg->skyplus_half;
        ge_render_cfg.rdp_affine = cfg->rdp_affine;
        ge_render_cfg.fog_vertex = cfg->fog_vertex;
        ge_render_cfg.framebuffers_enabled = cfg->framebuffers_enabled;
        ge_render_cfg.skyplus_failed = cfg->skyplus_failed;
        ge_render_cfg.skyplus_sea = cfg->skyplus_sea;
        ge_render_cfg.water_surfaces = cfg->water_surfaces;
    }

    s_backend = GE_RENDER_GL;
    s_rapi = &gfx_opengl_api;
    switch (want) {
#ifdef __SWITCH__
        case GE_RENDER_DEKO3D: {   /* D973: no SDL window; the backend presents on the default nwindow */
            static const struct GeWindowExternal ext = { gfx_deko3d_present, gfx_deko3d_get_dimensions, gfx_deko3d_park,
                                                         gfx_deko3d_set_swap_interval };
            s_backend = GE_RENDER_DEKO3D;
            s_rapi = &gfx_deko3d_api;
            ge_window_attach(GE_WINDOW_NONE, &ext);
            s_passes = { gfx_deko3d_fxaa_pass, gfx_deko3d_skyplus_stage, gfx_deko3d_skyplus_pass, gfx_deko3d_interp_store,
                         gfx_deko3d_interp_show, gfx_deko3d_interp_sync, gfx_deko3d_frame_count_rewind,
                         gfx_deko3d_perf_report, true /* GE+ water (water/water.frag.glsl) */ };
            break;
        }
#endif
#ifdef GE_VULKAN
        case GE_RENDER_VULKAN:   /* issue #44: an SDL Vulkan window, the backend owns the swapchain */
            if (gfx_vulkan_probe()) {
                static const struct GeWindowExternal ext = { gfx_vulkan_present, gfx_vulkan_get_dimensions, gfx_vulkan_park,
                                                             gfx_vulkan_set_swap_interval };
                s_backend = GE_RENDER_VULKAN;
                s_rapi = &gfx_vulkan_api;
                ge_window_attach(GE_WINDOW_VULKAN, &ext);
                s_passes = { gfx_vulkan_fxaa_pass, gfx_vulkan_skyplus_stage, gfx_vulkan_skyplus_pass, gfx_vulkan_interp_store,
                             gfx_vulkan_interp_show, gfx_vulkan_interp_sync, gfx_vulkan_frame_count_rewind,
                             gfx_vulkan_perf_report, false };
            } else {
                ge_log(GE_LOG_WARNING, "ge_render: Vulkan selected but no usable Vulkan 1.3 driver; using OpenGL");
            }
            break;
#endif
        default:
            break;
    }
    if (s_backend == GE_RENDER_GL) {   /* GL, also the fallback of a backend that is not there */
        s_passes = { gfx_opengl_fxaa_pass, gfx_opengl_skyplus_stage, gfx_opengl_skyplus_pass, gfx_opengl_interp_store,
                     gfx_opengl_interp_show, gfx_opengl_interp_sync, gfx_opengl_frame_count_rewind, gfx_opengl_perf_report,
                     false };
        ge_window_attach(GE_WINDOW_GL, NULL);
    }
    ge_render_cfg.backend = s_backend;

    s_wapi = &ge_window_sdl;
    s_wapi->init(&win);   /* the backend's own window / surface */
    s_rapi->init();
    s_open = true;
    return true;
}

void ge_render_close(void) {
    ge_window_teardown_for_exit();
    s_open = false;
}

GeRenderBackend ge_render_backend(void) {
    return s_backend;
}

const char *ge_render_renderer_string(void) {
#ifdef __SWITCH__
    if (s_backend == GE_RENDER_DEKO3D) return gfx_deko3d_renderer_string();
#endif
#ifdef GE_VULKAN
    if (s_backend == GE_RENDER_VULKAN) return gfx_vulkan_renderer_string();
#endif
    return gfx_opengl_renderer_string();
}

struct GfxRenderingAPI *ge_render_api(void) {
    return s_rapi;
}

struct GfxWindowManagerAPI *ge_render_window(void) {
    return s_wapi;
}

const GeRenderPasses *ge_render_passes(void) {
    return &s_passes;
}

uint32_t ge_render_mesh_create(const float *data, size_t nfloats) {
    return s_rapi->mesh_create(data, nfloats);
}

void ge_render_mesh_delete(uint32_t name) {
    s_rapi->mesh_delete(name);
}

void ge_render_mesh_draw(uint32_t name, const struct GfxMeshDraw *d) {
    s_rapi->mesh_draw(name, d);
}

bool ge_render_mesh_supported(uint64_t cc_id0, uint32_t cc_id1) {
    return s_rapi->mesh_supported ? s_rapi->mesh_supported(cc_id0, cc_id1) : true;
}

void ge_render_poll_events(void) {
    s_wapi->handle_events();
}

void ge_render_begin_frame(void) {
    s_rapi->start_frame();
}

void ge_render_end_frame(void) {
    s_rapi->end_frame();
}

void ge_render_present_begin(void) {
    s_wapi->swap_buffers_begin();
}

void ge_render_present_end(void) {
    s_wapi->swap_buffers_end();
}

void ge_render_present(void) {
    s_wapi->swap_buffers_begin();
    s_rapi->finish_render();
    s_wapi->swap_buffers_end();
}

void ge_render_get_dimensions(uint32_t *width, uint32_t *height, int32_t *x, int32_t *y) {
    s_wapi->get_dimensions(width, height, x, y);
}

void ge_render_set_target_fps(int fps) {
    s_wapi->set_target_fps(fps);
}

void ge_render_set_pre_present_hook(void (*hook)(void)) {
    ge_window_pre_present = hook;
}

void ge_render_bind_thread(void) {
    ge_window_make_context_current();
}

void ge_render_release_thread(void) {
    ge_window_release_context();
}

void ge_render_park(void) {
    ge_window_park_for_exit();
}

void ge_render_update_size(void) {
    ge_window_update_cached_size();
}

void ge_render_poll_refresh_rate(void) {
    ge_window_poll_refresh_rate();
}

int ge_render_get_refresh_rate(void) {
    return ge_window_get_refresh_rate();
}

bool ge_render_dump_window(uint32_t width, uint32_t height, const char *path) {
#ifdef GE_VULKAN
    if (s_backend == GE_RENDER_VULKAN) return gfx_vulkan_dump_window(path);
#endif
    if (s_backend == GE_RENDER_GL) return gfx_opengl_dump_bound_fbo(width, height, path);
    return false;
}

}   /* extern "C" */
