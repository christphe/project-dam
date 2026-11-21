/* Who plays with which device: the screen asked when a multiplayer game is set up (see port/src/assign.c). */
#ifndef ASSIGN_H
#define ASSIGN_H

void portAssignTick(void);       /* game thread, once per frame */
void portAssignRequest(void);    /* the menu: choose the devices again (the system controller screen on Switch) */
int portAssignBlocks(void);      /* poll thread: the game's own menus ignore the pads while the screen is up */
/* render thread: 1 and what each player got so far while the screen is up */
int portAssignState(char names[][48], int *step, int *count);

#endif
