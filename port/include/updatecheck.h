#ifndef PORT_UPDATECHECK_H
#define PORT_UPDATECHECK_H

/*
 * Opt-in "check for updates" (Game.CheckUpdates, default OFF).
 *
 * updateCheckStart() does nothing at all unless the ini value is 1 -- no
 * thread, no DNS, no library init. When enabled it spawns one detached
 * background thread that makes a single HTTPS GET to GitHub's releases API.
 */
void        updateCheckStart(void);          /* call once, after configLoad() */
const char *updateCheckAvailable(void);      /* newer tag (e.g. "vX.Y.Z") or NULL */
void        updateCheckOpenReleases(void);   /* SDL_OpenURL on the releases page */

/* Switch: install the update found by the check (download project-dam.nro,
 * verify its sha256, replace the running NRO; the new version starts on the
 * next launch). Runs on a background thread; the row text follows it. */
void        updateInstall(void);
void        updateCheckShutdown(void);       /* quit: stop and join the update threads (Switch) */
int         updateInstallIdle(void);         /* 1 while a press may start (idle or failed) */
const char *updateInstallStatus(void);       /* text for the "install" row */

#endif
