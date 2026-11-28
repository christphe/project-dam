-- jungle_natalya: Natalya (chr 0x01), the companion of rules 8-16: she follows the nearest living player, wanders
-- near him, looks for guards and engages them at range or up close, and chatters. Rule 17 (her death) is in the
-- main script. A require()d module: no wait() at its top level; run() is called from a task of the main script.
local lib = require("campaign_lib")
local guards = require("jungle_guards")
local guard_ai = require("guard_ai")

local N = {}

local STEP: number = lib.STEP
local NATALYA = 0x01
local START_PAD = 514          -- rule 16: the fallback spot she looks at
local QUIP_FIRST = 0x4808      -- rule 10: 19 quips, 4808..481A
local QUIP_COUNT = 19
-- rule 13: the body part the close-range finisher hits.
-- TODO(spec): the spec gives no body part for the sure hit; 8 is what the other levels' scripted hits use.
local FINISHER_BODY_PART = 8

local warned = false           -- rule 10: the low-health warning, once per level

function N.run()
    local nat: Target = chr(NATALYA)
    if not nat:exists() then
        return
    end
    guard_ai.stop(nat)              -- rule 14: never the generic guard behaviour
    nat:set_stats({ health = 8, max_health = 8, armour = 8, speed = 10 })
    nat:set_flag("no_autoaim", true)
    nat:set_flag("can_shoot_guards", true)

    local target: Target? = nil

    local function mark(t: Target, on: boolean)
        if t.chr ~= nil then
            guards.marked[t.chr] = if on then true else nil
        end
    end

    -- rule 11: the first guard (setup or spawned) within radius, never Natalya, never a player (rule 14)
    local function first_within(radius: number): Target?
        local function ok(g: Target): boolean
            return g.chr ~= NATALYA and not lib.gone(g) and nat:distance_to(g) <= radius
        end
        for _, g in all_guards() do
            if ok(g) then
                return g
            end
        end
        for _, g in guards.spawned do
            if ok(g) then
                return g
            end
        end
        return nil
    end

    local function chatter(): string
        -- rule 10 (rule 15: top-of-screen lines, not voiced); co-op: any player below 20%, once
        local low = false
        for _, n in lib.living() do
            if lib.health(n) < 0.2 then
                low = true
            end
        end
        if low and not warned then
            warned = true
            lib.top("481D")
        elseif lib.chance(24) and lib.chance(75) then
            lib.top(QUIP_FIRST + math.random(0, QUIP_COUNT - 1))
        end
        return "follow"
    end

    local function look_for(nothing: string): string
        -- rule 11
        if not lib.any_alive() then
            -- Bond dead: no new pick; rule 13 against the target she picked last. Already dead: only chatter
            -- (rule 10); never picked one: nothing is engaged, she follows again.
            if target == nil then
                return "follow"
            elseif lib.gone(target :: Target) then
                return "chatter"
            end
            return "close"
        end
        target = first_within(1000)
        if target ~= nil then
            return "close"
        end
        target = first_within(2000) or first_within(3000)
        if target ~= nil then
            return "ranged"
        end
        return nothing
    end

    local function follow(): string
        -- rule 9 (co-op: the nearest living player)
        nat:set_stats({ speed = 10 })
        while nat:exists() do
            local p = lib.nearest_player(nat)
            if p == nil then
                return "look_for"
            end
            if nat:distance() > 1500 then
                -- runs to his position, re-checking as she goes, until she arrives or is within 750
                nat:run_to_player("run", p)
                local t0 = clock()
                while nat:exists() and nat:is_moving() and nat:distance() > 750 do
                    if clock() - t0 > 1 then
                        local q = lib.nearest_player(nat)
                        if q ~= nil then
                            nat:run_to_player("run", q)
                        end
                        t0 = clock()
                    end
                    wait(STEP)
                end
                nat:halt()
                wait(STEP)
            elseif lib.chance(50) then
                return "check"          -- rule 11, then rule 10 when it finds nothing
            else
                -- wander to a spot near him (in his direction or to a side)
                local spot = nat:spot_near_player(p)
                if spot == nil or not nat:run_to(spot, "run") then
                    return "look_around"
                end
                local far = false
                while nat:exists() and nat:is_moving() do
                    if nat:distance() > 2500 then
                        far = true
                        break
                    end
                    wait(STEP)
                end
                if not far then
                    return "wander_check"   -- rule 11 on arrival (nothing found: rule 16)
                end
                nat:halt()
            end
        end
        return "dead"
    end

    local function ranged(): string
        -- rule 12
        local t = target
        if t == nil or lib.gone(t) then
            return "follow"
        end
        if not guards.same_room(nat, t) then
            return "follow"
        end
        local stance = if lib.chance(50) then "kneel" else "stand"
        if nat:fire_at(t, { stance = stance, aim_only = true }) then
            mark(t, true)                   -- the guard now knows she is after it (rule 20)
            wait(1)
            if lib.gone(t) then
                mark(t, false)
                return "chatter"
            end
            nat:fire_at(t, { stance = stance })
            lib.wait_idle(nat, 8)
            if lib.gone(t) then
                mark(t, false)
                return "chatter"
            end
            if lib.chance(50) then
                return "look_for"
            end
            mark(t, false)
            return "follow"
        end
        -- cannot aim: faces it for 1 s, then retries while it is in her room
        nat:face(t)
        wait(1)
        if not lib.gone(t) and guards.same_room(nat, t) then
            return "ranged"
        end
        return "follow"
    end

    local function close(): string
        -- rule 13
        local t = target
        if t == nil or lib.gone(t) then
            return "follow"
        end
        nat:set_stats({ speed = 20 })
        mark(t, true)
        if lib.chance(50) then
            nat:anim(0x0B, { from = 20, to = 60 })
            lib.wait_anim(nat, 3)
        end
        nat:fire_at(t, { stance = "kneel", aim_only = true })
        wait(1)
        if lib.gone(t) then
            mark(t, false)
            return "chatter"
        end
        if nat:distance_to(t) <= 250 then
            -- waits for her aim to settle and fires (fire_at fires as soon as the aim is set)
            if nat:fire_at(t, { stance = "kneel" }) then
                -- the shot went off: the target also takes a sure hit from her weapon (the finisher)
                local item = nat:held_weapon()
                if item ~= nil and not lib.gone(t) then
                    t:hit(FINISHER_BODY_PART, item)
                end
            end
            lib.wait_idle(nat, 8)
            if lib.gone(t) then
                mark(t, false)
            end
            return "follow"
        end
        nat:fire_at(t, { stance = "kneel" })
        lib.wait_idle(nat, 8)
        if lib.gone(t) then
            mark(t, false)
            return "chatter"
        end
        if lib.chance(50) then
            return "close"
        end
        mark(t, false)
        return "follow"
    end

    local LOOKS = { { 0x96, 86 }, { 0x97, 86 }, { 0x53, 34 }, { 0x54, 36 }, { 0x63, 149 } }
    local LOOK_PCT = { 24, 24, 5, 5, 5, 8, 8 }   -- the five animations, aim kneeling, aim standing; rest: face

    local function look_around(): string
        -- rule 16: she stands still (the spec's "speed 0": halted) and strikes a pose
        nat:halt()
        local i = lib.roll_index(LOOK_PCT)
        if i <= #LOOKS then
            nat:anim(LOOKS[i][1], { from = 0, to = LOOKS[i][2] })
            lib.wait_anim(nat, 6)
        else
            -- a nearby spot on the way to the nearest player (fallback pad 514)
            local spot: Pad = nat:spot_near_player(lib.nearest_player(nat)) or START_PAD
            if i == #LOOKS + 1 then
                nat:fire_at(pad(spot), { stance = "kneel", aim_only = true })
            elseif i == #LOOKS + 2 then
                nat:fire_at(pad(spot), { stance = "stand", aim_only = true })
            else
                nat:face_pad(spot)
            end
            wait(2)
        end
        return "follow"
    end

    local states: { [string]: () -> string } = {
        follow = follow,
        check = function()
            return look_for("chatter")
        end,
        wander_check = function()
            return look_for("look_around")
        end,
        look_for = function()
            return look_for("look_around")
        end,
        ranged = ranged,
        close = close,
        chatter = chatter,
        look_around = look_around,
    }
    local state = "follow"
    while nat:exists() and state ~= "dead" do
        local f = states[state]
        if f == nil then
            break
        end
        state = f()
        wait(0.05)
    end
end

return N
