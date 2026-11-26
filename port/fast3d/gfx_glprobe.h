#ifndef GFX_GLPROBE_H
#define GFX_GLPROBE_H

/* First-launch GPU mini-benchmark, GL half (port/src/gpuprobe.h decides from its numbers; video.c applies them once
 * per ini). Game side: draws raw GL on the GL backend's context (ge_render_gl.h). Main thread with the context, right
 * after ge_render_open. Fills run->skyMs / msaaMs / a2cDeltaMs / choice; unmeasured values come back as
 * GPUPROBE_UNMEASURED. Returns 1 if anything was measured. */

#ifdef __cplusplus
extern "C" {
#endif
struct GpuProbeRun;
int gfx_opengl_gpuprobe(struct GpuProbeRun* run);
#ifdef __cplusplus
}
#endif

#endif
