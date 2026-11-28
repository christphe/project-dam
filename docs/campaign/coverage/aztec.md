# Aztec coverage

Setup `assets/obseg/setup/UsetupaztZ.c`, spec `docs/campaign/specs/aztec.md`.

| AI list | Covered by |
|---|---|
| 0x0401 | dead: no guard or list uses it (patrol path 0, then simple guard) |
| 0x0402 | data (speed / accuracy 50, then standard guard) |
| 0x0403 | data (Jaws stats, then standard guard) |
| 0x0404 | data (speed / accuracy 50, then simple deaf guard) |
| 0x0405 | rule 14 |
| 0x0406 | rules 15, 16 (gives the slot 1 reinforcement chr number 0x03) |
| 0x0407 | rules 15, 16 (chr 0x04) |
| 0x0408 | rules 15, 16 (chr 0x05) |
| 0x0409 | rule 16; its alternative speed / accuracy blocks (50, 60, 80) are never reached |
| 0x040A | rule 17; its "remove when far and off screen" branch is unreachable (its distance tests can never both hold) |
| 0x1000 | rule 5 |
| 0x1001 | rules 1, 3, 4 |
| 0x1002 | rule 2 |
| 0x1003 | rules 6, 7 |
| 0x1004 | rules 8, 9 |
| 0x1005 | rules 10, 11 |
| 0x1006 | Cinematics: outro |
| 0x1007 | rule 12 |
| 0x1008 | rule 15 (slot 1) |
| 0x1009 | rule 15 (slot 2) |
| 0x100A | rule 15 (slot 3) |
| 0x100B | rule 13 |
| 0x100C | rule 18 |
