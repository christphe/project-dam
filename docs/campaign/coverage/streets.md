# Streets coverage (UsetuppeteZ.c)

| AI list | Covered by | Notes |
|---|---|---|
| 0x0401 | data | idle list of the parked cars |
| 0x0402 | dead | vehicle path start near pad 266: no vehicle or list refers to it |
| 0x0403 | 15, 17 | spawned soldier 0x20 |
| 0x0404 | 15, 19 | spawned civilian 0x27 |
| 0x0405 | dead | soldier 0x21: only used by the spawner of chr 0x2C, which is not in the table |
| 0x0406 | 15, 19 | spawned civilian 0x28 |
| 0x0407 | 15, 17 | spawned soldier 0x22 |
| 0x0408 | dead | civilian 0x29: only used by the spawner of chr 0x2F, not in the table |
| 0x0409 | dead | soldier 0x23: only used by the spawner of chr 0x30, not in the table |
| 0x040A | 17 | soldier loadout (two loadout branches inside are unreachable: dual Klobbs, rocket launcher) |
| 0x040B | 19, 20, 21 | civilian behaviour |
| 0x040C | 18 | hunter |
| 0x040D | 13 | rocket trooper |
| 0x040E | data | ambush guards wait (Characters) |
| 0x040F | 13 | rifleman |
| 0x0410 | 21, 4 | civilian vanishes, refund |
| 0x0411 | 18 | hunter vanishes |
| 0x0412 | 14 | posted riflemen |
| 0x0413 | 5, 6, 7, 8, 11, 12 | Valentin |
| 0x0414 | Cinematics: outro 3-5 | Bond model in the outro, level end |
| 0x1000 | 22, Cinematics: outro 1-3 | end point |
| 0x1001 | 16 | spawn pads by area (its pause branch is unreachable, so spawners never pause) |
| 0x1002 | 15 | soldier spawner (0x20, 5 s) |
| 0x1003 | 15, 4 | civilian spawner (0x27, 6 s); a second spawner (0x2C, 7 s) after its end is unreachable |
| 0x1005 | 15, 4 | civilian spawner (0x28, 8 s) |
| 0x1006 | 15 | soldier spawner (0x22, 9 s); two spawners (0x2F, 10 s; 0x30, 3 s) after its end are unreachable |
| 0x1009 | 1, 2, 3 | chase countdown (a branch setting 600 s is unreachable) |
| 0x100A | 4 | civilian budget |
| 0x100B | 13 | ambush role coin flips |
| 0x100C | 10 | Valentin killed -> objective a failed |

Dropped as mechanism: debug log strings; the pause/resume handshake between 0x1001 and the spawners
(never triggered); the per-tick reprint of Valentin's lines inside their 1 s windows.
