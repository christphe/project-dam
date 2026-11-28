# Bunker 1 (Severnaya satellite control bunker)

Setup file `assets/obseg/setup/UsetupsevbunkerZ.c`, text bank 30 (ids 0x7800..). Bond infiltrates the
Severnaya bunker, knocks out its surveillance, copies the GoldenEye key, forces a programmer to switch on the
mainframe, downloads its data and photographs the main video screen, then leaves by the exit room.

Distances below are in metres (1 m = 100 world units).

## Characters
| chr | Who | Difficulties | Starts as | Notes |
|---|---|---|---|---|
| 0x00 | guard, pad 65 | all | patrols path 0 | simple guard (global.md) after the patrol; carries the computer room keycard (key bit 1, renamed 7822..7826); grenade chance 5% |
| 0x08 | guard, pad 82 | all | patrols path 1 | simple guard (global.md) after the patrol; carries the security keycard (key bit 2, renamed 7827..782B); grenade chance 5% |
| 0x06 | guard, pad 6 | all | stands | simple alarm raiser guard (global.md); his alarm pad is 10022 |
| 0x07 | guard, pad 7 | all | stands | rules 16-17 (his own variant of the standard guard); his preset pad is 8 |
| 0x01-0x05, 0x09-0x11, 0x12, 0x13, 0x14, 0x17, 0x18 | guards on pads 1-5, 8-19, 21, 22 | all | stand | standard guard (global.md). chr 0x0C has two Klobbs (item 7) and 10% grenade chance; chr 0x12 (body 18) has a PP7 (item 4) and 10% grenade chance; chr 0x0A, 0x0B, 0x18 5% grenade chance; all others a KF7 (item 8) |
| 0x19 | the programmer, pad 0, body 6 | all | idle | rules 1-8; unarmed |
| 0x1A, 0x1B, 0x1C | alarm reinforcements | all | not present at start | spawned by rule 13; loadout rule 14; behaviour rule 15 |

Hearing / vision ranges vary per guard (setup data). Body armour on pad 39 exists on Agent only; body armour
on pad 46 on Agent and Secret Agent.

## Objectives
| Letter | Text | Difficulties | Completed when | Failed when |
|---|---|---|---|---|
| a | 781D (disrupt all surveillance) | Secret Agent and up | the four security cameras (tags 0, 1, 2, 3) are destroyed | - |
| b | 781E (copy key, leave original) | all | the GoldenEye key (tag 4) has been copied with the key analyser (the game's own key-copy state) | - |
| c | 781F (programmer activates computer) | 00 Agent and up | rule 5 (the programmer finishes his typing) | rules 7, 8 |
| d | 7820 (download mainframe data) | 00 Agent and up | rule 10 | rules 7, 8, 11 |
| e | 7821 (photograph main screen) | all | the main video wall (tag 5) is photographed with the camera | - |

Briefing pages: title 7818, overview 7819, M 781A, Q 781B, Moneypenny 781C.

## Rules

### The programmer (chr 0x19)

1. **When** the programmer sees or hears a player, a player's shot just misses him, or he sees another guard
   die, **then** he surrenders: hands up, drops what he holds, and keeps that pose (rules 2-3). He also
   surrenders again at once whenever rule 5 or rule 6 sends him back here.
2. **When** he has been surrendering for at least 3 s, **if** a player stands within 1.5 m of him and aims at
   him, **then** he leads the way (rule 4).
3. **When** he has been surrendering for 10 s, **if** no player is within 1.5 m, no player aims at him, and
   the nearest player is farther than 8 m or out of his sight, **then** he runs away (rule 6). While a player
   watches him from close by without aiming, he simply stays surrendered.
4. **Leading.** The first time, show 780A (Bond demands the mainframe terminal). On every later lead step,
   at most once per 8 s, show 780B (programmer: don't shoot, this way). He walks to pad 24 (the terminal).
   Every 5 s on the way, and whenever he stops short of pad 24: **if** a player aims at him or the nearest
   player is within 5 m, **then** he goes on leading (a new lead step); **else** he runs away (rule 6).
5. **At the terminal** (within 1.5 m of pad 24) he works through a fixed timeline counted from his arrival;
   each line is shown once:
   - at once, **if** the mainframe (tag 6 or tag 7) is still standing: show 7812 (Bond line) and he looks around nervously;
   - 5 s: 780E (it won't take long);
   - 10 s: 780F (the password was changed);
   - 15 s: 7810 (he types the password);
   - 20 s: the alarm goes off, objective c is completed, show 7811 (oops);
   - 30 s: he is **done**, and surrenders again (rule 1). From now on, when he runs away (rule 6) he heads
     for pad 97. A renewed lead request (rule 2) walks him through this timeline again.
   **If** at any moment of the timeline both mainframe parts (tags 6 and 7) are destroyed, **then** show
   7817 (he cannot fix this), he is **done** (without completing c) and surrenders again.
6. **Running away.** **If** he is done, he runs to pad 97. **Else** he picks an escape pad away from the
   player (the game's flee-pad choice) and goes there: 80% of the time running, 20% walking; if no escape pad
   is found he stays surrendered. **When** he comes within 10 m of pad 99, **then** he leaves (rule 7).
   **When** he reaches the escape pad (or stops): **if** he is done, he leaves (rule 7); **else if** a player
   is within 2 m or aims at him, he surrenders again (rule 1); **else** he flees once more (a new escape pad).
7. **Leaving.** He runs to pad 101. On arrival, **if** objective c is not complete and the difficulty is 00
   Agent or 007, **then** show 7813 (the programmer escaped) and objectives c and d fail. He then fades out
   (1.5 s) and is removed.
8. **When** the programmer is gone (killed and removed, or left by rule 7), **if** objective c is not
   complete, **then** objectives c and d fail.

### The mainframe download

9. **When** a player uses the data thief (item 55, held in hand) on the mainframe (tag 6 or tag 7), **if**
   objective c is not complete, **then** show 7814 (computer not activated).
10. **When** a player uses the data thief (held in hand) on the mainframe, **if** objective c is complete,
    **then** show 7815 (downloading), play sound 227, and 4 s later show 7816 (download finished) and
    complete objective d. A later use repeats the sequence harmlessly.
11. On 00 Agent and 007 only: **when** both mainframe parts (tags 6 and 7) are destroyed, **if** objective d
    is not complete, **then** show 782E (mainframe destroyed, no data) and objective d fails.

### The GoldenEye key

12. **When** objective b is completed, **then** the GoldenEye key (tag 4) can no longer be picked up (the
    original must stay).

### Alarm and reinforcements

13. **When** the alarm sounds for the first time, **then** start the level's alarm music track (slot 0, 75 s
    long, playing on for 60 s after a stop), and from then on, at once and every 20 s until the level ends:
    for each of chr 0x1A, 0x1B, 0x1C that is not present (never spawned, or killed and removed), spawn a guard
    with that chr number (body 4, random head) at pad 97, **unless** the exit sequence (rule 18) has begun.
14. **When** a reinforcement spawns, **then** it is equipped by one random roll:
    - **if** the mission clock is under 120 s, or with 21% chance: armour +2, hat model 0xDC, one KF7 (item 8);
    - **else** with 39% chance: armour +8, hat model 0xDD, a Klobb (item 7), and with 39% chance a second
      Klobb in the left hand;
    - **else** (39%): armour +6, hat model 0xDE, a DD44 (item 6), and with 39% chance a second DD44 in the
      left hand.
    (Armour is added to the guard's health, in health units.)
15. **Reinforcement behaviour.** It runs to the player's position, again and again. **When** the player is
    within 50 m and in its line of sight, **then** it fights: one action per roll — 4% throw a grenade, 16%
    sidestep, 12% firing roll, 8% fire while walking, 20% fire while running, 20% stand and fire, 21% kneel and
    fire (an action that cannot be done falls through to the next one in this order). After each action,
    **if** the player is still in sight it fights again, **else** it runs to the player again. It never goes
    back to idling.

### Guard chr 0x07

16. Chr 0x07 behaves like a standard guard while he stands still: **when** he sees a player he attacks;
    **when** he was shot at or saw a player in the last 10 s, or sees another guard shot or killed, he runs to
    the player; **when** he heard a player in the last 10 s he sends a clone or runs to the player; **when** a
    shot just misses him, 50% he is startled and runs to the player, 50% he attacks; otherwise, about 0.8% of
    the time per tick (when not already animating), he plays an idle animation. After each of these he comes
    back to this rule.
17. **When** security camera tag 2 is destroyed, **then** chr 0x07 attacks the player from then on, whether
    or not he sees him (each time he stands still).

### Exit

18. **When** a living player enters the room of pad 100, **then** that player becomes invulnerable and cannot
    pick anything up, the HUD is hidden, controls are locked, the mission clock stops, and the screen fades to
    black. **When** the fade is done: **if** every objective of the difficulty is complete, **then** play the
    outro (Cinematics); **else** the level ends at once (the usual end-of-mission objective check makes it a
    failure).

## Objects and world
- Doors: records 102/103 (double door) need key bit 1 (computer room keycard); records 110-115 (two
  three-part doors) need key bit 2 (security keycard). Door tag 8 (record 116) is snapped open, silently,
  in the outro.
- The mainframe is two objects, tags 6 (pad 49) and 7 (pad 50). Its destruction drives rules 5 and 11; it is not
  an objective by itself.
- Security cameras tags 0-3 (bound pads 10018-10021). Alarm boxes on bound pads 10022 and 10023.
- The main video wall (tag 5, bound pad 10044) is the photo target. The level's other screens show fixed
  images (setup data); no script changes them.
- The GoldenEye key (tag 4, pad 56): see rule 12.
- Music: the alarm track (rule 13); the outro restarts the same slot for 255 s.
- Sound: 227 at the start of the download (rule 10).
- Alarm: set off by the programmer (rule 5) or by the alarm raiser guard (chr 0x06).

## Cinematics

### Intro (setup intro data)
Played as `intro.md` describes; the data below is in its units (world units, API degrees, seconds).

- **Fixed cameras:** 5 (one is picked at random); world units, API degrees, fov 60 (intro.md §1). Lines: 0x782C
  (location title); no second line; the shot lasts 5 s, then fades to black over 1 s.

  | Camera | Position (x, y, z) | Yaw | Pitch | Room of pad |
  |---|---|---|---|---|
  | 0 | (413.31, 114.54, 969.71) | -130.0 | 12.0 | 51 |
  | 1 | (-632.66, 189.56, 1674.57) | -63.4 | 0.0 | 80 |
  | 2 | (-1961.07, 459.70, 571.07) | 52.0 | -22.0 | 49 |
  | 3 | (260.13, 462.26, 1087.18) | -144.4 | -22.0 | 51 |
  | 4 | (-1659.36, 439.61, 1846.35) | 153.5 | -16.0 | 79 |

- **Swirl points** in order (intro.md §2): every offset in the player frame (flag 2: x left, y up, z forward, from
  the eye point); no point looks at the eye (no flag 4); the end marker (flag 1) follows the last row. Duration
  and tension belong to the leg that starts at the point. Swirl length 3.001 s (the legs from P1 on: P0 is only the previous control point, intro.md section 2).

  | Point | Offset (x, y, z) | Duration (s) | Tension |
  |---|---|---|---|
  | P0 | (0, -100, 200) | 0.667 | 0.75 |
  | P1 | (0, -100, 200) | 0.667 | 0.75 |
  | P2 | (10, -100, 170) | 0.667 | 0.75 |
  | P3 | (70, -20, 0) | 0.667 | 0.75 |
  | P4 | (20, 40, -100) | 0.667 | 0.75 |
  | P5 | (0, 10, -30) | 0.333 | 0.25 |
  | P6 (final) | (0, 0, 0) | not used | not used |
  | P7 (shape only) | (0, 0, 0) | not used | not used |

- **Intro animation:** record index 1 of the intro-animation table (intro.md §3): animation 66 from frame 7 to
  frame 40, then frames 86 to 131 of the same animation (blended over 24 frames), held at frame 131; speed 0.5
  (30 animation frames per second). The only level with an intro-animation record.
- **Body during the swirl:** that entry only; no level script animates it.
- **Start (setup data):** pad 102; item 5 (silenced PP7), 100 of ammo type 1, item 46 (key analyser case), item 40
  (camera), 10 of ammo type 27, item 55 (data thief); outfit (cuff) 3; watch 10:20. Other start slots (not used in
  solo play): slot 1 pad 80, item 8 (KF7), 100 of ammo type 3; slot 2 pad 78, item 8 (KF7), 100 of ammo type 3,
  item 5 (silenced PP7), 100 of ammo type 1.

### Outro (after rule 18, all objectives done)
1. Every character is hidden; from now on any button press fades out and ends the level. Bond's weapon is
   hidden.
2. Camera cuts to the cutscene camera tag 9 (position (4.15, 472.59, -2199.69), room pad 103), turning to
   keep Bond in view. The third-person Bond is placed at pad 104.
3. Fade in from black; music slot 0 starts (the alarm track, 255 s); door tag 8 snaps open without sound.
4. Bond plays animation 0x6A (held on its last frame, idle pose after); 1 s later animation 0x59 (same
   flags).
5. 3 s later fade to black; when the fade is done the level ends (a win).

## Co-op notes
- Programmer rules 1-6: "a player" for seeing, aiming and distance; the nearest player for "farther than
  8 m / 5 m" and for the escape-pad choice.
- Reinforcements (rule 15) chase the nearest living player.
- Rule 18: the first living player to enter the exit room triggers the exit for everyone (both players are
  locked and made invulnerable).
- Rules 9-10: either player's data thief.

## API needed
- existing: `alarm.trigger()`; `event("alarm")`; `objectives.complete(id)`, `objectives.fail(id)`,
  `objectives.is_complete(id)`, `objectives.all_complete()`; `mission.difficulty()`, `mission.win()`,
  `mission.fail()`; `clock()`; `wait(seconds)`, `wait(event(...))`, `spawn`, `on`; `has(item)`;
  `guard:surrender()`, `guard:go_to(target, speed)`, `guard:is_moving()`, `guard:is_dead()`,
  `guard:sees_player()`, `guard:was_shot_at()`, `guard:set_ai(list)`; `target:near(radius)`,
  `target:distance()`, `target:distance_to(other)`; `say(who, text)` / `message(text)` for the text lines.
- new: `chr(num): Target` — a guard by its chr number (setup or spawned); nil-safe `is_dead()` when absent.
- new: `chr:is_present(): boolean` — the chr exists (not yet removed after death, or never spawned).
- new: `chr:hears_player(within_s?)`, `chr:saw_guard_die()`, `chr:saw_guard_shot()`, `chr:in_sight_of_player()`
  (line of sight), `chr:is_aimed_at(): boolean` (a player aims or has the crosshair on it).
- new: `chr:flee_pad(): Pad?` — the game's escape-pad choice away from the player; nil when none.
- new: `chr:look_around()` — the nervous look-around animation.
- new: `chr:fade_out()` — fades over 1.5 s, then the chr is removed.
- new: `chr:attack()` — one generic attack move at the player (then returns to the script).
- new: `chr:run_to_player()`, `chr:startle()`, `chr:clone_or_run()`, `chr:idle_anim()` — the generic guard's
  reaction moves, each once.
- new: `chr:combat_move(kind): boolean` — kind "grenade" | "sidestep" | "roll" | "walk_fire" | "run_fire" |
  "stand_fire" | "kneel_fire"; false when it cannot be done now.
- new: `chr:set_behaviour(name)` — hand the chr to a generic guard behaviour by name (global.md).
- new: `spawn_guard{chr, body, head, pad}: Target?` — spawn a guard at (or next to) a pad; nil when it fails.
- new: `chr:give_weapon(item, left?)`, `chr:give_hat(model)`, `chr:add_armour(amount)`.
- new: `object(tag): Target` — an object by its tag; `object:is_destroyed()`; `object:set_collectable(on)`.
- new: `event("gadget_used", {item, tag})` — a player used a gadget item, held in hand, on a tagged object.
- new: `player:in_room_of(pad): boolean` / `event("room", {pad})` — a player entered the room of a pad.
- new: `music.play_track(slot, seconds, linger_s)` — the level's secondary (x) track in a slot.
- new: `sound.play(id)`.
- new: `player:lock(on)` — HUD hidden, controls locked, mission clock paused; `player:set_invulnerable(on)`
  (no damage, no pickups).
- new: `screen.fade_out()`, `screen.fade_in()` (wait until done).
- new: `cinema.hide_all_chrs()`, `cinema.skip_on_button()` (any button fades out and ends the level),
  `cinema.hide_weapon()`, `cinema.camera_at(tag, look_at_bond)` (cutscene camera record by tag),
  `cinema.bond(): Target` (the third-person Bond) with `:teleport(pad)` and `:play_anim(id, flags)`.
- new: `door:snap_open()` — open instantly without sound, even when locked.
- new: `mission.finish()` — end the level now; the objective check decides win or failure.
