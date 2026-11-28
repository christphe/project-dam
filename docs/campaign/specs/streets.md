# Streets

Setup file `assets/obseg/setup/UsetuppeteZ.c`, text bank 25 (ids 0x6400..). Bond chases Ourumov's car
through St. Petersburg (on foot or in the tank) against a clock, meets Valentin on the way (Secret Agent and
up), while soldiers and civilians keep appearing around him.

Distances are world units. Health and armour are game units as the mission API counts them (a plain guard
has about 4); on 007 the game's own health modifier multiplies them.

## Characters
| chr | Who | Difficulties | Starts as | Notes |
|---|---|---|---|---|
| 0, 1, 2, 3, 4, 5, 9, 10, 11, 18, 19, 20, 24, 25, 26, 27, 28, 32, 33, 35, 36, 37, 38 | soldiers (body 2) on their own pads (0: 264, 1: 265, 2: 263, 3: 200, 4: 202, 5: 201, 9: 81, 10: 82, 11: 83, 18: 168, 19: 169, 20: 170, 24: 138, 25: 135, 26: 269, 27: 270, 28: 271, 32: 278, 33: 279, 35: 281, 36: 282, 37: 283, 38: 284) | setup gating | generic standard guard (global.md) | chrs 2, 9, 25 throw grenades (grenade probability 0x19, 10%) |
| 6, 7, 8, 12, 13, 14 (pads 209, 208, 210, 218, 219, 220) | street ambush, first half | setup gating | stand still, do nothing | rule 13 gives them their role; 6 and 12, 14 throw grenades (10%) |
| 15, 16, 17, 21, 22, 23 (pads 232, 231, 230, 242, 243, 244) | street ambush, second half | setup gating | stand still, do nothing | rule 13; 15, 17, 21, 23 throw grenades (10%) |
| 29, 30 (pads 276, 277) | posted riflemen | setup gating | hold their spot | rule 14: armour 4 |
| 31 | Valentin (body 10), pad 268 | setup gating (objective a is Secret Agent and up) | waits | no auto-aim on him; rules 5-10 |
| 34 (pad 280) | hunter | setup gating | chases Bond | rule 18 from the start |
| 0x20, 0x22 | spawned soldiers (body 2) | all | spawned | rules 15-18 |
| 0x27, 0x28 | spawned civilians (body 32) | all | spawned | rules 19-21 |
| 8 parked vehicles (model 280; pads 176, 196, 203, 213, 227, 235, 236, 237) | cars | all | parked | do nothing |
| tank (tag 0, model 288, pad 174) | the tank | all | parked | Bond can drive it; used in the outro |

## Objectives
| Letter | Text | Difficulties | Completed when | Failed when |
|---|---|---|---|---|
| a | 641B (contact Valentin) | Secret Agent and up | rule 7: Valentin finished talking to Bond | rule 8 or 10: Valentin killed, removed or left behind |
| b | 6419 (pursue Ourumov and Natalya) | all | rule 22: Bond reaches the end point | rule 2: the countdown ran out |
| c | 641A (minimise civilian casualties) | all (the setup word is 256, but the game reads only its low byte: 0, Agent) | as b | rule 4: the civilian budget ran out |

Briefing pages: 6414, 6415, 6416, 6417, 6418 (watch-menu texts 0-4).

## Rules

### The chase clock

1. **When** 10 s have passed since the level start, **then** show the HUD countdown and start it: 180 s on
   Agent, 300 s on Secret Agent and up.
2. **When** the countdown reaches 0, **then** fail objective b (the mission is lost), show 640A (Ourumov
   escaped with Natalya) at the top of the screen, and hide and stop the countdown.
3. **When** Valentin's associates are told (rule 9), **then** the countdown is set to 360 s (from whatever it
   showed) and keeps running; rule 2 still applies.

### Civilian casualties

4. A civilian budget starts at 15. Each civilian that spawns (rule 19) costs 1; each civilian that walks out
   of the player's reach and disappears (rule 21) gives 1 back, so the budget falls by one for every
   civilian killed (and is lower while spawned civilians are about).
   - **when** it drops below 8 for the first time, show 6407 (warning: casualties high);
   - **when** it drops below 4 for the first time, show 6408 (warning: casualties critical);
   - **when** it drops below 1, show 6409 (mission failure: too many casualties) and fail objective c; after
     that the budget is no longer watched.
   Objective c is active on every difficulty, so this failure fails the mission like any failed objective.
   (Original quirk: of the two civilian spawners only one marks the cost correctly; the other's spawns are
   also counted through the same marker, so two spawns at the same moment count once. Rebuild the intent:
   one point per civilian spawn.)

### Valentin (chr 31, Secret Agent and up)

5. **When** Valentin first has line of sight to Bond, **then** he turns to face Bond and the conversation
   begins. While Bond stays within 800 the conversation runs; its clock pauses whenever Bond is farther.
6. **Conversation** (on the conversation clock): 640B at 4 s (Valentin greets Bond), 640C at 8 s (Bond:
   Ourumov took Natalya), 640D at 12 s (Valentin line), 640E at 16 s (Valentin line), 640F at 20 s (Bond line), 6410 at 24 s (Valentin line); each line is
   shown for 1 s on its own and then stays on screen as usual.
7. **When** the conversation clock reaches 28 s, **then** complete objective a, and Valentin runs off
   towards pad 245 (the start pad). When he gets there (within 150), or stops, or Bond is more than 3000
   from him: show 6413 (Valentin's associates contacted), apply rule 3, and Valentin fades out (1.5 s) and
   is removed.
8. **When** Bond is farther than 5000 from Valentin after the conversation began and before it ended,
   **then** show 6412 (Valentin has left), fail objective a, and Valentin fades out and is removed (no
   rule 3).
9. (Rule 3 is the reward: it only follows rule 7, never rule 8.)
10. **When** Valentin dies or disappears before the conversation ended, **then** fail objective a.
11. **When** Bond walks beyond 800 during the conversation, **then** show 6411 (Valentin line) once; he says it again on each later walk-away, once Bond has come back within 800 in between.
12. Valentin never fights.

### Street troops

13. **When** Bond enters the room of pad 44, **then** the ambush groups get their roles. One coin flip for the
    first half: either chrs 6, 7, 8 become rocket troopers and 12, 13, 14 riflemen, or the other way round;
    a second, independent coin flip does the same for chrs 15, 16, 17 against 21, 22, 23.
    - **Rocket trooper:** gets a rocket launcher (item 25, model 211) and a red beret (hat model 222);
      speed rating 80, accuracy 100, hit reaction 80. It stays where it is; whenever it has line of sight to
      Bond it fires at him (78% standing, 22% kneeling), again and again while the line holds. **When** Bond
      comes within 1000 it becomes a generic standard guard (global.md).
    - **Rifleman:** gets a KF7 (item 8, model 184); speed 20, accuracy 20, hit reaction 50; then behaves as
      the standard generic guard (global.md).
14. **Posted riflemen** (chrs 29, 30): armour 4; they never leave their spot, keep their gun aimed at Bond,
    and when a shot from Bond misses them, they see a guard shot or killed, they see Bond or Bond is in their
    line of sight, they fire (59% at once; else they kneel and pop up to fire at a random moment, at most
    4 s later), and repeat.

### Reinforcements that keep coming

15. **Spawners.** Four spawn points follow Bond through the streets: two soldier slots (chr 0x20, every 5 s;
    chr 0x22, every 9 s) and two civilian slots (chr 0x27, every 6 s; chr 0x28, every 8 s). They start 20 s
    after the level start. At each tick of its period, if its character is dead or gone and Bond is not in a
    cutscene, a slot spawns a new one at its current pad (also in plain view of the player).
16. **Where they spawn** depends on the area Bond is in (the first matching row; checked about every
    frame):

    | Bond is in the room of pad | soldier 0x20 | civilian 0x27 | civilian 0x28 | soldier 0x22 |
    |---|---|---|---|---|
    | 44, 46 or 49 | 54 | 55 | 67 | 62 |
    | 52, 55 or 67 | 76 | 75 | 69 | 70 |
    | 73, 76 or 79 | 84 | 85 | 99 | 100 |
    | 59, 70 or 94 | 105 | 106 | 108 | 109 |
    | 92 or 100 | 108 | 109 | 85 | 114 |
    | 82, 85 or 88 | 105 | 106 | 111 | 112 |
    | 106, 109 or 112 | 118 | 121 | 127 | 130 |
    | 115 or 118 | 130 | 137 | 142 | 143 |
    | 124 or 130 | 133 | 134 | 147 | 148 |
    | 145, 143 or 148 | 153 | 156 | 160 | 166 |
    | 137, 132 or 138 | 168 | 169 | 156 | 157 |
    | 151, 154 or 157 | 139 | 172 | 169 | 143 |
    | 163, 167 or 172 | 133 | 137 | 136 | 154 |
    | anywhere else | 45 | 46 | 48 | 49 |

17. **Spawned soldier loadout** (each spawn rolls): 39% health 4, no armour, grenade chance 90%, KF7 (item 8,
    model 184); 47% health 4, armour 2, grenade chance 80%, KF7; 14% health 4, armour 2, grenade launcher
    (item 24, model 185). Their shots can hit other characters (civilians in the way).
18. **Hunter** (spawned soldiers and chr 34): if Bond is farther than 8000 it vanishes at once. Otherwise it
    sprints to Bond if he is farther than 4000, else runs; on the way, once within 4500 with line of sight,
    it attacks: one move at random (throw a grenade 8%, sidestep 12%, firing roll 12%, fire while walking
    8%, fire while running 20%, fire standing 20%, fire kneeling 20%), repeating while it has line of sight,
    and chasing again when it loses it.
19. **Spawned civilians** (body 32) mill around the player: if he is farther than 2000 a civilian runs
    towards him until within 1000; near him it either waits or (50%) wanders to a spot around him (stopping
    if he moves more than 3000 away). They never attack.
20. **When** the player aims at a civilian, **then** it surrenders (hands up). Every 4 s afterwards it rolls:
    22% hands up again, 20% flinch animation 0x94 (frames 0-17), 20% cower animation 0x93 (frames 0-25),
    39% it goes back to milling around.
21. **When** Bond is farther than 8000 from a civilian, **then** it vanishes at once and gives its point
    back (rule 4).

### The end

22. **When** Bond (alive) is in the room of pad 248 and within 1000 of that pad, **then** complete objective
    b, make him immune to damage and pickups, hide the HUD, lock the controls, stop the mission clock and
    fade to black. Once black: if not every objective of the difficulty is complete, end the level (lost);
    else play the outro (Cinematics).

## Objects and world
- Tank (tag 0, pad 174): drivable by the player throughout; the outro snaps it onto pad 246 only when it
  is already in that pad's room; elsewhere it stays put (Cinematics, outro step 3).
- Hand grenade object (tag 2, item 26, pad 237): blown up 2 s into the outro (an explosion in the shot).
- Parked cars (model 280): idle, never driven by a script.
- Music: extra track 0 starts at the outro's fade-in.
- HUD countdown: rules 1-3.
- No doors, alarm, fog, TV or aircraft script.

## Cinematics

### Intro (setup intro data)
Played as `intro.md` describes; the data below is in its units (world units, API degrees, seconds).

- **Fixed cameras:** 2 (one is picked at random); world units, API degrees, fov 60 (intro.md §1). Lines: 0x641C
  (location title); no second line; the shot lasts 5 s, then fades to black over 1 s.

  | Camera | Position (x, y, z) | Yaw | Pitch | Room of pad |
  |---|---|---|---|---|
  | 0 | (4811.61, 136.48, 12699.27) | -36.1 | -8.0 | 59 |
  | 1 | (5787.50, 96.40, 6085.60) | -139.9 | -6.0 | 274 |

- **Swirl points** in order (intro.md §2): every offset in the player frame (flag 2: x left, y up, z forward, from
  the eye point); no point looks at the eye (no flag 4); the end marker (flag 1) follows the last row. Duration
  and tension belong to the leg that starts at the point. Swirl length 3.001 s (the legs from P1 on: P0 is only the previous control point, intro.md section 2).

  | Point | Offset (x, y, z) | Duration (s) | Tension |
  |---|---|---|---|
  | P0 | (0, -100, 200) | 0.667 | 0.75 |
  | P1 | (0, -100, 200) | 0.667 | 0.75 |
  | P2 | (10, -100, 170) | 0.667 | 0.75 |
  | P3 | (50, -20, 0) | 0.667 | 0.75 |
  | P4 | (20, 37.5, -77.5) | 0.667 | 0.75 |
  | P5 | (0, 10, -30) | 0.333 | 0.25 |
  | P6 (final) | (0, 0, 0) | not used | not used |
  | P7 (shape only) | (0, 0, 0) | not used | not used |

- **Intro animation:** no record: table entry 0 (animation 61 from frame 95, speed 0.02; intro.md §3).
- **Body during the swirl:** the table pose only; no level script animates it.
- **Start (setup data):** pad 245; item 4 (PP7), 100 of ammo type 1; outfit (cuff) 0; watch 9:00.

### Outro (after rule 22, all objectives complete)
1. While black: hide every character, let a button press skip to the end (fade and exit), hide Bond's
   first-person weapon.
2. Cut to the fixed camera of tag 1 (the camera record at (12957.7, -39.4, 24944.5)).
3. Bond's third-person model is placed at pad 247. **If** the tank (tag 0) **is** in the room of pad 246 (the
   player left it near the outro spot), it is snapped onto pad 246 (position and facing of the pad); the
   original first puts it on pad 169 and one frame later on pad 246, all while the screen is still black,
   so only the final placement on pad 246 is visible. **If** it is anywhere else, it is left where it is
   (not moved, so usually out of shot). Bond's model plays animation 0x6B. Fade in from black and start extra music track 0.
4. 2 s after the fade-in, the hand grenade of tag 2 is made destructible and blows up.
5. 5 s after the fade-in, fade to black and the level ends (won).

## Co-op notes
- Rules 5, 8, 11: "Bond" is the nearest player to Valentin; the walk-away rules need "no player within".
- Rule 15-16: spawn pads follow one player; use player 1, or the player the slot last chose.
- Rules 18-21: the nearest player.
- Rule 22: first player to reach the end point ends the level (or require both).
- Rule 4 budget is shared.

## API needed
- existing: `objectives.complete(id)`, `objectives.fail(id)`, `objectives.all_complete()`,
  `mission.difficulty()`, `mission.win()`, `mission.fail()`, `wait(seconds)`, `event(name, filter)`,
  `on(name, fn, filter)`, `spawn(fn)`, `clock()`, `guard(n):is_dead()`, `guard(n):sees_player()`,
  `guard(n):was_shot_at()`, `guard(n):distance()`, `guard(n):go_to(target, speed)`, `guard(n):surrender()`,
  `guard(n):face(target)`, `guard(n):stop()`, `player(n):near(radius)`, `Target:pos()`.
- new: `text_top(text_id)` show a game text id at the top of the screen.
- new: `countdown.set(seconds)`, `countdown.start()`, `countdown.stop()`, `countdown.show(on)`,
  `countdown.left()` the HUD countdown.
- new: `music.play_extra(slot)` start one of the level's extra music tracks.
- new: `player(n):in_room_of(pad)` the player is in the room that holds pad; `object(tag):in_room_of(pad)`.
- new: `player(n):is_aiming_at(guard)` the player's crosshair is on the guard.
- new: `player(n):in_cutscene()`, `player(n):set_cutscene_mode(on)` (immune to damage and pickups, HUD
  hidden, controls locked, mission clock paused).
- new: `guard(n):in_sight_of_player()` line of sight to a player.
- new: `guard(n):set_armour(n)`, `guard(n):set_ratings{speed=, accuracy=, reaction=}`,
  `guard(n):set_autoaim(on)`, `guard(n):set_grenade_chance(p)`, `guard(n):set_can_hit_guards(on)`.
- new: `guard(n):give_weapon(item, model)`, `guard(n):give_hat(model)`.
- new: `guard(n):attack_move(kind, target)` one combat move ("grenade", "sidestep", "roll", "walk_fire",
  "run_fire", "fire", "kneel_fire", "aim"); `guard(n):fire_now()`; `guard(n):kneel()`;
  `guard(n):is_busy()`.
- new: `guard(n):play_anim_id(anim, from_frame, to_frame, hold)` a native animation id.
- new: `guard(n):spot_near_player(kind)` a pad near the player, or nil.
- new: `guard(n):fade_out()` fade over 1.5 s then remove; `guard(n):remove()` at once.
- new: `guard(n):set_generic(kind)` hand the guard to a generic behaviour of global.md ("standard").
- new: `spawn_guard{chr=, pad=, body=, head=nil, ai=fn, force=true}` a new character at a pad with a given
  chr number; force: even in view of a player.
- new: `object(tag):destroy()` (also making it destructible); `object(tag):move_to(pad)`.
- new: `screen.fade_out()` / `screen.fade_in()` (wait until done).
- new: `camera.cut_to(tag, track_player)` to a tagged fixed camera; `camera.end_on_button()`.
- new: `cinema.hide_all_chrs()`, `cinema.bond()` Bond's third-person model (place at pad, play_anim_id),
  `player(n):hide_weapon()`.
- new: `mission.end_level()` end now; the result follows the objectives.
