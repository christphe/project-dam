# Archives coverage (UsetuparchZ.c)

| AI list | Covered by | Notes |
|---|---|---|
| 0x0401 | data | chr 23: patrol path 0, then simple generic guard |
| 0x0402 | data | chr 24: patrol path 1, then simple generic guard |
| 0x0403 | data | chr 25: patrol path 2, then simple generic guard |
| 0x0404 | 7 | chr 26: patrol path 3, then the new-orders soldier |
| 0x0405 | data | chr 27: patrol path 4, then simple generic guard |
| 0x0406 | data | chr 28: patrol path 5, then simple generic guard |
| 0x0407 | data | chr 29: patrol path 6, then simple generic guard |
| 0x0408 | 10, 11, 12, 14 | Natalya main |
| 0x0409 | 9 | Natalya sees Bond, objective b |
| 0x040A | 4, 8 | hostile interrogator / soldiers 9, 11 |
| 0x040B | 12 | Natalya hides at pad 158 |
| 0x040C | 12 | Natalya hides at pad 196 |
| 0x040E | 1, 2, 3 | interrogator talk and triggers |
| 0x040F | 6 | second interrogator |
| 0x0410 | 7 | new-orders soldier |
| 0x0411 | 16, 17, 18, 19, 20 | Mishkin |
| 0x0412 | Cinematics: outro 1, 3, 4 | Bond model in the outro, level end |
| 0x0413 | Cinematics: outro 5 | Natalya in the outro |
| 0x1000 | 22, Cinematics: outro 1-3 | exit, end checks, outro setup |
| 0x1001 | 15 | Natalya gone -> objective d failed |
| 0x1002 | 21 | Mishkin dead before the key -> objective c failed |

There is no list 0x040D in the table. Dropped as mechanism: the "hello" / "saw bond" debug strings; the
per-character marker bits the interrogators set on each other and on chr 26 (kept as "alerted"); the
helper call from 0x0408 to 0x0409; the per-tick reprint of dialogue lines inside their 1 s windows.
