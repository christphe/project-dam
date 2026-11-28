# Coverage: global AI lists

Spec: `docs/campaign/specs/global.md`. One row per global list id (`g_GlobalAILists`, `src/game/chraidata.c`).

| id | List | Rules | Notes |
|---|---|---|---|
| 0x0000 | AimAtBond | 29 | |
| 0x0001 | DeadAI | 30 | |
| 0x0002 | StandardGuard | 1-10, 34 | its no-op set of the return list before the reinforcement jump is dropped (the return list is already the standard guard) |
| 0x0003 | IdleAnimations | 13 | the PORT hand expansion and the IDO SWITCH form give the same buckets |
| 0x0004 | BashKeyboard | 14 | |
| 0x0005 | SimpleGuardDeaf | 12 | |
| 0x0006 | AttackBond | 8, 15 | the `#if 0` alternative block is dead (not compiled) |
| 0x0007 | SimpleGuard | 11 | the trailing reinforcement branch is dead: no check jumps to it (hearing goes to chase) |
| 0x0008 | RunToBond | 16 | |
| 0x0009 | SimpleGuardAlarmRaiser | 22, 23 | |
| 0x000A | StartleAndRunToBond | 17 | |
| 0x000B | TryCloneSendOrRunToBond | 9, 18, 20, 21 | |
| 0x000C | StandardClone | 19 | |
| 0x000D | RunToBondPersistent | 3, 24-27 | the distance re-check block after the stopped branch is dead (unreachable, nothing jumps to it); the debug prints are dropped |
| 0x000E | WaitOneSecond | 31 | |
| 0x000F | EndLevel | 32 | |
| 0x0010 | DrawPistolAndAttackBond | 28 | |
| 0x0011 | RemoveSelf | 33 | |
