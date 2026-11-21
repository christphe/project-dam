/*
 * Input strategies: the snapshot that goes in, the commands that come out.
 *
 * One control style = one strategy: a pure function from an InputSnapshot (what
 * the device and the game say this poll) to InputCommands (what the player asks
 * for). A strategy includes no SDL header and reads no game global: the adapter
 * in port/src/input.c fills the snapshot and applies the commands, so a strategy
 * is tested with plain tables (tests/test_input_*.c, no platform, no game).
 *
 * Today the commands are bridged to the emulated N64 pad (n64bridge.c). The
 * fields are semantic (fire, use, zoom ...) so a later step can hand them to
 * the game directly instead.
 */
#ifndef PORT_INPUT_TYPES_H
#define PORT_INPUT_TYPES_H

#include <stdint.h>

/* Device buttons, by the label SDL reports (INB_A is the button SDL calls A:
 * the bottom one on an Xbox pad, the right one on a Nintendo pad). A style that
 * goes by position resolves it with InputSnapshot.family. The triggers are
 * digital here only for a tap the device layer latched; the analog value is
 * InputSnapshot.triggerL/R and the strategy applies settings.triggerPct to it. */
enum {
    INB_A = 1u << 0, INB_B = 1u << 1, INB_X = 1u << 2, INB_Y = 1u << 3,
    INB_LB = 1u << 4, INB_RB = 1u << 5, INB_LS = 1u << 6, INB_RS = 1u << 7,   /* LS/RS: stick clicks */
    INB_START = 1u << 8,
    INB_DUP = 1u << 9, INB_DDOWN = 1u << 10, INB_DLEFT = 1u << 11, INB_DRIGHT = 1u << 12,
    INB_LT = 1u << 13, INB_RT = 1u << 14
};

enum { INFAM_XBOX = 0, INFAM_PS = 1, INFAM_NINTENDO = 2 };

/* Held-action bits a strategy reports back (InputCommands.actionsHeld) and the
 * adapter hands in again next poll (InputSnapshot.prevActions), so edges need no
 * state inside the strategy. The order is input.c's PA_* order (asserted there). */
enum {
    INACT_FIRE, INACT_AIM, INACT_USE, INACT_RELOAD, INACT_CROUCH, INACT_NEXTWPN,
    INACT_PREVWPN, INACT_GADGET, INACT_START, INACT_COUNT
};
#define INACT_BIT(a) (1u << (a))

/* The N64 buttons the Original style binds (index order = input.c's NB_*). */
enum {
    INNB_A, INNB_B, INNB_Z, INNB_R, INNB_L, INNB_CU, INNB_CD, INNB_CL, INNB_CR, INNB_START,
    INNB_DU, INNB_DD, INNB_DL, INNB_DR, INNB_COUNT
};

#define INPUT_STICK_MAX        80       /* the N64 stick range the game reads */
#define INPUT_RSTICK_THRESHOLD 0x4000   /* a stick as a digital button (raw SDL units) */

/* Where the pad is, in the game: what the rules need to know, nothing more. */
typedef struct InputContext {
    int menu;            /* a front-end screen: the left stick navigates, A/B accept/cancel */
    int facePlayable;    /* in a stage and the face buttons are gameplay actions */
    int tank;            /* driving the tank (not facePlayable) */
    int inStage;         /* a running stage (the assist rules of the Modern style) */
    int aiming;          /* the game is in aim mode */
    int dead;            /* the player is dead */
    int scopedWeapon;    /* the weapon in hand zooms with C-up / C-down in aim (DISABLE_CROUCH) */
    int wheelOpen;       /* a weapon / gadget wheel is open on this pad: the right stick is the wheel's */
    int padDrives;       /* the pad plays this seat; 0 = the keyboard does (only the aim assists still read the pad, as before) */
} InputContext;

/* Per-seat settings (the F10 menu / ini). Sticks are raw SDL units (0..32767),
 * sensitivities are percent. */
typedef struct InputSettings {
    int deadzoneL, deadzoneR;
    int lookSensX, lookSensY;   /* 100 = native */
    int aimLookSens;            /* % while aiming, passed to aimScale */
    int lookSmooth;             /* 0 = off .. 10 */
    int lookInvertY;
    int triggerPct;             /* trigger press point, % of travel */
    int naturalPitch;           /* SOLITARE stick roles (left = digital moves, right = look) vs HONEY (Enhanced: already
                                 * resolved with the preset, see inputEnhancedRoles; Original ignores it) */
    int stickSwap;              /* Enhanced: the two sticks swap roles (1.3 Frost, 1.4 Elektra), never in menus */
    int n64StickFeel;           /* Original: 1 = the accurate N64 stick response, 0 = deadzone + rescale as the others */
    uint32_t bind[INACT_COUNT];     /* Enhanced: the INB_* sources bound to each action (the adapter resolves preset / Custom strings) */
    uint32_t n64Bind[INNB_COUNT];   /* Original: the INB_* sources bound to each N64 button */
} InputSettings;

typedef struct InputSnapshot {
    uint32_t buttons;           /* INB_* held (incl. latched taps) */
    int lx, ly, rx, ry;         /* raw SDL axes, -32768..32767, up = negative */
    int triggerL, triggerR;     /* raw 0..32767 */
    int family;                 /* INFAM_* */
    uint32_t prevActions;       /* INACT_BIT set the strategy reported last poll */
    InputContext ctx;
    InputSettings cfg;
    /* The game's aim-mode scaling of a look axis (zoom, assist); NULL = identity. */
    int (*aimScale)(void *user, int v, int sens);
    void *aimScaleUser;
} InputSnapshot;

enum { INMODE_NONE = 0, INMODE_MENU, INMODE_SOLITARE, INMODE_HONEY };

/* What the player asks for. Analog values are in N64 stick units (+-80, up and
 * right positive); the booleans are 0/1. */
typedef struct InputCommands {
    int mode;                   /* INMODE_*: which stick roles ran */
    int moveX, moveY;           /* the analog stick: menu navigation, and walk/turn in HONEY roles. 0 = leave the stick alone */
    int moveActive;             /* HONEY roles: the left stick is deflected */
    int lookX, lookY;           /* SOLITARE roles: look, after sensitivity / smoothing / invert */
    int lookRawActive;          /* SOLITARE roles: the look stick is deflected (before smoothing) */
    int moveFwd, moveBack;      /* SOLITARE roles: left stick as digital moves */
    int walk, strafe;           /* SOLITARE roles: the left stick as analog moves (deadzone + rescale, N64 units, forward / right positive) */
    int direct;                 /* GameDirect (#78): the game takes walk / strafe straight (inputDirectMoveDecide); the bridge then
                                 * leaves the digital move bits out. moveFwd / strafeL ... stay set: they mean "moving" to the assists. */
    int strafeL, strafeR;       /* digital strafe: SOLITARE left stick, HONEY right stick */
    int pitchUp, pitchDown;     /* HONEY roles: right stick up / down as digital look */
    int slowStrafeL, slowStrafeR;   /* aiming: the left stick strafes slowly */
    int zoomIn, zoomOut;        /* aiming a scoped weapon: D-pad up / down */
    int fire, aim;              /* triggers (before the southpaw swap) */
    int fireDigital, aimDigital;    /* Enhanced: a non-trigger source bound to fire / aim (LB aim): ORed AFTER the southpaw swap */
    int useEdge, reloadEdge;    /* pressed this poll (gameplay) */
    int nextWeapon, prevWeapon; /* pressed this poll (gameplay, tank) */
    int gadgetEdge;             /* pressed this poll: cycle the gadget */
    int crouch;                 /* held (gameplay) */
    int menuAccept, menuCancel; /* front end / watch: the fixed accept / cancel faces */
    int start;
    int dpadUp, dpadDown, dpadLeft, dpadRight;
    uint32_t actionsHeld;       /* INACT_BIT set: becomes next poll's prevActions */
    /* Original: the strict virtual N64 pad. When n64Raw is set the pad's whole contribution to the button
     * mask is n64Buttons (GE_CONT_* bits, the right stick's digital C included): the bridge ORs it in and
     * nothing else (no southpaw swap, no port action). The left stick still comes out as moveX / moveY. */
    int n64Raw;
    unsigned n64Buttons;
} InputCommands;

/* The only state a strategy keeps between polls (one per seat). */
typedef struct InputStrategyState {
    double lookSmoothX, lookSmoothY;   /* low-pass of the look stick */
} InputStrategyState;

typedef struct InputStrategy {
    const char *name;
    void (*map)(const InputSnapshot *in, InputStrategyState *st, InputCommands *out);
} InputStrategy;

void inputStrategyStateReset(InputStrategyState *st);

/* Shared helpers (input_common.c). */
int inputScaleAxis(int v, int dz);                         /* deadzone + rescale to +-INPUT_STICK_MAX */
int inputTriggerDown(const InputSnapshot *in, int left);   /* analog past the trigger point, or a latched tap */
/* GameDirect (#78): decides whether the left stick's analog walk / strafe go straight into the game's
 * movement (SOLITARE roles, the pad plays a playable seat, not aiming, not in the tank, alive). Returns 1 and
 * the values in the game's own stick scale (the N64 "safe" scale: a 5 unit deadzone off, +-75 at the rim, as
 * the game derives it from a HONEY stick), else 0 and the game's movement is untouched. */
int inputDirectMoveDecide(const InputContext *ctx, const InputCommands *cmds, int *walk, int *strafe);
/* GameDirect, the keyboard part: adds the held move keys to walk / strafe at full scale (+-INPUT_DIRECT_MAX),
 * on top of the stick, each axis clamped to +-INPUT_DIRECT_MAX. Opposite inputs cancel. */
#define INPUT_DIRECT_MAX       (INPUT_STICK_MAX - 5)   /* the game's "safe" stick scale at the rim */
void inputDirectMoveAddKeys(int fwd, int back, int left, int right, int *walk, int *strafe);
void inputMapSticks(const InputSnapshot *in, InputStrategyState *st, InputCommands *out);   /* Modern / Enhanced stick roles */

#endif
