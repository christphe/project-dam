-- archives: the Archives level (specs/archives.md). Bond, a prisoner of the GRU, escapes the interrogation room,
-- finds Natalya, meets Mishkin (the safe key for the black box, Secret Agent and up) and escapes with Natalya.
-- This file: objectives, the characters' start, the interrogation room (rules 1-8), Natalya's death (rules 14-15),
-- Mishkin's death and the key (rule 21), the exit (rule 22) and the outro. Natalya is in archives_natalya
-- (rules 9-13), Mishkin in archives_mishkin (rules 16-20).
local guard_ai = require("guard_ai")
guard_ai.boot()

local lib = require("campaign_lib")
local natalya = require("archives_natalya")
local mishkin = require("archives_mishkin")

local STEP: number = lib.STEP
local NATALYA, MISHKIN = 0, 1
local INTERROGATOR, SECOND, SOLDIER = 14, 15, 26

-- ---------------------------------------------------------------- objectives
objectives.add("a", text("0829") or "", 0)    -- escape the interrogation room
objectives.add("b", text("082A") or "", 0)    -- find Natalya
objectives.add("c", text("082B") or "", 1)    -- recover the black box (Secret Agent and up)
objectives.add("d", text("082C") or "", 0)    -- escape with Natalya

-- ---------------------------------------------------------------- characters
-- Patrolling soldiers: their path, then the simple generic guard; vision 70. 27 and 28 throw grenades (5%).
local PATROLS = { [23] = 0, [24] = 1, [25] = 2, [27] = 4, [28] = 5, [29] = 6 }
local GRENADES = { [27] = 5, [28] = 5, [9] = 10, [11] = 5, [10] = 10, [13] = 10, [20] = 10, [21] = 10, [22] = 10,
    [12] = 5, [26] = 5 }
local CLONERS = { 2, 3, 4, 5, 6, 10, 12, 13, 20, 21, 22, 9 }
local STANDARD = { 2, 3, 4, 5, 6, 10, 12, 13, 20, 21, 22, 7, 8, 16, 17, 18, 19 }

for n, pct in GRENADES do
    local g = chr(n)
    if g:exists() then
        g:set_stats({ grenade_chance = pct })
    end
end
for _, n in CLONERS do
    local g = chr(n)
    if g:exists() then
        g:set_flag("clone_sender", true)       -- "clone on gunfire"
    end
end
for n, path in PATROLS do
    local g = chr(n)
    if g:exists() then
        g:set_stats({ vision = 70 })
        guard_ai.start(g, "simple", { patrol = path })
    end
end
for _, n in STANDARD do
    local g = chr(n)
    if g:exists() and guard_ai.mode(g) == nil then
        guard_ai.start(g, "standard")
    end
end
-- The interrogators, soldiers 9 and 11, and the patrol soldier 26 are this script's; Natalya and Mishkin theirs.
for _, n in { INTERROGATOR, SECOND, 9, 11, SOLDIER, NATALYA, MISHKIN } do
    local g = chr(n)
    if g:exists() then
        guard_ai.stop(g)
        g:halt()
    end
end
chr(SOLDIER):patrol(3)                         -- rule 7: patrols path 3

-- ---------------------------------------------------------------- rule 4: a hostile interrogator
local hostileStarted: { [number]: boolean } = {}
local function hostile(n: number)
    if hostileStarted[n] then
        return
    end
    hostileStarted[n] = true
    local g = chr(n)
    spawn(function()
        if not g:exists() then
            return
        end
        guard_ai.stop(g)
        -- waits until it could see a player, fires walking, then running, then the standard generic guard
        while g:exists() and g:line_to_player() == nil do
            wait(STEP)
        end
        if not g:exists() then
            return
        end
        g:combat_move("walk_fire")
        lib.wait_idle(g, 8)
        g:combat_move("run_fire")
        lib.wait_idle(g, 8)
        guard_ai.start(g, "standard")
    end)
end

-- ---------------------------------------------------------------- rule 3: the interrogator turns hostile
local alerted = { [SECOND] = false, [SOLDIER] = false, [INTERROGATOR] = false }
local turned = false
local function interrogator_turns(line: string?)
    if turned then
        return
    end
    turned = true                               -- rule 2 stops
    if line ~= nil then
        lib.top(line)
    end
    alerted[SECOND] = true
    alerted[SOLDIER] = true
    hostile(INTERROGATOR)
end

-- rule 1: nothing of the interrogation room starts before the intro is over. The intro (start pad 192, watch time,
-- outfit 0, fists and the watch magnet stay setup data; one of the two fixed cameras with title 082D, the swirl,
-- the body's table pose) is played by the shared intro module from archives_intro_data.
require("intro").play(require("archives_intro_data"))

-- rule 2: the interrogation, on a clock from the end of the intro (co-op: either player)
spawn(function()
    local LINES: { { at: number, id: string } } = {
        { at = 5, id = "080D" }, { at = 10, id = "080F" }, { at = 15, id = "080E" }, { at = 20, id = "0811" },
        { at = 25, id = "0812" }, { at = 30, id = "0813" }, { at = 35, id = "0814" }, { at = 40, id = "0815" },
        { at = 45, id = "0816" },
    }
    local t0 = clock()
    for _, l in LINES do
        while not turned and clock() - t0 < l.at do
            wait(STEP)
        end
        if turned then
            return
        end
        lib.top(l.id)
    end
    -- after 50 s the talk is over; the interrogator keeps waiting for rule 3 (rule 5: it never leaves its post)
end)

-- rule 3 triggers: the PP7 (tag 0) or its magazine (tag 1) picked up
on("picked_up", function()
    interrogator_turns("0810")
end, { tag = 0 })
on("picked_up", function()
    interrogator_turns("0810")
end, { tag = 1 })

spawn(function()
    -- rule 3 triggers: a player within 200 of pad 150; the interrogator hit or alerted
    local way_out = pad(150)
    local i14 = chr(INTERROGATOR)
    while not turned do
        if way_out:near(200) then
            interrogator_turns("0817")
        elseif i14:exists() and (i14:hits() > 0 or alerted[INTERROGATOR]) then
            interrogator_turns(nil)
        end
        wait(STEP)
    end
end)

spawn(function()
    -- rule 6: the second interrogator stands until hit or alerted, then alerts the others and turns hostile
    local i15 = chr(SECOND)
    if not i15:exists() then
        return
    end
    while i15:exists() and not (i15:hits() > 0 or alerted[SECOND]) do
        wait(STEP)
    end
    alerted[INTERROGATOR] = true
    alerted[SOLDIER] = true
    hostile(SECOND)
end)

-- rule 8: soldiers 9 and 11 are hostile (rule 4) from the start
hostile(9)
hostile(11)

spawn(function()
    -- rule 7: the soldier with new orders
    local s = chr(SOLDIER)
    if not s:exists() then
        return
    end
    -- the one-update signals (spotted, near miss, witnessed) are events; line of sight and the 10 s windows states
    local signalled = false
    local function signal()
        signalled = true
    end
    local cons = {
        on("guard_spotted", signal, { chr = SOLDIER }),
        on("guard_near_miss", signal, { chr = SOLDIER }),
        on("guard_witnessed", signal, { chr = SOLDIER }),
    }
    while s:exists() do
        if signalled or s:line_to_player() ~= nil or s:alerted_within(10) or s:heard_shot_within(10) then
            break
        end
        wait(STEP)
    end
    for _, c in cons do
        c:off()
    end
    if not s:exists() then
        return
    end
    local function run_then_standard()
        local p = lib.nearest_player(s)
        if p ~= nil and s:run_to_player("run", p) then
            while s:exists() and s:is_moving() do
                wait(STEP)
            end
        end
        guard_ai.start(s, "standard")
    end
    if alerted[SOLDIER] then
        run_then_standard()
        return
    end
    local t0 = clock()
    local p = lib.nearest_player(s)
    if p ~= nil then
        s:run_to_player("walk", p)
    end
    while clock() - t0 < 2 do
        wait(STEP)
    end
    s:halt()
    lib.top("0818")
    while clock() - t0 < 6 do
        wait(STEP)
    end
    -- both interrogators turn hostile (rule 3, silently)
    interrogator_turns(nil)
    hostile(SECOND)
    run_then_standard()
end)

-- ---------------------------------------------------------------- objectives a, c
spawn(function()
    while lib.player_in_room(152) == nil do
        wait(STEP)
    end
    objectives.complete("a")
end)

spawn(function()
    -- c: a player holds the safe item of tag 2 (the black box)
    while not tag(2):held_by() do
        wait(STEP)
    end
    objectives.complete("c")
end)

-- ---------------------------------------------------------------- Natalya, Mishkin
local natTask = spawn(function()
    natalya.run()                   -- rules 9-13
end)
local mishTask = spawn(function()
    mishkin.run()                   -- rules 16-20
end)

-- rules 14-15: Natalya dies (no script of the level removes her)
on("guard_died", function()
    lib.top("080C")                 -- rule 14
    objectives.fail("d")            -- rule 15
end, { chr = NATALYA })

-- rule 21: on Secret Agent and up, Mishkin dies before a player has the safe key (his fade-out is no death)
on("guard_died", function()
    if mission.difficulty() >= 1 and not mishkin.left and not mishkin.key_held() then
        objectives.fail("c")
    end
end, { chr = MISHKIN })

-- ---------------------------------------------------------------- the outro
local function outro()
    natTask:cancel()
    mishTask:cancel()
    local nat = chr(NATALYA)
    local body = bond_body(0)       -- co-op: one Bond model, player 1's
    -- 1. while black
    cinema.hide_all_chrs(true)
    lib.end_on_skip()
    -- a gun: items 4-21 (PP7 through gold PP7), 24 or 25; the Moonraker laser (22) and the watch laser (23) do not count
    local it = player(0):held_item()
    local gun = (it >= 4 and it <= 21) or it == 24 or it == 25
    -- 2. the fixed camera of tag 5, tracking Bond's model
    camera.fixed(tag(5), { look_at = body })
    -- 3. Bond's model and Natalya in place
    body:teleport(171)
    body:show(true)
    if not gun then
        body:hold_item(4, "right")  -- a PP7 (model 191) in his hands
    end
    nat:set_hidden(false)
    nat:halt()
    nat:teleport(170)
    lib.fade_in()
    music.play_extra(0)
    -- 5. Natalya's animations
    spawn(function()
        nat:anim(0x73)
        wait(2.6)
        nat:anim(0x65, { from = 51 })
    end)
    -- 4. Bond's animations, then fade and end (won)
    body:anim(0x59)
    wait(1.7)
    body:anim(0x68)
    wait(2.4)
    lib.fade_out()
    mission.finish()
end

-- ---------------------------------------------------------------- the exit (rule 22)
-- Co-op: the first living player into the room of pad 208 ends the level for everyone.
while lib.player_in_room(208) == nil do
    wait(STEP)
end
if natalya.with and chr(NATALYA):exists() then
    objectives.complete("d")        -- Natalya is with him (rule 10)
end
cinema.begin()                      -- immune, no pickups, controls locked
lib.fade_out()
if objectives.all_complete() then
    outro()
else
    mission.finish()                -- lost
end
