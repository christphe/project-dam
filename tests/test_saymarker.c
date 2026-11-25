/* Positioned dialogue markers (#132): placement on one player's view and strength with distance
 * (port/src/saymarker_math.c). Pure: no game core. */
#include "ge_test.h"
#include "saymarker_math.h"

/* The camera at the origin looking down -z (the game's lookat convention), so world == camera space. */
static const float kIdent[4][4] = { { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 }, { 0, 0, 0, 1 } };

/* A 320x240 solo view with a 60 degree vertical fov (bondview.c currentPlayerSetCameraScale). */
static SayView view320(void)
{
    SayView v;
    float scaley = 0.57735027f / 120.0f;   /* tan(30 deg): the game's math.h (first on the path) has no tanf */
    v.left = 0; v.top = 0; v.halfw = 160; v.halfh = 120;
    v.recipscaley = 1.0f / scaley;
    v.recipscalex = 1.0f / (scaley * (4.0f / 3.0f) * 120.0f / 160.0f);
    v.near = 1.0f;
    return v;
}

static int near_(float a, float b, float eps) { float d = a - b; return (d < 0 ? -d : d) <= eps; }

int main(void)
{
    SayView v = view320();
    SayMarkerPos m;
    const float I = 10, T = 40, B = 20;   /* insets */

    GE_TEST_BEGIN("saymarker");

    /* Straight ahead: the centre of the view. */
    { float p[3] = { 0, 0, -500 }; sayMarkerPlace(kIdent, &v, p, I, T, B, &m); }
    GE_CHECK(m.onscreen);
    GE_CHECK(near_(m.x, 160, 0.01f) && near_(m.y, 120, 0.01f));

    /* Ahead, to the right and above: right of centre (x up the screen is -y). */
    { float p[3] = { 50, 30, -500 }; sayMarkerPlace(kIdent, &v, p, I, T, B, &m); }
    GE_CHECK(m.onscreen);
    GE_CHECK(m.x > 160 && m.y < 120);

    /* Ahead but far to the left: pinned to the left edge, pointing left. */
    { float p[3] = { -2000, 0, -500 }; sayMarkerPlace(kIdent, &v, p, I, T, B, &m); }
    GE_CHECK(!m.onscreen);
    GE_CHECK(near_(m.x, I, 0.01f));
    GE_CHECK(m.dirx < -0.99f);

    /* Right behind: pinned to the bottom edge, pointing down. */
    { float p[3] = { 0, 0, 500 }; sayMarkerPlace(kIdent, &v, p, I, T, B, &m); }
    GE_CHECK(!m.onscreen);
    GE_CHECK(near_(m.y, 240 - B, 0.01f) && near_(m.x, 160, 0.01f));
    GE_CHECK(m.diry > 0.99f);

    /* Behind and to the right: bottom-right, pointing right and down. */
    { float p[3] = { 500, 0, 400 }; sayMarkerPlace(kIdent, &v, p, I, T, B, &m); }
    GE_CHECK(!m.onscreen);
    GE_CHECK(m.dirx > 0 && m.diry > 0);
    GE_CHECK(m.x > 160 && m.y > 120);

    /* Exactly beside (z = 0) on the right: right edge, level with the centre. */
    { float p[3] = { 300, 0, 0 }; sayMarkerPlace(kIdent, &v, p, I, T, B, &m); }
    GE_CHECK(!m.onscreen);
    GE_CHECK(near_(m.x, 320 - I, 0.01f) && near_(m.y, 120, 0.01f));

    /* High above, ahead: pinned under the top inset (the dialogue box), not over it. */
    { float p[3] = { 0, 5000, -500 }; sayMarkerPlace(kIdent, &v, p, I, T, B, &m); }
    GE_CHECK(!m.onscreen);
    GE_CHECK(near_(m.y, T, 0.01f));

    /* On the camera itself: no direction, falls back to "down" instead of NaN. */
    { float p[3] = { 0, 0, 0 }; sayMarkerPlace(kIdent, &v, p, I, T, B, &m); }
    GE_CHECK(!m.onscreen);
    GE_CHECK(m.x == m.x && m.y == m.y);
    GE_CHECK(m.diry > 0.99f);

    /* A split-screen view (the lower half of 2P): the centre moves with the view. */
    {
        SayView h = v;
        h.top = 120; h.halfh = 60;
        float p[3] = { 0, 0, -500 };
        sayMarkerPlace(kIdent, &h, p, I, T, B, &m);
        GE_CHECK(m.onscreen && near_(m.y, 180, 0.01f));
        float q[3] = { 0, 0, 500 };
        sayMarkerPlace(kIdent, &h, q, I, T, B, &m);
        GE_CHECK(!m.onscreen && near_(m.y, 240 - B, 0.01f));
    }

    /* A camera moved and turned: 180 degrees about y, standing at x = 1000. A point at x = 1000, z = +500 is
     * then straight ahead. Row-vector matrix: rotation rows, translation row last. */
    {
        const float turned[4][4] = { { -1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, -1, 0 }, { 1000, 0, 0, 1 } };
        float p[3] = { 1000, 0, 500 };
        sayMarkerPlace(turned, &v, p, I, T, B, &m);
        GE_CHECK(m.onscreen && near_(m.x, 160, 0.01f));
    }

    /* Strength: full near, nothing past far, linear between; alpha and scale keep their floor. */
    GE_CHECK(sayMarkerCloseness(100, 300, 3000) == 1.0f);
    GE_CHECK(sayMarkerCloseness(3000, 300, 3000) == 0.0f);
    GE_CHECK(near_(sayMarkerCloseness(1650, 300, 3000), 0.5f, 1e-4f));
    GE_CHECK(sayMarkerCloseness(500, 300, 300) == 0.0f);
    GE_CHECK(near_(sayMarkerAlpha(0), 0.35f, 1e-6f) && near_(sayMarkerAlpha(1), 1.0f, 1e-6f));
    GE_CHECK(near_(sayMarkerScale(0), 0.6f, 1e-6f) && near_(sayMarkerScale(1), 1.0f, 1e-6f));

    return GE_TEST_END();
}
