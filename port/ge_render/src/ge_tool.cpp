/* ge_render's tool-only calls (ge_render_tool.h): GPU timers and the viewer shader variants. A dispatch to the backend in
 * use; the game never calls any of it. */

#include <stdlib.h>

#include "ge_internal.h"
#include "ge_render_tool.h"
#include "gfx_opengl.h"
#ifdef GE_VULKAN
#include "gfx_vulkan.h"
#endif
#ifdef __SWITCH__
#include "gfx_deko3d.h"
#endif

extern "C" {

void ge_render_gpu_timer_begin(int id) {
    if (id < 0 || id >= GE_GPU_TIMERS) return;
    switch (ge_render_backend()) {
#ifdef GE_VULKAN
        case GE_RENDER_VULKAN: gfx_vulkan_gpu_timer_begin(id); return;
#endif
#ifdef __SWITCH__
        case GE_RENDER_DEKO3D: gfx_deko3d_gpu_timer_begin(id); return;
#endif
        default: gfx_opengl_gpu_timer_begin(id); return;
    }
}

void ge_render_gpu_timer_end(int id) {
    if (id < 0 || id >= GE_GPU_TIMERS) return;
    switch (ge_render_backend()) {
#ifdef GE_VULKAN
        case GE_RENDER_VULKAN: gfx_vulkan_gpu_timer_end(id); return;
#endif
#ifdef __SWITCH__
        case GE_RENDER_DEKO3D: gfx_deko3d_gpu_timer_end(id); return;
#endif
        default: gfx_opengl_gpu_timer_end(id); return;
    }
}

double ge_render_gpu_timer_ms(int id) {
    if (id < 0 || id >= GE_GPU_TIMERS) return -1.0;
    switch (ge_render_backend()) {
#ifdef GE_VULKAN
        case GE_RENDER_VULKAN: return gfx_vulkan_gpu_timer_ms(id);
#endif
#ifdef __SWITCH__
        case GE_RENDER_DEKO3D: return gfx_deko3d_gpu_timer_ms(id);
#endif
        default: return gfx_opengl_gpu_timer_ms(id);
    }
}

bool ge_render_viewer_enable(const void *pack, size_t size) {
    switch (ge_render_backend()) {
#ifdef GE_VULKAN
        case GE_RENDER_VULKAN: return gfx_vulkan_viewer_enable(pack, size);
#endif
#ifdef __SWITCH__
        case GE_RENDER_DEKO3D: return gfx_deko3d_viewer_enable(pack, size);
#endif
        default: return gfx_opengl_viewer_enable(pack, size);
    }
}

void ge_render_viewer_set(const GeRenderViewer *v) {
    switch (ge_render_backend()) {
#ifdef GE_VULKAN
        case GE_RENDER_VULKAN: gfx_vulkan_viewer_set(v); return;
#endif
#ifdef __SWITCH__
        case GE_RENDER_DEKO3D: gfx_deko3d_viewer_set(v); return;
#endif
        default: gfx_opengl_viewer_set(v); return;
    }
}

void ge_render_tool_sky_div(int div) {
#ifdef __SWITCH__
    if (ge_render_backend() == GE_RENDER_DEKO3D) gfx_deko3d_tool_sky_div(div);
#else
    (void)div;
#endif
}

void ge_render_viewer_disable(void) {
    switch (ge_render_backend()) {
#ifdef GE_VULKAN
        case GE_RENDER_VULKAN: gfx_vulkan_viewer_disable(); return;
#endif
#ifdef __SWITCH__
        case GE_RENDER_DEKO3D: gfx_deko3d_viewer_disable(); return;
#endif
        default: gfx_opengl_viewer_disable(); return;
    }
}

}
