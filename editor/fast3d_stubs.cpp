// What port/fast3d/gfx_opengl.cpp expects from the rest of the game (gfx_pc.cpp, port/src/system.c,
// video.c). Compiled with the game's include dirs (it is part of ge_editor_f3d), so the prototypes
// are the game's own. Defaults are the GL backend's own init values; the editor sets nothing else.
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "system.h"

int gfx_rdp_affine = 0;               // set by gfx_opengl_init
int gfx_fog_vertex = 0;               // set by gfx_opengl_init (from portPerPixelFog)
bool gfx_framebuffers_enabled = true; // framebuffer 0 is the window; the editor draws nothing off-screen
extern "C" {
int portPerPixelFog = 0;   // Video.PerPixelFog: off = the N64's per-vertex fog
int portDisplayFixes = 0;  // Video.Visuals: not used by the replay
}

extern "C" {
// Command line of the editor, for the "--debug-gl" check of gfx_opengl_init.
static int s_argc; static char **s_argv;
void editorStubsSetArgs(int argc, char **argv) { s_argc = argc; s_argv = argv; }

int sysArgCheck(const char *arg)
{
    for (int i = 1; i < s_argc; i++)
        if (strcmp(s_argv[i], arg) == 0) return i;
    return 0;
}

void sysLogPrintf(enum LogLevel level, const char *fmt, ...)
{
    (void)level;
    va_list ap; va_start(ap, fmt);
    fputs("[gl] ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

void sysFatalError(const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    fputs("fatal: ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    abort();
}
}
