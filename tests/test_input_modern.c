/*
 * The Modern control style (port/src/input/style_modern.c): a table of
 * {snapshot, expected commands}, plus the commands -> N64 bridge. Pure: links
 * only the strategy sources, no game core, no platform.
 *
 * Stick numbers: raw SDL units in, N64 stick units out. With deadzone 3000,
 * a deflection of 20000 is (20000-3000) * 80 / (32767-3000) = 45.
 */
#include "ge_test.h"
#include "input/input_types.h"
#include "input/style_modern.h"
#include "input/n64bridge.h"

/* deadzoneL, deadzoneR, lookSensX, lookSensY, aimLookSens, lookSmooth, lookInvertY, triggerPct, naturalPitch */
#define CFG0 { 3000, 3000, 100, 100, 100, 0, 0, 25, 1 }
#define PLAY { .facePlayable = 1, .inStage = 1, .padDrives = 1 }
#define MENU { .menu = 1, .padDrives = 1 }
#define TANK { .tank = 1, .inStage = 1, .padDrives = 1 }
#define AIMING { .facePlayable = 1, .inStage = 1, .padDrives = 1, .aiming = 1 }
#define A(x) INACT_BIT(x)
#define WHEEL_ { .facePlayable = 1, .inStage = 1, .padDrives = 1, .wheelOpen = 1 }

typedef struct { const char *name; InputSnapshot in; InputCommands want; } Row;

static int halve(void *user, int v, int sens) { (void)user; (void)sens; return v / 2; }

static const Row rows[] = {
    /* ---- face buttons, by position ---- */
    { "idle", { .cfg = CFG0, .ctx = PLAY }, { .mode = INMODE_SOLITARE } },
    { "xbox A = use", { .cfg = CFG0, .ctx = PLAY, .buttons = INB_A },
      { .mode = INMODE_SOLITARE, .useEdge = 1, .actionsHeld = A(INACT_USE) } },
    { "xbox X = reload", { .cfg = CFG0, .ctx = PLAY, .buttons = INB_X },
      { .mode = INMODE_SOLITARE, .reloadEdge = 1, .actionsHeld = A(INACT_RELOAD) } },
    { "xbox Y = next weapon", { .cfg = CFG0, .ctx = PLAY, .buttons = INB_Y },
      { .mode = INMODE_SOLITARE, .nextWeapon = 1, .actionsHeld = A(INACT_NEXTWPN) } },
    { "xbox B is free", { .cfg = CFG0, .ctx = PLAY, .buttons = INB_B }, { .mode = INMODE_SOLITARE } },
    { "playstation = xbox letters", { .cfg = CFG0, .ctx = PLAY, .family = INFAM_PS, .buttons = INB_A },
      { .mode = INMODE_SOLITARE, .useEdge = 1, .actionsHeld = A(INACT_USE) } },
    { "nintendo B (bottom) = use", { .cfg = CFG0, .ctx = PLAY, .family = INFAM_NINTENDO, .buttons = INB_B },
      { .mode = INMODE_SOLITARE, .useEdge = 1, .actionsHeld = A(INACT_USE) } },
    { "nintendo Y (left) = reload", { .cfg = CFG0, .ctx = PLAY, .family = INFAM_NINTENDO, .buttons = INB_Y },
      { .mode = INMODE_SOLITARE, .reloadEdge = 1, .actionsHeld = A(INACT_RELOAD) } },
    { "nintendo X (top) = next weapon", { .cfg = CFG0, .ctx = PLAY, .family = INFAM_NINTENDO, .buttons = INB_X },
      { .mode = INMODE_SOLITARE, .nextWeapon = 1, .actionsHeld = A(INACT_NEXTWPN) } },
    { "nintendo A (right) is free", { .cfg = CFG0, .ctx = PLAY, .family = INFAM_NINTENDO, .buttons = INB_A },
      { .mode = INMODE_SOLITARE } },
    { "use held across polls: no new edge",
      { .cfg = CFG0, .ctx = PLAY, .buttons = INB_A, .prevActions = A(INACT_USE) },
      { .mode = INMODE_SOLITARE, .actionsHeld = A(INACT_USE) } },
    { "use released: held cleared", { .cfg = CFG0, .ctx = PLAY, .prevActions = A(INACT_USE) },
      { .mode = INMODE_SOLITARE } },
    { "reload + next weapon together",
      { .cfg = CFG0, .ctx = PLAY, .buttons = INB_X | INB_Y },
      { .mode = INMODE_SOLITARE, .reloadEdge = 1, .nextWeapon = 1, .actionsHeld = A(INACT_RELOAD) | A(INACT_NEXTWPN) } },
    { "crouch is R3", { .cfg = CFG0, .ctx = PLAY, .buttons = INB_RS },
      { .mode = INMODE_SOLITARE, .crouch = 1, .actionsHeld = A(INACT_CROUCH) } },
    { "L3 is not crouch", { .cfg = CFG0, .ctx = PLAY, .buttons = INB_LS }, { .mode = INMODE_SOLITARE } },
    { "start", { .cfg = CFG0, .ctx = PLAY, .buttons = INB_START },
      { .mode = INMODE_SOLITARE, .start = 1, .actionsHeld = A(INACT_START) } },
    { "d-pad passes through",
      { .cfg = CFG0, .ctx = PLAY, .buttons = INB_DUP | INB_DLEFT },
      { .mode = INMODE_SOLITARE, .dpadUp = 1, .dpadLeft = 1 } },

    /* ---- triggers vs the trigger point (25 % = 8175) ---- */
    { "RT above the point fires", { .cfg = CFG0, .ctx = PLAY, .triggerR = 8176 },
      { .mode = INMODE_SOLITARE, .fire = 1, .actionsHeld = A(INACT_FIRE) } },
    { "RT at the point does not", { .cfg = CFG0, .ctx = PLAY, .triggerR = 8175 }, { .mode = INMODE_SOLITARE } },
    { "LT above the point aims", { .cfg = CFG0, .ctx = PLAY, .triggerL = 20000 },
      { .mode = INMODE_SOLITARE, .aim = 1, .actionsHeld = A(INACT_AIM) } },
    { "trigger point 50 %: 12000 is not a press",
      { .cfg = { 3000, 3000, 100, 100, 100, 0, 0, 50, 1 }, .ctx = PLAY, .triggerR = 12000 },
      { .mode = INMODE_SOLITARE } },
    { "trigger point 50 %: 17000 is",
      { .cfg = { 3000, 3000, 100, 100, 100, 0, 0, 50, 1 }, .ctx = PLAY, .triggerR = 17000 },
      { .mode = INMODE_SOLITARE, .fire = 1, .actionsHeld = A(INACT_FIRE) } },
    { "a latched RT tap fires with the trigger at rest", { .cfg = CFG0, .ctx = PLAY, .buttons = INB_RT },
      { .mode = INMODE_SOLITARE, .fire = 1, .actionsHeld = A(INACT_FIRE) } },
    { "LB is not aim (it opens the weapon wheel)", { .cfg = CFG0, .ctx = PLAY, .buttons = INB_LB },
      { .mode = INMODE_SOLITARE } },
    { "RB is not an action (it opens the gadget wheel)", { .cfg = CFG0, .ctx = PLAY, .buttons = INB_RB },
      { .mode = INMODE_SOLITARE } },

    /* ---- sticks: SOLITARE roles ---- */
    { "left stick up = move forward", { .cfg = CFG0, .ctx = PLAY, .ly = -20000 },
      { .mode = INMODE_SOLITARE, .moveFwd = 1 } },
    { "left stick down = move back", { .cfg = CFG0, .ctx = PLAY, .ly = 20000 },
      { .mode = INMODE_SOLITARE, .moveBack = 1 } },
    { "left stick at the digital threshold is still", { .cfg = CFG0, .ctx = PLAY, .ly = -16384, .lx = 16384 },
      { .mode = INMODE_SOLITARE } },
    { "left stick left = strafe", { .cfg = CFG0, .ctx = PLAY, .lx = -16385 },
      { .mode = INMODE_SOLITARE, .strafeL = 1 } },
    { "left stick right = strafe", { .cfg = CFG0, .ctx = PLAY, .lx = 20000 },
      { .mode = INMODE_SOLITARE, .strafeR = 1 } },
    { "right stick = look (up = positive)", { .cfg = CFG0, .ctx = PLAY, .rx = 20000, .ry = -20000 },
      { .mode = INMODE_SOLITARE, .lookX = 45, .lookY = 45, .lookRawActive = 1 } },
    { "right stick down-left", { .cfg = CFG0, .ctx = PLAY, .rx = -20000, .ry = 20000 },
      { .mode = INMODE_SOLITARE, .lookX = -45, .lookY = -45, .lookRawActive = 1 } },
    { "look deadzone: inside", { .cfg = CFG0, .ctx = PLAY, .rx = 2999, .ry = -3000 },
      { .mode = INMODE_SOLITARE } },
    { "look deadzone: just outside", { .cfg = CFG0, .ctx = PLAY, .rx = 5000 },
      { .mode = INMODE_SOLITARE, .lookX = 5, .lookRawActive = 1 } },
    { "look deadzone 5000 (right): 5000 is still inside",
      { .cfg = { 3000, 5000, 100, 100, 100, 0, 0, 25, 1 }, .ctx = PLAY, .rx = 5000 },
      { .mode = INMODE_SOLITARE } },
    { "look deadzone 5000 (right): 6000 gives 2",
      { .cfg = { 3000, 5000, 100, 100, 100, 0, 0, 25, 1 }, .ctx = PLAY, .rx = 6000 },
      { .mode = INMODE_SOLITARE, .lookX = 2, .lookRawActive = 1 } },
    { "the left deadzone does not touch look",
      { .cfg = { 9000, 3000, 100, 100, 100, 0, 0, 25, 1 }, .ctx = PLAY, .rx = 20000 },
      { .mode = INMODE_SOLITARE, .lookX = 45, .lookRawActive = 1 } },
    { "invert Y", { .cfg = { 3000, 3000, 100, 100, 100, 0, 1, 25, 1 }, .ctx = PLAY, .rx = 20000, .ry = -20000 },
      { .mode = INMODE_SOLITARE, .lookX = 45, .lookY = -45, .lookRawActive = 1 } },
    { "sensitivity X 200 % clamps to 80",
      { .cfg = { 3000, 3000, 200, 100, 100, 0, 0, 25, 1 }, .ctx = PLAY, .rx = 20000 },
      { .mode = INMODE_SOLITARE, .lookX = 80, .lookRawActive = 1 } },
    { "sensitivity Y 50 %",
      { .cfg = { 3000, 3000, 100, 50, 100, 0, 0, 25, 1 }, .ctx = PLAY, .ry = -20000 },
      { .mode = INMODE_SOLITARE, .lookY = 22, .lookRawActive = 1 } },
    { "aim scaling applies only while aiming",
      { .cfg = CFG0, .ctx = PLAY, .rx = 20000, .aimScale = halve },
      { .mode = INMODE_SOLITARE, .lookX = 45, .lookRawActive = 1 } },
    { "aim scaling while aiming",
      { .cfg = CFG0, .ctx = AIMING, .rx = 20000, .ry = -20000, .aimScale = halve },
      { .mode = INMODE_SOLITARE, .lookX = 22, .lookY = 22, .lookRawActive = 1 } },

    /* ---- wheels: the right stick is neutral while one is open ---- */
    { "weapon wheel open: look is neutral",
      { .cfg = CFG0, .ctx = { .facePlayable = 1, .inStage = 1, .padDrives = 1, .wheelOpen = 1 },
        .buttons = INB_LB, .rx = 20000, .ry = -20000 },
      { .mode = INMODE_SOLITARE } },
    { "wheel open: the left stick still walks, buttons still act",
      { .cfg = CFG0, .ctx = { .facePlayable = 1, .inStage = 1, .padDrives = 1, .wheelOpen = 1 },
        .buttons = INB_RB | INB_A, .ly = -20000, .rx = 20000 },
      { .mode = INMODE_SOLITARE, .moveFwd = 1, .useEdge = 1, .actionsHeld = A(INACT_USE) } },

    /* ---- D-pad zoom: only while aiming a scoped weapon ---- */
    { "scoped + aiming: d-pad up zooms in",
      { .cfg = CFG0, .ctx = { .facePlayable = 1, .inStage = 1, .padDrives = 1, .aiming = 1, .scopedWeapon = 1 },
        .buttons = INB_DUP },
      { .mode = INMODE_SOLITARE, .zoomIn = 1, .dpadUp = 1 } },
    { "scoped + aiming: d-pad down zooms out",
      { .cfg = CFG0, .ctx = { .facePlayable = 1, .inStage = 1, .padDrives = 1, .aiming = 1, .scopedWeapon = 1 },
        .buttons = INB_DDOWN },
      { .mode = INMODE_SOLITARE, .zoomOut = 1, .dpadDown = 1 } },
    { "aiming, not scoped: no zoom",
      { .cfg = CFG0, .ctx = AIMING, .buttons = INB_DUP | INB_DDOWN },
      { .mode = INMODE_SOLITARE, .dpadUp = 1, .dpadDown = 1 } },
    { "scoped, not aiming: no zoom",
      { .cfg = CFG0, .ctx = { .facePlayable = 1, .inStage = 1, .padDrives = 1, .scopedWeapon = 1 },
        .buttons = INB_DUP },
      { .mode = INMODE_SOLITARE, .dpadUp = 1 } },
    { "scoped + aiming but dead: no zoom",
      { .cfg = CFG0, .ctx = { .facePlayable = 1, .inStage = 1, .padDrives = 1, .aiming = 1, .scopedWeapon = 1, .dead = 1 },
        .buttons = INB_DUP },
      { .mode = INMODE_SOLITARE, .dpadUp = 1 } },
    { "scoped + aiming outside a running stage: no zoom",
      { .cfg = CFG0, .ctx = { .facePlayable = 1, .padDrives = 1, .aiming = 1, .scopedWeapon = 1 }, .buttons = INB_DUP },
      { .mode = INMODE_SOLITARE, .dpadUp = 1 } },

    /* ---- slow strafe: only while aiming ---- */
    { "aiming: left stick past 12000 strafes slowly (left)",
      { .cfg = CFG0, .ctx = AIMING, .lx = -12001 },
      { .mode = INMODE_SOLITARE, .slowStrafeL = 1 } },
    { "aiming: left stick past 12000 strafes slowly (right)",
      { .cfg = CFG0, .ctx = AIMING, .lx = 12001 },
      { .mode = INMODE_SOLITARE, .slowStrafeR = 1 } },
    { "aiming: 12000 exactly does nothing", { .cfg = CFG0, .ctx = AIMING, .lx = 12000 },
      { .mode = INMODE_SOLITARE } },
    { "aiming: a full push is the fast strafe too",
      { .cfg = CFG0, .ctx = AIMING, .lx = -20000 },
      { .mode = INMODE_SOLITARE, .strafeL = 1, .slowStrafeL = 1 } },
    { "not aiming: no slow strafe", { .cfg = CFG0, .ctx = PLAY, .lx = -12001 }, { .mode = INMODE_SOLITARE } },
    { "aiming, dead: no slow strafe",
      { .cfg = CFG0, .ctx = { .facePlayable = 1, .inStage = 1, .padDrives = 1, .aiming = 1, .dead = 1 }, .lx = -12001 },
      { .mode = INMODE_SOLITARE } },

    /* ---- menus ---- */
    { "menu: left stick navigates (up = positive)", { .cfg = CFG0, .ctx = MENU, .lx = 20000, .ly = -20000 },
      { .mode = INMODE_MENU, .moveX = 45, .moveY = 45 } },
    { "menu: the right stick is ignored", { .cfg = CFG0, .ctx = MENU, .rx = 20000, .ry = 20000 },
      { .mode = INMODE_MENU } },
    { "menu: A accepts", { .cfg = CFG0, .ctx = MENU, .buttons = INB_A },
      { .mode = INMODE_MENU, .menuAccept = 1, .actionsHeld = A(INACT_USE) } },
    { "menu: X accepts", { .cfg = CFG0, .ctx = MENU, .buttons = INB_X },
      { .mode = INMODE_MENU, .menuAccept = 1, .actionsHeld = A(INACT_RELOAD) } },
    { "menu: B cancels", { .cfg = CFG0, .ctx = MENU, .buttons = INB_B },
      { .mode = INMODE_MENU, .menuCancel = 1 } },
    { "menu: Y cancels", { .cfg = CFG0, .ctx = MENU, .buttons = INB_Y },
      { .mode = INMODE_MENU, .menuCancel = 1, .actionsHeld = A(INACT_NEXTWPN) } },
    { "menu: the letters stay on a nintendo pad", { .cfg = CFG0, .ctx = MENU, .family = INFAM_NINTENDO, .buttons = INB_A },
      { .mode = INMODE_MENU, .menuAccept = 1 } },   /* (nintendo: SDL A is not the bottom button, so not "use") */
    { "menu: no use / reload / crouch",
      { .cfg = CFG0, .ctx = MENU, .buttons = INB_RS | INB_A, .prevActions = 0 },
      { .mode = INMODE_MENU, .menuAccept = 1, .actionsHeld = A(INACT_USE) | A(INACT_CROUCH) } },
    { "menu: triggers still read", { .cfg = CFG0, .ctx = MENU, .triggerR = 30000 },
      { .mode = INMODE_MENU, .fire = 1, .actionsHeld = A(INACT_FIRE) } },
    { "menu: start and d-pad", { .cfg = CFG0, .ctx = MENU, .buttons = INB_START | INB_DDOWN },
      { .mode = INMODE_MENU, .start = 1, .dpadDown = 1, .actionsHeld = A(INACT_START) } },

    /* ---- the tank ---- */
    { "tank: Y (top) changes weapon", { .cfg = CFG0, .ctx = TANK, .buttons = INB_Y },
      { .mode = INMODE_SOLITARE, .nextWeapon = 1, .actionsHeld = A(INACT_NEXTWPN) } },
    { "tank: A is not accept (use goes through the locked path)", { .cfg = CFG0, .ctx = TANK, .buttons = INB_A },
      { .mode = INMODE_SOLITARE, .actionsHeld = A(INACT_USE) } },

    /* ---- HONEY roles (Input.NaturalPitch off) ---- */
    { "honey: left stick is analog", { .cfg = { 3000, 3000, 100, 100, 100, 0, 0, 25, 0 }, .ctx = PLAY, .lx = 20000, .ly = -20000 },
      { .mode = INMODE_HONEY, .moveX = 45, .moveY = 45, .moveActive = 1 } },
    { "honey: invert flips the left stick Y",
      { .cfg = { 3000, 3000, 100, 100, 100, 0, 1, 25, 0 }, .ctx = PLAY, .ly = -20000 },
      { .mode = INMODE_HONEY, .moveY = -45, .moveActive = 1 } },
    { "honey: right stick is digital",
      { .cfg = { 3000, 3000, 100, 100, 100, 0, 0, 25, 0 }, .ctx = PLAY, .rx = 20000, .ry = -20000 },
      { .mode = INMODE_HONEY, .strafeR = 1, .pitchUp = 1 } },
    { "honey: right stick left / down",
      { .cfg = { 3000, 3000, 100, 100, 100, 0, 0, 25, 0 }, .ctx = PLAY, .rx = -20000, .ry = 20000 },
      { .mode = INMODE_HONEY, .strafeL = 1, .pitchDown = 1 } },
    { "honey: wheel open neutralises the right stick",
      { .cfg = { 3000, 3000, 100, 100, 100, 0, 0, 25, 0 },
        .ctx = { .facePlayable = 1, .inStage = 1, .padDrives = 1, .wheelOpen = 1 }, .rx = 20000, .ry = -20000 },
      { .mode = INMODE_HONEY } },

    /* ---- the keyboard plays the seat: only the aim assists read the pad ---- */
    { "keyboard seat: pad faces, triggers and sticks are ignored",
      { .cfg = CFG0, .ctx = { .inStage = 1 }, .buttons = INB_A | INB_START, .triggerR = 30000, .rx = 20000, .ly = -20000 },
      { 0 } },
    { "keyboard seat: the aim assists still read the pad",
      { .cfg = CFG0, .ctx = { .inStage = 1, .aiming = 1, .scopedWeapon = 1 }, .buttons = INB_DUP, .lx = -20000 },
      { .zoomIn = 1, .slowStrafeL = 1 } },
};

static const char *cmdDiff(const InputCommands *g, const InputCommands *w)
{
#define F(f) if (g->f != w->f) return #f;
    F(mode) F(moveX) F(moveY) F(moveActive) F(lookX) F(lookY) F(lookRawActive)
    F(moveFwd) F(moveBack) F(strafeL) F(strafeR) F(pitchUp) F(pitchDown)
    F(slowStrafeL) F(slowStrafeR) F(zoomIn) F(zoomOut) F(fire) F(aim)
    F(useEdge) F(reloadEdge) F(nextWeapon) F(crouch)
    F(menuAccept) F(menuCancel) F(start) F(dpadUp) F(dpadDown) F(dpadLeft) F(dpadRight) F(actionsHeld)
#undef F
    return NULL;
}

static void testTable(void)
{
    for (size_t i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        InputStrategyState st;
        InputCommands got;
        inputStrategyStateReset(&st);
        g_inputStyleModern.map(&rows[i].in, &st, &got);
        const char *bad = cmdDiff(&got, &rows[i].want);
        ge_test_checks++;
        if (bad) GE_FAIL_("row \"%s\": field %s differs", rows[i].name, bad);
    }
    printf("table rows: %zu\n", sizeof rows / sizeof rows[0]);
}

/* The look low-pass is the one state the strategy keeps. */
static void testSmoothing(void)
{
    InputSnapshot in = { .cfg = { 3000, 3000, 100, 100, 100, 5, 0, 25, 1 }, .ctx = PLAY, .rx = 20000 };
    InputStrategyState st;
    InputCommands c;
    inputStrategyStateReset(&st);
    g_inputStyleModern.map(&in, &st, &c);   /* a = 0.45: 45 * 0.55 = 24.75 */
    GE_CHECK_EQ_INT(c.lookX, 25);
    g_inputStyleModern.map(&in, &st, &c);   /* 24.75 * 0.45 + 24.75 = 35.89 */
    GE_CHECK_EQ_INT(c.lookX, 36);
    GE_CHECK_EQ_INT(c.lookRawActive, 1);
    in.rx = 0;                              /* released: the raw stick is idle at once, the output decays */
    g_inputStyleModern.map(&in, &st, &c);
    GE_CHECK_EQ_INT(c.lookRawActive, 0);
    GE_CHECK(c.lookX > 0 && c.lookX < 36);
    inputStrategyStateReset(&st);           /* re-plug: no stale value */
    in.rx = 20000;
    g_inputStyleModern.map(&in, &st, &c);
    GE_CHECK_EQ_INT(c.lookX, 25);
}

static void testBridge(void)
{
    N64BridgeOpts o = { 0, 0 };
    InputCommands c;

    memset(&c, 0, sizeof c);
    GE_CHECK_EQ_INT(n64BridgeButtons(0, &c, &o), 0);
    GE_CHECK_EQ_INT(n64BridgeButtons(GE_CONT_L, &c, &o), GE_CONT_L);   /* what the keyboard put in stays */

    c.fire = 1;
    GE_CHECK_EQ_INT(n64BridgeButtons(0, &c, &o), GE_CONT_G);
    c.aim = 1;
    GE_CHECK_EQ_INT(n64BridgeButtons(0, &c, &o), GE_CONT_G | GE_CONT_R);
    c.aim = 0;
    o.southpaw = 1;
    GE_CHECK_EQ_INT(n64BridgeButtons(0, &c, &o), GE_CONT_R);           /* fire and grenade swap */
    GE_CHECK_EQ_INT(n64BridgeButtons(GE_CONT_G, &c, &o), GE_CONT_R);   /* the swap acts on the keyboard's bits too */
    o.southpaw = 0;

    memset(&c, 0, sizeof c);
    c.useEdge = 1;
    GE_CHECK_EQ_INT(n64BridgeButtons(0, &c, &o), 0);                    /* dedicated use: no native bit */
    o.slotNative = 1;
    GE_CHECK_EQ_INT(n64BridgeButtons(0, &c, &o), GE_CONT_B);            /* native slot: B */
    c.useEdge = 0; c.reloadEdge = 1;
    GE_CHECK_EQ_INT(n64BridgeButtons(0, &c, &o), GE_CONT_B);
    o.slotNative = 0;

    memset(&c, 0, sizeof c);
    c.nextWeapon = 1;
    GE_CHECK_EQ_INT(n64BridgeButtons(0, &c, &o), GE_CONT_A);

    memset(&c, 0, sizeof c);
    c.menuAccept = 1; c.menuCancel = 1; c.start = 1;
    GE_CHECK_EQ_INT(n64BridgeButtons(0, &c, &o), GE_CONT_A | GE_CONT_B | GE_CONT_START);

    memset(&c, 0, sizeof c);
    c.dpadUp = 1; c.dpadDown = 1; c.dpadLeft = 1; c.dpadRight = 1;
    GE_CHECK_EQ_INT(n64BridgeButtons(0, &c, &o), GE_CONT_UP | GE_CONT_DOWN | GE_CONT_LEFT | GE_CONT_RIGHT);

    memset(&c, 0, sizeof c);
    c.moveFwd = 1; c.strafeL = 1;
    GE_CHECK_EQ_INT(n64BridgeButtons(0, &c, &o), GE_CONT_E | GE_CONT_C);
    memset(&c, 0, sizeof c);
    c.moveBack = 1; c.strafeR = 1;
    GE_CHECK_EQ_INT(n64BridgeButtons(0, &c, &o), GE_CONT_D | GE_CONT_F);
    memset(&c, 0, sizeof c);
    c.pitchUp = 1; c.pitchDown = 1;
    GE_CHECK_EQ_INT(n64BridgeButtons(0, &c, &o), GE_CONT_E | GE_CONT_D);

    /* the assists are their own step, after the crouch code */
    memset(&c, 0, sizeof c);
    c.zoomIn = 1; c.slowStrafeR = 1;
    GE_CHECK_EQ_INT(n64BridgeButtons(0, &c, &o), 0);
    GE_CHECK_EQ_INT(n64BridgeAssist(0, &c), GE_CONT_E | GE_CONT_F);
    memset(&c, 0, sizeof c);
    c.zoomOut = 1; c.slowStrafeL = 1;
    GE_CHECK_EQ_INT(n64BridgeAssist(GE_CONT_G, &c), GE_CONT_G | GE_CONT_D | GE_CONT_C);
}

/* GameDirect (#78): the left stick's analog walk / strafe, in the game's "safe" scale (5 off, 75 at the rim). */
static int directOf(InputSnapshot in, int *walk, int *strafe, InputCommands *cmd)
{
    InputStrategyState st;
    inputStrategyStateReset(&st);
    g_inputStyleModern.map(&in, &st, cmd);
    return inputDirectMoveDecide(&in.ctx, cmd, walk, strafe);
}

static void testDirect(void)
{
    int w = 99, s = 99;
    InputCommands c;
    GE_CHECK(directOf((InputSnapshot){ .cfg = CFG0, .ctx = PLAY, .ly = -32767 }, &w, &s, &c));   /* full tilt forward */
    GE_CHECK_EQ_INT(w, 75); GE_CHECK_EQ_INT(s, 0);
    GE_CHECK_EQ_INT(c.walk, 80);
    GE_CHECK(directOf((InputSnapshot){ .cfg = CFG0, .ctx = PLAY, .ly = 32767, .lx = 32767 }, &w, &s, &c));   /* back + right */
    GE_CHECK_EQ_INT(w, -75); GE_CHECK_EQ_INT(s, 75);
    GE_CHECK(directOf((InputSnapshot){ .cfg = CFG0, .ctx = PLAY, .lx = -20000, .ly = -20000 }, &w, &s, &c));   /* diagonal: each axis alone */
    GE_CHECK_EQ_INT(w, 40); GE_CHECK_EQ_INT(s, -40);
    GE_CHECK(directOf((InputSnapshot){ .cfg = CFG0, .ctx = PLAY, .lx = 3000, .ly = -2999 }, &w, &s, &c));   /* inside the deadzone: direct, no motion */
    GE_CHECK_EQ_INT(w, 0); GE_CHECK_EQ_INT(s, 0);
    GE_CHECK(directOf((InputSnapshot){ .cfg = CFG0, .ctx = PLAY, .ly = -4000 }, &w, &s, &c));   /* scaled 2: under the game's own 5 deadzone */
    GE_CHECK_EQ_INT(w, 0);
    GE_CHECK(directOf((InputSnapshot){ .cfg = CFG0, .ctx = WHEEL_, .ly = -20000, .rx = 20000 }, &w, &s, &c));   /* wheel open: still walks */
    GE_CHECK_EQ_INT(w, 40);
    GE_CHECK(!directOf((InputSnapshot){ .cfg = CFG0, .ctx = { .facePlayable = 1, .inStage = 1, .padDrives = 1, .aiming = 1 }, .ly = -20000 }, &w, &s, &c));
    GE_CHECK(!directOf((InputSnapshot){ .cfg = CFG0, .ctx = TANK, .ly = -20000 }, &w, &s, &c));
    GE_CHECK(!directOf((InputSnapshot){ .cfg = CFG0, .ctx = MENU, .ly = -20000 }, &w, &s, &c));
    GE_CHECK(!directOf((InputSnapshot){ .cfg = CFG0, .ctx = { .facePlayable = 1, .inStage = 1, .padDrives = 1, .dead = 1 }, .ly = -20000 }, &w, &s, &c));
    GE_CHECK(!directOf((InputSnapshot){ .cfg = CFG0, .ctx = { .inStage = 1, .padDrives = 1 }, .ly = -20000 }, &w, &s, &c));   /* watch / pause: not playable */
    GE_CHECK(!directOf((InputSnapshot){ .cfg = CFG0, .ctx = { .facePlayable = 1, .inStage = 1 }, .ly = -20000 }, &w, &s, &c));   /* keyboard plays the seat */
    GE_CHECK(!directOf((InputSnapshot){ .cfg = { 3000, 3000, 100, 100, 100, 0, 0, 25, 0 }, .ctx = PLAY, .ly = -20000 }, &w, &s, &c));   /* HONEY: already the analog stick */
    /* the bridge leaves the digital moves out once direct, and keeps them otherwise (the assists read moveFwd either way) */
    directOf((InputSnapshot){ .cfg = CFG0, .ctx = PLAY, .ly = -20000, .lx = 20000 }, &w, &s, &c);
    GE_CHECK_EQ_INT(c.moveFwd, 1);
    N64BridgeOpts bo = { 0 };
    GE_CHECK_EQ_INT(n64BridgeButtons(0, &c, &bo) & (GE_CONT_E | GE_CONT_F), GE_CONT_E | GE_CONT_F);
    c.direct = 1;
    GE_CHECK_EQ_INT(n64BridgeButtons(0, &c, &bo) & (GE_CONT_E | GE_CONT_D | GE_CONT_C | GE_CONT_F), 0);
}

/* GameDirect, the keyboard part: the keys add full scale to the stick, each axis clamped to the rim. */
static int keysOf(int padWalk, int padStrafe, int fwd, int back, int left, int right, int *strafe)
{
    int w = padWalk;
    *strafe = padStrafe;
    inputDirectMoveAddKeys(fwd, back, left, right, &w, strafe);
    return w;
}

static void testDirectKeys(void)
{
    int s;
    GE_CHECK_EQ_INT(INPUT_DIRECT_MAX, 75);
    GE_CHECK_EQ_INT(keysOf(0, 0, 1, 0, 0, 0, &s), 75);  GE_CHECK_EQ_INT(s, 0);     /* W, stick at rest: the bug of #78 */
    GE_CHECK_EQ_INT(keysOf(0, 0, 0, 1, 1, 0, &s), -75); GE_CHECK_EQ_INT(s, -75);   /* S + A */
    GE_CHECK_EQ_INT(keysOf(0, 0, 0, 0, 0, 1, &s), 0);   GE_CHECK_EQ_INT(s, 75);    /* D */
    GE_CHECK_EQ_INT(keysOf(40, 0, 1, 0, 0, 0, &s), 75);                            /* W + half stick: no speed boost */
    GE_CHECK_EQ_INT(keysOf(-75, 0, 1, 0, 0, 0, &s), 0);                            /* W + stick full back: cancel */
    GE_CHECK_EQ_INT(keysOf(0, 30, 1, 0, 0, 0, &s), 75); GE_CHECK_EQ_INT(s, 30);    /* W + stick right: diagonal */
    GE_CHECK_EQ_INT(keysOf(0, 0, 1, 1, 1, 1, &s), 0);   GE_CHECK_EQ_INT(s, 0);     /* opposite keys cancel */
    GE_CHECK_EQ_INT(keysOf(-40, 20, 0, 0, 0, 0, &s), -40); GE_CHECK_EQ_INT(s, 20); /* no key: the stick alone */
    GE_CHECK_EQ_INT(keysOf(0, -60, 0, 0, 1, 0, &s), 0); GE_CHECK_EQ_INT(s, -75);   /* A + stick left: clamped */
}

int main(void)
{
    GE_TEST_BEGIN("input: Modern style");
    testTable();
    testSmoothing();
    testBridge();
    testDirect();
    testDirectKeys();
    return GE_TEST_END();
}
