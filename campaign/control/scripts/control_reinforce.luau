-- control_reinforce: the Control reinforcements after the override (specs/control.md rules 20-24): two shooter
-- slots (chrs 0x1C, 0x1D) that take window / balcony posts and fire at Natalya, two chaser slots (chrs 0x26,
-- 0x27) that hunt the player. Required by control.luau, which calls start() at the override (rule 9),
-- natalya_left() when she leaves her console (rules 11, 12, 16), slow_down() at rule 12, exiting() at rule 19.
-- The top level only defines functions.

local guard_ai = require("guard_ai")
local U = require("campaign_lib")
local STEP: number = U.STEP

local M = {}

local nat: Target? = nil
local left = false        -- rule 22: Natalya has left the console
local slow = false        -- rule 24: Natalya left for the lift on success
local stop = false        -- rule 19: the exit sequence started
local started = false

local D5K = 0x0A
local D5K_MODEL = 189
-- Characters table: body 0 (camguard) always; no head given, so spawn_guard picks the game's random male head
local SPAWN_BODY = 0

-- Natalya as a target for the shooters, while she lives and works.
local function natalya_target(): Target?
    if left or nat == nil then return nil end
    local n = nat :: Target
    if not n:exists() or n:is_dying() then return nil end
    return n
end

-- rule 21: a shooter's post (pad) and the glass it smashes there, if any
local function shooter_post(n: number): (number, number?)
    if n == 0x1C then
        if math.random() * 100 < 23 then
            return 126, 0x22
        end
        return U.pick({ { 31, 123 }, { 31, 115 }, { 38, 116 } }), nil   -- a fresh draw
    end
    -- chr 0x1D: shares of all spawns
    local r = math.random() * 99
    if r < 23 then return 124, 0x21 end
    if r < 31 then return 101, nil end
    if r < 62 then return 109, nil end
    return 107, nil
end

-- rules 21-22: one shooter
local function shooter(g: Target, n: number)
    g:set_stats({ speed = 80, accuracy = 80 })
    g:set_flag("can_shoot_guards", true)
    g:give_weapon(D5K, { model = D5K_MODEL })
    spawn(function()
        local post, glass = shooter_post(n)
        U.run_near(g, post, 150, "run")                     -- rule 21: to within 1.5 m, D5K in hand
        if glass ~= nil and g:exists() and not g:is_dying() then
            tag(glass):destroy()                             -- rule 21: smashes the window on arrival
        end
        -- rule 22: bursts of about 2 s at Natalya, 50% standing / 50% kneeling, as long as she lives and works
        while g:exists() and not g:is_dying() do
            local t = natalya_target()
            if t == nil then break end
            g:fire_at(t, { stance = if math.random() < 0.5 then "stand" else "kneel" })
            wait(2)
            g:halt()
        end
        -- rule 22: then it chases and attacks the player persistently (the relentless hunter, global.md)
        if g:exists() and not g:is_dying() then
            guard_ai.start(g, "hunter")
        end
    end)
end

-- rule 24: one chaser: runs at the player, attacks on sight with one random move, never gives up
local CHASER_MOVES = {
    { 4, "grenade" }, { 16, "sidestep" }, { 20, "walk_fire" }, { 20, "run_fire" }, { 20, "stand" }, { 21, "kneel" },
}
local function chaser(g: Target)
    g:set_stats({ speed = 50, accuracy = 40, health = 40, max_health = 40, armour = 20, grenade_chance = 75 })
    g:give_weapon(D5K, { model = D5K_MODEL, left_hand = math.random() < 0.5 })
    g:give_hat(220)
    spawn(function()
        while g:exists() and not g:is_dying() do
            if g:is_busy() then
                wait(STEP)
            elseif g:sees_player() then
                local m = U.pick(CHASER_MOVES)
                local ok = false
                if m == "grenade" then
                    ok = g:throw_grenade()
                elseif m == "stand" or m == "kneel" then
                    ok = g:fire_at(nil, { stance = m })
                else
                    ok = g:combat_move(m)
                end
                if not ok then
                    g:fire_at(nil, { stance = "stand" })
                end
                wait(STEP)
            else
                g:run_to_player("run")
                U.wait_until(function(): boolean
                    return not g:exists() or g:is_dying() or g:sees_player() or not g:is_moving()
                end)
            end
        end
    end)
end

-- rule 20: a shooter slot, every 1 s, spawns even in the player's view
local function shooter_slot(n: number, at: number)
    if stop or chr(n):exists() then return end
    local g = spawn_guard({ chr = n, pad = at, body = SPAWN_BODY, force = true })
    if g then shooter(g, n) end
end

-- rule 23: a chaser slot spawns out of the player's view at a random pad
local CHASER_PADS = {
    [0x26] = { { 16, 126 }, { 16, 71 }, { 16, 129 }, { 16, 202 }, { 16, 189 }, { 12, 61 }, { 10, 124 } },
    [0x27] = { { 16, 86 }, { 16, 215 }, { 16, 140 }, { 16, 165 }, { 16, 149 }, { 12, 183 }, { 10, 82 } },
}
local function chaser_slot(n: number)
    if stop or chr(n):exists() then return end
    local g = spawn_guard({ chr = n, pad = U.pick(CHASER_PADS[n]), body = SPAWN_BODY })
    if g then chaser(g) end
end

-- rule 24: the rooms where the chaser cycle stays at 1 s after the slow-down
local FAST_ROOMS = { 149, 159, 10090, 164, 166 }

function M.set_natalya(g: Target)
    nat = g
end

-- rule 9: the override starts the reinforcements
function M.start()
    if started then return end
    started = true
    spawn(function()                      -- rule 20: shooters every 1 s for the rest of the level
        while not stop do
            shooter_slot(0x1C, 63)
            shooter_slot(0x1D, 69)
            wait(1)
        end
    end)
    spawn(function()                      -- rules 23-24: the chaser cycle
        local since = 0
        while not stop do
            local fast = not slow or U.player_in_rooms(FAST_ROOMS) ~= nil
            if fast or since >= 40 then
                since = 0
                chaser_slot(0x26)
                chaser_slot(0x27)
            end
            wait(1)
            since += 1
        end
    end)
end

function M.natalya_left()                 -- rules 11, 12, 16 -> rule 22
    left = true
end

function M.slow_down()                    -- rule 12 -> rule 24
    slow = true
end

function M.exiting()                      -- rule 19
    stop = true
end

return M
