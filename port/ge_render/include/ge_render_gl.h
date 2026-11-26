#ifndef GE_RENDER_GL_H
#define GE_RENDER_GL_H

/* GL backend extras for a host that draws raw GL on the backend's context (include glad/glad.h from this directory for
 * the GL functions). The game's first-launch GPU benchmark (port/fast3d/gfx_glprobe.cpp) is one. */

#ifdef __cplusplus
extern "C" {
#endif

/* The profile (1 = GLES), the GLSL version string the backend writes ("130", "300 es", ...) and the alpha-to-coverage
 * switch it tracks (restore it with ge_render_gl_set_a2c). */
void ge_render_gl_info(int *es, const char **glsl_version_str, int *a2c_on);
void ge_render_gl_set_a2c(int on);

/* The GL backend's GE+ sky machinery as the benchmark draws it: the sky program with its uniform locations (the SPL_* order
 * below), the composite program, the vertex array of the fullscreen triangle and the two builders. Read it again after a builder
 * ran (the programs are built on first use). */
enum { GE_GL_SPL_VP, GE_GL_SPL_ASP, GE_GL_SPL_C0, GE_GL_SPL_C1, GE_GL_SPL_C2, GE_GL_SPL_C3, GE_GL_SPL_EYE, GE_GL_SPL_CLOUDY,
       GE_GL_SPL_SKY, GE_GL_SPL_CLOUD, GE_GL_SPL_OFS, GE_GL_SPL_GAIN, GE_GL_SPL_TEXSCALE, GE_GL_SPL_SUNDIR, GE_GL_SPL_SUNCOL,
       GE_GL_SPL_SUNGLOW, GE_GL_SPL_MOONDIR, GE_GL_SPL_NIGHT, GE_GL_SPL_TEX, GE_GL_SPL_COUNT };
typedef struct GeGlSkyBench {
    int sp2_ok;                    /* the GL 4.6 sky path is in use (the benchmark skips it) */
    unsigned sky_prog, sky_vao;    /* 0 until built */
    int loc[GE_GL_SPL_COUNT];
    unsigned skyr_prog, skyr_loc_tex, skyr_loc_vp;
    int (*build_sky)(void);        /* 1 = the sky program exists */
    int (*build_composite)(void);  /* 1 = the composite program exists */
} GeGlSkyBench;
void ge_render_gl_sky_bench(GeGlSkyBench *out);

#ifdef __cplusplus
}
#endif

#endif
