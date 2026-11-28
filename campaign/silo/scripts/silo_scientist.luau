-- silo_scientist: the Silo scientists (rules 7-11): keyboard work, the standing reactions, the dodge, cowering
-- and fleeing.
-- A require()d module: nothing here waits at the top level.
local guard_ai = require("guard_ai")
local U = require("campaign_lib")
local Sig = require("silo_signals")   -- the guard events, latched (see its header)

local S = {}

local STEP: number = U.STEP

export type Opts = {
    flee_pad: number,          -- where a fleeing scientist runs (rule 11: pad 0xD1, the silo entrance)
    keyboard: boolean?,        -- a keyboard worker (rule 7)
}

local COWER_RANGE = 2000
local NEAR = 100
local FLEE_NEAR = 500
local BREAK_PER_POLL = 100 * STEP / 2   -- rule 10: breaks at random about once every 2 s

type State = { misses: number, keyboard: boolean, gone: boolean, opts: Opts }

-- A player's shot landed near it since the last call.
local function new_shot(g: Target, st: State): boolean
    local n = g:near_misses()
    local shot = n > st.misses or Sig.take_miss(g)
    st.misses = n
    return shot
end

local function in_front(g: Target, degrees: number): boolean
    return g:distance() < NEAR or math.abs(g:angle_to_player()) < degrees
end

-- rule 11: flee to the flee pad. Returns "gone" (faded out, or dead) or "back" (go back to what sent it).
-- forever: keep fleeing until gone.
function S.flee(g: Target, st: State?, opts: Opts, forever: boolean?): string
    local s: State = st or { misses = g:near_misses(), keyboard = false, gone = false, opts = opts }
    local fp = pad(opts.flee_pad)
    g:drop_items()
    g:run_to(opts.flee_pad, "run")
    wait(STEP)
    while not g:is_dead() do
        if new_shot(g, s) then
            g:raise_arms()   -- rule 11: hands up for a moment, then runs on
            wait(0.5)
            g:run_to(opts.flee_pad, "run")
        elseif g:distance_to(fp) < FLEE_NEAR then
            if g:line_to_player() == nil and not g:on_screen() then
                g:fade_out()   -- 1.5 s fade and removal
                s.gone = true
                return "gone"
            elseif not forever then
                return "back"
            end
        elseif not g:is_busy() then
            if not forever then
                return "back"   -- stopped short
            end
            g:run_to(opts.flee_pad, "run")
        end
        wait(STEP)
    end
    return "gone"
end

-- rule 10: cower. Returns "gone" (fled away, dead) or "fight" (handed to the draw-and-fire generic mode).
local function cower(g: Target, st: State): string
    g:surrender()   -- hands up, drops what it holds
    local hits0 = g:hits()
    while not g:is_dead() do
        if g:hits() - hits0 > 1 then
            -- rule 10: hit more than once while cowering: draws a pistol and fights
            guard_ai.start(g, "draw_and_fire")
            return "fight"
        end
        local covered = g:is_aimed_at() or (g:line_to_player() ~= nil and g:distance() <= COWER_RANGE)
        if new_shot(g, st) or U.chance(BREAK_PER_POLL) or not covered then
            if S.flee(g, st, st.opts) == "gone" then
                return "gone"
            end
            g:surrender()   -- back to cowering
        end
        wait(STEP)
    end
    return "gone"
end

-- rule 9: the dodge (sidestep 41%, run aside 59%), repeated while the player stays in view and close or not behind.
local function dodge(g: Target)
    while not g:is_dead() do
        local move = if U.chance(41) then "sidestep" else "side_run"
        if not g:combat_move(move) then
            return
        end
        wait(STEP)
        U.wait_idle(g, 3, { alive = U.not_dead })
        if not (Sig.spotted(g) ~= nil and in_front(g, 180 - 25)) then
            return
        end
    end
end

-- rule 8: as on a death: 25% flee, 75% try a grenade (nothing when it cannot).
local function as_on_death(g: Target): string
    if U.chance(25) then
        return "flee"
    end
    g:throw_grenade()
    return "acted"
end

-- rule 8: as on seeing the player.
local function as_on_sight(g: Target): string
    if g:distance() < 1000 then
        return "cower"
    elseif g:hits() > 0 then
        return as_on_death(g)
    end
    return "dodge"
end

-- rule 8: the reaction of a standing scientist, in priority order; nil: it stands still.
local function decide(g: Target, st: State): string?
    local shot = new_shot(g, st)
    local seen = Sig.take_witnessed(g)
    if g:is_busy() then
        -- while still moving only the near-shot reaction applies
        return if shot then "cower" else nil
    end
    if shot or seen == "hit" then
        return "cower"
    end
    if seen == "death" or g:hits() > 0 then
        return as_on_death(g)
    end
    if Sig.spotted(g) ~= nil and (g:is_aimed_at() or U.chance(22)) then
        return as_on_sight(g)
    end
    if g:heard_shot_within(10) or g:alerted_within(10) then
        if U.chance(75) then
            if g:combat_move("side_run") then
                U.wait_idle(g, 3, { alive = U.not_dead })
                if in_front(g, 90) then
                    return as_on_sight(g)
                end
            end
            return "acted"
        end
        return nil
    end
    if g:line_to_player() ~= nil and in_front(g, 90) then
        return as_on_sight(g)
    end
    return nil
end

-- rules 7-11: the standing scientist's loop, until it is gone or fights.
local function standing(g: Target, st: State)
    while not g:is_dead() and not st.gone do
        local act = decide(g, st)
        if act then
            st.keyboard = false   -- rule 7: a disturbed worker stops typing for good
        end
        if act == "cower" then
            cower(g, st)
            return   -- rule 10 only ends gone or fighting: either way this behaviour is over
        elseif act == "flee" then
            if S.flee(g, st, st.opts) == "gone" then
                return
            end
        elseif act == "dodge" then
            dodge(g)
        elseif act == nil and st.keyboard and g:is_idle() and not alarm.is_on() and g:on_screen() then
            -- rule 7: idle, unalarmed and on a player's screen: types at its console (keyboard generic mode)
            if not U.run_generic(g, "keyboard") then
                return
            end
        end
        wait(STEP)
    end
end

local function new_state(g: Target, opts: Opts): State
    return { misses = g:near_misses(), keyboard = opts.keyboard == true, gone = false, opts = opts }
end

-- rules 7-8: start a standing scientist.
function S.start(g: Target, opts: Opts): Task
    guard_ai.stop(g)
    return spawn(function()
        standing(g, new_state(g, opts))
    end)
end

return S
