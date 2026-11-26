/* ge_core: the part of ge_render with no backend and no window in it: the live config, the host callbacks with their
 * defaults, the combiner id helper. The GL backend alone (the mission editor compiles gfx_opengl.cpp + this) links it. */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#include "ge_internal.h"
#include "gfx_cc.h"

/* ---- config ---------------------------------------------------------------------------------------------- */

extern "C" {
GeRenderConfig ge_render_cfg = {
    /* backend */ GE_RENDER_GL, /* window */ {}, /* fxaa_on */ false, /* per_pixel_fog */ 0, /* display_fixes */ 0,
    /* skyplus_half */ 0, /* rdp_affine */ 0, /* fog_vertex */ 0, /* framebuffers_enabled */ true,
    /* skyplus_failed */ 0, /* skyplus_sea */ 0, /* water_surfaces */ 0,
};
}

/* ---- callbacks and their defaults ------------------------------------------------------------------------ */

static GeRenderCallbacks s_cb;   /* all NULL until ge_render_open */

extern "C" void ge_core_set_callbacks(const GeRenderCallbacks *cb) {
    if (cb) s_cb = *cb; else memset(&s_cb, 0, sizeof s_cb);
}

extern "C" const GeRenderCallbacks *ge_callbacks(void) {
    return &s_cb;
}

extern "C" void ge_log(int level, const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (s_cb.log) {
        s_cb.log(level, buf);
    } else {
        static const char *tags[] = { "ERROR", "WARN ", "NOTE ", "INFO ", "DEBUG" };
        fprintf(stderr, "[%s] %s\n", tags[level < 0 ? 0 : level > 4 ? 4 : level], buf);
    }
}

extern "C" void ge_fatal(const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (s_cb.fatal) {
        s_cb.fatal(buf);
    } else {
        fprintf(stderr, "[FATAL] %s\n", buf);
    }
    abort();
}

extern "C" int ge_arg_check(const char *arg) {
    return s_cb.arg_check ? s_cb.arg_check(arg) : 0;
}

extern "C" const char *ge_arg_get_string(const char *arg) {
    return s_cb.arg_get_string ? s_cb.arg_get_string(arg) : NULL;
}

extern "C" const char *ge_resolve_path(const char *path) {
    return s_cb.resolve_path ? s_cb.resolve_path(path) : path;
}

extern "C" void ge_sleep_us(uint32_t micros) {
    if (s_cb.sleep_us) {
        s_cb.sleep_us(micros);
    } else {
        SDL_Delay((micros + 999) / 1000);
    }
}

extern "C" void ge_cpu_relax(void) {
    if (s_cb.cpu_relax) s_cb.cpu_relax();
}

extern "C" int ge_is_handheld(void) {
    return s_cb.is_handheld ? s_cb.is_handheld() : 0;
}

extern "C" int ge_is_console(void) {
    return s_cb.is_console ? s_cb.is_console() : 0;
}


extern "C" {

uint64_t ge_render_cc_id(const uint8_t c[2][2][4]) {
    uint64_t id = 0;
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2; j++)
            for (int k = 0; k < 4; k++)
                id |= (uint64_t)(c[i][j][k] & 0xf) << (i * 32 + j * 16 + k * 4);
    return id;
}

uint64_t ge_render_cc_id_shade(void) {
    uint8_t c[2][2][4] = {};
    c[0][0][3] = SHADER_INPUT_1;   /* colour = shade */
    c[0][1][3] = SHADER_INPUT_1;   /* alpha = shade alpha */
    return ge_render_cc_id(c);
}

uint64_t ge_render_cc_id_texel0_shade(void) {
    uint8_t c[2][2][4] = {};
    c[0][0][0] = SHADER_TEXEL0;  c[0][0][2] = SHADER_INPUT_1;   /* colour = texel0 * shade */
    c[0][1][0] = SHADER_TEXEL0A; c[0][1][2] = SHADER_INPUT_1;
    return ge_render_cc_id(c);
}

}   /* extern "C" */
