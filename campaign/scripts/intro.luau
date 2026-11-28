-- intro: the solo level intro, played the way specs/intro.md says, from a level's intro data (#165).
-- Every level whose spec has an Intro section ("level_intro": false in its mission.json) calls
--
--     require("intro").play(require("<level>_intro_data"))
--
-- where its intro runs; play() returns at the hand-over (first person, control back, mission clock running).
-- A require()d module: its top level only defines functions (it never waits); play() and wait() wait.
--
-- The data (a level's <level>_intro_data.luau, in intro.md's units: world units, API degrees, seconds):
--   lines       { first line's text id, second line's id? }       (intro.md §1, the two text words)
--   cameras     { { pos = {x, y, z}, yaw, pitch } }               fixed cameras: each ONE static point (no
--               waypoints); ONE of them is shown, picked at random with equal odds
--   swirl       { { off = {x, y, z}, time, tension, flags } }     P0..Pn, then the end marker { flags = 1 }:
--               off: x left, y up, z forward (flags has 2), else world axes, from the player's eye point;
--               time / tension: the leg that STARTS at the point; flags 4: look at the eye itself there;
--               P0 only shapes the first leg (never reached, its time unused), Pn only shapes the last one
--   intro_anim  the intro-animation record (index into ANIMS below; 0 / nil: the default table pose)
--   body        { { at, anim, from?, to?, speed?, blend?, hold? } }  a level script's animation of the body,
--               "at" seconds after the swirl start; it overrides the table pose from its first step on
--   start_pad   the solo start pad (the body is placed there); weapon: the first start item (held by the body)
--
-- The timeline (intro.md "The timeline"):
--   1. the fixed shot: fade in 1 s; line 1 at 2 s; line 2 at 5 s; fade to black at 8 s (5 s without line 2),
--      black 1 s later. A skip ends the shot at once and its 1 s fade to black follows; the swirl still plays.
--   2. the swirl: opens AT P1 fading in over 1 s; the camera travels P1..P(n-1), the leg Pk -> Pk+1 lasting
--      Pk's time (the swirl lasts P1's to P(n-2)'s times; P0's leg never plays), each leg a cardinal spline over
--      the window P(k-1), Pk, Pk+1, Pk+2 with Pk's tension (P0 and Pn are only control points); it looks at the
--      eye point pushed 40 units along the player's facing (0 at a flag-4 point, blended over a leg). The frame
--      is the player's own facing (player(n):facing()), never the start pad's look direction. The body is shown
--      posed, the start weapon in its right hand, from the swirl's first frame and fades out over its last
--      0.5 s. A skip while more than 1 s of the swirl remains: fade to black 1 s, the hand-over, fade in 1 s; a
--      skip later is ignored.
--   3. the hand-over: body hidden, camera.reset(), cinema.finish() (control back; the mission clock starts).
--
-- Approximations (the API has no closer verb):
--   - the swirl's curve: camera.move draws the cardinal spline itself (smooth = true, ease "linear"): the
--     swirl's records go in as they are, P0 and Pn as control points (control = true), each with its tension.
--   - the look point: each point gets the yaw / pitch from itself toward its look point (eye + push along the
--     facing, 0 at a flag-4 point); the API blends yaw / pitch per leg (intro.md: a close approximation).
--   - the skip test polls cinema.skipped() every POLL seconds (up to POLL late); cinema.skipped() is latched from
--     cinema.begin(), so the scene is begun again at the swirl start: the press that skipped the fixed shot does
--     not also skip the swirl (intro.md: two presses skip all of it). TODO(api): a way to clear the skip latch.
--   - the body fade: Target:fade_out() (the API's fade-and-remove); its hand is emptied first so the fade drops
--     nothing on the floor. The body's hands are the player's, so the hand-over puts the start weapon back in
--     every living player's hand (force_weapon; intro.md section 4: the start weapon is in hand at the
--     hand-over; the fist, item 1, means bare-handed). TODO(api): a body fade of a given length (0.5 s) that
--     keeps what it holds.
--   - the body's weapon (intro.md section 3, checked on Dam): in the right hand from the swirl's first frame,
--     through every pose. UNCERTAIN(spec): Bunker 1 (the one intro-animation record) may play its frames 7..40
--     empty-handed and draw at 86..131; not confirmed, so it holds the weapon from the start like every level.
--   - not done (no API): the bold font of the lines, and the intro's alternative fog (intro.md: "a script that
--     cannot switch fog can ignore it").
--
-- Co-op (intro.md: multiplayer has no swirl; the cinema API shows the scene camera in every player's view): the
-- fixed shot and the swirl play once for everybody; the swirl follows the FIRST LIVING player (player 0 at a level
-- start): its eye point, its facing, its body. Every other player sees that same camera (the scene camera is every
-- player's view) and stays frozen where it starts; its own body is not posed.

local L = require("campaign_lib")

local M = {}

export type Camera = { pos: { number }, yaw: number, pitch: number }
export type SwirlPoint = { off: { number }?, time: number?, tension: number?, flags: number }
export type BodyStep = {
    at: number, anim: number, from: number?, to: number?, speed: number?, blend: number?, hold: boolean?,
}
export type Data = {
    lines: { number }?,
    cameras: { Camera }?,
    swirl: { SwirlPoint }?,
    intro_anim: number?,
    body: { BodyStep }?,
    start_pad: number?,
    weapon: number?,
}
export type Hooks = {
    swirl: (() -> ())?,       -- runs (in its own task) when the swirl starts
    handover: (() -> ())?,    -- runs (in its own task) at the hand-over
}

local POLL = 0.1          -- seconds between two looks at cinema.skipped()
local FOV = 60            -- intro.md: 60 degrees vertical throughout
local FADE = 1            -- every intro fade lasts 1 s
local LOOK_AHEAD = 40     -- the look point: the eye pushed 40 units forward
local BODY_FADE = 0.5     -- the body fades out over the swirl's last 0.5 s
local SKIP_MARGIN = 1     -- the swirl skips only while more than 1 s of it remains
local TICK = 1 / 60

-- intro.md §3: the intro-animation table (animation, from frame, to frame, speed; index 1 chains two ranges, the
-- second blended over 24 frames, read as 24 ticks). A to of nil: to its end, then held.
local ANIMS: { [number]: { BodyStep } } = {
    [0] = { { at = 0, anim = 61, from = 95, speed = 0.02, hold = true } },
    [1] = {
        { at = 0, anim = 66, from = 7, to = 40, speed = 0.5 },
        -- frames 7..40 at speed 0.5 (30 frames a second), then 86..131
        { at = (40 - 7) / (0.5 * 60), anim = 66, from = 86, to = 131, speed = 0.5, blend = 24 * TICK, hold = true },
    },
    [2] = { { at = 0, anim = 97, from = 0, speed = 0.5, hold = true } },
    [3] = { { at = 0, anim = 98, from = 0, speed = 0.5, hold = true } },
    [4] = { { at = 0, anim = 99, from = 0, speed = 0.5, hold = true } },
    [5] = { { at = 0, anim = 100, from = 0, speed = 0.5, hold = true } },
    [6] = { { at = 0, anim = 102, from = 0, speed = 0.5, hold = true } },
    [7] = { { at = 0, anim = 103, from = 0, speed = 0.5, hold = true } },
    [8] = { { at = 0, anim = 176, from = 0, speed = 0.5, hold = true } },
}

local finished = false     -- the hand-over happened
local handover_at: number? = nil

local function has_flag(flags: number, bit: number): boolean
    return math.floor(flags / bit) % 2 == 1
end

-- Sleeps `seconds` in POLL steps; true at once when a player pressed a skip button (skippable only).
local function hold(seconds: number, skippable: boolean): boolean
    local left = seconds
    while left > 0 do
        local d = math.min(POLL, left)
        wait(d)
        left -= d
        if skippable and cinema.skipped() then
            return true
        end
    end
    return false
end

-- The player the swirl follows: the first living one.
local function followed(): number
    local alive = L.living()
    return alive[1] or 0
end

-- ------------------------------------------------------------------ 1. the fixed intro camera

-- The fixed shot; false when the level has no fixed camera (nothing shown).
local function fixed_shot(d: Data): boolean
    local cams = d.cameras
    if cams == nil or #cams == 0 then
        return false
    end
    local c = cams[math.random(#cams)]          -- one camera, equal odds
    camera.set({ pos = c.pos, yaw = c.yaw, pitch = c.pitch, fov = FOV, time = 0 })
    screen.fade_out(0)
    screen.fade_in(FADE)                        -- 0 to 1 s
    local lines = d.lines or {}
    local first, second = lines[1], lines[2]
    local skipped = hold(2, true)
    if not skipped then
        if first ~= nil then
            L.bottom(first)                     -- 2 s: the first line, for 2 s
        end
        skipped = hold(3, true)
    end
    if not skipped and second ~= nil then
        L.bottom(second)                        -- 5 s: the second line, for 2 s
        hold(3, true)                           -- the shot ends at 8 s (or at a skip)
    end
    screen.fade_out(FADE)                       -- the shot ends: 1 s to black
    hold(FADE, false)
    return true
end

-- ------------------------------------------------------------------ 2. the swirl

-- The points P0..Pn before the end marker (the first record with flag 1).
local function swirl_points(d: Data): { SwirlPoint }
    local out: { SwirlPoint } = {}
    for _, s in d.swirl or {} do
        if has_flag(s.flags, 1) then
            break
        end
        table.insert(out, s)
    end
    return out
end

-- The camera points of the swirl around player p (intro.md section 2), and the swirl's length. The points are
-- the swirl's own records, handed to camera.move as the cardinal spline it draws itself: P0 first as a control
-- point (it only shapes the first leg), P1 the start (time 0: a cut), P2..P(n-1) each with the time of the leg
-- that reaches it (Pk's duration on Pk+1), every point with the tension of the leg that starts at it, and Pn last
-- as a control point (it only shapes the last leg).
local function waypoints(pts: { SwirlPoint }, p: number): ({ CameraPoint }, number)
    local me = player(p)
    local ex, ey, ez = me:eye()
    if ex == nil or ey == nil or ez == nil then
        ex, ey, ez = me:pos()
    end
    local eye = { ex or 0, ey or 0, ez or 0 }
    local face = me:facing()                    -- the player's own facing (not the start pad's look vector)
    local psi = math.rad(face)
    local cs, sn = math.cos(psi), math.sin(psi)
    local prev_yaw: number? = nil
    -- one record as a camera point: its world position (eye + offset), its yaw / pitch toward its look point
    local function point(s: SwirlPoint): CameraPoint
        local o = s.off or { 0, 0, 0 }
        local wx, wy, wz = o[1], o[2], o[3]
        if has_flag(s.flags, 2) then            -- the player's frame: x left, y up, z forward
            wx, wz = o[1] * cs + o[3] * sn, -o[1] * sn + o[3] * cs
        end
        local c = { eye[1] + wx, eye[2] + wy, eye[3] + wz }
        local pu = if has_flag(s.flags, 4) then 0 else LOOK_AHEAD   -- flag 4: look at the eye itself
        local dx, dy, dz = eye[1] + pu * sn - c[1], eye[2] - c[2], eye[3] + pu * cs - c[3]
        local flat = math.sqrt(dx * dx + dz * dz)
        local yaw, pitch = face, 0
        if flat > 0.001 or math.abs(dy) > 0.001 then
            yaw = if flat > 0.001 then math.deg(math.atan2(dx, dz)) else face
            pitch = math.deg(math.atan2(dy, flat))
        end
        local py = prev_yaw
        if py ~= nil then                       -- unwrapped: the blend takes the short way round
            while yaw - py > 180 do
                yaw -= 360
            end
            while yaw - py < -180 do
                yaw += 360
            end
        end
        prev_yaw = yaw
        return { pos = c, yaw = yaw, pitch = pitch, fov = FOV, ease = "linear", tension = s.tension or 0.5 }
    end
    local n = #pts                              -- pts[1] is P0, pts[n] is Pn
    if n < 4 then                               -- no leg (P1 is already P(n-1)): a still camera
        local cp = point(pts[math.min(2, n)])
        cp.time = 0
        return { cp }, 0
    end
    local out: { CameraPoint } = {}
    local c0 = point(pts[1])                    -- P0: shapes the first leg only, never travelled
    c0.control = true
    table.insert(out, c0)
    local c1 = point(pts[2])                    -- P1: the swirl opens here (a cut)
    c1.time = 0
    table.insert(out, c1)
    local total = 0
    for i = 3, n - 1 do                         -- P2 .. P(n-1): reached after the previous point's duration
        local dur = pts[i - 1].time or 0
        local ci = point(pts[i])
        ci.time = dur
        table.insert(out, ci)
        total += dur
    end
    local cn = point(pts[n])                    -- Pn: shapes the last leg only, never reached
    cn.control = true
    table.insert(out, cn)
    return out, total
end

-- The body's animation steps: the table pose, overridden from the level's first step on.
local function body_steps(d: Data): { BodyStep }
    local base = ANIMS[d.intro_anim or 0] or ANIMS[0]
    local level = d.body
    if level == nil or #level == 0 then
        return base
    end
    local out: { BodyStep } = {}
    for _, s in base do
        if s.at < level[1].at then
            table.insert(out, s)
        end
    end
    for _, s in level do
        table.insert(out, s)
    end
    return out
end

local function play_body(b: Target, steps: { BodyStep })
    local t = 0
    for _, s in steps do
        if s.at > t then
            wait(s.at - t)
            t = s.at
        end
        b:anim(s.anim, { from = s.from, to = s.to, speed = s.speed, blend = s.blend, hold = s.hold })
    end
end

-- The swirl; returns the body (nil: no swirl) and whether it was skipped (the screen is then black).
local function swirl(d: Data, hooks: Hooks): (Target?, boolean)
    local pts = swirl_points(d)
    if #pts == 0 then
        return nil, false
    end
    -- a new scene: the skip latch of the fixed shot is cleared and the bottom line goes (intro.md §1)
    cinema.finish()
    cinema.begin()
    local p = followed()
    local points, total = waypoints(pts, p)
    -- the body, shown and posed from the swirl's first frame (intro.md §3)
    local b = bond_body(p)
    b:show(true)   -- first: the third-person body only exists once shown (teleport / hold_item act on it)
    if p == 0 and d.start_pad ~= nil then
        b:teleport(d.start_pad)
    end
    local w = d.weapon or 0
    b:hold_item(if w <= 1 then 0 else w, "right")   -- the fist (1) has no model: an empty hand
    local anim = spawn(function()
        play_body(b, body_steps(d))
    end)
    local fade = spawn(function()
        wait(math.max(total - BODY_FADE, 0))
        anim:cancel()
        b:hold_item(0, "right")
        b:fade_out()
    end)
    camera.move(points, { smooth = true })
    screen.fade_in(FADE)
    if hooks.swirl then
        spawn(hooks.swirl)
    end
    local t = 0
    while t < total do
        local step = math.min(POLL, total - t)
        wait(step)
        t += step
        if total - t > SKIP_MARGIN and cinema.skipped() then
            screen.fade_out(FADE)               -- the skip: 1 s to black, then the hand-over
            wait(FADE)
            camera.stop()
            anim:cancel()
            fade:cancel()
            return b, true
        end
    end
    camera.wait()
    anim:cancel()
    fade:cancel()
    return b, false
end

-- ------------------------------------------------------------------ the whole intro

-- Plays the level's intro (a blocking call: it returns at the hand-over).
function M.play(d: Data, hooks: Hooks?)
    local h: Hooks = hooks or {}
    cinema.begin({ keep_bottom_text = true })   -- frozen, invulnerable, HUD hidden, mission clock stopped
    local shot = fixed_shot(d)
    local b, skipped = swirl(d, h)
    -- 3. the hand-over: first person at once (the last point is the eye looking ahead: an invisible cut)
    if b ~= nil then
        b:stop_anim()
        b:show(false)
    end
    camera.reset()
    -- intro.md sections 3 / 4: the start weapon in hand at the hand-over (the body's emptied hand is the player's)
    local w = d.weapon
    if w ~= nil and w > 0 then
        for _, i in L.living() do
            player(i):force_weapon(w)
        end
    end
    if skipped or (b == nil and shot) then
        screen.fade_in(FADE)                    -- from black (a skipped swirl, or no swirl): 1 s back in
    end
    cinema.finish()                             -- control back; the mission clock runs from here
    finished = true
    handover_at = clock()
    if h.handover then
        spawn(h.handover)
    end
end

-- The hand-over happened.
function M.done(): boolean
    return finished
end

-- Returns once the hand-over happened (a script, not a module top level).
function M.wait()
    while not finished do
        wait(POLL)
    end
end

-- Seconds of play since the hand-over (0 before it): the clock of the rules timed "from the level start".
function M.since(): number
    local t0 = handover_at
    if t0 == nil then
        return 0
    end
    return clock() - t0
end

return M
