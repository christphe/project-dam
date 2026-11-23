# ge-editor: the GoldenEye co-op mission editor

A native desktop tool (Windows, Linux) that edits **missions**: JSON deltas over one of the
original solo levels (`port/mission/mission.h`). It shows the level as the game draws it, lets you
move, add and remove guards and objects, saves `mission.json`, and starts the game on the result.
It is separate from the game and never built for the Switch.

## Never share an export

The level view comes from a **level export** (`level.rec` + `level.json`), written by the game from
the user's own ROM. It contains ROM-derived geometry and textures. Exports live in your local data
directory, are gitignored, and must never be committed, attached to issues or sent to anyone. What
you share is the mission: `mission.json` (and its Luau scripts). A mission holds only numbers (prop
indices, pad numbers, model ids), no ROM data.

**Where the data comes from.** The renderer-only part (room geometry, models, stan tiles, portals, the
model-to-owner link) is the export. The level *setup* (pads, guards, objects with every record field,
AI list ids) is read by the editor itself from the converted data the game keeps next to its
executable, `<game dir>/data/pccg-<region>/` (written from your ROM the first time the game starts;
`GE_EDITOR_DATA` overrides the data folder). The export's `level.json` names the setup row. If the
converted data is missing the editor says so: launch the game once with your ROM. The fields come out
through the same table (`port/mission/mission_fields.c`) that the game's mission loader writes them with.

## Build

Like the rest of the PC port, in Docker (the editor is a target of the root CMake build):

```sh
python tools/dev.py build windows      # build-win/ge-editor.exe (+ SDL2.dll next to it)
python tools/dev.py build linux        # build-linux/ge-editor
```

Only the editor and its test-export generator, faster:

```sh
docker compose run --rm windows sh -c 'export SDL2DIR=/usr/x86_64-w64-mingw32/sys-root/mingw; \
  cmake -S . -B build-win -G Ninja -DROMID=ntsc-final -DCMAKE_TOOLCHAIN_FILE=/usr/share/mingw/toolchain-mingw64.cmake \
  && cmake --build build-win --target ge-editor make_test_rec'
```

Targets (`editor/CMakeLists.txt`): `ge-editor`; `ge_editor_f3d` (fast3d's OpenGL backend and glad,
compiled as the game compiles them, plus `fast3d_stubs.cpp` for the few game symbols it expects);
`make_test_rec`. Needs OpenGL 3.3 core.

## Run

```sh
ge-editor --export <dir with level.rec and level.json> [--mission <mission.json>]
```

Produce a real export with the game: `ge007 -level_09 --export-level <dir>` (Bunker 1). Without an
export you can still edit a mission; the view then shows markers only.

To develop without the game or a ROM, generate a **synthetic** export (boxes, procedural textures):

```sh
make_test_rec <empty dir>          # writes level.rec + level.json of a fake "bunker1" and its setup
                                   # (<dir>/data/pccg-ntsc-final/, in the format the game's converted data uses)
GE_EDITOR_DATA=<that dir>/data ge-editor --export <that dir>
python editor/tools/mcp_smoke.py <ge-editor> <that dir> <work dir>   # headless MCP smoke test
```

Options: `--port N`, `--no-mcp`, `--game EXE`, `--mcp-stdio`, `--mcp-http`, `--screenshot F.png
[--frames N]`, `--cam x,y,z,yaw,pitch`, `--click X,Y [--tool ...]` (see `ge-editor --help`).

### Using it

* **View.** Hold the right mouse button to fly: mouse = look, W A S D (or Z Q S D) / arrows = move,
  Space = up, Ctrl = down, Shift = fast. The movement keys work **only while the right button is held**,
  so the shortcuts (Ctrl+C / V / D, G, X / Y / Z) never move the camera. Middle button: pan (the point under
  the cursor follows it). Wheel: zoom toward the cursor, 15% of the distance to the scene point under it per
  notch (up = closer, down = away; nothing under it: a fixed step along the cursor ray). `F`: frame the selection.
* **Copy, paste, duplicate** (the 3D view hovered, no text field focused). `Ctrl+C` copies the selected guard or
  object (an original or one the mission added) with all its record fields and its facing; `Ctrl+V` pastes a new
  item at the pad / floor point under the cursor (a point makes a mission pad there with the copied facing);
  `Ctrl+D` duplicates it in place and moves the copy with the mouse until the next click. A copy is always an
  "add" op, the original is untouched. The MCP tool `duplicate` does the same. The Properties panel lists every
  record field (`port/mission/mission_fields.h`): editable for added items, read-only for original props.
* **Facing.** Models and their selection boxes are drawn turned by the yaw of the pad they stand on (a line out of
  the front of the box shows it); picking uses the same turned box.
* **Window layout.** A menu bar (File, Edit, View, Mission, Mod, Tools, Help), a short toolbar (tools Select /
  Move / Guard / Object, Play + Co-op + the spawn label, snapping; Zone and Geometry are greyed out for #105), the
  *Scene* tree on the left (filter box; Guards, Objects, Zones, Scripts, Mission pads, Free pads, Rooms, each with
  its count), the 3D view, and on the right three tabs: *Selection*, *Mission* (the mission, its level, files,
  environment, thumbnail) and *Mod* (the campaign). The bottom is one status line (the last message, red for a
  failure); click it, or View > Show log, for the full log. Help > Controls lists the mouse and keys. The code is
  split by panel (`ui_*.cpp`, see below).
* **Settings** (Tools > Settings...): the game executable (Browse... on Windows), the export cache folder (Open
  folder, Re-export all cached levels one after another in the background), offscreen rendering and the MCP
  server (enable + port; both apply at the next start), clearing the recent list. They are kept in
  `ge-editor-prefs.txt` next to the editor (`key=value` lines: `coop`, `offscreen`, `mcp`, `mcpport`).
* **Select.** Click a prop, a pad (blue diamond; mission pads are green) or the floor (selects the
  room), or a row of the Scene tree; double-click a row to frame it.
* **Edit.** Tools in the toolbar: *Guard* / *Object* (click a pad, or the floor to create a
  new mission pad there), *Move* (select a prop first, then click its destination). Edit > Delete removes
  the selected prop (an original one is marked removed; one the mission added is deleted). Defaults
  for new items are at the top of the Selection tab. `Ctrl+Z` undoes, `Ctrl+S` saves. Moved props are yellow
  with a line to their original pad, added ones green, removed ones grey.
* **Check and play.** The bottom panel validates the mission continuously (format, ids, and pads and
  props against the export). *Play* saves the mission and runs
  `<game> -level_NN --mission <file> --spawn <pad>` with `GE_COOP=1 GE_PADS=2`. The game is
  `GE_EDITOR_GAME`, `--game`, or the `ge007` exe next to `ge-editor`. The spawn pad is the selected
  pad or prop (or *Set as spawn*), as a number or a mission pad id.

Props are drawn with the model the exporter recorded for their owner, placed so the bottom centre
of the recorded geometry sits on the prop's pad (the recorded placement and rotation are not
reused); a prop with no recorded model is a box. Everything is replayed through the game's own
fast3d OpenGL backend, so textures and colours match the game (fog is off).

### Transform, mods, thumbnails, environment

* **Gizmo.** A selected mission pad, guard or object shows arrows (axis), a square (horizontal plane)
  and an orange ring (yaw). `G` grabs it, `X`/`Y`/`Z` lock an axis while moving, click or release
  confirms, `Esc` cancels. The Properties panel has numeric position and yaw (Enter applies). Moving
  an original prop gives it a mission pad of its own at the new spot (an existing one is reused);
  original pads are level data and cannot be moved. Toolbar snaps: *Grid* (step), *Floor* (stan tile
  under the point), *Pads*. One drag is one undo step.
* **Opening and saving.** The top of the Mission tab shows the open mission's name, its path (shortened, the full
  path in a tooltip) and a `*` while it has unsaved changes; the window title reads `<mission>[*] - <level> -
  GoldenEye mission editor`. A new mission is saved to `<game data dir>/missions/<name>/mission.json` (the data
  folder next to the game, or `GE_EDITOR_DATA`), never to the export cache; a mission of an open mod keeps
  `mods/<mod>/<mission>/mission.json`. On Windows, *Open...* and *Save as...* open the standard file dialogs
  (`Ctrl+O`, `Ctrl+Shift+S`; `Ctrl+S` saves); the typed "mission file" field + *Open* works everywhere. The last
  8 opened or saved missions are listed under *recent* (stored in `ge-editor-recent.txt` next to the editor,
  missing files dropped). Closing the window, opening or creating another mission or mod while the mission has
  unsaved changes asks *Save / Discard / Cancel*; MCP `mission_open` refuses the same case unless `discard: true`.
* **Mod (campaign) panel.** A mod is `mods/<mod>/campaign.json` + `<mission>/mission.json` +
  `thumb.png`. Open or create a folder, edit name, author, description, affects, unlock; add,
  reorder, remove and open missions. *Mission > Capture thumbnail* (or the button in the Mission tab) renders
  the view, 4:3 cropped, without markers or any effect, to 320x240: `thumb.png` of the open mission when it
  is in the open mod, else the path in the Mission tab.
* **Environment panel.** Per-field overrides (checkbox = the key is present; unchecked = the level's
  own): mood, sun, sky, clouds, sea. Save only: the editor draws no sky or sea, preview in the game.
  The sun marker in the view is draggable (azimuth 0 = +z).

## MCP

The editor is its own MCP server; the tools run the same code as the buttons.

```sh
claude mcp add --transport http ge-editor http://127.0.0.1:8765/mcp     # GUI running (port shown in the top bar)
claude mcp add ge-editor -- ge-editor --mcp-stdio --export <dir>        # headless, no window
```

Streamable HTTP on 127.0.0.1 only: `POST /mcp`, JSON-RPC 2.0, JSON responses; requests with a
foreign `Origin` or `Host` are refused (403). `--mcp-stdio` is newline-delimited JSON-RPC on
stdin/stdout; `--mcp-http` serves HTTP without a window.

Test aids for scripted runs and screenshots: `--click X,Y`, `--mouse-at X,Y`, `--mouse-then FRAME,X,Y`,
`--key FRAME[xLEN]:SPEC` (ctrl+c, ctrl+d, rmb+ctrl, wheel1 ...), `--mouse-drag`, `--log-cam` (see `--help`).

Tools: `level_describe` (`detail`: summary, pads, props, mission), `level_open`, `mission_open`
(`path`, or `new` + `level` + `name`; optional `export`), `mission_save`, `place_guard`,
`place_object`, `move`, `remove` (original props by `prop`, added ones by `kind` + `op`), `undo`,
`validate`, `test_run`, `set_transform` (pos, yaw, axis, snap), `capture_thumb`, `mod_open`, `mod_save`,
`mod_add_mission`, `mod_reorder`, `set_environment`, `duplicate` (a guard or object, with all its fields and its facing, as a new item; optional `pad` / `pos`). `level_describe` `props` lists every item's record fields. `move` also takes `pos` (+ `axis`, `snap`). A pad is an original pad number or a mission pad id; `pos: [x, y, z]`
creates a new mission pad there.

Smoke test (headless stdio plus HTTP, against the synthetic export):

```sh
python editor/tools/mcp_smoke.py <ge-editor exe> <export dir> <scratch dir> [--gui]   # --gui also runs capture_thumb (needs a display)
```

## Layout

| File | Role |
|---|---|
| `doc.h/.cpp`, `doc_actions.cpp` | the document: level export, mission, selection, every action, undo |
| `scene.cpp`, `view.cpp` | replay through `gfx_opengl_api`; camera, projection, picking |
| `mcp.cpp`, `mcp_http.cpp` | JSON-RPC tools; the HTTP transport (cpp-httplib) |
| `main.cpp` | `main()`, the loop, the command line and the test aids |
| `ui_app.h` | the GUI state (`App`) and the helpers shared by the panel files |
| `ui_menubar.cpp`, `ui_toolbar.cpp`, `ui_scene.cpp` | menu bar, toolbar, the left Scene tree |
| `ui_selection.cpp`, `ui_mission.cpp`, `ui_mod.cpp` | the right panel's tabs (its tab bar is in `ui_frame.cpp`) |
| `ui_settings.cpp`, `ui_modals.cpp`, `ui_log.cpp` | Settings window, popups (unsaved changes, mission level, Controls), status line + log |
| `ui_overlay.cpp`, `ui_frame.cpp`, `ui_actions.cpp` | 3D overlay + gizmo + sun marker; one frame (layout, camera, shortcuts); actions shared by menus, buttons and keys |
| `launch.cpp` | starting the game |
| `third_party/` | cpp-httplib, the ImGui SDL2 and OpenGL3 backends (see `NOTICE`) |
| `tools/` | `make_test_rec.cpp`, `mcp_smoke.py` |

Dear ImGui itself and `port/fast3d/` are the game's vendored copies, used unmodified.
