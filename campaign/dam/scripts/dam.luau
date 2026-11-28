-- dam: the Dam level (specs/dam.md). Bond crosses the dam, silences the alarms, plants the covert modem on the
-- satellite link, triggers the data backup at the mainframes, and bungee-jumps off the platform.
-- Covers: the Characters table, objectives a-d, rules 1-17 (rules 12, 13, 15, 16 in dam_guards.luau), Objects and
-- world (airlock gates and their screens, the alarm boxes, the truck), the intro and outro, and the co-op notes.

local guard_ai = require("guard_ai")
local L = require("campaign_lib")
guard_ai.boot()

local dg = require("dam_guards")

local STEP = 0.25
local MODEM = 47
local UNARMED = 0   -- rule 17: unarmed is item 0 (the fist is 1)
local diff = mission.difficulty()
if diff < 0 then
    diff = 3
end

-- ------------------------------------------------------------------ players (co-op helpers)
local function player_alive(n: number): boolean
    return L.in_game(n) and not player(n):is_dead()
end


-- ------------------------------------------------------------------ objectives
objectives.add("a", text("2C16") or "", 1)   -- neutralise every alarm (Secret Agent and up)
objectives.add("b", text("2C17") or "", 2)   -- install the covert modem (00 Agent, 007)
objectives.add("c", text("2C18") or "", 2)   -- intercept the data backup (00 Agent, 007)
objectives.add("d", text("2C19") or "", 0)   -- bungee jump

-- The lowest difficulty of each objective: rules 1-10 run everywhere, but b and c only count on 00 Agent and up.
local MIN_DIFF = { a = 1, b = 2, c = 2, d = 0 }

local function complete(id: string)
    if diff >= MIN_DIFF[id] then
        if not objectives.is_failed(id) then
            objectives.complete(id)
        end
    end
end

local function fail(id: string)
    if diff >= MIN_DIFF[id] then
        -- a failed state wins over a completed one (b can still fail after the modem is in)
        if not objectives.is_failed(id) then
            objectives.fail(id)
        end
    end
end

-- objective a: the alarm boxes tag 0..3
spawn(function()
    while not (tag(0):is_destroyed() and tag(1):is_destroyed() and tag(2):is_destroyed() and tag(3):is_destroyed()) do
        wait(event("destroyed"))
    end
    complete("a")
end)

-- ------------------------------------------------------------------ level state
local bungeeStarted = false   -- rule 17
local modemState = "none"     -- "none" | "installed" | "misplaced" (rules 4, 5)
local backupState = "none"    -- "none" | "running" | "done" | "dead" (rules 8-10)
local linkDown = false        -- rule 6

-- ------------------------------------------------------------------ characters
-- Bodies, heads, stats, hearing and vision are setup data (Characters table).
local function start_generic(n: number, mode: string, opts: { [string]: any }?)
    local g = chr(n)
    if g:exists() and guard_ai.mode(g) == nil then
        guard_ai.start(g, mode, opts)
    end
end

dg.stand_and_shoot(chr(0x00), true)   -- rule 13
dg.stand_and_shoot(chr(0x01), true)   -- rule 13
dg.post_guard(chr(0x02), 4)           -- rule 15
for _, n in { 0x04, 0x05, 0x06, 0x07, 0x0a, 0x0b, 0x0c, 0x16, 0x1c } do
    start_generic(n, "standard")      -- Characters: standard generic guard (clone flag: setup data)
end
for n = 0x0e, 0x1b do
    if n ~= 0x16 and chr(n):exists() then
        dg.stand_and_shoot(chr(n))    -- rule 12
    end
end
local PATROLS = { [0x27] = 0, [0x28] = 1, [0x29] = 2, [0x2a] = 3, [0x2b] = 4, [0x2c] = 5, [0x2d] = 6 }
for n, path in PATROLS do
    start_generic(n, "simple", { patrol = path })   -- Characters: patrol, then simple guard
end
start_generic(0x2e, "alarm_raiser", { alarm_pad = 10074 })   -- Characters: alarm raiser at bound pad 10074
for _, n in { 0x2f, 0x0d } do
    if chr(n):exists() then
        dg.alert_only(chr(n))         -- rule 16
    end
end

-- rule 14: the gate guard works the gate control at 2 s, gate A closes, he walks to pad 5, faces the player,
-- then stands and shoots (rule 12).
spawn(function()
    local g = chr(0x03)
    if not g:exists() then
        return
    end
    wait(2)
    if L.dying_or_dead(g) then
        return
    end
    g:anim(172, { from = 0, to = 69 })
    while not g:anim_done() and not L.dying_or_dead(g) do
        wait(STEP)
    end
    tag(8):close()
    if L.dying_or_dead(g) then
        return
    end
    g:run_to(5, "walk")
    L.wait_done(g)
    if L.dying_or_dead(g) then
        return
    end
    g:face_player()
    dg.stand_and_shoot(g)   -- rule 12
end)

-- ------------------------------------------------------------------ covert modem and satellite link (rules 1-6)
-- rule 1: the players start with the modem (setup intro data). rule 2: screen tag 5, dish tag 4.
-- rule 3: rules 4-8 apply only until the backup completes.

-- rules 4 and 5: where the modem came to rest (any player's modem: co-op notes)
spawn(function()
    while modemState == "none" and backupState ~= "done" do
        local how, onTag = item_landed(MODEM)
        if how == "stuck" and onTag == 5 then
            -- rule 4
            modemState = "installed"
            L.top("2C08")
            sound.play(0xE3, { from = tag(5) })
            complete("b")
        elseif how ~= nil then
            -- rule 5
            modemState = "misplaced"
            L.top("2C21")
            fail("b")
        end
        wait(STEP)
    end
end)

-- rule 6: the dish or the screen destroyed before the backup completed
on("destroyed", function(ev)
    if linkDown or backupState == "done" then
        return
    end
    if ev.tag == 4 or ev.tag == 5 then
        linkDown = true
        L.top("2C0E")
        fail("b")
        fail("c")
    end
end)

-- ------------------------------------------------------------------ the data backup (rules 7-10)
tag(6):make_usable()
tag(7):make_usable()

local function both_mainframes_down(): boolean
    return tag(6):is_destroyed() and tag(7):is_destroyed()
end

-- rule 9 (the countdown and the beep), rule 10 (both mainframes lost during it)
local function run_backup()
    backupState = "running"
    L.top("2C09")
    countdown.set(10)
    countdown.show(true)
    countdown.start()
    local nextBeep = clock()
    while countdown.left() > 1 do
        if both_mainframes_down() then
            -- rule 10
            countdown.stop()
            countdown.show(false)
            backupState = "dead"
            L.top("2C0B")
            fail("c")
            return
        end
        if clock() >= nextBeep then
            nextBeep += 1
            sound.play(0xBD, { from = tag(6) })
        end
        wait(0.1)
    end
    countdown.stop()
    countdown.show(false)
    backupState = "done"
    L.top("2C0A")
    complete("c")
end

-- rules 7 and 9: any player using a mainframe (co-op notes)
local function on_use()
    if backupState ~= "none" then
        return
    end
    if modemState ~= "installed" then
        -- rule 7: the modem is not in yet; nothing else
        L.top("2C0D")
        return
    end
    -- rule 9: once, if objective c has not failed
    if linkDown or (diff >= MIN_DIFF.c and objectives.is_failed("c")) then
        return
    end
    run_backup()
end
on("activated", on_use, { tag = 6 })
on("activated", on_use, { tag = 7 })

-- rule 8: both mainframes destroyed before the backup started
spawn(function()
    while not both_mainframes_down() do
        wait(event("destroyed"))
    end
    if backupState == "none" then
        backupState = "dead"
        L.top("2C0C")
        fail("c")
    end
end)

-- ------------------------------------------------------------------ the alarm (rule 11)
spawn(function()
    wait(event("alarm"))
    if bungeeStarted then
        return
    end
    for i = 1, 2 do
        if i == 2 then
            wait(1 / 60)   -- one tick after the first
        end
        local g = spawn_guard({ pad = 116, body = 37 })   -- only out of view; not retried
        if g then
            g:set_stats({ health = 4.0, max_health = 4.0, armour = 2.0 })
            g:give_weapon(8)   -- AK-47
            spawn(function()
                dg.run_then_standard(g)
            end)
        end
    end
end)

-- ------------------------------------------------------------------ objects and world
-- The airlock: gate A (door tag 8) and gate B (door tag 9).
local gateA, gateB = tag(8), tag(9)
on("activated", function()
    if gateA:state() == "opening" and gateB:state() ~= "closed" then
        L.bottom("2C10")
    end
end, { tag = 8 })
on("activated", function()
    if gateB:state() == "opening" and gateA:state() ~= "closed" then
        L.bottom("2C0F")
    end
end, { tag = 9 })

-- The gate status screens: tags 15/16 for gate A, 17/18 for gate B, screens 0 and 1 of each.
local BANKS = {
    open = { 0x30, 0x32 },
    closing = { 0x2E, 0x30 },
    closed = { 0x31, 0x30 },
    opening = { 0x30, 0x2F },
}
local function show_gate(screens: { number }, state: string)
    local b = BANKS[state]
    if not b then
        return
    end
    for _, t in screens do
        tag(t):show_screen(b[1], 0)
        tag(t):show_screen(b[2], 1)
    end
end
show_gate({ 15, 16 }, "open")
show_gate({ 17, 18 }, "closed")
local function watch_gate(gate: Target, screens: { number })
    spawn(function()
        local last = ""
        while true do
            local s = gate:state()
            if s ~= last then
                last = s
                show_gate(screens, s)
            end
            wait(STEP)
        end
    end)
end
watch_gate(gateA, { 15, 16 })
watch_gate(gateB, { 17, 18 })

-- Objects and world: the truck (setup prop 321, a vehicle, model 279 at pad 317) drives patrol path 7 for the
-- whole level, top speed 512 in the spec's unit (1000 = 1 m/s), reaching that speed in 2 s.
-- TODO(api): Target:drive() does not state its speed unit; the spec's number is passed as is.
prop(321):drive(7, 512, 2)

-- ------------------------------------------------------------------ the outro (Cinematics: Outro)
-- Holds a shot for `seconds`; true when a player pressed a button (the level then ends).
local function hold(seconds: number): boolean
    local t = clock() + seconds
    while clock() < t do
        if cinema.skipped() then
            return true
        end
        wait(0.1)
    end
    return false
end

local function outro(p: number)
    -- 1. behind the black screen: all hidden, weapons away, Bond's model on pad 331, no shadow
    cinema.hide_all_chrs(true)
    for _, n in L.players() do
        player(n):hide_weapon(true)
    end
    local body = bond_body(p)
    body:show(true)
    body:set_shadow(false)
    body:set_gravity(false)   -- held in the air
    local function shot(cam: number, run: () -> boolean): boolean
        body:teleport(331)
        camera.fixed(tag(cam), { look_at = body })
        screen.fade_in()
        local skipped = run()
        screen.fade_out()
        L.wait_fade({ settle = 0, poll = 0.1 })
        return skipped
    end
    local skipped = false
    -- 2. shot 1, camera tag 10: music, animation 177 frames 0..3 held 1 s, then frames 3..90 for about 1.7 s
    skipped = shot(10, function()
        music.play_extra(0)
        body:anim(177, { from = 0, to = 3, hold = true })
        if hold(1) then
            return true
        end
        body:anim(177, { from = 3, to = 90, hold = true })
        return hold(1.7)
    end)
    -- 3. shot 2, camera tag 11: animation 177 from frame 80 to its end, held, 5 s
    if not skipped then
        skipped = shot(11, function()
            body:anim(177, { from = 80, hold = true })
            return hold(5)
        end)
    end
    -- 4. shot 3, camera tag 12: the same for 7.5 s; then the level ends as a win
    if not skipped then
        shot(12, function()
            body:anim(177, { from = 80, hold = true })
            return hold(7.5)
        end)
    end
    mission.win()
end

-- ------------------------------------------------------------------ rule 17: the bungee exit
spawn(function()
    local p: number? = nil
    while p == nil do
        local r = player_in_room(330)
        if r ~= nil and player_alive(r) then
            p = r
        else
            wait(STEP)
        end
    end
    local who = p :: number
    bungeeStarted = true
    complete("d")
    -- every player: invulnerable, no pickups, no HUD, no control (co-op: the first one in triggers it for all;
    -- only the jumper is pushed off the edge, the others stay frozen where they are)
    -- TODO(api): cinema.begin does not declare that the mission clock stops
    cinema.begin()
    local jumper = player(who)
    jumper:force_weapon(UNARMED)
    local t0 = clock()
    while clock() < t0 + 5.8 do
        local _, y = jumper:pos()
        if y ~= nil and y < -17000 then
            break
        end
        jumper:push(0, 4)   -- 4 units per tick along +Z, off the edge (a per-frame motion, not a poll)
        wait(1 / 60)
    end
    screen.fade_out()
    L.wait_fade({ settle = 0, poll = 0.1 })
    if not objectives.all_complete() then
        mission.fail()
    else
        outro(who)
    end
end)

-- ------------------------------------------------------------------ the intro (Cinematics: Intro)
-- Staged by the shared intro module ("level_intro": false in mission.json) from dam_intro_data (one of the six
-- fixed cameras, the swirl, the body: the table pose, then animation 99 at 6 s). The start pad 33, the PP7
-- silenced (item 5) with 100 rounds, the modem (item 47), cuff 3, watch 11:30 and the other start slots stay the
-- setup's own.
require("intro").play(require("dam_intro_data"))
