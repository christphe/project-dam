# Level decor files

A level's visible geometry (its "decor", the static room meshes) lives in one
ROM file per level, named `bg_<name>_all_p` in the ROM file table (the file
table the converter already embeds: `port/convert/convert_data.c`,
`cv_regions[].files`, rows `{offset, size, basename}`; look the row up by
basename with `cv_find_row`). The file is **not** compressed as a whole; the
room blocks inside it are.

The collision file (`Tbg_<name>_all_p_stanZ`) is separate and out of scope
here.

## Header

The file starts with five 32-bit words:

| Word | Meaning |
|---|---|
| 0 | always 0 for the files we read (a non-zero value is a different layout: reject the file) |
| 1 | offset of the **room table** |
| 2 | offset of the portal table (not needed for the mesh) |
| 3 | offset of an environment table, or 0 (not needed) |
| 4 | 0 for every retail level (reject otherwise) |

Every "offset" in this file is a segment address: the top byte is the segment
number `0x0F`, the low 24 bits are the byte offset from the start of the
file. Mask with `0x00FFFFFF` to get the file offset.

## Room table

An array of 24-byte records. Record 0 is a null room and carries no data;
real rooms are numbered from 1.

| Bytes | Field |
|---|---|
| 0-3 | `vtx`: offset of the room's compressed **vertex block**, or 0 |
| 4-7 | `pri`: offset of the room's compressed **primary display list**, or 0 |
| 8-11 | `sec`: offset of the room's compressed **secondary display list**, or 0 |
| 12-23 | room position: three big-endian IEEE 754 floats x, y, z |

Scan from index 1 until the first record whose `pri` is 0; call the number of
records scanned before it `n`. Record `n` (the last one with a non-zero
`pri`) is an **end marker**, not a room: its offsets point at the end of the
room data (on Bunker 1, US: all three equal the end of the file area that
holds blocks, and decompressing there fails). The real rooms are **1 .. n-1**.
Measured on the US ROM, Bunker 1: n = 31, rooms 1..30, every one of their
blocks decompresses to a complete DEFLATE stream. The end marker's offsets
are still used by the "next block" rule below.

### Block sizes

The compressed size of a block is not stored. It is the distance to the next
block in file order:

- **vertex block of room i**: from its `vtx` to the `vtx` of the next room
  (i+1, i+2, ...) whose `vtx` is non-zero, the terminating record included.
- **primary list of room i**: from its `pri` to the nearest following block
  start, which is either the `pri` of the next room with a non-zero `pri`, or
  the `sec` of room i itself (or of a following room) if that comes first.
  Concretely: take the first non-zero `pri` among rooms i+1.. and the first
  non-zero `sec` among rooms i.. ; the block ends at whichever of the two
  belongs to the lower room index (on a tie, the `pri` wins).
- **secondary list of room i**: same rule starting from its `sec`: the first
  non-zero `pri` among rooms i+1.. and the first non-zero `sec` among rooms
  i+1.. ; the lower room index wins (tie: `pri`).

In practice the blocks of one room are laid out vertices, primary,
secondary, then the next room. A simpler and equivalent approach for an
exporter that does not need exact sizes: decompress each block from its
start offset to the end of the file; the stream is self-terminating (see
below).

The game reads every block rounded up to 16 bytes; the extra bytes are never
used by the decompressor.

## Block compression ("rarezip")

Every room block is: two header bytes (`0x11 0x72` in practice; the game does
not depend on their value), then a **raw DEFLATE stream** (RFC 1951, no zlib
header, no checksum). `cv_rz_inflate` in `port/convert/conv_common.c` already
decodes exactly this. The uncompressed size is not stored; the DEFLATE stream
ends by itself.

## Vertex block

After decompression: an array of 16-byte vertices.

| Bytes | Field |
|---|---|
| 0-5 | position: three signed 16-bit integers x, y, z (model space of the room) |
| 6-7 | flag (unused here) |
| 8-11 | texture coordinates s, t: signed 16-bit, fixed point with 5 fractional bits (S10.5) |
| 12-15 | vertex colour r, g, b, a (unsigned bytes) |

The colour is the baked lighting of the level (rooms are drawn unlit, with
this colour as the shade). Vertex positions are addressed by the display
lists as segment `0x0E` + byte offset into this array (see
display-lists.md).

## Display-list blocks

After decompression: an array of 8-byte display-list commands (two
big-endian 32-bit words each), described in display-lists.md. The primary
list holds the opaque and cut-out geometry; the secondary list holds the
translucent geometry (glass, water, light glows, decals). Both are drawn
with the same vertex block.

Every list contains GE's texture-reference command (display-lists.md
§Texture reference): the game replaces it at load time by the real texture
loads. An exporter reads it directly.

At load time the game also swaps a few combine-mode commands for variants
(fog on / off; in the secondary list, shade alpha becomes environment alpha).
The exporter keeps the commands **as stored in the file** and records them
raw.

## World space

A room's vertex in world units (the units of the setup pads and of the
collision file) is:

```
world = (vertex + room_position) / level_scale
```

where `level_scale` is the per-level constant below. The axes are the game's:
Y up, right-handed, the same as glTF's.

## Which file is which level

Level scale per decor file (facts from the level table; some files serve
several levels, and then the scale is the same for all of them):

| Decor file | Level(s) | level_scale |
|---|---|---|
| `bg_dam_all_p` | Dam | 0.23363999 |
| `bg_ark_all_p` | Facility | 1.20648 |
| `bg_run_all_p` | Runway | 0.089571431 |
| `bg_sevx_all_p` | Surface 1, Surface 2 | 0.45445713 |
| `bg_sev_all_p` | Bunker 1 | 0.53931433 |
| `bg_silo_all_p` | Silo | 0.47256002 |
| `bg_dest_all_p` | Frigate | 0.44757429 |
| `bg_sevb_all_p` | Bunker 2 | 0.53931433 |
| `bg_stat_all_p` | Statue | 0.107202865 |
| `bg_arch_all_p` | Archives | 0.50678575 |
| `bg_pete_all_p` | Streets | 0.34187999 |
| `bg_depo_all_p` | Depot | 0.21847887 |
| `bg_tra_all_p` | Train | 0.15019713 |
| `bg_jun_all_p` | Jungle | 0.094662853 |
| `bg_arec_all_p` | Control | 0.49886572 |
| `bg_cave_all_p` | Caverns | 0.26824287 |
| `bg_crad_all_p` | Cradle | 0.23571429 |
| `bg_azt_all_p` | Aztec | 0.35300568 |
| `bg_cryp_all_p` | Egyptian | 0.25608 |
| `bg_len_all_p` | Cuba | 0.094662853 |
| `bg_dish_all_p` | Temple (multiplayer) | 0.47142857 |
| `bg_ref_all_p` | Complex (multiplayer) | 0.94285715 |
| `bg_cat_all_p` | Citadel (multiplayer) | 0.76852286 |
| `bg_ame_all_p` | Basement, Stack, Library (multiplayer) | 0.65999997 |
| `bg_oat_all_p` | Caves (multiplayer) | 0.14142857 |
| `bg_sho_all_p` | unused level "sho" | 0.528 |
| `bg_eld_all_p`, `bg_lue_all_p`, `bg_rit_all_p`, `bg_ear_all_p`, `bg_lee_all_p`, `bg_lip_all_p`, `bg_wax_all_p`, `bg_pam_all_p` | unused / test levels | 0.94285715 |

Any other `bg_*_all_p` file in the table (`bg_imp_all_p`, ...) has no level
entry: export it with scale 1.0 and say so in its metadata.

Output names: the campaign levels use the lower-case names the editor
already uses (`dam`, `facility`, `runway`, `surface1`, `bunker1`, `silo`,
`frigate`, `surface2`, `bunker2`, `statue`, `archives`, `streets`, `depot`,
`train`, `jungle`, `control`, `caverns`, `cradle`, `aztec`, `egypt`,
`cuba`). A file shared by several levels is exported once per level name
(Surface 1 and 2 are the same file: export it under both names), the
multiplayer maps as `temple`, `complex`, `citadel`, `basement`, `stack`,
`library`, `caves`, and the rest under the file's short name (`sho`, `eld`,
...).
