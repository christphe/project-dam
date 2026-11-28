---
title: Control styles
sources:
  - port/src/input/input_types.h
  - port/src/input/style_modern.c
  - port/src/input/n64bridge.c
  - port/src/input.c
  - port/src/switch/nxgame.c
  - port/src/wheel.c
  - tests/test_input_modern.c
verified: 7d15f029
---

# Control style

How a gamepad turns into what the player does. Read this before touching pad input. The
keyboard and mouse path is separate (`port/src/input_kbm.c`, aim model in `port/src/input_aim.c`).

There is one style, **Modern**, for every player on both platforms: RT fire, LT aim, R3 crouch,
LB / RB weapon and gadget wheels; face buttons by position (bottom use, left reload, top next
weapon), so a Nintendo pad keeps the same hands. While aiming, D-pad up / down zooms a scope and
the left stick strafes slowly. The layout is fixed: there is no style choice, no preset and no
rebinding of pad buttons. The Enhanced layouts (1.1 Jinx ... 1.4 Elektra, Custom) and the Original
style (the pad read as a strict N64 pad) were removed; their ini keys (`Input.ControlScheme*`,
`Input.PadPreset*`, `Input.Pad*.<Action>`, `Input.N64Pad*.<Button>`, `Input.N64StickFeel`) are no
longer read.

## How a poll works

```
SDL pad ──> InputSnapshot ──> strategy.map() ──> InputCommands ──> n64bridge ──> N64 pad bits ──> game
   (input.c fills it:              (pure:                (fire, aim, use,       (port/src/input/
    buttons, sticks, triggers,      style_*.c)            zoom, sticks...)       n64bridge.c)
    family, settings, context)
```

- **A strategy is a pure function** (`InputStrategy.map` in `input_types.h`): no SDL header, no game
  global. Everything it needs is in the snapshot, including the previous poll's held actions
  (`prevActions`), so edges need no hidden state. The only state is the look low-pass
  (`InputStrategyState`).
- **`input.c` is the adapter:** `padStrategyFor(seat)` picks the strategy, `padPoll` builds the
  snapshot and runs it, then the commands are applied. What touches the game (aim models, crouch,
  the gadget cycle, the wheels in `wheel.c`) stays in the adapter.
- **Buttons by label vs. by position:** `INB_A` is the button SDL calls A (bottom on an Xbox pad,
  right on a Nintendo pad). A style that goes by position resolves it with `InputSnapshot.family`.
  The family comes from SDL's pad type (always Nintendo on the Switch); `GE_PADFAMILY=xbox|ps|nintendo`
  forces it for a pad SDL cannot type, such as a handheld's built-in pad. The options menu follows the
  same family: Nintendo letters, or the PlayStation icons (`port/src/menu/icons`, `tools_pc/gen_menuicons.py`).
- **GameDirect (#78):** the left stick's analog walk and strafe go
  straight into the game's movement through one `#ifdef PORT` hook in `src/game/bondview2.c`
  for every player in split screen. Buttons still reach the game as
  N64 bits.
- **Look ahead under GameDirect (#85):** the game turns its own look-ahead off for the 1.x
  control styles the port forces. A second hook in `bondview2.c` asks `portLookAheadMode()` (`input.c`): Original
  runs the game's look-ahead (a deflected look stick counts as a manual pitch), Low / Medium /
  High keep it off and `nxLookAheadStep()` (`nxgame.c`) eases the pitch toward the slope instead.
- **Settings are per player** (#83): the pad family and the latch are indexed by player, even when
  the keyboard and mouse play player 1 and the pads shift up a seat.

## Changing the style

- Change the rule in `style_modern.c`, then `tests/test_input_modern.c`. The tests feed tables
  of snapshots and check the commands: no game, no window. They were written against a verbatim
  copy of the old inline mapping, so a behaviour change shows up as a failing case to update on
  purpose.
- Build the Linux host target and run `ctest` (CI runs it in the `tests` job of
  `ci.yml`), or run the test executables of the Windows build.
- Rules of the move and traps: `docs/porting-notes.md` D54 (test binaries linking the core) and
  D55 (moving a style rule by rule).
