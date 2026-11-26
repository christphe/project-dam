#ifndef GE_INTERNAL_H
#define GE_INTERNAL_H

/* ge_render's private side: what the library's own files share. The host reaches the library through
 * ge_render.h only; nothing here is game code. The host callbacks (ge_render.h, GeRenderCallbacks) arrive
 * here as the ge_* helpers, each with a default that works with no host. */

#include <stdint.h>
#include <stdbool.h>

#include "ge_render.h"

#ifdef __cplusplus
extern "C" {
#endif

void ge_log(int level, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void ge_fatal(const char *fmt, ...) __attribute__((format(printf, 1, 2))) __attribute__((noreturn));
int ge_arg_check(const char *arg);
const char *ge_arg_get_string(const char *arg);
const char *ge_resolve_path(const char *path);
void ge_sleep_us(uint32_t micros);
void ge_cpu_relax(void);
int ge_is_handheld(void);
int ge_is_console(void);

/* ge_render_open registers the host callbacks (NULL: none). */
void ge_core_set_callbacks(const GeRenderCallbacks *cb);
/* The registered callbacks (never NULL; members may be). */
const GeRenderCallbacks *ge_callbacks(void);

#ifdef __cplusplus
}
#endif

#endif
