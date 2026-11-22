/* Defaults: the generated tables (defaults.json), the lookups, and the ini migrations. */
#include "ge_test.h"
#include "null_platform.h"
#include "defaults.h"
#include "config.h"

static void tables(void)
{
    /* The lookup binary-searches: both tables must be sorted by key and free of duplicates. */
    for (int pc = 0; pc < 2; pc++) {
        const struct DefaultEntry *t = pc ? kDefaultsPc : kDefaultsSwitch;
        const int n = pc ? kDefaultsPcCount : kDefaultsSwitchCount;
        GE_CHECK(n > 0);
        for (int i = 1; i < n; i++) GE_CHECK(strcmp(t[i - 1].key, t[i].key) < 0);
        for (int i = 0; i < n; i++) {
            double v = -12345;
            GE_CHECK(defaultsGetFor(pc, t[i].key, &v));
            GE_CHECK(v == t[i].value);
        }
    }
    GE_CHECK(!defaultsGetFor(0, "No.SuchKey", NULL));
    GE_CHECK(!defaultsGetFor(1, "", NULL));
}

static void values(void)
{
    /* Values the console build relies on, and a place Switch and PC differ on purpose. */
    static const struct { const char *key; double sw, pc; } t[] = {
        { "Game.CheckUpdates", 0, 0 },    /* the update check is opt-in */
        { "Game.AllUnlocked",  0, 0 },
        { "Audio.MasterVolume", 100, 100 },
        { "Input.AimMode",     1, 1 },      /* Centered everywhere */
        { "Input.PadTriggerPct", 10, 25 },  /* the Switch-tuned trigger point */
    };
    for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++) {
        double v = -1;
        GE_CHECK(defaultsGetFor(0, t[i].key, &v));
        GE_CHECK(v == t[i].sw);
        GE_CHECK(defaultsGetFor(1, t[i].key, &v));
        GE_CHECK(v == t[i].pc);
    }
#ifdef __SWITCH__
    GE_CHECK(0);   /* the tests never build for the Switch */
#else
    double v = -1;
    GE_CHECK(defaultsGet("Input.PadTriggerPct", &v));   /* defaultsGet = this platform's table (PC) */
    GE_CHECK(v == 25);
#endif
}

static double getv(const char *key)
{
    double v = -1;
    GE_CHECK(configGetValue(key, &v));
    return v;
}

static void migrations(void)
{
    GE_CHECK(kDefaultsVersion >= 1);
    for (int i = 0; i < kDefaultMigrationCount; i++) {
        const struct DefaultMigration *m = &kDefaultMigrations[i];
        GE_CHECK(m->version >= 1 && m->version <= kDefaultsVersion);
        GE_CHECK(m->key != NULL);
        GE_CHECK(!m->reset || defaultsGetFor(1, m->key, NULL));
    }

    /* A fresh state (Config.Version 0, as when no ini exists or an ini predates versioning). */
    double v;
    GE_CHECK(configGetValue("Config.Version", &v));
    GE_CHECK(v == 0);

    /* The defaults: Modern for every player, 1.1 Jinx as the Enhanced layout. */
    GE_CHECK(getv("Input.ControlScheme") == 2 && getv("Input.ControlScheme2") == 2 &&
             getv("Input.ControlScheme3") == 2 && getv("Input.ControlScheme4") == 2);
    GE_CHECK(getv("Input.PadPreset") == 0 && getv("Input.PadPreset4") == 0);

    /* An old ini, the port's controls (global Input.ControlScheme 0): native widescreen off -> forced 4:3; Visuals 0
     * -> per-vertex fog; Custom (3) -> 1.1 Jinx for every player; layout 5 -> the Modern style; players 2-4 take
     * the global style. */
    GE_CHECK(configSetValue("Video.NativeWidescreen", 0));   /* legacy key, registered by defaultsApply */
    GE_CHECK(configSetValue("Video.Visuals", 0));
    GE_CHECK(configSetValue("Video.PerPixelFog", 1));
    GE_CHECK(configSetValue("Video.AspectMode", 0));
    GE_CHECK(configSetValue("Input.ControlScheme", 0));
    GE_CHECK(configSetValue("Input.PadPreset", 3));
    GE_CHECK(configSetValue("Input.PadPreset2", 5));
    GE_CHECK(configSetValue("Input.PadPreset3", 2));
    GE_CHECK(configSetValue("Input.PadPreset4", 3));
    defaultsMigrate();
    GE_CHECK(getv("Video.AspectMode") == 1);
    GE_CHECK(getv("Video.PerPixelFog") == 0);
    GE_CHECK(getv("Input.ControlScheme") == 0 && getv("Input.PadPreset") == 0);    /* Custom -> Enhanced Jinx */
    GE_CHECK(getv("Input.ControlScheme2") == 2 && getv("Input.PadPreset2") == 0);  /* layout 5 -> Modern */
    GE_CHECK(getv("Input.ControlScheme3") == 0 && getv("Input.PadPreset3") == 2);  /* Frost stays Enhanced Frost */
    GE_CHECK(getv("Input.ControlScheme4") == 0 && getv("Input.PadPreset4") == 0);
    GE_CHECK(getv("Config.Version") == kDefaultsVersion);
    GE_CHECK(!configGetValue("Video.NativeWidescreen", &v));   /* legacy keys dropped */

    /* A second run does nothing: the ini is at the current version. */
    GE_CHECK(configSetValue("Input.PadPreset", 3));
    defaultsMigrate();
    GE_CHECK(getv("Input.PadPreset") == 3);

    /* An old ini in Original (global Input.ControlScheme 1): every player is Original, a stored layout 5 does not
     * make one Modern. */
    GE_CHECK(configSetValue("Config.Version", 0));
    GE_CHECK(configSetValue("Input.ControlScheme", 1));
    GE_CHECK(configSetValue("Input.ControlScheme2", 2));   /* absent from the old ini: the default */
    GE_CHECK(configSetValue("Input.PadPreset", 5));
    GE_CHECK(configSetValue("Input.PadPreset2", 5));
    defaultsMigrate();
    GE_CHECK(getv("Input.ControlScheme") == 1 && getv("Input.ControlScheme2") == 1 &&
             getv("Input.ControlScheme3") == 1 && getv("Input.ControlScheme4") == 1);
    GE_CHECK(getv("Input.PadPreset") == 0 && getv("Input.PadPreset2") == 0);

    /* An ini written after versioning (v1) by a build where Modern was layout 5. */
    GE_CHECK(configSetValue("Config.Version", 1));
    GE_CHECK(configSetValue("Input.ControlScheme", 0));
    GE_CHECK(configSetValue("Input.ControlScheme2", 2));
    GE_CHECK(configSetValue("Input.PadPreset", 5));
    GE_CHECK(configSetValue("Input.PadPreset2", 1));
    defaultsMigrate();
    GE_CHECK(getv("Input.ControlScheme") == 2 && getv("Input.PadPreset") == 0);
    GE_CHECK(getv("Input.ControlScheme2") == 0 && getv("Input.PadPreset2") == 1);   /* Christmas stays Enhanced Christmas */

    /* No ini (version 0 with every value at its default): nothing changes. */
    GE_CHECK(configSetValue("Config.Version", 0));
    for (int n = 0; n < 4; n++) {
        static const char *const ks[4] = { "Input.ControlScheme", "Input.ControlScheme2", "Input.ControlScheme3", "Input.ControlScheme4" };
        static const char *const kp[4] = { "Input.PadPreset", "Input.PadPreset2", "Input.PadPreset3", "Input.PadPreset4" };
        GE_CHECK(configSetValue(ks[n], 2));
        GE_CHECK(configSetValue(kp[n], 0));
    }
    defaultsMigrate();
    GE_CHECK(getv("Input.ControlScheme") == 2 && getv("Input.ControlScheme2") == 2 &&
             getv("Input.ControlScheme3") == 2 && getv("Input.ControlScheme4") == 2);
    GE_CHECK(getv("Input.PadPreset") == 0 && getv("Input.PadPreset3") == 0);
    GE_CHECK(getv("Config.Version") == kDefaultsVersion);
}

static void applied(void)
{
    /* defaultsApply() set every registered key to its default (an unmentioned ini key keeps it). */
    double want, got;
    GE_CHECK(defaultsGet("Game.CheckUpdates", &want));
    GE_CHECK(configGetValue("Game.CheckUpdates", &got));
    GE_CHECK(got == want);
}

int main(void)
{
    GE_TEST_BEGIN("defaults");
    tables();
    values();
    GE_CHECK_EQ_INT(nullPlatformInitSdl(), 0);
    defaultsApply();
    applied();
    migrations();
    return GE_TEST_END();
}
