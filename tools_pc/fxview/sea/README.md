# The sea and sky tab of fxview

Issue #145 (phase 3 of #142). It replaced `tools_pc/seaview/` (a Win32 + OpenGL viewer and a deko3d console viewer, each with its own
copy of the plumbing): the GE+ sky and sea now go through `ge_render`'s own sky and water passes, so the tab runs the game's backend
code on Vulkan, GL and deko3d. Background: [shaders-sea.md](../../../docs/dev/shaders-sea.md), [shaders-sky.md](../../../docs/dev/shaders-sky.md),
[shaders-general.md](../../../docs/dev/shaders-general.md).

## What is here

| File | Role |
|---|---|
| `sea_tab.h`, `sea_tab.cpp` | the mode, compiled into fxview: `seaTabSetting` (fxview.ini's `sea.*` lines, then the arguments), `seaTabInit`, `seaTabFrame`, `seaTabUi` (ImGui widgets in fxview's panel), camera helpers, `seaTabWriteSettings` |
| `sea_pack.cmake`, `sea_pack.S` | the tool pack: the `#ifdef VIEWER` variants of `sky/sky`, `sky/lut` and `water/water` (SPIR-V for Vulkan, DKSH for deko3d) |
| `sea_env.cmake`, `sea_env.c`, `sea_env.h` | the game's environment records, cut out of `bgfog.c` at build time |
| `img.h`, `waterbake.h`, `clouds.h` | image I/O, the level-water colour bake (keep it in step with `skyPlusWaterSource` in `port/src/skyplus.c`), the generated fallback clouds |

`ge_render` gained two tool-only pieces (`port/ge_render/include/ge_render_tool.h`, never called by the game): the GPU timer
(`ge_render_gpu_timer_begin/end/ms`, any tab can time a stretch of the frame) and the viewer variants
(`ge_render_viewer_enable/set/disable`: a pack of the game's own format, the `Viewer` block of the variants at binding 8).
The game's shader packs are untouched: the variants are built by `sea_pack.cmake` into a pack of the tool.

## Inputs

No capture: the tab builds every input as the game does, from the user's ROM and the game's own code and data:

- the level's environment record: `src/game/bgfog.c`'s `fog_tables` / `fog_tables2`, cut out at build time (`sea_env.cmake`) and compiled with the game's headers (`sea_env.c`); every level with a record, `--level <name or stage number>`;
- the sky and water blocks: `port/src/skyplus_core.c` (the pure part of `skyplus.c`, compiled into the game and the tool);
- the cloud texture and the water texture: from the ROM fxview opened (`ge/fx_rom`: its lookup, its image list embedded at build time, the shared `mv_tex` decoder). The water texture is `skywaterimages[env->WaterImageId]` of the game's global image table (`src/game/sky.c`; `fx_rom` reads that table); a record with `WaterImageId` 0 (Dam, Complex) has its water draws textured by the level's own room data, which a tool without the level loader cannot reach: the first water image stands in (logged). `--wtex FILE` overrides it. Without a ROM: generated textures, logged;
- the wave fbm (the game's), the water settings (`<data dir>/water/<level>.ini`: the ROM's directory, or `--data DIR`), a free camera.

## Running it

fxview's `--mode sea` (also `water` or `sky`; "sky / sea" in the panel's mode list) hosts the tab: the panel shows its settings, the
scene its view. Same commands as fxview (`tools_pc/fxview/README.md`):

```sh
python tools/dev.py tools fxview win --mode sea --level frigate --view pier
python tools/dev.py tools fxview win --mode water --level dam --scene 1 --view pool --rep 8
python tools/dev.py tools fxview ryujinx --mode sky --level surface
python tools/dev.py tools fxview nx --mode sea --level frigate --rep 64      # GPU times: the console only
```

Arguments (fxview's `--rom` and the rest apply too): `--data DIR`, `--mode sea|water|sky`, `--level <name or stage number>` (names: frigate, dam, caverns, complex, surface, streets, runway, temple, cradle; others stage<N>), `--sea N`, `--sky N`,
`--tex N`, `--wtex FILE`, `--scene 0|1`, `--cam x,y,z,yaw,pitch`, `--view overview|pier|boxes|shore|pool|far`, `--time T`, `--sway 0|1`,
`--rep N` (load mode), `--scale 0|1|2`, `--set "Name=value"` (any value of the table by its name). The settings are saved in `fxview.ini`
as `sea.<key>` lines (only the values that differ from the level's); the arguments win over them. `--view`, `--scene` and `--mode`
are fxview's too: the mode takes them when the value is its own (a view name, 0/1, sea/water/sky).

PC: WASD, Shift fast, Space or E up, Ctrl or Q down, right mouse button to look, R / F load x2 / x1/2, M scale.
Switch (deko3d): the ROM and `water/*.ini` are the game's, in `sdmc:/switch/project-dam/data` (nothing else to copy); left stick moves, right stick looks, B fast, L / R load /2 x2, Minus scale, Y sway, X freeze, + quits.
The status line (mode, variants, GPU time) is at the top of the panel and in `fxview.log` once a second (over nxlink too). `--scale` also sets the divisor of deko3d's reduced sky passes
(`ge_render_tool_sky_div`; 0 = full size is the default here: Ryubing draws the game's half-size composite wrong, `docs/dev/shaders-sea.md`).
Ryubing reports GPU times of 0 (no timestamps): read them on the console.

## Changes from seaview

- The variants and the `PRM` values need the tool pack (Vulkan, deko3d) or GL's run-time compile; without it the tab shows the game's shaders.
- No `VIEWER_CONST` mode (the console NRO that compiled an `.ini`'s values in): the game's own shaders (viewer off) are the baked defaults.
- No TAA, no debug views of the half-resolution water, no `--half`: the backend draws the water the way the game does (deko3d keeps its
  half-size water passes). The sky debug `--mode 0..9` is gone with `uWave4.w`, which the backends use.
- The camera and the sea's far bake are those of the backend's own pass (`PortSkyPlus`, `bakeReset` on a value change).
