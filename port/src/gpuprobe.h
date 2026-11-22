/* First-launch GPU mini-benchmark: the pure decision half (no GL).
 * The GL half (gfx_opengl.cpp, gfx_opengl_gpuprobe) measures what the GE+ sky costs at each
 * Video.SkyScale and what alpha-to-coverage adds over the plain blend; this turns the
 * numbers into the two settings. video.c applies them once per ini. */
#ifndef GPUPROBE_H
#define GPUPROBE_H

#ifdef __cplusplus
extern "C" {
#endif

/* The sky may take this share of a frame (3 ms of 16.7). Above it the next smaller size is tried: the sky is
 * smooth, so half / quarter size costs little picture, while a weak GPU (Mali-G31: 23 ms at full size) cannot hold 60. */
#define GPUPROBE_SKY_FRAME_SHARE   0.18f
/* Alpha-to-coverage may cost this share of a frame over the plain blend (1.5 ms of 16.7). The blend is what
 * the game falls back to for those surfaces: a little aliasing on leaf edges against a missed frame. */
#define GPUPROBE_A2C_FRAME_SHARE   0.09f
/* MSAA may cost this share of a frame over 1x (2.0 ms of 16.7). The sky gets 18% because a slow sky is a missed frame
 * on every scene; MSAA is an edge-quality feature on top of a picture that is already complete, and on a tiler its cost
 * (resolve + tile memory) is paid every frame, so it gets two thirds of the sky's allowance. The highest level within it wins. */
#define GPUPROBE_MSAA_FRAME_SHARE  0.12f
/* "not measured" marker for the inputs */
#define GPUPROBE_UNMEASURED        (-1.0e9f)   /* a delta can be a small negative number, so not -1 */
#define GPUPROBE_MEASURED(x)       ((x) > -1.0e8f)

typedef struct GpuProbeChoice {
    int skyScale;   /* Video.SkyScale 0 full / 1 half / 2 quarter; -1 = not measured, keep the current value */
    int a2c;        /* alpha-to-coverage 1 on / 0 off; -1 = not measured, keep the current value */
    int msaa;       /* Video.MSAA 1 / 2 / 4; -1 = not measured (or nothing to lower), keep the current value */
} GpuProbeChoice;

/* One probe run: what video.c asks and what the GL half (gfx_opengl_gpuprobe) fills in. */
typedef struct GpuProbeRun {
    int w, h;               /* window size: every target is this big */
    int msaa;               /* the current Video.MSAA (levels above it are not measured, and never chosen) */
    float frameMs;          /* the frame budget, 1000 / 60 */
    int holdMs;             /* how long the summary stays on screen (GE_GPUPROBE_HOLD_MS for a screenshot) */
    int visible;            /* 1: show each phase on the window (the maintainer sees the benchmark); GE_GPUPROBE_HIDDEN=1 turns it off */
    void (*present)(void);  /* swaps the window (video.c wraps the window manager's swap); NULL = not visible */
    float skyMs[3];         /* out: ms per sky draw at Video.SkyScale 0/1/2 */
    float msaaMs[3];        /* out: ms of the MSAA fill workload at 1x / 2x / 4x (GPUPROBE_UNMEASURED where not measured) */
    float a2cDeltaMs;       /* out: ms A2C costs beyond the blend, at the MSAA level chosen */
    GpuProbeChoice choice;  /* out: gpuProbeDecide of the above (the GL half needs the MSAA pick for the A2C run and the summary) */
} GpuProbeRun;

/* Highest MSAA level (4 / 2 / 1) at or below curMsaa whose cost over 1x fits the budget; -1 = keep curMsaa (nothing to
 * lower, nothing measured, or curMsaa is above 4 where the probe measures nothing: it never raises or lowers those).
 * msaaMs[i]: ms at 1x / 2x / 4x (GPUPROBE_UNMEASURED where not measured). Used by the GL half to pick the level it times A2C at. */
int gpuProbeMsaaPick(const float msaaMs[3], int curMsaa, float frameMs);

/* skyMs[i]: ms per sky draw at Video.SkyScale i (GPUPROBE_UNMEASURED when the sky could not be timed).
 * msaaMs / curMsaa: see gpuProbeMsaaPick.
 * a2cDeltaMs: ms the A2C variant costs beyond the blend variant (may be negative; UNMEASURED when skipped).
 * frameMs: the frame budget, 1000 / 60. */
GpuProbeChoice gpuProbeDecide(const float skyMs[3], const float msaaMs[3], int curMsaa, float a2cDeltaMs, float frameMs);

#ifdef __cplusplus
}
#endif
#endif
