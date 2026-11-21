/* Null platform + fake joystick feed (see null_platform.h). Test tree only. */
#include <stdlib.h>
#include <string.h>
#include "null_platform.h"
#include "defaults.h"
#include "input.h"

int nullPlatformInitSdl(void)
{
    /* Before SDL_Init: no window, no audio device. */
    SDL_SetHint(SDL_HINT_VIDEODRIVER, "dummy");
    SDL_SetHint(SDL_HINT_AUDIODRIVER, "dummy");
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");   /* no focused window to give them */
    /* Only the virtual pads: the controllers plugged into the machine running
     * the tests stay out (Windows backends, HIDAPI). */
    SDL_SetHint("SDL_XINPUT_ENABLED", "0");
    SDL_SetHint("SDL_DIRECTINPUT_ENABLED", "0");
    SDL_SetHint("SDL_JOYSTICK_RAWINPUT", "0");
    SDL_SetHint("SDL_JOYSTICK_WGI", "0");
    SDL_SetHint("SDL_JOYSTICK_HIDAPI", "0");
    if (SDL_Init(SDL_INIT_EVENTS | SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) != 0) return -1;
    return 0;
}

int nullPlatformInit(void)
{
    if (nullPlatformInitSdl() != 0) return -1;
    defaultsApply();   /* main(): defaults, then configLoad (no ini here) */
    inputInit();   /* returns the connected-pad mask, not a status */
    return 0;
}

void nullPlatformShutdown(void)
{
    inputDestroy();
    SDL_Quit();
}

/* The test holds its own handle of each virtual pad: SDL only accepts state
 * writes on an opened joystick. */
#define NULL_PADS 8
static SDL_Joystick *s_pad[NULL_PADS];

int nullPadAttach(void)
{
    const int dev = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER,
                                              SDL_CONTROLLER_AXIS_MAX, SDL_CONTROLLER_BUTTON_MAX, 0);
    if (dev < 0) return -1;
    SDL_Joystick *j = SDL_JoystickOpen(dev);
    if (!j) return -1;
    for (int i = 0; i < NULL_PADS; i++) {
        if (!s_pad[i]) { s_pad[i] = j; break; }
    }
    SDL_PumpEvents();
    return SDL_JoystickInstanceID(j);
}

static SDL_Joystick *openById(int id)
{
    for (int i = 0; i < NULL_PADS; i++) {
        if (s_pad[i] && SDL_JoystickInstanceID(s_pad[i]) == id) return s_pad[i];
    }
    return NULL;
}

void nullPadDetach(int id)
{
    for (int i = 0; i < NULL_PADS; i++) {
        if (s_pad[i] && SDL_JoystickInstanceID(s_pad[i]) == id) { SDL_JoystickClose(s_pad[i]); s_pad[i] = NULL; }
    }
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_JoystickGetDeviceInstanceID(i) == id) { SDL_JoystickDetachVirtual(i); break; }
    }
}

void nullPadSetButton(int id, SDL_GameControllerButton b, int down)
{
    SDL_Joystick *j = openById(id);
    if (j) SDL_JoystickSetVirtualButton(j, (int)b, down ? 1 : 0);
}

void nullPadSetAxis(int id, SDL_GameControllerAxis a, int value)
{
    SDL_Joystick *j = openById(id);
    if (j) SDL_JoystickSetVirtualAxis(j, (int)a, (Sint16)value);
}

void nullPadClear(int id)
{
    for (int b = 0; b < SDL_CONTROLLER_BUTTON_MAX; b++) nullPadSetButton(id, (SDL_GameControllerButton)b, 0);
    for (int a = 0; a < SDL_CONTROLLER_AXIS_MAX; a++) nullPadSetAxis(id, (SDL_GameControllerAxis)a, 0);
}
