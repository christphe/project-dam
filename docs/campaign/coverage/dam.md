# Dam coverage

| AI list | Covered by |
|---|---|
| 0x0401 | data (chr 0x27 patrol path 0, then generic) |
| 0x0402 | data (chr 0x28 patrol path 1) |
| 0x0403 | data (chr 0x29 patrol path 2) |
| 0x0404 | data (chr 0x2a patrol path 3) |
| 0x0405 | data (chr 0x2b patrol path 4) |
| 0x0406 | data (chr 0x2c patrol path 5) |
| 0x0407 | data (chr 0x2d patrol path 6) |
| 0x0408 | rule 15 |
| 0x0409 | dead: no guard, vehicle or list refers to it (a distance-triggered ambusher) |
| 0x040a | Objects (truck on path 7) |
| 0x040b | dead: rotor speed for an aircraft; the Dam setup has no aircraft and nothing refers to it |
| 0x040c | rule 12 |
| 0x040d | rule 13 |
| 0x040e | dead: an empty idle loop nothing refers to |
| 0x040f | rule 11 |
| 0x0410 | rule 16 |
| 0x0411 | Cinematics intro 3 |
| 0x0412 | Cinematics outro 1-4 |
| 0x0413 | rule 14 |
| 0x0414 | rule 15 (walk to the post) |
| 0x1000 | rules 3-6 |
| 0x1001 | rules 7-10 |
| 0x1002 | rule 11 |
| 0x1003 | Objects (gate airlock lines) |
| 0x1004 | rule 17, Cinematics outro 1 |
| 0x1005 | Cinematics intro 3 |
| 0x1006 | Objects (gate status screens, gate A) |
| 0x1007 | Objects (gate status screens, gate B) |

Dropped as dead or mechanism:
- 0x1000: a second "satellite destroyed" block that no jump reaches (same text, different flags).
- 0x0408: a timer-reset entry point no jump reaches; debug prints.
- 0x040a: debug print.
