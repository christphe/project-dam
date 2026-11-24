# fxview: combat-effect sprites up close, original vs effect shader

A PC dev viewer for tuning the effect shader `fxShade` (the restyling of tracers, impact puffs, blood puffs and
bullet-hole decals that the game tags with `port/include/portfx.h`). It draws one sprite large, **ORIGINAL** (as the
game draws it) next to **EFFECT** (the same, then `fxShade`), and writes before/after PNGs. In-game captures are not
usable for this: the runs are not reproducible and the sprites are a few pixels.

- **Single source of truth.** `fxShade` and its helpers are read at run time from
  `port/switch/shaders/uber.frag.glsl`, between the lines `// FXSHADE BEGIN` and `// FXSHADE END`, and **hot-reloaded**
  when the file changes (polled every 0.3 s). Tuning = editing that block with the viewer open. A block that does not
  compile keeps the previous version on screen and shows the compiler log in the panel, with line numbers of
  `uber.frag.glsl` (a `#line` is inserted). The viewer provides `uniform int frame_count;` (the game's per-frame counter;
  it advances at 60 per second while "animate frame_count" is on, `--fc N` fixes it).
- **The sprites come from your own ROM at run time.** The image tables are read from the `Globalimagetable` segment
  (ROM address from `port/src/romassets_<r>.s`, table offsets from `src/game/image_bank.c`), the textures decoded with
  modelview's decoder (`tools_pc/modelview/pc/mv_tex.cpp`) plus `pc/fx_tex.cpp` for the codecs it lacks. Nothing
  ROM-derived is written into the repo: `data/` and `build/` are git-ignored and `shots` refuses an output dir inside
  the repo (outside `data/`).

## Setup

As modelview: a Windows g++ (MinGW-w64) in `FXVIEW_GXX`, `MODELVIEW_GXX` or `SEAVIEW_GXX` (`<repo>/.env` or the
environment; in a git worktree the main checkout's `.env` is used). The ROM (a big-endian `.z64`): `--rom FILE`, else
`FXVIEW_ROM`, else `MODELVIEW_ROM`, else the main checkout's `build-win/data/*.z64`. Builds go to `build/`, settings to
`data/fxview.ini`. Dependencies are the ones modelview already uses: Dear ImGui (`port/third_party/imgui`, in `NOTICE`)
and modelview's own `mv_tex`, `mv_zlib`, `mv_imgui` and `mv_png.h`, compiled from `tools_pc/modelview/pc`.

## Commands

```sh
python tools_pc/fxview/fxview.py pc                                   # build and open the viewer
python tools_pc/fxview/fxview.py pc --kind bloodpuff --age 4 --backdrop wall
python tools_pc/fxview/fxview.py pc --shot <dir>/out.png --kind tracer --view split
python tools_pc/fxview/fxview.py shots --out <dir> [--backdrop grey|dark|checker|wall] [--bgtex N] [--fc N]
python tools_pc/fxview/fxview.py pc --mode decal --mat wood          # the port's bullet holes on a lit wall (below)
python tools_pc/fxview/fxview.py decalshots --out <dir> [--holes N] [--yaw DEG] [--pitch DEG]
python tools_pc/fxview/fxview.py selftest                             # codecs, block extraction, puff frame steps; no ROM
```

`shots` writes one side-by-side PNG per effect (tracer, laser, watch laser; wall, blood and scatter puffs at several
ages; nine bullet-hole types) and `<dir>/index.html`. Viewer options (they win over the `.ini`; `--shot` ignores the
`.ini`): `--kind tracer|laser|watchlaser|wallpuff|bloodpuff|scatter|impact`, `--impact 0..19`, `--age TICKS`,
`--angle RAD`, `--view side|split|orig|fx`, `--split 0..1`, `--backdrop grey|dark|checker|wall`, `--bgtex N` (a ROM
texture number for the wall backdrop, default 20, NTSC numbering), `--zoom F`, `--fc N`, `--nearest 0|1`,
`--tracerlen F`, `--seed N` (decal vertex colours), `--shader FILE`, `--w --h`.

## Viewer

Panel on the left: the effect, its parameters (impact type; puff age with play / speed and the spark's random angle;
tracer length), the view (side by side, split with a slider, original only, effect only), zoom (also the mouse wheel),
nearest or bilinear texels, the backdrop (grey, dark, checker, or a decoded ROM texture tiled at 2 px per texel), the
fxShade file with its reload count and errors. F12 or the button saves `data/shot_NNN.png`; Esc quits (the settings
are saved to `data/fxview.ini`).

## What is drawn, per effect (from the game code)

| Effect | Game code | Image (NTSC texture numbers) | Vertex colour | PORTFX kind |
|---|---|---|---|---|
| Tracer | `gunfire.c` `sub_GAME_7F061E18` | `flareimage3` (2128, 16x32, RGBA32) | `D_80035C98` white, a 255 | TRACER |
| Laser / watch laser | same | `flareimage4` (2135); radius 50 / 10 | white; watch laser a 150..199 (175 shown) | TRACER |
| Wall puff | `glass2.c` `bullet_sparks_init` type 1 | `explosion_smokeimages` (2176..2181, 6 frames, 64x64 IA8), lifetime 11, frame = age x 0.5 | `g_BulletSparkColors[1]` FFFFC8 | SPARK |
| Blood puff | type 2 (default branch) | the same smoke frames | the port's `BLOOD_R/G/B` (read from `port/src/switch/nxgame.c`, 8C0A0A) | BLOOD |
| Scatter puff | type 3 | `scattered_explosions` (2182..2186, 5 frames), lifetime 9 | `g_BulletSparkColors[3]` white | SPARK |
| Bullet-hole decal | `explosion.c` (`impactimages[t]`, `g_ImpactTypes[t]`) | `impactimages` (20 entries, e.g. 2168, 2169, 2170, 206, 2171..2175, 1475..1479) | by `apptype`: light 216..255 / dark 0..39 / random primaries | IMPACT |

Geometry: the tracer is the game's quad (+-radius at the muzzle, +-0.9 radius at the far end, texture S across, T
along); the puff is the game's diamond (`unk1c/unk20` = cos/sin of the spark's random angle; the angle slider), with
the game's corner texture coordinates; the decal is a quad with the type's width/height. The views are fitted to
the pane (the relative size between effects is not kept); the zoom scales that.

## What is approximated

- **Combiner and blender.** The game sets `G_CC_MODULATEIA` / `MODULATEI` (TEXEL0 x SHADE, rgb and alpha) and a
  translucent render mode (`G_RM_ZB_CLD_SURF` for tracers and puffs, `G_RM_AA_ZB_XLU_DECAL` for decals). fxview does
  texel x vertex colour, then `fxShade` on the effect side, then a plain src-alpha-over blend. No RDP colour dither,
  no coverage / AA, no fog, no z-buffer (the decal is not depth-tested against a real wall).
- **Filtering.** Plain GL bilinear (or nearest), not the N64 3-point filter or the port's filter setting, and no
  mip-maps (the flares are mip-mapped in the game, level 6); the largest image of each texture is used. Wrap modes
  follow the table entry's flagsS/T (wrap / mirror / clamp).
- **Per-instance randomness.** Puff size and angle, the watch laser's alpha and its occasional red and green drop (1 frame in 5), the decal vertex
  colours (`randomGetNext`) are fixed or seeded here. The laser's second quad (`flareimage5`, the end flare) is not
  drawn. The tracer's length depends on the shot (`--tracerlen`).
- **Lighting and room tint.** None: the sprites are unlit in the game too, but the backdrop is not a lit room.
- **frame_count** advances at 60 per second here; in the game it counts rendered frames.

## Decal mode: the port's bullet holes (`fxDecalBlend`)

The level's bullet holes are drawn by `port/src/fxparticles.c` (`decalDraw`) and shaded by `fxDecalBlend` in
`uber.frag.glsl` (between `// FXDECAL BEGIN` and `// FXDECAL END`). The result is a factor for a 2x-multiply blend
over the wall. Decal mode draws this in 3D: a wall (the plane z = 0) with a real level texture, the game's hole
texture on it, lit by a movable sun and a movable point light (a muzzle flash). **GAME** on the left is the hole
as the game draws it: `impactimages[type]` times the vertex colour by `g_ImpactTypes[type].unk1`, alpha over.
**PORT** on the right is the same quad with `fxDecalBlend(8 + group)`, blended with src = DST_COLOR, dst = SRC_COLOR
(fast3d blend mode 3).

- **Single source.** The viewer compiles the FXSHADE block (`fxHash` and `fxNoise`), the uber shader's `dynLight`
  (it lights the wall, as under Video.Visuals = Enhanced) and the FXDECAL block, all read from the same file and
  hot-reloaded like `fxShade`. It provides what the block uses from the uber shader, with the same names: `vEye`
  (eye space, from a real perspective camera, so `dFdx`/`dFdy` give the wall's normal and the bump frame),
  `vTexCoord0`, `uTex0`, `texVal0`, `lcount`/`sun_dir`/`sun_col`/`lpos`/`lcol` (plain uniforms here, not a block),
  and `flags = 0` (no fog).
- **Materials** (`--mat`): stone, tile, default, wood, metal, metalobj, dirt, mud, snow, glass. Each one maps to the
  port's group (`decalKind`) and to the game's impact type (`src/game/tex.c` `*_impact_types`: stone, wood, snow and
  tile 1, default and metal 7, dirt and mud 2, glass 4/5/6). `--dtype N` overrides the type. Hole size =
  `g_ImpactTypes[type].width/height` (half extents, world units), times 0.85..1.15, with a random rotation as in
  `decalAdd`. `--holes 1..6` (overlapping allowed) and `--hseed N` (layout).
- **Wall.** A real level texture of that material's hit type. The hit type is the `hitTexture` field of
  `assets/images.def`, which also gives the texture number (NTSC numbering) and the name. Defaults: stone 286,
  tile 696, default 722, wood 1203, metal 399, metalobj 77, dirt 594, mud 775, snow 1157 (the only HIT_SNOW one),
  glass 809. `--walltex N` overrides it. The **texture sheet** view lists the material's textures; click one to put
  it on the wall. `--texel` sets the world units per texel (default 2), `--wallshade` the vertex colour (the room's
  baked light, default 0.6), `--walllit 0|1` turns dynLight on the wall on or off. The wall texture is mip-mapped.
- **Lights.** Sun: `--sun 0|1 --sunaz DEG --sunel DEG` (azimuth in the wall plane: 0 from the right, 90 from above;
  elevation off the wall). **The game sends no sun today** (`port/src/dynlights.c`: the GE+ sky's sun is no shader
  light), so `fxDecalBlend` uses its fallback, an eye-space light "from above" that turns with the camera. Sun off
  shows that case. Point light: `--pt 0|1 --ptx --pty` (on the wall) `--pth` (off the wall) `--ptr` (radius)
  `--ptcol r,g,b`. The defaults are dynlights.c's muzzle flash (2.2, 1.5, 0.7; radius 260; eye units = world units here).
- **Camera.** It orbits the middle hole: `--yaw --pitch` (degrees; 0/0 = straight at the wall), `--dzoom`. In the
  viewer, left-drag orbits, right-drag moves the point light on the wall, the wheel zooms.
- **Views.** Side by side, split, game, ours, texture sheet (`--view side|split|orig|fx|sheet`).
- `decalshots` writes `decal_<mat>_sun_flash.png` (sun from the upper left and a flash beside the holes) and
  `decal_<mat>_flash.png` (flash only, today's in-game case) for eight materials. It also writes a relief pair (stone,
  sun from the upper left and from the lower right, no flash), a grazing view, six overlapping holes, and
  `index.html`. Like `shots`, it refuses an output dir inside the repo.

Approximations in decal mode: the wall is one plane with a single texture and a uniform vertex shade. GE walls carry
per-vertex baked light and often a second (detail) texture. The textures use plain GL bilinear filtering (the port
may use 3-point filtering and clamps for `texVal0`), and there is no fog, no z-buffer and no N64 coverage. The game's
own hole on the left uses the port's decal geometry (rotation, scale), not explosion.c's. The point light's eye
position is the world position (renderScale 1). `sun_col` is a fixed warm white, because the game sets none.

## Tabs: muzzle flashes, tracers, impact bursts, explosions

A tab bar at the top of the panel switches between **Impacts** (decal mode, above), **Muzzle**, **Tracers**,
**Blood/smoke/debris**, **Explosions** and **Sprites** (the fxShade sprite view). On the command line:
`--mode decal|muzzle|tracer|burst|explosion|sprite`. Each new tab shows **GAME ORIGINAL (F9 off)** and **PORT FX
(F9 on)** side by side (or one alone: `--tshow game|port|side`) in 3D, in world units, with a lit wall behind
(`--twall 0|1`, `--twalltex N`). Replay, play, loop, a speed factor and a time slider; `--t SECONDS` fixes the time
(a shot at a given moment: the scene is re-simulated from 0 every frame, in 1/60 s ticks, so it is reproducible),
`--tspeed F`, `--tseed N`. Camera: left-drag orbits, the wheel zooms (`--tyaw --tpitch --tzoom`).

All draws go through one shader built like `uber.frag.glsl` `main`: the combiner (texture x shade, or the explosions'
TEXEL0 x TEXEL1 x SHADE), then the uber shader's `dynLight`, then `fxShade` with the PORTFX kind (both read from the
shader file and hot-reloaded). The blend follows fast3d: SPARK and TRACER additive (`gfx_pc.cpp` `use_modulate = 2`),
everything else alpha over.

| Tab | Game side | Port side |
|---|---|---|
| Muzzle (`--gun NAME --mrate --mvis --mscale`) | `model.c` GUNFIRE node: the quad from the node's Offset / Size, turned to the eye, random scale 0.75..1.25 and texture turn per frame, the node's texture (MODULATEIA, CLD_SURF). The node is read per weapon from the guards' held-weapon model files (`Pchr*Z`) in your ROM, through `scripts/filelist.<region>.csv`: 16 weapons | the same quad (fast3d gives MUZZLE no fxShade) plus `dynlights.c`'s muzzle light (2.2, 1.5, 0.7; radius 260) on the wall and the quad |
| Tracers (`--tritem gun|laser|watchlaser --trrange --trperiod`) | `gunfire.c`: CapBeamLengthAndDecideIfRendered (random start), gunAdvanceBeamTimer (+0.2 x range a frame), the flareimage3/4 quad of `sub_GAME_7F061E18` | `fxparticles.c` onShot: the streak from 30 ahead of the muzzle at 9000 / s, its tail clipped at the muzzle, the halo (x3.5, a / 4), the near fade; fxShade TRACER, additive |
| Blood/smoke/debris (`--burst metal|wood|glass|water|snow|dirt|stone|blood --bperiod`) | the `glass2.c` puff: the smoke frames over 11 ticks, size 26 (`bullet_spark_create(pos, 1, 26)`), FFFFC8 or the blood colour | `fxparticles.c` onImpact by material / onPuff (blood): the same spray and cloud numbers, cone directions, drag, gravity, streak / disc / solid shapes; fxShade SPARK / BLOOD, SPARK additive |
| Explosions (`--explo 0..20 --etail`) | `explosion.c` explosionTick (parts spawned every other tick, D486) and explosionRenderPart (pulled to the eye), the 15 display lists of `assets/oddtextures.c` (SMOKE_n x FIRE_n, tile 1 shifted by 2) | the same sprites plus `dynlights.c` addExplosions (rise, fade, size gain, radius = range x 4) |

Approximations: the muzzle's model scale (model units to world, default 0.1) is a guess; the 1P gun's flash (a
switch in the 1P gun model) is not drawn, only the guards' GUNFIRE node; the gun model itself is a dark bar.
The tracer's per-frame advance assumes one game frame per 1/60 s (the port). The blood spray goes along a shot from
the left. The smoke an explosion leaves (`explosionCreateSmoke`) and the scorch are not drawn. No fog, no z-buffer.

## Files

`fxview.py` (launcher, build, `shots`, `selftest`), `pc/fxview.cpp` (window, GL, ImGui panel, the ROM image tables,
sprite geometry, the fxShade / fxDecalBlend loader and hot reload, decal mode, shots), `pc/fx_tabs.inc` (included by
fxview.cpp: the muzzle, tracer, burst and explosion tabs), `pc/fx_tex.h/.cpp` (the texture codecs modelview lacks:
compmethods 0-4, 8, 9 and direct-colour zlib images, ported from `src/game/image.c`).
