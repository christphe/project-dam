#ifndef FXCLUSTERS_H
#define FXCLUSTERS_H

/* The bullet holes on the level as clusters (port/src/fxclusters.c). */

#include "portevents.h"

/* A bullet hit the level (PEV_IMPACT; props keep the game's own holes): stamps it into its cluster. */
void fxClustersAddImpact(const PortEvImpact *e);

/* Draws every cluster (the level's holes) into the world pass. */
Gfx *fxClustersDraw(Gfx *gdl);

#endif
