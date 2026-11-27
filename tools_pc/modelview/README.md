# modelview: compare mesh subdivision methods on a character

A PC dev viewer to judge how a subdivision method treats a character: original and subdivided next to each
other, with the edges coloured by why they stayed straight. It shows a **body and a head put together as the
game does** (`makeonebody`: the head file hangs off the body's neck joint), read from the **character export**
(`characters/characters.json` and one glTF per body and per head, [docs/specs/characters.md](../../docs/specs/characters.md)).
The export is made from **your own ROM** by `ge007-export` and lives outside the repo or in the git-ignored
`data/`; the viewer never reads the ROM itself. `shots` refuses an output dir inside the repo (outside `data/`).
A procedural test mannequin (original, not a game character) is always available.

Methods: **GAME (PN130, heads only)** (the default; the converter's own pass: only triangles whose three corners are head are subdivided, PN with a 130 degree crease limit, body triangles and neck edges stay whole; head = every vertex of the head file, or in a body with its own head (`has_head`) the joints from the neck down; View > Highlight head region tints them, neck edges draw white), **FLAT** (midpoints, linear UV and colour), **PN** (the converter's PN code, `cv_sd_run`, on the whole model with the crease angle from the slider: viewer only,
to compare with GAME), and viewer-only
experiments on the same mesh: **PHONG** (Phong tessellation of the edge midpoints, alpha slider), **BUTTERFLY**
(interpolating 8-point stencil), **LOOP** (approximating: it also moves the original vertices, so it is not
usable in the converter). **HEAD** (viewer-only, experimental, for heads): Loop subdivision with the piecewise-smooth rules of Hoppe et al. (crease edges 1/2-1/2, crease vertices 1/8-3/4-1/8, corners fixed), vertices welded by position so UV seams keep their UVs, passes 1 to 3, optional "texture features are creases" (UV-island borders and texture changes, drawn cyan), a neck pin (the open boundary keeps its exact positions; the Stats show the neck gap), and Taubin smoothing after the subdivision. Every method keeps crease, boundary and joint edges straight and reports why.

## Setup

A Windows g++ (MinGW-w64), as for seaview: set `MODELVIEW_GXX` (or `SEAVIEW_GXX`) in `<repo>/.env` or the
environment (see `.env.example`). The export: `--export DIR` (the `characters` folder or the export folder above
it), else `MODELVIEW_EXPORT`, else `tools_pc/modelview/data/export`, which `modelview.py export` writes:
`ge007-export` of this checkout, built and run in the pinned build image (Docker), on the ROM from `--rom`,
`MODELVIEW_ROM`, `N64_ROM` or a game `data/` folder. A game export works too (`ge007 --export-levels DIR` writes
`DIR/characters`). When the export was written by another exporter version than the checkout's
(`EX_EXPORTER_VERSION`), `modelview.py pc` says so. Without an export the viewer shows the mannequin only. In a
git worktree the main checkout's `.env` is used if the worktree has none. No zlib is needed: `pc/mv_zlib.cpp` backs
the PNG reader and the converter's inflate calls (`pc/shim/zlib.h`). Builds go to `build/`, settings to
`data/modelview.ini`.

## Commands

```sh
python tools_pc/modelview/modelview.py export                   # write data/export from your ROM
python tools_pc/modelview/modelview.py pc                       # build and open the viewer
python tools_pc/modelview/modelview.py pc --body CgreyguardZ --head CheadkarlZ --specs 1 --method PN
python tools_pc/modelview/modelview.py pc --shot out.png --model CheadbrosnanZ --method PN --view split --checker 1
python tools_pc/modelview/modelview.py shots --out <dir> [--models A,B+C] [--methods FLAT,PN]   # a PNG per model and method + <dir>/index.html
```

Options (they win over the `.ini`): `--export DIR`, `--body FILE` and `--head FILE` (export file names,
`CgreyguardZ`; both empty = the mannequin), `--model FILE` (a body or a head) or `--model BODY+HEAD`,
`--specs 0|1` (sunglasses), `--hair 0|1`, `--bodyscale 0|1`, `--tier 0|1`,
`--method GAME|FLAT|PN|PHONG|BUTTERFLY|LOOP|HEAD`, `--passes 1|2`, `--crease DEG`, `--clamp F` (PN bulge clamp),
`--splitjoint 0|1`, `--view side|split|sub|orig`, `--wire --edges --checker --normals --light --smooth 0|1`,
`--w --h` (shot size). Relative `--shot` and `--export` paths are taken from where you run the command.

## Viewer

- Camera (outside the panel): left-drag orbit, right- or middle-drag pan, wheel zoom, **R** / Home reset
  (frames the model's bounding box), **F12** screenshot (`data/shot_NNN.png`), Esc quit. The mouse over the
  panel never moves the camera.
- The UI is Dear ImGui: a menu bar at the top and a panel on the left under it.
  **File**: save / load `data/modelview.ini`, screenshot, reset camera, quit. **View**: side by side, split slider,
  subdivided, original; wireframe overlay, colour straight edges by reason, checker texture instead of vertex
  colours, show normals, directional light, smooth shading (vertex normals).
- **Character** panel: drop-down lists with a filter field (label, name or file) for the **Body** and the
  **Head**, listed by the export's labels. A head alone (body None) is shown on its own; a body without a head
  shows headless, as the game never draws it. The Head list is greyed out for a body that has its own head
  (`has_head`). "Only heads of the body's sex" narrows the list to the pool the game draws a random head from
  (`is_male`). **Tier**: near = the default scene (what the game draws up close), far = the far level-of-detail
  parts (a file without far parts shows the same mesh in both). The game's parameters of the pair:
  **Sunglasses** (head part 0, hidden unless the guard wears them) and **Hair** (head part 1), each greyed out
  when the head has no such part; **Body scale from the table** (0.95 for one body, 1 for the others) and, for
  information, `pov` (the scale of the root motion) and the body's sex. Below: triangles of the body, the head,
  the sunglasses and the hair, and how many textures loaded.
- **Animation** panel: all 170 animations, from the export's `animations/<name>.gltf` (one animation on the guard rig
  per file, listed in `characters.json`, loaded when chosen), retargeted onto the chosen body by node name: the animated rotations on the body's own joints, the root's height times the
  body's `pov` (an older export without that file: the body glTF's own animations). Frame slider, play at the
  animation's own rate and speed. The default is frame 0 of `idle`; off = the bind pose. The subdivided mesh
  is the rest-pose subdivision skinned per joint, so the seams show what the game does. The root's travel is left
  out (the model stays in place). A head on its own is not animated.
- **Method**, **Edges** (crease angle 0-180 deg, default 60; split joint edges, off like the converter and ignored
  by PN; passes 1 or 2; PN bulge clamp, default 0.15; Phong alpha for PHONG; the split position in split view),
  **Stats** (vertices and triangles before and after, edge counts per reason) and the edge-colour legend.
- Edge colours: red = boundary, green = joint (the edge touches vertices of different matrices), yellow = crease
  (the two faces differ by more than the crease angle), magenta = UV seam (same position, different UV; informational).
- View toggles: textures, vertex colours (off = texture only), wireframe, straight-edge colours, normals; **Clean view** turns all overlays off. Flags: `--vcol --wire --edges --normals --clean 1 --texture 0|1`.
- `modelview.py selftest` checks the PNG reader, the pose maths and the HEAD method without an export. `shots` takes `--export DIR --anim NAME --frame N --clean`.
- Model vertex colours are one colour per joint (limbs read apart; the head has its own); the checker shows the
  model's UVs. Textures are the export's PNGs.

## Files

`pc/mv_mesh.h` (the data model: joint-local vertices, UV, RGBA, joint id, layer; joints with a rest offset; materials
with the export's image), `pc/mv_export.h/.cpp` (the export: `characters.json` through cJSON, the glTF through
cgltf, body + head assembly, posing from `animations/<name>.gltf` retargeted by node name, a PNG reader), `pc/mv_subdiv.h/.cpp` (the method
interface, edge classification, FLAT, PN through `conv_subdiv.c`, PHONG, BUTTERFLY, LOOP), `pc/mv_head.cpp` (HEAD),
`pc/mv_mannequin.cpp` (the test model), `pc/modelview.cpp` (window, GL, the ImGui panel, `.ini`),
`pc/mv_imgui.h/.cpp` (ImGui renderer and input; ImGui, cgltf and cJSON are `port/third_party`), `pc/mv_zlib.cpp` +
`pc/shim/zlib.h` (inflate), `pc/mv_png.h` (PNG writer, copied from seaview).
