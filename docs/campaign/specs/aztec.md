# Aztec

Setup file `assets/obseg/setup/UsetupaztZ.c`, text bank 5 (ids 0x1400..). Bond infiltrates Drax's launch
complex in an Aztec ruin, uploads Q's guidance data, loads the launch protocol into the launch mainframe and
keeps the shuttle's exhaust bay open until the shuttle lifts off. Jaws guards the complex.

Distances below are in metres (1 m = 100 world units).

## Characters
| chr | Who | Difficulties | Starts as | Notes |
|---|---|---|---|---|
| 0x2A | Jaws, pad 115, body 13 | all | stands | armour +50, health 50 (007 health modifier applies), speed / accuracy / hit recovery 80, no auto-aim on him; then standard guard (global.md). Carries two AR33 (item 13) and the security smart card (key bit 1, renamed 141D..1421). Rule 13 |
| 0x06, 0x07, 0x14, 0x20, 0x21, 0x22, 0x23 | marksmen on pads 89, 90, 103, 116, 117, 118, 119 | all | stand | rule 14. chr 0x06, 0x07 have 50% grenade chance; chr 0x20-0x23 carry a Moonraker laser (item 22), the others an AR33 (item 13) |
| 0x13, 0x15, 0x16, 0x17 | guards on pads 102, 104, 105, 106 | all | stand | speed 50, accuracy 50; then simple deaf guard (global.md); AR33 |
| 0x03, 0x04, 0x05 | guards on pads 98, 122, 121 | all | stand | speed 50, accuracy 50; then standard guard (global.md); AR33; chr 0x03 has 50% grenade chance. These three chr numbers are also the reinforcement slots of rule 15 |
| all other guards (0x08-0x0F, 0x10-0x12, 0x18-0x1F, 0x24-0x29) | pads 91-97, 120, 99-101, 107-114, 123-128 | all | stand | speed 50, accuracy 50; then standard guard (global.md). chr 0x28, 0x29 carry a laser (item 22), the others an AR33. chr 0x08-0x0D have 50% grenade chance |

All setup guards except Jaws have body 39 (Moonraker guard). Body armour on pad 161 (all difficulties).
Six ceiling drone guns on pads 162-167 (setup data, not scripted).

## Objectives
| Letter | Text | Difficulties | Completed when | Failed when |
|---|---|---|---|---|
| a | 1415 (reprogram shuttle guidance) | all | rule 7 | - |
| b | 1416 (launch the shuttle) | all | rule 11 (lift-off) | - |

Briefing pages: title 1410, overview 1411, M 1412, Q 1413, Moneypenny 1414.

## Rules

### The exhaust bay

1. The exhaust bay starts **closed**. Its switch is tag 11 (the monitor on bound pad 10129).
2. **When** the switch is activated (by a player, or by rule 5), **if** the bay is not already moving,
   **then**: **if** the bay is closed, it opens (rule 3); **else if** the launch has locked the bay (rule 10),
   show 140F (bay secured for launch) and nothing moves; **else** it closes (rule 4).
3. **Opening:** show 1406 (bay opening); door tag 20 closes and door tag 0 opens at once; 15 s later door
   tag 9 opens and door tag 10 closes. The bay counts as **open** once door tag 0 is fully open.
4. **Closing:** show 1407 (bay closing); door tag 20 opens and door tag 0 closes at once; 15 s later door
   tag 9 closes and door tag 10 opens. The bay counts as **closed** once door tag 0 is fully closed.
5. **When** a player first enters the room of pad 131, **if** the bay is closed, **then** the switch is
   activated (the bay starts to open). **When** the bay is then open, the eight charges tags 1-8
   (pads 131-138, normally indestructible) explode in pairs: tags 1 and 2 0.5 s after the bay is open, tags 3
   and 4 at 1.5 s, tags 5 and 6 at 2.5 s, tags 7 and 8 at 3.5 s. This happens once per level.

### Guidance and launch

6. **When** a player activates the disk drive (tag 19, bound pad 10128), or uses a gadget on it without the
   guidance floppy (item 50) in hand, **then** show 1409 (drive feeds the guidance computer).
7. **When** a player uses the guidance floppy (item 50, in hand) on the disk drive (tag 19), **then**
   objective a is completed and 1408 (guidance data uploaded) is shown. Once done, the drive no longer
   reacts.
8. **When** a player activates the launch mainframe (tag 15, bound pad 10141), or uses a gadget on it
   without the DAT (item 73) in hand, **then** show 140A (mainframe runs the launch).
9. **When** a player uses the DAT (item 73, in hand; the pickup tag 12 on pad 142) on the launch mainframe
   (tag 15), **then** show 140B (launch protocol uploaded), and 5 s later the launch sequence starts
   (rule 10). This does not depend on objective a. Once done, the mainframe no longer reacts.
10. **When** the launch sequence starts, **then** a 30 s countdown appears on the HUD and runs, the level's
    secondary music track plays (slot 0, 255 s, no linger), and 140C (launch sequence initiated) is shown.
    From now on, whenever the bay is open, it is **locked** open (rule 2 refuses to close it).
11. **When** the countdown reaches 10 s:
    - **if** the bay is open (and not being closed), show 140D (final checks, launch is go), the countdown
      runs on, and **when** it reaches 1 s the shuttle lifts off: objective b is completed, the countdown is
      hidden, and the outro plays (Cinematics);
    - **else** the countdown stops at 10 s, 140E (bay closed, holding at T-10) is shown, and it is shown
      again every 5 s while the bay stays shut. **When** the bay is open again, the countdown resumes from
      10 s, the bay is locked, and the launch goes on as above (140D, lift-off at 1 s).

### Jaws and the reinforcements

12. The reinforcement spawn points follow the player: **when** a player is in the room of
    - pad 130 or 22, the three spawn pads are 97, 101, 99;
    - pad 23 or 107: 103, 90, 89;
    - pad 84, 85 or 78: 117, 128, 118;
    - pad 39 or 37: 84, 125, 124;
    - pad 126 or 65: 118, 111, 112;
    - pad 109 or 75: 95, 91, 124.
    (Slot 1 uses the first pad, slot 2 the second, slot 3 the third; the last room entered decides.)
13. While Jaws (chr 0x2A) is present there are no reinforcements. **When** Jaws is gone (killed and removed),
    rule 15 starts.
14. **Marksmen** (chr 0x06, 0x07, 0x14, 0x20-0x23): armour +2, speed 80, accuracy 80. They never move from
    their spot. They keep their gun aimed at the player. **When** a shot just misses them, they see another
    guard shot or killed, they see the player, or the player is in their line of sight, **then** they act by
    a random roll: 40% fire at the player (if that cannot be done, they try a grenade, then kneel); 21%
    throw a grenade (if that fails, kneel); 39% kneel. A kneeling marksman aims at the player and fires at a
    random moment (about 2% chance per tick) or after 3 s at the latest. After each action they wait until
    it is finished and start over.
15. **Reinforcements.** 2 s after the level starts, and then every 10 s, for each slot k = 1, 2, 3 whose chr
    (slot 1: chr 0x03, slot 2: chr 0x04, slot 3: chr 0x05) is not present: **if** Jaws is gone (rule 13) and
    the exit sequence has not begun, **then** spawn a Moonraker guard (body 39, random head) with that chr
    number at the slot's current spawn pad (rule 12).
16. **When** a reinforcement spawns, **then** it gets speed 40, accuracy 40, health 4, and by a random roll:
    49%: armour +2 and two lasers (item 22, one in each hand); 51%: armour +1, one laser and 80% grenade
    chance.
17. **Reinforcement behaviour.** It goes for the player: sprinting when the player is farther than 30 m,
    running otherwise. **When** the player is within 40 m and in its line of sight, **then** it fights: one
    action per roll — 4% throw a grenade, 16% sidestep, 12% firing roll, 8% fire while walking, 20% fire while
    running, 20% stand and fire, 21% kneel and fire (an action that cannot be done falls through to the next
    one in this order). After each action, **if** the player is still in sight it fights again, **else** it
    goes for the player again. **When** it stops moving without contact, it starts over.

### Screen by the smart-card door

18. TV tag 18 (bound pad 10006) shows screen bank 0x2F while door tag 21 is opening or open, and screen bank
    0x2E while it is closing or closed (0x2E at the start).

## Objects and world
- Exhaust bay doors: tag 0 (records 216/217, the bay itself), tag 9 (records 219/220), tag 10 (records
  222-229, an eight-part linked door), tag 20 (record 231). They are only moved by rules 3-4.
- Door tag 13 (records 249-251) needs key bit 1 (Jaws' smart card); its locked message is 142B. Door tag 21
  (records 239/240) needs key bit 2; locked message 1426 (inoperative).
- Disk drive tag 19, launch mainframe tag 15, DAT pickup tag 12 (renamed 1418..141C), bay switch tag 11.
- The shuttle: tag 23 (bound pad 10047); it lifts off like a rocket in the outro (no animation; Outro step 3).
- Charges tags 1-8: rule 5.
- TV tag 18: rule 18.
- Fog: switches at once to the level's next fog slot in the outro (the launch exhaust).
- Music: the secondary track from the launch start (rule 10).
- HUD countdown: rules 10-11.
- No alarm in this level's scripts.

## Cinematics

### Intro (setup intro data)
Played as `intro.md` describes; the data below is in its units (world units, API degrees, seconds).

- **Fixed cameras:** 3 (one is picked at random); world units, API degrees, fov 60 (intro.md §1). Lines: 0x1417
  (location title); no second line; the shot lasts 5 s, then fades to black over 1 s.

  | Camera | Position (x, y, z) | Yaw | Pitch | Room of pad |
  |---|---|---|---|---|
  | 0 | (1837.78, 497.69, 653.66) | 113.6 | -16.0 | 21 |
  | 1 | (-1817.01, -876.52, 4073.64) | -106.1 | 24.0 | 63 |
  | 2 | (-4440.56, 674.48, 2000.45) | 121.1 | -24.0 | 40 |

- **Swirl points** in order (intro.md §2): every offset in the player frame (flag 2: x left, y up, z forward, from
  the eye point); no point looks at the eye (no flag 4); the end marker (flag 1) follows the last row. Duration
  and tension belong to the leg that starts at the point. Swirl length 3.001 s (the legs from P1 on: P0 is only the previous control point, intro.md section 2).

  | Point | Offset (x, y, z) | Duration (s) | Tension |
  |---|---|---|---|
  | P0 | (0, -100, 200) | 0.667 | 0.75 |
  | P1 | (0, -100, 200) | 0.667 | 0.75 |
  | P2 | (10, -100, 170) | 0.667 | 0.75 |
  | P3 | (50, -20, 0) | 0.667 | 0.75 |
  | P4 | (20, 37.5, -77.5) | 0.667 | 0.75 |
  | P5 | (0, 10, -30) | 0.333 | 0.25 |
  | P6 (final) | (0, 0, 0) | not used | not used |
  | P7 (shape only) | (0, 0, 0) | not used | not used |

- **Intro animation:** no record: table entry 0 (animation 61 from frame 95, speed 0.02; intro.md §3).
- **Body during the swirl:** the table pose only; no level script animates it.
- **Start (setup data):** pad 130; item 4 (PP7), 100 of ammo type 1, item 50 (guidance floppy); outfit (cuff) 8;
  watch 10:25.

### Outro (lift-off, rule 11)
Skipped entirely if the player is dead at that moment.
1. The player becomes invulnerable and cannot pick up, the HUD is hidden, controls are locked, the mission
   clock stops; fade to black.
2. When the fade is done: every character is hidden, any button press from now on fades out and ends the
   level, Bond's weapon is hidden. Camera cuts to the cutscene camera tag 22 (position (-3555.08, 405.16,
   1813.72), room pad 128), turning to keep Bond in view; the third-person Bond is placed at pad 129.
3. Fade in from black; the shuttle (tag 23) lifts off; the fog switches to the next slot at once. The original
   plays no model animation on the shuttle (no animation id, no frames): the game turns the object into a free
   flying body. Its orientation is reset to upright, it starts moving straight up at 1/60 unit per tick and
   accelerates upward by a net 1/72 unit per tick per tick (a lift of 0.2917 against the game's falling
   acceleration of 0.2778), with no sideways motion. So it rises about 2510 units in the 10 s of the shot
   (y = t + 25 t^2 with t in seconds) and is moving up at about 500 units per second at the end.
4. 10 s later fade to black; when the fade is done the level ends (a win: both objectives are complete).

## Co-op notes
- Rule 5 and rule 12: "a player" entering a room — any player; with two players in different rooms the last
  room entered by either decides the spawn pads.
- Rules 14 and 17: the marksmen and reinforcements target the nearest living player.
- Rules 6-9: either player's item and use.
- Outro: skipped only if the player who would be shown is dead; with two players, play it if any player is
  alive.

## API needed
- existing: `objectives.complete(id)`; `door(prop):open()`, `:close()`, `:is_open()`; `event("door")`
  (state "open" / "closed" / "moving"); `wait(seconds)`, `wait(event(...))`, `spawn`, `on`; `has(item)`;
  `clock()`; `mission.win()`; `target:make_usable()` / `:was_used()` (switch, drive and mainframe
  activation); `guard:set_ai(list)`; `say(who, text)` / `message(text)`.
- new: `chr(num): Target` — a guard by chr number (setup or spawned); `chr:is_present(): boolean`.
- new: `object(tag): Target` — an object by tag; `object:activate()` (as if a player pressed it);
  `object:explode()` (destroy it, even when it was made indestructible in the setup).
- new: `door(tag)` by tag (door tags are used here, not prop numbers) and `door:state(): "open" | "closed" |
  "opening" | "closing"`.
- new: `event("gadget_used", {item, tag})` — a player used an item held in hand on a tagged object.
- new: `player:in_room_of(pad): boolean` / `event("room", {pad})`.
- new: `hud.countdown_set(seconds)`, `hud.countdown_run(on)`, `hud.countdown_show(on)`,
  `hud.countdown(): number` (seconds left).
- new: `music.play_track(slot, seconds, linger_s)` — the level's secondary track.
- new: `tv(tag):show(bank)` — set a tagged monitor's screen bank.
- new: `spawn_guard{chr, body, head, pad}: Target?`.
- new: `chr:set_ratings{speed, accuracy, recovery}`, `chr:add_armour(n)`, `chr:set_health(n)` (007 modifier
  applied), `chr:set_grenade_chance(pct)`, `chr:set_no_autoaim(on)`, `chr:give_weapon(item, left?)`.
- new: `chr:set_behaviour(name)` — hand to a generic guard behaviour (global.md).
- new: `chr:aim_at_player()`, `chr:fire_at_player(): boolean`, `chr:kneel()`, `chr:combat_move(kind): boolean`
  (kind "grenade" | "sidestep" | "roll" | "walk_fire" | "run_fire" | "stand_fire" | "kneel_fire"),
  `chr:run_to_player()`, `chr:sprint_to_player()`; queries `chr:in_sight_of_player()`,
  `chr:saw_guard_shot()`, `chr:saw_guard_die()`.
- new: `player:lock(on)` (HUD hidden, controls locked, clock paused), `player:set_invulnerable(on)`,
  `player:is_dead()`.
- new: `screen.fade_out()`, `screen.fade_in()`.
- new: `cinema.hide_all_chrs()`, `cinema.skip_on_button()`, `cinema.hide_weapon()`,
  `cinema.camera_at(tag, look_at_bond)`, `cinema.bond():teleport(pad)`.
- new: `object(tag):launch()` — the object flies up like a rocket.
- new: `fog.next()` — switch instantly to the level's next fog slot.
