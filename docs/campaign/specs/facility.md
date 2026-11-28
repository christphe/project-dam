# Facility

Setup file `UsetuparkZ`, text bank 3 (ids 0x0C00..). Bond enters the chemical weapons plant
through the toilets, meets the double agent Dr. Doak for a door decoder, joins 006 in the bottling room to
destroy the gas tanks, and witnesses Ourumov's ambush and Trevelyan's execution before escaping.

## Characters
| chr | Who | Difficulties | Starts as | Notes |
|---|---|---|---|---|
| 0x00, 0x01, 0x03, 0x04, 0x0C, 0x0D, 0x0F, 0x10, 0x12, 0x13, 0x15, 0x16, 0x18, 0x19, 0x1B, 0x1C, 0x1E, 0x1F | soldiers (body 2) on their pads | all | standard generic guard (global.md) | 0x00, 0x04, 0x13, 0x18 grenade chance 3% |
| 0x02, 0x0E, 0x11, 0x14, 0x17, 0x1A, 0x1D, 0x23, 0x24 | soldiers (body 2) on their pads | all | deaf simple generic guard (global.md) | 0x0E, 0x1D grenade chance 3% |
| 0x05, 0x06, 0x07 | soldiers (body 2) | all | rule 22, watching the room of pad 0x1D | |
| 0x08, 0x0A | soldiers (body 2) | all | rule 22, watching the room of pad 0x39 | |
| 0x09, 0x0B | soldiers (body 2) | all | rule 22, watching the room of pad 0x3B | 0x09 grenade chance 3% |
| 0x21, 0x22, 0x25, 0x26, 0x2E | soldiers (body 2) | all | rule 21 | 0x25 carries keycard B (keyflags 0x4); grenade chance 3% for 0x22, 0x2E |
| 0x2A, 0x2B | soldiers (body 2) at pads 0x22, 0x21 | all | rule 20 | 0x2A grenade chance 3% |
| 0x2C | soldier | all | patrols path 0 | then simple generic guard (global.md); grenade chance 3% |
| 0x2D | soldier | all | patrols path 1 | then simple generic guard |
| 0x2F, 0x30 | soldiers | all | patrol path 3 | then simple generic guard; 0x30 grenade chance 3% |
| 0x31, 0x32 | soldiers | all | patrol path 4 | then simple generic guard |
| 0x27 | soldier | all | patrols path 5 | then simple generic guard; grenade chance 3% |
| 0x28 | soldier | all | patrols path 6 | then simple generic guard |
| 0x42 | scientist (body 35) | all | patrols path 2 | rule 9 while on patrol |
| 0x33..0x37, 0x3A..0x41 | scientists (body 35) | all | rules 8-13 | 0x33..0x37 are keyboard workers (rule 8) |
| 0x39 | scientist (body 35) | all | rule 14 | carries keycard A (keyflags 0x6) |
| 0x43 | Trevelyan / 006 (body 9) | all | rule 23 | health 8, no auto-aim on him, can shoot other characters |
| 0x4F | Dr. Doak (body 35, head 51) | all | spawned, rule 15 | |
| 0x4E | Ourumov (body 7, random head) | all | spawned, rule 30 | armour 100, health 30, speed rating 60, accuracy 100, hit reaction 100, no auto-aim, can shoot other characters, armed with a TT33 (item 6, model 205) |
| 0x44..0x49 | Ourumov's squad (body 2, random head) | all | spawned, rule 33 | AK-47 (item 8, model 184) and a hat (model 217) |
| 0x25, 0x26, 0x27 | replacements (body 2) | all | spawned, rule 34 | AK-47 |
| (new) | four reinforcements (body 2) | all | spawned, rule 20 | AK-47 and hat |

Scientists 0x33..0x42 have a 25% grenade chance (setup attribute 63). All hearing 1.0, vision 100.

## Objectives
Objective difficulty: the low byte of the setup word counts (objective e's word is 256: low byte 0, Agent).

| Letter | Text | Difficulties | Completed when | Failed when |
|---|---|---|---|---|
| a | 0x0C30 (get into the laboratory area) | all | the player enters the room of pad 0x56 | never |
| b | 0x0C31 (meet the double agent) | Secret Agent, 00 Agent, 007 | Doak hands over the decoder (rule 16) | Doak dies before that, 00 Agent and 007 only (rule 17) |
| c | 0x0C32 (meet up with 006) | all | Trevelyan's third greeting line (rule 24) | the player hurts Trevelyan before that (rule 28), or he dies before the meeting (rule 23) |
| d | 0x0C33 (destroy the bottling room tanks) | all | all ten gas tanks tags 0x01..0x0A (props 106..124) are destroyed | never |
| e | 0x0C34 (keep scientist deaths low) | all | the player reaches an exit room (rule 36) | more than 2 scientists are dead (rule 13) |

## Rules

### Start
1. **When** the level starts, **then** the intro plays (Cinematics). Bond starts at pad 0xA7 (slot 0) with the
   silenced PP7 (item 5), remote mines (item 29) with their trigger (item 30), 100 rounds of PP7 ammo (type 1)
   and 5 remote mines (ammo type 7); outfit (cuff) 3; watch time 11:52.
2. Alternative start slots: slot 1 at pad 0x6D, same kit as slot 0; slot 2 at pad 0x89 with the PP7 and 100
   rounds; slot 3 at pad 0x5D with an AK-47 (item 8) and 100 rounds (ammo type 3).

### Doors, locks and screens
3. **On Agent**, door tag 0x26 (prop 102) loses its decoder lock (lock bit 0x08) at the start: it opens like
   an ordinary door.
4. **When** the player uses the door decoder (item 38, it must be the equipped item) on door tag 0x26,
   **then** a decoding sound (0xB8) plays from the door; 3 s later a second sound (0x53) plays from it, bottom
   text 0x0C29 (code accepted) and the door opens. Works every time it is used.
5. **When** a player activates one of the door-panel screens tags 0x0B, 0x0C, 0x0D (each is linked to its
   remotely locked door: 0x27, 0x20, 0x29), **then** bottom text 0x0C27 (security door activated). The
   panel itself opens its door (engine switch link).
6. **When** the first of the small bottling tanks tags 0x17..0x1F (props 175..191, pads 245..253) is
   destroyed, **then** top text 0x0C28 (gas leak, sealing area), sound 0xA1, and door tag 0x20 closes and is
   sealed for the rest of the level: locked with a lock no key opens (bit 0x80), locked one-way on both sides,
   and characters cannot operate it.
7. Door screens follow their doors for the whole level:
   - screen tag 0x0B shows image 0x2E while door 0x27 is closed or closing and image 0x2F while it is opening
     or open; screen 0x0C does the same for door 0x20, screen 0x0D for door 0x29;
   - screens tags 0x2A and 0x2B follow door 0x20: image 0x31 when closed, 0x2E while closing, 0x2F (screen
     index 1) while opening, 0x32 when open;
   - screens tags 0x2C and 0x2D follow door 0x21 the same way (open image 0x32 on screen index 1).
   All screens start on the closed image.

### Scientists
8. **At start** scientists 0x33..0x37 are keyboard workers: while idle, unalarmed and on a player's screen
   they type at their console (keyboard generic list, global.md) and come back to rule 10 after it. A worker
   who is disturbed (any reaction of rule 10 except standing still) stops being a keyboard worker for good.
9. **While** scientist 0x42 walks its patrol, **when** it sees the player, saw or heard the player within the
   last 10 s, a player's shot lands near it, or it sees someone shot or killed, **then** it cowers (rule 11)
   and afterwards behaves as in rule 10.
10. A standing scientist reacts in this order of priority:
    - a player's shot lands near it, or it sees another character shot: it **cowers** (rule 11);
    - it sees another character die, or it has been hit before: 25% it **flees** (rule 12); 75% it tries to
      throw a grenade (scientists have a 25% grenade chance; if it cannot, nothing happens);
    - it sees the player: 22% of the time, or always when the player aims at it: if the player is within
      1000 units it **cowers**; else if it has been hit before it reacts as on a death (above); else it
      **dodges** (sidestep 41% or run aside 59%; repeated while the player is in view and within 100 units or
      not in the 50-degree sector behind it);
    - it hears the player, or was shot at or saw / heard the player within the last 10 s: 75% it runs aside;
      when that succeeds and the player is within 100 units or within 90 degrees in front, it reacts as on
      seeing the player;
    - it has a line of sight to the player and the player is within 100 units or in front of it: as on seeing
      the player.
    While a scientist is still moving, only the near-shot reaction applies.
11. **Cower**: while the player aims at it, or has a line of sight to it within 2000 units, the scientist
    keeps its hands up (surrenders, dropping what it holds). It breaks and flees (rule 12, then returns to
    cowering) when a player's shot lands near it, at random (about once every 2 s on average), or as soon as
    the player has neither line of sight nor aim on it or is farther than 2000 units. **If** it is hit more
    than once while cowering, it draws a pistol and fights (draw-TT33-and-attack generic list, global.md).
12. **Flee**: the scientist drops everything it carries and runs to pad 0x17. A player's shot landing near it
    on the way makes it raise its hands for a moment, then it runs on. **When** within 500 units of pad 0x17,
    **if** no player sees it, it fades out (1.5 s) and is removed; otherwise, or if it stopped short, it goes
    back to the behaviour that sent it.
13. **At start** scientists 0x33..0x37 and 0x39..0x42 count as civilians (Doak counts himself, rule 15).
    **When** more than 2 civilians are dead, **then** top text 0x0C2A (failure: civilian casualties) and
    objective e fails.
14. Scientist 0x39 stands still until it sees the player (then rule 10) or the mission clock passes 240 s;
    then it runs to pad 0x0B and switches to rule 10 when it arrives or sees the player on the way.

### Dr. Doak (objective b)
15. **When** a player first enters the room of pad 0x0B, **then** Doak (chr 0x4F) appears on one of pads
    0xEE, 0xEF, 0xF0, 0xF1, 0xF2, 0xF3 (14% each) or 0xF4 (17%), even in view of the player (if that pad is
    blocked, the next pads of the list are tried in order). He counts as a civilian, cannot be auto-aimed,
    and holds the door decoder (tag 0x00, item 38).
16. Doak waits until he has a line of sight to the player, then walks to the player. **When** he is within
    200 units, **if** alive: top text 0x0C11 (Bond tells Doak to go) and Doak keeps facing the player; 5 s
    later, **if** alive: top text 0x0C12 (Doak offers the decoder), the player silently receives the decoder
    and objective b is completed; 8 s after the meeting began Doak flees (rule 12, he keeps fleeing until he
    is gone). **If** the player moves out of reach before the meeting, he follows again.
17. **When** Doak dies before handing over the decoder, **if** the difficulty is 00 Agent or 007, **then**
    top text 0x0C47 (contact killed) and objective b fails.

### Ambushes in the corridors
18. The waiting soldiers of rules 19-22 do not react through the generic guard until their trigger fires;
    the patrolling soldiers (Characters) walk their path and react as the simple generic guard.
19. Soldiers 0x2A and 0x2B (rule 20) stand still until they have a line of sight to the player.
20. **When** 0x2A or 0x2B first has a line of sight to the player, **then** it fires on the move (rule 21)
    and, once for the level (not during the exit scene), four soldiers (body 2, random head) are created on
    pads 259, 260, 261 and 262, each only if its pad is out of the players' view. Each takes an AK-47 (item 8, model 184) and
    hat model 217 (the same hat as the squad), then waits until it sees the player (or, at random, after about 1 s on average, runs to pad 0x3F and
    waits there for a line of sight); then it fires running and attacks (attack generic list), then behaves
    as the standard generic guard (global.md).
21. Soldiers 0x21, 0x22, 0x25, 0x26, 0x2E (and 0x2A, 0x2B of rule 20) stand still until they have a line of
    sight to the player; then they fire while walking (or running when walking fire is not possible) and
    then behave as the standard generic guard (global.md).
22. Soldiers 0x05..0x0B stand guard like a standard guard with idle animations (0.8% chance per tick of an
    idle animation when not already animating). **When** a player is in the room of the guard's watch pad
    (0x1D for 0x05..0x07, 0x39 for 0x08 and 0x0A, 0x3B for 0x09 and 0x0B), or it was shot at / saw the player
    within 10 s, or it sees someone shot or killed, it runs to the player (run-to-player generic list); **when**
    it sees the player, it attacks (attack generic list); **when** it heard the player within 10 s, it sends a
    clone or runs to the player (try-clone generic list); **when** a player's shot lands near it, 50% it is
    startled and runs to the player, 50% it attacks. After each generic action it comes back to this rule;
    once dead it does nothing.

### Trevelyan in the bottling room (objective c)
23. Trevelyan starts by walking to pad 0x7C, then keeps moving between nearby cover pads on the path towards
    the player until he sees the player (rule 24). **If** he is killed here, objective c fails at once (he
    can no longer be met); no text shows, no alarm sounds and the ambush (rules 29-30) never begins. (The
    original leaves c open instead, which only shows at the end as a mission that cannot be won; the rebuild
    fails it as soon as it becomes impossible, by the maintainer's decision.) A hit taken here (without killing him) is remembered: the moment he next sees
    the player, rule 28 (traitor) fires instead of rule 24.
24. **When** Trevelyan sees the player, **then** he runs to the player. The meeting starts when he is within
    250 units: he stops and says 0x0C09 (glad you came), or 0x0C15 (so you came back) if the player had
    walked away before (rule 26). From then on (times from the start of the meeting), each time he walks
    back towards the player if the player is more than 300 units away:
    - 7 s: 0x0C0A (for England, James);
    - 10 s: 0x0C0B (Bond answers for England): objective c is completed;
    - 15 s: Bond's reply depends on his health: 0x0C18 (almost killed myself) below 30%, 0x0C17 (bit tricky)
      below 70%, else 0x0C0C (too easy);
    - 20 s: 0x0C0D (half is luck); 25 s: 0x0C0E (and the other half);
    - 28 s: the alarm goes off; 30 s: 0x0C0F (fate), and the ambush begins (rule 29).
25. During rule 24 and rule 29, **when** Trevelyan dies, **then** top text 0x0C10 (why, James), the alarm goes
    off and the ambush begins (rule 29) if it has not.
26. During rules 23-24, **when** a player enters the room of pad 0x63 (walks away from the bottling room),
    **then** Trevelyan says 0x0C16 (job to do here) and goes back to rule 23.
27. Rule 24 is interrupted by rule 28 as soon as a player hurts Trevelyan.
28. **Traitor**: **when** a player hits Trevelyan during rules 23-24, **then** top text 0x0C25 (golden boy
    traitor), objective c fails, and Trevelyan fights the player: he runs to the player (until within 1000
    units) when he has no line of sight, and fires at the player when he has one. The first time he has a
    line of sight 15 s or more after turning, he says 0x0C26 (why not just be...). **When** he dies: rule 25 in full: its
    text, the alarm, and the ambush begins (Ourumov and his squad, rule 30; Trevelyan's own part of rule 29
    is moot since he is dead).
29. **The ambush**: after the fate line (or Trevelyan's death), Trevelyan runs to pad 0x7C (within 150 units)
    and fights from there: he kneels and aims at a squad member (the first that exists of 0x44..0x48, or pad
    0x6D when none), fires 1 s later, and repeats until Ourumov arrives (rule 31).

### Ourumov and his squad
30. **When** the ambush begins, **then** (not during the exit scene) Ourumov (chr 0x4E) is created once on pad
    0x58 if it is out of view, and he runs to pad 0x7B.
31. **When** Ourumov arrives within 200 units of pad 0x7B and Trevelyan is alive, **then** the urgent music
    (x-track, slot 0) starts, top text 0x0C13 (throw down weapons) and Ourumov aims at Trevelyan. Trevelyan
    (health 1) walks to pad 0x7A; at 200 units from it he says 0x0C14 (finish the job), raises his hands, and
    kneels 1 s later; he stays kneeling until he dies. **If** Trevelyan is already dead when Ourumov arrives,
    Ourumov fights at once (rule 32).
32. **Countdown** (from Trevelyan's last line): Ourumov counts down on the top line: 2 s 0x0C24 (ten
    seconds), 6 s 0x0C19, 8 s 0x0C1A, 10 s 0x0C1B, 12 s 0x0C1C, 14 s 0x0C1D, 16 s 0x0C1E, 18 s 0x0C1F, 20 s
    0x0C20 (nine down to two), 22 s 0x0C23 (one, kill him): Ourumov shoots Trevelyan, who takes TT33 hits to
    body part 8 (killing him). At 23 s, or earlier when the fight starts (a player's shot lands near Ourumov,
    a squad member starts fighting, or the gas has been leaking 5 s), Ourumov **fights**: he fires at
    Trevelyan while he lives (each round also hitting Trevelyan with TT33 damage to body part 8), otherwise at
    the player, firing on the move; reinforcements arrive (rule 34). **When** the gas has been leaking 5 s, or
    Ourumov has been hit more than 5 times, **then** he becomes invincible, sprints to pad 0x17 and fades out
    there (if he stops short he sprints again).
33. **Squad**: from the ambush until the gas has leaked for 5 s, each of chr 0x44..0x49 that does not exist
    is (re)created (not during the exit scene, only when its pad is out of view): 0x44 on pad 0x5C, 0x45 on
    0x5A, 0x46 on 0x5D, 0x47 on 0x5F, 0x48 on 0x59, 0x49 on 0x5B; their cover pads are 0x73, 0x74, 0x75,
    0x76, 0x77, 0x79. A squad member that a player can see at the moment it appears is removed at once. It
    takes an AK-47 and a hat; **if** the player is more than 2000 units from pad 0x7B, it simply hunts the
    player (below). Otherwise it sprints to its cover pad; on arrival it holds the player at gunpoint (aims,
    standing 50% or kneeling 50%, without firing), re-aiming after each move. **When** the fight starts (a
    player's shot lands near any squad member, a player is more than 4000 units from one, Ourumov is shot at,
    the countdown ends, or the gas has leaked 5 s), every squad member fires at the player, re-aiming standing
    or kneeling between bursts. **When** the player is within 500 units of one on its way, or more than 2000
    units from a fighting one, it hunts the player: health set to 8, runs to the player (run-to-player generic
    list), then behaves as the standard generic guard (global.md).
34. **Reinforcements**: once Ourumov has started fighting (and has not fled), each of chr 0x25, 0x26, 0x27
    that does not exist is (re)created on pad 0x5C (out of view only; removed if seen as it appears) with an
    AK-47 and health 8, runs to the player (run-to-player generic list) and then behaves as the standard
    generic guard (global.md). This stops, with the squad respawn, once the gas has leaked for 5 s.
35. **When** the gas leak event starts (the engine's gas-release objects; the bottling room tanks), **then**
    5 s later the squad and reinforcement respawns stop, the fight starts, and Ourumov flees (rule 32).

### The exit (objective e)
36. **When** a player enters the room of pad 0x135 or the room of pad 0x87, **if** alive, **then** objective e
    is completed (unless failed), players can no longer be hurt or pick anything up, the HUD is hidden,
    controls are locked, and the screen fades to black (1 s).
37. **If** not every objective of the difficulty is complete, **then** the level ends (mission failed).
38. Otherwise the exit scene of that room plays (Cinematics) and the level ends as a success.

## Objects and world
- Door 0x26 (prop 102, pad 121): decoder door, rules 3-4. Remotely locked doors 0x27 (prop 63), 0x20 / 0x21
  (props 69 / 74, a linked pair), 0x29 (prop 77), opened by their linked panels (rule 5); door 0x20 is sealed
  by the gas (rule 6). Exit doors 0x22 / 0x23 (props 57 / 58) snap open in one exit scene.
- Gas tanks tags 0x01..0x0A (objective d) and small tanks 0x17..0x1F (rule 6).
- Screens (rule 7). Alarm (rules 24, 25, 28). Urgent music (rule 31). Sounds 0xA1, 0xB8, 0x53.
- No fog, vehicle or aircraft scripting.

## Cinematics

### Intro (setup intro data)
Played as `intro.md` describes; the data below is in its units (world units, API degrees, seconds).

- **Fixed cameras:** 4 (one is picked at random); world units, API degrees, fov 60 (intro.md §1). Lines: 0x0C46
  (location title); no second line; the shot lasts 5 s, then fades to black over 1 s.

  | Camera | Position (x, y, z) | Yaw | Pitch | Room of pad |
  |---|---|---|---|---|
  | 0 | (8669.05, -359.25, -3541.46) | -17.1 | 12.0 | 104 |
  | 1 | (8447.07, -419.25, -3962.70) | 12.8 | 18.0 | 104 |
  | 2 | (524.66, -397.93, 565.66) | -7.1 | 10.0 | 2 |
  | 3 | (8270.73, 280.74, -3480.91) | -27.2 | -14.0 | 104 |

- **Swirl points** in order (intro.md §2): flags per row (2 = player frame: x left, y up, z forward, from the eye
  point; 4 = look at the eye itself); the end marker (flag 1) follows the last row. Duration and tension belong to
  the leg that starts at the point. Swirl length 3.336 s (the legs from P1 on: P0 is only the previous control point, intro.md section 2).

  | Point | Offset (x, y, z) | Duration (s) | Tension | Flags |
  |---|---|---|---|---|
  | P0 | (0, 595, -40) | 0.417 | 0.75 | 6 (player frame, look at eye) |
  | P1 | (-40, 510, 0) | 0.417 | 0.75 | 6 (player frame, look at eye) |
  | P2 | (0, 425, 40) | 0.417 | 0.75 | 6 (player frame, look at eye) |
  | P3 | (45, 340, 0) | 0.417 | 0.75 | 6 (player frame, look at eye) |
  | P4 | (0, 255, -45) | 0.417 | 0.75 | 6 (player frame, look at eye) |
  | P5 | (-45, 170, 0) | 0.417 | 0.75 | 6 (player frame, look at eye) |
  | P6 | (0, 85, 40) | 0.417 | 0.75 | 6 (player frame, look at eye) |
  | P7 | (35, 20, 5) | 0.417 | 0.75 | 6 (player frame, look at eye) |
  | P8 | (10, 0, -25) | 0.417 | 0.75 | 6 (player frame, look at eye) |
  | P9 (final) | (0, 0, 0) | not used | not used | 2 (player frame) |
  | P10 (shape only) | (0, 0, 0) | not used | not used | 2 (player frame) |

- **Intro animation:** no record: table entry 0 (animation 61 from frame 95, speed 0.02; intro.md §3).
- **Body during the swirl:** the table pose only; no level script animates it.
- **Start (setup data):** pad 167; item 5 (silenced PP7), item 29 (remote mine), item 30 (detonator), 100 of ammo
  type 1, 5 of ammo type 7; outfit (cuff) 3; watch 11:52. Other start slots (not used in solo play): slot 1 pad
  109, item 5 (silenced PP7), item 29 (remote mine), item 30 (detonator), 100 of ammo type 1, 5 of ammo type 7;
  slot 2 pad 137, item 5 (silenced PP7), 100 of ammo type 1; slot 3 pad 93, item 8 (KF7), 100 of ammo type 3.

### Exit (all objectives complete)
1. Screen black; every character is hidden; from now any button press fades out and ends the level (skip).
2. **If** the player is not holding a gun (items 4..22, 24, 25), Bond's body is given the silenced PP7
   (item 5) for the scene. The first-person weapon is hidden.
3. Exit through the room of pad 0x87: fixed camera at camera position tag 0x24 (prop 519: (7969.43, -311.01,
   -5450.34), theta 3.17 rad, verta 6.22 rad). Doors 0x22 and 0x23 snap open; Bond's body is placed on pad
   0x86 and plays animation 0x6B, holding the last frame; fade in (1 s); 1 s after the shot starts it plays
   animation 0x68 from the start; when that ends, fade to black and the level ends as a success.
4. Exit through the room of pad 0x135: fixed camera at camera position tag 0x25 (prop 521: (6810.16,
   -288.99, -5319.39), theta 3.15 rad, verta 6.25 rad). Bond's body is placed on pad 0x136 and plays
   animation 0x62 backwards from frame 127; fade in; at 3.1 s it puts away the right-hand weapon; at 3.2 s
   (or when the animation ends) fade to black and the level ends as a success.

## Co-op notes
- Rules 15, 26, 36: "a player" entering a room: any player. The exit should take both players.
- Doak (rule 16) walks to and meets the nearest player; the decoder goes to that player.
- Trevelyan's health-dependent line (rule 24, 15 s) uses the player he met.
- Rule 13 and objectives: shared. Rule 4: the player using the decoder.
- Sight / aim / distance checks: any or the nearest player, as in the generic guard.

## API needed
- existing: `objectives.add/complete/fail/is_complete/is_failed/all_complete`, `mission.win()`,
  `mission.fail()`, `mission.difficulty()`, `wait(seconds)`, `wait(event(...))`, `on(...)`, `clock()`,
  `alarm.trigger()`, `message(text)`, `give(item, player)`, `has(item, player)`, `guard(n):set_ai(list)`,
  `guard(n):sees_player()`, `guard(n):was_shot_at()`, `guard(n):hits()`, `guard(n):surrender()`,
  `guard(n):go_to(target, speed)`, `guard(n):is_moving()`, `guard(n):face(target)`, `guard(n):health()`,
  `guard(n):set_health(n)`, `guard(n):set_max_health(n)`, `guard(n):distance()`, `guard(n):is_dead()`,
  `door(n):open()`, `door(n):close()`, `door(n):lock()`, `door(n):unlock(force)`, `player(n):pos()`,
  event `death`, event `door`.
- new: `chr(n):spawn_at(pad, {body, head, ai, ignore_view})` create a character on a pad (only out of view
  unless `ignore_view`; tries around the pad when blocked) with a character number; false when it failed.
- new: `chr(n):exists()`; `chr(n):remove()` instantly; `guard(n):fade_out()` 1.5 s fade and removal.
- new: `guard(n):set_generic(name, return_to?)` run a global behaviour (standard, simple, deaf simple,
  keyboard, idle animation, attack, run-to-player, startle-and-run, try-clone, draw-TT33-and-attack).
- new: `guard(n):start_patrol(path)`.
- new: `guard(n):set_armour(amount)`, `guard(n):set_ratings{speed=, accuracy=, reaction=}`,
  `guard(n):set_invincible(on)`, `guard(n):set_autoaim(on)`, `guard(n):set_can_shoot_chrs(on)`,
  `guard(n):give_item(item, model, concealed?)`, `guard(n):give_hat(model)`, `guard(n):hold(tag)` take a
  tagged object in hand.
- new: `guard(n):fire_at(target, {kneel=, aim_only=, on_the_move=})` target = the player, a character or a
  pad; `guard(n):throw_grenade(): boolean`; `guard(n):kneel()`; `guard(n):freeze()`; `guard(n):sidestep()`;
  `guard(n):run_aside()`; `guard(n):pick_cover_pad_towards_player(): pad`; `guard(n):walk_to_player()` /
  `guard(n):run_to_player()`.
- new: `chr(n):hit(body_part, item)` apply an item's damage to a body part with the hit reaction.
- new: `guard(n):drop_carried()`; `guard(n):saw_guard_shot()`, `guard(n):saw_guard_die()`,
  `guard(n):heard_player(within_s?)`, `guard(n):noticed_player_within(seconds)`,
  `guard(n):has_line_of_sight()`, `guard(n):is_aimed_at()`, `guard(n):angle_to_player()`,
  `guard(n):on_screen()`, `guard(n):is_animating()`, `guard(n):in_room_of(pad)`.
- new: `guard(n):count_as_civilian()`, `civilians_killed(): number`.
- new: `player_in_room_of(pad, player?)`, `player(n):health_fraction()`, `player(n):equipped(): item`,
  `player(n):give_silent(tag)` the player collects a tagged object without the pickup text.
- new: event `gadget_used{tag, item}` a player used the equipped item on a tagged object; event
  `object_activated{tag}`; event `object_destroyed{tag}`; event `gas_leak` (the engine's gas release began).
- new: `door(tag):seal()` (lock bit 0x80 + both one-way locks + characters cannot operate it),
  `door(tag):state(): "closed" | "opening" | "open" | "closing"`, `door(tag):open_instant()`.
- new: `tv(tag):show(image, screen_index?)` set a monitor's screen image bank.
- new: `sound:play(id, from_tag?)`; `music:play_xtrack(slot)`; `say_top(text_id)`, `say_bottom(text_id)`.
- new: `cinema:begin()` / `cinema:finish()`, `player(n):set_invulnerable(on)`, `player(n):set_pickups(on)`,
  `chrs:hide_all()`, `camera:fixed(tag)`, `screen:fade_out()`, `screen:fade_in()`, `screen:wait_fade()`,
  `skip_on_button()`, `player_body(n):teleport(pad)`, `player_body(n):play_anim(id, start, end, opts)`
  (hold, reverse, blend), `player_body(n):equip(item)`, `player_body(n):holster(hand)`,
  `player_body(n):anim_done()`.
- new: `exiting(): boolean` the exit scene has started (spawns are skipped then).
