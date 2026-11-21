# The GE+ sea (water levels: Frigate, and the sea of Dam / Caverns / Complex)

Issue #38. Read [shaders-general.md](shaders-general.md) first. Code: `port/switch/shaders/skycommon.glsl` (`waterNormal`, `waterShade`, `waveLayers`, `texWaves`), `skyplus.frag.glsl` (the passes), `port/fast3d/gfx_deko3d.cpp` (the sky pass driver), `port/src/skyplus.c` (the wave texture, per-level settings).

## What it is

A procedural sea drawn in the sky pass, under the horizon, as a plane at the level's water height `uSeaY`. Original mode keeps GE's own sky. Per-level look (tint, swell, chop, speed) comes from `skyplus.c` (`$S/water/<level>.ini` is the intended override).

The look target: realistic but stylised, a fine busy grain that tightens toward the horizon and **never turns into a mirror lake**; Frigate in blue hour (no warm tint); Dam calmer.

## Pieces

- **Wave texture**: 128x128 RGBA8, tiling fbm. R = height, G/B = slope (decoded as `(v*255 - 127.5) / (6*255)`, `WTEX_SLOPE_K = 6`). One uv unit = 128 texels. Mips, **anisotropic 2**.
- **`waveLayers` / `texWaves`**: sum of rotated, scaled, drifting reads of that texture (a medium swell, then layers 2-4 at decreasing scale). Layer 2 is cut with distance (`LAYER2_CUT`); the medium swell exists near only.
- **`waterNormal(p, dist)`**: a slope from the layers, scaled by calm/rough patches (`vnoise` of the position, drifting slowly with `uTime`), then flattened to 35% at long range (specular anti-aliasing). `gSeaMode` selects the wave set: 0 both sets crossfaded by distance (the level's own water draws, Dam), 1 the moving set only (the live near sea), 2 the still set only (the bake).
- **`waterShade`**: body colour under a Fresnel reflection of the sky (the 128x64 panorama `uSkyLut`, see the sky note), plus sun glints (none in blue hour). `gLite` swaps the sky reflection for the horizon colour.
- **Horizon**: `mix(horizonColor, sea, smoothstep(0, 0.006, -nd.y))`: sea and sky meet in the sky's own horizon colour.

## Passes (`uPass`, `uC0_.w`)

- 0: one pass (no reduced target).
- 1: the sea into a **half-width target of exactly the viewport's size** (above-horizon pixels write `horizonColor` so the bilinear read at the horizon does not blend in black).
- 2: the full-resolution pass reads that target below the horizon (`uHalfSea`, binding 3) and draws the sky above it.
- 4: **bake** the far sea into `uSeaPano` (binding 4).
- 5: enlarge the half-size final image onto the screen (half scale, below).

**Half scale** (deko3d, `SKY_HALF_SCALE`): the sea target is a quarter of the viewport's width and half its height, and the final pass (pass 2) is drawn into a half-size target (`s_skyr_fb`); pass 5 enlarges it onto the screen. Halving only the final pass gained ~1 ms on the console (12.4 -> 11.4 ms): the waves in the sea target are the heavy part, so the sea target has to follow the scale too. Both together: 12.4 -> **6.2 ms** median per frame (console, Frigate, handheld).

## The level's own water surfaces (Dam, Caverns, Complex; #79)

Only Frigate has a sea plane. On Dam, Caverns and Complex the water is level geometry drawn with GE's water combiner (`unk_092E50.c`, `lerp(TEXEL0, TEXEL1, PRIM_LOD_FRAC) * SHADE`); fast3d tags it `SHADER_OPT_WATER` and deko3d draws it with `water.frag.glsl`, which restyles it only while `uWave4.z > 0.5` (set by the GE+ sky pass, cleared each frame by `dk_start_frame`): Visuals = Original and N64 display mode keep GE's combiner exactly.

- **Body colour from the level's own texture.** The first water draw of a level hands its decoded TEXEL0 to `skyPlusWaterSource` (`gfx_pc.cpp`, forced even on a texture-cache hit). `skyplus.c` enlarges it to 64x64, blurs it heavily (3 box passes of radius 6, wrapping) and stretches min..max to 0..1: a tiling **patch map** (binding 4, R = 0 deep .. 1 light). The shader reads it with the draw's own texcoord divided by `PatchScale` (large soft patches) and blends **deep** into **light** by it, times the level's tint. Default deep / light = the mean colour of the texture's darker / lighter half (by luminance, split at the median). Until the bake is ready (first frame) or if no texture was captured, the body is GE's texture x shade as before.
- **The body keeps the original's tone by design**: `body = clamp(mix(deep, light, patch) * SHADE) * tint`, the patch colour standing in for GE's `lerp(TEXEL0, TEXEL1, LOD)` in `texture x SHADE` (same order, no extra factor). Deep / light being the means of the texture's darker / lighter halves, the average tone matches the original's, only redistributed into large soft patches.
- **The sky enters only through the reflection** (`waterShade`'s Fresnel), not the body. **`ToneKeep`** (0..1) pulls the reflection into the body's hue (its brightness kept: `body * lum(w) / lum(body)`), so the sky's blue does not repaint the water; **`ReflectGain`** scales the reflection's weight. Measured on the PC at Dam's intro camera 1 (mean water colour): GE 12,25,29; the reflection as is 18,38,58 (bright blue); ToneKeep 0.8 + ReflectGain 0.6 gives 15,32,39 (the original's teal, a little brighter).
- **`reflectLit`** (0..1): how much the vertex lighting dims the reflection's weight (not the body: it always carries SHADE). 0 = full reflection (Dam, Caverns: their water is lit dark), 1 = the old rule `lit = max(shade) * 1.4` (Complex: keeps its pits black; also the default for unknown levels).
- **Caverns (no sky).** With GE+ active and a level without clouds that is in `waterLevelName`, `skyPlusRender` emits the sky command with `waterOnly = 1`: the pass only fills the water uniforms (time, waves, colours, `uWave4.z = 1`) and draws nothing; `skyplus.c` then does the same fill GE's `skyRender` does for a level without clouds. The water mirrors the level's fog colour (`gLite`, the horizon colour = the record's colour) and has no sun glints (`gGlint = 0`). Its texture (TEXEL0, a 64x64 I4) alone is about half the tone of GE's lerp with TEXEL1, so its tint is 2 (measured at intro camera 1: 10,14,14 vs GE's 10,12.5,12.6), with ToneKeep 0.8 and ReflectGain 0.5 (the reflected fog colour is dark).
- **World position.** `p = uEye + d * w * uWX3.z`: the draws' clip `w` comes out in the level's scaled units, the eye and the corner rays in the player's, so `uWX3.z` = bg.c `room_data_float2` (1 / the level's scale). Without it the waves and the reflection slid with the camera when strafing (Dam: the pattern moved ~0.22x as far as the scenery). Debug: a world-space checker (`mod(floor(p.x / 200) + floor(p.z / 200), 2)`) must stay put on the water while strafing.
- **Uniforms.** The Sky block of `water.frag` carries four more vec4 after the sky block's 16 (`uWX0` deep + patch size, `uWX1` light + reflectLit, `uWX2` ready / waterOnly flags, `uWX3` toneKeep / reflectGain / 1 / level scale), pushed at `WATER_UBO_OFS + 256`; the UBO is bound with 512 bytes.
- **Log line** (once per level, `LOG_NOTE`): `GE+ water: <level> source texture WxH fmt F siz S; computed deep ... light ...; used deep ... light ... tint ... reflectLit ... patchScale ...`.

**Half resolution on the Switch (deko3d only, #79).** Measured on the console at Dam with the lake filling the screen: 19.4 ms GPU per frame (51 fps), against ~7-8 ms without the water: `water.frag` (waterNormal = up to 2 wave sets x 4-5 layers, waterShade) ran per pixel at full size. Frigate's sea costs ~6 ms because the sky pass computes it small. The same idea here, for the level's own water draws (`gfx_deko3d.cpp`, `wfx_*`):
- **Main pass**: with the GE+ water on (`s_wfx_sky_on`: this frame's sky or water-only pass has set `uWave4.z`), each water draw is drawn with the **body variant** of `water.frag` (pack entry `DK_PACK_WATERBODY`, the same file compiled with `#define WATER_BODY`, added after the `#version` line by `cmake/SwitchPort.cmake`: no waves, no reflection; the patch map x shade x tint, GE's colour until the map is ready; fog as before; ~3 KB of code against 24 KB). It also writes the depth, as the water always did. The draw is **recorded**: its vertices (copied: the data ring may be recycled before the replay), `Draw` block, TEXEL0/1 handles, viewport, scissor, depth range and state.
- **Replay** (`wfx_flush`), when something that must not be under the water comes (a blended draw or one without a depth test, a change of render target, a clear, a copy / resolve, the FXAA pass, the next player's sky pass, the end of the frame; at most 6 times a frame, then the draws take the full path again): (1) every recorded draw again into a **half-size target** (each axis; `s_wfx_hfb`, colour + its own depth buffer, cleared to (0,0,0,0) / 1.0) with the **full** `water.frag` (waves, reflection, fog; coverage = alpha 1). It is depth-tested only against the other water draws in that target, so a water surface keeps its colour behind an object that hides it. (2) The same draws again at **full size** over the body colour with `watercomp.frag.glsl` (`DK_PACK_WATERCOMP`): colour = the half-size target at the pixel's screen position (from the interpolated clip position, `1 - f.y` like pass 5 above), un-premultiplied by its coverage, blended by the coverage (a silhouette mixes toward the body, never to a dark halo); **depth test LEQUAL against the scene's depth buffer, depth writes off**.
- **Edge-awareness by the depth test** instead of sampling the depth buffer (the lead's first sketch: compare the scene's depth with a depth key per pixel): the replay's vertices and viewport are those of the main draw, so a pixel passes exactly where that water is the front-most surface. Nothing drawn in front of the water is overwritten (a pier, a boat, the player's hands), and no tolerance, no linearising of a Z24 buffer, no depth texture (a compressed Z24S8 sampled as a texture is untested here).
- **Cost**: the waves run on a quarter of the pixels; the full-size part is the body (a patch map read and the combiner, as before the GE+ water) plus one bilinear read per water pixel.
- **Falls back** to today's full `water.frag` in the main pass (log `DK: GE+ water half resolution: off (...)`) when the pack lacks the variants, `gfx_framebuffers_enabled` is off, the half target or its descriptor cannot be made, or `GE_WATERHALF=0`. Log line when it works: `DK: GE+ water half resolution: on (target WxH for a WxH view)`, once. Original visuals and N64 display mode never record (`uWave4.z = 0`): unchanged. The PC backends (Vulkan, GL) are unchanged: they draw the full `water.frag`.
- **Known limits**: a translucent draw that comes before the flush trigger is overwritten where the water is the front surface (the flush waits for the first blended / depth-less draw, so smoke and glass drawn after are fine); the half-size waves are softer (mips pick from a doubled footprint); a water draw of a frame past the caps (128 draws, 1 MB of vertices, 6 flushes) takes the full path.
- **Measured (console, Dam, handheld 1280x720, target 640x360): 59.1-59.8 fps throughout, GPU 7.1-11.6 ms per frame (19.4 ms and 51 fps facing the lake at full resolution)** (PERFSTAT gpu= at Dam facing the lake, before 19.4 ms; Caverns, Complex) and a look on the console at the pier, the shores and the boats (no halo). Ryubing draws the half-size sky image wrong (open items below): judge the composite's orientation on the console.

**PC backends (Vulkan, desktop GL 4.6).** The same `water.frag.glsl` draws these surfaces; each backend sets `gfx_water_surfaces` (log line `GE+ water surfaces: on (vulkan)` / `on (opengl)`), which turns on the `SHADER_OPT_WATER` tag in fast3d, the texture capture and `waterOnly` in `skyplus.c`. Original mode and backends without the water (GL < 4.6, ES) keep GE's combiner.
- **Vulkan**: `water.frag` is in the SPIR-V pack as `DK_PACK_WATER` (blocks moved up by 4 like the other draws'; its colour-map sampler moved to binding 7 and its Sky block to 8 by `cmake/VulkanShaders.cmake`), paired with the uber vertex shader (every output it reads exists there). Its own descriptor set layout (`s_water_set_layout`): samplers 0..3 and 7, Draw 4, Comb 5, Sky 8. The Sky block (20 vec4) is a ring allocation made by each sky (or water-only) pass; until one ran this frame the water draws read zeros (`uWave4.z = 0` = GE's combiner).
- **GL**: the generated vertex shader is kept (made `#version 460`, plus `out vec3 vClip = (clip x, y, w)`), the fragment shader is `water.frag.glsl` under `#define GE_GL` with the program's inputs defined (`WATER_FOG`, `WATER_PIXEL_FOG`, `WATER_TPF`, `WATER_IVEC`, `WATER_SHADEQ`, `WATER_FOGQ`; the Draw / Comb blocks become constants). The Sky block is a uniform buffer at binding 2 (20 vec4, zeroed in `uWave4` at frame start), the cloud / wave / colour-map textures sit on units 2, 3, 4 (the sky pass uses 3..7 inside and puts back what it found). A water program that fails to build falls back to the generated shader (GE's own water) with the error in the log.

`$S/water/<level>.ini` (read on a level change, keys not case-sensitive), besides `Tint`, `Swell`, `Chop`, `Speed`:

| Key | Meaning | Default |
|---|---|---|
| `Deep = r, g, b` | the deep colour, 0..1 | computed from the texture |
| `Light = r, g, b` | the light colour, 0..1 | computed from the texture |
| `ReflectLit = x` | 0..1, see above | Dam 0, Caverns 0, others 1 (Complex 1) |
| `PatchScale = x` | the patch map's size in water-texture repeats (0.5..64) | 6 |
| `ToneKeep = x` | 0..1: the reflection pulled into the body's hue | Dam 0.8, Caverns 0.8, others 0 |
| `ReflectGain = x` | 0..2: the reflection's weight, x the Fresnel | Dam 0.6, Caverns 0.5, others 1 |

Tuning: `GE_SEADUMP=1` also writes the level's source water texture to `data/water_src.bin` (int w, int h, RGBA8), to convert into `tools_pc/seaview/data/water_<level>.tga` for the viewer's Water surface mode (README there). The PC intro cameras (`GE_INTROCAM=n`) give fixed views of the water for before/after captures (Dam: 1 = the pier, 0 = the lake from the dam; Caverns: 1, 2).

## The far sea (the part that took a week)

Observations that settled the design:

- Seen from far away, the sea reads as a still, fine grain; breathing/camera sway of a few pixels must not make it swim; it must lie on the sea plane (horizontal), with texels that tighten toward the horizon, like a photo.
- **The right far look is just the true plane, filtered.** The texture is busy enough that mips plus anisotropic 2 give streaks that tighten and lose contrast with distance. That is what the reference photo shows.
- It must be **static in time** beyond a distance (a wave's drift there is a pixel of motion; big moving layers made the whole background pulse), and the line where the moving set gives way to the still one must not be a circle around the player.

Current design:

1. **Near sea, live** (`gSeaMode = 1`): moving waves, per pixel. Blend weight `kf = smoothstep(NEAR_Z0, NEAR_Z1, zd)` = 600..6000, with `zd` the depth of the sea point **along the camera's view axis** (not the distance to the eye: distance draws a circle on the sea, depth draws a straight line).
2. **Far sea, baked** (`gSeaMode = 2`, pass 4): a panorama `uSeaPano`, 4096 x 512 RGBA8. `u = azimuth / 2pi + 0.5`; `v = sqrt(elevation sine) / panoMax`, `panoMax = 0.8` (finer rows near the horizon; the baked sea never needs more than ~0.64). The bake evaluates the still waves + a horizon-colour reflection (`gLite`, **no sky reflection out there**) with analytic footprints (`gDpdx` along azimuth, `gDpdy` along the range), clamped to 3000 units so the mip reads stay finite at the horizon. The final pass reads it with one `textureLod` per pixel, **through the baked eye**: the sea point under the pixel is looked up as the baked eye (`uBakeX/Z/H`, the free `w` of the corner rays) saw it. That is exact on a plane, so a strafe slides the far sea with the world like the live sea, and the blend shows no shear.
3. **When it is baked** (`gfx_deko3d.cpp`, `skyplus.c`): on the level's first frame, before that frame's sky, from the player's start eye (`bakeEye` = `g_CurrentPlayer->field_488.pos`, set before the intro: not the intro's flying camera), so no image is drawn without it; again on the next frame, because **the first-frame bake comes out black** (cause not found; its inputs are valid from frame 1) and the sea stays live until the second one is done; again only when the sun or moon direction moves more than 3 degrees or the level's sky or water colour changes (at most once per 60 frames). Entering the intro (a new level, a restart: `bakeReset`) redoes it. **Never for a move**: the reprojection keeps the far sea right wherever the player goes.
4. Because the far sea is a lookup, it is **still by construction**: no flicker, nothing to accumulate.

Cost: one bake is ~21 ms on the console (a bake forced every frame took the frame from 13.5 to 35 ms), twice per level start. Per frame the far sea is one texture read.

Coverage limit: the panorama goes down to sin(elevation) 0.64 from the bake eye, so sea closer than ~800 units under the bake point reads its last row. Not seen in play; `panoMax = 1.0` would cover it at the cost of coarser rows near the horizon.

## Things tried and rejected (do not redo)

| Idea | Why it failed |
|---|---|
| Replace the far layers with a static noise texture mapped by **view direction** (azimuth/elevation), flat or half-perspective | Reads as a vertical wall; tile repeats show as columns and grids; does not lie on the plane. |
| Continue the plane's mapping **by its tangent** at constant texel size | Squares curve into arcs (distance is radial), and at constant size the sea looks like a ramp rising toward the horizon ("la mer remonte"). Damping the continuation only trades it for vertical streaks. |
| Cheapest "reduced" far sea by dropping layers one by one | Each cut moved the problem (mud bubbles, standing-wave oscillation). |
| Quarter-width sea | Same cost as half. |
| **TAA** (full screen, then on the half-width sea target only, jitter + reprojected history + neighbourhood clip) | In the viewer it looked good and cost +0.1 ms on the sea target; in the game it came out flat and uniform (unresolved: new UBO binding, see general note), and the metric showed no flicker to fix to begin with. Replaced by the bake. Removed. |
| Frame interpolation / reprojection of the sea | The far sea costs nothing once baked; the near sea moves, so reuse leaves trails; the worst frame is the same. |
| Sky reflection in the far sea | Not needed; removing it makes the bake independent of the sky and of cloud drift. |

## Debug

- `GE_TMPDBG` (temporary, remove before commit) selects debug outputs in `skyplus.frag.glsl` through `uWave4.w`. Checkerboard mode 8 on the true plane (raw left, footprint-filtered right) is the quickest way to judge geometry and filtering.
- A black far sea in the game = the panorama read returns 0: not baked, baked for another eye height, or NaN. The tests that found it: output the read UV, output NaN/inf/black as colours.

## Open items

- Ryubing shows the half-scale image wrong (an enlarged sea; a test gradient drawn into the half-size target read back as one flat colour) where the console is right: not explained, judge half scale on the console.
- The first-frame black bake's cause.
- Looks owed: docked, co-op (two viewports share one panorama), Dam (legacy `gSeaMode 0`; the new body colour above, to judge), Caverns (water-only, to judge), Complex (`reflectLit 1`, pits must stay black).
- Judge this sea on Ryubing or the console only: the old Ryujinx (r.49574a9) draws bright streaks, arcs and blocks in the far sea that neither shows.
- The `$S/water/<level>.ini` override: document it in `docs/` when final.
