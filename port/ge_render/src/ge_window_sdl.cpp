#include <stdio.h>
#include <string.h>
#include <SDL.h>
#include <time.h>

#include "ge_internal.h"
#include "ge_window.h"
#include "gfx_window_manager_api.h"
#include "gfx_screen_config.h"
#include "ge007_icon.h"
#if defined(__APPLE__)
#include <OpenGL/OpenGL.h>   /* D952: CGL swap interval */
#endif
#ifdef GE_VULKAN
#include <SDL_vulkan.h>   /* issue #44 */
#endif

static SDL_Window* wnd;
static SDL_GLContext ctx;
static SDL_Renderer* renderer;
static int sdl_to_lus_table[512];
static bool vsync_enabled = true;

/* D952: macOS swap interval through CGL, not SDL. SDL3's Cocoa GL VSync makes
 * SDL_GL_SwapWindow wait, with no timeout, for a tick from the CVDisplayLink it
 * starts when the context is created. On some first launches of a never-seen
 * binary that display link never starts (no CVDisplayLink thread in the hung
 * process), so the first swap blocks forever: no frame, a black window. SDL's
 * own interval stays 0 (its swap never waits) and CGL's kCGLCPSwapInterval does
 * the VSync inside flushBuffer, as SDL2 did on macOS. Must be called with the
 * context current (both callers are: init on the host thread, live config on
 * the render thread). */
#if defined(__APPLE__)
static int s_cglSwapInterval = 0;
static bool gfx_sdl_apply_swap_interval(int interval) {
    SDL_GL_SetSwapInterval(0);
    CGLContextObj cgl = CGLGetCurrentContext();
    GLint v = interval ? 1 : 0;
    if (!cgl || CGLSetParameter(cgl, kCGLCPSwapInterval, &v) != kCGLNoError) {
        return false;
    }
    s_cglSwapInterval = v;
    {
        GLint back = -1;
        CGLGetParameter(cgl, kCGLCPSwapInterval, &back);
        ge_log(GE_LOG_INFO, "SDL: swap interval %d via CGL (read back %d)", (int)v, (int)back);
    }
    return true;
}
#endif
// OTRTODO: These are redundant. Info can be queried from SDL.
static int window_width = DESIRED_SCREEN_WIDTH;
static int window_height = DESIRED_SCREEN_HEIGHT;
static uint32_t fullscreen_flag = SDL_WINDOW_FULLSCREEN_DESKTOP;
static bool fullscreen_state;
static bool maximized_state;
static bool is_running = true;
static void (*on_fullscreen_changed_callback)(bool is_now_fullscreen);

// GE port: frame pacing is owned by the cooperative thread kernel's vsync
// tick (libultra.c posts one VI retrace per frame at the console rate), so
// fast3d's internal busy-wait sync is disabled to avoid double-pacing. The
// GL swap still honors display vsync (vsync_enabled below).
static int target_fps = 0;
static uint64_t previous_time;
static uint64_t qpc_freq;

#define FRAME_INTERVAL_US_NUMERATOR 1000000
#define FRAME_INTERVAL_US_DENOMINATOR (target_fps)

static int32_t gfx_sdl_get_maximized_state(void) {
    return (int32_t)maximized_state;
}

static int32_t gfx_sdl_get_fullscreen_state(void) {
    return (int32_t)fullscreen_state;
}

static int32_t gfx_sdl_get_fullscreen_flag_mode(void) {
    return fullscreen_flag == SDL_WINDOW_FULLSCREEN_DESKTOP ? 0 : 1;
}

static void gfx_sdl_set_fullscreen_flag(int32_t mode) {
    switch (mode) {
        case 0: {
            fullscreen_flag = SDL_WINDOW_FULLSCREEN_DESKTOP;
        } break;
        case 1: {
            fullscreen_flag = SDL_WINDOW_FULLSCREEN;
        } break;
    }
}

/* D639: set once a resolution is picked explicitly (gfx_sdl_set_closest_resolution). */
static bool exclusive_mode_chosen = false;

static void set_fullscreen(bool on, bool call_callback) {
    fullscreen_state = on;
    /* D639: exclusive fullscreen switches the monitor to the window's display
     * mode; with none set, SDL derives it from the current WINDOW size, so a
     * 1440x1080 window asked the monitor for ~1440x1080 instead of its desktop
     * resolution (seen with NVIDIA "OpenGL present method = Prefer native").
     * Unless a resolution was picked explicitly, use the desktop mode of the
     * display the window is on. */
    if (on && fullscreen_flag == SDL_WINDOW_FULLSCREEN && !exclusive_mode_chosen) {
        SDL_DisplayMode dm;
        const int disp = SDL_GetWindowDisplayIndex(wnd);
        if (SDL_GetDesktopDisplayMode(disp >= 0 ? disp : 0, &dm) == 0 && dm.w > 0 && dm.h > 0) {
            SDL_SetWindowDisplayMode(wnd, &dm);
        }
    }
    SDL_SetWindowFullscreen(wnd, on ? fullscreen_flag : 0);
    if (call_callback && on_fullscreen_changed_callback) {
        on_fullscreen_changed_callback(on);
    }
}

static void set_maximize_window(bool on) {
	maximized_state = on;
	if (on) {
		SDL_MaximizeWindow(wnd);
	} else {
		SDL_RestoreWindow (wnd);
	}
}

static void gfx_sdl_get_active_window_refresh_rate(uint32_t* refresh_rate) {
    int display_in_use = SDL_GetWindowDisplayIndex(wnd);

    SDL_DisplayMode mode;
    SDL_GetCurrentDisplayMode(display_in_use, &mode);
    *refresh_rate = mode.refresh_rate;
}

// D581 (#151): SDL lists the OS primary display first (X11 RandR primary,
// Windows MONITORINFOF_PRIMARY, macOS main display), so display 0 IS the
// primary. The original bug was centring with display 0's size as absolute
// coordinates, ignoring its position; callers now use its global bounds.
// Not "the display containing (0,0)": on X11 the primary can sit right of a
// secondary at the origin (the #151 layout; verified on KDE X11, 2026-10-13).
static int primary_display_index() {
    return 0;
}

/* D973: deko3d mode (the Switch default; GE_RENDERER=gl selects GL): the rendering backend owns the swapchain
 * on the default nwindow; SDL keeps input, audio and the event pump but creates no window and no GL context
 * (GE_WINDOW_NONE). Issue #44: the Vulkan backend uses the same hooks, with an SDL Vulkan window and no GL
 * context (GE_WINDOW_VULKAN). ge_render_open attaches the mode with the backend (ge_window_attach). */
static enum GeWindowMode s_mode = GE_WINDOW_GL;
static struct GeWindowExternal s_ext;
static bool s_has_ext = false;
static int s_ext_swap_interval = 1;

extern "C" void ge_window_attach(enum GeWindowMode mode, const struct GeWindowExternal *ext) {
    s_mode = mode;
    s_has_ext = ext != NULL;
    if (ext) s_ext = *ext;
}

extern "C" void *ge_window_sdl_window(void) {
    return (void*)wnd;
}

/* Drawable size in pixels: the GL one, or the Vulkan one for a Vulkan window
 * (issue #44; SDL_GL_GetDrawableSize knows only GL windows). */
static void drawable_size(int *w, int *h) {
#ifdef GE_VULKAN
    if (s_mode == GE_WINDOW_VULKAN) {
        SDL_Vulkan_GetDrawableSize(wnd, w, h);
        return;
    }
#endif
    SDL_GL_GetDrawableSize(wnd, w, h);
}

static void create_gl_window(const char *title, int posX, int posY, Uint32 flags) {
    // ideally we need 3.0 compat
    // if that doesn't work, try 3.2 core in case we're on mac, 2.1 compat as a last resort
    static uint32_t glver[][3] = {
        { 0, 0, 0                                    }, // for command line override
        { 3, 0, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY }, // 3.0: default, has all the features required
        { 4, 1, SDL_GL_CONTEXT_PROFILE_CORE          }, // 4.1core: macs only have core profile and this is the latest
        { 3, 2, SDL_GL_CONTEXT_PROFILE_CORE          }, // 3.2core: older macs will only have this at best
        { 3, 0, SDL_GL_CONTEXT_PROFILE_ES            }, // es3: don't really support ES properly, but we can try
        { 2, 1, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY }, // 2.1: absolute last resort, will still require GLSL130 as an extension
    };

    uint32_t verstart = 1;
    const uint32_t verend = sizeof(glver) / sizeof(*glver);
    const char *verstr = ge_arg_get_string("--gl-version");
    if (verstr && *verstr) {
        // user override
        glver[0][2] = strstr(verstr, "core") ? SDL_GL_CONTEXT_PROFILE_CORE :
            (strstr(verstr, "es") ? SDL_GL_CONTEXT_PROFILE_ES :
            SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
        sscanf(verstr, "%d.%d", &glver[0][0], &glver[0][1]);
        if (glver[0][0] >= 1 && glver[0][0] <= 4 && glver[0][1] < 9) {
            verstart = 0;
        }
    }

    ctx = NULL;
    uint32_t vmin = 0, vmaj = 0, vprof = SDL_GL_CONTEXT_PROFILE_COMPATIBILITY;
    const char *vprofstr = "";
    for (uint32_t i = verstart; i < verend && !ctx; ++i) {
        vmaj = glver[i][0];
        vmin = glver[i][1];
        vprof = glver[i][2];
        vprofstr = (vprof == SDL_GL_CONTEXT_PROFILE_CORE ? "core" :
            (vprof == SDL_GL_CONTEXT_PROFILE_ES ? "es" : ""));

        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, vmaj);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, vmin);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, vprof);

        wnd = SDL_CreateWindow(title, posX, posY, window_width, window_height, flags);
        if (!wnd) {
            ge_log(GE_LOG_WARNING, "SDL: could not open SDL window for GL%d.%d%s:\n%s", vmaj, vmin, vprofstr, SDL_GetError());
            continue;
        }

        ctx = SDL_GL_CreateContext(wnd);
        if (!ctx) {
            ge_log(GE_LOG_WARNING, "SDL: could not create GL%d.%d%s context: %s", vmaj, vmin, vprofstr, SDL_GetError());
            SDL_DestroyWindow(wnd);
            wnd = nullptr;
        }
#if defined(__linux__) && !defined(__SWITCH__)
        /* A desktop context is only worth keeping if it reaches what the desktop path needs (GLSL 3.30: FXAA, the
         * blits): Mesa drivers such as V3D (Raspberry Pi 4/5, GL 3.1 + GLES 3.1) hand out a "3.0 compat" context
         * that is below it, and the ES 3.0 entry further down then works fully. Linux only: there a GLES context
         * through EGL is always the next try; macOS has no ES and keeps its 3.2 core. A --gl-version override and
         * the 2.1 last resort are never refused. */
        if (ctx && i != 0 && vprof != SDL_GL_CONTEXT_PROFILE_ES && !(vmaj == 2 && vmin == 1)) {
            typedef const unsigned char *(*GetStringFn)(unsigned int);
            GetStringFn getString = (GetStringFn)SDL_GL_GetProcAddress("glGetString");
            const char *ver = getString ? (const char *)getString(0x1F02 /* GL_VERSION */) : NULL;
            int gmaj = 0, gmin = 0;
            if (ver && sscanf(ver, "%d.%d", &gmaj, &gmin) == 2 && (gmaj < 3 || (gmaj == 3 && gmin < 3))) {
                ge_log(GE_LOG_NOTE, "SDL: GL%d.%d%s context is GL %d.%d (\"%s\"), below the 3.3 the desktop path needs; trying the next one",
                             vmaj, vmin, vprofstr, gmaj, gmin, ver);
                SDL_GL_DeleteContext(ctx);
                ctx = nullptr;
                SDL_DestroyWindow(wnd);
                wnd = nullptr;
            }
        }
#endif
    }

    if (!wnd || !ctx) {
        ge_fatal("Could not open SDL window with an OpenGL context of any supported version:\n%s", SDL_GetError());
    } else {
        ge_log(GE_LOG_NOTE, "SDL: created GL%d.%d%s context", vmaj, vmin, vprofstr);
    }

    SDL_GL_MakeCurrent(wnd, ctx);
#ifdef __SWITCH__
    {   // project-dam: the surface SDL created (720p handheld, 1080p docked)
        int ww = 0, wh = 0, dw = 0, dh = 0;
        SDL_DisplayMode dm = {};
        SDL_GetWindowSize(wnd, &ww, &wh);
        SDL_GL_GetDrawableSize(wnd, &dw, &dh);
        SDL_GetCurrentDisplayMode(0, &dm);
        ge_log(GE_LOG_NOTE, "SDL(switch): requested %dx%d, window %dx%d, drawable %dx%d, display %dx%d",
                     window_width, window_height, ww, wh, dw, dh, dm.w, dm.h);
    }
#endif
#if defined(__APPLE__)
    gfx_sdl_apply_swap_interval(1);   /* D952 */
#else
    SDL_GL_SetSwapInterval(1);
#endif
}

#ifdef GE_VULKAN
extern "C" int ge_window_gl_usable(void) {
    static const int vers[][3] = {   /* the same tries as create_gl_window, minus the 2.1 last resort */
        { 3, 0, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY }, { 4, 1, SDL_GL_CONTEXT_PROFILE_CORE },
        { 3, 2, SDL_GL_CONTEXT_PROFILE_CORE }, { 3, 0, SDL_GL_CONTEXT_PROFILE_ES },
    };
    int ok = 0;
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) return 0;
    for (size_t i = 0; i < sizeof(vers) / sizeof(*vers) && !ok; i++) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, vers[i][0]);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, vers[i][1]);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, vers[i][2]);
        SDL_Window *w = SDL_CreateWindow("", 0, 0, 64, 64, SDL_WINDOW_HIDDEN | SDL_WINDOW_OPENGL);
        if (!w) continue;
        SDL_GLContext c = SDL_GL_CreateContext(w);
        if (c) { ok = 1; SDL_GL_DeleteContext(c); }
        SDL_DestroyWindow(w);
    }
    SDL_GL_ResetAttributes();
    return ok;
}

static void create_vk_window(const char *title, int posX, int posY, Uint32 flags) {
    flags = (flags & ~(Uint32)SDL_WINDOW_OPENGL) | SDL_WINDOW_VULKAN;
    wnd = SDL_CreateWindow(title, posX, posY, window_width, window_height, flags);
    if (!wnd) {
        ge_fatal("Could not open an SDL window for Vulkan:\n%s", SDL_GetError());
    }
    ge_log(GE_LOG_NOTE, "SDL: created Vulkan window");
}
#endif

static void gfx_sdl_init(const struct GfxWindowInitSettings *set) {
    window_width = set->width;
    window_height = set->height;

#ifdef SDL_HINT_VIDEO_HIGHDPI_DISABLED
    if (!set->allow_hidpi) {
        // HiDPI control, if available
        SDL_SetHint(SDL_HINT_VIDEO_HIGHDPI_DISABLED, "1");
    }
#endif
#if defined(_WIN32) && defined(SDL_HINT_WINDOWS_DPI_AWARENESS)   /* PD used PLATFORM_WIN32, never defined here */
    // Always declare per-monitor DPI awareness on Windows so the window and
    // fullscreen surfaces are 1:1 with physical pixels. Without it, a scaled
    // display (e.g. 3840x2160 at 125%) hands the game a 3072x1728 surface and
    // stretches it (soft picture) -- this used to be set only when HiDPI was
    // disabled. Width/Height in the ini are physical pixels either way.
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
#endif

    // D566: the port never takes text input, so never ask for an on-screen
    // keyboard (Steam Deck Game Mode popped one at every launch). Literal
    // hint name: older SDL2 headers lack the macro; unknown hints are ignored.
    SDL_SetHint("SDL_ENABLE_SCREEN_KEYBOARD", "0");

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        ge_fatal("Could not init SDL:\n%s", SDL_GetError());
    }
    if (s_mode == GE_WINDOW_NONE) {
        /* D973: no window, no GL context: the deko3d backend presents */
        if (s_ext.dims) {
            uint32_t w = 0, h = 0;
            s_ext.dims(&w, &h);
            window_width = (int)w;
            window_height = (int)h;
        }
        maximized_state = false;
        fullscreen_state = true;
        qpc_freq = SDL_GetPerformanceFrequency();
        ge_log(GE_LOG_NOTE, "SDL(switch): external present (deko3d), no SDL window");
        return;
    }

    // Auto window size: a width/height <= 0 means "pick a sensible default from
    // the desktop" rather than the 640x480 native res. Fit a 4:3 window into
    // ~85% of the primary desktop -- big enough to actually judge mouse/menu
    // behaviour, and 4:3 so the pointer maps onto the render with no letterbox.
#ifdef __SWITCH__
    // project-dam: one fullscreen surface at the console's output mode;
    // windows make no sense here. SDL reports 1920x1080 even handheld, where
    // the panel is 1280x720 and the GPU runs at about half the docked clock:
    // rendering 1080p there cost up to 35 ms of GPU per frame on hardware.
    {
        if (ge_is_handheld()) {
            window_width = 1280;
            window_height = 720;
        } else {
            window_width = 1920;
            window_height = 1080;
        }
    }
#endif
#ifndef __SWITCH__
    // Console mode off the Switch (portIsConsole: GE_CONSOLE=1 / System.Console,
    // e.g. an arm64 handheld on muOS, fbdev + libmali): the window IS the panel.
    // Without this the 85%/4:3 auto size clamps to 640x480 and the picture is
    // pillarboxed on the 720x480 panel. Always the full desktop size, always fullscreen.
    if (ge_is_console()) {
        SDL_DisplayMode dm;
        if (SDL_GetDesktopDisplayMode(primary_display_index(), &dm) == 0 && dm.w > 0 && dm.h > 0) {
            window_width = dm.w;
            window_height = dm.h;
        } else {
            window_width = 720;
            window_height = 480;
        }
        ge_log(GE_LOG_INFO, "SDL(console): window = panel %dx%d, fullscreen", window_width, window_height);
    }
#endif
    if (window_width <= 0 || window_height <= 0) {
        SDL_DisplayMode dm;
        if (SDL_GetDesktopDisplayMode(primary_display_index(), &dm) == 0 && dm.w > 0 && dm.h > 0) {
            int maxw = (int)(dm.w * 0.85);
            int maxh = (int)(dm.h * 0.85);
            if (maxw * 3 > maxh * 4) { window_height = maxh; window_width = maxh * 4 / 3; }
            else                     { window_width = maxw; window_height = maxw * 3 / 4; }
        }
        if (window_width < 640 || window_height < 480) { window_width = 640; window_height = 480; }
        ge_log(GE_LOG_INFO, "SDL: auto window size %dx%d", window_width, window_height);
    }

    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    if (ge_arg_check("--debug-gl")) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);
    }

    int posX = set->x;
    int posY = set->y;
    int display_in_use = SDL_GetWindowDisplayIndex(wnd);
    if (display_in_use < 0) { // Fallback to default if out of bounds
        posX = SDL_WINDOWPOS_UNDEFINED;
        posY = SDL_WINDOWPOS_UNDEFINED;
    }

    if (set->centered) {
        // centre on the primary display, in global desktop coordinates
        SDL_Rect b = { 0, 0, 0, 0 };
        if (SDL_GetDisplayBounds(primary_display_index(), &b) != 0 || b.w <= 0 || b.h <= 0) {
            SDL_DisplayMode mode = {};
            SDL_GetCurrentDisplayMode(0, &mode);
            b.x = 0; b.y = 0; b.w = mode.w; b.h = mode.h;
        }
        posX = b.x + b.w / 2 - window_width / 2;
        posY = b.y + b.h / 2 - window_height / 2;
    }

    if (set->fullscreen_is_exclusive) {
        fullscreen_flag = SDL_WINDOW_FULLSCREEN;
    }

    // we will unhide the window once the GL context is successfully created
    Uint32 flags = SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL;

    // if fullscreen was requested, start the window in fullscreen right away
    bool want_fullscreen = set->fullscreen;
#ifndef __SWITCH__
    if (ge_is_console()) want_fullscreen = true;   // the window is the panel
#endif
    if (want_fullscreen) {
        flags |= fullscreen_flag;
        fullscreen_state = true;
    }

    if (set->maximized) {
        flags |= SDL_WINDOW_MAXIMIZED;
        maximized_state = true;
    }

#ifdef __SWITCH__
    // project-dam: the Switch window always covers the screen; SDL sizes its
    // surface from the window size, so no fullscreen/maximized flag (the ini's
    // Video.Fullscreen would bring in the 1920x1080 desktop mode, even
    // handheld). RESIZABLE stays: SDL's switch backend then resizes the window
    // itself on dock/undock (1920x1080 / 1280x720).
    flags &= ~(Uint32)(SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_MAXIMIZED);
    maximized_state = false;
    fullscreen_state = true;
#endif

#ifdef SDL_WINDOW_ALLOW_HIGHDPI
    if (set->allow_hidpi) {
        flags |= SDL_WINDOW_ALLOW_HIGHDPI;
    }
#endif

#ifdef GE_VULKAN
    if (s_mode == GE_WINDOW_VULKAN) {   /* issue #44: the Vulkan backend owns the swapchain */
        create_vk_window(set->title, posX, posY, flags);
    } else
#endif
    create_gl_window(set->title, posX, posY, flags);

    // D550: window / taskbar icon = the project mark (generated by
    // tools_pc/make_icon.py into ge007_icon.h; R,G,B,A byte order per
    // pixel, portable -- packed here against the surface's masks).
    {
        const Uint32 rmask = 0x000000ff, gmask = 0x0000ff00,
                     bmask = 0x00ff0000, amask = 0xff000000;
        SDL_Surface *icon = SDL_CreateRGBSurface(
            0, GE007_ICON_SIZE, GE007_ICON_SIZE, 32, rmask, gmask, bmask, amask);
        if (icon) {
            for (int y = 0; y < GE007_ICON_SIZE; ++y) {
                for (int x = 0; x < GE007_ICON_SIZE; ++x) {
                    const uint8_t *p = ge007_icon_rgba[y][x];
                    // Shift each channel into its mask's byte lane before
                    // the & (an unshifted p[1] & 0x0000ff00 is always 0):
                    const Uint32 v = ((Uint32)p[0] & rmask) | ((Uint32)(p[1] << 8) & gmask) |
                                     ((Uint32)(p[2] << 16) & bmask) | ((Uint32)(p[3] << 24) & amask);
                    *(Uint32 *)((Uint8 *)icon->pixels + y * icon->pitch + x * 4) = v;
                }
            }
            SDL_SetWindowIcon(wnd, icon);
            SDL_FreeSurface(icon);
        } else {
            ge_log(GE_LOG_NOTE, "SDL: could not create the window icon surface: %s", SDL_GetError());
        }
    }

    SDL_ShowWindow(wnd);
    // D566: SDL2 starts with text input enabled; with it on, Steam's Game Mode
    // treats the window as wanting its virtual keyboard. Keys still arrive as
    // SDL_KEYDOWN (bindings, menus), which is all the port reads.
    SDL_StopTextInput();

    qpc_freq = SDL_GetPerformanceFrequency();
}

static void gfx_sdl_close(void) {
    is_running = false;
}

static void gfx_sdl_set_fullscreen_changed_callback(void (*on_fullscreen_changed)(bool is_now_fullscreen)) {
    on_fullscreen_changed_callback = on_fullscreen_changed;
}

static void gfx_sdl_set_fullscreen(bool enable) {
    set_fullscreen(enable, true);
}

static void gfx_sdl_set_fullscreen_exclusive(bool enable) {
    const uint32_t newflag = enable ? SDL_WINDOW_FULLSCREEN : SDL_WINDOW_FULLSCREEN_DESKTOP;
    if (fullscreen_flag != newflag) {
        fullscreen_flag = newflag;
        // reset fullscreen to take new value into account if it already is in fullscreen
        if (fullscreen_state) {
            fullscreen_state = false;
            set_fullscreen(enable, true);
        }
    }
}

static void gfx_sdl_set_maximize_window(bool enable) {
    set_maximize_window(enable);
}

static void gfx_sdl_set_cursor_visibility(bool visible) {
    if (visible) {
        SDL_ShowCursor(SDL_ENABLE);
    } else {
        SDL_ShowCursor(SDL_DISABLE);
    }
}

static void get_centered_positions_native(int32_t width, int32_t height, int32_t *posX, int32_t *posY) {
    const int disp_idx = SDL_GetWindowDisplayIndex(wnd);
    SDL_DisplayMode mode = {};
    SDL_GetDesktopDisplayMode(disp_idx, &mode);
    SDL_Rect b = { 0, 0, 0, 0 };   /* D581: the display's desktop offset */
    if (disp_idx < 0 || SDL_GetDisplayBounds(disp_idx, &b) != 0) { b.x = 0; b.y = 0; }
    *posX = b.x + mode.w / 2 - width / 2;
    *posY = b.y + mode.h / 2 - height / 2;
}

static void gfx_sdl_get_centered_positions(int32_t width, int32_t height, int32_t *posX, int32_t *posY) {
    const int disp_idx = SDL_GetWindowDisplayIndex(wnd);
    /* D581: centre on the window's display in global coordinates (its bounds
     * carry the desktop offset; the mode size alone lands on the leftmost). */
    SDL_Rect b = { 0, 0, 0, 0 };
    if (disp_idx < 0 || SDL_GetDisplayBounds(disp_idx, &b) != 0 || b.w <= 0 || b.h <= 0) {
        SDL_DisplayMode mode = {};
        SDL_GetCurrentDisplayMode(disp_idx < 0 ? 0 : disp_idx, &mode);
        b.x = 0; b.y = 0; b.w = mode.w; b.h = mode.h;
    }
    *posX = b.x + b.w / 2 - width / 2;
    *posY = b.y + b.h / 2 - height / 2;
}

static void gfx_sdl_set_closest_resolution(int32_t width, int32_t height, bool should_center) {
    const SDL_DisplayMode mode = {.w = width, .h = height};
    const int disp_idx = SDL_GetWindowDisplayIndex(wnd);
    SDL_DisplayMode closest = {};
    if (SDL_GetClosestDisplayMode(disp_idx, &mode, &closest)) {
        exclusive_mode_chosen = true;   /* D639: an explicit pick wins over the desktop mode */
        SDL_SetWindowDisplayMode(wnd, &closest);
        SDL_SetWindowSize(wnd, closest.w, closest.h);
        if (should_center) {
            int32_t posX = 0;
            int32_t posY = 0;
            get_centered_positions_native(closest.w, closest.h, &posX, &posY);
            SDL_SetWindowPosition(wnd, posX, posY);
        }
    }
}

static void gfx_sdl_set_dimensions(uint32_t width, uint32_t height, int32_t posX, int32_t posY) {
    SDL_SetWindowSize(wnd, width, height);
    SDL_SetWindowPosition(wnd, posX, posY);
}

static void gfx_sdl_get_dimensions(uint32_t* width, uint32_t* height, int32_t* posX, int32_t* posY) {
    if (s_has_ext && s_ext.dims) {   /* D973 */
        s_ext.dims(width, height);
        *posX = 0;
        *posY = 0;
        return;
    }
    SDL_GL_GetDrawableSize(wnd, static_cast<int*>((void*)width), static_cast<int*>((void*)height));
    SDL_GetWindowPosition(wnd, static_cast<int*>(posX), static_cast<int*>(posY));
}

static void gfx_sdl_handle_events(void) {
#if defined(__APPLE__)
    /* AppKit requires event polling on the process main thread. The host
     * loop in videoPumpEvents owns the complete SDL queue, so the scheduler
     * render thread must not call SDL_PollEvent on macOS. */
    return;
#else
    const GeRenderCallbacks *cb = ge_callbacks();
    if (cb->is_handheld && cb->on_dock_change) {
        static int s_last_handheld = -1;
        const int hh = cb->is_handheld() ? 1 : 0;
        if (s_last_handheld >= 0 && hh != s_last_handheld) cb->on_dock_change(hh);
        s_last_handheld = hh;
    }
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_KEYDOWN:
                if (event.key.keysym.sym == SDLK_RETURN && (event.key.keysym.mod & KMOD_ALT)) {
                    // alt-enter received, switch fullscreen state
                    set_fullscreen(!fullscreen_state, true);
                } else if (cb->on_key_down) {
                    // D145/D344: Alt+F4 quits, ESC releases the mouse capture (WI-1): the host's call (both
                    // pumps, this one and the host thread's, drain the same SDL queue, so either dequeues a keydown).
                    cb->on_key_down((int)event.key.keysym.sym, (int)event.key.keysym.mod, event.key.repeat);
                }
                break;
            case SDL_TEXTINPUT:
                if (cb->on_text) cb->on_text(event.text.text);
                break;
            case SDL_MOUSEBUTTONDOWN:
                if (cb->on_mouse_button_down) cb->on_mouse_button_down(event.button.button);   // WI-1: (re)lock in click-to-lock mode
                break;
            case SDL_MOUSEWHEEL:
                if (cb->on_mouse_wheel) cb->on_mouse_wheel(event.wheel.y);   // weapon cycle
                break;
            case SDL_CONTROLLERBUTTONDOWN:      // D961: GE_PRESSLATCH, a tap between two pad samples
                if (cb->on_pad_button_down) cb->on_pad_button_down((int)event.cbutton.which, event.cbutton.button, event.cbutton.timestamp);
                break;
            case SDL_CONTROLLERAXISMOTION:
                if (cb->on_pad_axis) cb->on_pad_axis((int)event.caxis.which, event.caxis.axis, event.caxis.value, event.caxis.timestamp);
                break;
            case SDL_CONTROLLERDEVICEADDED:     // D450: this pump and the host's drain the same
            case SDL_CONTROLLERDEVICEREMOVED:   // SDL queue; the host requests a rescan, never rescans here
                if (cb->on_pad_device_change) cb->on_pad_device_change();
                break;
            case SDL_AUDIODEVICEREMOVED:   // D959: the host thread no longer polls SDL here
                if (!event.adevice.iscapture && cb->on_audio_device_removed) cb->on_audio_device_removed(event.adevice.which);
                break;
            case SDL_WINDOWEVENT:
                if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                    drawable_size(&window_width, &window_height);
                    if (cb->on_resize) cb->on_resize(window_width, window_height);
                    if (!fullscreen_state) {
                        maximized_state = SDL_GetWindowFlags(wnd) & SDL_WINDOW_MAXIMIZED ? true : false;
                    }
                } else if (event.window.event == SDL_WINDOWEVENT_CLOSE &&
                           event.window.windowID == SDL_GetWindowID(wnd)) {
                    // We listen specifically for main window close because closing main window
                    // on macOS does not trigger SDL_Quit.
                    if (cb->on_quit) cb->on_quit("window closed (render pump)");   // D344
                } else if (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                    if (cb->on_focus) cb->on_focus(0);   // free + show cursor on alt-tab (also handled by the host's pump; whichever dequeues it)
                } else if (event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
                    if (cb->on_focus) cb->on_focus(1);
                }
                break;
            case SDL_QUIT:
                if (cb->on_quit) cb->on_quit("quit event (render pump)");   // D344
                break;
        }
    }
#endif
}

/* The window/context are created on the host main thread, but the game's
 * scheduler thread (shedThread) does all rendering. Make the context current
 * there before each frame. extern "C": called from C (port/src/video.c). */
extern "C" void ge_window_make_context_current(void) {
    if (wnd && ctx) {
        /* Note: SDL2's MakeCurrent return value is unreliable on this MinGW
         * toolchain (ABI artifact), so we don't branch on it. A genuine bind
         * failure will surface later as a GL call returning nothing / shader
         * compile failure, which is far louder. */
        SDL_GL_MakeCurrent(wnd, ctx);
    }
}

/* D344: called on the render thread when a quit was requested, at a frame
 * boundary: wait for the GPU to drain everything this process queued, then
 * unbind the context, so process teardown never lands inside the driver. */
extern "C" void ge_window_park_for_exit(void) {
    if (s_has_ext) {   /* D973: queue idle, swapchain destroyed (replay thread) */
        if (s_ext.park) s_ext.park();
        return;
    }
    if (ctx && SDL_GL_GetCurrentContext() == ctx) {
        typedef void (SDLCALL *FinishFn)(void);   // = APIENTRY on Windows
        FinishFn finish = (FinishFn)SDL_GL_GetProcAddress("glFinish");
        if (finish) {
            finish();
        }
        SDL_GL_MakeCurrent(NULL, NULL);
    }
}

/* D632: host thread, at exit, after the render thread has parked (context
 * unbound everywhere, GPU drained by gfx_sdl_park_for_exit). Delete the GL
 * context and the window while the process is still alive, so the driver
 * retires its hardware queues through the normal path. Left alone, they are
 * destroyed by the kernel inside process teardown (DXGPROCESS::Destroy ->
 * DXGCONTEXT::DestroyContext -> VidSchTerminateHwQueue), which is exactly
 * where the 0x119 bugcheck of 2026-10-10 fired (minidump, HAGS on). */
extern "C" void ge_window_teardown_for_exit(void) {
    if (ctx) {
        SDL_GL_MakeCurrent(NULL, NULL);   // host never holds it after init; harmless
        SDL_GL_DeleteContext(ctx);
        ctx = NULL;
    }
    if (wnd) {
        SDL_DestroyWindow(wnd);
        wnd = NULL;
    }
}

/* Release the GL context from the calling (host main) thread. WGL only allows
 * a context to be current on one thread at a time, and while another thread
 * still holds it, wglMakeContextCurrent fails with "The requested resource is
 * in use" (SDL2 reports this via SDL_GetError). The host thread must release
 * after init so the game's scheduler thread can bind it per frame. */
extern "C" void ge_window_release_context(void) {
    if (ctx) {
        SDL_GL_MakeCurrent(NULL, NULL);
    }
}

/* The host thread owns SDL event pumping (see videoPumpEvents); when it sees
 * a window resize it calls this to refresh the cached drawable size that
 * fast3d uses for scaling. */
/* D578: refresh rate of the window's display, polled on the host thread
 * (SDL video calls belong there) and read by the render thread. 0 = unknown. */
static SDL_atomic_t s_refresh_hz;

extern "C" void ge_window_poll_refresh_rate(void) {
    SDL_DisplayMode mode;
    const int idx = wnd ? SDL_GetWindowDisplayIndex(wnd) : -1;
    const int hz = (idx >= 0 && SDL_GetCurrentDisplayMode(idx, &mode) == 0) ? mode.refresh_rate : 0;
    SDL_AtomicSet(&s_refresh_hz, hz);
}

extern "C" int ge_window_get_refresh_rate(void) {
    return SDL_AtomicGet(&s_refresh_hz);
}

extern "C" void ge_window_update_cached_size(void) {
    if (wnd) {
        drawable_size(&window_width, &window_height);
    }
}

static bool gfx_sdl_start_frame(void) {
    return true;
}

static uint64_t qpc_to_100ns(uint64_t qpc) {
    return qpc / qpc_freq * 10000000 + qpc % qpc_freq * 10000000 / qpc_freq;
}

static inline void sync_framerate_with_timer(void) {
    uint64_t t;
    t = qpc_to_100ns(SDL_GetPerformanceCounter());

    const int64_t next = previous_time + 10 * FRAME_INTERVAL_US_NUMERATOR / FRAME_INTERVAL_US_DENOMINATOR;
    int64_t left = next - t;
    // We want to exit a bit early, so we can busy-wait the rest to never miss the deadline
    left -= 15000UL;
    if (left > 0) {
        /* D250: `left` is in 100ns units (qpc_to_100ns); ge_sleep_us() takes
         * microseconds. Passing it unconverted slept ~10x too long every
         * time this branch fired (e.g. an intended 2ms sleep became a real
         * ~20ms one), silently eating the rest of the frame budget whenever
         * per-frame work left little slack -- a resolution/GPU-independent
         * frame-pacing bug, not a real render cost. */
        ge_sleep_us((uint32_t)(left / 10));
    }

    do {
        ge_cpu_relax();
        t = qpc_to_100ns(SDL_GetPerformanceCounter());
    } while ((int64_t)t < next);

    t = qpc_to_100ns(SDL_GetPerformanceCounter());
    if (left > 0 && t - next < 10000) {
        // In case it takes some time for the application to wake up after sleep,
        // or inaccurate timer,
        // don't let that slow down the framerate.
        t = next;
    }
    previous_time = t;
}

/* Optional hook run with the freshly-composited frame still in the back
 * buffer, immediately before the swap. video.c uses it for GE_PCDUMP /
 * screenshots: reading the back buffer AFTER SDL_GL_SwapWindow is undefined
 * on buffer-exchange drivers (Mesa, WSLg) and yields a black capture. */
extern "C" void (*ge_window_pre_present)(void) = NULL;

static void gfx_sdl_swap_buffers_begin(void) {
    if (target_fps) {
        sync_framerate_with_timer();
    }
    if (ge_window_pre_present) {
        ge_window_pre_present();
    }
    if (s_has_ext) {   /* D973 */
        s_ext.present();
        return;
    }
    SDL_GL_SwapWindow(wnd);
}

static void gfx_sdl_swap_buffers_end(void) {

}

static double gfx_sdl_get_time(void) {
    return SDL_GetPerformanceCounter() / (double)qpc_freq;
}

static int32_t gfx_sdl_get_target_fps(void) {
    return target_fps;
}

static void gfx_sdl_set_target_fps(int fps) {
    /* D186: the pacing wait below runs inline on the game's scheduler thread
     * (libultra.c osSpTaskStartGo -> gfx_run -> swap_buffers_begin), so a low
     * cap stalls VI-retrace delivery and throttles the whole sim, not just
     * presentation. Until pacing is moved off that thread, refuse caps low
     * enough to be user-visible as a slowdown: treat 1..29 as uncapped. */
    if (fps > 0 && fps < 30) {
        ge_log(GE_LOG_WARNING, "gfx_sdl: ignoring FpsCap=%d (< 30 throttles the sim); running uncapped", fps);
        fps = 0;
    }
    target_fps = fps;
}

static bool gfx_sdl_can_disable_vsync(void) {
    return true;
}

static void *gfx_sdl_get_window_handle(void) {
    return (void *)wnd;
}

static void gfx_sdl_set_window_title(const char *title) {
    SDL_SetWindowTitle(wnd, title);
}

static int gfx_sdl_get_swap_interval(void) {
    if (s_has_ext) return s_ext_swap_interval;   /* D973 */
#if defined(__APPLE__)
    return s_cglSwapInterval;   /* D952 */
#else
    return SDL_GL_GetSwapInterval();
#endif
}

static bool gfx_sdl_set_swap_interval(int interval) {
    if (s_has_ext) {   /* D973: the deko3d swapchain's interval */
        s_ext_swap_interval = interval;
        if (s_ext.set_interval) s_ext.set_interval(interval);
        vsync_enabled = interval != 0;
        return true;
    }
#if defined(__APPLE__)
    const bool success = gfx_sdl_apply_swap_interval(interval);   /* D952 */
#else
    const bool success = SDL_GL_SetSwapInterval(interval) >= 0;
#endif
    vsync_enabled = success && (interval != 0);
    if (!success) {
        ge_log(GE_LOG_WARNING, "SDL: failed to set vsync %d: %s", interval, SDL_GetError());
    }
    return success;
}

int gfx_sdl_get_display_mode(int modenum, int *out_w, int *out_h) {
    const int display_in_use = SDL_GetWindowDisplayIndex(wnd);
    SDL_DisplayMode sdlmode;
    if (SDL_GetDisplayMode(display_in_use, modenum, &sdlmode) == 0) {
        *out_w = sdlmode.w;
        *out_h = sdlmode.h;
        return 1;
    }
    return 0;
}

int gfx_sdl_get_current_display_mode(int *out_w, int *out_h) {
    const int display_in_use = SDL_GetWindowDisplayIndex(wnd);
    SDL_DisplayMode sdlmode;
    if (SDL_GetCurrentDisplayMode(display_in_use, &sdlmode) == 0) {
        *out_w = sdlmode.w;
        *out_h = sdlmode.h;
        return 1;
    }
    return 0;
}

int gfx_sdl_get_num_display_modes(void) {
    const int display_in_use = SDL_GetWindowDisplayIndex(wnd);
    return SDL_GetNumDisplayModes(display_in_use);
}

struct GfxWindowManagerAPI ge_window_sdl = {
    gfx_sdl_init,
    gfx_sdl_close,
    gfx_sdl_get_display_mode,
    gfx_sdl_get_current_display_mode,
    gfx_sdl_get_num_display_modes,
    gfx_sdl_get_fullscreen_state,
    gfx_sdl_set_fullscreen_changed_callback,
    gfx_sdl_set_fullscreen,
    gfx_sdl_set_fullscreen_exclusive,
    gfx_sdl_set_fullscreen_flag,
    gfx_sdl_get_fullscreen_flag_mode,
    gfx_sdl_get_maximized_state,
    gfx_sdl_set_maximize_window,
    gfx_sdl_get_active_window_refresh_rate,
    gfx_sdl_set_cursor_visibility,
    gfx_sdl_set_closest_resolution,
    gfx_sdl_set_dimensions,
    gfx_sdl_get_dimensions,
    gfx_sdl_get_centered_positions,
    gfx_sdl_handle_events,
    gfx_sdl_start_frame,
    gfx_sdl_swap_buffers_begin,
    gfx_sdl_swap_buffers_end,
    gfx_sdl_get_time,
    gfx_sdl_get_target_fps,
    gfx_sdl_set_target_fps,
    gfx_sdl_can_disable_vsync,
    gfx_sdl_get_window_handle,
    gfx_sdl_set_window_title,
    gfx_sdl_get_swap_interval,
    gfx_sdl_set_swap_interval,
};
