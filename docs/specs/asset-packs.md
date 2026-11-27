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
| `fonts.json`, `fonts/` | fonts: reserved |

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

## The menu

Video > Asset packs: **Pack** lists the enabled packs first, numbered in their
order (1 wins over 2), then the others; **Enabled** turns the highlighted pack
on (it goes last) or off; **Move up** and **Move down** change its place. A
change shows at once, also during a mission.
