-- jungle_xenia: Xenia (chr 0x00), her own fight of rules 3-7: decide, attack, chase, evade, against the nearest
-- player (co-op). Her first sighting (rule 1) and her death (rule 2) are in the main script.
-- A require()d module: no wait() at its top level.
local lib = require("campaign_lib")
local guard_ai = require("guard_ai")

local X = {}

local STEP: number = lib.STEP
local XENIA = 0x00

-- Her stats and her waiting pose (Characters table), before rule 1.
function X.setup()
    local x: Target = chr(XENIA)
    if not x:exists() then
        return
    end
    guard_ai.stop(x)                -- rule 7: never the generic guard
    x:set_stats({ health = 10, max_health = 10, armour = 40, speed = 100, accuracy = 100, reaction = 100,
        vision = 50 })
    x:set_flag("no_autoaim", true)
end

function X.fight()
    local x: Target = chr(XENIA)

    local function evade()
        -- rule 6: hop sideways, sidestep, run to the side (24% each), else the dodge animation
        local moves = { "side_hop", "sidestep", "side_run" }
        local i = lib.roll_index({ 24, 24, 24 })
        local ok = false
        if i <= #moves then
            ok = x:combat_move(moves[i])
        end
        if not ok then
            -- (also when the move rolled is impossible where she stands)
            x:anim(0x0B, { from = 20, to = 60 })
        end
        lib.wait_idle(x, 5)
    end

    -- rule 4: firing roll, fire walking, fire running, fire standing, fire kneeling (16% each), aim kneeling
    -- (10%), aim standing (the rest, 12%)
    local ATTACK_PCT = { 16, 16, 16, 16, 16, 10 }

    local function attack()
        local p: number? = lib.nearest_player(x)
        if p == nil then
            return
        end
        if lib.chance(50) and x:is_aimed_at() then
            evade()                 -- then back to the attack
            p = lib.nearest_player(x)
            if p == nil then
                return
            end
        end
        local n: number = p :: number
        local who = player(n)
        if lib.health(n) < 0.25 then
            -- fires while running at him (fire standing when the run is not possible)
            if not x:combat_move("run_fire") then
                x:fire_at(who, { stance = "stand" })
            end
        else
            local i = lib.roll_index(ATTACK_PCT)
            if i == 1 then
                if not x:combat_move("roll_fire") then
                    x:fire_at(who, { stance = "kneel" })
                end
            elseif i == 2 then
                if not x:combat_move("walk_fire") then
                    x:fire_at(who, { stance = "stand" })
                end
            elseif i == 3 then
                if not x:combat_move("run_fire") then
                    x:fire_at(who, { stance = "stand" })
                end
            elseif i == 4 then
                x:fire_at(who, { stance = "stand" })
            elseif i == 5 then
                x:fire_at(who, { stance = "kneel" })
            elseif i == 6 then
                x:fire_at(who, { stance = "kneel", aim_only = true })
                wait(1)
            else
                x:fire_at(who, { stance = "stand", aim_only = true })
                wait(1)
            end
        end
        lib.wait_idle(x, 8)          -- when the move ends she decides again (rule 3)
    end

    local function chase()
        -- rule 5
        local p = lib.nearest_player(x)
        if p == nil or not x:run_to_player("run", p) then
            wait(STEP)
            return
        end
        while x:exists() and x:is_moving() do
            if x:line_to_player() ~= nil then
                return
            end
            if x:is_aimed_at() then
                evade()
                local q = lib.nearest_player(x)
                if q == nil or not x:run_to_player("run", q) then
                    return
                end
            end
            wait(STEP)
        end
    end

    -- rule 3: the fight loop, deciding again after every move
    while x:exists() and not x:is_dying() do
        if lib.chance(24) and x:is_aimed_at() then
            evade()
        elseif x:line_to_player() ~= nil then
            attack()
        else
            chase()
        end
        wait(0.05)
    end
end

return X
