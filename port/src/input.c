/*
 * Input: SDL2 keyboard/mouse/gamepad -> N64 controller state (Phase 3, D118).
 *
 * The game reads controllers via osContStartReadData / osContGetReadData
 * (src/joy.c). libultra.c's SI section calls into this module once per
 * controller poll: inputUpdate() refreshes SDL + accumulates the mouse-aim
 * delta, then inputComputePad() produces the 16-bit button mask + stick for
 * one controller. This is the single source of controller state on PC --
 * the old contSnapshotFromKeyboard() now delegates here.
 *
 * Deliberately NOT a full port of pd_port/port/src/input.c (1551 lines): GE's
 * menu/config code never calls that module's VK/bind-string API, so this is a
 * focused implementation matching port/include/input.h plus the two helpers
 * libultra.c needs.
 *
 * ------------------------------------------------------------------------
 * BINDING SCHEME (documented in docs/internals.md sec F "D118")
 *
 * GE's default "1.1" control style: analog stick = move/strafe, the four
 * C-buttons = aim/turn/look (DIGITAL on N64), R = aim mode, Z = fire.
 *
 * Keyboard + mouse (controller 0, migrated PC defaults):
 *   W/S/A/D or arrows .. movement / strafe
 *   mouse motion ....... look (mode-aware -- see MOUSE-LOOK below)
 *   left mouse ......... fire; right mouse / LShift .. aim
 *   Q .................. next weapon; E .. dedicated use
 *   R .................. dedicated reload; LCtrl .. independent crouch
 *   Enter / Tab ........ Start
 *
 * Xbox / SDL_GameController (controller 0 merges pad 0 with kbd/mouse;
 * pads 1-3 -> controllers 1-3):
 *   left stick ........ analog stick   (move / strafe)
 *   right stick ....... C-buttons      (digital, 50% threshold -- aim)
 *   right trigger ..... Z trigger      (fire)
 *   left trigger ...... R trigger      (aim mode)
 *   A ................. use/interact (in-game); accept (menus)
 *   X ................. reload (in-game); accept (menus)
 *   B ................. cycle inventory gadgets (in-game); cancel (menus)
 *   Y ................. next weapon (in-game); cancel (menus)
 *   stick clicks ...... crouch (in-game)
 *   LB ................ aim (alternate to LT); RB reserved (no HD assets)
 *   D-pad ............ N64 D-pad
 *   Start ............ Start; Back opens the PC options overlay
 *
 * MOUSE-LOOK (mode-aware, no src/ changes)
 *   GE's aim model (bondview2.c bondviewProcessInput / MoveData) is
 *   mode-dependent, so the mouse->pad mapping is too:
 *
 *   - Hipfire (!insightaimmode): yaw = analog stick-X ("natural turn");
 *     pitch = DIGITAL C-up/C-down (stick-Y here is move fwd/back and does
 *     not pitch). Mouse Y emits a C-button on polls where it moved.
 *   - Aim mode (R / RMB held): yaw AND pitch are analog -- the stick pushed
 *     past +/-60 gives proportional (stick-60)/10 aim speed. Mouse X/Y push
 *     the stick into the 61..80 band. We emit NO C-buttons in this mode
 *     because there they mean crouch / lean / zoom (this was D118c:
 *     aim + look-down -> crouch).
 *
 *   "aim button held" is read from our own RMB/LShift state -- exact for
 *   the default hold-to-aim scheme; a toggle-aim scheme would need a read
 *   of inputSlotPlayer()->insightaimmode (still no logic change).
 *
 *   GE's native pitch is inverted ("flight" style: C-up -> look down). We
 *   hide that: mouse-down looks down by default, MouseInvertY flips it.
 *
 *   Tunable via ge007.ini [Input]: MouseAimSpeed (aim mode), MouseTurnSpeed
 *   (hipfire yaw), MouseInvertY, MouseEnabled. Residual: in hipfire, yaw
 *   (analog) and pitch (digital) still feel different (D118a) -- a fully
 *   analog hipfire pitch needs an #ifdef PORT hook in bondview.c (TODO).
 * ------------------------------------------------------------------------
 */

#include "port_math.h"   /* real system math decls; see header for why */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>
#include "sdl_compat.h"   /* names newer than the arm64 build's SDL 2.0.12 */

#include "platform.h"
#include "system.h"
#include "config.h"
#include "wheel.h"
#include "assign.h"
#include "input.h"
#include "envflag.h"
#include "console.h"
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

static void overlayFrontEndCrosshair(void);

static int numControllers = 1;
static int connectedMask   = 0x1;   /* controller 0 always present */

static SDL_GameController *pads[MAX_PADS];

/* Local controller assignment (issue #99 / D416, reworked: input type per player).
 * Input.P1Type: what player 1 plays with, 0 = keyboard + mouse, 1 = gamepad,
 * 2 = auto (the default). Players 2-4 are always gamepads.
 *  - auto: as before the setting existed. Solo: the keyboard + mouse and the
 *    pad both drive player 1, whichever is used. Split-screen: one pad ->
 *    keyboard + mouse is P1 and the pad P2; two or more pads -> the pads are P1..PN.
 *  - keyboard + mouse / gamepad: only that device drives player 1 in a stage (the
 *    other is ignored there; menus accept both, and with no gamepad connected
 *    player 1 falls back to the keyboard + mouse). Split-screen: keyboard + mouse
 *    is P1 and the pads P2.., or the pads are P1..PN (one pad alone leaves MP
 *    locked, as stock).
 * Input.Player<N>Pad names the gamepad player N wants (SDL GUID + "#k" for
 * identical models, "" = first free); it only orders the seats.
 * The shift applies only while a 2+ player stage is running; front-end menus
 * keep the pad on controller 0. The reported controller mask still counts the
 * shifted slots so the game's own "needs 2 controllers" gates open. */
static int p1Type = 2;
static char padPref[4][48];   /* Input.Player1Pad .. Input.Player4Pad */
static int nRealPads   = 0;
extern s32 getPlayerCount(void);

extern s32 coopActive(void);
int inputMpSplit(void)
{
    /* D960: a direct -level_XX co-op boot never visits RUN_STAGE (menu stays INVALID). */
    return (current_menu == GE_MENU_RUN_STAGE || (current_menu == GE_MENU_INVALID && coopActive())) &&
           getPlayerCount() >= 2;
}

/* DNEW-mp-1 (issue #99): pad SEATS. pads[] is indexed by seat, not by SDL
 * device index. A seat is taken in connection order (boot: SDL device order)
 * and keyed by the pad's SDL instance id, so a rescan keeps every still-attached
 * pad in its seat. Outside a split-screen match the seats are compacted (P1..PN
 * dense, which the game's joyGetControllerCount needs: it counts contiguous
 * connected bits). During a match a vacated seat stays reserved and its mask bit
 * stays set, so an unplug neither shifts the other players' pads nor tells the
 * game a controller vanished, and a re-plugged pad takes the first free seat,
 * i.e. back into its old one. Compaction runs lazily once the match is over.
 * Seat -> controller slot: slot = seat, or seat + 1 when keyboard/mouse is P1.
 * The routing decision (keyboard/mouse P1 or not) counts the match's sticky
 * seats for the same reason: an Auto-mode unplug from 2 pads to 1 must not flip
 * the keyboard onto P1 and shift every pad by one slot mid-match. */
static SDL_JoystickID padInst[MAX_PADS] = { -1, -1, -1, -1 };
static int s_matchSeatMask = 0;     /* seats occupied at any point this match */
static int s_seatCompactOwed = 0;   /* a seat was vacated in a match */

enum { MPR_KBMP1 = 1, MPR_PADSP1 = 2 };
static int mpRoute(void)
{
    /* In a match, count the seats used at any point this match (the sticky
     * mask only grows), so an unplug never changes the routing. */
    int n = nRealPads;
    if (inputMpSplit()) {
        n = 0;
        for (int s = 0; s < MAX_PADS; ++s) n += (s_matchSeatMask >> s) & 1;
    }
    /* D974: no keyboard/mouse on the console, so it is never player 1 (with
     * one pad it made the game count two controllers). Run-time CONSOLE mode:
     * the Switch, or a handheld run with GE_CONSOLE=1. */
    if (portIsConsole())
        return n >= 2 ? MPR_PADSP1 : 0;
    if ((p1Type == 0 && n >= 1) || (p1Type == 2 && n == 1))
        return MPR_KBMP1;
    return n >= 2 ? MPR_PADSP1 : 0;
}
static int inputMpKbmP1(void)
{
    return (mpRoute() & MPR_KBMP1) != 0;
}
#define GE_MENU_MP_CHAR_SELECT 15
/* Pads move up one slot behind the keyboard/mouse player 1: in a 2+ player stage, and on the character select (MP and
 * co-op), where each player picks with their own device; other menus keep the first pad on controller 0. */
static int inputSeatShift(void)
{
    return (inputMpSplit() || current_menu == GE_MENU_MP_CHAR_SELECT) && inputMpKbmP1();
}
/* Pads own P1 (2+ pads, not KbmP1): the keyboard/mouse block is skipped. */
static int inputMpPadsOwnP1(void)
{
    return inputMpSplit() && (mpRoute() & MPR_PADSP1) != 0;
}
/* In a stage (not a front-end menu); menus take every device. */
static int inStage(void)
{
    return current_menu == GE_MENU_RUN_STAGE || current_menu == GE_MENU_INVALID;
}
/* Player 1 plays with the keyboard + mouse only: the pad is ignored in a stage.
 * Never in console mode (no keyboard/mouse: the pad is the only device). */
static int padBlockedForP1(void) { return !portIsConsole() && p1Type == 0 && inStage(); }
/* Player 1 plays with a gamepad only: the keyboard + mouse is ignored in a stage
 * (unless no gamepad is connected, so the game stays playable). */
static int kbmBlockedForP1(void) { return !portIsConsole() && p1Type == 1 && nRealPads > 0 && inStage(); }
static SDL_GameController *slotPad(int idx)
{
    if (idx < 0 || idx >= MAX_PADS) return NULL;
    if (inputSeatShift()) return idx == 0 ? NULL : pads[idx - 1];
    if (idx == 0 && !inputMpSplit() && padBlockedForP1()) return NULL;
    return pads[idx];
}

/* D416 per-slot input: the controller poll runs on the scheduler thread, where
 * g_CurrentPlayer is whichever player the game thread last selected -- wrong
 * for a split-screen slot. While a slot is being polled (g_inputSlot >= 0) in a
 * 2+ player stage, every player-struct access in this file goes through
 * inputSlotPlayer(), which resolves the player that owns the slot (player N reads
 * controller N, as on the N64). Outside a poll, or in solo, it is exactly
 * g_CurrentPlayer. */
int g_inputSlot = -1;
struct player *inputSlotPlayer(void)
{
    if (g_inputSlot >= 0 && g_inputSlot < 4 && inputMpSplit() && g_playerPointers[g_inputSlot])
        return g_playerPointers[g_inputSlot];
    return g_CurrentPlayer;
}

/* D511: Video.CrosshairHealthColor input -- health + armour of the player whose
 * crosshair is being built (gunDrawSight runs on that player's game thread). */
float portCrosshairHealthRatio(void)
{
    struct player *p = g_CurrentPlayer;
    if (!p) return 1.0f;
    float r = p->bondhealth + p->bondarmour;
    return r < 0.0f ? 0.0f : (r > 2.0f ? 2.0f : r);
}

/* In a 2+ player stage this file never calls a game entry point that acts on
 * g_CurrentPlayer (bond_interact_object, reload, gadget cycle, control-type
 * setter): the poll thread is not the game thread, so re-pointing the
 * current-player globals from here races the game tick (first attempt did,
 * and hung the game in bondinvCycleForward after a respawn). Match play
 * routes use/reload through the native B button instead, and reads/writes
 * the slot player's fields directly. */
static int padTriggerPct;               /* defined (with its default) further down */
static u32 padActPrev[MAX_PADS];        /* D469: per-slot previous action-held bitset (edges track through menus too) */
static int padSelectPrev[MAX_PADS];     /* Select (BACK) edge per pad (D472: was one shared flag) */
static int s_ovDev = -1;               /* pad seat (pads[] index) that opened the menu (-1 = keyboard + mouse) */
static int s_ovPlayer = 0;              /* player (0..3) who opened the menu; its pages are edited */

/* The pad that drives the menu: the opener's while it is attached. Opened by the keyboard (or the
 * opener detached): in a 2+ player stage with the keyboard + mouse as player 1 no pad drives it (-1);
 * otherwise the lowest attached pad (it is player 1's), else 0. */
int inputOverlayOwnerPad(void)
{
    if (s_ovDev >= 0 && s_ovDev < MAX_PADS && pads[s_ovDev]) return s_ovDev;
    if (inputSeatShift()) return -1;
    for (int i = 0; i < MAX_PADS; i++)
        if (pads[i]) return i;
    return 0;
}

/* The player who opened the menu (0 for the keyboard + mouse). */
int inputOverlayOwnerSeat(void)
{
    return s_ovPlayer >= 0 && s_ovPlayer < MAX_PADS ? s_ovPlayer : 0;
}

/* pads[] seat of a controller (-1 if none). */
static int padSeatOfPad(const SDL_GameController *pad)
{
    for (int i = 0; i < MAX_PADS; i++)
        if (pad && pads[i] == pad) return i;
    return -1;
}

int inputSlotPadSeat(int slot)
{
    return padSeatOfPad(slotPad(slot));
}

/* ------------------------------------------------------------------------
 * Gamepad sources and the Modern layout.
 *
 * Sources are SDL controller buttons plus the two analog triggers treated as
 * digital (padTriggerPct). A layout binds each action to a mask of sources.
 * Seat = pads[] index (D448), not the controller slot. Threading: the poll
 * thread reads an immutable published table (atomic pointer) rebuilt into a
 * spare buffer only when the signature changes (the pad family); no strtok.
 * Not rebindable (menu lockout guard): menu accept/cancel (A,X -> native A;
 * B,Y -> native B outside a playable stage), D-pad -> N64 D-pad, sticks,
 * Back (opens the overlay).
 * ------------------------------------------------------------------------ */
enum { PS_A, PS_B, PS_X, PS_Y, PS_LB, PS_RB, PS_LS, PS_RS, PS_START,
       PS_DUP, PS_DDOWN, PS_DLEFT, PS_DRIGHT, PS_LT, PS_RT, PS_COUNT };
#define PSM(s)        (1u << (s))
#define PS_TRIGGERS   (PSM(PS_LT) | PSM(PS_RT))
static const struct { const char *name; int btn; } kPadSrc[PS_COUNT] = {
    [PS_A] = { "a", SDL_CONTROLLER_BUTTON_A },
    [PS_B] = { "b", SDL_CONTROLLER_BUTTON_B },
    [PS_X] = { "x", SDL_CONTROLLER_BUTTON_X },
    [PS_Y] = { "y", SDL_CONTROLLER_BUTTON_Y },
    [PS_LB] = { "leftshoulder", SDL_CONTROLLER_BUTTON_LEFTSHOULDER },
    [PS_RB] = { "rightshoulder", SDL_CONTROLLER_BUTTON_RIGHTSHOULDER },
    [PS_LS] = { "leftstick", SDL_CONTROLLER_BUTTON_LEFTSTICK },
    [PS_RS] = { "rightstick", SDL_CONTROLLER_BUTTON_RIGHTSTICK },
    [PS_START] = { "start", SDL_CONTROLLER_BUTTON_START },
    [PS_DUP] = { "dpup", SDL_CONTROLLER_BUTTON_DPAD_UP },
    [PS_DDOWN] = { "dpdown", SDL_CONTROLLER_BUTTON_DPAD_DOWN },
    [PS_DLEFT] = { "dpleft", SDL_CONTROLLER_BUTTON_DPAD_LEFT },
    [PS_DRIGHT] = { "dpright", SDL_CONTROLLER_BUTTON_DPAD_RIGHT },
    [PS_LT] = { "lefttrigger", -1 },
    [PS_RT] = { "righttrigger", -1 },
};

enum { PA_FIRE, PA_AIM, PA_USE, PA_RELOAD, PA_CROUCH, PA_NEXTWPN, PA_START, PA_COUNT };

/* Modern: RT fire, LT aim, A use, X reload, Y next weapon, R3 crouch; LB opens the weapon wheel and RB
 * the gadget wheel (port/src/wheel.c reads them itself); B is free. */
static const char *const kPadModern[PA_COUNT] = {
    [PA_FIRE] = "righttrigger", [PA_AIM] = "lefttrigger", [PA_USE] = "a", [PA_RELOAD] = "x", [PA_CROUCH] = "rightstick",
    [PA_NEXTWPN] = "y", [PA_START] = "start",
};
/* Modern goes by position, not by letter: a Nintendo pad prints Y where Xbox has X (left), X where Xbox has Y (top),
 * B at the bottom and A on the right. Same hands on every pad. */
static const char *const kPadModernNin[PA_COUNT] = {
    [PA_FIRE] = "righttrigger", [PA_AIM] = "lefttrigger", [PA_USE] = "b", [PA_RELOAD] = "y", [PA_CROUCH] = "rightstick",
    [PA_NEXTWPN] = "x", [PA_START] = "start",
};
static u8 padFamily[MAX_PADS];   /* defined with its notes below (D471); 2 = Nintendo */

typedef struct { u32 mask[PA_COUNT]; } PadBinds;
typedef struct { PadBinds seat[MAX_PADS]; } PadTable;

/* D194/D238: 1 = continuous analog pitch (the natural-look stick roles);
 * 0 = the legacy 1.1/HONEY roles + D166 digital pulse. */
static int naturalPitchMode = 1;                 /* Input.NaturalPitch */
#if defined(PLATFORM_SWITCH)
/* project-dam: the console's controller defaults, the maintainer's tuned values:
 * look sensitivity 0.85x, look inverted vertically, look smoothing 2,
 * trigger point 10 %. Both sticks' deadzone is 10 % on every platform
 * (PAD_DZ_DEFAULT). */
#define NX_LOOK_SENS     85
#define NX_LOOK_SMOOTH   2
#define NX_TRIGGER_PCT   10
#define NX_LOOK_INVERT_Y 1
#else
#define NX_LOOK_SENS     100
#define NX_LOOK_SMOOTH   0
#define NX_TRIGGER_PCT   25
#define NX_LOOK_INVERT_Y 0
#endif
static int padLookInvertY = NX_LOOK_INVERT_Y;                   /* Input.PadLookInvertY: 1 = invert the look stick's Y (right stick in SOLITARE roles, left stick in HONEY roles -- #145) */
int inputPadLookInvert(void) { return padLookInvertY; }
void inputSetPadLookInvert(int v) { padLookInvertY = v ? 1 : 0; }
static PadTable padTabBuf[2];
static void *padTabPub;                         /* published PadTable*; pointer-sized/aligned for SDL_Atomic{Get,Set}Ptr (D599) */
static unsigned padTabSig = 0;
static int padTabValid = 0;

/* Parse "a,x" / "none" into a source mask (no strtok). *bad counts unknown names. */
static u32 padParseMask(const char *s, int *bad)
{
    u32 m = 0;
    while (s && *s) {
        while (*s == ' ' || *s == ',') s++;
        const char *e = s;
        while (*e && *e != ',' && *e != ' ') e++;
        size_t n = (size_t)(e - s);
        if (n) {
            int hit = 0;
            for (int i = 0; i < PS_COUNT; i++)
                if (strlen(kPadSrc[i].name) == n && !SDL_strncasecmp(s, kPadSrc[i].name, n)) {
                    m |= PSM(i);
                    hit = 1;
                    break;
                }
            if (!hit && !(n == 4 && !SDL_strncasecmp(s, "none", 4)) && bad) (*bad)++;
        }
        s = e;
    }
    return m;
}

static void padBuildBinds(PadBinds *b, int seat, int *bad)
{
    for (int a = 0; a < PA_COUNT; a++)
        b->mask[a] = padParseMask(padFamily[seat] == 2 ? kPadModernNin[a] : kPadModern[a], bad);
}

/* Which stick mapping the port emits: the natural-look roles (SOLITARE) unless Input.NaturalPitch is off. */
static int stickSolitare(int seat) { (void)seat; return naturalPitchMode != 0; }
static int styleIsSolitare(int seat) { return stickSolitare(seat); }

/* Signature of everything the table depends on: the pad family of each seat. */
static unsigned padComputeSig(void)
{
    unsigned h = 2166136261u;
    for (int s = 0; s < MAX_PADS; s++) h = (h ^ (unsigned)padFamily[s]) * 16777619u;   /* Modern follows the pad's family (by position) */
    return h;
}

/* Poll thread: rebuild into the spare buffer and publish atomically. */
static const PadTable *padTabGet(void)
{
    static SDL_SpinLock rebuildLock;   /* callers on several threads (poll, overlay, UI) */
    unsigned sig = padComputeSig();
    if (!padTabValid || sig != padTabSig) {
        SDL_AtomicLock(&rebuildLock);
        if (padTabValid && padComputeSig() == padTabSig) {   /* another thread just rebuilt */
            SDL_AtomicUnlock(&rebuildLock);
            return (const PadTable *)SDL_AtomicGetPtr((void **)&padTabPub);
        }
        PadTable *cur = (PadTable *)SDL_AtomicGetPtr((void **)&padTabPub);
        PadTable *spare = (cur == &padTabBuf[0]) ? &padTabBuf[1] : &padTabBuf[0];
        int bad = 0;
        for (int s = 0; s < MAX_PADS; s++) padBuildBinds(&spare->seat[s], s, &bad);
        SDL_AtomicSetPtr((void **)&padTabPub, spare);
        padTabSig = sig;
        padTabValid = 1;
        sysLogPrintf(LOG_INFO, "input: pad table rebuilt (%d unknown name(s))", bad);
        SDL_AtomicUnlock(&rebuildLock);
        return spare;
    }
    return (const PadTable *)SDL_AtomicGetPtr((void **)&padTabPub);
}

/* Current raw source state of a pad (digital buttons + triggers). */
static u32 padReadRaw(SDL_GameController *pad)
{
    u32 raw = 0;
    if (!pad) return 0;
    for (int i = 0; i < PS_LT; i++)
        if (SDL_GameControllerGetButton(pad, (SDL_GameControllerButton)kPadSrc[i].btn)) raw |= PSM(i);
    int trigPt = padTriggerPct * 327;   /* % of the 0..32767 trigger travel */
    if (SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > trigPt) raw |= PSM(PS_LT);
    if (SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > trigPt) raw |= PSM(PS_RT);
    return raw;
}

/* D961 GE_PRESSLATCH: event latch. The pad is sampled by value (once per
 * retrace on the scheduler thread), so a tap that goes down and up between two
 * samples is invisible. The render thread's SDL event pump (port/ge_render/src/ge_window_sdl.cpp)
 * also runs SDL_JoystickUpdate, at its own instants, and sees the DOWN event:
 * it is latched here and OR-ed into the next sample for exactly one sample.
 * Double-edge guard: a DOWN whose time is not after the previous sample, when
 * that sample already showed the source held, is dropped (the game saw it). */
static int s_latchOn = -1;
/* Lock-free (a spin lock between the render and scheduler threads can starve on
 * the Switch's strict-priority scheduler): bits and times are plain atomics,
 * the writer stores the time before it sets the bit. */
static u32 s_latchBits[MAX_PADS];
static Uint32 s_latchTs[MAX_PADS][PS_COUNT];
static u32 s_latchPrevRaw[MAX_PADS];     /* what the game was shown at the previous sample */
static Uint32 s_latchPrevMs[MAX_PADS];
static SDL_atomic_t s_latchSaved;
static int padSeatOf(SDL_JoystickID inst);

static int padLatchEnabled(void)
{
    if (s_latchOn < 0) {
        const char *e = getenv("GE_PRESSLATCH");
        s_latchOn = (e && atoi(e) != 0) ? 1 : 0;
    }
    return s_latchOn;
}

static void padLatchNote(SDL_JoystickID which, int src, Uint32 ts, int axis)
{
    const int seat = padSeatOf(which);
    if (seat < 0 || seat >= MAX_PADS) return;
    if (!(axis && (__atomic_load_n(&s_latchPrevRaw[seat], __ATOMIC_ACQUIRE) & PSM(src)))) {   /* an analog trigger already shown held is not a new press */
        __atomic_store_n(&s_latchTs[seat][src], ts, __ATOMIC_RELAXED);
        __atomic_fetch_or(&s_latchBits[seat], PSM(src), __ATOMIC_RELEASE);
    }
}

/* From the render thread's SDL event loop. */
void inputNoteButtonDown(SDL_JoystickID which, int button, Uint32 ts)
{
    if (!padLatchEnabled()) return;
    for (int i = 0; i < PS_LT; i++)
        if (kPadSrc[i].btn == button) { padLatchNote(which, i, ts, 0); return; }
}

void inputNoteAxis(SDL_JoystickID which, int axis, int value, Uint32 ts)
{
    if (!padLatchEnabled() || value <= padTriggerPct * 327) return;
    if (axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT) padLatchNote(which, PS_LT, ts, 1);
    else if (axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT) padLatchNote(which, PS_RT, ts, 1);
}

unsigned inputTapsLatched(void)
{
    return (unsigned)SDL_AtomicGet(&s_latchSaved);
}

/* Gameplay sample of a seat: the held sources plus latched taps (once). */
static u32 padLatchApply(int seat, u32 raw)
{
    if (!padLatchEnabled() || seat < 0 || seat >= MAX_PADS) return raw;
    const Uint32 nowMs = SDL_GetTicks();
    u32 add = 0;
    const u32 pend = __atomic_exchange_n(&s_latchBits[seat], 0, __ATOMIC_ACQ_REL);   /* single consumer: the pad poll */
    for (int b = 0; pend && b < PS_COUNT; b++) {
        if (!(pend & PSM(b)) || (raw & PSM(b))) continue;
        const Uint32 ts = __atomic_load_n(&s_latchTs[seat][b], __ATOMIC_RELAXED);
        const int seenBefore = (s_latchPrevRaw[seat] & PSM(b)) && (Sint32)(s_latchPrevMs[seat] - ts) >= 0;
        if (!seenBefore) add |= PSM(b);
    }
    s_latchPrevMs[seat] = nowMs;
    __atomic_store_n(&s_latchPrevRaw[seat], raw | add, __ATOMIC_RELEASE);
    if (add) SDL_AtomicAdd(&s_latchSaved, 1);
    return raw | add;
}

static u32 padActHeld(const PadBinds *b, u32 raw)
{
    u32 h = 0;
    for (int a = 0; a < PA_COUNT; a++)
        if (raw & b->mask[a]) h |= 1u << a;
    return h;
}

/* D401: per-pad haptics (N64 Rumble Pak -> SDL_GameControllerRumble).
 * padRumbleOn is detected when the pad opens (inputOpenPads); gRumbleScale
 * is the single global Input.RumbleScale config value (0 = silent,
 * 1 = full), applied to every pad. */
static int padRumbleOn[MAX_PADS];

/* D471: controller family per seat, for button NAMES in the PC menus (no
 * glyphs). Detected once when a pad is seated; 0 = Xbox-style (default,
 * also Steam Deck / unknown), 1 = PlayStation, 2 = Nintendo. SDL's default
 * GAMECONTROLLER_USE_BUTTON_LABELS=1 means BUTTON_A is the button printed
 * "A" on a Nintendo pad, so only shoulders/triggers/Back/Start differ there. */
enum { PADFAM_XBOX = 0, PADFAM_PS = 1, PADFAM_NINTENDO = 2 };

/* GE_PADFAMILY=xbox|ps|nintendo: the family of every pad, -1 = detect. D471
 * made it a dev knob for headless screenshots (button names only); it now also
 * sets the family Modern places its face buttons by, for pads SDL reports with
 * an unknown type: a PortMaster handheld's built-in pad (muOS "Deeplay-keys")
 * mapped by the letters of a Nintendo layout. The launcher exports it. */
static int padFamilyOverride(void)
{
    static int ov = -2;
    if (ov == -2) {
        const char *e = getenv("GE_PADFAMILY");
        ov = !e ? -1 : !SDL_strcasecmp(e, "ps") ? PADFAM_PS
           : !SDL_strcasecmp(e, "nintendo") ? PADFAM_NINTENDO
           : !SDL_strcasecmp(e, "xbox") ? PADFAM_XBOX : -1;
    }
    return ov;
}

static void padDetectFamily(int seat)
{
#ifdef __SWITCH__
    /* project-dam: every pad on the console is a Nintendo one (SDL reports the
     * generic "Switch Controller" with an unknown type). */
    padFamily[seat] = PADFAM_NINTENDO;
    return;
#endif
    if (padFamilyOverride() >= 0) { padFamily[seat] = (u8)padFamilyOverride(); return; }
    padFamily[seat] = PADFAM_XBOX;
#if SDL_VERSION_ATLEAST(2, 0, 12)
    switch (SDL_GameControllerGetType(pads[seat])) {
    case SDL_CONTROLLER_TYPE_PS3:
    case SDL_CONTROLLER_TYPE_PS4:
#if SDL_VERSION_ATLEAST(2, 0, 14)
    case SDL_CONTROLLER_TYPE_PS5:
#endif
        padFamily[seat] = PADFAM_PS; break;
    case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_PRO:
#if SDL_VERSION_ATLEAST(2, 24, 0)
    case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
    case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
    case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
#endif
        padFamily[seat] = PADFAM_NINTENDO; break;
    default: break;
    }
#endif
}
static float gRumbleScale = 0.5f;

static int mouseAimSpeed  = 16;     /* aim-mode sensitivity, percent (B3: 50 -> 25 M-29 -> 16; still overshot at 25) */
static int aimBand        = 20;     /* aim mode: usable stick range above the 60 gate */
/* D407: mouse->stick gain multiplier (%, 100 = unchanged) applied ONLY while
 * aiming in tank. In-tank aim must use the legacy velocity-stick path (the
 * game's turret code reads raw stick deflection only), whose px->screen feel
 * differs from the direct-write on-foot models; this knob lets the user dial
 * the turret to taste without touching on-foot aim. */
static int tankAimScale   = 100;
/* D194/D238: default 100 -> 40 (M-123 user calibration). The old gain
 * (MOUSE_TURN_GAIN=6 stick/px) saturated the game's quadratic natural-turn
 * curve at ~13 px/poll, i.e. hipfire ran at full 315 deg/s for any normal
 * movement while GEPD aim mode moves proportionally -- "too fast when I
 * leave that mode". At 40% (2.4 stick/px) the mid-speed view rate matches
 * the GEPD reticle's angular pace at GepdSens=30; flicks still reach full
 * turn speed, only later. */
int g_inputMouseTurnSpeed = 50;     /* hipfire yaw sensitivity, percent
                                        (40 = M-123 match point at GepdSens=30; 50 = the user's
                                        "~20-35% faster" default request, same +25% as g_inputGepdSens) */
static int mouseInvertY   = 0;      /* 1 = mouse-down looks up */
static int mouseYScale    = 100;    /* extra vertical (pitch) sensitivity, % */
static int mouseSmoothing = 0;      /* 0 = raw; 1..90 = low-pass strength (%) */
static int mouseRawInput  = 0;      /* 1 = bypass OS pointer accel for aim    */
static int mouseDtDecouple = 1;     /* D194(b): normalize look sensitivity by real
                                      * elapsed poll time instead of raw px/poll.
                                      * Escape hatch: 0 = prior (coupled) behaviour. */
int g_inputMouseSensitivity = 100;  /* D238: single master sensitivity, percent --
                                      * multiplies BOTH MouseAimSpeed and MouseTurnSpeed
                                      * so one number tunes overall feel; the two legacy
                                      * knobs stay as an independent per-mode trim on
                                      * top of it (both default 100/16 => unchanged
                                      * unless the user also touches those). */
static int aimCurveGamma  = 160;    /* D194(a): aim-mode response curve exponent x100
                                      * (100 = linear, >100 = more low-speed control /
                                      * less bang-bang, <100 = more twitchy). */
int g_inputMouseDirectLook  = 1;    /* WI-1 (GEPD-INPUT-PLAN.md #89): 1 = hipfire
                                      * writes vv_theta/vv_verta directly (see
                                      * inputHipDirectCompute), same linear-in-px model
                                      * as aim mode; 0 = legacy stick-curve path
                                      * (MOUSE_TURN_GAIN), kept as an escape hatch. */

int g_inputAimDevMouse;               /* the device that aimed last: 1 = the mouse (D337) */
double g_inputHipThetaDegOut;   /* D428: yaw degrees the last inputHipDirectCompute applied */
void inputSwayFeedTheta(double deg);
int inputPadDirectCompute(int dx, int dy);   /* D404: pad twin of the above */
/* D194: bondview2's "look-ahead" pitch centreing (docentreupdown) arms during
 * hip-fire walking whenever the pitch strays from the horizon target, and --
 * once armed -- keeps pulling vv_verta back to it even in aim mode, EXCEPT
 * while a manual pitch stick (|stick_y|>60) is present. That reads as "the
 * gun always tries to return to centre" the moment the mouse stops. Port-
 * side fix: on entering aim with the spring armed, emit a minimal 2-tick
 * pitch nudge in the direction of current motion so the game's own rule
 * (manual input clears docentreupdown) disarms it; aiming is then free and
 * holds when the mouse stops. No game logic touched -- this is just what
 * stick we choose to present. */
/* (s_aimHeldPrev / s_centreClearTicks live in AIMS since D422.) */
/* D194: always 0 since the free-cursor experiment was reverted (aim uses
 * grabbed relative deltas). Kept because inputReconcileGrab(),
 * inputApplyCursorVisibility() and the mouse-button mask still branch on it --
 * with it pinned to 0 they take exactly the pre-D194 paths. */


static int aimAbsCompute(double dxPx, double dyPx, int *outSx, int *outSy);  /* D194 */
static Uint64 s_lastLookPollCounter = 0;  /* D194(b): monotonic clock, gameplay-look drain only */

/* Gamepad tuning -- defaults reproduce the old hardcoded constants exactly.
 * Wave A (v0.5.0 controller/input wave, docs/dev/CONTROLLER-INPUT-PLAN.md):
 * per-stick deadzone splits the single Input.PadDeadzone into a left (movement)
 * and a right (look) value (padDeadzone stays registered so old inis load; an
 * ini that has the old key but not the new ones seeds L/R from it once in
 * inputInit). The look knobs (sens X/Y, smoothing) are feel-only and identity
 * at their defaults.
 * project-dam: no "unset" sentinel any more. The first run writes the ini
 * before inputInit, so a -1 initializer reached the file, and the next load
 * clamped it to 0 = no deadzone (stick drift). */
#define PAD_DZ_DEFAULT 3000   /* 10 % on both sticks, every platform (= defaults.json) */
static int padDeadzoneL    = PAD_DZ_DEFAULT;    /* left (movement) stick deadzone, raw 0..32767 */
static int padDeadzoneR    = PAD_DZ_DEFAULT;    /* right (look) stick deadzone, raw 0..32767 */
static int padDeadzone     = STICK_DEADZONE;  /* legacy shared deadzone (Input.PadDeadzone) */
static int padLookSensX    = NX_LOOK_SENS;             /* right-stick look: horizontal sensitivity, % (100 = native) */
static int padLookSensY    = NX_LOOK_SENS;             /* right-stick look: vertical (pitch) sensitivity, % */
#if defined(PLATFORM_SWITCH)
static int padAimLookSens  = 75;
#else
static int padAimLookSens  = 100;   /* right stick while aiming (Centered), % */
#endif
#if defined(PLATFORM_SWITCH)
int g_inputPadLookCurve    = 200;            /* project-dam: Centred stick curve exponent x100 (game: 200) */
int g_inputPadTurnAccel    = 2;              /* project-dam: 0 off, 1 low (1.5x), 2 high (2x) */             /* project-dam: right stick while aiming (game aim mode), % */
#endif
static int padSouthpaw     = 0;               /* 1 = swap fire (G) / grenade (R) trigger actions */
static int padLookSmooth   = NX_LOOK_SMOOTH;               /* right-stick look low-pass strength, 0-10 (0 = off, 10 = max) */
static int padTriggerPct   = NX_TRIGGER_PCT;              /* trigger press point, % of travel (~30*256) */
/* padLookInvertY: declared with the D513 accessors above (D645). */
static int inputDefaultsRev = 0;              /* D546: 1 once the deadzone default migration ran */

/* Smoothed mouse delta carried between polls when mouseSmoothing > 0. */
static double mouseSmDX = 0.0, mouseSmDY = 0.0;

/* Raw relative-mouse delta accumulated since the last inputComputePad(0).
 * NOT a persistent aim accumulator: mouse-look is a displacement device and
 * GE's aim is a rate device, so we consume the whole delta each poll and
 * reset -- stop moving and the stick/ C-button releases immediately. */

/* Mouse-wheel -> weapon cycle (D223): directional, like a normal PC shooter.
 * The port forces CONTROLLER_CONFIG_HONEY (bondview.c:1484), so GE's default
 * scheme applies (bondview2.c:5177-5179): invButtons = A_BUTTON,
 * shootButtons = Z_TRIG, and the cycle signals are (bondview2.c:5337-5346):
 *   forward  = fresh A edge while Z is NOT held
 *   backward = Z fresh edge while A IS held (the N64 "hold A, tap Z" trick)
 * moveData.triggerOn (bondview2.c:5419) requires A to be released, so the
 * synthesized Z edge in the backward sequence can never fire a shot.
 *
 * wheelFwd:  remaining polls of the A pulse (clean press+release edge).
 * wheelBack: 2 = present A-only next poll; 1 = present A+Z next poll (the
 *            fresh-Z-edge-while-A-held poll). A must already read "held"
 *            oldbuttons-wise before Z's edge, hence the two-poll sequence. */
#define WHEEL_FWD_POLLS 2
static int wheelFwd  = 0;
static int wheelBack = 0;

/* Item 1 (D165) — front-end pointer P-controller state. */
static int    hipfirePitchSpeed = 100; /* D166: hipfire pitch pulse rate, percent */
static int    menuPrevActive = 0;
static double hipPitchPhase = 0.0;              /* D166: hipfire pitch pulse phase 0..1     */
static int    lastMenuMouseX = -1, lastMenuMouseY = -1;  /* WI-2: last abs cursor seen in a menu */
/* D345: carry pointer ownership across front-end screen changes. The game
 * teleports cursor_h/v_pos on some transitions (file select -> mode select
 * calls setCursorPOSforMode(0), front.c:2516); the absolute write below only
 * fires while the OS mouse moves, so with an idle mouse the snap persists
 * until the next movement. lastMenuId detects the change; transitionFrames is
 * a short countdown during which a recent-mouse re-assert is allowed;
 * lastAbsWrite is the perf-counter stamp of the last absolute write (mouse
 * recency). */
static int    menuPointerLastMenuId = -1;
static int    menuPointerTransitionFrames = 0;
static Uint64 menuPointerLastAbsWrite = 0;
/* D345(b): 1 while the 1:1 pointer can own cursor_h/v_pos this poll
 * (menu, mouse enabled + grabbed). Refreshed every pad-0 poll. */
static int    s_menuPointerLive = 0;


/* ------------------------------------------------------------------------ */

static void padDetectRumble(int i)
{
    padRumbleOn[i] = 0;
#ifdef __SWITCH__
    padRumbleOn[i] = 1;   /* #40: rumble goes through libnx, SDL reports none */
    return;
#endif
    /* D401: haptics detection (PD pattern, pd_port input.c:320-324).
     * SDL_GameControllerHasRumble() landed in SDL 2.0.18 even though
     * SDL_GameControllerRumble() is from 2.0.9. */
#if SDL_VERSION_ATLEAST(2, 0, 18)
    padRumbleOn[i] = SDL_GameControllerHasRumble(pads[i]);
#else
    padRumbleOn[i] = SDL_JoystickIsHaptic(SDL_GameControllerGetJoystick(pads[i]));
    if (!padRumbleOn[i]) {
        /* At least on Windows some controllers report no haptics but
         * rumble will still function: assume it's supported for a
         * known controller type. */
        const SDL_GameControllerType ctype = SDL_GameControllerGetType(pads[i]);
        padRumbleOn[i] = ctype && (ctype != SDL_CONTROLLER_TYPE_VIRTUAL);
    }
#endif
}

static int padSeatOf(SDL_JoystickID inst)
{
    for (int s = 0; s < MAX_PADS; ++s) {
        if (pads[s] && padInst[s] == inst) return s;
    }
    return -1;
}

#ifdef __SWITCH__
/* D974: SDL's switch driver lists one joystick per npad (No1..No8),
 * connected or not. Only open the ones with a controller behind them
 * (nxPadConnectedMask, port/src/switch/nxmem.c). GE_PADS=<1..4> forces the
 * first N instead, for tests (Ryujinx, nxlink) whatever is attached. */
extern int nxPadConnectedMask(void);
static int s_nxPadMask = -1;   /* last mask the seats were built from */

static int nxPadAllowedMask(void)
{
    static int forced = -2;
    if (forced == -2) {
        const char *e = getenv("GE_PADS");
        forced = (e && e[0] >= '1' && e[0] <= '4') ? (1 << (e[0] - '0')) - 1 : -1;
        if (forced > 0) sysLogPrintf(LOG_NOTE, "input: GE_PADS forces %d controller(s)", e[0] - '0');
    }
    return forced > 0 ? forced : nxPadConnectedMask();
}

static int padDev[MAX_PADS] = { -1, -1, -1, -1 };   /* D974: SDL device index of each seat */

/* #40: devkitPro's SDL2 reports no rumble on Switch pads, so the Rumble Pak
 * goes to the controllers through libnx. Tuning knobs for the console test:
 * the two linear motors' frequencies (Hz) and each one's share of the
 * amplitude (multiplied with strength * Input.RumbleScale, then clamped). */
#define NX_RUMBLE_FREQ_LOW   100.0f
#define NX_RUMBLE_FREQ_HIGH  200.0f
#define NX_RUMBLE_AMP_LOW    1.0f
#define NX_RUMBLE_AMP_HIGH   0.15f

extern void nxRumbleSend(int seat, int dev, float amp, float ampLow, float ampHigh,
                         float freqLow, float freqHigh);
extern void nxRumbleForget(int seat);

static SDL_SpinLock s_nxRumLock;          /* game thread, poll thread, atexit */
static SDL_atomic_t s_nxRumActive[MAX_PADS];     /* 1 = a deadline is pending */
static SDL_atomic_t s_nxRumDeadline[MAX_PADS];   /* SDL_GetTicks() value */

/* Send amp (0..1) to the seat's controller; zero only reaches cached handles.
 * Caller holds s_nxRumLock. */
static void nxRumSendLocked(int s, float amp)
{
    nxRumbleSend(s, padDev[s], amp, NX_RUMBLE_AMP_LOW, NX_RUMBLE_AMP_HIGH,
                 NX_RUMBLE_FREQ_LOW, NX_RUMBLE_FREQ_HIGH);
}

/* Game thread: strength already scaled, 0..1; time in seconds. */
static void nxRumbleSet(int s, float amp, float time)
{
    if (amp > 1.f) amp = 1.f;
    if (amp <= 0.f || time <= 0.f) {
        SDL_AtomicSet(&s_nxRumActive[s], 0);
        SDL_AtomicLock(&s_nxRumLock);
        nxRumSendLocked(s, 0.f);
        SDL_AtomicUnlock(&s_nxRumLock);
        return;
    }
    SDL_AtomicSet(&s_nxRumDeadline[s], (int)(SDL_GetTicks() + (Uint32)(time * 1000.f)));
    SDL_AtomicSet(&s_nxRumActive[s], 1);
    SDL_AtomicLock(&s_nxRumLock);
    nxRumSendLocked(s, amp);
    SDL_AtomicUnlock(&s_nxRumLock);
}

/* Poll thread: stop seats whose time ran out. */
static void nxRumbleTick(void)
{
    for (int s = 0; s < MAX_PADS; ++s) {
        if (!SDL_AtomicGet(&s_nxRumActive[s])) continue;
        if ((int)(SDL_GetTicks() - (Uint32)SDL_AtomicGet(&s_nxRumDeadline[s])) < 0) continue;
        if (!SDL_AtomicCAS(&s_nxRumActive[s], 1, 0)) continue;
        SDL_AtomicLock(&s_nxRumLock);
        nxRumSendLocked(s, 0.f);
        SDL_AtomicUnlock(&s_nxRumLock);
    }
}

/* Seat dropped or moved: silence it and forget its handles. */
static void nxRumbleReset(int s)
{
    SDL_AtomicSet(&s_nxRumActive[s], 0);
    SDL_AtomicLock(&s_nxRumLock);
    nxRumSendLocked(s, 0.f);
    nxRumbleForget(s);
    SDL_AtomicUnlock(&s_nxRumLock);
}
#endif

/* ---- the gamepad each player asked for (Input.Player<N>Pad) ----
 * An id is the SDL GUID plus "#k", k = how many controllers of the same model
 * come before it in SDL's device order, so two identical pads stay apart. */
static void padIdOfDevice(int dev, char *out, int n)
{
    SDL_JoystickGUID g = SDL_JoystickGetDeviceGUID(dev);
    char s[40];
    SDL_JoystickGetGUIDString(g, s, sizeof s);
    int k = 0;
    for (int j = 0; j < dev; ++j) {
        if (!SDL_IsGameController(j)) continue;
        SDL_JoystickGUID h = SDL_JoystickGetDeviceGUID(j);
        if (!memcmp(&g, &h, sizeof g)) k++;
    }
    snprintf(out, (size_t)n, "%s#%d", s, k);
}

#define PADLIST_MAX 16
static char s_listId[PADLIST_MAX][48];
static char s_listName[PADLIST_MAX][48];
static int  s_listN = 0;

/* Connected game controllers in SDL order, for the menu's Controller row. */
int inputPadListRefresh(void)
{
    s_listN = 0;
    const int n = SDL_NumJoysticks();
    for (int i = 0; i < n && s_listN < PADLIST_MAX; ++i) {
        if (!SDL_IsGameController(i)) continue;
        padIdOfDevice(i, s_listId[s_listN], sizeof s_listId[0]);
        const char *nm = SDL_GameControllerNameForIndex(i);
        snprintf(s_listName[s_listN], sizeof s_listName[0], "%s", nm ? nm : "Controller");
        s_listN++;
    }
    return s_listN;
}
const char *inputPadListName(int k) { return k >= 0 && k < s_listN ? s_listName[k] : ""; }
const char *inputPadListId(int k)   { return k >= 0 && k < s_listN ? s_listId[k] : ""; }
const char *inputPadPref(int player) { return player >= 0 && player < 4 ? padPref[player] : ""; }
void inputPadPrefSet(int player, const char *id)
{
    if (player < 0 || player > 3) return;
    snprintf(padPref[player], sizeof padPref[player], "%s", id ? id : "");
    inputRequestRescan();   /* re-seat the pads now */
}
int inputP1Type(void) { return portIsConsole() ? 1 : p1Type; }   /* console: always a gamepad */

/* A pad whose button went down since the last call: its id (as the Controller row stores it). 0 = none. */
int inputAnyPadHeld(void)
{
    for (int s = 0; s < MAX_PADS; ++s)
        if (pads[s] && padReadRaw(pads[s])) return 1;
    return 0;
}

int inputPadNewPress(char *id, int n)
{
    static u32 prev[MAX_PADS];
    for (int s = 0; s < MAX_PADS; ++s) {
        if (!pads[s]) { prev[s] = 0; continue; }
        const u32 raw = padReadRaw(pads[s]);
        const u32 down = raw & ~prev[s];
        prev[s] = raw;
        if (!down) continue;
        for (int d = 0, nd = SDL_NumJoysticks(); d < nd; ++d)
            if (SDL_IsGameController(d) && SDL_JoystickGetDeviceInstanceID(d) == padInst[s]) {
                padIdOfDevice(d, id, n);
                return 1;
            }
    }
    return 0;
}

const char *inputPadNameForId(const char *id)
{
    inputPadListRefresh();
    for (int k = 0; id && inputPadListId(k)[0]; ++k)
        if (!strcmp(id, inputPadListId(k))) return inputPadListName(k);
    return "Controller";
}

int inputPadCount(void)
{
    int n = 0;
    for (int s = 0; s < MAX_PADS; ++s) if (pads[s]) n++;
    return n;
}

#ifndef __SWITCH__
/* Outside a match, order the seats by what the players asked for: a pad that
 * is player N's choice goes before every pad nobody asked for (which keep their
 * connection order). Seats stay dense. */
static void padApplyOrder(void)
{
    int rank[MAX_PADS], order[MAX_PADS], cnt = 0;
    for (int s = 0; s < MAX_PADS; ++s) {
        rank[s] = 100 + s;
        if (!pads[s]) continue;
        char id[48] = "";
        for (int d = 0, n = SDL_NumJoysticks(); d < n; ++d) {
            if (SDL_IsGameController(d) && SDL_JoystickGetDeviceInstanceID(d) == padInst[s]) {
                padIdOfDevice(d, id, sizeof id);
                break;
            }
        }
        for (int p = 0; p < 4; ++p) {
            if (padPref[p][0] && !strcmp(padPref[p], id)) { rank[s] = p; break; }
        }
        order[cnt++] = s;
    }
    for (int a = 1; a < cnt; ++a) {   /* stable insertion sort by rank */
        int v = order[a], b = a - 1;
        while (b >= 0 && rank[order[b]] > rank[v]) { order[b + 1] = order[b]; b--; }
        order[b + 1] = v;
    }
    int changed = 0;
    for (int a = 0; a < cnt; ++a) changed |= order[a] != a;
    if (!changed) return;
    SDL_GameController *np[MAX_PADS] = {0};
    SDL_JoystickID ni[MAX_PADS] = { -1, -1, -1, -1 };
    int nr[MAX_PADS] = {0};
    u8 nf[MAX_PADS] = {0};
    for (int a = 0; a < cnt; ++a) {
        np[a] = pads[order[a]]; ni[a] = padInst[order[a]];
        nr[a] = padRumbleOn[order[a]]; nf[a] = padFamily[order[a]];
    }
    for (int s = 0; s < MAX_PADS; ++s) {
        pads[s] = np[s]; padInst[s] = ni[s];
        padRumbleOn[s] = nr[s]; padFamily[s] = s < cnt ? nf[s] : PADFAM_XBOX;
    }
    sysLogPrintf(LOG_NOTE, "input: gamepads re-seated to the players' choices");
}
#endif

/* Seat table maintenance (DNEW-mp-1, see padInst above). Called at boot and on
 * every rescan: drops detached pads, compacts outside a match, opens any new
 * controller into the first free seat, then rebuilds the controller mask. */
static void inputOpenPads(void)
{
    const int inMatch = inputMpSplit();
#ifdef __SWITCH__
    const int allowed = nxPadAllowedMask();
    s_nxPadMask = allowed;
#endif

    /* 1. drop pads that are gone (the handle of a detached pad is dead) */
    for (int s = 0; s < MAX_PADS; ++s) {
#ifdef __SWITCH__
        const int npadGone = pads[s] && padDev[s] >= 0 && !(allowed & (1 << padDev[s]));
#else
        const int npadGone = 0;
#endif
        if (pads[s] && (npadGone || !SDL_GameControllerGetAttached(pads[s]))) {
            sysLogPrintf(LOG_NOTE, "input: seat %d pad detached%s", s,
                         inMatch ? " (seat kept for the match)" : "");
#if SDL_VERSION_ATLEAST(2, 0, 12)
            SDL_GameControllerSetPlayerIndex(pads[s], -1);   /* D471: LED off */
#endif
            SDL_GameControllerClose(pads[s]);
            pads[s] = NULL;
            padInst[s] = -1;
            padRumbleOn[s] = 0;
            padFamily[s] = PADFAM_XBOX;
#ifdef __SWITCH__
            padDev[s] = -1;
            nxRumbleReset(s);   /* #40 */
#endif
            if (inMatch) s_seatCompactOwed = 1;
        }
    }

    /* 2. outside a match, seats are dense in their existing order */
    if (!inMatch) {
        int d = 0;
        for (int s = 0; s < MAX_PADS; ++s) {
            if (!pads[s]) continue;
            if (s != d) {
                pads[d] = pads[s];           pads[s] = NULL;
                padInst[d] = padInst[s];     padInst[s] = -1;
                padRumbleOn[d] = padRumbleOn[s]; padRumbleOn[s] = 0;
                padFamily[d] = padFamily[s]; padFamily[s] = PADFAM_XBOX;   /* D471 */
#ifdef __SWITCH__
                padDev[d] = padDev[s];       padDev[s] = -1;
                nxRumbleReset(s); nxRumbleReset(d);   /* #40 */
#endif
                sysLogPrintf(LOG_NOTE, "input: pad '%s' moved seat %d -> %d",
                             SDL_GameControllerName(pads[d]), s, d);
            }
            d++;
        }
        s_seatCompactOwed = 0;
    }

    /* 3. open new controllers into the first free seat */
    int n = SDL_NumJoysticks();
    for (int i = 0; i < n; ++i) {
        if (!SDL_IsGameController(i)) {
            continue;
        }
#ifdef __SWITCH__
        if (i < 8 && !(allowed & (1 << i))) {
            continue;   /* D974: no controller behind this npad */
        }
#endif
        if (padSeatOf(SDL_JoystickGetDeviceInstanceID(i)) >= 0) {
            continue;   /* already seated */
        }
        int seat = -1;
        for (int s = 0; s < MAX_PADS; ++s) {
            if (!pads[s]) { seat = s; break; }
        }
        if (seat < 0) {
            break;      /* all seats taken */
        }
        pads[seat] = SDL_GameControllerOpen(i);
        if (pads[seat]) {
            padInst[seat] = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pads[seat]));
#ifdef __SWITCH__
            padDev[seat] = i;
#endif
            padDetectRumble(seat);
            padDetectFamily(seat);   /* D471 */
            sysLogPrintf(LOG_NOTE, "input: opened gamepad %d '%s' as seat %d (rumble %s)",
                         i, SDL_GameControllerName(pads[seat]), seat, padRumbleOn[seat] ? "yes" : "no");
        }
    }

#ifndef __SWITCH__
    if (!inMatch) padApplyOrder();
#endif

    /* 4. controller mask: seat bits, shifted up one when keyboard/mouse is P1 */
    int seatMask = 0;
    nRealPads = 0;
    for (int s = 0; s < MAX_PADS; ++s) {
        if (pads[s]) { seatMask |= 1 << s; nRealPads++; }
    }
    if (inMatch) {
        s_matchSeatMask |= seatMask;   /* reserved seats stay "connected, idle" */
        seatMask = s_matchSeatMask;
    } else {
        s_matchSeatMask = seatMask;
    }
    connectedMask = 0x1 | seatMask;
    if (inputMpKbmP1()) {
        /* keyboard/mouse is controller 0; seat s is controller s + 1 */
        connectedMask = (0x1 | (seatMask << 1)) & ((1 << MAX_PADS) - 1);
    }
#if SDL_VERSION_ATLEAST(2, 0, 12)
    /* D471 (PD-port parity): the pad's player LED shows its player number;
     * seat s is player s + 1 when keyboard/mouse is P1. Re-applied on every
     * (re)scan, so compaction and MP-mode changes pick it up. */
    for (int s = 0; s < MAX_PADS; ++s)
        if (pads[s]) SDL_GameControllerSetPlayerIndex(pads[s], s + (inputMpKbmP1() ? 1 : 0));
#endif
    {   /* D416 test harness: GE_MPVIRT=<n> reports n extra idle controllers
         * (1..3) so the MP menu unlocks and a 2P+ stage loads with no pads. */
        const char *v = getenv("GE_MPVIRT");
        int n = v ? atoi(v) : 0;
        if (n > 0) {
            if (n > MAX_PADS - 1) n = MAX_PADS - 1;
            connectedMask |= (1 << (n + 1)) - 1;
        }
    }
    numControllers = 1;
    for (int i = 1; i < MAX_PADS; ++i) {
        if (connectedMask & (1 << i)) {
            numControllers = i + 1;
        }
    }
}

/* Scripted input and record / replay: port/src/input_script.c (the declarations are in input/input_internal.h). */


static int crouchMode = 1;   /* 0 = hold, 1 = toggle (latched crouch input); D556: toggle by default (existing ini values kept) */

int inputInit(void)
{
    if (!SDL_WasInit(SDL_INIT_GAMECONTROLLER)) {
        if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0) {
            sysLogPrintf(LOG_WARNING, "input: SDL_INIT_GAMECONTROLLER failed: %s",
                         SDL_GetError());
        }
    }

#ifdef __SWITCH__
    /* project-dam: devkitPro's SDL maps the console's own "Switch Controller"
     * POSITIONALLY like an Xbox pad (a:b1 b:b0 x:b3 y:b2, i.e. the physical
     * B button reports BUTTON_A). The port assumes SDL's default for Nintendo
     * pads, GAMECONTROLLER_USE_BUTTON_LABELS=1 (BUTTON_A = the button printed
     * "A"), so A/B and X/Y came out swapped. Re-register the same GUID with a
     * by-label mapping before any pad is opened. */
    if (SDL_GameControllerAddMapping(
            "000038f853776974636820436f6e7400,Switch Controller,"
            "a:b0,b:b1,x:b2,y:b3,back:b11,start:b10,"
            "dpdown:b15,dpleft:b12,dpright:b14,dpup:b13,"
            "leftshoulder:b6,rightshoulder:b7,lefttrigger:b8,righttrigger:b9,"
            "leftstick:b4,rightstick:b5,leftx:a0,lefty:a1,rightx:a2,righty:a3,") < 0) {
        sysLogPrintf(LOG_WARNING, "input: Switch label mapping rejected: %s", SDL_GetError());
    }
#endif

    /* D401: make BT PS4/PS5 pads report haptics (PD input.c:696-697). Must
     * be set before the pads below are opened. */
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS4_RUMBLE, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS5_RUMBLE, "1");
    /* abpad live A/B: GE_PAD_BACKGROUND=1 keeps pad input flowing while the
     * window is unfocused, so one real pad can drive the port and 1964 at once.
     * Off by default (SDL ignores pads in unfocused windows). */
    if (getenv("GE_PAD_BACKGROUND") && getenv("GE_PAD_BACKGROUND")[0] == '1')
        SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");

    for (int i = 0; i < MAX_PADS; ++i) {
        pads[i] = NULL;
        padInst[i] = -1;
    }
    inputOpenPads();

    inputMigrateBinds();  /* D380: materialise effective GEPD defaults once */
    inputRebuildBinds();   /* D214: parse [Bind] now that configLoad() has run */

    /* Wave A: one-time per-stick deadzone migration. Old inis carry a single
     * Input.PadDeadzone; if the player has not set the new per-stick keys, seed
     * both from it. A fresh install has neither key and keeps PAD_DZ_DEFAULT. */
    if (configIntKeyWasLoaded("Input.PadDeadzone")) {
        if (!configIntKeyWasLoaded("Input.PadDeadzoneL")) padDeadzoneL = padDeadzone;
        if (!configIntKeyWasLoaded("Input.PadDeadzoneR")) padDeadzoneR = padDeadzone;
    }
    /* D546: one-time move of the untouched old deadzone default (7000 = 23 %)
     * to the standardized 25 % (7500); a value the player set is kept. */
    if (inputDefaultsRev < 1) {
        if (padDeadzone  == 7000) padDeadzone  = STICK_DEADZONE;
        if (padDeadzoneL == 7000) padDeadzoneL = STICK_DEADZONE;
        if (padDeadzoneR == 7000) padDeadzoneR = STICK_DEADZONE;
        inputDefaultsRev = 1;
    }
    /* project-dam: the desktop default deadzone 25 % -> 10 % on both sticks,
     * as on the console (modern shooters use 5-15 %; 25 % swallowed the small
     * aim movements). Only the untouched old default moves. The console moved
     * its right stick in NxDefaultsRev 2; its left stick was already 10 %.
     * 0 is the old first-run bug (a -1 sentinel saved, then clamped to 0 by
     * the next load): heal it too. The console never had the sentinel. */
    if (inputDefaultsRev < 2) {
#if !defined(PLATFORM_SWITCH)
        if (padDeadzoneL == STICK_DEADZONE || padDeadzoneL <= 0) padDeadzoneL = PAD_DZ_DEFAULT;
        if (padDeadzoneR == STICK_DEADZONE || padDeadzoneR <= 0) padDeadzoneR = PAD_DZ_DEFAULT;
#endif
        inputDefaultsRev = 2;
    }
#if defined(PLATFORM_SWITCH)
    /* Right-stick deadzone 10 % instead of the desktop 25 %: modern shooters
     * use 5-12 %, and 25 % swallowed the small aim movements. Only the
     * untouched default moves; a value the player set is kept. */
    if (nxDefaultsRev < 2) {
        if (padDeadzoneR == STICK_DEADZONE) padDeadzoneR = PAD_DZ_DEFAULT;
        nxDefaultsRev = 2;
    }
#endif
    if (getenv("GE_BINDPROBE")) inputBindingProbe();

    sysLogPrintf(LOG_INFO, "input: PC bindings, crouch mode %s",
                 crouchMode ? "toggle" : "hold");

    /* Relative mouse mode for mouse-look. Click-to-lock: we start released
     * and wait for a click in the window (video.c -> inputNotifyClick). */
    g_inputMouseGrabbed = 0;
    if (g_inputMouseEnabled) {
        if (mouseRawInput) {
            /* Feed the raw device delta straight through: no OS pointer
             * acceleration, no warp-based emulation. Must be set before
             * relative mode is enabled. */
            SDL_SetHint(SDL_HINT_MOUSE_RELATIVE_SYSTEM_SCALE, "0");
            SDL_SetHint(SDL_HINT_MOUSE_RELATIVE_MODE_WARP, "0");
            sysLogPrintf(LOG_INFO, "input: raw mouse input (no OS pointer accel)");
        }
        inputMouseRequestRelative(g_inputMouseGrabbed);
    }

    inputApplyCursorVisibility();   /* hide the OS cursor if we start focused */
    inputApplyMouseRequests(); /* inputInit runs on the host thread */


    sysLogPrintf(LOG_INFO, "input: ready (mask=0x%x, %d controller(s), aimSpeed=%d)",
                 connectedMask, numControllers, mouseAimSpeed);
    return connectedMask;
}

void inputDestroy(void)
{
    for (int i = 0; i < MAX_PADS; ++i) {
        if (pads[i]) {
            SDL_GameControllerClose(pads[i]);
            pads[i] = NULL;
        }
        padInst[i] = -1;
        padRumbleOn[i] = 0;   /* D401: closed */
    }
    if (SDL_WasInit(SDL_INIT_GAMECONTROLLER)) {
        SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
    }
}

/* Poll once per controller-read: refresh SDL device state and integrate the
 * mouse-aim delta into the accumulator. */
/* D450: controller hot-plug. SDL_CONTROLLERDEVICEADDED / REMOVED land on
 * whichever of the two event pumps dequeues them first: video.c's host loop
 * or ge_render's window pump (ge_render_poll_events) (render thread, every frame), and the
 * latter used to drop them, so a replugged pad was never reopened. Both pumps
 * now only post a request; the rescan runs here, at the top of inputUpdate(),
 * on the thread that is about to read pads[], so a pad is never closed under
 * a reader. */
static SDL_atomic_t s_rescanReq;

void inputRequestRescan(void)
{
    SDL_AtomicSet(&s_rescanReq, 1);
}

void inputUpdate(void)
{
    if (SDL_AtomicSet(&s_rescanReq, 0)) inputRescanPads();   /* D450 */
#ifdef __SWITCH__
    {   /* D974: SDL sends no add/remove events for npads; watch the mask */
        static int tick = 0;
        if (++tick >= 30) {
            tick = 0;
            if (s_nxPadMask >= 0 && nxPadAllowedMask() != s_nxPadMask) inputRescanPads();
        }
    }
    nxRumbleTick();   /* #40 */
#endif
    SDL_GameControllerUpdate();
    if (s_seatCompactOwed && !inputMpSplit()) {
        inputOpenPads();   /* DNEW-mp-1: match over -> compact the seats */
    }

    /* D287: skip until the host thread has actually enabled relative mode
     * (a benign racy read of a host-thread int), so the few ms between the
     * logical grab and its application do not read absolute cursor motion. */
    if (!g_inputMouseEnabled || !g_inputMouseGrabbed || g_inputAppRelative != 1) {
        return;
    }

    int dx = 0, dy = 0;
    SDL_GetRelativeMouseState(&dx, &dy);

    /* Raw px; per-mode sensitivity is applied in inputComputePad(). Accumulate
     * in case inputUpdate() is polled more than once between pad reads. */
    g_inputMouseDX += dx;
    g_inputMouseDY += dy;
}

/* D416: crouch state is per controller slot (slot 0 only in solo). */
static int s_crouchLatchA[4], s_crouchHeldPrevA[4], s_crouchAppliedA[4];
static struct player *s_crouchPlayerA[4];
#define CSLOT() (g_inputSlot > 0 && g_inputSlot < 4 ? g_inputSlot : 0)
#define s_crouchLatch    (s_crouchLatchA[CSLOT()])
#define s_crouchHeldPrev (s_crouchHeldPrevA[CSLOT()])
#define s_crouchApplied  (s_crouchAppliedA[CSLOT()])   /* port-owned stance; not the native C-down crouch */
#define s_crouchPlayer   (s_crouchPlayerA[CSLOT()])
static int s_useHeldPrev = 0, s_reloadHeldPrev = 0;

/* GEPD-style crouch operates on the game's existing stance field, without
 * holding the aim button. The game interpolates ducking_height_offset from
 * crouchpos (bondview2.c:7038-7074). Only undo a stance we applied; preserve
 * auto-crouch in tight spaces (autocrouchpos is a separate minimum). */
static void inputDropCrouch(void)
{
    /* The engine can take over the stance (tank, respawn, etc.) between
     * polls; do not undo its new value or write into a different player. */
    if (s_crouchApplied && inputSlotPlayer() == s_crouchPlayer &&
        inputSlotPlayer()->crouchpos == CROUCH_SQUAT)
        inputSlotPlayer()->crouchpos = CROUCH_STAND;
    s_crouchApplied = 0;
    s_crouchPlayer = NULL;
}

/* Re-derive binds after an F10 input option or future key capture
 * (optionsoverlay rowSetCommit hook, scheduler thread).
 * g_inputBind is read per-frame by the game thread's inputActHeld(): a mid-poll
 * rebuild can at worst drop or add one scancode for one frame (the
 * D214 startup-rebuild is the same thread-pairing, just at boot).
 * Also drops the crouch latch on a layout/mode change. */
void inputBindingsApply(void)
{
    s_crouchLatch = 0;
    s_crouchHeldPrev = 0;
    inputDropCrouch();
    inputRebuildBinds();
    sysLogPrintf(LOG_INFO, "input: PC bindings, crouch %s applied",
                 crouchMode ? "toggle" : "hold");
}

/* Direct use/reload and modern face buttons share the same game-state gate.
 * In watch, pause, tank, front end or scripted cameras use native menu bits. */
static int inputCanUseGameplayActions(int menuMode)
{
    return !menuMode && inputSlotPlayer() && !inputSlotPlayer()->bonddead &&
           inputSlotPlayer()->outside_watch_menu && !inputSlotPlayer()->pause_state &&
           !inputSlotPlayer()->mpmenuon && !g_PlayerIsInTank &&
           !lvlGetControlsLockedFlag() && !gameScriptedCameraActive();
}

/* D394: the Xbox B gadget cycle has no N64 button equivalent (native A
 * deliberately skips IDs >= ITEM_BOMBCASE). Select the next owned,
 * equippable inventory gadget using the game's normal weapon-switch path;
 * never conjure a missing item or touch the N64 game logic. The first press
 * from a gun selects the lowest gadget; the next press wraps the list. */
static void inputCycleGadget(void)
{
    if (inputMpSplit()) return;   /* D419: acts on g_CurrentPlayer; solo only */
    struct player *p = inputSlotPlayer();
    if (!p || !p->ptr_inventory_first_in_cycle) return;

    int current = get_next_weapon_in_cycle_for_hand(GUNRIGHT, 1);
    int first = -1, next = -1;
    InvItem *head = p->ptr_inventory_first_in_cycle;
    InvItem *item = head;
    do {
        if (item->type == INV_ITEM_WEAPON) {
            int id = item->type_inv_item.type_weap.weapon;
            if (id >= ITEM_BOMBCASE && id <= ITEM_WATCHMAGNETATTRACT) {
                if (first < 0 || id < first) first = id;
                if (id > current && (next < 0 || id < next)) next = id;
            }
        }
        item = item->next;
    } while (item && item != head);

    if (next < 0) next = first;
    if (next >= 0 && next != current) {
        gunRequestHandWeaponChange(GUNRIGHT, next, 1);
        gunRequestHandWeaponChange(GUNLEFT, ITEM_UNARMED, 1);
    }
}

/* ------------------------------------------------------------------------
 * The pad control style: the adapter around port/src/input/style_modern.c.
 * Snapshot = the pad + the game's context; the strategy of the seat's style
 * returns commands; the N64 bridge (input/n64bridge.c) turns them into the pad
 * bits the game reads. What touches the game (aim models, crouch, the gadget
 * cycle) stays here.
 * ------------------------------------------------------------------------ */
_Static_assert(PSM(PS_A) == INB_A && PSM(PS_B) == INB_B && PSM(PS_X) == INB_X && PSM(PS_Y) == INB_Y &&
               PSM(PS_LB) == INB_LB && PSM(PS_RB) == INB_RB && PSM(PS_LS) == INB_LS && PSM(PS_RS) == INB_RS &&
               PSM(PS_START) == INB_START && PSM(PS_DUP) == INB_DUP && PSM(PS_DDOWN) == INB_DDOWN &&
               PSM(PS_DLEFT) == INB_DLEFT && PSM(PS_DRIGHT) == INB_DRIGHT &&
               PSM(PS_LT) == INB_LT && PSM(PS_RT) == INB_RT, "pad source bits must match INB_*");
_Static_assert((int)PA_FIRE == (int)INACT_FIRE && (int)PA_AIM == (int)INACT_AIM && (int)PA_USE == (int)INACT_USE && (int)PA_RELOAD == (int)INACT_RELOAD &&
               (int)PA_CROUCH == (int)INACT_CROUCH && (int)PA_NEXTWPN == (int)INACT_NEXTWPN &&
               (int)PA_START == (int)INACT_START && (int)PA_COUNT == (int)INACT_COUNT,
               "pad actions must match INACT_*");
_Static_assert(STICK_MAX == INPUT_STICK_MAX && RSTICK_THRESHOLD == INPUT_RSTICK_THRESHOLD, "stick constants");

static InputStrategyState s_padState[MAX_PADS];   /* per slot: the look low-pass */

static int padAimScale(void *user, int v, int sens)
{
    extern int nxAimStickP(int v, int sens, int slot);   /* nxgame.c: sens, Game.AimZoom, Game.AimAssist (D960: per slot) */
    return nxAimStickP(v, sens, (int)(intptr_t)user);
}

/* Builds the snapshot of slot idx and runs its seat's strategy. `pad` is
 * slotPad(idx): NULL when the keyboard plays the seat, in which case only the
 * Modern aim assists still read the pad (pads[idx]); the other styles have
 * nothing to do then. Returns the strategy that ran (commands in *cmd), or NULL
 * (commands untouched). */
static const InputStrategy *padPoll(int idx, SDL_GameController *pad, int wheelOpen, InputCommands *cmd)
{
    const InputStrategy *strat = &g_inputStyleModern;
    SDL_GameController *dev = pad ? pad : (inputSeatShift() ? NULL : pads[idx]);   /* behind the keyboard player 1, pads[idx] is another player's */
    if (!dev) return NULL;

    InputSnapshot in;
    memset(&in, 0, sizeof in);
    u32 raw = padReadRaw(dev);
    if (pad) raw = padLatchApply(idx, raw);   /* D961 */
    in.buttons = raw;
    in.lx = SDL_GameControllerGetAxis(dev, SDL_CONTROLLER_AXIS_LEFTX);
    in.ly = SDL_GameControllerGetAxis(dev, SDL_CONTROLLER_AXIS_LEFTY);
    in.rx = SDL_GameControllerGetAxis(dev, SDL_CONTROLLER_AXIS_RIGHTX);
    in.ry = SDL_GameControllerGetAxis(dev, SDL_CONTROLLER_AXIS_RIGHTY);
    in.triggerL = SDL_GameControllerGetAxis(dev, SDL_CONTROLLER_AXIS_TRIGGERLEFT);
    in.triggerR = SDL_GameControllerGetAxis(dev, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
    in.family = padFamily[padSeatOfPad(dev)];   /* button names: the physical pad */
    in.prevActions = padActPrev[idx];

    struct player *mp = inputSlotPlayer();
    const int menu = (current_menu != GE_MENU_RUN_STAGE && current_menu != GE_MENU_INVALID);
    in.ctx.menu = menu;
    in.ctx.wheelOpen = wheelOpen;
    in.ctx.padDrives = (pad != NULL);
    if (pad) {
        in.ctx.facePlayable = (idx == 0 || inputMpSplit()) && inputCanUseGameplayActions(menu);
        in.ctx.tank = !in.ctx.facePlayable && idx == 0 && !menu && g_PlayerIsInTank == 1 &&
                      mp && mp->outside_watch_menu && !mp->pause_state;
    }
    in.ctx.inStage = (current_menu == GE_MENU_RUN_STAGE);
    in.ctx.aiming = mp && mp->insightaimmode;
    in.ctx.dead = mp && mp->bonddead;
    if (in.ctx.inStage && in.ctx.aiming && !in.ctx.dead)   /* weapon lookup only when it can matter */
        in.ctx.scopedWeapon = bondwalkItemCheckBitflags(mp->hands[GUNRIGHT].weaponnum, WEAPONSTATBITFLAG_DISABLE_CROUCH) != 0;

    in.cfg.deadzoneL = padDeadzoneL;
    in.cfg.deadzoneR = padDeadzoneR;
    in.cfg.lookSensX = padLookSensX;
    in.cfg.lookSensY = padLookSensY;
    in.cfg.aimLookSens = padAimLookSens;
    in.cfg.lookSmooth = padLookSmooth;
    in.cfg.lookInvertY = padLookInvertY;
    in.cfg.triggerPct = padTriggerPct;
    in.cfg.naturalPitch = styleIsSolitare(idx);

    in.aimScale = padAimScale;
    in.aimScaleUser = (void *)(intptr_t)idx;

    strat->map(&in, &s_padState[idx], cmd);
    {   /* GameDirect (#78): the harness (script / record / replay) is the sole source of its slot, so the pad stays digital there */
        int w = 0, sd = 0;
        cmd->direct = !(idx == 0 && (g_inputReplayActive || inputScriptIsActive(0) || g_inputRec != NULL)) &&
                      inputDirectMoveDecide(&in.ctx, cmd, &w, &sd);
        cmd->walk = w;
        cmd->strafe = sd;
    }
    return strat;
}

/* GameDirect (#78): poll thread -> game thread, one cell per player (= N64 slot). The poll thread publishes
 * the latest decision (never re-points g_CurrentPlayer, D419); the game thread reads it from the hook in
 * bondview2.c. A stale cell (no poll for 250 ms: paused scheduler, detached pad) reads as "not direct". */
static struct { int walk, strafe, look; Uint32 stamp; int valid; } s_direct[MAX_PADS];   /* look: the look stick is deflected */

static void padDirectPublish(int idx, int direct, int walk, int strafe, int look)
{
    if (idx < 0 || idx >= MAX_PADS) return;
    __atomic_store_n(&s_direct[idx].look, look, __ATOMIC_RELAXED);
    __atomic_store_n(&s_direct[idx].walk, walk, __ATOMIC_RELAXED);
    __atomic_store_n(&s_direct[idx].strafe, strafe, __ATOMIC_RELAXED);
    __atomic_store_n(&s_direct[idx].stamp, SDL_GetTicks(), __ATOMIC_RELAXED);
    __atomic_store_n(&s_direct[idx].valid, direct, __ATOMIC_RELEASE);
}

/* The hook in src/game/bondview2.c: the left stick's analog walk / strafe of `playernum`, in the game's stick
 * scale (what analogWalk / analogStrafe hold). 0 = the game's own movement stands. */
int portDirectMove(int playernum, int *walk, int *strafe)
{
    if (playernum < 0 || playernum >= MAX_PADS) return 0;
    if (!__atomic_load_n(&s_direct[playernum].valid, __ATOMIC_ACQUIRE)) return 0;
    if (SDL_GetTicks() - __atomic_load_n(&s_direct[playernum].stamp, __ATOMIC_RELAXED) > 250) return 0;
    *walk = __atomic_load_n(&s_direct[playernum].walk, __ATOMIC_RELAXED);
    *strafe = __atomic_load_n(&s_direct[playernum].strafe, __ATOMIC_RELAXED);
    return 1;
}

/* #85 Look ahead under GameDirect, read by the hook in src/game/bondview2.c after the 1.x styles set
 * disableLookAhead. The game's own look-ahead is off in SOLITARE / GOODNIGHT (the N64 stick is the view
 * there); with GameDirect the left stick walks and the right stick looks, so the reason is gone.
 *   0 not GameDirect: the game's own rules stand (HONEY roles, scripted slot 0)
 *   1 Original level, look stick at rest: the game's look-ahead runs (slope target, thresholds, centring)
 *   2 Original level, look stick deflected: the game's look-ahead runs, the hook treats the stick as a
 *     manual pitch (C-up / C-down on the N64: cancel, disarm, block until the walk stick is released)
 *   3 Low / Medium / High: the game's centre stays off, nxLookAheadStep (nxgame.c) eases instead */
int portLookAheadMode(int playernum)
{
    extern int nxLookAheadLevel(void);   /* nxgame.c: 0 Original, 1 Low, 2 Medium, 3 High */
    int w, sd;
    if (!portDirectMove(playernum, &w, &sd)) return 0;
    if (nxLookAheadLevel() != 0) return 3;
    return __atomic_load_n(&s_direct[playernum].look, __ATOMIC_RELAXED) ? 2 : 1;
}

/* Fill button mask + stick for controller idx. Returns the 16-bit mask. */
static unsigned inputComputePadSlot(int idx, signed char *stick_x, signed char *stick_y);
unsigned inputComputePad(int idx, signed char *stick_x, signed char *stick_y)
{
    unsigned r;
    if (idx > 0 && inputMpSplit() && idx >= getPlayerCount()) {   /* D416: no player owns this slot */
        if (stick_x) *stick_x = 0;
        if (stick_y) *stick_y = 0;
        return 0;
    }
    g_inputSlot = idx;
    r = inputComputePadSlot(idx, stick_x, stick_y);
    g_inputSlot = -1;
    return r;
}

static unsigned inputComputePadSlot(int idx, signed char *stick_x, signed char *stick_y)
{
    unsigned button = 0;
    int sx = 0, sy = 0;
    int crouchNow = 0;
    /* GameDirect, the keyboard part: when set, the move keys go to the game as analog walk / strafe (merged with
     * the stick at the publish below) instead of C-button steps, so a stick at rest can't cancel them. */
    int kbDirect = 0, kbFwd = 0, kbBack = 0, kbLeft = 0, kbRight = 0, kbLook = 0;

    /* D194/D238: self-correcting every poll -- cheap (plain field writes,
     * see options.c cur_player_set_control_type), and re-asserts itself if
     * anything else ever calls the setter (menu, save load) in between. */
    if ((idx == 0 || inputMpSplit()) && inputSlotPlayer() != NULL &&
        (idx == 0 || idx < getPlayerCount())) {   /* D416: each slot rewrites only its own player's style */
        int wantSolitare = stickSolitare(idx) ? CONTROLLER_CONFIG_SOLITARE_ : CONTROLLER_CONFIG_HONEY_;   /* D498, D518 */
        struct player *sp = inputSlotPlayer();
        if (!inputMpSplit()) {
            if (cur_player_get_control_type() != wantSolitare)
                cur_player_set_control_type(wantSolitare);
        } else if (sp->cur_player_control_type_0 != wantSolitare) {
            /* mirror of options.c cur_player_set_control_type on the slot's own player */
            sp->cur_player_control_type_0 = wantSolitare;
            sp->cur_player_control_type_1 = wantSolitare;
            sp->cur_player_control_type_2 = (float)wantSolitare;
            sp->neg_vspacing_for_control_type_entry = -((j_text_trigger ? 14 : 10) * wantSolitare);
            sp->has_set_control_type_data = TRUE;
        }
    }

    if (idx < 0 || idx >= MAX_PADS) {
        if (stick_x) *stick_x = 0;
        if (stick_y) *stick_y = 0;
        return 0;
    }

    /* F10 options overlay: while it is open, controller 0 is fully swallowed
     * (neutral pad, no stick) and the nav keys / wheel / gamepad drive the
     * overlay instead. Mirrors the WI-1 "cursor free in a stage -> withhold
     * input" pattern. Controllers 1-3 are untouched. */
    if (idx == 0 && !optionsOverlayIsOpen()) { s_ovDev = -1; s_ovPlayer = 0; }   /* ownership lasts one open */

    /* D472: the slot that plays the owner pad is swallowed too (neutral) while the overlay
     * is open; slot 0 below still hosts the overlay tick. */
    if (idx != 0 && optionsOverlayIsOpen() && slotPad(idx) && padSeatOfPad(slotPad(idx)) == inputOverlayOwnerPad()) {
        padActPrev[idx] = padActHeld(&padTabGet()->seat[idx], padReadRaw(slotPad(idx)));
        if (stick_x) *stick_x = 0;
        if (stick_y) *stick_y = 0;
        return 0;
    }

    /* Modern: a wheel is open on this pad (LB / RB held). The pad goes on walking and pressing; only the aim is the
     * wheel's: the right stick is read by the wheel itself (wheel.c), so the game sees it at rest. */
    const int wheelOpen = portWheelBlocks(idx);

    /* The press that closed the menu (Resume, Back...) must not reach the game as a new press: after the menu closes,
     * every pad stays neutral until its buttons are let go. Same when the pads move one slot behind the keyboard
     * (inputSeatShift: the co-op / MP character select): the A that confirmed the mode menu on slot 0 would otherwise
     * reach the next slot as a new press and pick that player's character. */
    {
        static int s_menuWasOpen = 0, s_shiftWas = 0;
        static u8 s_untilRelease[MAX_PADS];
        if (idx == 0) {
            const int open = optionsOverlayIsOpen();
            if (s_menuWasOpen && !open) memset(s_untilRelease, 1, sizeof s_untilRelease);
            s_menuWasOpen = open;
            const int shift = inputSeatShift();
            if (shift != s_shiftWas) memset(s_untilRelease, 1, sizeof s_untilRelease);
            s_shiftWas = shift;
        }
        if (idx >= 0 && idx < MAX_PADS && s_untilRelease[idx]) {
            const u32 held = slotPad(idx) ? padActHeld(&padTabGet()->seat[idx], padReadRaw(slotPad(idx))) : 0;
            if (held) {
                padActPrev[idx] = held;
                if (stick_x) *stick_x = 0;
                if (stick_y) *stick_y = 0;
                return 0;
            }
            s_untilRelease[idx] = 0;
        }
    }

    /* the device screen takes every press */
    if (portAssignBlocks()) {
        padActPrev[idx] = slotPad(idx) ? padActHeld(&padTabGet()->seat[idx], padReadRaw(slotPad(idx))) : 0;
        if (stick_x) *stick_x = 0;
        if (stick_y) *stick_y = 0;
        return 0;
    }

    if (idx == 0 && optionsOverlayIsOpen()) {
        const Uint8 *overlayKs = SDL_GetKeyboardState(NULL);
        /* D469: pad use/reload/edge state follows the bound actions, not fixed A/X/B/Y */
        u32 ovHeld = slotPad(0) ? padActHeld(&padTabGet()->seat[0], padReadRaw(slotPad(0))) : 0;
        s_useHeldPrev = inputActHeld(overlayKs, IA_CANCEL) || (ovHeld & (1u << PA_USE));
        s_reloadHeldPrev = inputActHeld(overlayKs, IA_RELOAD) || (ovHeld & (1u << PA_RELOAD));
        s_crouchLatch = 0;
        s_crouchHeldPrev = 0;
        inputDropCrouch();
        padActPrev[0] = ovHeld;
        /* D472: the OWNER pad's Select closes the overlay; every pad's edge is
         * tracked here and on the open path alike, so a button held across
         * the transition cannot immediately re-toggle it. */
        {
            int own = inputOverlayOwnerPad();
            int closeNow = 0;
            for (int i = 0; i < MAX_PADS; i++) {
                int selNow = pads[i] ? SDL_GameControllerGetButton(pads[i], SDL_CONTROLLER_BUTTON_BACK) : 0;
                if (i == own && selNow && !padSelectPrev[i]) closeNow = 1;
                padSelectPrev[i] = selNow;
            }
            if (closeNow) optionsOverlayToggle();
        }
        optionsOverlayHandleInput();
        /* D567: only while the mouse is the active input -- with a pad (Deck) the
         * untouched OS pointer sits at 0,0 and dragged the front end's own
         * crosshair to the top-left corner; it now stays where it was. */
        if (g_inputMouseEnabled && current_menu != GE_MENU_RUN_STAGE && current_menu != GE_MENU_INVALID &&
            optionsOverlayMouseActive()) {
            overlayFrontEndCrosshair();   /* D519: the game cursor tracks the mouse (D555: the overlay pointer is drawn exactly over it) */
            /* D544: the OS cursor stays visible (inputSuspendForOverlay) -- the crosshair
             * draws under the overlay box, so it alone could not point at F10 rows. */
        }
        if (stick_x) *stick_x = 0;
        if (stick_y) *stick_y = 0;
        s_menuPointerLive = 0;   /* D345(b): the overlay owns the mouse */
        return 0;
    }

    /* ---- keyboard + mouse: controller 0 only (not when pads own P1, D416) ---- */
    if (idx == 0 && !inputMpPadsOwnP1() && !kbmBlockedForP1()) {
        const Uint8 *ks = SDL_GetKeyboardState(NULL);
        Uint32 mb = g_inputMouseEnabled ? SDL_GetMouseState(NULL, NULL) : 0;
        int menuMode = (current_menu != GE_MENU_RUN_STAGE &&
                        current_menu != GE_MENU_INVALID);

        /* D345: detect front-end screen changes (any current_menu flip,
         * including into the F10 overlay's parent state)
         * and open a short re-assert window. Runs every poll so the
         * countdown drains even when the pointer block below is skipped
         * (mouse disabled, unfocused window, legacy velocity mode). */
        if ((int)current_menu != menuPointerLastMenuId) {
            menuPointerLastMenuId = (int)current_menu;
            menuPointerTransitionFrames = 3;   /* this poll + two more */
        }
        if (menuPointerTransitionFrames > 0) menuPointerTransitionFrames--;

        /* D194: aim mode drives the view from GRABBED relative deltas (see
         * aimAbsCompute) -- the cursor stays hidden and clipped to the window
         * for the whole hold. The earlier free-cursor experiment (suspend the
         * grab, read absolute position) is reverted: the visible cursor could
         * leave the window and OS micro-jitter read as view jitter.
         * g_inputAbsAimSuspend therefore stays 0; the readers that honour it
         * (inputReconcileGrab/inputApplyCursorVisibility/button mask) behave exactly as
         * pre-D194. */
        g_inputAbsAimSuspend = 0;
        inputReconcileGrab(menuMode);
        /* D337 harness: scripted sustained mouse replaces the real deltas and
         * behaves as a grabbed mouse (headless windows are never focused). */
        if (inputScriptIsActive(0)) {
            inputScriptRefreshHolds(0);
            if (g_inputScriptSlots[0].mouseOn) {
                g_inputMouseDX = (double)g_inputScriptSlots[0].mdx;
                g_inputMouseDY = (double)g_inputScriptSlots[0].mdy;
                g_inputMouseGrabbed = 1;
                /* D597: scripted sustained mouse buttons (MLB/MRB) OR into the
                 * polled mask; a headless window has no real mouse state. */
                if (g_inputScriptSlots[0].mlb) mb |= SDL_BUTTON(1);
                if (g_inputScriptSlots[0].mrb) mb |= SDL_BUTTON(2);
            }
        }
        /* D196: the F10 options overlay forces the OS cursor visible via
         * inputSuspendForOverlay() but nothing re-hides it on close unless
         * inputReconcileGrab() happens to re-grab (only true if you had already
         * clicked-to-lock in a stage). Re-assert the correct visibility
         * every poll here -- this line is unreachable while the overlay is
         * open (early return above), so it only fires once it has closed. */
        inputApplyCursorVisibility();

        /* Click-to-lock, in a stage, cursor free: the mouse buttons must not
         * reach the game (no phantom fire) -- the first click only re-locks
         * (handled in video.c -> inputNotifyClick). Menus keep their buttons. */
        /* ...but not while an aim hold has the cursor out for absolute aim:
         * then the buttons are intentional (RMB is the hold itself, LMB fires). */
        if (!g_inputMouseGrabbed && !menuMode && !g_inputAbsAimSuspend) {
            mb = 0;
        }

        /* GE default control (1.1): stick Y = move fwd/back, stick X = turn,
         * C-left/right = sidestep, C-up/down = look. FPS layout: W/S move,
         * A/D strafe (C-buttons), mouse X turns (stick X), mouse Y looks. */
        /* D194/D238 natural-pitch mode (GE's own 1.2/SOLITARE style) reads
         * forward/back from digital C-up/C-down instead of the analog stick
         * (bondview2.c: digitalStepForward/Back <- U/D_JPAD|U/D_CBUTTONS),
         * because the stick's Y axis is what carries continuous analog
         * pitch there instead. Strafe and turn are unchanged -- both
         * schemes read them the same way. */
        /* D345(d): natural pitch is an in-stage aim scheme -- the N64 front
         * end navigates on the analog stick only, so in menus W/S (and the
         * Up/Down arrows) must emit stick even with NaturalPitch=1, or no
         * front-end screen (file select, cheat) can be
         * driven vertically from the keyboard. Watch/pause keeps the C-button
         * mapping: it is current_menu==RUN_STAGE (menuMode 0), N64-style. */
        /* GameDirect (the same rules as the pad's inputDirectMoveDecide): W/S/A/D walk and
         * strafe at full analog scale, through the hook in bondview2.c, not as C steps. The pad's GameDirect
         * zeroes the game's digital steps, so the C bits alone stopped as soon as a pad was plugged in. Aiming
         * (the C-bit slow strafe), the tank, the watch and the QA harness keep the C bits. */
        kbDirect = styleIsSolitare(idx) && inputCanUseGameplayActions(menuMode) &&
                   !(inputSlotPlayer() && inputSlotPlayer()->insightaimmode) &&
                   !(g_inputReplayActive || inputScriptIsActive(0) || g_inputRec != NULL);
        kbFwd = inputActHeld(ks, IA_FORWARD);
        kbBack = inputActHeld(ks, IA_BACK);
        kbLeft = inputActHeld(ks, IA_STRAFE_L);
        kbRight = inputActHeld(ks, IA_STRAFE_R);
        kbLook = (g_inputMouseDY != 0.0);   /* the mouse pitches: the "look stick deflected" of portLookAheadMode */
        if (kbDirect) {
            /* walk / strafe go out at the GameDirect publish below */
        } else if (styleIsSolitare(idx) && !menuMode) {   /* D498, D513, D518 */
            if (kbFwd)  button |= GE_CONT_E;   /* C-up = forward   */
            if (kbBack) button |= GE_CONT_D;   /* C-down = back    */
        } else {
            if (kbFwd)  sy =  STICK_MAX;
            if (kbBack) sy = -STICK_MAX;
        }
        if (!kbDirect && kbLeft)  button |= GE_CONT_C;   /* strafe left  */
        if (!kbDirect && kbRight) button |= GE_CONT_F;   /* strafe right */
        if (inputActHeld(ks, IA_TURN_L))   sx = -STICK_MAX;       /* keyboard turn */
        if (inputActHeld(ks, IA_TURN_R))   sx =  STICK_MAX;
        if (sx) g_inputAimDevMouse = 0;   /* D337: keyboard stick turn is the active aim device */

        if (inputActHeld(ks, IA_FIRE))
            button |= GE_CONT_G;
        /* aimButton = the Aim bind (emits the game's R);
         * aimHeld = "the game is in aim mode" for routing the mouse. D337: the
         * game's aim buttons in 1.1/1.2 are L|R (bondview2.c aimButtons), so Q
         * ("LeanLeft", sends L) is a second aim button -- it used to leave the
         * mouse on the hipfire camera path, the accidental "centred aim" #104
         * found. Routing also follows the game's own insightaimmode, which
         * covers the Aim Control "Toggle" option and gamepad aim. The R emit
         * stays tied to the physical button so Toggle still sees edges. */
        int aimButton = inputActHeld(ks, IA_AIM);
        int aimHeld = aimButton || inputActHeld(ks, IA_LEAN_L) ||
                      (inputSlotPlayer() != NULL && inputSlotPlayer()->insightaimmode);
        int aimRisingEdgeAim = aimHeld && !AIMS.aimHeldPrev;
        if (aimRisingEdgeAim && inputSlotPlayer() && inputSlotPlayer()->docentreupdown)
            AIMS.centreClearTicks = 2;   /* see D194 centre-spring note above */
        if (!aimHeld) {
            AIMS.centreClearTicks = 0;
        }
        AIMS.aimHeldPrev = aimHeld;
        if (aimRisingEdgeAim && inputSlotPlayer() && inputSlotPlayer()->docentreupdown
            && configGetInputLog()) {
            sysLogPrintf(LOG_NOTE,
                "GE_INPUTLOG absaim centre-spring armed at aim entry; nudging to clear");
        }
        if (aimButton)
            button |= GE_CONT_R;
        if (inputActHeld(ks, IA_ACTION))
            button |= GE_CONT_A;
        if (wheelFwd > 0) {             /* wheel up: fresh A edge = cycle forward */
            button |= GE_CONT_A;
            wheelFwd--;
        } else if (wheelBack > 0) {     /* wheel down: A+Z together, not staggered.
             * D223 follow-up: staggering (A alone for a poll, THEN adding Z) races
             * the real game-tick rate -- if the two states land in separate ticks,
             * the "A alone" tick is itself a fresh A edge with no Z held, which
             * the game's own weaponForwardOffset formula reads as a genuine
             * cycle-FORWARD request (bondview2.c weaponForwardOffset/
             * weaponBackOffset, both control-scheme sites) *before* the
             * correcting backward tick runs -- so depending on real-time
             * poll/tick alignment (D117-class nondeterminism) a single wheel-down
             * notch could silently do a stray forward step, or forward-then-back
             * (net a skipped slot on wrap). Presenting A and Z together from the
             * very first poll means whichever single tick samples the 0->(A|Z)
             * transition sees them rising simultaneously; weaponForwardOffset
             * requires Z NOT held, so it's unambiguous -- only backward fires,
             * every time, regardless of tick timing. moveData.triggerOn (actual
             * fire) is separately gated off while A/invButtons is held, so this
             * doesn't risk an accidental shot either. */
            button |= GE_CONT_A | GE_CONT_G;
            wheelBack--;
        }
        /* D378/D393: GE's B tap calls bond_interact_object(), then reloads
         * only when no target was found (lv.c:796). E and pad A use the same
         * interaction with NO fallback; R and pad X only reload. Their
         * combined edges prevent a held input from repeatedly using a door.
         * In menus/watch/tank the pad keeps native accept/cancel buttons. */
        SDL_GameController *slot0Pad = slotPad(0);
        u32 slot0Held = slot0Pad ? padActHeld(&padTabGet()->seat[0], padReadRaw(slot0Pad)) : 0;
        int padUse = (slot0Held & (1u << PA_USE)) != 0;       /* D469: bound actions (Jinx 1.1: A / X) */
        int padReload = (slot0Held & (1u << PA_RELOAD)) != 0;
        int useNow = inputScriptIsActive(0) ? g_inputScriptSlots[0].use : (inputActHeld(ks, IA_CANCEL) || padUse);
        int reloadNow = inputScriptIsActive(0) ? g_inputScriptSlots[0].reload : (inputActHeld(ks, IA_RELOAD) || padReload);
        inputReplayFetch();
        if (g_inputReplayActive) { useNow = g_inputRepCur.use; reloadNow = g_inputRepCur.reload; }
        g_inputRecUse = useNow; g_inputRecReload = reloadNow;
        int playable = inputCanUseGameplayActions(menuMode);
        /* D407: on the N64 the B bit also drives bondview2.c's tank
         * handlers (board when g_BondCanEnterTank, exit while
         * g_PlayerIsInTank). The D378/D393 split above routes E/pad A to a
         * dedicated interact call without presenting B, which left tank
         * board/exit dead on PC (the tank blocks movement but can never be
         * entered -- Runway, Streets). Present B only in the tank states so
         * E keeps its no-reload-fallback semantics everywhere else.
         * In the tank states the dedicated interact call is ALSO skipped:
         * on the N64 the use/reload fallback (lv.c:796) runs on the B bit
         * AFTER the board handler in the same tick, so pre-activating the
         * tank prop on the input side (propobjInteract ->
         * RUNTIMEBITFLAG_ACTIVATED, propobj.c) reorders the N64 sequence
         * and flaps g_BondCanEnterTank between input sampling and the
         * board check (user: still can't board, 2026-09-28). */
        int tankState = (g_PlayerIsInTank == 1 || g_BondCanEnterTank != 0);
        /* D407 (cont.): board-animation lockout. The game's B-tap handler
         * (bondview2.c) is a toggle: while g_PlayerIsInTank is set, a B tap
         * is the EXIT path. Boarding commits a ~45-frame (0.75 s) sit blend
         * during which g_EnterTankAudioState stays TANK_RUN_STATE_NOT_RUNNING;
         * only once it finishes does the engine SFX start. A key bounce or a
         * player re-pressing E in that silent window ("nothing is happening")
         * hits the exit a few frames later and silently cancels the whole
         * animation -- 2026-09-28 playtest: every attempt was a
         * board->exit microcycle, so no seat, no engine, and the tank camera
         * transition reverted ("tries to do something, cancels out"). An N64
         * player simply presses once and waits; give the keyboard the same
         * guarantee in the port layer: suppress B from E while the entry
         * animation is running. Once seated, E = exit again. */
        static int s_tankBoardLock = 0;
        s_tankBoardLock = (g_PlayerIsInTank == 1 &&
                           g_EnterTankAudioState == TANK_RUN_STATE_NOT_RUNNING);
        if (playable) {
            if (useNow && !s_useHeldPrev && !tankState && inputMpSplit()) {
                button |= GE_CONT_B;   /* D419: native use (+ reload fallback); no cross-thread game call in a match */
            } else if (useNow && !s_useHeldPrev && !tankState) {
                bool empty = bond_interact_object();
                if (configGetInputLog()) sysLogPrintf(LOG_NOTE, "GE_INPUTLOG dedicated use (no target=%d)", empty);
            }
        } else if ((inputScriptIsActive(0) ? g_inputScriptSlots[0].use : inputActHeld(ks, IA_CANCEL)) && !tankState) {
            /* A on the pad is still native accept outside playable stages.
             * D407: NOT in tank states -- there B must come only from the
             * locked edge path below. This mapping is level-triggered on E;
             * in-tank (playable=false) it would re-present B on any E press,
             * bypassing the board-animation lockout and re-edging the game's
             * own exit toggle (2026-09-28: unprobed board->exit microcycles). */
            button |= GE_CONT_B;
        }
        if (useNow && !s_useHeldPrev && tankState && !s_tankBoardLock)
            button |= GE_CONT_B;
        if (reloadNow && !s_reloadHeldPrev && playable && inputMpSplit()) {
            button |= GE_CONT_B;
        } else if (reloadNow && !s_reloadHeldPrev && playable) {
            attempt_reload_item_in_hand(GUNRIGHT);
            attempt_reload_item_in_hand(GUNLEFT);
            if (configGetInputLog()) sysLogPrintf(LOG_NOTE, "GE_INPUTLOG dedicated reload (no use)");
        }
        s_useHeldPrev = useNow;
        s_reloadHeldPrev = reloadNow;
        crouchNow = inputActHeld(ks, IA_CROUCH);
        if (inputActHeld(ks, IA_LEAN_L))
            button |= GE_CONT_L;
        if (inputActHeld(ks, IA_START))
            button |= GE_CONT_START;

        /* Mouse-look. Mode-dependent (see the tuning-constants comment):
         *   aim mode  -> push the analog stick past +/-60 for proportional
         *                yaw + pitch; emit NO C-buttons (they mean crouch here).
         *   hipfire   -> yaw on analog stick-X; pitch on digital C-up/C-down.
         * "look down" convention: mouse-down looks down by default; GE's
         * native pitch is inverted so hipfire down = C-up (GE_CONT_E) and
         * aim-mode down = +stick_y. MouseInvertY flips both. */
        if (g_inputMouseEnabled) {
            double invert = mouseInvertY ? -1.0 : 1.0;

            /* Optional exponential low-pass (mouseSmoothing = blend % of the
             * previous poll). Off (0) => edx/edy are the raw deltas. */
            double edx = g_inputMouseDX, edy = g_inputMouseDY;
            if (mouseSmoothing > 0) {
                double a = mouseSmoothing / 100.0;
                if (a > 0.90) a = 0.90;
                mouseSmDX = mouseSmDX * a + edx * (1.0 - a);
                mouseSmDY = mouseSmDY * a + edy * (1.0 - a);
                edx = mouseSmDX;
                edy = mouseSmDY;
            }
            edy *= mouseYScale / 100.0;

            double dyLook = edy * invert;   /* >0 => look down */
            if (!menuMode && (fabs(edx) > 0.01 || fabs(dyLook) > 0.01))
                g_inputAimDevMouse = 1;          /* D337: mouse is the active aim device */

            /* D194(b): gameplay-look-only dt normalization -- see the
             * MOUSE_DT_REF comment above. Computed unconditionally (so the
             * clock stays warm across mode switches) but only ever applied
             * below, inside the aimHeld/hipfire branches. */
            double lookDtScale = 1.0;
            {
                Uint64 now = SDL_GetPerformanceCounter();
                if (s_lastLookPollCounter != 0) {
                    double freq = (double)SDL_GetPerformanceFrequency();
                    double dtActual = (double)(now - s_lastLookPollCounter) / freq;
                    if (dtActual > 0.0005) {   /* ignore sub-ms jitter / double polls */
                        lookDtScale = MOUSE_DT_REF / dtActual;
                        if (lookDtScale < MOUSE_DT_SCALE_MIN) lookDtScale = MOUSE_DT_SCALE_MIN;
                        if (lookDtScale > MOUSE_DT_SCALE_MAX) lookDtScale = MOUSE_DT_SCALE_MAX;
                    }
                }
                s_lastLookPollCounter = now;
            }
            if (!mouseDtDecouple) lookDtScale = 1.0;

            if (configGetInputLog() && !menuMode && (aimHeld || fabs(edx) > 0.01 || fabs(dyLook) > 0.01)) {
                sysLogPrintf(LOG_NOTE, "GE_INPUTLOG lookdt scale=%.3f raw=(%.2f,%.2f)",
                             lookDtScale, edx, dyLook);
            }

            if (menuMode) {
                /* Front-end menu pointer.
                 *
                 * front.c frontUpdateControlStickPosition() is the game's cursor
                 * integrator: it reads joyGetStickX/Y, applies a +/-5 deadband,
                 * clamps the stick to +/-70, then does
                 *   cursor_h_pos += (stick*0.075 +/- 0.5) * timerDelta
                 * and clamps cursor_h/v_pos into [screenleft+20 ..
                 * screenleft+screenwidth-20] x [screentop+20 ..
                 * screentop+screenheight-20]. That integrator caps the cursor at
                 * ~5.75 virtual px per poll, so feeding it a stick derived from
                 * an absolute mouse position (D165/D169 P-controller) is
                 * unavoidably laggy/floaty and desyncs from the real cursor.
                 *
                 * With click-to-lock the front end has a
                 * real free OS cursor, so we know exactly where the pointer is.
                 * Write cursor_h/v_pos DIRECTLY from the absolute mouse position
                 * whenever the mouse moved, and emit a zero stick so the game's
                 * integrator adds nothing (stick 0 -> deadband). When the mouse
                 * is idle we leave the cursor alone and let the keyboard stick
                 * (WASD/arrows, added above) drive it through the game as usual.
                 * cursor_h/v_pos are the plain front.c UI globals every menu
                 * hit-test already reads -- no logic change, just where the
                 * pointer is placed. */
                double loH = MENU_CURSOR_LO, hiH = MENU_CURSOR_HI_H;
                double loV = MENU_CURSOR_LO, hiV = MENU_CURSOR_HI_V;
                {
                    double sw = getPlayer_c_screenwidth(),  sh = getPlayer_c_screenheight();
                    double sl = getPlayer_c_screenleft(),   st = getPlayer_c_screentop();
                    if (sw > 200.0 && sw < 2000.0 && sh > 150.0 && sh < 2000.0) {
                        loH = sl + 20.0;  hiH = sl + sw - 20.0;
                        loV = st + 20.0;  hiV = st + sh - 20.0;
                    }
                }

                int haveAbs = 0;
                if (!g_inputMouseGrabbed) {
                    int mx = 0, my = 0;
                    SDL_GetMouseState(&mx, &my);
                    SDL_Window *w = SDL_GetMouseFocus();
                    int ww = 0, wh = 0;
                    if (w) SDL_GetWindowSize(w, &ww, &wh);
                    if (ww > 0 && wh > 0) {
                        haveAbs = 1;
                        if (!menuPrevActive) {
                            /* just entered a menu: adopt the current pointer as
                             * the baseline, don't yank the cursor this frame */
                            lastMenuMouseX = mx;
                            lastMenuMouseY = my;
                        }
                        /* D345: re-assert right after a screen change when the
                         * mouse was the recent navigation device (click-to-
                         * enter). A stale idle mouse (WASD/arrow navigation)
                         * keeps the cede-to-keyboard behaviour: no yank. */
                        Uint64 pcNow = SDL_GetPerformanceCounter();
                        int mouseRecent = (menuPointerLastAbsWrite != 0) &&
                            ((double)(pcNow - menuPointerLastAbsWrite) /
                             (double)SDL_GetPerformanceFrequency() * 1000.0) <
                            MENU_POINTER_REASSERT_MS;
                        if ((mx != lastMenuMouseX || my != lastMenuMouseY) ||
                            (menuPointerTransitionFrames > 0 && mouseRecent)) {
                            double fx = (double)mx / (double)ww;
                            double fy = (double)my / (double)wh;
                            /* D447: Video.AspectMode = Original draws into a
                             * letter/pillarboxed rect; map across that rect. */
                            {
                                double rx0, ry0, rx1, ry1;
                                videoGetOutputRectFrac(&rx0, &ry0, &rx1, &ry1);
                                if (rx1 > rx0 && ry1 > ry0) {
                                    fx = (fx - rx0) / (rx1 - rx0);
                                    fy = (fy - ry0) / (ry1 - ry0);
                                }
                            }
                            /* D335: under native widescreen the front end is
                             * pillarboxed to a centred 4:3 region
                             * (front.c menu_jump_constructor_handler), so map
                             * the pointer across that region, not the window. */
                            {
                                extern f32 portNativeAspect(void);
                                f32 na = portNativeAspect();
                                if (na > (4.0f / 3.0f)) {
                                    double vis = (4.0 / 3.0) / (double)na;   /* visible width fraction */
                                    fx = (fx - (1.0 - vis) * 0.5) / vis;
                                }
                            }
                            if (fx < 0.0) fx = 0.0; else if (fx > 1.0) fx = 1.0;
                            if (fy < 0.0) fy = 0.0; else if (fy > 1.0) fy = 1.0;
                            cursor_h_pos = (float)(loH + fx * (hiH - loH));
                            cursor_v_pos = (float)(loV + fy * (hiV - loV));
                            sx = 0;   /* pointer owns the cursor this poll */
                            sy = 0;
                            menuPointerLastAbsWrite = pcNow;
                        }
                        lastMenuMouseX = mx;
                        lastMenuMouseY = my;
                    }
                }

                if (configGetInputLog()) {
                    sysLogPrintf(LOG_NOTE,
                        "GE_INPUTLOG menuptr abs=%d cursor=(%.1f,%.1f) stick=(%d,%d)",
                        haveAbs, (double)cursor_h_pos, (double)cursor_v_pos, sx, sy);
                }
            } else if (inputSlotPlayer() != NULL &&
                       (!inputSlotPlayer()->outside_watch_menu ||
                        inputSlotPlayer()->pause_state != 0)) {
                /* D330 (#103): in-stage watch / pause -- the mouse drives
                 * nothing. menuMode is 0 here (current_menu is still
                 * RUN_STAGE) and inputHipDirectCompute declines on this same gate,
                 * so the legacy stick fallbacks below used to turn mouse
                 * motion into stick deflection (and an RMB hold ran the GEPD
                 * aim writes, which GEPD itself gates on !watch): a ~15 px flick
                 * crossed the watch's +/-0x2E left/right threshold, which on
                 * a selected option means "pick OFF/ON" (options.c
                 * game_option_toggle_input) and otherwise flips watch pages,
                 * while mouse Y scrolled the rows. Auto-aim / look-ahead
                 * "wouldn't stay on". Keyboard and pad still drive the watch
                 * exactly as before. */
            } else if (aimHeld) {
                /* D194 GEPD-mirror aim: direct crosshair/camera writes, no
                 * look stick (see inputHipDirectCompute). Keyboard turn (sx/sy set
                 * above) still works. Otherwise fall through to the legacy
                 * velocity stick below.
                 * D407 follow-up: in-tank the game aims the turret from raw
                 * stick deflection ONLY (bondview2.c HONEY/SOLITARE branch:
                 * |stick_x|/|stick_y| > 60 -> aimTurn/speedVerta), so the
                 * direct-write models must be bypassed there and the legacy
                 * velocity stick emitted instead. */
                int tankAimStick = (g_PlayerIsInTank == 1);
                if (!tankAimStick) {
                    /* Centred aim: the camera takes the mouse (same path as hipfire, which
                     * also declines on the watch/pause/cutscene gates) and no stick is
                     * emitted, so the crosshair settles at centre. */
                    inputHipDirectCompute(edx * lookDtScale, dyLook * lookDtScale);
                } else {
                double aimEdx = edx * lookDtScale, aimDyLook = dyLook * lookDtScale;
                double aimSens = (mouseAimSpeed / 100.0) * (g_inputMouseSensitivity / 100.0);
                if (tankAimStick) aimSens *= tankAimScale / 100.0;   /* D407 */
                double gamma = aimCurveGamma / 100.0;
                double normX = fabs(aimEdx) * aimSens / AIM_FULL_SPEED_PX;
                double normY = fabs(aimDyLook) * aimSens / AIM_FULL_SPEED_PX;
                if (normX > 1.0) normX = 1.0;
                if (normY > 1.0) normY = 1.0;
                int ceilStick = 60 + aimBand;
                if (ceilStick > AIM_STICK_GAME_MAX) ceilStick = AIM_STICK_GAME_MAX;
                if (fabs(aimEdx) >= AIM_MOVE_THRESH) {
                    int m = AIM_STICK_MIN + (int)(pow(normX, gamma) * (AIM_STICK_GAME_MAX - AIM_STICK_MIN));
                    if (m > ceilStick) m = ceilStick;
                    sx += (aimEdx > 0) ? m : -m;
                }
                if (fabs(aimDyLook) >= AIM_MOVE_THRESH) {
                    int m = AIM_STICK_MIN + (int)(pow(normY, gamma) * (AIM_STICK_GAME_MAX - AIM_STICK_MIN));
                    if (m > ceilStick) m = ceilStick;
                    sy += (aimDyLook > 0) ? m : -m;   /* +stick_y = look down */
                }
                } /* in a tank the game aims the turret from the raw stick */

                /* D194 centre-spring clear: a minimal pitch stick for a couple
                 * of ticks -- enough for bondview2's manual-input rule to clear
                 * docentreupdown, small enough (~0.1-0.3 deg) not to read as a
                 * jerk. Only when we are not already pitching this poll. */
                if (AIMS.centreClearTicks > 0 && inputSlotPlayer() &&
                    sy >= -60 && sy <= 60) {
                    sy = (inputSlotPlayer()->speedverta >= 0.0f) ? -61 : 61;
                    AIMS.centreClearTicks--;
                }
            } else {
                double hipEdx = edx * lookDtScale, hipDyLook = dyLook * lookDtScale;
                double hipSens = (g_inputMouseTurnSpeed / 100.0) * (g_inputMouseSensitivity / 100.0);
                /* WI-1: direct camera write, same linear px->degree model as
                 * aim mode (GEPD's hipfire branch) -- bypasses the N64 stick's
                 * quadratic natural-turn curve entirely (D238/#89: that curve
                 * saturated at ~13 px/poll, making slow motion "almost
                 * unrecognised" and fast motion bang-bang). Falls through to
                 * the legacy stick path below when it declines (disabled, no
                 * player, or a safety gate is closed). */
                /* D426 (#116): in a tank the hull turns from stick X (analogTurn) and stick Y
                 * is drive, so the direct camera write is ignored/overwritten -- emit
                 * mouse X as stick turn (scaled by Input.TankAimScale) and drop Y. */
                int inTankHip = (g_PlayerIsInTank == 1);
                if (!inTankHip && g_inputMouseDirectLook && inputHipDirectCompute(hipEdx, hipDyLook)) {
                    inputSwayFeedTheta(g_inputHipThetaDegOut);   /* D428: hipfire gun turn-lag */
                    /* Pitch handled inside inputHipDirectCompute too; nothing left
                     * to do for yaw/pitch this poll. Digital pitch-pulse
                     * (naturalPitchMode==0) still applies below only in the
                     * legacy path, so skip both branches here. */
                } else {
                sx += (int)(hipEdx * hipSens * MOUSE_TURN_GAIN * (inTankHip ? tankAimScale / 100.0 : 1.0));
                if (inTankHip) {
                    /* no pitch/drive from the mouse in a tank */
                } else if (styleIsSolitare(idx)) {   /* D498, D513, D518 */
                    /* D194/D238: SOLITARE gives hipfire pitch the same
                     * continuous analog stick treatment as yaw -- same
                     * formula as the sx line above, so X and Y are, by
                     * construction, symmetric. Forward/back moved to
                     * digital C-up/C-down above, freeing the stick's Y axis
                     * for this. */
                    sy += (int)(hipDyLook * hipSens * MOUSE_TURN_GAIN);
                } else {
                    /* D166 (legacy): hipfire pitch as C-button pulses whose
                     * frequency scales with mouse-Y speed -- fast mouse =
                     * solid hold, slow = sparse taps. Kept as the
                     * Input.NaturalPitch=0 escape hatch. */
                    double sp = fabs(hipDyLook);
                    if (sp >= MOUSE_PITCH_THRESH) {
                        double duty = sp * (hipfirePitchSpeed / 100.0) * (g_inputMouseSensitivity / 100.0) / HIP_PITCH_FULL;
                        if (duty > 1.0) duty = 1.0;
                        hipPitchPhase += duty;
                        if (hipPitchPhase >= 1.0) {
                            hipPitchPhase -= 1.0;
                            button |= (hipDyLook > 0) ? GE_CONT_E : GE_CONT_D; /* E=C-up=look down */
                        }
                    } else {
                        hipPitchPhase = 0.0;
                    }
                }
                } /* end g_inputMouseDirectLook fallback (WI-1) */
            }
        }

        if (menuMode && g_inputMouseEnabled) {
            /* Clicks are select / back in the front end, not fire / aim. */
            button &= ~(GE_CONT_G | GE_CONT_R);
            if (mb & SDL_BUTTON(SDL_BUTTON_LEFT))  button |= GE_CONT_A;
            if (mb & SDL_BUTTON(SDL_BUTTON_RIGHT)) button |= GE_CONT_B;
        }
        /* D384: the PC settings editor MUST NOT navigate using live game
         * binds. Otherwise capturing A/D/C instantly turns that held key
         * into C-left/right or stick input, cycling the selected bind slot
         * and chasing the menu cursor (user repro, bind log). Physical arrow
         * keys and Enter/Escape are fixed menu controls; mouse/pad still
         * work. Scripted QA input remains the sole source when active. */
        menuPrevActive = menuMode;
        s_menuPointerLive = (menuMode && g_inputMouseEnabled && !g_inputMouseGrabbed) ? 1 : 0;

        g_inputMouseDX = 0.0;
        g_inputMouseDY = 0.0;
    }

    /* ---- gamepad ---- */
    SDL_GameController *pad = slotPad(idx);
    InputCommands mc;
    const InputStrategy *const strat = padPoll(idx, pad, wheelOpen, &mc);   /* the seat's control style decides, the blocks below apply it */
    {
        const int padDirect = (strat && pad) ? mc.direct : 0;
        int walk = padDirect ? mc.walk : 0, strafe = padDirect ? mc.strafe : 0;
        if (kbDirect) inputDirectMoveAddKeys(kbFwd, kbBack, kbLeft, kbRight, &walk, &strafe);   /* stick + keys, clamped */
        const int direct = padDirect || kbDirect;
        padDirectPublish(idx, direct, walk, strafe, (padDirect && mc.lookRawActive) || (kbDirect && kbLook));
    }
    if (pad) {
        /* D282: front-end menus (main menu, file select, mission-select map)
         * navigate with the left stick whatever the stick roles (the strategy
         * does that: INMODE_MENU) -- a menu-context remap only, no effect on in-level
         * control feel. */
        /* Sticks from the strategy (input/style_modern.c); the C bits go out through
         * the bridge below. The aim models and their side effects stay here. */
        if (mc.mode == INMODE_MENU) {
            if (mc.moveX) sx = mc.moveX;
            if (mc.moveY) sy = mc.moveY;
        } else if (mc.mode == INMODE_SOLITARE) {
            const int rxs = mc.lookX, rys = mc.lookY;
            if (idx == 0 && mc.lookRawActive) g_inputAimDevMouse = 0;   /* D337: pad aim active */
            if (idx == 0) { g_inputRecPadDX = rxs; g_inputRecPadDY = rys; g_inputRecPadDirect = 0; }
            {   /* Game.AimEntry (nxgame.c): the view follows the crosshair into the aim (D960: every slot, state per player) */
                extern void nxAimEntryStep(struct player *p);
                nxAimEntryStep(inputSlotPlayer());
                extern void nxAimStopStep(struct player *p, int moving);   /* Game.AimStop */
                nxAimStopStep(inputSlotPlayer(), mc.moveFwd || mc.moveBack || mc.strafeL || mc.strafeR ||
                                            gameScriptedCameraActive() || g_PlayerIsInTank == 1);
                extern void nxLookAheadStep(struct player *p, int walkingForward, int lookIdle);   /* Look ahead Low / Medium / High (#85) */
                nxLookAheadStep(inputSlotPlayer(), mc.moveFwd, rxs == 0 && rys == 0);
            }
            if (!(idx == 0 && g_inputReplayActive) &&
                inputPadDirectCompute(rxs, rys)) {
                if (idx == 0) g_inputRecPadDirect = 1;   /* D404: the stick drives the camera directly, no stick emitted */
                /* D404 (resolved by implementation, 2026-09-28): pad CENTRED
                 * aim -- the stick drives the camera directly (crosshair
                 * pinned at centre), the pad-side twin of the mouse CENTRED
                 * branch above, so the Aim-style toggle is no longer inert
                 * on a pad. No look stick is emitted this poll; when
                 * inputPadDirectCompute declines (dead/watch/pause/cutscene) the
                 * legacy stick emission below runs, as the mouse branch
                 * falls back to its legacy path. */
            } else {
                if (rxs) sx = rxs;
                if (rys) sy = rys;
            }
        } else {   /* INMODE_HONEY: the left stick is the analog stick (#145: the look stick in the 1.1 roles) */
            if (mc.moveX) sx = mc.moveX;
            if (mc.moveY) sy = mc.moveY;
            if (idx == 0 && mc.moveActive) g_inputAimDevMouse = 0;   /* D337: pad aim active */
        }

        /* D469: triggers/faces/shoulders/Start/D-pad via the data-driven
         * action table (preset 0 == the pre-D469 mapping: RT fire, LT/LB aim,
         * A use, X reload, B gadget, Y next weapon, stick clicks crouch,
         * Start; menus keep native A/X accept + B/Y cancel). Xbox 1.1
         * Jinx-style; RB's remaster HD toggle is unavailable. */
        {
            N64BridgeOpts bo;
            bo.slotNative = (idx > 0 || inputMpPadsOwnP1());
            bo.southpaw = padSouthpaw;
            button = n64BridgeButtons(button, &mc, &bo);
            if (mc.crouch) crouchNow = 1;
            padActPrev[idx] = mc.actionsHeld;
        }

        /* Select (BACK) opens the F10 options overlay -- the gamepad
         * equivalent of the F10 key for controller-only machines (Steam
         * Deck). The game never reads BACK, so nothing is withheld. */
        /* Outside a stage (not split) the sweep below handles every pad. */
        if (inStage() || inputMpSplit()) {
            const int seat = padSeatOfPad(pad);   /* edges are tracked per pads[] seat */
            int selNow = SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_BACK);
            /* D472: only opens here (closing is the owner's, in the overlay path) */
            if (selNow && !padSelectPrev[seat] && !optionsOverlayIsOpen()) {
                s_ovPlayer = idx;
                s_ovDev = seat;
                optionsOverlayToggle();
            }
            padSelectPrev[seat] = selNow;
        }
    }

    /* Every device can reach the menus: Select on any attached pad opens the menu outside a
     * stage, and in a solo stage where no pad plays player 1 (Input.P1Type = keyboard + mouse),
     * so the input type can be changed from a pad alone. The opener is player = its seat there
     * (player 0 in a stage), device = that pad. */
    if (idx == 0 && !inputMpSplit() && (!inStage() || !pad)) {
        int open = optionsOverlayIsOpen();
        for (int i = 0; i < MAX_PADS; i++) {
            if (!pads[i]) continue;
            const int selNow = SDL_GameControllerGetButton(pads[i], SDL_CONTROLLER_BUTTON_BACK);
            if (selNow && !padSelectPrev[i] && !open) {
                s_ovPlayer = inStage() ? 0 : i + inputSeatShift();   /* behind the keyboard player 1 (character select) */
                s_ovDev = i;
                optionsOverlayToggle();
                open = 1;   /* one opener per frame */
            }
            padSelectPrev[i] = selNow;
        }
    }

    if (idx == 0 && inputScriptIsActive(0)) crouchNow = g_inputScriptSlots[0].crouch;

    /* The GEPD preset's crouch key is independent of aim. Do not turn it
     * into C-down: outside aim C-down moves backwards in 1.2, and inside
     * aim it also zooms on weapons that disable crouch. This is a PC input
     * adapter writing the same stance field the native game adjusts, not
     * a change to the N64 game logic. */
    if (idx == 0 || inputMpSplit()) {
        int stage = current_menu == GE_MENU_RUN_STAGE || current_menu == GE_MENU_INVALID;
        if (!stage) s_crouchLatch = 0;
        if (crouchMode == 1) {
            if (stage && crouchNow && !s_crouchHeldPrev) s_crouchLatch ^= 1;
            s_crouchHeldPrev = crouchNow;
            crouchNow = s_crouchLatch;
        }
        struct player *p = inputSlotPlayer();
        int canCrouch = stage && p && !p->bonddead && p->outside_watch_menu &&
                        !p->pause_state && !p->mpmenuon && !g_PlayerIsInTank &&
                        !lvlGetControlsLockedFlag() &&
                        !gameScriptedCameraActive();
        /* D483/D494 (#136): a weapon with DISABLE_CROUCH (the sniper rifle)
         * makes the native code turn aim-mode C-up/C-down into zoom, so the
         * N64 pad cannot change stance with it out. GEPD's crouch key (the
         * mouse injector, the reference for this preset) writes the stance
         * directly and ignores the flag: Bond crouches and stands freely with
         * the sniper (maintainer A/B on 1964 GEPD, 2026-10-03). Do the same,
         * but never feed C-down for such a weapon (it would zoom out). */
        int weaponBlocks = 0;
        if (canCrouch && (crouchNow || s_crouchApplied))   /* weapon lookup only when it can matter */
            weaponBlocks = bondwalkItemCheckBitflags(p->hands[GUNRIGHT].weaponnum,
                                                     WEAPONSTATBITFLAG_DISABLE_CROUCH);
        if (canCrouch && crouchNow) {
            /* In aim mode the native crouchUp branch runs every tick without
             * C-down, undoing the port stance by one step. Feed the native
             * axis ONLY while aiming; hipfire still crouches by stance alone. */
            if (!weaponBlocks && (p->insightaimmode || (button & (GE_CONT_R | GE_CONT_L))))
                button |= GE_CONT_D;
            p->crouchpos = CROUCH_SQUAT;
            if (!s_crouchApplied && configGetInputLog())
                sysLogPrintf(LOG_NOTE, "GE_INPUTLOG free crouch applied (aim=%d, weapon disables crouch=%d)",
                             p->insightaimmode, weaponBlocks);
            s_crouchApplied = 1;
            s_crouchPlayer = p;
        } else if (!(canCrouch && !weaponBlocks && (button & GE_CONT_D) && p->insightaimmode)) {
            /* If native C-down is still held in aim, let the game retain its
             * own crouch on this tick rather than force a stand. */
            if (s_crouchApplied && configGetInputLog())
                sysLogPrintf(LOG_NOTE, "GE_INPUTLOG free crouch released");
            inputDropCrouch();
        } else {
            s_crouchApplied = 0;
            s_crouchPlayer = NULL;
        }
    }

    /* Modern: while aiming, D-pad up / down zoom a scoped weapon (the native C-up / C-down in aim mode) and the left
     * stick strafes slowly (the native C-left / C-right). After the crouch code above, which reads the C-down bit. */
    if (strat) button = n64BridgeAssist(button, &mc);

    /* D541: front-end release hold-off. One physical press can reach the menu
     * through two OR'd sources a few ms apart -- on a Steam Deck in Desktop
     * Mode, Steam's desktop layout types Enter for A (Escape for B) while SDL
     * also reads the raw pad -- and a gap between them is a second edge, so
     * one A press advanced two screens. A menu button only counts as released
     * after ~70 ms up; press edges are untouched. Menus only. */
    if (idx >= 0 && idx < MAX_PADS &&
        current_menu != GE_MENU_RUN_STAGE && current_menu != GE_MENU_INVALID) {
        static Uint32 s_menuBtnDownAt[MAX_PADS][3];
        static const u16 kHoldBits[3] = { GE_CONT_A, GE_CONT_B, GE_CONT_START };
        Uint32 now = SDL_GetTicks();
        for (int k = 0; k < 3; k++) {
            if (button & kHoldBits[k]) s_menuBtnDownAt[idx][k] = now;
            else if (s_menuBtnDownAt[idx][k] && now - s_menuBtnDownAt[idx][k] < 70) button |= kHoldBits[k];
        }
    }

    if (idx == 0 && inputScriptIsActive(0)) {
        /* D263: apply BEFORE the stick is written out -- scripted stick
         * tokens (SUP/SDOWN/SLEFT/SRIGHT) used to be discarded. */
        button = inputScriptApply(0, button);
        sx = g_inputScriptSlots[0].curSX;
        sy = g_inputScriptSlots[0].curSY;
    } else if (idx > 0 && idx <= 3 && inputScriptIsActive(idx)) {
        /* Per-slot harness (GE_INPUTSCRIPT_P1..P3): same sole-source
         * semantics for a split-screen test seat. Virtual seats behind
         * GE_MPVIRT reach here with button/sx/sy still at their idle zeros
         * (the gamepad block above is guarded by `if (pad)`), so an
         * unscripted seat stays idle exactly as before. */
        button = inputScriptApply(idx, button);
        sx = g_inputScriptSlots[idx].curSX;
        sy = g_inputScriptSlots[idx].curSY;
    }

    if (sx > STICK_MAX)  sx = STICK_MAX;
    if (sx < -STICK_MAX) sx = -STICK_MAX;
    if (sy > STICK_MAX)  sy = STICK_MAX;
    if (sy < -STICK_MAX) sy = -STICK_MAX;

    if (idx == 0) {
        inputRecordReplayStep(&button, &sx, &sy);
    }

    if (stick_x) *stick_x = (signed char)sx;
    if (stick_y) *stick_y = (signed char)sy;

    /* D337: GE_AIMLOG=1 -- per-poll aim-model trace (the game's own crosshair
     * state), for measuring jitter / range / edge-scroll headlessly. */
    if (idx == 0 && inputSlotPlayer() != NULL && GE_ENVFLAG("GE_AIMLOG") &&
        (current_menu == GE_MENU_RUN_STAGE || current_menu == GE_MENU_INVALID)) {
        sysLogPrintf(LOG_NOTE, "AIMLOG f=%ld aim=%d dev=%d drawx=%.3f cx=%.5f cy=%.5f theta=%.4f verta=%.4f acc=%.4f",
                     g_inputScriptSlots[0].frame, (int)inputSlotPlayer()->insightaimmode, g_inputAimDevMouse,
                     (double)inputSlotPlayer()->crosshair_angle.f[0],
                     (double)inputSlotPlayer()->crosshair_x_pos, (double)inputSlotPlayer()->crosshair_y_pos,
                     (double)inputSlotPlayer()->vv_theta, (double)inputSlotPlayer()->vv_verta);
    }
    if (configGetInputLog() && (button || sx || sy)) {
        sysLogPrintf(LOG_NOTE, "GE_INPUTLOG cont%d: btn=%04x stick=(%d,%d)",
                     idx, button, sx, sy);
    }
    return button;
}

int inputMenuPointerLive(void)
{
    /* D345(b): true while the 1:1 menu pointer owns cursor_h/v_pos -- in a
     * menu, the abs pointer is available, and the mouse was used recently.
     * Port screens that teleport the crosshair (frontoptions.c cursorToItem)
     * skip their snap when this is set: the cursor is already under the OS
     * pointer. Keyboard/D-pad users (stale mouse) still get the snap. */
    if (!s_menuPointerLive || menuPointerLastAbsWrite == 0) return 0;
    Uint64 now = SDL_GetPerformanceCounter();
    return ((double)(now - menuPointerLastAbsWrite) /
            (double)SDL_GetPerformanceFrequency() * 1000.0) <
           MENU_POINTER_REASSERT_MS;
}

/* F10 options overlay: while it owns controller 0 the inputComputePad poll
 * early-returns before inputReconcileGrab()/inputApplyCursorVisibility(), so whatever
 * grab state was live when F10 was pressed (relative mode + hidden cursor in a
 * stage) would persist and the mouse UI would be unusable. Force the cursor
 * free + visible every poll; the normal reconcile resumes once the overlay
 * closes and the early-return no longer fires. */
/* D471: family of a seat's pad (GE_PADFAMILY overrides, see padFamilyOverride;
 * it also holds with no pad open, for headless screenshots). */
int inputPadFamily(int seat)
{
    const int ov = padFamilyOverride();
    if (ov >= 0) return ov;
    return (seat >= 0 && seat < MAX_PADS && pads[seat]) ? padFamily[seat] : PADFAM_XBOX;
}

/* D471: expand {A} {B} {X} {Y} {BACK} {START} {LB} {RB} in a menu help template with
 * the names of the menu pad's family (seat 0, which drives the menus). Xbox
 * expands to exactly the old text. */
void inputPadHelpFmtFam(char *out, size_t n, const char *tmpl, int fam)
{
    static const char *const names[3][8] = {
        { "A", "B", "X", "Y", "BACK", "START", "LB", "RB" },
        { "CROSS", "CIRCLE", "SQUARE", "TRIANGLE", "SHARE", "OPTIONS", "L1", "R1" },
        { "A", "B", "X", "Y", "MINUS", "PLUS", "L", "R" },
    };
    static const char *const keys[8] = { "{A}", "{B}", "{X}", "{Y}", "{BACK}", "{START}", "{LB}", "{RB}" };
    if (fam < 0 || fam > 2) fam = PADFAM_XBOX;
    size_t o = 0;
    if (n == 0) return;
    while (*tmpl && o + 1 < n) {
        int hit = -1;
        if (*tmpl == '{')
            for (int k = 0; k < 8; k++)
                if (!strncmp(tmpl, keys[k], strlen(keys[k]))) { hit = k; break; }
        if (hit >= 0) {
            for (const char *p = names[fam][hit]; *p && o + 1 < n; p++) out[o++] = *p;
            tmpl += strlen(keys[hit]);
        } else {
            out[o++] = *tmpl++;
        }
    }
    out[o] = 0;
}

void inputPadHelpFmt(char *out, size_t n, const char *tmpl)
{
    inputPadHelpFmtFam(out, n, tmpl, inputPadFamily(optionsOverlayIsOpen() ? inputOverlayOwnerPad() : 0));  /* D472 */
}

/* D471: display name for a binding source token (kPadSrc names) on a seat's
 * pad family; NULL = keep the caller's existing Xbox-style text. */
const char *inputPadSourceFamilyName(int seat, const char *src)
{
    static const struct { const char *src, *ps, *nin; } t[] = {
        { "a", "Cross", NULL }, { "b", "Circle", NULL }, { "x", "Square", NULL }, { "y", "Triangle", NULL },
        { "start", "Options", "Plus" },
        { "leftshoulder", "L1", "L" }, { "rightshoulder", "R1", "R" },
        { "lefttrigger", "L2", "ZL" }, { "righttrigger", "R2", "ZR" },
        { "leftstick", "L3", NULL }, { "rightstick", "R3", NULL },
    };
    int fam = inputPadFamily(seat);
    if (fam == PADFAM_XBOX) return NULL;
    for (size_t k = 0; k < sizeof(t) / sizeof(t[0]); k++)
        if (!strcmp(src, t[k].src)) return fam == PADFAM_PS ? t[k].ps : t[k].nin;
    return NULL;
}

int inputPadButton(int idx, SDL_GameControllerButton b)
{
    if (idx < 0 || idx >= MAX_PADS || !pads[idx]) return 0;
    return SDL_GameControllerGetButton(pads[idx], b);
}

short inputPadAxis(int idx, SDL_GameControllerAxis a)
{
    if (idx < 0 || idx >= MAX_PADS || !pads[idx]) return 0;
    return SDL_GameControllerGetAxis(pads[idx], a);
}

/* D519: on the front end the game's red crosshair is the mouse pointer (D345).
 * While the overlay is open it keeps following the OS mouse so it can be
 * used over the overlay. Unlike the menu-pointer block (which insets the
 * range by 20 px like the game's own clamp) this maps window -> canvas
 * exactly, so the drawn crosshair sits where the overlay hit-tests the click
 * (optionsoverlay.c maps the same OS position). The OS cursor is hidden by the
 * caller so there is one pointer, not two. */
static void overlayFrontEndCrosshair(void)
{
    SDL_Window *w = SDL_GetMouseFocus();
    int mx = 0, my = 0, ww = 0, wh = 0;
    if (!w) return;
    SDL_GetMouseState(&mx, &my);
    SDL_GetWindowSize(w, &ww, &wh);
    if (ww <= 0 || wh <= 0) return;
    double fx = (double)mx / ww, fy = (double)my / wh;
    double rx0, ry0, rx1, ry1;
    videoGetOutputRectFrac(&rx0, &ry0, &rx1, &ry1);
    if (rx1 > rx0 && ry1 > ry0) { fx = (fx - rx0) / (rx1 - rx0); fy = (fy - ry0) / (ry1 - ry0); }
    extern f32 portNativeAspect(void);
    f32 na = portNativeAspect();
    if (na > (4.0f / 3.0f)) { double vis = (4.0 / 3.0) / (double)na; fx = (fx - (1.0 - vis) * 0.5) / vis; }
    double sw = getPlayer_c_screenwidth(), sh = getPlayer_c_screenheight();
    double sl = getPlayer_c_screenleft(),  st = getPlayer_c_screentop();
    if (!(sw > 200.0 && sw < 2000.0 && sh > 150.0 && sh < 2000.0)) return;
    double h = sl + fx * sw, v = st + fy * sh;
    double loH = sl + 20.0, hiH = sl + sw - 20.0, loV = st + 20.0, hiV = st + sh - 20.0;
    cursor_h_pos = (float)(h < loH ? loH : h > hiH ? hiH : h);
    cursor_v_pos = (float)(v < loV ? loV : v > hiV ? hiV : v);
}

/* D555 rework: INVERSE of overlayFrontEndCrosshair's mapping. Where the game's
 * front-end cursor (cursor_h_pos/cursor_v_pos, which frontDrawCursor draws at)
 * lands, as fractions of the UI screen rect (the rect the overlay's full-canvas
 * DL maps to), plus the x scale of the front-end canvas inside it (D335
 * native-widescreen pillarbox: vis, else 1). 0 = not available. */
int inputFrontEndCursorUiFrac(double *fx, double *fy, double *xscale)
{
    double sw = getPlayer_c_screenwidth(), sh = getPlayer_c_screenheight();
    double sl = getPlayer_c_screenleft(),  st = getPlayer_c_screentop();
    if (!(sw > 200.0 && sw < 2000.0 && sh > 150.0 && sh < 2000.0)) return 0;
    double x = ((double)cursor_h_pos - sl) / sw, y = ((double)cursor_v_pos - st) / sh;
    double vis = 1.0;
    extern f32 portNativeAspect(void);
    f32 na = portNativeAspect();
    if (na > (4.0f / 3.0f)) { vis = (4.0 / 3.0) / (double)na; x = x * vis + (1.0 - vis) * 0.5; }
    *fx = x; *fy = y; *xscale = vis;
    return 1;
}

void inputSuspendForOverlay(void)
{
    if (g_inputMouseGrabbed) {
        g_inputMouseGrabbed = 0;
        inputMouseRequestRelative(0);
        g_inputMouseDX = g_inputMouseDY = 0.0;
    }
    /* D544: the OS cursor is always shown while the overlay is open, so the
     * pointer draws OVER the menu (maintainer). The front-end path no longer
     * hides it, which also removes the show/hide race that flickered. */
    inputMouseRequestCursor(1);
}

void inputPostWheel(int notches)
{
    /* D223: keep the direction. v0.2.1: swapped per user request -- up now
     * cycles to the PREVIOUS weapon, down to the NEXT one (PC convention
     * "scroll down = advance list"). A burst of same-direction notches just
     * re-arms the same pulse; a direction change mid-sequence restarts it
     * (last wins). */
    if (notches > 0)      { wheelBack = 2;              wheelFwd = 0; }
    else if (notches < 0) { wheelFwd = WHEEL_FWD_POLLS; wheelBack = 0; }
}

/* D450: rescan requested by inputRequestRescan() from either SDL event pump
 * on SDL_CONTROLLERDEVICEADDED/REMOVED and performed at the top of
 * inputUpdate(), on the scheduler thread, before pads[] is read. Re-syncs
 * the pad seats (inputOpenPads); `connectedMask` bit 0 (keyboard/mouse) is
 * always kept. The game re-reads the mask through joyCheckStatus (every 120
 * polls and on any errno edge, src/joy.c), so a newly seated pad becomes its
 * own controller channel within ~2 s. */
void inputRescanPads(void)
{
    /* DNEW-mp-1: still-attached pads keep their open handle and seat;
     * inputOpenPads closes only detached ones and seats new ones. */
    for (int i = 0; i < MAX_PADS; ++i) {
        inputStrategyStateReset(&s_padState[i]);   /* Wave A: clear the look-smoothing EMA so a stick
                                                    * snap-back after re-plug can't lag from a stale value */
    }
    inputOpenPads();
    sysLogPrintf(LOG_NOTE, "input: rescanned pads (mask=0x%x, %d controller(s))",
                 connectedMask, numControllers);
}

/* ------------------------------------------------------------------------
 * D401: Rumble Pak -> real gamepad haptics.
 *
 * The game's Rumble-Pak state machine (src/joy.c joyRumblePakInit/Tick;
 * events from gunfire.c / bondview2.c / lv.c / sched.c) drives the
 * osMotor* shims in port/src/libultra.c, which route here. `strength`
 * is 0..1 and `time` is seconds; a 0-strength / 0-duration call stops
 * an active rumble. The global Input.RumbleScale config value (0 = fully
 * silent) scales the strength for every pad. idx is the N64 controller
 * channel 0..3 (MAXCONTROLLERS may be 6 on JPN; out-of-range is a no-op).
 * ------------------------------------------------------------------------ */
int inputRumbleSupported(int idx)
{
    if (idx < 0 || idx >= MAX_PADS) return 0;
    if (inputMpSplit() && inputMpKbmP1()) return idx >= 1 ? padRumbleOn[idx - 1] : 0;
    return padRumbleOn[idx];
}

/* D493: set once inputRumbleStopAll has run at exit, so the still-running
 * game thread cannot start a new rumble after the pads were silenced. */
static SDL_atomic_t s_rumbleStopped;

void inputRumble(int idx, f32 strength, f32 time)
{
    if (SDL_AtomicGet(&s_rumbleStopped)) return;
    if (idx < 0 || idx >= MAX_PADS) return;
    SDL_GameController *rpad = slotPad(idx);
    const int rIdx = (inputMpSplit() && inputMpKbmP1()) ? idx - 1 : idx;
    if (!rpad) return;
    if (gRumbleScale <= 0.f) return;
    if (padRumbleOn[rIdx]) {
        strength *= gRumbleScale;
#ifdef __SWITCH__
        nxRumbleSet(rIdx, strength, time);   /* #40 */
        return;
#endif
        if (strength <= 0.f) {
            strength = 0.f;
            time = 0.f;
        } else {
            strength *= 65535.f;
            time *= 1000.f;
        }
        SDL_GameControllerRumble(rpad, (Uint16)strength, (Uint16)strength, (Uint32)time);
    }
}

/* D493: silence every open pad at a clean exit. SDL_GameControllerRumble
 * runs for its duration on the pad itself, so a rumble started just before
 * the window closed kept going after the process ended (seen on a DualSense
 * when quitting mid-firefight). inputDestroy never runs: the host loop exits
 * through exit(0), so this is called from main.c's atexit handler. */
void inputRumbleStopAll(void)
{
    SDL_AtomicSet(&s_rumbleStopped, 1);
    for (int i = 0; i < MAX_PADS; ++i) {
#ifdef __SWITCH__
        SDL_AtomicSet(&s_nxRumActive[i], 0);   /* #40 */
        SDL_AtomicLock(&s_nxRumLock);
        nxRumSendLocked(i, 0.f);
        SDL_AtomicUnlock(&s_nxRumLock);
        continue;
#endif
        if (pads[i] && padRumbleOn[i]) {
            SDL_GameControllerRumble(pads[i], 0, 0, 0);
        }
    }
}

f32 inputRumbleGetScale(void)
{
    return gRumbleScale;
}

void inputRumbleSetScale(f32 v)
{
    if (v < 0.f) v = 0.f;
    if (v > 1.f) v = 1.f;
    gRumbleScale = v;
}

int inputConnectedMask(void)
{
    return connectedMask;
}

int inputGetNumControllers(void)
{
    return numControllers;
}

int inputPadSeatPresent(int seat) { return seat >= 0 && seat < MAX_PADS && pads[seat] != NULL; }

/* CONSOLE mode (port/include/console.h): no keyboard/mouse. Resolved once on
 * first use, which is after configLoad (main.c) so the ini key is readable. */
static int cfgConsole;   /* System.Console: hidden ini override, 0/1 */
int portIsConsole(void)
{
    static int resolved = -1;
    if (resolved < 0) {
        static const char *const kWhy[] = { "off", "Switch", "GE_CONSOLE", "ini" };
        int why = 0;
#ifdef __SWITCH__
        const int isSwitch = 1;
#else
        const int isSwitch = 0;
#endif
        resolved = portConsoleResolve(isSwitch, getenv("GE_CONSOLE"),
                                      configIntKeyWasLoaded("System.Console") ? cfgConsole : -1, &why);
        sysLogPrintf(LOG_NOTE, resolved ? "console mode: on (%s)" : "console mode: %s",
                     resolved ? kWhy[why] : kWhy[0]);
    }
    return resolved;
}

/* HANDHELD (port/include/console.h, #131): small screen. The Switch asks the
 * applet every time; elsewhere GE_HANDHELD / System.Handheld, resolved once. */
static int cfgHandheld;  /* System.Handheld: hidden ini override, 0/1 */
#ifdef __SWITCH__
extern int nxIsHandheld(void);
#endif
int portIsHandheld(void)
{
#ifdef __SWITCH__
    return nxIsHandheld();
#else
    static int resolved = -1;
    if (resolved < 0) {
        static const char *const kWhy[] = { "off", "GE_HANDHELD", "ini" };
        int why = 0;
        resolved = portHandheldResolve(getenv("GE_HANDHELD"),
                                       configIntKeyWasLoaded("System.Handheld") ? cfgHandheld : -1, &why);
        sysLogPrintf(LOG_NOTE, resolved ? "handheld mode: on (%s)" : "handheld mode: %s",
                     resolved ? kWhy[why] : kWhy[0]);
    }
    return resolved;
#endif
}

PD_CONSTRUCTOR static void inputConfigInit(void)
{
    configRegisterInt("System.Console", &cfgConsole, 0, 1);   /* hidden: not in the options menu */
    configRegisterInt("System.Handheld", &cfgHandheld, 0, 1); /* hidden: not in the options menu (#131) */
    configRegisterInt("Input.P1Type", &p1Type, 0, 2);   /* player 1: 0 keyboard + mouse, 1 gamepad, 2 auto */
    configRegisterString("Input.Player1Pad", padPref[0], sizeof(padPref[0]));
    configRegisterString("Input.Player2Pad", padPref[1], sizeof(padPref[1]));
    configRegisterString("Input.Player3Pad", padPref[2], sizeof(padPref[2]));
    configRegisterString("Input.Player4Pad", padPref[3], sizeof(padPref[3]));
    configRegisterInt("Input.MouseEnabled", &g_inputMouseEnabled, 0, 1);
    configRegisterInt("Input.CrosshairCursor", &g_inputCfgCrosshairCursor, 0, 1);   /* D555 */
    configRegisterInt("Input.MouseAimSpeed", &mouseAimSpeed, 1, 500);
#if defined(PLATFORM_SWITCH)
    configRegisterInt("Input.NxDefaultsRev",  &nxDefaultsRev,  0, 2);   /* project-dam */
#endif
    configRegisterInt("Input.MouseDirectLook", &g_inputMouseDirectLook, 0, 1);  /* WI-1 */
    configRegisterInt("Input.AimBand", &aimBand, 5, 40);
    configRegisterInt("Input.TankAimScale", &tankAimScale, 10, 300);   /* D407 */
    configRegisterInt("Input.MouseTurnSpeed", &g_inputMouseTurnSpeed, 1, 500);
        configRegisterInt("Input.HipfirePitchSpeed", &hipfirePitchSpeed, 10, 500);
    configRegisterInt("Input.MouseInvertY", &mouseInvertY, 0, 1);
    configRegisterInt("Input.MouseYScale", &mouseYScale, 1, 500);
    configRegisterInt("Input.MouseSmoothing", &mouseSmoothing, 0, 90);
    configRegisterInt("Input.MouseRawInput", &mouseRawInput, 0, 1);
    configRegisterInt("Input.MouseDtDecouple", &mouseDtDecouple, 0, 1);  /* D194(b) */
    configRegisterInt("Input.MouseSensitivity", &g_inputMouseSensitivity, 1, 500);  /* D238 */
    configRegisterInt("Input.MouseAimCurve", &aimCurveGamma, 50, 400);  /* D194(a), x100 */
    configRegisterInt("Input.NaturalPitch", &naturalPitchMode, 0, 1);  /* D194/D238 */
    configRegisterInt("Input.PadDeadzone", &padDeadzone, 0, 30000);  /* legacy; Wave A migrates to L/R */
    configRegisterInt("Input.PadDeadzoneL", &padDeadzoneL, 0, 30000);  /* Wave A: left (movement) stick */
    configRegisterInt("Input.PadDeadzoneR", &padDeadzoneR, 0, 30000);  /* Wave A: right (look) stick */
    configRegisterInt("Input.DefaultsRev", &inputDefaultsRev, 0, 2);   /* D546; 2 = project-dam 10 % deadzone */
    configRegisterInt("Input.PadLookSensX", &padLookSensX, 25, 200);   /* Wave A: look horizontal sensitivity (%) */
    configRegisterInt("Input.PadLookSensY", &padLookSensY, 25, 200);
    configRegisterInt("Input.PadAimLookSens", &padAimLookSens, 25, 200);   /* project-dam */
#if defined(PLATFORM_SWITCH)
    configRegisterInt("Input.PadLookCurve", &g_inputPadLookCurve, 100, 400);      /* project-dam */
    configRegisterInt("Input.PadTurnAccel", &g_inputPadTurnAccel, 0, 2);          /* project-dam */
#endif   /* Wave A: look vertical (pitch) sensitivity (%) */
    configRegisterInt("Input.PadSouthpaw", &padSouthpaw, 0, 1);        /* Wave A: swap fire/grenade triggers */
    configRegisterInt("Input.PadLookSmooth", &padLookSmooth, 0, 10);   /* Wave A: look low-pass strength, 0-10 (0 = off) */
    configRegisterInt("Input.PadTriggerPct", &padTriggerPct, 1, 99);
    configRegisterInt("Input.PadLookInvertY", &padLookInvertY, 0, 1);
    /* D401: global rumble scale (0 = silent .. 1 = full), applied to every
     * pad in inputRumble(). One setting for all pads (user call, 2026-09-28):
     * the original per-pad Input.PlayerN.RumbleScale keys were dropped --
     * stale ones in an existing ini log a one-time "unknown key" note and
     * self-clean on the next configSave. */
    configRegisterFloat("Input.RumbleScale", &gRumbleScale, 0.f, 1.f);
    /* D380: single PC layout. Old Input.Layout is ignored and removed on
     * configSave; BindingsVersion marks the one-time stale-default migration. */
    configRegisterInt("Input.BindingsVersion", &g_inputBindsVersion, 0, 3);
    configRegisterInt("Input.CrouchMode", &crouchMode, 0, 1);

    /* Seed fresh ini files with the REAL PC binding strings. Older ini
     * values are migrated at inputInit after configLoad, exactly once. */
    for (int a = 0; a < IA_COUNT; a++) {
        const char *def = g_inputGepdPreset[a] ? g_inputGepdPreset[a] : g_inputBindDefs[a].def;
        strncpy(g_inputBindStr[a], def, sizeof(g_inputBindStr[a]) - 1);
        configRegisterString(g_inputBindDefs[a].key, g_inputBindStr[a], sizeof(g_inputBindStr[a]));
    }
}
