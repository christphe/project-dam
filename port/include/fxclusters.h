#ifndef FXCLUSTERS_H
#define FXCLUSTERS_H

/* The bullet holes on the level as clusters (port/src/fxclusters.c). */

#include "portevents.h"

/* Level hit: sets the event's material (and a matching hole type) from the hit texture, once, before the hole and the
 * particles use it. Props and events without a triangle are left as they are. */
void fxClustersResolveMaterial(PortEvImpact *e);

/* A bullet hit the level (PEV_IMPACT; props keep the game's own holes): stamps it into its cluster. Takes an event
 * fxClustersResolveMaterial has been run on. */
void fxClustersAddImpact(const PortEvImpact *e);

/* Draws every cluster (the level's holes) into the world pass. */
Gfx *fxClustersDraw(Gfx *gdl);

#endif
