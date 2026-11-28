-- train: the Train level (specs/train.md). Bond works through the armoured train, destroys the brake units,
-- survives the standoff in the rear carriage, lets Natalya trace the Janus base, and escapes through the floor.
-- Covers: the Characters table, objectives a-e, rules 1-29 (rules 1-5 in train_guards.luau), Objects and world,
-- the intro and outro, and the co-op notes. Rule numbers are in the comments where each rule is done.

local guard_ai = require("guard_ai")
local L = require("campaign_lib")
guard_ai.boot()

local tg = require("train_guards")

local STEP = 0.25
local diff = mission.difficulty()
if diff < 0 then
    diff = 3
end

-- ------------------------------------------------------------------ players (co-op helpers)
local function player_alive(n: number): boolean
    return L.in_game(n) and not player(n):is_dead()
end

-- The first living player within `radius` of pad `p` (player 0 first), or nil.
local function player_near_pad(p: number, radius: number): number?
    for _, n in L.players() do
        if player_alive(n) and player(n):distance_to(pad(p)) <= radius then
            return n
        end
    end
    return nil
end

local function in_room(p: number): boolean
    return player_in_room(p) ~= nil
end

-- ------------------------------------------------------------------ objectives
objectives.add("a", text("9022") or "", 0)   -- destroy every brake unit
objectives.add("b", text("9023") or "", 0)   -- rescue Natalya
objectives.add("c", text("9024") or "", 1)   -- find the Janus base (Secret Agent and up)
objectives.add("d", text("9025") or "", 2)   -- crack the password (00 Agent, 007)
objectives.add("e", text("9026") or "", 0)   -- escape to safety

-- ------------------------------------------------------------------ level state
local exitStarted = false     -- rule 29 has begun: no more reinforcements, no more lines
local fightStarted = false    -- rules 13-16
local fightAt = 0             -- clock() when the fight started
local standoffStarted = false -- rule 13
local countdownOut = false    -- rule 27 ran out
local natalyaShot = false     -- rule 18

local trevelyan = chr(0x45)
local ourumov = chr(0x47)
local xenia = chr(0x48)
local natalya = chr(0x46)

local function natalya_gone(): boolean
    return natalya:is_dead() or not natalya:exists()
end

local function ourumov_gone(): boolean
    return ourumov:is_dead() or not ourumov:exists()
end

-- ------------------------------------------------------------------ characters
-- Stats, bodies, heads, hearing, vision and held items of the setup guards are setup data (Characters table).
local STAND_AND_FIGHT = { 0x03, 0x04, 0x06, 0x0e, 0x11, 0x16, 0x1c, 0x27, 0x2a, 0x2f, 0x43 }
local AIM_AND_FIRE = { 0x00, 0x01, 0x02, 0x05, 0x0d, 0x0f, 0x41, 0x32, 0x37 }
local AMBUSHERS = {   -- chr -> post pad
    [0x12] = 159, [0x1e] = 159,
    [0x13] = 163, [0x1b] = 163, [0x1d] = 163,
    [0x1f] = 169,
    [0x20] = 136, [0x24] = 136,
    [0x22] = 127, [0x2b] = 127, [0x2d] = 127,
    [0x26] = 140, [0x2e] = 140,
}

for _, n in STAND_AND_FIGHT do
    local g = chr(n)
    if g:exists() then
        tg.stand_and_fight(g)   -- rule 1
    end
end
for _, n in AIM_AND_FIRE do
    local g = chr(n)
    if g:exists() then
        tg.aim_and_fire(g)      -- rule 2
    end
end
for n, post in AMBUSHERS do
    local g = chr(n)
    if g:exists() then
        tg.ambusher(g, post)    -- rule 3 (then rule 4)
    end
end

do
    local g = chr(0x33)
    if g:exists() and guard_ai.mode(g) == nil then
        guard_ai.start(g, "simple_deaf")   -- Characters: simple guard deaf generic guard
    end
end

-- rule 5: the heavy guards idle until objective a is complete, then hunt (rule 4).
spawn(function()
    while not objectives.is_complete("a") do
        wait(event("objective"))
    end
    for _, n in { 0x42, 0x44 } do
        local g = chr(n)
        if g:exists() then
            tg.hunter(g)
        end
    end
end)

-- The four of the rear carriage: no auto-aim (Characters table).
for _, g in { trevelyan, ourumov, xenia, natalya } do
    if g:exists() then
        g:set_flag("no_autoaim", true)
    end
end
natalya:surrender()   -- a hostage with hands up

-- ------------------------------------------------------------------ objective a: the brake units
local BRAKES = { 8, 9, 10, 11, 12, 13 }
spawn(function()
    while true do
        local all = true
        for _, t in BRAKES do
            if not tag(t):is_destroyed() then
                all = false
            end
        end
        if all then
            objectives.complete("a")
            return
        end
        wait(event("destroyed"))
    end
end)

-- ------------------------------------------------------------------ reinforcements (rules 6, 7)
local function pick_pad(list: { { any } }): number
    return list[tg.roll(list)][2]
end

-- One spawned guard (body 4, random head, never forced into view); nil when the spawn failed (not retried).
local function reinforcement(p: number): Target?
    if exitStarted then
        return nil
    end
    return spawn_guard({ pad = p, body = 4 })
end

-- rule 6
spawn(function()
    while player_near_pad(175, 1000) == nil do
        wait(STEP)
    end
    local pads = {
        pick_pad({ { 39, 10 }, { 39, 12 }, { 22, 13 } }),
        pick_pad({ { 39, 9 }, { 61, 11 } }),
    }
    for _, p in pads do
        local g = reinforcement(p)
        if g then
            g:set_stats({ health = 8.0, max_health = 8.0 })
            g:give_hat(220)
            g:give_weapon(10)   -- MP5K
            if L.chance(22) then
                spawn(function()
                    g:run_to_player("run")
                    L.wait_done(g)
                    if not L.dying_or_dead(g) then
                        guard_ai.start(g, "standard")
                    end
                end)
            else
                tg.stand_and_fight(g)   -- rule 1
            end
        end
    end

    -- rule 7: after that, a player enters the room of pad 144
    while not in_room(144) do
        wait(STEP)
    end
    local function random_loadout(g: Target)
        local i = tg.roll({ { 20 }, { 20 }, { 61 } })
        if i == 1 then
            g:set_stats({ health = 4.0, max_health = 4.0, armour = 2.0 })
            g:give_weapon(10)
            g:give_hat(220)
        elseif i == 2 then
            g:set_stats({ health = 4.0, max_health = 4.0, armour = 4.0 })
            g:give_weapon(9)
            g:give_weapon(9, { left_hand = true })
            g:give_hat(222)
        else
            g:set_stats({ health = 6.0, max_health = 6.0, armour = 4.0 })
            g:give_weapon(9)
            g:give_weapon(9, { left_hand = true })
            g:give_hat(221)
        end
    end
    for _, e in { { 65, 67 }, { 58, 120 }, { 59, 120 }, { 53, 120 } } do
        local g = reinforcement(e[1])
        if g then
            random_loadout(g)
            tg.ambusher(g, e[2])   -- rule 3
        end
    end
    local groups = {
        { { 1, 54 }, { 1, 61 }, { 1, 66 } },
        { { 1, 55 }, { 1, 62 }, { 1, 64 } },
        { { 31, 56 }, { 31, 63 }, { 38, 52 } },
        { { 47, 57 }, { 53, 60 } },
    }
    for _, grp in groups do
        local g = reinforcement(pick_pad(grp))
        if g then
            if L.chance(53) then
                g:set_stats({ health = 8.0, max_health = 8.0 })
                g:give_weapon(9)
                g:give_weapon(9, { left_hand = true })
                g:give_hat(222)
            else
                g:set_stats({ health = 6.0, max_health = 6.0 })
                g:give_weapon(10)
                g:give_hat(220)
            end
            tg.stand_and_fight(g)   -- rule 1
        end
    end
end)

-- ------------------------------------------------------------------ the train (rules 8-11)
local trainRunning = true

-- rule 8: the clatter every second, with the braking sound once objective a is complete.
spawn(function()
    while trainRunning do
        sound.play(0x40)
        if objectives.is_complete("a") then
            sound.play(0x3F)
        end
        wait(1)
    end
end)

local function creak()
    local creaks = { { 39, 0xF9 }, { 30, 0xDF }, { 31, 0x33 } }
    sound.play(creaks[tg.roll(creaks)][2])
end

-- rule 9: a creak every 2 to 3 s. rule 10: once objective a completes, two more creaks about 2 s apart, then the
-- train stops (sound 0x3D) and the clatter, braking and creaks end.
spawn(function()
    while not objectives.is_complete("a") do
        wait(2 + math.random())
        if not objectives.is_complete("a") then
            creak()
        end
    end
    for _, pct in { 84, 68 } do
        wait(2)
        if math.random() * 100 < pct then   -- rule 10: 84%, then 68%
            creak()
        end
    end
    trainRunning = false
    sound.play(0x3D)
end)

-- rule 11: the carriage doors tag 30..40 lose the lock bit no key provides, for the whole level.
for t = 30, 40 do
    tag(t):unlock(true)
end

-- ------------------------------------------------------------------ floor charges: indestructible until set off
local FLOOR_CHARGES = { 15, 16, 17, 18, 19, 20, 21, 43, 44, 45 }
local EXIT_CHARGES = { 25, 26, 27, 28 }
for _, t in FLOOR_CHARGES do
    tag(t):set_invincible(true)
end
for _, t in EXIT_CHARGES do
    tag(t):set_invincible(true)
end
local function blow(t: number)
    tag(t):set_invincible(false)
    tag(t):destroy()
end

-- ------------------------------------------------------------------ the rear carriage standoff (rules 12-19)
local function start_fight(shot: boolean)
    if fightStarted then
        return
    end
    fightStarted = true
    fightAt = clock()
    -- rule 15: a shot at the three starts the fight with Trevelyan's taunt and one shot at the player
    if shot and not trevelyan:is_dead() then
        L.top("900D")
        spawn(function()
            trevelyan:fire_at(nil, { stance = "stand" })
            L.wait_done(trevelyan)
            if not L.dying_or_dead(trevelyan) then
                trevelyan:halt()
            end
        end)
    end
end

-- Waits until the fight starts.
local function wait_fight()
    while not fightStarted do
        wait(STEP)
    end
end

-- rule 12: Trevelyan sees a player, or a player enters the room of pad 94.
spawn(function()
    while not (trevelyan:line_to_player() ~= nil or trevelyan:sees_player() or in_room(94)) do
        if fightStarted then
            break
        end
        wait(STEP)
    end
    if not fightStarted then
        L.top("9010")
    end
    music.play_extra(0, { total = 255 })

    -- rule 13: then a player enters the room of pad 83: the standoff, its lines, and the fight at 30 s.
    -- All three times count from the start of the standoff (one clock).
    while not in_room(83) and not fightStarted do
        wait(STEP)
    end
    if fightStarted then
        return
    end
    standoffStarted = true
    local t0 = clock()
    L.top("9009")
    local LINES: { { any } } = { { 10, "900C" }, { 20, "900A" }, { 30, "9019" } }
    for _, line in LINES do
        while clock() < t0 + line[1] and not fightStarted do
            wait(STEP)
        end
        if fightStarted then
            return
        end
        L.top(line[2])
    end
    start_fight(false)
end)

-- rule 14: during the standoff, near pad 82 or backing out into the room of pad 88.
spawn(function()
    while not standoffStarted do
        if fightStarted then
            return
        end
        wait(STEP)
    end
    while not fightStarted do
        if player_near_pad(82, 250) ~= nil then
            L.top("900F")
            start_fight(false)
        elseif in_room(88) then
            L.top("9011")
            start_fight(false)
        end
        wait(STEP)
    end
end)

-- rule 15: a player's shot that misses or wounds Trevelyan, Xenia or Ourumov, before or during the standoff
-- (the guard events, once each).
do
    local conns: { Connection } = {}
    local function shot()
        for _, c in conns do
            c:off()
        end
        start_fight(true)
    end
    for _, n in { 0x45, 0x48, 0x47 } do
        table.insert(conns, on("guard_near_miss", shot, { chr = n }))
        table.insert(conns, on("guard_damaged", shot, { chr = n }))
    end
end

-- rule 16: the carriage doors close and Trevelyan and Xenia escape.
spawn(function()
    wait_fight()
    local t = clock() + 1.25
    while clock() < t do
        if player_near_pad(82, 200) ~= nil or (standoffStarted and in_room(88)) then
            break
        end
        wait(0.1)
    end
    tag(0):close()
    tag(1):close()
    spawn(function()
        wait(3)
        for _, d in { 2, 3, 4, 5, 6, 7, 46 } do
            tag(d):close()
        end
    end)
    while tag(1):state() ~= "closed" do
        wait(STEP)
    end
    for _, g in { trevelyan, xenia } do
        if g:exists() then
            guard_ai.stop(g)
            g:set_invincible(true)
            g:set_hidden(true)
            g:teleport(186)
        end
    end
end)

-- rule 17: Ourumov keeps Natalya covered; at the fight he fires at her once (rule 18), then covers her again;
-- once she is dead or gone he runs to the player and becomes a standard guard.
spawn(function()
    if not ourumov:exists() then
        return
    end
    ourumov:aim_at(natalya)
    wait_fight()
    if ourumov_gone() then
        return
    end
    if not natalya_gone() then
        local fired = false
        local conn = on("gunfire", function()
            fired = true
        end, { prop = ourumov.prop })
        ourumov:fire_at(natalya)
        while not fired and not ourumov_gone() and not natalya_gone() do
            wait(0.1)
        end
        conn:off()
        -- rule 18: his shot hits her: a TT-33 hit to body part 8, objective b fails, she does nothing more
        if fired and not natalya_gone() then
            natalyaShot = true
            natalya:hit(8, 6)
            objectives.fail("b")
        end
        if not ourumov_gone() then
            ourumov:aim_at(natalya)
        end
    end
    while not natalya_gone() and not ourumov_gone() do
        wait(STEP)
    end
    if not ourumov_gone() then
        ourumov:run_to_player("run")
        L.wait_done(ourumov)
        if not ourumov_gone() then
            guard_ai.start(ourumov, "standard")
        end
    end
end)

-- rule 19: Xenia waits; at the fight she fires once at the player; dying, her line.
spawn(function()
    if not xenia:exists() then
        return
    end
    wait_fight()
    if not L.dying_or_dead(xenia) then
        xenia:fire_at(nil, { stance = "stand" })
    end
end)
spawn(function()
    while xenia:exists() and not xenia:is_dying() and not xenia:is_dead() do
        wait(STEP)
    end
    if xenia:is_dying() then
        L.top("900E")
    end
end)

-- ------------------------------------------------------------------ Natalya at Boris's computer (rules 20-26)
local natState = "hostage"   -- "hostage" | "free" | "working" | "stopped" | "following"

-- rule 26: Natalya dead and gone: the line at the top, objective b fails.
spawn(function()
    while not natalya_gone() do
        wait(STEP)
    end
    L.top("901C")
    if not objectives.is_failed("b") then
        objectives.fail("b")
    end
end)

-- rules 20-23
spawn(function()
    wait_fight()
    -- rule 20: Ourumov dead or gone and Natalya not shot: hands down, she looks around
    while not ourumov_gone() do
        if natalyaShot or natalya_gone() then
            return
        end
        wait(STEP)
    end
    if natalyaShot or natalya_gone() then
        return
    end
    natState = "free"
    natalya:stand()
    natalya:look_around()

    -- rule 21: at 20 s she walks to pad 81, moved there at once whenever no player sees her
    while clock() < fightAt + 20 do
        wait(STEP)
        if natalya_gone() or natState ~= "free" then
            return
        end
    end
    natState = "working"
    natalya:run_to(81, "walk")
    while natState == "working" and natalya:is_moving() and not natalya_gone() do
        if not natalya:on_screen() then
            natalya:teleport(81)
            break
        end
        wait(STEP)
    end
    if natState ~= "working" or natalya_gone() then
        return
    end
    natalya:face(tag(41))

    -- rule 23: her reports while she works (times from the start of the fight; a time passed before she got to
    -- work is said as soon as she works)
    spawn(function()
        local lines: { { any } } = { { 40, "9014", nil }, { 50, "9015", "c" } }
        if diff >= 2 then
            table.insert(lines, { 65, "9016", nil })
            table.insert(lines, { 75, "9017", "d" })
        end
        for _, line in lines do
            while clock() < fightAt + line[1] do
                if natState ~= "working" or natalya_gone() then
                    return
                end
                wait(STEP)
            end
            if natState ~= "working" or natalya_gone() then
                return
            end
            L.top(line[2])
            if line[3] == "c" and diff >= 1 then
                objectives.complete("c")
            elseif line[3] == "d" then
                objectives.complete("d")
            end
        end
    end)

    -- rule 22: she types; 1 s into each animation a keyboard sound from tag 41 with 50% chance
    local TYPING = { { 39, 172, 69 }, { 20, 173, 74 }, { 20, 174, 79 }, { 22, 175, 89 } }
    while natState == "working" and not natalya_gone() do
        local e = TYPING[tg.roll(TYPING)]
        natalya:anim(e[2], { from = 0, to = e[3] })
        spawn(function()
            wait(1)
            if natState == "working" and L.chance(50) then
                sound.play(0xB8, { from = tag(41) })
            end
        end)
        wait(STEP)
        while natState == "working" and not natalya:anim_done() and not natalya_gone() do
            wait(STEP)
        end
    end
end)

-- rule 24: the computer is destroyed while she works.
spawn(function()
    while not (tag(41):is_destroyed() or tag(42):is_destroyed()) do
        wait(event("destroyed"))
    end
    if natState ~= "working" or natalya_gone() then
        return
    end
    natState = "stopped"
    natalya:stop_anim()
    natalya:halt()
    natalya:face_player()
    if not countdownOut then
        L.top("901A")
        wait(5)
        if not natalya_gone() then
            L.top("901B")
        end
    end
end)

-- rule 25: a player enters the room of pad 98 while she works (or after rule 24): she follows.
spawn(function()
    while true do
        if natalya_gone() then
            return
        end
        if (natState == "working" or natState == "stopped") and in_room(98) then
            break
        end
        wait(STEP)
    end
    natState = "following"
    natalya:stop_anim()
    L.top("9018")
    local running = false
    while not natalya_gone() do
        local d = natalya:distance()
        if running then
            if d <= 250 then
                natalya:halt()
                running = false
            else
                natalya:run_to_player("run")
            end
        elseif d > 400 then
            running = true
            natalya:run_to_player("run")
        elseif not natalya:is_busy() then
            if d <= 80 and math.abs(natalya:angle_to_player()) < 45 then
                natalya:raise_arms()
            else
                natalya:look_around()
            end
        end
        wait(0.5)
    end
end)

-- ------------------------------------------------------------------ the floor countdown (rule 27)
spawn(function()
    wait_fight()
    local delay = if xenia:is_dead() and not natalya_gone() then 30 else 15
    wait(delay)
    if exitStarted then
        return
    end
    L.top("9012")
    wait(5)
    if exitStarted then
        return
    end
    L.top("9013")
    countdown.set(60)
    countdown.show(true)
    countdown.start()
    local hum: number? = nil
    local stage = 0
    while countdown.left() > 0 do
        local left = countdown.left()
        if stage == 0 and left <= 30 then
            stage = 1
            sound.play(0xD6, { from = pad(71) })
            hum = sound.play(0xD8, { from = pad(71) })
        elseif stage == 1 and left <= 23 then
            stage = 2
            if hum then
                sound.stop(hum)
            end
            sound.play(0xD7, { from = pad(71) })
        elseif stage == 2 and left <= 20 then
            stage = 3
            local ch = sound.play(0x3A)
            sound.volume(ch, 0, 60)
        end
        wait(STEP)
    end
    countdownOut = true
    countdown.stop()
    countdown.show(false)
    -- the floor explodes, charge by charge
    local schedule: { { any } } = {
        { 0.5, { 15 } }, { 1.5, { 16, 43 } }, { 3.5, { 17 } }, { 7.5, { 18, 44 } },
        { 9.5, { 19 } }, { 10.5, { 20 } }, { 14.5, { 21, 45 } },
    }
    local t0 = clock()
    for _, s in schedule do
        while clock() < t0 + s[1] do
            wait(0.1)
        end
        for _, t in s[2] do
            blow(t)
        end
    end
end)

-- ------------------------------------------------------------------ rule 28: the fog and objective e
spawn(function()
    while not in_room(98) do
        wait(STEP)
    end
    fog.next()
    objectives.complete("e")
end)

-- ------------------------------------------------------------------ the outro (Cinematics: Outro)
local function outro(p: number)
    -- 1. behind the black screen: all hidden, weapons away, a button press ends the level, camera tag 29
    cinema.hide_all_chrs(true)
    for _, n in L.players() do
        player(n):hide_weapon(true)
    end
    camera.fixed(tag(29))
    -- 2. Natalya on pad 204 (animation 90 from frame 7); Bond's model on pad 205 (animation 90)
    natalya:set_hidden(false)
    natalya:teleport(204)
    natalya:anim(90, { from = 7 })
    local body = bond_body(p)
    body:show(true)
    body:set_invincible(true)
    body:teleport(205)
    body:anim(90)
    -- 3. fade in; the exit charges explode
    screen.fade_in()
    local t0 = clock()
    local charges = { { 1, 25 }, { 1.5, 26 }, { 3, 27 }, { 4, 28 } }
    local i = 1
    while clock() < t0 + 5 do
        if cinema.skipped() then
            break
        end
        local c = charges[i]
        if c and clock() >= t0 + c[1] then
            blow(c[2])
            i += 1
        end
        wait(0.1)
    end
    -- 4. at 5 s (or on a button press) fade to black; the level ends as a win
    L.fade_out(nil, { settle = 0, poll = 0.1 })
    mission.win()
end

-- ------------------------------------------------------------------ rule 29: the escape hatch
spawn(function()
    local p: number? = nil
    while p == nil do
        p = player_near_pad(203, 500)
        if p == nil then
            wait(STEP)
        end
    end
    local who = p :: number
    wait(3)
    if not player_alive(who) then
        -- rule 29: dead during the 3 s: nothing happens and the hatch never triggers again
        return
    end
    exitStarted = true
    -- every player: invulnerable, no pickups, no HUD, no control (co-op notes); the clock stop is the scene's
    -- TODO(api): cinema.begin does not declare that the mission clock stops
    cinema.begin()
    if not natalya_gone() and not objectives.is_failed("b") then
        objectives.complete("b")
    end
    L.fade_out(nil, { settle = 0, poll = 0.1 })
    if natalya_gone() then
        mission.fail()
    elseif not objectives.all_complete() then
        mission.fail()
    else
        outro(who)
    end
end)

-- ------------------------------------------------------------------ the intro (Cinematics: Intro)
-- Played by the shared intro module ("level_intro": false in mission.json) from train_intro_data: the fixed
-- camera with 902C, the swirl, the body's animation 100. The start pad 186, PP7 (item 4) with 100 rounds, watch
-- laser (item 23) with 300, cuff 0 and watch 10:17 stay the setup's own.
require("intro").play(require("train_intro_data"))
