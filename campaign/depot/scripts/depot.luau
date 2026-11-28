-- depot: the Depot level (specs/depot.md). Seven reinforcement slots hunt the player through the yard, their
-- spawn pads picked by the room he is in (rules 1-6, behaviour: campaign_lib.cover_hunter); the train door completes
-- objective e (rule 7); the exit room plays the train outro (rule 8, Cinematics).

local guard_ai = require("guard_ai")
guard_ai.boot()

local U = require("campaign_lib")
local STEP: number = U.STEP

-- ---------------------------------------------------------------- objectives
objectives.add("a", text("300F") :: string, 2)   -- destroy the arms caches (00 Agent and up)
objectives.add("b", text("3010") :: string, 1)   -- destroy the computer network
objectives.add("c", text("3011") :: string, 1)   -- obtain the safe key
objectives.add("d", text("3012") :: string, 1)   -- recover the blueprints
objectives.add("e", text("3013") :: string, 0)   -- locate Trevelyan's train

local function all_destroyed(tags: { number }): boolean
    for _, t in tags do
        if not tag(t):is_destroyed() then return false end
    end
    return true
end

local CACHES = {}
for t = 0x0A, 0x1A do table.insert(CACHES, t) end
local NETWORK = { 0x01, 0x02, 0x03 }

spawn(function()                          -- objective a: the 17 crates
    U.wait_until(function(): boolean return all_destroyed(CACHES) end)
    objectives.complete("a")
end)
spawn(function()                          -- objective b: the monitor bank and the two mainframes
    U.wait_until(function(): boolean return all_destroyed(NETWORK) end)
    objectives.complete("b")
end)
spawn(function()                          -- objective c: a player holds the safe key (tag 0x00)
    U.wait_until(function(): boolean return tag(0x00):held_by() end)
    objectives.complete("c")
end)
spawn(function()                          -- objective d: a player holds the blueprints (tag 0x05)
    U.wait_until(function(): boolean return tag(0x05):held_by() end)
    objectives.complete("d")
end)

-- rule 7: the train door of tag 0x06 starts to open
spawn(function()
    U.wait_until(function(): boolean return tag(0x06):state() ~= "closed" end)
    objectives.complete("e")
end)

-- ---------------------------------------------------------------- characters
-- Start (pad 272, PP7, ammo, outfit, watch) is the level's own start data: nothing to script.
-- The train guards 0x0E, 0x0F and the yard guards are simple deaf generic guards.
local function simple_deaf(n: number)
    local g = chr(n)
    if g:exists() and guard_ai.mode(g) == nil then
        guard_ai.start(g, "simple_deaf")
    end
end
simple_deaf(0x0E)
simple_deaf(0x0F)
for n = 0x10, 0x15 do simple_deaf(n) end
for n = 0x18, 0x1D do simple_deaf(n) end

-- ---------------------------------------------------------------- reinforcements (rules 1-6)
local PERIODS = { [0] = 20, 25, 30, 35, 60, 120, 150 }     -- rule 1: slot k spawns chr k
local OFF_ROOMS = { 115, 230, 255, 260, 262, 151, 137, 146, 201, 206, 214, 219, 224, 239, 245, 166 }
local ROOM_SETS = {                                         -- rule 4: rooms -> spawn pads of slots 0-6
    { rooms = { 272, 128 }, pads = { 151, 110, 115, 146, 154, 190, 186 } },
    { rooms = { 121 }, pads = { 272, 166, 17, 190, 195, 186, 178 } },
    { rooms = { 154 }, pads = { 110, 115, 272, 195, 186, 201, 219 } },
    { rooms = { 192 }, pads = { 121, 166, 236, 44, 45, 178, 219 } },
    { rooms = { 186 }, pads = { 154, 245, 195, 236, 230, 251, 239 } },
    { rooms = { 178 }, pads = { 44, 190, 239, 219, 232, 251, 45 } },
    { rooms = { 236 }, pads = { 186, 45, 239, 255, 190, 219, 44 } },
    { rooms = { 251 }, pads = { 232, 176, 266, 268, 268, 44, 45 } },
    { rooms = { 257 }, pads = { 253, 251, 232, 176, 178, 44, 45 } },
}

local spawning = true       -- rule 3: on at the start
local current_set = 1       -- rule 4: the start pad 272 is in the first room
local exiting = false       -- rule 8

local function spawning_on(): boolean
    return spawning
end

local function room_set_of(n: number): number?
    for i, set in ROOM_SETS do
        for _, p in set.rooms do
            if player(n):in_room_of(p) then return i end
        end
    end
    return nil
end

-- rules 3-4: the spawning state and the spawn set follow the players' rooms
spawn(function()
    local last: { [number]: number? } = {}
    while true do
        spawning = U.player_in_rooms(OFF_ROOMS) == nil   -- rule 3: off while any player is in one
        if spawning then
            -- rule 4 (co-op notes): the player who entered a listed room most recently picks the set
            for _, n in U.players() do
                local m = room_set_of(n)
                if m ~= nil and m ~= last[n] then
                    current_set = m
                end
                last[n] = m
            end
        end
        wait(STEP)
    end
end)

-- The reinforcement kit (Characters table)
local D5K = 0x0A
local D5K_MODEL = 189
local function kit(g: Target)
    local r = math.random() * 100
    if r < 37 then
        g:set_stats({ health = 40, max_health = 40, armour = 20 })
        g:give_weapon(D5K, { model = D5K_MODEL })
        g:give_weapon(D5K, { model = D5K_MODEL, left_hand = true })
    elseif r < 68 then
        g:set_stats({ health = 40, max_health = 40, armour = 10, grenade_chance = 80 })
        g:give_weapon(D5K, { model = D5K_MODEL })
    else
        g:set_stats({ health = 40, max_health = 40, armour = 0, grenade_chance = 89 })
        g:give_weapon(D5K, { model = D5K_MODEL })
    end
end

-- rule 2: one slot. Nothing before 20 s; then a spawn attempt, and another every period (a failed one is retried
-- next period). While spawning is off the slot waits, and restarts its period when it comes back on (rule 3).
local function slot(k: number)
    spawn(function()
        wait(20)
        local t = PERIODS[k]              -- the first attempt right at 20 s
        while true do
            if not spawning then
                t = 0
            elseif t >= PERIODS[k] then
                t = 0
                if not exiting and not chr(k):exists() then
                    local g = spawn_guard({
                        chr = k, pad = ROOM_SETS[current_set].pads[k + 1], body = 17, force = true,
                    })
                    if g then
                        kit(g)
                        U.cover_hunter(g, { spawning_on = spawning_on, cover_exit = 5000 })   -- rules 5-6
                    end
                end
            end
            wait(STEP)
            if spawning then t += STEP end
        end
    end)
end
for k = 0, 6 do slot(k) end

-- ---------------------------------------------------------------- exit and outro (rule 8)
local BODY_PART = 15   -- outro step 5: the chest (2x damage)
local PP7 = 4

local function train_guard(n: number): Target?
    local g = chr(n)
    if g:exists() and not g:is_dying() and (g:in_room_of(273) or g:in_room_of(280) or g:in_room_of(277)) then
        return g
    end
    return nil
end

local function fade_out_wait()
    screen.fade_out()
    U.wait_fade({ settle = 0 })
end

local function outro()
    local bond = bond_body(0)             -- co-op notes: one Bond model, player 0's weapon
    -- 1. hide every character; any button from now on fades out and ends the level
    cinema.hide_all_chrs(true)
    spawn(function()
        U.wait_until(function(): boolean return cinema.skipped() end)
        fade_out_wait()
        mission.win()
    end)
    -- 2. Bond keeps his firearm (items 0x04-0x16), else a PP7; the first-person weapon is hidden
    local held = player(0):held_item()
    if held < 0x04 or held > 0x16 then held = PP7 end
    bond:hold_item(held)
    player(0):hide_weapon(true)
    -- 3. the camera-position record tag 0x08, looking at Bond, for the whole outro
    camera.fixed(tag(0x08), { look_at = bond })
    -- 4. the train doors snap open; the train guards take part if alive and in the train rooms
    tag(0x06):open_now()
    tag(0x07):open_now()
    local g0F = train_guard(0x0F)
    local g0E = train_guard(0x0E)
    if g0F or g0E then
        -- 5. the shoot-out
        player(0):set_invincible(true)    -- (cinema.begin already made every player invulnerable)
        -- "can hit other characters": the body's fire_at() is a scene shot, its hits are the script's (hit() below).
        bond:show(true)
        bond:teleport(276)
        local finished_0F = false
        if g0F then
            local g = g0F :: Target
            guard_ai.stop(g)
            g:set_hidden(false)
            g:set_health(10)
            g:set_stats({ armour = 0 })
            g:teleport(274)
            g:anim(0, { hold = true })
            spawn(function()
                wait(1)
                if g:exists() and not g:is_dying() then g:fire_at(pad(278)) end
            end)
        end
        if g0E then
            local g = g0E :: Target
            guard_ai.stop(g)
            g:set_hidden(false)
            if g0F == nil then
                g:set_health(10)
                g:set_stats({ armour = 0 })
                g:teleport(275)
                g:anim(0, { hold = true })
                spawn(function()
                    wait(1.2)
                    if g:exists() and not g:is_dying() then g:fire_at(pad(278)) end
                end)
            else
                g:teleport(279)
                g:anim(0, { hold = true, move = false })
                spawn(function()
                    U.wait_until(function(): boolean return finished_0F end)
                    g:anim(42, { hold = true })
                    wait(0.3)
                    if g:exists() and not g:is_dying() then g:fire_at(pad(278)) end
                end)
            end
        end
        screen.fade_in()
        music.play_extra(0)
        -- step 5: the pace is Bond's aim and fire animations (no fixed timer); the hit lands 0.05 s after his shot.
        -- TODO(api): no query says when the body's firing animation has finished; is_busy() on the body stands for
        -- it (at most FIRE_CAP seconds).
        local FIRE_CAP = 3
        local function shot(g: Target, stance: Stance)
            bond:aim_at(g)
            bond:fire_at(g, { stance = stance })
            wait(0.05)
            g:hit(BODY_PART, PP7)         -- a PP7 hit to the chest
            wait(STEP)
            U.wait_until(function(): boolean return not bond:is_busy() end, FIRE_CAP)
        end
        if g0F then
            shot(g0F :: Target, "stand")  -- Bond aims at 0x0F standing
            finished_0F = true
        end
        if g0E then
            shot(g0E :: Target, "kneel")  -- he kneels, aims at 0x0E, fires
        end
        fade_out_wait()
    end
    -- 6. the final shot
    cinema.hide_all_chrs(true)
    bond:show(true)
    bond:teleport(273)
    bond:hold_item(0, "left")             -- removes his left-hand item (0 = nothing)
    tag(0x06):open_now()
    tag(0x07):open_now()
    bond:anim(105, { from = 0, to = 29, hold = true })
    screen.fade_in()
    U.wait_until(function(): boolean return bond:anim_done() end)
    bond:hold_item(0, "right")            -- removes his right-hand item
    bond:anim(105, { from = 29, to = 160, hold = true })
    -- 7. when it ends (or after 2 s) door 0x06 closes; once closed (or after 3 s) fade to black, won
    U.wait_until(function(): boolean return bond:anim_done() end, 2)
    tag(0x06):close()
    U.wait_until(function(): boolean return tag(0x06):state() == "closed" end, 3)
    fade_out_wait()
    mission.win()
end

spawn(function()
    -- rule 8: the first living player in the room of pad 271 (co-op notes); every player frozen
    U.wait_until(function(): boolean
        return U.player_in_rooms({ 271 }) ~= nil
    end)
    exiting = true                        -- rule 2: no spawns during the exit sequence
    cinema.begin()                        -- invulnerable, no pickups, HUD hidden, frozen (every player)
    fade_out_wait()
    if not objectives.all_complete() then
        mission.fail()
        return
    end
    outro()
end)

-- ---------------------------------------------------------------- intro (Cinematics, intro)
-- mission.json "level_intro": false: one of the three fixed cameras with 300E, the swirl and the body's table pose
-- are played by the shared intro module from depot_intro_data.
require("intro").play(require("depot_intro_data"))
