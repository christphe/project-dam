-- streets_valentin: Valentin (chr 31, Secret Agent and up), rules 5-12: he meets Bond, talks while a player stays
-- close, then runs off and has his associates told (the countdown reward of rule 3); left behind or killed, objective
-- a fails. A require()d module: no wait() at its top level.
local lib = require("campaign_lib")
local guard_ai = require("guard_ai")

local V = {}

local STEP: number = lib.STEP
local VALENTIN = 31
local START_PAD = 245
local TALK_RANGE = 800
local LEFT_RANGE = 5000

-- rule 6: the conversation, on its own clock
local LINES: { { at: number, id: string } } = {
    { at = 4, id = "640B" }, { at = 8, id = "640C" }, { at = 12, id = "640D" }, { at = 16, id = "640E" },
    { at = 20, id = "640F" }, { at = 24, id = "6410" },
}
local TALK_END = 28

-- `on_reward()`: rule 3, called once rule 7 ends with his associates told.
function V.run(on_reward: () -> ())
    local v: Target = chr(VALENTIN)
    if not v:exists() then
        return
    end
    guard_ai.stop(v)                -- rule 12: he never fights
    v:set_flag("no_autoaim", true)

    -- rule 5: first line of sight to a player (co-op: the nearest player)
    while v:exists() and v:line_to_player() == nil do
        wait(STEP)
    end
    if not v:exists() then
        objectives.fail("a")        -- rule 10
        return
    end
    v:halt()
    v:face_player()

    local talk = 0
    local last = clock()
    local nextLine = 1
    local away = false
    while talk < TALK_END do
        if v:is_dead() then
            objectives.fail("a")    -- rule 10: dies or disappears before the end
            return
        end
        local d = v:distance()      -- co-op: "no player within" = the nearest one is farther
        if d > LEFT_RANGE then
            -- rule 8: left behind (no rule 3, rule 9)
            lib.top("6412")
            objectives.fail("a")
            v:fade_out()
            return
        end
        local now = clock()
        if d <= TALK_RANGE then
            talk += now - last      -- rule 5: the conversation clock runs only while a player is within 800
            away = false
        elseif not away then
            away = true
            lib.top("6411")         -- rule 11: once per walk-away
        end
        last = now
        while nextLine <= #LINES and talk >= LINES[nextLine].at do
            lib.top(LINES[nextLine].id)   -- rule 6
            v:face_player()
            nextLine += 1
        end
        wait(STEP)
    end

    -- rule 7: done talking
    objectives.complete("a")
    v:run_to(START_PAD, "run")
    while v:exists() do
        if v:distance_to(pad(START_PAD)) <= 150 or not v:is_moving() or v:distance() > 3000 then
            break
        end
        wait(STEP)
    end
    if v:exists() then
        lib.top("6413")
        on_reward()                 -- rule 3 (rule 9: only after rule 7)
        v:fade_out()                -- 1.5 s, then removed
    end
end

return V
