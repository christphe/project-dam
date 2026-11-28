# Caverns

Setup file `UsetupcaveZ`, text bank 7 (ids 0x1C00..). The water caverns under the Janus base: destroy the pump
controls and the master console, radio Jack Wade, spare the scientists, and follow Trevelyan, who runs ahead
shutting doors behind him, taunts Bond from a ledge and slips away, to the exit lift.

## Characters
| chr | Who | Difficulties | Starts as | Notes |
|---|---|---|---|---|
| 0x00-0x02, 0x05-0x08, 0x0A-0x0D, 0x0F, 0x12, 0x14-0x17, 0x19-0x1C, 0x29, 0x2A, 0x3B, 0x3D, 0x3E | Janus guards | all | each on the pad of its own number | generic guard (global.md, standard) |
| 0x3F-0x43 | Janus guards | all | pads 65, 66, 67, 68, 69 | generic guard (global.md, standard) |
| 0x47 | Janus guard | all | pad 64 | generic guard (global.md, standard); grenade probability 229 |
| 0x10 | Janus guard | all | pad 228, patrols path 4 | then behaves as the simple generic guard (global.md) |
| 0x11 | Janus guard | all | pad 252, patrols path 5 | then behaves as the simple generic guard (global.md) |
| 0x45 | Janus guard | all | pad 336, patrols path 6 | then behaves as the simple generic guard (global.md) |
| 0x28, 0x35-0x38 | Janus guards | all | each on the pad of its own number | deaf simple generic guard (global.md); 0x36 has grenade probability 25 |
| 0x03, 0x04, 0x09, 0x0E, 0x13, 0x18, 0x1D, 0x27, 0x3C | armoured Janus guards (hat model 221) | all | each on the pad of its own number | rule 3; 0x3C carries code card A (key prop 468), 0x27 code card C (key prop 472); grenade probability 25 for 0x1D and 0x27 |
| 0x48 | armoured Janus guard (hat model 221) | all | pad 75 | rule 3; carries code card B (key prop 470) |
| 0x1E-0x26, 0x2B-0x2F, 0x31-0x34, 0x3A | Janus guards | all | each on the pad of its own number | rule 2; grenade probability 25 for 0x22, 0x2C, 0x31 |
| 0x44 | Janus guard | all | pad 73 | rule 2 |
| 0x30, 0x39 | grenadiers | all | each on the pad of its own number | rule 4; grenade probability 229 |
| 0x46 | grenadier | all | pad 63 | rule 4; grenade probability 229 |
| 0x58 | Trevelyan (body 8) | all | pad 190, waits | rules 6-10; invincible (setup flag) and health 2481.0; cannot be auto-aimed; grenade probability 255; carries collectable record 380 (item 0x10, model 207) |
| 0x49, 0x4A, 0x4B, 0x54, 0x55 | scientists (body 35) | all | 0x49 pad 70, 0x4A pad 71, 0x4B pad 72, 0x54 pad 77, 0x55 pad 78 | rules 11-15; their deaths count as civilian kills (rule 16) |
| 0x59, 0x5A, 0x5B | reinforcements (body 4) | all | not present at start | rules 17-18 |
| player | Bond | all | intro spawn pad 368 | outfit (cuff) 2; starts with item 0x09, the Walther PPK (item 4) and item 0x1B (timed mines); ammo type 1: 100 + 100, ammo type 9: 6; watch shows 3:48 |

All guards have hearing 1.0 and vision 100; body 4 unless stated, random heads.

## Objectives
| Letter | Text | Difficulties | Completed when | Failed when |
|---|---|---|---|---|
| a | 0x1C13 (destroy inlet pump controls) | Secret Agent and up | the objects of tags 0, 1, 2 and 3 are destroyed | never |
| b | 0x1C14 (destroy outlet pump controls) | Secret Agent and up | the objects of tags 4, 5, 6 and 7 are destroyed | never |
| c | 0x1C15 (destroy master control console) | Secret Agent and up | the object of tag 8 is destroyed | never |
| d | 0x1C16 (radio Jack Wade) | 00 Agent and up | rule 19 (the radio was used) | any of the radio parts, tags 9, 10, 11, 12, is destroyed (rule 20) |
| e | 0x1C18 (minimize scientist casualties) | all | the player reaches the exit (rule 21) | more than 2 civilians are killed (rule 16) |

Briefing pages: watch-menu texts 0x1C0E..0x1C12 (title, overview, M, Q, Moneypenny). Text 0x1C17 (pursue
Trevelyan) is not used by any objective.

## Rules

### Guards
1. Guards on the standard / simple / deaf generic behaviours (Characters table) need no level rule.
2. **Waiting guards.** Until something alerts it, the guard stands still. **When** it sees another guard shot or
   killed, a player's shot lands near it, it has a line of sight to the player, or the player aims at it
   (checked with a 20% chance each time), **then** it runs to the player and behaves as the run-to-Bond
   generic guard (global.md), falling back to the standard generic guard afterwards.
3. **Armoured guards.** Body armour 8.0. As rule 2, and also alerted when it hears the player; **then** it
   behaves as the relentless hunter (global.md, global list 0x000D) for the rest of the level.
4. **Grenadiers** (they never move). **When** a player's shot lands near one, it sees another guard shot or
   killed, it sees the player or has a line of sight to him, **then** it attacks from where it stands, picking
   each time: 59% fire standing, 39% fire kneeling, 2% throw a grenade **if** the player is between 800 and
   3000 units away (else it fires kneeling). It goes on as long as it keeps the player in sight.

### Trevelyan
5. Trevelyan is unkillable in this level (invincible, very high health).
6. **When** Trevelyan first has a line of sight to the player, **then** he starts his run and never waits again:
   he sprints from room to room, first to the room of pad 193. Each time he reaches the room he was heading
   for, he closes the two doors he has just passed and heads on (if he stops short of a room, he sprints on):

   | Reaching the room of | Doors he closes (tags) | Next he heads for |
   |---|---|---|
   | pad 193 | 25, 16 | pad 211 |
   | pad 211 | 26, 17 | pad 222 |
   | pad 222 | 27, 18 | pad 263 |
   | pad 263 | 28, 19 | pad 272 |
   | pad 272 | 29, 20 | pad 345 |
   | pad 345 | 30, 21 | pad 359 |
   | pad 359 | 32, 23, 36, 37 | pad 361 |
   | pad 361 | none | runs to pad 360 (rule 7) |

7. **When** he reaches the room of pad 361, **then** he runs to pad 360 (the ledge); once within 500 units of it
   (or stopped), he waits until he has a line of sight to the player, then: top text 0x1C0A (Trevelyan mocks
   Bond's slowness) and the reinforcements start (rule 17).
8. **Then** he attacks from the ledge: 79% fire kneeling, 21% throw a grenade (a failed shot becomes a grenade),
   one attack at a time.
9. **When** an attack ends, **if** the player is within 1000 units of him or the drone gun of tag 34 still
   exists (is not destroyed), **then** the doors of tags 33 and 24 are opened (and are shut again at once by
   rule 10). In every case he then leaves (rule 10). (He makes only one attack from the ledge.)
10. **When** he leaves, **then** he closes the doors of tags 33 and 24, runs to pad 367, and on arriving (within
    100 units, or stopped) he fades out and is removed.

### Scientists
11. **Calm.** A scientist stands still. **When** a player's shot lands near him or he sees a guard shot, **then**
    he cowers (rule 13). **When** he sees a guard die, **then** with 25% chance (re-rolled each time it is
    checked) he flees (rule 14). **When** he hears the player, or was shot or saw or heard the player in the last
    10 s, **then** with 75% chance he runs a short way to the side. **When** he stops moving after having been
    hit, **then** as when he sees a guard die.
12. **Calm, the player in view.** **When** he sees the player (or has a line of sight to him with the player in
    front of him, within 90 degrees either side, or the player is within 100 units), **then** with 22% chance, or
    always when the player aims at him: **if** the player is within 200 units, he cowers (rule 13); **else if**
    he has been hit, as when he sees a guard die; **else** he dodges (rule 15).
13. **Cowering.** He puts his hands up and stays put while the player is within 2000 units and aims at him or
    has a line of sight to him. **When** he has been hit more than once, **then** he draws a pistol and attacks:
    then behaves as the draw-pistol-and-attack generic guard (global.md). **When** the player is beyond 2000
    units or out of sight, a shot lands near him, or by chance (0.8% per tick, about 40% per second), **then** he
    flees (rule 14) and afterwards cowers again.
14. **Fleeing.** He drops whatever he carries and runs to pad 368. **When** a shot lands near him on the way,
    **then** he raises his hands for a moment (until the gesture ends, he sees a guard die, or 14% per tick) and
    runs on. **When** he arrives (within 500 units of pad 368) and no player can see him, **then** he fades out
    and is removed (he got away; it does not count as a kill). **When** he arrives seen, or stops elsewhere,
    **then** he goes back to what he was doing before fleeing (rule 11 or rule 13).
15. **Dodging.** He waits until he has stopped, then sidesteps (41%) or runs to the side (59%); **while** he can
    still see the player and the player is not behind him (more than about 155 degrees from where he faces, and
    farther than 100 units), he dodges again; **else** he goes back to rule 11.
16. **When** the level starts, the five scientists are marked so that killing them counts as killing civilians.
    **When** more than 2 civilians have been killed, **then** top text 0x1C0D (unacceptable civilian
    casualties: failure) and objective e fails.

### Reinforcements
17. **When** Trevelyan taunts from the ledge (rule 7), **then** the doors of tags 35, 36 and 37 open, and at once
    and every 40 s after that, **if** the player is not in a cinematic, each missing reinforcement (never spawned,
    or killed and faded) is placed: chr 0x59 at pad 358, chr 0x5A at pad 292, chr 0x5B at pad 307 (body 4,
    random head, sunglasses).
18. **When** a reinforcement appears, **then** it gets a kit: **if** the mission clock is under 120 s, or with 21%
    chance, health 6.0, hat model 0xDC, item 0x10 (model 0xCF); **else** 39% health 12.0, hat model 0xDD, item 0x0E
    (model 0xC5) and with 39% chance a second one in the other hand; **else** health 8.0, hat model 0xDE, item 0x09
    (model 0xC3) and with 39% chance a second one. **Then** it runs to the player and behaves as the run-to-Bond
    generic guard (global.md), then the standard generic guard.

### Radio and exit
19. **When** the player uses any of the radio parts (tags 9, 10, 11, 12) while none of them is destroyed, **then**
    sound 0x50 plays on channel 0 from the object of tag 9; 1 s later top text 0x1C0C (Jack Wade is coming) and
    objective d completes.
20. **When** any radio part (tags 9..12) is destroyed before the radio was used, **then** objective d fails and top
    text 0x1C0B (radio damaged: no contact). After either rule 19 or 20 the radio does nothing more.
21. **When** the player enters the room of pad 369 (the exit lift), **if** he is alive, **then** objective e
    completes (unless already failed), he becomes invulnerable and cannot pick up, the HUD is hidden, controls
    locked, the mission clock stops, and the screen fades to black. **If** not every objective of the difficulty
    is complete, the level ends there (the game's normal end-of-level, a failure). **Else** the outro plays
    (Cinematics).

## Objects and world
- Pump controls: inlet tags 0..3 (multi-monitors, bound pads 10000..10003), outlet tags 4..7 (bound pads
  10061..10064), master console tag 8 (bound pad 10060). Their destruction drives objectives a-c natively.
- Radio: tags 9, 10 (single monitors, bound pads 10067, 10068), 11, 12 (props at bound pads 10069, 10070), usable
  (rule 19) and destructible (rule 20).
- Doors closed by Trevelyan: tags 16..30, 32, 33 (props 248..282) and 23, 24, 36, 37 (rule 6, 9, 10). Tags 31
  and 22 are never touched. Doors 35..37 (props 290..294) open for the reinforcements (rule 17).
- Exit lift doors: tags 38, 39 (props 285, 286); opened instantly at the outro, door 38 closed by it.
- Drone guns: props 166 (pad 161), 167 (pad 187), tag 34 (prop 169, pad 188); tag 34 matters in rule 9.
- Music: x-track slot 1 plays while the player is in the room of pad 368 (the start room), from the end of the
  intro swirl: it starts each time he enters the room and stops each time he leaves it. X-track slot 0 starts in
  the outro (up to 255 s).
- Sound 0x50 from the radio (rule 19). No fog or alarm changes.

## Cinematics
**Intro** (setup intro data). Played as `intro.md` describes; the data below is in its units (world units, API
degrees, seconds).

- **Fixed cameras:** 4 (one is picked at random); world units, API degrees, fov 60 (intro.md §1). Lines: 0x1C19
  (location title); no second line; the shot lasts 5 s, then fades to black over 1 s.

  | Camera | Position (x, y, z) | Yaw | Pitch | Room of pad |
  |---|---|---|---|---|
  | 0 | (852.71, -3343.55, 689.46) | 55.3 | 38.0 | 224 |
  | 1 | (920.50, 976.44, 737.37) | 57.5 | -46.0 | 224 |
  | 2 | (-7733.62, 718.16, -3006.01) | 31.4 | -20.0 | 303 |
  | 3 | (2121.75, -2774.07, 364.46) | -22.0 | 42.0 | 224 |

- **Swirl points** in order (intro.md §2): every offset in the player frame (flag 2: x left, y up, z forward, from
  the eye point); no point looks at the eye (no flag 4); the end marker (flag 1) follows the last row. Duration
  and tension belong to the leg that starts at the point. Swirl length 4.68 s (the legs from P1 on: P0 is only the previous control point, intro.md section 2).

  | Point | Offset (x, y, z) | Duration (s) | Tension |
  |---|---|---|---|
  | P0 | (-101, -159, 122) | 1.167 | 0.557 |
  | P1 | (-101, -159, 124) | 1.167 | 0.557 |
  | P2 | (72, -142, 119) | 1.167 | 0.557 |
  | P3 | (129, -79, 0) | 1.167 | 0.557 |
  | P4 | (23, 14, -74) | 0.846 | 0.495 |
  | P5 | (0.562, 10, -30) | 0.333 | 0.25 |
  | P6 (final) | (0, 0, 0) | not used | not used |
  | P7 (shape only) | (0, 0, 0) | not used | not used |

- **Intro animation:** no record: table entry 0 (animation 61 from frame 95, speed 0.02; intro.md §3).
- **Body during the swirl (a level script, not the intro-animation record):** from the swirl start, animation 98
  frames 0 to 1, and from the next tick frame 1 to its end, held at the last frame (blend 16 ticks).
- **Start (setup data):** pad 368; item 9 (ZMG), item 4 (PP7), item 27 (timed mine), 100 of ammo type 1, 100 of
  ammo type 1, 6 of ammo type 9; outfit (cuff) 2; watch 3:48.

**Outro** (rule 21, all objectives complete): after the fade to black, all characters are hidden, any button
press from now on fades out and ends the level, the exit doors (tags 38, 39) open instantly. **If** the player
holds none of items 2-22 (knife, throwing knife, PP7 through Moonraker laser) or 24-29 (grenade launcher, rocket
launcher, hand grenade, the three mines) in his right hand, that is unarmed (0), fist (1), the watch laser (23)
or any item from 30 up, Bond is shown holding the Walther PPK
(item 4) for the scene. Bond's weapon view is hidden and the view cuts to the cutscene camera of tag 40 (prop
477: (1935.24, 658.93, -3595.42), yaw 4.190, pitch 5.774, in the room of pad 369). Bond's third-person body:
1. is placed on pad 369 with his left hand emptied; the screen fades in and x-track slot 0 starts;
2. 1 s after the start he plays animation 0x65 frames 8..109 (blend 0.27 s);
3. at 1.9 s his right hand is emptied;
4. at 3.9 s door tag 38 closes; when it is fully closed, fade to black and the level ends (success).

## Co-op notes
- Rules 2-4, 11-15 (seeing, aiming, nearness): any player.
- Rule 6 starts on the first player Trevelyan sees; rule 9's distance uses the nearest player.
- Rule 14's vanishing needs no player to see the scientist.
- Rule 21: the exit should wait for both players (or the first to arrive ends it for both); the outro shows one
  Bond body.
- Rule 19: any player using the radio.
- Music in the start room (Objects): per player is not possible; play it while any player is in that room.

## API needed
- existing: `chr(n)`, `tag(n)`, `wait(seconds)`, `on(name, fn, filter)`, `spawn(fn)`, `clock()`,
  `objectives.complete/fail/all_complete`, `mission.win/fail`, `Target:open`, `Target:close`, `Target:is_open`,
  `Target:distance`, `Target:sees_player`, `Target:was_shot_at`, `Target:hits`, `Target:make_usable`,
  `Target:was_used`, `Target:surrender`, `Target:pos`, `event("death")`, `text(id)`.
- new: `chr(n):exists()`; `spawn_guard{chr, body, head, pad, sunglasses}` (false when it fails).
- new: `chr(n):set_stats{health, armour}`; `chr(n):give_weapon(item, model, hand)`; `chr(n):give_hat(model)`.
- new: `chr(n):set_ai_global(name, return_to)` hand a character to a generic behaviour of global.md
  ("run_to_bond" | "standard" | "relentless_hunter" (0x000D) | "draw_pistol_attack" | "simple" | "simple_deaf"), with the
  behaviour it falls back to.
- new: `chr(n):start_patrol(path)`.
- new: events `chr_saw_guard_shot {chr}`, `chr_saw_guard_die {chr}`, `chr_heard_player {chr}`,
  `chr_near_miss {chr}`; queries `chr(n):has_los_to_player()`, `chr(n):player_aims_at()`,
  `chr(n):alerted_recently(seconds)` (shot, saw or heard the player), `chr(n):angle_to_player()` (degrees from
  where it faces).
- new: `chr(n):run_to(pad, speed)`, `chr(n):is_moving()`, `chr(n):in_room_of(pad)`, `player(n):in_room_of(pad)`.
- new: `chr(n):fire_at_player(stance)` "stand" | "kneel"; `chr(n):throw_grenade()`; `chr(n):sidestep()`;
  `chr(n):run_to_side()`; `chr(n):drop_carried()`; `chr(n):fade_out()`.
- new: `chr(n):set_civilian(on)` its death counts as a civilian kill; `mission.civilians_killed()`.
- new: `say_top(text_id)`.
- new: `object(tag):is_destroyed()` / event `destroyed {tag}`; `object(tag):open_instant()` for doors.
- new: `sound:play(sound_id, channel)`, `sound:emit_from(channel, tag)`.
- new: `music:xtrack_play(slot, stop_after, total)`, `music:xtrack_stop(slot)`.
- new: `player(n):in_cinematic()` (damage and pickups disabled).
- new: `player(n):held_item()`, `cinema:show_with_weapon(item)` the weapon Bond's body holds in the scene.
- new: `cinema:begin(opts)`, `cinema:hide_all_chrs()`, `cinema:exit_on_button()`, `camera:cut_to(tag)`,
  `screen:fade_in()`, `screen:fade_out()`, `screen:wait_fade()`, `player_body(n)` with `teleport`, `play_anim`,
  `empty_hand(hand)`.
- new: `cinema:in_intro_swirl()` / event `intro_swirl`.
- new: `mission.exit()` end the level now and let the objectives decide.
- new: `random(percent)`.
