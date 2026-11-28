# Surface 1 coverage (`UsetupsevxZ.c`)

47 table entries, 46 distinct ids (0x100A appears twice; both lists run, every background list of the
table is started).

| List id | Covered by |
|---|---|
| 0x0401 | dead: patrol path 0, no guard or script uses the id |
| 0x0402 | dead: patrol path 1, unused |
| 0x0403 | data (chr 0x17 patrol path 2) + rule 13 |
| 0x0404 | data (chr 0x18 patrol path 3) + rule 13 |
| 0x0405 | dead: patrol path 4, unused |
| 0x0406 | data (chr 0x1A patrol path 5) + rule 13 |
| 0x0407 | dead: patrol path 6, unused |
| 0x0408 | data (chr 0x1C patrol path 7) + rule 13 |
| 0x0409 | dead: patrol path 8, unused |
| 0x040A | data (chr 0x1E patrol path 9) + rule 13 |
| 0x040B | dead: patrol path 10, unused |
| 0x040C | dead: patrol path 11, unused |
| 0x040D | dead: patrol path 12, unused |
| 0x040E | dead: patrol path 13, unused |
| 0x040F | dead: patrol path 14, unused |
| 0x0410 | dead: patrol path 15, unused |
| 0x0412 | dead: only reached from 0x0413, which is dead |
| 0x0413 | dead: no guard, spawn or script uses it (it only names itself as its return list) |
| 0x0414 | data (spawned chr 0x04) + rule 7 |
| 0x0415 | data (chr 0x05) + rule 7 |
| 0x0416 | data (chr 0x06) + rule 7 |
| 0x0417 | data (chr 0x07) + rule 7 |
| 0x0418 | rule 7 |
| 0x0419 | rules 9, 10, 11 |
| 0x041A | rule 7 (sniper weapon) |
| 0x041B | rules 11, 12 |
| 0x041C | rule 13 |
| 0x041D | data (chr 0x08, pad 9) + rule 14 |
| 0x041E | data (chr 0x09, pad 10) + rule 14 |
| 0x041F | data (chr 0x0A, pad 11) + rule 14 |
| 0x0420 | data (chr 0x0B, pad 12) + rule 14 |
| 0x0421 | data (heavy guard armour, hat, weapons) |
| 0x0422 | rule 16 |
| 0x0423 | Cinematics, Intro (Bond animation during the swirl) |
| 0x0424 | Cinematics, Exit |
| 0x1000 | rule 5 |
| 0x1001 | rule 6 (spawn point 0) |
| 0x1002 | rule 6 (spawn point 1) |
| 0x1003 | rule 6 (spawn point 2) |
| 0x1004 | rule 6 (spawn point 3) |
| 0x1005 | rules 1, 2, 3 |
| 0x1006 | rule 4 |
| 0x1007 | rule 14 |
| 0x1008 | rule 15 |
| 0x1009 | rule 17, Cinematics Exit steps 1-2 |
| 0x100A (first) | Cinematics, Intro (starts the swirl animation) |
| 0x100A (second) | rule 18 (hatch sound) |
