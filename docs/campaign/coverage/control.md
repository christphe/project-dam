# Control coverage

Setup `UsetupcontrolZ.c`, 28 AI lists. Spec: `docs/campaign/specs/control.md`.

| AI list | Rules | Notes |
|---|---|---|
| 0x0401 | 1, 2 | Natalya, lift; stats in Characters |
| 0x0402 | 3, 4, 5, 6, 7 | Natalya, security console |
| 0x0403 | 8, 9, 10, 11, 13 | Natalya, guidance console; the "time" debug print dropped |
| 0x0404 | 12 | Natalya leaves on success |
| 0x0405 | 20, 21 | shooter chr 0x1C setup |
| 0x0406 | 20, 21 | shooter chr 0x1D setup |
| 0x0407 | 21 | shooter chr 0x1C window post (glass 0x22) |
| 0x0408 | 21 | shooter chr 0x1D window post (glass 0x21) |
| 0x0409 | dead | shooter chr 0x1E (posts 103/110/121/114): no setup guard, spawn or list hands a character to it |
| 0x040A | 22 | shooters fire at Natalya, then persistent chase (global) |
| 0x040B | Cinematics (outro steps 5-7) | Bond's outro animation, run on the Bond cinema chr |
| 0x040C | 25 | ambushers chrs 0x17, 0x18, 0x1A, 0x1B |
| 0x040D | 24 | chaser attack loop |
| 0x040E | 23 | chaser takes chr 0x26 (debug print dropped) |
| 0x040F | 23 | chaser takes chr 0x27 (debug print dropped) |
| 0x0410 | data | chaser stats, weapon, hat (Characters), then rule 24 |
| 0x0411 | 15 | Boris meets Bond, gun trick, surrender |
| 0x0412 | 17 | Boris runs off |
| 0x0413 | 18 | Trevelyan |
| 0x0414 | 11, 16 | Natalya gives up, lift, level ends when Bond joins |
| 0x0415 | 16 | Natalya reacts to Boris's death |
| 0x1000 | 20 | shooter respawn every 1 s after the override; third check for 0x1D is a debug print only (dead code) |
| 0x1001 | 27 | doors |
| 0x1002 | 14 | Natalya killed |
| 0x1003 | 19, Cinematics (outro 1-4) | exit; a dead player branch hands the list to the do-nothing global list |
| 0x1004 | 23, 24 | chaser respawn and pacing |
| 0x1005 | 28 | lift music |
| 0x1006 | 26 | typing sound |
