/* Who plays with which device, asked when a multiplayer game is set up.
 *
 * PC: once the player count is 2 or more and the saved assignment does not cover it, a "press a button" screen
 * asks each player in turn: a gamepad button takes that pad, a key press takes the keyboard and mouse (player 1
 * only). The choice is saved in the ini (Input.Player<N>Pad, Input.P1Type) and the screen is skipped while it
 * still holds. Esc skips it for this player count.
 * Switch: the system controller applet does the same job, once.
 *
 * Game thread: portAssignTick (libultra.c frame hook). Poll thread: portAssignBlocks keeps the game's own menus
 * from reacting to the presses. Render thread: portAssignState draws the screen (menuAssignFrame). */
#include <stdio.h>
#include <string.h>
#include <SDL.h>
#include <ultra64.h>
#include <bondconstants.h>
#include "front.h"
#include "config.h"
#include "input.h"
#include "system.h"
#include "assign.h"

#ifdef __SWITCH__
extern unsigned nxConfigureInput(int players, int force);   /* port/src/switch/nxassign.c */
#endif

extern MENU current_menu;
extern s32 get_selected_num_players(void);
extern s32 getPlayerCount(void);
extern s8 joyGetControllerCount(void);

static SDL_atomic_t s_active;
static SDL_SpinLock s_lock;
static int s_count = 0, s_step = 0;
static char s_names[4][48];          /* what each player got so far */
static int s_dismissedCount = 0;     /* the player count the user skipped the screen for */
static int s_armed = 0;              /* every button released since the screen opened */
static int s_switchAsked = 0;
static SDL_atomic_t s_forced;        /* the menu asked for the screen (to swap the controllers) */
static int s_manual = 0;             /* the screen was asked for by the menu: it stays whatever the screen the game is on */

#define ASSIGN_MENU_RUN_STAGE 11   /* current_menu while a mission runs (input.c's GE_MENU_RUN_STAGE) */
static int frontEnd(void)
{
    return (int)current_menu != ASSIGN_MENU_RUN_STAGE && (int)current_menu != -1;
}

#ifndef __SWITCH__
/* the saved assignment covers `count` players: the keyboard for player 1, or connected pads in the ini */
static int satisfied(int count)
{
    inputPadListRefresh();
    for (int p = 0; p < count; p++) {
        if (p == 0 && inputP1Type() == 0) continue;   /* the keyboard and mouse */
        const char *id = inputPadPref(p);
        int ok = 0;
        for (int k = 0; id && id[0] && inputPadListId(k)[0]; k++)
            if (!strcmp(id, inputPadListId(k))) { ok = 1; break; }
        if (!ok) return 0;
    }
    return 1;
}

static int anyKeyDown(void)
{
    const Uint8 *ks = SDL_GetKeyboardState(NULL);
    for (int i = 4; i < 256; i++)   /* letters, digits, arrows, space, enter... */
        if (ks[i] && i != SDL_SCANCODE_ESCAPE) return 1;
    return 0;
}
#endif

static SDL_atomic_t s_want;          /* the player count the screen is asked for (0 = the game's own) */

void portAssignRequest(void)
{
    sysLogPrintf(LOG_NOTE, "assign: the menu asked for the controller screen");
    SDL_AtomicSet(&s_want, 0);
    SDL_AtomicSet(&s_forced, 1);
}

/* The front end needs `n` players: 1 when that many controllers are connected, otherwise the controller screen opens
 * for `n` players (Co-op and Multiplayer stay open with one pad) and the mode waits for the next press. */
int portNeedPads(int n)
{
    if (joyGetControllerCount() >= n) return 1;
    sysLogPrintf(LOG_NOTE, "assign: %d player(s) need %d controller(s), %d connected", n, n, (int)joyGetControllerCount());
    SDL_AtomicSet(&s_want, n);
    SDL_AtomicSet(&s_forced, 1);
    return 0;
}

void portAssignTick(void)
{
    const int count = frontEnd() ? get_selected_num_players() : 1;
#ifdef __SWITCH__
    if (SDL_AtomicSet(&s_forced, 0)) {   /* the menu: the system's controller screen, whoever is connected */
        s_switchAsked = 1;
        int n = frontEnd() ? get_selected_num_players() : getPlayerCount();   /* in a match: the players there are */
        if (SDL_AtomicGet(&s_want) > 0) n = SDL_AtomicGet(&s_want);
        sysLogPrintf(LOG_NOTE, "assign: opening the controller applet for %d player(s) (front end %d)", n < 1 ? 1 : n, frontEnd());
        const unsigned rc = nxConfigureInput(n < 1 ? 1 : n, 1);
        sysLogPrintf(LOG_NOTE, "assign: the controller applet closed (0x%x)", rc);
        inputRequestRescan();   /* re-seat the pads that came back */
        return;
    }
    return;   /* the front end asks through portNeedPads, when a mode needs more pads than are connected */
#else
    if (!SDL_AtomicGet(&s_active) && SDL_AtomicSet(&s_forced, 0)) {   /* the menu asked: start over, for the players there are */
        int n = frontEnd() ? get_selected_num_players() : getPlayerCount();
        if (SDL_AtomicGet(&s_want) > 0) n = SDL_AtomicGet(&s_want);
        SDL_AtomicLock(&s_lock);
        s_count = n < 1 ? 1 : n; s_step = 0; s_armed = 0;
        memset(s_names, 0, sizeof s_names);
        SDL_AtomicUnlock(&s_lock);
        s_manual = 1;
        SDL_AtomicSet(&s_active, 1);
        sysLogPrintf(LOG_INFO, "assign: asked from the menu, %d player(s)", s_count);
        return;
    }
    if (!SDL_AtomicGet(&s_active)) {
        if (count < 2) { s_dismissedCount = 0; return; }
        if (count == s_dismissedCount || satisfied(count)) return;
        SDL_AtomicLock(&s_lock);
        s_count = count; s_step = 0; s_armed = 0;
        memset(s_names, 0, sizeof s_names);
        SDL_AtomicUnlock(&s_lock);
        SDL_AtomicSet(&s_active, 1);
        sysLogPrintf(LOG_INFO, "assign: %d players, asking who plays with which device", count);
        return;
    }
    if (!s_manual && (count < 2 || !frontEnd())) { SDL_AtomicSet(&s_active, 0); return; }
    if (SDL_GetKeyboardState(NULL)[SDL_SCANCODE_ESCAPE]) {   /* skip: the game's own order stays */
        if (!s_manual) s_dismissedCount = count;
        s_manual = 0;
        SDL_AtomicSet(&s_active, 0);
        return;
    }
    char id[48];
    const int pad = inputPadNewPress(id, (int)sizeof id);
    const int key = !pad && anyKeyDown();
    if (!s_armed) {   /* nothing counts until every button was let go (the press that opened this screen) */
        if (!pad && !key) s_armed = 1;
        return;
    }
    if (!pad && !(key && s_step == 0)) return;
    SDL_AtomicLock(&s_lock);
    const int step = s_step;
    SDL_AtomicUnlock(&s_lock);
    char label[48];
    if (pad) {
        /* a pad can serve one player only */
        for (int p = 0; p < step; p++) if (!strcmp(inputPadPref(p), id)) return;
        inputPadPrefSet(step, id);
        snprintf(label, sizeof label, "%s", inputPadNameForId(id));
        if (step == 0) configSetValue("Input.P1Type", 2.0);   /* player 1 on a gamepad: Auto (a hard "Gamepad" blocks the keyboard path where the dedicated use / reload live) */
    } else {
        configSetValue("Input.P1Type", 0.0);   /* the keyboard and mouse */
        inputPadPrefSet(0, "");
        snprintf(label, sizeof label, "Keyboard + mouse");
        inputRequestRescan();
    }
    SDL_AtomicLock(&s_lock);
    snprintf(s_names[step], sizeof s_names[step], "%s", label);
    s_step++;
    const int done = s_step >= s_count;
    SDL_AtomicUnlock(&s_lock);
    s_armed = 0;
    if (done) {
        configSave();
        s_manual = 0;
        SDL_AtomicSet(&s_active, 0);
        sysLogPrintf(LOG_INFO, "assign: devices chosen and saved");
    }
#endif
}

int portAssignBlocks(void)
{
    return SDL_AtomicGet(&s_active);
}

int portAssignState(char names[][48], int *step, int *count)
{
    if (!SDL_AtomicGet(&s_active)) return 0;
    SDL_AtomicLock(&s_lock);
    *step = s_step;
    *count = s_count;
    memcpy(names, s_names, sizeof s_names);
    SDL_AtomicUnlock(&s_lock);
    return 1;
}
