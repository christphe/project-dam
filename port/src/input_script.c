/* Scripted input and record / replay: the test harness, moved out of input.c verbatim (#78). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#include "system.h"
#include "envflag.h"
#include "input/input_types.h"
#include "input/n64bridge.h"
#include "input/input_internal.h"

#define STICK_MAX          80

/* ------------------------------------------------------------------------
 * Scripted input (test harness, port-only). GE_INPUTSCRIPT lets a headless
 * run walk the front-end / pause menus with no human at the keyboard.
 *
 *   GE_INPUTSCRIPT="120:START;180:A;240:A;600:SDOWN;900:SNONE,A"
 *
 * Each entry is `<frame>:<tok>[,<tok>...]`. Buttons (A B Z START L R UP DOWN
 * LEFT RIGHT CUP CDOWN CLEFT CRIGHT) pulse for INPUTSCRIPT_PULSE controller
 * reads from <frame>. Analog-stick tokens (SUP SDOWN SLEFT SRIGHT) are
 * SUSTAINED: the stick stays deflected until a later entry changes it; SNONE
 * re-centres it. CHOLD/CREL hold/release GEPD crouch (D377);
 * UHOLD/UREL and RELOADHOLD/RELOADREL exercise dedicated use/reload (D378).
 * MLB<n>/MRB<n> (D597) press/release the sustained mouse buttons (n=1 press,
 * 0 release; slot 0 only): headless fire for the D595-class regression.
 * "Frame" = count of controller-0 reads since launch (roughly
 * 2 per rendered frame -- watch GE_INPUTLOG to calibrate). Unset env => no
 * effect; when set it is the ONLY controller-0 input source.
 *
 * GE_INPUTSCRIPT_P1 / _P2 / _P3 do the same for slots 1-3 (split-screen
 * seats), so a headless run can drive players 2-4 behind GE_MPVIRT/
 * GE_STARTMP with no physical pads:
 *   GE_INPUTSCRIPT_P1="600:ZHOLD;900:SUP;1200:SNONE,A"
 * Same entry syntax; "frame" = count of THAT controller's reads since
 * launch. Slots 1-3 have no mouse path, so mouse tokens (MDX/MDY) and
 * CHOLD/CREL are ignored there (one warning per script if they appear).
 * Unset => the seat stays idle exactly as before.
 *
 * GE_INPUTSCRIPT_MS=1 (abpad, tools_pc/abpad): every slot's "frame" is
 * instead milliseconds since SDL init (SDL_GetTicks), and buttons pulse for
 * INPUTSCRIPT_PULSE_MS. Read rates vary across the intro and menus, so a
 * read count can't be lined up with a wall-clock reference (1964). */
#define INPUTSCRIPT_PULSE     6
#define INPUTSCRIPT_PULSE_MS  50

static int scriptMsMode(void)
{
    static int ms = -1;
    if (ms < 0) ms = GE_ENVFLAG("GE_INPUTSCRIPT_MS") ? 1 : 0;
    return ms;
}

struct scriptSlot g_inputScriptSlots[4] = {
    { .count = -1 }, { .count = -1 }, { .count = -1 }, { .count = -1 },
};

static const char *scriptTag(int slot)
{
    if (slot == 0) return "GE_INPUTSCRIPT";
    return slot == 1 ? "GE_INPUTSCRIPT_P1" : slot == 2 ? "GE_INPUTSCRIPT_P2"
                                                       : "GE_INPUTSCRIPT_P3";
}

static const char *scriptEnv(int slot)
{
    if (slot == 0) return getenv("GE_INPUTSCRIPT");
    return slot == 1 ? getenv("GE_INPUTSCRIPT_P1")
         : slot == 2 ? getenv("GE_INPUTSCRIPT_P2") : getenv("GE_INPUTSCRIPT_P3");
}

/* Apply one token to `e`. Buttons: A B Z START L R UP DOWN LEFT RIGHT CUP
 * CDOWN CLEFT CRIGHT (D-pad/C-buttons). Analog stick: SUP SDOWN SLEFT SRIGHT
 * (full +/-80 deflection -- moves menu cursors). */
static void scriptApplyToken(struct scriptEntry *e, const char *s, int n,
                             int slot)
{
    struct { const char *k; unsigned v; } btn[] = {
        {"A",GE_CONT_A}, {"B",GE_CONT_B}, {"Z",GE_CONT_G}, {"START",GE_CONT_START},
        {"L",GE_CONT_L}, {"R",GE_CONT_R}, {"UP",GE_CONT_UP}, {"DOWN",GE_CONT_DOWN},
        {"LEFT",GE_CONT_LEFT}, {"RIGHT",GE_CONT_RIGHT},
        {"CUP",GE_CONT_E}, {"CDOWN",GE_CONT_D}, {"CLEFT",GE_CONT_C}, {"CRIGHT",GE_CONT_F},
    };
    for (size_t i = 0; i < sizeof(btn)/sizeof(btn[0]); ++i) {
        if ((int)strlen(btn[i].k) == n && SDL_strncasecmp(btn[i].k, s, n) == 0) {
            e->mask |= btn[i].v;
            return;
        }
    }
    /* D337: sustained scripted mouse (px per poll) and aim hold, for headless
     * aim-model measurements: MDX<n> MDY<n> (latest entry wins, 0 stops),
     * RHOLD / RREL (hold / release the game's R aim button). */
    if (n > 3 && (SDL_strncasecmp("MDX", s, 3) == 0 || SDL_strncasecmp("MDY", s, 3) == 0)) {
        if (slot > 0) {   /* no mouse path on slots 1-3: ignore, warn once */
            struct scriptSlot *st = &g_inputScriptSlots[slot];
            if (!st->ignoredWarned) {
                st->ignoredWarned = 1;
                sysLogPrintf(LOG_WARNING, "%s: mouse tokens are ignored for slots 1-3 ('%.*s')",
                             scriptTag(slot), n, s);
            }
            return;
        }
        char num[16]; int k = n - 3; if (k > 15) k = 15;
        memcpy(num, s + 3, k); num[k] = 0;
        e->hasMouse = 1;
        if (s[2] == 'X' || s[2] == 'x') e->mdx = atoi(num); else e->mdy = atoi(num);
        return;
    }
    /* D597: sustained scripted mouse buttons: MLB<n> MRB<n> (n=1 press,
     * 0 release; latest entry wins), slot 0 only. Headless fire for the
     * D595-class regression: aim with MDX, MLB1 on an AI -> struck animation
     * with no SIGSEGV. A headless window has no real mouse state, so the
     * bit ORs into the polled mask (see the D337 block in
     * inputComputePadSlot); the game's mouse-button bindings (LMB=IA_FIRE)
     * then drive it like a real press. */
    if (n > 3 && (SDL_strncasecmp("MLB", s, 3) == 0 || SDL_strncasecmp("MRB", s, 3) == 0)) {
        if (slot > 0) {   /* no mouse path on slots 1-3: ignore, warn once */
            struct scriptSlot *st = &g_inputScriptSlots[slot];
            if (!st->ignoredWarned) {
                st->ignoredWarned = 1;
                sysLogPrintf(LOG_WARNING, "%s: mouse tokens are ignored for slots 1-3 ('%.*s')",
                             scriptTag(slot), n, s);
            }
            return;
        }
        char num[16]; int k = n - 3; if (k > 15) k = 15;
        memcpy(num, s + 3, k); num[k] = 0;
        e->hasMouse = 1;
        if (s[1] == 'L') e->mlb = atoi(num) != 0; else e->mrb = atoi(num) != 0;   /* MLB/MRB: the L/R is index 1 */
        return;
    }
    if (n == 5 && SDL_strncasecmp("RHOLD", s, 5) == 0) { e->hasHold = 1; e->hold = 1; return; }
    if (n == 5 && SDL_strncasecmp("LHOLD", s, 5) == 0) { e->hasHold = 1; e->hold = 2; return; }
    if (n == 4 && SDL_strncasecmp("RREL", s, 4) == 0)  { e->hasHold = 1; e->hold = 0; return; }
    /* D207: ZHOLD / ZREL hold / release fire (Z) for automatic weapons;
     * independent of RHOLD so aim + fire can be held together. */
    if (n == 5 && SDL_strncasecmp("ZHOLD", s, 5) == 0) { e->hasZHold = 1; e->zhold = 1; return; }
    if (n == 4 && SDL_strncasecmp("ZREL", s, 4) == 0)  { e->hasZHold = 1; e->zhold = 0; return; }
    /* abpad: CUPHOLD/CDOWNHOLD/CLEFTHOLD/CRIGHTHOLD hold C buttons until CNONE
     * (the port forces 1.2 SOLITARE, where C-up/down walk and C-left/right strafe). */
    if (n == 7 && SDL_strncasecmp("CUPHOLD", s, 7) == 0)    { e->hasCHold = 1; e->chold |= GE_CONT_E; return; }
    if (n == 9 && SDL_strncasecmp("CDOWNHOLD", s, 9) == 0)  { e->hasCHold = 1; e->chold |= GE_CONT_D; return; }
    if (n == 9 && SDL_strncasecmp("CLEFTHOLD", s, 9) == 0)  { e->hasCHold = 1; e->chold |= GE_CONT_C; return; }
    if (n == 10 && SDL_strncasecmp("CRIGHTHOLD", s, 10) == 0) { e->hasCHold = 1; e->chold |= GE_CONT_F; return; }
    if (n == 5 && SDL_strncasecmp("CNONE", s, 5) == 0)      { e->hasCHold = 1; e->chold = 0; return; }
    if ((n == 5 && SDL_strncasecmp("CHOLD", s, 5) == 0) ||
        (n == 4 && SDL_strncasecmp("CREL", s, 4) == 0)) {
        if (slot > 0) {   /* no crouch path on slots 1-3: ignore, warn once */
            struct scriptSlot *st = &g_inputScriptSlots[slot];
            if (!st->ignoredWarned) {
                st->ignoredWarned = 1;
                sysLogPrintf(LOG_WARNING, "%s: CHOLD/CREL are ignored for slots 1-3 ('%.*s')",
                             scriptTag(slot), n, s);
            }
            return;
        }
        e->hasCrouch = 1; e->crouch = (SDL_strncasecmp("CHOLD", s, 5) == 0);
        return;
    }
    if (n == 5 && SDL_strncasecmp("UHOLD", s, 5) == 0) { e->hasUse = 1; e->use = 1; return; }
    if (n == 4 && SDL_strncasecmp("UREL", s, 4) == 0)  { e->hasUse = 1; e->use = 0; return; }
    if (n == 10 && SDL_strncasecmp("RELOADHOLD", s, 10) == 0) { e->hasReload = 1; e->reload = 1; return; }
    if (n == 9 && SDL_strncasecmp("RELOADREL", s, 9) == 0) { e->hasReload = 1; e->reload = 0; return; }
    e->hasStick = 1;
    if (n == 3 && SDL_strncasecmp("SUP", s, 3) == 0)      { e->sy =  STICK_MAX; return; }
    if (n == 5 && SDL_strncasecmp("SDOWN", s, 5) == 0)    { e->sy = -STICK_MAX; return; }
    if (n == 5 && SDL_strncasecmp("SLEFT", s, 5) == 0)    { e->sx = -STICK_MAX; return; }
    if (n == 6 && SDL_strncasecmp("SRIGHT", s, 6) == 0)   { e->sx =  STICK_MAX; return; }
    if (n == 5 && SDL_strncasecmp("SNONE", s, 5) == 0)    { return; }  /* recentre */
    e->hasStick = 0;
    sysLogPrintf(LOG_WARNING, "%s: unknown token '%.*s'", scriptTag(slot), n, s);
}

static void scriptParse(int slot)
{
    struct scriptSlot *st = &g_inputScriptSlots[slot];
    const char *tag = scriptTag(slot);
    st->count = 0;
    const char *env = scriptEnv(slot);
    if (!env || !*env) {
        return;
    }
    const char *p = env;
    while (*p && st->count < INPUTSCRIPT_MAX) {
        char *end = NULL;
        long fr = strtol(p, &end, 10);
        if (end == p || *end != ':') {
            sysLogPrintf(LOG_WARNING, "%s: bad entry near '%s'", tag, p);
            break;
        }
        p = end + 1;
        struct scriptEntry *e = &st->entries[st->count];
        e->frame = fr;
        e->mask = 0;
        e->sx = e->sy = 0;
        e->hasStick = 0;
        e->hasMouse = e->mdx = e->mdy = e->mlb = e->mrb = 0;
        e->hasHold = e->hold = 0;
        e->hasZHold = e->zhold = 0;
        e->hasCHold = 0; e->chold = 0;
        e->hasCrouch = e->crouch = 0;
        e->hasUse = e->use = e->hasReload = e->reload = 0;
        while (*p && *p != ';') {
            const char *tok = p;
            while (*p && *p != ',' && *p != ';') ++p;
            scriptApplyToken(e, tok, (int)(p - tok), slot);
            if (*p == ',') ++p;
        }
        if (*p == ';') ++p;
        st->count++;
    }
    sysLogPrintf(LOG_INFO, "%s: %d entr%s parsed",
                 tag, st->count, st->count == 1 ? "y" : "ies");
}

int inputScriptIsActive(int slot)
{
    struct scriptSlot *st = &g_inputScriptSlots[slot];
    if (st->count < 0) {
        scriptParse(slot);
    }
    return st->count > 0;
}

/* When a script is loaded it is the SOLE source of controller-0 input: real
 * keyboard/mouse/pad is ignored so headless menu walks are deterministic
 * (a relative-mouse SDL window with no focus otherwise spews phantom deltas).
 * Returns the scripted button mask for the current frame; advances the frame
 * counter (call exactly once per controller-0 read). */
/* Refresh the latest sustained holds for one slot from its entries (latest
 * entry wins). Slot 0's mouse fields are read by the keyboard/mouse path; on
 * slots 1-3 only hold/zhold/use/reload can be set (mouse and crouch tokens
 * are ignored at parse time). */
void inputScriptRefreshHolds(int slot)
{
    struct scriptSlot *st = &g_inputScriptSlots[slot];
    long bestM = -1, bestH = -1, bestZ = -1, bestC = -1, bestU = -1, bestR = -1, bestCH = -1;
    for (int i = 0; i < st->count; ++i) {
        long d = st->frame - st->entries[i].frame;
        if (d < 0) continue;
        if (st->entries[i].hasMouse && st->entries[i].frame > bestM) {
            bestM = st->entries[i].frame;
            st->mouseOn = 1;
            st->mdx = st->entries[i].mdx;
            st->mdy = st->entries[i].mdy;
            st->mlb = st->entries[i].mlb;   /* D597 */
            st->mrb = st->entries[i].mrb;
        }
        if (st->entries[i].hasHold && st->entries[i].frame > bestH) {
            bestH = st->entries[i].frame;
            st->hold = st->entries[i].hold;
        }
        if (st->entries[i].hasZHold && st->entries[i].frame > bestZ) {
            bestZ = st->entries[i].frame;
            st->zhold = st->entries[i].zhold;
        }
        if (st->entries[i].hasCHold && st->entries[i].frame > bestCH) {
            bestCH = st->entries[i].frame;
            st->chold = st->entries[i].chold;
        }
        if (st->entries[i].hasCrouch && st->entries[i].frame > bestC) {
            bestC = st->entries[i].frame;
            st->crouch = st->entries[i].crouch;
        }
        if (st->entries[i].hasUse && st->entries[i].frame > bestU) {
            bestU = st->entries[i].frame;
            st->use = st->entries[i].use;
        }
        if (st->entries[i].hasReload && st->entries[i].frame > bestR) {
            bestR = st->entries[i].frame;
            st->reload = st->entries[i].reload;
        }
    }
}

/* Input record/replay (test harness, port-only). GE_INPUTRECORD=<file>
 * appends controller 0's final button mask + stick per controller read;
 * GE_INPUTREPLAY=<file> feeds the same stream back read for read (then
 * neutral). Reads are tick-driven, so with -level_XX and a pinned GE_RSEED a
 * replay re-plays a human run (tools_pc/levelbench.sh --replay). Pad only:
 * mouse aim is not captured; D404 centred right-stick look is (fields 4-6). */
int g_inputReplayActive;                          /* GE_INPUTREPLAY stream open */
int g_inputRecPadDX, g_inputRecPadDY, g_inputRecPadDirect;  /* this poll's D404 pad-direct look */

FILE *g_inputRec;
static FILE *s_rep;
static int s_rrInit, s_repFetched;
struct InputRepCur g_inputRepCur;
int g_inputRecUse, g_inputRecReload;   /* this poll's dedicated use/reload (D378) */

static void inputRecordReplayInit(void)
{
    if (s_rrInit) return;
    s_rrInit = 1;
    const char *r = getenv("GE_INPUTRECORD");
    const char *p = getenv("GE_INPUTREPLAY");
    if (p && *p) {
        s_rep = fopen(p, "r");
        g_inputReplayActive = s_rep != NULL;
        sysLogPrintf(LOG_NOTE, "GE_INPUTREPLAY %s: %s", p, s_rep ? "playing" : "cannot open");
    }
    if (r && *r) {
        g_inputRec = fopen(r, "w");
        if (g_inputRec) setvbuf(g_inputRec, NULL, _IOLBF, 0);   /* survive a crash/kill */
        sysLogPrintf(LOG_NOTE, "GE_INPUTRECORD %s: %s", r, g_inputRec ? "recording" : "cannot open");
    }
}

/* Read this poll's replay line (once per controller-0 poll). */
void inputReplayFetch(void)
{
    inputRecordReplayInit();
    if (!s_rep || s_repFetched) return;
    s_repFetched = 1;
    char line[128];
    memset(&g_inputRepCur, 0, sizeof g_inputRepCur);
    if (!fgets(line, sizeof line, s_rep) ||
        sscanf(line, "%x %d %d %d %d %d %d %d", &g_inputRepCur.b, &g_inputRepCur.x, &g_inputRepCur.y,
               &g_inputRepCur.pdx, &g_inputRepCur.pdy, &g_inputRepCur.pd, &g_inputRepCur.use, &g_inputRepCur.reload) < 3) {
        sysLogPrintf(LOG_NOTE, "GE_INPUTREPLAY: end of stream");
        fclose(s_rep);
        s_rep = NULL;
        g_inputReplayActive = 0;
        memset(&g_inputRepCur, 0, sizeof g_inputRepCur);
    }
}

void inputRecordReplayStep(unsigned *button, int *sx, int *sy)
{
    inputReplayFetch();
    if (g_inputReplayActive || s_repFetched) {
        *button = g_inputRepCur.b; *sx = g_inputRepCur.x; *sy = g_inputRepCur.y;
        /* D404 centred pad aim turns the camera directly, not via the
         * stick channel: re-apply the recorded deflection, same poll. */
        if (g_inputRepCur.pd) inputPadDirectCompute(g_inputRepCur.pdx, g_inputRepCur.pdy);
    }
    s_repFetched = 0;
    if (g_inputRec) {
        fprintf(g_inputRec, "%x %d %d %d %d %d %d %d\n", *button, *sx, *sy,
                g_inputRecPadDX, g_inputRecPadDY, g_inputRecPadDirect, g_inputRecUse, g_inputRecReload);
    }
    g_inputRecPadDX = g_inputRecPadDY = g_inputRecPadDirect = g_inputRecUse = g_inputRecReload = 0;
}

unsigned inputScriptApply(int slot, unsigned button)
{
    struct scriptSlot *st = &g_inputScriptSlots[slot];
    if (!inputScriptIsActive(slot)) {
        return button;
    }
    int ms = scriptMsMode();
    if (ms) st->frame = (long)SDL_GetTicks();
    inputScriptRefreshHolds(slot);   /* D207: refresh sustained holds even off the mouse path (idempotent) */
    unsigned m = 0;
    long bestStickFrame = -1;
    long pulse = ms ? INPUTSCRIPT_PULSE_MS : INPUTSCRIPT_PULSE;
    for (int i = 0; i < st->count; ++i) {
        long d = st->frame - st->entries[i].frame;
        if (d >= 0 && d < pulse) {
            m |= st->entries[i].mask;
        }
        /* sustained stick: the latest-starting entry that carried a stick token */
        if (d >= 0 && st->entries[i].hasStick && st->entries[i].frame > bestStickFrame) {
            bestStickFrame = st->entries[i].frame;
            st->curSX = st->entries[i].sx;
            st->curSY = st->entries[i].sy;
        }
    }
    if (st->hold == 1) m |= GE_CONT_R;   /* D337 RHOLD */
    if (st->hold == 2) m |= GE_CONT_L;   /* D337 LHOLD (Q / "LeanLeft") */
    if (st->zhold) m |= GE_CONT_G;       /* D207 ZHOLD (fire) */
    m |= st->chold;                      /* abpad C-button holds */
    if (!ms) st->frame++;
    return m;
}