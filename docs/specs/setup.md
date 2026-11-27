# Stage setup files

A level's *setup* says what is placed in it and how the mission runs: the
pads (named positions), the bound pads (pads with a box), every guard and
object, the links between objects (dual weapons, switches, padlocks, safes,
tags), the objectives and the briefing text, the level's own AI lists, the
patrol paths and the waypoint graph, and the intro (spawn points, starting
inventory, intro cameras, the credits). One setup file per level and mode.

This page describes the file as it is stored in the ROM, so that an exporter
can turn it into `setup.json` without the game. Measured values are from the
US ROM (`ntsc-final`) unless stated; EU and JP differences are listed in
§Regions. All multi-byte values are big-endian.

Related pages: level-decor.md (level names, `level_scale`, world units),
models.md (the model numbers the object records use), stan.md (the
collision-tile names pads refer to; it may not be on every branch yet).

## Where the files are

### Names and compression

Each setup is one ROM file named in the ROM file table the converter already
embeds (`port/convert/convert_data.c`, `cv_regions[].files`, rows
`{offset, size, basename}`; look a row up by basename as level-decor.md
does):

- solo setups: `Usetup<name>Z`
- multiplayer setups: `Ump_setup<name>Z` (the solo name with `mp_` inserted
  after the leading `U`; from the decompilation: the game builds the
  multiplayer name exactly that way at load time)

Each file is **one rarezip block** (level-decor.md §Block compression: the
two bytes `0x11 0x72`, then a raw DEFLATE stream that ends by itself). The
row's size is the compressed size. The decompressed bytes are the setup file
described below; every offset in this page is an offset into the
decompressed bytes.

Measured: every region's table has exactly **34** setup rows (21 solo, 13
multiplayer); all start with `0x11 0x72` and inflate completely.

Enumerate setups from the level table below (it says which file a level
uses), not by scanning names: some names the game knows have no file, and
one file serves two stage numbers.

### Which setup belongs to which level

Stage number = the game's level id (the number of `-level_NN`). The solo name
comes from the game's stage-to-setup table (from the decompilation); the
multiplayer name is derived from it as above. "-" means no such file in the
ROM. Level names are the editor's (level-decor.md §Output names).

| Level | Stage | Solo setup | US size | Multiplayer setup | US size |
|---|---|---|---|---|---|
| dam | 33 | `UsetupdamZ` | 81808 | - | |
| facility | 34 | `UsetuparkZ` | 96160 | `Ump_setuparkZ` | 39776 |
| runway | 35 | `UsetuprunZ` | 31072 | - | |
| surface1 | 36 | `UsetupsevxZ` | 73136 | - | |
| bunker1 | 9 | `UsetupsevbunkerZ` | 41056 | - | |
| silo | 20 | `UsetupsiloZ` | 75136 | - | |
| frigate | 26 | `UsetupdestZ` | 58624 | - | |
| surface2 | 43 | `UsetupsevxbZ` | 63136 | - | |
| bunker2 | 27 | `UsetupsevbZ` | 65424 | `Ump_setupsevbZ` | 30992 |
| statue | 22 | `UsetupstatueZ` | 33952 | `Ump_setupstatueZ` | 17616 |
| archives | 24 | `UsetuparchZ` | 99792 | `Ump_setuparchZ` | 46368 |
| streets | 29 | `UsetuppeteZ` | 57392 | - | |
| depot | 30 | `UsetupdepoZ` | 56720 | - | |
| train | 25 | `UsetuptraZ` | 82032 | - | |
| jungle | 37 | `UsetupjunZ` | 103424 | - | |
| control | 23 | `UsetupcontrolZ` | 71648 | - | |
| caverns | 39 | `UsetupcaveZ` | 93008 | `Ump_setupcaveZ` | 39520 |
| cradle | 41 | `UsetupcradZ` | 24096 | `Ump_setupcradZ` | 14368 |
| aztec | 28 | `UsetupaztZ` | 73696 | - | |
| egypt | 32 | `UsetupcrypZ` | 37728 | `Ump_setupcrypZ` | 20496 |
| cuba | 54 | `UsetuplenZ` | 9328 | - | |
| temple | 38 | - (`UsetupdishZ` named, no file) | | `Ump_setupdishZ` | 8672 |
| complex | 31 | - (`UsetuprefZ`) | | `Ump_setuprefZ` | 7392 |
| caves | 50 | - (`UsetupoatZ`) | | `Ump_setupoatZ` | 6560 |
| basement | 45 | - (`UsetupimpZ`) | | `Ump_setupimpZ` | 8784 |
| stack | 46 | - (`UsetupashZ`) | | `Ump_setupashZ` | 14672 |
| library | 48 | - (`UsetupameZ`) | | `Ump_setupameZ` | 17136 |
| citadel | 40 | - (`UsetupcatZ`) | | - (`Ump_setupcatZ`) | |

Notes (facts):

- Stage 21 is also mapped to `UsetupsevbunkerZ` in the game's table; it has
  no level name and no decor entry. Export Bunker 1 once, under `bunker1`.
- The names `UsetupshoZ`, `UsetupeldZ`, `UsetuplueZ`, `UsetupritZ`,
  `UsetupearZ`, `UsetupleeZ`, `UsetuplipZ`, `UsetupwaxZ`, `UsetuppamZ`
  (stages 42, 44, 47, 49, 51-53, 55, 56) are in the game's table but neither
  the solo nor the multiplayer file exists. Skip them.
- Basement, Stack and Library share one decor file (`bg_ame_all_p`) but have
  three different multiplayer setups.
- `Ump_setupstatueZ` and `Ump_setupcradZ` exist although Statue and Cradle are
  not retail multiplayer maps (UNCERTAIN whether any menu reaches them;
  export them anyway).
- Mode: the game loads the multiplayer name whenever a multiplayer game is
  running, the solo name otherwise (from the decompilation).

## File layout

### Header

The file starts with ten 32-bit words, each the **byte offset from the start
of the file** of one section (no segment byte, unlike the decor file). 0
means "absent" (allowed for words 8 and 9; the others are always non-zero in
the retail files, measured).

| Word | Bytes | Section | Record | Ends with |
|---|---|---|---|---|
| 0 | 0-3 | waypoints (the navigation graph's nodes) | 16 bytes | a record whose word 0 is negative (-1) |
| 1 | 4-7 | waypoint groups | 12 bytes | a record whose word 0 is 0 |
| 2 | 8-11 | intro | variable, typed | the END intro record |
| 3 | 12-15 | propDefs (guards, objects, links, objectives) | variable, typed | the END propDef |
| 4 | 16-19 | patrol paths | 8 bytes | a record whose word 0 is 0 |
| 5 | 20-23 | AI list table | 8 bytes | a record whose word 0 is 0 |
| 6 | 24-27 | pads | 44 bytes | a record whose name offset (bytes 36-39) is 0 |
| 7 | 28-31 | bound pads | 68 bytes | a record whose name offset (bytes 36-39) is 0 |
| 8 | 32-35 | waypoint debug names (optional) | 4 bytes | a 0 word |
| 9 | 36-39 | waypoint-group debug names (optional) | 4 bytes | a 0 word |

Measured: the pads always start at 0x28, right after the header. The other
sections come in varying order; strings (pad names, debug names) are at the
end of the file. Example (US, Bunker 1): `8d4c 8a40 8630 20fc 912c 969c 0028
1260 902c 8aa0`.

### Offsets inside the sections ("relocation")

Several fields inside the sections are also file offsets (the game adds the
file's load address to them at load time; from the decompilation). A static
reader simply treats them as offsets into the decompressed file:

- pad and bound-pad name (bytes 36-39 of each record) -> zero-terminated ASCII
- waypoint neighbour list, waypoint-group neighbour list and member list,
  patrol-path waypoint list -> arrays of signed 32-bit ints ending at the
  first negative value (-1)
- AI list table entry word 0 -> the first byte of the AI list
- debug-name table entries -> zero-terminated ASCII
- the credits intro record's word 1 -> the credits block

Every other field of the file is plain data. Fields that hold run-time
pointers in the game are 0 in the file (measured for pads' tile field: 0 in
every pad and bound pad of Bunker 1 and Dam).

### Endianness and number formats

- Integers: big-endian, two's complement.
- Pad and bound-pad coordinates and vectors: big-endian IEEE 754 single
  floats.
- Most "float" fields of propDef and intro records are **16.16 fixed point**
  in the file (a signed 32-bit integer; value = raw / 65536), converted by
  the game at load time (from the decompilation; porting-notes D65). This page
  says "fixed" for them.
- Positions in the camera propDef and the intro camera are signed 32-bit
  integers in hundredths (value = raw / 100).
- A reader that takes every field from the big-endian bytes at the offsets
  given here is immune to the little-endian traps of the PC sidecar
  (porting-notes D139, D151, D157): those concern the converted layout, not
  the ROM file.

## Pads and bound pads

### Pad record (44 bytes)

| Bytes | Field |
|---|---|
| 0-11 | `pos`: x, y, z (float, file units) |
| 12-23 | `up`: x, y, z (float, unit vector) |
| 24-35 | `look`: x, y, z (float, unit vector) |
| 36-39 | offset of the pad's **tile name** (zero-terminated ASCII) |
| 40-43 | 0 (the game stores the resolved tile there at load) |

### Bound pad record (68 bytes)

Bytes 0-43 as a pad, then the box, six floats in this order:

| Bytes | Field |
|---|---|
| 44-47 | `xmin` |
| 48-51 | `xmax` |
| 52-55 | `ymin` |
| 56-59 | `ymax` |
| 60-63 | `zmin` |
| 64-67 | `zmax` |

The box is in the pad's own frame (porting-notes D70: x = `up` x `look`,
y = `up`, z = `look`, around the pad position), not in world axes.

### Numbering

Pads are numbered from 0 in file order. Bound pads are numbered from 0 too,
but every reference that can name either kind uses **10000 + index** for a
bound pad (from the decompilation: "pad < 10000" selects the plain pad table,
otherwise the bound table at pad - 10000). Exception: a **door** record's pad
field is always a bound-pad index *without* the 10000 (porting-notes D70).
Export bound pads with `num = 10000 + index` (what the editor and
`mission.json` use).

### Units

The stored coordinates are in file units. At load the game multiplies every
pad position and every bound-pad box bound (not `up`/`look`) by
`1 / level_scale` (level-decor.md's per-level constant; the game computes the
reciprocal in single precision and multiplies in single precision; from the
decompilation). That gives world units, the same as the decor's
`world = (vertex + room_position) / level_scale`. `level.json`'s `scale` is
that reciprocal (Bunker 1: 1.854206 = 1 / 0.53931433; Dam: 4.280089 =
1 / 0.23363999; measured).

Export both: the raw file values and the world values computed exactly as
`float32(file * float32(1 / level_scale))`.

### Tile name

The name is the collision tile the pad stands on (stan.md). Format: a letter
`p` or `q`, a decimal number 0..32767, a lower-case letter `a`..`z`, and an
optional single digit. The game packs it into the 24-bit tile name of the
collision file (from the decompilation):

- high 16 bits = (letter - 'p') << 15 | number
- low 8 bits = (file letter - 'a') << 3 | digit, where a missing digit counts
  as 0 and the digits `1`..`7` count as their value
- the digit `0`, a digit 8 or 9, a number over 32767, a bad first letter or
  extra characters make the name invalid (packed value 0xFFFFFF, matches no
  tile). Note that an explicit `0` digit is rejected, although "no digit"
  means 0.

Examples (US): `p609b` -> `0x026108`; `p1709c1` -> `0x06AD11`; `p334a2` ->
`0x014E02`; `p3830a` -> `0x0EF600`. Measured: every pad name of Bunker 1
packs to a valid id; Dam has **13 pads with an empty name** (export
`stan_id: null`).

When the named tile does not exist or the pad is not inside it, the game
searches for a tile under the pad at load (run-time behaviour; a static
export only records the name and the packed id).

### Debug name tables

Header words 8 and 9, when non-zero, point to tables of string offsets (end:
a 0 word). From the decompilation they are used only by debug prints about
the navigation graph: word 8 names waypoints, word 9 names waypoint groups
("halls"). Measured: where present, their lengths equal the waypoint count
and the group count (Bunker 1: 45 and 7; Dam: 205 and 23; present in 7 solo
setups), e.g. `"PAD_leave_sev_all_p"`, `"ha7_locssev_all_p"`. Export them as
two string lists (`null` when the header word is 0); nothing else depends on
them.

## PropDefs

### The stream

Header word 3 points at a stream of records laid end to end, each a whole
number of 32-bit words. Every record starts with one **header word**:

| Bytes | Field |
|---|---|
| 0-1 | `extrascale`: unsigned 8.8 fixed point (0x0100 = 1.0); a size factor for objects |
| 2 | `state` (run-time state bits; 0 in the retail files except where noted) |
| 3 | `type`: the record type (table below) |

The record size depends only on the type (table below). The stream ends with
a record of type **48 (END)**, one word. Read the type from **byte 3** of the
header word (porting-notes D139: a little-endian "low byte" read gets
`extrascale` instead).

Record numbering: every record of the stream, of every type, gets an index
0, 1, 2, ... in file order; the END record has none. This index is what
`level.json` / `mission.json` call `prop` and what the AI scripts and the
editor's "setup prop number" mean (mission_setup.h: "position in the propDefs
stream"). Relative references between records (below) are counted in the
same numbering.

Measured: in every retail file the walk with the sizes below lands exactly on
END. (The PC converter also requires that the next section starts within 15
zero bytes after END, and it accepts every retail file.)

### Load gating (difficulty, multiplayer)

Most object records are only created when their `flags2` word (bytes 12-15 of
the object header below) does not have the bit for the current mode
(from the decompilation): `0x10` Agent, `0x20` Secret Agent, `0x40` 00 Agent,
`0x80` 007; in multiplayer `0x08` (all multiplayer) and `0x400000` /
`0x800000` / `0x1000000` for 2 / 3 / 4 players. A static export keeps every
record and exports the bits (`load_on` below); the consumer filters.
Measured: Bunker 1 has 1 record with `0x20`, 2 with `0x40`, 2 with `0x80`.

### Object header (types that are "objects")

The object types (marked **obj** in the type table) start with the same 128
bytes (32 words):

| Bytes | Field | mission field name | Notes |
|---|---|---|---|
| 0-3 | header word | `extrascale` (bytes 0-1), `state` (byte 2) | |
| 4-5 | `model`: model number, signed 16-bit | | models.md numbering |
| 6-7 | `pad`, signed 16-bit | | see §The pad field |
| 8-11 | `flags` | `flags` | placement and behaviour bits |
| 12-15 | `flags2` | `flags2` | gating and behaviour bits |
| 16-111 | run time (pointers, matrix, position, bit flags) | | 0 in the file |
| 112-115 | fixed | `maxdamage` | 0 in the retail files measured |
| 116-119 | fixed | `damage` | the object's health; 1000.0 (raw `0x03E80000`) is the usual value |
| 120-127 | run time (colours) | | 0 |

Type-specific fields follow at byte 128. Field names in this page are the
mission system's (`port/mission/mission_fields.c`) wherever it has one, so a
`setup.json` field and a `mission.json` field mean the same file word.

**The pad field** of an object:

- `flags & 0x8000` (object inside another object, e.g. a weapon in a crate):
  `pad` is a **relative record offset** (usually negative) to the containing
  object: container index = this record's index + `pad`.
- else `flags & 0x4000` (assigned to a character: carried weapons, hats,
  keys): `pad` is the **chr number** of the guard that carries it (the
  guard record's `chrnum`). The editor calls this `carrier`.
- else `pad` is a pad number (< 10000 plain, >= 10000 bound). Doors: always a
  bound-pad index without the 10000.

Placement bits in `flags` used by the game when it puts an object on its pad
(from the decompilation; models.md §Placement has the formulas): `0x10`
uniform fit to the bound pad's box, `0x20` / `0x40` / `0x80` fit x / y / z,
`0x4` hang from the ceiling, `0x8` keep the pad's height, otherwise drop onto
the floor tile under it (porting-notes D69: the stored pad height is not the
final height), `0x1000` absolute position, `0x2` rotated placement.

### Record types

Sizes are in bytes in the ROM file (N64 words x 4; from the decompilation's
size function, and they tile every retail stream exactly, measured). "Count"
is the total over all 34 setups of all three regions (measured), so 0 means
"never in a retail file".

| Type | Name | Size | obj | Count | Fields after the header word (byte offset: meaning) |
|---|---|---|---|---|---|
| 1 | door | 256 | obj | 1692 | see §Doors |
| 2 | door scale | 8 | | 9 | 4: fixed; global scale applied to the doors that follow (game default 1.0) |
| 3 | prop (standard object) | 128 | obj | 4575 | none |
| 4 | key | 132 | obj | 78 | 128: `keyflags` (u32 bit mask; the editor shows it as the item) |
| 5 | alarm | 128 | obj | 21 | none |
| 6 | cctv | 236 | obj | 45 | 128: `lookpad` (s32, the pad the camera looks at); 132-235: run time |
| 7 | magazine (ammo clip) | 132 | obj | 144 | 128: `ammo_type` (s32) |
| 8 | collectable (weapon) | 136 | obj | 3117 | 128: `weaponnum` (s8, item number; the editor's `item`), 129: `linked_weapon_type` (s8), 130-131: `timer` (s16), 132-135: run time |
| 9 | guard | 28 | | 1995 | see §Guards |
| 10 | monitor | 256 | obj | 690 | 128-243: run time (image script state), 244: `owner_offset` (s32), 248: `owner_part` (s32), 252: `image_num` (s32) |
| 11 | multi monitor | 596 | obj | 165 | 128-591: run time (4 screen states), 592..595: `image_num0`..`image_num3` (one u8 each) |
| 12 | rack (hanging monitors) | 128 | obj | 18 | none |
| 13 | autogun | 216 | obj | 123 | 128: `pad_id`, 132: `rot_related`, 136: `unk88`, 140: `unk8c`, 164: `speed`, 168: `aimdist`, 172: `unkac`, 184: `unkb8`, 188: `unkbc`, 192: `unkc0`, 208: `is_active` (all raw s32, meaning not established; see UNCERTAIN); 196-207: run time pointers (0) |
| 14 | link (dual weapons) | 12 | | 6 | 4: `offset1` (s32), 8: `offset2` (s32): relative offsets of two collectables that form a pair |
| 15, 16 | - | (4) | | 0 | never used; the game's size function gives 4 bytes |
| 17 | hat | 128 | obj | 1047 | none (usually `flags & 0x4000`, pad = chr number) |
| 18 | guard attribute | 12 | | 294 | 4: `chrnum` (s32), 8-11: one word; its low byte (byte 11) is the guard's grenade probability (from the decompilation; bytes 8-10 not used by the game) |
| 19 | switch | 16 | | 30 | 4: `offset1` (s32, the switch object), 8: `offset2` (s32, the door it operates), 12: run time |
| 20 | ammo box | 180 | obj | 702 | 128 + 4k (k = 0..12): `slot<k>_model` (u16, model number, 0xFFFF = none), 130 + 4k: `slot<k>_qty` (u16) |
| 21 | body armour | 136 | obj | 228 | 128: `initial_amount` (fixed), 132: run time (the game copies the amount there) |
| 22 | tag | 16 | | 1194 | 4: `tag_id` (u16), 6: `offset` (s16, relative offset of the tagged object), 8-15: run time |
| 23 | objective start | 16 | | 240 | 4: `number` (s32, 0-based objective letter a, b, c...), 8: `text` (u32 text id), 12: `min_difficulty` (s32, 0 Agent .. 3 007) |
| 24 | objective end | 4 | | 240 | none |
| 25 | objective: destroy | 8 | | 276 | 4: `tag_id` |
| 26 | objective: complete when | 8 | | 141 | 4: `stage_flags` (u32 mask; complete when the AI has set these flags) |
| 27 | objective: fail when | 8 | | 138 | 4: `stage_flags` (u32 mask; failed when set) |
| 28 | objective: collect | 8 | | 48 | 4: `tag_id` |
| 29 | objective: deposit | 8 | | 0 | 4: `tag_id` |
| 30 | objective: photograph | 16 | | 6 | 4: `tag_id`, 8: run time status, 12: run time |
| 31 | objective: null | 4 | | 0 | none |
| 32 | objective: enter room | 16 | | 15 | 4: `pad` (a pad in the room), 8: run time status, 12: run time |
| 33 | objective: deposit in room | 20 | | 12 | 4: `tag_id`, 8: `pad`, 12: run time status, 16: run time |
| 34 | objective: copy item | 12 | | 3 | 4: `tag_id` (s32; UNCERTAIN, the game only tests a global "key copied" state), 8: run time |
| 35 | watch menu text (briefing) | 16 | | 315 | 4: `page` (s32: 0 title, 1 overview, 2 M, 3 Q, 4 Moneypenny), 8: `text` (u32 text id), 12: run time |
| 36 | gas releasing | 128 | obj | 57 | none |
| 37 | rename (text override) | 40 | | 177 | 4: `offset` (s32, relative offset of the renamed object), 8: `weapon` (item number), 12: `title1`, 16: `title2`, 20: `short_text`, 24: `long_text`, 28: `pickup_text` (u32 text ids, 0 = keep), 32-39: run time |
| 38 | lock door (padlock) | 16 | | 81 | 4: `offset1` (s32, the door), 8: `offset2` (s32, the lock object), 12: run time |
| 39 | vehicle | 176 | obj | 27 | 128: `ailist` (u32, AI list id), 132: `ai_offset` (u16), 134: `ai_return_list` (s16), 136-175: run time |
| 40 | aircraft | 180 | obj | 18 | as vehicle: 128 `ailist`, 132 `ai_offset`, 134 `ai_return_list`; 136-179: run time |
| 41 | (unknown object) | (4) | obj | 0 | the game would treat it as an object but its size function says 4 bytes: refuse the file if it appears |
| 42 | glass | 128 | obj | 423 | none |
| 43 | safe | 128 | obj | 15 | none |
| 44 | safe item | 20 | | 18 | 4: `offset_item`, 8: `offset_safe`, 12: `offset_door` (s32 relative offsets), 16: run time |
| 45 | tank | 224 | obj | 6 | 128-163: run time, 164..192 step 4: `unka4`, `unka8`, `unkac`, `unkb0`, `unkb4`, `unkb8`, `unkbc`, `unkc0` (raw s32; the decompilation's comments: acceleration, top speed / gun, sounds), 196-223: run time |
| 46 | camera position (cutscene) | 28 | | 93 | 4, 8, 12: x, y, z (s32 hundredths, world units), 16: `theta` (fixed, radians), 20: `verta` (fixed, radians), 24: `pad` (s32) |
| 47 | tinted glass | 148 | obj | 576 | 128: `tint_dist` (s32), 132: `cull_dist` (s32), 136: run time opacity, 140: run time portal, 144: `unk90` (s32 raw; fixed when the glass has a portal) |
| 48 | END | 4 | | (1 per file) | none |
| 0 | - | (4) | | 0 | never used |

Every record type 1..47 that has a "run time" part has zeros there in the
retail files (UNCERTAIN for all bytes: the exporter keeps them in `raw`
anyway, so nothing is lost).

**Relative offsets** (`offset`, `offset1`, `linked_door_offset`, ...) are
counted in record indices, not bytes: target index = this record's index +
offset (from the decompilation, for links, switches, padlocks, safe items,
tags, renames, door links and the "inside another object" pad field). Export
the raw offset and the resolved absolute index (`*_prop`), and `null` when it
falls outside 0..count-1.

### Doors (type 1, 256 bytes)

Object header (128 bytes), then:

| Bytes | mission field | Meaning |
|---|---|---|
| 128-131 | `linked_door_offset` (s32) | relative offset of a door that opens together with this one; 0 = none |
| 132-135 | `max_frac` (fixed) | how far the door opens (fraction of its travel) |
| 136-139 | `perim_frac` (fixed) | fraction after which the door no longer blocks |
| 140-143 | `accel` (fixed) | |
| 144-147 | `decel` (fixed) | |
| 148-151 | `max_speed` (fixed) | |
| 152-153 | `door_flags` (u16) | |
| 154-155 | `door_type` (u16) | sliding, vertical, swinging, eye, iris, ... |
| 156-159 | `keyflags` (u32) | key bits needed to open; 0 = unlocked |
| 160-163 | `auto_close_frames` (u32) | |
| 164-167 | `open_sound` (u32) | |
| 168-255 | run time | 0 in the file |

The door's `model` is the door model number (models.md: 135-183). Its `pad`
is a bound-pad index (no 10000).

### Guards (type 9, 28 bytes)

| Bytes | Field | mission field | Meaning (from the decompilation) |
|---|---|---|---|
| 0-3 | header word | `extrascale`, `state` | 0 in practice |
| 4-5 | `chrnum` (u16) | | the guard's chr number; other records and AI lists refer to the guard by it |
| 6-7 | `pad` (u16) | | always a **plain** pad index (the game reads `pads[pad]` directly) |
| 8-9 | `body` (u16) | | body number; 0xFFFF = random body |
| 10-11 | `ailist` (u16) | | AI list id (§AI lists) |
| 12-13 | | `preset` | a pad number (may be >= 10000) the AI uses |
| 14-15 | | `chrpreset1` | a chr number the AI uses; 0xFFFF = none |
| 16-17 | | `health` | despite the name: the guard's hearing scale x 1000 (1000 = 1.0) |
| 18-19 | | `reaction_time` | despite the name: the guard's vision range |
| 20-21 | | `bitflags` | 0x4 clone on gunfire, 0x8 invincible, others select the model variant |
| 22-23 | | `head` (s16) | head number; negative = chosen by the game |
| 24-27 | | | run time (0) |

The body and head numbers index the character table that the characters page
covers; a guard's `body` is the number `level.json`'s `models[]` calls
`model` for its `guard: true` entries (measured, below).

### Objectives

The objective records form groups: an **objective start** (23), then its
conditions (25-34), then an **objective end** (24). The watch-menu text
records (35) are separate (briefing pages). A group's conditions all have to
hold (from the decompilation's status check: destroy = the tagged object is
no longer healthy, collect = it is in the inventory, complete/fail = AI stage
flags, enter room / deposit in room / photograph = set by the game when it
happens). Tag ids are matched against the tag records (22): a tag record with
`tag_id` N names the object at its resolved index. Export each group with its
start record's fields and the list of condition records (type, fields,
index).

## AI lists

### Table

Header word 5 points at the AI list table: 8-byte entries {u32 file offset
of the list, s32 id}, ending with an entry whose offset is 0. Measured: no
multiplayer setup has any entry (the table is just the terminator); Bunker 1
has 19, Dam 28.

Ids (from the decompilation): ids up to 1024 (0x400) are the game's **global**
lists (built into the game code, not in the file); 1025..4095 are a level's
character lists; 4096 and up are its background lists. A guard's `ailist`,
a vehicle's / aircraft's `ailist` and AI commands that change lists refer to
lists by id; the game looks a local id up in this table. Measured: Bunker 1
guards use 4 local and 20 global list ids, Dam guards 26 local and 10 global.

### List bytes

An AI list is a byte-code stream: one opcode byte, then a fixed number of
argument bytes per opcode, ending with opcode **0x04** (end of list). The
exporter does not interpret it, but it must know each command's length to
find the end (lists are not separated by anything reliable: measured, 7 gaps
of unreferenced bytes, e.g. dead code, sit between lists in `UsetuparkZ`,
`UsetupcrypZ`, `UsetuppeteZ`, `UsetuprunZ`, `UsetupsevxZ`, so "up to the next
list" is wrong).

Command length in bytes (opcode included) by opcode, rows of 16 (from the
decompilation's command table):

| | +0 | +1 | +2 | +3 | +4 | +5 | +6 | +7 | +8 | +9 | +A | +B | +C | +D | +E | +F |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 0x00 | 2 | 2 | 2 | 1 | 1 | 4 | 3 | 1 | 1 | 1 | 9 | 2 | 1 | 1 | 2 | 2 |
| 0x10 | 2 | 2 | 2 | 2 | 6 | 6 | 6 | 6 | 4 | 4 | 2 | 5 | 3 | 1 | 3 | 3 |
| 0x20 | 2 | 1 | 1 | 2 | 4 | 1 | 1 | 2 | 2 | 2 | 2 | 2 | 3 | 3 | 3 | 2 |
| 0x30 | 3 | 3 | 2 | 1 | 3 | 3 | 2 | 2 | 2 | 2 | 2 | 2 | 2 | 2 | 2 | 2 |
| 0x40 | 3 | 2 | 2 | 2 | 4 | 2 | 2 | 3 | 3 | 3 | 3 | 4 | 4 | 7 | 7 | 5 |
| 0x50 | 5 | 4 | 6 | 6 | 5 | 4 | 3 | 3 | 4 | 3 | 3 | 3 | 3 | 3 | 2 | 2 |
| 0x60 | 2 | 2 | 2 | 2 | 3 | 4 | 2 | 2 | 4 | 3 | 3 | 3 | 4 | 3 | 3 | 3 |
| 0x70 | 3 | 3 | 4 | 4 | 4 | 4 | 3 | 3 | 3 | 3 | 3 | 3 | 4 | 4 | 3 | 3 |
| 0x80 | 3 | 2 | 2 | 2 | 3 | 2 | 2 | 2 | 2 | 3 | 2 | 3 | 2 | 2 | 2 | 3 |
| 0x90 | 3 | 2 | 2 | 2 | 2 | 2 | 3 | 3 | 3 | 4 | 5 | 5 | 6 | 5 | 5 | 6 |
| 0xA0 | 6 | 6 | 7 | 6 | 6 | 7 | 6 | 6 | 7 | 2 | 3 | 3 | 4 | S | 1 | 1 |
| 0xB0 | 1 | 1 | 2 | 5 | 5 | 1 | 1 | 3 | 1 | 1 | 2 | 4 | 4 | 12 | 11 | 9 |
| 0xC0 | 8 | 5 | 3 | 3 | 4 | 5 | 6 | 6 | 6 | 2 | 5 | 2 | 5 | 5 | 2 | 2 |
| 0xD0 | 4 | 2 | 1 | 1 | 3 | 6 | 4 | 2 | 1 | 5 | 1 | 1 | 2 | 1 | 1 | 2 |
| 0xE0 | 3 | 3 | 4 | 2 | 2 | 3 | 5 | 2 | 2 | 1 | 1 | 2 | 1 | 1 | 13 | 1 |
| 0xF0 | 2 | 2 | 3 | 2 | 4 | 2 | 1 | 3 | 3 | 1 | 1 | 1 | 2 | | | |

- **S = opcode 0xAD** ("print", a debug string): the opcode, then a
  zero-terminated string; length = string length + 2 (the opcode and the
  terminating zero). It occurs in level lists (measured: 1146 times over all
  files and regions).
- Opcodes 0xFD..0xFF: not defined; the game counts an unknown opcode as 1
  byte. Measured: no retail list contains one.

Walking every list of every retail setup (all regions) with this table ends
each list on its 0x04 and never runs into another list (measured).

What to export per list: `id`, file `offset`, `size` (bytes up to and
including the 0x04), `raw` (hex), and whether the id is a character or a
background list. No interpretation.

## Navigation graph and patrol paths

### Waypoints (header word 0, 16 bytes each)

| Bytes | Field |
|---|---|
| 0-3 | `pad` (s32, plain pad index) |
| 4-7 | offset of the neighbour list (s32 waypoint indices, ends at -1) |
| 8-11 | `group` (s32, waypoint-group index) |
| 12-15 | 0 (run time) |

Ends with a record whose `pad` is negative. Waypoints are numbered from 0.

### Waypoint groups (header word 1, 12 bytes each)

| Bytes | Field |
|---|---|
| 0-3 | offset of the neighbour list (s32 group indices, ends at -1) |
| 4-7 | offset of the member list (s32 waypoint indices, ends at -1) |
| 8-11 | 0 (run time) |

Ends with a record whose word 0 is 0.

### Patrol paths (header word 4, 8 bytes each)

| Bytes | Field |
|---|---|
| 0-3 | offset of the step list (s32 **waypoint** indices, ends at -1) |
| 4 | `id` (u8; AI lists and vehicles refer to a path by it) |
| 5 | `loop` (u8; non-zero = the path loops) |
| 6-7 | 0 in the file (the game stores the step count there) |

Ends with a record whose word 0 is 0. The steps are waypoint indices, not pad
numbers (from the decompilation: the game reads `pads[waypoints[step].pad]`);
export both the waypoint list and the resolved pads.

Measured: multiplayer setups have no waypoints, groups or paths (each table
is just its terminator).

## Intro

Header word 2 points at a stream of typed records; the first word of each is
its type (s32), and the size is fixed per type (from the decompilation's
record sizes; the walk ends on END in every retail file, measured):

| Type | Name | Size | Words after the type |
|---|---|---|---|
| 0 | spawn | 12 | `pad` (plain pad index), `slot` (0 normal; other values select alternative starts, e.g. for demos / extra players; UNCERTAIN) |
| 1 | item | 16 | `right` (item number), `left` (item number, -1 = none), `slot` |
| 2 | ammo | 16 | `ammo_type`, `amount`, `slot` |
| 3 | swirl (intro camera spline point) | 32 | `flags`, `x`, `y`, `z` (fixed: offset from the player), `scale` (fixed), `duration` (fixed), `pad` (-1 = none) |
| 4 | animation | 8 | `anim` (intro animation number) |
| 5 | cuff | 8 | `cuff` (Bond's outfit) |
| 6 | camera (fixed intro camera) | 40 | `x`, `y`, `z` (s32 hundredths), `yaw`, `pitch` (fixed, radians), `pad` (a pad of the room), `text1` (u32: two u16 text ids), `text2` (s32 text id), word 9 run time (0) |
| 7 | watch time | 12 | `hours`, `minutes` |
| 8 | credits | 8 | file offset of the credits block |
| 9 | END | 4 | |

Measured: the intro camera's x, y, z (raw / 100) are already in world units:
Dam camera 0 is (17608.08, -59.79, 17408.57) next to its pad 312 at world
(17647, -56, 17078); Bunker 1 camera 1 (-632.66, 189.56, 1674.57) next to pad
80 at world (-1012, 185, 1767). The same holds for the camera-position
propDef (46).

**Credits block** (only Cuba, `UsetuplenZ`): 12-byte entries {u16 text1,
u16 text2, s16 position1, u16 alignment1, s16 position2, u16 alignment2},
ending with the first entry whose two text ids are both 0. Measured: US 271
entries before the end, EU/JP 299 (block at offset 0x8D8 in all three).

## Regions

Measured (decompressed bytes compared):

- **EU and JP are byte-identical** for all 34 setup files.
- US equals EU/JP for 26 files. The 8 that differ (US -> EU/JP):
  `Ump_setuparchZ` (propDef 9, a prop, differs), `UsetupcradZ` (AI lists
  0x41D, 0x41F longer/shorter), `UsetupdestZ` (intro differs), `UsetupjunZ`
  (propDef 29, a complete-when condition, and AI lists 0x409, 0x40B),
  `UsetuplenZ` (credits: 271 -> 299 entries), `UsetupsiloZ` (intro 31 -> 35
  records), `UsetupstatueZ` (45 -> 46 AI lists), `UsetuptraZ` (AI list
  0x1003). Pads, bound pads (positions, vectors, names, boxes) and propDef
  counts are identical in all three regions (measured).
- EU runs the doors' `accel`, `decel` and `max_speed` 1.2 times faster at load
  (from the decompilation; the file values are the same). Export the file
  values.

## Recommended JSON: `setup.json`

One file per (level, mode): `<out>/<level>/setup.json` for solo and
`<out>/<level>/setup_mp.json` for multiplayer (temple, complex, caves,
basement, stack and library have only `setup_mp.json`). Numbers are JSON
numbers; floats printed so they read back to the same float32 (shortest
round-trip, as `missionFloatText` does); `raw` fields are lower-case hex of
the file bytes.

```json
{
  "format": "ge007-setup", "version": 1,
  "level": "bunker1", "stage": 9, "mode": "solo",
  "setup": "UsetupsevbunkerZ", "region": "ntsc-final",
  "file": { "rom_offset": 9261456, "compressed_size": 6704, "size": 41056,
            "sha1": "ce3bafe8c0bddbc9aebd73e25c74a10691f08805" },
  "level_scale": 0.53931433, "scale": 1.8542062,
  "header": [36172, 35392, 34352, 8444, 37164, 38556, 40, 4704, 36908, 35488],
  "pads": [
    { "num": 0, "pos": [1092.1274, 64.89722, 730.55725], "file_pos": [589, 35, 394],
      "up": [0, 1, 0], "look": [0.970137, 0, -0.242556],
      "tile": "p609b", "stan_id": 155912 }
  ],
  "bound_pads": [
    { "num": 10000, "pos": [...], "file_pos": [...], "up": [...], "look": [...],
      "tile": "p334a2", "stan_id": 85506,
      "bbox": { "min": [xmin, ymin, zmin], "max": [xmax, ymax, zmax] },
      "file_bbox": [xmin, xmax, ymin, ymax, zmin, zmax] }
  ],
  "waypoint_names": ["PAD_leave_sev_all_p", "..."], "group_names": ["ha7_locssev_all_p", "..."],
  "props": [
    { "prop": 120, "type": 9, "kind": "guard", "offset": 26816,
      "chr": 0, "pad": 65, "body": 2, "ailist": 1025,
      "fields": { "extrascale": 0, "state": 0, "preset": 10022, "chrpreset1": 65535,
                  "health": 1950, "reaction_time": 70, "bitflags": 0, "head": -1 },
      "raw": "0000000900000041..." },
    { "prop": 100, "type": 1, "kind": "door", "offset": 22176,
      "model": 138, "pad": 10008, "pad_raw": 8, "load_on": [...],
      "fields": { "extrascale": 256, "state": 0, "flags": 1073741824, "flags2": 0,
                  "maxdamage": 0, "damage": 1000,
                  "door": { "linked_door_offset": 1, "max_frac": 0.79998779296875, "...": 0 } },
      "refs": { "linked_door": 101 },
      "raw": "..." }
  ],
  "objectives": [
    { "prop": 5, "number": 0, "text": 30749, "min_difficulty": 1,
      "conditions": [ { "prop": 6, "type": 25, "tag_id": 0, "object": 48 } ] }
  ],
  "briefing": [ { "prop": 0, "page": 0, "text": 30744 } ],
  "ailists": [ { "id": 1025, "class": "chr", "offset": 37188, "size": 7, "raw": "200005fd000704" } ],
  "waypoints": [ { "pad": 101, "group": 0, "neighbours": [2] } ],
  "waypoint_groups": [ { "waypoints": [0, 1, 2, 3, 4, 5], "neighbours": [6] } ],
  "patrol_paths": [ { "id": 0, "loop": false, "waypoints": [36, 40, ...], "pads": [...] } ],
  "intro": [ { "type": 0, "kind": "spawn", "pad": 102, "slot": 0 } ],
  "credits": null
}
```

Rules:

- `props` has **one entry per propDef record, every type, in stream order**,
  with `prop` = its index; nothing is dropped (gated, run-time-only and
  non-object records included). Every entry has `type`, `kind` (the names of
  the type table, snake_case), `offset` and `raw` (all the record's bytes).
- Object types (obj in the table) add `model` (s16 bytes 4-5), `pad`,
  `pad_raw`, `fields` and `load_on`; `pad` is the normalised pad: a door's
  `pad_raw` + 10000; `null` when `flags & 0x8000` (then `refs.inside` = the
  container's index) or `flags & 0x4000` (then `carrier` = the chr number, as
  the editor's `SetupProp.carrier`).
- `item`: collectable -> `weaponnum`; key -> `keyflags` (what
  `missionSetupDecode` fills in today); otherwise absent.
- `fields` has exactly the field set `missionRecJson` writes for that type
  (common fields flat, the type's own fields under the group name `door`,
  `key`, `cctv`, `magazine`, `collectable`, `monitor`, `multi_monitor`,
  `autogun`, `ammo`, `armour`, `vehicle`, `aircraft`, `tank`,
  `tinted_glass`), with the same kinds (fixed values as raw / 65536). That
  keeps `setup.json` and `mission.json` field names and values identical.
  Measured: all 50 407 mission-field values of every setup in the US sidecar
  read the same from the ROM bytes at the offsets of this page.
- Types the mission system has no table for (tag, link, switch, objectives,
  rename, guard attribute, camera position, safe item, lock door, door scale,
  vehicle/aircraft `ailist`) get their fields from the type table here under
  `fields`, and their relative offsets resolved under `refs` (`linked_door`,
  `inside`, `tagged`, `renamed`, `first` / `second` (link), `switch` / `door`
  (switch), `door` / `lock` (lock door), `item` / `safe` / `door` (safe item)).
- `load_on`: from `flags2`: `{"agent", "secret", "00", "007", "multi", "2p",
  "3p", "4p"}`, each true unless its "don't load" bit is set.
- `ai`: guards' `ailist` (also written as `ai` for the editor), vehicles' and
  aircraft's `ailist`.

### What `level.json` has that the setup does not, and the reverse

`level.json` (`port/src/levelexport.c`) carries the rooms' bounds, the stan
polygons in world units (used by the editor to give each pad a room by
point), the portals, the level `scale` and the model owners of `level.rec`.
None of these are in the setup. Its pads and props are no longer in it: the
editor already reads them from the converted setup sidecar
(`missionSetupDecode`), and `setup.json` carries the same values straight
from the ROM, so the editor can swap its `missionSetupLoadFile` +
`missionSetupDecode` for a `setup.json` reader without changing what it
shows. A pad's room can instead come from its `stan_id` through the stan
file (stan.md), which the static export can resolve without the game.

The setup has what `level.json` never had: pad names, objectives, briefing
text ids, tags and links, AI list bytes, the navigation graph, patrol paths,
the intro and the credits.

## Verification

### Counts (US) the implementer must reproduce

| | Bunker 1 (`UsetupsevbunkerZ`) | Dam (`UsetupdamZ`) |
|---|---|---|
| decompressed size, SHA-1 | 41056, `ce3bafe8c0bddbc9aebd73e25c74a10691f08805` | 81808, `7f20d446c1642eaee235270eebcf2f616b08d747` |
| pads | 105 | 367 (13 with an empty tile name) |
| bound pads | 54 | 95 |
| propDef records (before END) | 205 | 328 |
| guards (type 9) | 24 (no random body) | 36 (no random body) |
| AI lists; bytes of all lists in table order, SHA-1 | 19; 1354, `653647f392157f62fc88e44423b60baf0b96db2e` | 28; 1582, `1cee450630f1a9cea1decf75778f3ca81855b966` |
| waypoints, groups, patrol paths | 45, 7, 2 | 205, 23, 8 |
| intro records (END included) | 33 | 33 |
| objectives (type 23 groups) | 5 | 4 |

Records by type:

- Bunker 1: 1:17, 3:24, 4:2, 5:2, 6:4, 8:25, 9:24, 10:31, 11:1, 12:2, 17:23,
  18:7, 20:1, 21:2, 22:10, 23:5, 24:5, 25:4, 26:2, 27:3, 30:1, 34:1, 35:5,
  37:2, 42:1, 46:1.
- Dam: 1:18, 3:97, 5:4, 8:84, 9:36, 10:5, 11:4, 17:1, 19:4, 21:2, 22:18,
  23:4, 24:4, 25:4, 26:3, 27:3, 35:5, 37:1, 38:2, 39:1, 42:20, 46:3, 47:5.

Against the game's capture (`ge007 -level_NN --export-level DIR`, the
captures of both levels in the lead's scratch area, `rec/bunker1` and
`rec/dam`): the set of model numbers of all object records (plus the ammo
box slot models with a quantity) equals the set of `models[]` entries with
`guard: false` (Bunker 1: 38 models, Dam: 27), and the set of guard `body`
values equals the `guard: true` entries (Bunker 1: 2, 6, 18; Dam: 2, 18,
37); `level.json`'s `scale` equals `1 / level_scale` (measured, both levels).
Against the converted sidecar (`data/pccg-ntsc-final`, what the editor reads
today): every pad, bound pad, box, record type sequence, AI list id and
mission field equals the ROM decode (measured, all setups in the sidecar).

### Example records (US)

Bunker 1:

1. Pad 0 (offset 0x28): file pos (589, 35, 394), up (0, 1, 0), look
   (0.970137, 0, -0.242556) as float32, tile `p609b`, stan id `0x026108`;
   world pos (1092.12744140625, 64.897216796875, 730.5572509765625) with the
   float32 scale 1.8542062044143677.
2. Bound pad 10000 (offset 0x1260): file pos (65, 29, -309), up (-1, 0, 0),
   look (0, 1, 0), tile `p334a2`, stan id `0x014E02`, file box xmin
   -4.000001907348633, xmax 4.0, ymin -1.0, ymax 23.000001907348633, zmin
   -29.0, zmax 94.0; world box -7.416828155517578, 7.416824817657471,
   -1.8542062044143677, 42.64674758911133, -53.77198028564453,
   174.29537963867188.
3. Prop 120, guard (offset 0x68C0), raw
   `0000000900000041000204012726ffff079e00460000ffff00000000`: chrnum 0, pad
   65, body 2, ailist 0x401 (1025), preset 10022, chrpreset1 65535, health
   1950, reaction_time 70, bitflags 0, head -1.
4. Prop 100, door (offset 0x56A0): model 138, pad_raw 8 (pad 10008), flags
   0x40000000, flags2 0, damage 1000, linked_door_offset 1 (door 101),
   max_frac 0.79998779296875 (raw 0xCCCC), perim_frac 1.0, accel = decel =
   0.0012359619140625 (raw 0x51), max_speed 0.024993896484375 (raw 0x666),
   door_flags 4, door_type 0, keyflags 0, auto_close_frames 900, open_sound 3.
5. Prop 29, tag: tag_id 4, offset +1 -> tags prop 30, a collectable with
   model 248 (`PchrgoldeneyekeyZ`), pad 56, weaponnum 61.

Dam:

1. Pad 0: file pos (3807, -18, 2997), up (0, 1, 0), look (1.0, 0.0,
   1.9999999949504854e-06), tile `p1988e`, stan id `0x07C420`; world
   (16294.2998046875, -77.04161071777344, 12827.427734375), scale
   4.280089378356934.
2. Prop 23, guard (offset 0x59D8), raw
   `00000009000000010025040d0001ffff03e800640000ffff00000000`: chrnum 0,
   pad 1, body 37, ailist 0x40D, preset 1, chrpreset1 65535, health 1000,
   reaction_time 100, bitflags 0, head -1.
3. Prop 267, door (offset 0xC830): model 178 (`PdamgatedoorZ`), pad_raw 6,
   flags 0xC2000000, flags2 0x40000004, linked_door_offset 1, max_frac
   0.9499969482421875, perim_frac 1.0, accel = decel = 0.000244140625,
   max_speed 0.00311279296875, door_flags 12, door_type 0,
   auto_close_frames 1500, open_sound 3.
4. Prop 122, prop (offset 0x7EC0): model 62, pad 358, extrascale 512 (2.0),
   flags 0x101, flags2 0x200, damage 250.
5. Prop 270, switch: offset1 -1 (prop 269), offset2 -3 (prop 267, the door
   above).

Intro (Bunker 1, the first records): five cameras (type 6), the first
(41331, 11454, 96971) / 100, yaw raw 354612, pitch raw 13725, pad 51, text1
30764; then watch time 10:20; spawn pad 102 slot 0; item 5 / -1; ammo 1 x 100;
items 46, 40; ammo 27 x 10; item 55; spawn pad 80 slot 1; ...; cuff 3;
animation 1; nine swirl points; END.

Objectives (Bunker 1): a (text 30749, min difficulty 1): destroy tags 0, 1,
2, 3; b (30750, 0): copy item 29; c (30751, 2): complete when flags 0x100,
fail when 0x200; d (30752, 2): complete 0x400, fail 0x800 and 0x200;
e (30753, 0): photograph tag 5. Briefing pages 0..4: texts 30744..30748.

## Pitfalls

- **Multiplayer vs solo**: a different file, not a filtered solo file. The
  multiplayer files have no AI lists, no waypoints and no patrol paths; their
  intro holds the spawn points. In multiplayer the game replaces each
  collectable whose `weaponnum` is 0xF0..0xF7 by the chosen weapon set's slot
  0..7 (weapon, model and `extrascale` rewritten at load) and sets the ammo
  boxes' quantities from it (from the decompilation): export the file values
  and mark those collectables `mp_weapon_slot`.
- **Records the game rewrites at load** (export the file form; porting-notes
  D65): all fixed fields become floats; door `max_frac` ... `max_speed`
  (EU also x 1.2); armour amount copied; tinted glass `portalnum` computed
  and `unk90` converted only when the glass has a portal on a bound pad;
  vehicle / aircraft `ailist` replaced by the list's address and their speed
  fields reset; camera position (46) converted; intro swirl and camera
  converted; pad positions scaled; an object's height is dropped onto the
  floor (D69), so `pos` is the pad's, not the object's final height.
- **Door pad without 10000**, other objects with; the guard pad always plain.
- **`pad` is not always a pad**: `flags & 0x8000` = relative record offset,
  `flags & 0x4000` = chr number.
- **The model field is the setup's own number**: use it as is with models.md's
  table. The PC build's extra negative enumerator in the model enum
  (porting-notes D3, D417) does not shift any number; nothing in the file is
  offset by it.
- **Relative offsets count records, not bytes**, and include non-object
  records (tags, objectives, text) in the count.
- **Unused types 0, 15, 16, 41**: never in a retail file; the game's size for
  them is one word, which would desynchronise the walk if 41 (an "object") ever
  appeared. Refuse a file that has one.
- **AI list ends**: walk the byte code with the length table and the 0xAD
  string rule; do not take "up to the next list".
- **Port-side converter fixups a static reader does NOT need**: the PC sidecar
  (`port/convert/conv_d88.c`) widens pointer slots, re-orders sub-word fields
  for little-endian and zeroes run-time pointers; its record sizes and field
  offsets therefore differ from this page (the mission field table's offsets
  are the sidecar's: N64 offset = sidecar offset - 16 for the common and most
  type fields, -20 for vehicle / aircraft / tank fields, -28 for the monitor
  fields, -64 for the multi-monitor images, -32 for the autogun's
  `is_active`). A ROM reader uses only the offsets here. The one converter
  rule that carries over: the vehicle / aircraft `ailist` word holds an id the
  game resolves (porting-notes D123), not a pointer; export it.
- **Debug name tables** are not pad names; do not attach them to pads.

## UNCERTAIN

- Intro `slot` meaning (spawn / item / ammo word 3): values 0, 1, 2 seen
  (Bunker 1 has three spawn sets). Settle by reading the intro walk's use of
  the word (demo playback or player selection) and checking a co-op start.
- Meaning of the autogun and tank raw words, the monitor `owner_offset` /
  `owner_part` (whether `owner_offset` is a relative record offset), and the
  copy-item objective's word: kept raw. Settle per field against the
  decompilation when a consumer needs it.
- That every "run time" byte is 0 in every retail file: not checked byte by
  byte; `raw` keeps them either way.
- Whether `Ump_setupstatueZ` / `Ump_setupcradZ` are reachable from the
  multiplayer menu.

## Open questions

- Whether the editor should take pad rooms from the stan file (`stan_id`)
  instead of `level.json`'s point test.
- Whether the briefing / objective text ids should be resolved to strings
  here (the language files are a separate format, not covered).
