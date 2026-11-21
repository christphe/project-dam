/* Helpers shared by the input strategies (pure: no SDL, no game state). */
#include <math.h>
#include <string.h>
#include "input_types.h"

/* D324 class: in the game build -Iinclude resolves <math.h> to the N64 stub, which does not declare lround. */
long lround(double x);

void inputStrategyStateReset(InputStrategyState *st)
{
    memset(st, 0, sizeof *st);
}

/* Deadzone then rescale of one raw SDL axis to +-INPUT_STICK_MAX. */
int inputScaleAxis(int v, int dz)
{
    if (dz < 0) dz = 0;
    if (dz > 30000) dz = 30000;
    if (v > -dz && v < dz) {
        return 0;
    }
    if (v < 0) v += dz; else v -= dz;
    int out = (int)((long)v * INPUT_STICK_MAX / (32767 - dz));
    if (out >  INPUT_STICK_MAX) out =  INPUT_STICK_MAX;
    if (out < -INPUT_STICK_MAX) out = -INPUT_STICK_MAX;
    return out;
}

int inputTriggerDown(const InputSnapshot *in, int left)
{
    const int pt = in->cfg.triggerPct * 327;   /* % of the 0..32767 travel */
    return left ? ((in->buttons & INB_LT) != 0 || in->triggerL > pt)
                : ((in->buttons & INB_RT) != 0 || in->triggerR > pt);
}

static int clampStick(int v)
{
    if (v >  INPUT_STICK_MAX) return  INPUT_STICK_MAX;
    if (v < -INPUT_STICK_MAX) return -INPUT_STICK_MAX;
    return v;
}

/* The stick roles of the port-driven styles (Modern, Enhanced): the menu stick,
 * SOLITARE (left = digital moves, right = look) or HONEY (left analog, right
 * digital), chosen by ctx.menu and cfg.naturalPitch. */
void inputMapSticks(const InputSnapshot *in, InputStrategyState *st, InputCommands *o)
{
    const InputSettings *c = &in->cfg;
    int rx = in->rx, ry = in->ry;
    if (in->ctx.wheelOpen) rx = ry = 0;   /* the open wheel has the aim */

    if (in->ctx.menu) {
        o->mode = INMODE_MENU;
        o->moveX = inputScaleAxis(in->lx, c->deadzoneL);
        o->moveY = -inputScaleAxis(in->ly, c->deadzoneL);   /* SDL up = negative -> N64 up = positive */
    } else if (c->naturalPitch) {
        o->mode = INMODE_SOLITARE;
        if (in->ly < -INPUT_RSTICK_THRESHOLD) o->moveFwd = 1;
        if (in->ly >  INPUT_RSTICK_THRESHOLD) o->moveBack = 1;
        if (in->lx < -INPUT_RSTICK_THRESHOLD) o->strafeL = 1;
        if (in->lx >  INPUT_RSTICK_THRESHOLD) o->strafeR = 1;
        o->strafe = inputScaleAxis(in->lx, c->deadzoneL);
        o->walk = -inputScaleAxis(in->ly, c->deadzoneL);   /* SDL up = negative -> N64 forward = positive */

        int rxs = inputScaleAxis(rx, c->deadzoneR);
        int rys = -inputScaleAxis(ry, c->deadzoneR);
        if (c->lookSensX != 100 && rxs) rxs = rxs * c->lookSensX / 100;
        if (c->lookSensY != 100 && rys) rys = rys * c->lookSensY / 100;
        if (in->ctx.aiming && in->aimScale) {   /* aim sensitivity, zoom and assist: the game's own scaling */
            rxs = in->aimScale(in->aimScaleUser, rxs, c->aimLookSens);
            rys = in->aimScale(in->aimScaleUser, rys, c->aimLookSens);
        }
        rxs = clampStick(rxs);
        rys = clampStick(rys);
        o->lookRawActive = (rxs || rys);
        if (c->lookSmooth > 0) {
            double a = c->lookSmooth * 0.09;   /* 10 -> 0.9 (heaviest); 0 = off */
            st->lookSmoothX = st->lookSmoothX * a + (double)rxs * (1.0 - a);
            st->lookSmoothY = st->lookSmoothY * a + (double)rys * (1.0 - a);
            rxs = (int)lround(st->lookSmoothX);
            rys = (int)lround(st->lookSmoothY);
        }
        if (c->lookInvertY) rys = -rys;
        o->lookX = rxs;
        o->lookY = rys;
    } else {
        o->mode = INMODE_HONEY;
        int px = inputScaleAxis(in->lx, c->deadzoneL);
        int py = -inputScaleAxis(in->ly, c->deadzoneL);
        if (c->lookInvertY) py = -py;
        o->moveX = px;
        o->moveY = py;
        o->moveActive = (px || py);
        if (rx >  INPUT_RSTICK_THRESHOLD) o->strafeR = 1;
        if (rx < -INPUT_RSTICK_THRESHOLD) o->strafeL = 1;
        if (ry >  INPUT_RSTICK_THRESHOLD) o->pitchDown = 1;
        if (ry < -INPUT_RSTICK_THRESHOLD) o->pitchUp = 1;
    }
}


/* The game's own deadzone of a stick axis (bondview2.c: controlStick*Safe). */
static int safeAxis(int v)
{
    return v < -5 ? v + 5 : (v > 5 ? v - 5 : 0);
}

int inputDirectMoveDecide(const InputContext *ctx, const InputCommands *cmds, int *walk, int *strafe)
{
    if (cmds->mode != INMODE_SOLITARE) return 0;   /* menu, HONEY (the left stick is already the analog N64 stick), Original */
    if (!ctx->padDrives || !ctx->facePlayable) return 0;   /* keyboard seat; menu / watch / pause / locked / dead / tank are not playable */
    if (ctx->menu || ctx->tank || ctx->dead || ctx->aiming) return 0;   /* aiming keeps the C-bit moves */
    *walk = safeAxis(cmds->walk);
    *strafe = safeAxis(cmds->strafe);
    return 1;
}

static int clampDirect(int v)
{
    return v > INPUT_DIRECT_MAX ? INPUT_DIRECT_MAX : (v < -INPUT_DIRECT_MAX ? -INPUT_DIRECT_MAX : v);
}

void inputDirectMoveAddKeys(int fwd, int back, int left, int right, int *walk, int *strafe)
{
    *walk = clampDirect(*walk + (fwd ? INPUT_DIRECT_MAX : 0) - (back ? INPUT_DIRECT_MAX : 0));
    *strafe = clampDirect(*strafe + (right ? INPUT_DIRECT_MAX : 0) - (left ? INPUT_DIRECT_MAX : 0));
}
