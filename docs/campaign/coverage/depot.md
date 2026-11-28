# Depot coverage

Setup `UsetupdepoZ.c`, 27 AI lists. Spec: `docs/campaign/specs/depot.md`.

| AI list | Rules | Notes |
|---|---|---|
| 0x0401 | 2 | slot 0 reinforcement takes chr 0x00 (debug print dropped) |
| 0x0402 | 2 | slot 1, chr 0x01 |
| 0x0403 | 2 | slot 2, chr 0x02 |
| 0x0404 | 2 | slot 3, chr 0x03 |
| 0x0405 | 2 | slot 4, chr 0x04 |
| 0x0406 | 2 | slot 5, chr 0x05 |
| 0x0407 | 2 | slot 6, chr 0x06 |
| 0x0408 | data | random reinforcement kit (Characters), then rule 5 |
| 0x0409 | 5 | hunt and attack |
| 0x040A | 6 | cover: run to the player (debug print dropped) |
| 0x040B | 6 | cover: kneel and fire (debug print dropped) |
| 0x040C | Cinematics (outro 6-7) | Bond's final shot, run on the Bond cinema chr |
| 0x040D | Cinematics (outro 4-5) | train guards shot by Bond, run on the Bond cinema chr |
| 0x040E | Cinematics (outro 5) | train guard 0x0E alone |
| 0x040F | Cinematics (outro 5) | train guard 0x0F |
| 0x0410 | Cinematics (outro 5) | train guard 0x0E with 0x0F present |
| 0x0411 | dead | Bond intro animation 97: no setup record, intro record or list hands Bond to it; its own tail (after the end marker, unreachable) is the only reference |
| 0x1000 | 7 | train door opens -> objective e |
| 0x1001 | 3, 4 | spawning on/off rooms and spawn pads; the set for the room of pad 230 is unreachable (that room is an off room, checked first) and one set (pads 154, 201, 44, 45, 219, 178, 224) is never selected: dead |
| 0x1002 | 1, 2 | slot 0 spawner, 20 s |
| 0x1003 | 1, 2 | slot 1 spawner, 25 s |
| 0x1004 | 1, 2 | slot 2 spawner, 30 s |
| 0x1005 | 1, 2 | slot 3 spawner, 35 s |
| 0x1006 | 1, 2 | slot 4 spawner, 60 s |
| 0x1007 | 1, 2 | slot 5 spawner, 120 s |
| 0x1008 | 1, 2 | slot 6 spawner, 150 s |
| 0x1009 | 8, Cinematics (outro 1-3) | exit; dead-player branch hands the list to the do-nothing global list |
