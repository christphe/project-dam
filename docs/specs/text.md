# Game text (language banks)

This page covers where the game's text is in the ROM and how it is stored:
item and weapon names, level and mission names, briefings, objectives, menu
strings, the multiplayer character names, and the in-level messages. It also
covers the small tables that link game objects (items, levels, multiplayer
stages and characters) to their string ids, and the static export the editor
and a future engine read names from (`text_<lang>.json` + `names.json`).

It builds on [level-decor.md](level-decor.md) (the ROM file table,
rarezip) and [models.md](models.md) (the compressed data segment "cdata"
and its base addresses). It does not repeat them.

Unless a line says otherwise, every value below was **measured on the US ROM**
(`ntsc-final`) with a scratch script that reads the ROM through the file
table in `port/convert/convert_data.c`. EU (`pal-final`) and JP (`jpn-final`)
values were measured the same way and are given where they differ. This page
gives string ids and the format. It does not reproduce the game's text,
apart from a few short item names that verification needs.

## Languages per ROM

Every bank exists once per language, as one ROM file per bank and language.
The language is the last letter of the file name:

| Suffix | Language | In the US ROM | In the EU ROM | In the JP ROM |
|---|---|---|---|---|
| `E` | English (US spelling) | yes, **used by the game** | yes (unused) | yes (unused) |
| `P` | English (UK spelling, PAL) | no | yes, **used by the game** | no |
| `J` | Japanese | yes (unused) | yes (unused) | yes, **used by the game** |

No ROM carries French, German or any other language. From the decompilation:
the build fixes which suffix the game loads (US → `E`, EU → `P`, JP → `J`),
with no run-time switch. The other files are leftovers that the game never loads.

Measured differences between the copies:

- The JP ROM's `E` files are the EU ROM's `E` files, except `LdestE` (which
  matches the US one) and `LwaxE` (below). The US `E` files are an earlier
  revision: 1785 strings against 1811.
- The US ROM's `J` files are an earlier revision of the JP ROM's `J` files:
  1741 strings against 1850; 282 ids differ (173 changed, 109 missing in the
  US). Use the JP ROM for Japanese.
- `P` differs from the same ROM's `E` only in spelling and a few strings: 100 ids
  differ between EU `E` and EU `P`, 47 of them because of `Lwax` (below).
- String ids are **stable across regions**: wherever an id exists in two
  files, it means the same string. Later revisions only add ids at the end of
  a bank (for example `LTITLE` has 288 slots in the US and 304 in EU and JP).

## Where the banks are

### The files

One ROM file per bank and language, named `L<stem><suffix>` in the ROM file
table (`cv_regions[].files`, rows `{offset, size, basename}`; look the row up
by basename as level-decor.md does): `LgunE`, `LtitleJ`, `LdamP`, ... Measured:
every one of the 88 (US, JP) / 132 (EU) bank files is **one rarezip block**
(starts with `0x11 0x72`, raw DEFLATE after the two header bytes) and
decompresses completely. The name has no `Z` suffix, but the file is still
compressed. The row's size is the compressed size, padded to 16. The
decompressed length of every bank is a multiple of 16.

### Bank numbers

A **string id** is a 16-bit number: `bank = id >> 10`, `index = id & 0x3FF`
(that is, `id = bank * 1024 + index`). Bank 0 has no file. The highest bank
is 44, so ids run from `0x0400` to `0xB3FF`. In records, ids are stored as
big-endian unsigned 16-bit values. Id 0 means "no text".

The banks, in number order (the names are the decompilation's enum labels, the
file stem is the ROM file name without the language letter, and the purpose
is from the decompilation's comments and from measuring which records use
the ids):

| # | Name | File stem | Purpose |
|---|---|---|---|
| 0 | LNULL | (none) | unused, no file |
| 1 | LAME | `Lame` | Library (multiplayer), empty |
| 2 | LARCH | `Larch` | Archives |
| 3 | LARK | `Lark` | Facility |
| 4 | LASH | `Lash` | Stack (multiplayer), empty |
| 5 | LAZT | `Lazt` | Aztec |
| 6 | LCAT | `Lcat` | Citadel (multiplayer), empty |
| 7 | LCAVE | `Lcave` | Caverns |
| 8 | LAREC | `Larec` | Control |
| 9 | LCRAD | `Lcrad` | Cradle |
| 10 | LCRYP | `Lcryp` | Egyptian |
| 11 | LDAM | `Ldam` | Dam |
| 12 | LDEPO | `Ldepo` | Depot |
| 13 | LDEST | `Ldest` | Frigate |
| 14 | LDISH | `Ldish` | Temple (multiplayer), empty |
| 15 | LEAR | `Lear` | unused level, empty |
| 16 | LELD | `Leld` | unused level, empty |
| 17 | LIMP | `Limp` | Basement (multiplayer), empty |
| 18 | LJUN | `Ljun` | Jungle |
| 19 | LLEE | `Llee` | unused level, empty |
| 20 | LLEN | `Llen` | Cuba (also the end credits) |
| 21 | LLIP | `Llip` | unused level, empty |
| 22 | LLUE | `Llue` | unused level, empty |
| 23 | LOAT | `Loat` | Caves (multiplayer), empty |
| 24 | LPAM | `Lpam` | unused level, empty |
| 25 | LPETE | `Lpete` | Streets |
| 26 | LREF | `Lref` | Complex (multiplayer), empty |
| 27 | LRIT | `Lrit` | unused level, empty |
| 28 | LRUN | `Lrun` | Runway |
| 29 | LSEVB | `Lsevb` | Bunker 2 |
| 30 | LSEV | `Lsev` | Bunker 1 |
| 31 | LSEVX | `Lsevx` | Surface 1 |
| 32 | LSEVXB | `Lsevxb` | Surface 2 |
| 33 | LSHO | `Lsho` | unused level, empty |
| 34 | LSILO | `Lsilo` | Silo |
| 35 | LSTAT | `Lstat` | Statue |
| 36 | LTRA | `Ltra` | Train |
| 37 | LWAX | `Lwax` | unused level, empty (but see the pitfalls) |
| 38 | LGUN | `Lgun` | item and weapon names, watch and HUD weapon text |
| 39 | LTITLE | `Ltitle` | front end: legal screen, mission and level names, multiplayer stage and character names, end-credit cast |
| 40 | LMPMENU | `Lmpmenu` | multiplayer menus |
| 41 | LPROPOBJ | `Lpropobj` | pickup messages ("picked up" plus articles) |
| 42 | LMPWEAPONS | `Lmpweapons` | multiplayer weapon-set names |
| 43 | LOPTIONS | `Loptions` | the in-game (watch) menus |
| 44 | LMISC | `Lmisc` | cheat names and messages, objective words |

From the decompilation: banks 38-44 are loaded at boot and stay loaded. A
level bank (1-37) is loaded only while its stage runs, and while the briefing
of that stage shows. The game maps a stage to its bank with a fixed switch.
It is a table of facts (stage numbers are the game's level ids, the same
numbers `level.json` and the folder table below use):

| Level id | Stage | Bank | | Level id | Stage | Bank |
|---|---|---|---|---|---|---|
| 9 | Bunker 1 | 30 | | 33 | Dam | 11 |
| 20 | Silo | 34 | | 34 | Facility | 3 |
| 22 | Statue | 35 | | 35 | Runway | 28 |
| 23 | Control | 8 | | 36 | Surface 1 | 31 |
| 24 | Archives | 2 | | 37 | Jungle | 18 |
| 25 | Train | 36 | | 38 | Temple | 14 |
| 26 | Frigate | 13 | | 39 | Caverns | 7 |
| 27 | Bunker 2 | 29 | | 41 | Cradle | 9 |
| 28 | Aztec | 5 | | 43 | Surface 2 | 32 |
| 29 | Streets | 25 | | 45 | Basement | 17 |
| 30 | Depot | 12 | | 46 | Stack | 4 |
| 31 | Complex | 26 | | 48 | Library | 1 |
| 32 | Egyptian | 10 | | 50 | Caves | 23 |
| | | | | 54 | Cuba | 20 |

Any other level id has no bank. From the decompilation, the game hangs when
asked for one. The banks LCAT, LEAR, LELD, LLEE, LLIP, LLUE, LPAM, LRIT, LSHO
and LWAX are reached by no stage.

## Bank file layout

After decompression (all values big-endian):

1. **Offset table**: `slots` unsigned 32-bit entries. Entry `i` is the byte
   offset of string `index = i` from the **start of the decompressed bank**, or
   0 when the slot has no string. The table length is not stored:
   `slots = (smallest non-zero entry) / 4`. A bank whose entries are all zero
   (the empty banks: 32 bytes, all zero, measured) has `slots = length / 4`
   and no strings.
2. **Strings**: zero-terminated byte strings, in slot order. Each string
   starts on a 4-byte boundary, and the padding after the terminator is
   zero bytes. The file is zero-padded to a multiple of 16.

Measured on all 308 bank files of the three ROMs: every non-zero offset is
4-aligned, lies inside the file, and the offsets increase strictly with the
slot number. No two slots share a string. The table is padded with zero
slots so that the first string starts 16-aligned: every slot count is a
multiple of 4. Some trailing zero slots are therefore padding, not missing
strings. An index at or past `slots` has no string.

**Relocation**: none in the ROM. From the decompilation: the game keeps the
bank where it loads it and adds the bank's address to the slot's offset on
each lookup, and a 0 offset yields "no string". An exporter reads the
offsets directly. (The PC port byte-swaps the table in place after loading,
with a heuristic that stops at the first implausible value. An exporter must
not copy that: it uses the slot count rule above.)

## Character encoding

### English (`E`, `P`)

Measured: every byte of every string in every `E` and `P` file is either `0x0A`
or in `0x20`-`0x7E`. The text is plain ASCII:

| Byte | Meaning | Export as |
|---|---|---|
| `0x00` | end of string | (not exported) |
| `0x0A` | line break | `\n` (JSON newline) |
| `0x20` | space (advance 5 pixels, no glyph) | space |
| `0x21`-`0x7E` | one glyph of the Latin font the caller draws with, ASCII order ([fonts.md](fonts.md)) | the same ASCII character |

There are **no** colour, button, variable or formatting codes in the banks.
Measured: no `%`, `{` or `}` byte in any bank of any language or region. Colours, outlines, button prompts and numbers are
added by the code that draws the text (see the pitfalls). Most strings (1496
of 1785 in US `E`) end with `0x0A`. The line break is part of the string and
the export keeps it.

### Japanese (`J`)

A byte below `0x80` is ASCII, exactly as above. The `J` banks use it for
Latin words, digits and punctuation, drawn with the English font. A byte at
or above `0x80` starts a **two-byte glyph code** `b0 b1`:

- `code = ((b0 & 0x7F) << 7) | (b1 & 0x7F)` (14 bits).
- If `code & 0x2000` (that is, `b0 >= 0xC0`): a **wide** glyph, index
  `code & 0x1FFF` in the wide font. Otherwise a **narrow** glyph, index
  `code` in the narrow font.
- Measured: in bank text, `b1` always has bit 7 set. The game masks it, so
  a decoder must not depend on it. Code built at run time (below) uses a
  second byte without bit 7.
- From the decompilation: the game draws an index (narrow or wide, after
  the `0x1FFF` mask) at or above `0x3C7` (US) or `0x3C8` (EU, JP) as narrow
  glyph 2 ([fonts.md](fonts.md)). Measured: the highest narrow index in any
  bank is 966 (US `J`) and 967 (EU and JP `J`), so no bank text hits that
  limit.
- Measured: wide glyphs occur only in `LTITLE`, 241 times in each region's
  `J` file (the end-credit cast list).

The glyph indices are **not** a standard character set. There is no table in
the ROM that maps them to Unicode. The ROM only holds the glyph bitmaps: the
narrow font (976 slots, 16 x 12 cells) and the wide font (106 slots, 16 x 16
cells). Where they are, their bitmap layout, metrics and how they are drawn:
[fonts.md](fonts.md).

Narrow glyph order, measured visually on the rendered JP sheet (32 glyphs
per row): 0-255 hold punctuation, full-width digits and Latin letters,
katakana (small forms first), hiragana and a few symbols. Kanji run from 256
to the end. The order is game-specific: neither JIS nor Unicode order.

**Converting Japanese to UTF-8 needs a glyph table that is not in the ROM**
(976 narrow + 106 wide entries, index → Unicode character). UNCERTAIN: no
such table exists in the repo. What would settle it: render both sheets with
the layout above, transcribe them by hand (or with OCR plus review), and
check the result against the in-game look. Until that table exists, the
export writes each glyph as an escape (below). The escapes are lossless and
can be converted later without reading the ROM again.

## The export

### `text_<lang>.json`, one per language file set in the ROM

Write one file for each suffix present in the ROM: US gives `text_en.json`
(`E`) and `text_ja.json` (`J`). EU gives `text_en.json` (`E`),
`text_en-GB.json` (`P`) and `text_ja.json` (`J`). JP gives `text_en.json`
(`E`) and `text_ja.json` (`J`). Mark the one the game uses.

```json
{
  "format": "ge007-text/1",
  "region": "ntsc-final",
  "suffix": "E",
  "language": "en",
  "game_language": true,
  "banks": [
    { "bank": 38, "name": "LGUN", "file": "LgunE", "slots": 224, "strings": 223 }
  ],
  "strings": {
    "9840": { "bank": "LGUN", "index": 64, "text": "Hunting Knife\n" }
  }
}
```

- Key: the id as 4 upper-case hex digits. Slots with offset 0 are left out.
  The 44 banks are listed in `banks` even when empty.
- `text`: UTF-8. English: the bytes as ASCII, with `0x0A` as a JSON `\n`.
  Japanese: ASCII bytes as above. A narrow glyph becomes its Unicode
  character when a glyph table is supplied, else `{j:NNN}` (decimal index). A
  wide glyph becomes `{jw:NN}` in the same way. A literal `{` never occurs in
  bank text (measured: no `{` byte in any file), so the escapes cannot
  collide.
- Also write `raw` (the bytes as hex) when the glyph table is missing, so
  that a later pass can redo the conversion.

### `names.json`: the ids the editor needs

Every entry gives the **string id** (hex) and, for convenience, the text in
the ROM's game language with the final `\n` removed. Other languages are a
lookup in `text_<lang>.json`. The sources are tables in the decompressed cdata
block (models.md: ROM offset US `0x21990`, EU `0x1F850`, JP `0x219D0`; cdata
base address US `0x80020D90`, EU `0x8001EC50`, JP `0x80020DD0`; a pointer
minus the base is an offset into the decompressed cdata). All tables were
measured in all three regions, and their string ids are identical in the
three except where stated.

**1. Items and weapons** (item id → names). The table is indexed by the game's
item id (the number scripts and setup records use; the editor's
`item_names.h` holds the same ids with enum labels). It has 89 records of 56
bytes:

| Region | cdata offset |
|---|---|
| US | `0x12B94` |
| EU | `0x10224` |
| JP | `0x12B94` |

| Bytes | Field |
|---|---|
| 0-3 | pointer to the item's model header (0 = none) |
| 4-7 | pointer to the first-person model file name (`"GknifeZ"`; item 84 points at a `C...Z` name; 0 = none) |
| 8-11 | signed 32-bit: 1 if the item has no first-person model |
| 12-15 | pointer to the weapon statistics (0 = none) |
| 16-17 | string id: watch title, first line |
| 18-19 | string id: watch title, second line |
| 20-39 | watch display position and rotation (floats; not text) |
| 40-41 | string id: **long name** (the weapon-of-choice screen and the inventory's long-name lookup) |
| 42-43 | string id: **short name**, lower case (the watch inventory list) |
| 44-55 | floats (not text) |

From the decompilation: the inventory uses the short name and the
weapon-of-choice screen uses the long name. Both default to these records,
unless a setup file overrides them for an object (see the pitfalls).
Invariants (all three regions): record 0 has bytes 0-15 = `0,0,1,0` and ids
`0x9883 0x9811 0x9811 0x9811`; record 1's file name is `GfistZ`; every one
of the 356 ids is in bank 38. Records 84-87 have empty long and short names
(id `0x9883`, the empty line). Record 88 is the multiplayer token. How the
table was found (for another region): the unique position where those 20
bytes of record 0 are followed, 56 bytes later, by a record whose name
pointer resolves to `GfistZ`.

In `names.json`: `"items": [{ "item": 2, "file": "GknifeZ", "long": "9840", "short": "98A8", "watch": ["9883", "9846"], ... }]`,
each id with its text.

**2. Missions and levels (solo).** This is the mission-select folder table:
29 records of 28 bytes and a terminator.

| Region | cdata offset |
|---|---|
| US | `0x9E54` |
| EU | `0x74E4` |
| JP | `0x9E54` |

| Bytes | Field |
|---|---|
| 0-3 | pointer to an ASCII label in cdata: `"1"`-`"9"` for a mission, `"i"`-`"v"` for a part |
| 4-5 | string id: full name (mission name, or the level's long title) |
| 6-7 | string id: short name (0 = same as the full name) |
| 8-11 | signed 32: level id, -1 for a mission header |
| 12-15 | signed 32: 0-4, rises with the missions (UNCERTAIN: probably the folder page) |
| 16-19 | signed 32: 1 = mission header, 0 = level |
| 20-23 | signed 32: solo stage number 0-19 (-1 for a header) |
| 24-27 | pointer to the briefing file name (`"UbriefdamZ"`), 0 for a header |

The terminator has a label pointer of 0, both ids 0, level id -1, stage -1.
There are 9 mission headers and 20 levels. Mission 5 repeats mission 2's name
id, and its two levels repeat the ids of mission 2's levels (Surface,
Bunker): key levels by level id, not by name id. All ids are in bank 39. Invariant: record 0 is
`"1"` with id `0x9C78`, record 1 is level 33 with id `0x9C79` and
`UbriefdamZ`, and the records are identical in the three regions.

**3. Briefings and objectives per level.** One file per solo level, named by
the folder table: `Ubrief<stem>Z`, 20 files. Each is one rarezip block that
decompresses to 48 bytes: 4 big-endian u16 string ids (the briefing pages,
in order: overview, M, Q, Moneypenny), then 10 pairs `{u16 objective id,
u16 lowest difficulty}`. Difficulty is 0 Agent, 1 Secret Agent, 2 00 Agent.
From the decompilation: an objective is shown when the selected difficulty
is at or above that value. A 0 id is skipped. All ids are in the level's own
bank (above). Measured: the 20 files are identical in the three regions. They
hold 80 objectives in total. Example: `UbriefdamZ` uses page ids
`0x2C00`-`0x2C03` and objectives `0x2C04`-`0x2C07` with difficulties 1, 2, 2, 0.

**4. Multiplayer stages.** 12 records of 24 bytes.

| Region | cdata offset |
|---|---|
| US | `0xA2E4` |
| EU | `0x7974` |
| JP | `0xA2E4` |

Each record: u16 name id, u16 select-screen name id, s32 photo number, s32
level id (-1 = random), s32 solo stage that unlocks it (-1 = always), s32
minimum and s32 maximum player count. Record 0 has ids `0x9C9A`/`0x9C9B` (the
random stage). Identical in the three regions.

**5. Multiplayer characters** (the only character names the game holds as
text). 64 records of 12 bytes.

| Region | cdata offset |
|---|---|
| US | `0xA408` |
| EU | `0x7A98` |
| JP | `0xA408` |

Each record: u16 name id, u8 gender (1 male, 0 female), u8 photo number, s16
body id, s16 head id (the character model numbers of the characters spec),
then a float scale. Record 0 has name `0x9CB8` and body 5. From the
decompilation: the game offers the first 8, 33 or all 64, depending on
unlocks. The EU and JP tables are equal to each other but differ from the US
one in 21 name ids: the generic guard names use the newer ids
`0x9D1F`-`0x9D2D`, which exist only in the EU and JP `LTITLE`.

**6. Level id → bank**: the table in §Bank numbers, written out so that a
reader of a stage's script text knows which bank its ids come from.

## Verification

Counts per bank are written `strings/slots` (strings = non-zero slots,
slots = table length by the rule above). Columns are ROM and suffix:

| # | Stem | US E | US J | EU E | EU J | EU P | JP E | JP J |
|---|---|---|---|---|---|---|---|---|
| 1, 4, 6, 14-17, 19, 21-24, 26, 27, 33 | (empty banks) | 0/8 | 0/8 | 0/8 | 0/8 | 0/8 | 0/8 | 0/8 |
| 2 | `Larch` | 62/64 | 63/64 | 62/64 | 63/64 | 62/64 | 62/64 | 63/64 |
| 3 | `Lark` | 71/72 | 72/72 | 71/72 | 72/72 | 71/72 | 71/72 | 72/72 |
| 5 | `Lazt` | 36/44 | 44/44 | 36/44 | 44/44 | 36/44 | 36/44 | 44/44 |
| 7 | `Lcave` | 41/44 | 41/44 | 41/44 | 41/44 | 41/44 | 41/44 | 41/44 |
| 8 | `Larec` | 45/48 | 45/48 | 45/48 | 45/48 | 45/48 | 45/48 | 45/48 |
| 9 | `Lcrad` | 35/36 | 35/36 | 35/36 | 35/36 | 35/36 | 35/36 | 35/36 |
| 10 | `Lcryp` | 15/16 | 15/16 | 15/16 | 15/16 | 15/16 | 15/16 | 15/16 |
| 11 | `Ldam` | 34/36 | 34/36 | 34/36 | 34/36 | 34/36 | 34/36 | 34/36 |
| 12 | `Ldepo` | 30/36 | 35/36 | 30/36 | 35/36 | 30/36 | 30/36 | 35/36 |
| 13 | `Ldest` | 31/32 | 31/32 | 31/32 | 31/32 | 31/32 | 31/32 | 31/32 |
| 18 | `Ljun` | 42/44 | 42/44 | 42/44 | 42/44 | 42/44 | 42/44 | 42/44 |
| 20 | `Llen` | 130/140 | 55/56 | 140/148 | 148/148 | 140/148 | 140/148 | 148/148 |
| 25 | `Lpete` | 29/32 | 29/32 | 29/32 | 29/32 | 29/32 | 29/32 | 29/32 |
| 28 | `Lrun` | 26/32 | 30/32 | 26/32 | 30/32 | 26/32 | 26/32 | 30/32 |
| 29 | `Lsevb` | 104/108 | 108/108 | 104/108 | 108/108 | 104/108 | 104/108 | 108/108 |
| 30 | `Lsev` | 47/48 | 47/48 | 47/48 | 47/48 | 47/48 | 47/48 | 47/48 |
| 31 | `Lsevx` | 38/44 | 42/44 | 38/44 | 42/44 | 38/44 | 38/44 | 42/44 |
| 32 | `Lsevxb` | 31/36 | 35/36 | 31/36 | 35/36 | 31/36 | 31/36 | 35/36 |
| 34 | `Lsilo` | 71/72 | 71/72 | 71/72 | 71/72 | 71/72 | 71/72 | 71/72 |
| 35 | `Lstat` | 68/68 | 68/68 | 68/68 | 68/68 | 68/68 | 68/68 | 68/68 |
| 36 | `Ltra` | 45/48 | 45/48 | 45/48 | 45/48 | 45/48 | 45/48 | 45/48 |
| 37 | `Lwax` | 0/8 | 0/8 | 47/48 | 47/48 | 0/8 | 0/8 | 0/8 |
| 38 | `Lgun` | 223/224 | 223/224 | 223/224 | 223/224 | 223/224 | 223/224 | 223/224 |
| 39 | `Ltitle` | 287/288 | 287/288 | 302/304 | 302/304 | 302/304 | 302/304 | 302/304 |
| 40 | `Lmpmenu` | 36/36 | 36/36 | 36/36 | 36/36 | 36/36 | 36/36 | 36/36 |
| 41 | `Lpropobj` | 65/68 | 65/68 | 65/68 | 65/68 | 65/68 | 65/68 | 65/68 |
| 42 | `Lmpweapons` | 14/16 | 14/16 | 14/16 | 14/16 | 14/16 | 14/16 | 14/16 |
| 43 | `Loptions` | 61/64 | 61/64 | 61/64 | 61/64 | 61/64 | 61/64 | 61/64 |
| 44 | `Lmisc` | 68/68 | 68/68 | 69/72 | 69/72 | 69/72 | 69/72 | 69/72 |
| | **total strings** | **1785** | **1741** | **1858** | **1897** | **1811** | **1811** | **1850** |

SHA-1 of the 44 decompressed banks concatenated in bank order 1-44:

| Set | SHA-1 |
|---|---|
| US E | `d96c68685336dcf802af4d8aaab465d6b681f2ee` |
| US J | `6dc32d754c0b48b9917993bf342dbfe71aea61f6` |
| EU E | `456f969513944726d02ea4686b15cb85d1406ac1` |
| EU J | `78dd1128df28d425815b09e714f6a4f29f1bb19e` |
| EU P | `1824d661bfe5b0447f7d5160271f29b76f85dc0f` |
| JP E | `0601e5fdb4effab8081a4fcf5aa6dd19afa1e293` |
| JP J | `593afa7a783da5bfd40cf4b5823fe08a45fcc31c` |

Example ids (US `E`; the same in EU `P` and JP `E` unless stated):

| Id | Bank / index | Text | Used by |
|---|---|---|---|
| `0x9840` | 38 / 64 | `Hunting Knife\n` | item 2, long name |
| `0x9856` | 38 / 86 | `PP7 Special Issue\n` | item 4, long name |
| `0x986B` | 38 / 107 | `Klobb\n` | item 7, long name (the same ASCII bytes in every `J` file) |
| `0x9842` | 38 / 66 | `KF7 Soviet\n` | item 8, long name |
| `0x9866` | 38 / 102 | `Sniper Rifle\n` | item 17, long name |
| `0x9C79` | 39 / 121 | `Dam` (no line break) | folder table record 1 |

Spelling check: `0x9844` (item 46, second watch line) reads `Key Analyzer\n` in US `E`
and `Key Analyser\n` in EU `P`.

Japanese check (JP `J`, no glyph table needed): `0x9876` (item 1, long name)
is the bytes `82 DE 85 96 82 BC 81 C5 0A`, which are the narrow glyphs 350, 662,
316, 197, then a line break. The export writes `{j:350}{j:662}{j:316}{j:197}\n`.

`names.json` checks: 89 items, 29 folder records (9 missions + 20 levels), 20
briefings with 80 objectives, 12 multiplayer stages, 64 multiplayer
characters.

## Pitfalls

- **Strings built at run time are not in the banks.** From the
  decompilation: the pickup message is "picked up" + an article (`some` / `a` /
  `an` / `the`, all in LPROPOBJ) + the item name, joined by the code. Objective
  letters (`a.`, `b.`, ...) are added in front of objective text by the code.
  In Japanese the letter is a two-byte glyph code built from `0x80` and a
  computed byte, whose second byte has no bit 7. Kill counts, death counts and
  timers use format strings that live in code, with bank strings as
  arguments. Export the parts, never the joined text.
- **Strings in code or data, not in banks:** the folder labels `"1"`...`"v"`,
  every model and file name (`GknifeZ`, `P...Z`, `C...Z`), the format strings
  above and debug text. These are not translated and are not game text.
- **Per-level renames:** from the decompilation, a level's setup file can
  carry text overrides (an object or a weapon gets its own short, long and
  pickup ids, usually in the level's bank). Those are the names a player sees
  in that level. They belong to the setup export (the sibling setup spec),
  which should reference ids in this export. `names.json` gives only the
  defaults.
- **Level bank context:** an id in banks 1-37 resolves only while that stage's
  bank is loaded. A static export resolves every id, but a consumer must not
  assume that the game could show a stage-bank id outside its stage.
- **Region-only ids:** `LTITLE` ids `0x9D1F` and up (EU and JP; used by their
  multiplayer character table), the extra `Llen` and `Lmisc` slots in EU and
  JP, and the US `J` being an older revision. Ids are stable: never remap an
  id by region.
- **`Lwax` in the EU ROM** (`LwaxE` and `LwaxJ` only) holds a byte-identical
  copy of the same ROM's Bunker 1 bank (`LsevE` / `LsevJ`). No stage loads it.
  Export it as it is.
- **Do not normalise the text.** Leading spaces in the watch title lines
  (used for centring), trailing `\n`, lower-case short names and original
  spelling mistakes are data. `names.json` strips only the final `\n`.
- **The empty string is a real string:** id `0x9883` is a lone line break,
  which items use as "no title line". It is different from a 0 offset (no
  string).
- **The PORT's own strings are not ROM data** and must not be exported: the
  options and menu overlay text, the custom-mission UI labels, mission names,
  briefings and objectives written in `mission.json` or in mods, editor
  labels, and the enum-style labels in `editor/item_names.h` and
  `editor/model_names.h`. Port code that calls the game's text lookup (for
  example the custom-mission objective list) reads ROM ids. Its own literals are
  not text from the banks.

## The editor's hand-copied tables

- `editor/item_names.h`: the ids (-1, 0-88) are the item ids above. The
  names (`KNIFE`, `WPPKSIL`, ...) are the decompilation's enum labels, **not
  game text**, and no ROM table holds them. The export can add the display
  names (long, short, watch lines) next to them, keyed by the same id. It
  cannot replace the labels, which the script language uses as identifiers.
  Id -1 (`NOTHING`) has no record.
- `editor/model_names.h`: the file names (`CdjbondZ`, ...) are ROM data, but
  they come from the character model table (characters spec), not from the text
  banks. The labels (`Brosnan_Tuxedo`, `Jungle_Commando`, ...) are enum labels,
  not game text. The only character names the ROM holds as text are the 64
  multiplayer character records (body + head → name id). These name some
  body/head pairs, never all bodies or heads, and several share one generic
  name.
- Level names: the editor uses port-defined short tokens (`bunker1`), which
  are not game text. The export gives the display names per level id (folder
  table) and per multiplayer stage.

## Open questions

1. UNCERTAIN: the Japanese glyph → Unicode table (976 narrow + 106 wide). It is
   not in the ROM. Until it is transcribed and checked, `text_ja.json` carries
   escapes.
2. UNCERTAIN: the meaning of folder-table bytes 12-15 (0-4, rising with the
   missions). It is not needed for names.
3. Not measured here: which ids the setup files and AI scripts reference
   (in-game objectives, text overrides, on-screen messages). That is for
   the setup spec. This page only guarantees that every such id resolves
   through `text_<lang>.json`.
