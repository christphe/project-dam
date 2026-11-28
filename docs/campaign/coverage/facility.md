# Facility coverage (`UsetuparkZ`)

| AI list | Covered by | Notes |
|---|---|---|
| 0x0401 | data | chr 0x2C: patrol path 0, then simple generic guard |
| 0x0402 | data | chr 0x2D: patrol path 1 |
| 0x0403 | data, rule 9 | chr 0x42 (scientist): patrol path 2, then the patrol-scientist reaction |
| 0x0404 | data | chr 0x2F, 0x30: patrol path 3 |
| 0x0405 | data | chr 0x31, 0x32: patrol path 4 |
| 0x0406 | data | chr 0x27: patrol path 5 |
| 0x0407 | data | chr 0x28: patrol path 6 |
| 0x0408 | rules 19, 20 | chr 0x2A, 0x2B: line of sight sets the reinforcement trigger, then 0x0409 |
| 0x0409 | rule 21 | fire walking / running on sight, then standard guard |
| 0x040A | dead | empty list, used by no character |
| 0x040B | rule 20 | the four spawned corridor reinforcements; the second "line of sight" branch (run-to-player) is unreachable |
| 0x040C | rule 9 | patrolling scientist alert -> cower |
| 0x040D | rules 8, 10 | scientist main reactions; debug print dropped |
| 0x040E | rule 10 (dodge) | |
| 0x040F | rule 11 | cower / hands up, TT33 after 2+ hits |
| 0x0410 | rule 12 | scientist flight to pad 0x17 |
| 0x0411 | rule 32 | Ourumov's invincible flight to pad 0x17 |
| 0x0412 | rule 14 | scientist 0x39, 240 s trip to pad 0x0B; debug print dropped |
| 0x0413 | rule 23; Characters | Trevelyan's stats and wandering |
| 0x0414 | rules 24, 25, 26, 27, 29 | meeting dialogue, alarm, ambush stance; "flag 0x10" (came back) is a latch for the 0x0C15 line |
| 0x0415 | rule 31 | Trevelyan surrenders and kneels |
| 0x0416 | rule 28 | Trevelyan traitor |
| 0x0417 | rules 31, 32 | Ourumov arrival, countdown, fight, flight; the trailing run-to-player branch is unreachable (dead) |
| 0x0418 | Characters (0x4E), rule 30 | Ourumov stats, chr number, TT33 |
| 0x0419 | rule 33 | squad chr 0x44, cover pad 0x73 |
| 0x041A | rule 33 | squad chr 0x45, cover 0x74 |
| 0x041B | rule 33 | squad chr 0x46, cover 0x75 |
| 0x041C | rule 33 | squad chr 0x47, cover 0x76 |
| 0x041D | rule 33 | squad chr 0x48, cover 0x77 |
| 0x041E | rule 33 | squad chr 0x49, cover 0x79 |
| 0x041F | rule 34 | replacement chr 0x25 |
| 0x0420 | rule 34 | replacement chr 0x26 |
| 0x0421 | rule 34 | replacement chr 0x27 |
| 0x0422 | dead | would make chr 0x28 a replacement; no spawn uses this list |
| 0x0423 | rule 33 | squad soldier behaviour; its fade-out block is unreachable (dead) |
| 0x0424 | rule 34 | replacement soldier behaviour |
| 0x0425 | rules 15, 16 | Doak |
| 0x0426 | Cinematics exit 3 | Bond's body, exit via the room of pad 0x87 |
| 0x0427 | Cinematics exit 4 | Bond's body, exit via the room of pad 0x135 (listed twice in the table, same list) |
| 0x0428 | rule 22 | room-watching guards 0x05..0x0B |
| 0x1000 | rules 8, 30, 33, 34 | keyboard worker marks; Ourumov, squad and replacement spawns; debug prints dropped |
| 0x1001 | rule 35 | gas leak + 5 s -> stop respawns, start fight, Ourumov flees |
| 0x1002 | rule 5 | panel activation text |
| 0x1003 | rule 20 | four corridor reinforcements |
| 0x1004 | rule 15 | Doak's spawn pad choice; sets a "Doak spawned" latch read by 0x100C |
| 0x1005 | rule 6 | gas seal of door 0x20 |
| 0x1006 | rules 36, 37, 38; Cinematics exit 1-2 | exit trigger and choice of exit scene |
| 0x1007 | rules 3, 4 | decoder door |
| 0x1008 | rule 13 | civilians and failure of e |
| 0x1009 | rule 7 | screen 0x0B follows door 0x27 |
| 0x100A | rule 7 | screen 0x0C follows door 0x20 |
| 0x100B | rule 7 | screen 0x0D follows door 0x29; after its end marker it holds a memory-test spawn (body 7 at pad 0x58) that is never executed (dead) |
| 0x100C | rule 17 | Doak killed -> objective b fails (00 Agent, 007) |
| 0x100D | rule 7 | screens 0x2A, 0x2B follow door 0x20 |
| 0x100E | rule 7 | screens 0x2C, 0x2D follow door 0x21 |
