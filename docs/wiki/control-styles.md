---
title: Control styles
sources:
  - port/src/input/input_types.h
  - port/src/input/style_original.c
  - port/src/input/style_enhanced.c
  - port/src/input/style_modern.c
  - port/src/input/n64bridge.c
  - port/src/input.c
  - port/src/switch/nxgame.c
  - port/src/wheel.c
  - tests/test_input_original.c
  - tests/test_input_enhanced.c
  - tests/test_input_modern.c
verified: 7d15f029
---

# Control styles

How a gamepad turns into what the player does, for the three control styles. Read this before
touching pad input. The keyboard and mouse path is separate (`port/src/input_kbm.c`, aim model
in `port/src/input_aim.c`).

## The three styles

| Style | Ini | What it is |
|---|---|---|
| **Original** | `Input.ControlScheme=1` | The N64 control styles (1.1 Honey … 2.4 Goodhead, chosen per profile with `Bond.Control`). The pad is read as an N64 pad. |
| **Enhanced** | `Input.ControlScheme=0`, `Input.PadPreset` 0..4 | The remake layouts: 1.1 Jinx, 1.2 Christmas, 1.3 Frost, 1.4 Elektra (3 is a retired Custom slot, migrated to Jinx). The preset picks the stick roles; buttons come from a bind table. |
| **Modern** | `Input.ControlScheme=2` | The default on both platforms, for every player. RT fire, LT aim, R3 crouch, LB / RB weapon and gadget wheels; face buttons by position (bottom use, left reload, top next weapon), so a Nintendo pad keeps the same hands. While aiming, D-pad up / down zooms a scope and the left stick strafes slowly. |

The style and the Enhanced layout are both per player: **Control style** is
`Input.ControlScheme`, `Input.ControlScheme2..4`, and **Layout** (Enhanced only) is
`Input.PadPreset`, `Input.PadPreset2..4`. Each menu row writes only its own key. Inis older than
`Config.Version` 2 had one global `Input.ControlScheme` (0 = the port's controls) and Modern as
layout 5; the v2 migration converts them.

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
- **GameDirect (#78):** in Enhanced and Modern, the left stick's analog walk and strafe go
  straight into the game's movement through one `#ifdef PORT` hook in `src/game/bondview2.c`
  (signed off under rule #2), for every player in split screen. Buttons still reach the game as
  N64 bits.
- **Look ahead under GameDirect (#85):** the game turns its own look-ahead off for the 1.x
  control styles. A second hook in `bondview2.c` asks `portLookAheadMode()` (`input.c`): Original
  runs the game's look-ahead (a deflected look stick counts as a manual pitch), Low / Medium /
  High keep it off and `nxLookAheadStep()` (`nxgame.c`) eases the pitch toward the slope instead.
- **Settings are per player** (#83): preset, bindings, latch and the Modern style are indexed by
  player, even when the keyboard and mouse play player 1 and the pads shift up a seat.

## Changing a style

- Change the rule in `style_*.c`, then the matching `tests/test_input_*.c`. The tests feed tables
  of snapshots and check the commands: no game, no window. They were written against a verbatim
  copy of the old inline mapping, so a behaviour change shows up as a failing case to update on
  purpose.
- Build the Linux host target and run `ctest` (CI runs it in the `tests` job of
  `switch.yml`), or run the test executables of the Windows build.
- Rules of the move and traps: `docs/porting-notes.md` D54 (test binaries linking the core) and
  D55 (moving a style rule by rule).
