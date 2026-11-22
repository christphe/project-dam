#ifndef SDL_COMPAT_H
#define SDL_COMPAT_H
/* project-dam: stand-ins for the SDL2 names newer than the oldest SDL the port
 * builds against. PC and Switch build against a recent SDL and this header is
 * then empty; the arm64 handheld build compiles and links against SDL 2.0.12
 * (the stock Anbernic H700 firmware's, Dockerfile.arm64), so a call newer than
 * that fails to link instead of crashing on a device whose SDL lacks it.
 * Include it after <SDL.h> wherever one of these names is used. */
#include <SDL.h>

/* Hints are plain strings: an SDL that does not know one ignores it. */
#ifndef SDL_HINT_JOYSTICK_HIDAPI_PS5_RUMBLE
#define SDL_HINT_JOYSTICK_HIDAPI_PS5_RUMBLE "SDL_JOYSTICK_HIDAPI_PS5_RUMBLE"   /* 2.0.14 */
#endif
#ifndef SDL_HINT_MOUSE_RELATIVE_SYSTEM_SCALE
#define SDL_HINT_MOUSE_RELATIVE_SYSTEM_SCALE "SDL_MOUSE_RELATIVE_SYSTEM_SCALE"   /* 2.0.14 */
#endif

#if !SDL_VERSION_ATLEAST(2, 0, 14)
/* The value 2.0.14 gave it; an older SDL never reports it. */
#define SDL_CONTROLLER_TYPE_VIRTUAL ((SDL_GameControllerType)6)
/* No browser on the devices that build this: report failure, like an SDL
 * without a URL handler does. */
static inline int SDL_OpenURL(const char *url)
{
    (void)url;
    return SDL_SetError("SDL_OpenURL needs SDL 2.0.14");
}
#endif

#if !SDL_VERSION_ATLEAST(2, 0, 18)
/* The 32-bit millisecond counter wraps after 49.7 days; enough for a session. */
static inline Uint64 SDL_GetTicks64(void) { return SDL_GetTicks(); }
#endif

#endif /* SDL_COMPAT_H */
