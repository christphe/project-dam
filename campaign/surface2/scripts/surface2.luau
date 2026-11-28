-- surface2: Severnaya, second visit (specs/surface2.md). The comms link, the cameras, the mined helicopter and
-- its charges, the snowfield reinforcements, the patrols and their replacements, the armoured troops after the
-- alarm, the way into the bunker and the capture cinematic, and the intro (staged here: mission.json
-- "level_intro": false; the start pad, inventory, outfit and watch time stay the setup's).
local guard_ai = require("guard_ai")
guard_ai.boot()
local L = require("campaign_lib")

local STEP = 0.25
local diff = mission.difficulty()
local exiting = false  -- rule 13: the exit has begun (all spawning stops)

-- ------------------------------------------------------------------ objectives

objectives.add("a", text("8014") :: string, 2)
objectives.add("b", text("8015") :: string, 0)
objectives.add("c", text("8016") :: string, 1)
objectives.add("d", text("8017") :: string, 0)

-- a: the four cameras tags 3, 4, 6, 8 destroyed.
spawn(function()
    local cams = { 3, 4, 6, 8 }
    while true do
        local all = true
        for _, t in cams do
            if not tag(t):is_destroyed() then all = false end
        end
        if all then break end
        wait(event("destroyed"))
    end
    if diff >= 2 then objectives.complete("a") end
end)

-- d: the player enters the room of pad 3 (the game's own objective; also set by rule 13 before its check).
spawn(function()
    while L.player_in_room(3) == nil do wait(STEP) end
    objectives.complete("d")
end)

-- ------------------------------------------------------------------ characters

-- chr 0x08, the comms room officer: the generic deaf simple guard.
do
    local g: Target = chr(0x08)
    if g:exists() and guard_ai.mode(g) ~= "simple_deaf" then
        guard_ai.start(g, "simple_deaf")
    end
end

-- rule 9: the hunter (campaign_lib.hunt): runs at the player (sprints beyond 200), re-plans every 4 s or on
-- arrival; within 80 with a line of sight, one random attack, again while still within 80; removed beyond 1500.
local HUNTER = { sprint_beyond = 200, attack_range = 80, remove_beyond = 1500, replan = 4 }

local function hunter(g: Target)
    L.start_hunter(g, HUNTER)
end

-- rule 10: walk the path; seen within 300 or hurt: hunter for good. The hurt is the "guard_damaged" event;
-- "sees the player within 300" is a state (sees_player() and the distance), polled, since the spotted event
-- fires once on the edge into seeing, maybe farther than 300.
local function patrol_until_disturbed(g: Target, path: number)
    g:patrol(path)
    local hurt = false
    local sub = on("guard_damaged", function() hurt = true end, { chr = g.chr or -1 })
    while L.alive_guard(g) and not hurt and not (g:sees_player() and g:distance() <= 300) do
        wait(STEP)
    end
    sub:off()
    if L.alive_guard(g) then
        L.hunt(g, HUNTER)
    end
end

local PATROLS = {
    { chr = 0x09, path = 0, post = 39 }, { chr = 0x0A, path = 1, post = 57 }, { chr = 0x0B, path = 2, post = 72 },
    { chr = 0x0C, path = 7, post = 185 }, { chr = 0x0D, path = 10, post = 127 }, { chr = 0x0E, path = 15, post = 283 },
}
for _, p in PATROLS do
    local g: Target = chr(p.chr)
    if g:exists() then
        guard_ai.stop(g)
        L.run_task(g, function() patrol_until_disturbed(g, p.path) end)  -- rule 10
    end
end

-- rule 11: every 40 s, each dead or gone patrol is replaced at pad 214 (out of view): AK47, runs to its post;
-- within 20 units of the post it takes up the patrol; seeing the player on the way or stopping elsewhere: hunter.
local function replacement(g: Target, p)
    g:run_to(p.post, "run")
    local spotted = false
    local sub = on("guard_spotted", function() spotted = true end, { chr = g.chr or -1 })
    wait(STEP)
    while L.alive_guard(g) do
        if g:distance_to(pad(p.post)) <= 20 then
            sub:off()
            patrol_until_disturbed(g, p.path)
            return
        end
        if spotted or g:sees_player() or not g:is_moving() then
            sub:off()
            L.hunt(g, HUNTER)
            return
        end
        wait(STEP)
    end
    sub:off()
end

spawn(function()
    while not exiting do
        wait(40)
        for _, p in PATROLS do
            if not exiting and not chr(p.chr):exists() then
                local g = spawn_guard({ chr = p.chr, pad = 214, body = 21, sunglasses = true })
                if g then
                    g:give_weapon(8, { model = 184 })
                    L.run_task(g, function() replacement(g, p) end)
                end
            end
        end
    end
end)

-- ------------------------------------------------------------------ comms link (objective b)

local comms_used = false     -- rule 1 happened
local comms_damaged = false  -- rule 2 happened

-- rule 3: after b fails: the monitor tag 2 to bank 19, sound 0x00E3 at pad 10058, sound 0x00A2 swells.
local function link_lost_effects()
    tag(2):show_screen(19)
    sound.play(0x00E3, { from = pad(10058) })
    local ch = sound.play(0x00A2)
    sound.volume(ch, 0)
    wait(2)
    sound.volume(ch, 32500 / 32767, 30)
end

-- rule 1: a unit used while neither has been destroyed.
local function comms_used_by_player()
    if comms_used or comms_damaged then return end
    if tag(0):is_destroyed() or tag(1):is_destroyed() then return end
    comms_used = true
    sound.play(0x0050, { from = tag(0) })
    wait(1)
    hud.top(text("8009") :: string)
    objectives.fail("b")
    link_lost_effects()  -- rule 3
end
on("activated", function() comms_used_by_player() end, { tag = 0 })
on("activated", function() comms_used_by_player() end, { tag = 1 })

-- rule 2: the first unit destroyed, before any use; b completes when both are gone.
local function comms_destroyed()
    if not comms_used and not comms_damaged then
        comms_damaged = true
        hud.top(text("8008") :: string)
    end
    comms_damaged = true
    if tag(0):is_destroyed() and tag(1):is_destroyed() and not objectives.is_failed("b") then
        objectives.complete("b")
    end
end
on("destroyed", function() comms_destroyed() end, { tag = 0 })
on("destroyed", function() comms_destroyed() end, { tag = 1 })

-- ------------------------------------------------------------------ helicopter (objective c)

-- rule 6: the troop helicopter spins up from rest to 620 over 120 s; the parked one stays still.
tag(9):set_rotor(0)
tag(9):set_rotor(620, 120)
prop(24):set_rotor(0)  -- the parked helicopter (no tag)

-- rule 5: the charges go off in turn, then the helicopter.
local function blow(t: number)
    local o = tag(t)
    o:set_invincible(false)  -- made destructible first
    o:explode()
end

local function helicopter_bomb()
    countdown.set(10)
    countdown.show(true)
    countdown.start()
    while countdown.left() > 0 do wait(STEP) end
    countdown.stop()
    countdown.show(false)
    blow(14)
    blow(15)
    wait(3)
    blow(16)
    blow(17)
    wait(3)
    blow(18)
    blow(19)
    blow(9)
    hud.top(text("800D") :: string)
end

-- rule 4: the mine (item 29) sticks to the helicopter tag 9, or first comes to rest elsewhere.
spawn(function()
    while true do
        local how, t = item_landed(29)
        if how == "stuck" and t == 9 then
            hud.top(text("800B") :: string)
            objectives.complete("c")
            helicopter_bomb()  -- rule 5
            return
        elseif how ~= nil then
            hud.top(text("800C") :: string)
            objectives.fail("c")
            return
        end
        wait(STEP)
    end
end)

-- ------------------------------------------------------------------ snowfield reinforcements

-- rule 7: the spawn points follow the player (first matching room in this order; kept elsewhere).
local SPAWN_TABLE = {
    { 30, { 39, 57, 72, 42 } }, { 68, { 30, 57, 42, 39 } }, { 72, { 68, 93, 42, 72 } },
    { 42, { 45, 39, 93, 72 } }, { 93, { 45, 72, 114, 60 } }, { 45, { 47, 93, 138, 42 } },
    { 114, { 138, 58, 183, 93 } }, { 138, { 114, 224, 47, 51 } }, { 210, { 65, 183, 177, 62 } },
    { 65, { 210, 201, 62, 183 } }, { 201, { 62, 134, 179, 177 } }, { 177, { 138, 226, 172, 134 } },
    { 226, { 177, 243, 52, 232 } }, { 243, { 179, 56, 51, 232 } }, { 232, { 243, 226, 52, 177 } },
}
local spawn_pads = { 39, 57, 72, 42 }  -- the start room's row (start pad 30)
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

-- rule 8: from 5 s, every 2 s, each spawn point refills its chr (out of view, chance by weapon in hand).
spawn(function()
    wait(5)
    while not exiting do
        for k = 0, 3 do
            local c = 0x04 + k
            local p = spawn_pads[k + 1]
            if not exiting and not chr(c):exists() and L.spawn_roll(pad(p)) then
                -- body 21, random head (head omitted), sunglasses
                local g = spawn_guard({ chr = c, pad = p, body = 21, sunglasses = true })
                if g then
                    g:set_stats({ grenade_chance = 75 })  -- 191/256
                    g:give_weapon(7, { model = 193 })
                    hunter(g)  -- rule 9
                end
            end
        end
        wait(2)
    end
end)

-- ------------------------------------------------------------------ armoured troops (chr 0x0F-0x11)

-- rule 12: outfits.
local function armour_outfit(g: Target)
    local r = math.random()
    if r < 0.21 or clock() < 120 then
        g:set_stats({ armour = 2.0 })
        g:give_hat(220)
        g:give_weapon(8, { model = 184 })
    elseif math.random() < 0.5 then
        g:set_stats({ armour = 8.0 })
        g:give_hat(221)
        g:give_weapon(7, { model = 193 })
        if math.random() < 0.39 then
            g:give_weapon(7, { model = 193, left_hand = true })
        end
    else
        g:set_stats({ armour = 6.0 })
        g:give_hat(222)
        g:give_weapon(8, { model = 184 })
    end
end

-- rule 12: once the alarm has sounded: at once and every 20 s, each dead or gone armoured trooper at pad 214.
spawn(function()
    if not alarm.is_on() then wait(event("alarm")) end
    while not exiting do
        for c = 0x0F, 0x11 do
            if not exiting and not chr(c):exists() then
                local g = spawn_guard({ chr = c, pad = 214, body = 38, sunglasses = true })
                if g then
                    armour_outfit(g)
                    hunter(g)
                end
            end
        end
        wait(20)
    end
end)

-- ------------------------------------------------------------------ the exit cinematic

local function aim_kneel(g: Target, at: Target)
    g:fire_at(at, { stance = "kneel", aim_only = true })
end
local function aim_stand(g: Target, at: Target)
    g:fire_at(at, { stance = "stand", aim_only = true })
end

local function soldier(p: number, body: number): Target?
    local g = spawn_guard({ pad = p, body = body, sunglasses = true, force = true })
    if g then
        g:set_hidden(false)
        g:give_weapon(8, { model = 184 })
    end
    return g
end

local function exit_cinematic()
    -- 1: everyone and the weapons hidden; any button ends the level (co-op: only one body is shown).
    cinema.hide_all_chrs(true)
    for _, n in L.living() do
        player(n):hide_weapon(true)
        if n ~= 0 then bond_body(n):show(false) end
    end
    L.end_on_skip({ settle = 0 })
    -- 2: camera tag 11 on Bond; Bond on pad 3; every level character removed; a kneeling soldier on pad 0.
    local body: Target = bond_body(0)
    camera.fixed(tag(11), { look_at = body })
    body:show(true)
    body:teleport(3)
    for c = 0x00, 0x11 do
        if c ~= 0x08 then
            local g: Target = chr(c)
            if g:exists() then g:remove() end
        end
    end
    local s1 = soldier(0, 38)
    if s1 then aim_kneel(s1, body) end
    L.fade_in(nil, { settle = 0 })
    -- 3: animation 89 in full, held; the item in the left hand put away.
    body:anim(89, { hold = true })
    body:hold_item(0, "left")                              -- item 0 empties the left hand
    -- 4: 3 s later: fade, camera tag 12, Bond on pad 4 (animation 101 to frame 34), two more soldiers, fade in,
    -- the extra music.
    wait(3)
    L.fade_out(nil, { settle = 0 })
    camera.fixed(tag(12), { look_at = body })
    body:teleport(4)
    body:anim(101, { to = 34, hold = true })
    local s2 = soldier(1, 38)
    if s2 then aim_stand(s2, body) end
    local s3 = soldier(5, 21)
    if s3 then
        spawn(function()
            if s3:anim(107) then wait(event("anim_end", { chr = s3.chr or -1 })) end
            wait(2)
            aim_stand(s3, body)
        end)
    end
    screen.fade_in()
    local faded_in_at = clock()
    music.play_extra(0, { stop_after = 255, fade = 255 })
    -- 5: at frame 34 Bond lets go of the right-hand weapon and plays 101 to its end; text 800E.
    spawn(function()
        while not body:anim_done() do wait(STEP) end
        body:drop_item("right")                            -- lets go of the right-hand weapon
        body:anim(101, { from = 34, hold = true })
        hud.top(text("800E") :: string)
    end)
    -- 6: 4 s after the fade-in of step 4: fade, camera tag 13, the bunker door snapped open, fade in.
    local left = 4 - (clock() - faded_in_at)
    if left > 0 then wait(left) end
    L.fade_out(nil, { settle = 0 })
    camera.fixed(tag(13))
    tag(10):open_now()
    L.fade_in(nil, { settle = 0 })
    -- 7: the door closes; once shut (or after 3 s), fade to black and the level ends, won.
    tag(10):close()
    local t0 = clock()
    while tag(10):state() ~= "closed" and clock() - t0 < 3 do wait(STEP) end
    L.fade_out(nil, { settle = 0 })
    mission.win()
end

-- ------------------------------------------------------------------ into the bunker

-- rule 13: any living player entering the room of pad 3, 6, 7, 8 or 9 starts the exit (co-op: the first one
-- in; the cinematic freezes everyone).
spawn(function()
    local ROOMS = { 3, 6, 7, 8, 9 }
    local inside = false
    while not inside do
        for _, p in ROOMS do
            if L.player_in_room(p) ~= nil then inside = true break end
        end
        if not inside then wait(STEP) end
    end
    -- objective d, rule 13: the room of pad 3 is the only way into the others, so it is always entered first
    if L.player_in_room(3) ~= nil then objectives.complete("d") end
    exiting = true  -- all spawning stops for good
    cinema.begin({ keep_top_text = true, keep_bottom_text = true })  -- invulnerable, no pickups, controls locked
    -- TODO(api): no verb stops the mission clock (rule 13).
    L.fade_out(nil, { settle = 0 })
    if not objectives.all_complete() then
        mission.fail()
        return
    end
    exit_cinematic()
end)

-- ------------------------------------------------------------------ intro

-- Cinematics, Intro: played by the shared intro module ("level_intro": false in mission.json) from
-- surface2_intro_data (one of the three fixed cameras with 8018, the swirl, the body's animation 98 to frame 99).
require("intro").play(require("surface2_intro_data"))
