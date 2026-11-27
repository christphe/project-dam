# Texture bank

Every texture the game draws is identified by a **texture number** (0 ..
about 2700). Its compressed data sits in one ROM area, the *images
segment*; its compressed size sits in a table inside the game's compressed
data segment. This page says where both are and how to turn one texture
into pixels. Only the first image (the full-size level of detail) is needed
for export.

## Where the data is

### The size table

The game's initialised data segment ("cdata") is stored compressed in the
ROM, in the rarezip format of level-decor.md (two header bytes, then raw
DEFLATE). Inside the decompressed bytes there is a table of 8-byte entries,
one per texture number, in order:

| Bits of word 0 (big-endian) | Meaning |
|---|---|
| 31-28 | hit sound class (gameplay, not needed) |
| 27-24 | hit effect class (gameplay, not needed) |
| 23-0 | **compressed size** of this texture in bytes |

Word 1 holds four 4-bit fields: a detail-texture offset pair and a detail
scale pair (see display-lists.md, detail textures). Keep them; they are
raw data for the glTF extras.

The table ends at the first entry whose low 24 bits are `0xFFFF`.

The data of texture `n` starts at `images_start + sum(size[0 .. n-1])` and
is `size[n]` bytes long. The data is not aligned: a texture can start on any
byte.

Measured on the **US ROM** (`ntsc-final`, SHA-1 checked by
`port/convert/conv_romcheck.c`):

| Value | US |
|---|---|
| cdata segment, ROM offset | `0x21990` |
| size table, offset inside decompressed cdata | `0x28570` |
| entries before the terminator | 2698 |
| sum of all sizes | `0x2EEF18` |
| images segment, ROM offset (`images_start`) | `0x8F7DF0` |

Measured on the EU (`pal-final`) and JP (`jpn-final`) ROMs as well:

| Value | US | EU | JP |
|---|---|---|---|
| cdata segment, ROM offset | `0x21990` | `0x1F850` | `0x219D0` |
| size table, offset inside decompressed cdata | `0x28570` | `0x22C30` | `0x28560` |
| entries before the terminator | 2698 | 2698 | 2698 |
| sum of all sizes | `0x2EEF18` | `0x2EEF18` | `0x2EEF18` |
| images segment, ROM offset | `0x8F7DF0` | `0x8F7840` | `0x8F9050` |

The `0x2EEF18` bytes of texture data are byte-identical in the three regions
(same SHA-1); only their position in the ROM moves. How the values were
found (for a future region or a check): decompress each rarezip block in the
first 1 MiB, find an 8-byte-stride run of at least 2000 entries with sizes
in 1..`0x8000` ending in the `0xFFFF` terminator; then the images start is
the one ROM offset at which every 7th texture's first bytes parse as a valid
header (zlib family: format 9-12; bit-packed: format <= 8, method <= 9,
non-zero size of at most 8192 texels). Each search gave exactly one match.
An exporter uses the table above and checks the invariants (terminator,
count 2698, sum `0x2EEF18`); on a mismatch it refuses the textures, reports
why, and still exports the geometry, untextured.

## One texture

The first byte of a texture's data:

| Bit | Meaning |
|---|---|
| 7 | the texture stores its smaller levels of detail explicitly (not needed) |
| 6 | **zlib family** (1) or **bit-packed family** (0) |
| 5-0 | number of levels of detail it was authored with (not needed for level 0) |

The rest is read as a **bit stream**, most significant bit first: bytes are
consumed one at a time and appended below the bits already buffered; a read
of `k` bits returns the top `k` buffered bits. The stream starts at the
second byte.

Formats (the format number appears in both families):

| # | Name | Texel |
|---|---|---|
| 0 | RGBA32 | 8 bits each R, G, B, A |
| 1 | RGBA16 | 5 bits R, G, B, 1 bit A |
| 2 | RGB24 | 8 bits R, G, B; A = 255 |
| 3 | RGB15 | 5 bits R, G, B; stored as RGBA16 with A = 1 |
| 4 | IA16 | 8 bits intensity, 8 bits alpha |
| 5 | IA8 | 4 bits intensity, 4 bits alpha |
| 6 | IA4 | 3 bits intensity, 1 bit alpha |
| 7 | I8 | 8 bits intensity |
| 8 | I4 | 4 bits intensity |
| 9 | CI8, RGBA16 palette | 8-bit index into a palette of RGBA16 colours |
| 10 | CI4, RGBA16 palette | 4-bit index into RGBA16 colours |
| 11 | CI8, IA16 palette | 8-bit index into IA16 colours |
| 12 | CI4, IA16 palette | 4-bit index into IA16 colours |

For RGBA output, expand channels exactly as the port's renderer does (this is
what the check against the game's own capture, `level.rec`, compares to):

- 5-bit: `v * 255 / 31` with integer division (rounds down; 4 -> 32, 31 -> 255);
- 4-bit: `v * 17`;
- 3-bit (only IA4 intensity uses it): `v * 255 / 7` with integer division
  (7 -> 255, 6 -> 218);
- 1-bit alpha: 0 or 255.

Per format:

- RGBA16 / RGB15 and the RGBA16 palette colours: R, G, B 5-bit each, A from
  the 1-bit alpha.
- IA16 and the IA16 palette colours: R = G = B = the high byte, A = the low
  byte.
- IA8: R = G = B = high nibble (4-bit), A = low nibble (4-bit).
- IA4: R = G = B = the top 3 bits (3-bit), A = the low bit (1-bit).
- **I8 and I4: R = G = B = I and also A = I** (an intensity texture's alpha
  is its intensity on this hardware; 4-bit I expands with `v * 17`).
- RGBA32 and RGB24: as stored (RGB24 alpha 255).

These rules are checked against the game's `level.rec` capture by
`tools_pc/export_vs_levelrec.py`. Two kinds of difference are expected and
are not decoding errors: the renderer stores some textures padded (a 54x54
texture as 56x54, a 64x1 one as 64x2; the decoded pixels match the top-left
corner exactly), and texture 1511 (water) is altered by the game at run
time. Any other difference is reported per texture, never tuned blindly.

Per-format tables used below (index = format number 0..12):

| # | channels | 1-bit alpha plane | channel range | lookup entry bits |
|---|---|---|---|---|
| 0 | 4 | no | 256 | 32 |
| 1 | 3 | yes | 32 | 16 |
| 2 | 3 | no | 256 | 24 |
| 3 | 3 | no | 32 | 15 |
| 4 | 2 | no | 256 | 16 |
| 5 | 2 | no | 16 | 8 |
| 6 | 1 | yes | 8 | 4 |
| 7 | 1 | no | 256 | 8 |
| 8 | 1 | no | 16 | 4 |
| 9-12 | 1 | no | 256 or 16 | 16 |

### Zlib family (bit 6 set)

Used only for the palette formats 9-12.

1. 8 bits: format number.
2. 8 bits: palette size minus one (so 1..256 colours).
3. That many 16-bit palette colours (RGBA16 for formats 9 and 10, IA16 for
   11 and 12).
4. For the first image: 8 bits width, 8 bits height (texels). The stream is
   byte-aligned at this point.
5. At the current byte: a rarezip block (two header bytes, then raw
   DEFLATE). Its output is the image's indices, row after row, with no
   padding: one byte per texel for CI8, two texels per byte for CI4 (high
   nibble first).

Each index selects a palette colour.

### Bit-packed family (bit 6 clear)

1. 4 bits: format number.
2. 8 bits: width. 8 bits: height.
3. 4 bits: **method**, one of:

| Method | Payload |
|---|---|
| 0, 1 | raw texels |
| 2 | one Huffman code over all channel planes |
| 3 | one Huffman code per channel plane |
| 4 | LZ-style run coding over all channel planes |
| 5 | a colour lookup table, then one table index per texel |
| 6 | a lookup table, then the indices Huffman-coded |
| 7 | a lookup table, then the indices run-coded |
| 8 | a 3-bit predictor id, then as 2, then the predictor is undone |
| 9 | a 3-bit predictor id, then as 4, then the predictor is undone |

Texels are always in row order, top row first, left to right. Image sizes
above 8192 texels do not occur (the game rejects them).

**Raw texels (0, 1)**, read in row order, per format: RGBA32 as two 16-bit
reads (high half first); RGB24 as 24 bits (alpha 255); RGBA16 and IA16 as 16
bits; RGB15 as 15 bits (then alpha = 1); IA8 and I8 as 8 bits; IA4 and I4 as
8 bits per **pair** of texels (high nibble = left texel). For a 4-bit format
with an odd width, the last byte of a row carries one texel and a padding
nibble.

**Channel planes (2, 3, 4, 8, 9).** The texture is coded as `channels`
separate planes of `width * height` values each, plane after plane
(channel order: R, G, B, A for colour formats; I, A for IA formats; I for I
formats). Then, if the format has a 1-bit alpha plane (RGBA16, IA4), a
separate plane of `width * height` single bits follows **in the stream**,
read one bit per texel; it is the alpha. Values are in `0 .. range-1` of the
format (5-bit channels for RGBA16/RGB15, 4-bit for IA8, 3-bit intensity for
IA4, ...). A texel is assembled from the planes at its position.

**Huffman (2, 3, 6, 8).** A code over an alphabet of `N` symbols (`N` = the
channel range, or the lookup size for method 6) is defined by `N` 8-bit
weights read first. The tree is built by repeatedly merging the two
lowest-weight live entries; ties and the order of merging follow exact rules,
because the bit patterns depend on them:

- Work on an array of `N` weights and a node table of 2048 entries of two
  children, all initially "none". A *leaf mark* is the symbol + 10000.
- Selecting the two minima: scan the weights in index order keeping two
  candidates `a` and `b`, both starting at "weight 9999" (meaning empty). For
  each weight `w`: if `w` is below `a`'s weight, it replaces whichever of `a`
  and `b` currently holds the **larger** weight: `a` if `a`'s weight is
  strictly greater than `b`'s, else `b` (so with both empty, `b` fills
  first). Otherwise, if `w` is below `b`'s weight, it replaces `b`. "Below"
  is strict everywhere.
- Merge `a` and `b`: their sum (taken as 1 if it is 0) is the new weight;
  both slots are set to 9999. If slot `a` is still a bare leaf (both
  children none), slot `a` becomes the parent: child 0 = leaf mark of `a`,
  child 1 = leaf mark of `b` if `b` is a bare leaf, else `b` itself; its
  weight is the sum. Else if `b` is a bare leaf, the same with the roles
  swapped (`b` becomes the parent, child 0 = leaf mark of `b`, child 1 = leaf
  mark of `a` or `a`). Else take the first slot index whose children are both
  none **and** whose weight is 9999; it becomes the parent with children `a`
  and `b` and the sum as weight. The parent is the current root.
- Repeat selection and merging until a selection finds fewer than two
  entries below 9999.
- Decode a symbol: start at the root, read one bit, follow child 0 or 1,
  until the value is a leaf mark (>= 10000); the symbol is the mark - 10000.

Method 2 decodes `channels * width * height` symbols with one tree; method 3
reads a new tree (new weights) before each plane and decodes `width *
height` symbols with it.

**Run coding (4, 7, 9).** Header: 3 bits `back_bits`, 3 bits `len_bits`, 4
bits `value_bits`. Let `extra` = the number of times `value_bits + 1` must
be subtracted from `back_bits + len_bits + value_bits + 1` before the result
is no longer positive (i.e. the ceiling of that sum divided by
`value_bits + 1`). Then, until the target count of values is produced: read
1 bit; if 0, read one literal of `value_bits`; if 1, read `back_bits` →
distance `d`, `len_bits` → `L`; copy `L + extra` values starting `d + 1`
values back from the current end (the copy may overlap what it is writing),
then read one literal of `value_bits`. Values wider than 8 bits are stored
as 16-bit.

**Lookup (5, 6, 7).** 11 bits: number of colours `C`. Then `C` entries, each
of "lookup entry bits" (table above); a 32-bit entry is read as 24 bits then
8 bits. Each entry is a texel in the format's own layout (RGBA16 entries are
16-bit 5551 words, IA8 entries 8-bit, I4 entries 4-bit, ...; an RGB24 entry
is 24 bits, alpha 255; an RGB15 entry is 15 bits, alpha 1).

- Method 5: for each texel, read an index of `bits(C)` bits, where
  `bits(C)` is the number of bits needed for `C - 1` (0 for `C = 1`).
- Method 6: Huffman over an alphabet of `C` symbols, `width * height`
  symbols (`C` may exceed 256).
- Method 7: run coding over `width * height` values.

For the 4-bit formats each texel takes its own index; for an odd width the
right half of the last pair is absent.

**Predictor (8, 9).** After the planes are decoded (before the alpha bit
plane is read), all planes are treated as one image `width` wide and
`channels * height` tall (so the first row of a plane sees the last row of
the previous plane as "above"). In row order, each value `v` becomes
`(v + 2 * range + p) mod range`, using the values already rebuilt, with
`left`, `above`, `aboveleft` = 0 outside the image, and `p` by predictor id:

| id | p |
|---|---|
| 0 | left |
| 1 | above |
| 2 | above-left |
| 3 | left + above - above-left |
| 4 | (above - above-left) / 2 + left |
| 5 | (left - above-left) / 2 + above |
| 6 | (left + above) / 2 |
| 7 | unchanged |

Divisions truncate toward zero.

## What the exporter outputs

Level 0 only, as 8-bit RGBA, row 0 at the top. Keep for the glTF extras:
the texture number, the format number, width and height, the family (zlib /
bit-packed) and method, and the word-1 detail fields of the size table.
