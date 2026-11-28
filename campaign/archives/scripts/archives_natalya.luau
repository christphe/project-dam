-- archives_natalya: Natalya (chr 0), rules 9-13: she waits until she can see a player, then follows the nearest
-- living one; a player close up gets a line; a missed shot sends her to a hideout. Exposes `with` (she counts as with
-- the player: objective d). Her death (rules 14-15) is in the main script.
-- A require()d module: no wait() at its top level.
local lib = require("campaign_lib")
local guard_ai = require("guard_ai")
local mishkin = require("archives_mishkin")

local N = {}

local STEP: number = lib.STEP
local NATALYA = 0
local HIDEOUTS = { 158, 196 }

N.with = false             -- rule 10

local used: { [number]: boolean } = {}
local missed = false       -- rule 12: a player's shot just missed her (the "guard_near_miss" event)

function N.run()
    local nat: Target = chr(NATALYA)
    if not nat:exists() then
        return
    end
    guard_ai.stop(nat)                     -- rule 13: she never fights
    on("guard_near_miss", function()
        missed = true
    end, { chr = NATALYA })

    local function arms_up()
        nat:raise_arms()
        wait(0.5)
    end

    local function seek(): string
        -- rule 9: her own line of sight to a player, facing ignored
        N.with = false
        while nat:exists() and nat:line_to_player() == nil do
            wait(STEP)
        end
        if not nat:exists() then
            return "dead"
        end
        lib.top("0808")
        arms_up()
        objectives.complete("b")
        N.with = true
        return "follow"
    end

    local function hide(): string
        -- rule 12: a shot from a player just missed her
        N.with = false
        lib.top("080B")
        local spot = HIDEOUTS[math.random(1, #HIDEOUTS)]
        if used[spot] then
            N.with = true                  -- that hideout was used: she keeps following
            return "follow"
        end
        used[spot] = true
        nat:run_to(spot, "run")
        while nat:exists() do
            if nat:distance_to(pad(spot)) <= 500 then
                nat:halt()
                return "seek"              -- she waits there for rule 9
            end
            if not nat:is_moving() then
                if nat:is_aimed_at() then
                    nat:run_to(spot, "run")   -- aimed at: she keeps running
                else
                    return "seek"          -- she gives up
                end
            end
            wait(STEP)
        end
        return "dead"
    end

    local function follow(): string
        -- rule 10: runs to the nearest living player, stops within 250, runs again beyond 400
        missed = false                     -- a miss counts while she follows
        while nat:exists() do
            if missed then
                missed = false
                return "hide"              -- rule 12
            end
            local p = lib.nearest_player(nat)
            local d = nat:distance()
            if p ~= nil and d <= 80 then
                -- rule 11: a player stands within 80
                nat:halt()
                local a = nat:angle_to_player()
                if math.abs(a) < 90 then
                    -- in front of her
                    local mishkinGone = mishkin.left or chr(1):is_dead()
                    if not mishkin.talk_over and not mishkinGone then
                        lib.top("0822")
                    else
                        lib.top("080A")
                    end
                    arms_up()
                else
                    -- behind her or at her sides
                    nat:look_around()
                    lib.top("0809")
                    lib.wait_idle(nat, 4)
                end
                lib.wait_idle(nat, 2)      -- she says it again after each gesture while he stays that close
            elseif p ~= nil and nat:is_moving() and d <= 250 then
                nat:halt()
            elseif p ~= nil and not nat:is_moving() and d > 400 then
                nat:run_to_player("run", p)
            end
            wait(STEP)
        end
        return "dead"
    end

    local states: { [string]: () -> string } = { seek = seek, follow = follow, hide = hide }
    local state = "seek"
    while nat:exists() and state ~= "dead" do
        local f = states[state]
        if f == nil then
            break
        end
        state = f()
    end
    N.with = false
end

return N
