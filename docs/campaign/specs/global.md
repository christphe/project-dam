# Generic guard (global behaviours)

Shared by every level: the 18 global AI lists (ids 0x0000..0x0011) that any guard can be given, by the setup or
by a level script. On the Luau side they become one shared guard module in `campaign/scripts/`, with one entry
point per mode below; a level script can have guard 0x05 patrol path 2 and then behave as the simple guard.

How to read this file: **Definitions** names the perception and state checks every mode uses (most of them are
native engine state, listed again in API needed). **Modes** is the table of behaviours with their global list
id, their entry and where they end. **Rules** describe each mode. Unless a rule says otherwise, a mode that
"ends" hands the guard to the **standard guard** mode (rule 10), which is the resting state of nearly every
guard in the game.

A guard runs its behaviour once per AI update (once per game frame). Percentages below are per decision; a
check that is "per update" happens every frame while the condition holds.

## Definitions

These are used by the rules. "The player" means the guard's current target player (see Co-op notes).

1. **Idle.** The guard is idle when it stands still and has finished turning or settling its aim, or walks a
   patrol path, or plays an animation that has finished or that was started as an interruptible idle animation
   (the fidgets of rule 13). It is **busy** in every other state: running to a place, firing, a combat move, a
   hit reaction, dying, the startled look-around, pointing, pressing an alarm, a scripted animation still
   playing. Most modes only make decisions while the guard is idle, so an action is never cut short by the
   behaviour that started it.
2. **Spots the player** (engine sight check, per update). If the guard is inside its alert window (rule 4), it
   spots the player whenever it has a clear line to the player (rule 3), regardless of facing or range.
   Otherwise the player must be within 110 degrees either side of the guard's facing and within the guard's
   vision range (setup value, in metres; at least 200 units in any case) and nearer than the far fog; then a
   random reaction roll decides whether it is noticed this update: the chance falls with distance, falls further
   when the player is more than 45 degrees off the guard's facing, and is scaled by the difficulty's reaction
   speed modifier. A noticed player must also pass the clear-line test. Spotting the player restarts the alert
   window and records where the player is. While the player is flagged invisible to guards (cinematics), the
   guard never spots or has a line to the player.
3. **Clear line to the player.** A straight walkable line on the floor from the guard to the player's floor
   tile, blocked by objects, doors, other characters, path blockers and AI-opaque geometry. No vision range or
   facing is involved.
4. **Alert window.** The 10 s after the guard last spotted the player or finished a hit reaction (being hit
   counts as having seen the shooter; an invincible guard only flinches and does not count). Also refreshed by
   the end of some native moves (the sidestep among them).
5. **Heard window.** The 10 s after the guard last heard the player fire. A player's shot is heard by every
   guard whose distance to the player is less than the guard's hearing scale (setup) times the weapon's
   loudness times 100 units.
6. **Near miss.** A player's shot that passes through the guard's bounds without hitting it. It counts for
   the update it happens in only; the guard also keeps a running total of near misses and a running total of
   hits taken (hits count even when invincible).
7. **Witnessed a hit / a death.** When a character is hit (hit reaction) or dies, the engine tells the other
   guards within 20 m (2000 units) that face it (within 100 degrees either side) and have a line of sight to it,
   a few guards per update. The flag lasts for that update only. It does not fire for a hit on an invincible or
   armoured guard.
8. **Aware.** A per-guard flag, false at spawn. It is set the first time the guard engages, chases or makes the
   reinforcement decision (rules 15, 16, 21). An aware guard never does the pointing gesture of rule 15.
9. **Seen by the player.** A per-guard flag set for the rest of the level the first time the guard is drawn on
   a player's screen.

## Modes

| id | Mode | Entered by | Waits for idle | Ends in |
|---|---|---|---|---|
| 0x0002 | standard guard | setup, level scripts, every mode that ends | yes | never ends; dispatches to the modes below |
| 0x0007 | simple guard | setup / level script (typical for patrolling guards) | no | engage / chase, then standard guard |
| 0x0005 | simple guard, deaf | setup / level script | yes | engage / startle / chase, then standard guard |
| 0x0009 | alarm raiser | setup / level script | yes | alarm run, then chase, then standard guard |
| 0x0006 | engage (one combat action) | standard / simple / chase modes | no | back to the caller's fallback (standard guard) |
| 0x0008 | chase the player | many | no | engage, or the fallback (standard guard) |
| 0x000A | startled, then chase | near miss reactions | yes (its own animation) | chase |
| 0x000B | reinforcement decision | standard guard on hearing a shot | no | chase, or standard guard |
| 0x000C | clone (reinforcement) | given to a clone at spawn | no | chase, then standard guard |
| 0x000D | relentless hunter | level scripts | no | never ends (only death) |
| 0x0010 | draw pistol and fire | level scripts (a surprised unarmed character) | yes (its own steps) | chase, then standard guard |
| 0x0003 | idle fidget (one animation) | standard guard | no | back to the caller |
| 0x0004 | keyboard work (one animation) | level scripts, as a subroutine | no | back to the caller |
| 0x0000 | hold aim on the player | level scripts | no | never ends (a level script changes it) |
| 0x0001 | inert | death, end of other modes, level scripts | no | never ends |
| 0x000E | one-second pause | level scripts, as a subroutine | no | back to the caller |
| 0x000F | end the level | level scripts (exit cinematics) | no | inert |
| 0x0011 | remove self | level scripts | no | inert |

**Fallback.** The modes that "end" return to a fallback mode, which is the standard guard in every use in the
game (the engage, chase, fidget, keyboard and pause modes return to whoever started them; the guard modes always
set the standard guard as the place to return). The shared module should take the fallback as a parameter
(default: standard guard) so a level script can, for example, have a guard chase and then resume its patrol.

**Parameters a level script sets** (all native per-guard setup values, also settable at run time): vision range,
hearing scale, accuracy rating, speed rating (animation speed), grenade probability (0..255), health, the clone
flag, the invincible flag, the preset pad (the alarm raiser's alarm), the patrol path, and the fallback mode.

## Rules

### Standard guard (0x0002)

10. **Standard guard.** Each update: if the guard is dying or dead it becomes inert (rule 30). While it is busy
    it does nothing. When it is idle it checks, in this priority order, and acts on the first that holds:
    1. it spots the player: **engage** (rule 15);
    2. it is in its alert window (was hit or saw the player in the last 10 s): **chase** (rule 16);
    3. it is in its heard window: **reinforcement decision** (rule 21);
    4. a near miss this update: 50% **engage**, 50% **startled, then chase** (rule 17);
    5. it witnessed a hit or a death this update: **chase**;
    6. it is playing an idle fidget already: nothing;
    7. otherwise a 0.8% chance (2 in 256) per idle update to play one **idle fidget** (rule 13): a fidget
       about every 2 s on average at the port's 60 updates per second (about every 6 s at the N64's ~20).
    After any of these modes ends, the guard is back here. Because every alert check reads a 10 s window, an
    alerted standard guard keeps engaging and chasing for 10 s after it last saw the player, was hit, or heard a
    shot, then settles back to idling wherever it stands. (Standard guards are not meant for patrols: a fidget
    interrupts a patrol walk.)

### Simple guards (0x0007, 0x0005)

11. **Simple guard (0x0007).** Every update, idle or not (it reacts even mid-walk on a patrol, and does not
    fidget): if it spots the player it **engages**; else if it is in its alert window, or in its heard window,
    or had a near miss, or witnessed a hit or death, it **chases**. Whatever it did, it then lives on as a
    standard guard. (It never makes the reinforcement decision.)
12. **Simple guard, deaf (0x0005).** Like the simple guard but only decides while idle (patrol walking counts as
    idle), and does not react to gunfire it only hears: spots the player: **engage**; alert window: **chase**;
    near miss: 50% **engage**, 50% **startled, then chase**; witnessed a hit or death: **chase**. Then standard
    guard, which does hear shots from then on.

### Idle and work animations (0x0003, 0x0004)

13. **Idle fidget (0x0003).** One animation, chosen by a random roll, played from frame 0 to the end frame
    given, returning to the idle pose when it completes; the animation's sound may play (the sneeze sound plays
    about half the time). The fidget is interruptible: the standard guard keeps checking for alerts while it
    plays. Then back to the caller.

    | animation | frames | chance |
    |---|---|---|
    | 0x9A yawn | 0..193 | 19.9% (51 in 256) |
    | 0x9B swat flies | 0..294 | 19.5% (50 in 256) |
    | 0x9C scratch leg | 0..183 | 19.5% |
    | 0x9D scratch backside | 0..123 | 19.5% |
    | 0x9E adjust crotch | 0..56 | 19.5% |
    | 0x9F sneeze | 0..137 | 2.0% (5 in 256) |

14. **Keyboard work (0x0004).** One typing animation, random, from frame 0 to the end frame given, no idle pose
    at the end and no sound; then back to the caller (level scripts use it in a loop for technicians at
    consoles).

    | animation | frames | chance |
    |---|---|---|
    | 0xAC right hand typing | 0..69 | 23.8% (61 in 256) |
    | 0xAD right hand typing, second | 0..74 | 23.4% (60 in 256) |
    | 0xAE left hand typing | 0..79 | 23.4% |
    | 0xAF right hand tapping | 0..89 | 29.3% (75 in 256) |

### Engage and chase (0x0006, 0x0008, 0x000A)

15. **Engage (0x0006): one combat action, then back to the fallback.**
    - **First sighting gesture.** If the guard is not yet aware: a 12.9% chance (33 in 256) that it only makes
      the startled pointing gesture at the player (played only when the player is roughly in front of it; else
      nothing visible happens), becomes aware, and returns. The next idle update of the standard guard then
      engages again, now without the gesture.
    - Otherwise it becomes aware and picks an action. The options are considered in this order, each with its
      own roll; an option that is not possible right now falls through to the next:
      1. **throw a grenade**, chance = grenade probability / 255 (0 by default; set per guard). Needs the
         player at least 10 units away and either a grenade in hand or a free hand (a grenade is created in it).
      2. 4.3% (11 in 256): **roll sideways, then fire crouched**. Needs a weapon, the player at least 200 units
         away and 200 units of room on one side (random side first, then the other).
      3. 4.3%: **hop sideways while firing**. Needs the player within 45 degrees of straight ahead or straight
         behind and 200 units of room on one side.
      4. 10.2% (26 in 256): **sidestep**. Same angle condition, 100 units of room.
      5. 19.9% (51 in 256): **run sideways** 200..400 units (random length and side, then the other side),
         across the line to the player. Not within 3 s of the guard's last walking or running attack.
      6. 25.4% (65 in 256): **walk towards the player firing**. Needs a weapon, the player at least 1000 units
         (10 m) away, and not within 3 s of the last walking or running attack.
      7. **run towards the player firing**, same conditions as 6, taken when the roll of option 6 was at most
         127 of 256 (that is: 50% overall at that roll, including the cases where 6 was rolled but impossible).
      8. otherwise **fire at the player**: 50% kneeling, 50% standing. Needs a weapon (an unarmed guard does
         nothing here).
    - Resulting shares when every option is possible and the grenade probability is 0: roll 4.3%, hop 4.1%,
      sidestep 9.3%, side run 16.4%, walk-and-fire 16.7%, run-and-fire 16.2%, kneel-and-fire 16.5%,
      stand-and-fire 16.5%. Inside 10 m, without the walk/run options: the two fire stances get about 33% each.
    - The action is started and the mode returns at once; the engine plays the action to its end (the fire
      sequence is raise, a burst, lower; the guard is busy meanwhile) and the standard guard decides again when
      the guard is idle.
16. **Chase (0x0008).** The guard becomes aware and is sent running to where the player is **now** (the
    player's actual position, not the last known one; the destination is fixed when the run starts). The run
    fails, and the mode ends at once, when the guard is dying or in a hit reaction, when there is no route, or
    when 10 or more guards have spotted the player in the last 2 s (the engine's cap on simultaneous chasers).
    While running: if it arrives or is stopped (idle), the mode ends; else if it spots the player, it
    **engages** (rule 15; the run is replaced by the combat action). The arrival check comes first in an update.
    Because the standard guard re-enters chase for as long as the alert or heard window lasts, a guard that has
    lost the player keeps re-running to the player's current position for up to 10 s.
17. **Startled, then chase (0x000A).** The guard plays the startled look-around (unless it is dying or in a hit
    reaction), waits for it to finish, then **chases** (rule 16).

### Reinforcements (0x000B, 0x000C)

18. **Clones.** A guard with the clone flag (setup) can spawn one clone: a new guard of the same body, standing
    in front of it, holding copies of its weapons (dual weapons stay dual) and its hat, numbered chr + 10000
    (0x2710) when that number is free. Spawning fails without the clone flag or when no character slot is free.
19. **Clone (0x000C).** A freshly spawned clone **chases** the player at once, then lives on as a standard
    guard.
20. **One clone at a time.** A guard has at most one clone: it can send a new one only once the previous clone
    is gone (died and faded out, or removed). A clone that is still dying or lying there counts as existing.
21. **Reinforcement decision (0x000B).** The guard becomes aware. If the player has already seen this guard on
    screen (definition 9), it **chases** (rule 16). If not: when its clone (chr + 10000) exists, it stays where
    it is as a standard guard; otherwise it tries to spawn a clone that will run the clone mode; if that works
    it stays where it is as a standard guard (the clone does the running, so an unseen guard appears to call for
    help), if not it **chases** itself. While its heard window lasts the standard guard re-enters this decision
    every idle update, so an unseen guard with a clone simply stays put (it still engages if it spots the
    player) until the window runs out.

### Alarm raiser (0x0009)

22. **Waiting.** While idle (patrol walking counts) the guard watches: if it spots the player it goes straight
    to the alarm run (rule 23). If it has had any near miss so far (the running total, not just this update),
    or is in its heard window, or witnessed a hit or a death: if it has been hit at least once it goes straight
    to the alarm run, else it first plays the startled look-around, waits for it to end, then makes the alarm
    run. (Being hit is not a trigger of its own here, nor is the alert window; in practice the shot that hits it
    is usually also heard or counted as a near miss. Keep this faithful.)
23. **Alarm run.** The guard runs to its preset pad (setup value) and waits until it stops (arrived, or
    interrupted, e.g. by a hit reaction). If it is within 100 units of the pad and the object standing on that
    pad is not destroyed (any object type: alarm box, panel), it plays the alarm-pressing animation (0x3D,
    frames 40..82; hand chosen by its free hand) and the **alarm sounds** at frame 60 of it: a guard shot or
    killed before then raises no alarm. It waits for the animation to end. If it was too far from the pad or the
    object was destroyed, it skips the alarm. Either way it then **chases** the player, then lives on as a
    standard guard.
    **No preset pad** (a setup preset of 0xFFFF, or a guard spawned by a level script, which starts with
    none): no object stands on such a pad, so the alarm press always fails and **no alarm sounds**; the
    guard goes on to the chase as above. (The original's run towards a missing pad reads outside the pad
    table: undefined, in practice it goes nowhere useful. A rebuild skips the run and goes straight from
    the trigger, startled look-around included, to the chase.)

### Relentless hunter (0x000D)

24. **Hunt.** The guard runs to the player's current position (rule 16's run, same failure cases; a failed run
    still goes on with the next steps). While running, as soon as it has a clear line to the player (definition
    3: no vision cone, no range limit) it **attacks** (rule 26); if it arrives or stops without one, it goes to
    the after-attack check (rule 27).
25. **Watching.** Between actions, each update: if it is invincible and has taken 6 or more hits, it loses its
    invincibility and attacks at once (rule 26). If it is idle, it attacks if it has a clear line to the player,
    else goes to the after-attack check. If it is busy and the player is more than 20 m (2000 units) away, it
    restarts the hunt (rule 24). (Faithful quirk: a busy guard with the player in view beyond 20 m restarts the
    run every update and shuffles on the spot.)
26. **Attack.** If the guard is invincible: with 6 or more hits taken it loses its invincibility; either way it
    goes on. One roll picks a starting option; an impossible option falls through to the next one down this list
    until one starts (the last always starts when armed):

    | roll (of 256) | starting option | chance |
    |---|---|---|
    | 0..10 | throw a grenade (rule 15's grenade chance and conditions) | 4.3% |
    | 11..50 | sidestep | 15.6% |
    | 51..80 | roll, then fire crouched | 11.7% |
    | 81..100 | walk towards the player firing | 7.8% |
    | 101..150 | run towards the player firing | 19.5% |
    | 151..200 | fire standing | 19.5% |
    | 201..255 | fire kneeling | 21.5% |

    The action starts and the guard goes straight to the after-attack check (it does not wait for the action).
27. **After-attack check.** If it has a clear line to the player it goes back to watching (rule 25), which waits
    for the action to end. Without a line: 37.1% back to watching, 62.9% a **probe**: it runs at the player for
    at most 1 s (cancelling any action in progress), stops where it is, and holds there for up to 10 s; a clear
    line to the player or a near miss during the hold sends it back to watching. If the hold runs out, it rolls
    again: 62.9% another probe, 37.1% a full hunt run (rule 24). The mode never ends; only death stops it.

### Scripted one-offs (0x0010, 0x0000, 0x0001, 0x000E, 0x000F, 0x0011)

28. **Draw pistol and fire (0x0010).** Used for characters that are caught unarmed. The guard gets speed rating
    40 and accuracy rating 50 (both 0..100, set as given: the difficulty modifiers are not applied), turns to
    keep facing the player, waits 1/3 s, plays the fast one-handed draw (animation 0x42, frames 0..20, holding
    the last frame) and waits for it to end. It then gets a DD44 in its hand (item 0x06, hand model 0xCD; it
    fails silently if its hands are full or memory is short, and it fights with what it holds), aims at the
    player, opens fire, and waits until it has stopped firing. Then it **chases** the player and lives on as a
    standard guard.
29. **Hold aim (0x0000).** If armed, the guard raises its weapon and keeps it aimed at the player (tracking,
    not firing) until a level script gives it another behaviour. Unarmed: it does nothing.
30. **Inert (0x0001).** The guard's behaviour does nothing, forever (the engine still runs its body: a patrol in
    progress, an animation, death and fade-out). Every mode ends here once the guard dies.
31. **One-second pause (0x000E).** Wait 1 s, then back to the caller.
32. **End the level (0x000F).** The level ends now, as when the player reaches the exit: the mission result is
    whatever the objectives say. (On N64 with the hi-res mode the end is deferred one frame; the port has no such
    concern.) Then inert.
33. **Remove self (0x0011).** The guard vanishes at once: no death, no fade, and it drops nothing (unlike a
    normal removal or death). Then inert.

### What the engine does by itself

34. **Native, not part of any mode:** hit reactions and death animations (the guard is busy during them), item
    drops on death, body fade-out, the patrol walk along a path, the whole fire sequence and its burst length,
    aiming accuracy (accuracy rating plus the difficulty's accuracy modifier), animation speed (speed rating plus
    the difficulty's reaction modifier), how the sight roll of definition 2 scales with difficulty, the 10 s
    windows, the chase cap of rule 16, the alarm's sound and timeout once raised, and the per-update clearing of
    the near-miss and witnessed flags. The Luau module only decides; it never reimplements these.

## Co-op notes

- Every check about "the player" (spots, clear line, distance, angle) is asked for each living player in turn;
  the first player for whom it holds becomes the guard's target, and the following actions (run to, fire at,
  throw at) aim at that target. When nobody satisfies it the target stays as it was. The module should do the
  same: `spots_player()` returns the player it spotted, and the verbs take that player.
- Chase runs to the target player's position; with no target yet, the nearest living player.
- Hearing (definition 5) is per shooter: the guard hears a shot when it is within range of the player who fired.
- The clone decision's "seen on screen" counts a sighting on any player's screen.
- End the level (rule 32) ends it for everyone.

## API needed

Verbs and queries on a guard target (`guard(n)`), unless noted.

- existing: `guard(prop)` the guard target; `player(n)` a player target.
- existing: `target:set_ai(list)` give a guard a global or level AI list (the level scripts' way into these modes
  until the module replaces them).
- existing: `target:is_dead()` dying or dead (rules 10, 30).
- existing: `target:hits()` hits taken so far (rules 22, 25, 26); must count hits on an invincible guard.
- existing: `target:was_shot_at()` a near miss this update (rules 10, 12, 27).
- existing: `target:sees_player()` must be exactly the engine sight check of definition 2 (cone, range, fog,
  reaction roll, alert-window shortcut, line test), with its side effect of restarting the alert window; new
  return value: the player number spotted, or nil.
- existing: `target:distance()` distance to the nearest living player; new: `guard:distance_to_target()` to the
  guard's current target player (rule 25's 20 m test).
- existing: `target:distance_to(other)` guard to a pad (rule 23's 100 units).
- existing: `target:pos()`, `clock()`, `wait(seconds)`, `spawn(fn)`, `on(name, fn, filter)`, `event(...)`.
- existing: `alarm.trigger()` (not used directly: the alarm raiser's press raises it natively).
- existing: events `gunfire`, `guard_hurt`, `death`, `alarm` (a module may listen instead of polling).
- new: `guard:is_idle()` true when the guard is idle as in definition 1 (stand settled, patrol, finished or
  interruptible animation).
- new: `guard:is_playing_anim()` an animation (scripted or fidget) is in progress.
- new: `guard:has_clear_line_to_player()` definition 3; returns the player number or nil.
- new: `guard:saw_player_within(seconds)` the alert window (definition 4); the game uses 10.
- new: `guard:heard_shot_within(seconds)` the heard window (definition 5); the game uses 10.
- new: `guard:near_misses()` the running near-miss total (rule 22).
- new: `guard:witnessed()` returns `"hit"`, `"death"` or nil, with the chr witnessed, for this update
  (definition 7).
- new: `guard:has_been_seen()` drawn on a player's screen at least once (definition 9).
- new: `guard:target_player()` the guard's current target player (co-op).
- new: `guard:point_at_player()` the startled pointing gesture; does nothing unless the player is in front and
  the guard is able (rule 15).
- new: `guard:look_around()` the startled look-around animation (rules 17, 22).
- new: `guard:run_to_player(speed?)` route to the target player's current position ("walk" | "run" default |
  "sprint"); false when dying or in a hit reaction, no route, or 10+ guards spotted the player in the last 2 s
  (rule 16).
- new: `guard:fire_at_player(stance)` stance `"stand"` | `"kneel"`: the full native fire sequence at the
  target player; false when unarmed.
- new: `guard:aim_at_player(stance?)` raise the weapon and keep the target player covered without firing
  (rules 28, 29); false when unarmed.
- new: `guard:open_fire()` turn a held aim into firing at the same target (rule 28); false when not aiming.
- new: `guard:combat_move(kind)` kind `"sidestep"` | `"side_hop"` | `"side_run"` | `"roll_fire"` |
  `"walk_fire"` | `"run_fire"`; starts the move with the native conditions of rule 15 (angle, room, range,
  3 s since the last walk/run attack, armed); false when not possible.
- new: `guard:throw_grenade()` rolls the guard's grenade probability and, on success and when possible, throws a
  grenade at the target player (rule 15); false otherwise.
- new: `guard:grenade_probability()` / `guard:set_grenade_probability(n)` 0..255.
- new: `guard:halt()` stop any move or action and return to the idle pose (rule 27's end of a probe).
- new: `guard:face_player()` keep turning to face the target player while standing (rule 28).
- new: `guard:play_anim(id, opts)` any animation id with `{ from = frame, to = frame, idle_pose_after = bool,
  sound = bool, hold_last = bool }` (rules 13, 14, 28); the existing named form lacks most fidget and typing ids.
- new: `guard:give_weapon(item, model)` put a weapon in the guard's free hand; false when the hands are full
  (rule 28: item 0x06, model 0xCD).
- new: `guard:set_speed_rating(n)`, `guard:set_accuracy_rating(n)` 0..100, as given, no difficulty modifier.
- new: `guard:is_invincible()`, `guard:set_invincible(on)` (rules 25, 26).
- new: `guard:preset_pad()` the guard's setup preset pad (rule 23).
- new: `pad(n)` a pad target constructor (for `go_to` / `distance_to` with a pad number).
- existing: `target:go_to(target, speed)` run to the preset pad (rule 23); it must not give the guard a passive
  list as `follow` does.
- new: `guard:press_alarm(pad)` start the alarm-pressing animation at the object on that pad; false when the
  object is destroyed or missing or the guard is dying or hit; the alarm sounds partway through (rule 23).
- new: `guard:can_clone()` has the clone flag; `guard:clone(mode)` spawn the clone of rule 18 and start the given
  mode on it, returning the clone's guard target or nil; `guard:clone_exists()` its clone (chr + 10000) is alive
  and present (rules 18, 21).
- new: `guard:patrol(path)` start walking a setup patrol path; `guard:is_patrolling()` (level scripts' "patrols
  path 2, then ..."; patrol counts as idle).
- new: `guard:remove()` vanish at once, no drop (rule 33).
- new: `mission.finish()` end the level now with the result the objectives give (rule 32).
- new (shared module, `campaign/scripts/`): `guard_ai.start(g, mode, opts)` run one mode on guard `g`, where
  mode is `"standard"`, `"simple"`, `"simple_deaf"`, `"alarm_raiser"`, `"engage"`, `"chase"`, `"startle_chase"`,
  `"reinforce"`, `"clone"`, `"hunter"`, `"draw_and_fire"`, `"hold_aim"`, `"inert"`, `"remove"`; `opts.fallback`
  the mode to go to when it ends (default `"standard"`), `opts.alarm_pad` (default the preset pad). Starting a
  mode cancels the guard's previous one. Plus the subroutines `guard_ai.fidget(g)`, `guard_ai.type(g)` and
  `guard_ai.mode(g)` (the current mode name).
- Randomness: Luau `math.random`; every percentage above is a fresh roll unless the rule says it reuses one
  (rule 15 option 7).
