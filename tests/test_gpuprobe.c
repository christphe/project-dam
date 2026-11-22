/*
 * The first-launch GPU benchmark's decision (port/src/gpuprobe.c). Pure: links only that file.
 * Budgets at 60 Hz: sky 3.0 ms, alpha-to-coverage delta 1.5 ms, MSAA over 1x 2.0 ms.
 */
#include "ge_test.h"
#include "gpuprobe.h"

#define FRAME (1000.0f / 60.0f)
#define U GPUPROBE_UNMEASURED

static const float nm[3] = { U, U, U };   /* MSAA not measured */

int main(void)
{
    GE_TEST_BEGIN("gpuprobe");
    {   /* a strong GPU: full sky, A2C on */
        const float s[3] = { 1.2f, 0.4f, 0.2f };
        GpuProbeChoice c = gpuProbeDecide(s, nm, 4, 0.3f, FRAME);
        GE_CHECK_EQ_INT(c.skyScale, 0);
        GE_CHECK_EQ_INT(c.a2c, 1);
    }
    {   /* Mali-G31 class: 23 / 6 / 1.7 ms, A2C +5 ms */
        const float s[3] = { 23.0f, 6.0f, 1.7f };
        GpuProbeChoice c = gpuProbeDecide(s, nm, 4, 5.0f, FRAME);
        GE_CHECK_EQ_INT(c.skyScale, 2);
        GE_CHECK_EQ_INT(c.a2c, 0);
    }
    {   /* in between: half fits */
        const float s[3] = { 9.0f, 2.9f, 0.8f };
        GpuProbeChoice c = gpuProbeDecide(s, nm, 4, 1.0f, FRAME);
        GE_CHECK_EQ_INT(c.skyScale, 1);
        GE_CHECK_EQ_INT(c.a2c, 1);
    }
    {   /* boundaries are inclusive */
        const float s[3] = { 3.0f, 1.0f, 0.5f };
        GpuProbeChoice c = gpuProbeDecide(s, nm, 4, 1.5f, FRAME);
        GE_CHECK_EQ_INT(c.skyScale, 0);
        GE_CHECK_EQ_INT(c.a2c, 1);
    }
    {   /* just over */
        const float s[3] = { 3.1f, 1.0f, 0.5f };
        GpuProbeChoice c = gpuProbeDecide(s, nm, 4, 1.6f, FRAME);
        GE_CHECK_EQ_INT(c.skyScale, 1);
        GE_CHECK_EQ_INT(c.a2c, 0);
    }
    {   /* nothing fits: quarter anyway */
        const float s[3] = { 90.0f, 30.0f, 8.0f };
        GpuProbeChoice c = gpuProbeDecide(s, nm, 4, 0.0f, FRAME);
        GE_CHECK_EQ_INT(c.skyScale, 2);
    }
    {   /* the probe ran out of time after the full size: smaller sizes unmeasured */
        const float s[3] = { 80.0f, U, U };
        GpuProbeChoice c = gpuProbeDecide(s, nm, 4, U, FRAME);
        GE_CHECK_EQ_INT(c.skyScale, 2);
        GE_CHECK_EQ_INT(c.a2c, -1);
    }
    {   /* nothing measured: keep everything */
        const float s[3] = { U, U, U };
        GpuProbeChoice c = gpuProbeDecide(s, nm, 4, U, FRAME);
        GE_CHECK_EQ_INT(c.skyScale, -1);
        GE_CHECK_EQ_INT(c.a2c, -1);
    }
    {   /* sky skipped, A2C measured; negative delta (A2C faster than the blend) keeps it on */
        const float s[3] = { U, U, U };
        GpuProbeChoice c = gpuProbeDecide(s, nm, 4, -0.4f, FRAME);
        GE_CHECK_EQ_INT(c.skyScale, -1);
        GE_CHECK_EQ_INT(c.a2c, 1);
        c = gpuProbeDecide(s, nm, 4, 4.0f, FRAME);
        GE_CHECK_EQ_INT(c.a2c, 0);
    }
    {   /* a 30 Hz budget doubles both allowances */
        const float s[3] = { 5.5f, 2.0f, 1.0f };
        GpuProbeChoice c = gpuProbeDecide(s, nm, 4, 2.9f, 1000.0f / 30.0f);
        GE_CHECK_EQ_INT(c.skyScale, 0);
        GE_CHECK_EQ_INT(c.a2c, 1);
    }
    {   /* MSAA: a strong GPU keeps 4x (delta 1.0 ms) */
        const float m[3] = { 4.0f, 4.4f, 5.0f };
        GE_CHECK_EQ_INT(gpuProbeMsaaPick(m, 4, FRAME), 4);
    }
    {   /* 4x over budget (+3.1), 2x fits (+1.2) */
        const float m[3] = { 5.0f, 6.2f, 8.1f };
        GE_CHECK_EQ_INT(gpuProbeMsaaPick(m, 4, FRAME), 2);
    }
    {   /* nothing fits: 1x */
        const float m[3] = { 5.0f, 8.0f, 12.0f };
        GE_CHECK_EQ_INT(gpuProbeMsaaPick(m, 4, FRAME), 1);
    }
    {   /* boundary inclusive (2.0 ms) and just over */
        const float a[3] = { 5.0f, 5.5f, 7.0f }, b[3] = { 5.0f, 5.5f, 7.1f };
        GE_CHECK_EQ_INT(gpuProbeMsaaPick(a, 4, FRAME), 4);
        GE_CHECK_EQ_INT(gpuProbeMsaaPick(b, 4, FRAME), 2);
    }
    {   /* never raised above the current level: current 2x, 4x would fit but is not a candidate */
        const float m[3] = { 5.0f, 5.5f, 6.0f };
        GE_CHECK_EQ_INT(gpuProbeMsaaPick(m, 2, FRAME), 2);
        const float n[3] = { 5.0f, 9.0f, 6.0f };
        GE_CHECK_EQ_INT(gpuProbeMsaaPick(n, 2, FRAME), 1);
    }
    {   /* 1x already, above 4x (not measured there), or nothing measured: keep */
        const float m[3] = { 5.0f, 5.5f, 6.0f };
        GE_CHECK_EQ_INT(gpuProbeMsaaPick(m, 1, FRAME), -1);
        GE_CHECK_EQ_INT(gpuProbeMsaaPick(m, 8, FRAME), -1);
        GE_CHECK_EQ_INT(gpuProbeMsaaPick(nm, 4, FRAME), -1);
    }
    {   /* 4x unmeasured (GL_MAX_SAMPLES 2 or out of time): 2x decides; the 1x baseline missing: keep */
        const float m[3] = { 5.0f, 5.8f, U }, o[3] = { U, 5.8f, 6.0f };
        GE_CHECK_EQ_INT(gpuProbeMsaaPick(m, 4, FRAME), 2);
        GE_CHECK_EQ_INT(gpuProbeMsaaPick(o, 4, FRAME), -1);
    }
    {   /* a negative delta (noise) fits; a 30 Hz budget doubles the allowance */
        const float m[3] = { 5.0f, 4.8f, 4.9f }, k[3] = { 5.0f, 6.5f, 8.9f };
        GE_CHECK_EQ_INT(gpuProbeMsaaPick(m, 4, FRAME), 4);
        GE_CHECK_EQ_INT(gpuProbeMsaaPick(k, 4, FRAME), 2);
        GE_CHECK_EQ_INT(gpuProbeMsaaPick(k, 4, 1000.0f / 30.0f), 4);
    }
    {   /* the decision carries all three */
        const float s[3] = { 23.0f, 6.0f, 1.7f }, m[3] = { 5.0f, 6.2f, 8.1f };
        GpuProbeChoice c = gpuProbeDecide(s, m, 4, 5.0f, FRAME);
        GE_CHECK_EQ_INT(c.skyScale, 2);
        GE_CHECK_EQ_INT(c.msaa, 2);
        GE_CHECK_EQ_INT(c.a2c, 0);
    }
    return GE_TEST_END();
}
