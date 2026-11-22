/* First-launch GPU mini-benchmark: the decision (see gpuprobe.h). Pure, so tests/test_gpuprobe.c covers it. */
#include "gpuprobe.h"

int gpuProbeMsaaPick(const float msaaMs[3], int curMsaa, float frameMs)
{
    const float budget = frameMs * GPUPROBE_MSAA_FRAME_SHARE + 1.0e-4f;   /* inclusive: 12% of 16.667 is 2.0 only up to float rounding */
    int any = 0;

    if (curMsaa < 2 || curMsaa > 4 || !GPUPROBE_MEASURED(msaaMs[0])) return -1;
    for (int lvl = 2; lvl >= 1; lvl--) {   /* index 2 = 4x, 1 = 2x */
        const int samples = 1 << lvl;
        if (samples > curMsaa || !GPUPROBE_MEASURED(msaaMs[lvl])) continue;   /* an unmeasured level is not a candidate */
        any = 1;
        if (msaaMs[lvl] - msaaMs[0] <= budget) return samples;
    }
    return any ? 1 : -1;
}

GpuProbeChoice gpuProbeDecide(const float skyMs[3], const float msaaMs[3], int curMsaa, float a2cDeltaMs, float frameMs)
{
    GpuProbeChoice c = { -1, -1, -1 };
    c.msaa = gpuProbeMsaaPick(msaaMs, curMsaa, frameMs);
    const float skyBudget = frameMs * GPUPROBE_SKY_FRAME_SHARE;
    const float a2cBudget = frameMs * GPUPROBE_A2C_FRAME_SHARE;

    if (GPUPROBE_MEASURED(skyMs[0])) {
        c.skyScale = 2;   /* nothing fits: the smallest there is */
        for (int i = 0; i < 3; i++) {
            /* an unmeasured size (the probe ran out of time on a slow GPU) is not a candidate */
            if (GPUPROBE_MEASURED(skyMs[i]) && skyMs[i] <= skyBudget) { c.skyScale = i; break; }
        }
    }
    if (GPUPROBE_MEASURED(a2cDeltaMs)) {
        c.a2c = a2cDeltaMs <= a2cBudget ? 1 : 0;   /* a negative delta (A2C faster than the blend) stays on */
    }
    return c;
}
