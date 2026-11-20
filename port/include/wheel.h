/* The weapon and gadget wheels of the Modern control style (see port/src/wheel.c). */
#ifndef WHEEL_H
#define WHEEL_H

void portWheelGameTick(void);                 /* game thread, once per frame */
int portWheelBlocks(int slot);                /* poll thread: the pad is neutral while its wheel is open */
/* render thread: the open wheel's item names and the picked one; returns the item count (0 = no wheel).
 * kind: 1 = weapons, 2 = gadgets. */
int portWheelMask(void);                      /* bit per seat whose wheel is open */
int portWheelState(int seat, char names[][24], int max, int *sel, int *kind);
/* where a seat's picture is on the screen, as fractions of it */
void portSeatRegion(int seat, float *x0, float *y0, float *x1, float *y1);

#endif
