-- dam_guards: the Dam's own guard behaviours (specs/dam.md rules 12, 13, 15, 16): stand and shoot, the tower
-- snipers, the post guard and the alert-only guards. A module: its top level defines functions only.
-- "The player" is the API's default target (the guard's target player, else the nearest living one).

local guard_ai = require("guard_ai")
local L = require("campaign_lib")

local M = {}

local STEP = 0.25
local FAST = 0.1   -- the reaction polls (the one-update signals arrive as events and are latched)

-- Runs to the player, then lives on as the standard generic guard.
function M.run_then_standard(g: Target)
    g:run_to_player("run")
    L.wait_done(g)
    if not L.dying_or_dead(g) then
        guard_ai.start(g, "standard")
    end
end

-- The one-update guard signals as a latch (events, never the one-tick queries): one on() per { event name, filter
-- fields }, filtered by the guard's chr; take() reads and clears it, off() unsubscribes.
type Latch = { take: () -> boolean, off: () -> () }
local function latch(g: Target, subs: { { any } }): Latch
    local fired = false
    local conns: { Connection } = {}
    local c = g.chr
    if c ~= nil then
        for _, sub in subs do
            local filter: EventFilter = { chr = c }
            if sub[2] ~= nil then
                filter.what = sub[2]
            end
            table.insert(conns, on(sub[1], function(_ev: MissionEvent)
                fired = true
            end, filter))
        end
    end
    return {
        take = function(): boolean
            local f = fired
            fired = false
            return f
        end,
        off = function()
            for _, k in conns do
                k:off()
            end
        end,
    }
end

-- Rule 12's signals: a near miss, another guard seen shot or killed, the player spotted.
local RULE12_EVENTS = { { "guard_near_miss" }, { "guard_witnessed" }, { "guard_spotted" } }

-- Rule 12's trigger: one of those signals since the last call, or it sees the player, or line of sight (states).
local function triggered(g: Target, signals: Latch): boolean
    if signals.take() then
        return true
    end
    return g:sees_player() or g:line_to_player() ~= nil
end

-- rule 12: stand and shoot. Never walks. Triggered: 10% run to the player then standard guard for good; 47% fire
-- kneeling; 43% fire standing; after a shot it stands and re-tests at once.
-- rule 13 (tower = true): no run; 22% throw a grenade, 78% fire standing.
function M.stand_and_shoot(g: Target, tower: boolean?)
    return spawn(function()
        local signals = latch(g, RULE12_EVENTS)
        while not L.dying_or_dead(g) do
            if triggered(g, signals) then
                local r = math.random() * 100
                if tower then
                    -- rule 13
                    if r < 22 and g:throw_grenade() then
                        L.wait_done(g)
                    else
                        g:fire_at(nil, { stance = "stand" })
                        L.wait_done(g)
                    end
                else
                    if r < 10 then
                        signals.off()
                        M.run_then_standard(g)
                        return
                    elseif r < 57 then
                        g:fire_at(nil, { stance = "kneel" })
                    else
                        g:fire_at(nil, { stance = "stand" })
                    end
                    L.wait_done(g)
                end
                if not L.dying_or_dead(g) then
                    g:halt()   -- stands again
                end
            else
                wait(FAST)
            end
        end
        signals.off()
    end)
end

-- rule 15: the post guard (chr 0x02), post at pad 4.
local POST_MOVES: { { any } } = { { 20, "sidestep" }, { 20, "walk_fire" }, { 20, "run_fire" }, { 20, "fire" }, { 22, "fire_kneel" } }

local function do_move(g: Target, kind: string): boolean
    if kind == "fire" then
        return g:fire_at(nil, { stance = "stand" })
    elseif kind == "fire_kneel" then
        return g:fire_at(nil, { stance = "kneel" })
    end
    return g:combat_move(kind)
end

local function pick_move(g: Target): boolean
    local total = 0
    for _, e in POST_MOVES do
        total += e[1]
    end
    local r = math.random() * total
    local first = #POST_MOVES
    for i, e in POST_MOVES do
        r -= e[1]
        if r < 0 then
            first = i
            break
        end
    end
    for i = first, #POST_MOVES do
        if do_move(g, POST_MOVES[i][2]) then
            return true
        end
    end
    return false
end

function M.post_guard(g: Target, post: number)
    return spawn(function()
        local home = pad(post)
        g:run_to(post, "walk")
        L.wait_done(g)
        -- a player's shot just missed it, or it saw another guard shot
        local signals = latch(g, { { "guard_near_miss" }, { "guard_witnessed", "hit" } })
        local chaseAt = -1          -- clock() when the last chase began (-1: none pending)
        local function sight(): boolean
            return g:line_to_player() ~= nil or g:sees_player()
        end
        local function trigger(): boolean
            return signals.take() or sight()
        end
        -- one short run toward the player of at most 2 s
        local function short_run()
            g:run_to_player("run")
            local t = clock() + 2
            while clock() < t and g:is_moving() and not L.dying_or_dead(g) do
                wait(FAST)
            end
            if not L.dying_or_dead(g) then
                g:halt()
            end
        end
        while not L.dying_or_dead(g) do
            -- watching
            if trigger() then
                -- fights, a new move after each one while it still sees the player
                repeat
                    if not pick_move(g) then
                        wait(STEP)
                    else
                        L.wait_done(g)
                    end
                until L.dying_or_dead(g) or not sight()
                if L.dying_or_dead(g) then
                    break
                end
                -- lost sight
                if home:distance() > 2000 then   -- the nearest living player to the post (co-op notes)
                    if L.chance(98) then
                        if not L.chance(22) then
                            short_run()
                        end
                        chaseAt = clock()
                    else
                        g:run_to(post, "run")
                        L.wait_done(g)
                    end
                end
            elseif chaseAt >= 0 and clock() >= chaseAt + 5 then
                -- still watching 5 s after a chase began: back to the post, one more short run, then watch
                chaseAt = -1
                wait(math.random())
                g:run_to(post, "run")
                L.wait_done(g)
                if not L.dying_or_dead(g) then
                    short_run()
                end
            else
                wait(FAST)
            end
        end
        signals.off()
    end)
end

-- rule 16: alert-only guards stand idle until they hear the player fire, see another guard shot, have line of
-- sight to a player, or the alarm is on; then they run to the player and become standard guards.
function M.alert_only(g: Target)
    return spawn(function()
        -- it hears the player fire, or sees another guard shot (events); line of sight and the alarm are states
        local signals = latch(g, { { "guard_heard" }, { "guard_witnessed", "hit" } })
        while not L.dying_or_dead(g) do
            if signals.take() or g:line_to_player() ~= nil or alarm.is_on() then
                signals.off()
                M.run_then_standard(g)
                return
            end
            wait(FAST)
        end
        signals.off()
    end)
end

return M
