# seaview: the GE+ sky and sea, outside the game

Tune the sky and the sea on the PC, then check the look and the GPU time on the console, without
playing the game each time. Both viewers draw with the game's own shaders (`port/switch/shaders/`)
from one captured frame of Frigate. Background: [docs/dev/shaders-sea.md](../../docs/dev/shaders-sea.md),
[shaders-sky.md](../../docs/dev/shaders-sky.md), [shaders-general.md](../../docs/dev/shaders-general.md).

## Setup

Copy `.env.example` to `.env` at the repo root and fill it in (console IP, emulator, Docker image,
a Windows g++). `.env` is ignored by git.

## Commands

```sh
python tools_pc/seaview/seaview.py dump        # capture Frigate in the Switch emulator -> data/sea_dump.bin
python tools_pc/seaview/seaview.py pc          # build and open the PC viewer (--hp: ask for the discrete GPU; more options below)
python tools_pc/seaview/seaview.py nx-send --ini tools_pc/seaview/data/seaview.ini   # console viewer, with those settings
python tools_pc/seaview/seaview.py nx-build    # rebuild the console viewer (after a shader change: nx-send --build)
python tools_pc/seaview/seaview.py ini2args F  # the console arguments an .ini turns into
```

The capture holds GE's cloud texture, which comes from the ROM: it stays in `data/`, which git ignores,
and so does the console NRO that embeds it.

## PC viewer

- Menus **Sea tests** / **Sky tests**: variants of the near sea and of the sky (viewer only: the shaders
  compile them under `#ifdef VIEWER`; the game never defines it). **Load**: the sky and sea passes drawn
  1-64 times per frame, so differences show on a fast GPU (the title gives the time per pass).
- **Settings** window: every value the shaders take (sun, sky, clouds, dome curvature, eye height, water,
  waves, Fresnel, bake distances; `0` for the bake start = no bake), with colour pickers, a reset per value,
  and save/load to an `.ini`. `data/seaview.ini` is read at start.
- View: click in it for mouse look (Escape releases), WASD or ZQSD, Shift = fast, Space/E up, Ctrl down.
- The GPU time of the PC says nothing about the console: compare variants with it, measure on the console.

## Water surface mode (PC viewer, issue #79)

Draws the level's own water (Dam, Caverns, Complex) the way the game does: a large horizontal plane under the sky, shaded by
`port/switch/shaders/water.frag.glsl` (the viewer has its own small vertex shader that provides the locations `water.frag` reads:
`vTexCoord0/1`, `vFog`, `vFogW`, `vInput1` = PRIM_LOD_FRAC, `vInput2` = SHADE, `vClip`). The sky and its reflection come from the same
passes as the Sea mode (the capture's sky, the sky LUT at binding 2); the Frigate sea is off. Block `Sky` is bound at 2 with the four
level-water vec4s (`uWX0..uWX2`: deep colour + patch size, light colour + reflectLit, map ready + water-only), `Draw` at 0, `Comb` at 1
(fog flag), the water texture at samplers 0/1, the wave texture at 3, the baked colour map at 4.

- Menu **Mode**: Sea (the Frigate view) / Water surface. Menu **Level**: Dam / Caverns (no sky: the fog colour is cleared as the
  horizon and mirrored, `uWX2.y`) / Complex. A level sets the preset (guesses until real captures exist: plane height, texture tile,
  patch size, reflectLit 0/0/1, fog, shade, tint, waves, eye height) and loads that level's texture. Menu **Water**: pick a texture file,
  reload and re-bake, screenshot.
- **Settings** window, tab **Water surface**: plane height, texture tile (world units per repeat), patch map size (repeats), reflectLit,
  PRIM_LOD_FRAC, 2nd texture scale, fog end (0 = no fog), water-only flag, and the colours deep / light / shade (the vertex colour) / fog.
  Tab **Sea** holds the tint (`Water colour`) and the waves (swell, chop, speed), tab **Fresnel / bake** the Fresnel terms. Everything is
  saved in the `.ini` (names as in the window) with `__mode`, `__level` and `__wtex` (the texture path).
- Water texture: `--wtex FILE`, the Water menu, `__wtex`, else `data/water_<level>.png` or `.tga` (`dam`, `caverns`, `complex`; RGBA,
  8-bit PNG without palette or interlace, or TGA 24/32 bit; the game's capture, 2..128 wide to match the game's limit), else a generated
  grey-blue noise placeholder. The viewer bakes it like `skyPlusWaterSource` in `port/src/skyplus.c`: deep / light = mean colour of the
  darker / lighter half, the patch map = the luma enlarged to 64x64 and blurred 3x(radius 6), stretched to 0..1. The bake is isolated in
  `common/waterbake.h` (keep it in step with the game's). Loading a texture writes the baked deep / light colours into the pickers.
- The plane is a 256x256 grid, 60000 units wide, denser near the eye (the fog is per vertex). The camera is the viewer's usual one:
  eye height is the "Eye height" value of the Sun tab.

### Half resolution and the test scene

Menu **Water** > "Half resolution" (`--half 0|1`, `.ini` `__half`) draws the water the way the Switch (deko3d) path does, with the game's
own shader files: (1) the main pass draws the water with `water.frag.glsl` + `#define WATER_BODY` (the cheap body colour), depth written;
(2) the same water draws are replayed into a half-size RGBA8 target (linear, clamp, cleared to 0, own half-size depth) with the full
`water.frag.glsl`; (3) they are replayed once more at full size with `watercomp.frag.glsl`, alpha-blended over the scene, depth test
LEQUAL against the scene depth with writes off. Off = one full-size `water.frag.glsl` pass. Menu **Debug view** (`--dbgview
final|fx|mask|cheap`): the final image, the half-size target as the composite reads it, a mask of step 3 (green = the half-size colour
is used, red = `watercomp` discards, the cheap body stays), or the cheap body only (step 1).

Menu **Water** > "Test scene" (`--scene 0|1`, default on): a pier slab half in the water, three boxes and a thin pole standing in it, a
wall the plane ends against, and a raised second pool with a rim, all drawn with depth, in a frame fixed to the capture's eye.
`--view overview|pier|boxes|shore|pool|far` puts the camera at a ready-made spot of that scene (relative to the scene, so the shots are
reproducible); `--cam` stays absolute. `--seahalf N` is the old `--half N` (the Sea mode's target width divisor).

### Screenshots, unattended

```sh
python tools_pc/seaview/seaview.py pc --shot out.png                                   # the Sea view (Frigate dump)
python tools_pc/seaview/seaview.py pc --shot out.png --mode water --level dam          # Water surface, Dam preset
python tools_pc/seaview/seaview.py pc --shot out.png --mode water --level caverns --cam 100,160,100,40,-8 --w 1280 --h 720
python tools_pc/seaview/seaview.py pc --shot out.png --mode water --level dam --wtex data/water_dam.tga --view pier --half 1   # the game's 3 steps
python tools_pc/seaview/seaview.py pc --shot out.png --mode water --level dam --view boxes --half 1 --dbgview mask       # where the composite takes the half-size colour
python tools_pc/seaview/seaview.py pc --mode water --level complex --wtex data/water_complex.png   # interactive
```

`--shot` renders one frame, writes it (`.png` or `.tga`, by the extension) and exits; `seaview.py` waits for it and uses a fixed time
(`--time 0`, override with `--time T`) and no sway so the images repeat. `--cam x,y,z,yaw,pitch`: eye position in world units, yaw and
pitch in degrees relative to the capture's view (works interactively too). Other options: `--w`, `--h`, `--yaw`, `--pitch`, `--time`,
`--mode 0..9` (the sky debug mode, as before) and `--mode sea|water`. In the viewer **F12** (or Water > Screenshot) writes
`shot_NNN.png` in `data/`, **P** writes `shot.tga`. Relative paths of `--shot` and `--wtex` are taken from where you run the command.
The `.ini` in `data/` is read at start; the command line wins over it.

## Console viewer

`nx-send` sends `nx/seaview-nx.nro` through nxlink (hbmenu, netloader: Y) with the `.ini` turned into
arguments. It draws the same passes as the game's sky draw (reflections, far-sea bake, the sea at half
width, the final pass) at 1920x1080 and prints the GPU time on the nxlink output every second.

Pad: left stick move, right stick look, ZL/ZR down/up, B (held) fast (700 units/s, 6000 held), D-pad up/down sea variant,
D-pad left/right sky variant (live build only), L/R load x1-x64, Minus render scale 1, 1/2, 1/4 (the sky and sea drawn smaller and enlarged; `scale=N` argument), Y sway, X freeze time, A re-bake, + quit.

## How the editable values reach the shaders

`PRM(i, default)` in `skycommon.glsl` is the constant `default` in the game and `uPrv[i]` (block `Viewer`,
binding 1) in the viewers. A value found good in the viewer becomes the new constant in the shader.
The settings table lives in `pc/seaview.cpp` (`kPar`); `seaview.py` reads it to translate an `.ini`.
