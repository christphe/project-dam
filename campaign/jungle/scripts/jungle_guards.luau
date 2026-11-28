-- jungle_guards: the jungle soldier of rules 18-22 (idle at its post, alert, attack, fight Natalya, hunt, look
-- around), shared by the setup's soldiers and the base reinforcements that lose their way to a post (rule 26).
-- Also holds the marks Natalya leaves on the guards she is after (rules 12, 13, 20) and the list of guards spawned
-- at run time (Natalya's targets besides the setup's). A require()d module: no wait() at its top level.
local lib = require("campaign_lib")
local guard_ai = require("guard_ai")

local G = {}

local STEP: number = lib.STEP
local NATALYA = 0x01
local FRESH = 2 * STEP                   -- a one-update signal still counts at the next decision this long

G.marked = {} :: { [number]: boolean }   -- chr -> Natalya has marked this guard (rules 12, 13)
G.spawned = {} :: { Target }             -- guards spawned by the level (rule 26)

local function natalya(): Target
    return chr(NATALYA)
end

-- Natalya and the guard share a room.
function G.same_room(a: Target, b: Target): boolean
    return a:same_room_as(b)
end

-- The engine's one-update guard signals (rule 19), latched from their events with the time they came in.
type Signals = { spot: number?, miss: number?, witness: number? }

local function listen(g: Target): (Signals, { Connection })
    local s: Signals = {}
    local f = { chr = g.chr or -1 }
    local cons = {
        on("guard_spotted", function() s.spot = clock() end, f),
        on("guard_near_miss", function() s.miss = clock() end, f),
        on("guard_witnessed", function() s.witness = clock() end, f),
    }
    return s, cons
end

-- The signal came in at most FRESH ago; it is used up by the read.
local function take(s: Signals, key: string): boolean
    local t: number? = (s :: any)[key]
    ;(s :: any)[key] = nil
    return t ~= nil and clock() - t <= FRESH
end

-- rule 20: the attack moves, in fall-through order, with their chances; the rest fires standing or kneeling
local MOVES = { "roll_fire", "side_hop", "sidestep", "side_run", "walk_fire", "run_fire" }
local MOVE_PCT = { 4, 4, 9, 16, 17, 16 }

local function attack_move(g: Target, p: number)
    local i = lib.roll_index(MOVE_PCT)
    for k = i, #MOVES do
        if g:combat_move(MOVES[k]) then
            return
        end
    end
    g:fire_at(player(p), { stance = if lib.chance(50) then "kneel" else "stand" })
end

-- One guard's state machine. Each state returns the next one.
function G.run(g: Target, first: string?)
    guard_ai.stop(g)            -- rule 18: the level's own behaviour, not the generic guard
    local state = first or "watch"
    local pointed = false       -- rule 20: the first-attack gesture is done
    local sig, cons = listen(g)

    local function watch(): string
        -- rule 18: stands at its post, now and then an idle fidget (about 0.8% per frame)
        while g:exists() do
            if not g:is_busy() then
                -- rule 19: alert, in this order (spotted, witnessed, near miss: the guard events, latched)
                local spotted = take(sig, "spot")
                local saw = take(sig, "witness")
                local missed = take(sig, "miss")
                if spotted then
                    return "attack"
                end
                if g:alerted_within(10) then
                    return "hunt"
                end
                if g:heard_shot_within(10) or saw then
                    return "reinforce"
                end
                if missed then
                    return if lib.chance(50) then "attack" else "look"
                end
                if lib.chance(11) then   -- 0.8% per frame over one 0.25 s poll
                    g:idle_anim()
                end
            end
            wait(STEP)
        end
        return "dead"
    end

    local function reinforce(): string
        -- rule 19: the try-clone-or-run-to-Bond generic guard (global.md), then back here
        lib.run_generic(g, "reinforce", { alive = function(x: Target): boolean return x:exists() end, timeout = 20 })
        return "watch"
    end

    local function attack(): string
        local p: number? = lib.nearest_player(g)
        if p == nil then
            return "watch"
        end
        -- rule 20: the first time, point and shout (12.5%) or try a grenade
        if not pointed then
            pointed = true
            if lib.chance(12.5) then
                g:point_at_player()
            elseif lib.chance(g:grenade_chance()) then
                g:throw_grenade()
            end
            lib.wait_idle(g, 4)
        end
        -- rule 20: 39% of the time it checks Natalya first
        local nat = natalya()
        if nat:exists() and lib.chance(39) then
            local marked = G.marked[g.chr or -1] == true
            if (g:distance_to(nat) <= 800 or marked) and G.same_room(g, nat) then
                return "natalya"
            end
        end
        if g:distance() > 1500 then
            return "hunt"
        end
        attack_move(g, p)
        lib.wait_idle(g, 8)
        return "watch"
    end

    local function fight_natalya(): string
        -- rule 20a
        local nat = natalya()
        if lib.chance(50) then
            g:set_flag("can_shoot_guards", true)
        end
        local stance = if lib.chance(50) then "kneel" else "stand"
        if g:fire_at(nat, { stance = stance, aim_only = true }) then
            wait(1)
            if not nat:exists() then
                return "hunt"
            end
            g:fire_at(nat, { stance = stance })
            lib.wait_idle(g, 8)
        else
            g:face(nat)
            wait(1)
        end
        if not nat:exists() then
            return "hunt"
        end
        return "attack"
    end

    local function hunt(): string
        -- rule 21: runs to the player's position; attacks if it sees him within 1500 on the way
        local p = lib.nearest_player(g)
        if p == nil or not g:run_to_player("run", p) then
            wait(STEP)
            return "watch"
        end
        while g:exists() and g:is_moving() do
            if g:sees_player() and g:distance() <= 1500 then
                return "attack"
            end
            wait(STEP)
        end
        return "watch"
    end

    local function look(): string
        -- rule 22
        g:look_around()
        lib.wait_idle(g, 4)
        return "hunt"
    end

    local states: { [string]: () -> string } = {
        watch = watch, reinforce = reinforce, attack = attack, natalya = fight_natalya, hunt = hunt, look = look,
    }
    while g:exists() and state ~= "dead" do
        local f = states[state]
        if f == nil then
            break
        end
        state = f()
    end
    for _, c in cons do
        c:off()
    end
    -- rule 18: when it dies it stays dead (nothing more to do)
end

return G
