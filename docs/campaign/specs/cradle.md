# Cradle

Setup file `UsetupcradZ` (US), text bank 9 (ids 0x2400..). The duel with Trevelyan on the antenna cradle:
he runs to a control console that will realign the antenna, then keeps relocating around the cradle with his
bodyguards respawning, until his health is low and he leads Bond down to the platform under the cradle,
where he is shot dead or falls. The ending shows Bond being lifted away.

## Characters
| chr | Who | Difficulties | Starts as | Notes |
|---|---|---|---|---|
| 0x00 | Trevelyan (body 8, head chosen by the game) | all | pad 121, runs to the console spot (rule 5) | health 17.0, body armour 2.0, speed 100, accuracy 100, hit-reaction recovery 100 (barely flinches); cannot be auto-aimed; starts invulnerable (rule 5); grenade probability 255 (setup guard-attribute record 13); carries the setup collectable record 14 (item 0x0D, model 188) |
| 0x01 | bodyguard | all | pad 18 | bodyguard kit (rule 2), then bodyguard combat (rule 3) |
| 0x02 | bodyguard | all | pad 135 | as 0x01 |
| 0x03 | bodyguard | all | pad 128 | as 0x01 |
| 0x04 | bodyguard | all | pad 79 | as 0x01 |
| 0x05 | bodyguard | all | pad 92 | as 0x01 |
| player | Bond | all | intro spawn pad 151 | outfit (cuff) 2; Walther PPK (item 4) in the right hand, nothing left; 100 rounds of ammo type 1; watch shows 4:20 |

All six guards have hearing 1.0 and vision 100. Bodyguards use body 4 with a random head.

## Objectives
| Letter | Text | Difficulties | Completed when | Failed when |
|---|---|---|---|---|
| a | 0x241F (destroy the control console) | all | the object of tag 0 (the console monitor, prop 21) is destroyed | the antenna countdown reaches 0 (rule 9) |
| b | 0x2420 (defeat Trevelyan) | all | Trevelyan no longer exists (died and faded away, or removed after his fall: rule 19) | never |

Briefing pages: watch-menu texts 0x241A..0x241E (title, overview, M, Q, Moneypenny).

## Rules

### Bodyguards
1. **When** the level starts, and then every 12 s for the rest of the level, **if** the player is not in a
   cinematic (damage and pickups disabled), **then** each bodyguard slot 0x01..0x05 whose guard no longer
   exists (killed and faded) gets a new bodyguard (body 4, random head, sunglasses) with the same chr number,
   placed 50/50 on one of two pads; if that pad cannot take it, the other pad is tried:
   0x01 pad 131 or 79, 0x02 pad 138 or 8, 0x03 pad 18 or 92, 0x04 pad 62 or 0, 0x05 pad 110 or 125.
   A respawned bodyguard goes through rules 2-3 like the originals.
2. **When** a bodyguard appears (start or respawn), **then** it gets one of three kits at random; all three
   have health 4.0 and body armour 2.0:
   - 32%: speed 50, accuracy 50, grenade probability 191 (75%), one weapon (item 0x09, model 0xC3), hat model 0xDC;
   - 31%: speed 60, accuracy 60, two weapons (item 0x09, model 0xC3, one per hand), hat model 0xDE;
   - 37%: speed 80, accuracy 80, two weapons (item 0x09, model 0xC3, one per hand), hat model 0xDD.
3. **While** a bodyguard lives, **then** it runs towards the player's position; **when** it is within 4500
   units and sees the player, it attacks with one move picked at random each time: 4% throw a grenade (only
   succeeds for the grenade kit, by its grenade probability; otherwise it sidesteps), 16% sidestep, 20% fire
   while walking, 20% fire while running, 20% fire standing, 20% fire kneeling. **When** the move ends, **if**
   it still sees the player it attacks again, **else** it runs to the player again. (This is a level-specific
   aggressive behaviour, not the generic guard.)

### Trevelyan: the console
4. **When** the level starts, **then** Trevelyan is invulnerable and sprints for his console spot, pad 144
   (he keeps re-issuing the run until he is within 100 units of it; if he stops farther than 300 units away
   he runs again).
5. **When** Trevelyan reaches pad 144 (within 100 units, or stopped within 300), **then** the console
   sequence starts (rule 8), his sprint speed is raised for the rest of the level, and he starts fighting
   from his spot (rule 6).
6. **Fighting from a spot.** **When** Trevelyan arrives at a spot, **then** he becomes vulnerable, aims at the
   player and, while he sees the player, fires at him.
   - **When** he takes damage, **then** he becomes invulnerable until he arrives at his next spot, a taunt is
     due (rule 12), and with 24% chance he decides to relocate.
   - **When** he has decided to relocate, or he is invulnerable and the player's shot hits him or lands near
     him, **then** he relocates (rule 7). While he is firing at the player in this invulnerable state he
     throws a grenade 20% of the time per burst, and a shot near him only makes him relocate on a 24% chance
     rolled per burst.
   - **When** the player comes within 800 units of him, **then** he relocates at once.
   - A relocation runs to the spot the spot director (rule 10) has assigned him; if none has been assigned
     since his last arrival, he asks the director for one and runs as soon as it is assigned.
7. **Running to a spot.** **When** Trevelyan relocates, **if** his health is 9.0 or less, **then** the final
   phase starts instead (rule 14). **Else** he sprints to his assigned spot. During the run:
   - **when** he takes damage, **then** he becomes invulnerable, a taunt is due (rule 12), and he re-issues
     the sprint (to his current assigned spot);
   - 1 s into the run, once, 32% chance: he drops a live grenade (item 0x1A, model 0xC4) behind him;
   - 10 s into the run, once: **if** he sees the player and the player's health is under 20%, top text
     0x2419 (mocks Bond's condition), and he re-issues the sprint;
   - **when** he is within 100 units of the spot or has stopped, **then** he fights from it (rule 6).
8. **Console sequence.** **When** Trevelyan first reaches pad 144, **if** the console (tag 0) is not
   destroyed, **then** top text 0x2406 (Trevelyan activated the console); 5 s later top text 0x2407 (three
   minutes left), sound 0xA1 starts on channel 0 and a 180 s countdown is shown on the HUD and starts.
   At 15 s left: top text 0x2409 (fifteen seconds warning).
9. **When** the countdown reaches 0, **then** objective a fails, top text 0x240A (satellite reactivated:
   mission failure), the channel 0 sound and the x-track music stop and the countdown is hidden and stopped.
10. **When** the console is destroyed (at any time, before or during the countdown), **then** top text
    0x2408 (console destroyed), the channel 0 sound and the x-track music stop and the countdown is hidden
    and stopped. (The objective completes by itself from the destroy condition.)

### Trevelyan: the spot director
11. A level script keeps track of which spot Trevelyan is assigned to (it starts at pad 144) and picks his
    next spot from where the player is. **When** the player comes within 500 units of a trigger pad of the
    current spot (checked in the order listed), or Trevelyan asks for a spot (rule 6), **then** his next spot
    is chosen from the table and marked as assigned (he runs there on his next relocation; an assignment made
    while he is already running replaces his destination when he next re-issues the sprint).
    "Hub" = 47% pad 144, 53% pad 145. "Side move": a move between the side spots pads 1, 9, 19.
    A side move after a side move is limited: **if** the previous move was a side move, 78% of the time the
    choice falls back to "Hub" instead.

    | Current spot | Player near (500 units) | Next spot |
    |---|---|---|
    | 144 | pad 59 | 92 |
    | 144 | pad 62 | 145 24%, 1 23%, 9 23%, 19 30% (not a side move) |
    | 144 | asks, player in room of pad 34 / of pads 69, 91 / elsewhere | 92 / as "pad 62" / 20% 92, 80% as "pad 62" |
    | 145 | pad 39 | 79 |
    | 145 | pad 42 | 144 24%, 1 23%, 9 23%, 19 30% (not a side move) |
    | 145 | asks, player in room of pad 31 / of pads 49, 80 / elsewhere | 79 / as "pad 42" / 20% 79, 80% as "pad 42" |
    | 1 | pad 4 or pad 7 | Hub |
    | 1 | pad 29 | side move: 47% 19, 53% 9 (limited) |
    | 1 | asks, player in room of pad 24 or 29 | as "pad 29" |
    | 1 | asks, player in room of pad 17, 19 or 28 | 33% side move to 9, 33% 144, 34% 145 |
    | 1 | asks, player in room of pad 3, 9 or 27 | 33% side move to 19, 33% 144, 34% 145 |
    | 1 | asks, elsewhere | 50% Hub, 50% as "pad 29" |
    | 9 | pad 12 or pad 15 | Hub |
    | 9 | pad 27 | side move: 47% 1, 53% 19 (limited) |
    | 9 | asks, player in room of pad 9 or 27 | as "pad 27" |
    | 9 | asks, player in room of pad 3, 1 or 29 | 33% side move to 19 (*), 33% 144, 34% 145 |
    | 9 | asks, player in room of pad 11, 19 or 28 | 33% side move to 1 (*), 33% 144, 34% 145 |
    | 9 | asks, elsewhere | 50% Hub, 50% as "pad 27" |
    | 19 | pad 22 or pad 21 | Hub |
    | 19 | pad 28 | side move: 47% 1, 53% 9 (limited) |
    | 19 | asks, player in room of pad 19 or 28 | as "pad 28" |
    | 19 | asks, player in room of pad 11, 9 or 27 | 33% side move to 1 (*), 33% 144, 34% 145 |
    | 19 | asks, player in room of pad 17, 1 or 29 | 33% side move to 9 (*), 33% 144, 34% 145 |
    | 19 | asks, elsewhere | 50% Hub, 50% as "pad 28" |
    | 92 | pad 69 | 145 |
    | 92 | pad 89 or pad 96 | 144 |
    | 92 | asks, player in room of pad 68 or 62 / of pads 96, 97, 100, 80 / elsewhere | 145 / 144 / 50-50 |
    | 79 | pad 49 | 144 |
    | 79 | pad 84 or pad 100 | 145 |
    | 79 | asks, player in room of pad 49 or 42 / of pads 100, 99, 96, 93 / elsewhere | 144 / 145 / 50-50 |

    (*) In the original, these four side moves send Trevelyan to the named pad but the director then treats
    him as standing at a different spot: from 9, "to 19" is afterwards tracked as spot 9 and "to 1" as spot
    19; from 19, "to 1" is tracked as spot 9 and "to 9" as spot 19. Reproduce this (it only changes which
    trigger pads apply next).

### Taunts and doors
12. **When** a taunt is due (Trevelyan was damaged, rules 6-7), **then** the next unused taunt is shown as top
    text, in this order, each once: 0x240D, 0x240E, 0x240F, 0x2410, 0x2411, 0x2412, 0x2413, 0x2414
    (Trevelyan's jibes). After the eighth, damage brings no more taunts.
13. **While** Trevelyan lives, **when** he is within 600 units of bound pad 10001 / 10002 / 10003 / 10004, or
    within 200 units of bound pad 10005, **if** the door of tag 6 / 7 / 8 / 9 / 10 respectively is not open
    or opening, **then** that door opens (he never has to stop at them).

### Trevelyan: the final phase
14. **When** Trevelyan would relocate with health 9.0 or less, **then** he becomes invulnerable, top text
    0x2415 (dares Bond to finish it), the level's x-track music starts (slot 0, up to 255 s), his speed,
    accuracy and hit recovery become 100, and he sprints to pad 150 (the way down under the cradle).
15. **When** he stops at pad 150: **if** the player is in the room of pad 115, 116, 148 or 109 (already below),
    **then** rule 17. **Else** he is set exactly on pad 150 and waits there (rule 16; if he cannot be placed
    there, rule 17).
16. **Waiting at pad 150**: he loops idle animation 0x6A. **When** he sees the player or the player's shot lands
    near him, **then** top text 0x2416 (claims he was always better), he plays animation 0x6B (once, 0.27 s
    blend), and 2 s later **if** the player is in the room of pad 116 **then** rule 18 **else** rule 17.
17. He walks to pad 116, then rule 18.
18. He runs to pad 118 and waits there; **when** the player is in the room of pad 116 and within 700 units of
    pad 116, **then** the last fight starts (rule 19).
19. **Last fight.** Trevelyan is vulnerable, can be knocked off the platform by any hit, and is culled by his
    hit box. He runs at the player when he cannot see him and attacks when he can: 25% fire while walking,
    37% fire standing, 38% fire kneeling, repeated while he sees the player.
    - **When** he enters the room of pad 148 (he fell off the platform), **then** the level remembers that he
      fell, his body ignores animation translation, and he fades out and is removed (1.5 s).
    - **When** he falls (as above) or is killed, **then** top text 0x2417 (Trevelyan line: final taunt), and 2 s
      later top text 0x2418 (Bond line: the reply).

### The ending
20. **When** the player is in the room of pad 146 (the platform) and Trevelyan no longer exists, **then** 1 s
    later the ending plays: **if** Trevelyan fell (rule 19), the final scene (Cinematics, scene C) after a fade
    to black; **else** his fall is shown first (scene B), then the final scene. The level then ends; the
    objectives decide success.
21. **When** the player, in the room of pad 147 (the drop under the cradle), is lower than y = -2550, **then**
    the HUD is hidden and controls are locked (he is falling); **if** he leaves that room again, the HUD and
    controls come back. **When** he gets lower than y = -4070 there, **then** he has fallen to his death:
    scene D, the mission is failed (killed in action), and the level ends after the scene.
22. **When** an ending scene would start and the player is already dead, **then** nothing is played (the
    game's own death handling runs).

## Objects and world
- Console: the single monitor of tag 0 (prop 21, bound pad 10000); destroying it completes objective a and
  stops the countdown (rule 10).
- Doors of tags 6..10 (props 29..37): opened for Trevelyan as he approaches (rule 13); otherwise normal doors.
- Helicopter: the aircraft of tag 5 (prop 38, model 282, pad 147). From the start its rotor turns at speed 620
  and its flight animation 0 is held at its first two frames; the setup starts it with the object flag
  PROPFLAG2_00080000 set (flags2 bit 0x00080000; the decompilation's comment calls it invincible except to
  explosions, but the game code reads it for two things only: an object with it is never treated as on
  screen, so it is **not drawn**, and a vehicle or aircraft with it plays **no engine / rotor sound**). So
  the helicopter is hidden and silent all level; the ending clears the flag, which makes it appear (and be
  heard) for scene C. In the ending it flies its animation from frame 2 (scene C).
- Four cutscene camera positions (tags 1..4, props 40..46, all on pad 146), x, y, z in world units, yaw,
  pitch in radians: tag 1 (-100.82, -2640.79, 572.35) 5.985 / 0.447; tag 2 (-343.77, -2392.79, 2166.36)
  0.532 / 0.244; tag 3 (138.10, -2061.79, 499.62) 6.013 / 5.048; tag 4 (107.05, -2969.79, 769.56) 6.040 / 0.489.
- Body armour pickups: props 24..27 (pads 143, 131, 138, 124), not scripted.
- Sound: 0xA1 on channel 0 during the countdown (rules 8-10). Music: x-track slot 0 from the final phase
  (rule 14), stopped by rules 9-10 if still playing then. No fog or alarm changes.
- HUD countdown: 180 s, rules 8-10.

## Cinematics
**Intro** (setup intro data). Played as `intro.md` describes; the data below is in its units (world units, API
degrees, seconds).

- **Fixed cameras:** 2 (one is picked at random); world units, API degrees, fov 60 (intro.md §1). Lines: 0x2422
  (location title); no second line; the shot lasts 5 s, then fades to black over 1 s.

  | Camera | Position (x, y, z) | Yaw | Pitch | Room of pad |
  |---|---|---|---|---|
  | 0 | (-5153.33, 622.41, -3235.23) | 52.0 | 0.0 | 121 |
  | 1 | (246.05, -1181.20, -1619.83) | -34.4 | 26.0 | 106 |

- **Swirl points** in order (intro.md §2): every offset in the player frame (flag 2: x left, y up, z forward, from
  the eye point); no point looks at the eye (no flag 4); the end marker (flag 1) follows the last row. Duration
  and tension belong to the leg that starts at the point. Swirl length 4.748 s (the legs from P1 on: P0 is only the previous control point, intro.md section 2).

  | Point | Offset (x, y, z) | Duration (s) | Tension |
  |---|---|---|---|
  | P0 | (181, 0, 4675) | 0.333 | 0.6 |
  | P1 | (181, 0, 4675) | 0.333 | 0.6 |
  | P2 | (167, 0, 4075) | 0.333 | 0.6 |
  | P3 | (163, 0, 3475) | 0.333 | 0.6 |
  | P4 | (149, -6.5, 2875) | 0.333 | 0.6 |
  | P5 | (135, -22, 2275) | 0.333 | 0.6 |
  | P6 | (121, -29, 1675) | 0.333 | 0.6 |
  | P7 | (107, 0, 1075) | 0.333 | 0.6 |
  | P8 | (128, 87, 527) | 0.333 | 0.6 |
  | P9 | (165, 153, 232) | 0.417 | 0.591 |
  | P10 | (173, 133, 31) | 0.5 | 0.591 |
  | P11 | (67, 59, -81) | 0.667 | 0.398 |
  | P12 | (9, 21, -46) | 0.5 | 0.422 |
  | P13 (final) | (0, 0, 0) | not used | not used |
  | P14 (shape only) | (0, 0, 0) | not used | not used |

- **Intro animation:** no record: table entry 0 (animation 61 from frame 95, speed 0.02; intro.md §3).
- **Body during the swirl (a level script, not the intro-animation record):** from the swirl start, animation 99
  frames 0 to 1 for 0.333 s, then frame 1 to its end, held at the last frame (blend 16 ticks).
- **Start (setup data):** pad 151; item 4 (PP7), 100 of ammo type 1; outfit (cuff) 2; watch 4:20.

**Scene B: Trevelyan's fall** (rule 20, he was killed on the cradle rather than falling): fade to black; the
player becomes invulnerable and cannot pick up, the HUD is hidden, controls locked, mission clock stopped, the
weapon is put away (unarmed). If Trevelyan's body is gone, a Trevelyan (body 8, chr 0x00) is placed on pad 146
(if this fails, go straight to scene C). All characters are hidden, any button press from now on fades to
black and ends the level, Bond's weapon is hidden, and the view cuts to camera tag 4. Trevelyan is shown;
Bond's third-person body is shown standing still on pad 149 only if the player is in the room of pad 116,
else it stays hidden. Trevelyan's fall:
1. on pad 146 he plays animation 0xB4, then is moved to pad 148 with no shadow; fade in from black; he drops
   under gravity for 1 s; fade to black;
2. cut to camera tag 3; again animation 0xB4 on pad 146, then moved to pad 148; fade in; he drops for 1.5 s;
   fade to black;
3. then scene C.

**Scene C: the final scene**: (when entered directly, after a fade to black and the same invulnerable /
HUD-hidden / locked / unarmed state) all characters hidden, any button press fades out and ends the level,
weapon hidden, view cut to camera tag 1 aimed at Bond. Bond's third-person body is shown:
1. placed on pad 146, both hands emptied, no shadow, held in the air (no gravity); fade in; animation 0xB3
   from frame 2; the helicopter (tag 5) gets its flag cleared and plays its flight animation from frame 2;
   4.8 s; fade to black;
2. cut to camera tag 2; Bond's body placed on pad 146 again, fade in, animation 0xB3 from frame 2; the
   helicopter is moved back to pad 147 and plays its animation from frame 2 again; 4.8 s; fade to black;
3. the level ends.

**Scene D: Bond's fatal fall** (rule 21): invulnerable, HUD hidden, controls locked, clock stopped, unarmed,
the mission is marked failed (killed in action); fade to black; characters hidden, button press ends the level,
weapon hidden, camera tag 4. Bond's third-person body plays the same fall as Trevelyan in scene B (steps 1-2).
If Trevelyan lives and is in the room of pad 116, he is shown standing still on pad 149. When the fall is
done, the level ends.

## Co-op notes
- Rules 1, 3, 6, 7, 11, 16, 18, 19 refer to "the player": take the nearest living player for distances and
  sight; the spot director (rule 11) should react to any player near a trigger pad (first one found).
- Rule 20: any player on the platform with Trevelyan gone. Rule 21: per player; with two players a fatal fall
  should kill only that player (the scene D failure is single-player behaviour), unless both are down.
- Scenes B/C show one Bond body (player 1's, or the player on the platform).
- Rule 12 taunts are global (one sequence).

## API needed
- existing: `chr(n)`, `tag(n)`, `wait(seconds)`, `on(name, fn, filter)`, `spawn(fn)`, `clock()`,
  `objectives.complete/fail`, `mission.win/fail`, `Target:is_dead`, `Target:open`, `Target:is_open`,
  `Target:distance`, `Target:distance_to`, `Target:sees_player`, `Target:was_shot_at`, `Target:health`,
  `Target:pos`, `Target:near`, `text(id)`.
- new: `chr(n):exists()` false once the character has died and faded out or was removed (or never spawned).
- new: `spawn_guard{chr, body, head, pad, alt_pad, sunglasses, force}` place a new character with a given chr
  number on a pad (trying around it; `force`: even in the player's view); returns false when it fails.
- new: `chr(n):set_stats{health, armour, speed, accuracy, hit_recovery, grenade_prob}` health / armour in
  game units (007 health modifier applied as the game does).
- new: `chr(n):give_weapon(item, model, hand)` / `chr(n):give_hat(model)` put a held weapon or hat on a guard.
- new: `chr(n):set_invulnerable(on)`, `chr(n):set_no_autoaim(on)`, `chr(n):set_fast_sprint(on)`,
  `chr(n):set_knockable(on)` (any hit can knock it back / off a ledge), `chr(n):set_gravity(on)`,
  `chr(n):set_shadow(on)`, `chr(n):set_hidden(on)`.
- new: event `chr_damaged {chr}` a character took damage (not while invulnerable); event `chr_near_miss {chr}`
  a player's shot hit or landed near it, including while invulnerable.
- new: `chr(n):run_to(pad, speed)` walk / run / sprint to a pad (by number) with `chr(n):is_moving()`;
  `chr(n):run_to_player()` run to the player's current position.
- new: `chr(n):aim_at_player()` / `chr(n):fire_at_player(stance)` stance "stand" | "kneel" | "walk" | "run";
  `chr(n):sidestep()`; `chr(n):throw_grenade()` (by the guard's grenade probability; false when not thrown);
  `chr(n):drop_live(item, model)` drop a live grenade / mine.
- new: `chr(n):attack_player()` the generic guard combat (for the last fight, if not scripted move by move).
- new: `chr(n):teleport(pad)` returns false when it cannot be placed.
- new: `chr(n):play_anim(anim_id, start_frame?, end_frame?, opts)` native animation by id, loop / hold /
  blend options; `chr(n):stop_anim()`.
- new: `chr(n):fade_out()` fade away (1.5 s) and be removed; `chr(n):ignore_anim_translation(on)`.
- new: `player(n):in_room_of(pad)`, `chr(n):in_room_of(pad)`; `player(n):y()` (or `pos`) for the fall test.
- new: `player(n):health()` 0..1 (exists for events only today).
- new: `say_top(text_id)` top-of-screen text line.
- new: `hud_countdown:set(seconds)`, `:start()`, `:stop()`, `:show()`, `:hide()`, `:remaining()`.
- new: `sound:play(sound_id, channel)`, `sound:stop(channel)`.
- new: `music:xtrack_play(slot, stop_after, total)`, `music:xtrack_stop(slot)`.
- new: `object(tag):is_destroyed()` / event `destroyed {tag}`.
- new: `aircraft(tag):set_rotor(speed, accel_time)`, `aircraft(tag):play_anim(anim_id, start, end)`,
  `object(tag):move_to(pad)`, `object(tag):show()` / `object(tag):hide()` (confirmed: the flag is the
  hidden-and-silent state, flags2 bit 0x00080000).
- new: `cinema:begin(opts)` HUD hidden (with exceptions), controls locked, mission clock stopped, player
  invulnerable and unable to pick up, weapon put away and hidden; `cinema:end()` gives it back (rule 21's
  temporary lock); `cinema:hide_all_chrs()`; `cinema:exit_on_button()` any button fades out and ends the level.
- new: `camera:cut_to(tag, look_at_player)` switch the view to a cutscene camera position record.
- new: `screen:fade_in()`, `screen:fade_out()`, `screen:wait_fade()`.
- new: `player_body(n)` Bond's third-person body during a cinema, taking the `chr` verbs.
- new: `mission:killed_in_action()` mark the mission failed without killing the player; `mission.exit()` end
  the level and let the objectives decide; `player(n):is_dead()`.
- new: `cinema:in_intro_swirl()` / event `intro_swirl` the intro camera has reached the swirl.
- new: `random(percent)`.
