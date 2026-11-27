# Room display lists

A room's primary and secondary display lists (level-decor.md) are arrays of
8-byte commands: word 0 then word 1, both big-endian. The **opcode** is the
top byte of word 0. The microcode is a Fast3D variant; the numbers below are
the opcode values as they appear in the files.

Bit ranges are written `w0[a..b]` (inclusive, bit 0 = least significant).

## Walking a list

Start at the first command and run them in order until `0xB8` (end of list).
Report, per room, how many bytes follow the first end-of-list (a few
padding bytes are normal: 176 bytes over all of Bunker 1, US)
and every opcode not described here (with a count): unknown opcodes are
skipped, never guessed. The **vertex cache** has 16 slots; triangle commands
refer to slots, not to vertices.

## Geometry

| Opcode | Command | Fields |
|---|---|---|
| `0x04` | load vertices | count = `w0[0..15] / 16`; first slot = `w0[16..19]`; source = `w1`: segment `0x0E` (top byte) + byte offset into the room's vertex block. Copies `count` consecutive vertices (16 bytes each, from offset / 16) into slots `first .. first+count-1`. |
| `0xBF` | one triangle | slots = `w1[16..23] / 10`, `w1[8..15] / 10`, `w1[0..7] / 10` |
| `0xB1` | up to four triangles | triangle k (k = 0..3) uses slots `w1[8k .. 8k+3]`, `w1[8k+4 .. 8k+7]`, `w0[4k .. 4k+3]`; a triangle whose three slots are all 0 is absent (padding) |
| `0x06` | call / jump to another list | `w0[16]` = 0 call, 1 jump; target `w1`. Room lists are not expected to use it: count and report, do not follow. |

Winding: the three slots in the order given are the triangle's vertices;
front faces are counter-clockwise in the game's coordinate system (same as
glTF), and the game culls back faces when the cull bit is set (below).

A triangle takes, from the state at the time it is issued: the current
texture (and its UV rule), the current combine and other modes, the cull
mode. Group triangles by that state; each group becomes one glTF primitive
of one material.

## State commands

| Opcode | Command | What to keep |
|---|---|---|
| `0xBB` | texture scale | s scale = `w1[16..31]`, t scale = `w1[0..15]` (0xFFFF means 1.0; scale = value / 65536); on = `w0[0..7]`. Affects the UV rule. |
| `0xB7` | set geometry mode bits | `w1` bits to set |
| `0xB6` | clear geometry mode bits | `w1` bits to clear |
| `0xBA` | set other modes, high word | shift = `w0[8..15]`, length = `w0[0..7]`, value = `w1`: replaces bits `shift .. shift+length-1` of the high other-mode word |
| `0xB9` | set other modes, low word | same, on the low other-mode word (holds the render mode / blender and alpha compare) |
| `0xFC` | set combine mode | keep `w0[0..23]` and `w1` raw |
| `0xFA` | set primitive colour | `w1` = RGBA |
| `0xFB` | set environment colour | `w1` = RGBA |
| `0xE7`, `0xE6`, `0xE8`, `0xE9` | syncs | ignore |
| `0xC0` | **texture reference** (GE; Fast3D's no-op opcode) | see below |

Geometry-mode bits used here: `0x00002000` = cull back faces, `0x00001000` =
cull front faces. A triangle with neither bit set is **double-sided**.

Low other-mode word bits used to classify transparency (the standard
Fast3D render-mode bits): `0x4000` force blend, `0x1000` coverage x alpha,
`0x2000` alpha coverage select, `w[0..1]` alpha compare (1 = threshold).

Material alpha mode, in this order:

1. force blend set (`0x4000`): **BLEND**;
2. else coverage x alpha (`0x1000`) or alpha compare = threshold: **MASK**
   (cutoff 0.5);
3. else **OPAQUE**.

In practice the primary list is opaque or masked and the secondary list is
blended; if a secondary-list group classifies as OPAQUE, keep it OPAQUE but
count it in the report.

## Texture reference (opcode `0xC0`)

The game never stores texture loads in room lists. Instead a command with
opcode `0xC0` names a texture by number, and the game expands it at load
time into the real load and tile commands. The opcode is `0xC0`, the
microcode's no-op. Fields:

| Field | Bits | Meaning |
|---|---|---|
| texture | `w1[0..11]` | texture number (textures.md) |
| second texture | `w1[12..23]` | for type 1 (detail): the detail texture |
| min level | `w1[24..31]` | for types 0 and 1 |
| type | `w0[0..2]` | 0 = level-of-detail, 1 = detail, 2 = mipmap, 3 = tile, 4 = tile (pre-swapped) |
| s mode | `w0[22..23]` | 0 = repeat, 1 = clamp, 2 = mirror |
| t mode | `w0[20..21]` | same |
| offset | `w0[18..19]` | 2 = half-texel offset (below) |
| s shift | `w0[14..17]` | types 0 and 1 |
| t shift | `w0[10..13]` | types 0 and 1 |

Every `0xC0` in a room list is a texture reference (the game treats them all
as such). After it, the current texture is
`texture`; the second texture is extra data (the detail layer: keep its
number in extras, do not blend it).

Two texture numbers are special in the game: 1508 and 1511 (water, Dam and
Complex) get extra animated effects at run time. Mark materials using them
`"anim": "water"` in extras.

## Combine mode: the parts that matter for a material

The combine command (`0xFC`) describes, per cycle, colour = `(a - b) * c + d`
and alpha = `(a - b) * c + d` from selectable inputs. Field positions (RDP
facts): word 0: `a0` colour `[20..23]`, `c0` colour `[15..19]`, `a0` alpha
`[12..14]`, `c0` alpha `[9..11]`, `a1` colour `[5..8]`, `c1` colour `[0..4]`;
word 1: `b0` colour `[28..31]`, `b1` colour `[24..27]`, `a1` alpha
`[21..23]`, `c1` alpha `[18..20]`, `d0` colour `[15..17]`, `b0` alpha
`[12..14]`, `d0` alpha `[9..11]`, `d1` colour `[6..8]`, `b1` alpha `[3..5]`,
`d1` alpha `[0..2]`. Input codes used by the room lists: 0 = combined (the
first cycle's result, in cycle 1), 1 = TEXEL0, 2 = TEXEL1, 3 = primitive
colour, 4 = shade (vertex colour), 5 = environment colour, 6 = one, and for
the `c` colour field 13 = LOD fraction; alpha `c` field 0 = LOD fraction;
the "zero" codes are 31/15 (colour) and 7 (alpha). In one-cycle mode
(other-mode high bits `[20..21]` = 0) only cycle 0 counts; in two-cycle mode
(= 1) cycle 1 takes cycle 0's result as "combined".

### Shade alpha becomes environment alpha in the secondary list

When the game loads a room, it rewrites some combine commands of the
**secondary** list (always, with or without fog): wherever the alpha
equation multiplies by shade alpha, environment alpha is used instead. The
rewritten modes are those whose alpha equation is one of: `TEXEL0 * SHADE`
(one cycle), `COMBINED * SHADE` (second cycle after a texture blend), or plain
`SHADE`. Example measured on Dam (US): almost every translucent material of
the secondary list has the alpha `COMBINED * SHADE` in cycle 1 and an
environment alpha between 30 and 255 set before it: that environment alpha is
the material's real opacity (a yellow-black pole at 38/255 is mostly
see-through), and the vertex alpha does not apply.

For the glTF: in the secondary list, when the (cycle 1, or the only cycle)
alpha equation uses shade alpha as described, put `environment alpha / 255`
into the material's `baseColorFactor[3]` and write the vertex alpha of
COLOR_0 as 1.0 for that primitive. The primary list is not rewritten for
alpha when the level has fog, and the primary-list materials measured so far
are opaque or cut-out; leave its alpha as is.

### Which texture is TEXEL0 and TEXEL1

For texture-reference types 0 (level-of-detail), 2 (mipmap), 3 and 4 (tile),
TEXEL0 and TEXEL1 are both the referenced texture (TEXEL1 at a smaller
level of detail). For **type 1 (detail)**, TEXEL0 is the **second texture**
(`w1[12..23]`, the detail layer) and TEXEL1 is the referenced texture
(`w1[0..11]`). The detail texture is sampled with the tile shifts of the
reference (`s shift`, `t shift`, rule below) and repeat wrapping.

A tile shift `k`: 0 = none; 1..10 = the coordinate is divided by `2^k`;
11..15 = multiplied by `2^(16-k)`.

The base colour texture of a material is the **referenced (main) texture**
for every type, including type 1; the detail texture's number goes to
extras as `detail` (with its shifts), it is not blended. (The port's own
renderer draws these materials with the main texture too: the detail texture
never appears in the game's capture. Measured: on Bunker 1 the four type-1
materials, textures 1198-1201 with detail 2465, are four large stacked
translucent layers in room 30, not a window.)

### Texture generation (reflections)

Geometry-mode bit `0x40000` turns on **texture generation** (with `0x80000`,
its linear variant): the texture coordinates of the vertices are not used;
the hardware computes them per vertex from the vertex **normal** at draw time
(a reflection that follows the view). While it is on, the four colour bytes
of the room's vertices hold a normal, not a colour: x, y, z as signed bytes
(-128..127, divide by 127) and the alpha byte as usual.

For the glTF, a texture-generated primitive gets: COLOR_0 = white (alpha
from the alpha byte), and TEXCOORD_0 computed once from the normal as
`u = 0.5 + 0.5 * nx`, `v = 0.5 - 0.5 * ny` (a fixed reflection, as seen from
the front), and extras `texgen: true` (plus `texgen_linear` when `0x80000`
is set) so a renderer that can do real reflections knows. Measured: Bunker
1, room 6, texture 1196 (54x54, intensity) is the round "eye" window; drawn
with its stored coordinates and the normals as colours, it came out black.

## UV rule

For a vertex with stored coordinates `s`, `t` (S10.5, so `s / 32` is in
texels), with the current texture of `W x H` texels and the current texture
scale `ks`, `kt`:

```
u = (s / 32 * ks - o) / W
v = (t / 32 * kt - o) / H
```

where `o` = 0.5 when the reference's offset field is 2 **and** the
texture's header bit 7 (explicit levels of detail) is clear, else 0. `v`
grows downwards, which is glTF's convention: no flip.

The level-of-detail types (0, 1, 2) also carry tile shifts used by the
smaller levels; level 0 is drawn without shift. The check against
`level.rec` is the authority for UVs: if a group's UVs differ by a constant
factor, report the type and shift values instead of correcting silently.

The sampler of the material: s mode / t mode → glTF `wrapS` / `wrapT`
(repeat 10497, clamp 33071, mirror 33648); filtering linear.

## Measured census

US ROM, Bunker 1, rooms 1..30, primary and secondary lists, up to the first
end-of-list: `0x04` 568, `0xB1` 1367, `0xBF` 44, `0xC0` 256, `0xBB` 113,
`0xB7` 58, `0xB6` 4, `0xB9` 93, `0xBA` 234, `0xFC` 91, `0xFB` 63, `0xE7`
116, `0xB8` 55. No `0x06` and no other opcode. Geometry mode: set `0x2000`
(cull back) 57 times, clear `0x3000` (both culls off) 3 times.
