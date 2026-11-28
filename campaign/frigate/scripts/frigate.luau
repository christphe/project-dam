-- frigate: the whole Frigate level (specs/frigate.md).
-- Covers: the sailors (generic guards), the six hostage takers and their hostages (rules 1-8), the two bombs
-- (rules 9-11), the tracker bug on the helicopter (rule 12), the engine room sound (rules 13-14), the exit and
-- the outro (rule 15), the helicopter at rest (Objects and world), the intro (Cinematics: staged here, mission.json
-- has "level_intro": false). Bond's start items (and the slot-1 start on pad 139) are setup data: no script.

local guard_ai = require("guard_ai")
local L = require("campaign_lib")
guard_ai.boot()

-- Objectives (declared first: the tasks below complete and fail them)
objectives.add("a", text("3414") or "", 0)
objectives.add("b", text("3415") or "", 1)
objectives.add("c", text("3416") or "", 1)
objectives.add("d", text("3417") or "", 0)

local STEP = 0.25
local DIFF = math.max(mission.difficulty(), 0)   -- 0 Agent .. 3 007

-- ------------------------------------------------------------------ helpers


local function any_in_room(pads: { number }): boolean
    for _, p in pads do
        if player_in_room(p) ~= nil then
            return true
        end
    end
    return false
end


-- ------------------------------------------------------------------ sailors (Characters table)

local STANDARD = { 0x00, 0x02, 0x04, 0x08, 0x0A, 0x0C, 0x0E, 0x10, 0x12, 0x1E, 0x20, 0x22, 0x24, 0x26, 0x28, 0x2A }
local DEAF = { 0x03, 0x05, 0x0B, 0x0D, 0x11, 0x13, 0x15, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1D, 0x1F, 0x21, 0x23,
    0x25, 0x27, 0x29 }
local GRENADE_25 = { [0x10] = true, [0x22] = true, [0x1A] = true }

-- boot() already runs the setup's generic guards; a guard the setup left on a level list gets its mode here
local function ensure_mode(n: number, mode: string)
    local g: Target = chr(n)
    if not g:exists() then
        return
    end
    if GRENADE_25[n] then
        g:set_stats({ grenade_chance = L.grenade_pct(25) })
    end
    if guard_ai.mode(g) == nil then
        guard_ai.start(g, mode)
    end
end
for _, n in STANDARD do ensure_mode(n, "standard") end
for _, n in DEAF do ensure_mode(n, "simple_deaf") end

-- ------------------------------------------------------------------ hostages (rules 1-8)

-- hostage chr -> captor chr
local HOSTAGES = { [0x2C] = 0x01, [0x2D] = 0x09, [0x30] = 0x0F, [0x31] = 0x16, [0x34] = 0x14, [0x35] = 0x1C }
local ESCAPE_PADS = { { 143, 16 }, { 145, 16 }, { 147, 16 }, { 148, 16 }, { 168, 16 }, { 169, 21 } }
local COUNTDOWN = { 3, 2.5, 2, 2 }        -- rule 1, per difficulty
local MAX_DEAD = { 4, 2, 1, 1 }           -- rule 7, per difficulty
local PRE_ESCAPED = { 3, 1, 0, 0 }        -- rule 8, per difficulty
local ESCAPES_NEEDED = 5

local escapes = PRE_ESCAPED[DIFF + 1]     -- rule 8
local hostage_deaths = 0

-- rule 6: objective a completes on the fifth escape
local function count_escape()
    escapes += 1
    if escapes >= ESCAPES_NEEDED and not objectives.is_failed("a") then
        objectives.complete("a")
    end
end

-- rule 7: deaths by anyone (a captor's shot too, which mission.civilians_killed() would not count)
local function count_hostage_death()
    hostage_deaths += 1
    if hostage_deaths == MAX_DEAD[DIFF + 1] + 1 then
        L.top("341E")
        objectives.fail("a")
    end
end

local function pick_escape_pad(): number
    local total = 0
    for _, e in ESCAPE_PADS do total += e[2] end
    local r = math.random() * total
    for _, e in ESCAPE_PADS do
        r -= e[2]
        if r < 0 then return e[1] end
    end
    return ESCAPE_PADS[#ESCAPE_PADS][1]
end

-- A flag set by one guard event of g (filtered by its chr), with the connection to unsubscribe.
local function flag_on(g: Target, name: string, accept: ((ev: MissionEvent) -> boolean)?): ({ set: boolean }, Connection?)
    local flag = { set = false }
    local n = g.chr
    if n == nil then return flag, nil end
    local conn = on(name, function(ev: MissionEvent)
        if accept == nil or accept(ev) then flag.set = true end
    end, { chr = n })
    return flag, conn
end

-- rules 1-3: the captor
local function captor_task(c: Target, h: Target)
    guard_ai.stop(c)
    c:aim_at(h)                                            -- rule 1: covers his hostage from the start
    local hits0 = c:hits()
    local missed, miss_conn = flag_on(c, "guard_near_miss")
    -- rule 1: a near miss, sight or a clear line to a player starts the countdown
    while true do
        if not c:exists() then break end
        if c:hits() > hits0 then                           -- rule 3: hit before noticing: forgets the hostage
            if miss_conn then miss_conn:off() end
            guard_ai.start(c, "standard")
            return
        end
        if missed.set or c:sees_player() or c:line_to_player() ~= nil then
            break
        end
        wait(STEP)
    end
    if miss_conn then miss_conn:off() end
    if not c:exists() then return end
    local left = COUNTDOWN[DIFF + 1]
    while left > 0 do
        if not c:exists() then return end
        if c:hits() > hits0 then                           -- rule 3: hit during the countdown
            guard_ai.start(c, "standard")
            return
        end
        wait(0.1)
        left -= 0.1
    end
    -- rule 2: shoots his hostage dead, then runs at the player
    if h:exists() then
        c:shoot(h)
    end
    wait(0.3)
    if c:exists() then
        guard_ai.start(c, "chase", { fallback = "standard" })
    end
end

-- rule 6: runs for an escape pad until it gets away unseen; returns true when it escaped
local function flee(h: Target): boolean
    h:drop_items()
    local dest = pick_escape_pad()
    h:run_to(dest, "run")
    local missed, miss_conn = flag_on(h, "guard_near_miss")
    local saw_death, death_conn = flag_on(h, "guard_witnessed", function(ev) return ev.what == "death" end)
    local function done(escaped: boolean): boolean
        if miss_conn then miss_conn:off() end
        if death_conn then death_conn:off() end
        return escaped
    end
    wait(STEP)
    while true do
        if not h:exists() then return done(false) end
        if missed.set then                                 -- rule 6: a shot near him: hands up a moment
            missed.set = false
            h:raise_arms()
            saw_death.set = false
            wait(STEP)
            while h:is_busy() and not saw_death.set and not L.tick_chance(14, STEP) do
                wait(STEP)
            end
            dest = pick_escape_pad()
            h:run_to(dest, "run")
            wait(STEP)
        elseif h:distance_to(pad(dest)) <= 500 then        -- rule 6: arrived
            if h:line_to_player() ~= nil then
                dest = pick_escape_pad()                    -- seen: on to another pad
                h:run_to(dest, "run")
                wait(STEP)
            else
                L.top("340A")
                h:fade_out()
                count_escape()
                return done(true)
            end
        elseif not h:is_moving() then                      -- rule 6: stopped short: another pad
            dest = pick_escape_pad()
            h:run_to(dest, "run")
            wait(STEP)
        else
            wait(STEP)
        end
    end
end

-- rules 4-6: the hostage
local function hostage_task(h: Target, c: Target)
    guard_ai.stop(h)
    h:set_flag("civilian", true)                           -- rule 7
    h:surrender()                                          -- rule 4: hands up
    local noticed = false
    local fleeing: Task? = nil
    local escaped = false
    local died = false
    local n = h.chr
    if n ~= nil then
        on("guard_died", function()                        -- rule 4: his death
            if died or escaped then return end
            died = true
            if noticed then
                L.top("3408")                              -- rule 4: only once he had seen the player
            end
            count_hostage_death()                          -- rule 7
        end, { chr = n })
    end
    while not died and h:exists() do
        if not noticed and (h:sees_player() or h:line_to_player() ~= nil) then
            noticed = true
        end
        if fleeing == nil and L.departed(c) then                 -- rule 5: his captor is gone
            L.top("3409")
            fleeing = spawn(function()
                escaped = flee(h)                          -- rule 6
            end)
        end
        if escaped then return end
        wait(STEP)
    end
end

for hn, cn in HOSTAGES do
    local h: Target = chr(hn)
    local c: Target = chr(cn)
    if h:exists() then
        spawn(function() hostage_task(h, c) end)
    end
    if c:exists() then
        spawn(function() captor_task(c, h) end)
    end
end

-- ------------------------------------------------------------------ bombs (rules 9-11)

local DEFUSER = 0x27
local BOMBS = {
    [1] = { objective = "b", channel = 1, done_text = "340D" },
    [2] = { objective = "c", channel = 2, done_text = "340E" },
}
local defused: { [number]: boolean } = {}

for t, _ in BOMBS do
    tag(t):make_usable()                                   -- rule 9: the bombs react to use
end

local function blow(t: number)
    if not defused[t] and not tag(t):is_destroyed() then
        tag(t):explode()
    end
end

-- rule 10 / 11: a gadget used on a bomb
on("gadget_used", function(ev)
    local t = ev.tag
    if t == nil or BOMBS[t] == nil or defused[t] or tag(t):is_destroyed() then return end
    local b = BOMBS[t]
    if ev.item == DEFUSER or (ev.player ~= nil and player(ev.player):holds(DEFUSER)) then
        defused[t] = true                                  -- rule 10
        sound.play(0xF7, { from = tag(t), channel = b.channel })
        L.top(b.done_text)
        objectives.complete(b.objective)
        tag(t):set_invincible(true)
    else
        blow(t)                                            -- rule 11: the wrong gadget
    end
end)

-- rule 11: pressing use on a bomb sets it off (a defused one is indestructible and stays as it is)
on("activated", function(ev)
    if ev.tag ~= nil and BOMBS[ev.tag] ~= nil then
        blow(ev.tag)
    end
end)

-- rule 11: a bomb destroyed by any means fails its objective; rule 14: the engine room bomb takes tag 3 with it
local engine_sound_on = true
on("destroyed", function(ev)
    local t = ev.tag
    if t ~= nil and BOMBS[t] ~= nil then
        if DIFF >= 1 then
            objectives.fail(BOMBS[t].objective)
        end
        if t == 2 then                                     -- rule 14
            engine_sound_on = false
            tag(3):set_invincible(false)
            tag(3):destroy()
            sound.stop(0)
        end
    end
end)

-- ------------------------------------------------------------------ tracker bug (rule 12)

local BUG = 0x2F
local HELI = 0
spawn(function()
    local last: string? = nil   -- "planted" | "missed" | nil (not thrown, or in flight)
    while true do
        local how, stuck_tag = item_landed(BUG)
        local now: string? = nil
        if how == "stuck" and stuck_tag == HELI then
            now = "planted"
        elseif how ~= nil then
            now = "missed"
        end
        if now ~= last then
            if now == "planted" then                       -- rule 12: planted: complete, failure lifted
                L.top("340B")
                objectives.set_failed("d", false)
                objectives.complete("d")
            elseif last == "planted" then                  -- rule 12: no longer attached: failed again
                objectives.reset("d")
                objectives.set_failed("d", true)
            elseif now == "missed" then                    -- rule 12: came to rest elsewhere
                L.top("340C")
                objectives.set_failed("d", true)
            end
            last = now
        end
        wait(STEP)
    end
end)

-- ------------------------------------------------------------------ engine room sound (rules 13-14)

local ENGINE_ROOMS = { 108, 28, 53, 110, 29, 117, 102, 99 }
local ENGINE_PAD = 10068
spawn(function()
    sound.play(0x3C, { channel = 0 })                      -- rule 13
    sound.volume(0, 0)
    local loud = false
    while engine_sound_on do
        local d = pad(ENGINE_PAD):distance()
        if not loud and (d <= 900 or any_in_room(ENGINE_ROOMS)) then
            loud = true
            sound.volume(0, 1, 2)
        elseif loud and d > 1400 then
            loud = false
            sound.volume(0, 0, 4)
        end
        wait(STEP)
    end
end)

-- ------------------------------------------------------------------ helicopter (Objects and world)

tag(HELI):set_rotor(620)
tag(HELI):anim(2, { from = 0, to = 2, hold = true })      -- its own model animation 2, held at frames up to 2
-- ------------------------------------------------------------------ the intro (Cinematics)
-- Played by the shared intro module from frigate_intro_data (one of the five fixed cameras with 3418, the swirl,
-- the body's table pose), alongside the rest of the level as before.
spawn(function()
    require("intro").play(require("frigate_intro_data"))
end)

-- ------------------------------------------------------------------ exit and outro (rule 15)


local function outro(p: number)
    cinema.hide_all_chrs(true)
    L.end_on_button()
    local pl = player(p)
    local body = bond_body(p)
    if not L.is_real_weapon(pl:held_item()) then   -- rule 15: unarmed, fist, watch laser or a gadget
        body:hold_item(4, "right")                         -- shown with the Walther PPK
    end
    pl:hide_weapon(true)
    camera.fixed(tag(4))
    screen.fade_in()
    -- step 1
    body:show(true)
    body:teleport(176)
    body:hold_item(0, "left")                              -- left hand emptied (item 0)
    body:anim(0x69, { from = 0, to = 29 })
    while not body:anim_done() and not L.ending() do wait(STEP) end
    if L.ending() then return end
    -- step 2
    body:hold_item(0, "right")                             -- right hand emptied
    body:anim(0x69, { from = 29, to = 160 })
    -- step 3: when it ends, or 3.7 s after step 2 began (counted by waits: the mission clock is stopped)
    local t = 0
    while not body:anim_done() and t < 3.7 and not L.ending() do
        wait(0.1)
        t += 0.1
    end
    L.end_level()
end

-- rule 15; co-op: the first living player to reach the boat ends it for everyone
spawn(function()
    local left_start: { [number]: boolean } = {}
    while true do
        for _, p in L.players() do
            local pl = player(p)
            if not left_start[p] then
                if not pl:in_room_of(153) and not pl:in_room_of(149) then
                    left_start[p] = true
                end
            elseif L.alive(p) and pl:in_room_of(153) then
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

