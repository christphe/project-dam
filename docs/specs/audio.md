# Audio: instrument banks, compressed sequences, music table

GoldenEye's music is a set of **compressed MIDI-like sequences** played by a
sample-based synthesizer with an **instrument bank**; its sound effects are
the sounds of a second bank. This page gives the ROM locations and the byte
layouts needed to rebuild them as standard files: each sequence as a Standard
MIDI File, each bank as a SoundFont 2 file. All values big-endian. Written
from the decompilation in the lead's own words; locations measured on the
three retail ROMs.

## Where things are

| Item | US (`ntsc-final`) | EU (`pal-final`) | JP (`jpn-final`) | Size |
|---|---|---|---|---|
| SFX bank header file ("ctl") | `0x2EBDE0` | `0x2E2220` | `0x2ECB00` | `0x5BC0` |
| SFX sample data ("tbl") | `0x2F19A0` | `0x2E7DE0` | `0x2F26C0` | `0xC2AB0` |
| Instrument bank ctl | `0x3B4450` | `0x3AA890` | `0x3B5170` | `0x43A0` |
| Instrument sample data tbl | `0x3B87F0` | `0x3AEC30` | `0x3B9510` | `0x60FA0` |
| Music table | `0x419790` | `0x40FBD0` | `0x41A4B0` | `4 + 8 * 63` |

Each tbl directly follows its ctl. Both ctl files start with the bytes
`42 31 00 01` (format revision "B1", one bank). How the EU/JP values were
found: the two occurrences of `42 31 00 01` in the ROM are the two ctl files,
the music table is the one place where a 16-bit count `n` (40..80), a zero
16-bit word and `n` entries whose first offset equals `4 + 8n` all
decompress to their declared sizes. Check these invariants before use.

## Music table

| Bytes | Field |
|---|---|
| 0-1 | number of tracks `n` (63 in all three ROMs) |
| 2-3 | 0 |
| then `n` entries of 8 bytes | offset of the track's compressed data, **relative to the start of the table** (u32); uncompressed size (u16); compressed size (u16) |

Track 0 is a "no music" placeholder; a track whose compressed size is 0 has
no data. Each track's data is a rarezip block (level-decor.md §Block
compression: 2 header bytes, then raw DEFLATE) that decompresses to exactly
the declared uncompressed size: a **compressed sequence**.

The music uses the **instrument bank**: a program change to program `p`
selects instrument `p` of that bank. Every MIDI channel works the same way;
channel 10 has **no** drum meaning (when playing the exported MIDI with a
General MIDI-minded synth, force channel 10 to a melodic bank).

## Compressed sequence

### Header

| Bytes | Field |
|---|---|
| 0-63 | 16 track offsets (u32), from the start of the sequence; 0 = track absent |
| 64-67 | division: ticks per quarter note (u32) |

A track is a stream of events whose status bytes name their MIDI channel;
do not assume track `i` = channel `i`. The tracks play in parallel: at any
time the next event is the one with the smallest absolute tick over all
tracks.

### Reading a track's bytes (back-references)

The track data is compressed with back-references. A reader keeps, per track,
the current position and an optional "replay" window. To get the next byte:

1. If a replay window is active, return its next byte and shorten it by one.
   Bytes taken from a replay window are returned as they are (they are never
   interpreted as markers themselves).
2. Otherwise read the byte at the current position and advance. If it is not
   `0xFE`, return it.
3. If it is `0xFE`, read the next byte. If that byte is also `0xFE`, the pair
   stands for one literal `0xFE`: return `0xFE`.
4. Otherwise that byte and the one after it form a 16-bit distance `D`
   (high byte first), and the byte after those is a length `L`. Let `P` be
   the position of the first `0xFE` of this 4-byte marker. Start a replay
   window of `L` bytes at position `P - D`, and return its first byte (the
   window then has `L - 1` bytes left).

### Events

Delta times and note durations are MIDI variable-length numbers (7 bits per
byte, high bit = more bytes follow), read through the byte reader above.

A track is: a delta time, then an event, then a delta time, then an event,
and so on. Events:

- **Meta** (status `0xFF`, never running status), then a type byte:
  - `0x51` tempo: 3 bytes, microseconds per quarter note (as in MIDI).
  - `0x2F` end of track: no length byte follows; the track ends.
  - `0x2E` loop start: 2 bytes follow (a loop number and a value that is
    ignored).
  - `0x2D` loop end: 6 bytes follow: a loop count, a current-count byte, and
    a 32-bit distance `B`. When the loop is taken, playback continues at the
    position **just after these 6 bytes minus `B`** (one of the loop start
    markers before it). For export, do **not** unroll loops: write a marker
    (`loopStart` / `loopEnd` text in a MIDI marker meta event, type `0x06`)
    and continue reading past the loop end, so every track is written once.
  - any other type: unknown, report it.
  - After any meta event, running status is cleared.
- **Channel message**: if the byte has its high bit set it is a new status
  (and becomes the running status); otherwise it is the first data byte and
  the running status is used. Program change (`0xC_`) and channel pressure
  (`0xD_`) have one data byte; all others two. **Note-on (`0x9_`) is
  followed by a variable-length duration in ticks**: there are no note-off
  events in the stream. A note-on with velocity 0 does not occur.

### Which note a note-off ends (important)

Each note-on schedules a note-off for its channel and key at `tick +
duration`. When that note-off fires, the game's player does **not** end the
note that scheduled it: it ends the **oldest note still held on the same
channel with the same key** (notes are kept in start order; the first match
that is not already released wins). A note whose sample has simply run out
still counts as held until a note-off ends it.

This matters whenever notes of the same key overlap on a channel. Example
measured in track 2, channel 15: two notes start at tick 4 with a duration of
3692, and the same keys are struck again at ticks 242, 1141, 1395, ... with
durations of 59 to 114 ticks. In the game, the note-off scheduled by the
note at 242 (tick 356) ends the **tick-4** note; the 242 note then rings until
the next note-off of that key (tick 1200), i.e. its unlooped 1.06 s sample
plays out. Ending each note at its own `tick + duration` instead cuts these
notes after ~70 ms, which is audibly wrong.

So to compute when each note really ends, replay the note-offs in tick order
per (channel, key) against a first-in-first-out list of held notes: each
note-off ends the oldest held note of that key (ignore a note-off that finds
none). Every note's real end is the tick of the note-off that ended it (or
the end of the sequence if none did).

### Writing a Standard MIDI File

Format 1, same division: one MIDI track per present sequence track, the same
events at the same absolute ticks, each note-on followed by a note-off
(`0x8_`, same key, velocity 0x40) at the note's **real end** from the rule
above, tempo events kept, loop markers as above, end-of-track appended.

A MIDI channel cannot hold two notes of the same key: if a note's real end
falls after the next note-on of the same channel and key, write its note-off
just before that next note-on (the older sound is cut there; report the
count of such overlaps per track). Sort events by absolute tick; at equal
ticks put note-offs before note-ons.

## Bank file ("ctl") and samples ("tbl")

Every pointer inside a ctl file is a 32-bit offset **from the start of the
ctl file**; the sample `base` of a wave table is an offset **from the start
of the tbl file**. A 0 offset means "absent".

| Structure | Layout |
|---|---|
| Bank file | revision (s16, `0x4231`), bank count (s16, 1), then that many bank offsets (u32) |
| Bank | instrument count (s16), flags (u8), pad (u8), sample rate (s32, 22050 in both banks), percussion instrument offset (u32, unused here), then `count` instrument offsets (u32) |
| Instrument | volume (u8, 0-127), pan (u8, 0 = left, 64 = centre, 127 = right), priority (u8), flags (u8), tremolo type, rate, depth, delay (4 x u8), vibrato type, rate, depth, delay (4 x u8), bend range in cents (s16), sound count (s16), then that many sound offsets (u32) |
| Sound | envelope offset (u32), key map offset (u32), wave table offset (u32), sample pan (u8), sample volume (u8), flags (u8), pad |
| Envelope | attack time, decay time, release time (3 x s32, microseconds), attack volume (u8), decay volume (u8) |
| Key map | velocity min, velocity max, key min, key max, key base (5 x u8), detune (s8, cents) |
| Wave table | sample data offset in tbl (u32), length in bytes (s32), type (u8: 0 = ADPCM, 1 = raw 16-bit), flags (u8), pad (2), then for ADPCM: loop offset (u32), codebook offset (u32); for raw: loop offset (u32) |
| ADPCM codebook | order (s32), predictor count (s32), then `order * count * 8` coefficients (s16) |
| ADPCM loop | start, end, count (3 x u32, in samples; count `0xFFFFFFFF` = loop forever), then 16 s16 of decoder state at the loop start |
| Raw loop | start, end, count (3 x u32) |

The SFX bank has **one** instrument whose sounds are the individual sound
effects (sound `k` = effect `k`). The instrument bank has one instrument per
program (75 in the US ROM).

Pitch: a note with key `k` on a sound plays its sample at the ratio
`2 ^ (((k - key_base) * 100 + detune) / 1200)` relative to the bank sample
rate (22050 Hz). So in SoundFont terms: sample rate 22050, root key =
key base, fine tune = detune cents, key range and velocity range from the key
map.

Envelope (per voice): the gain goes from 0 to `attack volume` over the
attack time, then to `decay volume` over the decay time, holds there while
the note is held, and goes to 0 over the release time after the note ends.
Volumes are 0-127. The final level of a voice also scales with the note
velocity, the instrument volume and the sound volume (all 0-127), and its
pan combines the instrument pan and the sound pan. Mapping these to SoundFont
generators (attack, decay, sustain level as attenuation, release, initial
attenuation, pan) is an approximation: the player exists to tune it by ear.

### The SFX bank: the key map is not a key map

The game does not play sound effects through MIDI notes, and it reuses the
six key-map bytes of each SFX sound as effect parameters. Velocity and key
ranges are meaningless there (hence inverted or > 127 "ranges").

- **Effect numbers** are 1-based: effect `N` is sound `N-1` of the bank's
  single instrument (effect 0 means "no sound").
- **Pitch**: the effect plays at `2 ^ (cents / 1200)` times the bank rate
  (22050 Hz), with `cents = key_base * 100 + detune - 6000` (so key base 60
  and detune 0 play at the recorded rate). When flag `0x20` (below) is set,
  the detune is **not** part of the starting pitch: it is a pitch-slide step
  (cents) applied repeatedly while the effect plays.
- **Key min byte**: low 6 bits = volume group (0..63; the game has one volume
  per group, e.g. music vs effects); top 2 bits = high bits of the next
  effect in a chain (below).
- **Key max byte**: low 4 bits = reverb send (0..15, the game multiplies it
  by 8); high 4 bits = flags: `0x10` retrigger (the chain is re-posted),
  `0x20` pitch slide (see Pitch). `0x40` occurs on effects 169-182 (all but
  175 and 176) and is never read by the game: carried in the effect's state,
  tested nowhere. `0x80` does not occur. Record the nibble raw.
- **Chains**: an effect can start another one: next effect =
  `velocity_min + (key_min & 0xC0) * 4` (0 = end of chain), started
  `velocity_max * 33.333 ms` after this one.
- **Envelope**: as for instruments; a decay time of `-1` (`0xFFFFFFFF`) means
  "held until stopped" (looping effects such as alarms and engines), and
  those get a higher priority.
- **Volume / pan**: final volume scales with attack/decay volume x the
  sound's sample volume / 127 x the group volume; pan = sound pan (64 =
  centre).

For export, an SFX is a WAV plus metadata, not a SoundFont preset.

The data is a series of 9-byte frames, each giving 16 samples (16-bit
signed). With the codebook of order 2 (the case in these banks):

1. The frame's first byte: high nibble = `scale`, low nibble = predictor
   index `p`. Predictor `p` has two rows of 8 coefficients: `c0[0..7]` then
   `c1[0..7]` (the 16 coefficients of predictor `p` start at index `16p` of
   the codebook).
2. The 8 following bytes are 16 signed 4-bit values (high nibble first),
   each multiplied by `2^scale` (i.e. shifted left by `scale`).
3. The 16 samples are produced as two halves of 8. For a half, let `s1` and
   `s2` be the last and second-to-last output samples so far (0 at the very
   start, or the loop state when restarting a loop), and `x[0..7]` that
   half's 8 scaled values. Output sample `j` (0..7) is

   ```
   ( c0[j]*s2 + c1[j]*s1 + x[j]*2048 + sum over k < j of c1[j-1-k]*x[k] ) >> 11
   ```

   (arithmetic shift), clamped to -32768..32767.

The number of samples of a wave table is `length / 9 * 16`. A raw 16-bit wave
table is big-endian 16-bit samples.

SoundFont loops: if the wave table has a loop with count other than 0, set
the sample's loop points to `start` / `end` and the loop mode to "loop
continuously" (the bank's finite counts are rare; treat them as continuous
and report them); otherwise no loop.
