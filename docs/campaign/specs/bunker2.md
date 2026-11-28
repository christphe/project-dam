# Bunker 2 (Severnaya bunker, captured)

Setup file `assets/obseg/setup/UsetupsevbZ.c`, text bank 29 (ids 0x7400..). Bond wakes up unarmed in a cell
with Natalya. They talk, Bond fools the jailer, escapes, collects the evidence; Natalya checks the control
console, which starts the satellite's countdown, and both must get out before the bunker is destroyed.

Pads are plain pad numbers in decimal; bound pads keep their +10000. "Prop" is the setup prop number.

## Characters
| chr | Who | Difficulties | Starts as | Notes |
|---|---|---|---|---|
| 0x00 | Natalya, pad 97 | all | in the cell (rules 1-4) | body 16; chr flag 0x80 set at the start; her death fails e (rule 14) |
| 0x12 | jailer, pad 100 | all | walks between pads 100 and 96 (rule 5) | body 38; carries cell key 1 (prop 241, renamed 7439..743D) |
| 0x01, 0x02 | guards, pads 0, 1 | all | standing | body 38; hearing 1.0, vision 100; generic standard guard (global.md) |
| 0x03 | guard, pad 2 | all | standing | body 38; hearing 0.8; clones on gunfire; standard guard (global.md) |
| 0x04 | guard, pad 3 | all | standing | hearing 0.6; clones on gunfire; standard guard (global.md) |
| 0x05-0x09 | guards, pads 4, 5, 6, 7, 8 | all | standing | clones on gunfire; standard guard (global.md); 0x06 carries twinned safe key 2 (prop 225, renamed 744D..7451) |
| 0x0A | guard, pad 9 | all | standing | standard guard (global.md) |
| 0x0B | guard, pad 10 | all | standing | clones on gunfire; carries twinned safe key 1 (prop 232, renamed 7448..744C); standard guard (global.md) |
| 0x0C | guard, pad 11 | all | standing | standard guard (global.md) |
| 0x0D | guard, pad 12 | all | standing | deaf simple guard (global.md) |
| 0x0E | guard, pad 13 | all | standing | hearing 0.7; standard guard (global.md) |
| 0x0F-0x11 | guards, pads 14, 21, 22 | all | standing | clones on gunfire; standard guard (global.md) |
| 0x13 | guard, pad 17 | all | standing | deaf simple guard (global.md) |
| 0x14 | guard, pad 18 | all | standing | carries the casualty list (tag 0, prop 246, renamed 7457..745B); standard guard (global.md) |
| 0x15, 0x16 | guards, pads 19, 20 | all | standing | clones on gunfire; standard guard (global.md) |
| 0x17 | guard, pad 60 | all | standing | standard guard (global.md) |
| 0x18 | guard, pad 82 | all | standing | clones on gunfire; standard guard (global.md) |
| 0x19, 0x1A | patrol, pads 86, 81 | all | patrol path 0 | hearing 0.5; then simple guard (global.md); 0x1A carries keycard B (prop 254, renamed 7443..7447) |
| 0x1B-0x1E | patrol, pads 67, 70, 92, 80 | all | patrol path 1 | hearing 1.0 / 0.5 / 0.5 / 0.7; then simple guard (global.md) |
| 0x1F-0x22 | patrol, pads 114, 113, 111, 105 | all | patrol path 2 | hearing 0.5 / 0.5 / 0.7 / 1.0; then simple guard (global.md); 0x22 carries keycard A (prop 266, renamed 743E..7442) |
| 0x28, 0x29 | guards, pads 23, 24 | all | standing | deaf simple guard (global.md) |
| 0x23-0x27 | alarm reinforcements | all | spawned by rule 15 | body 38, random head, sunglasses; outfit by rule 15 |

Cell key 2 (prop 34, renamed 7434..7438) lies at bound pad 10076.

## Objectives
| Letter | Text | Difficulties | Completed when | Failed when |
|---|---|---|---|---|
| a | 742F (compare staff and casualty lists) | Secret Agent+ | rule 13 (player holds both lists) | - |
| b | 7430 (recover the CCTV tape) | all | the player holds the tape (tag 2, prop 39, renamed 745C..7460) | the tape is destroyed (rule 13 too) |
| c | 7431 (disable all security cameras) | Secret Agent+ | the six cameras tags 28-33 (CCTV props 87, 89, 91, 93, 95, 97) are destroyed | - |
| d | 7432 (recover the operations manual) | 00 Agent+ | the player holds the manual (tag 34, prop 109, in safe prop 101 behind door prop 102; renamed 7467..746B) | the manual is destroyed |
| e | 7433 (escape with Natalya) | all | Natalya has been freed (rule 6) **and** the player enters the room of pad 51 | Natalya dies (rule 14) |

Briefing pages: 742A title, 742B overview, 742C M, 742D Q, 742E Moneypenny.

## Rules

### In the cell
1. Natalya stands still. **When** the player is within 25 units of pad 95, **then** she walks to pad 95 and,
   on arrival, starts the next conversation that has not been held yet (rule 2, then rule 3). Once both are
   over she stays put until freed (rule 6).
2. First conversation: a top text every 5 s while the player stays in the cell (the room of pad 94):
   7409 (Bond line: opens the cell talk), 740A (she asks who he is), 740B (Bond introduces himself), 740C (she gives
   her name), 740D (Bond asks why she is locked up), 740E (accused of treason), 740F (Bond asks if true),
   7410 (Ourumov took the satellite), 7411 (Bond knows Ourumov), 7412 (Ourumov now a general). 7409 shows at
   0 s (on arrival at pad 95), each next line 5 s after the previous, so 7412 at 45 s; the conversation ends
   as 7412 shows (45 s), and the next step (rule 1: she stays put until the player is near pad 95 again)
   starts at once. **When** the
   player leaves the cell room, **then** the conversation ends at once and counts as held.
3. Second conversation, same pacing and the same early end: at 5 s 7413 (why is Bond here), 10 s 7414
   (Bond line), 15 s 7415 (that no longer matters), 20 s 7416 (Bond line: agrees), 25 s
   7417 (both will be shot), 30 s 7418 (Bond has a plan); it ends at 50 s. **When** it ends (either way),
   **then** the jailer starts coming to the cell (rule 5).
4. The lines are shown once each, for their own 1 s window; nothing is spoken by voice.

### The jailer (chr 0x12)
5. The jailer walks to pad 100 or pad 96 (50% each) and, on arrival, picks again. **When** he arrives and the
   second conversation is over and he has not yet done both exchanges, **then** he walks to pad 99 by the
   cell door and, on arrival (or when the player is within 10 units of pad 99), turns to face the player
   and plays the next exchange, then resumes his walk. Each exchange is three top texts while the player is
   in the cell room, 0 s / 5 s / 15 s, ending at 20 s (or when the player leaves the room):
   - first: 741A (Bond fakes sickness), 741B (jailer not fooled), 741C (Natalya line: a retort);
   - second: 741D (Bond insists, wants out), 741E (jailer line), 741F (Natalya line: a second retort).
   After the second exchange he only walks between pads 100 and 96.
6. **When** the cell door tag 3 (door prop 77, bound pad 101) is first opened, **then** Natalya is freed: the
   door becomes usable by characters (it was closed to them), Natalya's escape condition of objective e is
   met, and she follows the player (rule 8).
7. **When** the door tag 26 (door prop 75, bound pad 100) is first opened, **then** the jailer shows 7424
   (jailer line) and fights from where he stands: kneels, aims at the player and fires whenever
   he has a line of sight. **When** a shot of the player misses him, **then** he becomes a relentless hunter
   (global.md, global list 0x000D, Modes "relentless hunter"; it never ends); this ends his walking for good.

### Natalya follows
8. Freed Natalya follows the player: runs when farther than 100 units, walks when closer, re-planning when
   she stops; she halts within 25 units. While halted: **if** the player is within 8 units, **then** she
   says a line and gestures: 7421 (line: points to the next goal) with a short raise of the arms when the player
   is in front of her (within 90 degrees either side), else 7420 (you know this place) while looking about;
   **if** the player moves farther than 40 units, she follows again.
9. **When** she is within 250 units of pad 57 (the control room console), **then** she leaves the player:
   top text 7426 (checking the console), runs to pad 57 (walks the last 60 units), turns to the console
   (tag 35, monitor prop 215, bound pad 10027) and works it; this starts the countdown (rule 11).
10. At the console: she types (random typing animation each time the previous ends: 172 frames 0-69 39%,
    173 frames 0-74 20%, 174 frames 0-79 20%, 175 frames 0-89 21%); each typing bout has a 50% chance to
    play key sound 0x00B8 from prop 210 (tag 36) 1 s later. **When** she is off screen, **then** she is put
    back on pad 57. Top texts: 5 s 7427 (satellite activated), 10 s 7428 (it will fire on the base).
    **When** 15 s have passed, **or** the player is in the room of pad 25, **or** tag 35 or tag 36 is
    destroyed, **then** she says 7429 (we must escape), runs to pad 120 and waits there facing the player for
    the rest of the level (she no longer follows). If she is interrupted before reaching the console (player
    in the room of pad 25), the same ending happens and the countdown of rule 11 still starts.

### Countdown
11. **When** Natalya reaches the console (or rule 10 ends early), **then** over five frames the control room
    screens change (tag: screen bank) 5:16, 6:15, 7:3, 8:15, 9:3, 10:5, 11:15, 12:19, 13:5, 14:12, 15:8,
    16:13, 17:13, 18:14, 19:13, 20:2, 21:16, 22:13, 23:8, 24:17, 25:3, with sounds 0x00E3, 0x00C6, 0x00E3,
    0x00E3, 0x00C6 at bound pad 10071. Sound 0x00A2 starts silent and, 2 s later, swells to 32500 over 30 s;
    a 60 s HUD countdown starts and shows; the level's extra music track starts (slot 0).
12. **When** the countdown falls below 15 s, **then** all screens tags 5-25 switch to bank 46 (same five
    sounds). **When** it runs out, **then** it is hidden and explosions go off around the player without end
    (the player dies unless already out).

### Evidence and Natalya's life
13. **When** the player holds both the casualty list (tag 0) and the staff list (tag 1, prop 43), **then**
    objective a completes. **When** the tape (tag 2) is destroyed, **then** objective b fails.
14. **When** Natalya dies, **then** objective e fails, with top text 7422 and bottom text 7423 (Natalya dead).

### Alarm reinforcements (chr 0x23-0x27)
15. **When** the alarm has sounded (once is enough), **then** at once and every 30 s each of chr 0x23-0x27
    that is dead or gone is spawned (body 38, sunglasses) at pad 50 when out of view, unless the exit has
    begun. Outfit:
    - 20%: health 6.0, speed 30, accuracy 30, grenade chance 51/256, AK47 (item 8, model 184), hat 220;
    - 20%: health 4.0, armour 6.0, speed 30, accuracy 30, two Skorpions (item 7, model 193), hat 222;
    - 61%: health 6.0, armour 8.0, speed 50, accuracy 50, hat 221, two of item 6 (model 205).
    Then 50%: relentless hunter (global.md, global list 0x000D, Modes "relentless hunter"); else hunter: runs to the player's position, and when within 500
    units with a line of sight attacks with one random action (grenade 4%, sidestep 16%, firing roll 12%,
    walk-fire 8%, run-fire 20%, stand-fire 20%, kneel-fire 21%), repeating while it keeps the line of sight.

### Escape
16. **When** the player enters the room of pad 51 (and is alive), **then** the player becomes invulnerable
    and cannot pick up, the whole HUD is hidden, controls lock, the clock stops, and the screen fades to
    black. When done: **if** not every objective is complete, **then** the level ends at once (failed);
    **else** the exit cinematic plays.

## Objects and world
- Cell door tag 3 (prop 77): closed to characters until opened (rule 6). Door tag 26 (prop 75): rule 7.
  Door tag 4 (prop 72, bound pad 108): referenced only by its tag.
- Console tag 35 (monitor prop 215) and tag 36 (prop 210): destroying either ends rule 10.
- Control room screens tags 5-25 (monitors): rules 11, 12.
- Safe prop 101 (door prop 102, refusal text 7465) holds the manual and the two linked items props 104/106.
- Cameras tags 28-33: objective c.
- Sounds: 0x00B8 (rule 10), 0x00E3 / 0x00C6 / 0x00A2 (rule 11, 12). Countdown 60 s. Extra music in rule 11.

## Cinematics

### Intro (setup intro data)
Played as `intro.md` describes; the data below is in its units (world units, API degrees, seconds).

- **Fixed cameras:** 3 (one is picked at random); world units, API degrees, fov 60 (intro.md §1). Lines: 0x7466
  (location title); no second line; the shot lasts 5 s, then fades to black over 1 s.

  | Camera | Position (x, y, z) | Yaw | Pitch | Room of pad |
  |---|---|---|---|---|
  | 0 | (-485.07, 267.22, 5027.87) | 151.0 | -26.0 | 80 |
  | 1 | (-2518.69, 419.69, 1678.94) | 36.4 | -26.0 | 70 |
  | 2 | (-1812.15, 127.31, 3595.20) | -113.4 | 12.0 | 90 |

- **Swirl points** in order (intro.md §2): every offset in the player frame (flag 2: x left, y up, z forward, from
  the eye point); no point looks at the eye (no flag 4); the end marker (flag 1) follows the last row. Duration
  and tension belong to the leg that starts at the point. Swirl length 3.001 s (the legs from P1 on: P0 is only the previous control point, intro.md section 2).

  | Point | Offset (x, y, z) | Duration (s) | Tension |
  |---|---|---|---|
  | P0 | (0, -100, 200) | 0.667 | 0.75 |
  | P1 | (0, -100, 200) | 0.667 | 0.75 |
  | P2 | (-10, -100, 170) | 0.667 | 0.75 |
  | P3 | (-80, -20, 0) | 0.667 | 0.75 |
  | P4 | (-20, 40, -100) | 0.667 | 0.75 |
  | P5 | (0, 10, -30) | 0.333 | 0.25 |
  | P6 (final) | (0, 0, 0) | not used | not used |
  | P7 (shape only) | (0, 0, 0) | not used | not used |

- **Intro animation:** no record: table entry 0 (animation 61 from frame 95, speed 0.02; intro.md §3).
- **Body during the swirl:** the table pose only; no level script animates it.
- **Start (setup data):** pad 49; item 1 (fist), item 60 (watch magnet), 5 of ammo type 25; outfit (cuff) 3; watch
  6:45.

### Exit (rule 16, all objectives complete)
1. All characters hidden, weapons hidden; any button fades out and ends the level.
2. Camera: cutscene camera tag 27 (prop 343: (1331.45, 917.24, -2467.64), room of pad 119), kept on Bond.
3. Natalya is shown again; unless she is already in the room of pad 120, she is put on pad 50; she runs to
   pad 120 and faces Bond.
4. Bond's body is shown on pad 119 and plays animation 89 in full; fade in from black.
5. 4 s later fade to black; the level ends (won).

## Co-op notes
- Rules 1-5, the cell room and pad 95 tests: any player; the conversations stop only when no
  player is left in the cell room.
- Rules 5, 7, 8: the jailer faces / fights and Natalya follows the nearest living player.
- Rule 12: explosions around every living player.
- Rule 16: the lead decides whether the first player in the room of pad 51 triggers the exit.

## API needed
- existing: `objectives.complete(id)`, `objectives.fail(id)`, `objectives.all_complete()`
- existing: `has(item)`, `mission.difficulty()`, `wait(seconds)`, `on(name, fn)`, `spawn(fn)`,
  `event("alarm")`, event death (filter prop), event door (filter prop, state open)
- existing: `guard(prop):go_to(target, speed)`, `guard(prop):is_moving()`, `guard(prop):face(target)`,
  `guard(prop):stand()`, `target:distance()`, `target:distance_to(other)`, `guard(prop):was_shot_at()`,
  `guard(prop):is_dead()`, `guard(prop):follow(player)`, `guard(prop):stop()`
- new: `player_in_room(pad) -> boolean`; `chr_in_room(guard, pad) -> boolean`
- new: `player_has_object(prop) -> boolean` the player holds that exact object (a renamed prop, not an item id)
- new: `obj(prop):is_destroyed() -> boolean`
- new: `door(prop):allow_characters(on)` let characters operate a door
- new: `hud_text(text_id)`, `hud_text_bottom(text_id)`
- new: `guard:face_player()`, `guard:face_pad(pad)`
- new: `guard:play_anim(anim, from, to, hold, blend_ticks)` and `guard:stop_anim()`
- new: `guard:gesture(kind)` "raise_arms" (half a second), "look_around"
- new: `guard:bearing_to_player() -> degrees` (to tell front from back)
- new: `guard:is_on_screen() -> boolean`, `guard:teleport(pad)`
- new: `guard:kneel_aim()`, `guard:fire()`, `guard:has_line_of_sight()`, `guard:combat_action(kind)`,
  `guard:is_busy()`
- new: `guard:behave(name)` hand to a generic behaviour (global.md: "standard", "relentless hunter" = global list 0x000D)
- new: `spawn_guard{chr, body, head, pad, force_in_view, sunglasses} -> guard?`
- new: `guard:give_weapon(item, model, second_hand?)`, `guard:give_hat(model)`, `guard:set_armour(n)`,
  `guard:set_max_health(n)` (existing as set_max_health but here with the 007 modifier),
  `guard:set_speed(n)`, `guard:set_accuracy(n)`, `guard:set_grenade_chance(pct)`
- new: `guard:set_chr_flag(bits, on)` (Natalya's 0x80; showing a hidden character again in the exit)
- new: `tv(prop):show(bank, screen?)`
- new: `sound(id):play_from(prop)`, `sound(id):play_at(pad)`, `sound:set_volume(vol, seconds)`
- new: `countdown.show(seconds)`, `countdown.remaining()`, `countdown.hide()`
- new: `explosions_around_player()` endless explosions round the player
- new: `music.play_extra(slot, seconds, fade_seconds)`
- new: `cinema.begin()`, `player_set_invulnerable(on)`, `fade.to_black()`, `fade.from_black()`, `fade.wait()`
- new: `hide_all_characters()`, `player_hide_weapons()`, `camera.fixed(prop, look_at_player)`
- new: `bond_body:show()`, `bond_body:teleport(pad)`, `bond_body:play_anim(anim, from, to, hold, blend)`
- new: `mission.end_on_button()`, `mission.exit()`
