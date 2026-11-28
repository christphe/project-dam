# Surface 2 (Severnaya, second visit)

Setup file `assets/obseg/setup/UsetupsevxbZ.c`, text bank 32 (ids 0x8000..). Two years later, in the snow
at night: Bond cuts the station's link to the bunker, wrecks the surveillance cameras, mines the troop
helicopter and gets into the bunker, where Soviet troops take him prisoner. Reinforcements roam the snow
from the start; armoured troops join once the alarm has sounded.

Pads are plain pad numbers in decimal; bound pads keep their +10000. "Prop" is the setup prop number.

## Characters
| chr | Who | Difficulties | Starts as | Notes |
|---|---|---|---|---|
| 0x08 | comms room officer, pad 17 | all | standing | body 38; hearing 1.0, vision 100; hat model 222 (prop 158); carries the comms room key (prop 160, key bit 2, renamed 8019..801D); generic deaf simple guard (global.md) |
| 0x09 | patrol, pad 39 | all | patrols path 0 | body 21; hearing 1.0, vision 100; rule 10 |
| 0x0A | patrol, pad 57 | all | patrols path 1 | as 0x09 |
| 0x0B | patrol, pad 72 | all | patrols path 2 | as 0x09 |
| 0x0C | patrol, pad 185 | all | patrols path 7 | as 0x09 |
| 0x0D | patrol, pad 127 | all | patrols path 10 | as 0x09 |
| 0x0E | patrol, pad 283 | all | patrols path 15 | as 0x09 |
| 0x04-0x07 | snowfield reinforcements | all | spawned by rule 8 | body 21, random head, sunglasses; Skorpion (item 7, model 193); grenade chance 191/256 (75%) |
| 0x0F-0x11 | armoured troops | all | spawned by rule 12 | body 38, random head, sunglasses; outfit by rule 12 |
| (spawned) | exit scene soldiers | all | Cinematics, Exit | see there |

A guard attribute record gives chr 0x12 a grenade chance of 12/256; no chr 0x12 exists in this level.
Four spawn points (no characters, they hold a pad each) feed chr 0x04-0x07: spawn point k owns chr 0x04+k.

## Objectives
| Letter | Text | Difficulties | Completed when | Failed when |
|---|---|---|---|---|
| a | 8014 (wreck the surveillance cameras) | 00 Agent+ | the four cameras tags 3, 4, 6, 8 (CCTV props 51, 53, 55, 57) are destroyed | - |
| b | 8015 (cut the bunker comms link) | all | both comms units tags 0 and 1 (props 43, 45, bound pads 10056, 10057) are destroyed | rule 1 |
| c | 8016 (disable the support helicopter) | Secret Agent+ | rule 4 (mine on the helicopter) | rule 4 (mine placed elsewhere) |
| d | 8017 (get into the bunker) | all | the player enters the room of pad 3 (native objective) | - |

Briefing pages: 800F title, 8010 overview, 8011 M, 8012 Q, 8013 Moneypenny.

## Rules

### Comms link (objective b)
1. **When** the player uses either comms unit (tag 0 or tag 1) while neither has been destroyed yet,
   **then** sound 0x0050 plays from tag 0, and 1 s later the top text 8009 (radio: puzzled reply) is shown
   and objective b fails. After this, rule 2 no longer shows its text.
2. **When** the first of the two comms units is destroyed, before any use of rule 1, **then** the top text
   8008 (comms link damaged) is shown. After this, using a unit no longer fails b.
3. **When** objective b has failed (rule 1), **then** the monitor tag 2 (prop 47, bound pad 10058) switches to
   screen bank 19, sound 0x00E3 plays at bound pad 10058, and sound 0x00A2 starts silent and, 2 s later,
   swells to nearly full volume (32500 of 32767) over 30 s.

### Troop helicopter (objective c)
4. **When** the player's mine (item 29) sticks to the helicopter tag 9 (prop 89), **then** the top text
   800B (bomb on the helicopter) is shown, objective c completes, and a 10 s countdown is shown on the HUD.
   **Else, when** the mine first comes to rest anywhere else in the level, **then** the top text 800C (bomb
   misplaced, inactive) is shown and objective c fails. Only the first of the two counts.
5. **When** the countdown of rule 4 runs out, **then** it is hidden and the charges round the helicopter go
   off in turn: at once tags 14 and 15 (props 168, 170, pads 24, 25); 3 s later tags 16 and 17 (props 172,
   174, pads 26, 27); 6 s after the start tags 18 and 19 (props 176, 178, pads 28, 29) and the helicopter
   tag 9 itself, with the top text 800D (helicopter destroyed). Each object is made destructible first, so
   it explodes even if it was indestructible.
6. From the start of the level the helicopter tag 9 spins its rotors up from rest to speed 620 (10 units per
   degree per tick) over 120 s. The second helicopter (prop 24, pad 31) keeps its rotors still.

### Snowfield reinforcements (chr 0x04-0x07)
7. The four spawn points move with the player: whenever the player is in one of these rooms (first match in
   this order wins), spawn points 0-3 take the listed pads; they keep them when the player is elsewhere.

   | Player in the room of pad | Spawn pads |
   |---|---|
   | 30 | 39, 57, 72, 42 |
   | 68 | 30, 57, 42, 39 |
   | 72 | 68, 93, 42, 72 |
   | 42 | 45, 39, 93, 72 |
   | 93 | 45, 72, 114, 60 |
   | 45 | 47, 93, 138, 42 |
   | 114 | 138, 58, 183, 93 |
   | 138 | 114, 224, 47, 51 |
   | 210 | 65, 183, 177, 62 |
   | 65 | 210, 201, 62, 183 |
   | 201 | 62, 134, 179, 177 |
   | 177 | 138, 226, 172, 134 |
   | 226 | 177, 243, 52, 232 |
   | 243 | 179, 56, 51, 232 |
   | 232 | 243, 226, 52, 177 |

8. From 5 s after the start, every 2 s, each spawn point k checks chr 0x04+k; **if** it is dead or gone and
   the exit (rule 13) has not begun, **then** it tries to spawn it (body 21) at its current pad, only when
   the pad is out of the player's view, and only with a chance set by the weapon in the player's hand:
   fist, knife, throwing knife, PP7, silenced PP7, sniper rifle (items 1, 2, 3, 4, 5, 17): 21%; Skorpion,
   AK47, grenade launcher (items 7, 8, 24): 80%; anything else: always. The new guard is a hunter (rule 9).
9. Hunter: runs toward the player's position (sprints when farther than 200 units), re-planning its route
   every 4 s or when it arrives. **When** it is within 80 units with a line of sight, **then** it attacks
   with one random action (grenade 4%, sidestep 16%, firing roll 12%, fire while walking 8%, fire while
   running 20%, stand and fire 20%, kneel and fire 21%) and attacks again while still within 80 units with
   a line of sight, else resumes the pursuit. **When** it is more than 1500 units from the player, **then**
   it is removed at once.

### Patrols
10. The patrol guards 0x09-0x0E walk their paths. **When** one sees the player within 300 units, or is hurt,
    **then** it becomes a hunter (rule 9) for good.
11. Every 40 s, for each of chr 0x09-0x0E that is dead or gone (and the exit has not begun), a replacement is
    spawned (body 21, sunglasses) at pad 214 when that pad is out of view. It carries an AK47 (item 8, model
    184) and runs to its post: chr 0x09 pad 39, 0x0A pad 57, 0x0B pad 72, 0x0C pad 185, 0x0D pad 127, 0x0E
    pad 283. **When** it arrives within 20 units of the post, **then** it takes up the original's patrol path
    (rule 10). **When** it sees the player on the way, or stops anywhere else, **then** it becomes a hunter.

### Armoured troops (chr 0x0F-0x11)
12. **When** the alarm has sounded (any cause, once is enough), **then** at once and every 20 s after, each of
    chr 0x0F, 0x10, 0x11 that is dead or gone is spawned (body 38, sunglasses) at pad 214 when out of view,
    unless the exit has begun. Its outfit:
    - 21% of the time, and always during the first 120 s of mission time: armour 2.0, hat model 220, AK47;
    - else half of the rest (39% overall): armour 8.0, hat model 221, a Skorpion, and 39% of those a second
      Skorpion in the other hand;
    - else (39% overall): armour 6.0, hat model 222, AK47.
    Then it is a hunter (rule 9).

### Into the bunker
13. **When** the player enters the room of pad 3, 6, 7, 8 or 9 (and is alive), **then** the player becomes
    invulnerable and cannot pick anything up, the HUD is hidden except the top and bottom text lines, the
    controls lock, the clock stops, all spawning stops for good, and the screen fades to black. When the
    fade is done: **if** not every objective of the difficulty is complete, **then** the level ends at once
    (failed); **else** the exit cinematic plays.
    Checked against the collision floor: pads 3, 6, 7, 8, 9 stand in rooms 39, 38, 37, 36, 36, which form
    one chain (outside, then 39, 38, 37, 36), and room 39 (pad 3) is the only one that joins the rest of
    the level. So the player always enters the room of pad 3 first: objective d completes in the same
    moment this rule fires, and the other four rooms never make a difference. Observable result: entering
    the bunker always counts for d; a rebuild may test the room of pad 3 alone.

## Objects and world
- Comms units tags 0, 1 (props 43, 45): usable (rule 1) and destructible (objective b, rule 2).
- Monitor tag 2 (prop 47): screen bank 19 after rule 3.
- CCTV tags 3, 4, 6, 8 (props 51, 53, 55, 57; bound pads 10061, 10062, 10064, 10066): objective a.
- Drone gun prop 38 (bound pad 10068): native, no script changes it.
- Helicopter tag 9 (prop 89, pad 23): rotors rule 6, mine target rule 4, destroyed in rule 5. Parked
  helicopter prop 24 (pad 31): rotors stopped.
- Charges tags 14-19 (props 168-178): exploded by rule 5.
- Door tag 10 (prop 78, bound pad 6): the bunker door, opened and closed in the exit cinematic.
- A welded hatch (prop 34, padlock prop 37 holds it) shows 8022 (hatch welded shut) when tried.
- Sounds: 0x0050 (rule 1), 0x00E3 and 0x00A2 (rule 3).
- Music: the exit cinematic starts the level's extra track (slot 0, 255 s, fading 255 s).
- Countdown: 10 s HUD countdown in rule 4.

## Cinematics

### Intro (setup intro data)
Played as `intro.md` describes; the data below is in its units (world units, API degrees, seconds).

- **Fixed cameras:** 3 (one is picked at random); world units, API degrees, fov 60 (intro.md §1). Lines: 0x8018
  (location title); no second line; the shot lasts 5 s, then fades to black over 1 s.

  | Camera | Position (x, y, z) | Yaw | Pitch | Room of pad |
  |---|---|---|---|---|
  | 0 | (4012.26, 2774.39, -10688.95) | -109.4 | -22.0 | 50 |
  | 1 | (2714.40, 2893.51, -10181.77) | -149.6 | -30.0 | 287 |
  | 2 | (5780.34, 193.10, -19523.57) | -45.7 | -4.0 | 257 |

- **Swirl points** in order (intro.md §2): every offset in the player frame (flag 2: x left, y up, z forward, from
  the eye point); no point looks at the eye (no flag 4); the end marker (flag 1) follows the last row. Duration
  and tension belong to the leg that starts at the point. Swirl length 5.333 s (the legs from P1 on: P0 is only the previous control point, intro.md section 2).

  | Point | Offset (x, y, z) | Duration (s) | Tension |
  |---|---|---|---|
  | P0 | (-138, -101, 117) | 1 | 0.75 |
  | P1 | (-138, -101, 117) | 1 | 0.75 |
  | P2 | (-47, -143, 223) | 1 | 0.75 |
  | P3 | (143, -138, 170) | 1 | 0.75 |
  | P4 | (181, -32, 0) | 1 | 0.75 |
  | P5 | (20, 40, -100) | 1 | 0.75 |
  | P6 | (0, 10, -30) | 0.333 | 0.25 |
  | P7 (final) | (0, 0, 0) | not used | not used |
  | P8 (shape only) | (0, 0, 0) | not used | not used |

- **Intro animation:** no record: table entry 0 (animation 61 from frame 95, speed 0.02; intro.md §3).
- **Body during the swirl (a level script, not the intro-animation record):** from the swirl start, animation 98
  from its start to frame 99, held there (blend 16 ticks).
- **Start (setup data):** pad 30; item 5 (silenced PP7), 100 of ammo type 1, item 29 (remote mine), 1 of ammo type
  7; outfit (cuff) 4; watch 3:50.

### Exit (rule 13, all objectives complete)
1. All characters are hidden, the player's weapons are hidden; any button press from now fades out and ends
   the level.
2. Camera: cutscene camera tag 11 (prop 162: (-92.34, -249.47, -948.78), room of pad 7), kept on Bond.
   Bond's body is shown again and put on pad 3. Every level character is removed (chr 0x00-0x07, 0x09-0x11)
   and a soldier (body 38, sunglasses) is spawned on pad 0: AK47, kneels aiming at Bond. Fade in from black.
3. Bond plays animation 89 in full (holding the last frame) and puts away the item in his left hand.
4. 3 s later, fade to black. Camera: tag 12 (prop 164: (-87.12, -229.46, -1953.92), room of pad 0), kept on
   Bond. Bond is put on pad 4 and plays animation 101 from its start to frame 34. Two more soldiers appear:
   body 38 on pad 1 (AK47, stands aiming at Bond) and body 21 on pad 5 (AK47, plays animation 107 in full,
   then after 2 s stands aiming at Bond), both with sunglasses. Fade in; the extra music track starts.
5. When animation 101 reaches frame 34, Bond lets go of the weapon in his right hand and plays animation 101
   on to its end; the top text 800E (soldier's ironic welcome) is shown.
6. 4 s after the fade-in of step 4, fade to black. Camera: tag 13 (prop 166: (0.51, 230.54, 378.19), room of
   pad 179). The bunker door tag 10 is snapped open (no sound). Fade in.
7. When the fade-in is done the door closes; when it is shut, or after 3 s, fade to black and the level ends
   (won).

## Co-op notes
- Rules 1, 4: any player's use of a comms unit or mine counts (the mine is a single item: whoever holds it).
- Rule 7 room tests: use any living player (first match over both players); rule 8 weapon in hand: the
  player nearest to the spawn pad.
- Rule 9, 10, 11 "the player": the nearest living player.
- Rule 13: the lead decides whether one player entering triggers the exit (and the scene teleports both) or
  every living player must be inside.
- Exit cinematic: one Bond body is shown; with two players the second player is hidden for the scene.

## API needed
- existing: `objectives.complete(id)`, `objectives.fail(id)`, `objectives.all_complete()`
- existing: `mission.difficulty()`, `clock()`, `wait(seconds)`, `on(name, fn)`, `spawn(fn)`,
  `event("alarm")`, `event("player_death")`
- existing: `target:was_used()` (native activation of the comms units), `guard(prop):is_dead()`,
  `guard(prop):sees_player()`, `guard(prop):hits()`, `target:distance()`, `door(prop):open()`,
  `door(prop):close()`, `door(prop):is_open()`
- new: `player_in_room(pad) -> boolean`
- new: `player_weapon(n) -> item` the item in the player's right hand
- new: `obj(prop):is_destroyed() -> boolean`
- new: `obj(prop):explode()` destroy an object now, even an indestructible one
- new: `item_stuck_to(item, prop) -> boolean` a thrown/placed item is attached to that object
- new: `item_at_rest(item) -> boolean` a thrown/placed item lies still anywhere in the level
- new: `tv(prop):show(bank, screen?)`
- new: `sound(id):play_from(prop)`, `sound(id):play_at(pad)`, `sound:set_volume(vol, seconds)` (a handle
  returned by play, for a fade)
- new: `hud_text(text_id)` top-of-screen text
- new: `countdown.show(seconds)`, `countdown.remaining() -> seconds`, `countdown.hide()`
- new: `aircraft(prop):set_rotor(speed, seconds_to_reach)`
- new: `spawn_guard{chr, body, head, pad, force_in_view, sunglasses} -> guard?`
- new: `guard:give_weapon(item, model, second_hand?)`, `guard:give_hat(model)`, `guard:set_armour(n)`,
  `guard:set_grenade_chance(pct)`
- new: `guard:patrol(path)`, `guard:run_to(pad) / guard:run_to_player(speed)`, `guard:is_moving()`
  (existing on Target), `guard:distance_to(pad)`
- new: `guard:has_line_of_sight() -> boolean`
- new: `guard:combat_action(kind)` ("grenade", "sidestep", "roll_fire", "walk_fire", "run_fire",
  "stand_fire", "kneel_fire") and `guard:is_busy()`
- new: `guard:aim_at_player(kneel)` aim without firing, held
- new: `guard:play_anim(anim, from, to, hold, blend_ticks)` any animation id (the existing play_anim takes
  names only)
- new: `guard:remove()`; `guard:behave(name)` hand to a generic behaviour (global.md)
- new: `cinema.begin{keep_top_text, keep_bottom_text}`, `cinema.finish()`
- new: `player_set_invulnerable(on)` no damage, no pickups
- new: `fade.to_black()`, `fade.from_black()`, `fade.wait()`
- new: `hide_all_characters()`, `player_hide_weapons()`
- new: `camera.fixed(prop, look_at_player)`
- new: `bond_body:show()`, `bond_body:teleport(pad)`, `bond_body:play_anim(anim, from, to, hold, blend)`,
  `bond_body:anim_frame() / wait_anim()`, `bond_body:drop_hand_item(hand)`
- new: `door(prop):snap_open()` open at once without sound, even if locked
- new: `event("intro_swirl")`
- new: `music.play_extra(slot, seconds, fade_seconds)`
- new: `mission.end_on_button()`, `mission.exit()` (won when all objectives are complete)
