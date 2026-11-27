# Format specs (clean room)

These pages describe GoldenEye's ROM data formats in prose, as facts: byte
layouts, bit fields, algorithms and constants. They are written from the
decompilation by the lead, in the lead's own words. Code that must stay free
of game code (`port/export/`, issue #150) is written **from these pages
only**: its authors do not open `src/game/`, `include/`, `PR/gbi.h` or any
tool derived from them.

Rules for these pages:

- Describe behaviour and layout; never paste or paraphrase decompiled code
  line by line. Numbers (offsets, bit positions, constants, tables of facts)
  are fine.
- When a value was measured on a ROM rather than read from the
  decompilation, say so and say which ROM (region).
- All multi-byte ROM values are big-endian unless stated.

| Page | Covers |
|---|---|
| [level-decor.md](level-decor.md) | A level's decor file: header, room table, compressed room blocks, vertices, room positions, level scale, which file belongs to which level |
| [textures.md](textures.md) | The texture bank: where the size table and the data are, the per-texture header, the two decompression families, the 13 pixel formats |
| [display-lists.md](display-lists.md) | The display-list commands the room blocks use, including GE's texture-reference command, and how texture coordinates map to UVs |
| [characters.md](characters.md) | Guard bodies and heads: the body/head table, how a head attaches, the shared skeleton and its half-rotation matrices, matrix skinning, the rest and bind poses, placement scale and `pov`, the animation table and frame / root-motion bit format, the skinned glTF export and its check against `level.rec` |
| [models.md](models.md) | Object models (doors, crates, consoles, vehicles, floor weapons, glass): the model-number table, the model file's nodes and records, matrices and the rest pose, render types, placement scale, the glTF export and its check against `level.rec` |
| [stan.md](stan.md) | A level's collision floor ("stan"): tiles, points, links to neighbours, room, kind and colour, world space |
| [text.md](text.md) | Game text: the language banks per ROM (E / P / J), string ids (bank and index), the bank layout, ASCII and the Japanese two-byte glyph codes and fonts, the item, mission, briefing and multiplayer name tables in cdata, and the `text_<lang>.json` / `names.json` export |
| [setup.md](setup.md) | A level's stage setup (`Usetup*Z` / `Ump_setup*Z`): which file belongs to which level and mode, the header, pads and bound pads (tile names, units), every propDef record type with its fields (mission field names), relative links, objectives, AI list bytes and the opcode length table, waypoints and patrol paths, the intro, regional differences, the recommended `setup.json` and its check against `level.json` |
