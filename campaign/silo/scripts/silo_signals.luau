-- silo_signals: the engine's one-update guard signals for the Silo scripts, from the guard events
-- ("guard_spotted", "guard_near_miss", "guard_witnessed", mission.d.luau) instead of polling the one-tick queries
-- spotted_player() / was_shot_at() / witnessed(), which a 0.25 s poll mostly misses. One on() per event name,
-- routed by ev.chr; a signal counts while it is at most FRESH old, so a busy guard does not act on it much later.
-- A require()d module: its top level only subscribes (it never waits).

local Sig = {}

local FRESH = 0.5   -- seconds a latched signal still counts (two decisions of a 0.25 s poll)

type Latch = { spot_at: number?, spot_player: number?, miss_at: number?, wit_at: number?, wit: string? }
local latches: { [number]: Latch } = {}

local function latch(c: number): Latch
    local l = latches[c]
    if l ~= nil then
        return l
    end
    local n: Latch = { spot_at = nil, spot_player = nil, miss_at = nil, wit_at = nil, wit = nil }
    latches[c] = n
    return n
end

local function fresh(t: number?): boolean
    return t ~= nil and clock() - t <= FRESH
end

on("guard_spotted", function(ev: MissionEvent)
    if ev.chr ~= nil then
        local l = latch(ev.chr)
        l.spot_at = clock()
        l.spot_player = ev.player
    end
end)
on("guard_near_miss", function(ev: MissionEvent)
    if ev.chr ~= nil then
        latch(ev.chr).miss_at = clock()
    end
end)
on("guard_witnessed", function(ev: MissionEvent)
    if ev.chr ~= nil then
        local l = latch(ev.chr)
        -- a guard hit outranks a death seen in the same window (the "hit" reactions come first in the specs)
        if not (fresh(l.wit_at) and l.wit == "hit") then
            l.wit = ev.what
        end
        l.wit_at = clock()
    end
end)

-- The player g notices: the sight check of this tick, or a "guard_spotted" edge at most FRESH old. Not consumed.
function Sig.spotted(g: Target): number?
    local p = g:spotted_player()
    if p ~= nil then
        return p
    end
    local c = g.chr
    if c == nil then
        return nil
    end
    local l = latches[c]
    if l and fresh(l.spot_at) then
        return l.spot_player or 0
    end
    return nil
end

-- A player's shot passed g (a "guard_near_miss" at most FRESH old), consumed by this call.
function Sig.take_miss(g: Target): boolean
    local c = g.chr
    if c == nil then
        return false
    end
    local l = latches[c]
    if l and fresh(l.miss_at) then
        l.miss_at = nil
        return true
    end
    return false
end

-- What g saw happen to another guard ("hit" | "death", a "guard_witnessed" at most FRESH old), consumed.
function Sig.take_witnessed(g: Target): string?
    local c = g.chr
    if c == nil then
        return nil
    end
    local l = latches[c]
    if l and fresh(l.wit_at) then
        l.wit_at = nil
        return l.wit
    end
    return nil
end

return Sig
