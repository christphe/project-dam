# Runway

Setup file `UsetuprunZ.c`, text bank 28 (ids 0x7000..). Bond fights across the service runway, picks up the
plane's ignition key, knocks out the guns and the missile battery, and escapes in the plane; the guards keep
coming back while he is there.

## Characters
| chr | Who | Difficulties | Starts as | Notes |
|---|---|---|---|---|
| 0x17 | guard at pad 110 | all | patrols path 0 | then behaves as the simple guard generic guard (global.md) |
| 0x18 | guard at pad 102 | all | patrols path 1 | then behaves as the simple guard generic guard (global.md) |
| 0x19 | guard at pad 26 | all | standard guard generic guard (global.md) | reinforcement slot 1 (rule 3) |
| 0x1a | guard at pad 27 | all | standard guard generic guard (global.md) | reinforcement slot 2 |
| 0x1b | guard at pad 28 | all | standard guard generic guard (global.md) | never replaced (see coverage) |
| 0x1c | guard at pad 29 | all | standard guard generic guard (global.md) | reinforcement slot 3 |
| 0x1d | guard at pad 30 | all | standard guard generic guard (global.md) | reinforcement slot 4 |
| 0x1e | guard at pad 31 | all | standard guard generic guard (global.md) | reinforcement slot 5 |
| 0x14 | guard at pad 21 | all | standard guard generic guard (global.md) | grenade chance 25/255 (setup attribute) |
| 0x15 | guard at pad 20 | all | standard guard generic guard (global.md) | |

All setup guards: body 37, random head, hearing 750, vision 50. The setup's grenade attributes for chr 8, 11,
14, 16 and 18 name chrs that do not exist in this level (no effect).

Reinforcements (rule 3) are spawned guards: body 2 (slot 5: body 37), random head, the chr number of the slot
they replace, and the loadout and behaviour of rules 4 and 5.

## Objectives
| Letter | Text | Difficulties | Completed when | Failed when |
|---|---|---|---|---|
| a | 0x700F (find the ignition key) | all | the key (tag 2) is in the player's inventory | the key is destroyed |
| b | 0x7010 (destroy heavy gun emplacements) | 00 Agent, 007 | drone guns tag 0, tag 4 and tag 5 all destroyed | - |
| c | 0x7011 (destroy missile battery) | Secret Agent and up | the missile battery (tag 1) destroyed | - |
| d | 0x7012 (escape in the plane) | all | the player starts the plane with the key (rule 7) | the plane (tag 3) is destroyed (rule 6) |

Briefing pages: title 0x700A, overview 0x700B, M 0x700C, Q 0x700D, Moneypenny 0x700E.

## Rules

### Reinforcements
1. **When** the level starts, **then** the reinforcement system is on for the whole level.
2. **While** the player is in one of the rooms below, **then** the five reinforcement slots use these spawn pads
   (the slots keep the last match's pads while the player is in none of these rooms; the player starts in the
   room of pad 53, so there is always a match):

   | Player in the room of pad | slot 1 (0x19) | slot 2 (0x1a) | slot 3 (0x1c) | slot 4 (0x1d) | slot 5 (0x1e) |
   |---|---|---|---|---|---|
   | 165 | 26 | 30 | 171 | 140 | 171 |
   | 168 | 143 | 113 | 171 | 140 | 171 |
   | 147 | 143 | 113 | 171 | 142 | 171 |
   | 145 | 103 | 102 | 142 | 6 | 142 |
   | 142 | 26 | 30 | 5 | 6 | 5 |
   | 172 | 103 | 5 | 31 | 29 | 31 |
   | 132 | 29 | 31 | 142 | 102 | 142 |
   | 104 | 142 | 113 | 27 | 30 | 27 |
   | 102 | 28 | 29 | 26 | 113 | 26 |
   | 53 | 31 | 26 | 30 | 142 | 30 |

   The rooms are tested in this order; the first one that holds the player wins.
3. **When** 2 s have passed since the level started, and then every 2 s, **if** a slot's guard (chr 0x19, 0x1a,
   0x1c, 0x1d, 0x1e) is dead or gone **and** the exit scene (rule 7) has not started, **then** a new guard with
   that chr number spawns at the slot's current spawn pad (around it if the pad is blocked), even when the
   player can see the pad. So each slot always comes back about 2 s after its guard dies.
4. **When** a reinforcement spawns, **then** it rolls its loadout: 31% health 4.0, no armour, grenade chance
   229/255, one Skorpion (item 7); 31% health 4.0, armour 1.0, grenade chance 204/255, one AK-47 (item 8);
   37% health 4.0, armour 2.0, two Skorpions (item 7, one in each hand).
5. **Then** the reinforcement hunts the player for the rest of its life: it heads for the player's position,
   sprinting when farther than 4000 units, running otherwise; once within 4500 units and in line of sight it
   attacks. Each attack move is picked at random: 4% throw a grenade, 16% sidestep, 12% roll while firing,
   8% fire while walking, 20% fire while running, 20% fire standing, 21% fire kneeling (a move that cannot be
   done falls through to the next in this list). After each move: if it still sees the player it picks
   another move, else it hunts again. It never patrols, never gives up and never raises the alarm.

### The plane
6. **When** the plane (tag 3) is destroyed, **then** show 0x7009 (plane destroyed) at the top of the screen and
   objective d fails.
7. **When** the player uses the plane (tag 3) and it is not destroyed: **if** the player does not hold the
   ignition key (tag 2), **then** show 0x7008 (no ignition key) at the top of the screen and nothing else;
   **if** the player is dead, nothing; **else** objective d completes, the plane becomes indestructible, the
   player becomes invulnerable and cannot pick anything up, the HUD hides, the controls lock, the mission
   clock stops, and the screen fades to black. Then:
   - **if** not all objectives of the difficulty are complete, the level ends at once (a failed mission);
   - **else** the outro of the Cinematics section plays and the level ends as a win.

## Objects and world
- Plane (aircraft, tag 3): model 291 at bound pad 10002. Rotor stopped until the outro; in the outro it is
  moved to pad 76, its rotor spins up to 620 (tenths of a degree per tick, no ramp) and it plays aircraft
  animation 1 (the take-off).
- Ignition key (tag 2): key object model 243 at pad 80, key bit 1; its names and pickup line are 0x7014..0x7018.
- Heavy gun emplacements (drone guns, tags 0, 4, 5): model 292 at bound pads 10003, 10004, 10005; immune to
  gunfire (explosives only), active from the start.
- Missile battery (tag 1): model 103 at bound pad 10001, immune to gunfire.
- Exit camera (tag 6): a cutscene camera at (9257, 689, -27881), yaw 180 deg, pitch -6 deg, in the room of
  bound pad 10004.
- A tank (model 288, pad 44) is a drivable vehicle with no script.
- Several doors carry the rename line 0x701D (door goes back inside): data, no script.
- No alarm, no fog change, no TV script in this level.

## Cinematics

### Intro (setup intro data)
Played as `intro.md` describes; the data below is in its units (world units, API degrees, seconds).

- **Fixed cameras:** 6 (one is picked at random); world units, API degrees, fov 60 (intro.md §1). Lines: 0x7013
  (location title); no second line; the shot lasts 5 s, then fades to black over 1 s.

  | Camera | Position (x, y, z) | Yaw | Pitch | Room of pad |
  |---|---|---|---|---|
  | 0 | (5946.68, 569.68, -10252.42) | 34.0 | -6.0 | 172 |
  | 1 | (10614.85, 453.68, -27156.78) | -5.0 | -6.8 | 165 |
  | 2 | (8819.42, 1757.68, -5022.98) | -170.3 | -23.2 | 132 |
  | 3 | (5389.93, 628.58, -6104.64) | 116.0 | -24.0 | 39 |
  | 4 | (5389.83, 129.68, -8253.13) | 60.5 | 2.8 | 39 |
  | 5 | (9252.87, -491.62, -6454.90) | -74.6 | 4.0 | 110 |

- **Swirl points** in order (intro.md §2): every offset in the player frame (flag 2: x left, y up, z forward, from
  the eye point); no point looks at the eye (no flag 4); the end marker (flag 1) follows the last row. Duration
  and tension belong to the leg that starts at the point. Swirl length 4.554 s (the legs from P1 on: P0 is only the previous control point, intro.md section 2).

  | Point | Offset (x, y, z) | Duration (s) | Tension |
  |---|---|---|---|
  | P0 | (0, -100, 200) | 0.667 | 0.75 |
  | P1 | (1, -98, 201) | 0.68 | 0.64 |
  | P2 | (-96, -100, 170) | 1.094 | 0.75 |
  | P3 | (-181, -20, 0) | 0.991 | 0.666 |
  | P4 | (-110, 38, -90) | 0.751 | 0.64 |
  | P5 | (-29, 32, -88) | 0.519 | 0.402 |
  | P6 | (-5, 10, -40) | 0.519 | 0.226 |
  | P7 (final) | (0, 0, 0) | not used | not used |
  | P8 (shape only) | (0, 0, 0) | not used | not used |

- **Intro animation:** no record: table entry 0 (animation 61 from frame 95, speed 0.02; intro.md §3).
- **Body during the swirl (a level script, not the intro-animation record):** from the swirl start, animation 99
  from its start to its end, held at the last frame (blend 16 ticks).
- **Start (setup data):** pad 53; item 5 (silenced PP7), 100 of ammo type 1; outfit (cuff) 3; watch 1:10. Other
  start slots (not used in solo play): slot 1 pad 140, item 26 (grenade), 6 of ammo type 5; slot 2 pad 142, item 8
  (KF7), 100 of ammo type 3.

### Outro (rule 7, all objectives done)
1. Behind the black screen: Bond's weapon is put away, the view switches to the exit camera (tag 6), fixed,
   looking along its own direction, all characters (Bond's model included) are hidden.
2. From here any button press fades to black and ends the level.
3. Fade in from black; extra music track slot 0 plays (up to 255 s); the plane is placed on pad 76 and starts
   its take-off (rotor and animation, see Objects).
4. 1 s after the take-off starts, sound 0xF8 plays (engine).
5. About 3.7 s after the fade-in starts, fade to black, then the level ends as a win.

## Co-op notes
- Rule 2 tests "the player's room": use the first player found in a listed room (player 0 first), or run the
  table per player and keep the most recent match.
- Rule 5's hunt target: the nearest living player.
- Rule 7: any player may use the plane; the key test is on that player (has(item) for any player is also
  acceptable). The invulnerability, HUD lock and outro apply to every player.
- Outro step 2: a button press from any player.

## API needed
- existing: `objectives.complete(id)`, `objectives.fail(id)`, `objectives.all_complete()`
- existing: `mission.win()`, `mission.fail()`
- existing: `wait(seconds)`, `spawn(fn)`, `on(name, fn)`, `chr(n)`, `tag(n)`, `text(id)`
- existing: `target:is_dead()`, `target:was_used()`, `target:make_usable()`, `target:sees_player()`,
  `target:distance()`
- new: `players_in_room(pad) -> {player}` the players standing in the room that holds pad N
- new: `chr(n):spawn_at(pad, {body, head, force})` create a guard with chr number n at pad N (around it when
  blocked); `force`: even when a player sees the pad; returns the guard or nil when no room / memory
- new: `guard:set_armour(n)`, `guard:set_grenade_chance(n_of_255)`
- new: `guard:give_weapon(item, {left_hand})` the guard holds a new weapon of that item
- new: `guard:use_generic(name)` hand the guard to a generic guard behaviour of global.md
  ("standard", "simple", ...)
- new: `guard:patrol(path)` start patrol path N
- new: `guard:move_to_player(speed)` head for the nearest player's current position ("run" | "sprint");
  `guard:is_moving()` existing
- new: `guard:attack_move(kind) -> boolean` one combat move at the nearest player: "grenade" | "sidestep" |
  "roll" | "fire_walk" | "fire_run" | "fire" | "fire_kneel"; false when it cannot be done now
- new: `guard:in_line_of_sight() -> boolean` the guard and a player can see each other
- new: `tag(n):is_destroyed() -> boolean`
- new: `tag(n):held_by(player) -> boolean` the tagged object is in that player's inventory
- new: `tag(n):set_indestructible()`
- new: `tag(n):move_to(pad)` place the object on a pad (takes the pad's rotation)
- new: `aircraft(tag):set_rotor(speed, ramp_ticks)`, `aircraft(tag):play_anim(anim_id)`
- new: `message_top(text)` a line at the top of the screen
- new: `player(n):set_invulnerable(on)` no damage and no pickups
- new: `scene.lock_player(on)` hide the HUD, lock the controls, stop the mission clock
- new: `scene.fade_out()` / `scene.fade_in()` fade to / from black, `wait()` returns when done
- new: `scene.hide_weapon()`, `scene.hide_all_chrs()`
- new: `camera.fixed(tag)` view from a cutscene camera record
- new: `scene.skip_on_button()` from now on any button press fades out and ends the level
- new: `music.play_extra(slot, stop_s, total_s)`, `sound.play(id)`
- new: event `"intro_swirl"` the intro camera started the swirl to Bond
- new: `bond_model:play_anim(anim_id, hold)` Bond's third-person model during the intro or outro
