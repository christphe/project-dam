/* Project Dam: the system controller applet, for the multiplayer device screen (port/src/assign.c).
 * Its own file because libnx's types clash with the game's. */
#ifdef __SWITCH__
#include <switch.h>

void nxConfigureInput(int players)
{
    padConfigureInput((u32)players, HidNpadStyleSet_NpadStandard);
}
#endif
