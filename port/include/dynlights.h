#ifndef PORT_DYNLIGHTS_H
#define PORT_DYNLIGHTS_H

/* Dynamic lights (Video.Visuals = Enhanced).
 *
 * GE has no dynamic light of its own: room lighting is baked into vertex
 * colours, characters and objects take a tint from the floor tile they stand
 * on (propobj.c set_color_shading_from_tile), and a shot-out lamp only
 * darkens its own polygons. This adds real lights: explosions, and the sun
 * of the GE+ sky.
 *
 * Game side (port/src/dynlights.c): once per player view, just before the
 * level is drawn, the active light sources are gathered, moved into that
 * player's eye space (camGetWorldToScreenMtxf) and handed to fast3d through
 * one extended display-list command. Nothing in the game reads them.
 *
 * Fast3D side (gfx_pc.cpp gfx_dynlights_sync): the decoder turns the list into
 * a struct GfxLights (gfx_rendering_api.h) with the inverse of the camera's
 * clip transform, and the deko3d / Vulkan shaders light every pixel from it:
 * point lights from the pixel's eye-space position and a flat normal, the sun
 * as a directional light (radius < 0). OpenGL has no dynamic lights. */

#define G_DYNLIGHTS_EXT 0x49   /* w0 bits 0-7: light count; w1: PortDynLight[count] */
#define G_SHADOWCAST_EXT 0x4A  /* w0 bits 0-7: 1 = the draws that follow (props) cast shadows, 0 = they stop; no-op today */

#define PORT_DYNLIGHTS_MAX 16   /* point lights; more candidates: the strongest nearest are kept (dynlights.c) */

typedef struct PortDynLight {
    float pos[3];   /* eye space of the view being drawn */
    float radius;   /* world units; no light at or beyond it */
    float col[3];   /* linear colour (may exceed 1), already scaled by the light's current intensity */
    float pad;
} PortDynLight;

#ifndef __cplusplus
#include <ultra64.h>
/* Appends the light command for the current player's view (lv.c lvlRender). */
Gfx *portDynLightsEmit(Gfx *gdl);
/* Brackets the prop draws of the view (bg.c, #ifdef PORT) with G_SHADOWCAST_EXT (Enhanced only). */
Gfx *portShadowCastMark(Gfx *gdl, int on);
/* 1 = GE's round blob shadow is not drawn (GE_NOBLOB=1, model.c doshadow). */
int portHideBlob(void);
#endif

#endif
