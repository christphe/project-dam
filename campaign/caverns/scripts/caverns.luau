-- caverns: the whole Caverns level (specs/caverns.md).
-- Covers: the Janus guards (generic, waiting, armoured, grenadiers: rules 1-4), Trevelyan's run, ledge and exit
-- (rules 5-10), the scientists (rules 11-16), the reinforcements (rules 17-18), the radio (rules 19-20), the exit
-- lift and the outro (rule 21), the start-room music (Objects and world), the intro (Cinematics: staged here,
-- mission.json has "level_intro": false). Bond's start items are setup data.

local guard_ai = require("guard_ai")
local L = require("campaign_lib")
guard_ai.boot()

-- Objectives (declared first: the tasks below complete and fail them)
objectives.add("a", text("1C13") or "", 1)
objectives.add("b", text("1C14") or "", 1)
objectives.add("c", text("1C15") or "", 1)
objectives.add("d", text("1C16") or "", 2)
objectives.add("e", text("1C18") or "", 0)

local STEP = 0.25

-- ------------------------------------------------------------------ helpers


local function wait_until_still(g: Target)
    wait(STEP)
    while g:exists() and (g:is_busy() or g:is_moving()) do wait(STEP) end
end


local function chrs(list: { number }): { Target }
    local out = {}
    for _, n in list do
        local g: Target = chr(n)
        if g:exists() then table.insert(out, g) end
    end
    return out
end

-- One guard's perception signals, from the guard events (rules 2-4, 11-15): each one stays set until taken.
type Signals = { witnessed: string?, near_miss: boolean, heard: boolean, conns: { Connection } }

local function signals(g: Target): Signals
    local sig: Signals = { witnessed = nil, near_miss = false, heard = false, conns = {} }
    local n = g.chr
    if n == nil then return sig end
    local f = { chr = n }
    table.insert(sig.conns, on("guard_witnessed", function(ev: MissionEvent)
        if ev.what == "death" or sig.witnessed == nil then sig.witnessed = ev.what end   -- a death outweighs a hit
    end, f))
    table.insert(sig.conns, on("guard_near_miss", function() sig.near_miss = true end, f))
    table.insert(sig.conns, on("guard_heard", function() sig.heard = true end, f))
    return sig
end

local function take_witness(sig: Signals): string?
    local w = sig.witnessed
    sig.witnessed = nil
    return w
end

local function take_miss(sig: Signals): boolean
    local m = sig.near_miss
    sig.near_miss = false
    return m
end

local function take_heard(sig: Signals): boolean
    local h = sig.heard
    sig.heard = false
    return h
end

local function signals_off(sig: Signals)
    for _, c in sig.conns do c:off() end
    sig.conns = {}
end

local function range(a: number, b: number): { number }
    local out = {}
    for n = a, b do table.insert(out, n) end
    return out
end

local function concat(...: { number }): { number }
    local out = {}
    for _, l in { ... } do
        for _, n in l do table.insert(out, n) end
    end
    return out
end

-- ------------------------------------------------------------------ generic guards (Characters, rule 1)

local GRENADES: { [number]: number } = {
    [0x47] = 229, [0x36] = 25, [0x1D] = 25, [0x27] = 25, [0x22] = 25, [0x2C] = 25, [0x31] = 25,
    [0x30] = 229, [0x39] = 229, [0x46] = 229,
}
for n, v in GRENADES do
    local g: Target = chr(n)
    if g:exists() then g:set_stats({ grenade_chance = L.grenade_pct(v) }) end
end

-- rule 1: boot() runs the setup's generic guards; one the setup left on a level list gets its mode here
local function ensure_mode(list: { number }, mode: string)
    for _, g in chrs(list) do
        if guard_ai.mode(g) == nil then guard_ai.start(g, mode) end
    end
end
ensure_mode(concat(range(0x00, 0x02), range(0x05, 0x08), range(0x0A, 0x0D), { 0x0F, 0x12 }, range(0x14, 0x17),
    range(0x19, 0x1C), { 0x29, 0x2A, 0x3B, 0x3D, 0x3E }, range(0x3F, 0x43), { 0x47 }), "standard")
ensure_mode({ 0x28, 0x35, 0x36, 0x37, 0x38 }, "simple_deaf")

-- the three patrols: walk the path, then the simple guard
for n, path in { [0x10] = 4, [0x11] = 5, [0x45] = 6 } do
    local g: Target = chr(n)
    if g:exists() then guard_ai.start(g, "simple", { patrol = path }) end
end

-- ------------------------------------------------------------------ waiting and armoured guards (rules 2-3)

local function waiting_guard(g: Target, armoured: boolean)
    guard_ai.stop(g)
    local sig = signals(g)
    if armoured then
        g:set_stats({ armour = 8.0 })                      -- rule 3
    end
    g:halt()
    local missed = false                                   -- a shot landed near it (stays true)
    while g:exists() do
        missed = missed or take_miss(sig)
        local heard = take_heard(sig)
        local alerted = take_witness(sig) ~= nil           -- saw another guard shot or killed
            or missed
            or g:line_to_player() ~= nil
            or g:is_aimed_at()
            or (armoured and heard)                        -- rule 3: also hears the player
        -- the spec's 20% per check: per tick, which a 0.25 s poll turns into near certainty
        if alerted and L.tick_chance(20, STEP) then
            signals_off(sig)
            if armoured then
                guard_ai.start(g, "hunter")                -- rule 3: persistent chase for the rest of the level
            else
                guard_ai.start(g, "chase", { fallback = "standard" })   -- rule 2: run to Bond, then standard
            end
            return
        end
        wait(STEP)
    end
    signals_off(sig)
end

for _, g in chrs(concat(range(0x1E, 0x26), range(0x2B, 0x2F), range(0x31, 0x34), { 0x3A, 0x44 })) do
    spawn(function() waiting_guard(g, false) end)          -- rule 2
end
for _, g in chrs({ 0x03, 0x04, 0x09, 0x0E, 0x13, 0x18, 0x1D, 0x27, 0x3C, 0x48 }) do
    spawn(function() waiting_guard(g, true) end)           -- rule 3
end

-- ------------------------------------------------------------------ grenadiers (rule 4)

local function grenadier(g: Target)
    guard_ai.stop(g)
    local sig = signals(g)
    g:halt()
    local function in_sight(): boolean
        return g:sees_player() or g:line_to_player() ~= nil
    end
    while g:exists() do
        if take_witness(sig) ~= nil or take_miss(sig) or in_sight() then
            -- rule 4: attacks from where it stands while it keeps the player in sight
            while g:exists() and in_sight() do
                if not g:is_busy() then
                    local r = math.random() * 100
                    if r < 59 then
                        g:fire_at(nil, { stance = "stand" })
                    elseif r < 98 then
                        g:fire_at(nil, { stance = "kneel" })
                    else
                        local d = g:distance()
                        if not (d >= 800 and d <= 3000 and g:throw_grenade()) then
                            g:fire_at(nil, { stance = "kneel" })
                        end
                    end
                end
                wait(STEP)
            end
            take_miss(sig)                                 -- the shots of this attack are behind it
            take_witness(sig)
        end
        wait(STEP)
    end
    signals_off(sig)
end
for _, g in chrs({ 0x30, 0x39, 0x46 }) do
    spawn(function() grenadier(g) end)
end

-- ------------------------------------------------------------------ reinforcements (rules 17-18)

local REINFORCEMENTS = { { chr = 0x59, pad = 358 }, { chr = 0x5A, pad = 292 }, { chr = 0x5B, pad = 307 } }
local reinforcement: { [number]: Target? } = {}

-- rule 18: the kit, then run to Bond (chase), then the standard guard
local function kit(g: Target)
    local function second(item: number, model: number)
        if L.chance(39) then g:give_weapon(item, { model = model, left_hand = true }) end
    end
    if clock() < 120 or L.chance(21) then
        g:set_stats({ max_health = 6.0, health = 6.0 })
        g:give_hat(0xDC)
        g:give_weapon(0x10, { model = 0xCF })
    elseif L.chance(39) then
        g:set_stats({ max_health = 12.0, health = 12.0 })
        g:give_hat(0xDD)
        g:give_weapon(0x0E, { model = 0xC5 })
        second(0x0E, 0xC5)
    else
        g:set_stats({ max_health = 8.0, health = 8.0 })
        g:give_hat(0xDE)
        g:give_weapon(0x09, { model = 0xC3 })
        second(0x09, 0xC3)
    end
    guard_ai.start(g, "chase", { fallback = "standard" })
end

-- rule 17
local function start_reinforcements()
    tag(35):open()
    tag(36):open()
    tag(37):open()
    spawn(function()
        while true do
            if not L.any_in_cutscene() then
                for _, r in REINFORCEMENTS do
                    if L.departed(reinforcement[r.chr]) then
                        -- head omitted: the game picks one at random
                        local g = spawn_guard({ chr = r.chr, pad = r.pad, body = 4, sunglasses = true })
                        if g then
                            reinforcement[r.chr] = g
                            kit(g)                         -- rule 18
                        end
                    end
                end
            end
            wait(40)
        end
    end)
end

-- ------------------------------------------------------------------ Trevelyan (rules 5-10)

local ROUTE = {   -- rule 6: the room he reaches, the doors he shuts behind him, where he heads next
    { room = 193, doors = { 25, 16 } },
    { room = 211, doors = { 26, 17 } },
    { room = 222, doors = { 27, 18 } },
    { room = 263, doors = { 28, 19 } },
    { room = 272, doors = { 29, 20 } },
    { room = 345, doors = { 30, 21 } },
    { room = 359, doors = { 32, 23, 36, 37 } },
    { room = 361, doors = {} },
}

local function run_until(g: Target, pad_n: number, speed: string, done: () -> boolean)
    g:run_to(pad_n, speed)
    wait(STEP)
    while g:exists() and not done() do
        if not g:is_moving() then g:run_to(pad_n, speed) end   -- stopped short: sprints on
        wait(STEP)
    end
end

local function trevelyan(t: Target)
    guard_ai.stop(t)
    -- rule 5: unkillable (the setup's invincible flag and very high health, set again here)
    t:set_flag("invincible", true)
    t:set_flag("no_autoaim", true)
    t:set_stats({ grenade_chance = L.grenade_pct(255) })
    -- rule 6: waits for a first clear line to a player
    while t:line_to_player() == nil do wait(STEP) end
    for _, step in ROUTE do
        run_until(t, step.room, "sprint", function() return t:in_room_of(step.room) end)
        for _, d in step.doors do tag(d):close() end
    end
    -- rule 7: to the ledge, then a line to the player: the taunt and the reinforcements
    t:run_to(360, "sprint")
    wait(STEP)
    while t:distance_to(pad(360)) > 500 and t:is_moving() do wait(STEP) end
    while t:line_to_player() == nil do wait(STEP) end
    L.top("1C0A")
    start_reinforcements()                                 -- rule 17
    -- rule 8: one attack from the ledge
    if L.chance(79) then
        if not t:fire_at(nil, { stance = "kneel" }) then t:throw_grenade() end
    else
        t:throw_grenade()
    end
    wait_until_still(t)
    -- rule 9
    if t:distance() < 1000 or not tag(34):is_destroyed() then
        tag(33):open()
        tag(24):open()
    end
    -- rule 10: leaves
    tag(33):close()
    tag(24):close()
    t:run_to(367, "sprint")
    wait(STEP)
    while t:distance_to(pad(367)) > 100 and t:is_moving() do wait(STEP) end
    t:fade_out()
end

do
    local t: Target = chr(0x58)
    if t:exists() then spawn(function() trevelyan(t) end) end
end

-- ------------------------------------------------------------------ scientists (rules 11-16)

local SCIENTISTS = { 0x49, 0x4A, 0x4B, 0x54, 0x55 }

local function scientist(s: Target)
    guard_ai.stop(s)
    s:set_flag("civilian", true)                           -- rule 16
    local sig = signals(s)
    local hits = s:hits()
    local hit_pending = false                              -- rule 11: hit, waiting for him to stop

    local function near_miss(): boolean
        return take_miss(sig)
    end
    local function was_hit(): boolean
        if s:hits() > hits then
            hits = s:hits()
            return true
        end
        return false
    end

    -- rule 14: returns "gone" when he got away
    local function flee(): string
        s:drop_items()
        s:run_to(368, "run")
        wait(STEP)
        while s:exists() do
            if near_miss() then                            -- hands up a moment, then runs on
                s:raise_arms()
                wait(STEP)
                while s:is_busy() and take_witness(sig) ~= "death" and not L.tick_chance(14, STEP) do wait(STEP) end
                s:run_to(368, "run")
                wait(STEP)
            elseif s:distance_to(pad(368)) <= 500 then
                if not s:on_screen() then                  -- no player sees him: he got away (no kill)
                    s:fade_out()
                    return "gone"
                end
                return "back"                              -- arrived seen
            elseif not s:is_moving() then
                return "back"                              -- stopped elsewhere
            else
                wait(STEP)
            end
        end
        return "gone"
    end

    -- rule 13: returns the next state
    local function cower(): string
        s:surrender()
        while s:exists() do
            if was_hit() then hit_pending = true end
            if s:hits() > 1 then
                guard_ai.start(s, "draw_and_fire")         -- hit more than once: pistol, then chase, then standard
                return "ai"
            end
            local held = s:distance() <= 2000 and (s:is_aimed_at() or s:line_to_player() ~= nil)
            if not held or near_miss() or L.tick_chance(0.8, STEP) then
                if flee() == "gone" then return "gone" end
                s:surrender()                              -- and cowers again
            end
            wait(STEP)
        end
        return "gone"
    end

    -- rule 15
    local function dodge(): string
        while s:exists() do
            wait_until_still(s)
            if L.chance(41) then s:combat_move("sidestep") else s:combat_move("side_run") end
            wait_until_still(s)
            local behind = math.abs(s:angle_to_player()) > 155 and s:distance() > 100
            if not (s:sees_player() and not behind) then return "calm" end
        end
        return "gone"
    end

    -- "as when he sees a guard die" (rule 11): 25% flee, re-rolled each check
    local function as_guard_died(): string?
        if L.chance(25) then
            if flee() == "gone" then return "gone" end
        end
        return nil
    end

    -- rules 11-12: calm; the spec does not order the checks: shots first, then sight, then hearing
    local function calm(): string
        s:halt()
        take_heard(sig)                                    -- shots heard before he calmed down are past
        while s:exists() do
            if was_hit() then hit_pending = true end
            local w = take_witness(sig)
            local heard = take_heard(sig)                  -- this check only (a shot heard earlier is past)
            if near_miss() or w == "hit" then
                return "cower"                             -- rule 11
            end
            if w == "death" or (hit_pending and not s:is_moving() and not s:is_busy()) then
                hit_pending = false
                if as_guard_died() == "gone" then return "gone" end   -- rule 11
            end
            local d = s:distance()
            local sees = s:sees_player()
                or (s:line_to_player() ~= nil and math.abs(s:angle_to_player()) <= 90)
                or d <= 100
            if sees and (L.chance(22) or s:is_aimed_at()) then   -- rule 12
                if d <= 200 then
                    return "cower"
                elseif s:hits() > 0 then
                    if as_guard_died() == "gone" then return "gone" end
                else
                    local nxt = dodge()                    -- rule 15
                    if nxt == "gone" then return "gone" end
                end
            elseif (heard or s:alerted_within(10)) and not s:is_busy() and L.chance(75) then
                s:combat_move("side_run")                  -- rule 11: a short run to the side
                wait_until_still(s)
            end
            wait(STEP)
        end
        return "gone"
    end

    local state = "calm"
    while state ~= "gone" and state ~= "ai" and s:exists() do
        if state == "calm" then state = calm() else state = cower() end
    end
    signals_off(sig)
end

for _, s in chrs(SCIENTISTS) do
    spawn(function() scientist(s) end)
end

-- rule 16: more than 2 civilians killed (checked at each death)
local casualties_failed = false
on("guard_died", function()
    if casualties_failed or mission.civilians_killed() <= 2 then return end
    casualties_failed = true
    L.top("1C0D")
    objectives.fail("e")
end)

-- ------------------------------------------------------------------ pump controls (objectives a-c)

local INLET = { 0, 1, 2, 3 }
local OUTLET = { 4, 5, 6, 7 }
local function all_destroyed(tags: { number }): boolean
    for _, t in tags do
        if not tag(t):is_destroyed() then return false end
    end
    return true
end
-- the objectives are declared by this script, so their destroy conditions are checked here
on("destroyed", function(ev)
    if all_destroyed(INLET) and not objectives.is_complete("a") then objectives.complete("a") end
    if all_destroyed(OUTLET) and not objectives.is_complete("b") then objectives.complete("b") end
    if tag(8):is_destroyed() and not objectives.is_complete("c") then objectives.complete("c") end
end)

-- ------------------------------------------------------------------ radio (rules 19-20)

local RADIO = { [9] = true, [10] = true, [11] = true, [12] = true }
local radio_done = false
for t, _ in RADIO do tag(t):make_usable() end

local function radio_broken(): boolean
    for t, _ in RADIO do
        if tag(t):is_destroyed() then return true end
    end
    return false
end

on("activated", function(ev)                              -- rule 19: any player
    if radio_done or ev.tag == nil or not RADIO[ev.tag] or radio_broken() then return end
    radio_done = true
    sound.play(0x50, { from = tag(9), channel = 0 })
    wait(1)
    L.top("1C0C")
    objectives.complete("d")
end)

on("destroyed", function(ev)                              -- rule 20
    if radio_done or ev.tag == nil or not RADIO[ev.tag] then return end
    radio_done = true
    objectives.fail("d")
    L.top("1C0B")
end)

-- ------------------------------------------------------------------ start-room music (Objects and world)

local function start_room_music()                          -- from the end of the intro swirl (below)
    local playing = false
    while true do
        local inside = player_in_room(368) ~= nil          -- co-op: while any player is in the start room
        if inside and not playing then
            music.play_extra(1)
            playing = true
        elseif not inside and playing then
            music.stop_extra(1)
            playing = false
        end
        wait(STEP)
    end
end

-- ------------------------------------------------------------------ the intro (Cinematics)
-- Played by the shared intro module from caverns_intro_data (one of the four fixed cameras with 1C19, the swirl,
-- the body's animation 0x62); the start-room music runs from the hand-over. Alongside the rest of the level.
spawn(function()
    require("intro").play(require("caverns_intro_data"))
    start_room_music()
end)

-- ------------------------------------------------------------------ exit and outro (rule 21)


local function outro(p: number)
    cinema.hide_all_chrs(true)
    L.end_on_button()
    tag(38):open_now()
    tag(39):open_now()
    local pl = player(p)
    local body = bond_body(p)
    if not L.is_real_weapon(pl:held_item()) then   -- rule 21: unarmed, fist, watch laser or a gadget
        body:hold_item(4, "right")                         -- shown holding the Walther PPK
    end
    pl:hide_weapon(true)
    camera.fixed(tag(40))
    -- step 1
    body:show(true)
    body:teleport(369)
    body:hold_item(0, "left")                              -- left hand emptied (item 0)
    screen.fade_in()
    -- TODO(api): "up to 255 s": stop_after is read as the play length.
    music.play_extra(0, { stop_after = 255 })
    -- times counted by waits: the mission clock is stopped
    wait(1)
    if L.ending() then return end
    body:anim(0x65, { from = 8, to = 109, blend = 0.27 })  -- step 2
    wait(0.9)
    if L.ending() then return end
    body:hold_item(0, "right")                             -- step 3: right hand emptied
    wait(2.0)
    if L.ending() then return end
    tag(38):close()                                        -- step 4
    while tag(38):state() ~= "closed" and not L.ending() do wait(STEP) end
    L.end_level()
end

-- rule 21; co-op: the first living player to reach the lift ends it for everyone
spawn(function()
    while true do
        for _, p in L.players() do
            if L.alive(p) and player(p):in_room_of(369) then
                if not objectives.is_failed("e") then objectives.complete("e") end
                cinema.begin()                             -- invulnerable, no pickups, HUD hidden, no control, clock stopped
                screen.fade_out()
                L.wait_fade({ settle = 0 })
                if not objectives.all_complete() then
                    mission.finish()                       -- the game's normal end: a failure
                else
                    outro(p)
                end
                return
            end
        end
        wait(STEP)
    end
end)
