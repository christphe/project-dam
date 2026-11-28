-- statue_heli: the rigged helicopter, Natalya and the flight recorder (specs/statue.md rules 16-20 and 23):
-- the 180 s bomb countdown (objective c), reaching Natalya and taking her to the gates (objective d), the
-- explosion and the thrown flight recorder (objective e). A require()d module: start() spawns.

local lib = require("campaign_lib")

local M = {}

local STEP = 0.25
local HELI_PAD = 24
local GATES = 9

local show = lib.bottom

-- rule 23: the explosion, from the moment the countdown runs out.
local function blow(t: number)
    local o = tag(t)
    o:set_invincible(false)   -- the charges are indestructible until then
    o:explode()
end

local RECORDER_PADS = { 205, 214, 208, 200, 203, 196, 213, 194, 192 }
local RECORDER_WEIGHTS = { 12, 12, 12, 12, 12, 12, 12, 12, 6 }

local function explosion(S: any)
    S.blown = true
    blow(11)
    blow(12)
    -- the flight recorder becomes collectable and is thrown to one random pad
    local rec = tag(1)
    rec:set_collectable(true)
    rec:move_to(RECORDER_PADS[lib.roll(RECORDER_WEIGHTS)])
    spawn(function()
        wait(1)
        blow(0)
        blow(13)
        blow(14)
        wait(1)
        blow(15)
        blow(16)
    end)
    -- until objective e is complete: a player in the room of pad 179 is told the recorder is not that far
    spawn(function()
        local inside = false
        while not objectives.is_complete("e") do
            local now = lib.player_in_room(179) ~= nil
            if now and not inside then
                show("8C27")
            end
            inside = now
            wait(STEP)
        end
    end)
end

-- rule 18: Natalya follows player p (runs beyond 5 m, walks closer, stops within 2 m, waits until beyond 4 m),
-- until the helicopter blows up.
local function follow(S: any, n: Target, p: number)
    local stopped = false
    while n:exists() and not S.blown do
        local who = if lib.alive(p) then p else (lib.nearest_player(n))
        if who ~= nil then
            local d = n:distance_to(player(who))
            if stopped then
                if d > 400 then
                    stopped = false
                end
            elseif d <= 200 then
                n:halt()
                stopped = true
            else
                n:run_to_player(if d > 500 then "run" else "walk", who)
            end
        end
        wait(STEP)
    end
end

-- rule 19: Natalya at the gates.
local function to_gates(S: any, n: Target)
    wait(2)
    local function at_gates(): boolean
        return n:distance_to(pad(GATES)) <= 400
    end
    while n:exists() and not at_gates() do
        if objectives.is_complete("e") then
            n:teleport(GATES)   -- objective e complete meanwhile: straight onto pad 9
        elseif not n:is_moving() then
            n:run_to(GATES, "sprint")
        end
        wait(STEP)
    end
    if not n:exists() then
        return
    end
    S.natalya_at_gates = true
    n:face_player()
    -- when Mishkin arrives (rule 24) she raises her hands and stays so
    lib.wait_until(function() return S.mishkin_arrived or not n:exists() end)
    if n:exists() then
        n:surrender()
    end
end

-- rules 17-19: Natalya from her spawn.
local function natalya(S: any, n: Target)
    n:set_flag("no_autoaim", true)
    -- rule 17: crouches by the helicopter
    n:anim(0x1E, { from = 83, to = 84, loop = true })
    lib.wait_until(function() return S.blown or not n:exists() or n:near(300) end)
    if S.blown or not n:exists() then
        return
    end
    local p = lib.nearest_player(n) or 0
    show("8C26")
    wait(2)
    if not n:exists() then
        return
    end
    S.natalya_reached = true
    n:anim(0xA8, { from = 60, to = 120 })
    show("8C23")
    wait(STEP)
    while n:exists() and not n:anim_done() do
        wait(STEP)
    end
    show("8C24")
    follow(S, n, p)   -- rule 18
    if not n:exists() then
        return
    end
    -- rule 18: the helicopter blew up while she followed
    objectives.complete("d")
    wait(5)
    show("8C25")
    to_gates(S, n)   -- rule 19
end

function M.start(S: any)
    spawn(function()
        -- rule 16: Janus has been gone for 10 s
        lib.wait_until(function() return S.janus_gone and clock() - S.janus_gone_at >= 10 end)
        show("8C1F")
        tag(0):move_to(HELI_PAD)
        countdown.set(180)
        countdown.show(true)
        countdown.start()

        -- Natalya (chr 0x00, body 16) at pad 6
        spawn(function()
            local n = lib.spawn_retry({ chr = 0x00, pad = 6, body = 16 }, function() return S.exiting end)
            if n == nil then
                return
            end
            S.natalya = n
            -- rule 20: once she is gone (dead and removed), objective d fails
            spawn(function()
                while lib.present(n) do
                    wait(STEP)
                end
                if not objectives.is_failed("d") then
                    objectives.fail("d")
                end
            end)
            natalya(S, n)
        end)

        -- while the countdown runs: objective c within 30 m, the proximity fuse within 5 m
        local fuse = false
        while countdown.left() > 0 do
            if not objectives.is_complete("c") and pad(HELI_PAD):near(3000) then
                objectives.complete("c")
            end
            if not fuse and pad(HELI_PAD):near(500) then
                fuse = true
                show("8C32")
                countdown.set(15)
            end
            wait(STEP)
        end

        -- the countdown ran out
        explosion(S)   -- rule 23
        if not S.natalya_reached and S.natalya ~= nil and S.natalya:exists() then
            S.natalya:hit(8, 6)   -- not reached: she is shot
        end
        show("8C20")
        sound.stop(0)
        countdown.stop()
        countdown.show(false)
    end)

    -- objective e: the flight recorder (tag 1) picked up; it fails if the recorder is destroyed
    on("picked_up", function(_ev: MissionEvent)
        if not objectives.is_failed("e") then
            objectives.complete("e")
        end
    end, { tag = 1 })
    on("destroyed", function(_ev: MissionEvent)
        if not objectives.is_complete("e") then
            objectives.fail("e")
        end
    end, { tag = 1 })

    -- rule 20: Mishkin's men turn hostile: Natalya is shot and objective d fails
    spawn(function()
        lib.wait_until(function() return S.mishkin_hostile end)
        local n = S.natalya
        if n ~= nil and n:exists() then
            n:hit(8, 8)
        end
        if not objectives.is_failed("d") then
            objectives.fail("d")
        end
    end)
end

return M
