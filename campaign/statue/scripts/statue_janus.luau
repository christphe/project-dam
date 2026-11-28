-- statue_janus: Janus at the statue (specs/statue.md rules 6-8 and 11-15): his entrance with the four escorts, the
-- "put the gun away" exchange, the reveal (objective b), and his departure. A require()d module: start() spawns.

local lib = require("campaign_lib")
local guards = require("statue_guards")
local sig = require("statue_signal")

local M = {}

local STEP = 0.25
local STATUE = 59
local TALK_RANGE = 1400   -- 14 m from the statue
local BARE = 1            -- item 1: bare hands

local show = lib.bottom

-- Co-op (notes on rules 8 and 11): every living player within 14 m of the statue must have bare hands.
local function all_bare(): boolean
    for _, n in lib.living() do
        local p = player(n)
        if p:distance_to(pad(STATUE)) <= TALK_RANGE and p:held_item() ~= BARE then
            return false
        end
    end
    return true
end

function M.start(S: any)
    spawn(function()
        -- rule 6: objective a complete and a player within 8 m of the statue
        lib.wait_until(function()
            return objectives.is_complete("a") and pad(STATUE):near(800)
        end)
        local j = lib.spawn_retry({ pad = 54, body = 8 }, function() return S.exiting end)
        if j == nil then
            return
        end
        local janus = j :: Target
        S.janus = janus
        S.janus_spawned = true
        janus:set_flag("no_autoaim", true)
        for n = 0x02, 0x05 do
            local e = chr(n)
            if not lib.present(e) then
                local g = lib.spawn_retry({ chr = n, pad = 54, body = 4 }, function() return S.exiting end)
                if g then
                    spawn(function() guards.escort(S, g) end)   -- rules 7, 9, 10
                end
            end
        end

        -- rule 6: from now on, every player more than 14 m from the statue turns the level hostile
        spawn(function()
            lib.wait_until(function() return lib.all_farther(pad(STATUE), TALK_RANGE) end)
            S.set_hostile()
        end)

        local gun_away = false   -- rule 8 watches this

        -- rule 8: the player put his gun away, then holds anything else
        spawn(function()
            lib.wait_until(function() return gun_away end)
            lib.wait_until(function() return not all_bare() end)
            S.set_hostile()
        end)

        local missed: () -> boolean   -- set below (rules 12 / 14)

        local function leaves()   -- rule 15
            sig.off(missed)
            janus:set_invincible(true)
            S.janus_gone = true
            S.janus_gone_at = clock()
            if not objectives.is_complete("b") then
                objectives.fail("b")
            end
            local dest = 50
            while janus:exists() do
                local speed = if S.hostile then "sprint" else "run"
                if not janus:run_to(dest, speed) then
                    S.set_hostile()   -- stuck: hostile, and he runs again
                    wait(1)
                    continue
                end
                wait(STEP)
                while janus:exists() and janus:is_moving() do
                    wait(STEP)
                end
                if not janus:exists() then
                    return
                end
                if janus:distance_to(pad(dest)) > 150 then
                    S.set_hostile()   -- stopped short: stuck, he runs again
                    continue
                end
                if janus:on_screen() then
                    S.set_hostile()   -- arrives in sight: runs on to pad 139
                    dest = 139
                else
                    janus:fade_out()
                    return
                end
            end
        end

        -- rules 12 / 14: hostile, or a player's shot just misses Janus (the engine's near-miss signal)
        missed = sig.latch(janus, "guard_near_miss")
        local function disturbed(): boolean
            return S.hostile or missed()
        end

        -- rule 11: he walks to pad 1, faces Bond, greets him
        janus:run_to(1, "walk")
        wait(STEP)
        while janus:exists() and janus:is_moving() do
            if disturbed() then
                S.set_hostile()
                show("8C22")   -- rule 12
                leaves()
                return
            end
            wait(STEP)
        end
        janus:face_player()
        show("8C12")
        local t0 = clock()
        while clock() - t0 < 2 do
            if disturbed() then
                S.set_hostile()
                show("8C22")   -- rule 12
                leaves()
                return
            end
            wait(STEP)
        end
        if all_bare() then
            show("8C33")
            gun_away = true   -- interpretation: bare hands at the greeting count as the gun put away (rule 8)
        else
            show("8C13")
            local asked = clock()
            local warned = false
            while true do
                if disturbed() then
                    S.set_hostile()
                    show("8C22")   -- rule 12
                    leaves()
                    return
                end
                if all_bare() then
                    show("8C14")
                    gun_away = true
                    break
                end
                local t = clock() - asked
                if t >= 20 then
                    S.set_hostile()   -- rule 11: still out after 20 s
                    show("8C22")      -- rule 12
                    leaves()
                    return
                elseif t >= 10 and not warned then
                    warned = true
                    show("8C21")
                end
                wait(STEP)
            end
        end

        -- rule 13: the reveal
        local LINES: { { at: number, id: string } } = {
            { at = 5, id = "8C16" }, { at = 10, id = "8C17" }, { at = 15, id = "8C18" }, { at = 20, id = "8C19" },
            { at = 25, id = "8C1A" }, { at = 30, id = "8C1B" }, { at = 35, id = "8C1C" }, { at = 40, id = "8C1D" },
            { at = 45, id = "8C1E" },
        }
        local r0 = clock()
        local i = 1
        while i <= #LINES do
            if disturbed() then   -- rule 14
                S.set_hostile()
                show(if objectives.is_complete("b") then "8C15" else "8C22")
                leaves()
                return
            end
            local at, id = LINES[i].at, LINES[i].id
            if clock() - r0 >= at then
                if at == 10 then
                    objectives.complete("b")
                end
                show(id)
                i += 1
            else
                wait(STEP)
            end
        end
        leaves()   -- rule 13: then Janus leaves (rule 15)
    end)
end

return M
