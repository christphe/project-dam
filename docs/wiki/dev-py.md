---
title: tools/dev.py
sources:
  - tools/dev.py
  - .env.example
  - tools/switch/nxlink.py
verified: 7d15f029
---

# `tools/dev.py`: build and run

One command for the build -> run loop: Docker builds, sending a build to the console over
nxlink, the Switch emulator, the N64 reference emulator, and the native Linux / Windows builds.
Use it instead of hand-written `docker run` or `nxlink.py` command lines.

```sh
python tools/dev.py build  {switch|args|link|linux|windows}
python tools/dev.py nx     [--build link|args|switch|none] [--name N] [--installed] [--level L] [--hard N] [--secs S] [ARGS...]
python tools/dev.py switch [--build args|switch|none] [--level L] [--hard N] [ARGS...]
python tools/dev.py n64
python tools/dev.py win    [--build] [--level L] [--hard N] [ARGS...]
python tools/dev.py linux  [--build] [--level L] [--hard N] [ARGS...]
python tools/dev.py config
```

`-h` works on every command.

## Setup

Machine paths are never in the script: they come from `<repo>/.env` (gitignored; copy
`.env.example`). A worktree has no `.env` of its own and uses the main checkout's. Real
environment variables override the file.

| Variable | Used by |
|---|---|
| `SWITCH_IP` | `nx`: the console's address on the local network |
| `SWITCH_EMU_EXE` | `switch`: the Switch emulator |
| `SWITCH_EMU_SD` | `switch`: its virtual SD card (default `%APPDATA%/Ryujinx/sdcard`) |
| `N64_EMU_EXE`, `N64_ROM` | `n64`; `N64_ROM` also seeds the native builds' `data/` |
| `DOCKER_IMAGE` | builds (default `local/project-dam-build:latest`, from `docker compose build`) |

`python tools/dev.py config` prints what it resolved.

## Build targets

| Target | Directory | What it is |
|---|---|---|
| `switch` | `build-switch` | The console build, what CI ships. |
| `args` | `build-switch-args` | `-DNX_ARGS=ON`: reads its arguments from `data/ge007.args`. For the emulator. |
| `link` | `build-switch-link` | `-DNX_LINKSTDIO=ON -DNX_ARGS=ON`: stdout and the crash dump come back over nxlink. For the console. |
| `linux` | `build-linux` | `docker compose run --rm linux` |
| `windows` | `build-win` | `docker compose run --rm windows` |

The Switch builds run in the Docker image with a shared ccache volume. A build directory is
configured once; later builds re-run CMake so new source files are picked up.

## Game arguments

Every run command takes:

- `--level L`: boot straight into a solo level, by number or by name (`dam`, `facility`,
  `runway`, … `cuba`; `-h` lists them). Becomes `-level_NN`.
- `--hard N`: difficulty 0..3 (`-hardN`).
- `ARGS...`: any other game argument, and `NAME=value` tokens that become environment
  variables (`GE_COOP=1 GE_PADS=2`, `GE_MENUGOTO=1,2`, `GE_QUITFRAME=300`…). On the Switch the
  game turns them into variables itself, and only the `args` and `link` builds do (the plain
  `switch` build ignores them). `win` and `linux` set them in the game's environment (through
  `WSLENV` for `linux` on Windows); the PC game never reads them from its command line.

## Commands

**`nx`** builds (default `link`) and sends the NRO to the console over nxlink. The game starts
at once; with a `link` build its output is written to `<build dir>/nx.log` (or `--log FILE`)
and a crash dump comes back too.

- It lands at `sdmc:/switch/project-dam/project-dam-test.nro`, next to the installed game,
  which it does not replace. `--installed` sends it over `project-dam.nro` instead (to test the
  in-game update).
- `--name foo` sends a copy named `foo-test.nro`, to tell test builds apart.
- `--secs S` quits after S seconds (`NX_TEST_SECS`).
- `--build none` resends the last build.
- The console must be awake with the netloader listening; one client at a time. On a non-link
  build, the timeout after the upload is expected (nothing sends output back).
- Details of the protocol: [`docs/switch/nxlink-stdio.md`](../switch/nxlink-stdio.md); test arguments and logs: [testing-on-switch.md](testing-on-switch.md).

**`switch`** builds (default `args`), copies the NRO to the emulator's SD card as
`switch/project-dam/project-dam.nro`, writes the arguments to `data/ge007.args` (the first time
it keeps a `.args.bak` of the one already there) and starts the emulator. The game's log is
`switch/project-dam/data/ge007.log` on that SD card.

**`win` / `linux`** run the native build (`--build` rebuilds it first). The first run in a new
worktree creates `data/` next to the executable and copies the ROM into it (from `N64_ROM`, or
the one the Switch emulator already has). It reports a game that exits within three seconds
instead of failing silently. `linux` on Windows runs through WSL.

**`n64`** opens the ROM in the N64 emulator, for reference captures.

## Examples

```sh
python tools/dev.py nx --level facility                    # console, Facility, log in build-switch-link/nx.log
python tools/dev.py nx --name coop GE_COOP=1 GE_PADS=2     # co-op test build, named coop-test.nro
python tools/dev.py switch --level dam --hard 3            # emulator, Dam on 00 Agent
python tools/dev.py switch --build none GE_MENUGOTO=2,0    # emulator, last build, menu open on Input
python tools/dev.py win --build                            # rebuild and run the Windows build
```
