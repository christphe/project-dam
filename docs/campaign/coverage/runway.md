# Runway coverage

| AI list | Covered by |
|---|---|
| 0x0401 | data (chr 0x17 patrol path 0, then generic) |
| 0x0402 | data (chr 0x18 patrol path 1, then generic) |
| 0x0403 | rule 4 |
| 0x0404 | rule 3 (slot 1 takes chr 0x19), then rules 4-5 |
| 0x0405 | rule 3 (slot 2 takes chr 0x1a), then rules 4-5 |
| 0x0406 | dead: only the unlisted spawner for chr 0x1b uses it (see below) |
| 0x0407 | rule 3 (slot 3, chr 0x1c) |
| 0x0408 | rule 3 (slot 4, chr 0x1d) |
| 0x0409 | rule 3 (slot 5, chr 0x1e) |
| 0x040a | dead: only the unlisted spawner for chr 0x1f uses it (see below) |
| 0x040b | rule 5 |
| 0x040c | Cinematics intro 3 |
| 0x040d | Objects (plane), Cinematics outro 3 |
| 0x1000 | rules 1, 2 |
| 0x1001 | rule 3 (slot 1) |
| 0x1002 | rule 3 (slot 2) |
| 0x1004 | rule 3 (slot 3) |
| 0x1005 | rule 3 (slot 4) |
| 0x1006 | rule 3 (slot 5) |
| 0x1008 | rules 6, 7, Cinematics outro |
| 0x1009 | Cinematics intro 3 |
| 0x100a | Cinematics outro 4 |

Dead code notes:
- Two spawner bodies (for chr 0x1b and chr 0x1f, ids 0x1003 and 0x1007 by position) sit after the end of
  lists 0x1002 and 0x1006 with no entry in the table, so they never run; chr 0x1b and 0x1f are never replaced.
  Rule 2's table therefore omits the pads the background list sets for those two spawners.
- Every spawner has a pause branch on a stage flag nothing ever sets: dropped.
- Debug prints in the spawners and the reinforcement lists: dropped.
