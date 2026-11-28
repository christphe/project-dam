# Statue Park

Setup file `assets/obseg/setup/u/UsetupstatueZ.c`, text bank 35 (ids 0x8C00..). In a St. Petersburg statue
park at night Bond meets his contact Valentin, then Janus at the statue (Janus is Trevelyan), finds the
rigged Pirate helicopter, saves Natalya from its bomb, recovers the flight recorder from the wreck and gives
himself up to Defence Minister Mishkin at the park gates.

Distances below are in metres (1 m = 100 world units).

## Characters
| chr | Who | Difficulties | Starts as | Notes |
|---|---|---|---|---|
| 0x01 | Valentin, pad 8, body 10 | all | stands | rules 1-5; no auto-aim on him |
| 0x06-0x09 | park guards on pads 163, 152, 192, 194, body 3 | all | stand | each a KF7 (item 8) and hat model 217; rule 22, respawned by rule 21 |
| 0x02-0x05 | Janus' escort, body 4 | all | not present at start | spawned with Janus (rule 7) or as reinforcements (rule 21); loadout rule 9; behaviour rules 10, 22 |
| (Janus) | Janus / Trevelyan, body 8 | all | not present at start | spawned by rule 7 at pad 54; rules 11-15; no auto-aim on him; invincible once he leaves (rule 15) |
| 0x00 | Natalya, body 16 | all | not present at start | spawned by rule 16 at pad 6; rules 17-19; no auto-aim on her |
| 0x0A | Mishkin, body 19, head 69 | all | not present at start | spawned by rule 24 at pad 12; holds a DD44 (item 6); rule 25; no auto-aim on him |
| 0x0B, 0x0C | Mishkin's soldiers, body 19 | all | not present at start | spawned at pads 10, 11; rule 26 |
| (3 more) | Mishkin's soldiers, body 19 | all | not present at start | spawned at pads 13, 14, 15; rule 26 |

Body armour on pad 23 exists on Agent, Secret Agent and 00 Agent (not 007).

## Objectives
| Letter | Text | Difficulties | Completed when | Failed when |
|---|---|---|---|---|
| a | 8C39 (contact Valentin) | all | rule 3 (the talk is over) | the mission-failure state (rules 4, 5) |
| b | 8C3A (confront, unmask Janus) | all | rule 13 (10 s into the reveal) | Janus leaves unrevealed (rule 15); the mission-failure state |
| c | 8C3B (locate the helicopter) | all | rule 16 (a player comes within 30 m) | the mission-failure state |
| d | 8C3C (rescue Natalya) | all | rule 18 (she is with Bond when it blows) | Natalya dies (rule 20); the mission-failure state |
| e | 8C3D (find the flight recorder) | all | the flight recorder (tag 1) is in the player's inventory | the recorder is destroyed; the mission-failure state |

The **mission-failure state** (rules 4, 5) makes every objective fail. Briefing pages: title 8C34, overview
8C35, M 8C36, Q 8C37, Moneypenny 8C38.

Two level-wide states are used by many rules: **hostile** (Janus' side opens fire, rules 8, 10, 11-15) and
**Janus gone** (set when Janus starts to leave, rule 15).

## Rules

### Valentin (chr 0x01)

1. Valentin waits at pad 8. **When** a player comes into his line of sight, **then** he turns to face Bond
   and the **meeting** starts.
2. During the meeting he talks on a clock that only runs while the player is within 8 m of him:
   5 s 8C09 (good evening, strange times), 10 s 8C0A (Bond line: greets his contact), 15 s 8C0B (things have
   changed), 20 s 8C0C (Janus waits at the statue), 25 s 8C0D (he must leave now). **When** the player
   steps beyond 8 m, **then** the clock pauses and he shows 8C0E (be civil, I'm at risk) once (again on
   each new step away).
3. **When** the clock reaches 30 s, **then** objective a is completed, the meeting is **over**, and he walks
   to pad 22. He waits there until the player is more than 30 m away, then fades out (1.5 s) and is removed.
   (He also fades out on the way as soon as the player is more than 30 m away.)
4. **When** the player is more than 20 m away during the meeting (before 30 s), **then** show 8C0F (Valentin
   left, contact failed), the meeting is over, the mission-failure state is set, and he fades out.
5. **When** Valentin dies before or during the meeting, **then** show 8C30 (Valentin dead, mission failed)
   and set the mission-failure state. (Killing him after rule 3 has no effect.)

### Janus at the statue

6. **When** objective a is complete and a player comes within 8 m of pad 59 (the statue), **then** Janus
   spawns at pad 54 (retried until it works, not during the exit sequence), followed by his four escorts
   chr 0x02-0x05 (body 4) at the same pad. From then on, **when** the player goes more than 14 m from pad 59,
   **then** the level turns **hostile**.
7. The escorts' guard posts are pads 2, 3, 4, 5 for chr 0x02, 0x03, 0x04, 0x05 (rule 10).
8. **When** the player has put his gun away (rule 11) and then holds anything other than bare hands, **then**
   the level turns hostile.
9. **Escort loadout** (every spawn of chr 0x02-0x05): health 4, armour +2, 75% grenade chance, one
   automatic shotgun (item 16), 50% in the right hand, 50% in the left. **Then, if** the level is hostile or
   Janus is gone, they behave as in rule 22; **else** as in rule 10.
10. **Escort on guard:** walks to his guard post and keeps his gun aimed at the player. **When** the level
    turns hostile or a player's shot just misses him (on the way or at the post), **then** the level turns
    hostile, he opens fire, and behaves as in rule 22.
11. **Janus** walks to pad 1, turns to face Bond and shows 8C12 (good evening, 007). 2 s later:
    - **if** the player has bare hands: show 8C33 (no gun, budget cuts) and the reveal starts (rule 13);
    - **else** show 8C13 (put the gun away). **When** the player then has bare hands, show 8C14 (good
      fellow) and the reveal starts. **If** after 10 s the gun is still out, show 8C21 (kill him if not
      dropped) once. **If** after 20 s the gun is still out, the level turns hostile (rule 12).
12. **When** the level turns hostile, or a player's shot just misses Janus, before the reveal starts, **then**
    show 8C22 (impatient fool, now die) and Janus leaves (rule 15).
13. **The reveal** (a clock from its start): 5 s 8C16 (Bond line: unmasks the villain), 10 s objective b is
    completed and 8C17 (back from the dead), 15 s 8C18 (Bond line: asks for a motive), 20 s 8C19 (parents trusted the
    British), 25 s 8C1A (betrayed Lienz Cossacks, revenge), 30 s 8C1B (Bond line: asks after Natalya), 35 s 8C1C
    (left her at helicopter, gates), 40 s 8C1D (has the firing key now), 45 s 8C1E (helicopter blows in three
    minutes); then Janus leaves (rule 15).
14. **When** during the reveal the level turns hostile or a player's shot just misses Janus, **then** the
    level turns hostile, he shows 8C15 (so stubborn, James) if objective b is already complete, else 8C22,
    and he leaves (rule 15).
15. **Janus leaves:** he becomes invincible, **Janus gone** is set, and **if** objective b is not complete it
    fails. He runs (sprints when the level is hostile) to pad 50. **When** he gets there: **if** a player
    sees him, the level turns hostile and he runs on to pad 139 (and so on: each time he arrives in sight,
    he runs on to pad 139); **else** he fades out and is removed. **If** he gets stuck on the way, the level
    turns hostile and he runs again.

### The helicopter and Natalya

16. **When** Janus has been gone for 10 s, **then** show 8C1F (helicopter bomb in three minutes); the
    helicopter (aircraft tag 0) moves to pad 24; Natalya (chr 0x00, body 16) spawns at pad 6; a 180 s
    countdown appears on the HUD and runs. While it runs:
    - **when** a player first comes within 30 m of pad 24, objective c is completed;
    - **when** a player first comes within 5 m of pad 24, show 8C32 (proximity fuse, 15 seconds) and the
      countdown jumps to 15 s.
    **When** the countdown runs out, **then** the helicopter blows up (rule 23), **if** Natalya has not been
    reached (rule 17) she is shot (one hit, body part 8, with the damage of item 6); show 8C20 (helicopter
    destroyed), sound channel 0 stops, and the countdown is stopped and hidden.
17. **Natalya** crouches by the helicopter (animation 0x1E, frames 83-84, looping). **When** a player comes
    within 3 m, **then** show 8C26 (Bond line: calls to Natalya); 2 s later she is **reached**: she plays animation 0xA8
    (frames 60-120) and shows 8C23 (Natalya line: calls to Bond); when it ends, show 8C24 (Bond line: urges escape before the blast)
    and she follows Bond.
18. **Natalya follows:** she runs to Bond while he is more than 5 m away, walks when closer, stops within 2 m
    and waits until he is more than 4 m away. **When** the helicopter blows up while she follows, **then**
    objective d is completed; 5 s later show 8C25 (Bond line: orders before the gates) and she goes to the
    gates (rule 19).
19. **Natalya at the gates:** 2 s later she sprints to pad 9 (**if** objective e is complete meanwhile, she is
    put straight onto pad 9). Within 4 m of pad 9 she is **at the gates** and faces Bond. **When** Mishkin
    arrives (rule 24), **then** she raises her hands and stays so.
20. **When** Natalya is gone (dead and removed) after she spawned, objective d fails. **When** Mishkin's men
    turn hostile (rule 25), **then** she is shot (one hit, body part 8, with the damage of item 8) and
    objective d fails.

### Reinforcements and hunting guards

21. **Reinforcements**, every 2 s, for each listed chr that is not present (and not during the exit
    sequence); the spawn pad is chosen by one random roll per chr: 24% the first pad, 23% the second, 23% the
    third, 30% the fourth (a failed spawn falls through to the next pad):
    - **before** the meeting with Valentin, and **after** it is over until Janus spawns: park guards (body 3)
      chr 0x06 at pads 196 / 129 / 36 / 0, chr 0x07 at 163 / 89 / 58 / 52, chr 0x08 at 162 / 203 / 54 / 192,
      chr 0x09 at 214 / 152 / 107 / 42; each gets health 4, 75% grenade chance, a KF7 (item 8) in the right or
      left hand (50/50), hat model 0xD9, and behaves as in rule 22;
    - **while** the level is hostile or Janus is gone, **until** objective e is complete: escorts (body 4)
      chr 0x02 at pads 196 / 129 / 36 / 0, chr 0x03 at 163 / 89 / 58 / 52, chr 0x04 at 162 / 203 / 54 / 192,
      chr 0x05 at 214 / 152 / 107 / 42, with the rule 9 loadout (hence rule 22);
    - during the meeting, and while Janus is at the statue and not yet gone or hostile: none.
22. **Hunting guards** (park guards and escorts): they go for the player's position (68% sprinting, 32%
    running; re-aimed every 10 s and whenever they stop). **When** the player is within 15 m and in sight,
    **then** they fight: one action per roll — 4% grenade, 16% sidestep, 12% firing roll, 8% fire while
    walking, 20% fire while running, 20% stand and fire, 21% kneel and fire (an action that cannot be done
    falls through to the next one). After each action, if the player is still in sight they fight again,
    else they go for him again.
    - Park guards: **when** the meeting with Valentin starts (and is not over), or Janus spawns, **then** they
      sprint to pad 205, fade out and are removed.
    - Escorts: **when** objective e is complete, **then** they sprint to pad 35, fade out and are removed.
### The explosion and the flight recorder

23. **The explosion**, from the moment the countdown runs out: charges tags 11 and 12 (pads 25, 28) explode
    at once, the helicopter (tag 0) and charges tags 13, 14 (pads 26, 27) 1 s later, charges tags 15, 16
    (pads 29, 30) 2 s later (the charges are indestructible until then). At the same moment the flight
    recorder (tag 1) becomes collectable and is thrown to one random pad: 205 (12%), 214, 208, 200, 203, 196,
    213, 194 (each 12%), or 192 (6%). Until objective e is complete, while a player is in the room of pad
    179, show 8C27 (recorder can't be this far).

### Mishkin at the gates

24. **When** Natalya is at the gates and objective e is complete, **then**, as soon as every player is more
    than 50 m from pad 9: Mishkin (chr 0x0A) spawns at pad 12 (forced, retried until it works), Natalya
    raises her hands (rule 19), and five soldiers spawn: chr 0x0B at pad 10, chr 0x0C at pad 11, and three
    more at pads 13, 14, 15.
25. **Mishkin** aims his DD44 at the player. **When** a player comes within 15 m, **then** he talks on a clock
    that only runs while the player is within 25 m: 5 s 8C28 (livening up the nightlife), 10 s 8C29
    (blowing up helicopters), 15 s 8C2A (time to stop), 20 s 8C2B (questions about Severnaya), 25 s 8C2C
    (lead the way to HQ), 30 s 8C2D (truck waiting outside), 40 s 8C2F (step through the gates). **When**
    the player goes beyond 25 m, the clock pauses and 8C2E (come back, no way out) is shown once (again on
    each new step away). **When** a player's shot misses Mishkin or any of his soldiers, one of them is
    dying, or they are already hostile, **then** Mishkin's side turns **hostile**: everyone of them opens
    fire on the player and from then on keeps kneeling, aiming and firing at him (rule 20 for Natalya).
26. **Soldiers:** 75% grenade chance, a KF7 (item 8) in the right or left hand (50/50), hat model 0xD6. chr
    0x0B and 0x0C stand and keep Natalya covered (when hostile, their first shots go at Natalya, then at
    the player). The other three kneel and aim at the player. All react as in rule 25.

### Exit

27. **When** a living player enters the room of pad 22, **then** that player becomes invulnerable and cannot
    pick up, the HUD is hidden, controls are locked, the mission clock stops, and the screen fades to black.
    **When** the fade is done: **if** Mishkin is present, his side is not hostile, and every objective of
    the difficulty is complete, **then** the outro plays (Cinematics); **else** the level ends at once (the
    usual end-of-mission objective check decides).

## Objects and world
- The park gates: doors tags 3 and 4 (records 47/48, pads 5/6), snapped open silently in the outro.
- The Pirate helicopter: aircraft tag 0 (record 42). At level start its rotor is set to the setup's rotor speed
  620 with ramp 0 (`set_rotor(620, 0)`: full speed at once; the unit is 1/10 degree per tick, so 620 is the
  62 degrees per tick an earlier draft gave), it holds animation 2 at frame 2, and it is moved to pad 22
  (parked out of play).
  Rule 16 moves it to pad 24; rule 23 blows it up.
- Charges tags 11-16 (pads 25-30): rule 23 only.
- The flight recorder: tag 1 (record 44, renamed 8C3F..8C43), on pad 22 in the setup and not collectable
  until rule 23.
- HUD countdown: rule 16.
- Music: the outro starts the level's secondary track (slot 0, 255 s, no linger).
- Sound: channel 0 stops at the explosion (rule 16).
- No alarm and no fog change.

## Cinematics

### Intro (setup intro data)
Played as `intro.md` describes; the data below is in its units (world units, API degrees, seconds).

- **Fixed cameras:** 3 (one is picked at random); world units, API degrees, fov 60 (intro.md §1). Lines: 0x8C3E
  (location title); no second line; the shot lasts 5 s, then fades to black over 1 s.

  | Camera | Position (x, y, z) | Yaw | Pitch | Room of pad |
  |---|---|---|---|---|
  | 0 | (-12550.35, 127.27, 590.90) | -55.4 | 16.0 | 217 |
  | 1 | (248.90, 154.79, 4892.35) | -0.9 | 32.0 | 59 |
  | 2 | (-3528.08, -45.22, 4828.61) | -16.9 | 6.0 | 104 |

- **Swirl points** in order (intro.md §2): every offset in the player frame (flag 2: x left, y up, z forward, from
  the eye point); no point looks at the eye (no flag 4); the end marker (flag 1) follows the last row. Duration
  and tension belong to the leg that starts at the point. Swirl length 5.515 s (the legs from P1 on: P0 is only the previous control point, intro.md section 2).

  | Point | Offset (x, y, z) | Duration (s) | Tension |
  |---|---|---|---|
  | P0 | (-739, -29, 1417) | 0.833 | 0.6 |
  | P1 | (-739, -29, 1417) | 0.917 | 0.6 |
  | P2 | (-347, 0, 921) | 0.917 | 0.6 |
  | P3 | (128, 87, 527) | 0.917 | 0.6 |
  | P4 | (253, 153, 172) | 0.833 | 0.591 |
  | P5 | (162, 133, -80) | 0.67 | 0.591 |
  | P6 | (67, 59, -174) | 0.67 | 0.431 |
  | P7 | (9, 21, -106) | 0.591 | 0.422 |
  | P8 (final) | (0, 0, 0) | not used | not used |
  | P9 (shape only) | (0, 0, 0) | not used | not used |

- **Intro animation:** no record: table entry 0 (animation 61 from frame 95, speed 0.02; intro.md §3).
- **Body during the swirl (a level script, not the intro-animation record):** from the swirl start (the next
  tick), animation 97 from its start to frame 175, held there (blend 16 ticks).
- **Start (setup data):** pad 21; item 4 (PP7), 100 of ammo type 1; outfit (cuff) 0; watch 8:10.

### Outro (after rule 27, all objectives done, Mishkin calm)
1. Every character is hidden; from now on any button press fades out and ends the level. Bond's weapon is
   hidden.
2. Camera cuts to the cutscene camera tag 2 (position (-20154.92, 1062.74, 5653.49), room pad 223), turning
   to keep Bond in view. Fade in from black. The secondary music track starts (slot 0, 255 s). The gates
   (door tags 3 and 4) snap open without sound.
3. The third-person Bond is placed at pad 16 and plays animation 0x6B. Mishkin (chr 0x0A) reappears at pad
   17 playing animation 0x52; soldier chr 0x0B reappears at pad 20 playing animation 0x28; soldier chr 0x0C
   reappears at pad 19, kneels aiming at Bond, and 4.5 s later stands still.
4. 5 s after the start of step 3, fade to black; when the fade is done the level ends (a win).

## Co-op notes
- Valentin, Janus and Mishkin talk to "the player": use the nearest living player for the distance clocks
  (rules 2-4, 25) and the first player seen for rule 1. Rule 4 (contact failed) should use the nearest
  player too, so a second player wandering off does not fail the mission.
- Rules 8 and 11 (bare hands): with two players, the gun check should apply to every player within 14 m of
  the statue (a choice for the Luau writer; the original only knows one player).
- Rule 17 (Natalya reached) and rule 18 (following): the nearest player; she follows the player who reached
  her.
- Rule 24: every player more than 50 m from pad 9.
- Rule 27: the first living player to enter the room of pad 22 triggers the exit for everyone.

## API needed
- existing: `objectives.complete(id)`, `objectives.fail(id)`, `objectives.is_complete(id)`,
  `objectives.all_complete()`; `mission.win()`, `mission.fail()`; `clock()`; `wait`, `spawn`, `on`,
  `event("death")`, `event("pickup")`; `has(item)`; `guard:go_to(target, speed)`, `guard:is_moving()`,
  `guard:face(target)`, `guard:surrender()`, `guard:is_dead()`, `guard:sees_player()`,
  `guard:was_shot_at()`, `target:near(radius)`, `target:distance()`; `say(who, text)` / `message(text)`.
- new: `chr(num): Target` — a guard by chr number (setup or spawned); `chr:is_present()`, `chr:is_dying()`.
- new: `spawn_guard{chr?, body, head, pad, force?}: Target?` — spawn (force: even when the pad is in view);
  a spawned chr without a chr number (Janus, three soldiers) is returned as a Target.
- new: `chr:in_sight_of_player()` (line of sight), `chr:fade_out()` (1.5 s, then removed),
  `chr:set_invincible(on)`, `chr:set_no_autoaim(on)`, `chr:set_hidden(on)`, `chr:teleport(pad)`.
- new: `chr:aim_at(target)` / `chr:fire_at(target)` (target: the player or another chr), `chr:kneel_aim_at(target)`,
  `chr:stop_anim()`, `chr:play_anim(id, from_frame?, to_frame?, flags?)` (loop / hold flags),
  `chr:combat_move(kind): boolean`, `chr:run_to_player()`, `chr:sprint_to_player()`, `chr:walk_to_player()`.
- new: `chr:hit(part, item)` — one hit on a body part with an item's damage (with the hit reaction).
- new: `chr:set_health(n)`, `chr:add_armour(n)`, `chr:set_grenade_chance(pct)`, `chr:give_weapon(item, left?)`,
  `chr:give_hat(model)`.
- new: `player:holds(item): boolean` — the item in the player's hand (item 1 = bare hands).
- new: `player:distance_to_pad(pad)`, `player:in_room_of(pad)` / `event("room", {pad})`.
- new: `object(tag): Target`; `object:explode()` (even when made indestructible in the setup);
  `object:move_to(pad)`; `object:set_collectable(on)`.
- new: `aircraft(tag)`: `:set_rotor(speed, accel_s)`, `:play_anim(id, frame)` (plus `object:move_to`).
- new: `hud.countdown_set(seconds)`, `hud.countdown_run(on)`, `hud.countdown_show(on)`, `hud.countdown()`.
- new: `sound.stop_channel(n)`; `music.play_track(slot, seconds, linger_s)`.
- new: `player:lock(on)`, `player:set_invulnerable(on)`, `player:is_dead()`.
- new: `screen.fade_out()`, `screen.fade_in()`.
- new: `cinema.in_swirl(): boolean` (the intro camera is in its swirl), `cinema.bond(): Target` with
  `:teleport(pad)`, `:play_anim(id, from, to, flags)`; `cinema.hide_all_chrs()`, `cinema.skip_on_button()`,
  `cinema.hide_weapon()`, `cinema.camera_at(tag, look_at_bond)`.
- new: `door(tag):snap_open()`.
- new: `mission.finish()` — end the level now; the objective check decides.
- new: `objectives.fail_all()` — the mission-failure state (or `objectives.fail` on each id).
