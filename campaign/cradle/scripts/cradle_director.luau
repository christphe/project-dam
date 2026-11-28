-- cradle_director: Trevelyan's spot director on the Cradle (specs/cradle.md rule 11).
-- Tracks which spot he is assigned to (it starts at pad 144) and picks his next one from where the players are:
-- when a player comes within 500 units of a trigger pad of the current spot (poll()), or when Trevelyan asks
-- (ask()). A pick is { pad = where he runs, track = the spot the director considers him at afterwards }.
-- No wait() at the top level (a required module).

local M = {}

export type Pick = { pad: number, track: number, side: boolean }

local TRIGGER_RANGE = 500
local SIDE_LIMIT = 78        -- after a side move, a limited side move falls back to "Hub" this often

local L = require("campaign_lib")
local chance = L.chance

local function in_room(pads: { number }): boolean
    for _, p in pads do
        if player_in_room(p) ~= nil then return true end
    end
    return false
end

local function near(pads: { number }): boolean
    for _, p in pads do
        if pad(p):distance() <= TRIGGER_RANGE then return true end
    end
    return false
end

-- weighted choice: { { percent, fn }, ... }
local function weighted(options: { { any } }): Pick
    local total = 0
    for _, o in options do total += o[1] end
    local r = math.random() * total
    for _, o in options do
        r -= o[1]
        if r < 0 then return o[2]() end
    end
    return options[#options][2]()
end

function M.new()
    local self = {
        spot = 144,                 -- the spot the director tracks him at
        pending = nil :: Pick?,     -- assigned, not yet run to
        last_side = false,          -- the previous move was a side move
    }

    local function at(p: number): Pick
        return { pad = p, track = p, side = false }
    end
    local function hub(): Pick
        if chance(47) then return at(144) end
        return at(145)
    end
    -- a side move; `track`: the (*) rows of the table, where the director then tracks another spot
    local function side(p: number, track: number?): Pick
        return { pad = p, track = track or p, side = true }
    end
    -- "(limited)": after a side move, 78% of the time "Hub" instead
    local function limited(fn: () -> Pick): Pick
        if self.last_side and chance(SIDE_LIMIT) then return hub() end
        return fn()
    end
    local function three(sidefn: () -> Pick): Pick
        return weighted({ { 33, sidefn }, { 33, function() return at(144) end }, { 34, function() return at(145) end } })
    end
    local function half(a: () -> Pick, b: () -> Pick): Pick
        if chance(50) then return a() end
        return b()
    end
    local function p(n: number): () -> Pick
        return function() return at(n) end
    end

    local function as62(): Pick
        return weighted({ { 24, p(145) }, { 23, p(1) }, { 23, p(9) }, { 30, p(19) } })
    end
    local function as42(): Pick
        return weighted({ { 24, p(144) }, { 23, p(1) }, { 23, p(9) }, { 30, p(19) } })
    end
    local function as29(): Pick
        return limited(function()
            return weighted({ { 47, function() return side(19) end }, { 53, function() return side(9) end } })
        end)
    end
    local function as27(): Pick
        return limited(function()
            return weighted({ { 47, function() return side(1) end }, { 53, function() return side(19) end } })
        end)
    end
    local function as28(): Pick
        return limited(function()
            return weighted({ { 47, function() return side(1) end }, { 53, function() return side(9) end } })
        end)
    end

    -- per spot: trigger rows in table order ({ pads, choose }), then the answer when he asks
    local SPOTS: { [number]: { triggers: { { any } }, ask: () -> Pick } } = {
        [144] = {
            triggers = { { { 59 }, p(92) }, { { 62 }, as62 } },
            ask = function()
                if in_room({ 34 }) then return at(92) end
                if in_room({ 69, 91 }) then return as62() end
                if chance(20) then return at(92) end
                return as62()
            end,
        },
        [145] = {
            triggers = { { { 39 }, p(79) }, { { 42 }, as42 } },
            ask = function()
                if in_room({ 31 }) then return at(79) end
                if in_room({ 49, 80 }) then return as42() end
                if chance(20) then return at(79) end
                return as42()
            end,
        },
        [1] = {
            triggers = { { { 4, 7 }, hub }, { { 29 }, as29 } },
            ask = function()
                if in_room({ 24, 29 }) then return as29() end
                if in_room({ 17, 19, 28 }) then return three(function() return side(9) end) end
                if in_room({ 3, 9, 27 }) then return three(function() return side(19) end) end
                return half(hub, as29)
            end,
        },
        [9] = {
            triggers = { { { 12, 15 }, hub }, { { 27 }, as27 } },
            ask = function()
                if in_room({ 9, 27 }) then return as27() end
                if in_room({ 3, 1, 29 }) then return three(function() return side(19, 9) end) end    -- (*)
                if in_room({ 11, 19, 28 }) then return three(function() return side(1, 19) end) end  -- (*)
                return half(hub, as27)
            end,
        },
        [19] = {
            triggers = { { { 22, 21 }, hub }, { { 28 }, as28 } },
            ask = function()
                if in_room({ 19, 28 }) then return as28() end
                if in_room({ 11, 9, 27 }) then return three(function() return side(1, 9) end) end    -- (*)
                if in_room({ 17, 1, 29 }) then return three(function() return side(9, 19) end) end   -- (*)
                return half(hub, as28)
            end,
        },
        [92] = {
            triggers = { { { 69 }, p(145) }, { { 89, 96 }, p(144) } },
            ask = function()
                if in_room({ 68, 62 }) then return at(145) end
                if in_room({ 96, 97, 100, 80 }) then return at(144) end
                return half(p(144), p(145))
            end,
        },
        [79] = {
            triggers = { { { 49 }, p(144) }, { { 84, 100 }, p(145) } },
            ask = function()
                if in_room({ 49, 42 }) then return at(144) end
                if in_room({ 100, 99, 96, 93 }) then return at(145) end
                return half(p(144), p(145))
            end,
        },
    }

    local function assign(pick: Pick)
        self.pending = pick
        self.spot = pick.track
        self.last_side = pick.side
    end

    -- the trigger pads of the current spot, in table order; any player
    function self.poll()
        local s = SPOTS[self.spot]
        if s == nil then return end
        for _, row in s.triggers do
            if near(row[1]) then
                assign(row[2]())
                return
            end
        end
    end

    -- Trevelyan asks: a pick at once
    function self.ask(): Pick
        local s = SPOTS[self.spot]
        assign(if s then s.ask() else at(144))
        return self.pending :: Pick
    end

    -- the assignment he runs to now (nil: none since it was last taken)
    function self.take(): Pick?
        local pk = self.pending
        self.pending = nil
        return pk
    end

    -- he arrived: an assignment counts only when made after his last arrival
    function self.arrived()
        self.pending = nil
    end

    return self
end

return M
