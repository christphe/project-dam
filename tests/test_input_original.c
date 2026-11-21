/*
 * The Original control style (port/src/input/style_original.c): the strict
 * virtual N64 pad (D643). A table of {snapshot, expected commands} (default N64
 * binds, custom binds, the accurate vs the rescaled stick feel, the right stick
 * as digital C), the N64 bridge's raw pathway, and a differential test against
 * a verbatim copy of the orig branch of the padMapPure this strategy replaced.
 * Pure: no game core, no platform.
 */
#include "ge_test.h"
#include "input/input_types.h"
#include "input/style_original.h"
#include "input/n64bridge.h"

/* The default N64 binds (input.c kN64Btn, Nintendo Switch Online convention). */
#define N64_DEFAULT { [INNB_A] = INB_A, [INNB_B] = INB_X, [INNB_Z] = INB_RT, [INNB_R] = INB_LT | INB_RB, \
                      [INNB_L] = INB_LB, [INNB_START] = INB_START, [INNB_DU] = INB_DUP, [INNB_DD] = INB_DDOWN, \
                      [INNB_DL] = INB_DLEFT, [INNB_DR] = INB_DRIGHT }
#define CFG(feel) { .deadzoneL = 3000, .deadzoneR = 3000, .lookSensX = 100, .lookSensY = 100, .aimLookSens = 100, \
                    .triggerPct = 25, .n64StickFeel = (feel), .n64Bind = N64_DEFAULT }
#define ACC CFG(1)   /* accurate N64 stick response (the default) */
#define RES CFG(0)   /* the Modern deadzone / rescale */
#define PLAY  { .facePlayable = 1, .inStage = 1, .padDrives = 1 }
#define MENU  { .menu = 1, .padDrives = 1 }
#define TANK  { .tank = 1, .inStage = 1, .padDrives = 1 }

typedef struct { const char *name; InputSnapshot in; InputCommands want; } Row;

#define OUT(mode_, bits) .mode = (mode_), .n64Raw = 1, .n64Buttons = (bits)

static const Row rows[] = {
    /* ---- the default N64 table: one source at a time ---- */
    { "idle", { .cfg = ACC, .ctx = PLAY }, { OUT(INMODE_HONEY, 0) } },
    { "A = N64 A", { .cfg = ACC, .ctx = PLAY, .buttons = INB_A }, { OUT(INMODE_HONEY, GE_CONT_A) } },
    { "B (the letter) is nothing", { .cfg = ACC, .ctx = PLAY, .buttons = INB_B }, { OUT(INMODE_HONEY, 0) } },
    { "X = N64 B", { .cfg = ACC, .ctx = PLAY, .buttons = INB_X }, { OUT(INMODE_HONEY, GE_CONT_B) } },
    { "Y is nothing", { .cfg = ACC, .ctx = PLAY, .buttons = INB_Y }, { OUT(INMODE_HONEY, 0) } },
    { "LB = N64 L", { .cfg = ACC, .ctx = PLAY, .buttons = INB_LB }, { OUT(INMODE_HONEY, GE_CONT_L) } },
    { "RB = N64 R", { .cfg = ACC, .ctx = PLAY, .buttons = INB_RB }, { OUT(INMODE_HONEY, GE_CONT_R) } },
    { "stick clicks are nothing (no crouch)", { .cfg = ACC, .ctx = PLAY, .buttons = INB_LS | INB_RS }, { OUT(INMODE_HONEY, 0) } },
    { "start", { .cfg = ACC, .ctx = PLAY, .buttons = INB_START }, { OUT(INMODE_HONEY, GE_CONT_START) } },
    { "d-pad = N64 d-pad", { .cfg = ACC, .ctx = PLAY, .buttons = INB_DUP | INB_DDOWN | INB_DLEFT | INB_DRIGHT },
      { OUT(INMODE_HONEY, GE_CONT_UP | GE_CONT_DOWN | GE_CONT_LEFT | GE_CONT_RIGHT) } },
    { "RT above the trigger point = Z", { .cfg = ACC, .ctx = PLAY, .triggerR = 8176 }, { OUT(INMODE_HONEY, GE_CONT_G) } },
    { "RT at the point is not", { .cfg = ACC, .ctx = PLAY, .triggerR = 8175 }, { OUT(INMODE_HONEY, 0) } },
    { "LT = N64 R", { .cfg = ACC, .ctx = PLAY, .triggerL = 20000 }, { OUT(INMODE_HONEY, GE_CONT_R) } },
    { "a latched RT tap = Z", { .cfg = ACC, .ctx = PLAY, .buttons = INB_RT }, { OUT(INMODE_HONEY, GE_CONT_G) } },
    { "trigger point 90 %: 20000 is not a press",
      { .cfg = { .triggerPct = 90, .n64Bind = N64_DEFAULT }, .ctx = PLAY, .triggerR = 20000 }, { OUT(INMODE_HONEY, 0) } },
    { "RT + A together", { .cfg = ACC, .ctx = PLAY, .buttons = INB_A, .triggerR = 20000 }, { OUT(INMODE_HONEY, GE_CONT_G | GE_CONT_A) } },

    /* ---- identical in every context: no port action, no menu faces ---- */
    { "menu: A = N64 A", { .cfg = ACC, .ctx = MENU, .buttons = INB_A }, { OUT(INMODE_MENU, GE_CONT_A) } },
    { "menu: the letter B is nothing (no fixed cancel)", { .cfg = ACC, .ctx = MENU, .buttons = INB_B }, { OUT(INMODE_MENU, 0) } },
    { "menu: Y is nothing", { .cfg = ACC, .ctx = MENU, .buttons = INB_Y }, { OUT(INMODE_MENU, 0) } },
    { "menu: X = N64 B", { .cfg = ACC, .ctx = MENU, .buttons = INB_X }, { OUT(INMODE_MENU, GE_CONT_B) } },
    { "tank: A = N64 A, X = N64 B", { .cfg = ACC, .ctx = TANK, .buttons = INB_A | INB_X },
      { OUT(INMODE_HONEY, GE_CONT_A | GE_CONT_B) } },
    { "nintendo pad: same letters (A = N64 A)", { .cfg = ACC, .ctx = PLAY, .family = INFAM_NINTENDO, .buttons = INB_A },
      { OUT(INMODE_HONEY, GE_CONT_A) } },
    { "held across polls: no edges, no state", { .cfg = ACC, .ctx = PLAY, .buttons = INB_A, .prevActions = 0xffff },
      { OUT(INMODE_HONEY, GE_CONT_A) } },

    /* ---- custom N64 binds ---- */
    { "custom: N64 B := y,b",
      { .cfg = { .triggerPct = 25, .n64Bind = { [INNB_A] = INB_A, [INNB_B] = INB_Y | INB_B, [INNB_START] = INB_START } },
        .ctx = PLAY, .buttons = INB_Y }, { OUT(INMODE_HONEY, GE_CONT_B) } },
    { "custom: the old B source no longer gives N64 B",
      { .cfg = { .triggerPct = 25, .n64Bind = { [INNB_A] = INB_A, [INNB_B] = INB_Y | INB_B, [INNB_START] = INB_START } },
        .ctx = PLAY, .buttons = INB_X }, { OUT(INMODE_HONEY, 0) } },
    { "custom: C-up := R3 (a C source bound)",
      { .cfg = { .triggerPct = 25, .n64Bind = { [INNB_A] = INB_A, [INNB_B] = INB_X, [INNB_CU] = INB_RS, [INNB_START] = INB_START } },
        .ctx = PLAY, .buttons = INB_RS }, { OUT(INMODE_HONEY, GE_CONT_E) } },
    { "custom: one source on two N64 buttons",
      { .cfg = { .triggerPct = 25, .n64Bind = { [INNB_Z] = INB_RB, [INNB_R] = INB_RB, [INNB_A] = INB_A, [INNB_START] = INB_START } },
        .ctx = PLAY, .buttons = INB_RB }, { OUT(INMODE_HONEY, GE_CONT_G | GE_CONT_R) } },
    { "custom: two sources on one N64 button",
      { .cfg = { .triggerPct = 25, .n64Bind = { [INNB_A] = INB_A | INB_Y, [INNB_START] = INB_START } },
        .ctx = PLAY, .buttons = INB_Y }, { OUT(INMODE_HONEY, GE_CONT_A) } },
    { "custom: C-left, C-right, C-down",
      { .cfg = { .triggerPct = 25, .n64Bind = { [INNB_CL] = INB_Y, [INNB_CR] = INB_B, [INNB_CD] = INB_LS, [INNB_A] = INB_A, [INNB_START] = INB_START } },
        .ctx = PLAY, .buttons = INB_Y | INB_B | INB_LS }, { OUT(INMODE_HONEY, GE_CONT_C | GE_CONT_F | GE_CONT_D) } },
    { "custom: d-pad rebound to the face buttons",
      { .cfg = { .triggerPct = 25, .n64Bind = { [INNB_DU] = INB_Y, [INNB_A] = INB_A, [INNB_START] = INB_START } },
        .ctx = PLAY, .buttons = INB_Y | INB_DUP }, { OUT(INMODE_HONEY, GE_CONT_UP) } },
    { "custom: an unbound table gives nothing (the adapter's guards re-add A / B / Start)",
      { .cfg = { .triggerPct = 25 }, .ctx = PLAY, .buttons = INB_A | INB_B | INB_START }, { OUT(INMODE_HONEY, 0) } },

    /* ---- the left stick, accurate N64 response: 32767 -> 80 linear, 2500 centre ---- */
    { "accurate: inside the centre (2499)", { .cfg = ACC, .ctx = PLAY, .lx = 2499, .ly = -2499 }, { OUT(INMODE_HONEY, 0) } },
    { "accurate: just outside (2500)", { .cfg = ACC, .ctx = PLAY, .lx = 2500 },
      { OUT(INMODE_HONEY, 0), .moveX = 6, .moveActive = 1 } },
    { "accurate: half travel 16384 = 40", { .cfg = ACC, .ctx = PLAY, .lx = 16384, .ly = -16384 },
      { OUT(INMODE_HONEY, 0), .moveX = 40, .moveY = 40, .moveActive = 1 } },
    { "accurate: full right = 80, full down = -80", { .cfg = ACC, .ctx = PLAY, .lx = 32767, .ly = 32767 },
      { OUT(INMODE_HONEY, 0), .moveX = 80, .moveY = -80, .moveActive = 1 } },
    { "accurate: -32768 clamps to -80", { .cfg = ACC, .ctx = PLAY, .lx = -32768 },
      { OUT(INMODE_HONEY, 0), .moveX = -80, .moveActive = 1 } },
    { "accurate: the deadzone setting is ignored",
      { .cfg = { .deadzoneL = 9000, .triggerPct = 25, .n64StickFeel = 1, .n64Bind = N64_DEFAULT }, .ctx = PLAY, .lx = 5000 },
      { OUT(INMODE_HONEY, 0), .moveX = 12, .moveActive = 1 } },

    /* ---- the left stick, the Modern deadzone / rescale (feel off): 20000 -> 45 at deadzone 3000 ---- */
    { "rescaled: 20000 -> 45", { .cfg = RES, .ctx = PLAY, .lx = 20000, .ly = -20000 },
      { OUT(INMODE_HONEY, 0), .moveX = 45, .moveY = 45, .moveActive = 1 } },
    { "rescaled: inside the deadzone (2999)", { .cfg = RES, .ctx = PLAY, .lx = 2999 }, { OUT(INMODE_HONEY, 0) } },
    { "rescaled: deadzone 9000 hides 8000",
      { .cfg = { .deadzoneL = 9000, .triggerPct = 25, .n64StickFeel = 0, .n64Bind = N64_DEFAULT }, .ctx = PLAY, .lx = 8000 },
      { OUT(INMODE_HONEY, 0) } },
    { "rescaled: invert Y is not applied in Original (the game's own look setting does it)",
      { .cfg = { .deadzoneL = 3000, .lookInvertY = 1, .triggerPct = 25, .n64StickFeel = 0, .n64Bind = N64_DEFAULT },
        .ctx = PLAY, .ly = -20000 },
      { OUT(INMODE_HONEY, 0), .moveY = 45, .moveActive = 1 } },
    { "menu: the stick navigates with the same feel (accurate)", { .cfg = ACC, .ctx = MENU, .lx = -16384, .ly = -16384 },
      { OUT(INMODE_MENU, 0), .moveX = -40, .moveY = 40 } },
    { "menu: the stick navigates with the same feel (rescaled)", { .cfg = RES, .ctx = MENU, .lx = 20000, .ly = 20000 },
      { OUT(INMODE_MENU, 0), .moveX = 45, .moveY = -45 } },
    { "menu: the right stick gives no C buttons", { .cfg = ACC, .ctx = MENU, .rx = 30000, .ry = -30000 },
      { OUT(INMODE_MENU, 0) } },

    /* ---- the right stick: fixed digital C ---- */
    { "right stick right = C-right", { .cfg = ACC, .ctx = PLAY, .rx = 16385 }, { OUT(INMODE_HONEY, GE_CONT_F) } },
    { "right stick left = C-left", { .cfg = ACC, .ctx = PLAY, .rx = -16385 }, { OUT(INMODE_HONEY, GE_CONT_C) } },
    { "right stick up = C-up", { .cfg = ACC, .ctx = PLAY, .ry = -16385 }, { OUT(INMODE_HONEY, GE_CONT_E) } },
    { "right stick down = C-down", { .cfg = ACC, .ctx = PLAY, .ry = 16385 }, { OUT(INMODE_HONEY, GE_CONT_D) } },
    { "right stick at the threshold (16384) is still", { .cfg = ACC, .ctx = PLAY, .rx = 16384, .ry = -16384 },
      { OUT(INMODE_HONEY, 0) } },
    { "right stick + a bound C source",
      { .cfg = { .triggerPct = 25, .n64Bind = { [INNB_CU] = INB_Y, [INNB_A] = INB_A, [INNB_START] = INB_START } },
        .ctx = PLAY, .buttons = INB_Y, .rx = 20000 }, { OUT(INMODE_HONEY, GE_CONT_E | GE_CONT_F) } },
    { "right stick diagonal", { .cfg = ACC, .ctx = PLAY, .rx = -20000, .ry = 20000 }, { OUT(INMODE_HONEY, GE_CONT_C | GE_CONT_D) } },
    { "an open wheel owns the right stick", { .cfg = ACC, .ctx = { .facePlayable = 1, .inStage = 1, .padDrives = 1, .wheelOpen = 1 },
                                              .rx = 20000, .ry = -20000, .lx = 16384 },
      { OUT(INMODE_HONEY, 0), .moveX = 40, .moveActive = 1 } },
    { "the right stick never moves the analog stick or looks", { .cfg = RES, .ctx = PLAY, .rx = 30000, .ry = 30000 },
      { OUT(INMODE_HONEY, GE_CONT_F | GE_CONT_D) } },

    /* ---- the game's style decides the roles: the strategy ignores them ---- */
    { "naturalPitch set (the game's style is SOLITARE): still HONEY roles, no swap",
      { .cfg = { .deadzoneL = 3000, .deadzoneR = 3000, .lookSensX = 100, .lookSensY = 100, .triggerPct = 25, .naturalPitch = 1,
                 .stickSwap = 1, .n64StickFeel = 1, .n64Bind = N64_DEFAULT },
        .ctx = PLAY, .lx = 16384, .ly = -16384, .rx = 20000 },
      { OUT(INMODE_HONEY, GE_CONT_F), .moveX = 40, .moveY = 40, .moveActive = 1 } },
    { "look sensitivity / smoothing / aim assists do not apply",
      { .cfg = { .deadzoneL = 3000, .deadzoneR = 3000, .lookSensX = 300, .lookSmooth = 10, .triggerPct = 25, .n64StickFeel = 1,
                 .n64Bind = N64_DEFAULT },
        .ctx = { .facePlayable = 1, .inStage = 1, .padDrives = 1, .aiming = 1, .scopedWeapon = 1 }, .buttons = INB_DUP, .lx = -20000 },
      { OUT(INMODE_HONEY, GE_CONT_UP), .moveX = -48, .moveActive = 1 } },
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

static void testTable(void)
{
    for (size_t i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        InputStrategyState st;
        InputCommands got;
        inputStrategyStateReset(&st);
        g_inputStyleOriginal.map(&rows[i].in, &st, &got);
        const char *bad = cmdDiff(&got, &rows[i].want);
        ge_test_checks++;
        if (bad) GE_FAIL_("row \"%s\": field %s differs", rows[i].name, bad);
    }
    printf("table rows: %zu\n", sizeof rows / sizeof rows[0]);
}

static void testStickFunction(void)
{
    GE_CHECK_EQ_INT(inputN64StickAxis(0), 0);
    GE_CHECK_EQ_INT(inputN64StickAxis(2499), 0);
    GE_CHECK_EQ_INT(inputN64StickAxis(-2499), 0);
    GE_CHECK_EQ_INT(inputN64StickAxis(32767), 80);
    GE_CHECK_EQ_INT(inputN64StickAxis(-32768), -80);
    GE_CHECK_EQ_INT(inputN64StickAxis(16384), 40);
    GE_CHECK_EQ_INT(inputN64StickAxis(5000), 12);
    GE_CHECK_EQ_INT(inputN64StickAxis(-20000), -48);   /* truncates toward zero */
}

static void testBitTable(void)
{
    GE_CHECK_EQ_INT(inputN64ButtonBit(INNB_A), GE_CONT_A);
    GE_CHECK_EQ_INT(inputN64ButtonBit(INNB_B), GE_CONT_B);
    GE_CHECK_EQ_INT(inputN64ButtonBit(INNB_Z), GE_CONT_G);
    GE_CHECK_EQ_INT(inputN64ButtonBit(INNB_R), GE_CONT_R);
    GE_CHECK_EQ_INT(inputN64ButtonBit(INNB_L), GE_CONT_L);
    GE_CHECK_EQ_INT(inputN64ButtonBit(INNB_CU), GE_CONT_E);
    GE_CHECK_EQ_INT(inputN64ButtonBit(INNB_CD), GE_CONT_D);
    GE_CHECK_EQ_INT(inputN64ButtonBit(INNB_CL), GE_CONT_C);
    GE_CHECK_EQ_INT(inputN64ButtonBit(INNB_CR), GE_CONT_F);
    GE_CHECK_EQ_INT(inputN64ButtonBit(INNB_START), GE_CONT_START);
    GE_CHECK_EQ_INT(inputN64ButtonBit(INNB_DU), GE_CONT_UP);
    GE_CHECK_EQ_INT(inputN64ButtonBit(INNB_DD), GE_CONT_DOWN);
    GE_CHECK_EQ_INT(inputN64ButtonBit(INNB_DL), GE_CONT_LEFT);
    GE_CHECK_EQ_INT(inputN64ButtonBit(INNB_DR), GE_CONT_RIGHT);
    GE_CHECK_EQ_INT(inputN64ButtonBit(INNB_COUNT), 0);
}

/* The bridge's raw pathway: the pad's whole contribution, no southpaw swap, nothing else. */
static void testBridge(void)
{
    N64BridgeOpts o = { 1, 1 };   /* slotNative and southpaw are both ignored in Original */
    InputCommands c;
    memset(&c, 0, sizeof c);
    c.n64Raw = 1;
    GE_CHECK_EQ_INT(n64BridgeButtons(0, &c, &o), 0);
    c.n64Buttons = GE_CONT_G | GE_CONT_A;
    GE_CHECK_EQ_INT(n64BridgeButtons(0, &c, &o), GE_CONT_G | GE_CONT_A);              /* no swap */
    GE_CHECK_EQ_INT(n64BridgeButtons(GE_CONT_R, &c, &o), GE_CONT_G | GE_CONT_A | GE_CONT_R);   /* the keyboard's bits stay, unswapped */
    c.useEdge = 1; c.nextWeapon = 1; c.menuAccept = 1;   /* a raw pad would never set these; if it did they stay ignored */
    GE_CHECK_EQ_INT(n64BridgeButtons(0, &c, &o), GE_CONT_G | GE_CONT_A);
}

/* ---- differential: the orig branch of the padMapPure this strategy replaced ---- */
static uint32_t s_rng = 987654321u;
static uint32_t rnd(void) { s_rng = s_rng * 1664525u + 1013904223u; return s_rng >> 8; }

static void testDifferential(void)
{
    int mism = 0;
    for (int t = 0; t < 30; t++) {
        uint32_t bind[INNB_COUNT];
        const uint32_t def[INNB_COUNT] = N64_DEFAULT;
        for (int n = 0; n < INNB_COUNT; n++) {
            bind[n] = t ? 0 : def[n];
            for (int k = 0; t && k < (int)(rnd() % 4); k++) bind[n] |= 1u << (rnd() % 15);
        }
        for (int i = 0; i < 20000; i++) {
            unsigned raw = rnd() & 0x7fff;
            if (i & 1) raw &= rnd();
            /* old: for n: if (raw & n64[n]) button |= kN64Btn[n].bit; plus the right stick's C bits */
            unsigned want = 0;
            for (int n = 0; n < INNB_COUNT; n++)
                if (raw & bind[n]) want |= inputN64ButtonBit(n);
            int rx = (int)(rnd() % 65536) - 32768, ry = (int)(rnd() % 65536) - 32768;
            if (rx >  0x4000) want |= GE_CONT_F;
            if (rx < -0x4000) want |= GE_CONT_C;
            if (ry >  0x4000) want |= GE_CONT_D;
            if (ry < -0x4000) want |= GE_CONT_E;
            InputSnapshot s;
            memset(&s, 0, sizeof s);
            s.buttons = raw;
            s.rx = rx; s.ry = ry;
            s.ctx.padDrives = 1;
            s.ctx.menu = (int)(rnd() & 1);
            memcpy(s.cfg.n64Bind, bind, sizeof bind);
            s.cfg.n64StickFeel = 1;
            s.cfg.triggerPct = 25;
            InputStrategyState st;
            InputCommands c;
            inputStrategyStateReset(&st);
            g_inputStyleOriginal.map(&s, &st, &c);
            N64BridgeOpts bo = { (int)(rnd() & 1), (int)(rnd() & 1) };
            unsigned got = n64BridgeButtons(0, &c, &bo);
            if (s.ctx.menu) {   /* in a menu the old code ran the menu stick branch: no right-stick C bits */
                want = 0;
                for (int n = 0; n < INNB_COUNT; n++)
                    if (raw & bind[n]) want |= inputN64ButtonBit(n);
            }
            ge_test_checks++;
            if (got != want || c.actionsHeld != 0 || c.crouch || c.gadgetEdge) {
                if (mism++ < 5) printf("MISMATCH raw=%04x menu=%d rx=%d ry=%d got=%04x want=%04x\n", raw, s.ctx.menu, rx, ry, got, want);
            }
        }
    }
    GE_CHECK_EQ_INT(mism, 0);
    printf("differential: 600k samples vs the old orig branch, %d mismatches\n", mism);
}

int main(void)
{
    GE_TEST_BEGIN("input: Original style");
    testTable();
    testStickFunction();
    testBitTable();
    testBridge();
    testDifferential();
    return GE_TEST_END();
}
