# project-dam-editor: the Project Dam co-op mission editor

A native desktop tool (Windows, Linux) that edits **missions**: JSON deltas over one of the
original solo levels (`port/mission/mission.h`). It shows the level as the game draws it, lets you
move, add and remove guards and objects, saves `mission.json`, and starts the game on the result.
It is separate from the game and never built for the Switch.

## Never share an export

The level view comes from a **level export** (`level.rec` + `level.json`) and from the **glTF decor export** (below), both written by
the game from the user's own ROM. They contain ROM-derived geometry and textures. Exports live in your local data
directory, are gitignored, and must never be committed, attached to issues or sent to anyone. What
you share is the mission: `mission.json` (and its Luau scripts). A mission holds only numbers (prop
indices, pad numbers, model ids) and the uuids of the props it adds, no ROM data.

**Added props have a uuid.** Every added guard or object (Add, Duplicate, Paste, MCP) gets a fresh random v4 `"uuid"` in its `add` op
and an optional `"name"` (the Selection tab shows both; the name is editable, the uuid has a Copy button). Scripts reference an
added prop by that uuid (a `Guard` / `Door` / `Prop` property is the uuid string, a prop target is `{"kind": "prop", "uuid": "..."}`),
an original level prop by its number; the game resolves each uuid to the setup prop number the record gets, so adding or deleting
other props no longer shifts a script's references. Format: `port/mission/mission.h`.

**The decor comes from glTF.** The rooms (the level's walls, floors, scenery) are drawn from the static glTF export of the game
(`port/export/`, issue #150: `ge007 --export-levels DIR` writes `DIR/<level>/<level>.gltf` + `.bin` + `textures/N.png` for every level),
read by `decor.cpp` with the vendored cgltf. It lives in `<game dir>/data/exports/gltf/`, beside the per-level `level.rec` folders. When it
is missing, or its `asset.extras.exporter_version` is not the one this build reads (`EX_EXPORTER_VERSION`), the editor runs
`<game> --export-levels <that folder>` once in the background (about two seconds for all levels) and reloads the decor when it is done
(the log says "glTF export done"); *Tools > Settings > Re-export all cached levels* redoes it too. Without a game executable the view
keeps level.rec's rooms. `level.rec` still supplies the guards, the props whose model is not in the model export (see "Props come from the model export") and everything else below. The glTF is drawn
unlit as texture x vertex colour (colours converted back from linear to the game's own values), MASK as an alpha test at 0.5, BLEND
after the opaque draws (depth-tested, written only where opaque), back faces culled unless the material is double sided, a decal's
depth offset kept. Clicking the floor, the Zone tool and the surface snap use the glTF triangles. **`GE_EDITOR_DECOR=rec`** forces
level.rec's rooms (to compare the two). The glTF also carries the level's collision floor ("stan", `docs/specs/stan.md`) as a hidden
`collision` node (scene 1, not drawn as decor): *View > Stan tiles* (or `--floor`) draws it from there as filled translucent tiles with
the edges that have no neighbour (walls) highlighted, and falls back to `level.json`'s `stan` when the glTF has none (or with
`GE_EDITOR_DECOR=rec`); the log says which ("floor: glTF collision (N tiles)" / "floor: level.json"). `make_test_rec` also writes a synthetic glTF (`<dir>/gltf/bunker1/`, the editor looks in
`<export>/gltf/` too).

**Where the data comes from.** The renderer-only part (models, stan tiles, portals, the
model-to-owner link) is the level export; the rooms are the glTF above. The level *setup* (pads, guards, objects with every record field,
AI list ids) is read by the editor itself from the converted data the game keeps next to its
executable, `<game dir>/data/pccg-<region>/` (written from your ROM the first time the game starts;
`GE_EDITOR_DATA` overrides the data folder). The export's `level.json` names the setup row. If the
converted data is missing the editor says so: launch the game once with your ROM. The fields come out
through the same table (`port/mission/mission_fields.c`) that the game's mission loader writes them with.

## Build

Like the rest of the PC port, in Docker (the editor is a target of the root CMake build):

```sh
python tools/dev.py build windows      # build-win/project-dam-editor.exe (+ SDL2.dll next to it)
python tools/dev.py build linux        # build-linux/project-dam-editor
```

Only the editor and its test-export generator, faster:

```sh
docker compose run --rm windows sh -c 'export SDL2DIR=/usr/x86_64-w64-mingw32/sys-root/mingw; \
  cmake -S . -B build-win -G Ninja -DROMID=ntsc-final -DCMAKE_TOOLCHAIN_FILE=/usr/share/mingw/toolchain-mingw64.cmake \
  && cmake --build build-win --target project-dam-editor make_test_rec'
```

Targets (`editor/CMakeLists.txt`): `project-dam-editor`; `ge_luau_analysis` (Luau Analysis, a static library of its own with a clean include path);
`make_test_rec`; `test_decor` (the glTF loader on a synthetic glTF, a ctest, runs without a ROM). The editor links the `ge_render` library (`port/ge_render`, issue #142): it opens its window and its backend with
`ge_render_open`, draws the level through the backend's draw API (`scene.cpp`) and the GUI through the same API
(`imgui_render_ge.cpp`, replacing the OpenGL3 ImGui backend). **Renderer:** OpenGL (default; needs 3.0 or newer) or Vulkan 1.3,
`--renderer gl|vk` or Tools > Settings > Rendering (applies at the next start; `dev.py editor --renderer vk`). Vulkan falls back
to OpenGL when there is no usable driver. The whole-frame offscreen option is OpenGL only; on Vulkan a thumbnail or screenshot read-back
presents one frame.

## Run

```sh
project-dam-editor --export <dir with level.rec and level.json> [--mission <mission.json>]
```

Produce a real export with the game: `ge007 -level_09 --export-level <dir>` (Bunker 1). Without an
export you can still edit a mission; the view then shows markers only.

To develop without the game or a ROM, generate a **synthetic** export (boxes, procedural textures):

```sh
make_test_rec <empty dir>          # writes level.rec + level.json of a fake "bunker1" and its setup
                                   # (<dir>/data/pccg-ntsc-final/, in the format the game's converted data uses)
GE_EDITOR_DATA=<that dir>/data project-dam-editor --export <that dir>
python editor/tools/mcp_smoke.py <project-dam-editor> <that dir> <work dir>   # headless MCP smoke test
```

Options: `--port N`, `--no-mcp`, `--game EXE`, `--mcp-stdio`, `--mcp-http`, `--screenshot F.png
[--frames N]`, `--cam x,y,z,yaw,pitch`, `--click X,Y [--tool ...]` (see `project-dam-editor --help`).

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
  Move / Guard / Object / Zone, Play + Co-op + the spawn label, snapping; Geometry is greyed out for #105), the
  *Scene* tree on the left (filter box; Guards, Objects, Zones, Scripts, Mission pads, Free pads, Rooms, each with
  its count), the 3D view, and on the right three tabs: *Selection*, *Mission* (the mission's properties: sections General, Start and test,
  Objectives, Environment, Thumbnail, File) and *Mod* (the mod's properties and its missions). The bottom is one status line (the last message, red for a
  failure); click it, or View > Show log, for the full log. Help > Controls lists the mouse and keys. The code is
  split by panel (`ui_*.cpp`, see below).
* **Settings** (Tools > Settings...): the game executable (Browse... on Windows), the export cache folder (Open
  folder, Re-export all cached levels one after another in the background), offscreen rendering and the MCP
  server (enable + port; both apply at the next start), clearing the recent list. They are kept in
  `project-dam-editor-prefs.txt` next to the editor (`key=value` lines: `coop`, `offscreen`, `mcp`, `mcpport`).
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
  `GE_EDITOR_GAME`, `--game`, or the `ge007` exe next to `project-dam-editor`. The spawn pad is the selected
  pad or prop (or *Set as spawn*), as a number or a mission pad id.

Props are drawn with a model placed so its bottom centre sits on the prop's pad (the recorded placement and rotation are not
reused); a prop with no model at all is a box. Everything is replayed through the game's own
fast3d OpenGL backend, so textures and colours match the game (fog is off).

**Props come from the model export.** Object props (doors, crates, consoles, vehicles, items; not guards) are drawn from the static
model export (`port/export/`, issue #119): `models/models.json` and `models/<name>/<name>.gltf`, one glTF per model number in model
units, written beside the level folders by the same `ge007 --export-levels DIR` (so in `<game dir>/data/exports/gltf/models/`, or in
`gltf/models/` next to the level exports; the editor looks where it looks for the decor). `decor.cpp` reads it with the same material
mapping as the rooms, default scene only (the most detailed level of detail, switchable parts visible, joints in their rest pose, so a
vehicle is a whole vehicle where level.rec's capture collapses every joint to the origin). Each model is read and uploaded on first
use. Scale = the model's `base_scale` (0.1 for most props) x the record's extra scale (u8.8); placement = the pad, the pad's yaw and
the bottom centre of the glTF model's own bounds; a door is fitted to its bound pad's box like the level.rec one (model x to the pad's
y extent, y to z, z to x). The selection box is the glTF model's bounds. A model that is not in `models.json` (or has no triangles)
falls back to level.rec's model; guards always use level.rec. The log says what was decided, once per level: `[editor] props: glTF
models (N placed, M from glTF, K fallback: models ...)`. **`GE_EDITOR_MODELS=rec`** forces level.rec's props (to compare). The Props
browser has an *All models* category listing every model of `models.json` (number and name, search by either), placeable like the
level's own models (an object of type 3 with that model, item none).

### Scripts and zones

* **Doors.** A door record's pad is a bound pad of the level: the editor draws and picks a door as the game fits it (the model scaled to its bound pad's box, turned with the pad). A door can only be moved, added or pasted onto another *bound pad* (an original pad number >= 10000, click it in the view or `move` with `pad`); anywhere else is refused with one message, in the apply log, Validate and the tools, and the gizmo does not move doors.
* **Zones.** The *Zone* tool: a click on the floor (or a pad) creates a zone there, id `z1`, `z2`, ... , radius 150, the room under
  the point, its centre 50 units above the floor point (a script sees a player when the player's position is within the radius of
  the centre: a sphere). A zone is drawn as a ring with two faint vertical rings and a height marker; click it (or its row under
  *Zones* in the Scene tree) to select it. The gizmo (or `G`) moves it; the Selection tab edits centre, radius and room, *Remove* (or
  `Delete`) deletes it. All of it is undoable.
* **Script entries.** *Scripts* in the Scene tree lists every entry of `mission.json` "scripts" as `use > target`; a red row has
  an error. *Add script...* offers the built-in library (`g_missionLibScripts`), the mission's own scripts
  (`<mission dir>/<use>.luau` or `<mission dir>/scripts/<use>.luau`) and *New script* (a template with `export type Props = {}`).
  Selecting a row shows the **Script** section in the Selection tab and highlights the target in the view (cyan; what its
  properties point at is lighter): Up / Down / Remove reorder and delete; *script* picks another `use`; *Target* is a kind (mission /
  prop / pad / zone) plus the value, which *Pick in view* takes from a click (a door, guard or object; a pad); then the **typed
  property form**. Every change is one undo step and ends up in `mission.json`.
* **The form comes from the script's own `export type Props = { ... }`**, read with the vendored Luau parser (not text matching).
  The aliases of `port/mission/lib/mission.d.luau` choose the widget: `Item` an item combo (names from `item_names.h`, plus the
  number), `Objective` a combo of the mission's objectives, `Zone` a combo of its zones, `Door` / `Prop` / `Guard` the number of an
  original prop (typed, or picked in the view; its kind and model are shown) or a prop the mission adds, chosen from the *added...*
  combo (listed by name, "Kalinin (added guard)") or picked in the view: the form then holds its uuid, shown by name (`#` goes back to
  a number). Validate reports an unknown uuid, a uuid of the wrong kind (a `Door` field naming a guard) and a number that designates an
  added prop. `Pad` a pad number
  or mission pad id (picked in the view), `string` a text field, `number` a number field, `boolean` a checkbox. A `T?` field has a
  checkbox: unticked, the key is left out of `mission.json`. The form is rebuilt when the script's `Props` changes.
* **Code editor** (the *Code* button, a double click on a row, a finding in the log): a floating window with the vendored
  ImGuiColorTextEdit (Luau colouring, line numbers). A built-in library script is read-only with *Copy into the mission*, which writes
  `<mission dir>/scripts/<name>.luau` and points the entry at the copy; the mission's own scripts are edited and saved with
  `Ctrl+S` together with the mission (a new, unsaved mission has no folder yet: save it first).
* **Type checking.** Each script is checked with the vendored Luau Analysis (the editor only, never the game) in strict mode
  against `mission.d.luau` (`g_missionApiDefs`): `props` has the script's own `Props` type, `target` the class of the entry's
  target kind (`PropTarget`, `PadTarget`, `ZoneTarget`, `MissionTarget`, all `Target`). Findings show as a marker and squiggle on their
  line, in the list under the editor (350 ms after the last key), in Mission > Validate and the log, and in MCP `validate`
  (`severity`, `script`, `line`). The same Validate checks every entry's properties against `Props` (a required one missing, a
  wrong kind, an objective / zone / prop / pad that does not exist). `mission.d.luau` is written in the Luau 0.746 definition dialect and loaded as is (see
  `docs/porting-notes.md`).

### Transform, mods, thumbnails, environment

* **Gizmo.** A selected mission pad, guard or object shows arrows (axis), a square (horizontal plane)
  and an orange ring (yaw). `G` grabs it, `X`/`Y`/`Z` lock an axis while moving, click or release
  confirms, `Esc` cancels. The Properties panel has numeric position and yaw (Enter applies). Moving
  an original prop gives it a mission pad of its own at the new spot (an existing one is reused);
  original pads are level data and cannot be moved. Toolbar snaps: *Grid* (step), *Floor* (stan tile
  under the point), *Pads*. One drag is one undo step.
* **Opening and saving.** The Mission tab says "unsaved changes" and its File section shows the mission's path (shortened,
  the full path in a tooltip); the window title carries a `*` while it has unsaved changes and reads `<mission>[*] - <level> -
  GoldenEye mission editor`. A new mission is saved to `<game data dir>/missions/<name>/mission.json` (the data
  folder next to the game, or `GE_EDITOR_DATA`), never to the export cache; a mission of an open mod keeps
  `mods/<mod>/<mission>/mission.json`. On Windows, *Open...* and *Save as...* open the standard file dialogs
  (`Ctrl+O`, `Ctrl+Shift+S`; `Ctrl+S` saves); the typed "mission file" field + *Open* works everywhere. The last
  8 opened or saved missions are listed under *recent* (stored in `project-dam-editor-recent.txt` next to the editor,
  missing files dropped). Closing the window, opening or creating another mission or mod while the mission has
  unsaved changes asks *Save / Discard / Cancel*; MCP `mission_open` refuses the same case unless `discard: true`.
* **New mission** (File > New mission..., or the buttons of the Mission and Mod tabs). A dialog asks the name, the base
  level (fixed once the mission exists) and the starting point: *Level as is* (all the original guards and objects are
  there) or *Emptied level* (remove ops for every original guard, the weapons they carry and every pickup: keys,
  magazines, weapons, ammo, body armour; doors, glass, scenery and the objective objects stay). With a mod open a
  checkbox adds the mission to its campaign. The dialog says where the file goes
  (`mods/<mod>/<folder>/mission.json`, or `<game data>/missions/<folder>/mission.json`) and whether the level is loaded or
  exported first. MCP `mission_open` takes `emptied: true` with `new`. The recent lists (File > Recent, the Mission tab)
  show each mission's name and level; the path is the tooltip, and paths that differ only by slash direction are one entry.
* **Mission tab.** *General*: name, the base level (read only; red with *Load* / *Move the mission to...* when the loaded
  level is another one), briefing, and the checkbox *Keep the level's objectives* (`keep_level_objectives` in mission.json: the
  level's briefing, objectives and exit stay; the mission's objectives are added after them). The mission's `description` is not edited (kept in the file when present).
  *Start and test*: the play spawn (level start, or a pad picked in the view), Co-op, Play. *Objectives*: letter, text,
  lowest difficulty (Agent / Secret Agent / 00 Agent), add, remove, reorder; an objective is completed by a script or
  a zone. *Environment*: below. *Thumbnail*: *Capture the view* stores a plain 4:3 crop as the base image; Contrast and
  Brightness are baked into `thumb.png` on capture and on save; the previews show what the player sees, converted by
  `port/mission/mission_thumb.c` (the game's own code path): the mission select film strip (68x44 grey) and the
  briefing polaroid (16 greys in two 128x64 halves). The base image is the last capture of the session, or the loaded
  `thumb.png`. *File*: the mission path, Open / Save as, the recent list, the level to open or re-export.
* **Mod tab.** A mod is a folder `<mod>/campaign.json` + `<mission>/mission.json` + `thumb.png`. Open or create a folder; edit
  name, author, description, affects (Story / Multiplayer) and the unlock order (ordered / open); the missions are a list
  with thumbnail, name and level: open, reorder, remove (the folder stays), *New mission...*, *Add the open mission*
  (saved into the mod's folder); *Save mod* / *Check mod* / *Publish...*. `thumb.png` of a mission in the open mod is next
  to its `mission.json`; any other mission uses the folder of its file.
* **Textures (Mod tab).** `<mod>/textures/<n>.png` replace texture `n` of the glTF decor (`decorLoad` takes the sources in
  order: the open mod's folder, then the chosen texture pack `<game>/texturepacks/X.ptexpack`, a tar or a folder; the
  format is `port/mission/texpacks.h`). *Pick a surface* puts the texture number of the clicked decor triangle in the
  field (`LTri::texnum`, `Hit::texnum`); *Replace...* / *Remove*; the texture pack combo (also in Tools > Settings) is kept
  in the prefs file (`texpack=`). MCP: `textures`, `mod_set_texture`, `set_texture_pack`. The decor log lines go to stderr
  (stdout is the MCP channel in `--mcp-stdio`).
* **Packages (.pmod).** The game loads a mod as `mods/<mod>.pmod`, a plain `tar` of the mod's source folder (the source folder
  is the one you open in the Mod tab, kept outside `mods/`, usually a git repository of its own). *Play* on a mission of an
  open mod first writes `<mods>/<mod>.pmod` (the whole folder, so its scripts and pictures are what the game reads) and
  starts the game on `--mission <mods>/<mod>.pmod/<mission>/mission.json`; if the package cannot be written (the game
  holds it open) Play reports the error and does not start. *Mod > Publish...* (also a button of the Mod tab) opens a dialog
  with an output folder (default `<mod>/dist`): **Publish** writes `<folder>/<mod>.pmod` to share, **Deploy** writes
  `<mods>/<mod>.pmod`. Both save the mod, validate its campaign and every mission (an error refuses, the message shows in
  the dialog) and report the path and size written. Dotfiles (`.git`, `.gitignore`) and a top-level `dist/` are not packed. A
  new mod gets a `.gitignore` containing `dist/`. MCP: `mod_publish` (`out_dir` optional) and `mod_deploy` return the path and
  byte count; `test_run` follows Play.
* **Check (Mission > Validate, the log).** Format and ids; pads and ops against the level; every pad in use against the
  walkable floor (a stan tile within 35 units on the ground plane): a guard standing off the floor is an *error* (it does
  not spawn), an object or a mission pad is a *warning*; scripts whose target prop is removed or missing, whose
  `key_door` key item no object of the mission or the level gives (a `give_item` script counts), or whose `objective`
  is not listed. A finding about an item or a pad selects it and frames it when clicked in the log. MCP `validate`
  returns `errors` (texts) and `issues` (severity, text, the item / pad it is about).
* **Props browser** (View > Props browser). Category *Level*: the models the open level uses (the export's models table), each
  rendered once with the same fast3d replay as the view and cached as a texture; search by model id, `guard`, `object` or
  type. Drag a cell onto the 3D view to place an object (or a guard) with that model at the point under the mouse
  (as Ctrl+V does); a click makes it the default of the Guard / Object tool. Category *All models*: every model of the model export
  (see "Props come from the model export" above); guards are only in *Level* (they come from level.rec).
* **Environment panel.** Per-field overrides (checkbox = the key is present; unchecked = the level's
  own): mood, sun, sky, clouds, sea. Save only: the editor draws no sky or sea, preview in the game.
  The sun marker in the view is draggable (azimuth 0 = +z).

## MCP

The editor is its own MCP server; the tools run the same code as the buttons.

```sh
claude mcp add --transport http project-dam-editor http://127.0.0.1:8765/mcp     # GUI running (port shown in the top bar)
claude mcp add project-dam-editor -- project-dam-editor --mcp-stdio --export <dir>        # headless, no window
```

Streamable HTTP on 127.0.0.1 only: `POST /mcp`, JSON-RPC 2.0, JSON responses; requests with a
foreign `Origin` or `Host` are refused (403). `--mcp-stdio` is newline-delimited JSON-RPC on
stdin/stdout; `--mcp-http` serves HTTP without a window.

Test aids for scripted runs and screenshots: `--click X,Y`, `--mouse-at X,Y`, `--mouse-then FRAME,X,Y`,
`--key FRAME[xLEN]:SPEC` (ctrl+c, ctrl+d, rmb+ctrl, wheel1 ...), `--mouse-drag` (+ `--mouse-from X,Y`), `--open-tab`,
`--open-menu`, `--open-window settings|help|log|props|newmission`, `--select script:N|zone:ID|prop:N`, `--open-script NAME[:LINE]`, `--capture` and `--thumb-adjust C,B` (the Thumbnail
section), `--place-model object|guard:MODEL,TYPE,ITEM@X,Y`, `--log-cam` (see `--help`).

Tools: `level_describe` (`detail`: summary, pads, props, mission, links, names), `level_open`, `mission_open`
(`path`, or `new` + `level` + `name` [+ `emptied`]; optional `export`), `mission_save`, `place_guard`,
`place_object`, `move`, `remove` (original props by `prop`, added ones by `uuid` or `kind` + `op`; `move`, `set_transform`, `set_presence` and
`set_guard_behavior` take the same), `undo`,
`validate`, `set_guard_behavior` (a guard's AI list: an original guard gets a `move` op that keeps its pad and carries
`ai`, an added one its own `ai`), `test_run`, `set_transform` (pos, yaw, axis, snap), `capture_thumb`, `mod_open`, `mod_save`,
`mod_add_mission`, `mod_reorder`, `set_environment`, `script_list`, `script_read`, `script_write`, `script_copy`, `script_attach`, `set_props`, `script_remove`, `zone_add`, `zone_set`, `zone_remove`, `duplicate` (a guard or object, with all its fields and its facing, as a new item; optional `pad` / `pos`; an added item to copy is named by `from_uuid`).
`place_guard`, `place_object` and `duplicate` accept an optional `uuid` (lowercase UUID, unused) and `name`; without a `uuid` the editor makes one and returns it
(`{"added": {"kind": "guard", "op": 3, "uuid": "...", "name": "..."}, "uuid": "..."}`). `level_describe` items and `script_list` (`added_props`) show the uuid and name of every added prop. Scripts and zones: `script_list` (the entries with `use`, `target`, `props` and the script's Props schema, the library, the mission's own scripts, the zone ids), `script_read {use}` (source, schema, type-checker findings), `script_write {use, text}` (a mission script; held until `mission_save`; returns the findings with line numbers), `script_copy {use, switch?}` (library or mod script -> the mission's own), `script_attach {use, target, props}` or with `index` an update (`target` = `{kind: prop, prop: N}` (an original prop) | `{kind: prop, uuid: "..."}` (a prop the mission adds) | `{kind: pad, pad: N | "id"}` | `{kind: zone, zone: "id"}` | `{kind: mission}`; `props` = `{name: number | string | boolean}`; a `Door` / `Guard` / `Prop` property is an original prop's number or an added prop's uuid string), `set_props {index, props?, unset?, replace?}`, `script_remove {index}`, `zone_add {pos, radius?, room?, id?}`, `zone_set {id, pos?, radius?, room?}`, `zone_remove {id}`. `validate` returns the type-checker findings as issues with `script` and `line`.

A whole mission can be authored, checked and play-tested through these tools alone (the sample `docs/modding/samples/formula` is such a mission):
`mission_set {name?, briefing?, description?, keep_level_objectives?}`; `objective_add {id, text, difficulty?}`, `objective_set {id, text?, difficulty?, new_id?, index?}`
(`index` reorders), `objective_remove {id}` (`difficulty` = the lowest difficulty, 0 Agent .. 2 00 Agent; ids are unique, one undo step each);
`place_guard` / `place_object` take every record field of the add op by name (`head`, `health`, `reaction_time`, `bitflags`, ... for a guard; the
header fields flat and the type's own in a sub-object, e.g. `{"type": 1, "door": {...}}`, for an object; `mission_fields.h`), `difficulties`
and, for a guard, `weapons` (a list of at most two `{item, model?, flags?, collectable: {...}}`); an unknown field is refused with the valid names;
`level_describe` `detail: "links"` lists the terminal <-> door pairs, the dual-weapon links, the tag records (tag id -> prop) and every door's bound pad,
terminals and tags (read from the setup the editor loaded), `detail: "names"` the body / head ids with their model file names and the item ids
with names (`props` also names each guard's body and head); `script_check {text, target_kind?, use?}` type-checks a draft source without
writing it; `test_run {wait_seconds?, stop?}` can wait (cap 120 s; over HTTP the wait runs on the transport thread, so the editor keeps drawing and other calls are answered meanwhile; over stdio the call blocks) and returns the game log's
`missions:` lines since the launch, the `[ERROR]` lines and `running`; `stop: true` then ends the game process this editor started. `level_describe` `props` lists every item's record fields. `move` also takes `pos` (+ `axis`, `snap`). A pad is an original pad number or a mission pad id; `pos: [x, y, z]`
creates a new mission pad there.

Smoke test (headless stdio plus HTTP, against the synthetic export):

```sh
python editor/tools/mcp_smoke.py <project-dam-editor exe> <export dir> <scratch dir> [--gui]   # --gui also runs capture_thumb (needs a display)
```

## Layout

| File | Role |
|---|---|
| `doc.h/.cpp`, `doc_actions.cpp` | the document: level export, mission, selection, every action, undo |
| `scene.cpp`, `view.cpp` | replay through `gfx_opengl_api`; camera, projection, picking |
| `decor.h/.cpp`, `exportcache.*` | the glTF decor loader (cgltf + `png.cpp`); the export cache folders and the game runs that fill them |
| `mcp.cpp`, `mcp_http.cpp` | JSON-RPC tools; the HTTP transport (cpp-httplib) |
| `main.cpp` | `main()`, the loop, the command line and the test aids |
| `ui_app.h` | the GUI state (`App`) and the helpers shared by the panel files |
| `ui_menubar.cpp`, `ui_toolbar.cpp`, `ui_scene.cpp` | menu bar, toolbar, the left Scene tree |
| `ui_selection.cpp`, `ui_mission.cpp`, `ui_mod.cpp` | the right panel's tabs (its tab bar is in `ui_frame.cpp`) |
| `ui_scripts.cpp`, `ui_scripts.h` | the Scripts and Zones panels: Scene tree nodes, the Selection tab's Script / Zone sections (the typed property form), the code editor window, picking in the view, the zone and target overlay |
| `doc_scripts.cpp` | the document side: script entries, zones, script sources and their save, prop numbers, the script checks of Validate |
| `script_props.cpp`, `script_check.cpp` | the `Props` schema (Luau parser); the type checker (Luau Analysis, `props` / `target` typed per script) |
| `mcp_tools_script.inc`, `item_names.h` | the script and zone MCP tools; the ITEM_IDS names |
| `mcp_tools_mission.inc`, `model_names.h` | the mission-authoring MCP tools (metadata, objectives, record fields, links / names, script_check, test_run log); the body / head model names (generated) |
| `ui_thumb.cpp`, `ui_props.cpp` | the Mission tab's Thumbnail section (previews through `port/mission/mission_thumb.c`); the Props browser |
| `ui_settings.cpp`, `ui_modals.cpp`, `ui_log.cpp` | Settings window, popups (unsaved changes, mission level, New mission, Controls), status line + log |
| `ui_overlay.cpp`, `ui_frame.cpp`, `ui_actions.cpp` | 3D overlay + gizmo + sun marker; one frame (layout, camera, shortcuts); actions shared by menus, buttons and keys |
| `launch.cpp` | starting the game |
| `third_party/` | cpp-httplib, the ImGui SDL2 and OpenGL3 backends, ImGuiColorTextEdit (the code editor), `luau_shim/` (see `NOTICE`) |
| `tools/` | `make_test_rec.cpp`, `synth_gltf.h` (synthetic glTF), `test_decor.cpp`, `mcp_smoke.py` |

cgltf (`port/third_party/cgltf`, MIT, see `NOTICE`) is vendored for the decor loader. Dear ImGui itself and `port/fast3d/` are the game's vendored copies, used unmodified. The Luau type checker is `port/third_party/luau/Analysis` + `Config` (the same 0.746 as the game's VM and compiler), built as `ge_luau_analysis` for the editor only.
