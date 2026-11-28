# Asset packs (`.passets`)

An asset pack replaces or adds assets of the game: textures today, and models,
music (MIDI with SoundFonts), sounds, text labels and fonts as the engine learns
to read each kind. The player enables any number of packs and orders them; the
active mod sits under every pack and always wins. Code:
`port/assets/ge_assetstack.{h,c}` (the stack, shared by the game and the tools),
`port/src/texpack.c` (the game's textures). Tests: `tests/test_assetstack.c`.

## Layout

A pack is a folder `X.passets/` or an uncompressed ustar tar `X.passets` (the
`.pmod` format). It holds only the kinds it provides:

| Path | Kind |
|---|---|
| `pack.json` | optional: `name`, `author`, `description`, `base` (see below) |
| `textures.json`, `textures/` | textures: [texture-packs.md](texture-packs.md) (entries, keys, fields) |
| `models.json`, `models/` | models: reserved, specified with the engine's model reader |
| `music.json`, `music/` | music (MIDI + SF2): reserved |
| `sounds.json`, `sounds/` | sounds: reserved |
| `labels.json` | text labels: reserved |
| `fonts.json`, `fonts/` | fonts: [fonts.md](fonts.md) (keys `small`, `large`, `jp_narrow`, `jp_wide`) |

A `.ptexpack` (the older name) is read as a pack with textures only.

## Where packs are found

`assetpacks/` next to the game's `data/` folder (on the Switch
`sdmc:/switch/project-dam/assetpacks/`), then the older `texturepacks/`. A file
name found in both is taken from `assetpacks/`.

## The stack

`Video.AssetPacks` holds the enabled packs: their file names, highest priority
first, separated by `;` (`hd.passets;faces.passets`). An older
`Video.TexturePack` (one pack) becomes this list once. A pack named in the list
but not found keeps its place and is skipped until it is back.

Resolution of a key of any kind:

1. the active mod, if it has the key: **it always wins** (a mod that ships an
   asset needs it replaced);
2. else the first enabled pack, in the player's order, that has it;
3. else the key is missing. While the game still draws from the ROM, a missing
   texture is the game's own; once it draws from the export, each kind gets a
   visible fallback (a checker texture, a box model, silence, a default font)
   and a log line.

- **Every pack may be partial**, the base too. No pack is required: a mod that
  reuses the game's assets depends on keys, not on a pack, and nothing is
  declared or checked.
- **One mod at a time.** The GoldenEye campaign is a mod. There are no mods of
  mods: a mod is patched by extracting it and changing it.
- **Keys.** The game's keys have a reserved form per kind (texture numbers,
  `C…Z` character files, …). An asset a mod or a pack adds takes any other name;
  with one mod active, names do not need a prefix or a registry.

## The base pack

The export of the player's own ROM (`ge007`) is an ordinary pack: complete,
marked `"base": true` in its `pack.json`, enabled by default once it exists, and
it can be disabled. It is written on the player's machine and never shipped.
Assets derived from the game (upscaled textures, reworked models) are not
redistributed either: a tool builds them on the player's machine from that
export.

## The base pack layout

`ge007.passets` is written by `ex_export_pack` (`port/export/pack_export.c`):
by the game's first launch into `assetpacks/`, by `ge007-export <rom> <out>`
into `<out>/` (`--folder` writes a folder instead of the tar), and by
`--export-levels <out>`. It is an uncompressed tar, read lazily.

| Path | Content |
|---|---|
| `pack.json` | `name`, `author`, `description`, `"base": true`, `region`, `exporter_version` |
| `textures.json`, `textures/<n>.png` | every texture of the ROM ([texture-packs.md](texture-packs.md)) |
| `levels/<level>/<level>.gltf` + `.bin` | each level's decor, with its collision and setup files beside it |
| `models/<name>/<name>.gltf` + `.bin` | the object models |
| `characters/characters.json`, `characters/<name>/`, `characters/animations/` | bodies, heads and animations ([characters.md](characters.md)) |
| `labels/` | the game's text per language, and the names tables ([text.md](text.md)) |
| `fonts.json`, `fonts/<key>.png` | the four fonts: 8-bit greyscale atlases (Latin intensities as stored, Japanese level x 85) and their metrics, kerning and code map ([fonts.md](fonts.md)) |
| `music/track_NN.mid`, `music/instruments.sf2` | the music as Standard MIDI files and its SoundFont ([audio.md](audio.md)) |
| `sounds/sfx/sfx_NNNN.wav`, `sounds/sfx/sfx.json`, `sounds/report.txt` | the sound effects and their parameters |

Every glTF sits two folders under the root and names its pictures
`../../textures/<n>.png`. The pack is rewritten when its `exporter_version`
differs from the running exporter's; it is built in `ge007.passets.tmp/`, packed
into `ge007.passets.tartmp` and renamed into place, so a killed run leaves no
half pack.

The game's first launch enables the pack (last in the order) only when it
writes it for the first time; a rewrite after an exporter update keeps the
player's choice. The older `texturepacks/ge007.ptexpack` is removed once the
new pack is written, and a `ge007.ptexpack` in `Video.AssetPacks` becomes
`ge007.passets`.

## The menu

Video > Asset packs: one row per pack found, the enabled packs first, numbered in
their order (1 wins over 2), then the others. A row shows the title (pack.json
"name", else the file name) with the author faded under it (no line when there
is none), a checkbox that enables the pack (it goes last) or disables it, and
up / down arrows that move it (disabled for a disabled pack and at the ends of
the enabled ones). With no pack the menu says so. On a pad Up / Down move between
rows, Left / Right pick the checkbox or an arrow, A does it. These edits only
change a pending list, the rows show it, and nothing is applied or saved. **Apply** (disabled while the pending list equals the one in use) gives
the list to the readers once, which reload, and saves the config once, also
during a mission. **Discard changes** (same rule) resets the pending list to the
one in use. Leaving the menu drops pending edits silently, without asking.
