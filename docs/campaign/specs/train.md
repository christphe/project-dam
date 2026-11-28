# Train

Setup file `u/UsetuptraZ.c`, text bank 36 (ids 0x9000..). Bond works through the armoured train, destroys
the brake units, reaches the rear carriage where Trevelyan, Xenia and Ourumov hold Natalya, survives the
standoff, lets Natalya trace the Janus base and crack the password, and escapes through the floor before the
carriage blows.

## Characters
| chr | Who | Difficulties | Starts as | Notes |
|---|---|---|---|---|
| 0x03, 0x04, 0x06, 0x0e, 0x11, 0x16, 0x1c, 0x27, 0x2a, 0x2f, 0x43 | carriage guards (pads 5, 6, 8, 16, 19, 24, 30, 124, 44, 49, 69) | all | rule 1 (stand and fight) | body 17 |
| 0x00, 0x01, 0x02, 0x05, 0x0d, 0x0f, 0x41, 0x32, 0x37 | armoured guards (pads 2, 3, 4, 7, 15, 17, 0, 75, 80) | all | rule 2 (aim and fire) | body 17, armour 2.0 |
| 0x12, 0x1e | ambushers, pads 20, 32 | all | rule 3, post pad 159 | body 17 |
| 0x13, 0x1b, 0x1d | ambushers, pads 21, 29, 31 | all | rule 3, post pad 163 | body 17 |
| 0x1f | ambusher, pad 33 | all | rule 3, post pad 169 | body 17 |
| 0x20, 0x24 | ambushers, pads 34, 38 | all | rule 3, post pad 136 | body 17 |
| 0x22, 0x2b, 0x2d | ambushers, pads 36, 45, 47 | all | rule 3, post pad 127 | body 17 |
| 0x26, 0x2e | ambushers, pads 40, 48 | all | rule 3, post pad 140 | body 17 |
| 0x33 | guard at pad 76 | all | simple guard deaf generic guard (global.md) | body 17 |
| 0x42, 0x44 | heavy guards, pads 83, 82 | all | rule 5 | body 4, health 6.0, armour 4.0; 0x42 carries the train door key (key 167, opens the door tag 14) |
| 0x45 | Trevelyan, pad 71 | all | rules 12-16 | body 8; the player cannot auto-aim at him |
| 0x47 | Ourumov, pad 73 | all | rules 17-18 | body 7; no auto-aim; holds a TT-33 (item 6) |
| 0x48 | Xenia, pad 72 | all | rule 19 | body 11; no auto-aim; holds item 9 |
| 0x46 | Natalya, pad 74 | all | rules 20-26 | body 16; no auto-aim; a hostage with hands up |

All setup guards: random head, hearing 1000, vision 100. Trevelyan holds item 9 (setup data).

## Objectives
| Letter | Text | Difficulties | Completed when | Failed when |
|---|---|---|---|---|
| a | 0x9022 (destroy every brake unit) | all | brake units tag 8, 9, 10, 11, 12, 13 all destroyed | - |
| b | 0x9023 (rescue Natalya) | all | rule 29 (she is alive at the exit) | rule 18 or rule 26 |
| c | 0x9024 (find the Janus base) | Secret Agent and up | rule 23 (50 s line) | - |
| d | 0x9025 (crack Boris's password) | 00 Agent, 007 | rule 23 (75 s line) | - |
| e | 0x9026 (escape to safety) | all | rule 28 | - |

Briefing pages: title 0x901D, overview 0x901E, M 0x901F, Q 0x9020, Moneypenny 0x9021.

## Rules

### Guard behaviours
Combat move table used below ("the carriage moves"): 10% sidestep, 23% fire while walking, 22% fire while
running, 22.5% fire standing, 22.5% fire kneeling; a move that cannot be done now falls through to the next.

1. **Stand and fight**: the guard stays where it is. **When** a player's shot just missed it, it sees another
   guard shot or killed, it sees the player, or it has line of sight to a player, **then** it fights with the
   carriage moves, one after another, for good.
2. **Aim and fire** (armour 2.0): the guard keeps its gun aimed at the player without firing. **When** the
   same triggers as rule 1 happen, **then** 59%: it fires at the player, then goes back to aiming; 41%: it
   kneels and waits up to 4 s, popping up to aim and fire at a random moment (about 2% per tick), then goes
   back to aiming. Repeats while triggered.
3. **Ambusher** (guard with a post pad): waits. **When** it has line of sight to a player, a player comes
   within 200 units of its post, or (tested every 3 s) a player is within 2000 units of its post, **then** it
   becomes a hunter (rule 4).
4. **Hunter**: runs at the player's position; on line of sight it fights with the carriage moves, and keeps
   fighting while it sees the player; if the player gets more than 2000 units away it runs at him again. When
   it loses sight after a move: 22% it stays and waits for sight; 78% it runs toward the player for up to 1 s,
   then holds for up to 10 s, fighting at once if it sees the player or is shot at; after the 10 s, 22% it
   repeats this short probe, 78% it runs at the player again. A hunter made invincible by another rule loses
   the invincibility once it has been hit 6 times.
5. **Heavy guards** (chr 0x42, 0x44): idle until objective a is complete, then hunters (rule 4).

### Reinforcements (all spawned guards: body 4, random head, only at pads no player can see; a spawn that
### fails is not retried; none spawn once the exit scene, rule 29, has started)
6. **When** a player first comes within 1000 units of pad 175, **then** one guard spawns at pad 10 (39%),
   pad 12 (39%) or pad 13 (22%), and one at pad 9 (39%) or pad 11 (61%). Each: health 8.0, a hat (model 220),
   an MP5K (item 10); then 22% run to the player and behave as the standard guard generic guard (global.md),
   78% stand and fight (rule 1).
7. **When** after that a player enters the room of pad 144, **then** spawn:
   - at pads 65, 58, 59 and 53 one guard each, each with a random loadout (20%: health 4.0, armour 2.0, MP5K
     item 10, hat 220; 20%: health 4.0, armour 4.0, two item 9, hat 222; 61%: health 6.0, armour 4.0, two
     item 9, hat 221) and an ambusher post (rule 3): pad 67 for the one from pad 65, pad 120 for the other
     three;
   - four more, each at one pad picked at random: {54, 61, 66}, {55, 62, 64}, {56, 63, 52} (31% / 31% / 38%),
     {57, 60} (47% / 53%). Each: 53% health 8.0, two item 9, hat 222; 47% health 6.0, MP5K item 10, hat 220;
     then stand and fight (rule 1).

### The train
8. While the train runs, a clatter (sound 0x40) plays every second; once objective a is complete a braking
   sound (0x3F) plays with it.
9. Every 2 to 3 s (random) a creak plays: sound 0xF9 (39%), 0xDF (30%) or 0x33 (31%).
10. **When** objective a completes, **then** the regular creaks of rule 9 end and two last creak chances
    follow: 2 s later a creak plays with 84% chance, 2 s after that one plays with 68% chance (each creak
    picked as in rule 9: 0xF9 39%, 0xDF 30%, 0x33 31%). Right after the second chance (4 s after a) the
    train stops: sound 0x3D plays once and the clatter and braking end.
11. The carriage doors tag 30..40 carry a lock bit (2) no key provides; whenever one of them is closed the
    level removes that bit, so they always open normally. (Door tag 14 needs the train door key, setup data.)

### The rear carriage standoff
12. **When** Trevelyan has line of sight to a player, or a player enters the room of pad 94, **then** show
    0x9010 (Trevelyan line: greets Bond aboard) at the top and extra music slot 0 starts (up to 255 s).
13. **When** then a player enters the room of pad 83, **then** the standoff starts: show 0x9009 (Trevelyan:
    stop right there). At 10 s 0x900C (Trevelyan line: offers a choice); at 20 s 0x900A (Trevelyan line); at 30 s 0x9019 (Trevelyan line: mocks Bond) and the fight starts (rule 15). All three times count from the start of the standoff (one clock), not from the previous line.
14. **During** the standoff: **when** a player comes within 250 units of pad 82, **then** 0x900F (Trevelyan:
    kill them) and the fight starts; **when** a player enters the room of pad 88 (backing out), **then**
    0x9011 (Trevelyan line: parting taunt) and the fight starts.
15. **The fight starts** also as soon as a player's shot misses or wounds Trevelyan, Xenia or Ourumov (before
    or during the standoff). Then Trevelyan shows 0x900D (Trevelyan line: a taunt) and fires once at the
    player, then stands.
16. **When** the fight starts, **then** 1.25 s later (at once if a player is within 200 units of pad 82, or in
    the room of pad 88 after the standoff began) the carriage doors tag 0 and 1 close; 3 s after that the
    doors tag 2..7 and 46 close. **When** door tag 1 is fully closed, **then** Trevelyan and Xenia become
    invincible and hidden and are moved to pad 186 (they escape).
17. **Ourumov** keeps Natalya covered from the start (aiming, not firing). **When** the fight starts, **then**
    he keeps aiming at her and, as soon as his aim is set, fires at her once; after that he keeps covering
    her. **When** Natalya is dead or gone, **then** he runs to the player and behaves as the standard guard
    generic guard (global.md). Killing him before he fires saves her.
18. **When** Ourumov fires at Natalya (rule 17), **then** she takes a TT-33 (item 6) hit to body part 8 and
    objective b fails; she does nothing more.
19. **Xenia** waits; **when** the fight starts, **then** she fires once at the player; **when** she is dying,
    **then** show 0x900E (Xenia line: wounded).

### Natalya at Boris's computer (times from the start of the fight)
20. **When** Ourumov is dead or gone and Natalya was not shot, **then** she lowers her hands and looks around.
21. At 20 s she walks to pad 81 (the computer); whenever no player can see her on the way, she is moved there
    at once.
22. **While** she stands at the computer she types: one of animations 172 (39%), 173 (20%), 174 (20%), 175
    (22%), each played over its first 69 / 74 / 79 / 89 frames; each time, 1 s later a keyboard sound 0xB8
    plays from the computer (tag 41) with 50% chance.
23. At 40 s she shows 0x9014 (Natalya line: reports the enemy at work); at 50 s 0x9015 (Natalya line: reveals the enemy base) and
    objective c completes. On 00 Agent and 007 only: at 65 s 0x9016 (Natalya line: asks for time); at 75 s
    0x9017 (Natalya line: success) and objective d completes.
24. **When** the computer (tag 41 or tag 42) is destroyed while she works, **then** she stops working and
    turns to face the player; **if** the floor countdown (rule 27) has not run out, **then** she shows
    0x901A (Natalya line: the computer is lost), and 5 s later 0x901B (Natalya line: the trail is lost).
25. **When** a player enters the room of pad 98 (while she works or after rule 24), **then** she shows
    0x9018 (Natalya line: on her way) and follows: runs to the player's position until within 250 units, stays while
    the player is within 400 units, runs again beyond that. When the player is within 80 units and in front
    of her she briefly raises her arms, otherwise she looks around.
26. **When** Natalya is dead and gone, **then** show 0x901C (Natalya killed) at the top; objective b fails.

### The floor countdown and the escape
27. **When** the fight has started, **then** after 15 s (30 s if Xenia is dead and Natalya alive) show 0x9012
    (Trevelyan line); 5 s later 0x9013 (Trevelyan line: time left) and a 60 s countdown on the HUD.
    At 30 s left: sounds 0xD6 and 0xD8 from pad 71; at 23 s left: 0xD8 stops, 0xD7 plays from pad 71; at
    20 s left: sound 0x3A plays and fades out over 60 s. **When** the countdown runs out, **then** it hides
    and the floor explodes: the charges tag 15 at 0.5 s, tags 16 and 43 at 1.5 s, tag 17 at 3.5 s, tags 18 and
    44 at 7.5 s, tag 19 at 9.5 s, tag 20 at 10.5 s, tags 21 and 45 at 14.5 s (each is indestructible until
    then; destroying it makes its explosion).
28. **When** a player enters the room of pad 98, **then** the fog switches at once to the level's next fog and
    objective e completes.
29. **When** a player comes within 500 units of pad 203 (the escape hatch), **then** 3 s later, **if** that
    player is alive (walking away again during the 3 s does not cancel it; if he dies during the 3 s,
    nothing happens and the hatch never triggers again; it triggers once only): the player becomes invulnerable and cannot pick anything up, the HUD hides, the controls
    lock, the mission clock stops, the screen fades to black; **if** Natalya is alive, objective b completes.
    After the fade:
    - **if** Natalya is dead or gone, the level ends now (failed);
    - **else if** not all objectives of the difficulty are complete, the level ends now (failed);
    - **else** the outro plays and the level ends as a win.

## Objects and world
- Brake units tags 8..13: model 120 at bound pads 10000, 10052, 10055, 10075, 10103, 10119 (objective a).
- Floor charges tags 15..21, 43..45 (grenade items, pads 188..200) and tags 25..28 (bound pads 10171..10174),
  indestructible until rule 27 / the outro destroys them.
- Carriage doors tags 0..7 and 46 (model 325, pads 160..170), open at the start, closed by rule 16.
- Carriage doors tags 30..40 (rule 11); door tag 14 opens with the train door key.
- Boris's computer: monitor tag 42 (bound pad 10153) and unit tag 41 (bound pad 10154).
- Exit camera tag 29 at (-22810, 160, 314), yaw 72 deg, pitch 2 deg, room of pad 201.

## Cinematics

### Intro (setup intro data)
Played as `intro.md` describes; the data below is in its units (world units, API degrees, seconds).

- **Fixed cameras:** one; world units, API degrees, fov 60 (intro.md §1). Lines: 0x902C (location title); no
  second line; the shot lasts 5 s, then fades to black over 1 s.

  | Camera | Position (x, y, z) | Yaw | Pitch | Room of pad |
  |---|---|---|---|---|
  | 0 | (-4058.53, 393.69, -94.33) | 125.4 | -22.0 | 184 |

- **Swirl points** in order (intro.md §2): every offset in the player frame (flag 2: x left, y up, z forward, from
  the eye point); no point looks at the eye (no flag 4); the end marker (flag 1) follows the last row. Duration
  and tension belong to the leg that starts at the point. Swirl length 3.066 s (the legs from P1 on: P0 is only the previous control point, intro.md section 2).

  | Point | Offset (x, y, z) | Duration (s) | Tension |
  |---|---|---|---|
  | P0 | (4, -100, 276) | 0.833 | 0.75 |
  | P1 | (-113, -103, 235) | 0.75 | 0.75 |
  | P2 | (93, -100, 194) | 0.679 | 0.505 |
  | P3 | (163, -49, 45) | 0.557 | 0.495 |
  | P4 | (112.5, -10, -45.75) | 0.414 | 0.495 |
  | P5 | (38, 10.5, -61.5) | 0.333 | 0.5 |
  | P6 | (2.75, 10, -29) | 0.333 | 0.25 |
  | P7 (final) | (0, 0, 0) | not used | not used |
  | P8 (shape only) | (0, 0, 0) | not used | not used |

- **Intro animation:** no record: table entry 0 (animation 61 from frame 95, speed 0.02; intro.md §3).
- **Body during the swirl (a level script, not the intro-animation record):** from the swirl start, animation 100
  frames 0 to 1, and from the next tick frame 1 to its end, held at the last frame (blend 16 ticks).
- **Start (setup data):** pad 186; item 4 (PP7), 100 of ammo type 1, item 23 (watch laser), 300 of ammo type 24;
  outfit (cuff) 0; watch 10:17.

### Outro (rule 29, all objectives done)
1. Behind the black screen: all characters hidden, weapon put away, any button press from now fades out and
   ends the level; the view switches to camera tag 29 (fixed, its own direction).
2. Natalya is shown again, placed on pad 204, playing animation 90 from frame 7; Bond's model, invincible, on
   pad 205 playing animation 90 from the start.
3. Fade in. The charges explode: tag 25 at 1 s, tag 26 at 1.5 s, tag 27 at 3 s, tag 28 at 4 s.
4. At 5 s fade to black; the level ends as a win.

## Co-op notes
- Rules 12-16, 27-29: "a player" is any player; the standoff room tests are per player, first one wins.
- Rules 2, 4, 17, 25: aim, hunt and follow target is the nearest living player.
- Rule 29: the first player at the hatch triggers the exit for both; the invulnerability and lock apply to
  every player.

## API needed
- existing: `objectives.complete(id)`, `objectives.fail(id)`, `objectives.is_complete(id)`,
  `objectives.all_complete()`, `mission.win()`, `mission.fail()`, `mission.difficulty()`
- existing: `wait(seconds)`, `spawn(fn)`, `on(name, fn)`, `clock()`, `chr(n)`, `tag(n)`, `text(id)`
- existing: `target:is_dead()`, `target:sees_player()`, `target:was_shot_at()`, `target:hits()`,
  `target:go_to(target, speed)`, `target:is_moving()`, `target:distance()`, `target:distance_to(other)`,
  `target:near(radius)`, `target:close()`, `target:is_open()`, `target:stand()`, `target:surrender()`,
  `target:pos()`
- new: `players_in_room(pad) -> {player}`
- new: `guard:in_line_of_sight() -> boolean`, `guard:saw_guard_shot()`, `guard:saw_guard_die()`,
  `guard:was_damaged() -> boolean` (since the last call)
- new: `guard:attack_move(kind) -> boolean` ("sidestep", "fire_walk", "fire_run", "fire", "fire_kneel"),
  `guard:aim_at_player()` keep aiming without firing, `guard:fire_at_player()`, `guard:kneel()`
- new: `guard:cover(other)` keep another guard covered; `guard:fire_at(other)` (fire when aimed); existing
  `target:aim_at`, `target:shoot` may cover this if `shoot` can be delayed until the aim is set
- new: `guard:hit(body_part, item)` a scripted hit with an item's damage
- new: `guard:run_to_player()`, `guard:look_around()`, `guard:raise_arms()`, `guard:facing_player() -> boolean`
- new: `guard:play_anim_id(anim_id, from_frame, to_frame)`
- new: `guard:on_screen() -> boolean`, `guard:teleport(pad)`, `guard:set_invincible(on)`, `guard:hide(on)`
- new: `guard:no_autoaim()`, `guard:set_armour(n)`, `guard:give_weapon(item, {left_hand})`, `guard:give_hat(model)`,
  `guard:use_generic(name)`
- new: `chr(n):spawn_at(pad, {body, head, force})` (here never forced)
- new: `tag(n):is_destroyed()`, `tag(n):destroy()` (explodes it, clearing its indestructibility),
  `door(prop):unlock_bits(bits)` remove native lock bits, `door(prop):state()`
- new: `message_top(text)`, `hud.countdown(seconds)`, `hud.countdown_left()`, `hud.countdown_hide()`
- new: `sound.play(id, {channel, from_pad, from_tag})`, `sound.stop(channel)`, `sound.fade(channel, volume, seconds)`
- new: `music.play_extra(slot, stop_s, total_s)`, `fog.next()` switch at once to the level's next fog
- new: `player(n):set_invulnerable(on)`, `scene.lock_player(on)`, `scene.fade_out()`, `scene.fade_in()`,
  `scene.hide_weapon()`, `scene.hide_all_chrs()`, `scene.skip_on_button()`, `camera.fixed(tag)`
- new: event `"intro_swirl"`, `bond_model:place(pad)`, `bond_model:play_anim(anim_id, from_frame)`
