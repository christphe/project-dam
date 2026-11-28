# Silo coverage (`UsetupsiloZ`)

| AI list | Covered by | Notes |
|---|---|---|
| 0x0401 | data | chr 0x40: patrol path 0, then simple generic guard |
| 0x0402 | data | chr 0x41: path 1 |
| 0x0403 | data | chr 0x42: path 2 |
| 0x0404 | data | chr 0x43: path 3 |
| 0x0405 | data | chr 0x44: path 4 |
| 0x0406 | data | chr 0x45: path 5 |
| 0x0407 | data | chr 0x46: path 6 |
| 0x0408 | data | chr 0x47: path 7 |
| 0x0409 | data | chr 0x48: path 8 |
| 0x040A | data | chr 0x49: path 9 |
| 0x040B | data | chr 0x4A: path 10 |
| 0x040C | data | chr 0x4B: path 11 |
| 0x040D | data | chr 0x4C: path 12 |
| 0x040E | data | chr 0x4D: path 13 |
| 0x040F | data | chr 0x4E: path 14 |
| 0x0410 | rules 7, 8 | scientist main reactions; a "bit 0x80" flag set by 0x1000 selects the keyboard workers |
| 0x0411 | rule 9 | scientist dodge |
| 0x0412 | rule 10 | scientist cower / hands up, draws a TT33 after 2+ hits |
| 0x0413 | rule 11 | scientist flight to pad 0xD1, fade out when unseen; debug prints dropped |
| 0x0414 | rules 13, 14, 15; Characters | Ourumov stats and stand-off |
| 0x0415 | rule 16 | Ourumov flight; two objective-flag bits used only as "door already closed" latches (mechanism, dropped) |
| 0x0416 | Cinematics (exit steps 3-7) | Bond's cinema body in the exit scene |
| 0x0417 | rule 20 | Bond's intro animation |
| 0x0418 | rule 17; Characters | stationary gunners |
| 0x0419 | rule 18 | chr 0x07 dormant until below -900 |
| 0x041A | rule 18 | dormant until below 0 |
| 0x1000 | rules 5, 7 | circuit board destroyed -> fail flags for objective d; keyboard worker marks |
| 0x1001 | rules 3, 4 | bomb countdown per difficulty, music, warning, explosions |
| 0x1002 | rule 19 | door 0x0A open on unarmed / close on PP7 (developer test, but reachable) |
| 0x1003 | rule 21; Cinematics (exit 1-3) | exit trigger, objective e completion flag, failure exit, weapon choice for the scene |
| 0x1004 | rule 20 | starts Bond's intro animation when the swirl begins |
| 0x1005 | rules 6, 12 | civilians flag on scientists, failure of e after 3 deaths |
