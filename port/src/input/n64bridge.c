#include "n64bridge.h"

unsigned n64BridgeButtons(unsigned button, const InputCommands *c, const N64BridgeOpts *o)
{
    if (c->n64Raw) return button | c->n64Buttons;   /* Original: the pad is the N64 pad, nothing else to add */

    if ((c->moveFwd && !c->direct) || c->pitchUp)    button |= GE_CONT_E;   /* direct: the game takes walk / strafe (GameDirect) */
    if ((c->moveBack && !c->direct) || c->pitchDown) button |= GE_CONT_D;
    if (c->strafeL && !c->direct) button |= GE_CONT_C;
    if (c->strafeR && !c->direct) button |= GE_CONT_F;

    if (c->fire) button |= GE_CONT_G;
    if (c->aim)  button |= GE_CONT_R;
    if (o->southpaw) {   /* swap fire / grenade after the raw trigger bits */
        unsigned t = button;
        button = (t & ~(GE_CONT_G | GE_CONT_R))
               | ((t & GE_CONT_G) ? GE_CONT_R : 0)
               | ((t & GE_CONT_R) ? GE_CONT_G : 0);
    }

    if (c->fireDigital) button |= GE_CONT_G;   /* after the swap: only the trigger bits swap */
    if (c->aimDigital)  button |= GE_CONT_R;

    if (o->slotNative && (c->useEdge || c->reloadEdge)) button |= GE_CONT_B;
    if (c->nextWeapon) button |= GE_CONT_A;
    if (c->prevWeapon) button |= GE_CONT_A | GE_CONT_G;   /* same backward-cycle idiom as the mouse wheel */
    if (c->menuAccept) button |= GE_CONT_A;
    if (c->menuCancel) button |= GE_CONT_B;
    if (c->start) button |= GE_CONT_START;
    if (c->dpadUp)    button |= GE_CONT_UP;
    if (c->dpadDown)  button |= GE_CONT_DOWN;
    if (c->dpadLeft)  button |= GE_CONT_LEFT;
    if (c->dpadRight) button |= GE_CONT_RIGHT;
    return button;
}

unsigned n64BridgeAssist(unsigned button, const InputCommands *c)
{
    if (c->zoomIn)  button |= GE_CONT_E;
    if (c->zoomOut) button |= GE_CONT_D;
    if (c->slowStrafeL) button |= GE_CONT_C;
    if (c->slowStrafeR) button |= GE_CONT_F;
    return button;
}
