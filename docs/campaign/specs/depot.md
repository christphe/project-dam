# Depot

Setup file `UsetupdepoZ.c`, text bank 12 (ids 0x3000..). Bond crosses the St Petersburg rail depot, wrecks
the arms caches and the computer network, takes the safe key and the blueprints, and boards Trevelyan's
train while waves of reinforcements hunt him through the yard.

## Characters
| chr | Who | Difficulties | Starts as | Notes |
|---|---|---|---|---|
| 0x0E | guard inside the train (pad 280) | all | simple deaf generic guard (global.md) | appears in the outro if still alive (Cinematics) |
| 0x0F | guard inside the train (pad 281) | all | simple deaf generic guard (global.md) | appears in the outro if still alive (Cinematics) |
| 0x10-0x15, 0x18-0x1D | yard guards (chrs 16-21, 24-29), body 17 | all | each on its own pad (setup) | simple deaf generic guard (global.md) |
| 0x00-0x06 | reinforcements (spawned, body 17), one per spawn slot 0-6 | all | see rules 1-6 | random kit: 37% health 40, armour 20, two D5Ks (item 0x0A, model 189); 31% health 40, armour 10, grenades 80% of the time, one D5K; 32% health 40, no armour, grenades 89% of the time, one D5K |

Start: pad 272, PP7 (item 4) in the right hand, 100 rounds of ammo type 1, outfit (cuff) 0, watch 9:42.

## Objectives
| Letter | Text | Difficulties | Completed when | Failed when |
|---|---|---|---|---|
| a | 0x300F (destroy arms caches) | 00 Agent and 007 (min 2) | the objects of tags 0x0A..0x1A (17 crates) are all destroyed | never |
| b | 0x3010 (destroy computer network) | Secret Agent and up (min 1) | the objects of tags 0x01 (monitor bank), 0x02, 0x03 are destroyed | never |
| c | 0x3011 (obtain safe key) | Secret Agent and up (min 1) | the player holds the key of tag 0x00 | never |
| d | 0x3012 (recover blueprints) | Secret Agent and up (min 1) | the player holds the object of tag 0x05 (the blueprints, in the safe) | never |
| e | 0x3013 (locate Trevelyan's train) | all (min 0) | rule 7 | never |

Briefing pages (watch menu): 0x3009..0x300D.

## Rules

### Reinforcement waves
1. Reinforcements come from seven spawn slots (0-6); slot k spawns chr k. Each slot has a period: slot 0 20 s,
   slot 1 25 s, slot 2 30 s, slot 3 35 s, slot 4 60 s, slot 5 120 s, slot 6 150 s.
2. Nothing spawns during the first 20 s of the level. After that, each slot, every period: **if** its chr does not
   exist, spawning is on (rule 3), and the player is not in the exit sequence (rule 8), **then** a new guard (body 17)
   spawns at the slot's current spawn pad, even in the player's view, with the random kit of the Characters table.
   A failed spawn is retried next period.
3. Spawning is on at the start. **When** the player is in the room of any of pads 115, 230, 255, 260, 262, 151, 137,
   146, 201, 206, 214, 219, 224, 239, 245, 166, **then** spawning is off (slots wait, rule 5 applies to the guards
   already out); **when** he is in none of them, it is on again and the slots restart their periods.
4. While spawning is on, the room the player is in picks the spawn pads of slots 0-6 (the last match is kept when he is
   in none of these rooms):
   - room of pad 272 or 128: 151, 110, 115, 146, 154, 190, 186
   - room of pad 121: 272, 166, 17, 190, 195, 186, 178
   - room of pad 154: 110, 115, 272, 195, 186, 201, 219
   - room of pad 192: 121, 166, 236, 44, 45, 178, 219
   - room of pad 186: 154, 245, 195, 236, 230, 251, 239
   - room of pad 178: 44, 190, 239, 219, 232, 251, 45
   - room of pad 236: 186, 45, 239, 255, 190, 219, 44
   - room of pad 251: 232, 176, 266, 268, 268, 44, 45
   - room of pad 257: 253, 251, 232, 176, 178, 44, 45
   The start pad 272 is in the first room, so the first set applies from the start.
5. A reinforcement hunts the player: it runs to his position (sprints when he is farther than 40 m). Whenever it is
   within 20 m and sees him, or a player's shot just misses it, it attacks with one random move: grenade 4%,
   side-step 16%, firing roll 12%, fire while walking 8%, fire while running 20%, fire standing 20%, fire kneeling
   20%, and 2% of the time it takes cover instead (rule 6). After each move it attacks again while it sees him,
   else it runs after him again.
6. Cover: **when** spawning is off (rule 3) or the 2% roll of rule 5 hits, the reinforcement runs to the player's
   position; once within 20 m it kneels and aims at him, firing whenever it sees him. **When** a player's shot just
   misses it or the player is farther than 50 m, **then** it goes back to hunting (rule 5).

### The train
7. **When** the train door of tag 0x06 starts to open (any state but closed), **then** objective **e** completes.

### Exit
8. **When** the player enters the room of pad 271, if he is alive, **then** make him invulnerable and unable to pick up
   items, hide the HUD, freeze the controls and fade to black. **If** not all objectives (of the difficulty) are
   complete, the level ends (failed). **Otherwise** the outro plays (Cinematics, outro).

## Objects and world
- Arms caches: tags 0x0A-0x1A (model 3 crates). Computer network: monitor bank tag 0x01, mainframes tags 0x02,
  0x03. Safe key: tag 0x00 (model 243). Blueprints: tag 0x05, inside the safe (model 254, pad 10049).
- An autogun (model 98) sits at pad 301.
- Train doors tags 0x06 (pad 46) and 0x07 (pad 47): rule 7 and the outro.
- No alarm, fog or music changes during play; the outro plays the extra track.

## Cinematics
**Intro** (setup intro data). Played as `intro.md` describes; the data below is in its units (world units, API
degrees, seconds).

- **Fixed cameras:** 3 (one is picked at random); world units, API degrees, fov 60 (intro.md §1). Lines: 0x300E
  (location title); no second line; the shot lasts 5 s, then fades to black over 1 s.

  | Camera | Position (x, y, z) | Yaw | Pitch | Room of pad |
  |---|---|---|---|---|
  | 0 | (1654.21, 567.21, -2095.90) | -44.7 | -28.0 | 172 |
  | 1 | (4074.28, -32.78, -2285.37) | 28.8 | 20.0 | 247 |
  | 2 | (-464.20, -132.78, -139.85) | -125.3 | -2.0 | 268 |

- **Swirl points** in order (intro.md §2): every offset in the player frame (flag 2: x left, y up, z forward, from
  the eye point); no point looks at the eye (no flag 4); the end marker (flag 1) follows the last row. Duration
  and tension belong to the leg that starts at the point. Swirl length 5.515 s (the legs from P1 on: P0 is only the previous control point, intro.md section 2).

  | Point | Offset (x, y, z) | Duration (s) | Tension |
  |---|---|---|---|
  | P0 | (-93, -18, 2525) | 0.833 | 0.6 |
  | P1 | (-253, 21, 1606) | 0.917 | 0.6 |
  | P2 | (-93, -59, 900) | 0.917 | 0.6 |
  | P3 | (128, 87, 527) | 0.917 | 0.6 |
  | P4 | (253, 153, 172) | 0.833 | 0.591 |
  | P5 | (162, 133, -80) | 0.67 | 0.591 |
  | P6 | (67, 59, -174) | 0.67 | 0.431 |
  | P7 | (9, 21, -106) | 0.591 | 0.422 |
  | P8 (final) | (0, 0, 0) | not used | not used |
  | P9 (shape only) | (0, 0, 0) | not used | not used |

- **Intro animation:** no record: table entry 0 (animation 61 from frame 95, speed 0.02; intro.md §3).
- **Body during the swirl:** the table pose only. (The setup holds a swirl-start script for the body that is never
  reached.)
- **Start (setup data):** pad 272; item 4 (PP7), 100 of ammo type 1; outfit (cuff) 0; watch 9:42.

**Outro** (rule 8, all objectives complete), on the train:
1. Hide all characters; any button press from now on fades out and ends the level.
2. Bond (third-person model) keeps the firearm he holds if it is one of items 0x04-0x16; otherwise he holds a PP7
   (item 4). The first-person weapon is hidden.
3. Camera cut to the camera-position record tag 0x08: (-1762.29, -116.91, -163.31), yaw 0.014, pitch 6.269,
   room of pad 270, looking at Bond (flag 2). The camera stays there for the whole outro.
4. Train doors tags 0x06 and 0x07 snap open. Train guards take part only if alive and in the room of pad 273, 280 or
   277: guard 0x0E and/or guard 0x0F.
5. If at least one train guard takes part: Bond becomes invincible, can hit other characters, and is put on pad 276.
   - Guard 0x0F (if present): health 10, no armour, put on pad 274, idle pose (animation 0, held); after 1 s it
     fires at pad 278.
   - Guard 0x0E: if alone, health 10, no armour, put on pad 275, idle pose; after 1.2 s it fires at pad 278. If 0x0F
     is also present, it is put on pad 279 in an idle pose (no movement), waits for Bond to finish 0x0F, then plays
     animation 42 (held) and 0.3 s later fires at pad 278.
   - Fade in from black and play the extra track (slot 0).
   - If 0x0F is present, Bond aims at it (standing), fires, and 0.05 s after his shot goes off it takes a PP7 hit
     (item 4) to body part 15, the chest (2x damage). Then, if 0x0E is present, as soon as Bond's firing
     animation on 0x0F has finished (no fixed timer: the pace is the aim and fire animations), he kneels, aims
     at 0x0E, fires, and 0.05 s after the shot it takes the same hit (item 4, part 15). Fade to black as soon as
     Bond's last firing animation has finished.
   - Fade to black.
6. Final shot (with or without train guards): hide all characters, show Bond again, put him on pad 273, remove his
   left-hand item, train doors 0x06 and 0x07 snap open, he plays animation 105 frames 0-29 (held) while the screen
   fades in, removes his right-hand item, plays animation 105 frames 29-160 (held).
7. When that animation ends (or after 2 s), door 0x06 closes; when it is closed (or after 3 s) fade to black and
   the level ends (won).

## Co-op notes
- Rules 3 and 4 (player's room): with two players, use the player who entered a listed room most recently, or
  player 0; rule 3 off-rooms: spawning is off while any player is in one.
- Rules 5-6 target the nearest player.
- Rule 8: the first player to reach the room of pad 271; freeze both.
- Outro: a single Bond model; use player 0's weapon for step 2.

## API needed
- existing: `objectives.complete(id)`, `objectives.all_complete()`, `mission.difficulty()`
- existing: `door(prop):open()`, `:close()`, `:is_open()`, event `door` (state "moving"/"open")
- existing: `guard(prop):is_dead()`, `:pos()`, `:distance()`, `:sees_player()`, `:was_shot_at()`, `:go_to(target, speed)`, `:set_health(n)`, `:set_max_health(n)`
- existing: `wait(seconds)`, `spawn(fn)`, `clock()`
- new: `chr(n)` the character with chr number n (setup or spawned); `chr(n):exists()`
- new: `chr(n):spawn_at(pad, behaviour, {force_in_view, body})` spawn a guard taking chr number n
- new: `player_in_room(pad, player?)` a player is in the room of pad; `chr(n):in_room(pad)`
- new: `guard:set_stats{armour, health, grenade_chance, invincible, can_shoot_chrs, speed}`; `guard:give_item(model, item, left_hand)`; `guard:remove_held_item(hand)`
- new: `guard:run_to_player({sprint})`, `guard:throw_grenade()`, `guard:sidestep()`, `guard:fire_roll()`, `guard:fire_walking()`, `guard:fire_running()`, `guard:fire_at(target, {kneel, aim_only})` target = player, chr or pad
- new: `guard:teleport(pad)`, `guard:play_anim(anim_id, from_frame, to_frame, hold, no_translation)`, `guard:anim_done()`, `guard:set_hidden(on)`
- new: `guard:take_hit(item, body_part)` deal one weapon hit as if shot
- new: `door(tag):open_instant()`; door state query `door(tag):state()` "closed" / "opening" / "open" / "closing"
- new: `object(tag)` by setup tag, for the objective objects
- new: `hud.hide()`, `player_freeze(on)`, `player_invulnerable(on)` (no damage, no pickups), `player_held_item(player)`
- new: `screen.fade_out(seconds)`, `screen.fade_in(seconds)`, `screen.fade_done()`
- new: `mission.end_level()` end now, outcome from the objectives
- new: `music.play_extra(slot, seconds)`
- new: `cinema.begin()`: `cinema.hide_all_chrs()`, `cinema.skip_on_button()`, `cinema.bond_hold(item)`, `cinema.hide_weapon()`, `camera:cut_to(tag, look_at_bond)`, `cinema.bond()` the Bond model as a guard target
