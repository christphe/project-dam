# Statue Park coverage

Setup `assets/obseg/setup/u/UsetupstatueZ.c`, spec `docs/campaign/specs/statue.md`.

| AI list | Covered by |
|---|---|
| 0x0401 | dead: debug loop, no guard, object or list uses it |
| 0x0402 | rules 6, 21 (escort chr 0x02, guard post pad 2) |
| 0x0403 | rules 6, 21 (chr 0x03, post pad 3) |
| 0x0404 | rules 6, 21 (chr 0x04, post pad 4) |
| 0x0405 | rules 6, 21 (chr 0x05, post pad 5) |
| 0x0406 | rule 9 |
| 0x0407 | rule 21 (park guard loadout) |
| 0x0408 | rule 10 |
| 0x0409 | rule 22 (escorts; leave when e is complete) |
| 0x040A | rule 22 (park guards; leave on the meeting / Janus) |
| 0x040B | rule 21 (park guard chr 0x06) |
| 0x040C | rule 21 (chr 0x07) |
| 0x040D | rule 21 (chr 0x08) |
| 0x040E | rule 21 (chr 0x09) |
| 0x040F | rule 22 (park guards leave via pad 205) |
| 0x0410 | rule 22 (escorts leave via pad 35) |
| 0x0411 | Objects and world (helicopter start state) |
| 0x0412 | rules 1-5 |
| 0x0413 | rules 11, 12 |
| 0x0414 | rules 13, 14 |
| 0x0415 | rule 15 |
| 0x0416 | rule 17 |
| 0x0417 | rule 18 |
| 0x0418 | rule 19 |
| 0x0419 | rules 25, 26 (chr 0x0B, 0x0C) |
| 0x041A | rule 24 (gives chr number 0x0B) |
| 0x041B | rule 24 (gives chr number 0x0C) |
| 0x041C | rules 25, 26 (the three kneeling soldiers) |
| 0x041D | rule 25 |
| 0x041E | rule 25 (hostile: kneel, aim, fire) |
| 0x041F | Cinematics: intro (Bond's swirl animation) |
| 0x0420 | rule 27, Cinematics: outro steps 1-3 |
| 0x0421 | Cinematics: outro steps 3-4 (Bond) |
| 0x0422 | Cinematics: outro step 3 (Mishkin) |
| 0x0423 | Cinematics: outro step 3 (chr 0x0B) |
| 0x0424 | Cinematics: outro step 3 (chr 0x0C) |
| 0x1000 | rules 6, 8 |
| 0x1001 | rule 16 |
| 0x1002 | rule 21 |
| 0x1003 | rule 20 |
| 0x1004 | rule 24 |
| 0x1005 | rule 23 (recorder placement, room-of-pad-179 hint) |
| 0x1006 | rule 27 |
| 0x1007 | Cinematics: intro (starts Bond's swirl animation) |
| 0x1008 | rule 23 (staged explosions); the bytes after its end marker (a Valentin-death check) are dead: never executed |
