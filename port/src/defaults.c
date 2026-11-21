/* Defaults lookup over the generated tables (defaults_data.c, from defaults.json). */
#include <string.h>
#include "defaults.h"

int defaultsGetFor(int pc, const char *key, double *out)
{
    const struct DefaultEntry *t = pc ? kDefaultsPc : kDefaultsSwitch;
    int lo = 0, hi = (pc ? kDefaultsPcCount : kDefaultsSwitchCount) - 1;
    while (lo <= hi) {   /* the generator sorts by key (byte order) */
        int mid = (lo + hi) / 2, c = strcmp(key, t[mid].key);
        if (c == 0) { if (out) *out = t[mid].value; return 1; }
        if (c < 0) hi = mid - 1; else lo = mid + 1;
    }
    return 0;
}

#ifndef DEFAULTS_NO_CONFIG   /* the menu viewer has no config.c */
#include "config.h"
#include "system.h"

int defaultsGet(const char *key, double *out)
{
#ifdef __SWITCH__
    return defaultsGetFor(0, key, out);
#else
    return defaultsGetFor(1, key, out);
#endif
}

static char s_language[16] = "";   /* Menu.Language: "" = English; "fr" loads lang/fr.json next to the ini */
const char *portMenuLanguage(void) { return s_language; }
static int s_devMode = 0;      /* Dev.Mode: the controls marked "dev" show up in the menu (the Switch keeps its tuned values hidden) */
int portDevMode(void) { return s_devMode; }
static int s_iniVersion = 0;   /* Config.Version: 0 = an ini written before versioning (or none) */
static int s_legacyVal[16];

void defaultsApply(void)
{
    configRegisterInt("Config.Version", &s_iniVersion, 0, 0);
    configRegisterInt("Dev.Mode", &s_devMode, 0, 1);
    configRegisterString("Menu.Language", s_language, (int)sizeof s_language);
    for (int i = 0; i < kDefaultLegacyCount && i < 16; i++) {
        s_legacyVal[i] = kDefaultLegacy[i].value;
        configRegisterInt(kDefaultLegacy[i].key, &s_legacyVal[i], 0, 0);
    }
    const struct DefaultEntry *t =
#ifdef __SWITCH__
        kDefaultsSwitch;
    const int n = kDefaultsSwitchCount;
#else
        kDefaultsPc;
    const int n = kDefaultsPcCount;
#endif
    for (int i = 0; i < n; i++) {
        double cur;
        if (!configGetValue(t[i].key, &cur)) continue;   /* not registered here (another platform's, or action-backed) */
        if (cur < 0 && t[i].value >= 0) continue;   /* a negative initializer is "unset": the module resolves it after configLoad (pad deadzone <- the legacy key) */
        if (cur != t[i].value) {
            sysLogPrintf(LOG_NOTE, "defaults: %s initializer %g -> defaults.json %g", t[i].key, cur, t[i].value);
            configSetValue(t[i].key, t[i].value);
        }
    }
}

void defaultsMigrate(void)
{
    const int from = s_iniVersion;
    for (int i = 0; i < kDefaultMigrationCount; i++) {
        const struct DefaultMigration *m = &kDefaultMigrations[i];
        if (m->version <= from || m->version > kDefaultsVersion) continue;
        if (m->whenKey) {
            double w;
            if (!configGetValue(m->whenKey, &w) || w != m->whenEq) continue;
        }
        double v = m->value;
        if (m->reset && !defaultsGet(m->key, &v)) continue;
        configSetValue(m->key, v);
        sysLogPrintf(LOG_INFO, "defaults: ini v%d -> v%d: %s = %g", from, m->version, m->key, v);
    }
    for (int i = 0; i < kDefaultLegacyCount && i < 16; i++) configUnregister(kDefaultLegacy[i].key);
    s_iniVersion = kDefaultsVersion;
}
#endif
