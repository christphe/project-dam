# Wiki index

One line per page. Conventions: [README.md](README.md). Changes: [log.md](log.md).

## Working on the fork

- [dev-py.md](dev-py.md) — `tools/dev.py`: build in Docker, send to the console over nxlink, run in the Switch emulator or natively.
- [testing-on-switch.md](testing-on-switch.md) — emulator or console, test arguments (`GE_*` tokens), logs, reproducing a spot without replaying.
- [releases.md](releases.md) — the changelog, cutting a `project-dam-vX.Y.Z` release, what CI publishes.

## The game

- [first-launch.md](first-launch.md) — where the ROM goes, the one-time conversion into PC data, the `ge007-convert` CLI.
- [options-menu.md](options-menu.md) — the fullscreen menu: `menu.json`, how values are bound, defaults, adding a setting.
- [ini-keys.md](ini-keys.md) — every key with a default, per platform, and where it sits in the menu.
- [control-styles.md](control-styles.md) — the Modern pad style as a pure strategy, the N64 bridge, GameDirect, the tests.
- [coop.md](coop.md) — co-op for 2 to 4 players: the hooks in the game, device assignment, the rules.
- [model-subdivision.md](model-subdivision.md) — the converter's head subdivision and LOD rule, the buffers that bound character files, the `modelview` viewer.
