# How a solo level's intro plays (shared by every level spec)

Each level spec lists its intro data: fixed intro cameras, swirl points, an optional intro animation, the
start pad and items. This file says how the original game plays that data, so a script can rebuild the intro
on the mission API's `camera` and `cinema` verbs. Times are in seconds at 60 ticks per second (the setup counts
ticks of 1/60 s; the PAL build runs the same timeline in its own 1/50 s ticks). Distances are world units.

## The timeline

1. **One fixed camera** (when the level has any): fade in from black, hold, its text lines, fade to black.
2. **The swirl** (when the level has swirl points): fade in from black, a spline travelling that ends at the
   player's eyes, with the player's third-person body visible and posed.
3. **Hand-over:** a cut to first person, the start weapon in hand, the mission clock starts.

A level with no fixed camera starts at step 2. A level with no swirl point goes from step 1 straight to step 3.
Cuba has neither (and no intro-animation record): none of this sequence plays there.

## 1. The fixed intro camera

**Which one.** Only ONE fixed camera is shown, not all of them in turn: the game picks one of the level's
fixed-camera records at random with equal chances (one draw of the game's random generator at level start).
So Dam's six cameras are six alternatives; each play of the level shows one of them. A script that wants
variety picks one at random; a script may also play them in turn as a deliberate change, but that is not the
original. (Measured on the original: the game's own debug override that picks a camera by number counts the
records from the LAST one backwards, so number 0 shows a level's last record. Compare cameras by position, never by
number: Dam's record 5 at (7424.92, 1160.29, 3484.91) came out at yaw -143.24, pitch -12.00 and record 0 at
(17608.08, -59.79, 17408.57) at yaw 162.43, pitch 25.99 on the running game, equal to the converted table below.)
The fixed camera is ONE record: it never moves along points, and has no waypoints.

**The record's units.**
- x, y, z: signed integers in hundredths of a world unit. Divide by 100 to get world units.
- yaw, pitch: fixed point 16.16, in RADIANS. Divide by 65536 for radians (times 180/pi for degrees). Pitch
  values above pi are negative angles (subtract 2 pi): Dam camera 1's raw 379747 is 5.794 rad = 332 deg = -28 deg.
- The game's look direction for (yaw Y, pitch P) is (cos P sin Y, sin P, -cos P cos Y): yaw 0 looks along -z,
  yaw grows toward +x, pitch positive looks up.
- **Conversion to the API** (yaw 0 = +z, positive turns left, pitch positive up; verified by measurement, see above):
  `api_yaw = 180 - game_yaw_deg` (wrap into -180..180), `api_pitch = game_pitch_deg` (unchanged).
  A level spec that writes yaw / pitch (deg) straight from the record (Dam's does: 18 / 26) gives the game's
  degrees; convert them with this rule.
- pad: a pad in the room the camera stands in. It only tells the renderer which room the camera is in (what to
  draw); it does not move the camera. The API's world-point camera needs nothing for it.
- text ids: the first text word holds two 16-bit halves; the LOW half is the first line's text id (the high
  half is 0 in every retail level and is not shown). The second text word is the second line's id; 0 = no second line.

**How long, and the text.** The camera is static (no movement, no turn) for the whole shot. Field of view: the
game's normal 60 degrees vertical (fov unchanged). Times from the moment the shot starts:

| Time (s) | With a second text line | With no second text line |
|---|---|---|
| 0 to 1 | fade in from black (1 s) | same |
| 2 | first line appears on the bottom message line (where pickup messages appear), bold font, for 2 s | same |
| 5 | second line appears, same place, for 2 s | the shot ends: fade to black starts |
| 8 | the shot ends: fade to black starts | |
| 9 | black; the swirl starts | 6: black; the swirl starts |

A bottom message normally stays 2 s; when another message is queued behind it, the one on screen is cut to
0.5 s. The two intro lines are 3 s apart, so each gets its full 2 s. When the swirl starts, the bottom message
line is cleared.

**Fog.** During the fixed shot (and its fade to black) the game loads the level's alternative intro fog and
view-distance setting; the swirl and the rest of the level use the normal one. It matters: measured on Dam, the
fixed shots in the normal setting are drowned in fog (the dam and the mountains fade into blue), in the intro one
they are clear. A script asks for it with cinema.begin({ environment = "intro" }) for the fixed shot; the swirl's
begin (no environment) goes back to the normal one.

**Transitions.** Fixed shot to swirl: fade to black over 1 s on the fixed view, then the swirl opens at its first
point with a fade in from black over 1 s. Never a cut.

## 2. The swirl

**Records and windows.** A swirl point is one record (an offset, a duration, a tension, flags); the camera is NOT
three waypoints per shot. For each leg the spline reads a window of FOUR consecutive records: the one before the
leg's start, the start, the end and the one after (the end of the list is clamped: a window never reads past the
last point, and before the first record it repeats the first). The leg itself runs between the middle two.

**Frame of reference.** Each swirl point is an offset from the PLAYER, never from a pad or a world origin:
- The origin is the player's first-person eye point (the start pad's floor plus the eye height, about 175 units
  above the floor; the camera follows a smoothed copy of that point, which is the same point while the player
  stands still). The mission API's `player(n):pos()` reports this same point (UNCERTAIN: confirm on a run that
  its y is the eye, not the feet).
- With the point's flag 2 set (all 192 retail points: 183 carry 2, Facility's nine carry 6 = 2 + 4), the offset is in the player's own frame,
  turned with the player's facing (the start pad's look direction): **x to the player's LEFT, y up, z FORWARD.**
  Without flag 2 the offset is in world axes (still added to the player's eye point).
- To world axes, with the facing as an API yaw psi, the player's own facing at the swirl (the game keeps it as a
  unit vector (sin psi, cos psi); a script reads it from the player, never from a pad's look vector: measured on
  Dam and Facility, both showed (-1.0000, -0.0056), psi = -90.3 degrees, which is NOT the -37.0 that the start
  pad's look vector as listed in an earlier version of this page gave):
  `world_x = x cos psi + z sin psi`, `world_y = y`, `world_z = -x sin psi + z cos psi`.
- The point's pad (or -1) only names the room the camera is in for drawing (-1 = the player's room). It does not
  move the camera. (Dam's spec reads it as relative to the pad; that reading is wrong.)

**Units of a point.** x, y, z, scale and duration are fixed point 16.16: divide the raw word by 65536.
- x, y, z: world units after that division.
- duration: ticks after that division; divide by 60 for seconds. 0x00640000 = 100 ticks = 1.667 s.
- scale: the spline tension of the leg that STARTS at this point (0.5 = a Catmull-Rom curve; smaller = tighter,
  flatter around the points; larger = looser). Dam uses 0.5, one 0.51 and one 0.175.

**Which points and legs.** The point list ends at the first record with flag 1 (an end marker, not a point).
With points P0..Pn before the end marker (measured on Dam, 10 points, and Facility, 11 points):
- **The camera starts at P1, not P0.** The game starts its leg counter at 1: P0 is only the previous control point
  of the first leg (the levels list P0 and P1 as the same point most of the time, to make the curve start still).
  The camera is exactly at P1 when the swirl opens and travels P1 to P2, P2 to P3, ... and stops at **P(n-1)**: the
  last leg is P(n-2) to P(n-1) (the game ends the swirl when the record three after the current leg's start is the
  end marker).
- Pn is only a shape control for the last leg; it is never reached.
- The leg from Pk to Pk+1 lasts Pk's duration. The durations of P0, P(n-1) and Pn are not used: P0's leg never
  plays. The swirl lasts the sum of the durations of P1 to P(n-2). (Dam: 9.89 s = 593 ticks, measured 588 to 600;
  Facility: 3.336 s = 200 ticks, measured 192 to 204. Counting P0 would add its leg: 11.56 s and 3.75 s, which the
  original does not show.)
- In the API, where a point's `time` is the time to reach THIS point, point k+1 gets Pk's duration, from P1 on.

**The curve.** A cubic cardinal (Hermite) spline through the points: on the leg Pk to Pk+1 the tangent at Pk is
`scale * (Pk+1 - Pk-1)` and at Pk+1 it is `scale * (Pk+2 - Pk)`, with Pk's scale; on the first leg (P1 to P2) the
previous point is P0. The curve parameter runs linearly with time over the leg (no easing): API `smooth = true` and
`ease = "linear"`. Measured: this model, with the player's frame above and these tensions, puts the camera within
1 unit (Facility) and 10 units (Dam) of the original's camera at every sampled tick of the swirl.

**Where it looks.** At the player's eye point, pushed 40 units forward along the player's view direction (the
start facing, level, since the player cannot move). Flag 4 on a point means look at the eye point itself at
that point: across a leg the 40-unit push is 40 when neither end has flag 4, 0 when both do, and blends linearly
over the leg when one end does. Only Facility uses flag 4 (its nine points carry 6 = 2 + 4). An API script
gives each point the yaw / pitch from the point toward that look point (the API then blends yaw / pitch per leg,
a close approximation), or a look target if it has one at that spot.

**Field of view.** 60 degrees vertical throughout, as in first person.

**Hand-over.** When the last leg ends, the camera is at P(n-1). In the retail levels that point is offset
(0, 0, 0): the camera is at the eyes looking 40 units ahead, which is the first-person view, so the game simply
cuts to first person (no fade) and the cut is invisible. A script ends its travelling there and resets the
camera to the player's own view at once.

### Camera position by time (Dam, computed from the rules above, checked on the running port)

Eye point E = (20197.74, 60.31, 16902.07), player facing psi = -90.3 degrees. Positions are the camera's world position; "dist" is the horizontal distance from E; "x / z from E" is the world offset. These are the values a correct swirl must reproduce; the port's scripted swirl was measured at ten of these times (0.52 s to 9.52 s) and agrees within 0.2 units at every one.

| Time (s) | Leg | Camera x | Camera y | Camera z | x from E | y from E | z from E | dist |
|---|---|---|---|---|---|---|---|---|
| 0 | 1 (P1) | 15014.0 | 990.3 | 19685.1 | -5184 | 930 | 2783 | 5884 |
| 0.5 | 1 | 15238.8 | 904.8 | 19694.9 | -4959 | 844 | 2793 | 5691 |
| 1 | 1 | 15565.5 | 779.2 | 19713.1 | -4632 | 719 | 2811 | 5418 |
| 1.5 | 1 | 15921.4 | 646.3 | 19721.2 | -4276 | 586 | 2819 | 5122 |
| 2 | 1 | 16233.9 | 538.8 | 19700.7 | -3964 | 478 | 2799 | 4852 |
| 2.199 | 2 (P2) | 16330.1 | 510.3 | 19680.5 | -3868 | 450 | 2778 | 4762 |
| 2.5 | 2 | 16474.9 | 480.2 | 19623.4 | -3723 | 420 | 2721 | 4611 |
| 3 | 2 | 16637.0 | 478.5 | 19490.5 | -3561 | 418 | 2588 | 4402 |
| 3.5 | 2 | 16772.5 | 491.0 | 19310.1 | -3425 | 431 | 2408 | 4187 |
| 3.866 | 3 (P3) | 16902.1 | 478.3 | 19147.7 | -3296 | 418 | 2246 | 3988 |
| 4 | 3 | 16960.3 | 465.9 | 19080.8 | -3237 | 406 | 2179 | 3902 |
| 4.5 | 3 | 17199.4 | 400.8 | 18790.1 | -2998 | 340 | 1888 | 3543 |
| 5 | 3 | 17460.1 | 319.7 | 18456.1 | -2738 | 259 | 1554 | 3148 |
| 5.5 | 3 | 17725.7 | 241.0 | 18107.2 | -2472 | 181 | 1205 | 2750 |
| 5.533 | 4 (P4) | 17743.0 | 236.3 | 18084.4 | -2455 | 176 | 1182 | 2725 |
| 6 | 4 | 17973.8 | 164.7 | 17715.2 | -2224 | 104 | 813 | 2368 |
| 6.5 | 4 | 18219.3 | 86.7 | 17287.5 | -1978 | 26 | 385 | 2016 |
| 7 | 4 | 18508.7 | 26.7 | 16935.7 | -1689 | -34 | 34 | 1689 |
| 7.2 | 5 (P5) | 18647.0 | 12.3 | 16841.4 | -1551 | -48 | -61 | 1552 |
| 7.5 | 5 | 18957.6 | 3.8 | 16749.9 | -1240 | -56 | -152 | 1249 |
| 8 | 5 | 19577.5 | 22.9 | 16743.3 | -620 | -37 | -159 | 640 |
| 8.5 | 5 | 20102.0 | 50.5 | 16796.2 | -96 | -10 | -106 | 143 |
| 8.622 | 6 (P6) | 20186.3 | 54.3 | 16802.0 | -11 | -6 | -100 | 101 |
| 9 | 6 | 20298.6 | 71.1 | 16882.6 | 101 | 11 | -19 | 103 |
| 9.14 | 7 (P7) | 20304.7 | 74.3 | 16902.7 | 107 | 14 | 1 | 107 |
| 9.5 | 7 | 20261.6 | 69.0 | 16908.9 | 64 | 9 | 7 | 64 |
| 9.889 | 7 (P8 (hand-over)) | 20197.7 | 60.3 | 16902.1 | 0 | 0 | 0 | 0 |

Leg = the leg's number (leg k runs from Pk to Pk+1 and lasts Pk's duration). Mean camera speed per leg (straight distance between the two points over the leg's duration; the curve is a little longer):

| Leg | From -> to | Duration (s) | Straight distance | Mean speed (units/s) |
|---|---|---|---|---|
| 1 | P1 -> P2 | 2.199 | 1401 | 637 |
| 2 | P2 -> P3 | 1.667 | 782 | 469 |
| 3 | P3 -> P4 | 1.667 | 1377 | 826 |
| 4 | P4 -> P5 | 1.667 | 1553 | 932 |
| 5 | P5 -> P6 | 1.422 | 1540 | 1083 |
| 6 | P6 -> P7 | 0.518 | 157 | 303 |
| 7 | P7 -> P8 | 0.75 | 108 | 144 |

**When the swirl ends (timing).** The swirl ends in the very tick where the last leg's timer reaches its duration:
593.4 ticks (9.889 s) after it opens on Dam, to the tick. The hand-over (body removed, first person, start weapon
drawn) happens in that tick, with the camera exactly on the eye point. Measured on the port's scripted swirl: the
camera reached the eye on time, but the script ended 627 ticks (10.45 s) after the swirl opened, 34 ticks (0.57 s)
late, because it counted the swirl's length by adding up 0.1 s waits (each wait overshoots a little). A script ends
the swirl when the camera arrives (`camera.wait()`), or measures the elapsed time on the mission clock; it never
adds up short waits.

## 3. The intro animation and the body

- At the swirl start (the moment it fades in) the player's third-person body is shown at the player's position and
  facing, fully opaque, holding the start weapon (the first start-item record, right hand), and starts its intro
  animation at once: frame 0 of the swirl, not later.
- The intro animation record is an index (0 when the level has no record) into this fixed table. The animation
  ids are the game's animation numbers; speed 1.0 = 60 animation frames per second.

  | Index | Animation | From frame | To frame | Speed | Levels |
  |---|---|---|---|---|---|
  | 0 | 61 | 95 | its end, then held | 0.02 (1.2 frames per second: a near-still pose) | every level without a record |
  | 1 | 66 | 7 | 40, then frames 86 to 131 of the same animation (blended over 24 frames), held at 131 | 0.5 | Bunker 1 |
  | 2 | 97 | 0 | end, held | 0.5 | none |
  | 3 | 98 | 0 | end, held | 0.5 | none |
  | 4 | 99 | 0 | end, held | 0.5 | none |
  | 5 | 100 | 0 | end, held | 0.5 | none |
  | 6 | 102 | 0 | end, held | 0.5 | none |
  | 7 | 103 | 0 | end, held | 0.5 | none |
  | 8 | 176 | 0 | end, held | 0.5 | none |

  Only Bunker 1 has an intro animation record (index 1); every other level uses index 0.
- **When the weapon is in the hand (checked on the original, Dam).** The game puts the start weapon into the body's
  right hand when the swirl opens (the third-person body is built then, with the start weapon as its held item), not
  later: on a close-up capture of the original Dam swirl the silenced pistol hangs in the right hand, barrel down,
  from the first swirl frame through the near-still pose, and stays in the hand when Dam's script animation raises it
  (about 6 s in). No empty-handed phase appears on Dam. The maintainer's report of an animation first played
  empty-handed, then the weapon given, could not be reproduced on Dam; it may belong to the one level with an
  intro-animation record (Bunker 1: frames 7 to 40 play before the draw frames 86 to 131), which could not be run in
  this build (its level load crashes, with or without a mission): UNCERTAIN, to be checked on a Bunker 1 capture.
  The first-person weapon is different: it is only equipped at the hand-over (section 4).
- **A level script can replace that pose.** In ten levels a level script, running from the swirl start, plays
  its own animation on the third-person body (the same animations as table entries 2 to 8: 97, 98, 99, 100,
  102, 103), overriding the table pose from that moment. Dam's animation 99 at 6 s into the swirl is one of
  these: the body shows the entry 0 pose for 6 s, then animation 99 up to frame 96. Each level spec lists its
  own under Body during the swirl.
- **When the body disappears.** When 0.5 s of the swirl remain, the body fades out over 0.5 s, from its current
  opacity to invisible, so it is gone exactly at the hand-over, where it is removed. During the fade the body
  stays where it is, keeps its animation and keeps the weapon in its hand (the weapon fades with it); the camera
  reaches the eyes inside the fading head. A script does it with `bond_body():fade(0, 0.5)` (the body's
  opacity goes to 0 over 0.5 s, the body in place); an instant `show(false)` or a guard's `fade_out` is not it.
- **The hand-over, in the order the original does it** (the end of the swirl, or a skip's fade to black):
  1. **The body is removed**, not just made invisible. While the swirl plays the third-person body occupies both of
     the player's weapon slots (its own model, and the start weapon it holds) and the player's hands are locked
     empty; that is why no first-person weapon can show during the swirl. Removing the body frees the player's
     hands: three game ticks (0.05 s) later the game loads the player's weapon models back into both hands. A script
     that only fades the body to 0 (or hides it) leaves the hands locked: **the player then has no weapon on screen
     at all, and no draw animation** (measured on our script: the HUD shows the ammo, the gun never appears).
  2. **First person starts** (the camera is the player's again, input is back, the mission clock runs from here).
  3. **The start weapon is equipped with the game's draw animation**, in the same tick as step 2: both hands at once.
     Right hand: the right item of the level's FIRST start-item record; left hand: that record's left item when it
     has one (dual wield), otherwise empty. A level with no start-item record equips the fist (right hand), left
     empty. The weapon rises into view during the first moments of control; this is the same animation as selecting
     a weapon from the watch menu. It is skipped only if the watch menu is mid-animation (never at a hand-over).
  4. **Screen:** after a skip's fade to black (section 4) the screen fades in from black over 1 s as first person
     starts. After an unskipped swirl there is nothing to fade (the last point is the eye, the cut is invisible).
     On Cuba (no intro at all) the screen is black from the first tick and the level script fades in.
  A script does the same with `bond_body():remove()` (step 1; call it after the body has faded) followed at once by
  `player(n):force_weapon(item)` (step 3, right hand; the left is emptied: a left item of a dual-wield start is not
  reachable from the API yet, UNCERTAIN whether any of the 21 solo levels' first start item has one: the exported
  data carries only the right item). An instant `show(false)` or a guard's `fade_out` is not a removal.

- **Where the body stands and faces.** The body is built where the PLAYER is, turned the way the PLAYER faces (Dam:
  psi = -90.3 degrees, the same facing the camera frame uses), and nothing places it afterwards: no pad is involved.
  The start pad's own look direction is a different angle (-37 degrees on Dam). A script that teleports the body to
  the start pad turns it 53 degrees away from the original's and may push it a little off the player's spot: on the
  port the body was seen turned and off to the side while the camera, which is built on the player's frame,
  was right. A script leaves the body where `show(true)` puts it.
- **What the body holds.** Built with the start weapon (the first start item) in its right hand, as a second model
  in the player's left weapon slot; this is what makes the pistol hang in the hand. The game does this only
  because the body is built at the swirl's opening; a body built at that moment by a script must be built the same
  way. Measured on the port: the body had an EMPTY hand for the whole swirl (the weapon was "held" only in the
  player's own state), so no gun was ever drawn on it.
- **The animation names** (the table above; the ids are the game's animation numbers): 61 extending left hand;
  66 fire standing draw one-handed weapon fast; 97 draw one-handed weapon and look around; 98 draw one-handed weapon
  and stand up; 99 aim one-handed weapon left and right (Dam's script animation, frames 0 to 96); 100 cock
  one-handed weapon and turn around; 102 cock one-handed weapon, turn around and stand up; 103 draw one-handed
  weapon and turn around; 176 bond eye fire. So Dam's body, 6 s into the swirl, goes from the near-still
  "extending left hand" pose to AIMING the pistol left and right.
- **The body's intro action.** While the table animation is the body's current action, when it reaches its end
  frame: if that frame is below 86 the game plays animation 66 frames 86 to 131 (the quick draw) at the same speed,
  blended over 24 ticks; otherwise the body stands idle. A level script's animation replaces this action (Dam's
  animation 99 is held on frame 96, so the action never ends).
- **Animation speed.** Each animation starts at its own speed: 0.5 (30 animation frames a second) unless the
  table gives one (entry 0: 0.02). The speed of the previous animation NEVER carries over. Measured on the port:
  the near-still pose's 0.02 leaked into Dam's animation 99, which then crawled at 2 % speed (1.1 frames in
  3.7 s instead of 96 frames in 3.2 s) and Bond never aimed. A script step without a speed means 0.5.
- **Open (UNCERTAIN).** On the original, 2.5 to 3.5 s after animation 99 starts (swirl 8.5 to 9.5 s), Bond holds the
  pistol UPRIGHT by his shoulder, the arm bent. On the port, with the same animation, no weapon and the body turned
  off, the arm is stretched forward. Re-check once the body is placed and armed as above: if the pose still differs
  the animation's frame range or the aim-sweep phase is wrong.

## 4. What the game does at the same time

- **Player frozen.** No movement, turning, firing or item use from the first intro frame to the hand-over. The
  first-person weapon is not drawn; the start weapon is put in hand at the hand-over.
- **HUD.** Hidden: no crosshair, health, ammo or weapon. Only the bottom message line, the top text line, a
  countdown, fades and credits can show.
- **Mission clock.** Does not run until the hand-over (the level's elapsed time starts at 0 there).
- **Invulnerability.** The game sets nothing special; guards are normally not near the start. UNCERTAIN whether
  any guard can hurt the player during an intro; the API's `cinema.begin()` (frozen and invulnerable) is the safe choice.
- **Music.** The level's music (and its ambient track when the level has one) starts when the level loads, i.e.
  with the first intro frame. Nothing new starts at the swirl or the hand-over.
- **Skip.** Buttons A, B, Z, START, L and R, on a fresh press (a held button does not count), and only while no
  level script has locked the controls. C buttons, the stick and the d-pad do not skip.
  - During the fixed shot (any time, also during its fade in): the shot ends at once and the normal 1 s fade to
    black to the swirl follows. The swirl itself still plays.
  - During the swirl, only while more than 1 s of it remains: a fade to black over 1 s (from wherever the current
    fade is), then the hand-over (body removed, first person), then a fade in from black over 1 s.
  - So skipping all of it takes two presses: one for the fixed shot, one for the swirl.
- Multiplayer has no fixed shot and no swirl (a short orbit around each player instead).

## 5. Worked example: Dam

Measured on the running original: the player's facing at the swirl is (sin psi, cos psi) = (-1.0000, -0.0056), psi =
-90.3 degrees (API yaw), the eye point (20197.65, 60.28, 16902.07). (An earlier version of this page took psi = -37.0
from the start pad's look vector; that was wrong, a script reads the player's own facing.) Dam has six fixed cameras
(one is picked at random), each with lines 0x2C1A (location title) and 0x2C1B (time caption), so each shot lasts
8 s plus the 1 s fade out.

Fixed cameras, converted (world units; API degrees; fov 60):

| Camera | pos | yaw | pitch | Room of pad |
|---|---|---|---|---|
| 0 | (17608.08, -59.79, 17408.57) | 162.4 | 26.0 | 312 |
| 1 | (16740.06, 1261.11, 20253.95) | 170.1 | -28.0 | 298 |
| 2 | (8947.16, 2920.27, 4098.50) | -146.6 | -22.0 | 269 |
| 3 | (16872.39, 60.30, 9502.19) | 155.1 | 6.0 | 6 |
| 4 | (5248.06, 480.29, -8941.07) | -41.2 | -12.0 | 69 |
| 5 | (7424.92, 1160.29, 3484.91) | -143.2 | -12.0 | 273 |

Swirl points (all flag 2), local offset (left, up, forward), turned into world-axis offsets from the player's eye
point (psi = -90.3), the time to reach each point from the swirl start, and the yaw / pitch toward the look point
(eye + 40 units forward). The camera starts at P1; P0 (the same point as P1 here) is only the previous control point
of the first leg:

| Point | Local offset | World offset | Reached after (s) | Tension of the leg after it | Yaw / pitch |
|---|---|---|---|---|---|
| P0 | (2812, 930, 5168) | previous control point only | never | 0.5 | |
| P1 | same as P0 | (-5184, 930, 2783) | start | 0.5 | 118.4 / -9.0 |
| P2 | (2800, 450, 3852) | (-3868, 450, 2778) | 2.199 | 0.5 | 126.0 / -5.4 |
| P3 | (2264, 418, 3283) | (-3296, 418, 2246) | 3.866 | 0.5 | 124.6 / -6.0 |
| P4 | (1196, 176, 2448) | (-2455, 176, 1182) | 5.533 | 0.5 | 116.1 / -3.7 |
| P5 | (-52, -48, 1551) | (-1551, -48, -61) | 7.200 | 0.51 | 87.7 / 1.8 |
| P6 | (-100, -6, 12) | (-11, -6, -100) | 8.622 | 0.175 | -16.0 / 3.3 |
| P7 | (0, 14, -107) | (107, 14, 1) | 9.140 | 0.5 | -90.3 / -5.4 |
| P8 (final) | (0, 0, 0) | (0, 0, 0) | 9.890 | (not used) | -90.3 / 0.0 |
| P9 (shape only) | (0, 0, 0) | never reached | | | |

Measured on the original (camera positions from the running game, 12-tick samples): swirl start (-5179, 928, 2783)
from the eye at yaw 118.7 / pitch -8.9, equal to P1; the camera
reaches the eye 588 to 600 ticks (9.8 to 10 s) after the swirl opens.

Swirl length 9.89 s (593.4 ticks). The body (index 0 pose: animation 61 from frame 95 at speed 0.02, holding
item 5 in its right hand, hanging at its side: seen on the original from the swirl's first frame) shows from the
swirl start; at 6 s Dam's level script switches it to animation 99 up to frame 96 (dam.md); it fades out from
9.39 s to 9.89 s. Whole intro without skips: fixed shot 0 to 9 s, swirl 9 to 18.89 s, then first person.

The API points a script would write (P1 set as a cut, the rest as one smooth linear travelling; the per-leg tensions
0.51 and 0.175 have no API equivalent and are approximated by the API's curve; the world offsets above are for the
facing psi = -90.3, a script computes them from the player's facing):

```lua
local me = player(1)
local function at(x, y, z, yaw, pitch, t)
    return { pos = me, offset = { x, y, z }, yaw = yaw, pitch = pitch, time = t, ease = "linear", fov = 60 }
end
local pts = {
    at(-5184, 930, 2783, 118.4, -9.0, 0),      -- P1: the swirl opens here (P0 is not a position)
    at(-3868, 450, 2778, 126.0, -5.4, 2.199),  -- P2
    at(-3296, 418, 2246, 124.6, -6.0, 1.667),  -- P3
    at(-2455, 176, 1182, 116.1, -3.7, 1.667),  -- P4
    at(-1551, -48, -61, 87.7, 1.8, 1.667),     -- P5
    at(-11, -6, -100, -16.0, 3.3, 1.422),      -- P6
    at(107, 14, 1, -90.3, -5.4, 0.518),        -- P7
    at(0, 0, 0, -90.3, 0.0, 0.750),            -- P8: the eyes, looking ahead = first person
}
camera.move(pts, { smooth = true })
```

Then: show the body (index 0 pose) at the swirl start, fade it out over the last 0.5 s, and reset the camera to
the player's view when the travelling ends.
