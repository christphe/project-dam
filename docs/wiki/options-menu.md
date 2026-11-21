---
title: Options menu
sources:
  - port/src/menu/menu.json
  - port/src/menu/menu_model.h
  - port/src/menu/menu_ui.h
  - port/src/menu/menu_game.cpp
  - port/src/optionsoverlay.c
  - port/src/defaults.json
  - tools_pc/gen_menudata.py
  - tools_pc/menuview/menuview.py
verified: e2fda9e2
---

# The options menu

How the in-game menu is built, where its content lives, and how to add or change a setting.
The menu is the same on the Switch and the PC (#77): one fullscreen menu drawn with Dear ImGui,
whose content is a JSON file.

## What the player sees

| Open with | Notes |
|---|---|
| **F10** | Keyboard. Toggles the menu. |
| **Select / −** on a pad | Any pad opens it; the pad that opened it owns it (it drives the menu and the pages show its player, #83). Only the owner's Select closes it. |
| **Options** on file select | The label at the right end of the Select / Copy / Erase bar (`port/src/frontoptions.c`). |

B (or Esc) goes back one level and closes the menu from the first menu. In a mission the
game is paused while the menu is open.

1. **First menu:** Resume, Options, Controllers, Check for updates, Restart (PC only: the
   Switch cannot relaunch itself), Quit.
2. **Options:** tabs (Video, Audio, Input, Game; L/R or the mouse), a list of entries on the
   left, the selected entry's form on the right, and a help line that follows the selected
   value.

The same UI layer also draws three screens that are not options pages: the Modern control
style's weapon and gadget wheels, the "press a button" device assignment screen of a
multiplayer set-up, and the co-op player count.

## The layers

```
port/src/menu/menu.json        content: tabs, entries, controls, rules, help texts
        |  tools_pc/gen_menudata.py (embedded as a byte array)
port/src/menu/menu_data.cpp    generated, do not edit
port/src/menu/menu_model.*     parses the JSON, evaluates show / disabled expressions, value <-> config conversion
port/src/menu/menu_ui.*        draws the menu with ImGui only; knows nothing about the game
port/src/menu/menu_game.cpp    the adapter: feeds pad / keys / mouse to ImGui, hands the triangles to fast3d
        |                      (port/fast3d/gfx_ui.h -> OpenGL, Vulkan, deko3d), binds values and actions
port/src/optionsoverlay.c      the old F10 overlay: no longer draws, but still owns open / close, the pause,
        |                      the row table that resolves keys, pseudo keys, actions and key capture
config.c (ge007.ini)  ·  watchsettings.c (per-profile values in the save)  ·  defaults.c (defaults.json)
```

`menu_ui.cpp` and `menu_model.cpp` build on their own: `tools_pc/menuview` runs them in a
window without the game (see [Iterating](#iterating)).

## `menu.json`

The schema is written at the top of the file (`_schema`); the essentials:

- **Tabs** hold **items**. `{"group": "Title"}` is a heading; `{"name", "sub", "show",
  "controls"}` is an entry. An entry with no visible control is hidden.
- **A control:** `id` (unique, what expressions name it), `key` (the config key it drives),
  `label`, `type`, and depending on the type `options`, `min` / `max` / `step`, `div` /
  `decimals` / `suffix` (slider text), `per` (config units per control unit), `default`,
  `help`, `show`, `disabled`, `disabledTag`, `dev`, `restart`.
- **Types:** `choice` (buttons side by side), `select` (a list that opens; long lists),
  `toggle`, `slider` (with `options`: one notch per option), `action`, `bind` (a key or mouse
  button, two slots), `diagram` (the pad layout drawing).
- **Options** are `{"id", "label", "value", "show"}`. `value` is what the config stores
  (default: the option's position). A bare string is both id and label.
- **Logic uses ids, never texts.** Expressions, defaults and per-value help compare option ids;
  labels are English source text, translated at draw time.
- **Expressions:** control ids, `'strings'`, numbers, `== != && || !` and parentheses. Extra
  names: `platform` (`'switch'` / `'pc'`), `dev`, `fileselect`, `stage`.
- **`help`:** one text, or one per option id (`"1"` / `"0"` for a toggle).
  Choose by what the values are. Values that differ in kind get one text each (`helpBy`), for example the gamepad mode, where the player must read what each value does before choosing. Values that explain themselves get one text for the option (`help`): the crosshair colour, the anisotropy level, an on/off flag; the text says what the option is for. Never a per-value text that repeats the label, nor several behaviours squeezed into one text.
- **`dev`:** hidden unless dev mode is on. `true` / `"all"`, or `"switch"` / `"pc"` for one
  platform only.

## Where a value lives

The `key` decides it:

| Key | Stored in | Resolved by |
|---|---|---|
| `Video.*`, `Input.*`, `Game.*`, `Audio.*` | `ge007.ini` (`config.c`) | the row table in `optionsoverlay.c` (`rowByKey`) |
| `Bond.*` | the selected save file (the game's watch settings, per profile) | `watchsettings.c` |
| `__Name` (value) | computed: `__DisplayMode`, `__Resolution`, `__ControlStyle`, `__BondFile`, `Audio.Device` | `optionsMenuGet` / `optionsMenuSet` |
| `__Name` (action) | — | `optionsMenuAction` (`__QuitToDesktop`, `__RestartGame`, `__UpdateCheck`, `__AssignDevices`, `__ResetBindings`…) |

Lists only the game knows (resolutions, audio devices) are filled at run time with
`menu::setOptionLabels`.

## Defaults and the ini

`port/src/defaults.json` is the single source of defaults: it seeds the first-run ini, and the
menu's **Reset to defaults** uses it. A control without its own `default` takes the one from
`defaults.json` (converted to the control's units). A value is the same on both platforms, or
`{"switch": n, "pc": n}`.

The file also carries the **ini version** (`Config.Version`) and **migrations**: steps
(`set` / `reset`, optionally `when` another key equals a value) applied once to an ini older
than their version. Keys removed from the game but still read once by a migration go in
`legacy`.

After editing `menu.json` or `defaults.json`:

```sh
python tools_pc/gen_menudata.py           # regenerates menu_data.cpp and defaults_data.c (no-op if unchanged)
python tools_pc/gen_menudata.py --check   # what CI runs: fails if a generated file is stale
```

The generator also validates: the JSON parses, every control has a unique id and a known type,
every migrated key has a default. `tests/test_defaults.c` checks the generated tables.

## Platform rules

- **The PC shows every setting**: machines differ. On the Switch a setting is tuned once,
  becomes the default, and is marked `"dev": "switch"`: the console has no performance rows
  (VSync, frame cap, Show FPS, anti-aliasing…).
- **Dev mode:** `Dev.Mode=1` in `ge007.ini` shows the controls marked `dev`.
- **No new option per fix:** changes to the original look or behaviour go under the existing
  switches (Visuals, Gameplay fixes), not a new row.

## Translations

`Menu.Language=fr` in the ini makes the menu load `lang/fr.json` (resolved like the other data
files): a flat JSON object, English text to translated text. A text with no entry shows as
written.

## Adding a setting

1. **The value:** register the variable in its owner (`configRegisterInt("Game.Foo", &foo, 0, 1)`
   or similar).
2. **The row:** add it to the `rows[]` table in `port/src/optionsoverlay.c`, with the same key
   and kind. The menu reads and writes values through that table: a key that is in `menu.json`
   but not in `rows[]` reads as absent and its control does nothing.
3. **The default:** add the key to `port/src/defaults.json` (one value, or Switch / PC).
4. **The control:** add it to an entry in `port/src/menu/menu.json`, with a short English help
   text (see the `help` rule above for one text or one per value).
5. Run `python tools_pc/gen_menudata.py`, check the page in menuview, then build.

If the setting replaces an older key or changes its meaning, bump `version` in `defaults.json`
and add a migration rather than leaving old inis with a stale value.

## Iterating

- **menuview** (Windows; needs `SEAVIEW_GXX` in `.env`): `python tools_pc/menuview/menuview.py`
  opens the menu alone. F5 reloads `menu.json` and the translations, F2 switches Switch / PC,
  F3 turns dev mode on or off. No game values: the viewer keeps its own.
- **In the game:** `GE_MENUGOTO=tab,entry[,focus]` opens the menu on that tab and entry at
  boot (indices, 0-based; focus -1 = the list), for screenshots. Pass it as an env token
  through [dev.py](dev-py.md) (`python tools/dev.py switch GE_MENUGOTO=1,2`).

## Traps

- The old overlay's row table is still load-bearing (step 2 above); its drawing and navigation
  code is not, so a layout change belongs in `menu_ui.cpp`.
- `menu_data.cpp` and `defaults_data.c` are generated and checked in. Edit the JSON, not them;
  CI fails when they disagree.
- Option `value`s are what the ini holds. Reordering options without `value`s changes what
  existing inis mean.
