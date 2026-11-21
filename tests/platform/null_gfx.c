/*
 * Null graphics backend for the tests: a fast3d window manager and rendering
 * API that do nothing (no window, no GL/Vulkan). Every callback is set so a
 * test can hand the structs to the fast3d glue without a null check.
 */
#include <stddef.h>
/* The game headers first: on MinGW SDL.h reaches src/bondtypes.h, whose
 * `typedef s32 bool` fails once <stdbool.h> has made bool a macro. */
#include "null_platform.h"
#include "gfx_window_manager_api.h"
#include "gfx_rendering_api.h"

unsigned gNullGfxCalls;

#define HIT() (gNullGfxCalls++)

/* ---- window manager ---- */
static void w_init(const struct GfxWindowInitSettings *s) { (void)s; }
static void w_close(void) {}
static int  w_get_display_mode(int n, int *w, int *h) { (void)n; if (w) *w = 640; if (h) *h = 480; return 1; }
static int  w_get_current_display_mode(int *w, int *h) { if (w) *w = 640; if (h) *h = 480; return 1; }
static int  w_get_num_display_modes(void) { return 1; }
static int32_t w_zero(void) { return 0; }
static void w_set_fs_cb(void (*cb)(bool)) { (void)cb; }
static void w_set_bool(bool b) { (void)b; }
static void w_set_flag(int32_t m) { (void)m; }
static void w_get_refresh(uint32_t *r) { if (r) *r = 60; }
static void w_set_closest(int32_t w, int32_t h, bool c) { (void)w; (void)h; (void)c; }
static void w_set_dims(uint32_t w, uint32_t h, int32_t x, int32_t y) { (void)w; (void)h; (void)x; (void)y; }
static void w_get_dims(uint32_t *w, uint32_t *h, int32_t *x, int32_t *y)
{
    if (w) *w = 640;
    if (h) *h = 480;
    if (x) *x = 0;
    if (y) *y = 0;
}
static void w_get_centered(int32_t w, int32_t h, int32_t *x, int32_t *y) { (void)w; (void)h; if (x) *x = 0; if (y) *y = 0; }
static void w_void(void) {}
static bool w_start_frame(void) { return true; }
static double w_time(void) { return 0.0; }
static void w_set_fps(int f) { (void)f; }
static bool w_false(void) { return false; }
static void *w_null(void) { return NULL; }
static void w_set_title(const char *t) { (void)t; }
static int  w_get_swap(void) { return 0; }
static bool w_set_swap(int i) { (void)i; return true; }

struct GfxWindowManagerAPI gNullWindowApi = {
    .init = w_init,
    .close = w_close,
    .get_display_mode = w_get_display_mode,
    .get_current_display_mode = w_get_current_display_mode,
    .get_num_display_modes = w_get_num_display_modes,
    .get_fullscreen_state = w_zero,
    .set_fullscreen_changed_callback = w_set_fs_cb,
    .set_fullscreen = w_set_bool,
    .set_fullscreen_exclusive = w_set_bool,
    .set_fullscreen_flag = w_set_flag,
    .get_fullscreen_flag_mode = w_zero,
    .get_maximized_state = w_zero,
    .set_maximize = w_set_bool,
    .get_active_window_refresh_rate = w_get_refresh,
    .set_cursor_visibility = w_set_bool,
    .set_closest_resolution = w_set_closest,
    .set_dimensions = w_set_dims,
    .get_dimensions = w_get_dims,
    .get_centered_positions = w_get_centered,
    .handle_events = w_void,
    .start_frame = w_start_frame,
    .swap_buffers_begin = w_void,
    .swap_buffers_end = w_void,
    .get_time = w_time,
    .get_target_fps = w_zero,
    .set_target_fps = w_set_fps,
    .can_disable_vsync = w_false,
    .get_window_handle = w_null,
    .set_window_title = w_set_title,
    .get_swap_interval = w_get_swap,
    .set_swap_interval = w_set_swap,
};

/* ---- rendering API ---- */
static const char *r_name(void) { return "null"; }
static int  r_max_tex(void) { return 4096; }
static struct GfxClipParameters r_clip(void) { struct GfxClipParameters c = { false, false }; return c; }
static void r_unload_shader(struct ShaderProgram *p) { (void)p; HIT(); }
static void r_load_shader(struct ShaderProgram *p) { (void)p; HIT(); }
static struct ShaderProgram *r_create_shader(uint64_t a, uint32_t b) { (void)a; (void)b; HIT(); return NULL; }
static struct ShaderProgram *r_lookup_shader(uint64_t a, uint32_t b) { (void)a; (void)b; return NULL; }
static void r_shader_info(struct ShaderProgram *p, uint8_t *n, bool u[2])
{
    (void)p;
    if (n) *n = 0;
    if (u) u[0] = u[1] = false;
}
static void r_clear_shaders(void) { HIT(); }
static uint32_t r_new_texture(void) { HIT(); return 1; }
static void r_select_texture(int t, uint32_t id, bool l) { (void)t; (void)id; (void)l; HIT(); }
static void r_upload_texture(const uint8_t *b, uint32_t w, uint32_t h, bool m) { (void)b; (void)w; (void)h; (void)m; HIT(); }
static void r_set_sampler(int s, bool l, uint32_t a, uint32_t b, bool m) { (void)s; (void)l; (void)a; (void)b; (void)m; HIT(); }
static void r_set_depth_mode(bool a, bool b, bool c, bool d, uint16_t z) { (void)a; (void)b; (void)c; (void)d; (void)z; HIT(); }
static void r_set_depth_range(float n, float f) { (void)n; (void)f; HIT(); }
static void r_set_rect(int x, int y, int w, int h) { (void)x; (void)y; (void)w; (void)h; HIT(); }
static void r_set_use_alpha(bool a, bool m) { (void)a; (void)m; HIT(); }
static void r_draw_triangles(float *b, size_t n, size_t t) { (void)b; (void)n; (void)t; HIT(); }
static void r_void(void) { HIT(); }
static int  r_create_fb() { HIT(); return 0; }
static void r_update_fb(int id, uint32_t w, uint32_t h, uint32_t msaa, bool inv, bool rt, bool depth, bool extract)
{
    (void)id; (void)w; (void)h; (void)msaa; (void)inv; (void)rt; (void)depth; (void)extract;
    HIT();
}
static bool r_start_draw_fb(int id, float noise) { (void)id; (void)noise; HIT(); return true; }
static void r_copy_fb(int d, int s, int l, int t, bool f, bool b) { (void)d; (void)s; (void)l; (void)t; (void)f; (void)b; HIT(); }
static void r_clear_fb(bool c, bool d) { (void)c; (void)d; HIT(); }
static void r_clear_region(bool c, bool d, int x, int y, int w, int h) { (void)c; (void)d; (void)x; (void)y; (void)w; (void)h; HIT(); }
static void r_resolve(int a, int b) { (void)a; (void)b; HIT(); }
static void *r_fb_tex(int id) { (void)id; return NULL; }
static void r_select_tex_fb(int id) { (void)id; HIT(); }
static void r_delete_texture(uint32_t id) { (void)id; HIT(); }
static void r_set_filter(enum FilteringMode m) { (void)m; }
static enum FilteringMode r_get_filter(void) { return FILTER_NONE; }
static void r_set_mip(enum MipmapFilteringMode m) { (void)m; }
static void r_set_aniso(int l) { (void)l; }
static int  r_get_aniso(void) { return 1; }
static float *r_stream_begin(size_t n) { (void)n; return NULL; }
static void r_load_shader_with(struct ShaderProgram *p, uint32_t fc, float ns, bool a, bool b)
{
    (void)p; (void)fc; (void)ns; (void)a; (void)b;
    HIT();
}
static uint32_t r_mesh_create(const float *d, size_t n) { (void)d; (void)n; HIT(); return 0; }
static void r_mesh_delete(uint32_t n) { (void)n; }
static void r_mesh_draw(uint32_t n, const struct GfxMeshDraw *d) { (void)n; (void)d; HIT(); }
static bool r_mesh_supported(uint64_t a, uint32_t b) { (void)a; (void)b; return false; }

struct GfxRenderingAPI gNullRenderingApi = {
    .get_name = r_name,
    .get_max_texture_size = r_max_tex,
    .get_clip_parameters = r_clip,
    .unload_shader = r_unload_shader,
    .load_shader = r_load_shader,
    .create_and_load_new_shader = r_create_shader,
    .lookup_shader = r_lookup_shader,
    .shader_get_info = r_shader_info,
    .clear_shaders = r_clear_shaders,
    .new_texture = r_new_texture,
    .select_texture = r_select_texture,
    .upload_texture = r_upload_texture,
    .set_sampler_parameters = r_set_sampler,
    .set_depth_mode = r_set_depth_mode,
    .set_depth_range = r_set_depth_range,
    .set_viewport = r_set_rect,
    .set_scissor = r_set_rect,
    .set_use_alpha = r_set_use_alpha,
    .draw_triangles = r_draw_triangles,
    .init = r_void,
    .on_resize = r_void,
    .start_frame = r_void,
    .end_frame = r_void,
    .finish_render = r_void,
    .create_framebuffer = r_create_fb,
    .update_framebuffer_parameters = r_update_fb,
    .start_draw_to_framebuffer = r_start_draw_fb,
    .copy_framebuffer = r_copy_fb,
    .clear_framebuffer = r_clear_fb,
    .clear_region = r_clear_region,
    .resolve_msaa_color_buffer = r_resolve,
    .get_framebuffer_texture_id = r_fb_tex,
    .select_texture_fb = r_select_tex_fb,
    .delete_texture = r_delete_texture,
    .set_texture_filter = r_set_filter,
    .get_texture_filter = r_get_filter,
    .set_mipmap_filter = r_set_mip,
    .set_anisotropy_level = r_set_aniso,
    .get_max_anisotropy_level = r_get_aniso,
    .stream_begin = r_stream_begin,
    .load_shader_with = r_load_shader_with,
    .mesh_create = r_mesh_create,
    .mesh_delete = r_mesh_delete,
    .mesh_draw = r_mesh_draw,
    .mesh_supported = r_mesh_supported,
};
