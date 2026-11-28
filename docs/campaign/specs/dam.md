# Dam

Setup file `UsetupdamZ.c`, text bank 11 (ids 0x2C00..). Bond crosses the Byelomorye dam, silences the alarms,
plants the covert modem on the satellite link, triggers the data backup from the mainframes, and bungee-jumps
off the platform at the far end.

## Characters
| chr | Who | Difficulties | Starts as | Notes |
|---|---|---|---|---|
| 0x00 | tower guard, pad 1 | all | rule 13 (static sniper) | body 37, health 4.0, armour 2.0, no blood |
| 0x01 | tower guard, pad 2 | all | rule 13 | as 0x00 |
| 0x02 | guard at pad 7 | all | rule 15 (post at pad 4) | body 37 |
| 0x03 | gate guard at pad 6 | all | rule 14 | body 37 |
| 0x04..0x07 | guards at pads 8, 9, 10, 11 | all | standard guard generic guard (global.md) | body 37; pad preset 8, 9, 10, 11 |
| 0x0a, 0x0b, 0x0c, 0x16, 0x1c | guards at pads 16, 17, 18, 43, 49 | all | standard guard generic guard (global.md) | body 2; clone on gunfire |
| 0x0e..0x15, 0x17..0x1b | guards at pads 35..42, 44..48 | all | rule 12 (stand and shoot) | body 2, health 4.0, armour 2.0 |
| 0x27 | guard at pad 132 | all | patrols path 0 | then behaves as the simple guard generic guard (global.md) |
| 0x28 | guard at pad 158 | all | patrols path 1 | then simple guard (global.md) |
| 0x29 | guard at pad 191 | all | patrols path 2 | then simple guard (global.md) |
| 0x2a | guard at pad 252 | all | patrols path 3 | then simple guard (global.md) |
| 0x2b | guard at pad 221 | all | patrols path 4 | then simple guard (global.md) |
| 0x2c | guard at pad 302 | all | patrols path 5 | then simple guard (global.md) |
| 0x2d | guard at pad 171 | all | patrols path 6 | body 2; then simple guard (global.md) |
| 0x2e | guard at pad 31 | all | simple guard alarm raiser generic guard (global.md) | body 2; runs for the alarm at bound pad 10074 |
| 0x2f | guard at pad 21 | all | rule 16 (idle until alerted) | body 2, health 4.0, armour 2.0 |
| 0x0d | guard at pad 19 | all | rule 16 | body 18, health 4.0, armour 2.0 |

All guards: random head, hearing 1000, vision 100.
Alarm reinforcements (rule 11): two spawned guards, body 37, random head, health 4.0, armour 2.0, with an AK-47
(item 8).

## Objectives
| Letter | Text | Difficulties | Completed when | Failed when |
|---|---|---|---|---|
| a | 0x2C16 (neutralise every alarm) | Secret Agent and up | alarm boxes tag 0, 1, 2, 3 all destroyed | - |
| b | 0x2C17 (install the covert modem) | 00 Agent, 007 | rule 4 | rule 5 or rule 6 |
| c | 0x2C18 (intercept the data backup) | 00 Agent, 007 | rule 9 | rules 6, 8, 10 |
| d | 0x2C19 (bungee jump from platform) | all | rule 17 | - |

A failed state wins over a completed one (b can still fail after the modem is in, rule 6).
Briefing pages: title 0x2C11, overview 0x2C12, M 0x2C13, Q 0x2C14, Moneypenny 0x2C15.

## Rules

### Covert modem and satellite link (all difficulties; objectives b and c only count on 00 Agent and up)
1. The player starts with the covert modem (item 47, a throwable gadget; its names 0x2C1C..0x2C20).
2. The satellite link is the screen tag 5 (bound pad 10057) and its dish tag 4 (bound pad 10058).
3. These rules run until the backup completes (rule 9); after that nothing here applies.
4. **When** the modem lands stuck on tag 5, **if** the modem is not already installed or misplaced, **then**
   show 0x2C08 (modem installed) at the top, play sound 0xE3 from tag 5; objective b completes.
5. **When** the modem comes to rest anywhere else in the level (not on tag 5), **if** it is not installed yet,
   **then** show 0x2C21 (modem placed wrongly) at the top; objective b fails and the modem can no longer be
   installed.
6. **When** tag 4 or tag 5 is destroyed before the backup completed, **then** show 0x2C0E (satellite link
   destroyed) at the top; objectives b and c fail.

### Data backup at the mainframes (tags 6 and 7, pads 100 and 101)
7. **When** the player uses a mainframe (tag 6 or 7) **before** the modem is installed, **then** show 0x2C0D
   (modem missing) at the top; nothing else (the player may use it again later).
8. **When** both mainframes are destroyed before the backup started, **then** show 0x2C0C (computers damaged)
   at the top; objective c fails and the backup can no longer start.
9. **When** the player uses a mainframe after the modem is installed, **if** objective c has not failed, **then**
   show 0x2C09 (backup started) at the top and a 10 s countdown on the HUD; a beep (sound 0xBD, from tag 6)
   plays every second. **When** the countdown reaches its last second, **then** hide the countdown, show
   0x2C0A (backup done) at the top; objective c completes. This happens once.
10. **When** both mainframes are destroyed during the countdown, **then** hide the countdown, show 0x2C0B
   (backup failed) at the top; objective c fails.

### Alarm
11. **When** the alarm goes off (the first time), **if** the bungee exit has not started, **then** two
   reinforcements spawn at pad 116 (only if a player cannot see the pad; a spawn that fails is not retried),
   one 1 tick after the other. Each runs to the player, then behaves as the standard guard generic guard
   (global.md).

### Guards with level behaviour
12. **Stand and shoot** (chr 0x0e..0x15, 0x17..0x1b; and chr 0x03 after rule 14): the guard stands where it
   is. **When** a player's shot just missed it, it sees another guard shot or killed, it sees the player, or it
   has line of sight to a player, **then** it picks: 10% run to the player and then behave as the standard
   guard generic guard (global.md) for good; 47% fire kneeling; 43% fire standing. After a shot it stands
   again and re-tests at once (so it keeps firing while it sees the player). It never walks.
13. **Tower guards** (chr 0x00, 0x01): as rule 12 but with no run: 22% throw a grenade, 78% fire standing.
14. **Gate guard** (chr 0x03): 2 s after the start, plays animation 172 (frames 0..69, working the gate
   control), then gate A (door tag 8) closes; the guard walks to pad 5, turns to face the player, then follows
   rule 12.
15. **Post guard** (chr 0x02): walks to pad 4 (its post) at the start, then:
   - it stands and watches; **when** it has line of sight to a player, a player's shot just missed it, or it
     sees another guard shot, **then** it fights with moves picked at random: 20% sidestep, 20% fire while
     walking, 20% fire while running, 20% fire standing, 22% fire kneeling; after each move it picks another
     while it still sees the player;
   - **when** it loses sight of the player: **if** it is within 2000 units of pad 4, **then** it watches again;
     **else** (98%) it chases: 22% of the time it just watches again, else it runs toward the player's position
     for at most 2 s and watches again; the remaining 2% it runs back to pad 4;
   - **when** it has chased and is still watching 5 s after that chase began, **then** within about a second it
     runs back to pad 4, makes one more run toward the player of at most 2 s, and watches again (no longer
     returning home until it chases again).
16. **Alert-only guards** (chr 0x2f, 0x0d): stand idle. **When** they hear the player fire, see another guard
   shot, have line of sight to a player, or the alarm is on, **then** they run to the player and then behave
   as the standard guard generic guard (global.md).

### Bungee exit
17. **When** a living player enters the room of pad 330 (the platform's edge), **then** objective d completes;
   the player becomes invulnerable and cannot pick anything up; the HUD hides, the controls lock, the mission
   clock stops; the player is forced to unarmed (item 0, the first value of the item enum; the fist is 1) and pushed forward at 4 units per tick along +Z (off
   the edge). **When** the player falls below Y = -17000 or 5.8 s have passed, **then** fade to black, and:
   - **if** not all objectives of the difficulty are complete, the level ends at once (a failed mission);
   - **else** the outro plays (Cinematics) and the level ends as a win.
   If the player dies before reaching the room, this rule never fires.

## Objects and world
- Gates A (door tag 8, bound pad 6, starts open) and B (door tag 9, bound pad 9, starts closed) form an
  airlock. **When** the player uses gate A while it is opening and gate B is not closed, **then** show 0x2C10
  (other gate closing, wait) at the bottom; **when** the player uses gate B while it is opening and gate A is
  not closed, **then** show 0x2C0F (other gate closing, wait) at the bottom.
- Gate status screens: monitors tag 15 and 16 show gate A, tag 17 and 18 show gate B, two screens each
  (screen 0, screen 1). On each change of the gate's state they switch image bank:

  | Gate state | screen 0 bank | screen 1 bank |
  |---|---|---|
  | open | 0x30 | 0x32 |
  | closing | 0x2E | 0x30 |
  | closed | 0x31 | 0x30 |
  | opening | 0x30 | 0x2F |

  At the start, tags 15/16 show "open" and tags 17/18 show "closed".
- Alarm boxes tags 0, 1, 2, 3 (bound pads 10070, 10071, 10072, 10074): objective a.
- Truck (setup prop 321, a vehicle, model 279 at pad 317): drives patrol path 7 for the whole level, top speed 512 (1000 = 1 m/s),
  reaching it in 2 s.
- Body armour at pad 113 (Agent only) and pad 114 (Agent and Secret Agent): setup data.
- Cutscene cameras tag 10, 11, 12 (all in the room of pad 331), used by the outro.
- No fog change, no level-wide music change except the outro.

## Cinematics

### Intro (setup intro data)
Played as `intro.md` describes; the data below is in its units (world units, API degrees, seconds).

- **Fixed cameras:** 6 (one is picked at random); world units, API degrees, fov 60 (intro.md §1). Lines: 0x2C1A
  (location title), then 0x2C1B (time caption); the shot lasts 8 s, then fades to black over 1 s.

  | Camera | Position (x, y, z) | Yaw | Pitch | Room of pad |
  |---|---|---|---|---|
  | 0 | (17608.08, -59.79, 17408.57) | 162.4 | 26.0 | 312 |
  | 1 | (16740.06, 1261.11, 20253.95) | 170.1 | -28.0 | 298 |
  | 2 | (8947.16, 2920.27, 4098.50) | -146.6 | -22.0 | 269 |
  | 3 | (16872.39, 60.30, 9502.19) | 155.1 | 6.0 | 6 |
  | 4 | (5248.06, 480.29, -8941.07) | -41.2 | -12.0 | 69 |
  | 5 | (7424.92, 1160.29, 3484.91) | -143.2 | -12.0 | 273 |

- **Swirl points** in order (intro.md §2): every offset in the player frame (flag 2: x left, y up, z forward, from
  the eye point); no point looks at the eye (no flag 4); the end marker (flag 1) follows the last row. Duration
  and tension belong to the leg that starts at the point. Swirl length 9.89 s (the legs from P1 on: P0 is only the previous control point, intro.md section 2).

  | Point | Offset (x, y, z) | Duration (s) | Tension | Room of pad |
  |---|---|---|---|---|
  | P0 | (2812, 930, 5168) | 1.667 | 0.5 | 287 |
  | P1 | (2812, 930, 5168) | 2.199 | 0.5 | 287 |
  | P2 | (2800, 450, 3852) | 1.667 | 0.5 | 287 |
  | P3 | (2264, 418, 3283) | 1.667 | 0.5 | 311 |
  | P4 | (1196, 176, 2448) | 1.667 | 0.5 | - |
  | P5 | (-52, -48, 1551) | 1.422 | 0.51 | - |
  | P6 | (-100, -6, 12) | 0.518 | 0.175 | - |
  | P7 | (0, 14, -107) | 0.75 | 0.5 | - |
  | P8 (final) | (0, 0, 0) | not used | not used | - |
  | P9 (shape only) | (0, 0, 0) | not used | not used | - |

- **Intro animation:** no record: table entry 0 (animation 61 from frame 95, speed 0.02; intro.md §3).
- **Body during the swirl (a level script, not the intro-animation record):** the body shows the entry 0 pose for
  the first 6 s of the swirl; then it plays animation 99 from its start to frame 96 and holds that frame (blend 16
  ticks) until it fades out.
- **Start (setup data):** pad 33; item 5 (silenced PP7), 100 of ammo type 1, item 47 (covert modem), 1 of ammo
  type 20; outfit (cuff) 3; watch 11:30. Other start slots (not used in solo play): slot 1 pad 140, item 8 (KF7),
  100 of ammo type 3, item 47 (covert modem), 1 of ammo type 20; slot 2 pad 270, item 17 (sniper rifle), 100 of
  ammo type 3.

### Outro (rule 17, all objectives done)
1. Behind the black screen: all characters hidden, the weapon put away; from here any button press fades out
   and ends the level. Bond's third-person model is placed on pad 331, held in the air (no gravity) and casts
   no shadow.
2. Shot 1, camera tag 10 at (148, 165, -3016), always aimed at Bond: fade in, extra music slot 0 plays; Bond
   plays animation 177 from its start to frame 3, held in place for 1 s, then frames 3..90 for about 1.7 s.
   Fade to black.
3. Shot 2, camera tag 11 at (-1161, 683, -2643), aimed at Bond: Bond back on pad 331, fade in, animation 177
   from frame 80 to its end, held in the air, for 5 s. Fade to black.
4. Shot 3, camera tag 12 at (-51, -1077, -1682), aimed at Bond: Bond back on pad 331, fade in, animation 177
   from frame 80 to its end, held in the air, for 7.5 s. Fade to black; the level ends as a win.

## Co-op notes
- Rule 4/5: the modem of any player counts; one install completes b for both.
- Rules 7 and 9: any player using a mainframe.
- Rule 15 (within 2000 units of the post) and the chase target: the nearest living player.
- Rule 17: the first living player to enter the room of pad 330 triggers the jump; decide whether the other
  player is carried along (the original has one player: lock and push every player, or teleport the second
  player to the first). The outro shows the triggering player's model.

## API needed
- existing: `objectives.complete(id)`, `objectives.fail(id)`, `objectives.is_failed(id)`,
  `objectives.all_complete()`
- existing: `mission.win()`, `mission.fail()`, `wait(seconds)`, `spawn(fn)`, `on(name, fn)`, `event("alarm")`
- existing: `chr(n)`, `tag(n)`, `text(id)`, `door(prop)`, `target:close()`, `target:was_used()`,
  `target:sees_player()`, `target:was_shot_at()`, `target:go_to(target, speed)`, `target:is_moving()`,
  `target:face(target)`, `target:distance_to(other)`, `target:set_health(n)`
- new: `item_landed(item) -> "on" tag | "level" | nil` where a thrown item came to rest: stuck on a tagged
  object (its tag) or loose in the level
- new: `tag(n):is_destroyed() -> boolean`
- new: `door(prop):state() -> "open" | "closed" | "opening" | "closing"` and event `"door"` existing (state)
- new: `tv(tag):show(screen, bank)` set the image bank of one screen of a monitor
- new: `message_top(text)`; existing `message(text)` for the bottom line
- new: `hud.countdown(seconds)`, `hud.countdown_left() -> seconds`, `hud.countdown_hide()`
- new: `sound.play(id, from_tag?)` a sound, positioned at a tagged object when given
- new: `alarm.is_on() -> boolean`
- new: `chr(n):spawn_at(pad, {body, head, force})` (spawn a guard; without `force` only out of view)
- new: `guard:set_armour(n)`, `guard:give_weapon(item)`, `guard:use_generic(name)`, `guard:patrol(path)`
- new: `guard:attack_move(kind) -> boolean` ("grenade", "sidestep", "fire_walk", "fire_run", "fire",
  "fire_kneel"), `guard:run_to_player()`
- new: `guard:heard_gunfire() -> boolean`, `guard:saw_guard_shot() -> boolean`, `guard:saw_guard_die() -> boolean`,
  `guard:in_line_of_sight() -> boolean`
- new: `guard:play_anim_id(anim_id, from_frame, to_frame)` play a character animation by id over a frame range
- new: `guard:set_no_blood()`
- new: `players_in_room(pad) -> {player}`
- new: `player(n):set_invulnerable(on)`, `player(n):force_weapon(item)`, `player(n):push(dx, dz)` move the
  locked player by (dx, dz) units per tick, `player(n):pos()` existing
- new: `vehicle(prop):drive(path, top_speed, ramp_s)` a vehicle follows a patrol path in a loop
- new: `scene.lock_player(on)`, `scene.fade_out()`, `scene.fade_in()`, `scene.hide_weapon()`,
  `scene.hide_all_chrs()`, `scene.skip_on_button()`
- new: `camera.fixed(tag, {look_at_bond})` a cutscene camera record, optionally kept aimed at Bond's model
- new: `music.play_extra(slot, stop_s, total_s)`
- new: event `"intro_swirl"`
- new: `bond_model:place(pad)`, `bond_model:play_anim(anim_id, from_frame, to_frame, hold)`,
  `bond_model:hold_in_air(on)` (no gravity), `bond_model:no_shadow()`
