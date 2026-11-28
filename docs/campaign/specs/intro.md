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
view-distance setting; the swirl and the rest of the level use the normal one. UNCERTAIN how much it differs per
level; a script that cannot switch fog can ignore it.

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
- **When the body disappears.** When 0.5 s of the swirl remain, the body fades out over 0.5 s (opaque to
  invisible), so it is gone exactly at the hand-over, where it is removed.

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
