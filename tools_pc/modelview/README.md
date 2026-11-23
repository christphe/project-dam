# modelview: compare mesh subdivision methods on a model

A PC dev viewer to judge how a subdivision method treats a character: original and subdivided next to each
other, with the edges coloured by why they stayed straight. It loads the character files (`C*`: bodies and
heads) from **your own ROM at run time**, with the converter's own code in `port/convert` (file table, rz
decompression, the mesh extraction `cv_sd_model_mesh`). The models, shots and any decoded data come from your
machine and never enter the repo: `data/` and `build/` are git-ignored, `shots` refuses an output dir inside
the repo (outside `data/`). A procedural test mannequin (original, not a game character) is always available.

Methods: **GAME (PN130, heads only)** (the default; the converter's own pass: only triangles whose three corners are head are subdivided, PN with a 130 degree crease limit, body triangles and neck edges stay whole; head = every vertex of a `Chead*` file, or in a body with an integrated head the matrices of the NECK joint group and below, mirrored from `conv_subdiv.c`; View > Highlight head region tints them, neck edges draw white), **FLAT** (midpoints, linear UV and colour), **PN** (the converter's PN code, `cv_sd_run`, on the whole model with the crease angle from the slider: viewer only,
to compare with GAME), and viewer-only
experiments on the same mesh: **PHONG** (Phong tessellation of the edge midpoints, alpha slider), **BUTTERFLY**
(interpolating 8-point stencil), **LOOP** (approximating: it also moves the original vertices, so it is not
usable in the converter). **HEAD** (viewer-only, experimental, for heads): Loop subdivision with the piecewise-smooth rules of Hoppe et al. (crease edges 1/2-1/2, crease vertices 1/8-3/4-1/8, corners fixed), vertices welded by position so UV seams keep their UVs, passes 1 to 3, optional "texture features are creases" (UV-island borders and texture changes, drawn cyan), a neck pin (the open boundary keeps its exact positions; the Stats show the neck gap), and Taubin smoothing after the subdivision. Every method keeps crease, boundary and joint edges straight and reports why.

## Setup

A Windows g++ (MinGW-w64), as for seaview: set `MODELVIEW_GXX` (or `SEAVIEW_GXX`) in `<repo>/.env` or the
environment (see `.env.example`). The ROM (a big-endian `.z64`): `--rom FILE`, else `MODELVIEW_ROM` in the same
places, else the main checkout's `build-win/data/*.z64`. Without a ROM the viewer shows the mannequin only. In a
git worktree the main checkout's `.env` is used if the worktree has none. No zlib is needed: `pc/mv_zlib.cpp`
backs the converter's inflate calls (`pc/shim/zlib.h`). Builds go to `build/`, settings to `data/modelview.ini`.

## Commands

```sh
python tools_pc/modelview/modelview.py pc                       # build and open the viewer
python tools_pc/modelview/modelview.py pc --model CgreyguardZ --method PN
python tools_pc/modelview/modelview.py pc --shot out.png --model CheadbrosnanZ --method PN --view split --checker 1
python tools_pc/modelview/modelview.py shots --out <dir> [--models A,B] [--methods FLAT,PN]   # a PNG per model and method + <dir>/index.html
```

Options (they win over the `.ini`): `--rom`, `--model NAME` (a `C*` file, empty = the mannequin), `--tier 0|1`,
`--method FLAT|PN|PHONG|BUTTERFLY|LOOP`, `--passes 1|2`, `--crease DEG`, `--clamp F` (PN bulge clamp),
`--splitjoint 0|1`, `--view side|split|sub|orig`, `--wire --edges --checker --normals --light --smooth 0|1`,
`--w --h` (shot size). Relative `--shot` paths are taken from where you run the command.

## Viewer

- Camera (outside the panel): left-drag orbit, right- or middle-drag pan, wheel zoom, **R** / Home reset
  (frames the model's bounding box), **F12** screenshot (`data/shot_NNN.png`), Esc quit. The mouse over the
  panel never moves the camera.
- The UI is Dear ImGui: a menu bar at the top and a panel on the left under it.
  **File**: save / load `data/modelview.ini`, screenshot, reset camera, quit.
  **Model**: the test mannequin, then the ROM's **Bodies** and **Heads**. **View**: side by side, split slider,
  subdivided, original; wireframe overlay, colour straight edges by reason, checker texture instead of vertex
  colours, show normals, directional light, smooth shading (vertex normals).
  The panel: the model and its **tier** (near = the detailed meshes the game draws up close, far = the simplified
  LOD meshes), **Method**, **Edges** (crease angle 0-180 deg, default 60; split joint edges, off like the converter
  and ignored by PN; passes 1 or 2; PN bulge clamp, default 0.15; Phong alpha for PHONG; the split position in
  split view), **Stats** (vertices and triangles before and after, edge counts per reason) and the edge-colour legend.
- Edge colours: red = boundary, green = joint (the edge touches vertices of different matrices), yellow = crease
  (the two faces differ by more than the crease angle), magenta = UV seam (same position, different UV; informational).
- **Animation** panel: the game's animations (list from `assets/animationtable_data.c`), frame slider, play and speed. The default is frame 0 of `idle`. Models are posed with the joint rotations of the frame (the node tree from the ROM image, the skeleton and the frames from the repo's `assets/`), and the subdivided mesh is the rest-pose subdivision skinned per joint matrix, so the seams show what the game does. Only models on the `guard` skeleton are animated; heads and other skeletons stay at rest (the panel says so). Root motion is not applied.
- View toggles: textures, vertex colours (off = texture only), wireframe, straight-edge colours, normals; **Clean view** turns all overlays off. Flags: `--vcol --wire --edges --normals --clean 1 --texture 0|1`.
- `modelview.py selftest` checks the texture format decoders and the animation maths without a ROM. `shots` takes `--anim NAME --frame N --clean`.
- Model vertex colours are one colour per matrix (limbs read apart); the checker shows the model's UVs (1 UV unit = 32 texels). Textures are decoded from your ROM (zlib CI textures and the lookup codecs; the others fall back to the vertex colour).

## Files

`pc/mv_mesh.h` (the data model: joint-local vertices, UV, RGBA, joint id, layer; joints with a rest offset),
`pc/mv_subdiv.h/.cpp` (the method interface, edge classification, FLAT, PN through `conv_subdiv.c`, PHONG, BUTTERFLY,
LOOP), `pc/mv_rom.h/.cpp` (ROM, file list, model extraction), `pc/mv_mannequin.cpp` (the test model),
`pc/modelview.cpp` (window, GL, the ImGui panel, `.ini`), `pc/mv_imgui.h/.cpp` (ImGui renderer and input; ImGui
itself is `port/third_party/imgui`), `pc/mv_zlib.cpp` + `pc/shim/zlib.h` (inflate for the converter code),
`pc/mv_png.h` (PNG writer, copied from seaview).
