-- facility: the Facility level (specs/facility.md): characters, objectives a-e, doors / panels / screens (rules 3-7),
-- scientists (rules 8-14, facility_scientist), Doak (rules 15-17), the corridor ambushes (rules 18-22), the
-- bottling room (rules 23-35, facility_bottling) and the exit with its two exit scenes (rules 36-38).
local guard_ai = require("guard_ai")
guard_ai.boot()

local U = require("campaign_lib")
local Sig = require("facility_signals")   -- the guard events, latched (see its header)
local S = require("facility_scientist")
local B = require("facility_bottling")

local st = require("facility_util").state

local DECODER = 38
local AK47 = 8
local AK47_MODEL = 184
local HAT_MODEL = 217
local BODY_SOLDIER = 2
local BODY_SCIENTIST = 35
local FLEE_PAD = 0x17
local diff = mission.difficulty()
local STEP: number = U.STEP

-- ============================================================ objectives
objectives.add("a", text("0C30") or "", 0)
objectives.add("b", text("0C31") or "", 1)
objectives.add("c", text("0C32") or "", 0)
objectives.add("d", text("0C33") or "", 0)
objectives.add("e", text("0C34") or "", 0)

-- ============================================================ start (rules 1-2)
-- rules 1-2: the start pads (slots 0-3), the kit, outfit and watch time stay the setup's (mission.json
-- "level_intro": false removes only its intro cameras and swirl); the intro (Cinematics, Intro) is played by the
-- shared intro module from facility_intro_data, alongside the rest of the level (as the setup's own intro is).
spawn(function()
    require("intro").play(require("facility_intro_data"))
end)

-- ============================================================ characters
local function start_all(list: { number }, mode: string, grenade3: { number }?)
    for _, n in list do
        local g = chr(n)
        if g:exists() then
            if grenade3 and table.find(grenade3, n) then
                g:set_stats({ grenade_chance = 3 })
            end
            guard_ai.start(g, mode)
        end
    end
end

-- Characters: standard and deaf simple soldiers on their pads
start_all({ 0x00, 0x01, 0x03, 0x04, 0x0C, 0x0D, 0x0F, 0x10, 0x12, 0x13, 0x15, 0x16, 0x18, 0x19, 0x1B, 0x1C,
    0x1E, 0x1F }, "standard", { 0x00, 0x04, 0x13, 0x18 })
start_all({ 0x02, 0x0E, 0x11, 0x14, 0x17, 0x1A, 0x1D, 0x23, 0x24 }, "simple_deaf", { 0x0E, 0x1D })

-- Characters: patrols, then the simple generic guard
local PATROLS = { { 0x2C, 0 }, { 0x2D, 1 }, { 0x2F, 3 }, { 0x30, 3 }, { 0x31, 4 }, { 0x32, 4 }, { 0x27, 5 },
    { 0x28, 6 } }
for _, p in PATROLS do
    local g = chr(p[1])
    if g:exists() then
        if p[1] == 0x2C or p[1] == 0x30 or p[1] == 0x27 then
            g:set_stats({ grenade_chance = 3 })
        end
        guard_ai.start(g, "simple", { patrol = p[2] })
    end
end

-- ============================================================ doors, locks and screens (rules 3-7)
local decoder_door = tag(0x26)

-- rule 3: on Agent the decoder door is an ordinary door
if diff == 0 then
    decoder_door:unlock(true)
end

-- rule 4: the door decoder used on door 0x26 (works every time)
on("gadget_used", function(ev)
    sound.play(0xB8, { from = decoder_door })
    wait(3)
    sound.play(0x53, { from = decoder_door })
    U.bottom("0C29")
    -- TODO(api): assumes open() opens a door that keeps its decoder lock (bit 0x08); the spec keeps the lock so
    -- the decoder works every time.
    decoder_door:open()
end, { item = DECODER, tag = 0x26 })

-- rule 5: the door panels (their engine link opens the door)
for _, t in { 0x0B, 0x0C, 0x0D } do
    on("activated", function()
        U.bottom("0C27")
    end, { tag = t })
end

-- rule 6: the first small bottling tank destroyed seals door 0x20 for the rest of the level ("destroyed" events)
local sealed = false
local function seal()
    if sealed then
        return
    end
    sealed = true
    U.top("0C28")
    sound.play(0xA1)
    local d = tag(0x20)
    d:close()
    d:lock()                         -- a lock no key opens
    d:set_passable(false)            -- one-way on both sides
    d:set_usable_by_guards(false)    -- characters cannot operate it
end
for t = 0x17, 0x1F do
    on("destroyed", seal, { tag = t })
end

-- rule 7: door screens follow their doors
type ScreenRule = { screens: { number }, door: number, images: { [string]: { number } } }
local PANEL = { closed = { 0x2E }, closing = { 0x2E }, opening = { 0x2F }, open = { 0x2F } }
local SCREEN_RULES: { ScreenRule } = {
    { screens = { 0x0B }, door = 0x27, images = PANEL },
    { screens = { 0x0C }, door = 0x20, images = PANEL },
    { screens = { 0x0D }, door = 0x29, images = PANEL },
    { screens = { 0x2A, 0x2B }, door = 0x20,
        images = { closed = { 0x31 }, closing = { 0x2E }, opening = { 0x2F, 1 }, open = { 0x32 } } },
    { screens = { 0x2C, 0x2D }, door = 0x21,
        images = { closed = { 0x31 }, closing = { 0x2E }, opening = { 0x2F }, open = { 0x32, 1 } } },
}
spawn(function()
    local last: { [number]: string } = {}
    while true do
        for i, r in SCREEN_RULES do
            -- all screens start on the closed image
            local s = if last[i] == nil then "closed" else tag(r.door):state()
            if s ~= last[i] then
                last[i] = s
                local img = r.images[s]
                if img then
                    for _, t in r.screens do
                        tag(t):show_screen(img[1], img[2])
                    end
                end
            end
        end
        wait(STEP)
    end
end)

-- ============================================================ scientists (rules 8-14)
local SCI_OPTS = { flee_pad = FLEE_PAD }
local KEYBOARD_OPTS = { flee_pad = FLEE_PAD, keyboard = true }

-- rule 13: civilians (Doak is marked in rule 15)
for n = 0x33, 0x42 do
    if n ~= 0x38 then
        local g = chr(n)
        if g:exists() then
            g:set_flag("civilian", true)
            g:set_stats({ grenade_chance = 25 })   -- Characters: scientists' 25% grenade chance
        end
    end
end
for n = 0x33, 0x37 do   -- rule 8: keyboard workers
    if chr(n):exists() then
        S.start(chr(n), KEYBOARD_OPTS)
    end
end
for n = 0x3A, 0x41 do   -- rule 10: standing scientists
    if chr(n):exists() then
        S.start(chr(n), SCI_OPTS)
    end
end
if chr(0x42):exists() then   -- rule 9
    S.start_patrol(chr(0x42), 2, SCI_OPTS)
end
if chr(0x39):exists() then   -- rule 14 (carries keycard A: setup data)
    S.start_late_runner(chr(0x39), 0x0B, 240, SCI_OPTS)
end

-- rule 13: more than 2 civilians dead fails objective e. Every death counts ("guard_died"), not only the
-- players' kills that mission.civilians_killed() counts.
local CIVILIANS: { [number]: boolean } = { [0x4F] = true }   -- Doak counts himself (rule 15)
for n = 0x33, 0x42 do
    if n ~= 0x38 then
        CIVILIANS[n] = true
    end
end
local civilians_dead = 0
on("guard_died", function(ev: MissionEvent)
    local c = ev.chr
    if c == nil or not CIVILIANS[c] then
        return
    end
    CIVILIANS[c] = false   -- counted once
    civilians_dead += 1
    if civilians_dead == 3 and not objectives.is_complete("e") then
        U.top("0C2A")
        objectives.fail("e")
    end
end)

-- ============================================================ objective a
spawn(function()
    while U.player_in_room(0x56) == nil do
        wait(STEP)
    end
    objectives.complete("a")
end)

-- ============================================================ objective d
-- every gas tank tag 0x01..0x0A destroyed ("destroyed" events)
local function tanks_done()
    for t = 0x01, 0x0A do
        if not tag(t):is_destroyed() then
            return
        end
    end
    if not objectives.is_complete("d") then
        objectives.complete("d")
    end
end
for t = 0x01, 0x0A do
    on("destroyed", tanks_done, { tag = t })
end

-- ============================================================ Dr. Doak (rules 15-17)
local DOAK_PADS = { 0xEE, 0xEF, 0xF0, 0xF1, 0xF2, 0xF3, 0xF4 }
local decoder_given = false

local function doak_spawn(): Target?
    -- rule 15: 14% each for the first six pads, 17% for 0xF4; a blocked pad tries the next ones in order
    local roll = math.random() * 101
    local first = math.min(math.floor(roll / 14) + 1, #DOAK_PADS)
    for k = 0, #DOAK_PADS - 1 do
        local p = DOAK_PADS[(first - 1 + k) % #DOAK_PADS + 1]
        local g = spawn_guard({ chr = 0x4F, pad = p, body = BODY_SCIENTIST, head = 51, force = true })
        if g then
            return g
        end
    end
    return nil
end

spawn(function()
    -- rule 15: a player first enters the room of pad 0x0B
    while U.player_in_room(0x0B) == nil do
        wait(STEP)
    end
    local doak = doak_spawn()
    if doak == nil then
        return
    end
    local d = doak :: Target
    d:set_flag("civilian", true)
    d:set_flag("no_autoaim", true)
    d:give_object(tag(0x00))   -- rule 15: he holds the door decoder (tag 0x00)

    -- rule 17: Doak dies before the handover
    on("guard_died", function()
        if not decoder_given and diff >= 2 then
            U.top("0C47")
            objectives.fail("b")
        end
    end, { chr = 0x4F })

    -- rule 16: waits for a line of sight, then walks to the player
    while not d:is_dead() and d:line_to_player() == nil do
        wait(STEP)
    end
    while not d:is_dead() and d:distance() >= 200 do
        if not d:is_busy() then
            d:run_to_player("walk")
        end
        wait(STEP)
    end
    if d:is_dead() then
        return
    end
    d:halt()
    local met = U.nearest_player(d)
    local t0 = clock()
    U.top("0C11")
    while clock() - t0 < 5 and not d:is_dead() do
        d:face_player()
        wait(STEP)
    end
    if d:is_dead() then
        return
    end
    U.top("0C12")
    d:remove_held_items()
    -- the met player (the nearest one) silently receives the decoder
    give(DECODER, met, { silent = true })
    decoder_given = true
    objectives.complete("b")
    while clock() - t0 < 8 and not d:is_dead() do
        d:face_player()
        wait(STEP)
    end
    if not d:is_dead() then
        S.flee(d, nil, SCI_OPTS, true)   -- rule 16: he keeps fleeing until he is gone
    end
end)

-- ============================================================ corridor ambushes (rules 18-22)
-- rule 18: the waiting soldiers get no generic behaviour until their trigger fires (boot left them inert).

-- rule 21: stand still until a line of sight, then fire on the move, then the standard guard.
local function fire_on_the_move(g: Target)
    if not g:combat_move("walk_fire") then
        g:combat_move("run_fire")
    end
    wait(STEP)
    U.wait_idle(g, 3, { alive = U.not_dead })
end

local function waiting_soldier(g: Target, on_sight: (() -> ())?)
    guard_ai.stop(g)
    spawn(function()
        while not g:is_dead() and g:line_to_player() == nil do   -- rules 19, 21
            wait(STEP)
        end
        if g:is_dead() then
            return
        end
        if on_sight then
            on_sight()
        end
        fire_on_the_move(g)
        if not g:is_dead() then
            guard_ai.start(g, "standard")
        end
    end)
end

-- rule 20: the four extra soldiers, once for the level
local extras_sent = false
local function send_extras()
    if extras_sent or st.exiting or mission.exiting() then
        return
    end
    extras_sent = true
    for _, p in { 259, 260, 261, 262 } do
        local g = spawn_guard({ pad = p, body = BODY_SOLDIER })   -- only when the pad is out of view
        if g then
            local e = g :: Target
            e:give_weapon(AK47, { model = AK47_MODEL })
            e:give_hat(HAT_MODEL)                           -- rule 20: hat model 217, the squad's
            spawn(function()
                -- waits until it sees the player, or at random (about 1 s on average) runs to pad 0x3F first
                while not e:is_dead() and Sig.spotted(e) == nil do
                    if U.chance(100 * STEP) then
                        e:run_to(0x3F, "run")
                        wait(STEP)
                        U.wait_idle(e, 20, { alive = U.not_dead })
                        while not e:is_dead() and e:line_to_player() == nil do
                            wait(STEP)
                        end
                        break
                    end
                    wait(STEP)
                end
                if e:is_dead() then
                    return
                end
                e:combat_move("run_fire")
                wait(STEP)
                U.wait_idle(e, 3, { alive = U.not_dead })
                guard_ai.start(e, "engage")   -- attack, then the standard guard
            end)
        end
    end
end

for _, n in { 0x2A, 0x2B } do   -- rules 19-20
    local g = chr(n)
    if g:exists() then
        if n == 0x2A then
            g:set_stats({ grenade_chance = 3 })
        end
        waiting_soldier(g, send_extras)
    end
end
for _, n in { 0x21, 0x22, 0x25, 0x26, 0x2E } do   -- rule 21 (0x25 carries keycard B: setup data)
    local g = chr(n)
    if g:exists() then
        if n == 0x22 or n == 0x2E then
            g:set_stats({ grenade_chance = 3 })
        end
        waiting_soldier(g, nil)
    end
end

-- rule 22: soldiers watching a room
local IDLE_PER_POLL = U.per_poll(0.8)
local function watcher(g: Target, watch: number)
    guard_ai.stop(g)
    spawn(function()
        local misses = g:near_misses()
        while not g:is_dead() do
            local m = g:near_misses()
            local shot = m > misses or Sig.take_miss(g)
            misses = m
            local seen = Sig.take_witnessed(g)
            if not g:is_busy() then
                local mode: string? = nil
                if Sig.spotted(g) ~= nil then
                    mode = "engage"
                elseif U.player_in_room(watch) ~= nil or g:alerted_within(10) or seen ~= nil then
                    mode = "chase"
                elseif g:heard_shot_within(10) then
                    mode = "reinforce"
                elseif shot then
                    mode = if U.chance(50) then "startle_chase" else "engage"
                elseif g:is_idle() and U.chance(IDLE_PER_POLL) then
                    mode = "fidget"
                end
                if mode then
                    if not U.run_generic(g, mode) then
                        return   -- dead: does nothing
                    end
                end
            end
            wait(STEP)
        end
    end)
end

local WATCHERS = { { 0x05, 0x1D }, { 0x06, 0x1D }, { 0x07, 0x1D }, { 0x08, 0x39 }, { 0x0A, 0x39 }, { 0x09, 0x3B },
    { 0x0B, 0x3B } }
for _, w in WATCHERS do
    local g = chr(w[1])
    if g:exists() then
        if w[1] == 0x09 then
            g:set_stats({ grenade_chance = 3 })
        end
        watcher(g, w[2])
    end
end

-- ============================================================ bottling room (rules 23-35)
B.start()

-- ============================================================ the exit (rules 36-38)
local GUN_ITEMS: { [number]: boolean } = {}
for i = 4, 22 do
    GUN_ITEMS[i] = true
end
GUN_ITEMS[24] = true
GUN_ITEMS[25] = true

local ended = false
local function end_level()
    if not ended then
        ended = true
        mission.win()
    end
end

local function exit_scene(p: number, room: number)
    -- step 1: black, every character hidden, any button skips
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
    -- step 2: the body holds the silenced PP7 unless the player holds a gun; no first-person weapon
    local body = bond_body(p)
    for i = 0, U.MAX_PLAYERS - 1 do
        if U.in_game(i) then
            player(i):hide_weapon(true)
        end
    end
    if not GUN_ITEMS[player(p):held_item()] then
        body:hold_item(5, "right")
    end
    body:show(true)
    if room == 0x87 then
        -- step 3
        camera.fixed(tag(0x24))
        tag(0x22):open_now()
        tag(0x23):open_now()
        body:teleport(0x86)
        body:anim(0x6B, { hold = true })
        local t0 = clock()
        screen.fade_in(1)
        wait(math.max(0, 1 - (clock() - t0)))
        body:anim(0x68, { from = 0 })
        wait(STEP)
        while not body:anim_done() and not ended do
            wait(0.1)
        end
    else
        -- step 4
        camera.fixed(tag(0x25))
        body:teleport(0x136)
        body:anim(0x62, { from = 127, reverse = true })
        local t0 = clock()
        screen.fade_in(1)
        local holstered = false
        while clock() - t0 < 3.2 and not body:anim_done() and not ended do
            if not holstered and clock() - t0 >= 3.1 then
                body:hold_item(0, "right")                  -- puts away the right-hand weapon (item 0)
                holstered = true
            end
            wait(0.05)
        end
    end
    if not ended then
        U.fade_out(1, { poll = 0.1 })
    end
    end_level()
end

spawn(function()
    -- rule 36: a living player enters the room of pad 0x135 or 0x87
    local p: number?, room = nil, 0
    while p == nil do
        p = U.player_in_room(0x135)
        room = 0x135
        if p == nil then
            p = U.player_in_room(0x87)
            room = 0x87
        end
        if p == nil then
            wait(STEP)
        end
    end
    st.exiting = true
    if not objectives.is_failed("e") then
        objectives.complete("e")
    end
    cinema.begin()   -- invulnerable, no pickups, HUD hidden, controls locked
    U.fade_out(1, { poll = 0.1 })
    if not objectives.all_complete() then
        mission.fail()   -- rule 37
        return
    end
    exit_scene(p :: number, room)   -- rule 38
end)
