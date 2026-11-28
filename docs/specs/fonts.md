# Fonts

This page covers the game's fonts: the two Latin fonts that draw every
English string and the ASCII part of the Japanese strings, and the two
Japanese glyph sets. It gives where each one lives in the ROM, the glyph
bitmap formats, the per-glyph metrics and the kerning table, how a string is
laid out (advance, space, line height), how colour is applied to the glyph
pixels, and a proposed `fonts/` export for the base pack.

It builds on [text.md](text.md) (the character encoding: which byte or byte
pair selects which glyph) and [level-decor.md](level-decor.md) (the ROM file
table). It does not repeat them.

**Measured** means read from the **US ROM** (`ntsc-final`, SHA-1
`abe01e4aeb033b6c0836819f549c791b26cfde83`) with a scratch script that reads
the file table rows of `port/convert/convert_data.c` and parses the bytes as
described here. No EU or JP ROM was available for this page: EU and JP values
are from the file table (offsets and sizes) or from the decompilation, and say
so. The Japanese facts marked **measured on three ROMs** were measured on
US, EU and JP when text.md was first written (they moved to this page).

## The fonts

| Key (proposed) | What it is | Glyphs | Format | Line height |
|---|---|---|---|---|
| `small` | Latin, the small font | 94 (codes `0x21`-`0x7E`) | 8-bit intensity, proportional, kerned | 13 |
| `large` | Latin, the large font | 94 (codes `0x21`-`0x7E`) | 8-bit intensity, proportional, kerned | 15 |
| `jp_narrow` | Japanese, narrow glyphs | 976 slots | 2-bit intensity, fixed 16 x 12 cell, drawn 11 wide | 14 (minimum, see below) |
| `jp_wide` | Japanese, wide glyphs | 106 slots | 2-bit intensity, fixed 16 x 16 cell, drawn 15 wide | 14 (minimum) |

There are no other fonts. From the decompilation: the ROM file table row
`...jfont_dl` (192 bytes, next to the Japanese font) is three generic
render-state command lists, not a font. Digits and symbols drawn as pictures
(the watch, the ammo counters' icons) are textures, not fonts.

## Where they are in the ROM

All four are stored **raw** (not compressed). Measured on US: the Latin
blocks parse as below from their first byte (the first byte of the kerning
table is `0x00`, not the rarezip `0x11 0x72`).

### Latin fonts

Each Latin font is one contiguous block. The file table splits it in two rows,
`<name>_kerning` (676 bytes) followed directly by `<name>_fontchartable`
(the glyph table plus the pixels). Read from the start of the kerning row to
the end of the chartable row. The row basenames are the same in the three
regions (no region prefix):

| Font | Row basenames | US offset | EU offset | JP offset | Block size |
|---|---|---|---|---|---|
| `small` | `fontBankGothic_kerning`, `fontBankGothic_fontchartable` | `0x2E63F0` | `0x2DC830` | `0x2E7110` | 9 392 (676 + 8 716) |
| `large` | `fontZurichBold_kerning`, `fontZurichBold_fontchartable` | `0x2E88A0` | `0x2DECE0` | `0x2E95C0` | 13 632 (676 + 12 956) |

Offsets and sizes: from the file table, the same sizes in all three regions.
The US blocks were measured. From the decompilation: the Latin font data has
no regional variant, so EU and JP should be byte-identical to US (not
measured). SHA-1 of the US blocks: `small`
`f6c05b9ebf775c3f02fbae9a1e0b7f094b7511ee`, `large`
`1a2b27c24a04c1b62949c7b691ac1b7652ecef40`.

### Japanese fonts

As text.md gives (repeated here because this page owns the font data):

| Font | Row basename | US offset | EU offset | JP offset | Size | Slots |
|---|---|---|---|---|---|---|
| `jp_narrow` | `ge007.u.117940.jfont_chardata` / `ge007.e.1048B0.jfont_chardata` / `ge007.j.118660.jfont_chardata` | `0x117940` | `0x1048B0` | `0x118660` | 46 848 | 976 |
| `jp_wide` | `ge007.u.123040.efont_chardata` (all three tables) | `0x123040` | `0x10FFB0` | `0x123D60` | 6 784 | 106 |

Look the rows up by the basename suffix (`jfont_chardata`, `efont_chardata`),
because the prefix differs per region. Measured on three ROMs: the wide font
is identical in the three ROMs, the EU and JP narrow fonts are identical, and
the US narrow font has the same glyph order (checked on a rendered sheet) with
slightly different pixels and no glyph 967.

## Latin font block layout

All values big-endian signed 32-bit unless stated. Offsets are from the start
of the block (the start of the kerning row).

| Offset | Size | Content |
|---|---|---|
| `0x000` | 676 | kerning table: 13 x 13 signed 32-bit values |
| `0x2A4` | 2 256 | glyph table: 94 records of 24 bytes |
| `0xB74` | 12 | filler (measured: `00 00 00 00  B8 00 00 00  00 00 00 00` in both fonts; ignore) |
| `0xB80` | rest | glyph pixels |

### Glyph record (24 bytes)

| Bytes | Field |
|---|---|
| 0-3 | glyph id: equals the record number (0-93) in every record except one (below). Only its value below `0x80` matters to the game: it marks a Latin glyph. Ignore it otherwise. |
| 4-7 | **top**: rows from the line's top to the glyph's first row (can be negative) |
| 8-11 | **height**: rows in the bitmap |
| 12-15 | **width**: columns drawn, and the glyph's box width for the advance |
| 16-19 | **kerning class**: 0-12, a row/column of the kerning table |
| 20-23 | unsigned: offset of the glyph's first pixel, from the start of the block |

- Record `i` is the glyph for character code `0x21 + i`. Code `0x21` (`!`) is
  record 0, code `0x7E` (`~`) is record 93. Measured: the 94 pixel offsets
  increase with the record number in both fonts.
- Measured quirk: in `small`, record 31 (`@`) has glyph id 61 (the id of `^`).
  Its metrics are equal to `^`'s, but it has its own pixel offset and its
  bitmap differs from `^`'s. Use the record number, not the id.
- There is **no glyph for a space** (`0x20`), nor for `0x7F` or the control
  bytes. Space is a fixed advance (below). From the decompilation: the game
  indexes the table with `code - 0x21` without a range check, so any other
  byte is undefined there. text.md measured that the banks hold only `0x0A`
  and `0x20`-`0x7E`.

### Glyph pixels

- **Format: 8-bit intensity**, one byte per pixel, rows top to bottom, left
  pixel first. Measured: the US `small` font uses 221 distinct values, `large`
  203; the highest value is 238 (`small`) and 234 (`large`): no Latin pixel is
  fully opaque. The values are not 4-bit values doubled (`0x11` steps): keep
  all 8 bits.
- **Row stride** = `width` rounded up to a multiple of 8 bytes. The bitmap is
  `stride x height` bytes at the record's pixel offset.
- **Padding columns** (`width` to `stride - 1`) are **not blank**: measured,
  every padding byte of both fonts equals the last real column of the same
  row (1 763 bytes in `small`, 3 042 in `large`; no exception). They are a
  copy of the edge, not ink. The game draws only `width` columns. An exporter
  drops them; they can be rebuilt by repeating the last column.
- **Between glyphs**: measured, each glyph's bitmap is preceded by 8 bytes
  `B8 00 00 00 00 00 00 00` (the last 8 filler bytes of the header for record
  0, a gap of exactly 8 bytes after each bitmap for the others). They are not
  pixels. The block ends with the last bitmap plus 8 zero bytes.

### Metrics, measured (US)

`small` (line height 13; capitals: top 2, height 8, so the baseline is at row
10 of the line; lower case: top 3, height 7; widths 1-9; heights 2-14; tops
-1 to 10):

| Code | top | height | width | class | | Code | top | height | width | class |
|---|---|---|---|---|---|---|---|---|---|---|
| `!` | 2 | 8 | 2 | 0 | | `H` | 2 | 8 | 7 | 0 |
| `,` | 7 | 4 | 2 | 3 | | `I` | 2 | 8 | 1 | 12 |
| `.` | 7 | 3 | 2 | 3 | | `W` | 2 | 8 | 9 | 4 |
| `0` | 2 | 8 | 7 | 0 | | `g` | 3 | 7 | 6 | 0 |
| `A` | 2 | 8 | 8 | 1 | | `q` | 3 | 8 | 6 | 0 |
| `\|` | -1 | 14 | 1 | 0 | | `_` | 10 | 3 | 6 | 0 |

`large` (line height 15; capitals: top 0, height 12, baseline at row 12;
x-height glyphs: top 3, height 9; descenders `g p q y`: top 3, height 11;
widths 2-14; heights 2-15; tops 0-13):

| Code | top | height | width | class | | Code | top | height | width | class |
|---|---|---|---|---|---|---|---|---|---|---|
| `!` | 0 | 12 | 2 | 0 | | `H` | 0 | 12 | 8 | 0 |
| `.` | 9 | 3 | 2 | 0 | | `W` | 0 | 12 | 14 | 4 |
| `0` | 0 | 12 | 7 | 0 | | `g` | 3 | 11 | 7 | 8 |
| `A` | 0 | 12 | 10 | 1 | | `j` | 0 | 14 | 3 | 6 |
| `\|` | 0 | 15 | 2 | 0 | | `m` | 3 | 9 | 11 | 12 |

The fonts store no baseline: the "baseline" above is where the capitals end,
measured, and is only a description. Glyphs are placed by **top** only.

### Kerning

The kerning table is 13 rows by 13 columns of signed 32-bit values, row =
class of the **previous** glyph, column = class of the **current** glyph
(index `prev * 13 + cur`). A value is subtracted from the gap before the
current glyph: positive pulls it closer. Measured: every value is -1, 0 or 1.

Class members, measured:

| Class | `small` | `large` |
|---|---|---|
| 0 | every glyph not listed below | every glyph not listed below |
| 1 | `A a` | `A` |
| 2 | `L l` | `L b h k` |
| 3 | `, . J j` | `J d` |
| 4 | `T V W Y t v w y` | `T V W Y` |
| 5 | `F P f p` | `F P` |
| 6 | (none) | `j` |
| 7 | (none) | `i l` |
| 8 | (none) | `a c e g o p q s x y z` |
| 9 | (none) | `f t` |
| 10 | (none) | `v w` |
| 11 | (none) | `r` |
| 12 | `I` | `m n u` |

Measured tables (row = previous class 0-12, columns 0-12):

`small`: rows 0, 3, 6, 7, 9, 10, 11 are all 0 except column 12 = -1. Row 1
and row 2 and row 8: column 4 = 1, column 12 = -1, else 0. Rows 4 and 5:
columns 1, 3 and 8 = 1, column 12 = -1, else 0. Row 12: all -1. (So `I` gets
one extra pixel on both sides; class 8 is empty in `small` but has a row.)

`large`:

```
row  0:  0  0  0  0  0  0  0  0  0  0  0  0  0
row  1:  0  0  0  1  1  0  0  0  1  0  1  0  1
row  2:  0  0  0  0  1  0  1  0  0  0  0  0  0
row  3:  0  0  0  0  0  0  0 -1  0  0  0  0  0
row  4:  0  1  0  1  0  0  0  0  1  0  0  1  1
row  5:  0  1  0  1  0  0  0  0  1  0  0  0  1
row  6:  0  0  0  0  0  0  0  0  0  0  0  0  0
row  7: -1  0 -1  0  0 -1  1 -1  0  0  0  0  0
row  8:  0  1  0  0  1  0  1  0  0  1  1  0  0
row  9:  0  1  0  1  0  0  0  0  1  1  0  0  1
row 10:  0  1  0  1  0  0  0  0  1  0  0  0  1
row 11:  0  1  0  1  0  0  0  0  1  0  0  0  0
row 12:  0  1  0  0  1  0  1  0  0  1  1  0  0
```

(These are numbers measured from the ROM, laid out as a matrix; the
exporter reads them, it does not hard-code them.)

## Laying out a string (Latin)

From the decompilation, for both Latin fonts. `pen` is the horizontal pen
position, `line_top` the vertical one; the caller gives the start.

1. **Before each glyph**: `pen += 1 - kern[prev][cur] - extra`, where `prev` is
   the kerning class of the previous glyph and `extra` the letter spacing
   (normally 0; the front end's rotated folder-tab labels use 1, which widens
   by one pixel). So with no kerning there is a **one-pixel gap** between two
   glyph boxes.
2. **Draw** the glyph's `width x height` box with its left edge at `pen` and
   its top at `line_top + top` (+ a caller-given vertical offset).
3. **After it**: `pen += width`.

So the advance of a glyph is `width + 1 - kern[prev][cur] - extra`. There are
no left-side bearings or separate advance widths: the box is the advance.

- **Start of a string, after a space and after a line break**, `prev` is the
  glyph `H` (class 0 in both fonts). Measured: class 0 against class 0 is 0 in
  both fonts, so the first glyph sits one pixel right of the start position
  unless its class kerns against class 0 (`small`: `I`, one pixel further).
- **Space** (`0x20`): `pen += 5`, no gap added, `prev` becomes `H`. The
  same in both fonts and in Japanese text. When the width of a text is
  measured, a space directly before a line break is not counted.
- **Line break** (`0x0A`): `pen` returns to the start x and `line_top +=
  line height`.
- **Line height**: the caller may give one. If it gives none (0, the usual
  case), the line height is `top + height` of the glyph stored at **record
  91**, which is the glyph for `|` (code `0x7C`). Measured: 13 (`small`: top
  -1, height 14) and 15 (`large`: top 0, height 15). (From the decompilation:
  the game looks this glyph up with the raw byte of `[` as the record number;
  the result is `|`'s record. The number is what matters.)
- **Japanese mode** (the JP build, or the `-j` boot switch): a line height
  below 14 is raised to 14. `small` therefore uses 14 in Japanese, `large`
  stays 15.
- One edge case kept for fidelity, from the decompilation: two screens set a
  "line height for an empty line": a line break on a line where the pen has
  not moved from the start x (an empty line) advances by that smaller value
  instead. The front end's briefing page (`large`) uses 8; the watch's
  briefing page uses 5 (1 in Japanese). Plain (not outlined) text only. Not
  font data; listed so the renderer has a place for it.

### Which font is used where

The font is chosen by each caller, per string: there is no "current font"
state. From the decompilation, by where the calls are:

- **`small`**: the watch menus (all pages), the multiplayer menus, the HUD
  bottom message line (pickups, objectives), the ammo and timer strings drawn
  in the HUD, the front end's vertical folder-tab labels and the multiplayer
  stage names, and the debug read-outs.
- **`large`**: most of the front end (menus, mission select, the briefing
  page), the level intro's caption lines and the end credits.

Outlined text (HUD strings over the 3D view) uses the same fonts and layout;
only the drawing differs (below).

## Japanese glyphs

From the decompilation, as text.md gives: a byte pair `b0 b1` with `b0 >= 0x80`
is a 14-bit code `((b0 & 0x7F) << 7) | (b1 & 0x7F)`. Bit `0x2000` set: a
**wide** glyph, index `code & 0x1FFF`; else a **narrow** glyph, index `code`.

### Bitmap layout

From the decompilation, and measured on three ROMs (legible sheets rendered
when text.md was written; this page only counted values): glyphs come in **pairs**. Pair `k`
(glyphs `2k` and `2k + 1`) is a block of `0x60` bytes (narrow: 12 rows) or
`0x80` bytes (wide: 16 rows) at `k * block size`. A row is 8 bytes = 16
pixels of 4 bits, left pixel in the high nibble. In each 4-bit pixel,
**bits 0-1 are the even glyph and bits 2-3 the odd glyph**, each a 2-bit
intensity. So glyph `n` is in pair `n >> 1`, half `n & 1`.

### Metrics

From the decompilation: every Japanese glyph uses fixed metrics, nothing is
stored per glyph:

| | narrow | wide |
|---|---|---|
| cell stored | 16 x 12 | 16 x 16 |
| drawn box (`width x height`) | 11 x 12 | 15 x 16 |
| top | 0 | 0 |
| kerning class | 0 (kerned against itself) | 0 |
| advance | 11 + 1 = **12** | 15 + 1 = **16** |

- The advance uses the step rule above with the Latin font's `kern[0][0]`
  (the Japanese glyph is its own `prev`), which is 0 in both fonts (measured).
  So: narrow 12, wide 16. After a Japanese glyph, the next Latin glyph is
  kerned against the last Latin `prev` of the string, not reset (from the
  decompilation: the Japanese glyph does not change `prev`).
- **Vertical**: the glyph's top is at `line_top` (top 0), and there is no
  baseline: a narrow glyph fills rows 0-11 of the line, a wide one rows
  0-15. Measured: the ink of all narrow glyphs together spans rows 0-11 and
  of all wide glyphs rows 0-15. The line height is the Latin font's, raised
  to at least 14. Japanese text mixed with `small` Latin text: Latin capitals
  occupy rows 2-9 of the line, Japanese rows 0-11 (inferred from the numbers,
  not checked on screen).
- **The undrawn columns are clipped, not unused.** The game draws only
  columns 0-10 (narrow) or 0-14 (wide) of the 16-pixel cell; the texture is
  clamped, so nothing wraps. Measured on US: columns 12-15 of every narrow
  glyph are blank, but **column 11 has ink in 340 of the 976 narrow glyphs**,
  and column 15 has ink in 3 wide glyphs (48, 52, 73). That ink is never
  shown. Export the full cell (lossless) and draw the first 11 / 15 columns.

### Glyph counts and the out-of-range limit

Measured on US: narrow slots 0 and 15 are blank, and 967-975 are blank (the
padding of the last pairs): 965 narrow glyphs have ink, in slots 0-966. The
wide font has 105 glyphs with ink, slot 15 blank (106 slots). Measured on
three ROMs: JP and EU have one more narrow glyph, 967, with 968-975 blank.

From the decompilation: before drawing, an index (`code & 0x1FFF`, so for a
**wide glyph as well as a narrow one**) at or above **`0x3C7` = 967 (US)** or
**`0x3C8` = 968 (EU, JP)** is replaced by **narrow glyph 2**. Measured: the
limit is exactly the number of slots before the blank tail in each region
(US: ink up to 966, limit 967; EU/JP: ink up to 967, limit 968). Narrow glyph
2 has ink (40 pixels, US). Measured: the highest wide index used by the US
`LTITLE` `J` bank is 105 (102 distinct wide indices used); the highest narrow
index in any bank is in text.md (966 US, 967 EU/JP). So no bank text hits the
limit.

A wide index from 106 up to the limit has no data in the wide font: from the
decompilation, the game would read past the font. That never happens in bank
text. A renderer draws such an index as narrow glyph 2 too (proposed).

The glyph order (which slot holds which character) is in text.md
§Character encoding; there is no Unicode table in the ROM.

## Colour and intensity

From the decompilation (the render state the game sets for every string),
the same for Latin and Japanese:

- The text colour is one RGBA value (`0xRRGGBBAA`) per string.
- Each pixel of the glyph is drawn with **the text colour's RGB** and
  **alpha = text alpha x (glyph intensity / 255)**, blended over what is
  already on screen (ordinary alpha blending, translucent). The intensity
  only modulates coverage; it does not darken the colour.
- Latin: the intensity is the 8-bit pixel value (0-255; measured highest 238
  / 234).
- Japanese: the 2-bit value maps through the game's 4-entry table to
  intensity **0, 85, 170, 255** (0, 1/3, 2/3, full; `0x00 0x55 0xAA 0xFF`).
  The same value is used as intensity and alpha, so the result is again
  "colour with coverage level/3".
- Filtering: bilinear by default, at 1:1 scale on whole pixels, so each
  screen pixel takes exactly one glyph pixel (inferred: the glyph rectangle
  starts at texel 0 and steps one texel per pixel). A port that scales text
  up may filter as it likes; the source data is the 1:1 bitmap.
- **Outlined text**: the glyph is drawn 8 times in the outline colour at the
  8 neighbouring one-pixel offsets (left, right, up, down and the four
  diagonals), then once in the text colour at its place. Layout is
  unchanged (the outline does not widen the advance).
- **Rotated text** (the front end's folder-tab labels, `small`): the same
  layout, drawn rotated 90 degrees clockwise. A renderer feature, not font
  data.
- Clipping (from the decompilation): a glyph is drawn only if its whole box
  is inside the caller's clip rectangle horizontally; vertically it is cut
  to the rectangle. A glyph whose left edge is at or left of screen x 0 is
  skipped.

## Proposed export: `fonts/` in the base pack (PROPOSAL, the lead decides)

Keeps the ROM's information that affects drawing, losslessly; drops only
the padding columns (a copy of the edge), the 8-byte gaps, the filler and
the duplicated glyph id.

```
fonts.json
fonts/small.png
fonts/large.png
fonts/jp_narrow.png
fonts/jp_wide.png
```

**Pictures**: 8-bit greyscale PNG, no alpha channel. The grey value is the
glyph intensity, written unchanged for the Latin fonts (0-255 as stored; do
not normalise 238 to 255), and `level * 85` (0, 85, 170, 255) for the
Japanese fonts, so a reader gets the 2-bit level back as `grey / 85` and both
kinds are read the same way (grey = coverage). Pixels outside every glyph
box are 0.

- Latin atlas: a grid of 16 columns x 6 rows of cells, record `i` in cell
  `(i % 16, i / 16)`; cell size = (largest width + 1) x (largest height + 1)
  of that font (`small`: 10 x 15, `large`: 15 x 16 with the US values), the
  bitmap at the cell's top-left. The exact rectangle of each glyph is in
  `fonts.json`, so a reader never derives it from the grid.
- Japanese atlas: 32 cells per row, cell = the stored 16 x 12 or 16 x 16 cell
  (all 16 columns, lossless), slot `n` in cell `(n % 32, n / 32)`: 31 rows
  for `jp_narrow` (976 slots, the last row half used), 4 for `jp_wide`.

**`fonts.json`**:

```json
{
  "format": "ge007-fonts/1",
  "region": "ntsc-final",
  "space_advance": 5,
  "jp_min_line_height": 14,
  "fonts": {
    "small": {
      "kind": "latin",
      "image": "fonts/small.png",
      "line_height": 13,
      "first_code": 33,
      "kerning": [[0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1], "... 13 rows of 13"],
      "glyphs": [
        { "code": 33, "x": 0, "y": 0, "w": 2, "h": 8, "top": 2, "class": 0 }
      ]
    },
    "jp_narrow": {
      "kind": "cells",
      "image": "fonts/jp_narrow.png",
      "cell": [16, 12],
      "columns": 32,
      "slots": 976,
      "draw": [11, 12],
      "advance": 12,
      "top": 0,
      "limit": 967,
      "fallback": 2
    },
    "jp_wide": {
      "kind": "cells",
      "image": "fonts/jp_wide.png",
      "cell": [16, 16],
      "columns": 32,
      "slots": 106,
      "draw": [15, 16],
      "advance": 16,
      "top": 0,
      "limit": 967,
      "fallback": 2,
      "fallback_font": "jp_narrow"
    }
  }
}
```

- `glyphs`: 94 entries in code order, `code` 33-126; `x, y, w, h` the
  rectangle in the image (`w` = width, `h` = height); `top` and `class` as in
  the glyph record. The advance is not stored: it is the rule `w + 1 -
  kerning[prev][cur]` with `prev` starting as the class of `H`; a reader
  writes it once. (Alternative: store `"start_prev_class": 0` so the reader
  does not look up `H`.)
- `line_height`: the measured `top + height` of `|`, written by the exporter
  from the ROM (not a constant), so a modded font can set its own.
- `limit`: 967 for US, 968 for EU and JP (per region, written by the
  exporter from the region). `fallback`: the narrow slot drawn instead.
- Keys: `small`, `large`, `jp_narrow`, `jp_wide` are the game's reserved font
  keys (asset-packs.md §Keys). A mod's own font takes any other key, or
  replaces one of these with the same fields.
- A font in a mod or pack that lacks `kerning` means all zeros; that lacks
  `line_height` means `top + height` of its `|` glyph.

Verification for the exporter (US): `small` 94 glyphs, `large` 94 glyphs,
`jp_narrow` 976 slots of which 965 have ink, `jp_wide` 106 slots of which 105
have ink; the `small` record for `A` is top 2, height 8, width 8, class 1;
`large` `W` is top 0, height 12, width 14, class 4; kerning `small`
`[1][4]` (`A` then `V`) = 1, `large` `[7][0]` (`l` then `H`) = -1; line
heights 13 and 15.

## Confidence and what is still unknown

| Section | Confidence |
|---|---|
| ROM locations, sizes, block layouts | high: US measured, EU/JP from the file table |
| Latin metrics, kerning, pixel format, padding | high: measured on US, every glyph |
| Latin layout rules (gap, space, line height, `H` start) | high: from the decompilation, consistent with the measured values |
| Japanese layout, cell, clipping, the limit | high: from the decompilation; ink columns, counts and the limit/count match measured |
| Colour and intensity | high for the rule (from the decompilation); the exact on-screen look of bilinear at 1:1 is inferred |
| Which font is used where | medium: from the decompilation's call sites, not checked on screen per screen |

The EU and JP Latin font blocks were checked against the US ones: byte-identical
(same SHA-1 at each region's offset), and the exporter writes identical
`small` / `large` pictures from all three ROMs.

Unknown or not measured:

1. The EU and JP Japanese wide-index maximum was not measured here (US: 105).
2. The Japanese glyph to Unicode table: not in the ROM (text.md, open
   question 1).
