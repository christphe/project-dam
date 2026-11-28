# Archives

Setup file `assets/obseg/setup/UsetuparchZ.c`, text bank 2 (ids 0x0800..). Bond, a prisoner of the GRU,
escapes the interrogation room, finds Natalya, meets Defence Minister Mishkin (who hands over the safe key
for the helicopter black box on Secret Agent and up) and escapes with Natalya through the library.

Distances are world units.

## Characters
| chr | Who | Difficulties | Starts as | Notes |
|---|---|---|---|---|
| 0 | Natalya (body 16), pad 183 | all | waits | companion, rules 9-15 |
| 1 | Mishkin (body 19), pad 73 | all | waits | rules 16-20 |
| 14 | interrogator (body 19), pad 197 | all | interrogates Bond | rules 2-5 |
| 15 | second interrogator (body 19), pad 198 | all | stands by | rule 6 |
| 26 | soldier on patrol path 3, pad 151 | all | patrols path 3 | rule 7, grenade chance 5% |
| 23, 24, 25, 27, 28, 29 (pads 50, 2, 117, 69, 103, 167) | patrolling soldiers | all | patrol paths 0, 1, 2, 4, 5, 6 | then behave as the simple generic guard (global.md); vision 70; 27, 28 grenade chance 5% |
| 9, 11 (pads 179, 193) | soldiers (body 19) | all | wait for Bond | rule 8; 9 grenade 10%, 11 grenade 5%; 9 clones on gunfire |
| 2, 3, 4, 5, 6, 10, 12, 13, 20, 21, 22 (pads 185, 186, 187, 188, 189, 184, 194, 195, 173, 174, 175) | soldiers | all | stand guard | generic standard guard (global.md); clone on gunfire; 10, 13, 20, 21, 22 grenade 10%, 12 grenade 5% |
| 7, 8, 16, 17, 18, 19 (pads 177, 178, 180, 181, 182, 176) | soldiers | all | stand guard | generic standard guard (global.md) |

(Who is present at each difficulty is the setup's load gating.)

## Objectives
| Letter | Text | Difficulties | Completed when | Failed when |
|---|---|---|---|---|
| a | 0829 (escape interrogation room) | all | Bond enters the room of pad 152 | |
| b | 082A (find Natalya) | all | rule 9: Natalya sees Bond | |
| c | 082B (recover black box) | Secret Agent and up | Bond holds the safe item of tag 2 (the black box) | rule 21: Mishkin dies before Bond has the safe key |
| d | 082C (escape with Natalya) | all | Bond enters the room of pad 208 while Natalya is with him (rule 10) | rule 15: Natalya is gone |

Briefing pages: 0824, 0825, 0826, 0827, 0828 (watch-menu texts 0-4).

## Rules

### Interrogation room (start)

1. Nothing below starts before the intro (fixed cameras and swirl) is over.
2. **Interrogation.** When the intro ends, the interrogator (chr 14) questions Bond, on a clock from that
   moment: 080D at 5 s (guard line), 080F at 10 s (Bond line), 080E at
   15 s (guard line), 0811 at 20 s (Bond line), 0812 at 25 s (guard line), 0813 at 30 s (guard line), 0814 at 35 s (Bond line),
   0815 at 40 s (guard line), 0816 at 45 s (guard line). Each line is new on
   screen for 1 s. After 50 s the talk is over; the interrogator keeps waiting for rule 3.
3. **The interrogator turns hostile** (rule 2 stops) and alerts the second interrogator (chr 15) and the
   patrol soldier (chr 26), **when**:
   - Bond picks up the PP7 (tag 0, pad 207) or its magazine (tag 1, pad 206) from the table: shows 0810
     (guard line);
   - Bond comes within 200 of pad 150 (the way out): shows 0817 (guard line);
   - the interrogator is hit, or was alerted by the other interrogator (rule 6), or the soldier's orders
     arrived (rule 7): silently.
4. **Hostile interrogator** (both interrogators, and soldiers 9 and 11 from the start): it waits until it
   could see Bond, fires while walking, then fires while running, then behaves as the standard generic
   guard (global.md).
5. (The interrogator never leaves its post while talking.)
6. **Second interrogator** (chr 15): stands still until it is hit or alerted (rule 3); then it alerts chr 14
   and chr 26 and turns hostile (rule 4).
7. **Soldier with new orders** (chr 26): patrols path 3. After the intro, when it sees Bond, has line of
   sight, was shot at or saw Bond in the last 10 s, heard him in the last 10 s, a shot from Bond missed it,
   or it saw a guard shot or killed:
   - if an interrogator already alerted it, it runs to Bond, then behaves as the standard generic guard
     (global.md);
   - otherwise it walks towards Bond; after 2 s it stops and shows 0818 (soldier: Ourumov's orders, kill
     the spy); 6 s after the start of this walk both interrogators turn hostile (rule 3, silently) and the
     soldier runs to Bond, then behaves as the standard generic guard.
8. Soldiers 9 and 11 use rule 4 from the start.

### Natalya (chr 0)

9. **When** Natalya could see Bond (her own line of sight, facing ignored), **then** show 0808 (Natalya:
   James!), she raises her arms for half a second, and objective b completes. This repeats after each
   time she hid (rule 12): she has to see Bond again first.
10. **Following.** While she follows Bond, she counts as with him (objective d may complete). She runs to
    Bond; within 250 she stops; if he gets farther than 400 she runs again.
11. **When** Bond stands within 80 of her:
    - if he is behind her or at her sides, she startles and looks around and shows 0809 (Natalya line);
    - if he is in front of her: before Mishkin's talk is over and while Mishkin lives, 0822 (Natalya line); after it (or once Mishkin is gone), 080A (Natalya line: proposes the way out);
      then she raises her arms for half a second.
    She says it again after each gesture while he stays that close.
12. **When** a shot from Bond just misses her, **then** she no longer counts as with him, shows 080B
    (Natalya line), and runs to a hideout: 50/50 pad 158 or pad 196, each used only once
    (if the one picked is used, she keeps following and the next miss tries again). If she stops on the way
    and Bond is not aiming at her, she gives up and goes back to rule 9; if he is aiming at her she keeps
    running. Within 500 of the hideout she stops there and waits for rule 9.
13. Natalya never fights.
14. **When** Natalya dies, **then** show 080C (Natalya cries out).
15. **When** Natalya no longer exists (dead or removed), **then** objective d fails.

### Mishkin (chr 1)

16. **When** Mishkin has line of sight to Bond, **then** he walks towards Bond until he stops or is within
    300, and turns to face him.
17. **Talk**, on a clock that only runs while Bond is within 400 of Mishkin and in the room of pad 73
    (it pauses otherwise): 0819 at 0 s (Mishkin line), 081A at 5 s (Mishkin line), 081B at 10 s (Bond line), 081C at 15 s (Mishkin line), 081D at 20 s
    (Mishkin line), 081E at 25 s (Bond line), 081F at 30 s (Mishkin line), 0820 at 35 s (Mishkin line), 0821 at 40 s (Mishkin line).
18. **At the 30 s line**, if Bond does not have it yet, **then** give Bond the safe key (tag 4, key model
    243) and show 0823 (got safe key) at the bottom of the screen.
19. **At 45 s** the talk is over (Natalya's hint changes, rule 11) and Mishkin walks to pad 174. As soon as
    he is out of Bond's line of sight, or he reaches pad 174 (within 150), he fades out (1.5 s) and is
    removed. If he stops elsewhere while still seen he walks on.
20. **When** a shot from Bond misses Mishkin at any time before he leaves, **then** he draws his TT33 and
    attacks (the draw-TT33 generic guard, global.md), and the talk never resumes.
21. **On Secret Agent and up, when** Mishkin dies before Bond has the safe key, **then** objective c fails.
    Once Bond has the key, Mishkin's death no longer matters.

### The exit

22. **When** Bond (alive) enters the room of pad 208, **then** make him immune to damage and pickups, lock
    the controls and fade to black. Once black: if not every objective of the difficulty is complete, end
    the level (lost); else play the outro.

## Objects and world
- Interrogation table: PP7 (tag 0, model 191, pad 207) and a magazine (tag 1, model 133, pad 206): picking
  either up starts rule 3.
- Safe key (tag 4, key model 243): given by Mishkin (rule 18).
- Black box: the safe item of tag 2, inside the safe (objective c).
- Door of tag 3 (model 293, pad 135): no script acts on it.
- Music: extra track 0 starts at the outro's fade-in.
- No alarm, fog, TV, vehicle or aircraft script.

## Cinematics

### Intro (setup intro data)
Played as `intro.md` describes; the data below is in its units (world units, API degrees, seconds).

- **Fixed cameras:** 2 (one is picked at random); world units, API degrees, fov 60 (intro.md §1). Lines: 0x082D
  (location title); no second line; the shot lasts 5 s, then fades to black over 1 s.

  | Camera | Position (x, y, z) | Yaw | Pitch | Room of pad |
  |---|---|---|---|---|
  | 0 | (-1940.73, -8.26, -1037.23) | -36.0 | 18.0 | 69 |
  | 1 | (-494.42, 417.84, -1958.09) | -131.4 | -6.0 | 55 |

- **Swirl points** in order (intro.md §2): every offset in the player frame (flag 2: x left, y up, z forward, from
  the eye point); no point looks at the eye (no flag 4); the end marker (flag 1) follows the last row. Duration
  and tension belong to the leg that starts at the point. Swirl length 3.944 s (the legs from P1 on: P0 is only the previous control point, intro.md section 2).

  | Point | Offset (x, y, z) | Duration (s) | Tension |
  |---|---|---|---|
  | P0 | (-43, -118, 641) | 0.528 | 0.7 |
  | P1 | (-43, -118, 641) | 0.528 | 0.7 |
  | P2 | (28, -118, 648) | 0.74 | 0.7 |
  | P3 | (134, -23, 501) | 0.695 | 0.647 |
  | P4 | (162, 7, 246) | 0.552 | 0.586 |
  | P5 | (118, 10, 71) | 0.521 | 0.531 |
  | P6 | (40, 10, -18) | 0.481 | 0.402 |
  | P7 | (2, 10, -30) | 0.427 | 0.25 |
  | P8 (final) | (0, 0, 0) | not used | not used |
  | P9 (shape only) | (0, 0, 0) | not used | not used |

- **Intro animation:** no record: table entry 0 (animation 61 from frame 95, speed 0.02; intro.md §3).
- **Body during the swirl:** the table pose only; no level script animates it.
- **Start (setup data):** pad 192; item 1 (fist), item 60 (watch magnet), 5 of ammo type 25; outfit (cuff) 0;
  watch 8:25.
- The interrogation (rule 2) starts at the hand-over.

### Outro (after rule 22, all objectives complete)
1. While black: hide every character, let a button press skip to the end (fade and exit). If Bond holds none of
   items 4-21 (PP7 through gold PP7), 24 (grenade launcher) or 25 (rocket launcher), give his model the PP7
   (item 4). The Moonraker laser (22) and the watch laser (23) do not count as a gun here.
2. Switch from Bond's view to the fixed camera of tag 5 (the camera record at (432.6, 56.0, -3153.2)),
   tracking Bond's model.
3. Bond's third-person model is placed at pad 171. If his hands are empty, a PP7 (model 191) is put in
   them. Natalya is unhidden and placed at pad 170. Fade in from black and start extra music track 0.
4. Bond's model plays animation 0x59; 1.7 s later animation 0x68; 2.4 s after that, fade to black and the
   level ends (won).
5. Natalya plays animation 0x73; 2.6 s later animation 0x65 from frame 51.

## Co-op notes
- Rules 2-3, 7: "Bond" is either player (any player picking up the table weapon or nearing pad 150).
- Natalya (rules 9-12) follows the nearest living player; a miss by either player scares her.
- Mishkin (rules 16-19): talks to the nearest player; the clock runs while any player is within 400 and in
  the room of pad 73; the key (rule 18) goes to that player, or to both.
- Rule 21: Bond having the key means any player has it.
- Rule 22: first player into the room of pad 208 (or require both).

## API needed
- existing: `objectives.complete(id)`, `objectives.fail(id)`, `objectives.all_complete()`,
  `mission.difficulty()`, `mission.win()`, `wait(seconds)`, `event(name, filter)` ("pickup", "death"),
  `on(name, fn, filter)`, `spawn(fn)`, `clock()`, `give(item, player)`, `has(item, player)`,
  `guard(n):is_dead()`, `guard(n):sees_player()`, `guard(n):was_shot_at()`, `guard(n):hits()`,
  `guard(n):distance()`, `guard(n):go_to(target, speed)`, `guard(n):face(target)`, `guard(n):stop()`,
  `guard(n):is_moving()`, `guard(n):near(radius)`, `player(n):distance_to(pad)`.
- new: `text_top(text_id)` / `text_bottom(text_id)` show a game text id at the top / bottom of the screen.
- new: `event("intro_done")` (or `intro.is_running()`) the intro cameras and swirl have ended.
- new: `player(n):in_room_of(pad)` the player is in the room that holds pad.
- new: `player(n):is_aiming_at(guard)` the player's crosshair is on the guard.
- new: `player(n):picked_up(tag)` the player collected the tagged object; `give_object(tag, player)`
  give a tagged object (the safe key) and mark it collected.
- new: `guard(n):could_see_player()` line of sight ignoring facing; `guard(n):in_sight_of_player()`.
- new: `guard(n):player_angle()` where the nearest player stands relative to the guard's facing (degrees,
  counter-clockwise; front = below 90 or above 270).
- new: `guard(n):heard_player(seconds)`, `guard(n):alerted_within(seconds)`, `guard(n):saw_guard_hit()`,
  `guard(n):saw_guard_die()`.
- new: `guard(n):attack_move(kind, target)` ("walk_fire", "run_fire"); `guard(n):is_busy()`.
- new: `guard(n):raise_arms()` (half a second), `guard(n):startle()` (look around surprised).
- new: `guard(n):start_patrol(path)`.
- new: `guard(n):set_generic(kind)` hand the guard to a generic behaviour of global.md ("standard",
  "simple", "run_to_bond", "draw_tt33").
- new: `guard(n):fade_out()` fade over 1.5 s then remove.
- new: `player(n):set_cutscene_mode(on)` immune to damage and pickups, controls locked;
  `screen.fade_out()` / `screen.fade_in()`.
- new: `camera.cut_to(tag, track_player)`; `camera.end_on_button()`; `cinema.hide_all_chrs()`;
  `cinema.bond()` Bond's third-person model (place at pad, play_anim_id, set held item);
  `player(n):holds_gun()`; `guard(n):place_at(pad)`, `guard(n):show()`;
  `guard(n):play_anim_id(anim, from_frame, to_frame)`.
- new: `music.play_extra(slot)`.
- new: `mission.end_level()` end now; the result follows the objectives.
