#ifndef GFX_DEKO3D_H
#define GFX_DEKO3D_H

/* D973: deko3d rendering backend for the Switch (the default; GE_RENDERER=gl selects GL).
 * Implements GfxRenderingAPI and replays GfxFrame exactly as the GL backend
 * does; the window side (swapchain on the default nwindow) belongs to the
 * backend, SDL keeps input/audio/events (ge_render_open attaches the present hooks, GE_WINDOW_NONE). */

#include <stdint.h>
#include <stdbool.h>

#include "gfx_rendering_api.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __SWITCH__
extern struct GfxRenderingAPI gfx_deko3d_api;

/* true unless GE_RENDERER=gl; read once. */
bool gfx_deko3d_wanted(void);
/* Submit the frame and present the current swapchain image (only if the frame
 * drew to fb 0: presenting an image nothing was drawn to flickers). */
void gfx_deko3d_present(void);
/* Quit: queue idle, swapchain then memory then device destroyed (replay thread). */
void gfx_deko3d_park(void);
/* Swapchain size (1280x720 handheld, 1920x1080 docked). */
void gfx_deko3d_get_dimensions(uint32_t* w, uint32_t* h);
void gfx_deko3d_set_swap_interval(int interval);
const char* gfx_deko3d_renderer_string(void);

/* Passes outside the rendering API (gfx_pc.cpp picks these or the GL ones). */
struct PortSkyPlus;
void gfx_deko3d_fxaa_pass(int src_fb_id);
int gfx_deko3d_skyplus_stage(const struct PortSkyPlus* p, const int vp[4], float aspA, float aspB);
void gfx_deko3d_skyplus_pass(int slot);
void gfx_deko3d_interp_store(int slot, uint32_t width, uint32_t height);
void gfx_deko3d_interp_show(int slot);
void gfx_deko3d_interp_sync(void);
void gfx_deko3d_frame_count_rewind(void);
void gfx_deko3d_perf_report(double frames);

/* Tool-only (ge_render_tool.h): GPU timers and the viewer variants of the GE+ sky and water shaders. */
struct GeRenderViewer;
void gfx_deko3d_gpu_timer_begin(int id);
void gfx_deko3d_gpu_timer_end(int id);
double gfx_deko3d_gpu_timer_ms(int id);
bool gfx_deko3d_viewer_enable(const void* pack, size_t size);
void gfx_deko3d_viewer_set(const struct GeRenderViewer* v);
void gfx_deko3d_viewer_disable(void);
void gfx_deko3d_tool_sky_div(int div);
#endif

#ifdef __cplusplus
}
#endif

#endif
