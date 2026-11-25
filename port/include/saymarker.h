#ifndef _SAYMARKER_H_
#define _SAYMARKER_H_

/* Positioned dialogue lines (#132). A script line that comes from a place (a guard, a prop, a pad, a zone) shows a
 * marker over its speaker in every player's view: drawn over everything (seen through walls), fainter and smaller
 * the further away, pinned to the edge of the view, pointing at the speaker, when the speaker is out of view.
 *
 * The text itself stays the game's top message (bondview2.c hudmsgTopShow): one shared two-slot queue drawn in
 * every player's view. A marker is attached to the queue slot its line lands in, and checked against that slot's
 * text when drawn, so a game message that later takes the slot never inherits it.
 *
 * Game-includable: bondview2.c calls the three hooks under #ifdef PORT. */

#include <ultra64.h>

/* Where the speaker is now: the point the marker hangs from (above the head), in world units. 0: gone (no marker). */
typedef int (*SayAnchorFn)(void *ud, int key, float xyz[3]);

/* Call right before hudmsgTopShow(line). The speaker is fn(ud, key) while the line shows (a moving guard), or the
 * fixed point when fn is NULL (a pad, a zone); neither: a line with no place (plain say), the slot carries no marker.
 * range > 0: a player farther than this (world units) sees neither the line nor the marker; 0: no limit. */
void sayMarkerQueue(const char *line, SayAnchorFn fn, void *ud, int key, const float *fixed, float range);

/* Stage load (bondviewResetUpperTextDisplay): forget every marker. */
void sayMarkerReset(void);

/* For g_CurrentPlayer, in its HUD pass: 1 when the shown line has a range and this player is beyond it, so the
 * top message is skipped in this player's view only. */
s32 sayMarkerHidesLine(void);

/* For g_CurrentPlayer, after the top message: the marker of the shown line, if it has one. */
Gfx *sayMarkerRender(Gfx *gdl);

#endif
