-- statue_guards: the park guards and Janus' escorts of Statue Park (specs/statue.md rules 9, 10 and 22): the escort
-- loadout, the escort on guard at his post, and the hunting guard with its two ways of leaving.
-- A require()d module: it only defines functions (nothing waits at the top level).

local lib = require("campaign_lib")
local sig = require("statue_signal")

local M = {}

local STEP = 0.25

-- The escorts' guard posts (rule 7).
local POSTS: { [number]: number } = { [0x02] = 2, [0x03] = 3, [0x04] = 4, [0x05] = 5 }

-- Sprints to `p`, then fades out (1.5 s) and is removed.
local function leave_by(g: Target, p: number)
    g:run_to(p, "sprint")
    wait(STEP)
    while g:exists() and g:is_moving() do
        wait(STEP)
    end
    if g:exists() then
        g:fade_out()
    end
end

-- rule 22: the hunting guard. kind "park" or "escort".
function M.hunter(S: any, g: Target, kind: string)
    local function stop(): boolean
        if kind == "park" then
            -- park guards leave when the meeting with Valentin starts (and is not over), or Janus spawns
            return S.meeting == "on" or S.janus_spawned
        end
        -- escorts leave once objective e is complete
        return objectives.is_complete("e")
    end
    local stopped = lib.hunt_nearest(g, {
        range = 1500,   -- 15 m, in sight
        speed = function() return if lib.chance(68) then "sprint" else "run" end,
        reaim = 10,
        stop = stop,
    })
    if stopped then
        leave_by(g, if kind == "park" then 205 else 35)
    end
end

-- rule 10: the escort on guard at his post, gun aimed at the player, until the level turns hostile or a shot
-- just misses him.
local function on_guard(S: any, g: Target)
    local post = POSTS[g.chr or 0]
    if post then
        g:run_to(post, "walk")
    end
    local aiming = false
    local missed = sig.latch(g, "guard_near_miss")   -- a player's shot just missed him (the engine's signal)
    while g:exists() do
        if S.hostile or missed() then
            break
        end
        if not aiming and not g:is_moving() then
            g:fire_at(nil, { aim_only = true })
            aiming = true
        end
        wait(STEP)
    end
    sig.off(missed)
    if not g:exists() then
        return
    end
    S.set_hostile()
    g:fire_at(nil)
    M.hunter(S, g, "escort")
end

-- rule 9: the escort loadout (every spawn of chr 0x02-0x05), then rule 22 or rule 10.
function M.escort(S: any, g: Target)
    g:set_stats({ max_health = 4, health = 4, add_armour = 2, grenade_chance = 75 })   -- armour +2
    g:give_weapon(16, { left_hand = lib.chance(50) })
    if S.hostile or S.janus_gone then
        M.hunter(S, g, "escort")
    else
        on_guard(S, g)
    end
end

-- rule 21: a park guard's loadout (each respawn), then rule 22.
function M.park(S: any, g: Target)
    g:set_stats({ max_health = 4, health = 4, grenade_chance = 75 })
    g:give_weapon(8, { left_hand = lib.chance(50) })
    g:give_hat(0xD9)
    M.hunter(S, g, "park")
end

return M
