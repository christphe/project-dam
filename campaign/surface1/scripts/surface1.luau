-- surface1: Severnaya, first visit (specs/surface1.md). The dish console, the snowfield reinforcements and
-- their spawn points, the patrols, the heavy waves and the locked door alarm, the vent hatch, the exit down the
-- vent shaft and its cinematic, and the intro (staged here: mission.json "level_intro": false; the start pad,
-- inventory, outfit and watch time stay the setup's).
local guard_ai = require("guard_ai")
guard_ai.boot()
local L = require("campaign_lib")

local STEP = 0.25
local diff = mission.difficulty()

local exiting = false        -- rule 17: the exit has started (no more spawns)
local reinf_enabled = false  -- rule 4
local door_alarm_on = false  -- rule 15
local console_wrecked = false

-- ------------------------------------------------------------------ objectives

objectives.add("a", text("7C10") :: string, 0)
objectives.add("b", text("7C11") :: string, 1)
objectives.add("c", text("7C12") :: string, 1)
objectives.add("d", text("7C13") :: string, 0)

local function has_objective(min_diff: number): boolean
    return diff >= min_diff
end

-- b: the safe key (tag 0); c: the bunker plans (tag 2). Completed when picked up, failed when destroyed
-- first (whichever event comes first counts).
local function pickup_objective(id: string, t: number, min_diff: number)
    if not has_objective(min_diff) then return end
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
pickup_objective("b", 0, 1)
pickup_objective("c", 2, 1)

-- ------------------------------------------------------------------ characters

-- Hut guards: the generic standard guard (boot() starts them when the setup gives them the generic list).
for _, c in { 0x0E, 0x0F, 0x12, 0x13 } do
    local g: Target = chr(c)
    if g:exists() and guard_ai.mode(g) == nil then
        guard_ai.start(g, "standard")
    end
end

-- rules 9-11: the hunter (campaign_lib.hunt): rule 9 pursuit (sprint beyond 300, re-plan every 4 s or on
-- arrival), rule 10 one random attack within 160 with a line of sight, rule 11 removed beyond 3000 (any
-- reinforcement: snowfield, sniper, heavy; the patrols of rule 13 are not reinforcements and are never removed).
local REINF_HUNTER = { sprint_beyond = 300, attack_range = 160, remove_beyond = 3000, replan = 4 }
local PATROL_HUNTER = { sprint_beyond = 300, attack_range = 160, replan = 4 }

-- rule 12: sniper. Approach (sprint beyond 1500), within 300: hunter for good; within 1000 with a line of
-- sight: kneel and aim, fire once after 5 s of sight, then resume; losing sight resumes at once.
local function sniper(g: Target)
    local replan_at = 0
    while L.alive_guard(g) do
        if L.check_remove(g, REINF_HUNTER) then return end  -- rule 11
        local d = g:distance()
        if d <= 300 then
            L.start_hunter(g, REINF_HUNTER)
            return
        end
        if d <= 1000 and g:line_to_player() ~= nil then
            g:halt()
            g:fire_at(nil, { stance = "kneel", aim_only = true })
            local t0 = clock()
            while L.alive_guard(g) and g:line_to_player() ~= nil do
                if clock() - t0 >= 5 then
                    g:fire_at(nil, { stance = "kneel" })
                    wait(STEP)
                    L.wait_idle(g, 10, { alive = L.alive_guard })
                    break
                end
                wait(STEP)
            end
            replan_at = 0
        elseif clock() >= replan_at or not g:is_moving() then
            g:run_to_player(if d > 1500 then "sprint" else "run")
            replan_at = clock() + 4
        end
        wait(STEP)
    end
end

-- rule 7: a fresh snowfield reinforcement's weapon and behaviour.
local function arm_reinforcement(g: Target)
    g:set_stats({ grenade_chance = 75 })  -- 191/256
    local near = g:distance() <= 1000
    if near or math.random() < 0.21 then
        g:give_weapon(7, { model = 193 })
        L.start_hunter(g, REINF_HUNTER)    -- rules 9-11
    else
        g:give_weapon(17, { model = 210 })
        L.run_task(g, function() sniper(g) end) -- rule 12
    end
end

-- rule 13: the patrols walk their paths; seen within 300, a guard shot in sight, or hurt: hunter for good.
-- The witnessed hit and the damage are events; "sees the player within 300" is a state (sees_player() and the
-- distance), polled, since the spotted event fires once on the edge into seeing, maybe farther than 300.
local PATROLS = { [0x17] = 2, [0x18] = 3, [0x1A] = 5, [0x1C] = 7, [0x1E] = 9 }
for c, path in PATROLS do
    local g: Target = chr(c)
    if g:exists() then
        guard_ai.stop(g)
        L.run_task(g, function()
            g:patrol(path)
            local disturbed = false
            local subs: { Connection } = {
                on("guard_witnessed", function(ev: MissionEvent)
                    if ev.what == "hit" then disturbed = true end
                end, { chr = c }),
                on("guard_damaged", function() disturbed = true end, { chr = c }),
            }
            while L.alive_guard(g) and not disturbed and not (g:sees_player() and g:distance() <= 300) do
                wait(STEP)
            end
            for _, sub in subs do sub:off() end
            if L.alive_guard(g) then
                L.hunt(g, PATROL_HUNTER)
            end
        end)
    end
end

-- ------------------------------------------------------------------ communications dish (objective a)

-- rule 3: the drone gun (tag 6) is active from the start; only rules 1 and 2 change it.
local function drone_gun(on: boolean)
    tag(6):set_active(on)
end

-- rule 1: the console used while intact.
spawn(function()
    wait(event("activated", { tag = 1 }))
    if console_wrecked or tag(1):is_destroyed() then return end
    sound.play(0x0055, { from = tag(1) })
    hud.top(text("7C08") :: string)
    objectives.complete("a")
    tag(1):show_screen(51)
    drone_gun(false)
end)

local heavy_wave: () -> ()  -- rule 14, below

-- rule 2: the console destroyed (once, before or after rule 1).
spawn(function()
    wait(event("destroyed", { tag = 1 }))
    console_wrecked = true
    wait(3)
    sound.play(0x00C6, { from = tag(1) })
    hud.top(text("7C09") :: string)
    objectives.fail("a")
    drone_gun(true)
    alarm.trigger()
    heavy_wave()  -- rule 14: the console's destruction sends a heavy wave
end)

-- ------------------------------------------------------------------ snowfield reinforcements

-- rule 4: when the reinforcements are enabled.
spawn(function()
    if diff == 3 then
        reinf_enabled = true
        return
    end
    while true do
        local in_huts = L.player_in_room(123) ~= nil or L.player_in_room(147) ~= nil
        if in_huts and (diff ~= 0 or not chr(0x13):exists()) then break end
        wait(STEP)
    end
    reinf_enabled = true
end)

-- rule 5: the spawn points' pads follow the player (first matching room wins; kept when no room matches).
local SPAWN_TABLE = {
    { 45, { 48, 66, 56, 123 } }, { 77, { 45, 102, 69, 147 } }, { 81, { 45, 71, 123, 56 } },
    { 51, { 45, 69, 181, 56 } }, { 102, { 45, 60, 181, 51 } }, { 54, { 77, 69, 71, 60 } },
    { 123, { 48, 163, 74, 188 } }, { 147, { 77, 69, 61, 219 } }, { 219, { 188, 210, 181, 123 } },
    { 74, { 235, 123, 69, 252 } }, { 210, { 219, 102, 56, 252 } }, { 186, { 219, 241, 81, 51 } },
    { 235, { 81, 210, 71, 69 } }, { 252, { 210, 147, 64, 63 } }, { 241, { 252, 181, 56, 143 } },
}
local spawn_pads = { 48, 66, 56, 123 }  -- the start room's row until the player is seen in a listed room
spawn(function()
    while not exiting do
        for _, row in SPAWN_TABLE do
            if L.player_in_room(row[1]) ~= nil then
                spawn_pads = row[2]
                break
            end
        end
        wait(STEP)
    end
end)

-- rules 6, 8: every 2 s each spawn point k refills chr 0x04+k (one living chr per point, no other cap).
spawn(function()
    while not reinf_enabled do wait(STEP) end
    while not exiting do
        for k = 0, 3 do
            local c = 0x04 + k
            local p = spawn_pads[k + 1]
            if not chr(c):exists() and not exiting and L.spawn_roll(pad(p)) then
                -- body 21, random head (head omitted), sunglasses; only out of the player's view (force omitted)
                local g = spawn_guard({ chr = c, pad = p, body = 21, sunglasses = true })
                if g then arm_reinforcement(g) end  -- rule 7
            end
        end
        wait(2)
    end
end)

-- ------------------------------------------------------------------ heavy reinforcements (chr 0x08-0x0B)

-- rule 16: kneel and aim, fire on a line of sight; a near miss (the event) or the door alarm: hunter for good.
local function heavy_guard(g: Target)
    local missed = false
    local sub = on("guard_near_miss", function() missed = true end, { chr = g.chr or -1 })
    while L.alive_guard(g) do
        if door_alarm_on or missed then
            sub:off()
            L.hunt(g, REINF_HUNTER)
            return
        end
        if g:line_to_player() ~= nil then
            g:fire_at(nil, { stance = "kneel" })
            wait(STEP)
            L.wait_idle(g, 10, { alive = L.alive_guard })
        else
            g:fire_at(nil, { stance = "kneel", aim_only = true })
        end
        wait(STEP)
    end
end

-- rule 14: each dead or gone heavy is spawned at its own pad, even in view, unless the exit has started.
heavy_wave = function()
    if exiting then return end
    for k = 0, 3 do
        local c = 0x08 + k
        if not chr(c):exists() then
            local g = spawn_guard({ chr = c, pad = 9 + k, body = 38, force = true })
            if g then
                g:set_stats({ armour = 4.0, grenade_chance = 75 })
                g:give_hat(221)
                g:give_weapon(7, { model = 193 })
                if math.random() < 0.39 then
                    g:give_weapon(7, { model = 193, left_hand = true })
                end
                L.run_task(g, function() heavy_guard(g) end)
            end
        end
    end
end

-- rule 15: the locked door tag 3 (prop 185) refuses a player: alarm, text 7C0A, the door alarm on for 16 s
-- (a refusal while it is on restarts the 16 s without a new wave; a refusal while off sends a wave).
local alarm_gen = 0
local refused_once = false
local last_try = -1
local function door_tried()
    if clock() - last_try < 0.5 then return end  -- one try reported by both events below
    last_try = clock()
    refused_once = true
    alarm.trigger()
    hud.top(text("7C0A") :: string)
    local was_on = door_alarm_on
    door_alarm_on = true
    alarm_gen += 1
    local gen = alarm_gen
    if not was_on then heavy_wave() end  -- rule 14: each off-to-on switch
    spawn(function()
        wait(16)
        if alarm_gen == gen then door_alarm_on = false end
    end)
end
on("door_refused", function() door_tried() end, { prop = 185 })
-- rule 15 note: once refused, every later use of that door counts as a refused try, even one that opens it.
on("activated", function()
    if refused_once then door_tried() end
end, { prop = 185 })

-- ------------------------------------------------------------------ vent hatch

-- rule 18: the four padlocks (native links) hold the hatch; 2 s after it starts to open, sound 0x0105.
spawn(function()
    wait(event("door", { prop = 187, state = "moving" }))
    wait(2)
    sound.play(0x0105, { from = door(187) })
end)

-- ------------------------------------------------------------------ exit (rule 17) and its cinematic

local function exit_cinematic()
    -- Cinematics, Exit 1: all characters and the players' weapons hidden; any button ends the level.
    cinema.hide_all_chrs(true)
    for _, n in L.living() do player(n):hide_weapon(true) end
    L.end_on_skip({ settle = 0 })
    -- 2: the cutscene camera tag 4, kept on Bond.
    local body: Target = bond_body(0)
    camera.fixed(tag(4), { look_at = body })
    -- 3: Bond on pad 47, no shadow; the hatch immune to all but explosions; fade in; the extra music.
    body:show(true)
    body:teleport(47)
    body:set_shadow(false)
    body:set_body_flags({ hitbox_cull = true, height_lock = true })
    tag(5):set_damage("explosions")
    screen.fade_in()
    music.play_extra(0, { stop_after = 255, fade = 255 })
    -- 4: animation 99 to frame 66 (blend 16 ticks), then 178 from frame 20 to its end, held (blend 24 ticks).
    body:anim(99, { to = 66, blend = 16 / 60 })
    while not body:anim_done() do wait(STEP) end
    body:anim(178, { from = 20, hold = true, blend = 24 / 60 })
    -- 5: 3 s after 178 starts, fade to black and end (the result from the objectives).
    wait(3)
    L.fade_out(nil, { settle = 0 })
    mission.finish()
end

-- rule 17: in the room of pad 46 below height 300 (co-op: the first living player there triggers it; the
-- cinematic freezes everyone).
local function in_shaft(n: number, below: number): boolean
    if not L.alive(n) or not player(n):in_room_of(46) then return false end
    local _, y = player(n):pos()
    return y ~= nil and y < below
end

spawn(function()
    while true do
        local who: number? = nil
        while who == nil do
            for _, n in L.living() do
                if in_shaft(n, 300) then who = n break end
            end
            if who == nil then wait(STEP) end
        end
        local n = who :: number
        -- HUD hidden, controls locked, clock stopped
        hud.hide(true)
        L.freeze_all(true)
        -- TODO(api): no verb stops the mission clock (rule 17).
        local t0 = clock()
        local go = false
        while true do
            if not L.alive(n) then break end          -- dies meanwhile: nothing more happens
            if not player(n):in_room_of(46) then break end  -- left the room: re-arm
            if in_shaft(n, 0) or clock() - t0 >= 3 then go = true break end
            wait(STEP)
        end
        if go then break end
        hud.hide(false)
        L.freeze_all(false)
        if not L.alive(n) then
            -- the player died in the shaft: nothing more happens for this try (other co-op players may still
            -- use the shaft)
            wait(STEP)
        end
    end
    objectives.complete("d")
    exiting = true                      -- stops all spawning
    cinema.begin()                      -- invulnerable, no pickups
    L.fade_out(nil, { settle = 0 })
    if not objectives.all_complete() then
        mission.fail()
        return
    end
    exit_cinematic()
end)

-- ------------------------------------------------------------------ intro

-- Cinematics, Intro: played by the shared intro module ("level_intro": false in mission.json) from
-- surface1_intro_data (one of the three fixed cameras with 7C14 and 7C15, the swirl, the body's animation 102).
require("intro").play(require("surface1_intro_data"))
