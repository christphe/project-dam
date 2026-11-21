# CVG_X_ALPHA without FORCE_BL is coverage, not blending (#66)

## Symptom

A crate hit by an explosion turns dark (expected) but its lower half shows a
blue tint, with a sharp horizontal edge. At some angles the crate looks see-through:
its inner faces, or a crate behind it, show through. Intact crates are fine.
Every renderer (deko3d, Vulkan, GL), with `GE_MESHCACHE=0`, with Visuals =
Original, and in upstream v0.5.0. ares (US ROM) draws the same crate solid and
dark.

## Root cause

When an explosion damages an object, `propobj.c` (the deform/blacken loop after
`vtxstore_allocate`) copies its vertices, blackens some of them (rgb 0, a 255)
and, for one explosion type, sets the alpha of the others to 0, mostly on the
lower half. The renderer then marks the object `recolor`: env alpha
`v * 50 + 100`, which selects the `G_RM_FOG_PRIM_A, G_RM_AA_ZB_TEX_EDGE2` branch
of `modelApplyRenderModeType4` (alpha = vertex alpha + env alpha).

On the RDP, `CVG_X_ALPHA` multiplies the pixel coverage by that alpha; with no
`FORCE_BL` the inside of the triangle is written opaque. fast3d turned on
`use_alpha` for any `CVG_X_ALPHA` draw and alpha-blended it, so the low-alpha
part of the crate became translucent. Through it shows whatever is already in
the framebuffer, which at those pixels is the clear colour (blue in Dam). It is
not the floor behind it: the colour was the same with all non-object draws
skipped.

## Fix

`port/fast3d/gfx_pc.cpp` `gfx_tri_state`: `ts.blend = false` for
`CVG_X_ALPHA` without `FORCE_BL`. The texedge discard is unchanged. The mesh
cache takes `ts.blend`, so it follows. Surface 1 tree strip and Jungle
foliage/ferns (A2C off): no visible change, fix vs no fix within frame noise.

## Ruled out (measured, do not re-walk)

- Fog colour: the game sends a near-black fog register for the crate
  (`fogc=00000077`, no distance fog, `fogGetPropDistColor` returns 2), and
  fast3d receives the same value.
- Distance fog on props or rooms: removing it left the blue unchanged.
- LOD blend (`G_CC_TRILERP`, the inherited `LOD_FRACTION` hack): forced to 0, still blue.
- Smoke sprites, per-pixel fog (upstream has no per-pixel fog and has the bug).
- Vertex colours read wrong or 64-bit `Vertex` layout: the colours fast3d reads
  are the ones the game writes (greys, black, alpha 0 on purpose).

## Not port bugs (seen while chasing this)

- Dam tunnel truck looks blue: the game fades far props to the level fog
  colour (`fogGetPropDistColor`); ares shows it blue too.
- Thin blue line along an edge under the Dam tunnel: the maintainer saw it in
  the original game.
