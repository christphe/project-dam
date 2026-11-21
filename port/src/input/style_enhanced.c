/*
 * The Enhanced control styles (Input.ControlScheme 0, Input.PadPreset 0..4), as a
 * pure strategy: 1.1 Jinx, 1.2 Christmas, 1.3 Frost, Custom, 1.4 Elektra.
 *
 * The buttons follow a per-seat bind table (InputSettings.bind: INB_* sources per
 * action, resolved by the adapter from the preset's defaults or the Custom strings;
 * Start is never left unbound). The rules, from padMapPure:
 *   - a TRIGGER source bound to fire / aim acts in any state; a digital source
 *     (LB aim) only in a playable stage or in the tank, and after the southpaw swap;
 *   - playable stage: crouch while held, use / reload / next / previous weapon and
 *     the gadget cycle on the press edge;
 *   - tank: next / previous weapon both give the native A (the tank's weapon);
 *   - front end / watch: the fixed accept (A, X) and cancel (B, Y), the menu lockout guard;
 *   - Start, D-pad: always.
 * Stick roles: the shared ones (input_common.c), after the swap of 1.3 / 1.4.
 * No aim assists (those are Modern's).
 */
#include <string.h>
#include "style_enhanced.h"

int inputEnhancedSolitare(int preset, int naturalPitch)
{
    if (preset == INPRESET_CHRISTMAS || preset == INPRESET_FROST) return 0;
    if (preset == INPRESET_ELEKTRA) return 1;
    return naturalPitch != 0;
}

int inputEnhancedSwap(int preset)
{
    return preset == INPRESET_FROST || preset == INPRESET_ELEKTRA;
}

static void enhancedMap(const InputSnapshot *in, InputStrategyState *st, InputCommands *o)
{
    memset(o, 0, sizeof *o);
    const InputSettings *c = &in->cfg;
    const uint32_t trig = INB_LT | INB_RT;
    uint32_t b = in->buttons;
    if (inputTriggerDown(in, 1)) b |= INB_LT;   /* the trigger point, or a latched tap */
    if (inputTriggerDown(in, 0)) b |= INB_RT;

    uint32_t held = 0;
    for (int a = 0; a < INACT_COUNT; a++)
        if (b & c->bind[a]) held |= INACT_BIT(a);
    const uint32_t edge = held & ~in->prevActions;

    /* the sticks; 1.3 / 1.4 swap them, never in a menu */
    InputSnapshot s = *in;
    if (c->stickSwap && !in->ctx.menu) {
        s.lx = in->rx; s.ly = in->ry; s.rx = in->lx; s.ry = in->ly;
    }
    inputMapSticks(&s, st, o);

    if (b & c->bind[INACT_FIRE] & trig) o->fire = 1;
    if (b & c->bind[INACT_AIM] & trig)  o->aim = 1;
    if (in->ctx.facePlayable) {
        o->crouch = (held & INACT_BIT(INACT_CROUCH)) != 0;
        o->useEdge = (edge & INACT_BIT(INACT_USE)) != 0;
        o->reloadEdge = (edge & INACT_BIT(INACT_RELOAD)) != 0;
        o->gadgetEdge = (edge & INACT_BIT(INACT_GADGET)) != 0;
        o->nextWeapon = (edge & INACT_BIT(INACT_NEXTWPN)) != 0;
        o->prevWeapon = (edge & INACT_BIT(INACT_PREVWPN)) != 0;
        o->aimDigital = (b & c->bind[INACT_AIM] & ~trig) != 0;
        o->fireDigital = (b & c->bind[INACT_FIRE] & ~trig) != 0;
    } else if (in->ctx.tank) {
        o->nextWeapon = (edge & (INACT_BIT(INACT_NEXTWPN) | INACT_BIT(INACT_PREVWPN))) != 0;
        o->aimDigital = (b & c->bind[INACT_AIM] & ~trig) != 0;
        o->fireDigital = (b & c->bind[INACT_FIRE] & ~trig) != 0;
    } else {
        o->menuAccept = (b & (INB_A | INB_X)) != 0;
        o->menuCancel = (b & (INB_B | INB_Y)) != 0;
    }
    o->start = (held & INACT_BIT(INACT_START)) != 0;
    o->dpadUp = (b & INB_DUP) != 0;
    o->dpadDown = (b & INB_DDOWN) != 0;
    o->dpadLeft = (b & INB_DLEFT) != 0;
    o->dpadRight = (b & INB_DRIGHT) != 0;
    o->actionsHeld = held;
}

const InputStrategy g_inputStyleEnhanced = { "enhanced", enhancedMap };
