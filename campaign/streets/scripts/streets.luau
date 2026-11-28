-- streets: the Streets level (specs/streets.md). Bond chases Ourumov's car through St. Petersburg against a clock,
-- meets Valentin (Secret Agent and up), while soldiers and civilians keep appearing around him.
-- This file: objectives, the characters' start, the chase clock (rules 1-3), the street troops (rules 13-14), the
-- end (rule 22) and the outro. Valentin is in streets_valentin (rules 5-12), the spawners, the hunter, the civilians
-- and the civilian budget in streets_spawn (rules 4, 15-21).
local guard_ai = require("guard_ai")
guard_ai.boot()

local lib = require("campaign_lib")
local valentin = require("streets_valentin")
local spawner = require("streets_spawn")
local intro = require("intro")

local STEP: number = lib.STEP

-- ---------------------------------------------------------------- objectives
objectives.add("a", text("641B") or "", 1)    -- contact Valentin (Secret Agent and up)
objectives.add("b", text("6419") or "", 0)    -- pursue Ourumov and Natalya
objectives.add("c", text("641A") or "", 0)    -- minimise civilian casualties (every difficulty)

local ending = false
local function is_ending(): boolean
    return ending
end

-- ---------------------------------------------------------------- characters
-- Soldiers: the standard generic guard (global.md); chrs 2, 9, 25 throw grenades (10%).
local SOLDIERS = { 0, 1, 2, 3, 4, 5, 9, 10, 11, 18, 19, 20, 24, 25, 26, 27, 28, 32, 33, 35, 36, 37, 38 }
local GRENADE_10 = { [2] = true, [9] = true, [25] = true, [6] = true, [12] = true, [14] = true, [15] = true,
    [17] = true, [21] = true, [23] = true }
for _, n in SOLDIERS do
    local g = chr(n)
    if g:exists() then
        if GRENADE_10[n] then
            g:set_stats({ grenade_chance = 10 })
        end
        if guard_ai.mode(g) == nil then
            guard_ai.start(g, "standard")
        end
    end
end

-- The street ambush: they stand still and do nothing until rule 13.
local AMBUSH_1A, AMBUSH_1B = { 6, 7, 8 }, { 12, 13, 14 }
local AMBUSH_2A, AMBUSH_2B = { 15, 16, 17 }, { 21, 22, 23 }
for _, group in { AMBUSH_1A, AMBUSH_1B, AMBUSH_2A, AMBUSH_2B } do
    for _, n in group do
        local g = chr(n)
        if g:exists() then
            guard_ai.stop(g)
            g:halt()
            if GRENADE_10[n] then
                g:set_stats({ grenade_chance = 10 })
            end
        end
    end
end

-- rule 14: the posted riflemen
for _, n in { 29, 30 } do
    local g = chr(n)
    if g:exists() then
        g:set_stats({ armour = 4 })
        spawn(function()
            lib.post_guard(g)
        end)
    end
end

-- rule 18: chr 34 hunts from the start
spawn(function()
    local h = chr(34)
    if h:exists() then
        spawner.hunter(h)
    end
end)

-- ---------------------------------------------------------------- rules 13: the ambush roles
local function rocket_trooper(g: Target)
    g:give_weapon(25, { model = 211 })           -- rocket launcher
    g:give_hat(222)                              -- red beret
    g:set_stats({ speed = 80, accuracy = 100, reaction = 80 })
    while g:exists() do
        if g:distance() <= 1000 then
            guard_ai.start(g, "standard")        -- a player came within 1000
            return
        end
        local p = g:line_to_player()
        if p ~= nil and not g:is_busy() then
            g:fire_at(player(p), { stance = if lib.chance(78) then "stand" else "kneel" })
            lib.wait_idle(g, 8)
        end
        wait(STEP)
    end
end

local function rifleman(g: Target)
    g:give_weapon(8, { model = 184 })            -- KF7
    g:set_stats({ speed = 20, accuracy = 20, reaction = 50 })
    guard_ai.start(g, "standard")
end

local function assign(rockets: { number }, rifles: { number })
    for _, n in rockets do
        local g = chr(n)
        if g:exists() then
            spawn(function()
                rocket_trooper(g)
            end)
        end
    end
    for _, n in rifles do
        local g = chr(n)
        if g:exists() then
            rifleman(g)
        end
    end
end

spawn(function()
    -- rule 13: a player enters the room of pad 44; two independent coin flips
    while lib.player_in_room(44) == nil do
        wait(STEP)
    end
    if lib.chance(50) then
        assign(AMBUSH_1A, AMBUSH_1B)
    else
        assign(AMBUSH_1B, AMBUSH_1A)
    end
    if lib.chance(50) then
        assign(AMBUSH_2A, AMBUSH_2B)
    else
        assign(AMBUSH_2B, AMBUSH_2A)
    end
end)

-- ---------------------------------------------------------------- the chase clock (rules 1-3)
local clockOn = false
spawn(function()
    -- rule 1: 10 s of play, counted from the intro's hand-over (the mission clock starts there, intro.md)
    intro.wait()
    while intro.since() < 10 do
        wait(STEP)
    end
    if ending then
        return
    end
    countdown.set(if mission.difficulty() >= 1 then 300 else 180)
    countdown.show(true)
    countdown.start()
    clockOn = true
    -- rule 2
    while not ending and countdown.left() > 0 do
        wait(STEP)
    end
    clockOn = false
    if ending then
        return
    end
    objectives.fail("b")
    lib.top("640A")
    countdown.show(false)
    countdown.stop()
end)

-- rule 3: Valentin's associates were told
local function reward()
    if clockOn then
        countdown.set(360)              -- from whatever it showed, and it keeps running
    end
end

-- ---------------------------------------------------------------- Valentin, spawners
spawn(function()
    valentin.run(reward)                -- rules 5-12
end)
spawner.run(is_ending)                  -- rules 4, 15-21

-- ---------------------------------------------------------------- the outro
local function outro()
    local body = bond_body(0)           -- co-op: one Bond model, player 1's
    -- 1. while black
    cinema.hide_all_chrs(true)
    lib.end_on_skip()
    for _, n in lib.players() do
        player(n):hide_weapon(true)
    end
    -- 2. the fixed camera of tag 1
    camera.fixed(tag(1))
    -- 3. Bond's model, the tank
    body:teleport(247)
    body:show(true)
    local tank = tag(0)
    if tank:in_room_of(246) then
        -- in the room of pad 246: snapped onto pad 246 while still black (only the final placement shows);
        -- anywhere else it is left where it is
        tank:move_to(246)
    end
    body:anim(0x6B)
    lib.fade_in()
    music.play_extra(0)
    -- 4. 2 s after the fade-in: the hand grenade of tag 2 blows up
    wait(2)
    local grenade = tag(2)
    grenade:set_invincible(false)
    grenade:destroy()
    -- 5. 5 s after the fade-in: fade and end (won)
    wait(3)
    lib.fade_out()
    mission.finish()
end

-- ---------------------------------------------------------------- the intro
-- Start pad 245, watch time, outfit 0 and the PP7 with 100 rounds stay setup data; one of the two fixed cameras
-- with title 641C, the swirl and the body's table pose are played by the shared intro module from
-- streets_intro_data. Rules 1 and 15 count from its hand-over (intro.since()).
intro.play(require("streets_intro_data"))

-- ---------------------------------------------------------------- the end (rule 22)
-- Co-op: the first living player at the end point ends the level for everyone.
local function at_end(): boolean
    for _, n in lib.living() do
        local p = player(n)
        if p:in_room_of(248) and p:distance_to(pad(248)) <= 1000 then
            return true
        end
    end
    return false
end
while not at_end() do
    wait(STEP)
end
ending = true
objectives.complete("b")
objectives.complete("c")                -- "as b" (a failed c stays failed)
countdown.stop()
cinema.begin()                          -- immune, no pickups, HUD hidden, controls locked, mission clock stopped
lib.fade_out()
if objectives.all_complete() then
    outro()
else
    mission.finish()                    -- lost
end
