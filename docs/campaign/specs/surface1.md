# Surface 1 (Severnaya, first visit)

Setup file `assets/obseg/setup/UsetupsevxZ.c`, text bank 31 (ids 0x7C00..). Bond crosses the snowfield of
the Severnaya station, powers down the communications dish, takes a key and the bunker plans, and drops into
the base through the ventilation tower. Endless reinforcements roam the snow once the player reaches the
central huts.

Pads are plain pad numbers in decimal; bound pads keep their +10000. "Prop" is the setup prop number
(position in the propDefs stream).

## Characters
| chr | Who | Difficulties | Starts as | Notes |
|---|---|---|---|---|
| 0x0E | hut guard, pad 2 | all | standing | body 21; hearing 0.15, vision 25; generic standard guard (global.md) |
| 0x0F | hut guard, pad 3 | all | standing | body 21; as 0x0E |
| 0x12 | hut guard, pad 6 | all | standing | body 21; as 0x0E |
| 0x13 | hut guard, pad 7 | all | standing | body 18; carries the hut door key (prop 200, renamed 7C25..7C29: hut key); generic standard guard (global.md). Matters for rule 4 on Agent |
| 0x17 | patrol, pad 81 | all | patrols path 2 | body 21; hearing 0.25, vision 25; rule 13 while patrolling |
| 0x18 | patrol, pad 107 | all | patrols path 3 | as 0x17 |
| 0x1A | patrol, pad 159 | all | patrols path 5 | as 0x17 |
| 0x1C | patrol, pad 182 | all | patrols path 7 | as 0x17 |
| 0x1E | patrol, pad 233 | all | patrols path 9 | as 0x17 |
| 0x04-0x07 | snowfield reinforcements | all | spawned by rules 4-7 | body 21, random head, sunglasses; grenade chance 191/256 (75%) |
| 0x08-0x0B | heavy reinforcements | all | spawned by rule 14 | body 38, random head; armour 4.0; hat model 221; Skorpion (item 7, model 193), 39% also a second Skorpion in the left hand; grenade chance 191/256 |

Four spawn points (no characters, they hold a pad each) feed the reinforcements 0x04-0x07: spawn point k
(k = 0..3) owns chr 0x04+k.

## Objectives
| Letter | Text | Difficulties | Completed when | Failed when |
|---|---|---|---|---|
| a | 7C10 (power down the dish) | all | rule 1 (the console was used) | rule 2 (the console was destroyed), at any time, even after a |
| b | 7C11 (get the safe key) | Secret Agent+ | the player holds the safe key (tag 0, prop 94, pad 17; renamed 7C20..7C24) | the key object is destroyed |
| c | 7C12 (steal the bunker plans) | Secret Agent+ | the player holds the plans (tag 2, prop 115, inside the safe prop 112 behind door prop 113; renamed 7C1B..7C1F) | the plans are destroyed |
| d | 7C13 (enter through the vent tower) | all | rule 17 (the player drops down the vent shaft) | - |

Briefing pages (watch menu): 7C0B title, 7C0C overview, 7C0D M, 7C0E Q, 7C0F Moneypenny.

## Rules

### Communications dish (objective a)
1. **When** the player uses the dish console (monitor tag 1, prop 89, bound pad 10049) while it is intact,
   **then** sound 0x0055 plays from the console, the top text 7C08 (dish powered down) is shown, objective a
   completes, the console's screen switches to screen bank 51, and the drone gun (tag 6, prop 86) is
   switched off (it stops firing).
2. **When** the console is destroyed (before or after rule 1), **then** 3 s later sound 0x00C6 plays from the
   console, the top text 7C09 (controls destroyed, backup power on) is shown, objective a fails, the drone
   gun is switched back on, and the alarm sounds. This happens once.
3. The drone gun (tag 6) is active from the start; only rules 1 and 2 change it.

### Snowfield reinforcements (chr 0x04-0x07)
4. Reinforcements start disabled. **When** the reinforcement trigger fires, **then** they are enabled for the
   rest of the level:
   - 007: at the start of the level.
   - Secret Agent and 00 Agent: when the player enters the room of pad 123 or the room of pad 147.
   - Agent: when the player is in the room of pad 123 or of pad 147 **and** chr 0x13 is dead or gone.
5. Each spawn point's pad follows the player: whenever the player is in one of the rooms below (the first
   match in this order wins), the four spawn points move to the listed pads (spawn point 0, 1, 2, 3). They
   keep their last pads when the player is elsewhere.

   | Player in the room of pad | Spawn pads |
   |---|---|
   | 45 | 48, 66, 56, 123 |
   | 77 | 45, 102, 69, 147 |
   | 81 | 45, 71, 123, 56 |
   | 51 | 45, 69, 181, 56 |
   | 102 | 45, 60, 181, 51 |
   | 54 | 77, 69, 71, 60 |
   | 123 | 48, 163, 74, 188 |
   | 147 | 77, 69, 61, 219 |
   | 219 | 188, 210, 181, 123 |
   | 74 | 235, 123, 69, 252 |
   | 210 | 219, 102, 56, 252 |
   | 186 | 219, 241, 81, 51 |
   | 235 | 81, 210, 71, 69 |
   | 252 | 210, 147, 64, 63 |
   | 241 | 252, 181, 56, 143 |

6. **When** reinforcements are enabled, **then** every 2 s each spawn point k checks its chr 0x04+k; **if**
   that chr is dead or gone and the level exit (rule 17) has not started, **then** it tries to spawn it at
   its current pad. The spawn only happens when the pad is out of the player's view (the native spawn
   check), and only with a chance that depends on the weapon the player has in hand:
   - fist, knife, throwing knife, PP7, silenced PP7 or sniper rifle (items 1, 2, 3, 4, 5, 17): 21%;
   - Skorpion, AK47 or grenade launcher (items 7, 8, 24): 80%;
   - anything else: always.
   A failed attempt is retried 2 s later.
7. **When** a reinforcement appears, **if** it is within 1000 units of the player, or otherwise with a 21%
   chance, **then** it gets a Skorpion (item 7, model 193) and becomes a hunter (rules 9-11); **else** it gets
   a sniper rifle (item 17, model 210) and becomes a sniper (rule 12).
8. There is no cap other than one living chr per spawn point; the table of rule 4 is the only difficulty
   difference of the reinforcements.

### Reinforcement behaviours
9. Hunter, pursuit: the guard runs toward the player's position (sprints when farther than 300 units). It
    re-plans its route every 4 s, or when it arrives. **When** it is within 160 units of the player with a
    line of sight, **then** it attacks (rule 10).
10. Hunter, attack: one action chosen at random, then back to rule 9 unless still within 160 units with a
    line of sight, in which case it attacks again: throw a grenade 4%, sidestep 16%, firing roll 12%, fire
    while walking 8%, fire while running 20%, stand and fire 20%, kneel and fire 21%.
11. **When** any reinforcement (hunter or sniper) is more than 3000 units from the player, **then** it is
    removed at once (its spawn point may replace it, rule 6).
12. Sniper: the guard runs toward the player's position (sprints when farther than 1500 units), re-planning
    every 4 s. **When** it is within 300 units of the player, **then** it becomes a hunter for good. **When**
    it is within 1000 units with a line of sight, **then** it kneels and aims at the player; **if** it keeps
    the line of sight for 5 s, **then** it fires once, and resumes the approach; **if** it loses sight while
    aiming, it resumes the approach at once.

### Patrols
13. The patrol guards 0x17, 0x18, 0x1A, 0x1C and 0x1E walk their paths. **When** one sees the player within
    300 units, or sees another guard get shot, or is hurt, **then** it becomes a hunter (rules 9-11) for
    the rest of the level.

### Heavy reinforcements (chr 0x08-0x0B)
14. Heavy wave: **when** the dish console is destroyed (rule 2, once), **and each time** the door alarm of
    rule 15 switches on from off, **then** each of chr 0x08, 0x09, 0x0A, 0x0B that is dead or gone is
    spawned at its own pad (9, 10, 11, 12 respectively), even in the player's view, unless the level exit
    (rule 17) has started.
15. Locked door alarm: **when** the player tries to open the locked door tag 3 (prop 185, bound pad 6) and
    the game refuses it, **then** the alarm sounds, the top text 7C0A (access denied) is shown, and the door
    alarm is on for 16 s; another refused try while it is on shows the text and sounds the alarm again and
    restarts the 16 s, without a new wave. After the 16 s it is off again (a later try starts a new wave).
    Note: once the door has refused the player one time, every later use of that door counts as a refused
    try, even one that opens it.
16. Heavy guard: kneels at its pad and aims at the player; whenever it has a line of sight it fires, then
    kneels and aims again. **When** a shot of the player misses it, **or** the door alarm (rule 15) is on,
    **then** it becomes a hunter (rules 9-11) for good.

### Leaving the level
17. **When** the player is in the room of pad 46 (the vent tower shaft) and below height 300, **then** the
    HUD is hidden, the controls are locked and the mission clock stops. **If** the player leaves that room
    again before the next step, **then** HUD, controls and clock come back and the rule re-arms. **If** the
    player dies meanwhile, nothing more happens. **When** the player is below height 0 in that room, or 3 s
    have passed, **then** objective d completes, the player becomes invulnerable and cannot pick anything
    up (this also stops all spawning), and the screen fades to black. When the fade is done:
    - **if** not all objectives of the difficulty are complete, **then** the level ends at once (failed);
    - **else** the exit cinematic plays (Cinematics, Exit).
18. Vent hatch (door tag 5, prop 187, bound pad 48): the four padlocks (props 188, 190, 192, 194, health 400)
    hold it shut (native padlock links: shoot them all to open it). **When** the hatch starts to open,
    **then** 2 s later sound 0x0105 plays from it.

## Objects and world
- Drone gun tag 6 (prop 86): on at the start, off after rule 1, on again after rule 2.
- Dish console tag 1 (prop 89): activatable monitor; screen bank 51 after rule 1; destructible (rule 2).
- Locked door tag 3 (prop 185): a refused open is the trigger of rule 15.
- Vent hatch tag 5 (prop 187) with four padlocks; made immune to everything but explosions when the exit
  cinematic starts.
- Safe prop 112 with door prop 113 holds the plans (native safe link, prop 116); the safe key is tag 0.
  The safe door's refusal text is 7C1A (safe is locked).
- Alarm prop 85 (bound pad 10082); CCTV prop 84. Alarm sounds from rules 2 and 15 (and natively from guards
  and the camera).
- Sounds: 0x0055 (rule 1), 0x00C6 (rule 2), 0x0105 (rule 18).
- Music: the exit cinematic starts the level's extra track (slot 0, 255 s, fading 255 s).

## Cinematics

### Intro (setup intro data)
Played as `intro.md` describes; the data below is in its units (world units, API degrees, seconds).

- **Fixed cameras:** 3 (one is picked at random); world units, API degrees, fov 60 (intro.md §1). Lines: 0x7C14
  (location title), then 0x7C15 (time caption); the shot lasts 8 s, then fades to black over 1 s.

  | Camera | Position (x, y, z) | Yaw | Pitch | Room of pad |
  |---|---|---|---|---|
  | 0 | (-467.68, 374.25, -7190.45) | 153.6 | 22.0 | 59 |
  | 1 | (3453.03, 2014.25, -9406.21) | -116.6 | -16.0 | 59 |
  | 2 | (-6199.34, 198.88, -16040.75) | 75.4 | 0.0 | 59 |

- **Swirl points** in order (intro.md §2): every offset in the player frame (flag 2: x left, y up, z forward, from
  the eye point); no point looks at the eye (no flag 4); the end marker (flag 1) follows the last row. Duration
  and tension belong to the leg that starts at the point. Swirl length 3.864 s (the legs from P1 on: P0 is only the previous control point, intro.md section 2).

  | Point | Offset (x, y, z) | Duration (s) | Tension |
  |---|---|---|---|
  | P0 | (181, 0, 4675) | 0.333 | 0.6 |
  | P1 | (181, 0, 4675) | 0.333 | 0.6 |
  | P2 | (167, 0, 4075) | 0.333 | 0.6 |
  | P3 | (163, 0, 3475) | 0.333 | 0.6 |
  | P4 | (149, 0, 2875) | 0.333 | 0.6 |
  | P5 | (135, -17, 2275) | 0.333 | 0.6 |
  | P6 | (121, -29, 1675) | 0.333 | 0.6 |
  | P7 | (107, 0, 1075) | 0.333 | 0.6 |
  | P8 | (128, 87, 527) | 0.333 | 0.6 |
  | P9 | (165, 153, 172) | 0.333 | 0.591 |
  | P10 | (162, 133, -80) | 0.333 | 0.591 |
  | P11 | (67, 59, -174) | 0.267 | 0.431 |
  | P12 | (9, 21, -106) | 0.267 | 0.422 |
  | P13 (final) | (0, 0, 0) | not used | not used |
  | P14 (shape only) | (0, 0, 0) | not used | not used |

- **Intro animation:** no record: table entry 0 (animation 61 from frame 95, speed 0.02; intro.md §3).
- **Body during the swirl (a level script, not the intro-animation record):** from the swirl start, animation 102
  frames 0 to 1 for 0.833 s, then frame 1 to its end, held at the last frame (blend 16 ticks).
- **Start (setup data):** pad 45; item 5 (silenced PP7), 100 of ammo type 1, item 17 (sniper rifle), 50 of ammo
  type 3; outfit (cuff) 4; watch 8:30. Other start slots (not used in solo play): slot 1 pad 290, item 17 (sniper
  rifle), 100 of ammo type 3; slot 2 pad 67, item 24 (grenade launcher), 12 of ammo type 11.

### Exit (rule 17, all objectives complete)
1. All characters are hidden, the player's weapons are hidden; any button press from now fades out and ends
   the level.
2. Camera: the fixed cutscene camera tag 4 (prop 245: position (6152.07, 646.58, -5805.80), the room of pad
   249), turned to keep Bond in view.
3. Bond's body is put on pad 47 with no shadow, culled by its hit box and with its height locked; the vent
   hatch is made immune to all but explosions; the screen fades in from black; the extra music track starts.
4. Bond plays animation 99 from its start to frame 66 (blend 16 ticks), then animation 178 from frame 20 to
   its end (blend 24 ticks), holding the last frame.
5. 3 s after animation 178 starts, fade to black; when done the level ends (result from the objectives).

## Co-op notes
- Rules 4 and 5, the room tests: use any player for rule 4; for rule 5 use the player nearest to
  the most recent room change (simplest: player 0 first, else player 1).
- Rule 6 weapon in hand: use the weapon of the nearest player to the spawn pad.
- Rules 9-12 and 16, the target: the nearest living player.
- Rule 17: the exit should wait for every living player to be in the shaft room (or let the first one in
  trigger it and teleport the other); the lead decides.
- Rule 15: any player's refused try counts.

## API needed
- existing: `objectives.complete(id)`, `objectives.fail(id)`, `objectives.all_complete()`
- existing: `alarm.trigger()`
- existing: `mission.difficulty()`, `wait(seconds)`, `on(name, fn)`, `spawn(fn)`, `event("player_death")`
- existing: `guard(prop):is_dead()`, `guard(prop):sees_player()`, `guard(prop):was_shot_at()`,
  `guard(prop):hits()`, `target:distance()`, `player(n):pos()`
- existing: `target:was_used()` (the dish console's native activation)
- new: `player_in_room(pad) -> boolean` any living player is in the room that contains pad
- new: `player_weapon(n) -> item` the item the player holds in the right hand
- new: `obj(prop):is_destroyed() -> boolean`
- new: `drone(prop):set_active(on)` a drone gun stops / resumes firing
- new: `tv(prop):show(bank, screen?)` change a monitor's screen bank
- new: `sound(id):play_from(prop)` a sound effect emitted from an object
- new: `hud_text(text_id)` a text line at the top of the screen
- new: `event("door_refused", {prop})` the player tried a locked door and the game refused it
- new: `event("door", {prop, state = "opening"})` (extend the existing door event with the opening state)
- new: `spawn_guard{chr, body, head, pad, force_in_view, sunglasses} -> guard?` spawn a new character; fails
  (nil) when out of chr slots or, without force_in_view, when the pad is in the player's view
- new: `guard:give_weapon(item, model, left_hand?)`, `guard:give_hat(model)`, `guard:set_armour(n)`,
  `guard:set_grenade_chance(pct)`
- new: `guard:patrol(path)` start walking a patrol path
- new: `guard:run_to_player(speed)` route to the nearest player's current position ("run"/"sprint")
- new: `guard:has_line_of_sight() -> boolean` to the nearest player
- new: `guard:combat_action(kind)` one attack action: "grenade", "sidestep", "roll_fire", "walk_fire",
  "run_fire", "stand_fire", "kneel_fire"; `guard:is_busy()` while it lasts
- new: `guard:kneel_aim()` kneel and aim at the player, `guard:fire()` fire the aimed weapon once
- new: `guard:saw_guard_shot() -> boolean`
- new: `guard:remove()` remove at once, no death
- new: `guard:behave(name)` hand the guard to a generic behaviour of global.md ("standard")
- new: `cinema.begin(keep_hud?)` hide HUD, lock controls, pause the mission clock; `cinema.finish()` undo
- new: `player_set_invulnerable(on)` no damage and no pickups (also read back: `exit_started()`)
- new: `fade.to_black()`, `fade.from_black()`, `fade.wait()`
- new: `hide_all_characters()`, `player_hide_weapons()`
- new: `camera.fixed(prop, look_at_player)` cut to a cutscene camera position
- new: `bond_body:teleport(pad)`, `bond_body:play_anim(anim, from, to, hold, blend_ticks)`,
  `bond_body:wait_anim()`, `bond_body:set_flags{no_shadow, lock_height, hitbox_cull}`
- new: `event("intro_swirl")` the intro camera reached the swirl onto the player
- new: `music.play_extra(slot, seconds, fade_seconds)`
- new: `mission.end_on_button()` any button fades out and ends the level
- new: `mission.exit()` end the level now; won when all objectives are complete
- new: `obj(prop):set_explosion_only_damage(on)`
