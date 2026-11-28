# Cuba

Setup file `UsetuplenZ`, text bank 20 (ids 0x5000..). The ending: Bond and Natalya on the jungle clearing,
three lines of dialogue, then the credits roll and the level ends. There is no gameplay: the player never
gets control.

## Characters
| chr | Who | Difficulties | Starts as | Notes |
|---|---|---|---|---|
| 0x00 | Natalya (body 79, head chosen by the game) | all | placed at pad 0x2D, idle | not hostile, no weapon; hearing 1.0, vision 100 |
| player | Bond, shown in third person (the intro's third-person body) | all | spawns at pad 0x2C (intro spawn) | outfit (cuff) 2, starts with no weapon (item 0 right, none left) |

## Objectives
None. The setup has only the briefing pages (watch-menu texts 0x500B..0x500F: title, overview, M, Q,
Moneypenny).

## Rules
1. **When** the level starts, **then** the closing scene (Cinematics) plays at once: the HUD is hidden except
   the top-of-screen text line, the player's weapon is hidden, controls are locked and the mission clock is
   stopped for the whole level.
2. **When** the scene starts, **then** Natalya is placed on pad 0x2D and starts animation 0xB6 (whole animation, with
   its root translation, played once and held on its last frame);
   Bond's third-person body is placed on pad 0x2C and starts animation 0xB5 (the same way). No animation in
   this level loops by itself: every play runs once to its end frame and holds it. (A second Bond start,
   pad 0x2F, exists only for a debug alternative-actor option that the retail game never selects: ignore it.)
3. **When** Bond's animation reaches the end of its current play, **then** the pair picks their next shared
   moment: 86% of the time Bond replays animation 0xB5 frames 158..250 and Natalya animation 0xB6 frames
   158..250; 14% of the time both play frames 259..end of their animation instead. Both plays are without
   root translation, play once and hold their last frame (no looping), and blend in over about 0.5 s. Only
   Bond's play is watched: Natalya's play of the same frame range is started at the same moment and
   simply holds if it ends first. This repeats each time Bond's play ends, for
   the rest of the level, so the two stay in step.
4. **When** the credits have finished rolling, **then** the screen fades to black and the level ends (as a
   completed level: the game moves on to what follows the last mission).

## Objects and world
- No doors, objects, vehicles, fog, music or alarm changes are scripted. The level's props (jungle scenery
  objects, prop records 5..26) are static.
- A camera-position record (prop 28) at pad 0x2C exists for the setup's own intro camera; the scene in
  rule 1 uses the orbiting camera below instead.

## Cinematics
**Intro data** (setup intro data, `intro.md` units): no fixed camera, no swirl point and no intro-animation
record, so none of the intro sequence of `intro.md` plays. The intro data is only the start: pad 44 (0x2C),
item 0 (unarmed), outfit (cuff) 2, no watch time; plus the credits block (step 6).

The whole level is one scene:
1. The camera goes to third person and orbits pad 0x012C. The setup has only 48 pads (0x00..0x2F), so that
   number is out of range and the original reads whatever lies past the pad table: orbit the couple instead
   (the midpoint of pads 0x2C and 0x2D) unless measurement in the original shows another centre. Distance 100 units
   from the pad, 18 units up, aimed 70 units below the pad's height, starting at compass angle 0x1C71
   (about 40 degrees), turning slowly (orbit speed 46 in 1/65536 turns per tick, about one turn every
   23 s) for the whole level.
2. The screen fades in from black (the game's default fade length).
3. 0.5 s after the fade has completed: top text 0x5008 (Natalya asks if he is fine).
4. 3 s later: top text 0x5009 (Bond answers he is fine).
5. 3 s later: top text 0x500A (Natalya sighs his name).
6. 5 s later: the credits roll (the setup's credits block, data from the intro records: text pairs with
   positions and alignments; see docs/specs/setup.md §Intro). The orbit and the rule 3 cycle continue
   behind them.
7. When the credits have completed: fade to black, then the level ends (rule 4).

## Co-op notes
Cuba has no gameplay. With two players, both views should show the same scene; only one Bond body is
shown (player 1's). The second player can be hidden for the scene.

## API needed
- existing: `wait(seconds)`, `mission.win()`.
- new: `cinema:begin(keep_top_text)` hide the HUD (optionally keep the top text line), hide the player's
  weapon, lock every player's controls, stop the mission clock, and show the player as a third-person body
  that scripts can address (`player_body(n)`).
- new: `camera:orbit(centre, distance, height, target_offset_y, start_angle, speed)` third-person camera
  circling a pad at a fixed distance and height, aimed at the pad plus an offset, turning at a constant rate.
- new: `screen:fade_in()` / `screen:fade_out()` the game's fade from / to black; `screen:wait_fade()` until
  it completes.
- new: `say_top(text_id)` print a text id on the top text line (subtitle-style dialogue).
- new: `credits:roll()` start the end credits of the setup's credits block; `credits:wait()` until done.
- new: `chr(n):teleport(pad)` place a character on a pad.
- new: `chr(n):play_anim(anim_id, start_frame?, end_frame?, opts)` play a native animation by id, with
  frame range (30 fps frames, nil = whole), options `loop`, `no_translation`, `blend` (seconds); and
  `chr(n):anim_done()` / event `anim_end` when the current play has stopped.
- new: `player_body(n)` the third-person body of player n during a cinema, accepting the same `chr` verbs.
- new: `random(percent)` true with the given probability (or `math.random`).
