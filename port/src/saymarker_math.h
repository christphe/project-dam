#ifndef _SAYMARKER_MATH_H_
#define _SAYMARKER_MATH_H_

/* Positioned dialogue lines (#132): where a speaker's marker goes on one player's view, and how strong it is.
 * Pure: no game globals, no SDL; tests/test_saymarker.c drives it directly. saymarker.c feeds it the player's
 * camera (bondview.c: field_10CC, the c_* screen fields) and draws the result. */

/* One player's view, in the HUD's logical pixels (the c_* fields of the player). */
typedef struct SayView {
    float left, top;              /* c_screenleft, c_screentop */
    float halfw, halfh;           /* c_halfwidth, c_halfheight */
    float recipscalex, recipscaley;
    float near;                   /* c_perspnear: closer than this (or behind) is not projected */
} SayView;

/* Where the marker sits. onscreen: at the speaker's projected point. Otherwise it is pinned to the inset edge of
 * the view and (dirx, diry) is the unit screen direction from the view centre towards the speaker (y down). */
typedef struct SayMarkerPos {
    float x, y;
    int onscreen;
    float dirx, diry;
} SayMarkerPos;

/* m: the world-to-camera matrix in the game's row-vector layout (mtx4TransformVecInPlace: v' = v * m, translation
 * in m[3]); the camera looks down -z. world: the point the marker hangs from (above the speaker's head).
 * inset_x / inset_top / inset_bottom: the margin an edge-pinned marker keeps from each edge of the view. */
void sayMarkerPlace(const float m[4][4], const SayView *view, const float world[3],
                    float inset_x, float inset_top, float inset_bottom, SayMarkerPos *out);

/* How strong the marker is at `dist` world units: 1 up to `nearDist`, falling linearly to 0 at `farDist`. */
float sayMarkerCloseness(float dist, float nearDist, float farDist);

/* "Both" (the chosen look): fainter and smaller with distance, never below the floor so a far speaker still reads. */
float sayMarkerAlpha(float closeness);    /* 0.35 .. 1 */
float sayMarkerScale(float closeness);    /* 0.6 .. 1  */

#endif
