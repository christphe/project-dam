-- guard_ai: the generic guard shared by every level (specs/global.md): one Luau mode per generic AI list
-- 0x0000..0x0011, with the interface of CONTRACT.md ("The generic guard module"). Rule and definition numbers in
-- the comments are global.md's.
--
-- Cadence: every mode decides once per STEP = 0.1 s. global.md's guard decides once per game frame (60 per s on
-- the port); a 0.1 s step is six frames, so the per-update fidget chance of rule 10.7 is scaled to the step below.
-- Every perception check is the engine's own (mission.d.luau), never rebuilt here; the module only decides
-- (rule 34).
--
-- Perception from EVENTS. The engine's one-update signals are queued events, all delivered. The module subscribes
-- once for every guard (one on() per event name, installed by the first start() / boot(), never at the top
-- level) and routes each event by ev.chr into that guard's Memo, so no signal falls between two decisions:
--   "guard_spotted"   {chr, player}       -> Memo.spot (definition 2, the edge into seeing); a guard that keeps
--                                            seeing is still the spotted_player() query of this tick (no new roll)
--   "guard_heard"     {chr, player}       -> the guard's target player (Co-op notes: hearing is per shooter); the
--                                            heard window itself is a state: heard_shot_within() (definition 5)
--   "guard_near_miss" {chr, player}       -> Memo.near_miss (definition 6) and the target player
--   "guard_witnessed" {chr, what, other}  -> Memo.witnessed (definition 7)
--   "anim_end"        {chr}               -> Memo.anim_end: the fidget, typing and draw animations (rules 13, 14, 28)
--   "guard_died"      {chr}               -> the guard's task ends, mode "inert" (rule 30)
-- "guard_damaged" is not subscribed: no rule reacts to damage itself (the alert window after a hit reaction is
-- native, definition 4, and rule 26 counts hits with hits(), which also counts hits on an invincible guard).
-- A latched one-update signal counts at the guard's next decision only while it is at most FRESH old: global.md
-- says it counts "for the update it happens in only", so a busy guard does not act on it seconds later. The
-- alert and heard windows are states and stay queries (alerted_within, heard_shot_within).
--
-- Hand-back. When a mode of a guard started with opts.fallback (any fallback, "inert" included) ends and hands
-- over, the module raises
--     emit("guard_mode_end", { chr = g.chr, mode = <the mode that ended>, run = <that mode's run number> })
-- (delivered next tick). It is not raised when the guard dies (the "guard_died" event says that), nor when
-- start() / stop() replaces the mode, nor by the modes that never end (standard, hunter, hold_aim, inert).
-- guard_ai.on_end(g, fn) is built on it; level scripts use it rather than polling guard_ai.mode().
--
-- One task per guard (spawn()); start() cancels the guard's previous task and replaces it at any time, even
-- mid-action. A guard that dies or is removed ends in "inert" and its task ends (rule 30).

local guard_ai = {}

local STEP = 0.1                  -- seconds between two decisions (see the top)
local FRESH = 2 * STEP            -- how long a latched one-update signal still counts (see the top)
local FRAMES_PER_STEP = STEP * 60 -- game updates a step stands for
local WINDOW = 10                 -- the alert and heard windows, seconds (definitions 4, 5)
local HUNT_RESTART = 2000         -- rule 25: a busy hunter restarts its hunt beyond 20 m
local PRESS_RANGE = 100           -- rule 23: the alarm raiser presses only this close to its pad
local CLONE_OFFSET = 0x2710       -- rule 18: a clone's chr is the sender's + 10000

-- rule 10.7: 2 in 256 per idle update, as a chance per step of FRAMES_PER_STEP updates.
local FIDGET_PER_STEP = 1 - (1 - 2 / 256) ^ FRAMES_PER_STEP

export type Opts = { fallback: string?, alarm_pad: Pad?, patrol: number? }

type State = {
    mode: string?,       -- the mode running now (guard_ai.mode)
    task: Task?,         -- the guard's one task
    fallback: string,    -- where a mode that "ends" goes (default "standard")
    alarm_pad: Pad?,     -- rule 23's pad (default the preset pad)
    handback: boolean,   -- started with opts.fallback: a mode that ends raises "guard_mode_end" (see the top)
    run: number,         -- the run number of the mode running now (on_end matches it)
    waiters: { Connection },   -- on_end() subscriptions, dropped when start() / stop() / death replaces the mode
}

-- Per-guard facts that outlive a mode change, and the latched event signals (see the top).
type Memo = {
    aware: boolean,      -- definition 8
    target: number?,     -- the guard's current target player, 0-based (Co-op notes)
    fidget: boolean,     -- an idle fidget of ours is playing (rule 10.6); cleared by "anim_end"
    anim_end: boolean,   -- "anim_end" arrived since the guard's last anim()
    spot: number?,       -- "guard_spotted": the player, at spot_at
    spot_at: number?,
    near_miss_at: number?,   -- "guard_near_miss"
    witnessed_at: number?,   -- "guard_witnessed"
}

local states: { [number]: State } = {}
local memos: { [number]: Memo } = {}
local booted = false
local subscribed = false
local runs = 0           -- the last run number given to a mode

-- ------------------------------------------------------------------ helpers

-- A guard's key: its chr (setup and spawned guards carry one); a prop-only target falls back to its prop.
local function key(g: Target): number?
    if g.chr then
        return g.chr
    end
    if g.prop then
        return -1 - g.prop
    end
    return nil
end

local function memo_at(k: number): Memo
    local m = memos[k]
    if not m then
        m = { aware = false, target = nil, fidget = false, anim_end = false }
        memos[k] = m
    end
    return m
end

local function memo_of(g: Target): Memo
    return memo_at(key(g) or 0)
end

-- A latched signal still counts (see the top).
local function fresh(t: number?): boolean
    return t ~= nil and clock() - (t :: number) <= FRESH
end

-- Dying, dead or gone (rules 10, 30).
local function gone(g: Target): boolean
    return not g:exists() or g:is_dead() or g:is_dying()
end

-- A roll of n in 256, as global.md states its chances.
local function roll(n: number): boolean
    return math.random(0, 255) < n
end

local function coin(): boolean
    return math.random(0, 1) == 0
end

-- The target player as a Target for the verbs (nil: the engine's "the player").
local function tgt(m: Memo): Target?
    if m.target then
        return player(m.target)
    end
    return nil
end

local function target_distance(g: Target, m: Memo): number
    if m.target then
        return g:distance_to(player(m.target))
    end
    return g:distance()
end

-- Co-op notes: a check that names a player makes it the guard's target.
-- Definition 2: the "guard_spotted" edge since the last decision, else the engine's sight check of this tick (a
-- guard that keeps seeing the player gets no new event; the query does not roll the check again).
local function spotted(g: Target, m: Memo): number?
    local p: number? = if fresh(m.spot_at) then m.spot else nil
    m.spot, m.spot_at = nil, nil
    if p == nil then
        p = g:spotted_player()
    end
    if p then
        m.target = p
    end
    return p
end

local function line(g: Target, m: Memo): number?
    local p = g:line_to_player()   -- definition 3
    if p then
        m.target = p
    end
    return p
end

-- Definition 6: a "guard_near_miss" since the last decision (consumed).
local function near_miss(m: Memo): boolean
    local hit = fresh(m.near_miss_at)
    m.near_miss_at = nil
    return hit
end

-- Definition 7: a "guard_witnessed" hit or death since the last decision (consumed).
local function witnessed(m: Memo): boolean
    local saw = fresh(m.witnessed_at)
    m.witnessed_at = nil
    return saw
end

-- Plays an animation of ours; Memo.anim_end turns true on its "anim_end".
local function play(g: Target, m: Memo, anim: number, opts: AnimOpts): boolean
    m.anim_end = false
    return g:anim(anim, opts)
end

-- Waits for the animation of play() to end; false when the guard died meanwhile. anim_done() is kept beside the
-- event for an animation cut short (mission.d.luau raises "anim_end" when one "finished").
local function wait_anim(g: Target, m: Memo): boolean
    repeat
        wait(STEP)
        if gone(g) then
            return false
        end
    until m.anim_end or g:anim_done()
    return true
end

-- Waits until the guard is no longer busy (an action, an animation of its own); false when it died meanwhile.
local function wait_free(g: Target): boolean
    wait(STEP)
    while not gone(g) and g:is_busy() do
        wait(STEP)
    end
    return not gone(g)
end

-- An idle fidget of ours is dropped before the guard acts (rule 13: the fidget is interruptible); stop_anim()
-- does nothing once it has ended.
local function settle(g: Target, m: Memo)
    if m.fidget then
        m.fidget = false
        g:stop_anim()
    end
end

local function fire(g: Target, m: Memo, stance: string): boolean
    return g:fire_at(tgt(m), { stance = stance })
end

-- Rule 15 option 1 (also rule 26's first option): chance = the guard's grenade probability. global.md counts it
-- 0..255; grenade_chance() gives it 0..100, so the roll is out of 100. The 10 units minimum is checked here; a
-- grenade in hand or a free hand is throw_grenade()'s own condition.
local function try_grenade(g: Target, m: Memo): boolean
    if math.random() * 100 >= g:grenade_chance() then
        return false
    end
    if target_distance(g, m) < 10 then
        return false
    end
    return g:throw_grenade()
end

-- ------------------------------------------------------------------ actions shared by the modes

-- Rule 15: one combat action, started; returns at once (the engine plays it to its end).
-- Each move's own conditions (armed, range, angle, room, 3 s since the last walk/run attack) are combat_move()'s.
local function engage(g: Target, m: Memo)
    settle(g, m)
    if not m.aware and roll(33) then
        -- rule 15 first sighting gesture: only the pointing, then back.
        -- TODO(api): point_at_player() does not promise "only when the player is roughly in front"; assumed native.
        m.aware = true   -- definition 8
        g:point_at_player()
        return
    end
    m.aware = true       -- definition 8
    if try_grenade(g, m) then return end                              -- option 1
    if roll(11) and g:combat_move("roll_fire") then return end        -- option 2, 4.3%
    if roll(11) and g:combat_move("side_hop") then return end         -- option 3, 4.3%
    if roll(26) and g:combat_move("sidestep") then return end         -- option 4, 10.2%
    if roll(51) and g:combat_move("side_run") then return end         -- option 5, 19.9%
    local r = math.random(0, 255)                                     -- one roll for options 6 and 7
    if r < 65 and g:combat_move("walk_fire") then return end          -- option 6, 25.4%
    if r <= 127 and g:combat_move("run_fire") then return end         -- option 7, the same roll at most 127
    fire(g, m, if coin() then "kneel" else "stand")                   -- option 8 (unarmed: nothing)
end

-- Rule 16: run to where the target player is now; ends on arrival or stop, or engages on a sighting.
local function chase(g: Target, m: Memo)
    settle(g, m)
    m.aware = true       -- definition 8
    -- run_to_player fails when dying, in a hit reaction, without a route, or at the engine's chaser cap
    if not g:run_to_player("run", m.target) then
        return
    end
    while true do
        wait(STEP)
        if gone(g) then
            return
        end
        if not g:is_moving() or g:is_idle() then   -- the arrival check comes first
            return
        end
        if spotted(g, m) then
            engage(g, m)                            -- the run is replaced by the combat action
            return
        end
    end
end

-- Rule 17: the startled look-around (unless in a hit reaction or dying), then a chase.
local function startle_chase(g: Target, m: Memo)
    settle(g, m)
    if not g:is_busy() then   -- a hit reaction is a busy state (definition 1); dying is checked by gone()
        g:look_around()
        if not wait_free(g) then
            return
        end
    end
    chase(g, m)
end

-- Rule 20: the sender's clone (chr + 10000) still counts while it exists, dying or lying there.
local function clone_exists(g: Target): boolean
    if not g.chr then
        return false
    end
    local c = chr(g.chr + CLONE_OFFSET)
    -- TODO(api): no query says "a clone body is still lying there"; exists() is "alive", so a dead clone not yet
    -- faded out is detected through is_dying() / pos() only, which mission.d.luau does not promise for a body.
    return c:exists() or c:is_dying() or c:pos() ~= nil
end

-- Rule 21: the reinforcement decision.
local function reinforce(g: Target, m: Memo)
    m.aware = true       -- definition 8
    if g:was_seen() then -- definition 9, any player's screen
        chase(g, m)
        return
    end
    if clone_exists(g) then
        return           -- rule 21: stays where it is, as a standard guard
    end
    -- rule 18: only a guard with the clone flag sends one; clone() is nil when no slot is free
    local c = if g:get_flag("clone_sender") then g:clone() else nil
    if c then
        -- rule 19: the clone runs the clone mode. TODO(api): mission.d.luau does not say whether clone() gives the
        -- clone a native AI list; start() makes it natively inert before the Luau mode takes it.
        local ck = key(c)
        if ck then
            memos[ck] = nil   -- a fresh guard: not aware (definition 8), even if an earlier clone had its chr
        end
        guard_ai.start(c, "clone")
        return
    end
    chase(g, m)
end

-- Rule 13: one idle fidget, chosen by the table's roll; returns at once (the fidget is interruptible).
local FIDGETS = {   -- { animation, end frame, chance in 256 }
    { 0x9A, 193, 51 },
    { 0x9B, 294, 50 },
    { 0x9C, 183, 50 },
    { 0x9D, 123, 50 },
    { 0x9E, 56, 50 },
    { 0x9F, 137, 5 },
}

-- Rule 14: one typing animation.
local TYPING = {
    { 0xAC, 69, 61 },
    { 0xAD, 74, 60 },
    { 0xAE, 79, 60 },
    { 0xAF, 89, 75 },
}

local function pick(list: { { number } }): { number }
    local r = math.random(0, 255)
    for _, e in list do
        if r < e[3] then
            return e
        end
        r -= e[3]
    end
    return list[#list]
end

local function fidget(g: Target, m: Memo)
    local e = pick(FIDGETS)
    -- back to the idle pose at the end; interruptible: the guard counts as idle while it plays (definition 1)
    -- TODO(spec): "the animation's sound may play (the sneeze sound about half the time)": global.md gives no
    -- sound id for AnimOpts.sound, so no sound is asked for.
    if play(g, m, e[1], { from = 0, to = e[2], idle_after = true, interruptible = true }) then
        m.fidget = true
    end
end

-- No idle pose at the end, no sound (AnimOpts.sound left out).
local function keyboard(g: Target, m: Memo)
    local e = pick(TYPING)
    if play(g, m, e[1], { from = 0, to = e[2], idle_after = false }) then
        wait_anim(g, m)
    else
        wait(STEP)   -- refused: a fallback "keyboard" loop does not spin within one tick
    end
end

-- Rule 23: the alarm run.
local function alarm_run(g: Target, st: State)
    local where = st.alarm_pad or g:preset_pad()
    if where == nil then
        -- rule 23: no preset pad (0xFFFF, or a spawned guard): no run, no alarm; straight on to the chase
        return
    end
    if g:run_to(where, "run") then
        repeat
            wait(STEP)
            if gone(g) then
                return
            end
        until not g:is_moving()   -- arrived, or interrupted (a hit reaction)
    end
    if g:distance_to(pad(where)) > PRESS_RANGE then
        return                    -- too far: no alarm
    end
    -- press_alarm is false when the object on the pad is destroyed or missing, or the guard was stopped; the
    -- alarm itself sounds natively at frame 60 of the pressing animation (0x3D, frames 40..82, by free hand).
    if g:press_alarm(where) then
        wait_free(g)              -- waits for the animation to end
    end
end

-- Rule 26's starting options, in the table's order; an impossible one falls through to the next.
local HUNTER_OPTIONS: { (Target, Memo) -> boolean } = {
    function(g, m) return try_grenade(g, m) end,              -- 0..10
    function(g, m) return g:combat_move("sidestep") end,      -- 11..50
    function(g, m) return g:combat_move("roll_fire") end,     -- 51..80
    function(g, m) return g:combat_move("walk_fire") end,     -- 81..100
    function(g, m) return g:combat_move("run_fire") end,      -- 101..150
    function(g, m) return fire(g, m, "stand") end,            -- 151..200
    function(g, m) return fire(g, m, "kneel") end,            -- 201..255
}
local HUNTER_ROLL = { 10, 50, 80, 100, 150, 200, 255 }

local function lose_invincibility(g: Target): boolean
    if g:get_flag("invincible") and g:hits() >= 6 then
        g:set_invincible(false)
        return true
    end
    return false
end

-- Rule 26: one attack, started; the hunter does not wait for it.
local function hunter_attack(g: Target, m: Memo)
    lose_invincibility(g)
    local r = math.random(0, 255)
    local first = #HUNTER_ROLL
    for i, top in HUNTER_ROLL do
        if r <= top then
            first = i
            break
        end
    end
    for i = first, #HUNTER_OPTIONS do
        if HUNTER_OPTIONS[i](g, m) then
            return
        end
    end
end

-- Rule 24: the hunt run. Returns the hunter's next step.
local function hunt(g: Target, m: Memo): string
    if g:run_to_player("run", m.target) then
        while true do
            wait(STEP)
            if gone(g) then
                return "watch"
            end
            if line(g, m) then
                return "attack"
            end
            if not g:is_moving() then
                return "after"
            end
        end
    end
    return "after"   -- a failed run goes on with the next steps
end

-- Rule 25: watching, one step.
local function watch(g: Target, m: Memo): string
    wait(STEP)
    if gone(g) then
        return "watch"
    end
    if lose_invincibility(g) then
        return "attack"
    end
    if g:is_idle() then
        return if line(g, m) then "attack" else "after"
    end
    if g:is_busy() and target_distance(g, m) > HUNT_RESTART then
        return "hunt"   -- faithful quirk: a busy guard far away restarts the run every step
    end
    return "watch"
end

-- Rule 27: the after-attack check.
local function after_attack(g: Target, m: Memo): string
    if line(g, m) then
        return "watch"
    end
    return if roll(95) then "watch" else "probe"   -- 37.1% / 62.9%
end

-- Rule 27: a probe (1 s run at the player, then a hold of up to 10 s).
local function probe(g: Target, m: Memo): string
    g:halt()   -- cancels any action in progress
    g:run_to_player("run", m.target)
    local t0 = clock()
    while clock() - t0 < 1 do
        wait(STEP)
        if gone(g) then
            return "watch"
        end
    end
    g:halt()
    t0 = clock()
    while clock() - t0 < WINDOW do
        wait(STEP)
        if gone(g) then
            return "watch"
        end
        if line(g, m) or near_miss(m) then
            return "watch"
        end
    end
    return if roll(161) then "probe" else "hunt"   -- 62.9% another probe, 37.1% a full hunt
end

-- ------------------------------------------------------------------ the modes
-- Each mode runs until it ends and returns the next mode (nil: nothing more).

local MODES: { [string]: (Target, State) -> string? } = {}

-- Rule 10: the standard guard. Never ends.
MODES.standard = function(g, st)
    local m = memo_of(g)
    while true do
        if gone(g) then
            return "inert"           -- rules 10, 30
        end
        local fidgeting = m.fidget and not m.anim_end   -- "anim_end" clears it (rule 10.6)
        m.fidget = fidgeting
        if g:is_idle() then   -- busy: nothing (definition 1); an interruptible fidget counts as idle (rule 13)
            if spotted(g, m) then
                engage(g, m)                                   -- 10.1
            elseif g:alerted_within(WINDOW) then
                chase(g, m)                                    -- 10.2
            elseif g:heard_shot_within(WINDOW) then
                settle(g, m)
                reinforce(g, m)                                -- 10.3
            elseif near_miss(m) then
                if coin() then                                 -- 10.4
                    engage(g, m)
                else
                    startle_chase(g, m)
                end
            elseif witnessed(m) then
                chase(g, m)                                    -- 10.5
            elseif fidgeting then
                -- 10.6: a fidget is playing already: nothing
            elseif math.random() < FIDGET_PER_STEP then
                fidget(g, m)                                   -- 10.7
            end
        end
        wait(STEP)
    end
end

-- Rule 11: the simple guard: decides every step, idle or not (even mid-patrol); never fidgets nor reinforces.
MODES.simple = function(g, st)
    local m = memo_of(g)
    while true do
        if gone(g) then
            return "inert"
        end
        if spotted(g, m) then
            engage(g, m)
            return st.fallback
        end
        if g:alerted_within(WINDOW) or g:heard_shot_within(WINDOW) or near_miss(m) or witnessed(m) then
            chase(g, m)
            return st.fallback
        end
        wait(STEP)
    end
end

-- Rule 12: the deaf simple guard: decides only while idle (a patrol walk is idle), ignores shots it only hears.
MODES.simple_deaf = function(g, st)
    local m = memo_of(g)
    while true do
        if gone(g) then
            return "inert"
        end
        if g:is_idle() then
            if spotted(g, m) then
                engage(g, m)
                return st.fallback
            elseif g:alerted_within(WINDOW) then
                chase(g, m)
                return st.fallback
            elseif near_miss(m) then
                if coin() then
                    engage(g, m)
                else
                    startle_chase(g, m)
                end
                return st.fallback
            elseif witnessed(m) then
                chase(g, m)
                return st.fallback
            end
        end
        wait(STEP)
    end
end

-- Rules 22, 23: the alarm raiser.
MODES.alarm_raiser = function(g, st)
    local m = memo_of(g)
    while true do                    -- rule 22: waiting
        if gone(g) then
            return "inert"
        end
        if g:is_idle() then          -- patrol walking counts as idle
            if spotted(g, m) then
                break
            end
            -- the running near-miss total, the heard window, a witnessed hit or death (not the alert window,
            -- not being hit on its own: kept faithful)
            if near_miss(m) or g:near_misses() > 0 or g:heard_shot_within(WINDOW)
                or witnessed(m) then
                if g:hits() < 1 then
                    g:look_around()
                    if not wait_free(g) then
                        return "inert"
                    end
                end
                break
            end
        end
        wait(STEP)
    end
    alarm_run(g, st)                 -- rule 23
    if gone(g) then
        return "inert"
    end
    chase(g, m)
    return st.fallback
end

-- Rule 15 as a mode: one combat action, then the fallback.
MODES.engage = function(g, st)
    engage(g, memo_of(g))
    return st.fallback
end

-- Rule 16 as a mode.
MODES.chase = function(g, st)
    chase(g, memo_of(g))
    return st.fallback
end

-- Rule 17 as a mode.
MODES.startle_chase = function(g, st)
    startle_chase(g, memo_of(g))
    return st.fallback
end

-- Rule 21 as a mode.
MODES.reinforce = function(g, st)
    reinforce(g, memo_of(g))
    return st.fallback
end

-- Rule 19: a fresh clone chases at once, then the fallback.
MODES.clone = function(g, st)
    chase(g, memo_of(g))
    return st.fallback
end

-- Rules 24..27: the relentless hunter. Never ends; only death stops it.
MODES.hunter = function(g, st)
    local m = memo_of(g)
    local step = "hunt"
    while true do
        if gone(g) then
            return "inert"
        end
        if step == "hunt" then
            step = hunt(g, m)
        elseif step == "watch" then
            step = watch(g, m)
        elseif step == "attack" then
            hunter_attack(g, m)
            step = after_attack(g, m)
        elseif step == "probe" then
            step = probe(g, m)
        else
            step = after_attack(g, m)
        end
    end
end

-- Rule 28: draw a pistol and fire.
MODES.draw_and_fire = function(g, st)
    local m = memo_of(g)
    g:set_stats({ speed = 40, accuracy = 50, raw = true })   -- as given, no difficulty modifier
    -- turns to keep facing the player: face_player() turns once, so it is repeated every step of the 1/3 s wait
    local t0 = clock()
    repeat
        g:face_player()
        wait(STEP)
        if gone(g) then
            return "inert"
        end
    until clock() - t0 >= 1 / 3
    -- the fast one-handed draw, holding its last frame; waits for its end.
    -- TODO(api): facing is not re-issued during the draw: mission.d.luau does not say whether face_player() cuts
    -- a playing anim().
    if play(g, m, 0x42, { from = 0, to = 20, hold = true }) then
        if not wait_anim(g, m) then
            return "inert"
        end
    end
    g:give_weapon(0x06, { model = 0xCD })   -- the DD44; fails silently with full hands
    g:fire_at(tgt(m))                       -- aims, then opens fire
    if not wait_free(g) then                -- until it has stopped firing
        return "inert"
    end
    chase(g, m)
    return st.fallback
end

-- Rule 29: hold aim on the player, never ends (a level script changes it).
MODES.hold_aim = function(g, st)
    local m = memo_of(g)
    g:fire_at(tgt(m), { aim_only = true })  -- unarmed: false, nothing happens
    while not gone(g) do
        wait(0.5)
    end
    return "inert"
end

-- Rule 30: inert. Nothing, forever: the task ends (the engine still runs the body).
MODES.inert = function(g, st)
    return nil
end

-- Rule 13 as a mode: one fidget, then the fallback (the fallback keeps checking while it plays).
-- A guard started with opts.fallback (a level script's caller) waits for the fidget's end first, so its
-- "guard_mode_end" means the fidget is over and a fallback "fidget" loop plays one after the other.
MODES.fidget = function(g, st)
    local m = memo_of(g)
    fidget(g, m)
    if st.handback then
        if not m.fidget then
            wait(STEP)                   -- refused: no spin within one tick
        elseif not wait_anim(g, m) then
            return "inert"
        end
    end
    return st.fallback
end

-- Rule 14 as a mode: one typing animation, then the fallback (fallback "keyboard" loops it).
MODES.keyboard = function(g, st)
    keyboard(g, memo_of(g))
    return st.fallback
end

-- Rule 31: a one-second pause, then the fallback.
MODES.wait = function(g, st)
    wait(1)
    return st.fallback
end

-- Rule 32: end the level for everyone, then inert.
MODES.end_level = function(g, st)
    mission.finish()
    return "inert"
end

-- Rule 33: vanish at once, no drop, then inert.
MODES.remove = function(g, st)
    g:remove()
    return "inert"
end

-- Generic list id -> mode (CONTRACT.md).
local BY_ID: { [number]: string } = {
    [0x00] = "hold_aim", [0x01] = "inert", [0x02] = "standard", [0x03] = "fidget", [0x04] = "keyboard",
    [0x05] = "simple_deaf", [0x06] = "engage", [0x07] = "simple", [0x08] = "chase", [0x09] = "alarm_raiser",
    [0x0A] = "startle_chase", [0x0B] = "reinforce", [0x0C] = "clone", [0x0D] = "hunter", [0x0E] = "wait",
    [0x0F] = "end_level", [0x10] = "draw_and_fire", [0x11] = "remove",
}

-- Modes that react while their guard walks a patrol (rules 11, 12, 22): they take over the patrol at once.
local REACTS_ON_PATROL: { [string]: boolean } = { simple = true, simple_deaf = true, alarm_raiser = true }

-- opts.patrol for every other mode: walk the path until disturbed (a sighting, the alert or heard window, a
-- near miss, a witnessed hit or death), or until the patrol stops.
local function patrol_until_disturbed(g: Target)
    local m = memo_of(g)
    while true do
        wait(STEP)
        if gone(g) or not g:is_patrolling() then
            return
        end
        if spotted(g, m) or g:alerted_within(WINDOW) or g:heard_shot_within(WINDOW) or near_miss(m)
            or witnessed(m) then
            return
        end
    end
end

-- The guard's task: the patrol first when asked, then mode after mode until one returns nil.
local function drive(g: Target, st: State, first: string, path: number?)
    if path then
        g:patrol(path)
        if not REACTS_ON_PATROL[first] then
            patrol_until_disturbed(g)
        end
    end
    local name: string? = first
    local first_run = true
    while name do
        if gone(g) then
            name = "inert"   -- rule 30: every mode ends here once the guard dies
        end
        st.mode = name
        if not first_run then
            runs += 1
            st.run = runs    -- a new mode instance (start() numbered the first)
        end
        first_run = false
        local ended = name :: string
        local f = MODES[ended] or MODES.standard
        name = f(g, st)
        -- the hand-back (see the top): a mode that ended by itself, the guard still there
        if name ~= nil and st.handback and g.chr and not gone(g) then
            emit("guard_mode_end", { chr = g.chr :: number, mode = ended, run = st.run })
        end
    end
    st.task = nil
end

-- ------------------------------------------------------------------ the event routing (see the top)

-- The Memo of a guard the module runs, by the event's chr; nil for any other guard.
local function memo_for(ev: MissionEvent): Memo?
    local k = ev.chr
    if k == nil or states[k] == nil then
        return nil
    end
    return memo_at(k)
end

local function drop_waiters(st: State)
    for _, c in st.waiters do
        c:off()
    end
    table.clear(st.waiters)
end

-- One on() per event name for every guard; called by start() (never at the top level).
local function subscribe()
    if subscribed then
        return
    end
    subscribed = true
    on("guard_spotted", function(ev)                     -- definition 2
        local m = memo_for(ev)
        if m then
            m.spot = ev.player
            m.spot_at = clock()
            if ev.player then
                m.target = ev.player                     -- Co-op notes
            end
        end
    end)
    on("guard_heard", function(ev)                       -- definition 5: per shooter (Co-op notes)
        local m = memo_for(ev)
        if m and ev.player then
            m.target = ev.player
        end
    end)
    on("guard_near_miss", function(ev)                   -- definition 6
        local m = memo_for(ev)
        if m then
            m.near_miss_at = clock()
            if ev.player then
                m.target = ev.player                     -- Co-op notes
            end
        end
    end)
    on("guard_witnessed", function(ev)                   -- definition 7 ("hit" or "death", either counts)
        local m = memo_for(ev)
        if m then
            m.witnessed_at = clock()
        end
    end)
    on("anim_end", function(ev)                          -- rules 13, 14, 28
        local m = memo_for(ev)
        if m then
            m.anim_end = true
            m.fidget = false
        end
    end)
    on("guard_died", function(ev)                        -- rule 30: the guard's task ends, inert
        local k = ev.chr
        local st = if k then states[k] else nil
        if st then
            if st.task then
                st.task:cancel()
                st.task = nil
            end
            st.mode = "inert"
            drop_waiters(st)
        end
    end)
end

-- ------------------------------------------------------------------ the interface (CONTRACT.md)

function guard_ai.mode_of_id(id: number): string?
    return BY_ID[id]
end

function guard_ai.mode(g: Target): string?
    local k = key(g)
    local st = if k then states[k] else nil
    return if st then st.mode else nil
end

function guard_ai.stop(g: Target)
    local k = key(g)
    if not k then
        return
    end
    local st = states[k]
    if st then
        if st.task then
            st.task:cancel()
        end
        drop_waiters(st)
    end
    states[k] = nil
end

function guard_ai.start(g: Target, mode: string, opts: Opts?)
    local k = key(g)
    if not k then
        return
    end
    subscribe()
    local o: Opts = opts or {}
    local first = if MODES[mode] then mode else "standard"
    local fallback = if o.fallback and MODES[o.fallback] then o.fallback else "standard"

    local old = states[k]
    local was_running = old ~= nil and old.task ~= nil
    if old then
        if old.task then
            old.task:cancel()
        end
        drop_waiters(old)    -- on_end() belongs to the replaced mode
    end
    g:set_ai(1)          -- natively inert: only the Luau mode decides (CONTRACT.md)
    g:stop()             -- no follow order of a level script fights the mode
    if was_running and not gone(g) and g:is_moving() then
        g:halt()         -- a run the previous mode issued does not carry the guard away
    end

    runs += 1
    local st: State = {
        mode = first, task = nil, fallback = fallback, alarm_pad = o.alarm_pad,
        handback = o.fallback ~= nil, run = runs, waiters = {},
    }
    states[k] = st
    st.task = spawn(function()
        drive(g, st, first, o.patrol)
    end)
end

-- fn(mode) runs once, when the mode g runs now ends and hands over (the "guard_mode_end" event, see the top).
-- Only a guard started with opts.fallback raises it. Never runs when g dies, when start() / stop() replaces the
-- mode first, or for a mode that never ends; nothing happens when g runs no mode.
function guard_ai.on_end(g: Target, fn: (mode: string) -> ())
    local k = key(g)
    local st = if k then states[k] else nil
    if not st or not st.task or not g.chr then
        return
    end
    local conn: Connection? = nil
    conn = on("guard_mode_end", function(ev)
        if conn then
            conn:off()
            local i = table.find(st.waiters, conn)
            if i then
                table.remove(st.waiters, i)
            end
            conn = nil
            fn((ev :: any).mode)
        end
    end, { chr = g.chr :: number, run = st.run })
    table.insert(st.waiters, conn :: Connection)
end

-- Every setup guard with a generic list runs the matching mode; the level's own lists are left alone.
function guard_ai.boot()
    if booted then
        return
    end
    booted = true
    subscribe()
    for _, g in all_guards() do
        local id = g:setup_ai()
        local name = if id then BY_ID[id] else nil
        if name then
            guard_ai.start(g, name)   -- start() makes it inert natively (set_ai(1)) first
        end
    end
end

return guard_ai
