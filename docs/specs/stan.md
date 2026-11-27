# Collision floor ("stan") files

Each level has a collision file, `Tbg_<name>_all_p_stanZ` in the ROM file
table (the same `<name>` as its decor file, level-decor.md). It describes
the **walkable floor** as a set of convex polygons called **tiles**: where a
character can stand, at which height, and which tile continues across each
edge. There are no wall polygons: an edge without a neighbour **is** a wall.

## Compression

The whole file is one rarezip block (level-decor.md §Block compression: two
header bytes `0x11 0x72`, then raw DEFLATE). Everything below describes the
decompressed bytes. All values big-endian.

## Layout

| Offset | Content |
|---|---|
| 0 | a 32-bit word (0 in the files measured; keep it raw) |
| 4 | a table of 32-bit offsets, ending with a 0 word |
| after the 0 word | the **tiles**, one after the other, ending with a 0 word where the next tile would start |

The table entries are byte offsets from the start of the decompressed file;
the **first entry is the offset of the first tile** (it equals the position
right after the table's 0 word). Measured on Bunker 1 (US): the table has a
single entry, 12. Do not assume one entry per room: every tile names its
own room. Keep any further entries raw.

## A tile

| Bytes | Field |
|---|---|
| 0-2 | tile name (24 bits, an authoring id; keep it raw) |
| 3 | room number (the decor room the tile belongs to, as in level-decor.md) |
| 4-5 | bits 15-12: **kind** (0 = normal floor; other values mark special floors, e.g. crouch-only or ladder; keep raw); bits 11-8, 7-4, 3-0: a colour **r, g, b**, 4 bits each (the light the floor casts on characters standing on it) |
| 6-7 | bits 15-12: **point count** `n` (3..10); bits 11-8, 7-4, 3-0: three point indices (the tile's most extreme points, used by the game for quick height estimates; keep raw) |
| 8 ... | `n` points of 8 bytes: x, y, z (signed 16-bit), then a 16-bit **link** |

The size of a tile in bytes is not `8 + 8n` for small tiles: it comes from a
fixed table indexed by `n`: n = 0..3 → 32 bytes, 4 → 40, 5 → 48, 6 → 56,
7 → 64, 8 → 72, 9 → 80, 10 → 88 (the size is `8 + 8n` with a minimum of 32;
real tiles have 3 points or more). The next tile starts right after.

The points go around the tile in order. **Edge `i` runs from point `i` to
point `(i + 1) mod n`, and its link is stored with point `i`.**

### Links (neighbours)

A link value `L` names the tile on the other side of that edge:

- if `L >> 4` is 0 (L < 16), the edge has **no neighbour**: it is a wall (or
  a drop);
- otherwise the neighbour tile starts at byte offset
  `first_tile_offset - 0x80 + L * 8` (`first_tile_offset` = the first entry
  of the room table). Every tile start is a multiple of 8 from there, which is
  why the offset fits in 16 bits as "units of 8 bytes".

A link can cross into another room (that is how the floor continues through
a portal).

## World space

A point's world position (same units as the setup pads and the decor glTF,
level-decor.md §World space) is `point / level_scale`, with the per-level
scale of level-decor.md. The y of the points is the floor height; the
height inside a tile is interpolated from its points.

Measured on Bunker 1 (US): 1066 tiles (kind 0 for all but one tile of kind
2), every link lands on a tile start, the tile list ends 68 bytes before the
end of the decompressed data (padding), and `point / 0.53931433` gives the
game's capture coordinates exactly.

## Check

The game's own capture (`level.json`, `"stan"`: one polygon per tile, in
world units, with its room) must match the export tile for tile: same count
per room, same points.
