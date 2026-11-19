#ifndef PORT_BOOTSCREEN_H
#define PORT_BOOTSCREEN_H

/*
 * Pre-video boot screen (port/src/bootscreen.c). The game's own window does not
 * exist yet when the ROM is loaded and converted, so first launch used to show
 * nothing for several seconds. This is a tiny stand-alone surface for that
 * window of time:
 *
 *   PC     a small SDL window: a progress bar, the percentage in the title bar.
 *          Messages use a native message box.
 *   Switch the libnx text console: a text progress bar. Messages wait for A.
 *
 * Everything degrades to the log when no display is available. Never touches
 * the game's renderer; bootscreenClose() releases it all before videoInit().
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Shows/updates the progress screen. frac is 0..1; label is a short text such
 * as "Models". Cheap to call often (redraws only when something visible changed). */
void bootscreenProgress(const char *label, float frac);

/* Shows a blocking message (a missing ROM, a failed conversion) and returns
 * once the user dismissed it. Closes the progress screen first. */
void bootscreenMessage(const char *title, const char *text);

/* Conversion finished: shows the full bar and waits for A (any key / click on
 * PC) so the screen can be read, then returns. No-op when no screen is open. */
void bootscreenDone(void);

/* Releases the screen. Safe to call when nothing is open. */
void bootscreenClose(void);

#ifdef __cplusplus
}
#endif

#endif /* PORT_BOOTSCREEN_H */
