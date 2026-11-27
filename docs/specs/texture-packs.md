# Texture packs and the game's base pack

A **texture pack** is a complete set of the game's textures: one picture per
texture and one material entry per texture. The game's own textures are a
texture pack too, **`ge007.ptexpack`**, written from the player's ROM by the
exporter. Every other pack is a full replacement of it (usually made by
copying it), and a **mod** carries a partial set on top of the chosen pack.

This page is the format. The texture bank it is read from is
[textures.md](textures.md); the glTF exports that point into it are
[level-decor.md](level-decor.md), [models.md](models.md) and
[characters.md](characters.md).

## Layout

A pack is a folder `X.ptexpack/` or an uncompressed ustar tar `X.ptexpack`
with the same content (the `.pmod` package format). Paths inside are
relative to the pack's root and use `/`.

| Path | Content |
|---|---|
| `pack.json` | required: who made it, see below |
| `textures.json` | required: one entry per texture, see below |
| `textures/<key>.png` | the picture of texture `<key>` (the default `image` path) |

A mod (`X.pmod`) holds the same `textures.json` and `textures/` at its own
root, both optional, plus its campaign files.

### pack.json

```json
{
  "name": "GoldenEye 007",
  "author": "Rare",
  "description": "The game's own textures, exported from your ROM. GoldenEye 007 (c) 1997 Nintendo / Rare.",
  "base": true,
  "region": "ntsc-final",
  "exporter_version": 11
}
```

`name`, `author` and `description` are free text shown in pack lists.
`base`, `region` and `exporter_version` are written only into
`ge007.ptexpack`: `base: true` marks the game's own pack, `region` is the
ROM it came from, `exporter_version` the exporter that wrote it (a reader
re-exports when it differs from its own). Other packs leave them out.

### textures.json

```json
{
  "version": 1,
  "textures": {
    "0":   { "image": "textures/0.png", "surface": "stone", "impact_sound": "stone", "alpha": "image",
             "format": "rgba16", "width": 32, "height": 32,
             "detail": { "s_offset": 0, "t_offset": 0, "s_shift": 0, "t_shift": 0 } },
    "412": { "image": "textures/412.png", "surface": "metal", "impact_sound": "metal", "alpha": "luminance",
             "format": "i8", "width": 64, "height": 32,
             "detail": { "s_offset": 0, "t_offset": 0, "s_shift": 0, "t_shift": 0 } }
  }
}
```

`version` is the format version of this page (1). `textures` maps a
**texture key** to its entry.

## Texture keys

- The game's textures are keyed by their texture number written in decimal
  without leading zeros: `"0"` .. `"2697"` (the 2698 entries of the size
  table, [textures.md](textures.md)).
- A mod may add textures under any other key: a non-empty string of
  `a-z 0-9 _ - . /`, at most 63 bytes, that is not a decimal number. Prefix
  it with the mod's own name (`"mymod/rusty_door"`) so two mods do not
  collide. A key with `/` puts its default picture in a subfolder:
  `textures/mymod/rusty_door.png`.

## Entry fields

| Field | Type | Meaning |
|---|---|---|
| `image` | string or null | the picture, a PNG path relative to the pack (or mod) root. Default `textures/<key>.png`. `null`: the texture has no picture (it could not be decoded) |
| `surface` | name | what the surface is made of: decides the bullet impact effect and the material of a hit. One of the surface names below |
| `impact_sound` | name | the sound class of a shot hitting it. Same names |
| `alpha` | `"image"` or `"luminance"` | where the drawn alpha comes from: the PNG's own alpha channel, or, when the PNG is opaque, its luminance (`(299 R + 587 G + 114 B) / 1000`, truncated). See below |
| `format` | name | informational: the texture's pixel format in the ROM (below). Readers never depend on it |
| `width`, `height` | integers | informational: the size of level 0 in the ROM |
| `tile_width`, `tile_height` | integers | the size the game holds the texture at, when it differs from `width` x `height` (absent otherwise). The picture covers the top-left `width` x `height` of that tile; the rest is padding. See below |
| `detail` | object | informational: the four detail-layout fields of the size table, raw (below) |
| `normal` | string | reserved: a tangent-space normal map, PNG, glTF convention (+Y green). Readers that do not draw normal maps ignore it |
| `roughness`, `metallic`, `emissive` | — | reserved for later material fields; readers ignore them |

A reader ignores fields it does not know, so a pack can carry fields of a
later version.

### Surface names

The `surface` and `impact_sound` values are the game's 13 hit classes, by
number:

| # | Name | | # | Name |
|---|---|---|---|---|
| 0 | `default` | | 7 | `dirt` |
| 1 | `stone` | | 8 | `mud` |
| 2 | `wood` | | 9 | `tile` |
| 3 | `metal` | | 10 | `metalobj` |
| 4 | `glass` | | 11 | `chr` |
| 5 | `water` | | 12 | `glass_xlu` |
| 6 | `snow` | | | |

Classes 13-15 do not occur in the ROM; a base export that finds one writes
`"class13"` .. `"class15"` and reports it.

### Where the base values come from

From word 0 of the texture's size-table entry ([textures.md](textures.md),
§The size table), big-endian:

- bits 31-28: `impact_sound`;
- bits 27-24: `surface`.

From word 1, the `detail` fields (four 4-bit fields, raw):

- bits 31-28: `s_offset` (the detail layer's start column in texture memory);
- bits 27-24: `t_offset` (its start row, counted in lines of the texture);
- bits 23-20: `s_shift` (how many powers of two the detail layer's width mask is below the texture's);
- bits 19-16: `t_shift` (the same for the height mask).

These describe how the game lays the detail layer out in texture memory.
They are not a material: a pack copies them unchanged.

`format`, `width` and `height` come from the texture's header
([textures.md](textures.md), §One texture). Format names by number:

| # | Name | # | Name | # | Name |
|---|---|---|---|---|---|
| 0 | `rgba32` | 5 | `ia8` | 10 | `ci4_rgba16` |
| 1 | `rgba16` | 6 | `ia4` | 11 | `ci8_ia16` |
| 2 | `rgb24` | 7 | `i8` | 12 | `ci4_ia16` |
| 3 | `rgb15` | 8 | `i4` | | |
| 4 | `ia16` | 9 | `ci8_rgba16` | | |

### tile_width and tile_height

The game holds a texture in texture memory in whole lines of 8 bytes, and
a texture of one line on two lines. So the tile a texture is drawn from can
be larger than its picture:

- `tile_width` = `width` rounded up so that `tile_width` x (bits per
  texel) is a multiple of 64. Bits per texel by format number: 16 for 0
  to 4, 8 for 5, 7, 9 and 11, 4 for 6, 8, 10 and 12. The 32-bit formats (0
  and 2) count as 16: texture memory holds a 32-bit texel as two 16-bit
  halves, so their width is padded to a multiple of 4 texels.
- `tile_height` = 2 when `height` is 1, else `height`.

The base export writes both fields only when one of them differs. Measured
on the US ROM against the game's own capture of the 20 solo levels
(`level.rec`): of 1005 textures drawn, 93 are held padded and these two
rules give the held size of all 93 (for example `ci8_rgba16` 33x33 held as
40x33, `ia8` 125x23 as 128x23, `ci8_ia16` 64x1 as 64x2). The formats seen
padded were `ci8_rgba16`, `ci8_ia16`, `ci4_rgba16`, `ia8` and `i8`. The
game's renderer was also probed on the front-end and pause screens: the
menu portrait textures (`i4` / `ia4` 65x65) are held as 80x65 and an
`rgba32` 5x12 icon as 8x12, both as these rules give. The remaining formats
follow from their texel size.

A reader drawing a replacement picture where the game draws the tile maps
the picture onto the top-left `width` x `height` part of the tile, at the
picture's own resolution (a picture twice as large covers a tile twice as
large), and treats the rest as padding. A pack copies these fields from the
base unchanged; a new mod texture has none.

### alpha

The hardware reads an intensity texture's alpha from its intensity (I8, I4)
and an intensity-alpha texture's alpha from its own channel; a replacement
picture drawn where the game expects an intensity texture keeps that look
only if its alpha follows the same rule. The base pack writes:

- `"luminance"` for formats 4-8 (`ia16`, `ia8`, `ia4`, `i8`, `i4`);
- `"image"` for every other format.

The base pictures themselves already carry the decoded alpha
([textures.md](textures.md): I8 and I4 have A = I), so both values draw a
base picture the same way. The field matters for a replacement: an opaque
PNG under `"luminance"` gets its luminance as alpha; a PNG with any
non-opaque pixel, or under `"image"`, keeps its own alpha.

## Resolving a texture

The sources, first wins: **the active mod, then the chosen pack**. There is
always exactly one chosen pack; `ge007.ptexpack` is the default.

- **Packs are complete.** A pack is usable only when its `textures.json`
  has an entry for every key of `ge007.ptexpack`'s `textures.json` and each
  entry whose base `image` is not null names a picture that exists in the
  pack. A reader that finds a pack incomplete refuses it and says how many
  textures are missing; it never mixes two packs.
- **Mods are partial.** A mod entry is merged **field by field** over the
  chosen pack's entry with the same key: a mod entry `{"surface": "metal"}`
  changes only the surface, and the picture stays the pack's. A mod
  `textures/<key>.png` with no `textures.json` entry replaces the picture
  only (packs made before this format did that). A mod key the pack does
  not have is a new texture: its entry must give at least `image` (or have
  the default picture present) and `surface`.
- A path in `image` is resolved in the source that gave the field: a mod's
  `image` is read from the mod, a pack's from the pack.
- A mod's picture is the file its entry's `image` names, else its default
  `textures/<key>.png` when present; either replaces the pack's picture. A
  mod `image` naming a file the mod does not hold is reported and ignored
  (the pack's picture stays). A mod cannot remove a picture: `"image": null`
  in a mod is ignored.
- A new mod key without `impact_sound` uses its `surface`.
- A pack without `textures.json` (made before this format) is incomplete.
  Completeness needs `ge007.ptexpack`: with no base pack at hand, no other
  pack can be checked and none is usable.

The pack's own key set is fixed by `ge007.ptexpack`; only a mod adds keys.

## The base export

The exporter writes `ge007.ptexpack/` as a folder:

- `pack.json` as above;
- `textures/<n>.png` for every texture number whose level 0 decodes, 8-bit
  RGBA, row 0 at the top ([textures.md](textures.md), §What the exporter
  outputs);
- `textures.json` with all 2698 keys, in increasing number. A texture that
  does not decode gets `"image": null` and the fields of its size-table
  entry; its header fields are left out.

It is written into every export directory, at the root next to the level
folders, `models/`, `text/` and `characters/`:

```
<out>/ge007.ptexpack/pack.json, textures.json, textures/<n>.png
<out>/<level>/<level>.gltf
<out>/models/<name>/<name>.gltf
<out>/characters/<name>/<name>.gltf
```

The game also writes it to `<game>/texturepacks/ge007.ptexpack/` at start
when it is missing or its `exporter_version` differs, so that it shows up
as the default pack.

### How the glTF exports point into it

The glTF files carry no texture pictures of their own. Each glTF image is
the pack's picture, by a relative URI from the glTF's folder:
`../ge007.ptexpack/textures/<n>.png` for a level, `../../ge007.ptexpack/textures/<n>.png`
for a model or a character. Each material that has a texture carries
`extras.texture` = its texture key (`"412"`). A standard glTF viewer finds
the base picture through the URI; a reader that knows packs resolves the
key (§Resolving a texture) and uses the URI only when no pack is open.
