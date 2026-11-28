-- aztec: Drax's launch complex (specs/aztec.md). Characters table, objectives a-b, rules 1-18, the lift-off outro.
-- Helpers in campaign_lib.luau. The intro is staged here (mission.json "level_intro": false; the start pad, items,
-- outfit and watch stay setup data). Door key locks (tags 13, 21) and their locked messages are setup data too.

local guard_ai = require("guard_ai")
guard_ai.boot()

local lib = require("campaign_lib")

local show = lib.bottom
local STEP = 0.25

local state = {
    exiting = false,    -- the lift-off outro has begun
    launched = false,   -- rule 10: the launch sequence started (the bay is locked whenever it is open)
}

-- ===================================================================== objectives
objectives.add("a", text("1415") or "", 0)   -- reprogram the shuttle guidance
objectives.add("b", text("1416") or "", 0)   -- launch the shuttle

-- ===================================================================== characters
local function setup(n: number, stats: GuardStats, mode: string?)
    local g: Target = chr(n)
    if not g:exists() then
        return
    end
    g:set_stats(stats)
    if mode then
        guard_ai.start(g, mode)
    end
end

-- Jaws: armour +50, health 50 (007 health modifier applies), speed / accuracy / hit recovery 80, no auto-aim;
-- standard guard. He carries two AR33 and the security smart card (setup items).
setup(0x2A, { max_health = 50, health = 50, add_armour = 50, speed = 80, accuracy = 80, recovery = 80 }, "standard")
chr(0x2A):set_flag("no_autoaim", true)

-- Simple deaf guards.
for _, n in { 0x13, 0x15, 0x16, 0x17 } do
    setup(n, { speed = 50, accuracy = 50 }, "simple_deaf")
end
-- Standard guards (0x03-0x05 are also the reinforcement slots of rule 15).
local GRENADE50: { [number]: boolean } = { [0x03] = true, [0x08] = true, [0x09] = true, [0x0A] = true,
    [0x0B] = true, [0x0C] = true, [0x0D] = true }
local STANDARD = { 0x03, 0x04, 0x05,
    0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12,
    0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29 }
for _, n in STANDARD do
    setup(n, { speed = 50, accuracy = 50, grenade_chance = if GRENADE50[n] then 50 else nil }, "standard")
end

-- ===================================================================== marksmen (rule 14)
local MARKSMEN = { 0x06, 0x07, 0x14, 0x20, 0x21, 0x22, 0x23 }

local function kneel_and_fire(g: Target)
    -- A kneeling marksman aims and fires at a random moment (about 2% per tick: 26% per quarter second), 3 s at most.
    g:kneel()
    g:fire_at(nil, { stance = "kneel", aim_only = true })
    local t0 = clock()
    repeat
        wait(STEP)
    until not g:exists() or math.random() < 0.26 or clock() - t0 >= 3
    if g:exists() then
        g:fire_at(nil, { stance = "kneel" })
    end
end

local function marksman(g: Target)
    guard_ai.stop(g)
    g:set_stats({ add_armour = 2, speed = 80, accuracy = 80,
        grenade_chance = if g.chr == 0x06 or g.chr == 0x07 then 50 else nil })
    -- A shot just missing him, a guard seen hit or killed, a player seen: the engine's signals, latched until his
    -- next decision.
    local poked = false
    local conns: { Connection } = {}
    local c = g.chr
    if c ~= nil then
        for _, name in { "guard_near_miss", "guard_witnessed", "guard_spotted" } do
            table.insert(conns, on(name, function(_ev: MissionEvent)
                poked = true
            end, { chr = c }))
        end
    end
    -- They never move from their spot: no order here moves them.
    g:fire_at(nil, { aim_only = true })   -- keep the gun aimed at the player
    while g:exists() and not g:is_dying() do
        if poked or g:line_to_player() ~= nil then   -- or the player is in his line of sight
            local pick = lib.roll({ 40, 21, 39 })
            if pick == 1 then
                if not g:fire_at(nil) then          -- 40% fire; else a grenade; else kneel
                    if not g:throw_grenade() then
                        kneel_and_fire(g)
                    end
                end
            elseif pick == 2 then
                if not g:throw_grenade() then       -- 21% grenade; else kneel
                    kneel_and_fire(g)
                end
            else
                kneel_and_fire(g)                   -- 39% kneel
            end
            lib.wait_idle(g, nil, { settle = true })   -- wait until the action is finished, then start over
            poked = false   -- signals from during the action are stale
            g:fire_at(nil, { aim_only = true })
        end
        wait(STEP)
    end
    for _, conn in conns do
        conn:off()
    end
end

for _, n in MARKSMEN do
    local g: Target = chr(n)
    if g:exists() then
        spawn(function() marksman(g) end)
    end
end

-- ===================================================================== the exhaust bay (rules 1-5)
local bay = tag(0)   -- the bay door itself

local function bay_moving(): boolean
    local s = bay:state()
    return s == "opening" or s == "closing"
end

-- rule 3
local function open_bay()
    show("1406")
    tag(20):close()
    bay:open()
    spawn(function()
        wait(15)
        tag(9):open()
        tag(10):close()
    end)
end

-- rule 4
local function close_bay()
    show("1407")
    tag(20):open()
    bay:close()
    spawn(function()
        wait(15)
        tag(9):close()
        tag(10):open()
    end)
end

-- rule 2: the switch
local function toggle_bay()
    if bay_moving() then
        return
    end
    if bay:state() == "closed" then
        open_bay()
    elseif state.launched then
        show("140F")   -- the launch has locked the bay (rule 10)
    else
        close_bay()
    end
end

-- rule 1: the bay starts closed; its switch is tag 11.
on("activated", function(_ev: MissionEvent)
    toggle_bay()
end, { tag = 11 })

-- rule 5: the first time a player enters the room of pad 131, the bay opens; once open, the charges blow in pairs.
spawn(function()
    lib.wait_until(function() return lib.player_in_room(131) ~= nil end)
    if bay:state() == "closed" then
        toggle_bay()   -- as if the switch were activated
    end
    lib.wait_until(function() return bay:state() == "open" end)
    local t0 = clock()
    for k, at in { 0.5, 1.5, 2.5, 3.5 } do
        local left = at - (clock() - t0)
        if left > 0 then
            wait(left)
        end
        for _, t in { 2 * k - 1, 2 * k } do
            local c = tag(t)
            c:set_invincible(false)   -- normally indestructible
            c:explode()
        end
    end
end)

-- ===================================================================== guidance and launch (rules 6-11)
local guidance_done = false
local mainframe_done = false

-- rules 6-7: the disk drive, tag 19
on("activated", function(_ev: MissionEvent)
    if not guidance_done then
        show("1409")
    end
end, { tag = 19 })
on("gadget_used", function(ev: MissionEvent)
    if guidance_done then
        return
    end
    if ev.item == 50 then
        guidance_done = true          -- rule 7
        objectives.complete("a")
        show("1408")
    else
        show("1409")                  -- rule 6
    end
end, { tag = 19 })

local outro: () -> ()   -- the lift-off outro, defined at the end

-- rule 11: the hold at T-10 and the lift-off
local function liftoff()
    objectives.complete("b")
    countdown.stop()
    countdown.show(false)
    state.exiting = true
    spawn(function() outro() end)
end

local function final_countdown()
    lib.wait_until(function() return countdown.left() <= 10 end)
    if bay:state() ~= "open" then
        countdown.stop()
        countdown.set(10)
        show("140E")   -- holding at T-10
        local last = clock()
        while bay:state() ~= "open" do
            if clock() - last >= 5 then
                show("140E")
                last = clock()
            end
            wait(STEP)
        end
        countdown.set(10)
        countdown.start()   -- resumes from 10 s; the bay is locked (state.launched)
    end
    show("140D")   -- final checks, the launch is go
    lib.wait_until(function() return countdown.left() <= 1 end)
    liftoff()
end

-- rule 10
local function launch_sequence()
    state.launched = true   -- from now on an open bay is locked open
    countdown.set(30)
    countdown.show(true)
    countdown.start()
    music.play_extra(0, { total = 255, stop_after = 0 })   -- 255 s, no linger
    show("140C")
    final_countdown()
end

-- rules 8-9: the launch mainframe, tag 15
on("activated", function(_ev: MissionEvent)
    if not mainframe_done then
        show("140A")
    end
end, { tag = 15 })
on("gadget_used", function(ev: MissionEvent)
    if mainframe_done then
        return
    end
    if ev.item == 73 then
        mainframe_done = true         -- rule 9 (whether or not objective a is done)
        show("140B")
        wait(5)
        launch_sequence()
    else
        show("140A")                  -- rule 8
    end
end, { tag = 15 })

-- ===================================================================== reinforcements (rules 12-17)
local ROOMS = {   -- rule 12: rooms (by pad) and the three spawn pads they give
    { rooms = { 130, 22 }, pads = { 97, 101, 99 } },
    { rooms = { 23, 107 }, pads = { 103, 90, 89 } },
    { rooms = { 84, 85, 78 }, pads = { 117, 128, 118 } },
    { rooms = { 39, 37 }, pads = { 84, 125, 124 } },
    { rooms = { 126, 65 }, pads = { 118, 111, 112 } },
    { rooms = { 109, 75 }, pads = { 95, 91, 124 } },
}
local spawn_pads: { number }? = nil
local in_group: { [number]: number } = {}   -- per player: the room group it is in now

-- rule 12: the last room entered (by any player) decides.
spawn(function()
    while true do
        for _, n in lib.living() do
            local now: number? = nil
            for i, grp in ROOMS do
                for _, r in grp.rooms do
                    if player(n):in_room_of(r) then
                        now = i
                    end
                end
            end
            if now ~= nil and in_group[n] ~= now then
                spawn_pads = ROOMS[now].pads
            end
            in_group[n] = now or 0
        end
        wait(STEP)
    end
end)

-- rule 13: Jaws gone = killed and removed.
local function jaws_gone(): boolean
    return not lib.present(chr(0x2A))
end

-- rule 16: the loadout
local function equip(g: Target)
    if lib.chance(49) then
        g:set_stats({ speed = 40, accuracy = 40, max_health = 4, health = 4, add_armour = 2 })
        g:give_weapon(22)
        g:give_weapon(22, { left_hand = true })
    else
        g:set_stats({ speed = 40, accuracy = 40, max_health = 4, health = 4, add_armour = 1,
            grenade_chance = 80 })
        g:give_weapon(22)
    end
end

-- rule 17: goes for the player (sprinting beyond 30 m), fights within 40 m in sight.
local function reinforcement(g: Target)
    lib.hunt_nearest(g, {
        range = 4000,
        speed = function()
            local _, d = lib.nearest_player(g)
            return if d > 3000 then "sprint" else "run"
        end,
    })
end

-- rule 15: 2 s after the start, then every 10 s.
local SLOTS = { 0x03, 0x04, 0x05 }
spawn(function()
    wait(2)
    while true do
        if jaws_gone() and not state.exiting and spawn_pads ~= nil then   -- rule 13: none while Jaws is present
            local pads = spawn_pads :: { number }
            for k, n in SLOTS do
                if not lib.present(chr(n)) then
                    local g = spawn_guard({ chr = n, pad = pads[k], body = 39 })   -- head omitted: a random head
                    if g then
                        equip(g)
                        spawn(function() reinforcement(g) end)
                    end
                end
            end
        end
        wait(10)
    end
end)

-- ===================================================================== the smart-card door screen (rule 18)
spawn(function()
    local tv = tag(18)
    local door21 = tag(21)
    local shown = 0x2E
    tv:show_screen(0x2E)
    while true do
        local s = door21:state()
        local want = if s == "opening" or s == "open" then 0x2F else 0x2E
        if want ~= shown then
            tv:show_screen(want)
            shown = want
        end
        wait(STEP)
    end
end)

-- ===================================================================== outro (lift-off)
outro = function()
    local alive = lib.living()
    if #alive == 0 then
        return   -- skipped entirely when no player is alive
    end
    local p = alive[1]
    -- 1. players invulnerable, no pickups, HUD hidden, controls locked; fade to black
    -- TODO(api): no verb stops the mission clock; cinema.begin() is taken to cover it.
    cinema.begin()
    lib.fade_out(nil, { settle = lib.STEP })
    -- 2. every character hidden; any button press now fades out and ends the level; weapon hidden; camera tag 22
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
    local body = bond_body(p)
    body:teleport(129)
    body:show(true)
    camera.fixed(tag(22), { look_at = body })
    -- 3. fade in; the shuttle lifts off; the fog switches to the next slot at once
    screen.fade_in()
    -- TODO(spec): anim() on an object plays the shuttle's (tag 23) own model animation, but the spec names no
    -- animation id for the lift-off, so the shuttle does not move.
    fog.next(false)
    -- 4. 10 s later fade to black and the level ends (a win)
    wait(10)
    finish_win()
end

-- ===================================================================== intro (Cinematics, Intro)
-- Played by the shared intro module from aztec_intro_data (one of the three fixed cameras with 1417, the swirl,
-- the body's table pose), alongside the rest of the level as before.
spawn(function()
    require("intro").play(require("aztec_intro_data"))
end)
