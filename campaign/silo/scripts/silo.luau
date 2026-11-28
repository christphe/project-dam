-- silo: the Silo level (specs/silo.md): characters, objectives a-e, the bomb countdown (rules 3-4), the circuit
-- boards (rule 5), the scientists (rules 6-12, silo_scientist), Ourumov (rules 13-16), the stationary gunners
-- (rule 17), the dormant soldiers (rule 18), door 0x0A (rule 19), the intro animation (rule 20) and the exit with
-- its exit scene (rule 21).
local guard_ai = require("guard_ai")
guard_ai.boot()

local U = require("campaign_lib")
local S = require("silo_scientist")
local Sig = require("silo_signals")   -- the guard events, latched (see its header)

local st = require("silo_util").state

local PLASTIQUE = 0x22   -- item 34
local CAMERA = 40
local PP7_SILENCED = 5
local RULE19_ITEM = 1   -- rule 19: "unarmed (item 1)"; item 1 is the fist in the API (held_item)
local EMPTY = 0         -- hold_item(0) empties a hand
local diff = mission.difficulty()
local STEP: number = U.STEP

-- ============================================================ objectives
objectives.add("a", text("8812") or "", 2)
objectives.add("b", text("8813") or "", 0)
objectives.add("c", text("8814") or "", 1)
objectives.add("d", text("8815") or "", 1)
objectives.add("e", text("8816") or "", 0)

-- ============================================================ start (rules 1-2)
-- rules 1-2: the start pads (slots 0-2), the kit (silenced PP7, plastique, camera and their ammo), outfit and watch
-- time stay the setup's (mission.json "level_intro": false removes only its intro camera and swirl); the intro is
-- staged below (Cinematics, Intro).

-- ============================================================ the bomb countdown (rules 3-4)
local function charge_lying(): boolean
    return (item_landed(PLASTIQUE)) ~= nil
end

local function start_countdown(seconds: number, line: string)
    U.top(line)
    countdown.set(seconds)
    countdown.show(true)
    countdown.start()
end

-- rule 3: armed when the intro's swirl starts (the intro's swirl hook below)
local function bomb_countdown()
    local armed = clock()
    if diff <= 0 then
        while not charge_lying() do
            wait(STEP)
        end
        start_countdown(360, "8809")
    else
        while not charge_lying() and clock() - armed < 5 do
            wait(STEP)
        end
        if charge_lying() then
            start_countdown(360, "8809")
        else
            start_countdown(if diff == 1 then 450 else 510, "8846")
        end
    end
    -- rule 4
    local music_on, warned = false, false
    while not st.exiting do
        local left = countdown.left()
        if not music_on and left < 25 then
            music.play_extra(0)   -- the urgent x-track
            music_on = true
        end
        if not warned and left < 15 then
            U.top("880B")
            warned = true
        end
        if left <= 0 then
            countdown.stop()
            countdown.show(false)
            -- explosions around every living player without end: they kill the players (Co-op notes: all of them)
            explosions_around_players(true)
            return
        end
        wait(STEP)
    end
end

-- ============================================================ intro (Cinematics, Intro)
-- Played by the shared intro module from silo_intro_data: the fixed camera with 8844 and 8845, the swirl, and
-- rule 20 (Bond's body during the swirl: animation 0x62, its first frame for 1 s, then to its end, held: the
-- data's body steps). Rule 3: the bomb countdown is armed when the swirl starts (the swirl hook). Run alongside
-- the rest of the level, as before.
spawn(function()
    require("intro").play(require("silo_intro_data"), { swirl = bomb_countdown })   -- rules 3, 20
end)

-- ============================================================ objective a
-- a plastique charge placed in each of the four fuel rooms (the engine's deposit-in-room check, item_in_room)
spawn(function()
    local ROOMS = { 0x04, 0x0C, 0x58, 0x5F }
    while true do
        local all = true
        for _, r in ROOMS do
            if not item_in_room(PLASTIQUE, r) then
                all = false
                break
            end
        end
        if all then
            break
        end
        wait(STEP)
    end
    objectives.complete("a")
end)

-- ============================================================ objective b
on("photographed", function()
    if not objectives.is_complete("b") then
        objectives.complete("b")
    end
end, { tag = 0x00 })

-- ============================================================ objective c
on("picked_up", function()
    if not objectives.is_complete("c") then
        objectives.complete("c")
    end
end, { tag = 0x01 })

-- ============================================================ objective d and rule 5
local BOARDS = { 0x02, 0x03, 0x04, 0x05 }
local function boards_held()
    if objectives.is_failed("d") or objectives.is_complete("d") then
        return
    end
    for _, t in BOARDS do
        if not tag(t):held_by() then
            return
        end
    end
    objectives.complete("d")
end
for _, t in BOARDS do
    on("picked_up", boards_held, { tag = t })
    on("destroyed", function()
        if not objectives.is_complete("d") then
            objectives.fail("d")   -- rule 5
        end
    end, { tag = t })
end

-- ============================================================ scientists (rules 6-12)
local SCI_OPTS = { flee_pad = 0xD1 }
local KEYBOARD_OPTS = { flee_pad = 0xD1, keyboard = true }
local KEYBOARD = { [0x36] = true, [0x37] = true, [0x38] = true, [0x39] = true, [0x3B] = true, [0x3E] = true,
    [0x3F] = true }
for n = 0x36, 0x3F do
    local g = chr(n)
    if g:exists() then
        g:set_flag("civilian", true)   -- rule 6 (keycards and the DAT they carry are setup data)
        S.start(g, if KEYBOARD[n] then KEYBOARD_OPTS else SCI_OPTS)   -- rules 7-11
    end
end

-- rule 12: more than 2 scientists dead fails objective e. Every death counts ("guard_died"), not only the
-- players' kills that mission.civilians_killed() counts.
local scientists_dead = 0
on("guard_died", function(ev: MissionEvent)
    local c = ev.chr
    if c == nil or c < 0x36 or c > 0x3F then
        return
    end
    scientists_dead += 1
    if scientists_dead == 3 and not objectives.is_complete("e") then
        U.top("880C")
        objectives.fail("e")
    end
end)

-- ============================================================ soldiers
local function start_list(list: { number }, mode: string, clone: boolean?)
    for _, n in list do
        local g = chr(n)
        if g:exists() then
            if clone then
                g:set_flag("clone_sender", true)   -- Characters: clone on gunfire
            end
            guard_ai.start(g, mode)
        end
    end
end
start_list({ 0x05, 0x08, 0x09, 0x0E, 0x0F, 0x15, 0x17 }, "standard", true)
start_list({ 0x13, 0x23 }, "standard")
start_list({ 0x01, 0x02, 0x04, 0x06, 0x0C, 0x0D, 0x11, 0x12, 0x14, 0x16, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x22,
    0x24, 0x25, 0x26, 0x27, 0x2A, 0x2B, 0x2C, 0x2E, 0x2F, 0x30, 0x31, 0x33, 0x34 }, "simple_deaf")
for n = 0x40, 0x4E do   -- Characters: chr 0x40 + k patrols path k, then the simple guard
    local g = chr(n)
    if g:exists() then
        guard_ai.start(g, "simple", { patrol = n - 0x40 })
    end
end

-- rule 18: dormant soldiers wake when a player goes below a height
local function dormant(n: number, below: number)
    local g = chr(n)
    if not g:exists() then
        return
    end
    guard_ai.stop(g)
    g:set_flag("dormant", true)
    spawn(function()
        while not g:is_dead() do
            local low = false
            for i = 0, U.MAX_PLAYERS - 1 do
                if U.alive(i) then
                    local _, y = player(i):pos()
                    if y and y < below then
                        low = true
                    end
                end
            end
            if low then
                g:set_flag("dormant", false)
                guard_ai.start(g, "standard")
                return
            end
            wait(STEP)
        end
    end)
end
dormant(0x07, -900)
for _, n in { 0x28, 0x29, 0x2D, 0x32, 0x35 } do
    dormant(n, 0)
end

-- rule 17: the stationary gunners
local function gunner(g: Target)
    guard_ai.stop(g)
    g:set_stats({ speed = 30, accuracy = 20, grenade_chance = 20 })
    spawn(function()
        local misses = g:near_misses()
        local function triggered(): boolean
            local m = g:near_misses()
            local shot = m > misses or Sig.take_miss(g)
            misses = m
            return shot or Sig.take_witnessed(g) ~= nil or Sig.spotted(g) ~= nil or g:line_to_player() ~= nil
        end
        local function kneel_and_fire()
            g:kneel()
            wait(3 * math.random() ^ 3)   -- up to 3 s, under 1 s on average
            g:fire_at(nil, { stance = "kneel" })
        end
        while not g:is_dead() do
            g:fire_at(nil, { aim_only = true })   -- aims at the player without firing
            while not g:is_dead() and not triggered() do
                wait(STEP)
            end
            if g:is_dead() then
                return
            end
            local roll = math.random() * 100
            if roll < 39 then
                g:fire_at(nil)
            elseif roll < 61 then
                if not g:throw_grenade() then
                    kneel_and_fire()   -- falls back to kneeling
                end
            else
                kneel_and_fire()
            end
            wait(STEP)
            U.wait_idle(g, 5, { alive = U.not_dead })
        end
    end)
end
for _, n in { 0x03, 0x0A, 0x0B, 0x10, 0x1F, 0x20, 0x21 } do
    if chr(n):exists() then
        gunner(chr(n))
    end
end

-- ============================================================ Ourumov (rules 13-16)
local function ourumov_flee(o: Target)
    -- rule 16
    o:set_invincible(true)
    if o:health() < 20 then
        o:drop(tag(0x07))   -- his briefcase
    end
    local dest = pad(0xC7)
    local passed_ba, passed_be = false, false
    o:run_to(0xC7, "sprint")
    wait(STEP)
    while not o:is_dead() do
        if not passed_ba and o:in_room_of(0xBA) then
            passed_ba = true
            tag(0x0F):close()
        end
        if not passed_be and o:in_room_of(0xBE) then
            passed_be = true
            tag(0x10):close()
        end
        if o:distance_to(dest) < 500 then
            o:fade_out()
            return
        elseif not o:is_busy() then
            o:run_to(0xC7, "sprint")   -- stopped short: sprints on
        end
        wait(STEP)
    end
end

do
    local o = chr(0x00)
    if o:exists() then
        guard_ai.stop(o)
        -- Characters: armour 30, health 20, ratings 80, no auto-aim (briefcase and master keycard: setup data)
        o:set_stats({ armour = 30, health = 20, speed = 80, accuracy = 80, reaction = 80 })
        o:set_flag("no_autoaim", true)
        spawn(function()
            -- rule 13: frozen until a line of sight to the player
            o:halt()
            o:stop_anim()
            while not o:is_dead() and o:line_to_player() == nil do
                wait(STEP)
            end
            if o:is_dead() then
                return
            end
            -- rule 14
            U.top("880A")
            local t14 = clock()
            while not o:is_dead() do
                -- rule 15
                if o:distance() < 500 or o:health() < 20 or clock() - t14 >= 30 then
                    ourumov_flee(o)
                    return
                end
                if not o:is_busy() then
                    o:fire_at(nil, { stance = if U.chance(50) then "stand" else "kneel" })
                end
                wait(STEP)
            end
        end)
    end
end

-- ============================================================ door tag 0x0A (rule 19)
spawn(function()
    local d = tag(0x0A)
    local last: { [number]: number } = {}
    local opened = false
    while true do
        for i = 0, U.MAX_PLAYERS - 1 do
            if U.in_game(i) then
                local item = player(i):held_item()
                if item ~= last[i] then
                    if item == RULE19_ITEM then
                        d:open()
                        opened = true
                    elseif item == PP7_SILENCED and opened then
                        d:close()
                        opened = false
                    end
                    last[i] = item
                end
            end
        end
        wait(STEP)
    end
end)

-- ============================================================ the exit (rule 21)
local ended = false
local function end_level()
    if not ended then
        ended = true
        mission.win()
    end
end

local function real_weapon(item: number): boolean
    return item >= 2 and item <= 29 and item ~= 23
end

local function exit_scene(p: number)
    -- step 1: black, characters hidden, any button skips, exit doors snap open
    cinema.hide_all_chrs(true)
    spawn(function()
        while not ended do
            if cinema.skipped() then
                U.fade_out(1, { poll = 0.1 })
                end_level()
                return
            end
            wait(0.1)
        end
    end)
    tag(0x0D):open_now()
    tag(0x0E):open_now()
    -- step 2
    local body = bond_body(p)
    for i = 0, U.MAX_PLAYERS - 1 do
        if U.in_game(i) then
            player(i):hide_weapon(true)
        end
    end
    if not real_weapon(player(p):held_item()) then
        body:hold_item(PP7_SILENCED, "right")
    end
    body:show(true)
    -- step 3: shot 1
    camera.fixed(tag(0x0C))
    body:teleport(0xE8)
    local t0 = clock()
    screen.fade_in(1)
    -- step 4
    local animated, holstered, closing = false, false, false
    local door = tag(0x0D)
    while not ended do
        local t = clock() - t0
        if not animated and t >= 1 then
            body:anim(0x65, { from = 8, to = 109, hold = true })
            animated = true
        end
        if not holstered and t >= 1.9 then
            body:hold_item(EMPTY, "right")                   -- puts away the weapons of both hands
            body:hold_item(EMPTY, "left")
            holstered = true
        end
        if not closing and t >= 3.9 then
            door:close()
            closing = true
        end
        if closing and door:state() == "closed" then
            break   -- step 5
        end
        wait(0.05)
    end
    if ended then
        return
    end
    U.fade_out(1, { poll = 0.1 })
    -- step 6: the silo blows when the charges were placed
    if objectives.is_complete("a") and not ended then
        camera.fixed(tag(0x12))
        screen.fade_in(1)
        countdown.set(2)
        countdown.show(true)
        countdown.start()
        while countdown.left() > 0 and not ended do
            wait(0.05)
        end
        countdown.show(false)
        countdown.stop()
        local target = tag(0x11)
        target:set_invincible(false)
        target:explode()
        wait(3)
        if not ended then
            U.fade_out(1, { poll = 0.1 })
        end
    end
    end_level()   -- step 7
end

spawn(function()
    -- rule 21: a living player enters the exit lift room
    local p: number? = nil
    while p == nil do
        p = U.player_in_room(0xE8)
        if p == nil then
            wait(STEP)
        end
    end
    st.exiting = true
    explosions_around_players(false)   -- rule 4: the player has left
    if not objectives.is_failed("e") then
        objectives.complete("e")
    end
    cinema.begin({ keep_countdown = true })   -- invulnerable, no pickups, HUD hidden, controls locked
    countdown.stop()
    countdown.show(false)
    U.fade_out(1, { poll = 0.1 })
    if not objectives.all_complete() then
        mission.fail()
        return
    end
    exit_scene(p :: number)
end)
