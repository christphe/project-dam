-- campaign_lib: the helpers several levels of the campaign share (#165), one clean copy of each.
-- A require()d module: its top level only defines functions and subscribes to events (it never waits).
--
--   local L = require("campaign_lib")
--
-- Players (0-based numbers; mission.players() says how many the game has):
--   count()                     how many players the game has
--   in_game(n)                  player n is one of them
--   players()                   every player of the game, living or not
--   alive(n)                    player n is in the game and not dead (a co-op death lasts until the revive)
--   living()                    the living players; any_alive(): at least one
--   health(n)                   a player's health 0..1 (player(n):health())
--   nearest_player(t)           the living player nearest to target t, and its distance (nil, math.huge: none)
--   player_in_room(p)           the first living player in the room of pad p, or nil
--   player_in_rooms(pads)       the first living player in the room of any of these pads, or nil
--   all_farther(t, r)           every living player is farther than r from t
--   any_in_cutscene()           a player of the game is in a cutscene
--   freeze_all(on)              every living player frozen / free
--   holds_gun(n, a, b)          player n holds an item a..b in the right hand
--   is_real_weapon(item)        items 2-22 and 24-29 (not unarmed, fist, the watch laser, gadgets)
-- Random rolls:
--   chance(pct)                 true with pct percent
--   per_poll(pct, dt?)          a per-tick chance (60 ticks a second) as the percent chance over one poll of dt s
--   tick_chance(pct, dt?)       chance(per_poll(pct, dt))
--   roll(weights)               an index of weights, the roll over their sum (the last index as a floor)
--   roll_index(weights)         an index of weights read as percent buckets; #weights + 1 for the remainder to 100
--   pick(choices)               { {weight, value}, ... }: one value, the roll over the sum of the weights
--   grenade_pct(v)              a setup 0..255 grenade probability as the 0..100 GuardStats rating
-- Text: top(id) / bottom(id): a game text line by id at the top / bottom of the screen.
-- Waits:
--   wait_intro()                returns once the level's intro is over
--   wait_until(cond, timeout?, step?)   polls cond (every step, default STEP); false when timeout ran out first
--   wait_idle(g, timeout?, opts?)       waits while the guard is busy (opts: settle, alive; see the function)
--   wait_anim(g, timeout?)      waits for the end of g's anim()
--   anim(g, id, from?, to?, hold?)      plays an animation and waits for its end
--   run_near(g, p, radius, speed?) / walk_to(g, p): a guard to a pad, waiting until there (or stopped)
--   fade_out(seconds?, opts?) / fade_in(seconds?, opts?) / wait_fade(opts?): fades, waiting until done
--   end_on_skip(opts?)          from now on a button press fades out and ends the level (mission.finish)
--   end_level(failed?) / end_on_button(failed?) / ending(): the level's end, once (a latch), and its skip
-- Guards:
--   gone(g)                     dead, dying or not there; alive_guard(g): the opposite; not_dead(g)
--   dying_or_dead(g) / not_dying(g); wait_done(g): one STEP, then waits while busy and not dying_or_dead
--   present(g)                  spawned and not yet removed (alive, dying, or a body still lying there)
--   departed(g)                 not present (nil included): killed and faded away, removed, never there
--   spawn_retry(opts, give_up?, every?) spawn_guard() tried again until it works or give_up() holds
--   miss_watch(g)               a function true when a player's shot passed g since its last call
--   watch_witness(g) / take_witness(g)  a guard's per-tick "witnessed" flag, latched (sampled every tick
--                               for half a second after any guard damage or death)
--   run_generic(g, mode, opts?) one generic guard_ai mode as a sub-action; returns when it ends (false: g died)
--   run_task(g, fn)             one behaviour task per guard: starting one cancels the previous
-- Fights (the shared hunter: one attack drawn 4/16/12/8/20/20/21 over grenade, sidestep, firing roll, fire
-- walking, fire running, stand and fire, kneel and fire):
--   attack(g, opts?)            one attack by that roll (opts: player, weights, fallback; see the function)
--   hunt_nearest(g, opts)       goes for the nearest living player and fights when in range and in sight
--   attack_once(g) / hunt(g, opts) / check_remove(g, opts) / start_hunter(g, opts): the pursuit hunter (runs
--                               at the guard's player, attacks in range, removed beyond a distance)
--   cover_hunter(g, opts)       the reinforcement that hunts and takes cover (kneels and fires) on a 2% roll or
--                               when the level's spawning goes off
--   post_guard(g)               the posted rifleman: holds its spot, covers the player, fires or kneels and pops up
--   spawn_roll(at)              the weapon-in-hand spawn chance: 21% weak weapon, 80% mid weapon, else always

local guard_ai = require("guard_ai")

local M = {}

M.STEP = 0.25            -- seconds between two decisions of a polled behaviour
M.MAX_PLAYERS = 4
local STEP = M.STEP

-- ------------------------------------------------------------------ players

-- Players that died (co-op: until revived). There is no player death query, so the events tell.
local dead: { [number]: boolean } = {}
on("player_death", function(ev: MissionEvent)
    if ev.player ~= nil then
        dead[ev.player] = true
    end
end)
on("player_revived", function(ev: MissionEvent)
    if ev.player ~= nil then
        dead[ev.player] = nil
    end
end)

function M.count(): number
    return mission.players()
end

function M.in_game(n: number): boolean
    return n >= 0 and n < mission.players()
end

function M.players(): { number }
    local out = {}
    for n = 0, mission.players() - 1 do
        table.insert(out, n)
    end
    return out
end

function M.alive(n: number): boolean
    return M.in_game(n) and not dead[n]
end

function M.living(): { number }
    local out = {}
    for n = 0, mission.players() - 1 do
        if not dead[n] then
            table.insert(out, n)
        end
    end
    return out
end

function M.any_alive(): boolean
    return #M.living() > 0
end

function M.health(n: number): number
    return player(n):health()
end

function M.nearest_player(t: Target): (number?, number)
    local best: number? = nil
    local bestd = math.huge
    for _, n in M.living() do
        local d = t:distance_to(player(n))
        if d < bestd then
            best, bestd = n, d
        end
    end
    return best, bestd
end

function M.player_in_room(p: Pad): number?
    for _, n in M.living() do
        if player(n):in_room_of(p) then
            return n
        end
    end
    return nil
end

function M.player_in_rooms(pads: { Pad }): number?
    for _, n in M.living() do
        for _, p in pads do
            if player(n):in_room_of(p) then
                return n
            end
        end
    end
    return nil
end

function M.all_farther(t: Target, radius: number): boolean
    for _, n in M.living() do
        if t:distance_to(player(n)) <= radius then
            return false
        end
    end
    return true
end

function M.any_in_cutscene(): boolean
    for _, n in M.players() do
        if player(n):in_cutscene() then
            return true
        end
    end
    return false
end

function M.freeze_all(on: boolean)
    for _, n in M.living() do
        player(n):set_frozen(on)
    end
end

function M.holds_gun(n: number, first: number, last: number): boolean
    local it = player(n):held_item()
    return it >= first and it <= last
end

function M.is_real_weapon(item: number): boolean
    return (item >= 2 and item <= 22) or (item >= 24 and item <= 29)
end

-- ------------------------------------------------------------------ random rolls

function M.chance(pct: number): boolean
    return math.random() * 100 < pct
end

function M.per_poll(percent_per_tick: number, dt: number?): number
    local p = percent_per_tick / 100
    return (1 - (1 - p) ^ (60 * (dt or STEP))) * 100
end

function M.tick_chance(percent_per_tick: number, dt: number?): boolean
    return math.random() < 1 - (1 - percent_per_tick / 100) ^ (60 * (dt or STEP))
end

function M.roll(weights: { number }): number
    local total = 0
    for _, w in weights do
        total += w
    end
    local r = math.random() * total
    for i, w in weights do
        if r < w then
            return i
        end
        r -= w
    end
    return #weights
end

function M.roll_index(weights: { number }): number
    local r = math.random() * 100
    for i, w in weights do
        if r < w then
            return i
        end
        r -= w
    end
    return #weights + 1
end

function M.pick(choices: { { any } }): any
    local total = 0
    for _, c in choices do
        total += c[1]
    end
    local r = math.random() * total
    for _, c in choices do
        r -= c[1]
        if r < 0 then
            return c[2]
        end
    end
    return choices[#choices][2]
end

function M.grenade_pct(v: number): number
    return v * 100 / 255
end

-- ------------------------------------------------------------------ text

function M.top(id: number | string)
    local s = text(id)
    if s then
        hud.top(s)
    end
end

function M.bottom(id: number | string)
    local s = text(id)
    if s then
        hud.bottom(s)
    end
end

-- ------------------------------------------------------------------ waits

function M.wait_intro()
    if cinema.in_intro() then
        wait(event("intro_done"))
    end
end

function M.wait_until(cond: () -> boolean, timeout: number?, step: number?): boolean
    local t0 = clock()
    while not cond() do
        if timeout ~= nil and clock() - t0 >= timeout then
            return false
        end
        wait(step or STEP)
    end
    return true
end

export type IdleOpts = {
    settle: boolean?,                 -- wait one STEP first (an order given this tick is not yet "busy")
    alive: ((g: Target) -> boolean)?, -- what keeps the wait going (default g:exists())
}

-- Waits while the guard is busy (a move, a burst, an animation, a hit reaction), at most `timeout` seconds.
function M.wait_idle(g: Target, timeout: number?, opts: IdleOpts?)
    local o: IdleOpts = opts or {}
    local alive = o.alive or function(x: Target): boolean return x:exists() end
    if o.settle then
        wait(STEP)
    end
    local t0 = clock()
    while alive(g) and g:is_busy() do
        if timeout ~= nil and clock() - t0 >= timeout then
            return
        end
        wait(STEP)
    end
end

function M.wait_anim(g: Target, timeout: number?)
    local t0 = clock()
    while g:exists() and not g:anim_done() do
        if timeout ~= nil and clock() - t0 > timeout then
            return
        end
        wait(STEP)
    end
end

function M.anim(g: Target, id: number, from: number?, to: number?, hold: boolean?)
    g:anim(id, { from = from, to = to, hold = hold })
    M.wait_until(function(): boolean
        return g:anim_done()
    end)
end

-- "runs to pad N (until within x m)" of the specs.
function M.run_near(g: Target, p: number, radius: number, speed: string?)
    g:run_to(p, speed or "run")
    M.wait_until(function(): boolean
        return g:distance_to(pad(p)) <= radius or not g:is_moving()
    end)
end

function M.walk_to(g: Target, p: number)
    g:run_to(p, "walk")
    M.wait_until(function(): boolean
        return not g:is_moving()
    end)
end

export type FadeOpts = {
    settle: number?,   -- seconds waited before the first look at screen.fading() (default 0.1; 0: none)
    poll: number?,     -- seconds between two looks (default STEP)
}

function M.wait_fade(opts: FadeOpts?)
    local o: FadeOpts = opts or {}
    local settle = o.settle or 0.1
    if settle > 0 then
        wait(settle)
    end
    while screen.fading() do
        wait(o.poll or STEP)
    end
end

function M.fade_out(seconds: number?, opts: FadeOpts?)
    screen.fade_out(seconds)
    M.wait_fade(opts)
end

function M.fade_in(seconds: number?, opts: FadeOpts?)
    screen.fade_in(seconds)
    M.wait_fade(opts)
end

-- An outro the players may skip: a button press fades to black (opts: the fade's waits) and ends the level.
function M.end_on_skip(opts: FadeOpts?): Task
    return spawn(function()
        while not cinema.skipped() do
            wait(STEP)
        end
        M.fade_out(nil, opts)
        mission.finish()
    end)
end

-- The level's end, once (later calls do nothing): fades to black, then the level ends (failed: as a failure,
-- else mission.finish()). ending() is true from the first call.
local is_ending = false
function M.end_level(failed: boolean?)
    if is_ending then
        return
    end
    is_ending = true
    M.fade_out(nil, { settle = 0 })
    if failed then
        mission.fail()
    else
        mission.finish()
    end
end

function M.ending(): boolean
    return is_ending
end

-- From now on a button press ends the level (end_level(failed)), unless it is already ending.
function M.end_on_button(failed: boolean?)
    spawn(function()
        while not is_ending do
            if cinema.skipped() then
                M.end_level(failed)
                return
            end
            wait(STEP)
        end
    end)
end

-- ------------------------------------------------------------------ guards

-- Killed and faded away, removed, or never there (nil).
function M.departed(g: Target?): boolean
    if g == nil then
        return true
    end
    return not M.present(g)
end

function M.gone(g: Target): boolean
    return (not g:exists()) or g:is_dying() or g:is_dead()
end

function M.alive_guard(g: Target): boolean
    return g:exists() and not g:is_dying() and not g:is_dead()
end

function M.not_dead(g: Target): boolean
    return not g:is_dead()
end

function M.dying_or_dead(g: Target): boolean
    return g:is_dead() or g:is_dying()
end

function M.not_dying(g: Target): boolean
    return not (g:is_dead() or g:is_dying())
end

-- After an order: one STEP, then waits while the guard is busy and neither dying nor dead.
function M.wait_done(g: Target)
    M.wait_idle(g, nil, { settle = true, alive = M.not_dying })
end

function M.present(g: Target): boolean
    return g:exists() or g:is_dying() or g:pos() ~= nil
end

-- spawn_guard, tried again every `every` seconds (default 1) until it works or `give_up()` holds.
function M.spawn_retry(opts: any, give_up: (() -> boolean)?, every: number?): Target?
    while true do
        if give_up and give_up() then
            return nil
        end
        local g = spawn_guard(opts)
        if g then
            return g
        end
        wait(every or 1)
    end
end

-- A poll would miss the one-tick was_shot_at() flag: this counts near_misses() instead.
function M.miss_watch(g: Target): () -> boolean
    local seen = g:near_misses()
    return function(): boolean
        local now = g:near_misses()
        if now > seen then
            seen = now
            return true
        end
        return g:was_shot_at()
    end
end

-- "witnessed" is a per-tick engine flag a 0.25 s poll would miss: each guard damage or death opens a short window
-- (30 ticks) in which the watched guards are sampled every tick, the result latched until take_witness() reads it.
local witness_listeners: { Target } = {}
local witness_latch: { [Target]: string } = {}
local witness_hooked = false
local function sample_witnesses()
    for _ = 1, 30 do
        for _, g in witness_listeners do
            local kind = g:witnessed()
            if kind and g:exists() then
                witness_latch[g] = kind
            end
        end
        wait(1 / 60)
    end
end

function M.watch_witness(g: Target)
    if not witness_hooked then
        witness_hooked = true
        on("guard_damaged", function() sample_witnesses() end)
        on("death", function() sample_witnesses() end)
    end
    table.insert(witness_listeners, g)
end

function M.take_witness(g: Target): string?
    local k = witness_latch[g]
    witness_latch[g] = nil
    return k
end

-- Runs one generic mode on g as a sub-action and returns when the mode has ended (handed over to "inert",
-- guard_ai.on_end), leaving g to the caller again. opts.alive: what keeps the wait going (default not_dead);
-- opts.timeout: at most that many seconds. Returns false when g died meanwhile.
export type GenericOpts = { alive: ((g: Target) -> boolean)?, timeout: number? }
function M.run_generic(g: Target, mode: string, opts: GenericOpts?): boolean
    local o: GenericOpts = opts or {}
    local alive = o.alive or M.not_dead
    local ended = false
    guard_ai.start(g, mode, { fallback = "inert" })
    guard_ai.on_end(g, function()
        ended = true
    end)
    local t0 = clock()
    while not ended and alive(g) and (o.timeout == nil or clock() - t0 < o.timeout) do
        wait(STEP)
    end
    guard_ai.stop(g)
    return not g:is_dead()
end

-- One behaviour task per guard (by chr): starting a new one cancels the previous.
local tasks: { [number]: Task } = {}
function M.run_task(g: Target, fn: () -> ())
    local c = g.chr or -1
    local old = tasks[c]
    if old then
        old:cancel()
    end
    tasks[c] = spawn(fn)
end

-- ------------------------------------------------------------------ fights

M.ATTACK_WEIGHTS = { 4, 16, 12, 8, 20, 20, 21 }

export type AttackOpts = {
    player: number?,          -- the player to fire at (nil: the guard's own target player)
    weights: { number }?,     -- the 7 weights (default ATTACK_WEIGHTS); the roll is over their sum
    fallback: string?,        -- an attack that cannot start now: "next" tries the following ones in order;
                              -- "stand" (default) fires standing instead
}

local function try_attack(g: Target, i: number, who: Target?): boolean
    if i == 1 then
        return g:throw_grenade()
    elseif i == 2 then
        return g:combat_move("sidestep")
    elseif i == 3 then
        return g:combat_move("roll_fire")
    elseif i == 4 then
        return g:combat_move("walk_fire")
    elseif i == 5 then
        return g:combat_move("run_fire")
    elseif i == 6 then
        return g:fire_at(who, { stance = "stand" })
    else
        return g:fire_at(who, { stance = "kneel" })
    end
end

-- One attack by one roll over the weights. true: an attack started.
function M.attack(g: Target, opts: AttackOpts?): boolean
    local o: AttackOpts = opts or {}
    local weights = o.weights or M.ATTACK_WEIGHTS
    local who: Target? = if o.player ~= nil then player(o.player) else nil
    local first = M.roll(weights)
    if o.fallback == "next" then
        for i = first, #weights do
            if try_attack(g, i, who) then
                return true
            end
        end
        return false
    end
    if try_attack(g, first, who) then
        return true
    end
    return g:fire_at(who, { stance = "stand" })
end

-- The hunting loop: the guard goes for the nearest living player's position (speed() picks "run" / "sprint" at each
-- new run), re-aimed whenever it stops and every `reaim` seconds when given; whenever that player is within `range`
-- units and in its line of sight it attacks (fallback "next"), waits for the action to end and starts over.
-- Returns true when stop() held, false when the guard is gone.
export type HuntOpts = { range: number, speed: () -> string, reaim: number?, stop: (() -> boolean)? }
function M.hunt_nearest(g: Target, o: HuntOpts): boolean
    local last = -1e9
    while true do
        if not g:exists() then
            return false
        end
        if o.stop and o.stop() then
            return true
        end
        local p, d = M.nearest_player(g)
        if p ~= nil and d <= o.range and g:line_to_player() ~= nil then
            if M.attack(g, { player = p, fallback = "next" }) then
                M.wait_idle(g, nil, { settle = true })
            end
            last = -1e9   -- after an action out of sight: go for the player again at once
        elseif p ~= nil and (not g:is_moving() or (o.reaim ~= nil and clock() - last >= o.reaim)) then
            if g:run_to_player(o.speed(), p) then
                last = clock()
            else
                wait(1)   -- no route, hit, or the game's chase cap: try again shortly
            end
        end
        wait(STEP)
    end
end

-- The pursuit hunter: runs at the guard's player, attacks when close and in sight, until it dies or is removed.
export type HunterOpts = {
    sprint_beyond: number?,   -- sprints when farther than this from the player (nil: always runs)
    attack_range: number,     -- attacks when this close with a line of sight
    remove_beyond: number?,   -- removed at once when farther than this (nil: never)
    replan: number?,          -- seconds between two route plans (nil: only when it arrives or stops)
}

-- One attack (fallback: fire standing), then waits for it to end (at most 10 s).
function M.attack_once(g: Target)
    M.attack(g)
    wait(STEP)
    M.wait_idle(g, 10, { alive = M.alive_guard })
end

-- Removed at once when beyond opts.remove_beyond; true when it was removed.
function M.check_remove(g: Target, o: HunterOpts): boolean
    if o.remove_beyond and g:distance() > o.remove_beyond then
        g:remove()
        return true
    end
    return false
end

-- Pursuit + attack, until the guard dies or is removed. Runs in the caller's coroutine.
function M.hunt(g: Target, o: HunterOpts)
    local replan_at = 0
    while M.alive_guard(g) do
        if M.check_remove(g, o) then
            return
        end
        local d = g:distance()
        if d <= o.attack_range and g:line_to_player() ~= nil then
            M.attack_once(g)
            replan_at = 0
        elseif clock() >= replan_at or not g:is_moving() then
            local speed = if o.sprint_beyond and d > o.sprint_beyond then "sprint" else "run"
            g:run_to_player(speed)
            replan_at = if o.replan then clock() + o.replan else math.huge
        end
        wait(STEP)
    end
end

function M.start_hunter(g: Target, o: HunterOpts)
    M.run_task(g, function()
        M.hunt(g, o)
    end)
end

-- The covering reinforcement: hunts the nearest player (sprinting beyond sprint_beyond) and attacks with one
-- random move (fallback: fire standing; weights 4/16/12/8/20/20/20) when within attack_range and in sight or just
-- missed; on a 2% roll instead, or when opts.spawning_on() turns false, it takes cover: runs within cover_range,
-- kneels and fires at the player in sight, until a shot misses it or the player is beyond cover_exit.
export type CoverOpts = {
    spawning_on: () -> boolean,   -- the level's spawning state
    cover_exit: number,           -- the distance that ends cover
    attack_range: number?,        -- default 2000
    sprint_beyond: number?,       -- default 4000
    cover_range: number?,         -- default 2000
}
local COVER_WEIGHTS = { 4, 16, 12, 8, 20, 20, 20 }
function M.cover_hunter(g: Target, opts: CoverOpts)
    local attack_range = opts.attack_range or 2000
    local sprint_beyond = opts.sprint_beyond or 4000
    local cover_range = opts.cover_range or 2000
    spawn(function()
        local missed = M.miss_watch(g)
        local pending_miss = false
        local function alive(): boolean
            return g:exists() and not g:is_dying()
        end
        local function was_missed(): boolean
            if missed() then pending_miss = true end
            local m = pending_miss
            pending_miss = false
            return m
        end

        local function cover()
            while alive() and g:distance() > cover_range do
                if not g:is_moving() then
                    g:run_to_player("run")
                end
                if missed() then pending_miss = true end
                wait(STEP)
            end
            if not alive() then return end
            g:kneel()
            g:fire_at(nil, { stance = "kneel", aim_only = true })
            pending_miss = false
            while alive() do
                if was_missed() or g:distance() > opts.cover_exit then
                    return                            -- back to hunting
                end
                if g:sees_player() and not g:is_busy() then
                    g:fire_at(nil, { stance = "kneel" })
                end
                wait(STEP)
            end
        end

        local was_on = opts.spawning_on()
        while alive() do
            local on = opts.spawning_on()
            if was_on and not on then
                was_on = false
                cover()                               -- spawning went off
            else
                was_on = on
                if g:is_busy() then
                    if missed() then pending_miss = true end
                    wait(STEP)
                elseif (g:distance() <= attack_range and g:sees_player()) or was_missed() then
                    if math.random() < 0.02 then
                        cover()                       -- 2% takes cover instead
                    else
                        M.attack(g, { weights = COVER_WEIGHTS })
                        wait(STEP)
                    end
                else
                    g:run_to_player(if g:distance() > sprint_beyond then "sprint" else "run")
                    wait(STEP)
                    M.wait_until(function(): boolean
                        if missed() then pending_miss = true end
                        return not alive() or pending_miss or not g:is_moving()
                            or (g:distance() <= attack_range and g:sees_player())
                            or (was_on and not opts.spawning_on())
                    end)
                end
            end
        end
    end)
end

-- The posted rifleman: never leaves its spot and keeps its gun on the player. When a shot from a player misses it,
-- it sees a guard shot or killed, it spots a player or has a clear line to one: 59% it fires from where it stands,
-- else it kneels and pops up to fire at a random moment (about 2% per frame: typically within a second, at most
-- 4 s), then aims again. For as long as it lives.
function M.post_guard(g: Target)
    guard_ai.stop(g)
    g:halt()
    local aiming = false
    while g:exists() do
        local p = M.nearest_player(g)
        if p ~= nil and not g:is_busy() then
            local missed = g:was_shot_at()
            local saw = g:witnessed()
            local spotted = g:spotted_player()
            local line = g:line_to_player()
            if missed or saw ~= nil or spotted ~= nil or line ~= nil then
                local who = spotted or line or p
                if not M.chance(59) then
                    g:kneel()
                    local frames = math.floor(math.log(1 - math.random()) / math.log(0.98))
                    wait(math.min(4, frames / 60))
                end
                g:fire_at(player(who), { stance = "stand" })
                M.wait_idle(g, 8)
                aiming = false
            end
            if not aiming then
                aiming = g:fire_at(player(p), { aim_only = true })
            end
        end
        wait(STEP)
    end
end

-- The weapon-in-hand spawn chance (of the living player nearest `at`): 21% with a weak weapon, 80% with a mid
-- weapon, always otherwise; false when no player lives.
local WEAK = { [1] = true, [2] = true, [3] = true, [4] = true, [5] = true, [17] = true }
local MID = { [7] = true, [8] = true, [24] = true }
function M.spawn_roll(at: Target): boolean
    local n = M.nearest_player(at)
    if n == nil then
        return false
    end
    local item = player(n):held_item()
    if WEAK[item] then
        return math.random() < 0.21
    end
    if MID[item] then
        return math.random() < 0.80
    end
    return true
end

return M
