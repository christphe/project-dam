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
    /* Values the console build relies on, and the one place Switch and PC differ on purpose. */
    static const struct { const char *key; double sw, pc; } t[] = {
        { "Game.CheckUpdates", 0, 0 },    /* the update check is opt-in */
        { "Game.AllUnlocked",  0, 0 },
        { "Audio.MasterVolume", 100, 100 },
        { "Input.AimMode",     1, 0 },
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
    GE_CHECK(defaultsGet("Input.AimMode", &v));   /* defaultsGet = this platform's table (PC) */
    GE_CHECK(v == 0);
#endif
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

    /* Version 1: native widescreen off -> forced 4:3; Visuals 0 -> per-vertex fog; Custom pad layout (3) -> 0. */
    GE_CHECK(configSetValue("Video.NativeWidescreen", 0));   /* legacy key, registered by defaultsApply */
    GE_CHECK(configSetValue("Video.Visuals", 0));
    GE_CHECK(configSetValue("Video.PerPixelFog", 1));
    GE_CHECK(configSetValue("Video.AspectMode", 0));
    GE_CHECK(configSetValue("Input.PadPreset", 3));
    defaultsMigrate();
    GE_CHECK(configGetValue("Video.AspectMode", &v));
    GE_CHECK(v == 1);
    GE_CHECK(configGetValue("Video.PerPixelFog", &v));
    GE_CHECK(v == 0);
    GE_CHECK(configGetValue("Input.PadPreset", &v));
    GE_CHECK(v == 0);
    GE_CHECK(configGetValue("Config.Version", &v));   /* stamped with the current version */
    GE_CHECK(v == kDefaultsVersion);
    GE_CHECK(!configGetValue("Video.NativeWidescreen", &v));   /* legacy keys dropped */

    /* A second run does nothing: the ini is at the current version. */
    GE_CHECK(configSetValue("Input.PadPreset", 3));
    defaultsMigrate();
    GE_CHECK(configGetValue("Input.PadPreset", &v));
    GE_CHECK(v == 3);
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
