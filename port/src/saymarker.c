/* Positioned dialogue lines (#132): which top-message slot carries a speaker, and the marker drawn for it in each
 * player's view. See port/include/saymarker.h; the placement maths is port/src/saymarker_math.c. */
#include <ultra64.h>
#include <string.h>
#include <math.h>
#include "bondtypes.h"
/* bondview2.c's own buffer sizes (the internal header declares the buffers with them). */
#define BONDVIEW_HUD_MSG_TOP_BUFFER_LENGTH 0x97
#define BONDVIEW_HUD_MSG_BOTTOM_BUFFER_LENGTH 0x65
#include "game/bondview.h"
#include "game/bondview_internal.h"
#include "game/player.h"
#include "game/textrelated.h"
#include "saymarker.h"
#include "saymarker_math.h"
#include "filltri.h"

#if defined(LEFTOVERDEBUG)
#define SAY_TOP_TEXT(i) (stringbuffer_top[(i)])
#else
#define SAY_TOP_TEXT(i) (dword_CODE_bss_80079DC8[(i)])
#endif

/* World units. Full strength up to SAY_NEAR; without a script range, the fade and the shrink end at SAY_FAR
 * (the marker then stays at its floor: alpha 0.35, scale 0.6). */
#define SAY_NEAR 300.0f
#define SAY_FAR  3000.0f

/* HUD pixels an edge-pinned marker keeps from the view's edges. The top one clears the dialogue box
 * (bondview2.c: the box starts at the view top + 0xd, one line of Zurich Bold below it). */
/* The bubble's size at full strength: 1.5 x its 14 x 10 drawing, so it reads on a TV in half a split screen. */
#define SAY_SIZE 1.5f

#define SAY_INSET_X      14.0f
#define SAY_INSET_TOP    48.0f
#define SAY_INSET_BOTTOM 26.0f

#define SAY_RGB_FILL    0xF0, 0xC3, 0x5A   /* amber */
#define SAY_RGB_DOTS    0x1B, 0x1B, 0x16
#define SAY_RGB_OUTLINE 0x00, 0x00, 0x00

typedef struct SaySlot {
    s32 active;
    char text[BONDVIEW_HUD_MSG_TOP_BUFFER_LENGTH];
    SayAnchorFn fn;
    void *ud;
    s32 key;
    f32 fixed[3];
    f32 range;
} SaySlot;

static SaySlot s_slots[2];

void sayMarkerQueue(const char *line, SayAnchorFn fn, void *ud, int key, const float *fixed, float range)
{
    SaySlot *s;
    if (display_upper_text_window >= 2) return;   /* hudmsgTopShow drops the line: the queue is full */
    s = &s_slots[(upper_text_buffer_index + display_upper_text_window) % 2];
    memset(s, 0, sizeof *s);
    if ((!fn && !fixed) || !line) return;          /* a plain line: this slot carries no marker */
    strncpy(s->text, line, sizeof s->text - 1);    /* hudmsgTopShow copies the same BUFFER_LENGTH - 1 bytes */
    s->fn = fn;
    s->ud = ud;
    s->key = key;
    if (fixed) memcpy(s->fixed, fixed, sizeof s->fixed);
    s->range = range;
    s->active = 1;
}

void sayMarkerReset(void)
{
    memset(s_slots, 0, sizeof s_slots);
}

/* The slot the top message shows now, if it carries a speaker; NULL otherwise. Same gate as the message itself
 * (bondview2.c sub_GAME_7F08AAE8). */
static SaySlot *shownSlot(void)
{
    SaySlot *s;
    if (g_UpperTextDisplayFlag != 0 || upper_text_window_timer < 0 || display_upper_text_window <= 0) return NULL;
    if (!g_CurrentPlayer || g_CurrentPlayer->mpmenuon) return NULL;
    s = &s_slots[upper_text_buffer_index % 2];
    if (!s->active) return NULL;
    if (strncmp(s->text, SAY_TOP_TEXT(upper_text_buffer_index % 2), sizeof s->text - 1) != 0) {
        s->active = 0;   /* a game message took the slot */
        return NULL;
    }
    return s;
}

/* This player's distance to the speaker (from the camera); < 0 when there is no speaker or no camera. */
static f32 speakerDistance(SaySlot *s, float xyz[3])
{
    Mtxf *v2w = g_CurrentPlayer->viewtoworldmtxf;
    f32 dx, dy, dz;
    if (!v2w) return -1.0f;
    if (s->fn) {
        if (!s->fn(s->ud, s->key, xyz)) return -1.0f;
    } else {
        memcpy(xyz, s->fixed, sizeof s->fixed);
    }
    dx = xyz[0] - v2w->m[3][0];
    dy = xyz[1] - v2w->m[3][1];
    dz = xyz[2] - v2w->m[3][2];
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

s32 sayMarkerHidesLine(void)
{
    float xyz[3];
    f32 d;
    SaySlot *s;
    s = shownSlot();
    if (!s || s->range <= 0.0f) return 0;
    d = speakerDistance(s, xyz);
    return d >= 0.0f && d > s->range;
}

/* Nearest pixel. The game's math.h (first on the include path) has no floorf; HUD coordinates are >= 0 here
 * apart from a marker half off the left/top edge, which fill() clamps anyway. */
static s32 px(f32 v) { return v < 0.0f ? (s32)(v - 0.5f) : (s32)(v + 0.5f); }

static Gfx *fill(Gfx *gdl, f32 x0, f32 y0, f32 x1, f32 y1)
{
    s32 ix0 = px(x0), iy0 = px(y0);
    s32 ix1 = px(x1), iy1 = px(y1);
    if (ix0 < 0) ix0 = 0;
    if (iy0 < 0) iy0 = 0;
    if (ix1 <= ix0 || iy1 <= iy0) return gdl;
    gDPFillRectangle(gdl++, ix0, iy0, ix1, iy1);
    return gdl;
}

/* The arrow from the bubble (centre cx, cy; half size hw x hh) towards the speaker (dx, dy unit), its base just off
 * the bubble's edge. grow: added on every side (the outline pass). */
static Gfx *arrow(Gfx *gdl, f32 cx, f32 cy, f32 hw, f32 hh, f32 dx, f32 dy, f32 sc, f32 grow)
{
    f32 adx = dx < 0.0f ? -dx : dx, ady = dy < 0.0f ? -dy : dy;
    f32 tx = adx > 1e-4f ? hw / adx : 1e9f, ty = ady > 1e-4f ? hh / ady : 1e9f;
    f32 base = (tx < ty ? tx : ty) + 1.5f * sc - grow, tip = base + 8.0f * sc + 2.5f * grow, half = 3.0f * sc + grow;   /* ~40 deg: reads as a direction */
    /* One real triangle (port/include/filltri.h): smooth edges at the output resolution, 10.2 coordinates. */
    gDPFillTriangleEXT(gdl, (s32)((cx + dx * tip) * 4.0f), (s32)((cy + dy * tip) * 4.0f),
                       (s32)((cx + dx * base - dy * half) * 4.0f), (s32)((cy + dy * base + dx * half) * 4.0f),
                       (s32)((cx + dx * base + dy * half) * 4.0f), (s32)((cy + dy * base - dx * half) * 4.0f));
    return gdl + 2;
}

static Gfx *prim(Gfx *gdl, u8 r, u8 g, u8 b, f32 alpha)
{
    gDPPipeSync(gdl++);
    gDPSetPrimColor(gdl++, 0, 0, r, g, b, (u8)(alpha * 255.0f + 0.5f));
    return gdl;
}

/* The bubble: a rounded amber box with three dots, centred on (cx, cy); w x h at scale 1 is 14 x 10. */
static Gfx *bubble(Gfx *gdl, f32 cx, f32 cy, f32 sc, f32 alpha)
{
    f32 hw = 7.0f * sc, hh = 5.0f * sc, dot = 2.0f * sc < 1.0f ? 1.0f : 2.0f * sc, gap = 3.5f * sc;
    gdl = prim(gdl, SAY_RGB_OUTLINE, alpha * 0.55f);
    gdl = fill(gdl, cx - hw - 1, cy - hh, cx + hw + 1, cy + hh);
    gdl = fill(gdl, cx - hw, cy - hh - 1, cx + hw, cy + hh + 1);
    gdl = prim(gdl, SAY_RGB_FILL, alpha);
    gdl = fill(gdl, cx - hw, cy - hh + 1, cx + hw, cy + hh - 1);
    gdl = fill(gdl, cx - hw + 1, cy - hh, cx + hw - 1, cy + hh);
    gdl = prim(gdl, SAY_RGB_DOTS, alpha);
    gdl = fill(gdl, cx - gap - dot / 2, cy - dot / 2, cx - gap + dot / 2, cy + dot / 2);
    gdl = fill(gdl, cx - dot / 2, cy - dot / 2, cx + dot / 2, cy + dot / 2);
    gdl = fill(gdl, cx + gap - dot / 2, cy - dot / 2, cx + gap + dot / 2, cy + dot / 2);
    return gdl;
}

Gfx *sayMarkerRender(Gfx *gdl)
{
    SaySlot *s = shownSlot();
    float xyz[3], m[4][4];
    SayView view;
    SayMarkerPos pos;
    f32 d, k, sc, alpha, far;
    s32 i, j;

    if (!s || !g_CurrentPlayer->field_10CC) return gdl;
    d = speakerDistance(s, xyz);
    if (d < 0.0f) return gdl;
    if (s->range > 0.0f && d > s->range) return gdl;

    for (i = 0; i < 4; i++)
        for (j = 0; j < 4; j++)
            m[i][j] = g_CurrentPlayer->field_10CC->m[i][j];
    view.left = g_CurrentPlayer->c_screenleft;
    view.top = g_CurrentPlayer->c_screentop;
    view.halfw = g_CurrentPlayer->c_halfwidth;
    view.halfh = g_CurrentPlayer->c_halfheight;
    view.recipscalex = g_CurrentPlayer->c_recipscalex;
    view.recipscaley = g_CurrentPlayer->c_recipscaley;
    view.near = g_CurrentPlayer->c_perspnear;
    sayMarkerPlace(m, &view, xyz, SAY_INSET_X, SAY_INSET_TOP, SAY_INSET_BOTTOM, &pos);

    far = s->range > 0.0f ? s->range : SAY_FAR;
    k = sayMarkerCloseness(d, SAY_NEAR, far);
    sc = sayMarkerScale(k) * SAY_SIZE;
    alpha = sayMarkerAlpha(k);

    gdl = microcode_constructor(gdl);
    gDPSetRenderMode(gdl++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
    gDPSetCombineMode(gdl++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);

    if (pos.onscreen) {
        /* Hanging over the speaker: the bubble sits above the anchor, its tail pointing down at it. */
        f32 cy = pos.y - 9.0f * sc;
        gdl = bubble(gdl, pos.x, cy, sc, alpha);
        gdl = prim(gdl, SAY_RGB_FILL, alpha);
        gdl = fill(gdl, pos.x - 2.0f * sc, cy + 5.0f * sc, pos.x + 2.0f * sc, cy + 6.5f * sc);
        gdl = fill(gdl, pos.x - 1.0f * sc, cy + 6.5f * sc, pos.x + 1.0f * sc, cy + 8.0f * sc);
    } else {
        /* Pinned to the edge: an arrow from the bubble towards the speaker, outlined like the bubble. */
        f32 cx = pos.x - pos.dirx * 9.0f * sc, cy = pos.y - pos.diry * 9.0f * sc;   /* the arrow tip stays inside the inset */
        gdl = bubble(gdl, cx, cy, sc, alpha);
        gdl = prim(gdl, SAY_RGB_OUTLINE, alpha * 0.55f);
        gdl = arrow(gdl, cx, cy, 7.0f * sc, 5.0f * sc, pos.dirx, pos.diry, sc, 1.0f);
        gdl = prim(gdl, SAY_RGB_FILL, alpha);
        gdl = arrow(gdl, cx, cy, 7.0f * sc, 5.0f * sc, pos.dirx, pos.diry, sc, 0.0f);
    }

    gdl = combiner_bayer_lod_perspective(gdl);
    return gdl;
}
