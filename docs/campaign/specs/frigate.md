# Frigate

Setup file `UsetupdestZ` (US), text bank 13 (ids 0x3400..). The frigate La Fayette in Monte Carlo harbour, taken
by Janus: free the hostages held at gunpoint, defuse the two bombs, and throw a tracker bug onto the stealth
helicopter before leaving.

## Characters
| chr | Who | Difficulties | Starts as | Notes |
|---|---|---|---|---|
| 0x00, 0x02, 0x04, 0x08, 0x0A, 0x0C, 0x0E, 0x10, 0x12, 0x1E, 0x20, 0x22, 0x24, 0x26, 0x28, 0x2A | Janus sailors (body 17) | all | each on the pad of its own number | generic guard (global.md, standard); 0x10, 0x22 grenade probability 25 |
| 0x03, 0x05, 0x0B, 0x0D, 0x11, 0x13, 0x15, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1D, 0x1F, 0x21, 0x23, 0x25, 0x27, 0x29 | Janus sailors (body 17) | all | each on the pad of its own number | deaf simple generic guard (global.md); 0x1A grenade probability 25 |
| 0x01 | hostage taker (body 17) | all | pad 1 | rules 1-3; holds hostage 0x2C |
| 0x09 | hostage taker | all | pad 9 | holds hostage 0x2D |
| 0x0F | hostage taker | all | pad 15 | holds hostage 0x30 |
| 0x14 | hostage taker | all | pad 20 | holds hostage 0x34 |
| 0x16 | hostage taker | all | pad 22 | holds hostage 0x31 |
| 0x1C | hostage taker | all | pad 28 | holds hostage 0x35 |
| 0x2C, 0x2D, 0x30, 0x31, 0x34, 0x35 | hostages (body 20) | all | each on the pad of its own number, hands up | rules 4-6; their deaths count as civilian deaths (rule 7); each knows its captor (0x2C: 0x01, 0x2D: 0x09, 0x30: 0x0F, 0x31: 0x16, 0x34: 0x14, 0x35: 0x1C) |
| player | Bond | all | intro spawn pad 176 | outfit (cuff) 0; item 0x0B with 40 rounds of ammo type 1, Walther PPK (item 4) with 20 more, bomb defuser (item 0x27), one tracker bug (item 0x2F; ammo type 20: 1). A second start (slot 1) on pad 139 with item 0x0B and 50 rounds exists for an extra player |

All have hearing 1.0, vision 100, random heads. Chrs 0x0B..0x27 wear hat model 220 (setup hat records).

## Objectives
| Letter | Text | Difficulties | Completed when | Failed when |
|---|---|---|---|---|
| a | 0x3414 (rescue the hostages) | all | five hostage escapes are counted (rule 6; Agent and Secret Agent start with some counted, rule 8) | too many hostages died (rule 7) |
| b | 0x3415 (disarm the bridge bomb) | Secret Agent and up | rule 10 on the bomb of tag 1 | the bomb of tag 1 is destroyed (rule 11) |
| c | 0x3416 (disarm the engine room bomb) | Secret Agent and up | rule 10 on the bomb of tag 2 | the bomb of tag 2 is destroyed (rule 11) |
| d | 0x3417 (plant the tracker on the helicopter) | all | the tracker bug sticks to the helicopter (rule 12) | the bug came to rest anywhere else (rule 12; this failure is undone if a bug later sticks to the helicopter) |

Briefing pages: watch-menu texts 0x340F..0x3413 (title, overview, M, Q, Moneypenny).

## Rules

### Hostages and their captors
1. **Captor.** From the start the hostage taker aims at his hostage. **When** a player's shot lands near him, or
   he sees the player or has a line of sight to him, **then** a countdown starts: 3 s on Agent, 2.5 s on Secret
   Agent, 2 s on 00 Agent and 007.
2. **When** the countdown ends, **if** he has not been hit, **then** he shoots his hostage dead (the hostage is
   killed by the shot), and a fraction of a second later he runs to the player and behaves as the run-to-Bond
   generic guard (global.md), falling back to the standard generic guard.
3. **When** he is hit before the countdown ends (or at any time after he noticed the player), **then** he forgets
   the hostage: then behaves as the standard generic guard (global.md).
4. **Hostage.** He stands with his hands up. **When** he dies, **then** top text 0x3408 (a hostage was executed).
   (His death is noticed only once he has seen or been in line of sight of the player; a hostage killed before
   that prints nothing.)
5. **When** his captor no longer exists (killed and faded away), **if** the hostage lives, **then** top text 0x3409
   (hostage released) and he flees (rule 6).
6. **Fleeing hostage.** He drops whatever he carries and runs to one of six escape pads picked at random: 143,
   145, 147, 148, 168 (16% each) or 169 (21%). **When** a shot lands near him on the way, **then** he raises his
   hands for a moment (until the gesture ends, he sees a guard die, or 14% per tick), then picks a new escape pad
   and runs. **When** he stops short of the pad (more than 500 units away), he picks a new one. **When** he arrives
   (within 500 units) and a player has a line of sight to him, he picks a new one and runs on. **When** he arrives
   and no player sees him, **then** top text 0x340A (hostage escaped), he fades out and is removed, and one escape
   is counted for objective a.
7. **When** the level starts, the six hostages are marked so that their deaths (by anyone) count as civilian
   deaths. **When** the number of dead civilians exceeds 4 on Agent, 2 on Secret Agent, 1 on 00 Agent and 007,
   **then** top text 0x341E (unacceptable hostage casualties: failure) and objective a fails.
8. **When** the level starts, **then** on Agent three escapes are already counted (two hostages must escape), on
   Secret Agent one (four must escape); on 00 Agent and 007 none (five must escape).

### Bombs
9. The two bombs (tag 1 on the bridge, tag 2 in the engine room) react to the player using something on them.
10. **When** the player uses a gadget on a bomb while holding the bomb defuser (item 0x27), **then** it is defused:
    sound 0xF7 plays from the bomb (channel 1 for tag 1, channel 2 for tag 2), top text 0x340D (bridge bomb
    defused) or 0x340E (engine room bomb defused), its objective (b or c) completes, and the bomb becomes
    indestructible.
11. **When** the player uses a different gadget on a bomb, or activates it (presses the use button on it), **then**
    the bomb is destroyed (it explodes). **When** a bomb is destroyed by any means, **then** its objective (b for
    tag 1, c for tag 2) fails.

### Tracker bug
12. **When** a tracker bug (item 0x2F) is thrown and sticks to the helicopter (tag 0), **then** top text 0x340B
    (tracker planted) and objective d is complete (and not failed). **When** a thrown bug comes to rest anywhere
    else first, **then** top text 0x340C (careless with the bug) and objective d is failed; **if** a bug then sticks
    to the helicopter, the failure is lifted and rule 12's success applies. **If** the planted bug ever stops
    being attached, objective d is failed again.

### Engine room sound
13. **When** the level starts, **then** sound 0x3C starts on channel 0 at volume 0. **When** the player comes within
    900 units of bound pad 10068 (the object of tag 3), or enters the room of pad 108, 28, 53, 110, 29, 117, 102
    or 99, **then** its volume rises to full over 2 s; **when** he is again farther than 1400 units from bound pad
    10068, it fades to 0 over 4 s (and can rise again).
14. **When** the engine room bomb (tag 2) is destroyed, **then** the object of tag 3 loses its indestructibility and
    is destroyed, and the channel 0 sound stops for good.

### Exit
15. **When** the player, after having left the rooms of pads 153 and 149 (the start area), enters the room of
    pad 153 again (the boat), **if** he is alive, **then** he becomes invulnerable and cannot pick up, the HUD is
    hidden, controls locked, the mission clock stops and the screen fades to black. **If** not every objective
    of the difficulty is complete, the level ends there (the game's normal end-of-level, a failure). **Else** the
    outro plays (Cinematics).

## Objects and world
- Helicopter: aircraft of tag 0 (prop 101, model 283, pad 59). Its rotor turns at speed 620 from the start and it
  holds animation 2 at frames up to 2; it never flies in this level. Target of rule 12.
- Bombs: tag 1 (prop 91, model 273, pad 60) and tag 2 (prop 93, model 273, pad 61), rules 9-11.
- Tag 3: prop 99 (model 99, bound pad 10068) in the engine room, indestructible until rule 14 destroys it.
- Monitors (props 25..58) are scenery, not scripted.
- Sound: 0x3C on channel 0 (rules 13-14), 0xF7 for a defused bomb. No music, fog or alarm changes.

## Cinematics
**Intro** (setup intro data). Played as `intro.md` describes; the data below is in its units (world units, API
degrees, seconds).

- **Fixed cameras:** 5 (one is picked at random); world units, API degrees, fov 60 (intro.md §1). Lines: 0x3418
  (location title); no second line; the shot lasts 5 s, then fades to black over 1 s.

  | Camera | Position (x, y, z) | Yaw | Pitch | Room of pad |
  |---|---|---|---|---|
  | 0 | (-551.83, 282.51, 4099.82) | 162.5 | 8.0 | 176 |
  | 1 | (-340.38, 611.65, -2431.48) | 116.6 | -20.0 | 90 |
  | 2 | (327.95, 283.23, -3618.31) | -149.4 | -26.0 | 113 |
  | 3 | (-643.10, 390.29, -6564.73) | 49.8 | 12.8 | 168 |
  | 4 | (-4006.07, 522.51, 4115.82) | 135.4 | 0.0 | 176 |

- **Swirl points** in order (intro.md §2): every offset in the player frame (flag 2: x left, y up, z forward, from
  the eye point); no point looks at the eye (no flag 4); the end marker (flag 1) follows the last row. Duration
  and tension belong to the leg that starts at the point. Swirl length 3.668 s (the legs from P1 on: P0 is only the previous control point, intro.md section 2).

  | Point | Offset (x, y, z) | Duration (s) | Tension |
  |---|---|---|---|
  | P0 | (10, 151, 525) | 0.667 | 0.75 |
  | P1 | (10, 151, 525) | 0.667 | 0.75 |
  | P2 | (20, 127, 429) | 0.667 | 0.75 |
  | P3 | (80, 67, 179) | 0.667 | 0.75 |
  | P4 | (80, 31, 0) | 0.667 | 0.75 |
  | P5 | (20, 34, -100) | 0.667 | 0.75 |
  | P6 | (0, 10, -30) | 0.333 | 0.25 |
  | P7 (final) | (0, 0, 0) | not used | not used |
  | P8 (shape only) | (0, 0, 0) | not used | not used |

- **Intro animation:** no record: table entry 0 (animation 61 from frame 95, speed 0.02; intro.md §3).
- **Body during the swirl:** the table pose only; no level script animates it.
- **Start (setup data):** pad 176; item 11 (silenced D5K), 40 of ammo type 1, item 4 (PP7), 20 of ammo type 1,
  item 39 (bomb defuser), item 47 (covert modem), 1 of ammo type 20; outfit (cuff) 0; watch 2:35. Other start
  slots (not used in solo play): slot 1 pad 139, item 11 (silenced D5K), 50 of ammo type 1.
- Region: EU and JP move camera 2 to (424.60, 263.23, -3701.07), yaw -144.7, pitch -16.0 (same room, same line).

**Outro** (rule 15, all objectives complete): after the fade to black, all characters are hidden, any button
press from now on fades out and ends the level. **If** the player holds none of items 2-22 (knife, throwing knife, PP7 through Moonraker laser) or 24-29 (grenade launcher, rocket
launcher, hand grenade, the three mines) in his right hand, that is unarmed (0), fist (1), the watch laser (23)
or any item from 30 up, Bond is shown holding the Walther PPK (item 4). His weapon view is hidden and the view
cuts to the cutscene camera of tag 4 (prop 257: (-974.23, -93.22, 137.55), yaw 3.161, pitch 6.102, room of pad
176). Fade in. Bond's third-person body:
1. is placed on pad 176 with his left hand emptied and plays animation 0x69 frames 0..29;
2. when that ends, his right hand is emptied and he plays animation 0x69 frames 29..160;
3. when it ends, or 3.7 s after step 2 began, fade to black and the level ends (success).

## Co-op notes
- Rules 1, 6: "sees the player" means any player; an escaping hostage must be unseen by every player.
- Rule 10: the player using the gadget must hold the defuser himself.
- Rule 12: a bug thrown by either player.
- Rule 15: the exit should wait for both players (or the first one ends it for both); one Bond body in the outro.
- The setup has a slot-1 start (pad 139, item 0x0B, 50 rounds) that fits a second player.

## API needed
- existing: `chr(n)`, `tag(n)`, `wait(seconds)`, `on(name, fn, filter)`, `spawn(fn)`,
  `objectives.complete/fail/reset/all_complete`, `mission.difficulty`, `Target:sees_player`,
  `Target:was_shot_at`, `Target:hits`, `Target:surrender`, `Target:was_used`, `Target:make_usable`, `has`,
  `event("death")`, `text(id)`.
- new: `chr(n):exists()`.
- new: `chr(n):aim_at_chr(other)` keep a guard covered (existing `aim_at` takes a Target; this is the chr-number
  form) and `chr(n):execute(other)` fire at it and kill it with a shot's damage.
- new: `chr(n):set_ai_global(name, return_to)` hand to a generic behaviour of global.md ("run_to_bond",
  "standard").
- new: `chr(n):has_los_to_player()`, events `chr_near_miss {chr}`, `chr_saw_guard_die {chr}`.
- new: `chr(n):run_to(pad, speed)`, `chr(n):is_moving()`, `chr(n):drop_carried()`, `chr(n):fade_out()`,
  `player(n):in_room_of(pad)`, `player(n):distance_to_pad(pad)`.
- new: `chr(n):set_civilian(on)`; `mission.civilians_killed()`.
- new: `say_top(text_id)`.
- new: event `gadget_used {tag, item}` a player used a gadget on a tagged object (the item he held), and
  event `activated {tag}` a player pressed use on it.
- new: `object(tag):is_destroyed()` / event `destroyed {tag}`; `object(tag):destroy()`;
  `object(tag):set_invincible(on)`.
- new: `item_thrown_onto(item, tag)` / event `item_attached {item, tag}` a thrown item stuck to a tagged
  object; event `item_landed {item}` a thrown item came to rest in the level.
- new: `objectives.set_failed(id, on)` a failure that can be lifted again (the existing `fail` is final).
- new: `sound:play(sound_id, channel)`, `sound:emit_from(channel, tag)`, `sound:volume(channel, volume,
  fade_seconds)`, `sound:stop(channel)`.
- new: `aircraft(tag):set_rotor(speed, accel_time)`, `aircraft(tag):play_anim(anim_id, start, end)`.
- new: `cinema:begin(opts)`, `cinema:hide_all_chrs()`, `cinema:exit_on_button()`, `camera:cut_to(tag)`,
  `screen:fade_in()`, `screen:fade_out()`, `screen:wait_fade()`, `player_body(n)` with `teleport`, `play_anim`,
  `anim_done`, `empty_hand(hand)`; `player(n):held_item()`, `cinema:show_with_weapon(item)`.
- new: `mission.exit()` end the level now and let the objectives decide; `player(n):is_dead()`.
- new: `random(percent)`.
