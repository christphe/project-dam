# fxview: combat effects up close, original vs effect shader, through ge_render

A dev viewer for the port's effects (`port/shaders/fx/*.glsl`: tracers, impact puffs, blood puffs, bullet holes, and the bullet-hole
cluster; the muzzle flashes, tracer streaks, blood / smoke / debris bursts and explosions of `port/src/fxparticles.c`, `dynlights.c`). It draws **through `ge_render`**, the renderer library the game uses, so it shows what each backend really draws: Vulkan or
OpenGL on PC, deko3d in an NRO (Ryujinx or the console). Effects are drawn the way the game draws them: the game's combiner id, the
PORTFX kind in the id (`SHADER_OPT_FX`), the pack's effect shader, the `Lights` block (`set_lights`). **ORIGINAL** is the combiner without the kind
(what the game drew before the effect shaders), **EFFECT** the same draw with it.

It is a CMake target behind `GE_TOOLS` (OFF by default, `tools_pc/fxview/CMakeLists.txt`), built by Docker like the game. It replaces
the older Win32/WGL viewer (that one had its own GL copy of the shaders).

## Commands

```sh
python tools/dev.py tools fxview win [options]        # build (Docker, GE_TOOLS=ON), open on PC: Vulkan, or --backend gl;
                                                       # --lowcost: the Vulkan pack with the Switch's cheaper effects (FX_LOWCOST)
python tools/dev.py tools fxview ryujinx [options]    # build the NRO, copy it to Ryujinx's SD (switch/project-dam/fxview.nro), start Ryujinx
python tools/dev.py tools fxview nx [options]         # the NRO to the console over nxlink (the options are its argv)
python tools/dev.py tools fxview shots --out <dir> [--backend vk|gl] [--backdrop grey|dark|checker|wall] [--bgtex N] [--fc N]
python tools/dev.py tools fxview decalshots --out <dir> [--backend vk|gl] [--holes N] [--yaw DEG] [--pitch DEG]
python tools/dev.py tools fxview tabshots --out <dir> [--backend vk|gl]     # the four tabs at fixed times, and the decal texture sheets
python tools/dev.py tools fxview selftest             # PNG codec, puff frame stepping, the effect files; no ROM, no window
```

`shots` writes one side-by-side PNG per effect (tracer, laser, watch laser; wall, blood and scatter puffs at several ages; nine bullet-hole
types) and `<dir>/index.html`; `decalshots` the bullet-hole clusters per material on a real wall texture, game vs port; `tabshots` the muzzle flash,
the gun and laser tracers, the eight bursts, six explosion types (game left, port right) and four texture sheets. All refuse an `--out`
inside the repo (the images are ROM-derived), except `tools_pc/fxview/data/` (git-ignored). `--nobuild` reuses `build-win/fxview.exe`.

## Options (the same on PC and Switch)

On the Switch the options come from the nxlink argv, else from one line in `sdmc:/switch/project-dam/fxview/fxview.args`. Logs and shots go to
`sdmc:/switch/project-dam/fxview/` (the log also on nxlink stdout); on PC to `--out` (default `tools_pc/fxview/data/` from `win`).

| Mode | Options |
|---|---|
| `--mode sprite` (default) | `--kind tracer\|laser\|watchlaser\|wallpuff\|bloodpuff\|scatter\|impact`, `--impact 0..19`, `--age TICKS`, `--angle RAD`, `--tracerlen F`, `--view side\|split\|orig\|fx`, `--split 0..1`, `--backdrop grey\|dark\|checker\|wall`, `--bgtex N` (ROM texture, NTSC numbering), `--zoom F`, `--fc N` (frame_count), `--nearest 0\|1`, `--seed N` |
| `--mode decal` | `--mat stone\|tile\|default\|wood\|metal\|metalobj\|dirt\|mud\|snow\|glass`, `--dtype 0..19`, `--holes N`, `--yaw DEG`, `--pitch DEG`, `--dzoom F`, `--walltex N`, `--texel F`, `--wallshade F`, `--sun 0\|1 --sunaz --sunel`, `--pt 0\|1 --ptx --pty --pth --ptr`, `--view` as above, plus `--view sheet` (the texture sheet) with `--page N` |
| `--mode muzzle` | `--gun NAME` (a guard weapon with a GUNFIRE node: `autoshot`, ...; default the first), `--mrate F` (shots/s), `--mvis N` (ticks shown), `--mscale F` (model units to world) |
| `--mode tracer` | `--tritem gun\|laser\|watchlaser`, `--trrange F`, `--trperiod F` (s between shots) |
| `--mode burst` | `--burst metal\|wood\|glass\|water\|snow\|dirt\|stone\|blood`, `--bperiod F` (loop, s) |
| `--mode explosion` | `--explo 0..20` (g_ExplosionTypes), `--etail F` (pause after, s) |
| tabs (all four) | `--t SECONDS` (freezes the clock; a shot is reproducible at a given `--t` and `--tseed`), `--tspeed F`, `--tshow game\|port\|side`, `--tyaw DEG --tpitch DEG --tzoom F`, `--twall 0\|1`, `--twalltex N`, `--tseed N`, `--play 0\|1`, `--nearest 0\|1` |
| `--mode cluster` (#141 repro) | `--dist N`, `--holes N`, `--cmat 0..6`, `--holesize F`, `--seed N`, `--fov DEG`, `--sun 0\|1`, `--diag coverage\|veye\|uv\|pom\|pomalpha\|deriv`; `--bench 1 [--rep N]`: GPU time of `fxClusterPom` against coverage only, the cluster filling the window's height, in 120-frame phases (`BENCH` log lines) |
| `--mode sea` (also `water`, `sky`) | the GE+ sky, sea and level water (#145): `--level`, `--view`, `--cam`, `--rep`, `--scale`, ... — see `sea/README.md` |
| common | `--backend gl\|vk\|deko3d`, `--w N --h N`, `--frames N`, `--ui 0\|1`, `--out DIR`, `--rom FILE.z64`, `--shaders DIR`, `--shot FILE.png`, `--dump FILE.ppm`, `--probe X,Y` (repeat), `--probeframe N`, `--hold N`, `--selftest` |

`--probe X,Y` (window pixels, top-left origin) prints `PROBE x y r g b a`, the exact values the backend wrote (`ge_render_read_pixels`), at frame
12 (`--probeframe`) and quits (`--hold N` draws N more frames, for a window screenshot). `--shot` writes the window as a PNG at that frame.
deko3d sizes its window itself: 1280x720 when `--w <= 1280` (handheld), else 1920x1080 (docked); `--w/--h` do not resize it.

The viewer panel (ImGui, `ge_imgui`; pad navigation on the Switch) has the same settings; F12 / the button saves `shot_NNN.png`; Esc (Start on the Switch) quits.
Mouse in the scene (PC, decal mode and the tabs): left drag orbits, the wheel zooms; in decal mode middle drag pans and right drag moves the point light.
The panel is drawn with the Lights block cleared (the scene's lights do not wash it out), and the pane captions wrap to the pane.

### The tabs

Each of the four tabs (`--mode muzzle|tracer|burst|explosion`) shows **GAME** (what the game draws with the effects off, F9: its own sprites, no effect
kinds, no lights) and **PORT** (what the port draws instead, plus the Lights block), side by side or alone (`--tshow`). They draw through the draw API
with the game's combiner ids: `TEXEL0 x SHADE` for the sprites (MODULATEIA), `SHADE` for the untextured particles with the PORTFX kind in `id1`
(TRACER, SPARK, BLOOD; the blood keeps the game's `SHADER_OPT_ZCUT` bit so it hits the pack's combiner), and the explosions' two cycles
(`TEXEL0 x TEXEL1`, then `x SHADE`, TEXEL1 clamped: `0x010d010d0a080a08 / 0x00000c11`). SPARK and TRACER are blended additively (blend mode 2), the rest alpha over,
as fast3d sets them. The scene is a pure function of the tab's time (re-simulated from 0 each frame in 1/60 s ticks).

| Tab | GAME | PORT |
|---|---|---|
| Muzzle | the GUNFIRE node of the guard's weapon model (`Pchr*Z`, read from your ROM through the embedded file table): the quad from Offset / Size turned to the eye, random scale 0.75..1.25 and texture turn per frame, the node's texture | the same quad plus `dynlights.c`'s muzzle light (2.2, 1.5, 0.7; radius 260) on the wall |
| Tracers | `gunfire.c` beam (random start, +0.2 x range per frame) as the `flareimage3/4` quad | `fxparticles.c` streak from 30 ahead of the muzzle at 9000/s with its halo, kind TRACER, additive |
| Blood / smoke / debris | the `glass2.c` puff (smoke frames over 11 ticks) | `port/include/fxburst.h` (shared with the game): per material sprays, clouds, drops; kinds SPARK / BLOOD / none |
| Explosions | `explosion.c` parts, the 15 SMOKE x FIRE display lists, pulled to the eye | the same plus `dynlights.c` addExplosions (rise, fade, radius = range x 4) |

Not drawn: the 1P gun's flash, the smoke an explosion leaves (`explosionCreateSmoke`) and the scorch; no fog, no z-buffer.

### The decal texture sheet

`--mode decal --view sheet` (panel: view, "texture sheet"): the textures of the material's hit type (assets/images.def, embedded at build time), 8x5 per page (`--page N`),
each twice by two for the tiling, labelled with number and name. A click puts the texture on the wall and returns to the side-by-side view; with a pad,
use the panel's `wall texture` field (the sheet's page is the `sheet page` field).

### Settings (.ini)

Every setting the panel changes is saved to `<out>/fxview.ini` (on exit and with the "save settings" button; PC: `--out`, `win` uses `tools_pc/fxview/data/`;
Switch: `sdmc:/switch/project-dam/fxview/fxview.ini`) and loaded at start. Arguments win over the `.ini`; a run with `--shot`, `--probe` or `--dump` ignores it
and does not write it, so shots are reproducible.

## The ROM

Same lookup as the game, nothing ROM-derived is written anywhere: `--rom`, else `FXVIEW_ROM` / `N64_ROM` (environment; `python ... win` also reads
`<repo>/.env`), else the game's data folder (PC: `./data`, `../data`; Switch: `sdmc:/switch/project-dam/data`). The sprite tables (which textures each effect uses) and the guards' weapon
models are read from it; the textures themselves are not decoded from the ROM (see below). The table offsets are embedded at build time (CMake reads
`port/src/romassets_u.s`, `src/game/image_bank.c`), so the Switch build needs no repo files. Only the US ROM is supported.

## The textures

Through `ge_assets` (`port/assets/ge_tex.h`, the resolution the editor and the game use): the mod (`--mod PATH`) over the chosen texture pack (`--texpack NAME|PATH`)
over `ge007.passets`, by texture number. `ge007.passets` (the base asset pack, a tar or a folder; the older `ge007.ptexpack` still works) is `--pack` (the pack itself or a folder holding it), else `FXVIEW_PACK`, else `assetpacks/ge007.passets` beside the game (PC: `./assetpacks`,
`../assetpacks`, next to the ROM's `data/` folder; Switch: `sdmc:/switch/project-dam/assetpacks`; `fxview.py` also looks in the checkout's `build-win`, `build-linux`);
when none is found and a ROM is, it is written once into `--out` with the exporter (`ex_ensure_base_asset_pack`, the whole base pack). Every texture fxview shows is a texture-bank number, so all of
them come from the pack. The log says `texture pack: <root>, 2698 textures`.

## Hot reload of the effect shaders (PC)

`--shaders <repo>/port/shaders/fx` (`win` and the batch modes pass it) watches the four files; a change is compiled and swapped in without restarting.
- **GL**: the files go to `ge_render_fx_reload_gl` (GL has no effect shaders of its own; kinds 2..5: tracer, spark, blood, impact; the cluster draws plain on GL).
- **Vulkan**: dkshgen (compiled into the tool) generates the pack's GLSL, an external `glslangValidator` makes the SPIR-V (env `GLSLANG`, else PATH; the Docker image and MSYS2's
  `mingw-w64-x86_64-glslang` have it; without it the pack's shader stays and the panel says so), `ge_render_fx_replace_spirv` swaps the fragment shader.
- **deko3d**: the pack is built ahead (`ryujinx` / `nx` rebuild it when a file changed).
A file that does not compile keeps the previous shader; the log (file named `N(line)`: 0 common, 1 bullets, 2 bursts, 3 impacts) is in the panel and the console.

## Known limits

- **The GL backend has no `Lights` block** (`set_lights` is null; `vEye` and the lights are the Vulkan and deko3d shaders'): on GL the lit decal wall, the
  bullet-hole cluster (`fxClusterPom` needs `vEye`), the muzzle light and the explosion light draw plain. This is the game's GL path, not a viewer bug; compare
  lighting on Vulkan, deko3d or in Ryujinx.
- **#141** (`dFdy` was 0 on deko3d: devkitPro's uam never fills the y-sign constant it multiplies by) is fixed in the patched uam the build image carries (`docs/porting-notes.md` D79); `--diag deriv` reads the same on deko3d and Vulkan.
- Only the US ROM. The Muzzle tab needs the guards' weapon models from the ROM; without a ROM it says so and draws nothing.

## Diagnostics: tool-only effect kinds

PORTFX kinds 8..13 (`tools_pc/fxview/combiners.txt`, `shaders/diag.glsl`) are the cluster's combiner with a diagnostic output instead of the colour (hole coverage, `vEye`, uv,
the shader's colour with alpha 1, its alpha, the derivatives). They exist only in GE_TOOLS packs. Used for #141: see `docs/porting-notes.md` D73.
`--mode cluster --diag deriv` shows the derivatives the parallax steps start from (r = |dFdx(vEye)|, g = |dFdy(vEye)|, b = |dFdy(uv)|: g and b were 0 on deko3d before the #141 fix), `veye` the eye position
the Lights block rebuilds (r near 0 = no inverse projection), `coverage` the hole mask, `uv`, `pom` / `pomalpha` the parallax colour and its alpha. Read a value with `--probe X,Y`.

## Files

`fxview.py` (launcher: Docker builds, `shots`, `decalshots`, `selftest`), `ge/fxview.cpp` (the viewer), `ge/fx_rom.*` (ROM and image tables), `ge/fx_tex.*` (textures through ge_assets),
`ge/fx_tabs.inc` (the four tabs), `ge/fx_reload.*` (PC hot reload), `combiners.txt` + `shaders/diag.glsl` (tool-only combiners), `CMakeLists.txt`.
