/* ge_render_poll_input: mouse and the first game controller from SDL (ge_render.h). */
#include <SDL.h>

#include "ge_internal.h"

static SDL_GameController *s_pad = nullptr;

static void open_pad(void) {
    if (s_pad && SDL_GameControllerGetAttached(s_pad)) return;
    s_pad = nullptr;
    if (!SDL_WasInit(SDL_INIT_GAMECONTROLLER)) SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER);
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i) && (s_pad = SDL_GameControllerOpen(i)) != nullptr) break;
    }
}

extern "C" void ge_render_poll_input(GeRenderInput *out) {
    *out = GeRenderInput();
    const Uint32 b = SDL_GetMouseState(&out->mouse_x, &out->mouse_y);
    out->mouse_buttons = (b & SDL_BUTTON_LMASK ? 1 : 0) | (b & SDL_BUTTON_RMASK ? 2 : 0) | (b & SDL_BUTTON_MMASK ? 4 : 0);
    open_pad();
    if (!s_pad) return;
    SDL_GameControllerUpdate();
    static const struct { SDL_GameControllerButton sdl; uint32_t bit; } map[] = {
        { SDL_CONTROLLER_BUTTON_A, GE_PAD_A }, { SDL_CONTROLLER_BUTTON_B, GE_PAD_B }, { SDL_CONTROLLER_BUTTON_X, GE_PAD_X },
        { SDL_CONTROLLER_BUTTON_Y, GE_PAD_Y }, { SDL_CONTROLLER_BUTTON_BACK, GE_PAD_BACK }, { SDL_CONTROLLER_BUTTON_START, GE_PAD_START },
        { SDL_CONTROLLER_BUTTON_LEFTSHOULDER, GE_PAD_LSHOULDER }, { SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, GE_PAD_RSHOULDER },
        { SDL_CONTROLLER_BUTTON_DPAD_UP, GE_PAD_UP }, { SDL_CONTROLLER_BUTTON_DPAD_DOWN, GE_PAD_DOWN },
        { SDL_CONTROLLER_BUTTON_DPAD_LEFT, GE_PAD_LEFT }, { SDL_CONTROLLER_BUTTON_DPAD_RIGHT, GE_PAD_RIGHT },
    };
    for (const auto &m : map) if (SDL_GameControllerGetButton(s_pad, m.sdl)) out->pad_buttons |= m.bit;
    out->pad_lx = SDL_GameControllerGetAxis(s_pad, SDL_CONTROLLER_AXIS_LEFTX) / 32767.0f;
    out->pad_ly = SDL_GameControllerGetAxis(s_pad, SDL_CONTROLLER_AXIS_LEFTY) / 32767.0f;
    out->pad_rx = SDL_GameControllerGetAxis(s_pad, SDL_CONTROLLER_AXIS_RIGHTX) / 32767.0f;
    out->pad_ry = SDL_GameControllerGetAxis(s_pad, SDL_CONTROLLER_AXIS_RIGHTY) / 32767.0f;
}
