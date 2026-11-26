/* The first-launch GPU mini-benchmark, GL half (the decision half is port/src/gpuprobe.c). Game side: it draws raw GL on
 * the GL backend's context (ge_render), so it is not part of the renderer library. video.c calls it right after
 * ge_render_open. */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <algorithm>
#include <chrono>
#include <vector>

#include <SDL.h>

#include "glad/glad.h"

#include "system.h"
#include "bootfont.h"   /* the benchmark shows its phases with the boot screen font */
#include "../src/gpuprobe.h"   /* the decision half; port/src is not on the include path */
#include "ge_render_gl.h"

static inline uint64_t gl_now_ns(void) {
    return (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

/* The GL backend's sky machinery (ge_render_gl.h), under the names the benchmark was written with. */
static GeGlSkyBench gp_sky;
#define s_sp2_ok (gp_sky.sp2_ok != 0)
#define s_skyplus_prog gp_sky.sky_prog
#define s_skyplus_vao gp_sky.sky_vao
#define s_sp_loc gp_sky.loc
#define s_skyr_prog gp_sky.skyr_prog
#define s_skyr_loc_tex gp_sky.skyr_loc_tex
#define s_skyr_loc_vp gp_sky.skyr_loc_vp
enum { SPL_VP = GE_GL_SPL_VP, SPL_ASP = GE_GL_SPL_ASP, SPL_C0 = GE_GL_SPL_C0, SPL_C1 = GE_GL_SPL_C1, SPL_C2 = GE_GL_SPL_C2,
       SPL_C3 = GE_GL_SPL_C3, SPL_EYE = GE_GL_SPL_EYE, SPL_CLOUDY = GE_GL_SPL_CLOUDY, SPL_SKY = GE_GL_SPL_SKY,
       SPL_CLOUD = GE_GL_SPL_CLOUD, SPL_OFS = GE_GL_SPL_OFS, SPL_GAIN = GE_GL_SPL_GAIN, SPL_TEXSCALE = GE_GL_SPL_TEXSCALE,
       SPL_SUNDIR = GE_GL_SPL_SUNDIR, SPL_SUNCOL = GE_GL_SPL_SUNCOL, SPL_SUNGLOW = GE_GL_SPL_SUNGLOW,
       SPL_MOONDIR = GE_GL_SPL_MOONDIR, SPL_NIGHT = GE_GL_SPL_NIGHT, SPL_TEX = GE_GL_SPL_TEX };
static bool skyplus_build(void) { const bool ok = gp_sky.build_sky() != 0; ge_render_gl_sky_bench(&gp_sky); return ok; }
static bool skyr_build_comp(void) { const bool ok = gp_sky.build_composite() != 0; ge_render_gl_sky_bench(&gp_sky); return ok; }

static bool gl_es = false;
static const char* gl_glsl_version_str = "130";
static bool s_a2c_on = false;
static void gfx_opengl_set_a2c(bool on) { ge_render_gl_set_a2c(on ? 1 : 0); }
static void gp_sync_backend_state() {
    int es = 0, a2c = 0;
    ge_render_gl_info(&es, &gl_glsl_version_str, &a2c);
    gl_es = es != 0;
    s_a2c_on = a2c != 0;
    ge_render_gl_sky_bench(&gp_sky);
}

/* First-launch GPU mini-benchmark (port/src/gpuprobe.h decides from these numbers; video.c applies them once per ini).
 * Everything is drawn off screen into targets of the window's size, so nothing shows; the context is the main
 * thread's (called right after gfx_init, before the replay thread takes it over) and the GL state is put back.
 *  - sky: the real GE+ sky program (ES / non-sp2 path) at full, half and quarter size, the reduced sizes through
 *    the same sky target + composite the game uses. Wall clock around glFinish: the GPU timer query is unreliable
 *    while a nested FBO is active (measured on the Mali-G31).
 *  - A2C: a stack of alpha cut-out layers through an MSAA target, alpha-to-coverage against the plain blend.
 *  - MSAA: a synthetic opaque fill (two layers of jittered, depth-intersecting triangles, ~4k tris, ALU shading) into a 1x / 2x / 4x
 *    target at the window size, with the resolve blit the game's MSAA path does; the three are run back to back and the median of
 *    the per-round differences over 1x is the level's cost. A2C is then timed at the MSAA level that will be chosen.
 * A ~120 ms warm-up of full-size sky draws first lets the DVFS governor ramp the clocks, as the game's first frames do.
 * Visible (run->visible): after each phase, never inside a timed region, the off-screen result is blitted to the window with a
 * text panel (phase + ms) and presented; the time spent there is not counted against the deadline. A summary is held at the end.
 * Total budget < 1 s of work (a deadline skips what is left on a very slow GPU). Returns 1 if anything was measured. */
#define GPUPROBE_WARMUP_MS    120
#define GPUPROBE_DEADLINE_MS  900
#define GPUPROBE_A2C_LAYERS   4

static GLuint gp_compile(const char* vs_body, const char* fs_body, const char* defines) {
    /* The probe's shaders are written for GLSL 300 es (in/out, gl_InstanceID). Desktop: 330 core, as the FXAA pass --
     * the game's own desktop version is often 130 (a "3.0 compat" context), where gl_InstanceID does not exist and
     * the MSAA fill program failed to compile, silently leaving MSAA unmeasured on PC. */
    char head[128];
    if (gl_es) {
        snprintf(head, sizeof head, "#version %s\nprecision highp float;\n%s", gl_glsl_version_str, defines);
    } else {
        snprintf(head, sizeof head, "#version 330 core\n%s", defines);
    }
    const char* vsrc[2] = { head, vs_body };
    const char* fsrc[2] = { head, fs_body };
    GLuint v = glCreateShader(GL_VERTEX_SHADER), f = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(v, 2, vsrc, NULL);
    glCompileShader(v);
    glShaderSource(f, 2, fsrc, NULL);
    glCompileShader(f);
    GLuint pr = glCreateProgram();
    glAttachShader(pr, v);
    glAttachShader(pr, f);
    glLinkProgram(pr);
    GLint ok = 0;
    glGetProgramiv(pr, GL_LINK_STATUS, &ok);
    glDeleteShader(v);
    glDeleteShader(f);
    if (!ok) {
        glDeleteProgram(pr);
        return 0;
    }
    return pr;
}

extern "C" int gfx_opengl_gpuprobe(GpuProbeRun* run) {
    gp_sync_backend_state();
    const int w = run->w, h = run->h, msaa = run->msaa;
    float* skyMs = run->skyMs;
    float* msaaMs = run->msaaMs;
    for (int i = 0; i < 3; i++) skyMs[i] = msaaMs[i] = GPUPROBE_UNMEASURED;
    run->a2cDeltaMs = GPUPROBE_UNMEASURED;
    run->choice.skyScale = run->choice.a2c = run->choice.msaa = -1;
    if (w < 64 || h < 64) return 0;
    while (glGetError() != GL_NO_ERROR) {}
    const bool vis = run->visible && run->present;

    /* the state this touches */
    GLint prev_fbo = 0, prev_rfbo = 0, prev_prog = 0, prev_vao = 0, prev_unit = 0, prev_tex0 = 0, prev_tex7 = 0, prev_vp[4], prev_sc[4], prev_rb = 0;
    GLint prev_dfunc = GL_LESS, prev_bf[4] = { GL_ONE, GL_ZERO, GL_ONE, GL_ZERO };
    const GLboolean prev_depth = glIsEnabled(GL_DEPTH_TEST), prev_blend = glIsEnabled(GL_BLEND),
                    prev_cull = glIsEnabled(GL_CULL_FACE), prev_scissor = glIsEnabled(GL_SCISSOR_TEST);
    GLboolean prev_cm[4], prev_dm = GL_TRUE;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prev_fbo);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prev_rfbo);
    glGetIntegerv(GL_RENDERBUFFER_BINDING, &prev_rb);
    glGetIntegerv(GL_CURRENT_PROGRAM, &prev_prog);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prev_vao);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &prev_unit);
    glGetIntegerv(GL_VIEWPORT, prev_vp);
    glGetIntegerv(GL_SCISSOR_BOX, prev_sc);
    glGetIntegerv(GL_DEPTH_FUNC, &prev_dfunc);
    glGetIntegerv(GL_BLEND_SRC_RGB, &prev_bf[0]);
    glGetIntegerv(GL_BLEND_DST_RGB, &prev_bf[1]);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &prev_bf[2]);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &prev_bf[3]);
    glGetBooleanv(GL_COLOR_WRITEMASK, prev_cm);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &prev_dm);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev_tex0);
    glActiveTexture(GL_TEXTURE0 + 7);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev_tex7);
    const bool prev_a2c = s_a2c_on;
    gfx_opengl_set_a2c(false);

    const uint64_t t0 = gl_now_ns();
    double shownMs = 0.0;   /* wall time spent presenting / holding: not work, not against the deadline */
    double compileMs = 0.0; /* shader compiles: driver time that varies run to run and is no measure of the GPU; not against the deadline either */
    auto elapsed_ms = [&]() { return (double)(gl_now_ns() - t0) / 1.0e6 - shownMs - compileMs; };
    int measured = 0;
    double tCompiled = 0.0, tSky = 0.0, tMsaa = 0.0;

    GLuint dstFbo = 0, dstTex = 0, redFbo = 0, redTex = 0, noiseTex = 0, a2cProg = 0, a2cProgB = 0, msFbo = 0, msColor = 0, msDepth = 0,
           fillProg = 0, fillFbo[3] = { 0, 0, 0 }, fillColor[3] = { 0, 0, 0 }, fillDepth[3] = { 0, 0, 0 }, txTex = 0, txFbo = 0;
    glGenTextures(1, &dstTex);
    glBindTexture(GL_TEXTURE_2D, dstTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glGenFramebuffers(1, &dstFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, dstFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, dstTex, 0);
    const bool dstOk = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glEnable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    /* ---- the visible part: a panel of text over the off-screen picture, presented ---- */
    enum { PW = 336, PH = 160, MAXL = 8 };
    char lines[MAXL][40];
    int nlines = 0;
    const char* title = "Measuring the GPU...";
    auto add_line = [&](const char* fmt, double v) {
        if (nlines < MAXL) { snprintf(lines[nlines], sizeof lines[0], fmt, v); nlines++; }
    };
    auto show = [&](int holdMs) {
        if (!vis || !dstOk) return;
        const uint64_t a = gl_now_ns();
        const int ph = std::min((int)PH, 50 + nlines * 16 + 8);   /* the panel is as tall as its lines */
        std::vector<uint32_t> px((size_t)PW * ph, 0xFF24160Au);   /* ABGR: dark blue */
        auto rect = [&](int x, int y, int rw, int rh, uint32_t c) {
            for (int yy = y; yy < y + rh; yy++) for (int xx = x; xx < x + rw; xx++)
                if (xx >= 0 && xx < PW && yy >= 0 && yy < ph) px[(size_t)(ph - 1 - yy) * PW + xx] = c;   /* texture row 0 is the bottom */
        };
        auto text = [&](int x, int y, const char* t, uint32_t c) {
            for (; *t; t++, x += BOOTFONT_W) {
                const unsigned char ch = (unsigned char)*t;
                if (ch < 32 || ch > 126) continue;
                for (int row = 0; row < BOOTFONT_H; row++) for (int col = 0; col < BOOTFONT_W; col++)
                    if (kBootFont[ch - 32][row] & (0x80 >> col)) rect(x + col, y + row, 1, 1, c);
            }
        };
        const uint32_t gold = 0xFF50CDFFu, white = 0xFFF5EBE6u;   /* ABGR */
        rect(0, 0, PW, 2, gold); rect(0, ph - 2, PW, 2, gold); rect(0, 0, 2, ph, gold); rect(PW - 2, 0, 2, ph, gold);
        text((PW - (int)strlen(title) * BOOTFONT_W) / 2, 10, title, gold);
        rect(16, 32, PW - 32, 1, gold);
        for (int i = 0; i < nlines; i++) text(16, 42 + i * 16, lines[i], white);
        GLint prevUnit = 0;
        glGetIntegerv(GL_ACTIVE_TEXTURE, &prevUnit);
        glActiveTexture(GL_TEXTURE0);
        if (!txTex) {
            glGenTextures(1, &txTex);
            glBindTexture(GL_TEXTURE_2D, txTex);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, PW, PH, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glGenFramebuffers(1, &txFbo);
            glBindFramebuffer(GL_FRAMEBUFFER, txFbo);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, txTex, 0);
        } else {
            glBindTexture(GL_TEXTURE_2D, txTex);
        }
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, PW, ph, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
        glDisable(GL_SCISSOR_TEST);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, dstFbo);
        glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        const int sc = w >= 2 * PW ? 2 : 1, dx = (w - PW * sc) / 2, dy = (h - ph * sc) / 2;
        glBindFramebuffer(GL_READ_FRAMEBUFFER, txFbo);
        glBlitFramebuffer(0, 0, PW, ph, dx, dy, dx + PW * sc, dy + ph * sc, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glEnable(GL_SCISSOR_TEST);
        glActiveTexture((GLenum)prevUnit);
        run->present();
        SDL_PumpEvents();
        if (holdMs > 0) SDL_Delay((Uint32)holdMs);
        glBindFramebuffer(GL_FRAMEBUFFER, dstFbo);
        shownMs += (double)(gl_now_ns() - a) / 1.0e6;
    };
    if (dstOk) {   /* something on screen at once: the shader compile below takes a moment */
        glClearColor(0.02f, 0.03f, 0.08f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        show(0);
    }

    /* ---- the sky (not the sp2 path: its shaders differ, and a GL 4.6 desktop GPU is not the target) ---- */
    const uint64_t tc0 = gl_now_ns();
    const bool skyOk = dstOk && !s_sp2_ok && (s_skyplus_prog || skyplus_build()) && (s_skyr_prog || skyr_build_comp());
    compileMs += (double)(gl_now_ns() - tc0) / 1.0e6;
    tCompiled = elapsed_ms();
    if (skyOk) {
        static unsigned char noise[64 * 64];
        unsigned s = 12345u;
        for (int i = 0; i < 64 * 64; i++) { s = s * 1664525u + 1013904223u; noise[i] = (unsigned char)(s >> 24); }
        glGenTextures(1, &noiseTex);
        glBindTexture(GL_TEXTURE_2D, noiseTex);
        GLint prev_align = 4;
        glGetIntegerv(GL_UNPACK_ALIGNMENT, &prev_align);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, 64, 64, 0, GL_RED, GL_UNSIGNED_BYTE, noise);
        glGenerateMipmap(GL_TEXTURE_2D);
        glPixelStorei(GL_UNPACK_ALIGNMENT, prev_align);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glGenTextures(1, &redTex);
        glGenFramebuffers(1, &redFbo);

        /* a daytime sky looking slightly up: clouds, sun and haze all on the path */
        glUseProgram(s_skyplus_prog);
        const float sunDir[3] = { 0.3f, 0.7f, 0.65f }, one3[3] = { 1.0f, 1.0f, 1.0f }, skyCol[3] = { 0.55f, 0.72f, 0.95f },
                    eye[3] = { 0.0f, 0.0f, 0.0f }, moon3[3] = { 0.0f, -1.0f, 0.0f };
        const float c0[3] = { -1.0f, 0.7f, 1.0f }, c1[3] = { 1.0f, 0.7f, 1.0f }, c2[3] = { -1.0f, -0.25f, 1.0f }, c3[3] = { 1.0f, -0.25f, 1.0f };
        const float ofs[6] = { 0.0f, 0.0f, 3.0f, 5.0f, 7.0f, 2.0f };
        glUniform2f(s_sp_loc[SPL_ASP], 1.0f, 0.0f);
        glUniform3fv(s_sp_loc[SPL_C0], 1, c0);
        glUniform3fv(s_sp_loc[SPL_C1], 1, c1);
        glUniform3fv(s_sp_loc[SPL_C2], 1, c2);
        glUniform3fv(s_sp_loc[SPL_C3], 1, c3);
        glUniform3fv(s_sp_loc[SPL_EYE], 1, eye);
        glUniform1f(s_sp_loc[SPL_CLOUDY], 1500.0f);
        glUniform3fv(s_sp_loc[SPL_SKY], 1, skyCol);
        glUniform3fv(s_sp_loc[SPL_CLOUD], 1, one3);
        glUniform2fv(s_sp_loc[SPL_OFS], 3, ofs);
        glUniform1f(s_sp_loc[SPL_GAIN], 1.0f);
        glUniform1f(s_sp_loc[SPL_TEXSCALE], 1.0f);
        glUniform3fv(s_sp_loc[SPL_SUNDIR], 1, sunDir);
        glUniform3fv(s_sp_loc[SPL_SUNCOL], 1, one3);
        glUniform1f(s_sp_loc[SPL_SUNGLOW], 1.0f);
        glUniform3fv(s_sp_loc[SPL_MOONDIR], 1, moon3);
        glUniform1f(s_sp_loc[SPL_NIGHT], 0.0f);
        glUniform1i(s_sp_loc[SPL_TEX], 7);
        glBindVertexArray(s_skyplus_vao);
        glActiveTexture(GL_TEXTURE0 + 7);

        int redW = 0, redH = 0;
        /* one sky draw at Video.SkyScale `sc` into the window-sized target, the way gfx_opengl_skyplus_pass does it */
        auto sky_draw = [&](int sc) {
            const int div = sc >= 2 ? 4 : (sc == 1 ? 2 : 1);
            int sw = w, sh = h;
            if (div > 1) {
                sw = w / div; sh = h / div;
                if (redW != sw || redH != sh) {
                    glBindTexture(GL_TEXTURE_2D, redTex);
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, sw, sh, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                    glBindFramebuffer(GL_FRAMEBUFFER, redFbo);
                    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, redTex, 0);
                    redW = sw; redH = sh;
                }
                glBindFramebuffer(GL_FRAMEBUFFER, redFbo);
            } else {
                glBindFramebuffer(GL_FRAMEBUFFER, dstFbo);
            }
            glBindTexture(GL_TEXTURE_2D, noiseTex);
            glViewport(0, 0, sw, sh);
            glScissor(0, 0, sw, sh);
            glUseProgram(s_skyplus_prog);
            glUniform4f(s_sp_loc[SPL_VP], 0.0f, 0.0f, (float)sw, (float)sh);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            if (div > 1) {
                glBindFramebuffer(GL_FRAMEBUFFER, dstFbo);
                glViewport(0, 0, w, h);
                glScissor(0, 0, w, h);
                glBindTexture(GL_TEXTURE_2D, redTex);
                glUseProgram(s_skyr_prog);
                glUniform1i((GLint)s_skyr_loc_tex, 7);
                glUniform4f((GLint)s_skyr_loc_vp, 0.0f, 0.0f, (float)w, (float)h);
                glDrawArrays(GL_TRIANGLES, 0, 3);
            }
        };

        const double warmEnd = elapsed_ms() + GPUPROBE_WARMUP_MS;   /* after the shader compile, which is not load */
        while (elapsed_ms() < warmEnd) {   /* DVFS ramp: real full-size sky work */
            sky_draw(0);
            glFinish();
        }
        for (int sc = 0; sc < 3; sc++) {
            if (elapsed_ms() > GPUPROBE_DEADLINE_MS) break;
            double best = 1.0e9, spent = 0.0;
            for (int rep = 0; rep < (sc == 0 ? 2 : 4); rep++) {   /* min over a few draws: a stray stall is not the sky's cost (full size: the warm-up was its first draws) */
                const uint64_t a = gl_now_ns();
                sky_draw(sc);
                glFinish();
                const double d = (double)(gl_now_ns() - a) / 1.0e6;
                spent += d;
                if (rep == 0 && sc > 0) {   /* the first draw after a size change builds its target: untimed */
                    if (d > 120.0) { best = d; break; }
                    continue;
                }
                if (d < best) best = d;
                if (rep >= 1 && spent > 150.0) break;
            }
            skyMs[sc] = (float)best;
            measured = 1;
            add_line(sc == 0 ? "Sky full    %5.1f ms" : sc == 1 ? "Sky half    %5.1f ms" : "Sky quarter %5.1f ms", best);
            show(0);
        }
        glBindFramebuffer(GL_FRAMEBUFFER, dstFbo);
    }
    tSky = elapsed_ms();

    /* ---- MSAA: what 2x / 4x cost over 1x on a synthetic fill, resolve included ---- */
    GLint maxSamples = 0;
    glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
    if (dstOk && msaa >= 2 && maxSamples >= 2 && elapsed_ms() < GPUPROBE_DEADLINE_MS) {
        static const char* vs = R"GLSL(
uniform float uZ;
uniform vec2 uGrid;
out vec3 vCol;
float hsh(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }
void main() {
    int cols = int(uGrid.x);
    int cx = gl_InstanceID % cols, cy = gl_InstanceID / cols;
    int v = gl_VertexID;
    vec2 c = (v == 1) ? vec2(1.0, 0.0) : ((v == 2 || v == 4) ? vec2(1.0, 1.0) : ((v == 5) ? vec2(0.0, 1.0) : vec2(0.0, 0.0)));
    vec2 lp = vec2(float(cx), float(cy)) + c;
    vec2 j = vec2(hsh(lp), hsh(lp + 7.3)) - 0.5;
    vec2 p = (lp + j * 0.8) / uGrid * 2.0 - 1.0;
    float z = uZ + (hsh(lp + 3.1) - 0.5) * 0.4;   /* tilted, so layers cut through each other: many edges */
    vCol = vec3(hsh(vec2(float(cx), float(cy))), hsh(vec2(float(cy), float(cx)) + 1.7), 0.5 + 0.5 * z);
    gl_Position = vec4(p, z, 1.0);
}
)GLSL";
        static const char* fs = R"GLSL(
in vec3 vCol;
out vec4 o;
void main() {
    vec2 p = gl_FragCoord.xy;
    float t = 0.5 + 0.5 * sin(p.x * 0.09) * sin(p.y * 0.07);
    vec3 c = vCol * (0.55 + 0.45 * t);
    c = mix(c, c * c, 0.5 * t) + 0.08 * vec3(sin(p.x * 0.31 + c.r * 6.0), sin(p.y * 0.27 + c.g * 6.0), 0.0);
    o = vec4(c, 1.0);
}
)GLSL";
        const uint64_t tf0 = gl_now_ns();
        fillProg = gp_compile(vs, fs, "");
        compileMs += (double)(gl_now_ns() - tf0) / 1.0e6;
        if (!s_skyplus_vao) glGenVertexArrays(1, &s_skyplus_vao);
        const int lvl[3] = { 1, 2, 4 };
        bool lvlOk[3] = { false, false, false };
        for (int i = 0; i < 3; i++) {
            if (lvl[i] > msaa || lvl[i] > maxSamples) continue;
            glGenRenderbuffers(1, &fillDepth[i]);
            glBindRenderbuffer(GL_RENDERBUFFER, fillDepth[i]);
            glGenFramebuffers(1, &fillFbo[i]);
            glBindFramebuffer(GL_FRAMEBUFFER, fillFbo[i]);
            if (i == 0) {   /* 1x draws straight into the texture, as the game does without MSAA */
                glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, dstTex, 0);
            } else {
                glRenderbufferStorageMultisample(GL_RENDERBUFFER, lvl[i], GL_DEPTH_COMPONENT24, w, h);
                glGenRenderbuffers(1, &fillColor[i]);
                glBindRenderbuffer(GL_RENDERBUFFER, fillColor[i]);
                glRenderbufferStorageMultisample(GL_RENDERBUFFER, lvl[i], GL_RGBA8, w, h);
                glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, fillColor[i]);
            }
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, fillDepth[i]);
            lvlOk[i] = glGetError() == GL_NO_ERROR && glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        }
        if (fillProg && lvlOk[0] && glGetError() == GL_NO_ERROR) {
            const GLint locZ = glGetUniformLocation(fillProg, "uZ"), locG = glGetUniformLocation(fillProg, "uGrid");
            const int cols = 40, rows = 27;
            glBindVertexArray(s_skyplus_vao);
            glViewport(0, 0, w, h);
            glScissor(0, 0, w, h);
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_LESS);
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
            glUseProgram(fillProg);
            glUniform2f(locG, (float)cols, (float)rows);
            auto fill = [&](int i) {
                const uint64_t a = gl_now_ns();
                glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fillFbo[i]);
                glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                for (int l = 0; l < 2; l++) {
                    glUniform1f(locZ, 0.5f - 0.25f * (float)l);
                    glDrawArraysInstanced(GL_TRIANGLES, 0, 6, cols * rows);
                }
                if (i > 0) {   /* the MSAA resolve the game does too */
                    glBindFramebuffer(GL_READ_FRAMEBUFFER, fillFbo[i]);
                    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dstFbo);
                    glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
                }
                glFinish();
                return (double)(gl_now_ns() - a) / 1.0e6;
            };
            for (int i = 0; i < 3; i++) if (lvlOk[i]) fill(i);   /* first of each untimed: target setup, clocks */
            double t1[3], dl[3][3];
            int nr = 0;
            for (int rep = 0; rep < 2 && elapsed_ms() < GPUPROBE_DEADLINE_MS; rep++) {   /* two rounds: min of the two differences */
                t1[nr] = fill(0);
                for (int i = 1; i < 3; i++) dl[i][nr] = lvlOk[i] ? fill(i) - t1[nr] : 0.0;
                nr++;
            }
            if (nr > 0) {
                auto median = [&](const double* v) {
                    double t[3];
                    for (int i = 0; i < nr; i++) t[i] = v[i];
                    for (int i = 1; i < nr; i++) for (int j = i; j > 0 && t[j] < t[j - 1]; j--) { const double x = t[j]; t[j] = t[j - 1]; t[j - 1] = x; }
                    return t[(nr - 1) / 2];   /* lower median: the smaller of two */
                };
                const double base = median(t1);
                msaaMs[0] = (float)base;
                for (int i = 1; i < 3; i++) if (lvlOk[i]) msaaMs[i] = (float)(base + median(dl[i]));
                measured = 1;
                for (int i = 1; i < 3; i++) if (lvlOk[i]) add_line(i == 1 ? "MSAA 2x   %+5.1f ms" : "MSAA 4x   %+5.1f ms", (double)(msaaMs[i] - msaaMs[0]));
                show(0);
            }
        }
        glBindFramebuffer(GL_FRAMEBUFFER, dstFbo);
    }
    tMsaa = elapsed_ms();

    /* ---- alpha-to-coverage against the blend, through an MSAA target, at the level that will be chosen ---- */
    const int pick = gpuProbeMsaaPick(msaaMs, msaa, run->frameMs);
    int samples = pick >= 2 ? pick : (msaa > 1 ? msaa : 2);   /* 1x picked: A2C is moot for now, timed at 2x for a later raise */
    if (samples > maxSamples) samples = maxSamples;
    if (dstOk && samples >= 2 && elapsed_ms() < GPUPROBE_DEADLINE_MS) {
        static const char* vs = R"GLSL(
uniform float uZ;
void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    gl_Position = vec4(p * 2.0 - 1.0, uZ, 1.0);
}
)GLSL";
        /* mostly 0 or 1 with thin soft edges: what a filtered cut-out texture (foliage, fences) gives */
        static const char* fs = R"GLSL(
uniform float uLayer;
out vec4 o;
void main() {
    vec2 p = gl_FragCoord.xy;
    float t = 0.5 + 0.5 * sin(p.x * 0.11 + uLayer) * sin(p.y * 0.09 - uLayer * 1.7);
    float a = smoothstep(0.42, 0.58, t);
#ifdef A2C
    /* what the game's A2C cut-out shader does (SHADER_OPT_A2C): a derivative-sharpened edge and a discard, which on a
     * tiler costs early depth rejection; the blend variant has neither */
    a = clamp((a - 0.5) / max(fwidth(a), 1.0 / 1024.0) + 0.5, 0.0, 1.0);
    if (a < 1.0 / 256.0) discard;
#endif
    o = vec4(0.25, 0.55, 0.2, a);
}
)GLSL";
        const uint64_t ta0 = gl_now_ns();
        a2cProg = gp_compile(vs, fs, "#define A2C 1\n");
        a2cProgB = gp_compile(vs, fs, "");
        compileMs += (double)(gl_now_ns() - ta0) / 1.0e6;
        if (!s_skyplus_vao) glGenVertexArrays(1, &s_skyplus_vao);   /* core profile needs a VAO bound for any draw */
        glGenRenderbuffers(1, &msColor);
        glGenRenderbuffers(1, &msDepth);
        glBindRenderbuffer(GL_RENDERBUFFER, msColor);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA8, w, h);
        glBindRenderbuffer(GL_RENDERBUFFER, msDepth);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH_COMPONENT24, w, h);
        glGenFramebuffers(1, &msFbo);
        glBindFramebuffer(GL_FRAMEBUFFER, msFbo);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, msColor);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, msDepth);
        if (a2cProg && a2cProgB && glGetError() == GL_NO_ERROR && glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
            const GLint locZ[2] = { glGetUniformLocation(a2cProgB, "uZ"), glGetUniformLocation(a2cProg, "uZ") },
                        locL[2] = { glGetUniformLocation(a2cProgB, "uLayer"), glGetUniformLocation(a2cProg, "uLayer") };
            glBindVertexArray(s_skyplus_vao);
            glViewport(0, 0, w, h);
            glScissor(0, 0, w, h);
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_LESS);
            glDepthMask(GL_TRUE);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            auto a2cRun = [&](bool a2c) {
                const uint64_t a = gl_now_ns();
                glBindFramebuffer(GL_DRAW_FRAMEBUFFER, msFbo);
                glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                glUseProgram(a2c ? a2cProg : a2cProgB);
                if (a2c) { glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE); glDisable(GL_BLEND); }
                else { glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE); glEnable(GL_BLEND); }
                for (int l = 0; l < GPUPROBE_A2C_LAYERS; l++) {   /* far to near: every layer passes the depth test */
                    glUniform1f(locZ[a2c], 0.9f - 0.15f * (float)l);
                    glUniform1f(locL[a2c], (float)l);
                    glDrawArrays(GL_TRIANGLES, 0, 3);
                }
                glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
                glDisable(GL_BLEND);
                glBindFramebuffer(GL_READ_FRAMEBUFFER, msFbo);
                glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dstFbo);
                glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);   /* the MSAA resolve the game does too */
                glFinish();
                return (double)(gl_now_ns() - a) / 1.0e6;
            };
            a2cRun(false);
            a2cRun(true);   /* first of each untimed: shader and target setup */
            /* The GPU clock steps between runs (the governor), so the two variants are compared back to back and the
             * median (lower, with two pairs) of the per-pair differences is taken: a min over each variant separately mixes two clocks. */
            double d[3];
            int nd = 0;
            for (int rep = 0; rep < 2 && elapsed_ms() < GPUPROBE_DEADLINE_MS; rep++) {   /* two pairs: the smaller difference (a stall only inflates) */
                const double bl = a2cRun(false), ac = a2cRun(true);
                d[nd++] = ac - bl;
            }
            if (nd > 0) {
                for (int i = 1; i < nd; i++) for (int j = i; j > 0 && d[j] < d[j - 1]; j--) { const double t = d[j]; d[j] = d[j - 1]; d[j - 1] = t; }
                run->a2cDeltaMs = (float)d[(nd - 1) / 2];
                measured = 1;
                add_line("A2C       %+5.1f ms", (double)run->a2cDeltaMs);
                show(0);
            }
        }
    }

    run->choice = gpuProbeDecide(skyMs, msaaMs, msaa, run->a2cDeltaMs, run->frameMs);
    if (vis) {   /* the summary, held so it can be read */
        static const char* skyName[3] = { "full", "half", "quarter" };
        const GpuProbeChoice& c = run->choice;
        title = "GPU measured";
        nlines = 0;
        snprintf(lines[nlines++], sizeof lines[0], "Sky:  %s", c.skyScale >= 0 ? skyName[c.skyScale] : "kept");
        if (c.msaa >= 0) snprintf(lines[nlines++], sizeof lines[0], "MSAA: %dx", c.msaa);
        else snprintf(lines[nlines++], sizeof lines[0], "MSAA: kept");
        snprintf(lines[nlines++], sizeof lines[0], "A2C:  %s", c.a2c < 0 ? "kept" : c.a2c ? "on" : "off");
        show(run->holdMs);
    }

    /* ---- clean up and put the state back ---- */
    glFinish();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (dstFbo) glDeleteFramebuffers(1, &dstFbo);
    if (redFbo) glDeleteFramebuffers(1, &redFbo);
    if (msFbo) glDeleteFramebuffers(1, &msFbo);
    if (dstTex) glDeleteTextures(1, &dstTex);
    if (redTex) glDeleteTextures(1, &redTex);
    if (noiseTex) glDeleteTextures(1, &noiseTex);
    for (int i = 0; i < 3; i++) {
        if (fillFbo[i]) glDeleteFramebuffers(1, &fillFbo[i]);
        if (fillColor[i]) glDeleteRenderbuffers(1, &fillColor[i]);
        if (fillDepth[i]) glDeleteRenderbuffers(1, &fillDepth[i]);
    }
    if (fillProg) glDeleteProgram(fillProg);
    if (txFbo) glDeleteFramebuffers(1, &txFbo);
    if (txTex) glDeleteTextures(1, &txTex);
    if (msColor) glDeleteRenderbuffers(1, &msColor);
    if (msDepth) glDeleteRenderbuffers(1, &msDepth);
    if (a2cProg) glDeleteProgram(a2cProg);
    if (a2cProgB) glDeleteProgram(a2cProgB);
    glActiveTexture(GL_TEXTURE0 + 7);
    glBindTexture(GL_TEXTURE_2D, (GLuint)prev_tex7);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, (GLuint)prev_tex0);
    glActiveTexture((GLenum)prev_unit);
    glBindRenderbuffer(GL_RENDERBUFFER, (GLuint)prev_rb);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)prev_fbo);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)prev_rfbo);
    glBindVertexArray((GLuint)prev_vao);
    glUseProgram((GLuint)prev_prog);
    gfx_opengl_set_a2c(prev_a2c);
    glViewport(prev_vp[0], prev_vp[1], prev_vp[2], prev_vp[3]);
    glScissor(prev_sc[0], prev_sc[1], prev_sc[2], prev_sc[3]);
    glColorMask(prev_cm[0], prev_cm[1], prev_cm[2], prev_cm[3]);
    glDepthMask(prev_dm);
    glDepthFunc((GLenum)prev_dfunc);
    glBlendFuncSeparate((GLenum)prev_bf[0], (GLenum)prev_bf[1], (GLenum)prev_bf[2], (GLenum)prev_bf[3]);
    if (prev_depth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (prev_blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    if (prev_cull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    if (prev_scissor) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
    while (glGetError() != GL_NO_ERROR) {}
    sysLogPrintf(LOG_NOTE, "gpuprobe: measured in %.0f ms of work (excl. shader compiles; shaders ready at %.0f, sky done at %.0f, msaa done at %.0f); %.0f ms wall incl. %.0f ms %s",
                 elapsed_ms(), tCompiled, tSky, tMsaa, (double)(gl_now_ns() - t0) / 1.0e6, shownMs, vis ? "on screen" : "(hidden)");
    return measured;
}

