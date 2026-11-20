/* The weapon wheel and the gadget wheel of the Modern control style (Input.PadPreset 5).
 *
 * LB held opens the weapon wheel, RB the gadget wheel: the right stick picks a sector, letting go of the button
 * equips it. While a wheel is open the player's pad is neutral (no look, no move), and the menu layer draws the
 * wheel (menuWheelFrame). The game's state is only touched on the game thread (portWheelGameTick, called from the
 * frame message hook in libultra.c); the poll thread only asks whether it must neutralise a pad
 * (portWheelBlocks) and the render thread only reads the snapshot (portWheelState). */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <SDL.h>
#include <ultra64.h>
#include <bondconstants.h>
#include "bondview.h"
#include "bondinv.h"
#include "gun.h"
#include "player.h"
#include "input.h"
#include "optionsoverlay.h"
#include "wheel.h"
extern int portSplitVertical;

#define WHEEL_MAX 16
#define WHEEL_NAME 24

enum { KIND_NONE = 0, KIND_WEAPONS = 1, KIND_GADGETS = 2 };

struct WheelItem {
    int right, left;               /* the weapon ids (left = 0 for a single weapon) */
    char name[WHEEL_NAME];
};

/* snapshot per seat for the render thread, guarded by a spin lock */
static SDL_SpinLock s_lock;
static struct { int kind, count, sel; struct WheelItem items[WHEEL_MAX]; } s_snap[4];
static SDL_atomic_t s_blockMask;   /* bit per pad slot: neutral while a wheel is open */

extern u16 *get_ptr_short_watch_text_for_item(ITEM_IDS item);

static struct player *seatPlayer(int seat)
{
    if (getPlayerCount() > 1) return (seat >= 0 && seat < 4 && seat < getPlayerCount()) ? g_playerPointers[seat] : NULL;
    return seat == 0 ? g_CurrentPlayer : NULL;
}

static void copyName(char *dst, const u16 *text)
{
    const unsigned char *s = (const unsigned char *)text;   /* the game's short names are plain 8-bit text */
    int n = 0;
    while (s && s[n] && n < WHEEL_NAME - 1) { dst[n] = (char)s[n]; n++; }
    dst[n] = 0;
}

/* the items of a wheel from the player's inventory: guns (and dual pairs) or the other items */
static int build(struct player *p, int kind, struct WheelItem *out)
{
    int n = 0, hasSniper = 0;
    InvItem *first = p->ptr_inventory_first_in_cycle, *it = first;
    for (InvItem *q = first; q; ) {   /* with the sniper rifle in the bag, the game draws the fist as its stock */
        if (q->type == INV_ITEM_WEAPON && q->type_inv_item.type_weap.weapon == ITEM_SNIPERRIFLE) hasSniper = 1;
        q = q->next;
        if (q == first) break;
    }
    while (it && n < WHEEL_MAX) {
        struct WheelItem w;
        memset(&w, 0, sizeof w);
        int ok = 0;
        if (it->type == INV_ITEM_WEAPON) {
            const int id = it->type_inv_item.type_weap.weapon;
            const int gun = id < ITEM_BOMBCASE;
            if ((kind == KIND_WEAPONS) == gun) {
                w.right = id;
                copyName(w.name, get_ptr_short_watch_text_for_item((ITEM_IDS)id));
                if (id == ITEM_FIST && hasSniper) snprintf(w.name, WHEEL_NAME, "Rifle butt");   /* the game's blunt strike with the sniper rifle */
                ok = id != 0;
            }
        } else if (it->type == INV_ITEM_DUAL && kind == KIND_WEAPONS) {
            w.right = it->type_inv_item.type_dual.weapon_right;
            w.left = it->type_inv_item.type_dual.weapon_left;
            copyName(w.name, get_ptr_short_watch_text_for_item((ITEM_IDS)w.right));
            if (strlen(w.name) < WHEEL_NAME - 3) strcat(w.name, " x2");
            ok = 1;
        }
        if (ok) out[n++] = w;
        it = it->next;
        if (it == first) break;
    }
    return n;
}

static void equip(struct player *p, const struct WheelItem *w)
{
    /* what currentPlayerEquipWeaponWrapper does, on this player */
    p->hands[GUNRIGHT].weapon_current_animation = 5;
    p->hands[GUNRIGHT].weapon_next_weapon = w->right;
    p->hands[GUNRIGHT].weapon_animation_trigger = 0;
    p->hands[GUNLEFT].weapon_current_animation = 5;
    p->hands[GUNLEFT].weapon_next_weapon = w->left;
    p->hands[GUNLEFT].weapon_animation_trigger = 0;
}

static int s_openKind[4], s_openSel[4];

void portWheelGameTick(void)
{
    /* the stage is running and the menu is closed */
    if (!optionsMenuInStage() || optionsOverlayIsOpen()) {
        SDL_AtomicLock(&s_lock);
        for (int i = 0; i < 4; i++) s_snap[i].kind = KIND_NONE;
        SDL_AtomicUnlock(&s_lock);
        SDL_AtomicSet(&s_blockMask, 0);
        memset(s_openKind, 0, sizeof s_openKind);
        return;
    }
    int mask = 0;
    for (int seat = 0; seat < 4; seat++) {
        struct player *p = seatPlayer(seat);
        int kind = KIND_NONE, n = 0;
        struct WheelItem items[WHEEL_MAX];
        if (p && inputPadIsModern(seat)) {
            const int lb = inputPadButton(seat, SDL_CONTROLLER_BUTTON_LEFTSHOULDER) != 0;
            const int rb = inputPadButton(seat, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) != 0;
            const int want = lb ? KIND_WEAPONS : (rb ? KIND_GADGETS : KIND_NONE);
            if (want != KIND_NONE) {
                n = build(p, want, items);
                if (n > 0) {   /* nothing to pick from: the button does nothing */
                    kind = want;
                    mask |= 1 << seat;
                    if (s_openKind[seat] != want) s_openSel[seat] = -1;   /* nothing is picked until the stick is pushed: letting go without a pick keeps what is in hand */
                    s_openKind[seat] = want;
                    const float x = inputPadAxis(seat, SDL_CONTROLLER_AXIS_RIGHTX) / 32767.0f;
                    const float y = inputPadAxis(seat, SDL_CONTROLLER_AXIS_RIGHTY) / 32767.0f;
                    if (x * x + y * y > 0.36f) {   /* the stick is pushed: its direction picks the sector, the first one is at the top */
                        float a = atan2f(x, -y);   /* 0 = up, clockwise */
                        if (a < 0) a += 6.2831853f;
                        s_openSel[seat] = (int)(a / (6.2831853f / (float)n) + 0.5f) % n;
                    }
                }
            }
            if (kind == KIND_NONE && s_openKind[seat] != KIND_NONE) {   /* the button is let go: equip the picked sector */
                const int was = s_openKind[seat], sel = s_openSel[seat];
                const int m = build(p, was, items);
                if (sel >= 0 && sel < m) equip(p, &items[sel]);
            }
        }
        if (kind == KIND_NONE) s_openKind[seat] = KIND_NONE;
        SDL_AtomicLock(&s_lock);
        s_snap[seat].kind = kind;
        s_snap[seat].count = n;
        s_snap[seat].sel = kind != KIND_NONE ? s_openSel[seat] : -1;
        if (n > 0) memcpy(s_snap[seat].items, items, sizeof(struct WheelItem) * (size_t)n);
        SDL_AtomicUnlock(&s_lock);
    }
    SDL_AtomicSet(&s_blockMask, mask);
}

int portWheelBlocks(int slot)
{
    return slot >= 0 && slot < 4 && (SDL_AtomicGet(&s_blockMask) & (1 << slot));
}

int portWheelMask(void)
{
    int mask = 0;
    SDL_AtomicLock(&s_lock);
    for (int i = 0; i < 4; i++) if (s_snap[i].kind != KIND_NONE) mask |= 1 << i;
    SDL_AtomicUnlock(&s_lock);
    return mask;
}

int portWheelState(int seat, char names[][24], int max, int *sel, int *kind)
{
    int n = 0;
    if (seat < 0 || seat > 3) return 0;
    SDL_AtomicLock(&s_lock);
    if (s_snap[seat].kind != KIND_NONE) {
        n = s_snap[seat].count < max ? s_snap[seat].count : max;
        for (int i = 0; i < n; i++) { strncpy(names[i], s_snap[seat].items[i].name, 23); names[i][23] = 0; }
        *sel = s_snap[seat].sel;
        *kind = s_snap[seat].kind;
    }
    SDL_AtomicUnlock(&s_lock);
    return n;
}

/* where a seat's picture is on the screen, as fractions of it: the whole screen alone, halves for two players
 * (side by side with Game.SplitScreen), quarters for three or four */
void portSeatRegion(int seat, float *x0, float *y0, float *x1, float *y1)
{
    const int count = getPlayerCount();
    *x0 = 0; *y0 = 0; *x1 = 1; *y1 = 1;
    if (count >= 3) {
        *x0 = (seat & 1) ? 0.5f : 0; *x1 = *x0 + 0.5f;
        *y0 = (seat & 2) ? 0.5f : 0; *y1 = *y0 + 0.5f;
    } else if (count == 2 && GE_SPLIT_EQ2()) {
        if (portSplitVertical) { *x0 = seat == 1 ? 0.5f : 0; *x1 = *x0 + 0.5f; }
        else { *y0 = seat == 1 ? 0.5f : 0; *y1 = *y0 + 0.5f; }
    }
}
