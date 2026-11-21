/* Project Dam: the system controller applet, for the multiplayer device screen (port/src/assign.c).
 * Its own file because libnx's types clash with the game's. */
#ifdef __SWITCH__
#include <stdio.h>
#include <string.h>
#include <switch.h>

/* Opens the system's controller screen and returns when it closes. padConfigureInput only declares the styles and
 * player count the game supports (it opens nothing): the applet is hidLaShowControllerSupport. libnx notes it shows
 * itself only when it is needed, so a request that comes from the player (force) asks it not to take over the
 * connected controllers: they are disconnected and have to be paired again, which is the screen to reassign them.
 * Returns the libnx Result (0 = it ran). */
unsigned nxConfigureInput(int players, int force)
{
    if (players < 1) players = 1;
    if (players > 8) players = 8;
    HidLaControllerSupportArg arg;
    hidLaCreateControllerSupportArg(&arg);
    arg.hdr.player_count_min = (s8)players;
    arg.hdr.player_count_max = (s8)players;
    arg.hdr.enable_single_mode = players == 1;   /* a solo game may use the handheld or a single Joy-Con */
    if (force) arg.hdr.enable_take_over_connection = 0;
    HidLaControllerSupportResultInfo info;
    memset(&info, 0, sizeof info);
    const Result rc = hidLaShowControllerSupport(&info, &arg);
    fprintf(stderr, "[NOTE ] assign: controller applet (%d player(s), force %d) = 0x%x, %d player(s) chosen\n",
            players, force, (unsigned)rc, (int)info.player_count);
    fflush(stderr);
    return (unsigned)rc;
}
#endif
