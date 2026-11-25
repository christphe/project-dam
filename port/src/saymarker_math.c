/* Positioned dialogue lines (#132): marker placement and strength. See saymarker_math.h. */
#include "saymarker_math.h"
#include <math.h>

/* The game's own math.h (include/) comes first on the game build and has no fabsf. */
static float sayAbs(float x) { return x < 0.0f ? -x : x; }

void sayMarkerPlace(const float m[4][4], const SayView *v, const float p[3],
                    float inset_x, float inset_top, float inset_bottom, SayMarkerPos *out)
{
    /* World to camera, the game's row-vector convention (matrixmath.c mtx4TransformVecInPlace). */
    float cx = p[0] * m[0][0] + p[1] * m[1][0] + p[2] * m[2][0] + m[3][0];
    float cy = p[0] * m[0][1] + p[1] * m[1][1] + p[2] * m[2][1] + m[3][1];
    float cz = p[0] * m[0][2] + p[1] * m[1][2] + p[2] * m[2][2] + m[3][2];
    float midx = v->left + v->halfw, midy = v->top + v->halfh;
    float dx, dy, len, hx, hy, tx, ty, t;

    if (-cz > v->near) {
        /* In front: the game's own projection (bondview.c, the function above divide3DCoordinates). */
        float sx = midx - cx / cz * v->recipscalex;
        float sy = cy / cz * v->recipscaley + midy;
        if (sx >= v->left + inset_x && sx <= v->left + 2.0f * v->halfw - inset_x &&
            sy >= v->top + inset_top && sy <= v->top + 2.0f * v->halfh - inset_bottom) {
            out->x = sx;
            out->y = sy;
            out->onscreen = 1;
            out->dirx = out->diry = 0.0f;
            return;
        }
        dx = sx - midx;
        dy = sy - midy;
    } else {
        /* Beside or behind: seen from above. Right stays right, and the further behind, the lower on the screen. */
        dx = cx;                /* camera x: right is +x (in front, +x projects right of the centre too) */
        dy = cz;                /* behind is +z: down on the screen */
    }

    len = sqrtf(dx * dx + dy * dy);
    if (len < 1e-6f) { dx = 0.0f; dy = 1.0f; len = 1.0f; }
    dx /= len;
    dy /= len;

    hx = v->halfw - inset_x;
    hy = dy < 0.0f ? v->halfh - inset_top : v->halfh - inset_bottom;
    if (hx < 0.0f) hx = 0.0f;
    if (hy < 0.0f) hy = 0.0f;
    tx = sayAbs(dx) > 1e-6f ? hx / sayAbs(dx) : 1e30f;
    ty = sayAbs(dy) > 1e-6f ? hy / sayAbs(dy) : 1e30f;
    t = tx < ty ? tx : ty;

    out->x = midx + dx * t;
    out->y = midy + dy * t;
    out->onscreen = 0;
    out->dirx = dx;
    out->diry = dy;
}

float sayMarkerCloseness(float dist, float nearDist, float farDist)
{
    if (dist <= nearDist) return 1.0f;
    if (dist >= farDist || farDist <= nearDist) return 0.0f;
    return 1.0f - (dist - nearDist) / (farDist - nearDist);
}

float sayMarkerAlpha(float k) { return 0.35f + 0.65f * k; }
float sayMarkerScale(float k) { return 0.6f + 0.4f * k; }
