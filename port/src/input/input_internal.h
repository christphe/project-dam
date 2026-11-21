/*
 * Private to port/src/input*.c: what input.c (devices, snapshot, strategy dispatch, apply blocks),
 * input_kbm.c (keyboard + mouse) and input_script.c (test harness) share. Nothing outside those
 * files includes this. The names are prefixed because the former statics are now external.
 */
#ifndef PORT_INPUT_INTERNAL_H
#define PORT_INPUT_INTERNAL_H

#include <stdio.h>
#include <SDL.h>
#include "input.h"

#define INPUTSCRIPT_MAX       64

struct scriptEntry { long frame; unsigned mask; int sx, sy; int hasStick;
                     int hasMouse, mdx, mdy, mlb, mrb; int hasHold, hold; /* D337/D597 mouse tokens */
                     int hasZHold, zhold;                          /* D207: sustained fire */
                     int hasCHold; unsigned chold;                 /* abpad: sustained C buttons (1.2 walk/strafe) */
                     int hasCrouch, crouch;                       /* D377: free-crouch QA */
                     int hasUse, use, hasReload, reload; };        /* D378: split-action QA */
/* Per-slot state. Slot 0 is the original GE_INPUTSCRIPT harness (byte-for-
 * byte unchanged); slots 1-3 are GE_INPUTSCRIPT_P1..P3 for split-screen test
 * seats (virtual or physical pads). */
struct scriptSlot {
    struct scriptEntry entries[INPUTSCRIPT_MAX];
    int count;        /* -1 = not parsed yet, 0 = parsed empty */
    long frame;       /* controller reads for this slot since launch */
    int curSX, curSY; /* stick set by the last inputScriptApply() for this slot */
    int mouseOn, mdx, mdy, mlb, mrb;   /* sustained scripted mouse (slot 0 only) */
    int hold, zhold, crouch, use, reload;  /* latest sustained holds */
    unsigned chold;          /* latest sustained C-button mask */
    int ignoredWarned;       /* P1..P3: warned once about mouse/crouch tokens */
};

extern struct scriptSlot g_inputScriptSlots[4];
int inputScriptIsActive(int slot);
unsigned inputScriptApply(int slot, unsigned button);
void inputScriptRefreshHolds(int slot);

/* record / replay (input_script.c) */
struct InputRepCur { unsigned b; int x, y, pdx, pdy, pd, use, reload; };
extern int g_inputReplayActive;                                         /* GE_INPUTREPLAY stream open */
extern int g_inputRecPadDX, g_inputRecPadDY, g_inputRecPadDirect;       /* this poll's D404 pad-direct look */
extern FILE *g_inputRec;
extern struct InputRepCur g_inputRepCur;
extern int g_inputRecUse, g_inputRecReload;                             /* this poll's dedicated use / reload (D378) */
void inputReplayFetch(void);
void inputRecordReplayStep(unsigned *button, int *sx, int *sy);

/* input.c, used by the harness */
int inputPadDirectCompute(int dx, int dy);

/* ---- keyboard + mouse (input_kbm.c) ---- */
#define BIND_MAX_KEYS INPUT_BIND_SLOTS
enum {
    IA_FORWARD, IA_BACK, IA_STRAFE_L, IA_STRAFE_R, IA_TURN_L, IA_TURN_R,
    IA_FIRE, IA_AIM, IA_ACTION, IA_CANCEL, IA_LEAN_L, IA_START,
    /* Reload and crouch use the port's dedicated action/stance paths in
     * playable stages; the native B/C-down buttons have other meanings. */
    IA_RELOAD, IA_CROUCH, IA_COUNT
};

struct InputBindDef { const char *key; const char *def; };
extern const struct InputBindDef g_inputBindDefs[IA_COUNT];
extern const char *const g_inputGepdPreset[IA_COUNT];
extern char g_inputBindStr[IA_COUNT][64];
extern int g_inputBind[IA_COUNT][BIND_MAX_KEYS];
extern int g_inputBindsVersion;
extern int g_inputMouseEnabled, g_inputMouseGrabbed, g_inputCaptureArmed, g_inputWindowFocused, g_inputCfgCrosshairCursor;
extern double g_inputMouseDX, g_inputMouseDY;   /* raw px accumulated since the last drain */
extern int g_inputAbsAimSuspend;                /* D194: an aim hold wants the free cursor */
int inputKeyDown(const Uint8 *ks, SDL_Scancode sc);
int inputActHeld(const Uint8 *ks, int act);
void inputMigrateBinds(void);   /* D380; after configLoad */
void inputRebuildBinds(void);   /* D214 */
void inputBindingProbe(void);   /* D383 / D384; opt-in, restores ini state */
extern int g_inputAppRelative;   /* host thread's applied relative-mouse state (a benign racy read elsewhere) */
void inputMouseRequestRelative(int on);
void inputMouseRequestCursor(int show);
void inputApplyGrab(int want);
void inputReconcileGrab(int menuMode);
void inputApplyCursorVisibility(void);


/* ---- aim models (input_aim.c) ---- */
struct player;
#define MAX_PADS            4
#define STICK_DEADZONE      7500   /* 25 %; D546 (was 7000 = 23 %) */
#define STICK_MAX          80

/* Front-end pointer mode: GE's menu cursor ("crosshair") is stick-driven
 * (front.c frontUpdateControlStickPosition reads joyGetStickX/Y and
 * integrates a screen position). During a running stage current_menu ==
 * MENU_RUN_STAGE (11, src/bondconstants.h enum MENU); every other value is
 * a front-end / briefing / debrief / cheat screen where the mouse should
 * feel like a pointer, not a look-axis. We read the game global directly
 * (enum MENU is ABI int) -- UI-context only, no logic change.
 * MENU_INVALID (-1) means the front end never ran (bare -level_XX boot):
 * treat that as in-game so direct-launch mouse-look is unaffected. */
extern int current_menu;
#define GE_MENU_RUN_STAGE  11
#define GE_MENU_INVALID    (-1)
/* D169: the front-end cursor lives in the game's virtual-screen field, which
 * is 440x330 in the front end -- not the 320x240 the pointer P-controller
 * assumed. Read the live field so the mouse pointer can reach the whole
 * mission-select grid. These are plain non-static engine accessors
 * (src/game/bondview.c) and globals (src/game/front.c); UI geometry only, no
 * logic change. */
extern float getPlayer_c_screenwidth(void);
extern float getPlayer_c_screenheight(void);
extern float getPlayer_c_screenleft(void);
extern float getPlayer_c_screentop(void);
extern float cursor_h_pos, cursor_v_pos;

/* D194/D238 -- natural-pitch control scheme.
 *
 * GE's hipfire pitch is structurally digital: bondviewProcessInput's default
 * (1.1/HONEY) scheme reads pitch only from C-up/C-down (see D166), so the
 * port emulates continuous pitch by duty-cycling that button -- an
 * approximation that can never fully match yaw's genuinely continuous,
 * unbounded analog mapping, which is the root of the "vertical feels slower"
 * complaint. GE's own 1.2/SOLITARE scheme (cur_player_get/set_control_type,
 * src/game/options.c; dispatch in bondviewProcessInput, bondview2.c ~5192-
 * 5450) gives BOTH pitch and yaw continuous analog stick control in hipfire
 * (canNaturalTurn/canNaturalPitch) -- at the cost of moving movement
 * (forward/back/strafe) from the analog stick onto digital step buttons
 * (digitalStepForward/Back/Left/Right, fed from C-buttons/D-pad). This is an
 * ORIGINAL, player-selectable GE control style (not new game logic) -- we
 * select it from the port the same way the options menu would
 * (cur_player_set_control_type), and remap WASD/gamepad accordingly so
 * movement keeps working under it. No src/ edits; no behavior invented that
 * GE didn't already support. Escape hatch: Input.NaturalPitch=0 reverts to
 * the 1.1/HONEY default with the D166 digital-pitch-pulse hack. */
extern int cur_player_get_control_type(void);
extern void cur_player_set_control_type(int type);

/* D194 absolute aim: live camera/projection accessors (src/fr.c, src/game/,
 * port/src/video.c). All are plain reads of state owned by the game thread,
 * sampled from inputComputePad which already runs in that same context
 * (contSnapshotFromKeyboard <- osContStartReadData <- joy.c) -- no new
 * cross-thread access, and nothing here writes. */
extern f32 viGetFovY(void);
extern s16 viGetViewWidth(void);
extern s16 viGetViewHeight(void);
extern s16 viGetViewLeft(void);
extern s16 viGetViewTop(void);
extern s16 getWidth320or440(void);   /* CFB width the viewport rect lives in (320 NTSC) */
extern s16 getHeight330or240(void);  /* CFB height (240 NTSC) */
extern f32 portScaleFovY(f32 fovy, s32 isTitleScreen);
extern s32 lvlGetCurrentStageToLoad(void);
#define CONTROLLER_CONFIG_HONEY_    0
#define CONTROLLER_CONFIG_SOLITARE_ 1
#define CONTROLLER_CONFIG_GOODNIGHT_ 3   /* 1.4: SOLITARE movement, swapped button roles (bondview2.c) */
#define TRIG_THRESHOLD     (30 * 256)
#define RSTICK_THRESHOLD   0x4000
/* Mouse-look tuning. GE's aim model is mode-dependent (bondview2.c
 * bondviewProcessInput):
 *   - Hipfire (!insightaimmode): yaw = analog stick-X ("natural turn"),
 *     pitch = DIGITAL C-up/C-down only (stick-Y is move fwd/back here).
 *   - Aim mode (R held):          yaw AND pitch = analog stick pushed past
 *     +/-60 -> proportional (stick-60)/10. C-up/C-down mean crouch/lean/
 *     zoom in this mode, NOT aim -- so we must NOT emit them while aiming
 *     (that was D118c: aim + mouse-down -> crouch).
 * GE's native pitch is inverted ("flight" style): C-up -> look down. We
 * hide that so mouse-down looks down by default; MouseInvertY flips it. */
#define MOUSE_TURN_GAIN     6.0   /* hipfire: raw px this poll -> stick-X counts */
#define MOUSE_PITCH_THRESH  1.5   /* hipfire: px/poll before a C-button fires  */
#define AIM_MOVE_THRESH     0.3   /* aim mode: px/poll before the stick moves   */
#define HIP_PITCH_FULL      6.0   /* hipfire pitch: |px/poll| for a solid C hold (D166) */

/* D194(a) -- aim-mode response curve.
 *
 * bondviewPlayerControlStuff (bondview2.c) turns an aim-mode stick value
 * into turn speed as `(stick_x - 60) / 10.0`, clamped to 1.0 -- i.e. the
 * game's own proportional band is stick in [61,70]; anything from 71 up to
 * whatever AimBand allows (60+AimBand, up to 100) clamps to the exact same
 * max speed as 70. The old code aimed for the port's [61, 60+AimBand] range
 * (up to [61,80]) as if it were all proportional, so a config'd AimBand>10
 * bought nothing, and AIM_GAIN=4.0 reached the (effectively already-maxed)
 * top of that range by ~5 px/poll -- "near bang-bang", matching the D194(a)
 * complaint. Fix: curve-map px/poll onto the game's REAL proportional range
 * [61,70] (independent of AimBand, which still acts as an extra ceiling for
 * anyone who wants to cap below the game's own max), with a configurable
 * gamma so slow motions land near the low end instead of jumping to nearly
 * full speed immediately. */
#define AIM_STICK_MIN        61
#define AIM_STICK_GAME_MAX   70    /* bondview2.c: (stick-60)/10 saturates at stick=70 */
#define AIM_FULL_SPEED_PX    20.0  /* px/poll (at MouseAimSpeed=100) that reaches AIM_STICK_GAME_MAX */

/* D194 GEPD-style aim mapping. While RMB is held (cursor grabbed/hidden),
 * mouse MOVEMENT rotates the view by the same screen angle swept: a
 * sweep across the rendered viewport width sweeps the full horizontal FOV,
 * height <-> fovy. The crosshair stays locked at screen centre and stop
 * moving holds the view where it is (the game's own speed decay coasts it to
 * rest). Because the scale is angle-per-pixel of the CURRENT fovy, scope
 * zooms automatically become finer control. No target, no chase, no
 * snap-back: placement is cumulative and path-dependent, exactly like GEPD.
 */
/* D194 GEPD-mirror aim constants (MouseInjectorPlugin/games/goldeneye.c): */
#define AIM_ABS_DEAD_PX       2.0    /* per-poll px below this is jitter, not aim */
#define GEPD_CROSSHAIR_LIMIT  5.159373283  /* crosshair pos units at the screen edge (GEPD 0x40A51996) */
#define GEPD_EDGE_THRESHOLD   0.72f        /* |pos|/limit beyond which the view scrolls */
#define GEPD_SCROLL_SPEED     475.0        /* GEPD: (ratio-threshold)*475*timestep per tick */
#define GEPD_BASE_FOV         90.0f        /* unzoomed FOV (our native); GEPD uses its 60 override */

/* D194(b) -- mouse sensitivity coupled to frame/poll rate.
 *
 * MOUSE_TURN_GAIN/AIM_GAIN above treat "px accumulated since the last
 * inputComputePad(0) drain" as a fixed per-poll unit, but the real interval
 * between drains drifts with render/scene load (D193 measured a rock-steady
 * 60 sim ticks/s in the idle case, but the drain itself piggybacks on
 * whatever cadence calls contSnapshotFromKeyboard() -- 1-2x per rendered
 * frame per D165's comment -- so a hitch or a variable-length frame changes
 * how much real time one "poll" of accumulated px represents). Net effect:
 * the same physical hand motion emits a different turn rate depending on
 * how ragged the poll cadence is, which reads as "sensitivity changes with
 * movement/frame rate" (the user's own words).
 *
 * Fix: measure real elapsed time since the last drain with a monotonic
 * clock and rescale the accumulated px by (dtRef / dtActual) before it
 * hits MOUSE_TURN_GAIN/AIM_GAIN, where dtRef is the nominal 1/60s tick
 * (matches D193's measured steady-state sim rate) -- so "px this poll"
 * always means "px for a nominal 1/60s tick" regardless of how long that
 * tick actually took in real time. Clamped both directions so one big
 * stall (level load, GC pause) can't fling the view. Menu-cursor paths
 * (D165/D169's P-controller, which has its own poll-count-based settling
 * design) are deliberately NOT touched by this -- gameplay look only. */
#define MOUSE_DT_REF        (1.0 / 60.0)
#define MOUSE_DT_SCALE_MIN  0.25   /* cap correction for an abnormally long poll gap */
#define MOUSE_DT_SCALE_MAX  4.0    /* cap correction for an abnormally short poll gap */

/* Item 1 (D165) — front-end 1:1 pointer. front.c frontUpdateControlStickPosition
 * INTEGRATES the stick as a velocity into a screen-pixel cursor position
 * (cursor_h_pos += (stickx*0.075 +/- 0.5) * delta, deadzone +/-5, clamp +/-70,
 * cursor clamped into the ~320x240 virtual screen rect minus a 20px margin).
 * Feeding it mouse *velocity* therefore gives velocity^2 feel. Instead we run a
 * P-controller: keep our own estimate of where the game cursor is (menuEst*,
 * integrated with the SAME recurrence as front.c), accumulate a target from
 * mouse motion, and emit stick = clamp(GAIN*(target-est)). The estimate re-syncs
 * to the real cursor whenever the target is held at a screen edge. */
#define MENU_CURSOR_LO      20.0
#define MENU_CURSOR_HI_H    300.0
#define MENU_CURSOR_HI_V    220.0
/* D345: after a front-end screen change, re-assert the 1:1 pointer for a few
 * frames if the mouse was used within this many ms (see inputComputePad). */
#define MENU_POINTER_REASSERT_MS 1500.0
#define MENU_CURSOR_MID_H   160.0
#define MENU_CURSOR_MID_V   120.0

struct aimSlotState {
    double crossX, crossY;   /* was s_gepdCrossX/Y */
    int gepdHeldPrev;        /* aim held last tick -> adopt on entry */
    int aimHeldPrev;         /* was s_aimHeldPrev */
    int centreClearTicks;    /* was s_centreClearTicks */
};
enum { AIMMODE_N64 = 0, AIMMODE_CENTRED = 1 };
#define AIMS (g_inputAimSt[(g_inputSlot > 0 && g_inputSlot < 4) ? g_inputSlot : 0])
extern int g_inputSlot;
extern int g_inputGepdSens;
extern int g_inputMouseTurnSpeed;
extern int g_inputMouseSensitivity;
extern int g_inputAimAbsolute;
extern int g_inputMouseDirectLook;
extern int g_inputPdMouseAim;
extern int g_inputAimMode;
extern int g_inputAimDevMouse;
extern int g_inputAimLegacyGepd;
extern int g_inputAimStyleLegacy;
extern double g_inputHipThetaDegOut;
extern int g_inputPadLookCurve;
extern int g_inputPadTurnAccel;
int inputMpSplit(void);
struct player * inputSlotPlayer(void);
extern int g_inputAimRange;
int inputAimModeGet(void);
double inputAimRangeScale(void);
int inputPdMouseAimEnabled(void);
int inputAimGepdAccumulate(double dxPx, double dyLook);
void inputAimGepdEdgeScroll(void);
int inputAimGepdCompute(double dxPx, double dyLook);
void inputSwayFeedTheta(double deg);
int inputHipDirectCompute(double dxPx, double dyLook);
void inputPadAimSway(struct player *p, double speedTheta, double speedVerta);
extern struct aimSlotState g_inputAimSt[4];

#endif
