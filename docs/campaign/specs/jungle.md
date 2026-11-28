# Jungle

Setup file `assets/obseg/setup/u/UsetupjunZ.c`, text bank 18 (ids 0x4800..). Bond and Natalya cross the
Cuban jungle to the Janus base: destroy the drone guns, kill Xenia, blow up the ammo dump (Secret Agent and
up), and bring Natalya alive to the base entrance.

Distances are world units. Health and armour are game units as the mission API counts them (a plain guard
has about 4); on 007 the game's own health modifier multiplies them, as it does for every guard.

## Characters
| chr | Who | Difficulties | Starts as | Notes |
|---|---|---|---|---|
| 0x00 | Xenia (body 11), pad 567 | all | waits, vision 50 | health 10, armour 40, top speed / accuracy / hit reaction ratings (100), no auto-aim on her. Her own fight behaviour: rules 3-7 |
| 0x01 | Natalya (body 79), pad 515 | all | companion | health 8, armour 8, no auto-aim on her, may shoot guards. Companion behaviour: rules 8-17 |
| 0x02-0x24 (2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36) | jungle soldiers on their own pads (2: 350, 3: 351, 4: 491, 5: 568, 6: 493, 7: 575, 8: 495, 9: 569, 10: 576, 11: 498, 12: 499, 13: 570, 14: 501, 15: 571, 16: 503, 17: 504, 18: 505, 19: 572, 20: 573, 21: 508, 22: 509, 23: 510, 24: 574, 27: 469, 28: 471, 29: 577, 30: 579, 31: 477, 32: 479, 33: 481, 34: 578, 35: 485, 36: 487) | all (setup) | stand guard | the jungle guard of rules 18-22; clone on gunfire. Chrs 7, 10, 24, 29, 30, 34 throw grenades (grenade probability 0xCC, 80%) |
| 0x25, 0x26, 0x27, 0x28, 0x29 and 0x19, 0x1A (37, 25, 26, 38, 39, 40, 41; pads 580, 581, 582, 584, 585, 586, 587) | posted riflemen | all (setup) | hold their spot | the post guard of rule 23: armour 2, never move |
| 0x1D, 0x1E, 0x22, 0x27, 0x28, 0x29 (respawned) | base reinforcements | all | spawned at pad 467 | rules 24-28; they reuse the chr numbers of guards 29, 30, 34, 39, 40, 41. Body 0 (camguard, the jungle commando) always; the head is the game's random male head (one of heads 42-59, 62-68, the generic-guard pick) |

(Who is at which difficulty is the setup's load gating, not the scripts'.)

## Objectives
| Letter | Text | Difficulties | Completed when | Failed when |
|---|---|---|---|---|
| a | 4825 (destroy drone guns) | all | the drone guns of tags 0, 1, 2, 9, 10, 11, 12 are all destroyed | |
| b | 4826 (eliminate Xenia) | all | rule 2: Xenia is dead | |
| c | 4827 (blow up ammo dump) | Secret Agent and up | the objects of tags 20-28 are all destroyed | |
| d | 4828 (escort Natalya to base) | all | rule 29: Bond reaches the base entrance with Natalya alive | rule 17: Natalya dies |

Briefing pages: 4820, 4821, 4822, 4823, 4824 (watch-menu texts 0-4).

## Rules

### Xenia (chr 0x00)

1. **When** Xenia is in Bond's line of sight **and** on screen for the first time, **then** show 481E (Xenia
   taunts Bond) at the top of the screen, start the extra music track 0 (her theme) and she begins to fight
   (rule 3).
2. **When** Xenia dies or is removed, **then** stop the extra music track 0 and complete objective b.
3. **Fight loop.** Xenia decides again after every move: with 24% chance she first checks whether Bond is
   aiming at her, and if he is she evades (rule 6); otherwise, if she has line of sight to Bond she attacks
   (rule 4), else she runs towards him (rule 5).
4. **Attack.** Half the time she first checks whether Bond aims at her (then evades, rule 6, and comes back
   to attack). If Bond's health is below 25% she fires while running at him. Otherwise one move at random:
   firing roll, fire while walking, fire while running, fire standing, fire kneeling (16% each), aim
   kneeling (10%), aim standing (12%). When the move ends she decides again (rule 3).
5. **Chase.** She runs to Bond's position; as soon as she stops or has line of sight she decides again
   (rule 3); if Bond aims at her on the way she evades (rule 6) and then resumes the chase.
6. **Evade.** One of: hop sideways (24%), sidestep (24%), run to the side (24%), else a dodge animation
   (animation 0x0B, frames 20-60). When it ends she returns to what sent her (attack or chase).
7. Xenia does not use the generic guard behaviour at any point.

### Natalya (chr 0x01)

8. **When** the intro (fixed cameras and the swirl) is over, **then** show 481C (Natalya line: ready to move) and she
   starts following Bond (rule 9).
9. **Follow.** Natalya moves slowly (speed rating 10). While Bond is farther than 1500 she runs to his
   position, re-checking as she goes; she stops chasing when she arrives or comes within 750. Once close:
   - half the time she checks for enemies (rule 11, then rule 10 when the check finds nothing to do);
   - otherwise she wanders: she picks a spot near Bond (in his direction or to a side) and runs to it; when
     she arrives she checks for enemies (rule 11); if Bond gets farther than 2500 meanwhile she goes back to
     chasing him. If no such spot exists she stands and looks around (rule 16).
10. **Chatter.** (Run after each check for enemies that ends without a fight, and after a kill.) If Bond's
    health is below 20% and she has not warned him yet this level, show 481D (Natalya line: a warning) once.
    Otherwise, with 24% chance she says one quip picked at random from 4808-481A (19 short boasts and
    remarks, about 4% each; nothing 25% of the time). Then she follows again (rule 9).
11. **Look for a target.** If Bond is dead she makes no new pick and goes straight to rule 13 against the
    target she picked last (the guard of her previous engagement): if that guard is already dead she only
    chatters (rule 10); if she has never picked one, nothing is engaged and she follows again. She picks the first guard within
    1000 and engages it up close (rule 13); else the first guard within 2000 or else within 3000 and engages
    it at range (rule 12); if there is none within 3000 she looks around (rule 16).
12. **Ranged engagement.** If the target is not in her room she goes back to following (rule 9). Otherwise
    she aims at it, 50/50 standing or kneeling, and marks it (the guard now knows Natalya is after it,
    rule 20). After 1 s she fires. If the target dies while she aims or fires: chatter (rule 10). When the
    burst ends: 50% she starts again at rule 11, else she unmarks the target and follows (rule 9). If she
    cannot aim she just turns to face the target for 1 s, then retries if it is still in her room, else
    follows.
13. **Close engagement.** She moves a little faster (speed rating 20) and marks the target. Half the time she
    first plays a dodge animation (0x0B, frames 20-60). She aims at it kneeling; after 1 s (chatter, rule 10,
    if it died meanwhile):
    - if the target is within 250, she waits for her aim to settle and fires; if the shot goes off the target
      also takes a sure hit from her weapon (a close-range finisher), then she follows (rule 9);
    - otherwise she fires; after the burst, chatter if it died, else 50% she engages again (rule 13), 50% she
      unmarks it and follows.
14. Natalya never targets Bond, and never runs the generic guard behaviour.
15. Natalya's text lines are top-of-screen messages, not voiced.
16. **Look around.** She stands still (speed 0) and, at random: plays a looking animation 0x96 or 0x97
    (frames 0-86, 24% each), animation 0x53 (frames 0-34, 5%), animation 0x54 (frames 0-36, 5%), animation
    0x63 (frames 0-149, 5%); otherwise she picks a nearby spot on the way to Bond (fallback pad 514) and
    aims at it kneeling (8%), aims at it standing (8%) or just faces it. She keeps the pose about 2 s (or
    until the animation ends), then follows (rule 9).
17. **When** Natalya dies or is removed, **then** show 481F (mission failure: Natalya killed) and fail
    objective d (the mission is then lost).

### Jungle guards (chrs on 0x0D lists: see the Characters table)

18. **Idle.** A guard stands at its post. Now and then (about 0.8% per frame) it plays an idle animation
    (the generic idle, global.md). When it dies it stays dead.
19. **Alert.** When the guard sees Bond: attack (rule 20). When it was shot at or saw Bond in the last 10 s:
    hunt (rule 21). When it heard Bond in the last 10 s, or saw another guard shot or killed: it behaves as
    the try-clone-or-run-to-Bond generic guard (global.md), then comes back here. When a shot from Bond just
    missed it: 50% attack, 50% look around first (rule 22).
20. **Attack.** The first time, the guard either points at Bond and shouts (12.5%) or tries to throw a
    grenade (the rest; only guards with a grenade chance throw). Then, if Natalya is alive, 39% of the time
    it checks her: if she is within 800 or she has marked this guard (rules 12-13) and she is in its room,
    it turns on Natalya (rule 20a). Else, if Bond is farther than 1500 it hunts (rule 21); otherwise one
    attack move at random: firing roll (4%), hop sideways (4%), sidestep (9%), run to the side (16%), fire
    while walking (17%), fire while running (16%), else (34%) fire standing or kneeling (50/50). A move that
    is impossible where the guard stands falls through to the next one. Then back to
    rule 19.
    - 20a. **Fight Natalya.** Half the time the guard is set to be able to hit other characters. It aims at
      Natalya (50/50 standing or kneeling), marks itself as her attacker, fires after 1 s and keeps firing
      until the burst ends; if it cannot aim it faces her for 1 s. Then it returns to its attack (rule 20).
      If Natalya dies meanwhile, it hunts Bond (rule 21).
21. **Hunt.** The guard runs to Bond's position; if it sees Bond within 1500 on the way it attacks
    (rule 20); when it arrives it goes back to rule 19.
22. **Look around.** The guard looks around on the spot, then hunts (rule 21).

### Post guards (chrs 0x19, 0x1A, 0x25-0x29) and reinforcements at their post

23. The guard has armour 2 and never leaves its spot. It keeps its gun aimed at Bond. When a shot from Bond
    misses it, it sees another guard shot or killed, it sees Bond, or Bond is in its line of sight: 59% it
    fires from where it stands; otherwise it kneels and waits, popping up to fire at a random moment (about
    2% per frame, so typically within a second, at most 4 s), then aims again. This repeats for as long as
    it lives.

### Base reinforcements (respawning guards near the base)

24. **Spawning starts when** Bond enters the room of pad 456. While it runs, every 2 s, for each of six
    slots whose guard is gone and whose slot is not shut (rule 27), and only while Bond is not in a
    cutscene: a new soldier appears at pad 467. The slots are the chr numbers 0x27, 0x28, 0x29, 0x1D, 0x22,
    0x1E: each slot is refilled only once the guard holding that number (first the original guard, later
    the previous reinforcement) is dead or gone.
25. **Spawning stops when** Bond enters the room of pad 466 or the room of pad 441, and starts again (with
    all six slots open) the next time he enters the room of pad 456.
26. **Each reinforcement** gets its slot's chr number, a green combat helmet (hat model 217) and an AR33
    (item 13, model 188), and sprints to its post: slot 0x27 pad 585 (78%) or 459 (22%), 0x28 pad 586 or
    458, 0x29 pad 587 or 455, 0x1D pad 577 or 456, 0x22 pad 578 or 457, 0x1E pad 583 or 452 (always 78% /
    22%). If Bond comes within 1000 on the way it hunts him (rule 21, then the jungle guard of rules
    18-22); if it reaches its post (within 100) it becomes a post guard (rule 23). If it
    stops short of the post (no route, or blocked), it re-issues the sprint to the same post at once, so it
    stays where it is, not firing, until it gets there or Bond comes within 1000 (then the hunt above); it
    never gives up the post.
27. **Ramp-down.** 180 s after spawning starts, slots 0x1D, 0x22 and 0x1E shut; 600 s after, slots 0x27,
    0x28 and 0x29 shut and spawning stops (until Bond enters the room of pad 456 again, which reopens every
    slot). Leaving through rule 25 also resets the timer.
    - Original quirk: the original marks slot 0x1D shut with the same state as objective d's completion, so
      objective d shows complete 180 s into spawning (and the next restart clears it again). Do not
      reproduce: objective d completes only by rule 29.

### Base entrance and the end

28. Reinforcements are not spawned while the end scene runs (rule 24).
29. **When** Bond (alive) enters the room of pad 468, **then** complete objective d, make Bond immune to
    damage and pickups, hide the HUD, lock the controls and stop the mission clock, and fade to black. Once
    black:
    - if Natalya is dead or gone, end the level (it is lost through objective d);
    - else if not every objective of the difficulty is complete, end the level (lost);
    - else play the end scene (Cinematics: outro).

### Scenery

30. 10 s, 12 s and 13 s after the level starts, the small wooden crates of tags 3, 4 and 5 (model 82, on
    pads 10003, 10002, 10004) are made destructible and destroyed (they explode), one after the other.

## Objects and world
- Drone guns (tags 0, 1, 2, 9, 10, 11, 12; pads 517-523): objective a.
- Ammo dump: the ammo crates of tags 20-28 (pads 556-564): objective c.
- Base doors: tag 0x1F (door 565) and tag 0x20 (door 566). Both snap open at the start of the outro; door
  0x1F closes again 1.5 s after the outro's aiming beat.
- Music: extra track 0 plays from Xenia's first sighting until her death; extra track 1 plays from the
  start of the outro.
- No alarm, fog, TV, vehicle or aircraft script.

## Cinematics

### Intro (setup intro data)
Played as `intro.md` describes; the data below is in its units (world units, API degrees, seconds).

- **Fixed cameras:** 3 (one is picked at random); world units, API degrees, fov 60 (intro.md §1). Lines: 0x4829
  (location title); no second line; the shot lasts 5 s, then fades to black over 1 s.

  | Camera | Position (x, y, z) | Yaw | Pitch | Room of pad |
  |---|---|---|---|---|
  | 0 | (-257.41, 1016.96, 129.32) | 3.1 | -34.0 | 514 |
  | 1 | (5629.52, 826.74, -10144.17) | 143.5 | 18.0 | 520 |
  | 2 | (17940.40, 883.71, -9857.71) | -27.0 | 10.0 | 516 |

- **Swirl points** in order (intro.md §2): every offset in the player frame (flag 2: x left, y up, z forward, from
  the eye point); no point looks at the eye (no flag 4); the end marker (flag 1) follows the last row. Duration
  and tension belong to the leg that starts at the point. Swirl length 3.975 s (the legs from P1 on: P0 is only the previous control point, intro.md section 2).

  | Point | Offset (x, y, z) | Duration (s) | Tension |
  |---|---|---|---|
  | P0 | (599, -107, 653) | 1 | 0.346 |
  | P1 | (593, -106.5, 644) | 1 | 0.615 |
  | P2 | (536, -106, 583) | 0.833 | 0.75 |
  | P3 | (224, -95, 320) | 0.74 | 0.546 |
  | P4 | (156, 8, -9) | 0.583 | 0.546 |
  | P5 | (41, 17, -78) | 0.486 | 0.531 |
  | P6 | (0, 10, -30) | 0.333 | 0.209 |
  | P7 (final) | (0, 0, 0) | not used | not used |
  | P8 (shape only) | (0, 0, 0) | not used | not used |

- **Intro animation:** no record: table entry 0 (animation 61 from frame 95, speed 0.02; intro.md §3).
- **Body during the swirl (a level script, not the intro-animation record):** from the swirl start, animation 103
  frames 0 to 1 for 1 s, then frame 1 to its end, held at the last frame (blend 16 ticks).
- **Start (setup data):** pad 514; item 4 (PP7), 100 of ammo type 1, item 29 (remote mine), item 30 (detonator), 6
  of ammo type 7; outfit (cuff) 2; watch 2:15.
- Natalya speaks at the hand-over (rule 8).

### Outro (after rule 29, all objectives complete)
1. While black: hide every character, let a button press skip to the end (fade and exit), snap doors 0x1F
   and 0x20 open. If Bond holds no gun (PP7 through Moonraker laser, items 4-22), give his model the PP7
   for the scene. Hide Bond's first-person weapon.
2. Cut to the fixed camera of tag 0x1E (the camera record at (19663.6, 2303.1, -2972.5), room of pad
   467), not tracking Bond.
3. Natalya (unhidden, invincible) is placed at pad 591 and plays animation 0x5A from frame 7; Bond's model
   (invincible) is placed at pad 592 and plays animation 0x5A; if he has no gun in hand his hands are
   emptied and a PP7 (model 191) is put in his hand. Fade in from black and start extra music track 1.
4. Once Natalya is in the room of pad 468: about 0.2 s later she kneels and aims at pad 592; 0.5 s later
   Bond's model aims at pad 591.
5. 1.5 s after Bond aims, door 0x1F closes; 2.5 s after he aims, fade to black and the level ends (won).

## Co-op notes
- Rule 1 (Xenia's first sighting): any player.
- Rules 3-6: Xenia fights the nearest player; "Bond's health" (rule 4) is the targeted player's.
- Natalya (rules 9-16) follows the nearest living player; her low-health warning (rule 10) fires for any
  player below 20%, once.
- Rules 24-25: any player entering the rooms starts or stops spawning; rule 26's "Bond within 1000" is the
  nearest player.
- Rule 29: the first player to reach the room of pad 468 ends the level for both (or require both: a
  choice for the Luau writer; the original has one player).
- Outro: one Bond model; in co-op show player 1's.

## API needed
- existing: `guard(n):is_dead()`, `guard(n):follow(player?)`, `guard(n):stop()`, `guard(n):go_to(target, speed)`,
  `guard(n):face(target)`, `guard(n):sees_player()`, `guard(n):was_shot_at()`, `guard(n):distance()`,
  `guard(n):distance_to(other)`, `guard(n):set_health(n)`, `guard(n):set_max_health(n)`,
  `objectives.complete(id)`, `objectives.fail(id)`, `objectives.all_complete()`, `mission.win()`,
  `mission.fail()`, `mission.difficulty()`, `message(text)`, `wait(seconds)`, `event(name, filter)`,
  `on(name, fn, filter)`, `spawn(fn)`, `clock()`, `door(prop):open()`, `door(prop):close()`,
  `player(n):near(radius)`.
- new: `text_top(text_id)` show a game text id at the top of the screen (Natalya's and Xenia's lines).
- new: `music.play_extra(slot)` / `music.stop_extra(slot)` start / stop one of the level's extra music
  tracks over the main track.
- new: `event("intro_done")` (or `intro.is_running()` / `intro.in_swirl()`) the intro cameras and swirl
  have ended / the swirl is running.
- new: `player(n):in_room_of(pad)` and `guard(n):in_room_of(pad)` the player / guard is in the room that
  holds pad; `guard(n):in_room_with(other)` two characters share a room.
- new: `player(n):health()` 0..1 health (armour left out) of a player.
- new: `player(n):is_aiming_at(guard)` the player's crosshair is on the guard.
- new: `guard(n):in_sight_of_player()` / `guard(n):on_screen()` line of sight to a player / visible on a
  player's screen.
- new: `guard(n):set_armour(n)`, `guard(n):set_ratings{speed=, accuracy=, reaction=}`,
  `guard(n):set_autoaim(on)`, `guard(n):set_invincible(on)`, `guard(n):set_can_hit_guards(on)`,
  `guard(n):set_grenade_chance(p)`.
- new: `guard(n):attack_move(kind, target)` one combat move against a player, a guard or a pad: kind one of
  "roll", "walk_fire", "run_fire", "fire", "kneel_fire", "aim", "kneel_aim", "hop", "sidestep",
  "side_run", "point", "grenade"; `guard(n):fire_now()` fire from the current aim; returns false when the
  move is impossible; `guard(n):is_busy()` the move / animation is still running.
- new: `guard(n):play_anim_id(anim, from_frame, to_frame, hold)` play a native animation id (frames at 30
  per second); `guard(n):look_around()`.
- new: `guard(n):nearest_guard(radius)` the first other guard within radius, or nil.
- new: `guard(n):spot_near_player(kind)` a pad near the player in his direction or to his side, or nil.
- new: `guard(n):hit_with_weapon(other)` a sure hit on another character with the held weapon.
- new: `guard(n):kneel()`, `guard(n):idle_anim()`.
- new: `guard(n):heard_player(seconds)`, `guard(n):alerted_within(seconds)` (shot at or saw a player),
  `guard(n):saw_guard_hit()` / `saw_guard_die()`.
- new: `guard(n):set_generic(kind)` hand the guard to a generic behaviour of global.md by name
  ("try_clone_or_run", "standard", "idle_anim", ...).
- new: `spawn_guard{chr=, pad=, body=nil, head=nil, ai=fn}` a new soldier at a pad taking a given chr
  number (body 0, the game's random male head); `guard(n):give_hat(model)`, `guard(n):give_weapon(item, model)`.
- new: `object(tag):destroy()` blow up a tagged object (also making it destructible); `door(tag):open(instant)`.
- new: `player(n):set_cutscene_mode(on)` immune to damage and pickups, HUD hidden, controls locked,
  mission clock paused; `player(n):in_cutscene()`.
- new: `screen.fade_out()` / `screen.fade_in()` (wait until done).
- new: `camera.cut_to(tag, track_player)` to a tagged fixed camera; `camera.end_on_button()` a button press
  fades and ends the level.
- new: `cinema.hide_all_chrs()`, `cinema.bond()` Bond's third-person model as a guard-like target
  (place at pad, play_anim_id, aim_at pad, set held item), `player(n):hide_weapon()`,
  `player(n):holds_gun()`, `guard(n):place_at(pad)`, `guard(n):show()`.
- new: `guard(n):aim_at_pad(pad, kneel)`.
- new: `mission.end_level()` end now; the result follows the objectives.
