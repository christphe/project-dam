# fast3d — software RSP

A C++ interpreter that consumes the `Gfx` display list the game builds each
frame and emits draw calls for the active `ge_render` backend (OpenGL, Vulkan, deko3d), replacing the Nintendo 64's Reality Signal
Processor and Reality Display Processor.

## Origin and license

This code is **adapted from the [Perfect Dark PC
port](https://github.com/fgsfdsfgs/perfect_dark)** (`port/fast3d/`), which in
turn is the [Ship of Harkinian](https://github.com/HarbourMasters) /
libultraship fast3d, itself descended from
[sm64-port](https://github.com/sm64-port/sm64-port).

`LICENSE.txt` in this directory (MIT, © 2020 Emill, MaikelChan) covers that
lineage (the backends moved to `port/ge_render/`, which carries a copy). GoldenEye-specific changes in this directory are
under the repository's top-level MIT license.

## Files

The renderer (backends, shader generator, window) is the `ge_render` library, `port/ge_render/` (issue #142). fast3d is only
the N64 -> renderer translation.

| File | Role |
|---|---|
| `gfx_pc.cpp` | the RSP interpreter: matrices, lights, fog, `G_TRI1`/`G_TRI4`, vtx; talks to `ge_render` |
| `gfx_frame.cpp` | the frame recorder between the decoder and the replay (render worker, replay thread) |
| `gfx_meshcache.inc` | the display-list mesh cache (validates against RDP state; the GPU storage is `ge_render`'s mesh API) |
| `gfx_glprobe.cpp` | the game's first-launch GPU benchmark, GL half (raw GL on the GL backend's context) |
| `gfx_api.h` | the translator's API (`gfx_init` / `gfx_run` / `gfx_start_frame` / ...) |
| `gbiex.h` | GoldenEye custom GBI command definitions |

## GoldenEye-specific work

The PD fast3d already handles `G_TRI4` (GoldenEye uses it too). The GE additions
are the custom color-combiner / render modes and the handling of GE's GBI
extension commands. `rsp/graphics/gmain.s` (GE's real RSP microcode, in the
repo root) is the ground truth for the exact command encodings.
