# Object models

Every object a level's setup can place (doors, crates, furniture, consoles,
vehicles, weapons and items lying on the floor, glass, hats) is drawn from
one **model file**, picked by a **model number**. This page says where the
files and the model-number table are, how a model file is laid out, which
parts of it carry geometry, how it is placed in the world, and what a glTF
export of it should contain.

It builds on the other pages and does not repeat them:
[level-decor.md](level-decor.md) (rarezip, the ROM file table, world units),
[textures.md](textures.md) (the texture bank, decoding a texture by number),
[display-lists.md](display-lists.md) (the display-list commands, the texture
reference `0xC0`, the UV rule, the alpha rules).

Unless a line says otherwise, every value below was **measured on the US ROM**
(`ntsc-final`) and is the same on EU and JP (the model files are
byte-identical in the three regions; only the positions in the ROM differ,
given per region where they matter).

Characters (guard bodies and heads) and first-person weapons are out of
scope; [Characters and first-person weapons](#characters-and-first-person-weapons)
says what differs for them.

## Where the files are

### The files

One ROM file per model, named `P<name>Z` in the ROM file table
(`port/convert/convert_data.c`, `cv_regions[].files`, look the row up by
basename as level-decor.md does): `Palarm1Z`, `PdoorstatgateZ`,
`PchrkalashZ`, ... Each file is **one rarezip block** (level-decor.md
§Block compression: two header bytes `0x11 0x72`, then raw DEFLATE); the
decompressed bytes are the model file described below. Measured: all 340
files start with `0x11 0x72` and decompress completely; together they are
1 311 344 bytes decompressed (SHA-1 of the 340 decompressed files
concatenated in model-number order: `f08fa6587b635ab39c2877af9604f1a5a0bfc62a`,
identical in US, EU and JP). The row's size is the compressed size; the stream
ends by itself.

Do not enumerate the files by their `P...Z` names: enumerate the
model-number table below, which gives each model's file name and also the
counts needed to read the file, which the file itself does not hold.

### The model-number table

A model number (the number a setup object record holds, the one the editor
and `level.json` call `model`) is an index into a table in the game's
compressed data segment ("cdata", the same rarezip block textures.md uses for
the texture size table). The table is **not** in the model files: it gives,
per model, the file name, a base scale and a pointer to a small **header**
that holds the counts needed to read the file.

Decompress the cdata block (ROM offset per region in textures.md §The size
table: US `0x21990`, EU `0x1F850`, JP `0x219D0`). Pointers inside cdata are
N64 RAM addresses; subtract the region's cdata base address to get an offset
into the decompressed bytes.

| Value | US | EU | JP |
|---|---|---|---|
| cdata base address (subtract from cdata pointers) | `0x80020D90` | `0x8001EC50` | `0x80020DD0` |
| model-number table, offset in decompressed cdata | `0x19498` | `0x16138` | `0x19488` |
| first model header, offset in decompressed cdata | `0x16840` | `0x13A30` | `0x16830` |
| model header size (stride between consecutive headers) | 32 | 28 | 32 |
| entries before the terminator | 340 | 340 | 340 |

Table entry, 12 bytes:

| Bytes | Field |
|---|---|
| 0-3 | pointer to the model's header (cdata address) |
| 4-7 | pointer to the file name, a zero-terminated ASCII string in cdata (`"Palarm1Z"`) |
| 8-11 | base scale, big-endian IEEE float |

The table ends with an entry whose header pointer is 0, name `""` and scale
1.0. Model number `n` is entry `n`, from 0.

Model header (only the fields an exporter needs; the rest are 0 in the ROM and
filled in by the game at load time):

| Bytes | Field |
|---|---|
| 4-7 | pointer to the model's **skeleton** record in cdata (below) |
| 12-13 | `nparts`: number of entries in the file's **part table**, signed 16-bit |
| 14-15 | `nmatrices`: number of transform matrices the model uses, signed 16-bit |
| 16-19 | bounding radius, float (model units; used by the game for culling) |
| 22-23 | `ntextures`: number of entries in the file's texture table, signed 16-bit |

Skeleton record (kept for extras only): signed 16-bit joint count at 0, pointer
to a joint array at 4, signed 16-bit size at 8; a joint is three unsigned
16-bit values (the node kind of the joint, then two matrix numbers). Many
models share one skeleton record; the game also uses the skeleton's identity
to recognise kinds of object (two special doors, below).

Invariants to check (all three regions): 340 entries then the terminator;
entry 0 is `Palarm1Z`, entry 2 `PexplosionbitZ`, entry 339 `PbollardZ`; every
name starts with `P`, ends with `Z` and has a row in the ROM file table; the
sums over all 340 headers are `nparts` 356, `ntextures` 1850, `nmatrices`
412; base scales: 275 entries 0.1, 65 entries 1.0. How the table was found
(for another region): in the decompressed cdata, the one 12-byte-stride
position whose name pointers resolve, with a single base address, to
`Palarm1Z`, `Palarm2Z`, `PexplosionbitZ`, ... and that runs 340 entries to the
terminator. (Searching only for the first two names gives false hits: the
names are packed tightly, so check at least three, and the full run.)

On a mismatch, the exporter refuses the models and says why.

Examples (fact table, US): 0 `Palarm1Z`, 1 `Palarm2Z`, 3 `Pammo_crate1Z`,
38 `Pdesk1Z`, 104 `PwindowZ`, 135 `Psev_doorZ` (the first of the doors: 135
to 183 are doors, base scale 1.0), 183 `PdoorstatgateZ`, 184 `PchrkalashZ`
(the first of the floor weapons, 184 to 211), 212-223 hats, 279 `PmiltruckZ`,
282 `PhelicopterZ`, 288 `PtankZ`, 339 `PbollardZ`. Note: model 184 is the
Kalashnikov, not the gate; the gate is 183.

Name a model by its file name without the `P` and `Z` (`doorstatgate`):
that is the only name the ROM holds.

## The model file

All values big-endian. Every **pointer** inside the file is a segment address
with segment number 5: top byte `0x05`, low 24 bits = byte offset from the
start of the decompressed file (measured: every non-zero pointer of every
node, record and part-table entry has top byte 5; mask with `0x00FFFFFF`).
A pointer of 0 means "none".

Layout, in this order from offset 0:

1. the **part table**: `nparts` pointers (4 bytes each) to nodes, or 0;
2. the **texture table**: `ntextures` entries of 12 bytes;
3. the **root node**, at offset `4 * nparts + 12 * ntextures` (measured: true
   for all 340 files), then the rest of the nodes, records, vertex arrays and
   display lists, in no order the exporter needs to know: reach everything
   through pointers.

### Part table

Entry `k` points at the node the game addresses as "part k" of this kind of
object, or is 0. What a part number means depends on the kind of object, not
on the file: on doors part 1 is a switch node (the door's window, hidden when
the glass is shot out) and part 2 its bounding box; on floor weapons part 0 is
the muzzle-flash node and part 2 a switch holding the flash geometry; on
vehicles parts 1-4 are the wheel position nodes and part 6 the bounding box;
on monitors parts 0-3 are the screen geometry nodes (from the decompilation:
which part numbers the object code reads). The exporter records, per node, the
part numbers that point at it (extras `part`).

### Texture table

| Bytes | Field |
|---|---|
| 0-3 | texture id: top byte 0 = texture number in the low 16 bits (the bank of textures.md); top byte 5 = an image embedded in this file at that segment-5 offset |
| 4 | width (texels) |
| 5 | height |
| 6 | number of levels of detail it was authored with |
| 7 | pixel format family (RDP format code) |
| 8 | texel size code (RDP size code) |
| 9 | s wrap flags |
| 10 | t wrap flags |
| 11 | padding |

The game loads every table entry whose id is a bank number before drawing
the model. The geometry names its textures through the display lists'
texture references (`0xC0`, display-lists.md), with the same bank numbers;
measured: every texture a model's lists reference is in its table, and 26
models list a texture their lists never use. Width and height in the table
equal the texture header's (textures.md) for every texture checked (the
capture stores some of them padded, see Verification). Embedded images (top
byte 5) occur only in the four front-end models below (8 entries).

### Nodes

A node is 24 bytes:

| Bytes | Field |
|---|---|
| 0-1 | kind (`opcode`): the low byte is the kind; the high byte is 0 in every file |
| 4-7 | pointer to the node's **record** (kind-specific data) |
| 8-11 | parent node |
| 12-15 | next sibling |
| 16-19 | previous sibling |
| 20-23 | first child |

The tree is fully linked in the file (measured over all nodes: every child's
parent points back, every `prev` is the node whose `next` is this one, the
root's parent is 0). **Walk** it the way the game does: visit a node; if it
has a child (after the gating rules below), go to the child; else go to its
next sibling; with no next sibling, go up to the parent and take the parent's
next sibling, and so on until you climb past the root. Display lists are drawn
in visit order.

Node kinds used by the 340 object models (count of nodes, US):

| Kind | Count | Name here | Holds geometry |
|---|---|---|---|
| 1 | 5 | **header** (root of the 5 aircraft models) | no |
| 2 | 347 | **position** (a joint: origin + matrix number) | no |
| 4 | 115 | **display list, simple** | yes |
| 8 | 4 | **level of detail** | no (gate) |
| 9 | 24 | **draw-order split** | no (orders its two children) |
| 10 | 384 | **bounding box** | no |
| 12 | 21 | **muzzle flash** | generated at run time |
| 18 | 89 | **switch** | no (gate) |
| 21 | 60 | **position, simple** | no |
| 22 | 3 | **star flash** | generated at run time |
| 24 | 400 | **display list** (with collision data) | yes |

Roots: 289 models start with a position node (2), 46 with a simple position
node (21), 5 with a header node (1). Kinds 3, 5, 6, 7, 11, 13-17, 19, 20, 23
do not occur in object models; report any other kind and skip its subtree.

Records, by kind (offsets inside the record):

**Position (2)**, 28 bytes: 0-11 origin x, y, z (floats); 12-13 joint
number (unsigned); 14-15 **matrix number** (signed 16-bit); 16-17 and 18-19
two further matrix numbers (-1 = none; used by characters only); 20-23 pointer
(unused by objects); 24-27 bounding radius (float).

**Position, simple (21)**, 20 bytes: 0-11 origin x, y, z; 12-13 **matrix
number** (signed); 14-15 a second number (unused here); 16-19 bounding radius.

**Header (1)**: 0-1 animation part; 2-3 **matrix number** (signed); 4-7
pointer to the first position node; then two 16-bit group numbers and a
16-bit run-time index. It has no origin: aircraft are animated (their rotors
spin) and the game positions this node from the animation; at rest it is the
object's own matrix.

**Display list (24)**, 32 bytes: 0-3 primary list; 4-7 secondary list (or 0);
8-11 vertex array; 12-13 vertex count (signed); 14-15 collision-vertex count;
16-19 collision-vertex array; 20-23 point-usage array; 24-25 **render type**
(signed 16-bit, below); 26-27 run-time index; 28-31 0 in the file.

**Display list, simple (4)**, 20 bytes: 0-3 primary list; 4-7 secondary list
(or 0); 8-11 0 in the file; 12-15 vertex array; 16-17 vertex count
(unsigned); 18 **render type** (signed byte).

**Level of detail (8)**, 16 bytes: 0-3 near distance (float); 4-7 far
distance (float); 8-11 the node it gates (its child); 12-13 run-time index.

**Switch (18)**, 8 bytes: 0-3 the node it gates (its child); 4-5 run-time
index. Measured: the gated node is a display list (24) 36 times, a simple
display list (4) 42 times, a bounding box 8 times, a star flash 3 times.

**Draw-order split (9)**, 36 bytes: 0-11 a point, 12-23 a vector (floats);
24-27 first child, 28-31 second child; 32-33 mode (0: the vector is a
direction in model space; 1, 2, 3: use the matrix's x, y or z axis scaled by
the vector's x, y or z); 34-35 run-time index. The game draws the two child
groups in an order that depends on which side of the plane the camera is.
Measured: in every file the node's child is the first group and the first
group's sibling chain already continues into the second group, so a plain
walk draws both. For a static export the order does not matter: draw both.

**Bounding box (10)**, 28 bytes: 0-3 a part number (1 for 294 boxes, else 2,
3, 4, 5, 100 or 110; the object code uses it for hits); 4-27 six floats
`xmin, xmax, ymin, ymax, zmin, zmax` (model units). The **model's bounding
box**, the one the game uses to place the object, is the first bounding-box
node among the root's children, else among the children of the root's first
child (from the decompilation).

**Muzzle flash (12)**, 40 bytes: 0-11 offset, 12-23 size (floats); 24-27
pointer to an entry of this file's texture table (the flash texture: bank 884
or 2119 in the weapons measured); 28-31 scale (float). Its quad is built at
run time from these numbers, only while the weapon fires.

**Star flash (22)**, 16 bytes: 0-3 number of flash arms (signed 32-bit); 4-7
vertex array of 4 vertices per arm; 8-11 a display list. The game copies and
randomly jitters the vertices every frame while firing. Only in `Pchrm16Z`,
`Pchrfnp90Z` and `PtankZ`, always under a switch.

### Vertices

The vertex format is the room format (level-decor.md §Vertex block): 16
bytes, position x, y, z signed 16-bit, a 16-bit field (0 in the draw
vertices; in collision vertices it is the index of the draw vertex they stand
for), texture coordinates s, t (S10.5), and four colour bytes. The colour
bytes are the vertex colour (r, g, b, a) **except** while geometry-mode bit
`0x20000` (lighting) is on: then r, g, b are a signed normal and a is the
alpha, as display-lists.md §Texture generation describes. (Note: in the RDP
geometry mode `0x20000` is lighting and `0x80000` is the linear variant of
texture generation; in object models lighting is only ever turned on together
with texture generation, as `0x60000` or `0x62000`.)

The collision vertices and the point-usage array of kind 24 serve the game's
bullet tests: not needed for drawing. Keep their counts in extras.

### Display lists

Model lists are arrays of 8-byte commands like room lists; read
display-lists.md for every command it describes. Opcode census over the 336
object models (the four front-end models excluded), every primary and
secondary list once, up to the end of list:

`0x01` 540, `0x04` 2841, `0xB1` 6568, `0xBF` 200, `0xB6` 262, `0xB7` 284,
`0xB8` 574, `0xB9` 107, `0xBA` 1683, `0xBB` 1084, `0xC0` 1883, `0xE7` 714,
`0xFC` 1 (in `PexplosionbitZ`). No `0x06` (no calls between lists), no other
opcode.

What differs from room lists:

**Matrix (`0x01`).** `w0[16..23]` = parameters (always `0x02`: load,
modelview, no push), `w0[0..15]` = 64 (the matrix size); `w1` = segment 3 +
byte offset: the **matrix number** is `(w1 & 0xFFFFFF) / 64`. Vertices
loaded after it are transformed by that matrix (the matrix in force when the
vertex is loaded, not when the triangle is drawn). Measured: every list loads
a matrix before its first vertex; every matrix number used is below the
header's `nmatrices`, and is the matrix number of some position node of the
file.

**Vertex load (`0x04`)**: same fields as in rooms, but the address segment
depends on the node kind: in kind-24 lists it is segment **4**, an offset
from the start of **that node's vertex array**; in kind-4 lists it is
segment **5**, an offset from the start of **the file**. Measured: 2615 loads
use segment 4 (all in kind 24, never past the node's vertex count), 418 use
segment 5 (all in kind 4). One kind-4 node (`PexplosionbitZ`) has a vertex
count of 0 although its list loads 16 vertices from its vertex array: trust
the list, not the count.

**No combine or render-mode setup in most lists.** The game sets the colour
combiner and the render mode itself before each list, according to the
node's **render type**. Object models use render type **4** for 399 of 400
kind-24 nodes and for 44 kind-4 nodes; render type **0** (no setup: the
list sets its own state) for 70 kind-4 nodes and one kind-24 node, all in the
four front-end models. For an object drawn normally (fully opaque, from the
decompilation), render type 4 sets:

- two-cycle mode;
- combine: cycle 0 colour and alpha = (TEXEL1 - TEXEL0) x LOD fraction +
  TEXEL0 (a blend between two levels of detail of the same texture);
  cycle 1 colour = COMBINED x SHADE, cycle 1 alpha = COMBINED x SHADE +
  PRIMITIVE alpha (primitive alpha is 0 for ordinary objects; for tinted glass
  and windowed doors it is their current opacity, a run-time value);
- render mode, low other-mode word: primary list `0xC4112078` (fog blend in
  cycle 0, opaque z-buffered surface in cycle 1), secondary list `0xC41049D8`
  (translucent z-buffered surface: force blend set). The fog colour is the
  room's light colour at the object (the game tints objects by their room
  through the fog blender): not part of the model.

Then the list's own `0xB9` / `0xBA` / `0xB7` / `0xB6` commands change that
state as usual. Measured `0xB9` values in object lists: `0xC4104DD8` 73 times
(translucent decal: force blend, z mode decal) and `0xC4112D58` 33 times
(opaque decal: z mode decal), both on crates, drums, consoles and doors
(labels and stencils over a base surface), and `0x00552078` once (explosion
bit). Before each display-list node the game also sets **cull back faces**
(objects; a door flagged "flip" culls front faces instead); the lists turn
culling off (`0xB6` `0x3000`, 220 times) for their double-sided parts.

**Which lists are drawn.** For render type 4: the primary list in the opaque
pass and the secondary list (if non-zero) in the translucent pass; both are
part of the model. Render types 1 and 2: primary only. Render type 3
(characters and first-person guns): primary then secondary in the same pass.
Render type 0: primary only.

**Texture references.** Same command and fields as rooms (display-lists.md
§Texture reference). In object models the type is 2 (mipmap) 1840 times and
3 (tile) 43 times, and the offset field is always 2. The texture-scale
command `0xBB` is usually `0xFFFFFFFF` (1.0, 1.0); the glass texture of
`PwindowZ` uses `0x0D800D80` with texture generation.

## Transforms and the rest pose

Each position node (kinds 1, 2, 21) owns one matrix, its matrix number. The
model's vertices are stored **relative to the joint whose matrix they are
loaded with**. How the game builds the matrices of an object (from the
decompilation):

- Matrix of the **root** position node = the object's placement matrix
  (position, orientation, scale; next section). The root's own origin is
  **not** applied: it is the joint's position in the artist's scene (e.g.
  `PwindowZ` root origin (494.98, 1716.61, 0.0009) while its vertices are
  centred on 0).
- Matrix of any other position node = matrix of its nearest **ancestor
  position node** x translation(its origin) x a rotation the object code
  animates (door leaves, wheels, gun turrets, the camera head of a CCTV). At
  rest the rotation is the identity, so the **rest pose** is a pure
  translation by the origin relative to the parent position node.

Example: `Pdoor_eyelidZ` (164), parts 1 and 2 are the two lids, origins
(-1.05, -348.93, -1365.0) and (-1.05, 351.07, -1365.0) under the root; their
vertices span y 349..976 and -979..-351, z 1049..1681, so at rest the upper
lid covers y 0..627, the lower one -628..0, both z -316..316: they meet. Had
the root origin (-523.95, -1.07, 1365.0) been applied too, the door would not
be centred on its pad.

Measured on all 340 models: no triangle uses vertices loaded under two
different matrices, and every triangle's matrix is the matrix of its
display-list node's nearest ancestor position node (one exception: 6
triangles of `PexplosionbitZ`, which no setup places). So an exporter can
attach each display-list node's mesh to the glTF node of its ancestor
position node and keep the vertex positions as stored.

The object code builds non-root matrices only for some kinds of object
(doors with moving leaves, CCTV, auto guns, racks, vehicles, aircraft, tanks);
for an ordinary prop placed as a plain object only matrix 0 is set. The 22
object models with `nmatrices` > 1: CCTV (24), the two special doors (164,
165), the auto guns (98, 292, 299), the vehicles (279, 280, 281, 288, 305),
the aircraft (282-285, 291), four floor weapons (187, 188, 197, 211; their
second matrix only carries the hidden muzzle flash), and `Ptv_holderZ` (76)
and `PsevdishZ` (296), whose matrix code was not traced (UNCERTAIN; the rest
pose rule above still gives a sensible static pose).

## Gating: what a static export draws

- **Level of detail (8)**: the game shows the gated child when the view depth
  of the node (world units) is greater than `near x model scale` (or `near` is
  0) and at most `far x model scale`. At distance 0 only children with
  `near` = 0 show. Only two object models have levels of detail:
  `Pbookshelf1Z` (near 0 / far 7000, and near 7000 / far 7e9) and
  `PchrrocketZ` (near 3195 / far 2.13e8, and near 0 / far 3195). The most
  detailed child is the one with `near` = 0.
- **Switch (18)**: the game's default is **visible** (it hides them at run
  time: the window of a door when its glass breaks, a weapon's muzzle flash
  geometry when not firing, a tank part). Draw them; mark them.
- **Muzzle flash (12)** and **star flash (22)**: generated at run time,
  hidden by default (the flash) or under a switch (the star). Do not
  generate geometry for them; record their data in extras.
- **Bounding box (10)**: not drawn.

## Placement in the world, scale and units

Model files are in **model units**. The world units of the setup pads and of
level-decor.md's `world` are reached through the object's **scale** (from the
decompilation; not checked numerically here, see UNCERTAIN):

```
model scale = base scale (model-number table)
            x extra scale (the setup object record's first 16 bits, unsigned 8.8 fixed point: 0x0100 = 1.0)
            x pad fit (below, 1.0 when it does not apply)
world = object position + R x (model scale x v_rest)
```

where `v_rest` is the vertex transformed by the rest pose of its joint and `R`
the object's orientation. For most props the base scale is 0.1 (a crate
modelled 1000 units wide is 100 world units wide), for doors 1.0.

- **Orientation**: from the pad's `look` and `up` vectors: model +z goes to
  `look` (normalised), model +y to `up` made perpendicular to `look`, model +x
  to `up x look` (a right-handed basis; from the decompilation, see UNCERTAIN).
- **Pad fit** (objects on a pad with a bounding box, "bound pads"): when the
  object record asks for it (flag bits `0x10` uniform, `0x20` / `0x40` /
  `0x80` per axis), each axis is scaled so that the model's bounding box
  matches the pad's box; the uniform part goes into the model scale, the rest
  into the axes of the placement matrix.
- **Height**: by default the object is lowered or raised so that its bounding
  box's `ymin` (times the scale) sits 4 units above the floor under the pad;
  flag `0x4` hangs it upside down from its `ymax`, flag `0x8` puts its `ymin`
  at the pad's own height.
- **Doors** are scaled to their pad's box too, and their open position moves
  or rotates the door matrix; the rest pose is closed.

The glTF of a model therefore stays in model units with its rest pose; the
consumer that places instances (the level export) applies `model scale`,
orientation and position per instance. Put the base scale into the model's
extras and into `models.json`; never bake it into the vertices.

## What a glTF export should contain

Recommendation the implementer follows:

- One `.gltf` + `.bin` per model number, named by model number and file name
  (`183_doorstatgate.gltf`). Y up, as level-decor.md; model units.
- **Node hierarchy = position nodes**: one glTF node per position node (kinds
  1, 2, 21), named `joint_<matrix number>`, children nested as in the file.
  The root's translation is 0 (its origin goes to extras `origin`); every
  other one has translation = its origin. Extras: `matrix`, `origin`,
  `radius`, `kind`, `part` (the part numbers pointing at it).
- **Meshes**: one glTF node per display-list node (kinds 4, 24), child of the
  glTF node of its nearest ancestor position node, mesh = its triangles with
  vertex positions as stored. Extras: `kind`, `render_type`, `part`,
  `vertex_count`, `collision_vertex_count`, which list each primitive came
  from (`primary` / `secondary`).
- **Levels of detail**: export every child of a level-of-detail node, each
  under a glTF node named `lod_<near>_<far>` with extras `lod: {near, far}`.
  Scene 0 (`default`) contains only the `near` = 0 children; put the others in
  a second scene `all` that contains everything, so a plain viewer shows the
  detailed model.
- **Switches**: export the gated subtree under a node named `switch_<part>`
  (or `switch` with the node offset), extras `switch: true`, `default:
  "visible"`. In scene 0, visible.
- **Flashes, bounding boxes**: no geometry. A bounding box becomes an empty
  node with extras `bbox: [xmin, xmax, ymin, ymax, zmin, zmax]` and
  `bbox_part`; muzzle and star flashes become empty nodes with their record
  values in extras (`muzzle_flash: {offset, size, texture, scale}`,
  `star_flash: {arms}`).
- **Materials**: by display-lists.md (base colour texture = the referenced
  main texture, sampler from the s/t modes, UV rule, texture generation rule,
  COLOR_0 = vertex colour), with these model rules: the starting state of
  each list is render type 4's (above): primary **OPAQUE**, secondary
  **BLEND** with alpha = texture alpha x vertex alpha (no environment-alpha
  rewrite: that is a room rule); then the list's own render-mode commands
  reclassify as display-lists.md says. A z mode of decal (`0x0C00` in the low
  other-mode word) adds extras `decal: true` (glTF has no depth offset: a
  renderer should bias it). Keep the raw combine, render mode, geometry mode
  and the texture reference fields in the material's extras as the decor
  export does.
- **Model extras** (on the scene or asset): `model`, `file`, `base_scale`,
  `bounding_radius`, `nparts`, `nmatrices`, `ntextures`, the skeleton joint
  list, the model bounding box.
- A **`models.json`** index: one object per model number: `model`, `file`
  (`PdoorstatgateZ`), `name` (`doorstatgate`), `gltf`, `base_scale`, `bbox`
  (the model bounding box), `nodes` (position nodes), `triangles` (scene 0),
  `triangles_all` (every level of detail), `textures` (bank numbers used),
  `nmatrices`, and `notes` for the special cases below.

## Verification

The game's own capture: `ge007 -level_NN --export-level DIR` writes
`level.rec` (format in `port/levelrec/levelrec.h`). Its owners of kind 1
named `object <n>` carry model `n`'s triangles, drawn at distance 0 (so the
`near` = 0 level of detail), switches at their live state, primary and
secondary lists. The capture keeps the vertex positions **as stored** (the
position attribute is model x, y, z and a matrix index local to the run), and
its matrix list for a model is the identity except matrix 0, which is the
first placed instance's scale (0.1 for most props, other values where a setup
scaled the object: e.g. `PwindowZ` 0.8388 on Bunker 1, 0.4348 on Dam). Compare
raw positions and triangle counts, not transformed positions: in the capture
every joint of a multi-matrix model sits at the model origin.

Measured: the triangle count of the static walk above (levels of detail with
`near` = 0, switches visible, render-type-4 primary and secondary lists)
equals the capture's for **all 38 object models on Bunker 1 (level 09) and
all 27 on Dam (level 33)**. A selection (US; textures = bank numbers, size
from the texture header; the capture size where it stores the texture
padded):

| Model | File | Triangles | Textures |
|---|---|---|---|
| 1 | `Palarm2Z` | 12 | 29 (32x32), 30 (32x48), 771 (64x32) |
| 3 | `Pammo_crate1Z` | 20 | 33 (64x32), 37 (32x32), 39 (128x16) |
| 24 | `PcctvZ` | 21 | 247 (32x64), 265 (64x32), 511 (128x32), 654 (54x54; capture 56x54), 856 (64x64), 1514 (1x1; capture 16x1) |
| 38 | `Pdesk1Z` | 8 | 461 (64x32), 466 (64x64), 468 (64x48) |
| 48 | `Pmainframe1Z` | 32 | 468 (64x48), 891, 892 (33x32; capture 40x32), 893, 894 (32x33), 1071 (64x32), 1448 (64x64) |
| 104 | `PwindowZ` | 2 | 654 (54x54; capture 56x54), secondary list, texture generation |
| 138 | `Psev_door4_windZ` | 36 (34 with the switch hidden) | 7, 532 (32x1; capture 32x2), 545, 546, 547 (128x22), 618, 654, 1242 |
| 184 | `PchrkalashZ` | 54 | 873, 875 (16x16), 876 (16x32), 879 (1x16; capture 8x16), 887 (32x16), 1514, 1531 (16x32) |
| 63 | `PpadlockZ` (Dam) | 174 | 1004-1007 (32x32) |
| 178 | `PdamgatedoorZ` (Dam) | 22 | 1167 (32x32), 1370, 1371 (64x17) |
| 179 | `PdamtundoorZ` (Dam) | 38 | 18 (32x32), 56, 161, 167 (64x64) |
| 279 | `PmiltruckZ` (Dam) | 524 (504 with its switch hidden) | 22 textures, among them 654 and 1514 |
| 248 | `PchrgoldeneyekeyZ` (Bunker 1) | 250 | 479, 654, 807, 1514 |

The number of distinct textures per model equals the capture's for every model
of both levels except the monitors (next section): `Pconsole_sev2bZ` (33,
13 in the file, 14 in the capture) and `Ptv4screenZ` (78, 1 and 2).
`tools_pc/export_vs_levelrec.py` already matches textures by pixels including
the padded copies; extend it with the per-owner triangle comparison for
owners of kind 1.

## Pitfalls (models the game changes or draws by special code)

- **Monitors and screens** (`Ptv1Z`, `PtvscreenZ`, `Ptv4screenZ`,
  `Ptv_holderZ`, consoles, `PmodemboxZ`, `PdoorpanelZ`, ...): the screen
  geometry is a display-list node (a part) whose list the object code replaces
  every frame with a list that shows the screen's current image (a script of
  textures from the setup record). The file's own list is what a static export
  draws; the capture shows the live image, hence one more texture. Mark these
  nodes `screen: true` when they are a monitor's part 0-3.
- **Doors**: rest pose = closed. The leaves of `Pdoor_eyelidZ` and the 6
  blade pairs of `Pdoor_irisZ` (12 joints) rotate about their joints; sliding doors move as a
  whole. Windowed doors hide part 1 (a switch) when the glass breaks.
- **Floor weapons** (184-211) and gun props: the muzzle flash (12) and the
  star flash (22) are run-time geometry.
- **Vehicles and aircraft**: wheels, turrets and rotors are separate joints,
  rotated at run time; aircraft (header root) are animated.
- **Shot and destroyed objects** get darker vertex colours (the game swaps in
  recoloured vertex copies) and lose back-face culling: run-time only.
- **Front-end models** `PnintendologoZ` (276), `PgoldeneyelogoZ` (277),
  `PwalletbondZ` (278), `PlegalpageZ` (319) use render type 0, set their own
  combine modes (`0xFC`) and load images embedded in the file (`0xFD` with a
  segment-5 address, then `0xF5` / `0xF3` / `0xF2` tile and load commands,
  which display-lists.md does not cover). No setup places them. Export them
  untextured or skip them, and say so in `models.json`.
- **`PexplosionbitZ`** (2): vertex count 0 in its record, its own render
  mode; not placed by setups.
- Port-only changes (foliage drawn as cut-outs, monitors' and props'
  interpolation) are not in the ROM and not part of the export.

## Characters and first-person weapons

Character bodies and heads (`C...Z` files) and first-person weapons
(`G...Z` files) use the same container (part table, texture table, the same
24-byte nodes and segment-5 pointers, the same vertex and display-list
format), but their headers are in other cdata tables (not located here), and:
characters start with a header node (1) whose matrix the animation drives,
have many level-of-detail nodes (674 over all `C` files), a shadow node (13),
a head placeholder (23) where a separately loaded head model is attached at
run time, use render type 3 throughout (1068 kind-24 nodes, US), and are
skinned: triangles join vertices loaded under different joint matrices, so the
rest pose needs the animation's first frame and a glTF skin rather than
node-parented meshes. First-person weapons use kind-4 nodes with render type 3
(448) and many draw-order splits (278); they are drawn with a separate
projection in front of the camera. A later page covers both.

## UNCERTAIN

- **Orientation from a pad**: read from the decompilation (the basis
  function is given `-look` as a target from the origin and negates it again),
  not checked numerically. Settle it by placing `PwindowZ` or a door at its
  pad from `level.json` and comparing with the decor glTF (the door must fill
  its door frame).
- **Placement and scale numbers**: the scale formula and the pad fit are read
  from the decompilation, not measured; `level.json` gives no per-instance
  matrix. Settle it with one placed instance whose final matrix the game logs
  (the capture's matrix 0 shows only the scale of the first instance).
- **Meaning of the bounding-box part numbers** (1, 2, ..., 100, 110): used for
  hits; not needed for the export.
- **Texture-table bytes 6-10** (levels, format, size, wrap): described from
  the decompilation's field names; the exporter uses the texture header and
  the `0xC0` command instead.

## Open questions

- The header tables of the `C` and `G` files (for the character and weapon
  page).
- Whether the RDP's "alpha as coverage" bit in the opaque render mode makes
  texture alpha cut out on hardware for primary-list materials (measured: no
  object list sets alpha compare or coverage x alpha; the export keeps them
  OPAQUE).
