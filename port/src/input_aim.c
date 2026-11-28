/*
 * Mouse / pad aim models moved out of input.c verbatim (#78): the GEPD-style crosshair aim, the PD-style turn, the hip-fire
 * direct look, the weapon sway and the centred pad look. The tunables stay registered in input.c; the shared state is
 * external with a g_input / input prefix (input/input_internal.h).
 */
#include "port_math.h"   /* real system math decls; see header for why */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#include "platform.h"
#include "system.h"
#include "config.h"
#include "wheel.h"
#include "assign.h"
#include "input.h"
#include "envflag.h"
#include "optionsoverlay.h"
#include "video.h"
/* D194 absolute aim: read-only access to the live camera (struct player).
 * Game header pulled in through the same shim path every other compiled game
 * file uses; we only READ vv_theta/vv_verta/speedtheta/speedverta/aspect. */
#include "player.h"
#include "language.h"  /* j_text_trigger (control-type mirror) */
#include "gun.h"  /* native weapon flags + dedicated reload entry points */
#include "bondinv.h" /* inventory list for the Xbox-style gadget cycle */

/* D194 spazz diagnosis: game ticks batched into the current poll (lv.h).
 * Read-only; declared locally to avoid pulling lv.h's wider dependency set. */
extern s32 g_ClockTimer;
extern s32 lvlGetControlsLockedFlag(void); /* same gate as bondviewProcessInput */
extern int gameScriptedCameraActive(void);
extern bool bond_interact_object(void); /* chrprop.c: use target, no reload fallback */
extern s32 g_PlayerIsInTank;            /* bondview.c: tank state (D407) */
extern s32 g_BondCanEnterTank;          /* bondview.c: tank board gate (D407) */
extern s32 g_EnterTankAudioState;      /* bondview2.c: tank entry/running state (D407) */
/* gun.c: native weapon-switch entry points (not exposed in gun.h). */
extern ITEM_IDS get_next_weapon_in_cycle_for_hand(GUNHAND hand, s32 direction);
extern void gunRequestHandWeaponChange(enum GUNHAND hand, s32 nextWeapon, s32 cycleDirection);

/* The control styles as pure strategies (snapshot -> commands, no SDL, no game
 * state) and the commands -> N64 pad bridge; also home of the GE_CONT_* bits.
 * This file is their adapter. */
#include "input/input_types.h"
#include "input/n64bridge.h"
#include "input/style_modern.h"
#include "input/input_internal.h"

struct aimSlotState g_inputAimSt[4];
/* Auto-aim is for a pad: while the mouse is the device that aims (player 1), the game gets it off (GE_AUTOAIM_ON). */
extern s32 get_cur_playernum(void);
int portAimIsMouse(void) { return g_inputAimDevMouse && get_cur_playernum() == 0; }

/* D422: FOV of the player that owns the polled slot. In a split-screen
 * stage the poll thread's viGetFovY() is whichever player the game thread
 * last set up (bondview2 viSetFovY(g_CurrentPlayer->zoominfovy)), so read the
 * slot player's own zoominfovy there. Solo: exactly viGetFovY(), as before. */
static f32 slotFovY(void)
{
    if (inputMpSplit() && g_inputSlot >= 0) {
        struct player *p = inputSlotPlayer();
        if (p != NULL && p->zoominfovy > 0.0f)
            return p->zoominfovy;
    }
    return viGetFovY();
}

/* D422: the aim state is per controller slot (seat). AIMS = the slot being polled;
 * off the poll (g_inputSlot == -1, game thread) it is slot 0. */
/* WI-1 (GEPD-INPUT-PLAN.md #89): hipfire direct camera write.
 *
 * Mirrors GEPD's hipfire branch (games/goldeneye.c): `camx += XPOS/10 *
 * (SENS/40) * (fov/basefov)` degrees this poll -- linear in px, FOV-scaled,
 * no stick curve in between. We fold GEPD's SENS/40 into our own
 * MouseTurnSpeed*MouseSensitivity product (both already 100% at defaults,
 * same "in line" convention D238 established for inputAimGepdCompute's GepdSens):
 * at defaults this reduces to exactly GEPD's px/10 baseline.
 *
 * dxPx/dyLook are this poll's dt-scaled px (same convention as
 * inputAimGepdCompute -- callers pre-multiply by lookDtScale).
 *
 * GEPD's safety gates (`camera==4||0 && menupage==11 && !dead && !watch &&
 * !pause`) matter here in a way they didn't for aim mode: aim mode requires
 * RMB held, so it naturally can't fire during a death/cutscene/pause state a
 * player would be holding RMB through; hipfire looks are always live, so a
 * frozen or scripted-camera state (POSEND/INTRO cutscenes, death cam, pause)
 * must be checked explicitly or this would fight the game's own camera
 * control during those states. `current_menu` already gates menupage==11
 * (checked by the caller's menuMode branch, same as aim mode); the rest are
 * read here directly -- read-only, no logic change.
 *
 * Returns 1 if it handled this poll (caller must not also run the legacy
 * stick path); 0 to fall back (disabled, no player, or a safety gate is
 * closed -- e.g. mid-death or mid-cutscene, matching GEPD's !dead/!watch).
 */
/* D428 (#117): the gun's turn-lag ("sway") is driven by player->speedtheta
 * (gunSetBondWeaponSway, bondview2.c), which the game derives from STICK
 * deflection. Direct mouse look writes vv_theta instead and leaves speedtheta
 * at ~0, so the gun stayed glued to the view. Accumulate the yaw degrees the
 * direct path applied (poll thread) and hand the game thread an equivalent
 * turn speed once per tick: the exact inverse of the game's own yaw
 * integration (see portGunSwayTheta). Hipfire only; display-side. */
/* D428 follow-up (#99): one accumulator per player. Fed on the poll thread
 * for the polled slot (player N owns slot N), drained on the game thread for
 * the current player. Solo = slot/player 0 only, as before. The game-side hook
 * (bondview2.c, D428 Rule-2 block) still drains player 0 only; see findings
 * D428 "split-screen note" for the one-line widening it would need. */
static SDL_atomic_t s_swayThetaMdeg[4];

void inputSwayFeedTheta(double deg)
{
    SDL_AtomicAdd(&s_swayThetaMdeg[(g_inputSlot > 0 && g_inputSlot < 4) ? g_inputSlot : 0],
                  (int)lround(deg * 1000.0));
}

float portGunSwayTheta(float dt)
{
    extern s32 get_cur_playernum(void);
    s32 pn = get_cur_playernum();
    int m = SDL_AtomicSet(&s_swayThetaMdeg[(pn > 0 && pn < 4) ? pn : 0], 0);
    if (dt <= 0.0f)
        return 0.0f;
    /* Exact inverse of the game's own yaw integration (bondview2.c:
     * vv_theta += speedtheta * dt * 3.5) and of its natural-turn speed, which is
     * set straight from the stick with no ramp: speedtheta = (stick/70)^2 * fov/60,
     * i.e. |speedtheta| <= fov/60 (~1.0). So a mouse turn of X degrees/tick gives
     * the same gun lag the stick turning at that speed did on the N64. (An earlier
     * version used the vertical axis' delta/(2*dt) clamped to 0.7: too weak at full
     * speed, too strong at slow speed.) */
    f32 fov = viGetFovY();
    double lim = (fov > 0.0f) ? (double)fov / 60.0 : 1.0;
    double v = (m / 1000.0) / (3.5 * (double)dt);
    if (v >  lim) v =  lim;
    if (v < -lim) v = -lim;
    if (m != 0 && configGetInputLog())
        sysLogPrintf(LOG_NOTE, "GE_INPUTLOG gunsway theta=%.1f deg speed=%.3f", m / 1000.0, v);
    return (float)v;
}

int inputHipDirectCompute(double dxPx, double dyLook)
{
    struct player *p = inputSlotPlayer();

    if (!g_inputMouseDirectLook || !g_inputMouseGrabbed || p == NULL)
        return 0;

    /* GEPD: !dead && !watch && !pause. bonddead/outside_watch_menu/
     * pause_state are the decomp-canonical equivalents (bondview.h). */
    if (p->bonddead || !p->outside_watch_menu || p->pause_state != 0)
        return 0;

    /* M-190: the "frozen or scripted-camera state (POSEND/INTRO cutscenes...)
     * must be checked explicitly" gate this function's own comment above has
     * always documented, but never actually implemented -- found live: the
     * mouse could still freely move the camera during the Dam intro flyover
     * and the abseil-jump cutscene, both CAMERAMODE_INTRO/POSEND, which are
     * supposed to be camera-locked. bondviewFrozenMoveBond (bondview2.c:7947)
     * already calls bondviewProcessInput(0,0,0,0) to correctly ignore stick
     * input during these modes, but that gate lives entirely inside the
     * stick-based dispatch (MoveBond vs bondviewFrozenMoveBond,
     * bondview2.c:8268) -- this function writes p->vv_theta/vv_verta
     * directly, bypassing that dispatch entirely. Reuses the same
     * scripted-camera test D243's clamps use (bondview2.c, gameScriptedCameraActive,
     * formerly d243mCutsceneActive -- renamed since this isn't D243-specific). */
    if (gameScriptedCameraActive())
        return 0;

    f32 fov = slotFovY();
    f32 scale = (fov > 0.0f) ? fov / GEPD_BASE_FOV : 1.0f;
    double sens = (g_inputMouseTurnSpeed / 100.0) * (g_inputMouseSensitivity / 100.0);

    /* dyLook already carries MouseInvertY + MouseYScale (applied by the
     * caller before dt-scaling, same as every other consumer of dyLook) --
     * do not re-apply either here. */
    g_inputHipThetaDegOut = dxPx * 0.1 * sens * scale;
    p->vv_theta += (f32) g_inputHipThetaDegOut;
    p->vv_verta -= (f32) (dyLook * 0.1 * sens * scale);
    if (p->vv_verta >  90.0f) p->vv_verta =  90.0f;
    if (p->vv_verta < -90.0f) p->vv_verta = -90.0f;

    if (configGetInputLog()) {
        sysLogPrintf(LOG_NOTE,
            "GE_INPUTLOG hipdirect d=(%.1f,%.1f) cam=(%.1f,%.1f)",
            dxPx, dyLook, (double)p->vv_theta, (double)p->vv_verta);
    }
    return 1;
}

/* D404 (resolved by implementation, 2026-09-28): the pad-side twin of
 * inputHipDirectCompute's CENTRED path -- the right stick drives the camera
 * directly and the crosshair stays pinned at centre, so the Aim-style
 * toggle is no longer inert on a pad (it used to be: the pad always
 * emitted deflection into the game's N64 crosshair integrator, i.e. it
 * was *always in N64 mode*). Same safety gates as inputHipDirectCompute
 * (dead / watch / pause / scripted camera); the g_inputMouseDirectLook/
 * g_inputMouseGrabbed gate is dropped -- it is meaningless for a pad. Input
 * units are stick deflection (STICK_MAX = full, the same unit the N64
 * path feeds the game's integrator).
 *
 * Gain = the N64 natural-turn curve VERBATIM (bondview2.c, canNaturalTurn
 * / canNaturalPitch branches): v = clamp(deflection/70, \u00b11); v =
 * sign(v)\u00b7v\u00b2; camera += v \u00b7 (fov/60) \u00b7 3.5 deg/poll. First pass used a
 * linear 2.0 deg/poll \u00b7 (fov/GEPD_BASE_FOV) gain, which the user measured
 * as a dramatic sensitivity loss in Centred mode (linear vs quadratic
 * curve, and the 60-vs-90 FOV base compounded it to ~2.6\u00d7 slower at full
 * stick). With the verbatim curve, full-stick Centred turns at exactly
 * the N64 path's top rate -- switching modes changes the crosshair
 * behaviour, not the feel. */
/* project-dam: weapon sway for "Centered + sway". Off aim, the game itself
 * swings the weapon with the turn: bondview2.c feeds speedtheta * 0.3 and
 * -speedverta * 0.1 into the crosshair and weapon integrators every tick
 * (sub_GAME_7F067F58), and the weapon settles at its own AimLockSpeed. In
 * Centred the port turns the camera itself (inputPadDirectCompute), speedtheta
 * stays 0 and the weapon never moves. This feeds a turn of the same size into
 * the weapon only (gun_azimuth_angle/_turning), against the turn, so the
 * weapon trails the view like a modern shooter's weapon lag; the crosshair
 * stays at the centre, and shots go to crosshair_angle (gunfire.c), so the
 * sway is visual. speedTheta/speedVerta are the camera's
 * turn rates after the natural-turn curve, as the game would hold them. */
void inputPadAimSway(struct player *p, double speedTheta, double speedVerta)
{
    p->gun_azimuth_angle   -= (f32)(speedTheta * 0.3);    /* trails the turn */
    p->gun_azimuth_turning -= (f32)(-speedVerta * 0.1);
}

#if defined(PLATFORM_SWITCH)
/* project-dam: the Centred stick response. padLookCurveRadial(): |m|^(Input.
 * PadLookCurve / 100) on the stick's magnitude m; the game's natural turn is
 * v^2 (200), a higher exponent gives small deflections finer control at the
 * same top speed. padTurnAccelGain(): modern shooters' edge acceleration -- the stick
 * held at full deflection for 0.2 s ramps the horizontal turn up over 0.4 s,
 * to 1.5x (low) or 2x (high), so wide turns stay fast while the curve keeps
 * aim fine. */
/* The curve on the stick's magnitude, not per axis: with v^3 on each axis a
 * full diagonal (0.7, 0.7) came out at 0.35 per axis, about half the speed of
 * a full horizontal or vertical push. Direction is kept; the magnitude is
 * capped at 1 so a full diagonal tops out like a full axis. */
static void padLookCurveRadial(double *x, double *y)
{
    double m = sqrt(*x * *x + *y * *y);
    if (m <= 0.0) {
        return;
    }
    if (m > 1.0) {
        *x /= m;
        *y /= m;
        m = 1.0;
    }
    const double s = pow(m, g_inputPadLookCurve / 100.0) / m;
    *x *= s;
    *y *= s;
}

static double padTurnAccelGain(struct player *p, double vraw)
{
    static struct { struct player *p; Uint32 since; } st[4];
    if (g_inputPadTurnAccel <= 0) {
        return 1.0;
    }
    int slot = 0;
    for (int i = 0; i < 4; i++) {
        if (st[i].p == p) { slot = i; break; }
        if (st[i].p == NULL) { st[i].p = p; slot = i; break; }
    }
    const Uint32 now = SDL_GetTicks();
    if (fabs(vraw) < 0.95) {
        st[slot].since = 0;
        return 1.0;
    }
    if (st[slot].since == 0) {
        st[slot].since = now ? now : 1;
    }
    const double held = (now - st[slot].since) / 1000.0;
    const double ramp = held <= 0.2 ? 0.0 : (held >= 0.6 ? 1.0 : (held - 0.2) / 0.4);
    const double top = g_inputPadTurnAccel >= 2 ? 2.0 : 1.5;
    return 1.0 + (top - 1.0) * ramp;
}
#endif

int inputPadDirectCompute(int dx, int dy)
{
    struct player *p = inputSlotPlayer();

    if (p == NULL)
        return 0;
    if (p->bonddead || !p->outside_watch_menu || p->pause_state != 0)
        return 0;
    if (gameScriptedCameraActive())
        return 0;

    if (dx == 0 && dy == 0)
        return 1;   /* nothing to turn; caller simply emits no stick */

    f32 fov = slotFovY();
    double k = (fov > 0.0f) ? (double)fov / 60.0 : 1.0;   /* bondview2's base */
    double v;
#if defined(PLATFORM_SWITCH)
    double vcx = (double)dx / 70.0, vcy = (double)dy / 70.0;   /* both axes curved together */
    const double vrawx = vcx > 1.0 ? 1.0 : (vcx < -1.0 ? -1.0 : vcx);
    padLookCurveRadial(&vcx, &vcy);
#endif

    v = (double)dx / 70.0;   /* analogTurn = raw stick \u00b1 5, /70 -- bondview2.c:6222 */
    if (v > 1.0) v = 1.0; else if (v < -1.0) v = -1.0;
#if defined(PLATFORM_SWITCH)
    v = vcx * padTurnAccelGain(p, vrawx);
#else
    if (v >= 0.0) v *= v; else v = -v * v;
#endif
    p->vv_theta += (f32) (v * k * 3.5);
    const double speedTheta = v * k;

    v = (double)dy / 70.0;   /* analogPitch, /70 -- bondview2.c:6161 */
    if (v > 1.0) v = 1.0; else if (v < -1.0) v = -1.0;
#if defined(PLATFORM_SWITCH)
    v = vcy;
#else
    if (v >= 0.0) v *= v; else v = -v * v;
#endif
    const double speedVerta = -v * k;
    p->vv_verta -= (f32) (v * k * 3.5);   /* game: speedverta = -v\u00b7(fov/60) */
    if (p->vv_verta >  90.0f) p->vv_verta =  90.0f;
    if (p->vv_verta < -90.0f) p->vv_verta = -90.0f;
    inputPadAimSway(p, speedTheta, speedVerta);

    if (configGetInputLog()) {
        sysLogPrintf(LOG_NOTE,
            "GE_INPUTLOG paddirect d=(%d,%d) cam=(%.1f,%.1f)",
            dx, dy, (double)p->vv_theta, (double)p->vv_verta);
    }
    return 1;
}

