#ifndef GE_RENDER_TOOL_H
#define GE_RENDER_TOOL_H

/* ge_render's tool-only side (issue #145): what a dev tool (fxview and its tabs, ge_render_hello) needs and the game never
 * calls. Additive: nothing here is reached from the game's draw path, so the game renders exactly as before.
 *
 *   - a GPU timer (GL_TIME_ELAPSED, Vulkan timestamp queries, deko3d counter reports): the time of a stretch of the frame's
 *     GPU work, for the load mode of the tabs;
 *   - the viewer variants of the GE+ sky and water shaders (`#ifdef VIEWER` in port/shaders/sky and water): test variants
 *     and editable constants, compiled into a tool pack of their own, never into the game's. */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- GPU timer ------------------------------------------------------------------------------------------ */

#define GE_GPU_TIMERS 8

/* Brackets GPU work recorded between ge_render_begin_frame and ge_render_end_frame, on the render thread. id 0..7. Timers of
 * different ids may not overlap on GL (one GL_TIME_ELAPSED query at a time; do not combine with GE_PERFSTAT there). A pair
 * costs one query; the result arrives 2-4 frames later (nothing stalls). */
void ge_render_gpu_timer_begin(int id);
void ge_render_gpu_timer_end(int id);
/* The newest finished measurement of timer `id` in milliseconds; < 0 until the first one arrives, or when the backend has no
 * timestamps. Poll it once per frame (it collects the finished queries). */
double ge_render_gpu_timer_ms(int id);

/* ---- viewer variants of the sky and water shaders -------------------------------------------------------- */

/* The std140 block `Viewer` the VIEWER variants read (binding 8 in the GLSL): `ivec4 uVarv` (x = sea variant, y = sky variant)
 * and `vec4 uPrv[6]` (the PRM(i, d) values of sky/common.glsl, i = 0..23). */
typedef struct GeRenderViewer {
    int32_t var[4];
    float prm[24];
} GeRenderViewer;

/* Switches the backend to the viewer variants of skyplus, skylut and the water (and, on deko3d, the half-size water body).
 * `pack`: the tool pack (tools_pc/fxview/sea builds it: Vulkan SPIR-V, deko3d DKSH, the game's pack format with the entries
 * DK_PACK_SKYPLUS, DK_PACK_SKYLUT, DK_PACK_WATER[, DK_PACK_WATERBODY]); the GL backend compiles the variants itself and ignores
 * it. The pack must stay valid until ge_render_viewer_disable. false: the variants did not load (the game's shaders stay). */
bool ge_render_viewer_enable(const void *pack, size_t size);
/* The values the variants read, from the next sky or water draw on. */
void ge_render_viewer_set(const GeRenderViewer *v);
/* Back to the game's shaders. */
void ge_render_viewer_disable(void);

/* ---- the sky's reduced passes (deko3d) ---------------------------------------------------------------------- */

/* The divisor of the GE+ sky's reduced passes on deko3d: 1 full size, 2 half (the game's, the default), 4 quarter. GL and Vulkan
 * read ge_render_cfg.skyplus_half; deko3d has no such setting in the game (always half), so a tool that wants the full-size image
 * (a sea tab's scale option; Ryubing draws the half-size composite wrong) sets it here. No effect on other backends. */
void ge_render_tool_sky_div(int div);

#ifdef __cplusplus
}
#endif

#endif
