#ifndef GE_RENDER_H
#define GE_RENDER_H

/* ge_render: the renderer library (issue #142).
 *
 * The backends (deko3d, Vulkan, OpenGL), the combiner -> shader generator, the shader packs, the window and
 * the static-mesh API. It knows no game and no N64 code: fast3d (port/fast3d) translates the N64 display lists
 * into the draw API below, a tool (fxview, the editor, ge_render_hello) draws through the same calls.
 *
 * The API keeps its N64 vocabulary (combiner ids, RDP blend and depth modes, clamp/mirror), so a tool draws an
 * effect exactly as the game does. ge_render_cc_id() builds a combiner id.
 *
 * Shape:
 *   - no built-in loop: the host owns it. ge_render_open() creates the backend's own window or surface
 *     (Vulkan: SDL window + surface; GL: SDL window + context; deko3d: the default nwindow, no SDL window),
 *     the host calls ge_render_poll_events(), ge_render_begin_frame(), draws, ge_render_end_frame(),
 *     ge_render_present(), and ge_render_close().
 *   - events go out through the host's callbacks (GeRenderCallbacks); so do log, fatal error, command line,
 *     paths, sleep. Every callback may be NULL: the defaults log to stderr and abort.
 *   - settings are one struct, ge_render_cfg: the host writes the fields it owns (live ones whenever it likes,
 *     the render thread reads them each frame), the backends write the ones they decide.
 *   - threads are the host's: open/poll_events/close on the host thread, the draw calls, present and the rest
 *     on the one thread that bound the context (ge_render_bind_thread), exactly as the game uses them. */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "ge_render_cfg.h"
#include "gfx_rendering_api.h"
#include "gfx_window_manager_api.h"
#include "ge_render_sky.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- host callbacks ------------------------------------------------------------------------------------ */

enum { GE_LOG_ERROR = 0, GE_LOG_WARNING, GE_LOG_NOTE, GE_LOG_INFO, GE_LOG_DEBUG };

typedef struct GeRenderCallbacks {
    /* System. */
    void (*log)(int level, const char *msg);          /* one formatted line, no newline */
    void (*fatal)(const char *msg);                   /* must not return */
    int (*arg_check)(const char *arg);                /* the command line has this argument */
    const char *(*arg_get_string)(const char *arg);   /* the value after it, or NULL */
    const char *(*resolve_path)(const char *path);    /* "$S/..." -> a real path (static buffer) */
    void (*sleep_us)(uint32_t micros);
    void (*cpu_relax)(void);
    int (*is_handheld)(void);                         /* Switch: undocked */
    int (*is_console)(void);                          /* the window IS the panel (handheld Linux): full desktop size, fullscreen */

    /* Events, called from ge_render_poll_events on whichever thread polls. */
    void (*on_quit)(const char *why);                                 /* window closed, SDL_QUIT */
    void (*on_key_down)(int sdl_keysym, int sdl_mod, int repeat);     /* every key press; Alt+Enter is handled by the window */
    void (*on_mouse_button_down)(int button);
    void (*on_mouse_wheel)(int y);
    void (*on_text)(const char *utf8);
    void (*on_pad_button_down)(int pad_id, int button, uint32_t timestamp);
    void (*on_pad_axis)(int pad_id, int axis, int value, uint32_t timestamp);
    void (*on_pad_device_change)(void);               /* a controller was added or removed */
    void (*on_audio_device_removed)(uint32_t device_id);
    void (*on_resize)(int width, int height);         /* the drawable size changed */
    void (*on_focus)(int gained);
    void (*on_dock_change)(int handheld);             /* the is_handheld callback's value changed (polled each poll_events) */
} GeRenderCallbacks;

/* ---- open / close -------------------------------------------------------------------------------------- */

/* Creates the window or surface and the backend. false: nothing usable (the fatal callback usually ends the process first). */
bool ge_render_open(const GeRenderConfig *cfg, const GeRenderCallbacks *cb);
/* Host thread, after the render thread parked (ge_render_park): destroys the context and the window. */
void ge_render_close(void);

GeRenderBackend ge_render_backend(void);           /* the one in use */
bool ge_render_backend_usable(GeRenderBackend b);  /* Vulkan: the loader and a 1.3 device with dynamic rendering */
bool ge_render_deko3d_wanted(void);                /* Switch: true unless GE_RENDERER=gl */
/* First-launch GPU probe on a Vulkan player: 1 if a throw-away hidden window gets any GL context. */
int ge_render_gl_usable(void);
const char *ge_render_renderer_string(void);       /* the GPU, "" if unknown */

/* ---- the draw API -------------------------------------------------------------------------------------- */

/* The backend's draw vtable (gfx_rendering_api.h): shaders from combiner ids, textures, framebuffers, depth/blend
 * state, draw_triangles, the static meshes. */
struct GfxRenderingAPI *ge_render_api(void);
/* The window vtable (gfx_window_manager_api.h): fullscreen, size, vsync, display modes. */
struct GfxWindowManagerAPI *ge_render_window(void);

/* Passes outside the draw vtable. Backend-specific functions behind one table. */
typedef struct GeRenderPasses {
    void (*fxaa)(int src_fb_id);   /* reads the single-sample colour of framebuffer src_fb_id, draws it anti-aliased to the window */
    int (*sky_stage)(const PortSkyPlus *p, const int vp[4], float aspA, float aspB);   /* stage one GE+ sky draw (decoder thread) */
    void (*sky_pass)(int slot);                                                        /* replay it (render thread) */
    void (*interp_store)(int slot, uint32_t width, uint32_t height);   /* frame interpolation present slots: copy the back buffer in */
    void (*interp_show)(int slot);
    void (*interp_sync)(void);
    void (*frame_count_rewind)(void);
    void (*perf_report)(double frames);
    bool water_ext;   /* the backend draws the GE+ water on the level's own water surfaces */
} GeRenderPasses;
const GeRenderPasses *ge_render_passes(void);

/* Static meshes (GPU vertex transform): wrappers over the vtable. */
uint32_t ge_render_mesh_create(const float *data, size_t nfloats);
void ge_render_mesh_delete(uint32_t name);
void ge_render_mesh_draw(uint32_t name, const struct GfxMeshDraw *d);
bool ge_render_mesh_supported(uint64_t cc_id0, uint32_t cc_id1);

/* A combiner id (id0) from its terms: c[cycle][0 = colour, 1 = alpha][A, B, C, D], each a SHADER_* value
 * (gfx_cc.h: SHADER_0, SHADER_INPUT_n, SHADER_TEXEL0, ...). The options (id1) are the SHADER_OPT_* bits. */
uint64_t ge_render_cc_id(const uint8_t c[2][2][4]);
/* The combiner "shade colour only" (colour and alpha = INPUT_1) and the one "texel 0 times shade". */
uint64_t ge_render_cc_id_shade(void);
uint64_t ge_render_cc_id_texel0_shade(void);

/* ---- the frame ----------------------------------------------------------------------------------------- */

void ge_render_poll_events(void);   /* pumps the SDL events, calls the host's on_* callbacks; the host thread (or the render thread where it owns the pump) */
void ge_render_begin_frame(void);   /* the backend's start_frame */
void ge_render_end_frame(void);     /* the backend's end_frame */
/* Present: pace to the target fps, the pre-present hook, swap (begin) ... finish the GPU work, (end). present() is both. */
void ge_render_present_begin(void);
void ge_render_present_end(void);
void ge_render_present(void);
void ge_render_get_dimensions(uint32_t *width, uint32_t *height, int32_t *x, int32_t *y);   /* the drawable */
void ge_render_set_target_fps(int fps);
void ge_render_set_pre_present_hook(void (*hook)(void));   /* runs with the finished frame still in the back buffer (captures) */

/* Context ownership between threads (GL: one thread at a time; Vulkan and deko3d: no-ops except park). */
void ge_render_bind_thread(void);      /* make the context current on the calling thread */
void ge_render_release_thread(void);   /* release it (the host thread after init, so the render thread can bind) */
void ge_render_park(void);             /* quit: drain the GPU, unbind / destroy the swapchain (render thread) */
void ge_render_update_size(void);      /* host thread, after a resize: refresh the cached drawable size */
void ge_render_poll_refresh_rate(void);   /* host thread: the window's display refresh rate ... */
int ge_render_get_refresh_rate(void);     /* ... readable anywhere; 0 = unknown */

/* Frame capture (dev tool): read the window back as a PPM. GL: now (call from the pre-present hook); Vulkan: the next present writes it. */
bool ge_render_dump_window(uint32_t width, uint32_t height, const char *path);

/* ---- debug drawing ------------------------------------------------------------------------------------- */

/* Lines and points in flat colour, with or without the depth test (light gizmos, normals, wireframe). A batch:
 * begin gives the world -> clip matrix (row vectors: clip = (x y z 1) * m, the clip space of the draw API), the
 * size in pixels of the viewport the draw API has set (primitive sizes are in pixels) and the depth test; the
 * primitives are collected and drawn at end as screen-space quads (colours are straight alpha, blended over what
 * is there). Between begin and end the host draws nothing else. The flat colour is the combiner "shade only"
 * (ge_render_cc_id_shade, SHADER_OPT_ALPHA), which every pack has. */
void ge_debug_begin(const float clip_from_world[16], int vp_width, int vp_height, bool depth_test);
void ge_debug_line(const float a[3], const float b[3], const float rgba[4], float width_px);
void ge_debug_point(const float p[3], const float rgba[4], float size_px);
void ge_debug_end(void);

#ifdef __cplusplus
}
#endif

#endif
