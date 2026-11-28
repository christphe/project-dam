-- bunker2: the Severnaya bunker, captured (specs/bunker2.md). Main script: objectives, the generic guards,
-- the jailer, the evidence, the alarm reinforcements, the countdown and the escape with its cinematic.
-- Natalya (the cell talks, following, the console) is in bunker2_natalya.luau. The intro is staged here
-- (mission.json "level_intro": false; the start pad, inventory, outfit and watch time stay the setup's).
local guard_ai = require("guard_ai")
guard_ai.boot()
local L = require("campaign_lib")
local natalya = require("bunker2_natalya")

local STEP = 0.25
local diff = mission.difficulty()
local S = natalya.state     -- shared flags: freed, conv_done, countdown_started, exiting

-- ------------------------------------------------------------------ objectives

objectives.add("a", text("742F") :: string, 1)
objectives.add("b", text("7430") :: string, 0)
objectives.add("c", text("7431") :: string, 1)
objectives.add("d", text("7432") :: string, 2)
objectives.add("e", text("7433") :: string, 0)

-- rule 13: both lists held (casualty list tag 0, staff list tag 1): a.
spawn(function()
    while not (tag(0):held_by() and tag(1):held_by()) do wait(STEP) end
    if diff >= 1 then objectives.complete("a") end
end)

-- b: the tape (tag 2) held; rule 13: the tape destroyed fails b.
-- d: the manual (tag 34) held; destroyed fails d.
-- Whichever comes first counts: picked up (complete) or destroyed (fail).
local function pickup_objective(id: string, t: number, min_diff: number)
    if diff < min_diff then return end
    local settled = false
    on("picked_up", function()
        if settled then return end
        settled = true
        objectives.complete(id)
    end, { tag = t })
    on("destroyed", function()
        if settled then return end
        settled = true
        objectives.fail(id)
    end, { tag = t })
end
pickup_objective("b", 2, 0)
pickup_objective("d", 34, 2)

-- c: the six cameras tags 28-33 destroyed.
spawn(function()
    while true do
        local all = true
        for t = 28, 33 do
            if not tag(t):is_destroyed() then all = false end
        end
        if all then break end
        wait(event("destroyed"))
    end
    if diff >= 1 then objectives.complete("c") end
end)

-- rule 14: Natalya dies: e fails, top text 7422, bottom text 7423.
spawn(function()
    local n: Target = chr(0x00)
    if not n:is_dead() then wait(event("guard_died", { chr = 0x00 })) end
    objectives.fail("e")
    hud.top(text("7422") :: string)
    hud.bottom(text("7423") :: string)
end)

-- ------------------------------------------------------------------ characters

-- The generic guards of the Characters table (boot() starts them when the setup gives them a generic list;
-- the "clones on gunfire" flag and the hearing / vision values are setup data).
local STANDARD = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0E, 0x0F, 0x10,
    0x11, 0x14, 0x15, 0x16, 0x17, 0x18 }
local DEAF = { 0x0D, 0x13, 0x28, 0x29 }
for _, c in STANDARD do
    local g: Target = chr(c)
    if g:exists() and guard_ai.mode(g) == nil then guard_ai.start(g, "standard") end
end
for _, c in DEAF do
    local g: Target = chr(c)
    if g:exists() and guard_ai.mode(g) == nil then guard_ai.start(g, "simple_deaf") end
end
-- The patrols: walk their path, then the generic simple guard.
local PATROLS = { [0x19] = 0, [0x1A] = 0, [0x1B] = 1, [0x1C] = 1, [0x1D] = 1, [0x1E] = 1,
    [0x1F] = 2, [0x20] = 2, [0x21] = 2, [0x22] = 2 }
for c, path in PATROLS do
    local g: Target = chr(c)
    if g:exists() then guard_ai.start(g, "simple", { patrol = path }) end
end

-- rules 7 and 15: the relentless hunter (global.md, global list 0x000D; it never ends).
local function persistent_chaser(g: Target)
    guard_ai.start(g, "hunter")
end

-- ------------------------------------------------------------------ the jailer (chr 0x12)

local jailer: Target = chr(0x12)
local jailer_task: Task? = nil

-- rule 5: an exchange: three top texts at 0 / 5 / 15 s while a player is in the cell room, ending at 20 s.
local EXCHANGES = { { "741A", "741B", "741C" }, { "741D", "741E", "741F" } }
local function exchange(lines: { string })
    local times = { 0, 5, 15 }
    local t0 = clock()
    local i = 1
    while clock() - t0 < 20 and L.player_in_room(94) ~= nil do
        if i <= 3 and clock() - t0 >= times[i] then
            hud.top(text(lines[i]) :: string)
            i += 1
        end
        wait(STEP)
    end
end

local function walk_to(g: Target, p: number, speed: string, until_fn: (() -> boolean)?)
    g:run_to(p, speed)
    wait(STEP)
    while L.alive_guard(g) and g:is_moving() do
        if until_fn and until_fn() then g:halt() return end
        wait(STEP)
    end
end

-- rule 5: walks between pads 100 and 96; after the second conversation, two exchanges at the cell door.
if jailer:exists() then
    guard_ai.stop(jailer)
    jailer_task = spawn(function()
        local done = 0
        while L.alive_guard(jailer) do
            walk_to(jailer, if math.random() < 0.5 then 100 else 96, "walk")
            if S.conv_done >= 2 and done < 2 then
                walk_to(jailer, 99, "walk", function() return pad(99):near(10) end)
                jailer:face_player()
                done += 1
                exchange(EXCHANGES[done])
            end
            wait(STEP)
        end
    end)
end

-- rule 7: door tag 26 (prop 75) first opened: 7424, he kneels and fires on a line of sight; a near miss (the
-- event) makes him a persistent chaser. His walking ends for good.
spawn(function()
    wait(event("door", { prop = 75, state = "open" }))
    if jailer_task then jailer_task:cancel() end
    if not L.alive_guard(jailer) then return end
    hud.top(text("7424") :: string)
    jailer:halt()
    local missed = false
    local sub = on("guard_near_miss", function() missed = true end, { chr = 0x12 })
    while L.alive_guard(jailer) do
        if missed then
            sub:off()
            persistent_chaser(jailer)
            return
        end
        if jailer:line_to_player() ~= nil then
            jailer:fire_at(nil, { stance = "kneel" })
            wait(STEP)
            L.wait_idle(jailer, 10, { alive = L.alive_guard })
        else
            jailer:fire_at(nil, { stance = "kneel", aim_only = true })
        end
        wait(STEP)
    end
end)

-- ------------------------------------------------------------------ Natalya

natalya.start()  -- rules 1-4, 6, 8-10 (bunker2_natalya.luau)

-- ------------------------------------------------------------------ countdown (rules 11, 12)

local SCREENS = { { 5, 16 }, { 6, 15 }, { 7, 3 }, { 8, 15 }, { 9, 3 }, { 10, 5 }, { 11, 15 }, { 12, 19 },
    { 13, 5 }, { 14, 12 }, { 15, 8 }, { 16, 13 }, { 17, 13 }, { 18, 14 }, { 19, 13 }, { 20, 2 }, { 21, 16 },
    { 22, 13 }, { 23, 8 }, { 24, 17 }, { 25, 3 } }
local SOUNDS = { 0x00E3, 0x00C6, 0x00E3, 0x00E3, 0x00C6 }

-- The screens change in five steps (about a frame each), one of the five sounds at bound pad 10071 per step.
local function screens_change(bank_of: (number, number) -> number)
    local per = math.ceil(#SCREENS / 5)
    for step = 1, 5 do
        for i = (step - 1) * per + 1, math.min(step * per, #SCREENS) do
            local s = SCREENS[i]
            tag(s[1]):show_screen(bank_of(s[1], s[2]))
        end
        sound.play(SOUNDS[step], { from = pad(10071) })
        wait(1 / 60)
    end
end

spawn(function()
    while not S.countdown_started do wait(STEP) end
    -- rule 11
    spawn(function()
        screens_change(function(_, bank) return bank end)
    end)
    local ch = sound.play(0x00A2)
    sound.volume(ch, 0)
    spawn(function()
        wait(2)
        sound.volume(ch, 32500 / 32767, 30)
    end)
    countdown.set(60)
    countdown.show(true)
    countdown.start()
    music.play_extra(0)
    -- rule 12: below 15 s every screen to bank 46 (same five sounds); at 0 the countdown hides, explosions.
    while countdown.left() >= 15 do wait(STEP) end
    screens_change(function() return 46 end)
    while countdown.left() > 0 do wait(STEP) end
    countdown.stop()
    countdown.show(false)
    if S.exiting then return end
    explosions_around_players(true)  -- without end: the players die unless already out
end)

-- ------------------------------------------------------------------ alarm reinforcements (chr 0x23-0x27)

-- rule 15: the level's own hunter: runs at the player; within 500 with a line of sight, one random attack,
-- again while it keeps the line. (No sprint distance, re-plan interval or removal distance in the spec: it
-- runs, and re-plans when it arrives or stops.)
local HUNTER = { attack_range = 500 }

local function reinforcement_outfit(g: Target)
    local r = math.random()
    if r < 0.20 then
        g:set_stats({ max_health = 6.0, health = 6.0, speed = 30, accuracy = 30, grenade_chance = 20 })  -- 51/256
        g:give_weapon(8, { model = 184 })
        g:give_hat(220)
    elseif r < 0.40 then
        g:set_stats({ max_health = 4.0, health = 4.0, armour = 6.0, speed = 30, accuracy = 30 })
        g:give_weapon(7, { model = 193 })
        g:give_weapon(7, { model = 193, left_hand = true })
        g:give_hat(222)
    else
        g:set_stats({ max_health = 6.0, health = 6.0, armour = 8.0, speed = 50, accuracy = 50 })
        g:give_weapon(6, { model = 205 })
        g:give_weapon(6, { model = 205, left_hand = true })
        g:give_hat(221)
    end
end

spawn(function()
    if not alarm.is_on() then wait(event("alarm")) end
    while not S.exiting do
        for c = 0x23, 0x27 do
            if not S.exiting and not chr(c):exists() then
                local g = spawn_guard({ chr = c, pad = 50, body = 38, sunglasses = true })
                if g then
                    reinforcement_outfit(g)
                    if math.random() < 0.5 then
                        persistent_chaser(g)
                    else
                        L.start_hunter(g, HUNTER)
                    end
                end
            end
        end
        wait(30)
    end
end)

-- ------------------------------------------------------------------ escape (rule 16) and the exit cinematic

local function exit_cinematic()
    -- 1: all characters hidden, weapons hidden; any button ends the level.
    cinema.hide_all_chrs(true)
    for _, n in L.living() do
        player(n):hide_weapon(true)
        if n ~= 0 then bond_body(n):show(false) end
    end
    L.end_on_skip({ settle = 0 })
    -- 2: camera tag 27, kept on Bond.
    local body: Target = bond_body(0)
    camera.fixed(tag(27), { look_at = body })
    -- 3: Natalya shown again; put on pad 50 unless already in the room of pad 120; runs to 120, faces Bond.
    natalya.exit_scene()
    -- 4: Bond on pad 119, animation 89 in full; fade in.
    body:show(true)
    body:teleport(119)
    body:anim(89, { hold = true })
    L.fade_in(nil, { settle = 0 })
    -- 5: 4 s later fade to black; won.
    wait(4)
    L.fade_out(nil, { settle = 0 })
    mission.win()
end

-- rule 16: any living player entering the room of pad 51 (co-op: the first one in; the scene freezes all).
spawn(function()
    while L.player_in_room(51) == nil do wait(STEP) end
    if S.freed then objectives.complete("e") end  -- e: freed and in the room of pad 51
    S.exiting = true
    cinema.begin()  -- invulnerable, no pickups, the whole HUD hidden, controls locked
    -- TODO(api): no verb stops the mission clock (rule 16).
    L.fade_out(nil, { settle = 0 })
    if not objectives.all_complete() then
        mission.fail()
        return
    end
    exit_cinematic()
end)

-- ------------------------------------------------------------------ intro

-- Cinematics, Intro: played by the shared intro module ("level_intro": false in mission.json) from
-- bunker2_intro_data (one of the three fixed cameras with 7466, the swirl, the body's table pose).
require("intro").play(require("bunker2_intro_data"))
