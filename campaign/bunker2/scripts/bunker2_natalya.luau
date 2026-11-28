-- bunker2_natalya: Natalya (chr 0x00) in bunker2 (specs/bunker2.md rules 1-4, 6, 8-10, and her part of the
-- exit cinematic). The two cell conversations, her release by the cell door, following the player, and the
-- control console that starts the countdown. Top level never waits: bunker2.luau calls start().
local L = require("campaign_lib")

local M = {}
local STEP = 0.25

-- Flags shared with the main script.
M.state = {
    freed = false,             -- rule 6 (objective e's first condition)
    conv_done = 0,             -- rules 2, 3: conversations held (the jailer comes after the second, rule 5)
    countdown_started = false, -- rule 11
    exiting = false,           -- rule 16
}
local S = M.state

local task: Task? = nil

local function nat(): Target
    return chr(0x00)
end

local function in_cell(): boolean
    return L.player_in_room(94) ~= nil  -- co-op: ends only when no player is left in the cell room
end

-- rules 2, 3, 4: lines shown once each (top text) at their times while a player stays in the cell room.
local CONVERSATIONS: { Conversation } = {
    -- rule 2: 7409 at 0 s, a line every 5 s, 7412 at 45 s; the conversation ends as 7412 shows
    { lines = { "7409", "740A", "740B", "740C", "740D", "740E", "740F", "7410", "7411", "7412" },
      times = { 0, 5, 10, 15, 20, 25, 30, 35, 40, 45 }, ends = 45 },
    -- rule 3: lines at 5 .. 30 s, ending at 50 s
    { lines = { "7413", "7414", "7415", "7416", "7417", "7418" },
      times = { 5, 10, 15, 20, 25, 30 }, ends = 50 },
}

type Conversation = { lines: { string }, times: { number }, ends: number }

local function converse(c: Conversation)
    local t0 = clock()
    local i = 1
    while in_cell() and not S.freed and (clock() - t0 < c.ends or i <= #c.lines) do
        if i <= #c.lines and clock() - t0 >= c.times[i] then
            hud.top(text(c.lines[i]) :: string)  -- rule 4: once each, no voice
            i += 1
        end
        wait(STEP)
    end
end

-- rule 1: she stands still; a player within 25 units of pad 95: she walks there and starts the next talk.
local function cell_phase(g: Target)
    g:halt()
    while S.conv_done < 2 and not S.freed and L.alive_guard(g) do
        while not pad(95):near(25) and not S.freed do wait(STEP) end
        if S.freed then return end
        g:run_to(95, "walk")
        wait(STEP)
        while g:is_moving() and not S.freed do wait(STEP) end
        if S.freed then return end
        converse(CONVERSATIONS[S.conv_done + 1])
        S.conv_done += 1  -- rule 2: leaving the cell counts as held; rule 3: its end starts the jailer (rule 5)
    end
    -- once both are over she stays put until freed
end

-- rule 10, the ending: 7429, run to pad 120 and wait there facing the player for the rest of the level.
local function escape_wait(g: Target)
    S.countdown_started = true  -- rule 11 starts even when she was interrupted on the way
    hud.top(text("7429") :: string)
    g:run_to(120, "run")
    wait(STEP)
    while L.alive_guard(g) and g:is_moving() do wait(STEP) end
    while L.alive_guard(g) and not S.exiting do
        g:face_player()
        wait(1)
    end
end

-- rule 10: typing at the console.
local TYPING = {
    { 39, 172, 69 }, { 20, 173, 74 }, { 20, 174, 79 }, { 21, 175, 89 },
}
local function type_once(g: Target)
    local r = math.random() * 100
    local pick = TYPING[#TYPING]
    for _, t in TYPING do
        r -= t[1]
        if r < 0 then pick = t break end
    end
    g:anim(pick[2], { from = 0, to = pick[3] })
    if math.random() < 0.5 then
        spawn(function()
            wait(1)
            sound.play(0x00B8, { from = tag(36) })
        end)
    end
end

local function console_phase(g: Target)
    -- rule 9: she leaves the player: 7426, runs to pad 57, walks the last 60 units, turns to the console.
    hud.top(text("7426") :: string)
    local interrupted = function() return L.player_in_room(25) ~= nil end
    g:stop()
    g:run_to(57, "run")
    wait(STEP)
    while L.alive_guard(g) and g:is_moving() and g:distance_to(pad(57)) > 60 do
        if interrupted() then escape_wait(g) return end
        wait(STEP)
    end
    g:run_to(57, "walk")
    wait(STEP)
    while L.alive_guard(g) and g:is_moving() do
        if interrupted() then escape_wait(g) return end
        wait(STEP)
    end
    g:face(tag(35))
    S.countdown_started = true  -- rule 11
    -- rule 10
    local t0 = clock()
    local said5, said10 = false, false
    type_once(g)
    while L.alive_guard(g) do
        local t = clock() - t0
        if t >= 5 and not said5 then hud.top(text("7427") :: string) said5 = true end
        if t >= 10 and not said10 then hud.top(text("7428") :: string) said10 = true end
        if t >= 15 or interrupted() or tag(35):is_destroyed() or tag(36):is_destroyed() then break end
        if not g:on_screen() and g:distance_to(pad(57)) > 20 then
            g:teleport(57)
            g:face(tag(35))
        end
        if g:anim_done() then type_once(g) end
        wait(STEP)
    end
    g:stop_anim()
    escape_wait(g)
end

-- rule 8: follow the player: run beyond 100, walk closer, halt within 25; halted: within 8 a line and a
-- gesture; beyond 40 follow again. Rule 9 ends it near the console.
local function follow_phase(g: Target)
    local halted = false
    local spoke = false
    local speed: string? = nil
    while L.alive_guard(g) and not S.exiting do
        if g:distance_to(pad(57)) < 250 then
            console_phase(g)  -- rule 9
            return
        end
        local d = g:distance()
        if not halted then
            if d <= 25 then
                g:halt()
                halted, spoke, speed = true, false, nil
            else
                local want = if d > 100 then "run" else "walk"
                if want ~= speed or not g:is_moving() then
                    g:run_to_player(want)
                    speed = want
                end
            end
        else
            if d > 40 then
                halted = false
            elseif d <= 8 and not spoke then
                spoke = true
                local a = g:angle_to_player()
                if math.abs(a) <= 90 then
                    hud.top(text("7421") :: string)
                    g:raise_arms()
                else
                    hud.top(text("7420") :: string)
                    g:look_around()
                end
            end
        end
        wait(STEP)
    end
end

function M.start()
    local g: Target = nat()
    if not g:exists() then return end
    g:set_flag_raw(0x80, true)  -- Characters: chr flag 0x80 set at the start (data, meaning unknown)
    tag(3):set_usable_by_guards(false)  -- rule 6: the cell door is closed to characters until first opened
    task = spawn(function() cell_phase(g) end)
    -- rule 6: the cell door tag 3 (prop 77) first opened: she is freed and follows the player.
    spawn(function()
        wait(event("door", { prop = 77, state = "open" }))
        tag(3):set_usable_by_guards(true)
        S.freed = true
        if task then task:cancel() end
        if L.alive_guard(g) and not S.exiting and not S.countdown_started then
            task = spawn(function() follow_phase(g) end)
        end
    end)
end

-- Cinematics, Exit 3: shown again; on pad 50 unless already in the room of pad 120; runs to 120, faces Bond.
function M.exit_scene()
    local g = nat()
    if task then task:cancel() end
    if not L.alive_guard(g) then return end
    g:set_hidden(false)
    if not g:in_room_of(120) then g:teleport(50) end
    spawn(function()
        g:run_to(120, "run")
        wait(STEP)
        while L.alive_guard(g) and g:is_moving() do wait(STEP) end
        g:face_pad(119)  -- Bond's body stands on pad 119
    end)
end

return M
