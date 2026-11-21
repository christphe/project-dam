/* Step 2: the core starts without a window: config + input module on the null platform,
 * a fake gamepad read back through SDL, the null graphics backend callable. */
#include "ge_test.h"
#include "null_platform.h"
#include "input.h"
#include "gfx_window_manager_api.h"
#include "gfx_rendering_api.h"

int main(void)
{
    GE_TEST_BEGIN("null platform");

    GE_CHECK_EQ_INT(nullPlatformInit(), 0);
    GE_CHECK(SDL_GetCurrentVideoDriver() == NULL || strcmp(SDL_GetCurrentVideoDriver(), "dummy") == 0);
    GE_CHECK_EQ_INT(inputPadCount(), 0);   /* no pad plugged yet */
    GE_CHECK_EQ_INT(inputConnectedMask(), 1);   /* keyboard + mouse only */

    const int pad = nullPadAttach();
    GE_CHECK(pad >= 0);
    if (pad >= 0) {
        SDL_Joystick *j = SDL_JoystickFromInstanceID(pad);
        GE_CHECK(j != NULL);
        nullPadSetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX, 12345);
        nullPadSetButton(pad, SDL_CONTROLLER_BUTTON_A, 1);
        SDL_JoystickUpdate();
        if (j) {
            GE_CHECK_EQ_INT(SDL_JoystickGetAxis(j, SDL_CONTROLLER_AXIS_LEFTX), 12345);
            GE_CHECK_EQ_INT(SDL_JoystickGetButton(j, SDL_CONTROLLER_BUTTON_A), 1);
        }
        nullPadClear(pad);
        SDL_JoystickUpdate();
        if (j) GE_CHECK_EQ_INT(SDL_JoystickGetButton(j, SDL_CONTROLLER_BUTTON_A), 0);
        nullPadDetach(pad);
    }

    /* The null graphics backend: every callback is set and harmless. */
    GE_CHECK(gNullWindowApi.init && gNullWindowApi.swap_buffers_end && gNullWindowApi.get_dimensions);
    GE_CHECK(gNullRenderingApi.init && gNullRenderingApi.draw_triangles && gNullRenderingApi.mesh_supported);
    gNullWindowApi.init(NULL);
    gNullRenderingApi.init();
    gNullRenderingApi.start_frame();
    gNullRenderingApi.end_frame();
    GE_CHECK_STR(gNullRenderingApi.get_name(), "null");
    GE_CHECK_EQ_INT(gNullGfxCalls, 3);

    nullPlatformShutdown();
    return GE_TEST_END();
}
