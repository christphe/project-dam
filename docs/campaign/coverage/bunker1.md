# Bunker 1 coverage

Setup `assets/obseg/setup/UsetupsevbunkerZ.c`, spec `docs/campaign/specs/bunker1.md`.

| AI list | Covered by |
|---|---|
| 0x0401 | data (chr 0x00 patrol path 0, then simple guard) |
| 0x0402 | data (chr 0x08 patrol path 1, then simple guard) |
| 0x0403 | dead: no guard, object or list refers to it (would spawn a clone next to itself) |
| 0x0404 | dead: only reached from 0x0403 (KF7 + hat loadout, then standard guard) |
| 0x0405 | rules 13, 14 (gives the spawned reinforcement chr number 0x1A) |
| 0x0406 | rules 13, 14 (chr 0x1B) |
| 0x0407 | rules 13, 14 (chr 0x1C) |
| 0x0408 | rules 1-7 (the programmer chr 0x19); its fade-out block after the arrival branch is unreachable |
| 0x0409 | rule 14 |
| 0x040A | Cinematics: outro steps 3-5 (run by the third-person Bond) |
| 0x040B | rule 15 |
| 0x040C | rule 7 |
| 0x040D | rules 16, 17 (chr 0x07) |
| 0x1000 | rule 13 |
| 0x1001 | rule 8 |
| 0x1002 | rules 9, 10 |
| 0x1003 | rule 18, Cinematics: outro steps 1-2 |
| 0x1004 | rule 11 |
| 0x1005 | rule 12 |
