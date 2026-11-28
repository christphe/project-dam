-- cradle: the whole Cradle level (specs/cradle.md).
-- Covers: the respawning bodyguards (rules 1-3), Trevelyan at his console and around the cradle (rules 4-7,
-- 12-13), the console countdown (rules 8-10), the final phase and the last fight (rules 14-19), the ending and
-- Bond's fatal fall (rules 20-22, scenes B, C, D), the helicopter at rest, the intro (Cinematics: staged here,
-- mission.json has "level_intro": false). The spot director (rule 11) is in cradle_director.luau. Bond's start
-- items are setup data.

local guard_ai = require("guard_ai")
local L = require("campaign_lib")
guard_ai.boot()

local director_mod = require("cradle_director")

-- Objectives (declared first: the tasks below complete and fail them)
objectives.add("a", text("241F") or "", 0)
objectives.add("b", text("2420") or "", 0)

local STEP = 0.25

-- ------------------------------------------------------------------ helpers


local function room_player(pads: { number }): number?
    for _, p in pads do
        local n = player_in_room(p)
        if n ~= nil then return n end
    end
    return nil
end


-- ------------------------------------------------------------------ bodyguards (rules 1-3)

local BODYGUARD_PADS = {
    [0x01] = { 131, 79 }, [0x02] = { 138, 8 }, [0x03] = { 18, 92 }, [0x04] = { 62, 0 }, [0x05] = { 110, 125 },
}
local bodyguard: { [number]: Target? } = {}

-- rule 2: one of three kits
local function kit(g: Target)
    local r = math.random() * 100
    if r < 32 then
        g:set_stats({ max_health = 4.0, health = 4.0, armour = 2.0, speed = 50, accuracy = 50,
            grenade_chance = L.grenade_pct(191) })
        g:give_weapon(0x09, { model = 0xC3 })
        g:give_hat(0xDC)
    elseif r < 63 then
        g:set_stats({ max_health = 4.0, health = 4.0, armour = 2.0, speed = 60, accuracy = 60 })
        g:give_weapon(0x09, { model = 0xC3 })
        g:give_weapon(0x09, { model = 0xC3, left_hand = true })
        g:give_hat(0xDE)
    else
        g:set_stats({ max_health = 4.0, health = 4.0, armour = 2.0, speed = 80, accuracy = 80 })
        g:give_weapon(0x09, { model = 0xC3 })
        g:give_weapon(0x09, { model = 0xC3, left_hand = true })
        g:give_hat(0xDD)
    end
end

-- rule 3: the level's own aggressive bodyguard
local function attack_move(g: Target)
    local r = math.random() * 100
    if r < 4 then
        if not g:throw_grenade() then g:combat_move("sidestep") end
    elseif r < 20 then
        g:combat_move("sidestep")
    elseif r < 40 then
        g:combat_move("walk_fire")
    elseif r < 60 then
        g:combat_move("run_fire")
    elseif r < 80 then
        g:fire_at(nil, { stance = "stand" })
    else
        g:fire_at(nil, { stance = "kneel" })
    end
end

local function bodyguard_task(g: Target)
    guard_ai.stop(g)
    kit(g)                                                 -- rule 2
    while g:exists() do
        if g:distance() <= 4500 and g:sees_player() then
            attack_move(g)                                 -- rule 3: attacks again while it still sees him
            L.wait_idle(g, nil, { settle = true })
        else
            if not g:is_moving() and not g:is_busy() then
                g:run_to_player("run")                     -- rule 3: runs towards the player
            end
            wait(STEP)
        end
    end
end

-- rule 1: at the start and every 12 s, an empty slot gets a new bodyguard
local function respawn(n: number)
    local pads = BODYGUARD_PADS[n]
    local first = if L.chance(50) then 1 else 2
    local g = spawn_guard({ chr = n, pad = pads[first], body = 4, sunglasses = true })   -- head: random
    if g == nil then
        g = spawn_guard({ chr = n, pad = pads[3 - first], body = 4, sunglasses = true })
    end
    if g then
        local ng: Target = g
        bodyguard[n] = ng
        spawn(function() bodyguard_task(ng) end)
    end
end

for n, _ in BODYGUARD_PADS do
    local g: Target = chr(n)
    if g:exists() then
        bodyguard[n] = g
        spawn(function() bodyguard_task(g) end)            -- the setup's own bodyguards: rules 2-3 too
    end
end
spawn(function()
    while true do
        if not L.any_in_cutscene() then
            for n, _ in BODYGUARD_PADS do
                if L.departed(bodyguard[n]) then respawn(n) end
            end
        end
        wait(12)
    end
end)

-- ------------------------------------------------------------------ the console countdown (rules 8-10)

local COUNTDOWN = 180
local console_over = false                                 -- rule 9 or 10 has run

local function stop_console_effects()
    sound.stop(0)
    music.stop_extra(0)
    countdown.show(false)
    countdown.stop()
end

-- rule 10 (the objective's destroy condition is checked here: the objective is declared by this script)
on("destroyed", function(ev)
    if ev.tag ~= 0 or console_over then return end
    console_over = true
    L.top("2408")
    stop_console_effects()
    if not objectives.is_failed("a") then objectives.complete("a") end
end)

-- rule 8
local function console_sequence()
    if tag(0):is_destroyed() then return end
    L.top("2406")
    wait(5)
    if console_over then return end
    L.top("2407")
    sound.play(0xA1, { channel = 0 })
    countdown.set(COUNTDOWN)
    countdown.show(true)
    countdown.start()
    local warned = false
    while not console_over do
        local left = countdown.left()
        if not warned and left <= 15 then
            warned = true
            L.top("2409")                          -- rule 8: fifteen seconds
        end
        if left <= 0 then                                  -- rule 9
            console_over = true
            objectives.fail("a")
            L.top("240A")
            stop_console_effects()
            return
        end
        wait(STEP)
    end
end

-- ------------------------------------------------------------------ Trevelyan (rules 4-7, 11-19)

local T: Target = chr(0x00)
local director = director_mod.new()
local fell = false                                         -- rule 19: he fell off the platform
local final_phase = false

-- rule 12: eight taunts, each once, in order
local TAUNTS = { "240D", "240E", "240F", "2410", "2411", "2412", "2413", "2414" }
local next_taunt = 1
local function taunt()
    if next_taunt <= #TAUNTS then
        L.top(TAUNTS[next_taunt])
        next_taunt += 1
    end
end

local invulnerable = true
local function set_invulnerable(on: boolean)
    invulnerable = on
    T:set_invincible(on)
end

-- rule 13: the doors open as he approaches
local DOORS = { { pad = 10001, range = 600, tag = 6 }, { pad = 10002, range = 600, tag = 7 },
    { pad = 10003, range = 600, tag = 8 }, { pad = 10004, range = 600, tag = 9 },
    { pad = 10005, range = 200, tag = 10 } }
spawn(function()
    while T:exists() do
        for _, d in DOORS do
            if T:distance_to(pad(d.pad)) <= d.range then
                local st = tag(d.tag):state()
                if st ~= "open" and st ~= "opening" then tag(d.tag):open() end
            end
        end
        wait(STEP)
    end
end)

-- rule 11: the director reacts to players near the trigger pads (until the final phase)
spawn(function()
    while T:exists() and not final_phase do
        director.poll()
        wait(STEP)
    end
end)

-- rule 19: the last fight
local lines_done = false
local function final_lines()                               -- rule 19: his fall or death, the last lines (once)
    if lines_done then return end
    lines_done = true
    L.top("2417")
    wait(2)
    L.top("2418")
end

local function last_fight()
    set_invulnerable(false)
    T:set_body_flags({ knockable = true, hitbox_cull = true })   -- knocked off by any hit, culled by his hit box
    on("guard_died", function() final_lines() end, { chr = 0x00 })
    spawn(function()
        while T:exists() do
            if T:in_room_of(148) then                      -- he fell off the platform
                fell = true
                -- TODO(api): "his body ignores animation translation": no body flag says it on a guard (only
                -- anim(..., { move = false }) for one animation).
                T:fade_out()
                final_lines()
                break
            end
            wait(STEP)
        end
    end)
    while T:exists() and not fell do
        if T:sees_player() then
            local r = math.random() * 100
            if r < 25 then
                T:combat_move("walk_fire")
            elseif r < 62 then
                T:fire_at(nil, { stance = "stand" })
            else
                T:fire_at(nil, { stance = "kneel" })
            end
            L.wait_idle(T, nil, { settle = true })
        else
            if not T:is_moving() and not T:is_busy() then T:run_to_player("run") end
            wait(STEP)
        end
    end
end

-- rule 18
local function wait_at_118()
    T:run_to(118, "run")
    wait(STEP)
    while T:exists() and T:is_moving() do wait(STEP) end
    while T:exists() do
        local ready = false
        for _, p in L.players() do
            if player(p):in_room_of(116) and player(p):distance_to(pad(116)) <= 700 then ready = true end
        end
        if ready then break end
        wait(STEP)
    end
    last_fight()                                           -- rule 19
end

-- rule 17
local function walk_to_116()
    T:run_to(116, "walk")
    wait(STEP)
    while T:exists() and T:is_moving() do wait(STEP) end
    wait_at_118()                                          -- rule 18
end

-- rule 14
local function start_final_phase()
    final_phase = true
    set_invulnerable(true)
    L.top("2415")
    -- TODO(api): "up to 255 s": stop_after is read as the play length.
    music.play_extra(0, { stop_after = 255 })
    T:set_stats({ speed = 100, accuracy = 100, flinch = false })
    T:run_to(150, "sprint")
    wait(STEP)
    while T:exists() and T:is_moving() do wait(STEP) end
    -- rule 15
    if room_player({ 115, 116, 148, 109 }) ~= nil or not T:teleport(150) then
        walk_to_116()                                      -- rule 17
        return
    end
    -- rule 16: waits on pad 150
    T:anim(0x6A, { loop = true })
    local misses = T:near_misses()
    while T:exists() and not T:sees_player() and T:near_misses() == misses do wait(STEP) end
    L.top("2416")
    T:anim(0x6B, { blend = 0.27 })
    wait(2)
    if player_in_room(116) ~= nil then
        wait_at_118()                                      -- rule 18
    else
        walk_to_116()                                      -- rule 17
    end
end

-- rule 7: running to a spot; returns true when the final phase took over
local function run_to_spot(): boolean
    if T:health() <= 9.0 then
        start_final_phase()                                -- rule 14
        return true
    end
    local pick = director.take() or director.ask()        -- rule 6: asks when none was assigned
    local dest = pick.pad
    local function reissue()
        local newer = director.take()                      -- rule 11: a newer assignment replaces the destination
        if newer then dest = newer.pad end
        T:run_to(dest, "sprint")
    end
    T:run_to(dest, "sprint")
    local t, grenade_done, mock_done = 0, false, false
    wait(STEP)
    t += STEP
    while T:exists() do
        if T:was_damaged() then                            -- damaged on the run
            set_invulnerable(true)
            taunt()                                        -- rule 12
            reissue()
        end
        if not grenade_done and t >= 1 then
            grenade_done = true
            if L.chance(32) then
                -- rule 7: a live grenade (the spec's item 0x1A, model 0xC4: drop_grenade() takes no item, it drops
                -- the game's live grenade) at his feet, which is behind him as he runs
                T:drop_grenade()
            end
        end
        if not mock_done and t >= 10 then
            mock_done = true
            local p = L.nearest_player(T)
            if p ~= nil and T:sees_player() and L.health(p) < 0.2 then
                L.top("2419")
                reissue()
            end
        end
        if T:distance_to(pad(dest)) <= 100 or not T:is_moving() then break end
        wait(STEP)
        t += STEP
    end
    return false                                           -- he fights from the spot (rule 6)
end

-- rule 6: fighting from a spot, until he relocates
local function fight_from_spot()
    director.arrived()
    set_invulnerable(false)
    T:was_damaged()                                        -- clear the damage flag
    local relocate = false
    local hits = T:hits()
    local misses = T:near_misses()
    local missed_since_burst = false
    while T:exists() do
        if not invulnerable and T:was_damaged() then
            set_invulnerable(true)                         -- until his next spot
            taunt()                                        -- rule 12
            hits = T:hits()
            misses = T:near_misses()
            if L.chance(24) then relocate = true end
        end
        if invulnerable then
            if T:hits() > hits then relocate = true end   -- a hit while invulnerable
            if T:near_misses() > misses then
                misses = T:near_misses()
                missed_since_burst = true
            end
        end
        if T:distance() < 800 then relocate = true end    -- the player too close
        if relocate then break end
        if T:sees_player() and not T:is_busy() then
            -- a burst; invulnerable: 20% a grenade, and a near miss relocates on a 24% roll per burst
            if invulnerable and missed_since_burst then
                missed_since_burst = false
                if L.chance(24) then break end
            end
            if invulnerable and L.chance(20) and T:throw_grenade() then
                -- the grenade is this burst
            else
                T:fire_at(nil)
            end
        elseif not T:is_busy() then
            T:fire_at(nil, { aim_only = true })            -- aims at the player
        end
        wait(STEP)
    end
end

-- rules 4-5
local function trevelyan()
    guard_ai.stop(T)
    T:set_stats({ max_health = 17.0, health = 17.0, armour = 2.0, speed = 100, accuracy = 100, flinch = false,
        grenade_chance = L.grenade_pct(255) })
    T:set_flag("no_autoaim", true)
    set_invulnerable(true)                                 -- rule 4
    T:run_to(144, "sprint")
    wait(STEP)
    while T:exists() do
        local d = T:distance_to(pad(144))
        if d <= 100 or (not T:is_moving() and d <= 300) then break end
        if not T:is_moving() then T:run_to(144, "sprint") end
        wait(STEP)
    end
    if not T:exists() then return end
    spawn(console_sequence)                                -- rule 5 / rule 8
    T:set_flag("fast_sprint", true)                        -- rule 5
    while T:exists() do
        fight_from_spot()                                  -- rule 6
        if not T:exists() or run_to_spot() then break end  -- rule 7 (rule 14 ends the loop)
    end
end

if T:exists() then spawn(trevelyan) end

-- objective b: Trevelyan no longer exists
spawn(function()
    while not L.departed(T) do wait(STEP) end
    objectives.complete("b")
end)

-- ------------------------------------------------------------------ helicopter (Objects and world)

local HELI = tag(5)
HELI:set_hidden(true)                                      -- the setup's flag: not drawn and silent all level
HELI:set_rotor(620)
HELI:anim(0, { from = 0, to = 1, hold = true })            -- flight animation 0 held at its first two frames

-- ------------------------------------------------------------------ the intro (Cinematics)
-- Played by the shared intro module from cradle_intro_data (one of the two fixed cameras with 2422, the swirl,
-- the body's animation 0x63), alongside the rest of the level as before.
spawn(function()
    require("intro").play(require("cradle_intro_data"))
end)

-- ------------------------------------------------------------------ cinematics (scenes B, C, D)



-- invulnerable, no pickups, HUD hidden, controls locked, unarmed
local function scene_state(p: number)
    cinema.begin()
    -- (cinema.begin() also stops the mission clock)
    player(p):force_weapon(0)                              -- weapon put away (0 = unarmed)
end

-- scene B / D step 1-2: a body falls from pad 146 under the cradle, seen from tags 4 then 3
local function fall_shots(who: Target, over: () -> boolean)
    for i, shot in { { cam = 4, drop = 1.0 }, { cam = 3, drop = 1.5 } } do
        if over() then return end
        if i > 1 then camera.fixed(tag(shot.cam)) end
        who:teleport(146)
        who:anim(0xB4)
        who:teleport(148)
        who:set_shadow(false)
        screen.fade_in()
        wait(shot.drop)                                    -- drops under gravity (native)
        L.fade_out(nil, { settle = 0 })
    end
end

-- Scene C: the final scene
local function scene_c(p: number, direct: boolean)
    if direct then
        L.fade_out(nil, { settle = 0 })
        scene_state(p)
    end
    cinema.hide_all_chrs(true)
    L.end_on_button(false)
    player(p):hide_weapon(true)
    local body = bond_body(p)
    camera.fixed(tag(1), { look_at = body })
    body:show(true)
    -- step 1
    body:teleport(146)
    body:hold_item(0, "right")                             -- both hands emptied (item 0)
    body:hold_item(0, "left")
    body:set_shadow(false)
    body:set_gravity(false)                                -- held in the air
    screen.fade_in()
    body:anim(0xB3, { from = 2 })
    -- Objects and world: the ending clears the helicopter's setup flag (hidden and silent all level), so it
    -- appears and is heard
    HELI:set_hidden(false)
    HELI:anim(0, { from = 2 })
    wait(4.8)
    if L.ending() then return end
    L.fade_out(nil, { settle = 0 })
    -- step 2
    camera.fixed(tag(2), { look_at = body })
    body:teleport(146)
    screen.fade_in()
    body:anim(0xB3, { from = 2 })
    HELI:move_to(147)
    HELI:anim(0, { from = 2 })
    wait(4.8)
    L.end_level(false)                                       -- step 3
end

-- Scene B: Trevelyan's fall (he was killed on the cradle)
local function scene_b(p: number)
    L.fade_out(nil, { settle = 0 })
    scene_state(p)
    local tb: Target? = T
    if tb == nil or tb:pos() == nil then
        tb = spawn_guard({ chr = 0x00, pad = 146, body = 8, force = true })
    end
    if tb == nil then
        scene_c(p, false)
        return
    end
    local trev = tb :: Target
    cinema.hide_all_chrs(true)
    L.end_on_button(false)
    player(p):hide_weapon(true)
    camera.fixed(tag(4))
    trev:set_hidden(false)                                 -- Trevelyan is shown
    local body = bond_body(p)
    if player(p):in_room_of(116) then
        body:show(true)
        body:teleport(149)
    else
        body:show(false)
    end
    fall_shots(trev, L.ending)                             -- steps 1-2
    if not L.ending() then scene_c(p, false) end               -- step 3
end

-- Scene D: Bond's fatal fall (rule 21)
local function scene_d(p: number)
    scene_state(p)
    L.fade_out(nil, { settle = 0 })
    cinema.hide_all_chrs(true)
    -- the mission fails as killed in action; mission.killed_in_action() also ends the level, so it is called when
    -- the scene ends (or a button press cuts it short)
    local over = false
    local function end_kia()
        if over then return end
        over = true
        L.fade_out(nil, { settle = 0 })
        mission.killed_in_action()
    end
    spawn(function()
        while not over do
            if cinema.skipped() then
                end_kia()
                return
            end
            wait(STEP)
        end
    end)
    player(p):hide_weapon(true)
    camera.fixed(tag(4))
    if T:exists() and T:in_room_of(116) then
        T:set_hidden(false)
        T:teleport(149)
        T:halt()
    end
    local body = bond_body(p)
    body:show(true)
    fall_shots(body, function() return over end)
    end_kia()
end

-- ------------------------------------------------------------------ the ending (rules 20-22)

local scene_started = false

-- rule 20: any player on the platform with Trevelyan gone
spawn(function()
    while true do
        local p = player_in_room(146)
        if p ~= nil and L.departed(T) and not scene_started then
            wait(1)
            if scene_started then return end
            scene_started = true
            if not L.alive(p) then return end                -- rule 22
            if fell then
                scene_c(p, true)
            else
                scene_b(p)
            end
            return
        end
        wait(STEP)
    end
end)

-- rule 21: per player, the drop under the cradle
spawn(function()
    local locked: { [number]: boolean } = {}
    while not scene_started do
        for _, p in L.players() do
            local pl = player(p)
            local _, y, _ = pl:pos()
            local inside = pl:in_room_of(147)
            if inside and y ~= nil and y < -2550 and not locked[p] then
                locked[p] = true
                pl:set_frozen(true)
                hud.hide(true)                             -- the HUD is global, not per player
            elseif not inside and locked[p] then
                locked[p] = nil
                pl:set_frozen(false)
                hud.hide(false)
            end
            if inside and y ~= nil and y < -4070 and L.alive(p) then
                local others = false
                for _, q in L.players() do
                    if q ~= p and L.alive(q) then others = true end
                end
                if others then
                    pl:kill()                              -- co-op: the fall kills only this player
                    locked[p] = nil
                    pl:set_frozen(false)
                    hud.hide(false)
                else
                    scene_started = true
                    scene_d(p)                             -- rule 22: skipped once he is dead (alive() above)
                    return
                end
            end
        end
        wait(STEP)
    end
end)
