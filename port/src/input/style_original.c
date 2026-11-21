/*
 * The Original control style (Input.ControlScheme 1), as a pure strategy: a STRICT
 * virtual N64 controller (D643).
 *
 *   - Each N64 button is bound to up to two pad sources (InputSettings.n64Bind,
 *     resolved by the adapter, with its menu lockout guards); a press gives exactly
 *     that bit in every context (stage, menu, watch, tank). No port action is
 *     injected (no dedicated use / reload, crouch, gadget cycle, weapon cycle), no
 *     southpaw swap: the game's own code decides what A / B / Z ... mean.
 *   - The right stick is the digital C buttons (fixed, in addition to any C source
 *     bound); the left stick is the analog stick, with the accurate N64 response
 *     when n64StickFeel is set, else the usual deadzone + rescale. Never swapped,
 *     never inverted (the game's own "Look up/down" does that), and the stick roles
 *     are the game's own style's business: this style never switches to SOLITARE
 *     (cfg.naturalPitch is ignored), as the code it replaces did.
 *   - An open weapon wheel still owns the right stick.
 *
 * Output: commands.n64Raw / n64Buttons (the whole pad contribution) and
 * moveX / moveY (INMODE_MENU in a front-end screen, INMODE_HONEY otherwise).
 */
#include <string.h>
#include "style_original.h"
#include "n64bridge.h"

#define N64_STICK_DZ 2500   /* ~7.6 % of the travel: an assumption of typical dead-centre jitter */

static const unsigned kBit[INNB_COUNT] = {
    [INNB_A] = GE_CONT_A, [INNB_B] = GE_CONT_B, [INNB_Z] = GE_CONT_G, [INNB_R] = GE_CONT_R,
    [INNB_L] = GE_CONT_L, [INNB_CU] = GE_CONT_E, [INNB_CD] = GE_CONT_D, [INNB_CL] = GE_CONT_C,
    [INNB_CR] = GE_CONT_F, [INNB_START] = GE_CONT_START, [INNB_DU] = GE_CONT_UP,
    [INNB_DD] = GE_CONT_DOWN, [INNB_DL] = GE_CONT_LEFT, [INNB_DR] = GE_CONT_RIGHT,
};

unsigned inputN64ButtonBit(int n) { return (n >= 0 && n < INNB_COUNT) ? kBit[n] : 0; }

int inputN64StickAxis(int v)
{
    if (v > -N64_STICK_DZ && v < N64_STICK_DZ) return 0;
    int out = (int)((long)v * INPUT_STICK_MAX / 32767);
    if (out >  INPUT_STICK_MAX) out =  INPUT_STICK_MAX;
    if (out < -INPUT_STICK_MAX) out = -INPUT_STICK_MAX;
    return out;
}

static int stickAxis(const InputSnapshot *in, int v, int dz)
{
    return in->cfg.n64StickFeel ? inputN64StickAxis(v) : inputScaleAxis(v, dz);
}

static void originalMap(const InputSnapshot *in, InputStrategyState *st, InputCommands *o)
{
    (void)st;
    memset(o, 0, sizeof *o);
    const InputSettings *c = &in->cfg;
    uint32_t b = in->buttons;
    if (inputTriggerDown(in, 1)) b |= INB_LT;
    if (inputTriggerDown(in, 0)) b |= INB_RT;

    unsigned n64 = 0;
    for (int n = 0; n < INNB_COUNT; n++)
        if (b & c->n64Bind[n]) n64 |= kBit[n];

    int rx = in->rx, ry = in->ry;
    if (in->ctx.wheelOpen) rx = ry = 0;
    o->moveX = stickAxis(in, in->lx, c->deadzoneL);
    o->moveY = -stickAxis(in, in->ly, c->deadzoneL);   /* SDL up = negative -> N64 up = positive */
    if (in->ctx.menu) {
        o->mode = INMODE_MENU;
    } else {
        o->mode = INMODE_HONEY;
        o->moveActive = (o->moveX || o->moveY);
        if (rx >  INPUT_RSTICK_THRESHOLD) n64 |= GE_CONT_F;
        if (rx < -INPUT_RSTICK_THRESHOLD) n64 |= GE_CONT_C;
        if (ry >  INPUT_RSTICK_THRESHOLD) n64 |= GE_CONT_D;
        if (ry < -INPUT_RSTICK_THRESHOLD) n64 |= GE_CONT_E;
    }
    o->n64Raw = 1;
    o->n64Buttons = n64;
}

const InputStrategy g_inputStyleOriginal = { "original", originalMap };
