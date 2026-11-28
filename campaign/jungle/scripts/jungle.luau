-- jungle: the Jungle level (specs/jungle.md). Bond and Natalya cross the jungle to the base: destroy the drone
-- guns, kill Xenia, blow up the ammo dump (Secret Agent and up), bring Natalya alive to the base entrance.
-- This file: objectives, the characters' start, Xenia's sighting and death (rules 1-2), Natalya's start and death
-- (rules 8, 17), the end (rule 29), the scenery crates (rule 30) and the outro. The behaviours are in
-- jungle_xenia (rules 3-7), jungle_natalya (rules 9-16), jungle_guards (rules 18-22), campaign_lib.post_guard (rule 23) and
-- jungle_base (rules 24-28).
local guard_ai = require("guard_ai")
guard_ai.boot()

local lib = require("campaign_lib")
local guards = require("jungle_guards")
local natalya = require("jungle_natalya")
local xenia = require("jungle_xenia")
local base = require("jungle_base")

local STEP: number = lib.STEP
local XENIA, NATALYA = 0x00, 0x01

-- ---------------------------------------------------------------- objectives
objectives.add("a", text("4825") or "", 0)    -- destroy the drone guns
objectives.add("b", text("4826") or "", 0)    -- eliminate Xenia
objectives.add("c", text("4827") or "", 1)    -- blow up the ammo dump (Secret Agent and up)
objectives.add("d", text("4828") or "", 0)    -- escort Natalya to the base

local DRONE_GUNS = { 0, 1, 2, 9, 10, 11, 12 }
local AMMO_DUMP = { 20, 21, 22, 23, 24, 25, 26, 27, 28 }

local function all_destroyed(tags: { number }): boolean
    for _, t in tags do
        if not tag(t):is_destroyed() then
            return false
        end
    end
    return true
end

local function check_objects()
    if not objectives.is_complete("a") and all_destroyed(DRONE_GUNS) then
        objectives.complete("a")
    end
    if not objectives.is_complete("c") and all_destroyed(AMMO_DUMP) then
        objectives.complete("c")
    end
end
on("destroyed", function()
    check_objects()
end)
check_objects()

-- ---------------------------------------------------------------- characters
-- Jungle soldiers (rules 18-22); chrs 7, 10, 24, 29, 30, 34 throw grenades (80%). "Clone on gunfire" is the
-- setup's clone flag, used by the generic reinforcement decision of rule 19.
local SOLDIERS = { 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24,
    27, 28, 29, 30, 31, 32, 33, 34, 35, 36 }
local GRENADIERS = { [7] = true, [10] = true, [24] = true, [29] = true, [30] = true, [34] = true }
-- Posted riflemen (rule 23)
local POSTS = { 37, 25, 26, 38, 39, 40, 41 }

for _, n in SOLDIERS do
    local g = chr(n)
    if g:exists() then
        guard_ai.stop(g)
        g:set_flag("clone_sender", true)
        if GRENADIERS[n] then
            g:set_stats({ grenade_chance = 80 })
        end
        spawn(function()
            guards.run(g, "watch")
        end)
    end
end

for _, n in POSTS do
    local g = chr(n)
    if g:exists() then
        g:set_stats({ armour = 2 })
        spawn(function()
            lib.post_guard(g)           -- rule 23
        end)
    end
end

xenia.setup()

-- ---------------------------------------------------------------- Xenia (rules 1-2)
spawn(function()
    local x = chr(XENIA)
    -- rule 1: in a player's line of sight and on screen for the first time (co-op: any player)
    while x:exists() and not (x:line_to_player() ~= nil and x:on_screen()) do
        wait(STEP)
    end
    if not x:exists() then
        return
    end
    lib.top("481E")
    music.play_extra(0)
    xenia.fight()                       -- rules 3-7
end)

-- rule 2: Xenia dies (no script of the level removes her)
on("guard_died", function()
    music.stop_extra(0)
    objectives.complete("b")
end, { chr = XENIA })

-- ---------------------------------------------------------------- Natalya (rules 8, 17)
-- rule 17: Natalya dies (no script of the level removes her)
on("guard_died", function()
    lib.top("481F")
    objectives.fail("d")
end, { chr = NATALYA })

-- ---------------------------------------------------------------- scenery (rule 30)
spawn(function()
    local crates = { { 10, 3 }, { 12, 4 }, { 13, 5 } }     -- seconds after the start, tag
    for _, c in crates do
        while clock() < c[1] do
            wait(STEP)
        end
        local crate = tag(c[2])
        crate:set_invincible(false)     -- made destructible ...
        crate:destroy()                 -- ... and it explodes
    end
end)

-- ---------------------------------------------------------------- base reinforcements (rules 24-28)
local ending = false
spawn(function()
    base.run(function()
        return ending
    end)
end)

-- ---------------------------------------------------------------- the outro
local function outro()
    local nat = chr(NATALYA)
    local body = bond_body(0)           -- co-op: one Bond model, player 1's
    -- 1. while black
    cinema.hide_all_chrs(true)
    lib.end_on_skip()
    tag(0x1F):open_now()
    tag(0x20):open_now()
    local gun = lib.holds_gun(0, 4, 22)
    for _, n in lib.players() do
        player(n):hide_weapon(true)
    end
    -- 2. the fixed camera of tag 0x1E, not tracking Bond
    camera.fixed(tag(0x1E))
    -- 3. Natalya and Bond's model in place
    nat:set_hidden(false)
    nat:set_invincible(true)
    nat:teleport(591)
    nat:anim(0x5A, { from = 7 })
    body:teleport(592)
    body:show(true)
    body:set_invincible(true)
    if not gun then
        body:hold_item(0, "left")       -- hands emptied (item 0) ...
        body:hold_item(4, "right")      -- ... and a PP7 (model 191) in his hand
    end
    body:anim(0x5A)
    lib.fade_in()
    music.play_extra(1)
    -- 4. once Natalya is in the room of pad 468
    while not nat:in_room_of(468) do
        wait(0.1)
    end
    wait(0.2)
    nat:fire_at(pad(592), { stance = "kneel", aim_only = true })
    wait(0.5)
    body:aim_at(pad(591))
    -- 5. the door closes, fade, end (won)
    wait(1.5)
    tag(0x1F):close()
    wait(1.0)
    lib.fade_out()
    mission.finish()
end

-- ---------------------------------------------------------------- the intro
-- Start pad 514, watch time, outfit, the PP7 with 100 rounds, the remote mines and detonator stay setup data; one
-- of the three fixed cameras with title 4829, the swirl and the body's animation 103 are played by the shared
-- intro module from jungle_intro_data.
require("intro").play(require("jungle_intro_data"))

-- rule 8: once the intro is over, Natalya speaks and starts following
local natTask = spawn(function()
    if chr(NATALYA):exists() then
        lib.top("481C")
        natalya.run()                   -- rules 9-16
    end
end)

-- ---------------------------------------------------------------- the end (rule 29)
-- Co-op: the first living player to reach the room of pad 468 ends the level for everyone.
while lib.player_in_room(468) == nil do
    wait(STEP)
end
objectives.complete("d")
ending = true                           -- rule 28
natTask:cancel()                        -- the end scene takes Natalya over
chr(NATALYA):halt()
cinema.begin()                          -- immune, no pickups, HUD hidden, controls locked, mission clock stopped
lib.fade_out()
if chr(NATALYA):is_dead() then
    mission.finish()                    -- lost through objective d
elseif not objectives.all_complete() then
    mission.finish()                    -- lost
else
    outro()
end
