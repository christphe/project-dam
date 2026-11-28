-- bunker1_programmer: the programmer, chr 0x19 (specs/bunker1.md rules 1-8): he surrenders, is led to the mainframe
-- terminal at pad 24 and switches it on (objective c), or runs away and escapes (objectives c and d fail).
-- A require()d module: start() spawns the behaviour, nothing waits at the top level.

local lib = require("campaign_lib")

local M = {}

local STEP = 0.25
local CLOSE = 150        -- 1.5 m
local TERMINAL = 24
local DONE_PAD = 97
local EXIT_NEAR = 99
local EXIT_PAD = 101

local show = lib.bottom

function M.start(diff: number)
    local g: Target = chr(0x19)
    if not g:exists() then
        return
    end

    local done = false          -- rule 5: the terminal timeline is over
    local led_once = false      -- rule 4: 780A shown
    local last_780b = -1e9
    local shown: { [number]: boolean } = {}   -- rule 5: each timeline line is shown once

    local function fail_cd()
        if diff >= 2 and not objectives.is_complete("c") then
            if not objectives.is_failed("c") then
                objectives.fail("c")
            end
            if not objectives.is_failed("d") and not objectives.is_complete("d") then
                objectives.fail("d")
            end
        end
    end

    local function gone(): boolean
        return not g:exists()
    end

    local function aimed(): boolean
        return g:is_aimed_at()
    end

    local function mainframe_down(): boolean
        return tag(6):is_destroyed() and tag(7):is_destroyed()
    end

    -- Each state returns the next one ("surrender", "lead", "terminal", "flee", "leave") or nil when he is gone.

    -- rules 1-3: surrendered, hands up, waiting.
    local function surrender(): string?
        g:surrender()   -- hands up, drops what he holds, keeps the pose
        local since = clock()
        while not gone() do
            local t = clock() - since
            if t >= 3 and g:near(CLOSE) and aimed() then
                return "lead"   -- rule 2
            end
            if t >= 10 and not g:near(CLOSE) and not aimed()
                and (g:distance() > 800 or not g:sees_player()) then
                return "flee"   -- rule 3
            end
            wait(STEP)
        end
        return nil
    end

    -- rule 4: leading the player to the terminal.
    local function lead(): string?
        if not led_once then
            led_once = true
            show("780A")
        elseif clock() - last_780b >= 8 then
            last_780b = clock()
            show("780B")
        end
        g:run_to(TERMINAL, "walk")
        local step = clock()
        wait(STEP)
        while not gone() do
            if g:distance_to(pad(TERMINAL)) <= CLOSE then
                return "terminal"
            end
            if clock() - step >= 5 or not g:is_moving() then
                if aimed() or g:distance() <= 500 then
                    return "lead"   -- a new lead step
                end
                return "flee"
            end
            wait(STEP)
        end
        return nil
    end

    -- rule 5: the timeline at the terminal, counted from his arrival.
    local function terminal(): string?
        local t0 = clock()
        local TIMELINE = { 5, 10, 15, 20, 30 }
        if not shown[0] and not mainframe_down() then
            shown[0] = true
            show("7812")
            g:look_around()
        end
        local i = 1
        while not gone() do
            if mainframe_down() then
                show("7817")   -- he cannot fix this
                done = true
                return "surrender"
            end
            local t = clock() - t0
            while i <= #TIMELINE and t >= TIMELINE[i] do
                local at = TIMELINE[i]
                if at == 30 then
                    done = true
                    return "surrender"
                end
                if not shown[at] then
                    shown[at] = true
                    if at == 5 then
                        show("780E")
                    elseif at == 10 then
                        show("780F")
                    elseif at == 15 then
                        show("7810")
                    elseif at == 20 then
                        alarm.trigger()
                        objectives.complete("c")
                        show("7811")
                    end
                end
                i += 1
            end
            wait(STEP)
        end
        return nil
    end

    -- rule 6: running away.
    local function flee(): string?
        local dest: number?
        local speed = "run"
        if done then
            dest = DONE_PAD
        else
            dest = g:flee_pad()   -- the game's escape-pad choice away from the player
            if dest == nil then
                return "surrender"
            end
            speed = if lib.chance(80) then "run" else "walk"
        end
        g:run_to(dest :: number, speed)
        wait(STEP)
        while not gone() do
            if g:distance_to(pad(EXIT_NEAR)) <= 1000 then
                return "leave"
            end
            if not g:is_moving() then
                if done then
                    return "leave"
                elseif g:near(200) or aimed() then
                    return "surrender"
                end
                return "flee"
            end
            wait(STEP)
        end
        return nil
    end

    -- rule 7: leaving by pad 101.
    local function leave(): string?
        g:run_to(EXIT_PAD, "run")
        wait(STEP)
        while not gone() and g:is_moving() do
            wait(STEP)
        end
        if gone() then
            return nil
        end
        if not objectives.is_complete("c") and diff >= 2 then
            show("7813")
            fail_cd()
        end
        g:fade_out()
        return nil
    end

    local STATES: { [string]: () -> string? } = {
        surrender = surrender, lead = lead, terminal = terminal, flee = flee, leave = leave,
    }

    -- rule 8: once he is gone (killed and removed, or left), c and d fail if c is not complete.
    spawn(function()
        while lib.present(g) do
            wait(STEP)
        end
        fail_cd()
    end)

    spawn(function()
        -- rule 1: idle until he sees or hears a player, a shot just misses him, or he sees a guard die (the
        -- engine's guard signals)
        local woken = false
        local function wake(_ev: MissionEvent)
            woken = true
        end
        local conns: { Connection } = {
            on("guard_spotted", wake, { chr = 0x19 }),
            on("guard_heard", wake, { chr = 0x19 }),
            on("guard_near_miss", wake, { chr = 0x19 }),
            on("guard_witnessed", wake, { chr = 0x19, what = "death" }),
        }
        lib.wait_until(function(): boolean return woken or gone() end)
        for _, c in conns do
            c:off()
        end
        local st: string? = "surrender"
        while st ~= nil and not gone() do
            st = STATES[st :: string]()
        end
    end)
end

return M
