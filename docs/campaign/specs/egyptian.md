# Egyptian

Setup file `UsetupcrypZ.c`, text bank 10 (ids 0x2800..). In the el-Saghira temple Bond solves the floor puzzle
of the golden gun room to take the gun, then has to put Baron Samedi down three times while reinforcements
roam the temple.

## Characters
| chr | Who | Difficulties | Starts as | Notes |
|---|---|---|---|---|
| 0x14 | Baron Samedi, first form (body 12) | all | stands at pad 150 | rules 7-13; armour 200, health 200, speed/accuracy/flinch 50, two DD44s (item 6, model 205); not auto-aimed |
| 0x15 | Samedi, second form (spawned, body 12) | all | rule 8 | armour 1000, health 500, ratings 80, two ZMGs (item 9, model 205) |
| 0x16 | Samedi, third form (spawned, body 12) | all | rule 9 | armour 3000, health 500, ratings 100, two lasers (item 0x16, model 205) |
| 0x05-0x12 | temple guards (chrs 5-18, body 3) | all | each on its own pad (pads 123-136) | standard generic guard (global.md); removed in the outro |
| 0x05-0x09 | reinforcements (spawned, body 3), one per spawn slot 0-4 | all | rules 1-6 | speed and accuracy by mission time (rule 4); kit: 37% health 40, armour 20, two of item 9 (model 195); 31% health 40, armour 10, grenades 80% of the time, one; 32% health 40, no armour, grenades 89%, one |

Note: the reinforcement chr numbers 0x05-0x09 are also the numbers of setup guards; while one of those setup
guards lives, its reinforcement slot counts as occupied (rule 2).

Start: pad 149, PP7 (item 4) in the right hand, 100 rounds of ammo type 1, outfit (cuff) 8, watch 11:45.

## Objectives
| Letter | Text | Difficulties | Completed when | Failed when |
|---|---|---|---|---|
| a | 0x280C (recover the golden gun) | all (min 0) | the player holds the golden gun (tag 0x04) and its ammo box (tag 0x05) | never |
| b | 0x280D (put down Baron Samedi) | all (min 0) | Samedi's third form is gone (rule 10) | never |

Briefing pages (watch menu): 0x2807..0x280B.

## Rules

### Reinforcements
1. Five spawn slots (0-4); slot k spawns chr 5+k. Periods: slot 0 40 s, slot 1 40 s, slot 2 50 s, slot 3 50 s,
   slot 4 60 s.
2. Nothing spawns during the first 20 s. After that, each slot, every period: **if** its chr does not exist,
   spawning is on (rule 3), and the player is not in the end sequence (rule 17), **then** a guard (body 3) spawns at
   the slot's current spawn pad, only out of the player's view; a failed spawn is retried next period.
3. Spawning is on at the start. **While** the player is in the room of pad 145 it is off (slots wait; guards out take
   cover, rule 6); **when** he leaves, it is on again and the slots restart their periods.
4. A new reinforcement's speed and accuracy depend on the mission time: up to 300 s 20, after 300 s 40, after
   600 s 60, after 900 s 80. Kit: Characters table.
5. Spawn pads of slots 0-4 by the room the player is in (the last match is kept when he is in none):
   - room of pad 1: 21, 26, 90, 140, 137
   - room of pad 114: 108, 112, 80, 79, 103
   - room of pad 21: 141, 48, 47, 88, 139
   - room of pad 104: 101, 102, 114, 121, 97
   - room of pad 48: 141, 11, 21, 85, 64
   - room of pad 100: 113, 112, 95, 94, 114
   - room of pad 86: 141, 80, 71, 39, 48
   - room of pad 88: 21, 26, 140, 139, 80
   - room of pad 95: 97, 99, 47, 59, 145
   - room of pad 80: 114, 137, 138, 42, 66
   - room of pad 94: 14, 64, 67, 139, 138
   - room of pad 67: 39, 59, 71, 72, 85
   Until the player first enters one of these rooms the slots have no spawn pad (UNCERTAIN: the start pad 149 is in
   none of them; treat the slots as idle until the first match).
6. A reinforcement hunts the player as in the Depot: runs to him (sprints beyond 40 m); within 20 m and in sight, or
   when a player's shot just misses it, it attacks with one random move: grenade 4%, side-step 16%, firing roll 12%,
   fire while walking 8%, fire while running 20%, fire standing 20%, fire kneeling 20%. **When** spawning is off
   (rule 3) or 2% of the time instead of a move, it takes cover: runs to the player, kneels within 20 m and fires
   whenever it sees him, and goes back to hunting when a player's shot just misses it or he is farther than 30 m.

### Baron Samedi
7. Each form: on appearing it is invincible, faces the player, and 5 s later becomes vulnerable; **when** the player
   aims at it or it sees him, it laughs (sound 54) and fights (rules 11-13).
8. **When** the first form (chr 0x14) is gone (killed), **then** the second form (chr 0x15) appears at pad 186, even in
   view, the invincible object of tag 0x06 at that pad is made destructible and blown up, and once it has appeared it
   laughs (sound 54).
9. **When** the second form is gone, **then** the third form (chr 0x16) appears at pad 146, even in view, and the object
   of tag 0x07 there is made destructible and blown up. Once the third form has appeared, the fog slowly changes to
   the level's next fog, and 2 s later Samedi laughs (sound 54).
10. **When** the third form is gone, **then** objective **b** completes.
11. In a fight Samedi, each moment, 50% of the time checks whether the player is aiming at him: if so he dodges (rule 13).
    Otherwise, if he sees the player he attacks (rule 12), else he runs to the player's position until he stops or
    sees him (and dodges if aimed at on the way).
12. Attack: 50% of the time, if the player is aiming at him, he dodges first. **If** the player's health is below 25% he
    fires while running at him. Otherwise one random move: firing roll 16%, fire while walking 16%, fire while
    running 16%, fire standing 16%, fire kneeling 16%, aim kneeling then fire 10%, aim standing then fire 12%.
13. Dodge: one random move: hop sideways 24%, side-step 36%, run to the side 28%, otherwise a quick dodge
    animation 11 (frames 20-60); then he goes back to what he was doing.

### The golden gun room (a floor puzzle)
14. At the start (and each time the puzzle resets): doors tags 0x03 and 0x00 close, door tag 0x01 (the gun's case)
    opens, and the golden gun (tag 0x04) and its ammo box (tag 0x05) cannot be picked up.
15. **When** the player enters the room of pad 151, **then** doors tags 0x02 and 0x01 close behind him and the floor is
    live: **if** he comes within 0.9 m of any of pads 151-165 or 167-174 (a wrong tile), **then** door tag 0x00 opens
    (the trap is released) and the gun stays locked; **if** he comes within 1 m of pad 166 (the right tile), **then**
    door tag 0x01 opens, and once it starts opening the gun and the ammo box can be picked up.
16. **When** the player then enters the room of pad 69, **then** the puzzle resets (rule 14).

### End
17. **When** all objectives are complete and the player is alive, **then** make him invulnerable and unable to pick up
    items, hide the HUD, freeze the controls, fade to black, and play the outro (Cinematics).

## Objects and world
- Golden gun tag 0x04 (model 208, pad 147) and its ammo box tag 0x05 (pad 148): rules 14-15, objective a.
- Puzzle doors: tag 0x00 (trap, pad 43), 0x01 (gun case, pad 54), 0x02 (pad 6), 0x03 (pad 0).
- Samedi's appearance objects tags 0x06 (pad 186) and 0x07 (pad 146), invincible until rules 8 and 9 blow them up.
  Tags 0x08-0x0A (pads 184, 185, 186) are the same kind of object; no rule uses them.
- Fog: changes to the next fog slot (slow fade) when the third form appears (rule 9).
- Autoguns stand at pads 142-144 (model 98) and 175-178 (model 299); no rule drives them.
- Sound 54 (Samedi's laugh): rules 7-9 and the outro.

## Cinematics
**Intro** (setup intro data). Played as `intro.md` describes; the data below is in its units (world units, API
degrees, seconds).

- **Fixed cameras:** 2 (one is picked at random); world units, API degrees, fov 60 (intro.md §1). Lines: 0x280E
  (location title); no second line; the shot lasts 5 s, then fades to black over 1 s.

  | Camera | Position (x, y, z) | Yaw | Pitch | Room of pad |
  |---|---|---|---|---|
  | 0 | (-4620.83, 627.27, 359.58) | 115.8 | -19.6 | 10 |
  | 1 | (2310.44, 614.34, 2733.65) | 130.0 | -18.0 | 147 |

- **Swirl points** in order (intro.md §2): every offset in the player frame (flag 2: x left, y up, z forward, from
  the eye point); no point looks at the eye (no flag 4); the end marker (flag 1) follows the last row. Duration
  and tension belong to the leg that starts at the point. Swirl length 2.334 s (the legs from P1 on: P0 is only the previous control point, intro.md section 2).

  | Point | Offset (x, y, z) | Duration (s) | Tension |
  |---|---|---|---|
  | P0 | (0, -100, 200) | 0.667 | 0.75 |
  | P1 | (10, -100, 170) | 0.667 | 0.75 |
  | P2 | (80, -20, 0) | 0.667 | 0.75 |
  | P3 | (20, 40, -100) | 0.667 | 0.75 |
  | P4 | (0, 10, -30) | 0.333 | 0.25 |
  | P5 (final) | (0, 0, 0) | not used | not used |
  | P6 (shape only) | (0, 0, 0) | not used | not used |

- **Intro animation:** no record: table entry 0 (animation 61 from frame 95, speed 0.02; intro.md §3).
- **Body during the swirl:** the table pose only; no level script animates it.
- **Start (setup data):** pad 149; item 4 (PP7), 100 of ammo type 1; outfit (cuff) 8; watch 11:45.

**Outro** (rule 17), shot by shot:
1. Hide all characters; any button press from now on fades out and ends the level.
2. Bond (third-person model) holds the golden gun (item 0x13); the first-person weapon is hidden.
3. Camera cut to the camera-position record tag 0x0B: (-495.62, 247.24, -1638.34), yaw 1.364, pitch 5.934, room of
   pad 18, looking at Bond (flag 1).
4. Bond is shown, put on pad 187 and walks to pad 190. The temple guards chrs 0x05-0x12 are removed. Fade in.
5. **When** the walking Bond comes within 1 m (100 units, 3D distance) of pad 88, **then** Samedi (body 12) appears at pad
   187, runs to pad 88 and faces Bond. The check is on the player himself: in the outro the player's own
   body is the third-person Bond that walks, so the check follows the walk. Pads 187 (213, 24, -447), 88
   (-447, 24, -450) and 190 (-1005, 24, -447) lie on one straight line, so the walk passes 3 units from
   pad 88: the trigger fires 660 units (6.6 m) into the 12.2 m walk, a little past its middle. (Samedi
   then starts at pad 187 behind Bond and runs along the same line.)
6. Fade to black, camera cut to the camera-position record tag 0x0C: (-1850.84, 107.24, -1760.45), yaw 1.659,
   pitch 0.447, room of pad 1, looking at Bond (flag 2); fade in.
7. Samedi laughs (sound 54) and plays animation 163 frames 170-296, laughing again at each repeat.
8. After 10 s fade to black; the level ends (won).

## Co-op notes
- Rules 3, 5, 15, 16 (player in a room / near a tile): any player; for rule 5 the player who entered a listed room
  most recently. The puzzle (rule 15): the first tile stepped on by any player decides.
- Rules 6, 7, 11-13 target the nearest player; "aims at him" means any player aims at Samedi.
- Rule 12 health check: the player Samedi is attacking.
- Rule 17 / outro: freeze both; one Bond model (player 0).

## API needed
- existing: `objectives.complete(id)`, `objectives.all_complete()`, `clock()`, `wait(seconds)`, `spawn(fn)`
- existing: `door(prop):open()`, `:close()`, event `door` (state "moving")
- existing: `guard(prop):is_dead()`, `:go_to(target, speed)`, `:face(target)`, `:sees_player()`, `:was_shot_at()`, `:distance()`, `:set_health(n)`, `:set_max_health(n)`
- existing: `target:near(radius)` on a pad (player within radius of a pad)
- new: `chr(n)`, `chr(n):exists()`, `chr(n):spawn_at(pad, behaviour, {force_in_view, body})`
- new: `player_in_room(pad, player?)`
- new: `guard:set_stats{armour, health, speed, accuracy, flinch, grenade_chance, invincible, no_autoaim}`; `guard:give_item(model, item, left_hand)`
- new: `guard:is_aimed_at()` a player is aiming at this guard
- new: `player_health(player)` health only, 0..1
- new: `guard:run_to_player({sprint})`, `guard:fire_at(target, {kneel, aim_only})`, `guard:fire_roll()`, `guard:fire_walking()`, `guard:fire_running()`, `guard:throw_grenade()`, `guard:sidestep()`, `guard:hop_sideways()`, `guard:run_to_side()`
- new: `guard:play_anim(anim_id, from_frame, to_frame, hold)`, `guard:anim_done()`, `guard:teleport(pad)`, `guard:remove()` (instantly), `guard:set_hidden(on)`
- new: `object(tag)`: `:set_invincible(on)`, `:destroy()`, `:set_collectable(on)`
- new: `fog.next(slow)` fade to the level's next fog slot
- new: `sound.play(sound_id, emitter_tag?)`
- new: `hud.hide()`, `player_freeze(on)`, `player_invulnerable(on)`
- new: `screen.fade_out(seconds)`, `screen.fade_in(seconds)`, `screen.fade_done()`
- new: `mission.end_level()` end now, outcome from the objectives
- new: `cinema.begin()`: `cinema.hide_all_chrs()`, `cinema.skip_on_button()`, `cinema.bond_hold(item)`, `cinema.hide_weapon()`, `camera:cut_to(tag, look_at_bond)`, `cinema.bond()` the Bond model as a guard target (show, teleport, walk to a pad)
