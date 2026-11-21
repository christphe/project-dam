/*
 * Keyboard + mouse: the PC bindings (D214 / D380 / D384: scancode and mouse-button binds, their ini migrations and the
 * inputBinding* API the options UI edits), the mouse request / grab / cursor state machine (D287, WI-1) and the
 * key / action reads. Moved out of input.c verbatim (#78): the code is unchanged, the shared statics are external
 * with a g_input prefix (input/input_internal.h).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#include "platform.h"
#include "system.h"
#include "config.h"
#include "input.h"
#include "optionsoverlay.h"
#include "input/input_internal.h"

extern int current_menu;
#define GE_MENU_RUN_STAGE  11
#define GE_MENU_INVALID    (-1)

/* ---- mouse state, request / apply, cursor ---- */
double g_inputMouseDX = 0.0;
double g_inputMouseDY = 0.0;
int g_inputAbsAimSuspend = 0;
int g_inputMouseEnabled   = 1;
int g_inputMouseGrabbed    = 1;     /* released while the window is unfocused */

/* D287: SDL's mouse-mode and cursor calls belong on the window's thread (the
 * host thread that created it and pumps its events). Controller polls run on
 * the scheduler thread (osContStartReadData -> inputComputePad ->
 * inputReconcileGrab), so they called SDL_SetRelativeMouseMode / SDL_ShowCursor
 * concurrently with the host thread's own focus/click handling. On Windows
 * relative mode starts/stops SDL's raw-input thread and re-clips the cursor;
 * two threads flipping it at once (clicking back into the window after being
 * unfocused) can wedge. Callers now only record the wanted state; the host
 * thread applies it in inputApplyMouseRequests() from videoPumpEvents(). The
 * logical state (g_inputMouseGrabbed, deltas) still updates immediately. */
static SDL_SpinLock s_mouseReqLock;
static int s_reqRelative = -1, s_reqCursor = -1;    /* -1: nothing pending */
int g_inputAppRelative = -1;
static int s_appCursor = -1;    /* host thread only */

void inputMouseRequestRelative(int on)
{
    SDL_AtomicLock(&s_mouseReqLock);
    s_reqRelative = on ? 1 : 0;
    SDL_AtomicUnlock(&s_mouseReqLock);
}

void inputMouseRequestCursor(int show)
{
    SDL_AtomicLock(&s_mouseReqLock);
    s_reqCursor = show ? 1 : 0;
    SDL_AtomicUnlock(&s_mouseReqLock);
}

/* D555 (reworked 2026-10-06): Input.CrosshairCursor -- while the F10 overlay /
 * PC Options is open the OS cursor is HIDDEN and optionsoverlay.c draws the
 * game's own crosshair sprite at the mouse position, last in its display list
 * (so it is above the card). Off: the OS arrow over the overlay, as before. */
int g_inputCfgCrosshairCursor = 1;
int inputCrosshairCursorOn(void) { return g_inputCfgCrosshairCursor; }
static int s_cursorShown = -1;   /* host thread only: last SDL_ShowCursor state applied */

/* Host thread only (videoPumpEvents, inputInit). */
void inputApplyMouseRequests(void)
{
    SDL_AtomicLock(&s_mouseReqLock);
    const int rel = s_reqRelative, cur = s_reqCursor;
    s_reqRelative = s_reqCursor = -1;
    SDL_AtomicUnlock(&s_mouseReqLock);

    if (rel >= 0 && rel != g_inputAppRelative) {
        g_inputAppRelative = rel;
        SDL_SetRelativeMouseMode(rel ? SDL_TRUE : SDL_FALSE);
        if (rel) {
            SDL_GetRelativeMouseState(NULL, NULL);   /* drain the accumulated jump */
        }
    }
    if (cur >= 0) s_appCursor = cur;
    if (s_appCursor >= 0) {
        /* D555: with the crosshair pointer on, the overlay draws its own
         * pointer, so the OS cursor stays hidden while it is open. */
        const int show = s_appCursor == 1 && !(g_inputCfgCrosshairCursor && optionsOverlayIsOpen());
        if (show != s_cursorShown) {
            s_cursorShown = show;
            SDL_ShowCursor(show ? SDL_ENABLE : SDL_DISABLE);
        }
    }
}

/* WI-1: Quake-style click-to-lock cursor capture. The cursor is free until
 * you click in the game window; ESC (or focus loss, or opening a menu) frees
 * it again. Re-entering a stage while still "armed" re-locks automatically so
 * unpausing / starting a level does not need a click.
 * D239/D192: the legacy always-grab mode (former Input.MouseCaptureMode=0)
 * is removed -- it felt wrong on the file-select menu and its front-end
 * pointer could not reach the outer grid cells. Click-to-lock is the only
 * mode now; existing ini files setting the old key are ignored.
 * Controller input is entirely independent of all of this. */
/* D594: on macOS the FOCUS_GAINED-driven click-to-lock is unreliable (AppKit
 * key-window; no focus events over ssh), so start armed like PD's MLOCK_ON --
 * the mouse is locked by default in-stage (see inputReconcileGrab()). */
#ifdef PLATFORM_MACOS
int g_inputCaptureArmed     = 1;   /* D594: PD MLOCK_ON default (see inputReconcileGrab) */
#else
int g_inputCaptureArmed     = 0;   /* user has clicked to lock (capture mode) */
#endif
int g_inputWindowFocused    = 1;

/* ---- keyboard ---- */
int g_inputBindsVersion = 0; /* D380/D386: versioned migration of effective ini binds */
int inputKeyDown(const Uint8 *ks, SDL_Scancode sc)
{
    return ks && sc != SDL_SCANCODE_UNKNOWN && ks[sc];
}


/* ------------------------------------------------------------------------
 * D214 — keyboard rebinding (PD parity: config-string driven, no UI).
 *
 * Each gameplay action maps to a comma-separated list of SDL scancode names
 * (as printed by SDL_GetScancodeName: "W", "Up", "Left Ctrl", "Space", ...).
 * The defaults reproduce the previously-hardcoded FPS layout exactly, so a
 * fresh or [Bind]-less ini changes nothing. Parsed once in inputInit(), after
 * configLoad(). D385: buttons 1..5 share these slots with keyboard keys.
 * ---------------------------------------------------------------------- */

const struct InputBindDef g_inputBindDefs[IA_COUNT] = {
    [IA_FORWARD]  = { "Input.Bind.Forward",     "W,Up"          },
    [IA_BACK]     = { "Input.Bind.Back",        "S,Down"        },
    [IA_STRAFE_L] = { "Input.Bind.StrafeLeft",  "A"             },
    [IA_STRAFE_R] = { "Input.Bind.StrafeRight", "D"             },
    [IA_TURN_L]   = { "Input.Bind.TurnLeft",    "Left"          },
    [IA_TURN_R]   = { "Input.Bind.TurnRight",   "Right"         },
    [IA_FIRE]     = { "Input.Bind.Fire",        "Left Ctrl"     },
    [IA_AIM]      = { "Input.Bind.Aim",         "Left Shift"    },
    [IA_ACTION]   = { "Input.Bind.Action",      "Space,Z,E"     },
    [IA_CANCEL]   = { "Input.Bind.Cancel",      "X,R,F,Escape"  },
    [IA_LEAN_L]   = { "Input.Bind.LeanLeft",    "Q"             },
    [IA_START]    = { "Input.Bind.Start",       "Return,Tab"    },
    [IA_RELOAD]   = { "Input.Bind.Reload",      ""              },
    [IA_CROUCH]   = { "Input.Bind.Crouch",      ""              },
};

/* Legacy pre-D380 N64 defaults (g_inputBindDefs above) are only a migration
 * signature. The single PC layout is the GEPD/mouse default: Q = A
 * button (accept/next weapon), E = B button (use/cancel/reload),
 * R = dedicated reload, Ctrl = crouch). The preset is the effective default
 * for a one-time ini conversion only. On disk, every Bind key now stores
 * its actual effective value; `NONE` explicitly unbinds even a preset key.
 * Fire loses its Left Ctrl key (Mouse 1 fires; Ctrl crouches), LeanLeft unbound.
 * NULL entries retain their old defaults (movement etc.). */
const char *const g_inputGepdPreset[IA_COUNT] = {
    [IA_FIRE]   = "Mouse 1",
    [IA_AIM]    = "Mouse 3,Left Shift",
    [IA_ACTION] = "Q",
    [IA_CANCEL] = "E",
    [IA_LEAN_L] = "",
    [IA_RELOAD] = "R",
    [IA_CROUCH] = "Left Ctrl",
};

char         g_inputBindStr[IA_COUNT][64];
static int          s_legacyBindWarned[IA_COUNT];
int g_inputBind[IA_COUNT][BIND_MAX_KEYS];

/* Formerly LMB/RMB were unconditional, even for a NONE keyboard bind. Move
 * that effective behaviour into the two visible slots on first v2 load.
 * If both slots are already occupied, preserve both user keys and log the
 * mouse default that could not fit; never keep an invisible active third. */
static void migrateMouseDefault(int a, int button)
{
    char old[sizeof(g_inputBindStr[0])];
    snprintf(old, sizeof(old), "%s", g_inputBindStr[a]);
    char name[16];
    snprintf(name, sizeof(name), "Mouse %d", button);
    if (strstr(old, name)) return;
    const char *first = old;
    const char *comma = strchr(old, ',');
    if (comma && strchr(comma + 1, ',')) {
        sysLogPrintf(LOG_WARNING, "input: %s has extra legacy keys; cannot add %s in two slots",
                     g_inputBindDefs[a].key, name);
        return;
    }
    if (comma && strncmp(old, "NONE,", 5) != 0 &&
        SDL_strcasecmp(comma + 1, "NONE") != 0) {
        sysLogPrintf(LOG_WARNING, "input: %s already has two keys; rebind a slot to restore %s",
                     g_inputBindDefs[a].key, name);
        return;
    }
    if (SDL_strcasecmp(first, "NONE") == 0 || !*old) first = "";
    else if (comma && strncmp(old, "NONE,", 5) == 0) first = comma + 1;
    else if (comma) {
        /* A trailing NONE slot is empty; preserve the first key. */
        char *end = strchr(old, ',');
        *end = 0;
        first = old;
    }
    if (strlen(name) + (*first ? 1 + strlen(first) : 0) >= sizeof(g_inputBindStr[a])) {
        sysLogPrintf(LOG_WARNING, "input: %s too long to add %s; unchanged",
                     g_inputBindDefs[a].key, name);
        return;
    }
    strcpy(g_inputBindStr[a], name);
    if (*first) {
        strcat(g_inputBindStr[a], ",");
        strcat(g_inputBindStr[a], first);
    }
}

void inputMigrateBinds(void)
{
    if (g_inputBindsVersion >= 3) return;
    if (g_inputBindsVersion == 2) {
        /* D386: D385 called SDL button 2 "right" in its default, but SDL
         * button 2 is middle; button 3 is right. Only fix the exact old
         * default, never overwrite a user's custom Aim assignment. */
        if (strcmp(g_inputBindStr[IA_AIM], "Mouse 2,Left Shift") == 0)
            snprintf(g_inputBindStr[IA_AIM], sizeof(g_inputBindStr[IA_AIM]),
                     "Mouse 3,Left Shift");
    }
    if (g_inputBindsVersion == 0) {
        for (int a = 0; a < IA_COUNT; a++) {
            const char *preset = g_inputGepdPreset[a];
            if (preset && strcmp(g_inputBindStr[a], g_inputBindDefs[a].def) == 0)
                snprintf(g_inputBindStr[a], sizeof(g_inputBindStr[a]), "%s", preset);
        }
    }
    if (g_inputBindsVersion < 2) {
        migrateMouseDefault(IA_FIRE, SDL_BUTTON_LEFT);
        migrateMouseDefault(IA_AIM, SDL_BUTTON_RIGHT);
    }
    g_inputBindsVersion = 3;
    configSave();
    sysLogPrintf(LOG_INFO, "input: migrated binds to visible PC keyboard/mouse slots");
}

void inputRebuildBinds(void)
{
    for (int a = 0; a < IA_COUNT; a++) {
        for (int k = 0; k < BIND_MAX_KEYS; k++) {
            g_inputBind[a][k] = SDL_SCANCODE_UNKNOWN;
        }
        /* D380: ini values ARE the effective bindings, not hidden defaults.
         * NONE is an explicit unbind (PD-port convention). */
        const char *def = SDL_strcasecmp(g_inputBindStr[a], "NONE") == 0 ? "" : g_inputBindStr[a];
        /* D384: do not silently activate invisible 3rd/4th keys in the
         * two-slot UI. Preserve the old ini text until the user edits this
         * particular action, at which point only its visible pair is saved. */
        const char *second = strchr(def, ',');
        if (second && strchr(second + 1, ',') && !s_legacyBindWarned[a]) {
            s_legacyBindWarned[a] = 1;
            sysLogPrintf(LOG_WARNING, "input: %s has legacy extra keys (ignored; edit this action to remove)",
                         g_inputBindDefs[a].key);
        }
        char buf[64];
        strncpy(buf, def, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = 0;
        /* An empty bind or NONE is intentionally unbound, not a typo. */
        int hadInput = (def == g_inputBindStr[a] && g_inputBindStr[a][0] != 0);

        int n = 0, valid = 0;
        for (char *tok = strtok(buf, ","); tok && n < BIND_MAX_KEYS; tok = strtok(NULL, ",")) {
            while (*tok == ' ' || *tok == '\t') tok++;
            char *end = tok + strlen(tok);
            while (end > tok && (end[-1] == ' ' || end[-1] == '\t')) *--end = 0;
            if (!*tok) continue;
            /* Explicit NONE preserves an empty slot before later keys. */
            if (SDL_strcasecmp(tok, "NONE") == 0) { n++; continue; }
            int sc;
            if (!SDL_strncasecmp(tok, "Mouse ", 6) &&
                tok[6] >= '1' && tok[6] <= '5' && tok[7] == 0) {
                sc = INPUT_BIND_MOUSE(tok[6] - '0');
            } else if (!strncmp(tok, "SC:", 3)) {
                char *endnum;
                long id = strtol(tok + 3, &endnum, 10);
                sc = *endnum == 0 && id > 0 && id < SDL_NUM_SCANCODES
                    ? (SDL_Scancode)id : SDL_SCANCODE_UNKNOWN;
            } else {
                sc = SDL_GetScancodeFromName(tok);
            }
            if (sc == SDL_SCANCODE_UNKNOWN) {
                sysLogPrintf(LOG_WARNING, "input: %s: unknown binding '%s'",
                             g_inputBindDefs[a].key, tok);
                continue;
            }
            g_inputBind[a][n++] = sc;
            valid++;
        }
        /* v0.4.0 M3: an intentionally-empty string (a preset or default
         * that leaves an action unbound, e.g. GEPD Fire) is silent --
         * only a non-empty string that produced no valid keys warns. */
        if (valid == 0 && hadInput) {
            sysLogPrintf(LOG_WARNING, "input: %s has no valid keys; action unbound",
                         g_inputBindDefs[a].key);
        }
    }
}


/* D384/D385: the UI edits two honest keyboard/mouse slots. Legacy extra ini tokens
 * remain unmodified on disk until this action is explicitly edited; they
 * cannot fire invisibly while only two slots are displayed. */
static int bindingAction(const char *key)
{
    if (!key) return -1;
    for (int a = 0; a < IA_COUNT; a++)
        if (!strcmp(g_inputBindDefs[a].key, key)) return a;
    return -1;
}

const char *inputBindingSlot(const char *key, int slot)
{
    int a = bindingAction(key);
    if (a < 0 || slot < 0 || slot >= BIND_MAX_KEYS ||
        g_inputBind[a][slot] == SDL_SCANCODE_UNKNOWN) return "NONE";
    if (g_inputBind[a][slot] > SDL_NUM_SCANCODES &&
        g_inputBind[a][slot] <= INPUT_BIND_MOUSE(5)) {
        static const char *names[] = { "Mouse 1", "Mouse 2", "Mouse 3", "Mouse 4", "Mouse 5" };
        return names[g_inputBind[a][slot] - INPUT_BIND_MOUSE(1)];
    }
    const char *name = SDL_GetScancodeName((SDL_Scancode)g_inputBind[a][slot]);
    if (name && *name && !strchr(name, ',')) return name;
    /* Rare SDL names contain commas (e.g. keypad comma), which the ini
     * treats as separators. The stable numeric fallback remains bindable. */
    static _Thread_local char fallback[16];
    snprintf(fallback, sizeof(fallback), "SC:%d", (int)g_inputBind[a][slot]);
    return fallback;
}

int inputBindingSetSlot(const char *key, int slot, int sc)
{
    int a = bindingAction(key);
    if (a < 0 || slot < 0 || slot >= BIND_MAX_KEYS ||
        sc < SDL_SCANCODE_UNKNOWN || sc == SDL_NUM_SCANCODES ||
        sc > INPUT_BIND_MOUSE(5)) return 0;
    int slots[BIND_MAX_KEYS];
    memcpy(slots, g_inputBind[a], sizeof(slots));
    if (sc != SDL_SCANCODE_UNKNOWN) {
        for (int i = 0; i < BIND_MAX_KEYS; i++)
            if (i != slot && slots[i] == sc) slots[i] = SDL_SCANCODE_UNKNOWN;
    }
    slots[slot] = sc;
    char buf[sizeof(g_inputBindStr[0])] = {0};
    int used = 0;
    for (int i = 0; i < BIND_MAX_KEYS; i++) {
        if (slots[i] == SDL_SCANCODE_UNKNOWN) {
            /* Only serialize holes before a later bound slot. */
            int later = 0;
            for (int j = i + 1; j < BIND_MAX_KEYS; j++)
                if (slots[j] != SDL_SCANCODE_UNKNOWN) later = 1;
            if (!later) continue;
            if (used + 5 + (used != 0) >= (int)sizeof(buf)) return 0;
            if (used) buf[used++] = ',';
            memcpy(buf + used, "NONE", 4);
            used += 4;
            buf[used] = 0;
            continue;
        }
        const char *name;
        char fallback[32];
        if (slots[i] > SDL_NUM_SCANCODES) {
            snprintf(fallback, sizeof(fallback), "Mouse %d", slots[i] - SDL_NUM_SCANCODES);
            name = fallback;
        } else name = SDL_GetScancodeName((SDL_Scancode)slots[i]);
        if (!name || !*name || strchr(name, ',')) {
            snprintf(fallback, sizeof(fallback), "SC:%d", (int)slots[i]);
            name = fallback;
        }
        int len = (int)strlen(name);
        if (used + len + (used != 0) >= (int)sizeof(buf)) return 0;
        if (used) buf[used++] = ',';
        memcpy(buf + used, name, len);
        used += len;
        buf[used] = 0;
    }
    snprintf(g_inputBindStr[a], sizeof(g_inputBindStr[a]), "%s", used ? buf : "NONE");
    inputBindingsApply();
    return 1;
}

int inputBindingResetKey(const char *key)
{
    int a = bindingAction(key);
    if (a < 0) return 0;
    const char *def = g_inputGepdPreset[a] ? g_inputGepdPreset[a] : g_inputBindDefs[a].def;
    snprintf(g_inputBindStr[a], sizeof(g_inputBindStr[a]), "%s", def);
    return 1;
}

/* Isolated boot probe: validate multi-slot persistence without writing ini.
 * The entire original binding is restored before gameplay begins. */
void inputBindingProbe(void)
{
    const char *key = "Input.Bind.Crouch";
    int a = bindingAction(key), ok = a >= 0;
    char saved[sizeof(g_inputBindStr[0])];
    if (!ok) return;
    memcpy(saved, g_inputBindStr[a], sizeof(saved));
    ok &= inputBindingSetSlot(key, 1, SDL_SCANCODE_KP_COMMA);
    ok &= g_inputBind[a][1] == SDL_SCANCODE_KP_COMMA;
    ok &= inputBindingSetSlot(key, 0, SDL_SCANCODE_UNKNOWN);
    ok &= g_inputBind[a][0] == SDL_SCANCODE_UNKNOWN && g_inputBind[a][1] == SDL_SCANCODE_KP_COMMA;
    ok &= strstr(g_inputBindStr[a], "NONE,") == g_inputBindStr[a];
    ok &= inputBindingSetSlot(key, 1, SDL_SCANCODE_UNKNOWN);
    ok &= strcmp(g_inputBindStr[a], "NONE") == 0;
    memcpy(g_inputBindStr[a], saved, sizeof(saved));
    inputBindingsApply();
    /* Mouse buttons are real, serializable bindings; duplicate assignment
     * clears the other visible slot rather than leaving a hidden action. */
    key = "Input.Bind.Fire";
    a = bindingAction(key);
    memcpy(saved, g_inputBindStr[a], sizeof(saved));
    ok &= inputBindingSetSlot(key, 0, INPUT_BIND_MOUSE(5));
    ok &= strcmp(inputBindingSlot(key, 0), "Mouse 5") == 0;
    ok &= inputBindingSetSlot(key, 1, INPUT_BIND_MOUSE(5));
    ok &= g_inputBind[a][0] == 0 && g_inputBind[a][1] == INPUT_BIND_MOUSE(5);
    ok &= strcmp(g_inputBindStr[a], "NONE,Mouse 5") == 0;
    memcpy(g_inputBindStr[a], saved, sizeof(saved));
    inputBindingsApply();
    sysLogPrintf(ok ? LOG_INFO : LOG_ERROR,
                 "GE_BINDPROBE: %s (2 slots, mouse 1-5, sparse/comma, ini restored)",
                 ok ? "PASS" : "FAIL");
}

int inputActHeld(const Uint8 *ks, int act)
{
    /* Front-end mouse clicks stay fixed UI controls. Click-to-lock while
     * playing cannot fire a newly rebound button until the grab succeeds.
     * D594: the in-stage test must match the file-wide `menuMode` semantics
     * (RUN_STAGE *or* INVALID). A forced-level start (-level_NN) leaves
     * current_menu at MENU_INVALID, so the old strict ==RUN_STAGE check
     * silently disabled every mouse-driven action (fire, aim) in that path --
     * the reason mouse look worked but the mouse button never did. PD has no
     * such gate; a real front-end menu is a specific MENU_ id (menuMode 1),
     * so it still stays a UI control. */
    int inStage = (current_menu == GE_MENU_RUN_STAGE ||
                   current_menu == GE_MENU_INVALID);
    Uint32 mb = g_inputMouseEnabled && g_inputMouseGrabbed && inStage &&
                !optionsOverlayIsOpen()
              ? SDL_GetMouseState(NULL, NULL) : 0;
    for (int k = 0; k < BIND_MAX_KEYS; k++) {
        int code = g_inputBind[act][k];
        if (code > SDL_NUM_SCANCODES && code <= INPUT_BIND_MOUSE(5)) {
            if (mb & SDL_BUTTON(code - SDL_NUM_SCANCODES)) return 1;
        } else if (code > SDL_SCANCODE_UNKNOWN && code < SDL_NUM_SCANCODES &&
                   inputKeyDown(ks, (SDL_Scancode)code)) return 1;
    }
    return 0;
}


/* ---- mouse grab ---- */
void inputApplyGrab(int want)
{
    want = want && g_inputMouseEnabled;
    if (want == g_inputMouseGrabbed) {
        return;
    }
    g_inputMouseGrabbed = want;
    inputMouseRequestRelative(want);   /* D287: applied on the host thread */
    g_inputMouseDX = g_inputMouseDY = 0.0;
}

/* Reconcile the SDL grab state with what the current mode wants. Called once
 * per controller-0 poll (menuMode known there) and from the notify hooks. */
void inputReconcileGrab(int menuMode)
{
#ifdef PLATFORM_MACOS
    /* D594: the FOCUS_GAINED-driven `g_inputWindowFocused` gate is unreliable on
     * macOS (AppKit key-window; no focus events over ssh), so a click-to-lock
     * that waits on it never arms and the mouse button + look stay dead. PD
     * keeps the mouse locked by default (MLOCK_ON); match it -- drop only the
     * focus gate. Menus and aim-holds still free the cursor (menuMode /
     * g_inputAbsAimSuspend, exactly as on every other platform). */
    int want = g_inputCaptureArmed && !menuMode;
#else
    int want = g_inputCaptureArmed && g_inputWindowFocused && !menuMode; /* click-to-lock */
#endif
    if (g_inputAbsAimSuspend) {
        want = 0;   /* D194: an aim hold wants the free cursor for absolute aim */
    }
    inputApplyGrab(want);
}

/* Hide the OS cursor while the game window is focused (normal PC-game
 * behaviour): in a stage the mouse drives the look axis, and in menus GE draws
 * its own crosshair that now tracks the pointer 1:1 -- a visible OS arrow on
 * top is just clutter. Show it again when focus is lost so the desktop behaves
 * normally. Relative-mouse mode hides the cursor too, but only while grabbed;
 * this covers the free-but-focused states (menus, pre-click stage). */
void inputApplyCursorVisibility(void)
{
    /* D194: while an aim hold has the cursor out for absolute aim, show it --
     * the user is pointing with it and needs to see where. */
    int hide = g_inputWindowFocused && g_inputMouseEnabled && !g_inputAbsAimSuspend;
    inputMouseRequestCursor(!hide);    /* D287: applied on the host thread */
}

/* video.c focus events. Records focus and lets inputReconcileGrab() decide. */
void inputSetMouseGrab(int on)
{
    g_inputWindowFocused = on ? 1 : 0;
    inputReconcileGrab(0);   /* menuMode re-checked on the next poll anyway */
    inputApplyCursorVisibility();
}

/* A mouse click landed in the game window (video.c): arm + grab. */
void inputNotifyClick(void)
{
    if (g_inputMouseEnabled && g_inputWindowFocused) {
        g_inputCaptureArmed = 1;
        inputReconcileGrab(0);
    }
}

/* ESC pressed (video.c): release the cursor and report 1 so the caller can
 * swallow the key; reports 0 when there is nothing to release. */
int inputReleaseCapture(void)
{
    if (g_inputMouseGrabbed) {
        g_inputCaptureArmed = 0;
        inputApplyGrab(0);
        return 1;
    }
    return 0;
}

