-- statue_signal: latches on the engine's guard signal events for the Statue Park scripts (specs/statue.md rules 10,
-- 12, 14, 25 react to "a player's shot just misses him"). A require()d module: it only defines functions.
--
--   local sig = require("statue_signal")
--   local missed = sig.latch(g, "guard_near_miss")   -- missed() is true once per signal since its last call
--   ...
--   sig.off(missed)                                  -- unsubscribes (the guard is gone, the rule is over)

local M = {}

local conns: { [any]: Connection } = {}

-- A function true when the event `name` happened for guard g since its last call (the event filtered by g's chr).
function M.latch(g: Target, name: string): () -> boolean
    local hit = false
    local function take(): boolean
        local h = hit
        hit = false
        return h
    end
    local c = g.chr
    if c ~= nil then
        conns[take] = on(name, function(_ev: MissionEvent)
            hit = true
        end, { chr = c })
    end
    return take
end

-- Unsubscribes a latch made by latch().
function M.off(take: () -> boolean)
    local c = conns[take]
    if c then
        c:off()
        conns[take] = nil
    end
end

return M
