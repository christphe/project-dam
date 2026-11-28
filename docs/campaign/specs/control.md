# Control

Setup file `UsetupcontrolZ.c`, text bank 8 (ids 0x2000..). Bond escorts Natalya through the Janus control
centre: she hacks a security console to open the blast doors, then the satellite guidance console while
Bond holds off endless reinforcements; Boris and Trevelyan make short appearances.

## Characters
| chr | Who | Difficulties | Starts as | Notes |
|---|---|---|---|---|
| 0x00 | Natalya (body 79) | all | in the lift, pad 224 | armour 100, health 100; not auto-aimed by the player. Rules 1-14 |
| 0x01 | Trevelyan (body 8) | all | stands at pad 38 | armour 5000, speed 100, accuracy 100; not auto-aimed. Rule 18 |
| 0x02 | Boris (body 6) | all | stands at pad 2 | not auto-aimed. Rules 15-17 |
| 0x03 | guard | all | pad 0 | hearing 0.8, vision 25; standard generic guard (global.md) |
| 0x30 | guard | all | pad 3 | hearing 0.8, vision 20; standard generic guard (global.md) |
| 0x04-0x16, 0x19, 0x1C-0x21 | guards (chrs 4-22, 25, 28-33) | all | each on its own pad (setup) | hearing 0.01; standard generic guard (global.md) |
| 0x26-0x2C | guards (chrs 38-44) | all | each on its own pad (setup) | deaf (hearing 0); standard generic guard (global.md) |
| 0x17, 0x18, 0x1A, 0x1B | crouching ambushers at pads 17, 19, 20, 18 | all | aim at the player | armour 20, speed 80, accuracy 80; rule 25 |
| 0x1C, 0x1D | reinforcement shooters (spawned) | all | see rules 20-22 | speed 80, accuracy 80, can shoot other characters, carry a D5K (item 0x0A, model 189). Body 0 (camguard, the jungle commando) always; head: the game's random male head (one of heads 42-59, 62-68, the generic-guard pick) |
| 0x26, 0x27 | reinforcement chasers (spawned) | all | see rules 23-24 | speed 50, accuracy 40, health 40, armour 20, throws grenades 75% of the time, D5K (model 189) in one hand (50% left), hat model 220. Body 0 (camguard) always; head: the game's random male head (one of heads 42-59, 62-68) |

Note: chrs 0x1C/0x1D and 0x26/0x27 are numbers the spawned reinforcements take; setup guards with the same
numbers (chrs 28, 29, 38, 39) share them, so while one of those setup guards is alive, the matching
reinforcement slot counts as occupied (see rules 1, 20 and 23).

Start: pad 223 (the lift), PP7 (item 4) in the right hand, 100 rounds of ammo type 1, outfit (cuff) 2,
watch 3:11.

## Objectives
| Letter | Text | Difficulties | Completed when | Failed when |
|---|---|---|---|---|
| a | 0x2029 (protect Natalya) | all (min 0) | Natalya reaches the lift at the end of rule 12 | Natalya dies before rule 12 completed it (rule 14) |
| b | 0x202A (disable the satellite) | all (min 0) | Natalya finishes the guidance hack (rule 10) | a console she needs is destroyed, or Boris was killed (rules 6, 11, 16) |
| c | 0x202B (destroy armoured mainframes) | all (min 0) | the objects of tags 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A are all destroyed | never |

Briefing pages (watch menu): 0x2024..0x2028.

## Rules

### Natalya: the lift
1. **When** the intro ends (the intro cameras and the swirl are over), **then** show 0x2008 (Bond line: tells Natalya to stay put).
   Natalya stays in the lift **until** the three autoguns of tags 0x02, 0x03, 0x04 are destroyed **and** none of
   chrs 0x1C, 0x1D, 0x1E, 0x1F, 0x26, 0x27 is alive.
2. **Then**, **when** Natalya can see the player, she comments on the player's health (health only, armour
   not counted): below 30% 0x200D (that went badly), below 70% 0x200C (you're hurt), otherwise 0x200B (well done).
   She then goes to the security console (rule 3).

### Natalya: the security console (opens the blast door)
3. Natalya runs to pad 220 (until within 2.5 m), then walks to pad 225, faces pad 226, waits 1 s and plays
   typing animation 163 (frames 0-140), **then** shows 0x2009 (blast doors locked).
4. While at the console she keeps typing: each bout picks one animation at random: 163 (frames 0-140) 20%,
   172 (0-69) 20%, 173 (0-74) 20%, 174 (0-79) 20%, 175 (0-89) 21%. Every bout that is not 163 asks for the
   typing sound (rule 26). Between bouts, while she is out of the player's view, she is put back on pad 225.
5. **When** 15 s have passed since she started at the console (checked between bouts while she is in view),
   **then** show 0x200A (one door open, go to the control room) and the blast door opens (rule 27). Once only.
6. **When**, while she is at this console, the monitor of tag 0x23 or the console of tag 0x24 is destroyed,
   **then** show 0x201D (how do I hack now) and she gives up (rule 11).
7. **When** the player enters the room of pad 214 (the control room) while she is at the security console,
   **then**: if Boris was killed (he is gone and did not escape, rule 17) she goes to rule 16; otherwise she goes
   to the guidance console (rule 8).

### Natalya: the guidance console (disables the satellite)
8. Show 0x2010 (I'm coming); door tag 0x12 becomes passable for her (rule 27). She runs to pad 112 (until within
   2.5 m), walks to pad 227, faces pad 228, waits 1 s, plays animation 163 (frames 0-140), **then** shows 0x200E
   (the weapon target is revealed). She then types in bouts as in rule 4 (sound from the console of tag 0x26), and while
   out of view she is put back on pad 227 between bouts.
9. **When** 15 s have passed at this console, **then** show 0x200F (overriding the guidance), sound the alarm, and the
   override starts: the side doors unlock (rule 27) and reinforcements begin (rules 20-24). Once only.
10. The hack has a clock that counts from her arrival at pad 227. **While** the player is more than 15 m from her
    the clock is paused and she shows 0x2016 (I need cover) once per time the player strays (and again after he
    came back and strayed again). The clock always runs once the hack is done. At clock 40 s show 0x2015
    (yaw oscillation line), at 60 s 0x2017 (be quieter), at 80 s 0x2014 (temperature line), at 135 s 0x2011
    (orbit decaying) and objective **b** completes, at 140 s 0x2012 (Bond line: announces the way out), at 145 s she
    leaves (rule 12). Each line once.
11. **When**, before the hack is done, the monitor of tag 0x25 or the console of tag 0x26 is destroyed, **then** show
    0x201E (Natalya line: the hack is now impossible) and she gives up: 6 s later she shows 0x201F (going to the lift),
    objective **b** fails, the reinforcement shooters stop targeting her (rule 22), and she runs to pad 224.
    **When** she has reached it and the player enters the room of pad 224, **then** hide the HUD, freeze the
    player, fade to black and end the level (the objectives decide the outcome).
12. **When** the hack is done (clock 145 s), **then** show 0x2013 (good luck, I'll wait), the reinforcement shooters stop
    targeting her (rule 22), the reinforcement pace slows (rule 24), and she runs to pad 224 (until within 2.5 m);
    there objective **a** completes and she fades out (1.5 s) and is removed.
13. **When** Natalya dies during the guidance hack (before rule 12), **then** show 0x2018 (her last words) as she falls.
14. **When** Natalya no longer exists and objective **a** has not been completed, **then** show 0x2023 (Natalya killed)
    and objective **a** fails.

### Boris
15. Boris waits until he can see the player, faces him, and waits until the player is within 5 m, **then** shows 0x2022
    (I just look after computers). 4 s later, if he is on screen, he plays animation 66 (frames 0-20, hold), a PP7
    (item 4, model 191) appears in his hand, he plays animation 65 (frames 21-40, hold), drops everything he holds,
    shows 0x201C (oops), 0.5 s later 0x2019 (please don't shoot) and surrenders. When the surrender animation ends
    or he goes off screen, rule 17. If he was not on screen at the 4 s mark, rule 17 at once.
16. **When** Natalya learns Boris was killed (rule 7), **then** she does not do the guidance hack: door tag 0x12 unlocks
    and opens, she waits until she can see the player, shows 0x2020 (saw what you did), faces him, 5 s later
    shows 0x2021 (he was only a programmer), and gives up as in rule 11 (6 s, 0x201F, objective **b** fails, lift,
    level ends when the player joins her).
17. Boris (unless dead) shows 0x201A (I am invincible) and sprints to pad 229; within 3 m of it he fades out
    (1.5 s) and is removed: he has escaped (rule 7 treats him as not killed).

### Trevelyan
18. Trevelyan waits at pad 38. **When** he sees the player, he fires at him in bursts. **When** a player's shot just
    misses him, or the player comes within 5 m (10 m while he is firing), **then** door tag 0x1E closes and locks,
    door tag 0x1F locks, and he shows 0x201B (too slow); he does nothing more.

### Exit
19. **When** the player enters the room of pad 168, if he is alive, **then** make him invulnerable and unable to pick
    up items, hide the HUD, freeze the controls and fade to black. **If** not all objectives are complete, the level
    ends (failed). **Otherwise** the outro plays (Cinematics, outro).

### Reinforcements (after the override, rule 9)
20. Shooter slots: every 1 s, if chr 0x1C does not exist, a new guard (chr 0x1C) spawns at pad 63 even in the
    player's view; if chr 0x1D does not exist, one (chr 0x1D) spawns at pad 69. This goes on for the rest of the level.
21. A new shooter takes a post at random and runs there (to within 1.5 m), D5K in hand:
    chr 0x1C: 23% the window post at pad 126, where it smashes the glass of tag 0x22 on arrival; otherwise a fresh draw: pad 123
    31%, pad 115 31%, pad 116 38%. chr 0x1D: 23% the window post at pad 124, smashing the glass of tag 0x21 on
    arrival; otherwise pad 101 8%, pad 109 31%, pad 107 37% (shares of all spawns).
22. At its post a shooter fires at Natalya (50% standing, 50% kneeling) in bursts of about 2 s, as long as she
    lives. **When** Natalya is dead or gone, or she has left the console (rules 11, 12, 16), **then** it persistently
    chases and attacks the player (as the relentless hunter, global.md, global list 0x000D; it never ends).
23. Chaser slots: on each cycle (rule 24), if chr 0x26 does not exist, a guard (chr 0x26, stats in Characters) spawns
    out of the player's view at one of pads 126 16%, 71 16%, 129 16%, 202 16%, 189 16%, 61 12%, 124 10%; if chr 0x27
    does not exist, one (chr 0x27) at one of pads 86, 215, 140, 165, 149 (16% each), 183 12%, 82 10%.
24. A chaser runs to the player's position; whenever it sees him it attacks with one random move: grenade 4%,
    side-step 16%, fire while walking 20%, fire while running 20%, fire standing 20%, fire kneeling 21%; out of
    sight it runs after him again. It never gives up. The cycle of rule 23 repeats every 1 s; after Natalya has
    left for the lift on success (rule 12) it repeats every 40 s instead, except while the player is in the room
    of pad 149, 159, 10090, 164 or 166 (then still every 1 s).

### Ambushers
25. Chrs 0x17, 0x18, 0x1A, 0x1B stand aiming at the player. **When** a player's shot just misses one, it sees another
    guard shot or killed, or it sees the player, **then** it reacts at random: 21% throws a grenade then kneels, 39%
    kneels, 39% fires a burst. Kneeling lasts 3 s, with a 2% chance per frame to fire a burst. Then it aims again
    and repeats; it never leaves its spot.

### Sounds
26. **When** Natalya asks for the typing sound (rule 4), **then** after 1 s, 78% of the time, play sound 184 emitted from
    her console: the object of tag 0x24 (security console), or tag 0x26 once she is at the guidance console.

### Doors
27. At the start doors tags 0x11, 0x12, 0x0D, 0x08, 0x09, 0x0A are shut against everyone. **When** the security hack
    opens the blast door (rule 5), **then** door 0x11 unlocks and opens; **when** the player then enters the room of
    pad 210, door 0x11 closes and locks behind him. Door 0x12 becomes passable for Natalya when she heads for the
    guidance console (rule 8) or unlocks and opens in rule 16. **When** the override starts (rule 9), **then** doors
    0x0A, 0x0D, 0x08 and 0x09 unlock.

### Music
28. **While** the player is in the room of pad 223 (the lift he starts in), the level's extra track (slot 1) plays;
    **when** he leaves the room, it stops, and it plays again when he returns. Starts once the intro swirl begins.

## Objects and world
- Autoguns tags 0x02, 0x03, 0x04 (model 98) guard the first room; Natalya waits for them (rule 1).
- Security console: monitor tag 0x23, console tag 0x24. Guidance console: monitor tag 0x25, console tag 0x26.
  Destroying either pair while in use fails objective b (rules 6, 11).
- Mainframes tags 0x15-0x1A: objective c.
- Glass panes tags 0x21, 0x22: smashed by window shooters (rule 21).
- Doors: rule 27; tags 0x1E/0x1F (Trevelyan, rule 18); tags 0x1C/0x1D (exit, outro).
- Alarm: sounds when the override starts (rule 9).
- Music: rule 28; outro track (Cinematics).

## Cinematics
**Intro** (setup intro data). Played as `intro.md` describes; the data below is in its units (world units, API
degrees, seconds).

- **Fixed cameras:** 2 (one is picked at random); world units, API degrees, fov 60 (intro.md §1). Lines: 0x202C
  (location title); no second line; the shot lasts 5 s, then fades to black over 1 s.

  | Camera | Position (x, y, z) | Yaw | Pitch | Room of pad |
  |---|---|---|---|---|
  | 0 | (1032.20, 86.69, -33.95) | -46.1 | 6.0 | 121 |
  | 1 | (1886.46, 907.29, -750.88) | 71.3 | -32.0 | 172 |

- **Swirl points** in order (intro.md §2): every offset in the player frame (flag 2: x left, y up, z forward, from
  the eye point); no point looks at the eye (no flag 4); the end marker (flag 1) follows the last row. Duration
  and tension belong to the leg that starts at the point. Swirl length 3.001 s (the legs from P1 on: P0 is only the previous control point, intro.md section 2).

  | Point | Offset (x, y, z) | Duration (s) | Tension |
  |---|---|---|---|
  | P0 | (0, -100, 200) | 0.667 | 0.75 |
  | P1 | (0, -100, 200) | 0.667 | 0.75 |
  | P2 | (10, -100, 170) | 0.667 | 0.75 |
  | P3 | (80, -20, 0) | 0.667 | 0.75 |
  | P4 | (20, 40, -100) | 0.667 | 0.75 |
  | P5 | (0, 10, -30) | 0.333 | 0.25 |
  | P6 (final) | (0, 0, 0) | not used | not used |
  | P7 (shape only) | (0, 0, 0) | not used | not used |

- **Intro animation:** no record: table entry 0 (animation 61 from frame 95, speed 0.02; intro.md §3).
- **Body during the swirl:** the table pose only; no level script animates it.
- **Start (setup data):** pad 223; item 4 (PP7), 100 of ammo type 1; outfit (cuff) 2; watch 3:11.
- Rule 1's line follows the hand-over.

**Outro** (rule 19, all objectives complete), shot by shot:
1. Hide all characters; any button press from now on fades out and ends the level.
2. Doors tags 0x1C and 0x1D snap open. Bond (third-person model) holds no item.
3. Camera cut to the fixed camera-position record tag 0x20: (-327.35, 547.04, -3883.85), yaw 5.088,
   pitch 5.899, room of pad 168, looking at Bond (flag 2).
4. Fade in from black; play the level's extra track in slot 0.
5. Bond is put on pad 168; after 1 s he plays animation 101 (frames 8-109, hold last frame).
6. At 5 s his held items are removed; door tag 0x1C closes.
7. At 7 s fade to black; the level ends (won).

## Co-op notes
- Rule 2 (health comment): the player Natalya sees; with two players, the first one she sees.
- Rules 7, 11, 16, 19, 24, 28 (player in a room): any player.
- Rule 10 (stay within 15 m): the nearest player.
- Rule 15 (Boris within 5 m), rule 18 (Trevelyan's triggers): the nearest player / any player's shot.
- Rule 19 exit: when the first player reaches the room; both players should be frozen and invulnerable.
- Rules 22, 24, 25 attack the nearest player.
- Outro: one Bond model; use player 0.

## API needed
- existing: `objectives.complete(id)`, `objectives.fail(id)`, `objectives.is_complete(id)`, `objectives.all_complete()`
- existing: `message(text)` / `say(who, text)` for the top-of-screen lines
- existing: `alarm.trigger()`
- existing: `door(prop):lock()`, `:unlock()`, `:open()`, `:close()`
- existing: `guard(prop):is_dead()`, `:go_to(target, speed)`, `:is_moving()`, `:face(target)`, `:surrender()`, `:pos()`, `:distance()`, `:sees_player()`, `:was_shot_at()`
- existing: `wait(seconds)`, `wait(event(...))`, `on("death", ...)`, `clock()`
- new: `chr(n)` the character with chr number n (setup or spawned), with the Target methods; `chr(n):exists()`
- new: `chr(n):spawn_at(pad, ailist_or_behaviour, opts)` spawn a guard taking chr number n; opts: `force_in_view` (ignore the pad's view check), body 0 with the game's random male head
- new: `guard:play_anim(anim_id, from_frame, to_frame, hold)` by native animation id and frame range; `guard:anim_done()`
- new: `guard:is_on_screen()` the guard is drawn in a player's view
- new: `guard:teleport(pad)` put a guard on a pad
- new: `guard:fade_out()` fade 1.5 s then remove
- new: `guard:give_item(model, item, left_hand)` and `guard:give_hat(model)`; `guard:drop_items()`; `guard:remove_held_items()`
- new: `guard:set_stats{armour, health, speed, accuracy, grenade_chance, no_autoaim, can_shoot_chrs}`
- new: `guard:fire_at(target, {kneel, aim_only, burst})` fire at the player or another character; `guard:throw_grenade()`, `guard:kneel()`, `guard:sidestep()`, `guard:fire_walking()`, `guard:fire_running()`
- new: `guard:run_to_player()` and `guard:set_behaviour(name)` hand a character to a generic behaviour (global.md: standard, relentless hunter 0x000D)
- new: `guard:sees_guard_hit()` it saw another guard shot or killed
- new: `object(tag)` an object by setup tag: `:is_destroyed()`, `:destroy()`
- new: `player_in_room(pad, player?)` a player is in the room of pad
- new: `player_health(player)` health only, 0..1
- new: `intro_running()` / event `intro_end` (the intro cameras and swirl are over)
- new: `door(tag):set_passable(on)` the level's own "shut against everyone" state, distinct from the native lock
- new: `door(tag):open_instant()`
- new: `sound.play(sound_id, emitter_tag?)` a sound, optionally emitted from an object
- new: `music.play_extra(slot, seconds)`, `music.stop_extra(slot)`
- new: `hud.hide()`, `player_freeze(on)` (controls locked, mission clock paused), `player_invulnerable(on)` (no damage, no pickups)
- new: `screen.fade_out(seconds)`, `screen.fade_in(seconds)`, `screen.fade_done()`
- new: `mission.end_level()` end now, outcome from the objectives
- new: `cinema.begin()` third-person mode: `cinema.hide_all_chrs()`, `cinema.skip_on_button()` (any button fades out and ends), `cinema.bond_hold(item)` (nothing = empty hands), `camera:cut_to(tag, look_at_bond)` a camera-position record, `cinema.bond()` the Bond model as a guard target (teleport, play_anim)
