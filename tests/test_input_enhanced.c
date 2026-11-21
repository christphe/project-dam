/*
 * The Enhanced control styles (port/src/input/style_enhanced.c): presets 0..4
 * (1.1 Jinx, 1.2 Christmas, 1.3 Frost, Custom, 1.4 Elektra) as a table of
 * {snapshot, expected commands}, the bridge's new pieces, and a differential
 * test against a verbatim copy of the mapping this strategy replaced
 * (padMapPure of input.c before step 4 of #78). Pure: no game core, no platform.
 *
 * Stick numbers as in test_input_modern.c: deadzone 3000, 20000 -> 45.
 */
#include "ge_test.h"
#include "input/input_types.h"
#include "input/style_enhanced.h"
#include "input/n64bridge.h"

#define A(x) INACT_BIT(x)

/* 1.1 Jinx = the default table (input.c kPadAct) */
#define JINX_BIND { [INACT_FIRE] = INB_RT, [INACT_AIM] = INB_LT | INB_LB, [INACT_USE] = INB_A, [INACT_RELOAD] = INB_X, \
                    [INACT_CROUCH] = INB_LS | INB_RS, [INACT_NEXTWPN] = INB_Y, [INACT_PREVWPN] = 0, \
                    [INACT_GADGET] = INB_B, [INACT_START] = INB_START }
/* natural, swap: the roles a preset resolves to (checked in testRoles) */
#define CFG(nat, swap) { .deadzoneL = 3000, .deadzoneR = 3000, .lookSensX = 100, .lookSensY = 100, .aimLookSens = 100, \
                         .triggerPct = 25, .naturalPitch = (nat), .stickSwap = (swap), .bind = JINX_BIND }
#define JINX      CFG(1, 0)
#define CHRISTMAS CFG(0, 0)
#define FROST     CFG(0, 1)
#define ELEKTRA   CFG(1, 1)
#define PLAY  { .facePlayable = 1, .inStage = 1, .padDrives = 1 }
#define MENU  { .menu = 1, .padDrives = 1 }
#define TANK  { .tank = 1, .inStage = 1, .padDrives = 1 }
#define LOBBY { .padDrives = 1 }   /* not a menu, not playable: the watch */
#define WHEEL { .facePlayable = 1, .inStage = 1, .padDrives = 1, .wheelOpen = 1 }

typedef struct { const char *name; InputSnapshot in; InputCommands want; } Row;

static const Row rows[] = {
    /* ---- 1.1 Jinx, SOLITARE roles (NaturalPitch on), playable ---- */
    { "jinx idle", { .cfg = JINX, .ctx = PLAY }, { .mode = INMODE_SOLITARE } },
    { "jinx A = use", { .cfg = JINX, .ctx = PLAY, .buttons = INB_A },
      { .mode = INMODE_SOLITARE, .useEdge = 1, .actionsHeld = A(INACT_USE) } },
    { "jinx X = reload", { .cfg = JINX, .ctx = PLAY, .buttons = INB_X },
      { .mode = INMODE_SOLITARE, .reloadEdge = 1, .actionsHeld = A(INACT_RELOAD) } },
    { "jinx Y = next weapon", { .cfg = JINX, .ctx = PLAY, .buttons = INB_Y },
      { .mode = INMODE_SOLITARE, .nextWeapon = 1, .actionsHeld = A(INACT_NEXTWPN) } },
    { "jinx B = cycle gadget", { .cfg = JINX, .ctx = PLAY, .buttons = INB_B },
      { .mode = INMODE_SOLITARE, .gadgetEdge = 1, .actionsHeld = A(INACT_GADGET) } },
    { "jinx L3 = crouch", { .cfg = JINX, .ctx = PLAY, .buttons = INB_LS },
      { .mode = INMODE_SOLITARE, .crouch = 1, .actionsHeld = A(INACT_CROUCH) } },
    { "jinx R3 = crouch", { .cfg = JINX, .ctx = PLAY, .buttons = INB_RS },
      { .mode = INMODE_SOLITARE, .crouch = 1, .actionsHeld = A(INACT_CROUCH) } },
    { "jinx LB = aim (digital, after the swap)", { .cfg = JINX, .ctx = PLAY, .buttons = INB_LB },
      { .mode = INMODE_SOLITARE, .aimDigital = 1, .actionsHeld = A(INACT_AIM) } },
    { "jinx RB is free", { .cfg = JINX, .ctx = PLAY, .buttons = INB_RB }, { .mode = INMODE_SOLITARE } },
    { "jinx start", { .cfg = JINX, .ctx = PLAY, .buttons = INB_START },
      { .mode = INMODE_SOLITARE, .start = 1, .actionsHeld = A(INACT_START) } },
    { "jinx d-pad passes through", { .cfg = JINX, .ctx = PLAY, .buttons = INB_DUP | INB_DRIGHT },
      { .mode = INMODE_SOLITARE, .dpadUp = 1, .dpadRight = 1 } },
    { "jinx use held across polls: no new edge",
      { .cfg = JINX, .ctx = PLAY, .buttons = INB_A, .prevActions = A(INACT_USE) },
      { .mode = INMODE_SOLITARE, .actionsHeld = A(INACT_USE) } },
    { "jinx gadget edge only on the press",
      { .cfg = JINX, .ctx = PLAY, .buttons = INB_B, .prevActions = A(INACT_GADGET) },
      { .mode = INMODE_SOLITARE, .actionsHeld = A(INACT_GADGET) } },
    { "jinx goes by the letter on a nintendo pad (A = use)",
      { .cfg = JINX, .ctx = PLAY, .family = INFAM_NINTENDO, .buttons = INB_A },
      { .mode = INMODE_SOLITARE, .useEdge = 1, .actionsHeld = A(INACT_USE) } },
    { "jinx nintendo B is still the gadget",
      { .cfg = JINX, .ctx = PLAY, .family = INFAM_NINTENDO, .buttons = INB_B },
      { .mode = INMODE_SOLITARE, .gadgetEdge = 1, .actionsHeld = A(INACT_GADGET) } },

    /* ---- triggers vs the trigger point (25 % = 8175) ---- */
    { "RT above the point fires", { .cfg = JINX, .ctx = PLAY, .triggerR = 8176 },
      { .mode = INMODE_SOLITARE, .fire = 1, .actionsHeld = A(INACT_FIRE) } },
    { "RT at the point does not", { .cfg = JINX, .ctx = PLAY, .triggerR = 8175 }, { .mode = INMODE_SOLITARE } },
    { "LT above the point aims", { .cfg = JINX, .ctx = PLAY, .triggerL = 20000 },
      { .mode = INMODE_SOLITARE, .aim = 1, .actionsHeld = A(INACT_AIM) } },
    { "trigger point 90 %: 20000 is not a press",
      { .cfg = { .deadzoneL = 3000, .deadzoneR = 3000, .lookSensX = 100, .lookSensY = 100, .aimLookSens = 100,
                 .triggerPct = 90, .naturalPitch = 1, .bind = JINX_BIND }, .ctx = PLAY, .triggerR = 20000 },
      { .mode = INMODE_SOLITARE } },
    { "a latched RT tap fires with the trigger at rest", { .cfg = JINX, .ctx = PLAY, .buttons = INB_RT },
      { .mode = INMODE_SOLITARE, .fire = 1, .actionsHeld = A(INACT_FIRE) } },
    { "triggers act in the menu", { .cfg = JINX, .ctx = MENU, .triggerR = 20000, .triggerL = 20000 },
      { .mode = INMODE_MENU, .fire = 1, .aim = 1, .actionsHeld = A(INACT_FIRE) | A(INACT_AIM) } },
    { "triggers act in the tank", { .cfg = JINX, .ctx = TANK, .triggerR = 20000 },
      { .mode = INMODE_SOLITARE, .fire = 1, .actionsHeld = A(INACT_FIRE) } },

    /* ---- sticks: SOLITARE (Jinx with NaturalPitch, Elektra, Custom) ---- */
    { "solitare: left up = forward, left = strafe", { .cfg = JINX, .ctx = PLAY, .ly = -20000, .lx = -20000 },
      { .mode = INMODE_SOLITARE, .moveFwd = 1, .strafeL = 1 } },
    { "solitare: left down + right = back, strafe", { .cfg = JINX, .ctx = PLAY, .ly = 20000, .lx = 20000 },
      { .mode = INMODE_SOLITARE, .moveBack = 1, .strafeR = 1 } },
    { "solitare: right stick = look", { .cfg = JINX, .ctx = PLAY, .rx = 20000, .ry = -20000 },
      { .mode = INMODE_SOLITARE, .lookX = 45, .lookY = 45, .lookRawActive = 1 } },
    { "solitare: right stick inside the deadzone", { .cfg = JINX, .ctx = PLAY, .rx = 2999, .ry = -3000 },
      { .mode = INMODE_SOLITARE } },
    { "solitare: invert Y",
      { .cfg = { .deadzoneL = 3000, .deadzoneR = 3000, .lookSensX = 100, .lookSensY = 100, .aimLookSens = 100,
                 .triggerPct = 25, .naturalPitch = 1, .lookInvertY = 1, .bind = JINX_BIND },
        .ctx = PLAY, .rx = 20000, .ry = -20000 },
      { .mode = INMODE_SOLITARE, .lookX = 45, .lookY = -45, .lookRawActive = 1 } },
    { "solitare: look sensitivity X 200 % clamps",
      { .cfg = { .deadzoneL = 3000, .deadzoneR = 3000, .lookSensX = 200, .lookSensY = 100, .aimLookSens = 100,
                 .triggerPct = 25, .naturalPitch = 1, .bind = JINX_BIND }, .ctx = PLAY, .rx = 20000 },
      { .mode = INMODE_SOLITARE, .lookX = 80, .lookRawActive = 1 } },
    { "solitare: right deadzone 5000, 6000 gives 2",
      { .cfg = { .deadzoneL = 3000, .deadzoneR = 5000, .lookSensX = 100, .lookSensY = 100, .aimLookSens = 100,
                 .triggerPct = 25, .naturalPitch = 1, .bind = JINX_BIND }, .ctx = PLAY, .rx = 6000 },
      { .mode = INMODE_SOLITARE, .lookX = 2, .lookRawActive = 1 } },
    { "solitare: an open wheel owns the right stick",
      { .cfg = JINX, .ctx = WHEEL, .rx = 20000, .ry = -20000, .lx = 20000 },
      { .mode = INMODE_SOLITARE, .strafeR = 1 } },

    /* ---- 1.2 Christmas: HONEY roles (left analog, right digital) ---- */
    { "christmas: left stick = the analog stick", { .cfg = CHRISTMAS, .ctx = PLAY, .lx = 20000, .ly = -20000 },
      { .mode = INMODE_HONEY, .moveX = 45, .moveY = 45, .moveActive = 1 } },
    { "christmas: right stick = digital C",
      { .cfg = CHRISTMAS, .ctx = PLAY, .rx = 20000, .ry = -20000 },
      { .mode = INMODE_HONEY, .strafeR = 1, .pitchUp = 1 } },
    { "christmas: right stick left / down",
      { .cfg = CHRISTMAS, .ctx = PLAY, .rx = -20000, .ry = 20000 },
      { .mode = INMODE_HONEY, .strafeL = 1, .pitchDown = 1 } },
    { "christmas: right stick at the threshold is still", { .cfg = CHRISTMAS, .ctx = PLAY, .rx = 16384, .ry = -16384 },
      { .mode = INMODE_HONEY } },
    { "christmas: invert Y flips the analog stick Y",
      { .cfg = { .deadzoneL = 3000, .deadzoneR = 3000, .lookSensX = 100, .lookSensY = 100, .aimLookSens = 100,
                 .triggerPct = 25, .lookInvertY = 1, .bind = JINX_BIND }, .ctx = PLAY, .ly = -20000 },
      { .mode = INMODE_HONEY, .moveY = -45, .moveActive = 1 } },
    { "christmas: left deadzone 9000 hides 8000",
      { .cfg = { .deadzoneL = 9000, .deadzoneR = 3000, .lookSensX = 100, .lookSensY = 100, .aimLookSens = 100,
                 .triggerPct = 25, .bind = JINX_BIND }, .ctx = PLAY, .lx = 8000 },
      { .mode = INMODE_HONEY } },
    { "christmas: same buttons (A = use)", { .cfg = CHRISTMAS, .ctx = PLAY, .buttons = INB_A },
      { .mode = INMODE_HONEY, .useEdge = 1, .actionsHeld = A(INACT_USE) } },
    { "NaturalPitch off on jinx = honey roles", { .cfg = CFG(0, 0), .ctx = PLAY, .lx = 20000 },
      { .mode = INMODE_HONEY, .moveX = 45, .moveActive = 1 } },

    /* ---- 1.3 Frost: HONEY, the two sticks swapped ---- */
    { "frost: the physical left stick is the digital C",
      { .cfg = FROST, .ctx = PLAY, .lx = 20000, .ly = -20000 },
      { .mode = INMODE_HONEY, .strafeR = 1, .pitchUp = 1 } },
    { "frost: the physical right stick is the analog stick",
      { .cfg = FROST, .ctx = PLAY, .rx = 20000, .ry = -20000 },
      { .mode = INMODE_HONEY, .moveX = 45, .moveY = 45, .moveActive = 1 } },
    { "frost: an open wheel zeroes the swapped right stick (the physical left)",
      { .cfg = FROST, .ctx = WHEEL, .lx = 20000, .rx = 20000 },
      { .mode = INMODE_HONEY, .moveX = 45, .moveActive = 1 } },
    { "frost: the menu never swaps (left stick navigates)",
      { .cfg = FROST, .ctx = MENU, .lx = 20000, .ly = -20000, .rx = -20000 },
      { .mode = INMODE_MENU, .moveX = 45, .moveY = 45 } },

    /* ---- 1.4 Elektra: SOLITARE, the two sticks swapped ---- */
    { "elektra: the physical right stick moves",
      { .cfg = ELEKTRA, .ctx = PLAY, .ry = -20000, .rx = -20000 },
      { .mode = INMODE_SOLITARE, .moveFwd = 1, .strafeL = 1 } },
    { "elektra: the physical left stick looks",
      { .cfg = ELEKTRA, .ctx = PLAY, .lx = 20000, .ly = -20000 },
      { .mode = INMODE_SOLITARE, .lookX = 45, .lookY = 45, .lookRawActive = 1 } },
    { "elektra: an open wheel zeroes the look (physical left)",
      { .cfg = ELEKTRA, .ctx = WHEEL, .lx = 20000, .ry = -20000 },
      { .mode = INMODE_SOLITARE, .moveFwd = 1 } },
    { "elektra: the menu never swaps",
      { .cfg = ELEKTRA, .ctx = MENU, .lx = -20000, .ry = 20000 },
      { .mode = INMODE_MENU, .moveX = -45 } },

    /* ---- front end: menu lockout guard, fixed accept / cancel ---- */
    { "menu: A accepts (and is held as use)", { .cfg = JINX, .ctx = MENU, .buttons = INB_A },
      { .mode = INMODE_MENU, .menuAccept = 1, .actionsHeld = A(INACT_USE) } },
    { "menu: X accepts", { .cfg = JINX, .ctx = MENU, .buttons = INB_X },
      { .mode = INMODE_MENU, .menuAccept = 1, .actionsHeld = A(INACT_RELOAD) } },
    { "menu: B cancels", { .cfg = JINX, .ctx = MENU, .buttons = INB_B },
      { .mode = INMODE_MENU, .menuCancel = 1, .actionsHeld = A(INACT_GADGET) } },
    { "menu: Y cancels", { .cfg = JINX, .ctx = MENU, .buttons = INB_Y },
      { .mode = INMODE_MENU, .menuCancel = 1, .actionsHeld = A(INACT_NEXTWPN) } },
    { "menu: left stick navigates (up = positive)", { .cfg = JINX, .ctx = MENU, .lx = -20000, .ly = -20000 },
      { .mode = INMODE_MENU, .moveX = -45, .moveY = 45 } },
    { "menu: no crouch, no gadget, no digital aim",
      { .cfg = JINX, .ctx = MENU, .buttons = INB_LS | INB_LB },
      { .mode = INMODE_MENU, .actionsHeld = A(INACT_CROUCH) | A(INACT_AIM) } },
    { "menu: start and d-pad", { .cfg = JINX, .ctx = MENU, .buttons = INB_START | INB_DDOWN },
      { .mode = INMODE_MENU, .start = 1, .dpadDown = 1, .actionsHeld = A(INACT_START) } },
    { "watch (neither menu nor playable): A accepts", { .cfg = JINX, .ctx = LOBBY, .buttons = INB_A },
      { .mode = INMODE_SOLITARE, .menuAccept = 1, .actionsHeld = A(INACT_USE) } },

    /* ---- tank ---- */
    { "tank: Y changes weapon (native A)", { .cfg = JINX, .ctx = TANK, .buttons = INB_Y },
      { .mode = INMODE_SOLITARE, .nextWeapon = 1, .actionsHeld = A(INACT_NEXTWPN) } },
    { "tank: A (use) and X (reload) do nothing here", { .cfg = JINX, .ctx = TANK, .buttons = INB_A | INB_X },
      { .mode = INMODE_SOLITARE, .actionsHeld = A(INACT_USE) | A(INACT_RELOAD) } },
    { "tank: no crouch, no gadget cycle", { .cfg = JINX, .ctx = TANK, .buttons = INB_LS | INB_B },
      { .mode = INMODE_SOLITARE, .actionsHeld = A(INACT_CROUCH) | A(INACT_GADGET) } },
    { "tank: LB aims (digital)", { .cfg = JINX, .ctx = TANK, .buttons = INB_LB },
      { .mode = INMODE_SOLITARE, .aimDigital = 1, .actionsHeld = A(INACT_AIM) } },
    { "tank: no menu accept", { .cfg = JINX, .ctx = TANK, .buttons = INB_B | INB_A },
      { .mode = INMODE_SOLITARE, .actionsHeld = A(INACT_GADGET) | A(INACT_USE) } },
};

/* ---- Custom binds (preset 3): the adapter resolves the seat's strings to masks ---- */
typedef struct { const char *name; uint32_t bind[INACT_COUNT]; int nat; InputSnapshot in; InputCommands want; } CustomRow;

static const CustomRow customRows[] = {
    { "custom: fire := A (digital) acts when playable",
      { [INACT_FIRE] = INB_A, [INACT_START] = INB_START }, 1,
      { .ctx = PLAY, .buttons = INB_A }, { .mode = INMODE_SOLITARE, .fireDigital = 1, .actionsHeld = A(INACT_FIRE) } },
    { "custom: fire := A does nothing in the menu (but menu accept stays)",
      { [INACT_FIRE] = INB_A, [INACT_START] = INB_START }, 1,
      { .ctx = MENU, .buttons = INB_A }, { .mode = INMODE_MENU, .menuAccept = 1, .actionsHeld = A(INACT_FIRE) } },
    { "custom: RT is no longer fire when unbound",
      { [INACT_FIRE] = INB_A, [INACT_START] = INB_START }, 1,
      { .ctx = PLAY, .triggerR = 20000 }, { .mode = INMODE_SOLITARE } },
    { "custom: fire := RB,RT: both fire (RB digital, RT trigger)",
      { [INACT_FIRE] = INB_RB | INB_RT, [INACT_START] = INB_START }, 1,
      { .ctx = PLAY, .buttons = INB_RB, .triggerR = 20000 },
      { .mode = INMODE_SOLITARE, .fire = 1, .fireDigital = 1, .actionsHeld = A(INACT_FIRE) } },
    { "custom: aim := LT only (LB is free)",
      { [INACT_AIM] = INB_LT, [INACT_START] = INB_START }, 1,
      { .ctx = PLAY, .buttons = INB_LB }, { .mode = INMODE_SOLITARE } },
    { "custom: prev weapon := RB, playable",
      { [INACT_PREVWPN] = INB_RB, [INACT_START] = INB_START }, 1,
      { .ctx = PLAY, .buttons = INB_RB }, { .mode = INMODE_SOLITARE, .prevWeapon = 1, .actionsHeld = A(INACT_PREVWPN) } },
    { "custom: prev weapon := RB, tank gives the native A (next)",
      { [INACT_PREVWPN] = INB_RB, [INACT_START] = INB_START }, 1,
      { .ctx = TANK, .buttons = INB_RB }, { .mode = INMODE_SOLITARE, .nextWeapon = 1, .actionsHeld = A(INACT_PREVWPN) } },
    { "custom: prev weapon := RB, nothing in the menu",
      { [INACT_PREVWPN] = INB_RB, [INACT_START] = INB_START }, 1,
      { .ctx = MENU, .buttons = INB_RB }, { .mode = INMODE_MENU, .actionsHeld = A(INACT_PREVWPN) } },
    { "custom: use := Y, next := none",
      { [INACT_USE] = INB_Y, [INACT_START] = INB_START }, 1,
      { .ctx = PLAY, .buttons = INB_Y }, { .mode = INMODE_SOLITARE, .useEdge = 1, .actionsHeld = A(INACT_USE) } },
    { "custom: crouch := B (stick clicks free)",
      { [INACT_CROUCH] = INB_B, [INACT_START] = INB_START }, 1,
      { .ctx = PLAY, .buttons = INB_B | INB_RS }, { .mode = INMODE_SOLITARE, .crouch = 1, .actionsHeld = A(INACT_CROUCH) } },
    { "custom: gadget := X plus reload := X (one press, both edges)",
      { [INACT_GADGET] = INB_X, [INACT_RELOAD] = INB_X, [INACT_START] = INB_START }, 1,
      { .ctx = PLAY, .buttons = INB_X },
      { .mode = INMODE_SOLITARE, .reloadEdge = 1, .gadgetEdge = 1, .actionsHeld = A(INACT_RELOAD) | A(INACT_GADGET) } },
    { "custom: start := Y",
      { [INACT_START] = INB_Y }, 1,
      { .ctx = PLAY, .buttons = INB_Y | INB_START }, { .mode = INMODE_SOLITARE, .start = 1, .actionsHeld = A(INACT_START) } },
    { "custom with NaturalPitch off = honey roles",
      { [INACT_START] = INB_START }, 0,
      { .ctx = PLAY, .lx = 20000 }, { .mode = INMODE_HONEY, .moveX = 45, .moveActive = 1 } },
};

static const char *cmdDiff(const InputCommands *g, const InputCommands *w)
{
#define F(f) if (g->f != w->f) return #f;
    F(mode) F(moveX) F(moveY) F(moveActive) F(lookX) F(lookY) F(lookRawActive)
    F(moveFwd) F(moveBack) F(strafeL) F(strafeR) F(pitchUp) F(pitchDown)
    F(slowStrafeL) F(slowStrafeR) F(zoomIn) F(zoomOut) F(fire) F(aim) F(fireDigital) F(aimDigital)
    F(useEdge) F(reloadEdge) F(nextWeapon) F(prevWeapon) F(gadgetEdge) F(crouch)
    F(menuAccept) F(menuCancel) F(start) F(dpadUp) F(dpadDown) F(dpadLeft) F(dpadRight) F(actionsHeld)
    F(n64Raw) F(n64Buttons)
#undef F
    return NULL;
}

static void check(const char *name, const InputSnapshot *in, const InputCommands *want)
{
    InputStrategyState st;
    InputCommands got;
    inputStrategyStateReset(&st);
    g_inputStyleEnhanced.map(in, &st, &got);
    const char *bad = cmdDiff(&got, want);
    ge_test_checks++;
    if (bad) GE_FAIL_("row \"%s\": field %s differs", name, bad);
}

static void testTable(void)
{
    for (size_t i = 0; i < sizeof rows / sizeof rows[0]; i++) check(rows[i].name, &rows[i].in, &rows[i].want);
    for (size_t i = 0; i < sizeof customRows / sizeof customRows[0]; i++) {
        InputSnapshot in = customRows[i].in;
        memset(&in.cfg, 0, sizeof in.cfg);
        in.cfg.deadzoneL = in.cfg.deadzoneR = 3000;
        in.cfg.lookSensX = in.cfg.lookSensY = in.cfg.aimLookSens = 100;
        in.cfg.triggerPct = 25;
        memcpy(in.cfg.bind, customRows[i].bind, sizeof in.cfg.bind);
        in.cfg.naturalPitch = inputEnhancedSolitare(INPRESET_CUSTOM, customRows[i].nat);
        in.cfg.stickSwap = inputEnhancedSwap(INPRESET_CUSTOM);
        check(customRows[i].name, &in, &customRows[i].want);
    }
    printf("table rows: %zu + %zu custom\n", sizeof rows / sizeof rows[0], sizeof customRows / sizeof customRows[0]);
}

/* The roles each preset resolves to (the adapter's stickSolitare / stickSwap). */
static void testRoles(void)
{
    for (int nat = 0; nat <= 1; nat++) {
        GE_CHECK_EQ_INT(inputEnhancedSolitare(INPRESET_JINX, nat), nat);         /* 1.1 Jinx and Custom follow NaturalPitch */
        GE_CHECK_EQ_INT(inputEnhancedSolitare(INPRESET_CUSTOM, nat), nat);
        GE_CHECK_EQ_INT(inputEnhancedSolitare(INPRESET_CHRISTMAS, nat), 0);      /* forced HONEY */
        GE_CHECK_EQ_INT(inputEnhancedSolitare(INPRESET_FROST, nat), 0);
        GE_CHECK_EQ_INT(inputEnhancedSolitare(INPRESET_ELEKTRA, nat), 1);        /* forced SOLITARE */
    }
    GE_CHECK_EQ_INT(inputEnhancedSolitare(INPRESET_JINX, 7), 1);
    GE_CHECK_EQ_INT(inputEnhancedSwap(INPRESET_JINX), 0);
    GE_CHECK_EQ_INT(inputEnhancedSwap(INPRESET_CHRISTMAS), 0);
    GE_CHECK_EQ_INT(inputEnhancedSwap(INPRESET_FROST), 1);
    GE_CHECK_EQ_INT(inputEnhancedSwap(INPRESET_CUSTOM), 0);
    GE_CHECK_EQ_INT(inputEnhancedSwap(INPRESET_ELEKTRA), 1);
}

/* The look low-pass works the same through the shared roles. */
static void testSmoothing(void)
{
    InputSnapshot in = { .cfg = JINX, .ctx = PLAY, .rx = 20000 };
    in.cfg.lookSmooth = 5;
    InputStrategyState st;
    InputCommands c;
    inputStrategyStateReset(&st);
    g_inputStyleEnhanced.map(&in, &st, &c);
    GE_CHECK_EQ_INT(c.lookX, 25);
    g_inputStyleEnhanced.map(&in, &st, &c);
    GE_CHECK_EQ_INT(c.lookX, 36);
}

static int halve(void *user, int v, int sens) { (void)user; (void)sens; return v / 2; }

static void testAimScale(void)
{
    InputSnapshot in = { .cfg = JINX, .ctx = { .facePlayable = 1, .inStage = 1, .padDrives = 1, .aiming = 1 },
                         .rx = 20000, .aimScale = halve };
    InputStrategyState st;
    InputCommands c;
    inputStrategyStateReset(&st);
    g_inputStyleEnhanced.map(&in, &st, &c);
    GE_CHECK_EQ_INT(c.lookX, 22);
    /* no assists in Enhanced: aiming does not turn the D-pad / left stick into zoom / slow strafe */
    in.buttons = INB_DUP;
    in.lx = -20000;
    in.ctx.scopedWeapon = 1;
    g_inputStyleEnhanced.map(&in, &st, &c);
    GE_CHECK_EQ_INT(c.zoomIn, 0);
    GE_CHECK_EQ_INT(c.slowStrafeL, 0);
    GE_CHECK_EQ_INT(c.dpadUp, 1);
}

/* The bridge's new pieces: the digital fire / aim go out AFTER the southpaw swap. */
static void testBridge(void)
{
    N64BridgeOpts o = { 0, 1 };
    InputCommands c;
    memset(&c, 0, sizeof c);
    c.fire = 1;
    GE_CHECK_EQ_INT(n64BridgeButtons(0, &c, &o), GE_CONT_R);                   /* a trigger swaps */
    memset(&c, 0, sizeof c);
    c.fireDigital = 1;
    GE_CHECK_EQ_INT(n64BridgeButtons(0, &c, &o), GE_CONT_G);                   /* a digital fire does not */
    memset(&c, 0, sizeof c);
    c.aimDigital = 1;
    GE_CHECK_EQ_INT(n64BridgeButtons(0, &c, &o), GE_CONT_R);
    memset(&c, 0, sizeof c);
    c.fire = 1; c.aimDigital = 1;
    GE_CHECK_EQ_INT(n64BridgeButtons(0, &c, &o), GE_CONT_R);                   /* swapped G -> R, plus R */
    memset(&c, 0, sizeof c);
    c.prevWeapon = 1; c.nextWeapon = 1;
    GE_CHECK_EQ_INT(n64BridgeButtons(0, &c, &o), GE_CONT_A | GE_CONT_G);       /* the backward-cycle idiom is not swapped */
}

/* ---- differential: a verbatim copy of the mapping this strategy replaced ---- */
#define RAW_COUNT 15
#define PS_TRIGGERS_ (INB_LT | INB_RT)
typedef struct { int facePlayable, slotNative, southpaw, tank; } RefCtx;
typedef struct { unsigned button; int crouch, gadgetEdge; unsigned actHeld; } RefOut;

static unsigned refActHeld(const uint32_t *mask, unsigned raw)
{
    unsigned h = 0;
    for (int a = 0; a < INACT_COUNT; a++) if (raw & mask[a]) h |= 1u << a;
    return h;
}

/* padMapPure (non-orig branch), input.c at the parent of step 4: names changed (PS_* -> INB_*, PA_* -> INACT_*), nothing else. */
static void refMapPure(const uint32_t *mask, unsigned raw, unsigned actPrev, unsigned buttonIn, const RefCtx *c, RefOut *o)
{
    unsigned button = buttonIn;
    unsigned held = refActHeld(mask, raw);
    if (raw & mask[INACT_FIRE] & PS_TRIGGERS_) button |= GE_CONT_G;
    if (raw & mask[INACT_AIM] & PS_TRIGGERS_)  button |= GE_CONT_R;
    if (c->southpaw) {
        unsigned t = button;
        button = (t & ~(GE_CONT_G | GE_CONT_R))
               | ((t & GE_CONT_G) ? GE_CONT_R : 0)
               | ((t & GE_CONT_R) ? GE_CONT_G : 0);
    }
    o->crouch = 0;
    o->gadgetEdge = 0;
    unsigned edge = held & ~actPrev;
    if (c->facePlayable) {
        if (held & (1u << INACT_CROUCH)) o->crouch = 1;
        if (c->slotNative && (edge & ((1u << INACT_USE) | (1u << INACT_RELOAD)))) button |= GE_CONT_B;
        if (edge & (1u << INACT_GADGET)) o->gadgetEdge = 1;
        if (edge & (1u << INACT_NEXTWPN)) button |= GE_CONT_A;
        if (edge & (1u << INACT_PREVWPN)) button |= GE_CONT_A | GE_CONT_G;
        if (raw & mask[INACT_AIM] & ~PS_TRIGGERS_)  button |= GE_CONT_R;
        if (raw & mask[INACT_FIRE] & ~PS_TRIGGERS_) button |= GE_CONT_G;
    } else if (c->tank) {
        if (edge & ((1u << INACT_NEXTWPN) | (1u << INACT_PREVWPN))) button |= GE_CONT_A;
        if (raw & mask[INACT_AIM] & ~PS_TRIGGERS_)  button |= GE_CONT_R;
        if (raw & mask[INACT_FIRE] & ~PS_TRIGGERS_) button |= GE_CONT_G;
    } else {
        if (raw & (INB_A | INB_X)) button |= GE_CONT_A;
        if (raw & (INB_B | INB_Y)) button |= GE_CONT_B;
    }
    if (held & (1u << INACT_START)) button |= GE_CONT_START;
    if (raw & INB_DUP)    button |= GE_CONT_UP;
    if (raw & INB_DDOWN)  button |= GE_CONT_DOWN;
    if (raw & INB_DLEFT)  button |= GE_CONT_LEFT;
    if (raw & INB_DRIGHT) button |= GE_CONT_RIGHT;
    o->button = button;
    o->actHeld = held;
}

static uint32_t s_rng = 12345u;
static uint32_t rnd(void) { s_rng = s_rng * 1664525u + 1013904223u; return s_rng >> 8; }

/* A random bind table the way a Custom seat can produce one: 0..3 sources per action, Start never empty. */
static void randomBinds(uint32_t *bind)
{
    for (int a = 0; a < INACT_COUNT; a++) {
        bind[a] = 0;
        int n = (int)(rnd() % 4);
        for (int k = 0; k < n; k++) bind[a] |= 1u << (rnd() % RAW_COUNT);
    }
    if (!bind[INACT_START]) bind[INACT_START] = INB_START;
}

static void runDifferential(const uint32_t *bind, int samples, int *mism)
{
    static const unsigned kIn[5] = { 0, GE_CONT_G, GE_CONT_R, GE_CONT_G | GE_CONT_R, 0xffffu };
    for (int i = 0; i < samples; i++) {
        unsigned raw = rnd() & ((1u << RAW_COUNT) - 1);
        if (i & 1) raw &= rnd();   /* sparser sets too */
        unsigned prev = rnd() & ((1u << INACT_COUNT) - 1);
        if (i & 2) prev &= rnd();
        int cx = (int)(rnd() % 3);   /* playable / tank / menu */
        RefCtx rc = { cx == 0, (int)(rnd() & 1), (int)(rnd() & 1), cx == 1 };
        unsigned bin = kIn[rnd() % 5];

        RefOut ro;
        refMapPure(bind, raw, prev, bin, &rc, &ro);

        InputSnapshot s;
        memset(&s, 0, sizeof s);   /* sticks idle: no C bits from them */
        s.buttons = raw;
        s.prevActions = prev;
        s.ctx.facePlayable = rc.facePlayable;
        s.ctx.tank = rc.tank;
        s.ctx.menu = (cx == 2);
        s.ctx.padDrives = 1;
        s.cfg.triggerPct = 25;
        s.cfg.deadzoneL = s.cfg.deadzoneR = 3000;
        s.cfg.lookSensX = s.cfg.lookSensY = s.cfg.aimLookSens = 100;
        s.cfg.naturalPitch = (int)(rnd() & 1);
        memcpy(s.cfg.bind, bind, sizeof s.cfg.bind);
        InputStrategyState st;
        InputCommands c;
        inputStrategyStateReset(&st);
        g_inputStyleEnhanced.map(&s, &st, &c);
        N64BridgeOpts bo = { rc.slotNative, rc.southpaw };
        unsigned got = n64BridgeButtons(bin, &c, &bo);
        ge_test_checks++;
        if (got != ro.button || c.crouch != ro.crouch || c.gadgetEdge != ro.gadgetEdge || c.actionsHeld != ro.actHeld) {
            if ((*mism)++ < 5)
                printf("MISMATCH raw=%04x prev=%x ctx=%d bo=%d/%d in=%04x new=%04x/%d/%d/%x old=%04x/%d/%d/%x\n",
                       raw, prev, cx, bo.slotNative, bo.southpaw, bin, got, c.crouch, c.gadgetEdge, c.actionsHeld,
                       ro.button, ro.crouch, ro.gadgetEdge, ro.actHeld);
        }
    }
}

static void testDifferential(void)
{
    int mism = 0;
    uint32_t jinx[INACT_COUNT] = JINX_BIND;
    runDifferential(jinx, 300000, &mism);
    for (int t = 0; t < 40; t++) {
        uint32_t b[INACT_COUNT];
        randomBinds(b);
        runDifferential(b, 20000, &mism);
    }
    GE_CHECK_EQ_INT(mism, 0);
    printf("differential: 1.1M samples vs the old padMapPure, %d mismatches\n", mism);
}

/* GameDirect (#78): the left stick's analog walk / strafe, in the game's "safe" scale (5 off, 75 at the rim). */
static int directOf(InputSnapshot in, int *walk, int *strafe, InputCommands *cmd)
{
    InputStrategyState st;
    inputStrategyStateReset(&st);
    g_inputStyleEnhanced.map(&in, &st, cmd);
    return inputDirectMoveDecide(&in.ctx, cmd, walk, strafe);
}

static void testDirect(void)
{
    int w = 99, s = 99;
    InputCommands c;
    GE_CHECK(directOf((InputSnapshot){ .cfg = JINX, .ctx = PLAY, .ly = -32767 }, &w, &s, &c));   /* full tilt forward */
    GE_CHECK_EQ_INT(w, 75); GE_CHECK_EQ_INT(s, 0);
    GE_CHECK_EQ_INT(c.walk, 80);
    GE_CHECK(directOf((InputSnapshot){ .cfg = JINX, .ctx = PLAY, .ly = 32767, .lx = 32767 }, &w, &s, &c));   /* back + right */
    GE_CHECK_EQ_INT(w, -75); GE_CHECK_EQ_INT(s, 75);
    GE_CHECK(directOf((InputSnapshot){ .cfg = JINX, .ctx = PLAY, .lx = -20000, .ly = -20000 }, &w, &s, &c));   /* diagonal: each axis alone */
    GE_CHECK_EQ_INT(w, 40); GE_CHECK_EQ_INT(s, -40);
    GE_CHECK(directOf((InputSnapshot){ .cfg = JINX, .ctx = PLAY, .lx = 3000, .ly = -2999 }, &w, &s, &c));   /* inside the deadzone: direct, no motion */
    GE_CHECK_EQ_INT(w, 0); GE_CHECK_EQ_INT(s, 0);
    GE_CHECK(directOf((InputSnapshot){ .cfg = JINX, .ctx = PLAY, .ly = -4000 }, &w, &s, &c));   /* scaled 2: under the game's own 5 deadzone */
    GE_CHECK_EQ_INT(w, 0);
    GE_CHECK(directOf((InputSnapshot){ .cfg = JINX, .ctx = WHEEL, .ly = -20000, .rx = 20000 }, &w, &s, &c));   /* wheel open: still walks */
    GE_CHECK_EQ_INT(w, 40);
    GE_CHECK(!directOf((InputSnapshot){ .cfg = JINX, .ctx = { .facePlayable = 1, .inStage = 1, .padDrives = 1, .aiming = 1 }, .ly = -20000 }, &w, &s, &c));
    GE_CHECK(!directOf((InputSnapshot){ .cfg = JINX, .ctx = TANK, .ly = -20000 }, &w, &s, &c));
    GE_CHECK(!directOf((InputSnapshot){ .cfg = JINX, .ctx = MENU, .ly = -20000 }, &w, &s, &c));
    GE_CHECK(!directOf((InputSnapshot){ .cfg = JINX, .ctx = { .facePlayable = 1, .inStage = 1, .padDrives = 1, .dead = 1 }, .ly = -20000 }, &w, &s, &c));
    GE_CHECK(!directOf((InputSnapshot){ .cfg = JINX, .ctx = { .inStage = 1, .padDrives = 1 }, .ly = -20000 }, &w, &s, &c));   /* watch / pause: not playable */
    GE_CHECK(!directOf((InputSnapshot){ .cfg = JINX, .ctx = { .facePlayable = 1, .inStage = 1 }, .ly = -20000 }, &w, &s, &c));   /* keyboard plays the seat */
    GE_CHECK(!directOf((InputSnapshot){ .cfg = CHRISTMAS, .ctx = PLAY, .ly = -20000 }, &w, &s, &c));   /* HONEY: already the analog stick */
    /* the bridge leaves the digital moves out once direct, and keeps them otherwise (the assists read moveFwd either way) */
    directOf((InputSnapshot){ .cfg = JINX, .ctx = PLAY, .ly = -20000, .lx = 20000 }, &w, &s, &c);
    GE_CHECK_EQ_INT(c.moveFwd, 1);
    N64BridgeOpts bo = { 0 };
    GE_CHECK_EQ_INT(n64BridgeButtons(0, &c, &bo) & (GE_CONT_E | GE_CONT_F), GE_CONT_E | GE_CONT_F);
    c.direct = 1;
    GE_CHECK_EQ_INT(n64BridgeButtons(0, &c, &bo) & (GE_CONT_E | GE_CONT_D | GE_CONT_C | GE_CONT_F), 0);
}

int main(void)
{
    GE_TEST_BEGIN("input: Enhanced styles");
    testTable();
    testRoles();
    testSmoothing();
    testAimScale();
    testBridge();
    testDifferential();
    testDirect();
    return GE_TEST_END();
}
