#ifndef GFX_OPENGL_H
#define GFX_OPENGL_H

#include <stddef.h>
#include "gfx_rendering_api.h"

extern struct GfxRenderingAPI gfx_opengl_api;

#ifdef __cplusplus
extern "C" {
#endif
/* D482: GL_RENDERER captured at init ("" if unknown). */
const char *gfx_opengl_renderer_string(void);

/* Frame capture (dev tool, env GE_PCDUMP=1) — see gfx_opengl.cpp. */
bool gfx_opengl_pcdump_enabled(void);
bool gfx_opengl_read_pixels(uint32_t win_h, int x, int y, int w, int h, uint8_t* rgba);
bool gfx_opengl_fx_reload(const char* common, const char* bullets, const char* bursts, const char* impacts, char* log, size_t logsz);
bool gfx_opengl_dump_bound_fbo(uint32_t width, uint32_t height, const char* path);

/* D578: frame-interpolation present queue. store copies the finished back
 * buffer (width x height) into image `slot`; show copies it back to the back
 * buffer just before a swap. Slots are resized on demand. */
void gfx_opengl_interp_store(int slot, uint32_t width, uint32_t height);
void gfx_opengl_interp_show(int slot);

/* project-dam: FXAA pass, reads the single-sample colour texture of framebuffer
 * src_fb_id and draws it, anti-aliased, to the default framebuffer. */
void gfx_opengl_fxaa_pass(int src_fb_id);

/* GE+ sky pass and the rest of the passes outside the draw vtable (GeRenderPasses, ge_render.h). */
struct PortSkyPlus;
int gfx_opengl_skyplus_stage(const struct PortSkyPlus* p, const int vp[4], float aspA, float aspB);
void gfx_opengl_skyplus_pass(int slot);
void gfx_opengl_interp_sync(void);
void gfx_opengl_frame_count_rewind(void);
void gfx_opengl_perf_report(double frames);

/* Tool-only (ge_render_tool.h): GPU timers and the viewer variants of the GE+ sky and water shaders. */
struct GeRenderViewer;
void gfx_opengl_gpu_timer_begin(int id);
void gfx_opengl_gpu_timer_end(int id);
double gfx_opengl_gpu_timer_ms(int id);
bool gfx_opengl_viewer_enable(const void* pack, size_t size);
void gfx_opengl_viewer_set(const struct GeRenderViewer* v);
void gfx_opengl_viewer_disable(void);

#ifdef __cplusplus
}
#endif

#endif
