-- runway: the Runway level (specs/runway.md). Bond fights across the service runway, picks up the plane's
-- ignition key, knocks out the guns and the missile battery, and escapes in the plane; the reinforcement slots
-- keep coming back while he is there.
-- Covers: the Characters table, objectives a-d, rules 1-7, Objects and world, the intro and outro, co-op notes.

local guard_ai = require("guard_ai")
local L = require("campaign_lib")
guard_ai.boot()

local STEP = 0.25
local FAST = 0.1
local KEY_TAG = 2
local PLANE_TAG = 3

-- ------------------------------------------------------------------ helpers
local function player_alive(n: number): boolean
    return L.in_game(n) and not player(n):is_dead()
end

-- ------------------------------------------------------------------ objectives
objectives.add("a", text("700F") or "", 0)   -- find the ignition key
objectives.add("b", text("7010") or "", 2)   -- destroy the gun emplacements (00 Agent, 007)
objectives.add("c", text("7011") or "", 1)   -- destroy the missile battery (Secret Agent and up)
objectives.add("d", text("7012") or "", 0)   -- escape in the plane

local exitStarted = false   -- rule 7 has begun

-- a: the key (tag 2) in a player's inventory; failed when the key is destroyed
spawn(function()
    while not tag(KEY_TAG):held_by() do
        if tag(KEY_TAG):is_destroyed() then
            objectives.fail("a")
            return
        end
        wait(STEP)
    end
    objectives.complete("a")
end)

-- b: the drone guns tag 0, 4, 5 destroyed
spawn(function()
    while not (tag(0):is_destroyed() and tag(4):is_destroyed() and tag(5):is_destroyed()) do
        wait(event("destroyed"))
    end
    objectives.complete("b")
end)

-- c: the missile battery tag 1 destroyed
spawn(function()
    while not tag(1):is_destroyed() do
        wait(event("destroyed"))
    end
    objectives.complete("c")
end)

-- ------------------------------------------------------------------ characters
-- Bodies, heads, hearing, vision and the grenade attribute of 0x14 are setup data (Characters table).
local function start_generic(n: number, mode: string, opts: { [string]: any }?)
    local g = chr(n)
    if g:exists() and guard_ai.mode(g) == nil then
        guard_ai.start(g, mode, opts)
    end
end
start_generic(0x17, "simple", { patrol = 0 })
start_generic(0x18, "simple", { patrol = 1 })
for _, n in { 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x14, 0x15 } do
    start_generic(n, "standard")
end

-- ------------------------------------------------------------------ reinforcements (rules 1-5)
local SLOTS = { 0x19, 0x1a, 0x1c, 0x1d, 0x1e }
local SLOT_BODY = { 2, 2, 2, 2, 37 }

-- rule 2: the slots' spawn pads by the room the player is in (tested in this order, first match wins)
local ROOMS: { { any } } = {
    { 165, { 26, 30, 171, 140, 171 } },
    { 168, { 143, 113, 171, 140, 171 } },
    { 147, { 143, 113, 171, 142, 171 } },
    { 145, { 103, 102, 142, 6, 142 } },
    { 142, { 26, 30, 5, 6, 5 } },
    { 172, { 103, 5, 31, 29, 31 } },
    { 132, { 29, 31, 142, 102, 142 } },
    { 104, { 142, 113, 27, 30, 27 } },
    { 102, { 28, 29, 26, 113, 26 } },
    { 53, { 31, 26, 30, 142, 30 } },
}
local slotPads: { number } = ROOMS[#ROOMS][2]   -- the players start in the room of pad 53

-- rule 2 (co-op notes: the first player found in a listed room, player 0 first)
local function update_slot_pads()
    for _, n in L.players() do
        if player_alive(n) then
            for _, r in ROOMS do
                if player(n):in_room_of(r[1]) then
                    slotPads = r[2]
                    return
                end
            end
        end
    end
end

local ATTACK: { { any } } = {   -- rule 5, in fall-through order
    { 4, "grenade" }, { 16, "sidestep" }, { 12, "roll_fire" }, { 8, "walk_fire" },
    { 20, "run_fire" }, { 20, "fire" }, { 21, "fire_kneel" },
}

local function do_move(g: Target, kind: string): boolean
    if kind == "grenade" then
        return g:throw_grenade()
    elseif kind == "fire" then
        return g:fire_at(nil, { stance = "stand" })
    elseif kind == "fire_kneel" then
        return g:fire_at(nil, { stance = "kneel" })
    end
    return g:combat_move(kind)
end

local function attack(g: Target): boolean
    local r = math.random() * 101
    local first = #ATTACK
    for i, e in ATTACK do
        r -= e[1]
        if r < 0 then
            first = i
            break
        end
    end
    for i = first, #ATTACK do
        if do_move(g, ATTACK[i][2]) then
            return true
        end
    end
    return false
end

-- rule 5: hunts the player for the rest of its life: sprints beyond 4000 units, runs otherwise; within 4500
-- units and in line of sight it attacks, a new move after each while it sees the player. Never gives up.
local function hunt(g: Target)
    spawn(function()
        while not L.dying_or_dead(g) do
            local d = g:distance()
            local sight = g:line_to_player() ~= nil or g:sees_player()
            if sight and d <= 4500 then
                if attack(g) then
                    L.wait_done(g)
                else
                    wait(STEP)
                end
            else
                if not g:is_moving() or d > 4000 then
                    g:run_to_player(if d > 4000 then "sprint" else "run")
                end
                wait(FAST)
            end
        end
    end)
end

-- rule 4: the loadout roll
local function loadout(g: Target)
    local r = math.random() * 99
    if r < 31 then
        g:set_stats({ health = 4.0, max_health = 4.0, armour = 0, grenade_chance = 229 * 100 / 255 })
        g:give_weapon(7)   -- Skorpion
    elseif r < 62 then
        g:set_stats({ health = 4.0, max_health = 4.0, armour = 1.0, grenade_chance = 204 * 100 / 255 })
        g:give_weapon(8)   -- AK-47
    else
        g:set_stats({ health = 4.0, max_health = 4.0, armour = 2.0 })
        g:give_weapon(7)
        g:give_weapon(7, { left_hand = true })
    end
end

-- rule 1 and rule 3: on for the whole level; from 2 s, every 2 s, a dead or gone slot guard comes back at the slot's
-- current pad, even in view
spawn(function()
    -- rule 1
    spawn(function()
        while not exitStarted do
            update_slot_pads()   -- rule 2
            wait(STEP)
        end
    end)
    wait(2)
    while not exitStarted do
        for i, n in SLOTS do
            local g = chr(n)
            if g:is_dead() or not g:exists() then
                local ng = spawn_guard({ chr = n, pad = slotPads[i], body = SLOT_BODY[i], force = true })
                if ng then
                    loadout(ng)   -- rule 4
                    hunt(ng)      -- rule 5
                end
            end
        end
        wait(2)
    end
end)

-- ------------------------------------------------------------------ the plane (rules 6, 7)
local plane = tag(PLANE_TAG)
plane:make_usable()
plane:set_rotor(0)   -- rotor stopped until the outro (Objects and world)

-- rule 6
spawn(function()
    while not plane:is_destroyed() do
        wait(event("destroyed"))
    end
    if not exitStarted then
        L.top("7009")
        objectives.fail("d")
    end
end)

-- The outro (Cinematics: Outro)
local function outro()
    -- 1. behind the black screen: weapons away, the exit camera, every character (Bond's model too) hidden
    for _, n in L.players() do
        player(n):hide_weapon(true)
        bond_body(n):show(false)
    end
    camera.fixed(tag(6))
    cinema.hide_all_chrs(true)
    -- 2. from here a button press (any player) fades to black and ends the level: cinema.skipped() below
    -- 3. fade in; extra music slot 0; the plane on pad 76 takes off
    local t0 = clock()
    screen.fade_in()
    music.play_extra(0, { total = 255 })
    plane:move_to(76)
    plane:set_rotor(620, 0)
    plane:anim(1)   -- the aircraft's take-off animation (anim() on an object: its model animation)
    local engine = false
    while clock() < t0 + 3.7 do
        if cinema.skipped() then
            break
        end
        -- 4. the engine sound 1 s after the take-off starts
        if not engine and clock() >= t0 + 1 then
            engine = true
            sound.play(0xF8)
        end
        wait(0.1)
    end
    -- 5. about 3.7 s after the fade-in, fade to black; the level ends as a win
    screen.fade_out()
    L.wait_fade({ settle = 0, poll = 0.1 })
    mission.win()
end

-- rule 7: any player uses the plane (co-op notes); the key test is on that player
on("activated", function(ev)
    if exitStarted or plane:is_destroyed() then
        return
    end
    local p = ev.player
    if p == nil then
        return
    end
    if not tag(KEY_TAG):held_by(p) then
        L.top("7008")
        return
    end
    if not player_alive(p) then
        return
    end
    exitStarted = true
    objectives.complete("d")
    plane:set_invincible(true)
    -- every player: invulnerable, no pickups, no HUD, no control
    -- TODO(api): cinema.begin does not declare that the mission clock stops
    cinema.begin()
    screen.fade_out()
    L.wait_fade({ settle = 0, poll = 0.1 })
    if not objectives.all_complete() then
        mission.fail()
    else
        outro()
    end
end, { tag = PLANE_TAG })

-- ------------------------------------------------------------------ objects and world
-- The drone guns (tags 0, 4, 5) and the missile battery (tag 1) are immune to gunfire and active from the start;
-- the tank, the key object and the door rename lines are setup data. No alarm, fog or TV script.

-- ------------------------------------------------------------------ the intro (Cinematics: Intro)
-- Staged by the shared intro module ("level_intro": false in mission.json) from runway_intro_data (one of the six
-- fixed cameras, the swirl, the body playing animation 99 from the swirl start). The start pad 53, PP7 silenced
-- (item 5) with 100 rounds, cuff 3, watch 1:10 and the other start slots stay the setup's own.
require("intro").play(require("runway_intro_data"))
