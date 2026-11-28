-- facility_bottling: the bottling room story (rules 23-35): Trevelyan's meeting (objective c), the traitor case,
-- the ambush, Ourumov's arrival and countdown, his squad, the reinforcements and the gas leak.
-- A require()d module: nothing here waits at the top level; B.start() spawns the tasks.
local guard_ai = require("guard_ai")
local U = require("campaign_lib")
local Sig = require("facility_signals")   -- the guard events, latched (see its header)

local B = {}
local STEP: number = U.STEP
local st = require("facility_util").state

local TREV = 0x43
local OURUMOV = 0x4E
local TT33 = 6
local AK47 = 8
local AK47_MODEL = 184
local HAT_MODEL = 217
local BODY_SOLDIER = 2
local BODY_OURUMOV = 7

-- rule 33: chr, spawn pad, cover pad
local SQUAD = {
    { 0x44, 0x5C, 0x73 },
    { 0x45, 0x5A, 0x74 },
    { 0x46, 0x5D, 0x75 },
    { 0x47, 0x5F, 0x76 },
    { 0x48, 0x59, 0x77 },
    { 0x49, 0x5B, 0x79 },
}
local REINFORCEMENTS = { 0x25, 0x26, 0x27 }   -- rule 34

-- the gas tanks (objective d, tags 0x01..0x0A) and the small tanks (rule 6, tags 0x17..0x1F)
local GAS_TAGS = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A,
    0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F }

local trev: Target = chr(TREV)
local hurt = false          -- a player hurt Trevelyan (rules 27-28)
local walked_away = false   -- rule 26 happened at least once
local met_player: number? = nil

local function stance(): string
    return if U.chance(50) then "stand" else "kneel"
end

-- rule 26: a player has just entered the room of pad 0x63 (edge, not presence).
local was_in_63 = false
local function entered_63(): boolean
    local now = U.player_in_room(0x63) ~= nil
    local entered = now and not was_in_63
    was_in_63 = now
    return entered
end

local function begin_ambush()
    if st.ambush then
        return
    end
    st.ambush = true
    spawn(B.ourumov)                          -- rule 30
    for _, def in SQUAD do                    -- rule 33
        spawn(function()
            B.squad_slot(def[1], def[2], def[3])
        end)
    end
end

-- rule 25: Trevelyan dies during the meeting or the ambush.
local function trev_died()
    U.top("0C10")
    alarm.trigger()
    begin_ambush()
end

-- rule 23: walk to pad 0x7C, then move from cover to cover towards the player until he sees the player.
-- "seen" | "hurt" | "away" | "dead"
local function approach(): string
    trev:run_to(0x7C, "walk")
    wait(STEP)
    while true do
        if trev:is_dead() then
            return "dead"
        elseif hurt then
            return "hurt"
        elseif entered_63() then
            return "away"
        elseif Sig.spotted(trev) ~= nil then
            return "seen"
        end
        if not trev:is_busy() then
            local c = trev:cover_pad()
            if c then
                trev:run_to(c, "run")
            end
        end
        wait(STEP)
    end
end

-- rule 24: the meeting. "done" | "hurt" | "away" | "dead"
local function meeting(): string
    local function interrupted(): string?
        if trev:is_dead() then
            return "dead"
        elseif hurt then
            return "hurt"   -- rule 27
        elseif entered_63() then
            return "away"
        end
        return nil
    end
    -- runs to the player until within 250 units
    while trev:distance() >= 250 do
        local r = interrupted()
        if r then
            return r
        end
        if not trev:is_busy() then
            trev:run_to_player("run")
        end
        wait(STEP)
    end
    trev:halt()
    met_player = U.nearest_player(trev)
    U.top(if walked_away then "0C15" else "0C09")
    local t0 = clock()
    local steps: { { any } } = {
        { 7, function() U.top("0C0A") end },
        { 10, function()
            U.top("0C0B")
            objectives.complete("c")
        end },
        { 15, function()
            local h = if met_player then U.health(met_player) else 1
            local id = if h < 0.3 then "0C18" elseif h < 0.7 then "0C17" else "0C0C"
            U.top(id)
        end },
        { 20, function() U.top("0C0D") end },
        { 25, function() U.top("0C0E") end },
        { 28, function() alarm.trigger() end },
        { 30, function() U.top("0C0F") end },
    }
    local i = 1
    local walking = false
    while i <= #steps do
        local r = interrupted()
        if r then
            return r
        end
        if clock() - t0 >= steps[i][1] then
            steps[i][2]()
            i += 1
        end
        -- walks back towards the player whenever the player is more than 300 units away
        if trev:distance() > 300 then
            if not trev:is_busy() then
                trev:run_to_player("walk")
                walking = true
            end
        else
            if walking then
                trev:halt()
                walking = false
            end
            trev:face_player()
        end
        wait(STEP)
    end
    return "done"
end

-- rule 28: the traitor case.
local function traitor()
    U.top("0C25")
    objectives.fail("c")
    local turned = clock()
    local said = false
    guard_ai.stop(trev)
    while not trev:is_dead() do
        if trev:line_to_player() ~= nil then
            if not said and clock() - turned >= 15 then
                U.top("0C26")
                said = true
            end
            if not trev:is_busy() then
                trev:fire_at(nil)
            end
        elseif trev:distance() > 1000 and not trev:is_busy() then
            trev:run_to_player("run")
        end
        wait(STEP)
    end
    -- rule 28: on his death, rule 25 in full: its text, the alarm, and the ambush begins (Ourumov and the squad)
    trev_died()
end

-- rule 29: the ambush: back to pad 0x7C, then kneel and fire at the squad until Ourumov arrives.
local function ambush()
    local cover = pad(0x7C)
    trev:run_to(0x7C, "run")
    wait(STEP)
    while not trev:is_dead() and not st.ourumov_arrived and trev:distance_to(cover) > 150 do
        if not trev:is_busy() then
            trev:run_to(0x7C, "run")
        end
        wait(STEP)
    end
    while not st.ourumov_arrived do
        if trev:is_dead() then
            trev_died()   -- rule 25
            return
        end
        local target: Target = pad(0x6D)
        for n = 0x44, 0x48 do
            if chr(n):exists() then
                target = chr(n)
                break
            end
        end
        trev:fire_at(target, { stance = "kneel", aim_only = true })
        wait(1)
        if st.ourumov_arrived or trev:is_dead() then
            continue
        end
        trev:fire_at(target, { stance = "kneel" })
        wait(STEP)
        U.wait_idle(trev, 3, { alive = U.not_dead })
    end
end

-- rules 23-29: Trevelyan's story.
local function trevelyan()
    guard_ai.stop(trev)
    if not trev:exists() then
        return
    end
    -- Characters: health 8, no auto-aim, can shoot other characters
    trev:set_stats({ health = 8 })
    trev:set_flag("no_autoaim", true)
    trev:set_flag("can_shoot_guards", true)
    while true do
        local r = approach()   -- rule 23
        if r == "seen" then
            r = meeting()      -- rule 24
        end
        if r == "hurt" then
            traitor()          -- rules 27-28
            return
        elseif r == "dead" then
            if st.ambush or met_player ~= nil then
                trev_died()    -- rule 25 (during rule 24)
            else
                -- rule 23 / Objectives table: killed before the meeting, objective c fails at once (no text, no
                -- alarm, no ambush)
                objectives.fail("c")
            end
            return
        elseif r == "away" then
            -- rule 26: a player walked away: his line, then rule 23 again
            U.top("0C16")
            walked_away = true
            met_player = nil
        elseif r == "done" then
            begin_ambush()     -- rule 24 at 30 s
            ambush()           -- rule 29
            return
        end
    end
end

-- rule 34: reinforcements, once Ourumov fights, until the gas has leaked 5 s or Ourumov fled.
local function reinforcements()
    while not st.gas_done and not st.ourumov_fled do
        for _, n in REINFORCEMENTS do
            if not st.exiting and not chr(n):exists() then
                local g = spawn_guard({ chr = n, pad = 0x5C, body = BODY_SOLDIER })
                if g then
                    if g:on_screen() then
                        g:remove()   -- seen as it appears
                    else
                        g:give_weapon(AK47, { model = AK47_MODEL })
                        g:set_stats({ health = 8 })
                        guard_ai.start(g, "chase")   -- run to the player, then the standard guard
                    end
                end
            end
        end
        wait(STEP)
    end
end

-- rule 32: Ourumov flees once the gas has leaked 5 s or he was hit more than 5 times.
local function ourumov_flee(o: Target)
    st.ourumov_fled = true
    o:set_invincible(true)
    local dest = pad(0x17)
    o:run_to(0x17, "sprint")
    wait(STEP)
    while not o:is_dead() do
        if o:distance_to(dest) < 200 then
            o:fade_out()
            return
        elseif not o:is_busy() then
            o:run_to(0x17, "sprint")   -- stopped short: sprints again
        end
        wait(STEP)
    end
end

-- rule 32: the fight.
local function ourumov_fight(o: Target)
    st.fight = true
    st.ourumov_fighting = true
    spawn(reinforcements)   -- rule 34
    while not o:is_dead() do
        if st.gas_done or o:hits() > 5 then
            ourumov_flee(o)
            return
        end
        if not o:is_busy() then
            if not trev:is_dead() then
                o:fire_at(trev)
                trev:hit(8, TT33)   -- each round also hits Trevelyan with TT33 damage to body part 8
            elseif not o:combat_move("run_fire") and not o:combat_move("walk_fire") then
                o:fire_at(nil)
            end
        end
        wait(STEP)
    end
end

-- rules 31-32: Ourumov's arrival, Trevelyan's surrender and the countdown.
local function arrival(o: Target)
    st.ourumov_arrived = true
    music.play_extra(0)   -- the urgent x-track
    U.top("0C13")
    o:fire_at(trev, { aim_only = true })
    trev:set_health(1)
    trev:run_to(0x7A, "walk")
    local p7a = pad(0x7A)
    wait(STEP)
    while not trev:is_dead() and trev:distance_to(p7a) > 200 do
        if not trev:is_busy() then
            trev:run_to(0x7A, "walk")
        end
        wait(STEP)
    end
    if not trev:is_dead() then
        trev:halt()
        U.top("0C14")
        trev:raise_arms()
        spawn(function()
            wait(1)
            if not trev:is_dead() then
                trev:kneel()   -- stays kneeling until he dies
            end
        end)
    end
    -- rule 32: the countdown, from Trevelyan's last line
    local steps: { { any } } = { { 2, "0C24" }, { 6, "0C19" }, { 8, "0C1A" }, { 10, "0C1B" }, { 12, "0C1C" }, { 14, "0C1D" },
        { 16, "0C1E" }, { 18, "0C1F" }, { 20, "0C20" }, { 22, "0C23" } }
    local t0 = clock()
    local i = 1
    local misses = o:near_misses()
    while clock() - t0 < 23 and not o:is_dead() do
        local m = o:near_misses()
        if m > misses or Sig.take_miss(o) then
            st.fight = true   -- a player's shot near Ourumov starts the fight
        end
        misses = m
        if st.fight or st.gas_done then
            break
        end
        if i <= #steps and clock() - t0 >= steps[i][1] then
            local id = steps[i][2]
            U.top(id)
            if id == "0C23" and not trev:is_dead() then
                o:fire_at(trev)
                trev:hit(8, TT33)   -- the shot kills Trevelyan (health 1)
            end
            i += 1
        end
        wait(STEP)
    end
    ourumov_fight(o)
end

-- rule 30: Ourumov is created once on pad 0x58 (out of view) and runs to pad 0x7B.
function B.ourumov()
    local o: Target? = nil
    while o == nil do
        if st.exiting then
            return
        end
        o = spawn_guard({ chr = OURUMOV, pad = 0x58, body = BODY_OURUMOV })
        if o == nil then
            wait(STEP)
        end
    end
    local g = o :: Target
    g:set_stats({ armour = 100, health = 30, speed = 60, accuracy = 100, reaction = 100 })
    g:set_flag("no_autoaim", true)
    g:set_flag("can_shoot_guards", true)
    g:give_weapon(TT33, { model = 205 })
    local dest = pad(0x7B)
    g:run_to(0x7B, "run")
    wait(STEP)
    while not g:is_dead() and g:distance_to(dest) > 200 do
        if st.gas_done then
            ourumov_flee(g)
            return
        end
        if not g:is_busy() then
            g:run_to(0x7B, "run")
        end
        wait(STEP)
    end
    if g:is_dead() then
        return
    end
    if trev:is_dead() then
        st.ourumov_arrived = true
        ourumov_fight(g)   -- rule 31: Trevelyan already dead: he fights at once
    else
        arrival(g)
    end
end

-- rule 33: one squad member's behaviour, from its appearance.
local function squad_member(g: Target, cover: number)
    g:give_weapon(AK47, { model = AK47_MODEL })
    g:give_hat(HAT_MODEL)
    local function hunt()
        g:set_health(8)
        guard_ai.start(g, "chase")   -- run to the player, then the standard guard
    end
    if pad(0x7B):distance() > 2000 then
        hunt()
        return
    end
    local misses = g:near_misses()
    local function fight_trigger()
        local m = g:near_misses()
        if m > misses or Sig.take_miss(g) or g:distance() > 4000 then
            st.fight = true
        end
        misses = m
    end
    g:run_to(cover, "sprint")
    wait(STEP)
    while not g:is_dead() and g:is_busy() do
        fight_trigger()
        if g:distance() < 500 then
            hunt()   -- the player came within 500 units on its way
            return
        end
        wait(STEP)
    end
    local last_aim = -math.huge
    while not g:is_dead() do
        fight_trigger()
        if st.fight or st.gas_done then
            if g:distance() > 2000 then
                hunt()
                return
            end
            if not g:is_busy() then
                g:fire_at(nil, { stance = stance() })   -- re-aims standing or kneeling between bursts
            end
        elseif not g:is_busy() and clock() - last_aim > 2 then
            g:fire_at(nil, { stance = stance(), aim_only = true })   -- holds the player at gunpoint
            last_aim = clock()
        end
        wait(STEP)
    end
end

-- rule 33: a squad slot: (re)creates its member while the ambush lasts and the gas has not leaked 5 s.
function B.squad_slot(n: number, at: number, cover: number)
    while not st.gas_done do
        if not st.exiting and not chr(n):exists() then
            local g = spawn_guard({ chr = n, pad = at, body = BODY_SOLDIER })
            if g then
                if g:on_screen() then
                    g:remove()   -- a player sees it as it appears
                else
                    squad_member(g, cover)
                end
            end
        end
        wait(STEP)
    end
end

-- rule 35: the gas leak; 5 s later the respawns stop, the fight starts and Ourumov flees.
local function gas_watch()
    -- TODO(api): no "gas_leak" event (the engine's gas-release objects); the first destroyed bottling-room tank
    -- (tags 0x01..0x0A, 0x17..0x1F) stands for the start of the leak ("destroyed" events, not a poll).
    local subs: { Connection } = {}
    for _, t in GAS_TAGS do
        table.insert(subs, on("destroyed", function()
            if st.gas_at == nil then
                st.gas_at = clock()
            end
        end, { tag = t }))
    end
    while st.gas_at == nil do
        wait(STEP)
    end
    for _, c in subs do
        c:off()
    end
    wait(5)
    st.gas_done = true
    st.fight = true
end

function B.start()
    on("guard_damaged", function()
        if not st.ambush and not trev:is_dead() then
            hurt = true   -- rule 27: a player hurt Trevelyan during rules 23-24
        end
    end, { chr = TREV })
    spawn(trevelyan)
    spawn(gas_watch)
end

return B
