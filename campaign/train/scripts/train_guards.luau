-- train_guards: the Train's own guard behaviours (specs/train.md rules 1-5): stand and fight, aim and fire,
-- ambusher, hunter, and the carriage combat moves they share. A module: its top level defines functions only.
--
-- Every behaviour runs in a task of its own (spawn) and ends when the guard dies or is gone. "The player" is the
-- API's default target (the guard's target player in co-op, else the nearest living one: co-op notes, rules 2/4).

local L = require("campaign_lib")

local M = {}

local STEP = 0.25          -- seconds per poll
local FAST = 0.1           -- the reaction polls (the one-update signals arrive as events and are latched)

-- The carriage moves (rules 1 and 4): 10% sidestep, 23% fire walking, 22% fire running, 22.5% fire standing,
-- 22.5% fire kneeling; a move that cannot be done now falls through to the next.
local CARRIAGE_MOVES = {
    { 10, "sidestep" },
    { 23, "walk_fire" },
    { 22, "run_fire" },
    { 22.5, "fire" },
    { 22.5, "fire_kneel" },
}
M.CARRIAGE_MOVES = CARRIAGE_MOVES

-- An index into a list of { weight, ... } entries, picked by weight.
function M.roll(list: { { any } }): number
    local total = 0
    for _, e in list do
        total += e[1]
    end
    local r = math.random() * total
    for i, e in list do
        r -= e[1]
        if r < 0 then
            return i
        end
    end
    return #list
end

-- One move by name; false when it cannot be done now.
function M.do_move(g: Target, kind: string): boolean
    if kind == "fire" then
        return g:fire_at(nil, { stance = "stand" })
    elseif kind == "fire_kneel" then
        return g:fire_at(nil, { stance = "kneel" })
    elseif kind == "grenade" then
        return g:throw_grenade()
    else
        return g:combat_move(kind)
    end
end

-- Rolls a move from the table and falls through to the next ones until one starts.
function M.pick_move(g: Target, moves: { { any } }): boolean
    local first = M.roll(moves)
    for i = first, #moves do
        if M.do_move(g, moves[i][2]) then
            return true
        end
    end
    return false
end

-- The one-update guard signals as a latch (events, never the one-tick queries): one on() per event name, filtered
-- by the guard's chr; take() reads and clears it, off() unsubscribes.
export type Latch = { take: () -> boolean, off: () -> () }
function M.latch(g: Target, names: { string }): Latch
    local fired = false
    local conns: { Connection } = {}
    local c = g.chr
    if c ~= nil then
        for _, name in names do
            table.insert(conns, on(name, function(_ev: MissionEvent)
                fired = true
            end, { chr = c }))
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

-- The signals of rules 1 and 2: a player's shot just missed it, it saw another guard shot or killed, it spotted
-- the player.
local TRIGGER_EVENTS = { "guard_near_miss", "guard_witnessed", "guard_spotted" }

-- The trigger of rules 1 and 2: one of those signals since the last call, or it sees the player, or it has line
-- of sight to a player (states, queried).
function M.triggered(g: Target, signals: Latch): boolean
    if signals.take() then
        return true
    end
    return g:sees_player() or g:line_to_player() ~= nil
end

-- rule 1: stand and fight. It stays where it is until triggered, then fights with the carriage moves, one after
-- another, for good.
function M.stand_and_fight(g: Target)
    return spawn(function()
        local signals = M.latch(g, TRIGGER_EVENTS)
        g:halt()
        while not L.dying_or_dead(g) and not M.triggered(g, signals) do
            wait(FAST)
        end
        signals.off()
        while not L.dying_or_dead(g) do
            if not M.pick_move(g, CARRIAGE_MOVES) then
                wait(STEP)
            else
                L.wait_done(g)
            end
        end
    end)
end

-- rule 2: aim and fire. Keeps its gun on the player without firing; each time it is triggered: 59% fire at the
-- player then aim again; 41% kneel and wait up to 4 s, popping up at a random moment (about 2% per tick, so about
-- 26% per quarter second) to aim and fire, then aim again.
function M.aim_and_fire(g: Target)
    return spawn(function()
        local signals = M.latch(g, TRIGGER_EVENTS)
        g:fire_at(nil, { aim_only = true })
        while not L.dying_or_dead(g) do
            if M.triggered(g, signals) then
                if L.chance(59) then
                    g:fire_at(nil, { stance = "stand" })
                    L.wait_done(g)
                else
                    g:kneel()
                    local untilT = clock() + 4
                    while clock() < untilT and not L.dying_or_dead(g) do
                        wait(STEP)
                        if L.chance(26) then
                            break
                        end
                    end
                    if not L.dying_or_dead(g) then
                        g:fire_at(nil, { stance = "stand" })
                        L.wait_done(g)
                    end
                end
                if not L.dying_or_dead(g) then
                    g:fire_at(nil, { aim_only = true })
                end
            else
                wait(FAST)
            end
        end
        signals.off()
    end)
end

-- rule 4: hunter. Runs at the player; on line of sight fights with the carriage moves while it sees the player;
-- beyond 2000 units it runs at him again. Losing sight after a move: 22% waits for sight; 78% runs toward the
-- player for up to 1 s, then holds up to 10 s (fights at once on sight or when shot at); after the hold, 22%
-- another short probe, 78% a full run. An invincible hunter loses its invincibility once hit 6 times.
function M.hunter(g: Target)
    return spawn(function()
        local shots = M.latch(g, { "guard_near_miss", "guard_damaged" })   -- "shot at": missed or hit
        local function check_invincible()
            if g:get_flag("invincible") and g:hits() >= 6 then
                g:set_flag("invincible", false)
            end
        end
        local function shot_at(): boolean
            return shots.take()
        end
        local function fight()
            -- keeps fighting while it sees the player
            repeat
                check_invincible()
                if g:distance() > 2000 then
                    return
                end
                if not M.pick_move(g, CARRIAGE_MOVES) then
                    wait(STEP)
                else
                    L.wait_done(g)
                end
            until L.dying_or_dead(g) or not (g:sees_player() or g:line_to_player() ~= nil)
        end
        -- the 1 s probe and the 10 s hold; true: it got sight (or was shot at) and should fight
        local function probe(): boolean
            g:run_to_player("run")
            local t = clock() + 1
            while clock() < t and not L.dying_or_dead(g) do
                wait(FAST)
                if g:line_to_player() ~= nil then
                    return true
                end
            end
            g:halt()
            shots.take()   -- only a shot during the hold counts
            t = clock() + 10
            while clock() < t and not L.dying_or_dead(g) do
                wait(FAST)
                check_invincible()
                if g:sees_player() or g:line_to_player() ~= nil or shot_at() then
                    return true
                end
            end
            return false
        end

        local mode = "run"
        while not L.dying_or_dead(g) do
            check_invincible()
            if mode == "run" then
                -- runs at the player's position; fights as soon as it has line of sight
                g:run_to_player("run")
                local sight = false
                repeat
                    wait(FAST)
                    if g:line_to_player() ~= nil or g:sees_player() then
                        sight = true
                        break
                    end
                until L.dying_or_dead(g) or not g:is_moving()
                mode = if sight then "fight" else "lost"
            elseif mode == "fight" then
                fight()
                if L.dying_or_dead(g) then
                    break
                end
                mode = if g:distance() > 2000 then "run" else "lost"
            elseif mode == "lost" then
                if L.chance(22) then
                    -- stays and waits for sight
                    while not L.dying_or_dead(g) and not (g:sees_player() or g:line_to_player() ~= nil) do
                        wait(FAST)
                        check_invincible()
                        if g:distance() > 2000 then
                            break
                        end
                    end
                    mode = if g:distance() > 2000 then "run" else "fight"
                else
                    local again = true
                    mode = "run"
                    while again and not L.dying_or_dead(g) do
                        if probe() then
                            mode = "fight"
                            again = false
                        else
                            again = L.chance(22)   -- 22% another probe, 78% a full run
                        end
                    end
                end
            end
        end
        shots.off()
    end)
end

-- rule 3: ambusher. Waits; when it has line of sight to a player, a player comes within 200 units of its post,
-- or (tested every 3 s) a player is within 2000 units of its post, it becomes a hunter (rule 4).
function M.ambusher(g: Target, post: number)
    return spawn(function()
        local p = pad(post)
        local nextFar = clock() + 3
        while not L.dying_or_dead(g) do
            if g:line_to_player() ~= nil or p:near(200) then
                break
            end
            if clock() >= nextFar then
                nextFar = clock() + 3
                if p:near(2000) then
                    break
                end
            end
            wait(STEP)
        end
        if not L.dying_or_dead(g) then
            M.hunter(g)
        end
    end)
end

return M
