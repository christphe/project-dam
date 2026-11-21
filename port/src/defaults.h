/* Defaults of every config key, and the ini's version: one table (defaults.json, generated into
 * defaults_data.c) shared by the options menu, the first-run ini and the migration of older inis. */
#ifndef DEFAULTS_H
#define DEFAULTS_H

#ifdef __cplusplus
extern "C" {
#endif

struct DefaultEntry { const char *key; double value; };

/* One migration step, applied to an ini older than `version`. Either sets `key` to `value` or resets it to
 * its default; with `whenKey` it only applies while that key equals `whenEq` (the key may be a legacy one). */
struct DefaultMigration {
    int version;
    const char *key;
    int reset;
    double value;
    const char *whenKey;
    double whenEq;
};
/* A key that no longer exists but is still read (with this value when the ini lacks it) so a migration can
 * look at it; it is dropped from the ini once the migrations ran. */
struct DefaultLegacy { const char *key; int value; };

/* Default of `key` on this platform. Returns 1 and sets *out, or 0 when the key has no default here. */
int defaultsGet(const char *key, double *out);
/* Same, for an explicit platform (0 = Switch, 1 = PC): the menu viewer shows both. */
int defaultsGetFor(int pc, const char *key, double *out);

/* Registers Config.Version and the legacy keys, and sets every registered key to its default. Called after
 * the modules registered their keys and before configLoad(): a key the ini does not mention keeps this value. */
void defaultsApply(void);
const char *portMenuLanguage(void);   /* Menu.Language in the ini ("" = English) */
int portDevMode(void);   /* Dev.Mode in the ini (0 by default) */
/* Called right after configLoad(): runs the migrations newer than the ini's Config.Version, drops the legacy
 * keys and stamps the current version (written back at the next configSave). */
void defaultsMigrate(void);

extern const struct DefaultEntry kDefaultsSwitch[], kDefaultsPc[];
extern const int kDefaultsSwitchCount, kDefaultsPcCount;
extern const int kDefaultsVersion;
extern const struct DefaultMigration kDefaultMigrations[];
extern const int kDefaultMigrationCount;
extern const struct DefaultLegacy kDefaultLegacy[];
extern const int kDefaultLegacyCount;

#ifdef __cplusplus
}
#endif
#endif
