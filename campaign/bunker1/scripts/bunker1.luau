-- bunker1: the Severnaya bunker (specs/bunker1.md). Characters table, objectives a-e, rules 1-18, the outro.
-- The programmer (rules 1-8) lives in bunker1_programmer.luau; the helpers in campaign_lib.luau.
-- The intro is staged here (mission.json "level_intro": false; the start pads, items, outfit and watch stay setup data).

local guard_ai = require("guard_ai")
guard_ai.boot()

local lib = require("campaign_lib")
local programmer = require("bunker1_programmer")

local show = lib.bottom
local STEP = 0.25
local diff = mission.difficulty()
if diff < 0 then
    diff = 3
end

local state = { exiting = false }   -- rule 18: the exit sequence has begun

-- ===================================================================== objectives
objectives.add("a", text("781D") or "", 1)   -- disrupt all surveillance (Secret Agent and up)
objectives.add("b", text("781E") or "", 0)   -- copy the key, leave the original
objectives.add("c", text("781F") or "", 2)   -- the programmer activates the computer (00 Agent and up)
objectives.add("d", text("7820") or "", 2)   -- download the mainframe data (00 Agent and up)
objectives.add("e", text("7821") or "", 0)   -- photograph the main screen

-- ===================================================================== characters
local function start_guard(n: number, mode: string, opts: any?, stats: GuardStats?)
    local g: Target = chr(n)
    if not g:exists() then
        return
    end
    if stats then
        g:set_stats(stats)
    end
    guard_ai.start(g, mode, opts)
end

-- chr 0x00 / 0x08: patrols, then the simple guard; they carry the keycards (setup items).
start_guard(0x00, "simple", { patrol = 0 }, { grenade_chance = 5 })
start_guard(0x08, "simple", { patrol = 1 }, { grenade_chance = 5 })
-- chr 0x06: the alarm raiser, alarm on bound pad 10022.
start_guard(0x06, "alarm_raiser", { alarm_pad = 10022 })
-- The standard guards (their weapons are setup items).
local GRENADES: { [number]: number } = { [0x0C] = 10, [0x12] = 10, [0x0A] = 5, [0x0B] = 5, [0x18] = 5 }
local STANDARD = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11,
    0x12, 0x13, 0x14, 0x17, 0x18 }
for _, n in STANDARD do
    local gc = GRENADES[n]
    start_guard(n, "standard", nil, if gc then { grenade_chance = gc } else nil)
end

-- ===================================================================== objectives a, b, e

-- Objective a: the four security cameras (tags 0-3) destroyed.
local function cameras_down(): boolean
    for t = 0, 3 do
        if not tag(t):is_destroyed() then
            return false
        end
    end
    return true
end
on("destroyed", function(_ev: MissionEvent)
    if not objectives.is_complete("a") and cameras_down() then
        objectives.complete("a")
    end
end)

-- Objective b: the GoldenEye key (tag 4) copied with the key analyser (the game's own key copier).
on("key_copied", function(_ev: MissionEvent)
    if not objectives.is_complete("b") then
        objectives.complete("b")
        tag(4):set_collectable(false)   -- rule 12: the original must stay
    end
end)
-- rule 12: whatever completes b, the key can no longer be picked up.
on("objective", function(_ev: MissionEvent)
    tag(4):set_collectable(false)
end, { id = "b" })

-- Objective e: the main video wall (tag 5) photographed with the camera.
on("photographed", function(_ev: MissionEvent)
    if not objectives.is_complete("e") then
        objectives.complete("e")
    end
end, { tag = 5 })

-- ===================================================================== the programmer (rules 1-8)
programmer.start(diff)

-- ===================================================================== the mainframe download (rules 9-11)
local function mainframe_tag(t: any): boolean
    return t == 6 or t == 7
end

on("gadget_used", function(ev: MissionEvent)
    if not mainframe_tag((ev :: any).tag) then
        return
    end
    if not objectives.is_complete("c") then
        show("7814")   -- rule 9: the computer is not activated
        return
    end
    -- rule 10: the download (a later use repeats it harmlessly)
    show("7815")
    sound.play(227, { from = tag((ev :: any).tag) })
    wait(4)
    show("7816")
    objectives.complete("d")
end, { item = 55 })

-- rule 11: on 00 Agent and 007, both mainframe parts destroyed before the download fails d.
if diff >= 2 then
    on("destroyed", function(ev: MissionEvent)
        if not mainframe_tag((ev :: any).tag) then
            return
        end
        if tag(6):is_destroyed() and tag(7):is_destroyed()
            and not objectives.is_complete("d") and not objectives.is_failed("d") then
            show("782E")
            objectives.fail("d")
        end
    end)
end

-- ===================================================================== alarm reinforcements (rules 13-15)
local REINFORCEMENTS = { 0x1A, 0x1B, 0x1C }

-- rule 14: the loadout, by one random roll (armour is added to the guard's health, in health units).
local function equip(g: Target)
    local r = math.random() * 100
    if clock() < 120 or r < 21 then
        g:set_stats({ add_armour = 2 })
        g:give_hat(0xDC)
        g:give_weapon(8)
    elseif r < 60 then
        g:set_stats({ add_armour = 8 })
        g:give_hat(0xDD)
        g:give_weapon(7)
        if lib.chance(39) then
            g:give_weapon(7, { left_hand = true })
        end
    else
        g:set_stats({ add_armour = 6 })
        g:give_hat(0xDE)
        g:give_weapon(6)
        if lib.chance(39) then
            g:give_weapon(6, { left_hand = true })
        end
    end
end

-- rule 15: runs to the player again and again, fights within 50 m in sight; never idles.
local function reinforcement(g: Target)
    lib.hunt_nearest(g, { range = 5000, speed = function() return "run" end })
end

spawn(function()
    wait(event("alarm"))   -- rule 13: the first alarm
    -- TODO(api): play_extra's options for "75 s long, playing on for 60 s after a stop" are read as total / stop_after.
    music.play_extra(0, { total = 75, stop_after = 60 })
    while true do
        for _, n in REINFORCEMENTS do
            if state.exiting then
                break
            end
            if not lib.present(chr(n)) then
                local g = spawn_guard({ chr = n, pad = 97, body = 4 })   -- head omitted: a random head
                if g then
                    equip(g)   -- rule 14
                    spawn(function() reinforcement(g) end)   -- rule 15
                end
            end
        end
        wait(20)
    end
end)

-- ===================================================================== guard chr 0x07 (rules 16-17)
local attack_always = false
on("destroyed", function(ev: MissionEvent)
    if (ev :: any).tag == 2 then
        attack_always = true   -- rule 17
    end
end)

-- One generic move (a mode that ends), then back to this script.
local function sub(g: Target, mode: string)
    lib.run_generic(g, mode, { alive = function(x: Target): boolean return x:exists() end })
end

spawn(function()
    local g: Target = chr(0x07)
    if not g:exists() then
        return
    end
    guard_ai.stop(g)
    -- The engine's one-update signals, latched until his next decision (cleared after each reaction: a signal
    -- from while he was busy is stale).
    local spotted, witnessed, missed = false, false, false
    local conns: { Connection } = {
        on("guard_spotted", function(_ev: MissionEvent) spotted = true end, { chr = 0x07 }),
        on("guard_witnessed", function(_ev: MissionEvent) witnessed = true end, { chr = 0x07 }),
        on("guard_near_miss", function(_ev: MissionEvent) missed = true end, { chr = 0x07 }),
    }
    local function react(mode: string)
        sub(g, mode)
        spotted, witnessed, missed = false, false, false
    end
    while g:exists() and not g:is_dying() do
        if g:is_idle() then
            if attack_always then
                react("engage")               -- rule 17: attacks whether or not he sees the player
            elseif spotted then
                react("engage")               -- rule 16: sees a player
            elseif g:alerted_within(10) or witnessed then
                react("chase")                -- rule 16: shot at / saw a player lately, or saw a guard shot or killed
            elseif g:heard_shot_within(10) then
                react("reinforce")            -- rule 16: a clone or a run to the player
            elseif missed then
                react(if lib.chance(50) then "startle_chase" else "engage")   -- rule 16: near miss
            elseif lib.chance(11) then
                g:idle_anim()                 -- rule 16: about 0.8% per tick (11% per quarter second)
            end
        end
        wait(STEP)
    end
    for _, c in conns do
        c:off()
    end
end)

-- ===================================================================== exit (rule 18) and outro
local function outro(p: number)
    -- 1. every character hidden; any button press now fades out and ends the level; Bond's weapon hidden
    cinema.hide_all_chrs(true)
    local ending = false
    local function finish_win()
        if ending then
            return
        end
        ending = true
        lib.fade_out(nil, { settle = lib.STEP })
        mission.win()
    end
    spawn(function()
        while not ending do
            if cinema.skipped() then
                finish_win()
            end
            wait(STEP)
        end
    end)
    for _, n in lib.living() do
        player(n):hide_weapon(true)
    end
    -- 2. the cutscene camera tag 9, turning to keep Bond in view; Bond at pad 104
    local body = bond_body(p)
    body:teleport(104)
    body:show(true)
    camera.fixed(tag(9), { look_at = body })
    -- 3. fade in; the alarm track restarts (255 s); door tag 8 snaps open without sound
    screen.fade_in()
    music.play_extra(0, { total = 255 })
    tag(8):unlock(true)
    tag(8):open_now()
    -- 4. Bond: animation 0x6A held, 1 s later 0x59 held
    body:anim(0x6A, { hold = true })
    wait(1)
    body:anim(0x59, { hold = true })
    -- 5. 3 s later fade to black, then the level ends (a win)
    wait(3)
    finish_win()
end

spawn(function()
    -- rule 18: the first living player in the room of pad 100 triggers the exit for everyone
    local p: number? = nil
    while p == nil do
        p = lib.player_in_room(100)
        if p == nil then
            wait(STEP)
        end
    end
    state.exiting = true
    -- every player invulnerable, no pickups, HUD hidden, controls locked
    -- TODO(api): no verb stops the mission clock; cinema.begin() is taken to cover it.
    cinema.begin()
    lib.fade_out(nil, { settle = lib.STEP })
    if objectives.all_complete() then
        outro(p :: number)
    else
        mission.finish()   -- the usual end-of-mission check (a failure)
    end
end)

-- ===================================================================== intro (Cinematics, Intro)
-- Played by the shared intro module ("level_intro": false in mission.json) from bunker1_intro_data: one of the
-- five fixed cameras with 782C, the swirl, the body on intro-animation record 1 (animation 66, intro.md section 3).
require("intro").play(require("bunker1_intro_data"))
