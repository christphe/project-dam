#ifndef PORT_COOP_H
#define PORT_COOP_H

/*
 * Opt-in 2 to 4 player split-screen co-op campaign (every PORT build; docs/dev/findings.md D960).
 * Logic lives in port/src/coop.c. src/game reaches it through the GE_* hook
 * macros in src/game/player.h (one-line expression substitutions).
 * The N64 build has no hooks: there the macros are the original expressions.
 */

/* 1 while the running stage is a co-op campaign stage (2 to 4 players, mission rules). */
int coopActive(void);

/* 1 in a solo stage and in a co-op stage: replaces `getPlayerCount() == 1` at rule sites. */
int coopIsMission(void);

#endif
