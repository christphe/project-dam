/*
 * The Modern control style (Input.ControlScheme 2), as a pure strategy.
 *
 *   RT fire, LT aim, R3 crouch, Start; the face buttons go by POSITION, so a
 *   Nintendo pad (A/B and X/Y letters swapped) keeps the same hands:
 *     bottom = use, left = reload, top = next weapon, right = free.
 *   LB / RB open the weapon / gadget wheels (port/src/wheel.c reads them; while
 *   one is open the right stick is the wheel's, so look is neutral).
 *   While aiming: D-pad up / down zoom a scoped weapon, the left stick strafes
 *   slowly.
 *   Stick roles: SOLITARE (left stick = digital moves, right stick = look) or,
 *   with Input.NaturalPitch off, HONEY (left stick analog, right stick digital).
 *   In a front-end screen the left stick navigates and A/X accept, B/Y cancel.
 *
 * The rules are the ones inputComputePadSlot used to carry inline (input.c,
 * "gamepad" block, padMapPure and the "Modern: while aiming" block).
 */
#include <string.h>
#include "style_modern.h"

#define SLOW_STRAFE_THRESHOLD 12000

static void modernMap(const InputSnapshot *in, InputStrategyState *st, InputCommands *o)
{
    memset(o, 0, sizeof *o);
    const uint32_t b = in->buttons;
    const int nin = (in->family == INFAM_NINTENDO);
    /* By position: Xbox A/X/Y = bottom/left/top; a Nintendo pad prints B/Y/X there. */
    const uint32_t useB = nin ? INB_B : INB_A;
    const uint32_t reloadB = nin ? INB_Y : INB_X;
    const uint32_t nextB = nin ? INB_X : INB_Y;
    const int lt = inputTriggerDown(in, 1), rt = inputTriggerDown(in, 0);

    uint32_t held = 0;
    if (rt) held |= INACT_BIT(INACT_FIRE);
    if (lt) held |= INACT_BIT(INACT_AIM);
    if (b & useB) held |= INACT_BIT(INACT_USE);
    if (b & reloadB) held |= INACT_BIT(INACT_RELOAD);
    if (b & INB_RS) held |= INACT_BIT(INACT_CROUCH);
    if (b & nextB) held |= INACT_BIT(INACT_NEXTWPN);
    if (b & INB_START) held |= INACT_BIT(INACT_START);
    const uint32_t edge = held & ~in->prevActions;

    if (in->ctx.padDrives) {
        inputMapSticks(in, st, o);
        o->fire = rt;
        o->aim = lt;
        if (in->ctx.facePlayable) {
            o->crouch = (held & INACT_BIT(INACT_CROUCH)) != 0;
            o->useEdge = (edge & INACT_BIT(INACT_USE)) != 0;
            o->reloadEdge = (edge & INACT_BIT(INACT_RELOAD)) != 0;
            o->nextWeapon = (edge & INACT_BIT(INACT_NEXTWPN)) != 0;
        } else if (in->ctx.tank) {
            o->nextWeapon = (edge & INACT_BIT(INACT_NEXTWPN)) != 0;
        } else {
            /* front end / watch: the fixed native accept / cancel (menu lockout guard) */
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

    /* While aiming in a stage: D-pad up / down zoom a scoped weapon and the left
     * stick strafes slowly (the native aim-mode C-up / C-down / C-left / C-right). */
    if (in->ctx.inStage && in->ctx.aiming && !in->ctx.dead) {
        if (in->ctx.scopedWeapon) {
            o->zoomIn = (b & INB_DUP) != 0;
            o->zoomOut = (b & INB_DDOWN) != 0;
        }
        if (in->lx < -SLOW_STRAFE_THRESHOLD) o->slowStrafeL = 1;
        else if (in->lx > SLOW_STRAFE_THRESHOLD) o->slowStrafeR = 1;
    }
}

const InputStrategy g_inputStyleModern = { "modern", modernMap };
