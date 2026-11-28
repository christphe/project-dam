-- jungle_base: the base reinforcements of rules 24-28: six respawning slots near the base, started and stopped by
-- the rooms the players enter, ramped down over 600 s; each reinforcement sprints to a post and holds it (rule 23)
-- or hunts a player who comes close (rule 21). A require()d module: no wait() at its top level.
local lib = require("campaign_lib")
local guards = require("jungle_guards")

local B = {}

local STEP: number = lib.STEP
local SPAWN_PAD = 467
local START_ROOM = 456                 -- rule 24
local STOP_ROOMS = { 466, 441 }        -- rule 25
local PERIOD = 2                       -- rule 24: every 2 s
local EARLY_SHUT = 180                 -- rule 27
local ALL_SHUT = 600
local HAT = 217                        -- rule 26: green combat helmet
local GUN, GUN_MODEL = 13, 188         -- rule 26: AR33
local BODY = 0                         -- Characters table: body 0 (camguard) always; the head is random (omitted)

-- rule 24 / 26: the slots in their order, with their posts (78%) and alternative posts (22%); `early`: shut at 180 s
local SLOTS = {
    { chr = 0x27, post = 585, alt = 459, early = false },
    { chr = 0x28, post = 586, alt = 458, early = false },
    { chr = 0x29, post = 587, alt = 455, early = false },
    { chr = 0x1D, post = 577, alt = 456, early = true },
    { chr = 0x22, post = 578, alt = 457, early = true },
    { chr = 0x1E, post = 583, alt = 452, early = true },
}

local function in_stop_room(): boolean
    for _, p in STOP_ROOMS do
        if lib.player_in_room(p) ~= nil then
            return true
        end
    end
    return false
end

-- rule 26: one reinforcement's way to its post
local function reinforcement(g: Target, post: number)
    g:run_to(post, "sprint")
    while g:exists() do
        if g:distance() <= 1000 then
            guards.run(g, "hunt")           -- rule 21, then the jungle guard of rules 18-22
            return
        end
        if g:distance_to(pad(post)) <= 100 then
            g:set_stats({ armour = 2 })
            lib.post_guard(g)               -- rule 23
            return
        end
        if not g:is_moving() then
            -- rule 26: stopped short (no route, or blocked): it re-issues the sprint to the same post at once and
            -- stays where it is, not firing, until it gets there or a player comes within 1000; never gives up
            g:run_to(post, "sprint")
        end
        wait(STEP)
    end
end

local function spawn_one(slot: { chr: number, post: number, alt: number, early: boolean })
    local g = spawn_guard({ chr = slot.chr, pad = SPAWN_PAD, body = BODY })
    if g == nil then
        return                              -- refused (in view, blocked): the next tick tries again
    end
    g:give_hat(HAT)
    g:give_weapon(GUN, { model = GUN_MODEL })
    table.insert(guards.spawned, g)
    local post = if lib.chance(78) then slot.post else slot.alt
    spawn(function()
        reinforcement(g, post)
    end)
end

-- The spawner, for the rest of the level. `ending()` is true once the end scene runs (rule 28).
function B.run(ending: () -> boolean)
    while true do
        -- rule 24: starts when a player enters the room of pad 456 (rule 25 / 27: with every slot open again)
        while lib.player_in_room(START_ROOM) == nil do
            wait(STEP)
        end
        local t0 = clock()
        local nextSpawn = clock()
        local shut: { [number]: boolean } = {}
        while true do
            if in_stop_room() then
                break                       -- rule 25 (also resets the rule 27 timer)
            end
            local t = clock() - t0
            if t >= ALL_SHUT then
                break                       -- rule 27: every slot shut, spawning stops
            end
            if t >= EARLY_SHUT then
                for _, s in SLOTS do
                    if s.early then
                        shut[s.chr] = true  -- rule 27 (the original's objective-d quirk is not reproduced)
                    end
                end
            end
            if clock() >= nextSpawn then
                nextSpawn = clock() + PERIOD
                if not ending() and not lib.any_in_cutscene() then   -- rules 24, 28
                    for _, s in SLOTS do
                        if not shut[s.chr] and not chr(s.chr):exists() then
                            spawn_one(s)
                        end
                    end
                end
            end
            wait(STEP)
        end
        -- the next start needs a new entry into the room of pad 456
        while lib.player_in_room(START_ROOM) ~= nil do
            wait(STEP)
        end
    end
end

return B
