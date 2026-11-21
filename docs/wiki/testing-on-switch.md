---
title: Testing on the Switch
sources:
  - tools/dev.py
  - tools/switch/nxlink.py
  - tools/switch/make_sd.sh
  - port/src/config.c
  - port/src/input_script.c
  - port/src/coop.c
verified: e2fda9e2
---

# Testing on the Switch

The three ways to run a build, what each is good for, and the arguments that put the game in the
state you want to look at. Commands go through [dev.py](dev-py.md).

## Where to run it

| | Command | Good for | Not for |
|---|---|---|---|
| **Emulator** | `dev.py switch` | Menus, logic, screenshots, anything a script can drive. | Performance, feel, the exact look of the sea / sky (judge those on the console). |
| **Console over nxlink** | `dev.py nx` | Performance, feel, crashes: the log and crash dump come back to the PC. | Runs you need to start with buttons on a second pad you don't have. |
| **Console from the SD card** | `dev.py build switch`, then `tools/switch/make_sd.sh` | What a player gets: the release build, the update path. | Iterating (no log comes back). |

- The emulator reads its arguments from `data/ge007.args` on its SD card (`args` build). The console
  gets them on the nxlink command line (`link` build). The plain `switch` build reads none: never
  ship an `args` or `link` NRO.
- nxlink sends to `sdmc:/switch/project-dam/project-dam-test.nro`, beside the installed game.
  The console must be awake with the netloader up; one client at a time. Keep the client running
  for the whole test: the log arrives through it.
- A game window closed by hand shows up in the log as "quit requested (window closed)": not a bug.

## Arguments

| Argument | Effect |
|---|---|
| `--level L` / `-level_NN` | Boot straight into a solo level. |
| `--hard N` / `-hardN` | Difficulty 0..3. |
| `--secs S` / `NX_TEST_SECS=S` | Quit after S seconds. |
| `GE_CFG="Key=value;Key2=value2"` | Override ini values for this run; the ini is not written back. |
| `GE_COOP=1..4`, `GE_PADS=N` | Co-op without the menu; force N controller seats. Always together ([coop.md](coop.md)). |
| `GE_STARTWEAPON=N` | Start with that item (8 = KF7). |
| `GE_INPUTSCRIPT="frame:BUTTONS;..."` | Scripted input for player 1 (`GE_INPUTSCRIPT_P1..P3` for the others), counted in controller reads: `120:START;180:A`, or `300:ZHOLD` to hold fire from 300. Token list: the header of `port/src/input_script.c`. |
| `GE_MENUGOTO=tab,entry[,focus]` | Open the options menu on that page ([options-menu.md](options-menu.md)). |

Every `GE_*` probe, with its file and status: `docs/dev/GE-ENV-PROBES.md`. `python
tools_pc/gen_env_probes.py` reports the probes it misses and today's file:line of each one.

## Logs

- Emulator: `switch/project-dam/data/ge007.log` on its SD card (`ge007.prev.log` is the run before).
- Console over nxlink: `<build dir>/nx.log` on the PC.

## A bug at a precise spot

Input record / replay (`GE_INPUTRECORD` / `GE_INPUTREPLAY`) diverges under the emulator, so it does
not bring you back to a spot. What works: log the player position once while someone walks there,
then start each run at that position with a temporary local teleport hook (not committed: it is a
game-code change), plus `GE_STARTWEAPON` and `GE_INPUTSCRIPT` for the action. Nobody has to replay
the level for each attempt.
