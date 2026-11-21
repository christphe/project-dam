#ifndef GFX_UI_H
#define GFX_UI_H

/* The menu UI (Dear ImGui, port/src/menu/) is drawn straight through the rendering API, not through
 * N64 display lists: fast3d emulates a 4 KB texture memory, 8-bit vertex indices and S/T scaling that a
 * font atlas and an arbitrary triangle list have no use for. The menu fills these structs once per frame
 * (gfx_ui_submit) while the frame is being built; gfx_pc.cpp draws them after the overlay display list,
 * into the same target at the same resolution (gfx_current_dimensions).
 *
 * Plain structs, no ImGui types: GfxUiVertex has the layout of ImDrawVert. */
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct GfxUiVertex {
    float x, y;        /* target pixels, origin top-left */
    float u, v;        /* 0..1 over the atlas */
    uint32_t col;      /* 0xAABBGGRR */
};

struct GfxUiCmd {
    float clip_x0, clip_y0, clip_x1, clip_y1;   /* target pixels, origin top-left */
    uint32_t idx_offset, elem_count;             /* triangles: elem_count / 3 */
};

/* RGBA32 atlas (copied). Call again whenever it changes (font size, scale). */
void gfx_ui_set_atlas(const uint8_t* rgba, int width, int height);
/* This frame's geometry (copied; indices already rebased across draw lists). nv == 0 clears the UI. */
void gfx_ui_submit(const struct GfxUiVertex* vtx, uint32_t nv, const uint32_t* idx, uint32_t ni,
                   const struct GfxUiCmd* cmd, uint32_t nc);

#ifdef __cplusplus
}
#endif
#endif
