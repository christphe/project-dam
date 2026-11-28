# Jungle coverage (UsetupjunZ.c)

| AI list | Covered by | Notes |
|---|---|---|
| 0x0401 | dead | empty list, no guard or list refers to it |
| 0x0402 | 1, data | Xenia's start: stats (Characters) and first sighting |
| 0x0403 | 3 | Xenia's decision |
| 0x0404 | 4 | Xenia's attack |
| 0x0405 | 5 | Xenia's chase |
| 0x0406 | 6 | Xenia's evade |
| 0x0407 | 9 | Natalya follow / wander |
| 0x0408 | 8, data | Natalya start: stats, waits for the intro |
| 0x0409 | 11, 12 | Natalya target search, ranged fight |
| 0x040A | 10 | Natalya chatter / low-health warning |
| 0x040B | 13 | Natalya close fight |
| 0x040C | 16 | Natalya look around |
| 0x040D | 18, 19 | jungle guard idle / alert |
| 0x040E | 20 | jungle guard attack (incl. turning on Natalya) |
| 0x040F | 22 | look around then hunt |
| 0x0410 | 21 | hunt Bond |
| 0x0411 | dead | not referenced by any guard or list (clone sender) |
| 0x0412 | dead | only referenced from 0x0411 |
| 0x0413 | 23 | post guard |
| 0x0414 | 26 | reinforcement slot 0x27 |
| 0x0415 | 26 | reinforcement slot 0x28 |
| 0x0416 | 26 | reinforcement slot 0x29 |
| 0x0417 | 26 | reinforcement slot 0x1D |
| 0x0418 | 26 | reinforcement slot 0x22 |
| 0x0419 | 26 | reinforcement slot 0x1E |
| 0x041A | 26 | reinforcement hat and rifle |
| 0x041B | 26 | reinforcement sprint to post |
| 0x041C | 20a | guard fights Natalya |
| 0x041D | Cinematics: intro | Bond model intro animation |
| 0x041E | Cinematics: outro 3-4 | Natalya in the outro |
| 0x041F | Cinematics: outro 3-5 | Bond model in the outro, level end |
| 0x1000 | 2 | Xenia dead -> objective b |
| 0x1001 | 17 | Natalya dead -> objective d failed |
| 0x1002 | 29, Cinematics: outro 1-3 | base entrance, end checks, outro setup |
| 0x1003 | 24, 25, 28 | reinforcement spawner |
| 0x1004 | 27 | reinforcement ramp-down (quirk noted) |
| 0x1005 | Cinematics: intro | starts the Bond model animation at the swirl |
| 0x1006 | 30 | crates of tags 3-5 explode at 10/12/13 s |

Dropped as mechanism: debug log strings in 0x0403-0x0409, 0x040B, 0x040C, 0x041C, 0x1003, 0x1004 and
0x0414-0x0419; the per-character mark bit passed between Natalya and the guards (kept as intent:
"marked"); the never-ending idle loops after 0x1000/0x1001 fire.
