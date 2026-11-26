#ifndef GE_WINDOW_H
#define GE_WINDOW_H

/* The SDL window layer shared by the GL and Vulkan backends, and the "no window" mode of deko3d (the swapchain
 * on the default nwindow). Internal to ge_render: ge_render_open picks the mode with the backend. */

#include <stdint.h>
#include <stdbool.h>

#include "gfx_window_manager_api.h"

#ifdef __cplusplus
extern "C" {
#endif

extern struct GfxWindowManagerAPI ge_window_sdl;

/* How a backend with its own swapchain presents (the window then only pumps events and holds the SDL window, if any). */
struct GeWindowExternal {
    void (*present)(void);
    void (*dims)(uint32_t *w, uint32_t *h);
    void (*park)(void);
    void (*set_interval)(int interval);
};

enum GeWindowMode {
    GE_WINDOW_GL,           /* SDL window + GL context */
    GE_WINDOW_VULKAN,       /* SDL window (SDL_WINDOW_VULKAN), the Vulkan backend owns the swapchain */
    GE_WINDOW_NONE,         /* deko3d: no SDL window at all */
};

/* Before ge_window_sdl.init. `ext` is NULL for GL. */
void ge_window_attach(enum GeWindowMode mode, const struct GeWindowExternal *ext);
/* The SDL_Window* (Vulkan: SDL_Vulkan_CreateSurface). */
void *ge_window_sdl_window(void);

void ge_window_make_context_current(void);
void ge_window_release_context(void);
void ge_window_park_for_exit(void);
void ge_window_teardown_for_exit(void);
void ge_window_update_cached_size(void);
void ge_window_poll_refresh_rate(void);
int ge_window_get_refresh_rate(void);
int ge_window_gl_usable(void);

/* Runs with the finished frame still in the back buffer, just before the swap. */
extern void (*ge_window_pre_present)(void);

#ifdef __cplusplus
}
#endif

#endif
