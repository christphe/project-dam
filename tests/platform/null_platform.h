/*
 * The platform the tests run on: no window, no audio device, no real
 * gamepad. Lives in the test tree only; the shipping build never sees it.
 *
 *  - nullPlatformInit(): SDL's dummy video/audio drivers, SDL started with the
 *    subsystems the input module needs, the config defaults applied
 *    (defaultsApply, as main() does before configLoad; no ini is read), then
 *    inputInit() (its connected-pad mask is
 *    read with inputConnectedMask()). Returns 0 on success.
 *  - A fake joystick feed: a virtual SDL gamepad whose state a test sets, so
 *    the input module reads it like a real one (SDL_GameController API).
 *  - A null graphics backend (null_gfx.c): fast3d window manager and
 *    rendering API that do nothing, for tests that drive the renderer glue.
 */
#ifndef NULL_PLATFORM_H
#define NULL_PLATFORM_H

#include <SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Start the null platform; the core's config and input module are initialised. */
int  nullPlatformInit(void);
/* Only SDL + the dummy drivers (no core init), for tests that need no input module. */
int  nullPlatformInitSdl(void);
void nullPlatformShutdown(void);

/* Fake joystick feed. Returns the SDL joystick instance id (>= 0) or -1. The
 * pad is a standard gamepad (SDL_GameController buttons/axes). */
int  nullPadAttach(void);
void nullPadDetach(int id);
void nullPadSetButton(int id, SDL_GameControllerButton b, int down);
void nullPadSetAxis(int id, SDL_GameControllerAxis a, int value);   /* -32768..32767; triggers 0..32767 */
void nullPadClear(int id);   /* all buttons up, all axes 0 */

/* Null fast3d backend (null_gfx.c): both structs have every callback set. */
struct GfxWindowManagerAPI;
struct GfxRenderingAPI;
extern struct GfxWindowManagerAPI gNullWindowApi;
extern struct GfxRenderingAPI gNullRenderingApi;
/* Number of calls the null rendering API received (a test can assert nothing drew, or that something did). */
extern unsigned gNullGfxCalls;

#ifdef __cplusplus
}
#endif
#endif
