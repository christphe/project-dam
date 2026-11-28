-- egypt: the Egyptian level (specs/egyptian.md). Five reinforcement slots roam the temple (rules 1-6, behaviour
-- in campaign_lib.cover_hunter); the golden gun room's floor puzzle (rules 14-16); Baron Samedi's three forms (rules 7-10,
-- fight in egypt_samedi.luau); the end and the outro (rule 17, Cinematics).

local guard_ai = require("guard_ai")
guard_ai.boot()

local U = require("campaign_lib")
local S = require("egypt_samedi")
local STEP: number = U.STEP

-- ---------------------------------------------------------------- objectives
objectives.add("a", text("280C") :: string, 0)   -- recover the golden gun
objectives.add("b", text("280D") :: string, 0)   -- put down Baron Samedi

-- objective a: the player holds the golden gun (tag 0x04) and its ammo box (tag 0x05)
local picked: { [number]: boolean } = {}
on("picked_up", function(ev: MissionEvent)
    local t = (ev :: any).tag
    if t == 0x04 or t == 0x05 then
        picked[t] = true
    end
end)
spawn(function()
    U.wait_until(function(): boolean
        local gun = picked[0x04] or tag(0x04):held_by()
        local ammo = picked[0x05] or tag(0x05):held_by()
        return gun and ammo
    end)
    objectives.complete("a")
end)

-- ---------------------------------------------------------------- characters
-- Start (pad 149, PP7, ammo, outfit, watch) is the level's own start data: nothing to script.
-- Temple guards 0x05-0x12: standard generic guards.
for n = 0x05, 0x12 do
    local g = chr(n)
    if g:exists() and guard_ai.mode(g) == nil then
        guard_ai.start(g, "standard")
    end
end

-- ---------------------------------------------------------------- Baron Samedi (rules 7-10)
local SAMEDI_BODY = 12

local samedi1: Target = chr(0x14)
guard_ai.stop(samedi1)
samedi1:halt()
-- Characters table: armour 200, health 200, speed / accuracy / flinch 50, not auto-aimed; his two DD44s are the setup's.
samedi1:set_stats({ armour = 200, health = 200, max_health = 200, speed = 50, accuracy = 50, flinch = 50 })
samedi1:set_flag("no_autoaim", true)
S.form(samedi1)                           -- rule 7

-- Spawns a form even in view, retrying until there is room.
local function spawn_form(n: number, at: number): Target
    while true do
        local g = spawn_guard({ chr = n, pad = at, body = SAMEDI_BODY, force = true })
        if g then return g end
        wait(STEP)
    end
end

local function two_weapons(g: Target, item: number, model: number)
    g:give_weapon(item, { model = model })
    g:give_weapon(item, { model = model, left_hand = true })
end

spawn(function()
    -- rule 8: the first form is gone: the second form appears at pad 186
    U.wait_until(function(): boolean return not samedi1:exists() end)
    local s2 = spawn_form(0x15, 186)
    s2:set_stats({ armour = 1000, health = 500, max_health = 500, speed = 80, accuracy = 80, reaction = 80 })
    two_weapons(s2, 0x09, 205)            -- two ZMGs
    tag(0x06):set_invincible(false)
    tag(0x06):explode()
    S.laugh(s2)                           -- rule 8: once it has appeared
    S.form(s2)                            -- rule 7

    -- rule 9: the second form is gone: the third form appears at pad 146
    U.wait_until(function(): boolean return not s2:exists() end)
    local s3 = spawn_form(0x16, 146)
    s3:set_stats({ armour = 3000, health = 500, max_health = 500, speed = 100, accuracy = 100, reaction = 100 })
    two_weapons(s3, 0x16, 205)            -- two lasers
    tag(0x07):set_invincible(false)
    tag(0x07):explode()
    S.form(s3)                            -- rule 7
    fog.next(true)                        -- rule 9: slowly to the level's next fog
    spawn(function()
        wait(2)
        if s3:exists() then S.laugh(s3) end   -- rule 9: 2 s later
    end)

    -- rule 10: the third form is gone
    U.wait_until(function(): boolean return not s3:exists() end)
    objectives.complete("b")
end)

-- ---------------------------------------------------------------- the golden gun room (rules 14-16)
local WRONG_TILES: { number } = {}
for p = 151, 165 do table.insert(WRONG_TILES, p) end
for p = 167, 174 do table.insert(WRONG_TILES, p) end
local RIGHT_TILE = 166

-- rule 14: the room's start state
local function puzzle_reset()
    tag(0x03):close()
    tag(0x00):close()
    tag(0x01):open()
    tag(0x04):set_collectable(false)
    tag(0x05):set_collectable(false)
end

spawn(function()
    local unlock: Task? = nil
    while true do
        if unlock then unlock:cancel(); unlock = nil end
        puzzle_reset()                                    -- rule 14
        -- rule 15: a player enters the room of pad 151 (any player, co-op notes)
        U.wait_until(function(): boolean return U.player_in_rooms({ 151 }) ~= nil end)
        tag(0x02):close()
        tag(0x01):close()
        local right = false
        U.wait_until(function(): boolean          -- the first tile any player steps on decides
            for _, p in WRONG_TILES do
                if pad(p):near(90) then return true end
            end
            if pad(RIGHT_TILE):near(100) then
                right = true
                return true
            end
            return false
        end)
        if right then
            tag(0x01):open()                                -- the gun case opens
            unlock = spawn(function()
                U.wait_until(function(): boolean return tag(0x01):state() ~= "closed" end)
                tag(0x04):set_collectable(true)
                tag(0x05):set_collectable(true)
            end)
        else
            tag(0x00):open()                                -- the trap is released; the gun stays locked
        end
        -- rule 16: a player then enters the room of pad 69: the puzzle resets
        U.wait_until(function(): boolean return U.player_in_rooms({ 69 }) ~= nil end)
        if unlock then unlock:cancel(); unlock = nil end
    end
end)

-- ---------------------------------------------------------------- reinforcements (rules 1-6)
local PERIODS = { [0] = 40, 40, 50, 50, 60 }               -- rule 1: slot k spawns chr 5+k
local OFF_ROOMS = { 145 }                                    -- rule 3
local ROOM_SETS = {                                          -- rule 5: rooms -> spawn pads of slots 0-4
    { room = 1, pads = { 21, 26, 90, 140, 137 } },
    { room = 114, pads = { 108, 112, 80, 79, 103 } },
    { room = 21, pads = { 141, 48, 47, 88, 139 } },
    { room = 104, pads = { 101, 102, 114, 121, 97 } },
    { room = 48, pads = { 141, 11, 21, 85, 64 } },
    { room = 100, pads = { 113, 112, 95, 94, 114 } },
    { room = 86, pads = { 141, 80, 71, 39, 48 } },
    { room = 88, pads = { 21, 26, 140, 139, 80 } },
    { room = 95, pads = { 97, 99, 47, 59, 145 } },
    { room = 80, pads = { 114, 137, 138, 42, 66 } },
    { room = 94, pads = { 14, 64, 67, 139, 138 } },
    { room = 67, pads = { 39, 59, 71, 72, 85 } },
}

local spawning = true       -- rule 3: on at the start
local current_set: number? = nil   -- rule 5 (UNCERTAIN in the spec): no spawn pad until the first match
local ending = false        -- rule 17

local function spawning_on(): boolean
    return spawning
end

local function room_set_of(n: number): number?
    for i, set in ROOM_SETS do
        if player(n):in_room_of(set.room) then return i end
    end
    return nil
end

-- rules 3, 5: spawning and the spawn set follow the players' rooms
spawn(function()
    local last: { [number]: number? } = {}
    while true do
        spawning = U.player_in_rooms(OFF_ROOMS) == nil    -- rule 3 (any player)
        -- rule 5 (co-op notes): the player who entered a listed room most recently picks the set
        for _, n in U.players() do
            local m = room_set_of(n)
            if m ~= nil and m ~= last[n] then
                current_set = m
            end
            last[n] = m
        end
        wait(STEP)
    end
end)

-- rule 4: speed and accuracy by mission time
local function rating_now(): number
    local t = clock()
    if t > 900 then return 80 end
    if t > 600 then return 60 end
    if t > 300 then return 40 end
    return 20
end

-- the kit (Characters table): item 9, model 195
local GUN = 0x09
local GUN_MODEL = 195
local function kit(g: Target)
    local r = rating_now()
    local roll = math.random() * 100
    if roll < 37 then
        g:set_stats({ speed = r, accuracy = r, health = 40, max_health = 40, armour = 20 })
        g:give_weapon(GUN, { model = GUN_MODEL })
        g:give_weapon(GUN, { model = GUN_MODEL, left_hand = true })
    elseif roll < 68 then
        g:set_stats({ speed = r, accuracy = r, health = 40, max_health = 40, armour = 10, grenade_chance = 80 })
        g:give_weapon(GUN, { model = GUN_MODEL })
    else
        g:set_stats({ speed = r, accuracy = r, health = 40, max_health = 40, armour = 0, grenade_chance = 89 })
        g:give_weapon(GUN, { model = GUN_MODEL })
    end
end

-- rule 2: one slot. Nothing before 20 s; then a spawn attempt, and another every period (a failed one is retried
-- next period); spawns only out of the players' view. While spawning is off the slot waits, and restarts its
-- period when it comes back on (rule 3).
local function slot(k: number)
    spawn(function()
        wait(20)
        local t = PERIODS[k]              -- the first attempt right at 20 s
        while true do
            if not spawning then
                t = 0
            elseif t >= PERIODS[k] then
                t = 0
                local set = current_set
                if not ending and set ~= nil and not chr(5 + k):exists() then
                    local g = spawn_guard({ chr = 5 + k, pad = ROOM_SETS[set].pads[k + 1], body = 3 })
                    if g then
                        kit(g)
                        U.cover_hunter(g, { spawning_on = spawning_on, cover_exit = 3000 })   -- rule 6
                    end
                end
            end
            wait(STEP)
            if spawning then t += STEP end
        end
    end)
end
for k = 0, 4 do slot(k) end

-- ---------------------------------------------------------------- end and outro (rule 17)
local function fade_out_wait()
    screen.fade_out()
    U.wait_fade({ settle = 0 })
end

local function any_alive(): boolean
    for _, n in U.players() do
        if U.alive(n) then return true end
    end
    return false
end

local function outro()
    local bond = bond_body(0)             -- co-op notes: one Bond model (player 0)
    -- 1. hide every character; any button from now on fades out and ends the level
    cinema.hide_all_chrs(true)
    spawn(function()
        U.wait_until(function(): boolean return cinema.skipped() end)
        fade_out_wait()
        mission.win()
    end)
    -- 2. Bond holds the golden gun (item 0x13); the first-person weapon is hidden
    bond:hold_item(0x13)
    player(0):hide_weapon(true)
    -- 3. the camera-position record tag 0x0B, looking at Bond
    camera.fixed(tag(0x0B), { look_at = bond })
    -- 4. Bond shown on pad 187, walks to pad 190; the temple guards are removed; fade in
    bond:show(true)
    bond:teleport(187)
    bond:walk_to(190, "walk")
    for n = 0x05, 0x12 do
        local g = chr(n)
        if g:exists() then g:remove() end
    end
    screen.fade_in()
    -- 5. Samedi appears at pad 187, runs to pad 88 and faces Bond.
    -- When the walking Bond comes within 1 m (100 units, 3D) of pad 88 (the body walks the player's own path).
    U.wait_until(function(): boolean return bond:distance_to(pad(88)) <= 100 end, 15)
    local sam: Target? = nil
    U.wait_until(function(): boolean
        local s = spawn_guard({ pad = 187, body = SAMEDI_BODY, force = true })
        sam = s
        return s ~= nil
    end, 3)
    if sam then
        local s = sam :: Target
        s:set_hidden(false)
        s:run_to(88, "run")
        U.wait_until(function(): boolean return not s:is_moving() end, 6)
        if not s:face(bond) then s:face_player() end
    end
    -- 6. fade to black, the camera-position record tag 0x0C looking at Bond, fade in
    fade_out_wait()
    camera.fixed(tag(0x0C), { look_at = bond })
    screen.fade_in()
    -- 7. Samedi laughs and plays animation 163 frames 170-296, laughing again at each repeat
    -- 8. after 10 s fade to black; the level ends (won)
    local t0 = clock()
    if sam then
        local s = sam :: Target
        spawn(function()
            while true do
                S.laugh(s)
                s:anim(163, { from = 170, to = 296 })
                wait(STEP)
                U.wait_until(function(): boolean return s:anim_done() end)
            end
        end)
    end
    wait(math.max(0, 10 - (clock() - t0)))
    fade_out_wait()
    mission.win()
end

spawn(function()
    -- rule 17: every objective complete and a player alive; every player frozen
    U.wait_until(function(): boolean
        return objectives.all_complete() and any_alive()
    end)
    ending = true                         -- rule 2: no spawns during the end sequence
    cinema.begin()                        -- invulnerable, no pickups, HUD hidden, frozen (every player)
    fade_out_wait()
    outro()
end)

-- ---------------------------------------------------------------- intro (Cinematics, intro)
-- mission.json "level_intro": false: one of the two fixed cameras with 280E, the swirl and the body's table pose
-- are played by the shared intro module from egypt_intro_data.
require("intro").play(require("egypt_intro_data"))
