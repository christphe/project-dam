# Egyptian coverage

Setup `UsetupcrypZ.c`, 26 AI lists. Spec: `docs/campaign/specs/egyptian.md`.

| AI list | Rules | Notes |
|---|---|---|
| 0x0401 | dead | a plain Samedi variant (armour 300, waits for sight, then fights): no setup guard, spawn or list uses it |
| 0x0402 | 7, Characters | Samedi's three forms: stats and weapons by phase, 5 s invincible, laugh; a failed second weapon spawn falls into the next form's setup in the original (not kept) |
| 0x0403 | 11 | Samedi fight hub (debug print dropped) |
| 0x0404 | 12 | Samedi attack |
| 0x0405 | 11 | Samedi runs to the player; tail after its end marker is unreachable (dead) |
| 0x0406 | 13 | Samedi dodge |
| 0x0407 | 2 | slot 0 reinforcement takes chr 0x05 (debug print dropped) |
| 0x0408 | 2 | slot 1, chr 0x06 |
| 0x0409 | 2 | slot 2, chr 0x07 |
| 0x040A | 2 | slot 3, chr 0x08 |
| 0x040B | 2 | slot 4, chr 0x09 |
| 0x040C | 4, Characters | reinforcement ratings by mission time and random kit |
| 0x040D | 6 | hunt and attack |
| 0x040E | 6 | cover: run to the player (debug print dropped) |
| 0x040F | 6 | cover: kneel and fire (debug print dropped) |
| 0x0410 | Cinematics (outro 4) | Bond's walk, run on the Bond cinema chr |
| 0x0411 | Cinematics (outro 5-8) | Samedi's laugh scene and level end |
| 0x1001 | 14, 15, 16 | golden gun floor puzzle |
| 0x1002 | 1, 2 | slot 0 spawner, 40 s |
| 0x1003 | 1, 2 | slot 1 spawner, 40 s |
| 0x1004 | 1, 2 | slot 2 spawner, 50 s |
| 0x1005 | 1, 2 | slot 3 spawner, 50 s |
| 0x1006 | 1, 2 | slot 4 spawner, 60 s |
| 0x1007 | 3, 5 | spawning off room and spawn pads |
| 0x1008 | 8, 9, 10 | Samedi's returns, appearance objects, fog, objective b (debug prints dropped) |
| 0x1009 | 17, Cinematics (outro 1-5) | end sequence; dead-player branch hands the list to the do-nothing global list |
