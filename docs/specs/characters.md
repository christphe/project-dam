# Characters (guard bodies, heads, skeleton, animations)

Every guard, ally and villain is drawn from a **body** file and, for most
bodies, a separately loaded **head** file, posed by a shared **skeleton** and
the game's **animation** data. This page says where the files, the
body/head table, the skeleton and the animations are, how a character file
differs from an object model, how a frame of animation becomes joint
transforms, how a placed guard is sized, and what a glTF export of it should
contain.

It extends [models.md](models.md) and does not repeat it: the container (part
table, texture table, 24-byte nodes, segment-5 pointers), the node kinds and
their records, the vertex format, the display-list commands and the material
rules are the ones described there. Shared conventions come from
[level-decor.md](level-decor.md) (rarezip, the ROM file table, world units),
[textures.md](textures.md) and [display-lists.md](display-lists.md).

Unless a line says otherwise, every value below was **measured on the US ROM**
(`ntsc-final`) with scratch scripts that read the ROM directly. The
character files, the animation data and the table contents are
byte-identical in US, EU and JP; only positions in the ROM and in cdata
differ, given per region where they matter.

## Where the files are

### The files

One ROM file per body or head, named `C<name>Z` in the ROM file table (look
the row up by basename as models.md does): `CcamguardZ`, `CheadkarlZ`, ...
Each file is **one rarezip block** (two header bytes `0x11 0x72`, then raw
DEFLATE). Measured: all 80 files start with `0x11 0x72` and decompress
completely. Together they are 1 263 936 bytes decompressed (SHA-1 of the 80
decompressed files concatenated in table order:
`90777e98f4c0341a4c2a41e0ace557673aa4eeab`, the same in US, EU and JP).

As for object models, enumerate the **character table** below, not the
file names: it gives each file's name and the counts needed to read it.

### The character table (body and head ids)

A **body id** (the guard record's body number in a setup, the `model` of a
`guard N` owner in `level.rec`, the editor's body id) and a **head id** (the
guard record's head number) are both indexes into **one** table in the
decompressed cdata block (the cdata of textures.md / models.md).

| Value | US | EU | JP |
|---|---|---|---|
| cdata base address (subtract from cdata pointers) | `0x80020D90` | `0x8001EC50` | `0x80020DD0` |
| character table, offset in decompressed cdata | `0x1D080` | `0x18E10` | `0x1D070` |
| guard skeleton record, offset in decompressed cdata | `0x1C670` | `0x18540` | `0x1C660` |
| entries before the terminator | 80 | 80 | 80 |

Entry, **20 bytes** (all regions):

| Bytes | Field |
|---|---|
| 0-3 | pointer to the file's **header** (cdata address; same 32-byte US/JP, 28-byte EU header as models.md, same field offsets) |
| 4-7 | pointer to the file name, zero-terminated ASCII in cdata (`"CcamguardZ"`) |
| 8-11 | **body scale**, float (1.0 for every body except `CspicebondZ` 0.95 and `Csuit_lf_handZ` 0.1) |
| 12-15 | **pov** (the animation translation scale), float |
| 16 | `is_male` (1 / 0; which pool a random head is drawn from) |
| 17 | `has_head`: 1 = the body's own mesh includes a head, no head file is attached |
| 18-19 | 0 |

The terminator is an entry with header pointer 0, name pointer 0, scale 1.0.
The headers are **not** in table order (`CdjbondZ`'s header lies after the
Bond bodies'): always follow the pointer.

Header fields used here (models.md §The model-number table): skeleton
pointer at 4 (the guard skeleton for every body, a separate hand skeleton for
`Csuit_lf_handZ`, **0 for every head**), `nparts` at 12 (7 for bodies, 2 for
heads, 10 for the hand), `nmatrices` at 14 (20 or 21 for bodies, 1 for every
head, 9 for the hand), bounding radius at 16, `ntextures` at 22.

**Classify by the skeleton pointer, not by the id.** Ids 0-40 are bodies, 41
is the first-person hand, 42-78 are heads, and **id 79 (`CspicebondZ`,
Natalya in jungle fatigues) is a body** although it sits after the heads (the
editor's `kFirstHeadId = 42` boundary does not hold for it).

Invariants to check (all three regions): 80 entries then the terminator;
entry 0 `CcamguardZ`, 41 `Csuit_lf_handZ`, 42 `CheadkarlZ`, 78
`CheadbrosnanZ`, 79 `CspicebondZ`; every name starts with `C`, ends with `Z`
and has a row in the ROM file table; 42 entries have a non-zero skeleton
pointer that is the same address (the guard skeleton) except entry 41; 14
bodies have `has_head` = 1. How the table was found (for another region):
the address of the string `CcamguardZ` occurs exactly once in cdata as a
word; the table starts 4 bytes before it. On a mismatch the exporter
refuses the characters and says why.

`has_head` = 1 (built-in head, no head file): 6 `Cboris`, 7 `Corumov`, 8
`Ctrevelyan`, 9 `Cboilertrev`, 10 `Cvalentin`, 11 `Cxenia`, 12
`Cbaronsamedi`, 13 `Cjaws`, 14 `Cmayday`, 15 `Coddjob`, 16 `Cnatalya`, 21
`Csnowguard`, 36 `Cpilot`, 79 `Cspicebond`. All other bodies (0-5, 17-20,
22-35, 37-40) carry a **head placeholder** and take a head file.

Name a character by its file name without the `C` and `Z` (`camguard`,
`headkarl`). `editor/model_names.h` has descriptive labels per id
(`Russian_Soldier`, `Male_Karl`, ...) that `characters.json` may carry as
`label`.

### How a head attaches

From the decompilation (how the game builds a guard): when the body's
`has_head` is 0 and a head id is given, the node that **part 4** of the body's
part table points at must be a **head placeholder** node (kind 23); the
head file's root node and its whole sibling chain become that node's
children. Nothing else changes: the head file's display lists load
**matrix 0**, and in every guard-skeleton body matrix 0 is the matrix of the
**neck joint** (joint 3, below), which is the placeholder's nearest ancestor
position node. So the head is drawn rigidly with the neck joint's matrix,
with no offset of its own. Measured: in the 28 bodies with a placeholder it
always sits under the joint-3 position node; in the capture (Verification)
every head vertex carries exactly the neck joint's matrix (difference 0.0 on
two guards).

Head part table: **part 0** is a switch holding the **sunglasses** (hidden
unless the guard record asks for them: flag bit 0 = always, bit 1 = at
random); **part 1** is a switch holding the top of the hair, which the game
hides while the guard wears a peaked hat. Either entry may be 0 (no such
part: the Brosnan heads, `CheadvivienZ`, `CheadbalaclavaZ`, ...). From the
decompilation.

Body part table: part 4 = head placeholder (or 0 in `has_head` bodies), part
6 = the neck position node (joint 3); parts 0-3 and 5 point at arm position
nodes (elbow and wrist groups) the game uses to attach held weapons
(UNCERTAIN which index is which hand; not needed for the export, keep the
part numbers in extras as models.md does).

## The character file

The container is the object model's (models.md §The model file): part
table, texture table, root node at `4 * nparts + 12 * ntextures` (measured:
true for all 80 files), 24-byte nodes, segment-5 pointers. What differs:

**Node kinds** (count over the 80 files, US):

| Kind | Count | Where | Notes |
|---|---|---|---|
| 1 header | 43 | root of every body and of the hand | the character's **root joint** (joint 0), driven by root motion |
| 2 position | 635 | the joints | **high byte of the kind word is used**: `0x0102` (195 nodes) = this joint also owns a **half-rotation matrix** (below); `0x0002` otherwise. No `0x0200` occurs. |
| 4 display list, simple | 12 | hand only | |
| 8 level of detail | 674 | | |
| 9 draw-order split | 43 | under the neck joint | draw both children (models.md) |
| 10 bounding box | 630 | 15 per body | **has children** in characters: the geometry of one hit zone hangs below it (zone numbers 1-15 in record bytes 0-3). Walk into its children; draw nothing for the box itself. |
| 13 shadow | 42 | one per body, last child of the root | run-time quad (below) |
| 18 switch | 81 | heads (sunglasses, hair) and the hand | |
| 21 position, simple | 3 | hand only | |
| 23 head placeholder | 28 | bodies with a separate head | record: a 16-bit run-time index only |
| 24 display list | 732 | bodies and heads | render type **3** for all 732 |

Kinds 3, 5-7, 11, 12, 14-17, 19, 20, 22 do not occur. Head files have no
position and no header node: their root is a display list (24), a switch
(18) or a level-of-detail pair (8).

**Header (1) record in a character**: bytes 0-1 = the skeleton joint whose
root-motion channels drive it (0 in every file), 2-3 = its matrix number (4
in every file; no vertex is ever loaded under it), 4-7 = pointer to the first
position node (joint 1). It has no origin.

**Position (2) record** (models.md layout): origin 0-11, **joint id** 12-13
(the skeleton index, 1-15), **matrix 0** 14-15 (the joint's matrix),
**matrix 1** 16-17 (the half-rotation matrix when the kind word has `0x0100`,
else -1), matrix 2 18-19 (-1 in every file).

**Shadow (13) record**, 32 bytes: 0-7 a 2-D offset, 8-15 a 2-D size, 16-19
pointer to an entry of the file's texture table (the shadow image: bank
texture **1980** in the bodies checked; it is the last texture-table entry of
every body and no display list references it), 20-23 pointer to the root
header node, 24-27 a scale. The game draws a textured quad on the floor under
the character at run time: no geometry in the file. Export it as an empty
node with its values in extras.

### The skeleton

One skeleton record, shared by all 42 guard-skeleton bodies (models.md
§skeleton record: joint count 16 at 0, joint array pointer at 4, size 45 at
8). Joint entry `i` (6 bytes: node kind, channel A, channel B):

| Joint | Kind | A | B | Joint | Kind | A | B |
|---|---|---|---|---|---|---|---|
| 0 | `0x0401` | 0 | 0 | 8 | 2 | 21 | 24 |
| 1 | 2 | 0 | 0 | 9 | 2 | 24 | 21 |
| 2 | 2 | 3 | 3 | 10 | 2 | 27 | 30 |
| 3 | 2 | 6 | 6 | 11 | 2 | 30 | 27 |
| 4 | 2 | 9 | 12 | 12 | 2 | 33 | 36 |
| 5 | 2 | 12 | 9 | 13 | 2 | 36 | 33 |
| 6 | 2 | 15 | 18 | 14 | 2 | 39 | 42 |
| 7 | 2 | 18 | 15 | 15 | 2 | 42 | 39 |

For joints 1-15, A is the first of the joint's three **rotation channels**
in an animation frame (A = 3 x (joint - 1)); B is the mirror-image joint's
channels (used when the game plays an animation mirrored for a left-handed
guard: not needed for a static export). For joint 0, A and B index the
**root-motion fields** (0-3). Size 45 = 15 joints x 3 channels.

Joint ids are the same in every body; what they are (measured: parents,
from the position-node tree, identical in all 42 bodies; sides from the
origins: +x is the character's **left**, the character faces +z):

| Joint | Parent | Name here | Joint | Parent | Name here |
|---|---|---|---|---|---|
| 0 | none | `root` (header node) | 8 | 6 | `l_wrist` |
| 1 | 0 | `pelvis` | 9 | 7 | `r_wrist` |
| 2 | 1 | `spine` | 10 | 1 | `l_hip` |
| 3 | 2 | `neck` (head attaches here) | 11 | 1 | `r_hip` |
| 4 | 2 | `l_shoulder` | 12 | 10 | `l_knee` |
| 5 | 2 | `r_shoulder` | 13 | 11 | `r_knee` |
| 6 | 4 | `l_elbow` | 14 | 12 | `l_ankle` |
| 7 | 5 | `r_elbow` | 15 | 13 | `r_ankle` |

The **matrix numbers differ between bodies** (9 distinct joint-to-matrix
maps over the 42 bodies; e.g. `l_elbow` is matrix 14 in `CcamguardZ`, 17 in
`CborisZ`, 6 in `Ctrevelyan`). Constant in every body: joint 0 = matrix 4,
joint 1 = matrix 5, joint 2 = matrix 1, joint 3 = matrix 0, joints 4/5 =
matrices 2/3. Read the map from the file; key everything on the joint id.

The **origins** (bone lengths) also differ per body (Jaws' neck at y 576,
Oddjob's at 378.5, most guards' at 480.4); joint 1's origin is (0, 0, 0) in
every body.

**Half-rotation matrices.** Position nodes with `0x0100` in the kind word
own a second matrix (matrix 1 of the record): joints 6, 7 (elbows), 12, 13
(knees) in all 42 bodies and joint 2 (spine) in 27 of them (matrix 20 or
12). That matrix is built like the joint's own (same parent, same origin)
but with **half** the joint's rotation (below). Vertices of the seam around
the elbow, knee or waist are loaded under it, which is how the game bends a
limb smoothly without vertex weights. The `0x0200` variant (a y-only
half rotation with a stretch) exists in the game code but no file uses it.

Skeleton of `Csuit_lf_handZ` (the first-person hand on the watch screen):
a different record (US cdata `0x1B954`), 5 position joints (1, 2, 4, 6, 8)
plus 3 simple positions, no skinning (measured: no triangle mixes matrices),
its matrices built by the watch code. Out of scope for the character export
(see Pitfalls).

### Vertices and skinning

Vertex format, display lists and the per-list matrix rule are the object
model's (models.md §Vertices, §Display lists): a `0x01` command selects the
matrix by number, and a vertex is transformed by the matrix **in force when
it is loaded** (`0x04`). Measured over all character lists: vertex loads
never go past slot 15; every matrix number used is below `nmatrices` and is
the matrix 0 or matrix 1 of some position node (matrix 4, the header's, is
never used); every vertex record (file offset) is always loaded under the
**same** matrix (0 exceptions over 23 643 records in the 42 bodies); state
commands are `0xB7` 0x2000 (cull back), `0xB6` 0x3000 (double-sided parts),
`0xBA`, `0xBB`, `0xE7`, `0xC0`; **no lighting bit**, so the vertex colour
bytes are colours, never normals.

What makes characters different from objects: a triangle may join vertices
loaded under **different** matrices (172 of 347 triangles in `CcamguardZ`;
in every body between 164 and 258), and a display list's vertices are often
loaded under a matrix that is not its ancestor position node's. So:

- **One joint per vertex, weight 1.0.** There is no blending of two matrices
  on one vertex; the smooth bends come from the half-rotation matrices.
- Each vertex's **joint is its matrix number at load time**, not the node
  the list hangs under. A static export must track the current matrix while
  running each list (Primary, then Secondary, with one state).
- Two vertex records can have the same position, UV and colour under two
  different matrices: a vertex de-duplication key must include the matrix.

Heads: every head list loads matrix 0 only; head files are rigid.

Render type 3 (from the decompilation, a character drawn normally): two-cycle;
cycle 0 colour and alpha = (TEXEL1 - TEXEL0) x LOD fraction + TEXEL0; cycle 1
colour = COMBINED x SHADE, alpha = COMBINED x SHADE alpha (no primitive
term); render mode primary `0xC4112078` (fog blend, opaque z-buffered
surface), secondary `0xC41049D8` (translucent). The fog colour is the room's
light colour at the character, not part of the model. Both lists are drawn
in the same pass, primary then secondary. So the material rules of models.md
apply as written for render type 4 (primary **OPAQUE**, secondary **BLEND**),
then the list's own commands. Measured: 27 secondary lists in all: 25 in heads (24 of
them inside the sunglasses part, the lenses) and 2 in `CborisZ`. No texture-table entry is
an embedded image (top byte 5) in any character file.

## Transforms, rest pose and the bind pose

Each position node owns its matrix 0 (and matrix 1 if flagged). From the
decompilation, with column vectors (the game stores row-vector matrices; this
page writes the equivalent column form):

```
G(joint 0)   = Placement x T(0, rootY x pov, 0) x RotY(heading)        (see Placement)
L(joint j)   = T(origin_j) x Rz(rz_j) x Ry(ry_j) x Rx(rx_j)
G(joint j)   = G(parent position node) x L(joint j)                    -> matrix 0
G_half(j)    = G(parent position node) x T(origin_j) x H_j             -> matrix 1
H_j          = the rotation halfway from identity to Rz Ry Rx, along the shortest
               path: as a unit quaternion q (w, x, y, z) with w >= 0, H = normalize(w + 1, x, y, z)
vertex_world = G(matrix of the vertex) x (x, y, z, 1)
```

Rx, Ry, Rz are the standard right-handed rotations about x, y, z (Rx is
applied first). The "parent position node" is the nearest ancestor position
or header node; a child always hangs off its parent's **matrix 0**, never off
the half matrix. Measured (strongest check on this page): with this rule the
game's own captured joint matrices of `ColiveguardZ` on Bunker 1 are
reproduced to **1.7e-5** (walking, frame 12), and those of `CcommguardZ` to
3.9e-5 (idle_unarmed, frame 0), including the half matrices 9, 13, 16, 19; the
opposite order Rx Ry Rz is off by 5.8 to 47 (Verification).

**Bind pose** (what the vertices are stored in): every rotation identity.
Then `G(joint j) = T(sum of the origins from joint 1 down to j)`, the half
matrices equal their joint's, and a vertex's position relative to its joint
is the stored position. This pose is **not natural**: each limb's vertices
are authored along an axis away from its joint, so the identity pose has the
arms and the legs spread out sideways (measured `CcamguardZ`: x -1079 to
1088, y -198 to 524; the thigh goes from the hip (+-90, -100) along -x/+x to
the knee 430 units out).

**Rest pose** (what to show): **frame 0 of the `idle` animation** (number 0
below), root y included. Measured: with it every body but three stands with
its lowest vertex at -0.8 to -0.0 world units (feet on the floor; `CsnowbondZ`
-2.9, `CgreatguardZ` and `Cgreatguard2Z` +2.4), heights in the table at the
end (179.3 world units for an ordinary guard with a head).

## Placement in the world, scale and units

Character files are in **model units**. From the decompilation (how a setup
guard is spawned and drawn):

```
model scale  = 0.1 x body scale (table)            (0.1 for every guard body, 0.095 for CspicebondZ)
Placement    = T(pad x, floor y, pad z) x RotY(atan2(look.x, look.z)) x S(model scale)
rootY        = the animation's root y field (model units); G(joint 0) adds rootY x pov
```

- The position is the pad's, snapped to the floor (stan) under it; the
  floor height is where the feet go (the root y of the animation lifts the
  pelvis above it). The level scale does **not** apply to characters: a guard
  is 0.1 x body scale on every level (pads are already in world units,
  models.md / D63).
- The heading makes model +z point along the pad's `look` (horizontal part).
- `pov` scales only the root motion (the root's height and travel), not the
  mesh. It is the per-body fit that keeps short and tall bodies on the floor
  with the shared animations: Oddjob 0.7878, Jaws 1.199, the women 0.8853 to
  0.916, Bond bodies 1.04 to 1.058. Measured: with `rootY x pov` every body's
  idle feet land within 3 world units of the floor (table at the end); without
  it Oddjob would float 23 units.
- Run-time scalings that a static export ignores: DK mode cheat (x 0.8, a
  bigger head), the player's own body (x 0.97), the cast screen.

The glTF of a body therefore stays in model units; `characters.json` gives
`model_scale` (0.1 x body scale) and `pov`, and the level export places an
instance with `Placement`.

## Animations

### Where the data is

Two uncompressed ROM ranges (raw bytes, no rarezip), and one table in
cdata:

| Value | US | EU | JP |
|---|---|---|---|
| animation **frames** ("entries"), ROM range | `0x124AC0`-`0x28E980` | `0x111A30`-`0x27B8F0` | `0x1257E0`-`0x28F6A0` |
| animation **headers** ("data"), ROM range | `0x28E980`-`0x29D160` | `0x27B8F0`-`0x28A0D0` | `0x28F6A0`-`0x29DE80` |
| guard animation table, offset in decompressed cdata | `0x8FDC` | `0x666C` | `0x8FDC` |

The frames range is 0x169EC0 bytes, the headers range 0xE7E0 bytes; both are
byte-identical in the three regions (measured), and the headers range starts
right where the frames range ends. Invariant: the header at offset `0x1C` of
the headers range has frame offset 0, 163 frames, width 12, 544 bits per
frame (the idle animation).

### The animation table

An array of 32-bit words, **183 entries** then a 0 terminator (a second,
3-entry list for aircraft follows; not for characters). The **animation
number** (the index the AI scripts and the game use, 0 = idle) is the entry
index. A word is a byte offset of that animation's header **in the headers
range**; the value **1** marks an unused slot (13 slots: 50, 51, 91-96,
143-146, 169). 170 animations are real.

Names (the decompilation's, in index order; use them as glTF animation
names): 0 `idle`, 1 `fire_standing`, ... 26 `death_forward_face_down`, ... 40
`walking`, 41 `sprinting`, 42 `running`, ... 106 `idle_unarmed`, 107
`walking_unarmed`, 114 `walking_female`, 115 `running_female`, ... 182
`credits_natalya_kissing`. The full list is too long to repeat here and is a
fact table: the implementer may copy the list of names from the editor or
generate it once (it must not be derived from game code at build time).
UNCERTAIN: no editor file holds it yet; until one does, name animations
`anim_<number>` and add the names later.

### The animation header (20 bytes, in the headers range)

| Bytes | Field |
|---|---|
| 0-3 | offset of frame 0 **in the frames range** |
| 4-5 | number of frames (unsigned) |
| 6 | **width**: bits per rotation channel (12 in all 170) |
| 7 | flags: bit 0 = **loops** (37 animations: the walks, runs and sprints); bit 1 set on 5 hit/death animations, not read by the game (ignore) |
| 8-11 | offset of the **root-motion field descriptors** in the headers range |
| 12-13 | **root bits per frame** (0 to 35) |
| 14-15 | **frame size in bits** (544 in all 170 = 45 x 12, plus 4 padding bits) |
| 16-19 | offset of the **root-motion bit stream** in the headers range |

All offsets are relative to the start of their range (the game rebases them
at boot). Measured: per animation the headers range holds the descriptors,
then the root stream, then the header; frame offsets are consecutive (frame
0 of animation n+1 = frame 0 of n + frames x 68).

### One frame: joint rotations

Frame `f` is `frame size / 8` bytes (68) at
`frames range + frame offset + f x 68`. It is a bit string, **most
significant bit first** within each byte, holding 45 channels of `width`
bits: channel `c` starts at bit `c x width`. For joint `j` (1-15) with
skeleton channel A: x angle = channel A, y = A + 1, z = A + 2. A channel's
`width` bits are an unsigned value shifted left to 16 bits; the angle is
`value16 x 2 pi / 65536` radians (0 to 2 pi). These are the `rx, ry, rz` of
§Transforms.

### Root motion (joint 0)

Four fields: 0 = x, 1 = y, 2 = z, 3 = heading. Descriptor `k` (6 bytes, at
descriptors + 6k): bytes 0-1 bit offset (unsigned), byte 2 bit count, byte 3
padding, bytes 4-5 base value (unsigned). The value of field `k` at frame
`f`:

- bit count 0: the base value;
- else read `bit count` bits (MSB first) of the root stream at bit
  `root bits per frame x f + bit offset`, sign-extend them from `bit count`
  bits (when below 16), add the base, keep the low 16 bits.

x, y, z are then signed 16-bit model units; the heading is the unsigned
16-bit value x 2 pi / 65536. Meaning (from the decompilation):

- **y is absolute**: the pelvis height above the floor at that frame (idle
  1083 constant; walking 1031-1099).
- **x and z are per-frame displacements** (sideways and forward) and the
  heading is a per-frame turn: when the game steps from frame f-1 to f it
  adds RotY(current heading) x (x, 0, z) to the character's position and
  the heading delta to its heading. The displacement is scaled by
  `model scale x pov` into world units (`pov` alone in model units).

Measured (US): `idle` 163 frames, root constant (0, 1083, 0, 0); `walking` 37
frames, loops, root x 4 bits + 3, y 7 bits + 1065, z 5 bits + 49, heading
constant 0: one cycle moves 1738 model units forward (sum of z over all
frames; 81 sideways); `running` 24 frames, 2815 per cycle; `sprinting` 19
frames, 3333; `death_forward_face_down` 89 frames, y from 1081 down to 187
(the body ends on the floor), 884 forward; `idle_unarmed` 48 frames, y 1086;
`walking_female` 31 frames, 1795 per cycle. None of these turn.

### Speed

The game advances the frame by `speed` per 1/60 s tick (EU: per 1/50 s tick
with the speed x 1.2, the same real-time rate). The speed is chosen by the
code that starts the animation, not stored in the data. From the
decompilation: `idle` and `idle_unarmed` 0.25 (15 frames per second), the
walk, run and sprint loops 0.5 (30 frames per second); most combat and death
animations 0.5 or a difficulty-dependent 0.5-0.8. Between frames the game
interpolates each Euler angle linearly the short way round (a glTF LINEAR
quaternion channel is a close stand-in).

Recommendation: export frame `f` at time `f / 30` s for every animation and
put `fps` in `characters.json` (15 for the two idles, 30 otherwise) so a
viewer can retime; UNCERTAIN for the per-action speeds beyond these.

### Do bodies share the animations?

Yes: every guard-skeleton body uses the same 170 animations, addressed by
joint id. The rotations depend only on the joint id; what differs per body
is the origins (bone lengths, constant, not animated), the joint-to-matrix
numbering and `pov`. So one set of rotation channels drives every body, but
a glTF animation can only target nodes of its own file.

## What a glTF export should contain

Recommendation the implementer follows (Y up, model units, as models.md):

- **One `.gltf` + `.bin` per body** (`<id>_<name>.gltf`, `2_oliveguard.gltf`)
  and **one per head** (`42_headkarl.gltf`). The hand (41) is not exported
  (Pitfalls); say so in `characters.json`.
- **Body node hierarchy = the skeleton**: one glTF node per joint id 0-15,
  named `j<id>_<name>` (`j3_neck`), nested by the parent table above;
  joint 0 is the scene root of the rig. Plus one node per half-rotation
  matrix, named `j<id>_half`, child of the joint's **parent** with the
  joint's origin as translation. Node translation = the joint's origin
  (joint 0: (0, rootY x pov, 0) of idle frame 0); node rotation = idle frame
  0's rotation (the half nodes: the half quaternion). Extras: `joint`,
  `matrix` (and `half_matrix`), `origin`, `part`.
- **Skin**: `joints` = the nodes of every matrix number of the file (matrix
  0 and 1 of each position node, and joint 0's node for the header matrix),
  `skeleton` = joint 0's node. **Inverse bind matrices** from the bind pose:
  `IBM = T(-(sum of origins from joint 1 to the joint))` (identity for joint
  0; the half node's equals its joint's). The mesh positions must be in the
  same bind space: write `stored position + that sum of origins` for each
  vertex (then `G x IBM x position = G x stored position`, the game's
  result). `JOINTS_0` = the skin index of the vertex's matrix,
  `WEIGHTS_0` = (1, 0, 0, 0). The skinned mesh node sits at the scene root
  with no transform.
- **Meshes**: one mesh for the body (all display-list nodes reached by the
  walk: level of detail near = 0 only in scene 0, the other levels in a
  second scene `all` as models.md does; bounding-box children walked;
  switches visible), primitives by material as models.md. Extras per
  primitive: `list` (primary / secondary), the display-list node offset,
  `render_type` 3, the hit zone (the bounding box's number above it).
- **Heads**: one glTF per head, rigid: a root node `neck` (identity) with
  the mesh as child, positions as stored. Sunglasses (part 0) and hair (part
  1) subtrees each under a node `switch_specs` / `switch_hair` with extras
  `switch: true`, `default: "hidden"` for the sunglasses and `"visible"` for
  the hair; scene 0 leaves the sunglasses out. **To attach** a head to a
  body: make the head's root a child of the body's `j3_neck` node with an
  identity transform (equivalently: bind every head vertex to the neck
  joint with weight 1). The body's placeholder becomes an empty node
  `head_attach` (extras `head_placeholder: true`) under `j3_neck`.
- **Animations**: in each body file, the animations selected for export as
  glTF animations named by animation name: per joint a `rotation` channel
  (one key per frame), per half node a rotation channel of the half
  quaternion, and on joint 0 a `translation` channel with the accumulated
  root motion in model units (`x, z` summed per frame as above and multiplied
  by `pov`, `y x pov`) and a `rotation` channel with the accumulated heading.
  Keep `loop`, `frames`, `fps` in the animation's extras. Default selection:
  `idle` (0), `walking` (40), `running` (42), `death_forward_face_down` (26);
  an option exports all 170. A shared `animations.gltf` (the camguard rig
  only, every animation, nodes named as above) lets a consumer retarget by
  node name; this is optional.
- **`characters.json`**: one object per table id: `id`, `file`
  (`ColiveguardZ`), `name` (`oliveguard`), `label` (from
  `editor/model_names.h`), `kind` (`body` / `head` / `hand`), `gltf`,
  `body_scale`, `model_scale`, `pov`, `is_male`, `has_head`, `joints` (glTF
  skin joint count: 20 or 21 = `nmatrices`; 1 for heads), `triangles` (scene
  0; for heads with the sunglasses hidden, plus `triangles_specs`),
  `triangles_all`, `textures` (bank numbers used), `animations` (names
  exported), `notes`. Top level: the animation list (`number`, `name`,
  `frames`, `loop`, `fps`, root travel per cycle).

## Verification

The game's own capture (`ge007 -level_NN --export-level DIR`, format in
`port/levelrec/levelrec.h`): an owner of kind 1 named **`guard <body id>`**
holds the first guard of that body type, **with its attached head**, drawn
at distance 0 in its **live pose at the moment of the export** (the root's
position and heading zeroed, so the pelvis is at the origin and the
character faces +z), primary and secondary lists, sunglasses as the game set
them. Its vertex positions are **as stored** (model x, y, z), with a matrix
index into the run's own matrix list; those matrices are the posed joint
matrices including the model scale: `capture matrix (transposed to column
form) = S(model scale) x G(joint)` with joint 0 at the origin.

Measured, US captures of Bunker 1 (level 09) and Dam (level 33):

| Level | Owner | Body file | Body tris (ROM) | matched in capture | Capture total | Head part in capture | Pose found |
|---|---|---|---|---|---|---|---|
| Bunker 1 | `guard 2` | `ColiveguardZ` | 330 | 330 | 528 | 198 (ROM head 49) | `walking` frame 12, max error 1.7e-5 |
| Bunker 1 | `guard 18` | `CcommguardZ` | 329 | 329 | 527 | 198 (ROM head 49) | `idle_unarmed` frame 0 (also 1, 34, 35), 3.9e-5 |
| Bunker 1 | `guard 6` | `CborisZ` (`has_head`) | 480 | 399 (the 81 head triangles subdivided) | 725 | 326 | none within 4.1 (a blend of two animations) |
| Dam | `guard 37` | `Cgreatguard2Z` | 341 | 341 | 539 | 198 | `idle` frame 0, 0.053 (between frames) |
| Dam | `guard 2` | `ColiveguardZ` | 330 | 330 | 528 | 198 | `idle` frame 0, 0.053 |
| Dam | `guard 18` | `CcommguardZ` | 329 | 329 | 527 | 198 | `idle_unarmed` frame 0, 3.9e-5 |

"Matched" = the capture holds a triangle with the same three stored
positions for every ROM triangle of the static walk (near level of detail).
So: **body triangles equal the capture exactly; head triangles do not**,
because the PC port subdivides heads at first launch (Pitfalls). Compare
heads by vertex set instead: every vertex position of the ROM head (with the
sunglasses part left out) occurs in the capture (overlap 1.0), but several
heads share their base mesh (on Bunker 1 `guard 2` matches both
`CheadbalaclavaZ` 60 and `CheadbZ` 50), so the head id of a capture is not
always recoverable from geometry alone; textures settle it.

Per-file facts for the check (US; textures = bank numbers referenced by the
lists, sizes from the texture table):

| Id | File | Joints (`nmatrices`) | Triangles scene 0 / all LODs | Textures |
|---|---|---|---|---|
| 2 | `ColiveguardZ` | 20 | 330 / 446 | 14: 1609, 1613, 1616, 1619, 1622 (32x32), 1912 (32x32), 1914, 1915, 1916 (32x48), 1917 (1x1), 1966, 1967, 1969 (32x48), 1968 (48x32) |
| 18 | `CcommguardZ` | 20 | 329 / 445 | 14: 1609-1622 as above, 1688, 1689, 1690, 1692, 1693, 1694, 1786 (32x48), 1691 (32x32), 1785 (48x32) |
| 6 | `CborisZ` | 20 | 480 / 628 | 20: 1609-1622, 1653 (32x64), 1654 (48x32), 1655 (32x32), 1656, 1659, 1660, 1662, 1663, 1664 (32x48), 1657, 1658, 2081 (48x32), 1661, 1665 (32x64), 2690 (32x32) |
| 37 | `Cgreatguard2Z` | 20 | 341 / 457 | 14: 1609-1622, 1631 (48x32), 1761, 1783, 1784, 1959, 1961, 1965 (32x48), 1960, 1962 (32x32) |
| 0 | `CcamguardZ` | 20 | 347 / 463 | 13 |
| 23 | `CsuitbondZ` | 21 | 503 / 619 | 13 |
| 49 | `CheaddwayneZ` | 1 | 59 (49 without sunglasses) | 1733, 1734 (32x64), 1995 (16x32), 1996 (32x32) |
| 60 | `CheadbalaclavaZ` | 1 | 49 (no sunglasses part) | 1567, 1568 (32x64) |
| 67 | `CheadkenZ` | 1 | 59 (49) | 2504, 2505, 2680 (32x64), 1995, 1996 |
| 78 | `CheadbrosnanZ` | 1 | 209 | 1607 (64x32), 1608 (1x1), 1624, 1625, 1626 (32x64) |

Measured over the 37 heads: the sunglasses part draws texture 1996 (24
heads) and 1995 (10 heads).

Every body's texture table also lists 1980 (32x32, the shadow), referenced by
no list. `tools_pc/export_vs_levelrec.py` matches textures by pixels; extend
it with the per-owner check for `guard N` owners: (1) every ROM body triangle
(raw positions) occurs in the capture; (2) for each vertex matched to a
single ROM matrix, the capture matrix equals `S(model scale) x G` of some
(animation, frame) of the export (search all frames; accept 1e-3); a
blended live pose (Boris) is reported, not failed.

Full table of the static walk (US; tris = scene 0, idle-frame-0 lowest and
highest point in world units, bodies with a separate head measured with
`CheadkarlZ` without sunglasses):

| Id | File | body scale | pov | male | has_head | nmtx | tris | tris all LODs | min y | max y | textures |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 0 | `CcamguardZ` | 1 | 1 | 1 | 0 | 20 | 347 | 463 | -0.6 | 179.3 | 13 |
| 1 | `CgreyguardZ` | 1 | 1 | 1 | 0 | 20 | 363 | 479 | -0.6 | 179.3 | 12 |
| 2 | `ColiveguardZ` | 1 | 1 | 1 | 0 | 20 | 330 | 446 | -0.6 | 179.3 | 14 |
| 3 | `CrusguardZ` | 1 | 1 | 1 | 0 | 20 | 330 | 330 | -0.6 | 179.3 | 12 |
| 4 | `CtrevguardZ` | 1 | 1 | 1 | 0 | 21 | 319 | 435 | -0.6 | 179.3 | 17 |
| 5 | `CdjbondZ` | 1 | 1.045 | 1 | 0 | 21 | 503 | 619 | -0.6 | 185.4 | 12 |
| 6 | `CborisZ` | 1 | 0.9702 | 1 | 1 | 20 | 480 | 628 | -0.5 | 174.9 | 20 |
| 7 | `CorumovZ` | 1 | 1.078 | 1 | 1 | 20 | 502 | 642 | -0.8 | 189.2 | 20 |
| 8 | `CtrevelyanZ` | 1 | 1 | 1 | 1 | 21 | 608 | 736 | -0.6 | 180.8 | 15 |
| 9 | `CboilertrevZ` | 1 | 1 | 1 | 1 | 21 | 606 | 734 | -0.6 | 180.8 | 15 |
| 10 | `CvalentinZ` | 1 | 0.9324 | 1 | 1 | 20 | 473 | 601 | -0.8 | 172.7 | 15 |
| 11 | `CxeniaZ` | 1 | 1 | 0 | 1 | 21 | 604 | 732 | -0.2 | 184.4 | 18 |
| 12 | `CbaronsamediZ` | 1 | 1 | 1 | 1 | 21 | 548 | 688 | -0.6 | 190.8 | 24 |
| 13 | `CjawsZ` | 1 | 1.199 | 1 | 1 | 21 | 438 | 566 | -0.7 | 213.5 | 19 |
| 14 | `CmaydayZ` | 1 | 1 | 0 | 1 | 21 | 422 | 550 | -0.2 | 185.2 | 15 |
| 15 | `CoddjobZ` | 1 | 0.7878 | 1 | 1 | 20 | 492 | 632 | -0.6 | 146.2 | 17 |
| 16 | `CnatalyaZ` | 1 | 0.9661 | 0 | 1 | 21 | 572 | 700 | -0.3 | 178.6 | 15 |
| 17 | `CarmourguardZ` | 1 | 1 | 1 | 0 | 21 | 319 | 435 | -0.6 | 179.3 | 16 |
| 18 | `CcommguardZ` | 1 | 1 | 1 | 0 | 20 | 329 | 445 | -0.6 | 179.3 | 14 |
| 19 | `CgreatguardZ` | 1 | 1 | 1 | 0 | 20 | 342 | 458 | 2.4 | 179.3 | 10 |
| 20 | `CnavyguardZ` | 1 | 1 | 1 | 0 | 20 | 347 | 463 | -0.6 | 179.3 | 13 |
| 21 | `CsnowguardZ` | 1 | 1 | 1 | 1 | 20 | 442 | 570 | -0.6 | 183.1 | 16 |
| 22 | `CboilerbondZ` | 1 | 1.04 | 1 | 0 | 21 | 475 | 591 | -0.6 | 185.8 | 12 |
| 23 | `CsuitbondZ` | 1 | 1.058 | 1 | 0 | 21 | 503 | 619 | -0.0 | 186.3 | 13 |
| 24 | `CtimberbondZ` | 1 | 1.044 | 1 | 0 | 21 | 477 | 593 | -0.4 | 186.0 | 15 |
| 25 | `CsnowbondZ` | 1 | 1.044 | 1 | 0 | 21 | 560 | 686 | -2.9 | 188.8 | 13 |
| 26 | `CbluewomanZ` | 1 | 0.8853 | 0 | 0 | 21 | 378 | 378 | -0.2 | 162.9 | 11 |
| 27 | `CfattechwomanZ` | 1 | 0.8853 | 0 | 0 | 21 | 377 | 377 | -0.5 | 162.9 | 11 |
| 28 | `CtechwomanZ` | 1 | 0.916 | 0 | 0 | 21 | 365 | 365 | -0.8 | 167.7 | 11 |
| 29 | `CjeanwomanZ` | 1 | 0.8853 | 0 | 0 | 21 | 368 | 368 | -0.6 | 162.9 | 14 |
| 30 | `CgreymanZ` | 1 | 1 | 1 | 0 | 21 | 345 | 345 | -0.6 | 179.3 | 14 |
| 31 | `CbluemanZ` | 1 | 1 | 1 | 0 | 21 | 345 | 345 | -0.6 | 179.3 | 14 |
| 32 | `CredmanZ` | 1 | 1 | 1 | 0 | 21 | 345 | 461 | -0.6 | 179.3 | 14 |
| 33 | `CcardimanZ` | 1 | 1 | 1 | 0 | 21 | 345 | 345 | -0.6 | 179.3 | 13 |
| 34 | `CcheckmanZ` | 1 | 1 | 1 | 0 | 21 | 345 | 345 | -0.6 | 179.3 | 14 |
| 35 | `CtechmanZ` | 1 | 1 | 1 | 0 | 20 | 360 | 476 | -0.6 | 179.3 | 11 |
| 36 | `CpilotZ` | 1 | 1 | 1 | 1 | 21 | 473 | 613 | -0.6 | 182.5 | 23 |
| 37 | `Cgreatguard2Z` | 1 | 1 | 1 | 0 | 20 | 341 | 457 | 2.4 | 179.3 | 14 |
| 38 | `CbluecamguardZ` | 1 | 1 | 1 | 0 | 20 | 347 | 463 | -0.6 | 179.3 | 13 |
| 39 | `CmoonguardZ` | 1 | 1 | 1 | 0 | 21 | 319 | 435 | -0.6 | 179.3 | 12 |
| 40 | `CmoonfemaleZ` | 1 | 0.8853 | 0 | 0 | 21 | 381 | 381 | -0.6 | 162.9 | 10 |
| 41 | `Csuit_lf_handZ` | 0.1 | 1 | 1 | 0 | 9 | 1355 (every switch) | 1355 | | | 22 |
| 79 | `CspicebondZ` | 0.95 | 0.9661 | 0 | 1 | 21 | 585 | 713 | -0.6 | 169.7 | 16 |

Heads (all `nmatrices` 1, `is_male` 1 except 70-73; tris with / without the
sunglasses part): 42 karl 59/49, 43 alan 63/53, 44 pete 63/53, 45 martin
59/49, 46 mark 59/49, 47 duncan 59/49, 48 shaun 63/53, 49 dwayne 59/49, 50 b
59/49, 51 dave 63/53, 52 grant 59/49, 53 des 59/49, 54 chris 59/49, 55 lee
63/53, 56 neil 59/49, 57 jim 59/49, 58 robin 59/49, 59 steveh 59/49, 60
balaclava 49, 61 bike 102, 62 graham 63/53, 63 stevee 59/49, 64 joel 59/49,
65 scott 63/53, 66 joe 63/53, 67 ken 59/49, 68 joe2 53, 69 mishkin 75, 70
sally 53, 71 marion 53, 72 mandy 55, 73 vivien 59, 74 brosnanboiler 209, 75
brosnansuit 209 (221 with its far level of detail), 76 brosnantimber 209, 77
brosnansnow 213, 78 brosnan 209.

## Pitfalls

- **Head subdivision is a PORT feature.** The PC port's first-launch
  converter rebuilds every `Chead*` file and the head triangles of the 14
  `has_head` bodies with more triangles (porting-notes D62), and collapses
  body level-of-detail pairs to the near mesh. The exporter reads the ROM
  and must export the **ROM's original mesh**; never read the converted
  cache. This is why capture head counts (198, 326) differ from the ROM's
  (49, 81).
- **Id 79 is a body** (above). **Id 41 is the first-person hand**
  (`Csuit_lf_handZ`): its own skeleton, its switches (`HOUR`, `MINUTE`,
  `SECOND`, ticks and one sleeve per Bond outfit, mutually exclusive) and
  matrices set by the watch code; no setup places it. Skip it (record it in
  `characters.json` with `kind: "hand"`, not exported), or export it rigid
  like an object model with every switch visible and say so.
- **The identity pose is not a pose**: limbs stick out sideways. Never show
  the bind pose; scene 0 shows idle frame 0.
- **Bounding boxes have children** in characters (unlike most objects): a
  walk that stops at kind 10 draws nothing.
- **Skinned seams**: vertex joint = matrix at load time, de-duplicate by
  (position, UV, colour, **matrix**).
- **Run-time parts**: the shadow quad (kind 13), the sunglasses and hair
  switches, hats (placed by the game on the neck joint with a per-head
  offset table, not part of these files), blood decals (the game paints
  vertex alpha of hit parts), weapons in hand (separate object models
  attached to arm parts). None are geometry of the export.
- **Mirrored playback** (left-handed guards) swaps channels A/B and negates
  some angles; the export plays animations unmirrored.
- **EU timing**: same frames, 50 Hz ticks, speed x 1.2; export the same
  times for all regions.

## UNCERTAIN

- **Animation names**: taken from the decompilation's enum; no port-side
  table holds them yet (settle by adding the list to `editor/` as a fact
  table, like `model_names.h`).
- **Per-action playback speeds** beyond idle (0.25) and the walk/run loops
  (0.5): set by the AI code per action and partly by difficulty; `fps` 30 is
  a default, not a measurement.
- **Body part numbers 0-3 and 5** (which arm part holds the right or left
  weapon): not traced.
- **Root motion stepping**: the game rotates each frame's (x, z) by the
  heading current at the start of the tick; for an exported turning
  animation the accumulated path may differ slightly from the game's when
  several frames pass in one tick. The tested animations do not turn.
- **Floor height under a pad**: the game snaps a spawned guard to the stan
  floor under the pad (searching 20 units around it); a static placement
  should use the floor under the pad (from the decor) rather than the pad's
  own y. Not checked numerically.
- **Animation flag bit 1**: set on 5 animations, never read by the game code
  found; ignored.

## Open questions

- Retargeting one shared `animations.gltf` onto other bodies by node name is
  a consumer convention, not a glTF feature; whether the editor wants that or
  animations embedded per body (the default above) is the lead's choice.
- The hat offset table (per head, a translation and a scale) would let the
  export place hats; out of scope here.
