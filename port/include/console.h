#ifndef PORT_CONSOLE_H
#define PORT_CONSOLE_H

/*
 * CONSOLE mode: the device has no keyboard and no mouse (a Switch, a
 * PortMaster handheld). It is a RUN-TIME setting, not a compile flag, so one
 * Linux/aarch64 binary serves both a desktop and a handheld. It is not the
 * same thing as SWITCH: the Switch-only code (deko3d, nxlink, the A/B menu
 * swap, docked/handheld, NRO paths) stays under __SWITCH__/PLATFORM_SWITCH.
 *
 * Precedence (portConsoleResolve, pure and unit-tested):
 *   Switch build            -> on, always
 *   env GE_CONSOLE=0 / 1    -> that
 *   ini  System.Console 0/1 -> that (hidden key, not in the options menu)
 *   otherwise               -> off
 */

#ifdef __cplusplus
extern "C" {
#endif

enum { PORT_CONSOLE_OFF = 0, PORT_CONSOLE_SWITCH, PORT_CONSOLE_ENV, PORT_CONSOLE_INI };

/* envVal: getenv("GE_CONSOLE") or NULL. iniVal: System.Console, -1 = absent/unset.
 * *why (optional) gets the PORT_CONSOLE_* reason. Returns 1 for console mode. */
static inline int portConsoleResolve(int isSwitch, const char *envVal, int iniVal, int *why)
{
    int w = PORT_CONSOLE_OFF, on = 0;
    if (isSwitch) { w = PORT_CONSOLE_SWITCH; on = 1; }
    else if (envVal && (envVal[0] == '0' || envVal[0] == '1') && envVal[1] == '\0') {
        w = PORT_CONSOLE_ENV; on = envVal[0] == '1';
    } else if (iniVal == 0 || iniVal == 1) { w = PORT_CONSOLE_INI; on = iniVal; }
    if (why) *why = w;
    return on;
}

/* Resolved once on first use (after configLoad), logged once. */
int portIsConsole(void);

#ifdef __cplusplus
}
#endif

#endif
