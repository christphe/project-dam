-- streets_spawn: the characters that keep coming (rules 15-21) and the civilian budget (rule 4): two soldier slots
-- and two civilian slots that follow the players through the streets, the hunter behaviour of the spawned soldiers
-- and chr 34 (rule 18), and the civilians who mill around the player and put their hands up when aimed at.
-- A require()d module: no wait() at its top level.
local lib = require("campaign_lib")
local guard_ai = require("guard_ai")
local intro = require("intro")

local S = {}

local STEP: number = lib.STEP
local START_AT = 20                    -- rule 15: seconds after the level start (of play: the intro's hand-over)
local SOLDIER_BODY, CIVILIAN_BODY = 2, 32

-- rule 15: the four slots; `col` is the slot's column in the area table of rule 16
local SLOTS = {
    { chr = 0x20, period = 5, soldier = true, col = 1 },
    { chr = 0x27, period = 6, soldier = false, col = 2 },
    { chr = 0x28, period = 8, soldier = false, col = 3 },
    { chr = 0x22, period = 9, soldier = true, col = 4 },
}

-- rule 16: the room the player is in (any pad of the list) -> spawn pads of soldier 0x20, civilian 0x27,
-- civilian 0x28, soldier 0x22; the first matching row wins
local AREAS = {
    { rooms = { 44, 46, 49 }, pads = { 54, 55, 67, 62 } },
    { rooms = { 52, 55, 67 }, pads = { 76, 75, 69, 70 } },
    { rooms = { 73, 76, 79 }, pads = { 84, 85, 99, 100 } },
    { rooms = { 59, 70, 94 }, pads = { 105, 106, 108, 109 } },
    { rooms = { 92, 100 }, pads = { 108, 109, 85, 114 } },
    { rooms = { 82, 85, 88 }, pads = { 105, 106, 111, 112 } },
    { rooms = { 106, 109, 112 }, pads = { 118, 121, 127, 130 } },
    { rooms = { 115, 118 }, pads = { 130, 137, 142, 143 } },
    { rooms = { 124, 130 }, pads = { 133, 134, 147, 148 } },
    { rooms = { 145, 143, 148 }, pads = { 153, 156, 160, 166 } },
    { rooms = { 137, 132, 138 }, pads = { 168, 169, 156, 157 } },
    { rooms = { 151, 154, 157 }, pads = { 139, 172, 169, 143 } },
    { rooms = { 163, 167, 172 }, pads = { 133, 137, 136, 154 } },
}
local ELSEWHERE = { 45, 46, 48, 49 }

-- ---------------------------------------------------------------- rule 4: the civilian budget (shared in co-op)
local budget = 15
local watching = true
local warned8, warned4 = false, false

local function budget_change(d: number)
    budget += d
    if not watching then
        return
    end
    if budget < 1 then
        lib.top("6409")
        objectives.fail("c")
        watching = false            -- after that the budget is no longer watched
    elseif budget < 4 and not warned4 then
        warned4 = true
        lib.top("6408")
    elseif budget < 8 and not warned8 then
        warned8 = true
        lib.top("6407")
    end
end

-- ---------------------------------------------------------------- rule 18: the hunter
local HUNT_MOVES: { string } = { "grenade", "sidestep", "roll_fire", "walk_fire", "run_fire", "stand", "kneel" }
local HUNT_PCT = { 8, 12, 12, 8, 20, 20 }     -- the rest (20%): fire kneeling

local function hunt_move(g: Target, p: number)
    local i = lib.roll_index(HUNT_PCT)
    for k = i, #HUNT_MOVES do
        local m: string = HUNT_MOVES[k]
        local ok: boolean
        if m == "grenade" then
            ok = g:throw_grenade()
        elseif m == "stand" or m == "kneel" then
            ok = g:fire_at(player(p), { stance = m })
        else
            ok = g:combat_move(m)
        end
        if ok then
            return
        end
    end
end

function S.hunter(g: Target)
    guard_ai.stop(g)
    while g:exists() do
        if g:distance() > 8000 then
            g:remove()                  -- vanishes at once
            return
        end
        local p = lib.nearest_player(g)
        if p == nil then
            wait(STEP)
            continue
        end
        local speed = if g:distance() > 4000 then "sprint" else "run"
        if g:run_to_player(speed, p) then
            while g:exists() and g:is_moving() do
                if g:distance() <= 4500 and g:line_to_player() ~= nil then
                    break
                end
                if g:distance() > 8000 then
                    g:remove()
                    return
                end
                wait(STEP)
            end
        else
            wait(STEP)
        end
        -- attacks while the line holds, chases again when it loses it
        while g:exists() and g:distance() <= 4500 do
            local q = g:line_to_player()
            if q == nil then
                break
            end
            hunt_move(g, q)
            lib.wait_idle(g, 8)
            wait(STEP)
        end
    end
end

-- ---------------------------------------------------------------- rules 19-21: civilians
local function civilian(g: Target)
    while g:exists() do
        -- rule 21: too far from every player: vanishes and gives its point back
        if g:distance() > 8000 then
            g:remove()
            budget_change(1)
            return
        end
        -- rule 20: aimed at: hands up, then every 4 s a roll
        if g:is_aimed_at() then
            g:halt()
            g:surrender()
            while g:exists() do
                wait(4)
                local i = lib.roll_index({ 22, 20, 20 })
                if i == 1 then
                    g:surrender()
                elseif i == 2 then
                    g:anim(0x94, { from = 0, to = 17 })
                elseif i == 3 then
                    g:anim(0x93, { from = 0, to = 25 })
                else
                    break                   -- 39%: back to milling around
                end
            end
            g:stand()
            continue
        end
        -- rule 19: mills around the nearest player, never attacks
        local p = lib.nearest_player(g)
        if p ~= nil and g:distance() > 2000 then
            if g:run_to_player("run", p) then
                while g:exists() and g:is_moving() and g:distance() > 1000 and not g:is_aimed_at() do
                    wait(STEP)
                end
                g:halt()
            end
        elseif p ~= nil and lib.chance(50) then
            local spot = g:spot_near_player(p)     -- a spot around the player
            if spot ~= nil and g:run_to(spot, "walk") then
                while g:exists() and g:is_moving() and not g:is_aimed_at() do
                    if g:distance() > 3000 then
                        g:halt()
                        break
                    end
                    wait(STEP)
                end
            end
        end
        wait(1)                             -- it waits (the spec gives no length: one decision a second)
    end
end

-- ---------------------------------------------------------------- rule 17: a spawned soldier's loadout
local function soldier(g: Target)
    local i = lib.roll_index({ 39, 47 })
    if i == 1 then
        g:set_stats({ health = 4, max_health = 4, armour = 0, grenade_chance = 90 })
        g:give_weapon(8, { model = 184 })        -- KF7
    elseif i == 2 then
        g:set_stats({ health = 4, max_health = 4, armour = 2, grenade_chance = 80 })
        g:give_weapon(8, { model = 184 })
    else
        g:set_stats({ health = 4, max_health = 4, armour = 2 })
        g:give_weapon(24, { model = 185 })       -- grenade launcher
    end
    g:set_flag("can_shoot_guards", true)          -- civilians in the way
    S.hunter(g)                                   -- rule 18
end

-- rule 16: the slot's pad for the area the followed player is in (co-op: player 1, else the first one present)
local function spawn_pad(col: number): number
    local ps = lib.players()
    local n = if #ps > 0 then ps[1] else 0
    local who = player(n)
    for _, a in AREAS do
        for _, r in a.rooms do
            if who:in_room_of(r) then
                return a.pads[col]
            end
        end
    end
    return ELSEWHERE[col]
end

-- rule 15: the four spawners, for the rest of the level; `ending()` is true once the end scene runs.
function S.run(ending: () -> boolean)
    for _, s in SLOTS do
        spawn(function()
            while intro.since() < START_AT do   -- 0 until the hand-over
                wait(STEP)
            end
            while not ending() do
                if not chr(s.chr):exists() and not lib.any_in_cutscene() then
                    local g = spawn_guard({
                        chr = s.chr,
                        pad = spawn_pad(s.col),
                        body = if s.soldier then SOLDIER_BODY else CIVILIAN_BODY,
                        force = true,           -- also in plain view of the player
                    })
                    if g ~= nil then
                        if s.soldier then
                            spawn(function()
                                soldier(g)
                            end)
                        else
                            budget_change(-1)   -- rule 4: one point per civilian spawn
                            spawn(function()
                                civilian(g)
                            end)
                        end
                    end
                end
                wait(s.period)
            end
        end)
    end
end

return S
