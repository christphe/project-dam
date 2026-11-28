# Silo

Setup file `UsetupsiloZ`, text bank 34 (ids 0x8800..). Bond infiltrates the Kirghizstan missile silo: plants
charges in the fuel rooms, photographs the satellite, takes the telemetry tape and circuit boards, spares the
scientists and gets out before the silo blows; Ourumov confronts him and runs.

## Characters
| chr | Who | Difficulties | Starts as | Notes |
|---|---|---|---|---|
| 0x00 | Ourumov (body 7) at pad 0xB7 | all | rules 13-16 | carries Ourumov's briefcase (tag 0x07, item 43) and a master keycard (keyflags 0x1C); armour 30, health 20, speed / accuracy / hit-reaction ratings 80, no auto-aim on him |
| 0x03, 0x0A, 0x0B, 0x10, 0x1F, 0x20, 0x21 | stationary gunners (body 3) | all | rule 17 | speed rating 30, accuracy 20, grenade chance 20% |
| 0x07 | soldier (body 3) | all | dormant (rule 18) | then behaves as the standard generic guard (global.md) |
| 0x28, 0x29, 0x2D, 0x32, 0x35 | soldiers (body 3) | all | dormant (rule 18) | then behave as the standard generic guard (global.md) |
| 0x05, 0x08, 0x09, 0x0E, 0x0F, 0x15, 0x17 | soldiers (body 3) on their own pads | all | standard generic guard (global.md) | clone on gunfire |
| 0x13, 0x23 | soldiers (body 3) | all | standard generic guard (global.md) | |
| 0x01, 0x02, 0x04, 0x06, 0x0C, 0x0D, 0x11, 0x12, 0x14, 0x16, 0x18..0x1C, 0x22, 0x24..0x27, 0x2A..0x2C, 0x2E..0x31, 0x33, 0x34 | soldiers (body 3) on their own pads | all | deaf simple generic guard (global.md) | |
| 0x40..0x4E | patrolling soldiers (body 3) | all | chr 0x40 patrols path 0, 0x41 path 1, ... 0x4E path 14 | then behaves as the simple generic guard (global.md) |
| 0x36..0x3F | scientists (body 35) | all | rules 6-12 | count as civilians; 0x36..0x39, 0x3B, 0x3E, 0x3F are keyboard workers (rule 7); 0x38 carries keycard 4-H4 (keyflags 0x4), 0x39 keycard 4-C3 (0x8), 0x3B keycard 4-K2 (0x10); 0x3C carries the telemetry DAT (tag 0x01) |

All guards have hearing 1.0 and vision 100 (setup defaults); heads are chosen by the game.

## Objectives
Objective difficulty: the low byte of the setup word counts (objective e's word is 256: low byte 0, Agent).

| Letter | Text | Difficulties | Completed when | Failed when |
|---|---|---|---|---|
| a | 0x8812 (plant charges in fuel rooms) | 00 Agent, 007 | a plastique charge (item 0x22) has been placed in each of the rooms of pads 0x04, 0x0C, 0x58 and 0x5F (the engine's deposit-in-room check) | never |
| b | 0x8813 (photograph the satellite) | all | the player photographs tag 0x00 (prop 157 at pad 95) with the camera | never |
| c | 0x8814 (get the telemetry data) | Secret Agent, 00 Agent, 007 | the player holds tag 0x01 (DAT tape, item 73, carried by scientist 0x3C) | never |
| d | 0x8815 (take the satellite circuitry) | Secret Agent, 00 Agent, 007 | the player holds tags 0x02, 0x03, 0x04 and 0x05 (circuit boards, item 63, at pads 72, 73, 86, 94) | any of the four boards is destroyed (rule 5) |
| e | 0x8816 (keep scientist deaths low) | all | the player reaches the exit room (rule 21) | more than 2 scientists are dead (rule 12) |

## Rules

### Start
1. **When** the level starts, **then** the intro plays (Cinematics); Bond starts at pad 0xD1 (slot 0) with a
   silenced PP7 (item 5) and 100 rounds of its ammo (type 1), plastique (item 34, 8 charges, ammo type 23) and
   the camera (item 40, 10 shots, ammo type 27); outfit (cuff) 3; watch time 05:20.
2. Alternative start slots (co-op / extra players): slot 1 at pad 0x6E with the silenced PP7 and 100 rounds;
   slot 2 at pad 0x6D with the silenced PP7, 100 rounds and 8 plastique charges.

### The bomb countdown
3. **When** the intro camera starts its swirl onto Bond's head, **then** a countdown trigger is armed:
   - Agent: **when** a plastique charge (item 0x22) is lying still in the level (placed or thrown), **then**
     top text 0x8809 (charge placed, six minutes) and a 6:00 (360 s) countdown starts and is shown.
   - Secret Agent: if a charge is placed within the first 5 s, as for Agent; otherwise at 5 s top text 0x8846
     (silo infiltrated, charges primed) and a 7:30 (450 s) countdown starts.
   - 00 Agent and 007: same, but the automatic countdown is 8:30 (510 s).
4. **When** the countdown drops below 25 s, **then** the level's urgent music track (x-track, slot 0) starts;
   below 15 s, top text 0x880B (detonation imminent); **when** it reaches 0, **then** the countdown is hidden
   and stopped and explosions go off around the player without end (they kill the player: the mission is
   lost unless the player has already left, rule 21).

### Circuit boards
5. **When** any of the circuit boards (tags 0x02, 0x03, 0x04, 0x05) is destroyed, **then** objective d fails.

### Scientists (chr 0x36..0x3F)
6. **At start** every scientist counts as a civilian (their deaths are counted, rule 12).
7. **While** a keyboard worker (0x36..0x39, 0x3B, 0x3E, 0x3F) is standing idle, unalarmed and on the player's
   screen, **then** it types at its console (the keyboard generic list, global.md) and goes back to rule 8
   when that ends.
8. A standing scientist reacts in this order of priority:
   - a player's shot lands near it, or it sees another character shot: it **cowers** (rule 10);
   - it sees another character die, or it has been hit before: 25% it **flees** (rule 11); 75% it tries to
     throw a grenade (only possible when it has one; otherwise nothing happens);
   - it sees the player: 22% of the time, or always when the player aims at it, it reacts: if the player is
     within 1000 units it **cowers**; else if it has been hit before it reacts as on a death (above); else it
     **dodges** (rule 9);
   - it hears the player, or was shot at or saw / heard the player within the last 10 s: 75% it runs aside;
     when that succeeds, and the player is within 100 units or in front of it (within 90 degrees of where it
     faces), it reacts as on seeing the player;
   - it has a line of sight to the player and the player is within 100 units or in front of it: as on seeing
     the player.
   While a scientist is still moving, only the near-shot reaction applies.
9. **Dodge**: the scientist sidesteps (41%) or runs to one side (59%). **When** the move ends, **if** it still
   sees the player and the player is within 100 units or anywhere but a 50-degree sector behind it, it dodges
   again; otherwise it goes back to rule 8.
10. **Cower**: **while** the player aims at it, or has a line of sight to it within 2000 units, the scientist
    keeps its hands up (surrenders, dropping what it holds). It breaks and **flees** (rule 11, then comes back
    to cowering) when a player's shot lands near it, at random (about once every 2 s on average), or as soon
    as the player has neither line of sight nor aim on it or is farther than 2000 units. **If** it is hit
    more than once while cowering, **then** it draws a pistol and fights (the draw-TT33-and-attack generic
    list, global.md).
11. **Flee**: the scientist drops everything it carries (keycards, the DAT) and runs to pad 0xD1 (the silo
    entrance). **When** a player's shot lands near it on the way, it raises its hands, then (after a fraction
    of a second, or when it sees someone die) runs on. **When** it gets within 500 units of pad 0xD1, **if**
    no player has a line of sight to it, **then** it fades out (1.5 s) and is removed; otherwise, or if it
    stopped short of the pad, it returns to the behaviour that sent it (rule 8 or rule 10).
12. **When** more than 2 scientists have died, **then** top text 0x880C (failure: civilian casualties) and
    objective e fails.

### Ourumov (chr 0x00)
13. **At start** Ourumov stands frozen (no animation) until he has a line of sight to the player.
14. **When** he first has a line of sight to the player, **then** top text 0x880A (Ourumov orders the kill)
    and he fights in place: each time his previous shot is finished he fires at the player, standing (50%) or
    kneeling (50%).
15. **When** the player comes within 500 units of him, or his health falls below 20 (his armour is gone and
    he is hurt), or 30 s have passed since rule 14, **then** he flees (rule 16).
16. **Flee**: Ourumov becomes invincible; **if** his health is below 20, he first drops his briefcase (tag
    0x07). He sprints to pad 0xC7. **When** he first passes through the room of pad 0xBA, door tag 0x0F
    closes behind him; **when** he first passes through the room of pad 0xBE, door tag 0x10 closes. **When**
    he is within 500 units of pad 0xC7, **then** he fades out and is removed (if he stops short, he sprints
    on).

### Stationary gunners (chr 0x03, 0x0A, 0x0B, 0x10, 0x1F, 0x20, 0x21)
17. Each gunner stands on its pad aiming at the player without firing. **When** a player's shot lands near it,
    it sees another character shot or killed, it sees the player, or it has a line of sight to the player,
    **then** it starts a cycle: 39% it fires at the player; 22% it tries to throw a grenade (falls back to
    kneeling if it cannot); otherwise it kneels for up to 3 s (on average under 1 s), then aims and fires.
    It repeats the cycle (aim, wait for the trigger, act) for as long as it lives and never leaves its pad.

### Dormant soldiers
18. Chr 0x07 does nothing (no reaction at all) until a player is below height -900; chr 0x28, 0x29, 0x2D,
    0x32, 0x35 do nothing until a player is below height 0. **Then** each behaves as the standard generic
    guard (global.md).

### Door tag 0x0A
19. **When** the player switches to unarmed (item 1), **then** door tag 0x0A opens; **when** the player then
    switches to the silenced PP7 (item 5), it closes again. Repeats for the whole level. (Reachable in the
    retail game; looks like a developer test, kept as is.)

### Intro animation
20. **When** the intro camera starts its swirl onto Bond's head, **then** Bond's third-person body shows the
    first frame of animation 0x62 for 1 s, then plays animation 0x62 to its end and holds the last frame.

### The exit
21. **When** a player enters the room of pad 0xE8 (the exit lift room), **if** the player is alive, **then**
    objective e is completed (unless already failed), the player can no longer be hurt or pick anything up,
    the HUD is hidden, controls are locked, the countdown is hidden and stopped, and the screen fades to
    black (1 s).
    - **If** not every objective of the difficulty is complete, **then** the level ends (mission failed).
    - Otherwise the exit scene plays (Cinematics) and the level ends as a success.

## Objects and world
- Doors tag 0x0F (prop 112) and 0x10 (prop 114) close when Ourumov flees past (rule 16).
- Exit doors tag 0x0D (prop 116) and 0x0E (prop 117, linked pair): both snap open at the start of the exit
  scene; 0x0D closes during it.
- Door tag 0x0A (prop 127, linked to prop 128): rule 19.
- Object tag 0x11 (prop 170, at pad 107, invincible): made destructible and blown up in the exit scene when
  objective a is complete.
- Countdown (rules 3-4), urgent music (rule 4), endless explosions at 0 (rule 4).
- No fog, alarm, vehicle or aircraft scripting.

## Cinematics

### Intro (setup intro data)
Played as `intro.md` describes; the data below is in its units (world units, API degrees, seconds).

- **Fixed cameras:** one; world units, API degrees, fov 60 (intro.md §1). Lines: 0x8844 (location title), then
  0x8845 (time caption); the shot lasts 8 s, then fades to black over 1 s.

  | Camera | Position (x, y, z) | Yaw | Pitch | Room of pad |
  |---|---|---|---|---|
  | 0 | (5121.78, -1043.25, 2746.10) | -128.4 | -16.0 | 129 |

- **Swirl points** in order (intro.md §2): every offset in the player frame (flag 2: x left, y up, z forward, from
  the eye point); no point looks at the eye (no flag 4); the end marker (flag 1) follows the last row. Duration
  and tension belong to the leg that starts at the point. Swirl length 5.526 s (the legs from P1 on: P0 is only the previous control point, intro.md section 2).

  | Point | Offset (x, y, z) | Duration (s) | Tension |
  |---|---|---|---|
  | P0 | (-108, 2087, 700) | 1.167 | 0.65 |
  | P1 | (-108, 2087, 700) | 1.167 | 0.65 |
  | P2 | (-88, 1557, 700) | 1 | 0.65 |
  | P3 | (97, 972, 714) | 0.833 | 0.65 |
  | P4 | (266, 488, 508) | 0.75 | 0.65 |
  | P5 | (284, 126, 190) | 0.515 | 0.544 |
  | P6 | (195, 20, 33) | 0.505 | 0.502 |
  | P7 | (61, 7, -29) | 0.423 | 0.455 |
  | P8 | (0, 10, -30) | 0.333 | 0.25 |
  | P9 (final) | (0, 0, 0) | not used | not used |
  | P10 (shape only) | (0, 0, 0) | not used | not used |

- **Intro animation:** no record: table entry 0 (animation 61 from frame 95, speed 0.02; intro.md §3).
- **Body during the swirl (a level script, not the intro-animation record):** rule 20: from the swirl start,
  animation 98 frames 0 to 1 for 1 s, then frame 1 to its end, held at the last frame (blend 16 ticks).
- **Start (setup data):** pad 209; item 5 (silenced PP7), 100 of ammo type 1, item 34 (plastique), 8 of ammo type
  23, item 40 (camera), 10 of ammo type 27; outfit (cuff) 3; watch 5:20. Other start slots (not used in solo
  play): slot 1 pad 110, item 5 (silenced PP7), 100 of ammo type 1; slot 2 pad 109, item 5 (silenced PP7), 100 of
  ammo type 1, item 34 (plastique), 8 of ammo type 23.
- Region: EU and JP have four more fixed cameras (five to pick from), same two lines: (4541.67, -1183.11, 1031.74)
  yaw -145.4 pitch 34.0, room of pad 12; (2655.14, 44.46, 2403.41) yaw 20.3 pitch -64.0, room of pad 221; (178.34,
  -1408.42, 2454.20) yaw -57.2 pitch 58.0, room of pad 216; (-2138.47, 159.43, 870.22) yaw 32.1 pitch -30.0, room
  of pad 95.

### Exit (all objectives complete)
1. Screen black; every character is hidden; from now any button press fades out and ends the level (skip).
   Doors 0x0D and 0x0E snap open.
2. **If** the player is not holding a real weapon (knife, throwing knife, any gun, grenade, mines, launchers,
   laser: items 2..29 except 23), Bond's body is given the silenced PP7 (item 5) for the scene. The
   first-person weapon is hidden.
3. Shot 1: fixed camera at camera position tag 0x0C (prop 331: (1888.93, 91.06, -249.14), theta 3.12 rad,
   verta 6.25 rad, room of pad 0xC7). Bond's body is placed on pad 0xE8; fade in from black (1 s).
4. 1 s after the shot starts Bond's body plays animation 0x65 frames 8..109 and holds; at about 1.9 s it puts
   away the weapons of both hands; at 3.9 s door 0x0D closes.
5. When door 0x0D is fully closed: fade to black (1 s).
6. **If** objective a is complete: shot 2, fixed camera at camera position tag 0x12 (prop 333: (-1334.40,
   127.36, 829.96), theta 3.63 rad, verta 5.91 rad, room of pad 0x6B); fade in; a 2 s countdown is shown;
   at 0 it is hidden and object tag 0x11 explodes; 3 s later fade to black.
7. The level ends as a success.

## Co-op notes
- Rules 3, 18, 21: "a player" (any player) placing a charge, going below a height, or entering the exit room.
  The exit should trigger when any player enters (both are then taken into the scene); the "real weapon"
  check (exit step 2) applies to the player who triggered it.
- Rules 8-11 and 14-15 (sight, aim, distance, near shots): any player; distances to the nearest player.
- Rule 4: the explosions should go off around every player still in the level.
- Rule 19: either player switching weapons.
- Intro and exit scenes: shown to both players; one Bond body.

## API needed
- existing: `objectives.add/complete/fail/is_complete/is_failed/all_complete`, `mission.win()`,
  `mission.fail()`, `mission.difficulty()`, `wait(seconds)`, `wait(event(...))`, `on(...)`, `event("death")`,
  `guard(n):set_ai(list)`, `guard(n):sees_player()`, `guard(n):was_shot_at()`, `guard(n):hits()`,
  `guard(n):surrender()`, `guard(n):go_to(target, speed)`, `guard(n):is_moving()`, `guard(n):health()`,
  `guard(n):set_max_health(n)`, `guard(n):distance()`, `guard(n):distance_to(target)`, `guard(n):is_dead()`,
  `door(n):open()`, `door(n):close()`, `door(n):is_open()`, `player(n):pos()`, `clock()`.
- new: `guard(n):set_generic(name)` hand the character to a global behaviour (standard, simple, deaf simple,
  keyboard, draw-TT33-and-attack) with an optional list to return to.
- new: `guard(n):set_dormant(on)` the character stands still and ignores everything until switched off.
- new: `guard(n):start_patrol(path)`.
- new: `guard(n):set_armour(amount)`, `guard(n):set_ratings{speed=, accuracy=, reaction=}`,
  `guard(n):set_grenade_chance(percent)`, `guard(n):set_invincible(on)`, `guard(n):set_autoaim(on)`.
- new: `guard(n):fire_at_player(kneel?)` one burst at the nearest player; `guard(n):aim_at_player()` aim
  without firing; `guard(n):throw_grenade(): boolean`; `guard(n):kneel()`; `guard(n):freeze()` stop the
  animation; `guard(n):sidestep(): boolean`; `guard(n):run_aside(): boolean`.
- new: `guard(n):fade_out()` fade 1.5 s and remove; `guard(n):drop_carried()` drop every attached item;
  `guard(n):drop(tag)` drop one carried object.
- new: `guard(n):saw_guard_shot()`, `guard(n):saw_guard_die()`, `guard(n):heard_player(within_s?)`,
  `guard(n):noticed_player_within(seconds)`, `guard(n):has_line_of_sight()`, `guard(n):is_aimed_at()` (a
  player aims at it), `guard(n):angle_to_player()` (degrees, 0 = straight ahead), `guard(n):on_screen()`,
  `guard(n):in_room_of(pad)`.
- new: `guard(n):count_as_civilian()` and `civilians_killed(): number`.
- new: `player_in_room_of(pad, player?)`; `player(n):equipped(): item`.
- new: `item_lying_in_level(item): boolean` an item of this type is resting in the world (placed or thrown).
- new: `countdown:set(seconds)`, `countdown:start()`, `countdown:stop()`, `countdown:show()`,
  `countdown:hide()`, `countdown:remaining(): number`.
- new: `music:play_xtrack(slot)` the level's urgent track.
- new: `explode_around_players()` endless explosions around the players.
- new: `object(tag):destroy()`, `object(tag):set_invincible(on)`, `object(tag):is_destroyed()`, event
  `object_destroyed{tag}`.
- new: `door(tag):open_instant()`, `door(tag):is_closed()` fully shut (not closing).
- new: event `intro_swirl` the intro camera started moving onto the player's head.
- new: `cinema:begin(keep_countdown)` / `cinema:finish()`: hide HUD, lock controls, stop mission clock, hide
  the first-person weapon, show the player as a third-person body (`player_body(n)`).
- new: `player(n):set_invulnerable(on)` and `player(n):set_pickups(on)`.
- new: `chrs:hide_all()` hide every character (and stop their behaviour).
- new: `camera:fixed(tag)` cut to a camera-position record (its own angles).
- new: `screen:fade_out()` / `screen:fade_in()` / `screen:wait_fade()` (1 s fades).
- new: `skip_on_button()` from now any button fades out and ends the level.
- new: `player_body(n):teleport(pad)`, `player_body(n):play_anim(id, start, end, opts)` (hold last frame,
  blend seconds), `player_body(n):equip(item)`, `player_body(n):holster()`.
- new: `say_top(text_id)` top-of-screen text line.
