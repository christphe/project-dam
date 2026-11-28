-- statue: Statue Park (specs/statue.md). Characters table, objectives a-e, the level states, Valentin (rules 1-5),
-- the reinforcements (rule 21), the exit (rule 27) and the outro. The other rules live in:
--   statue_janus.luau   rules 6-8, 11-15 (Janus at the statue)
--   statue_guards.luau  rules 9, 10, 22 (escorts, park guards, the hunting guard)
--   statue_heli.luau    rules 16-20, 23 (the helicopter, Natalya, the flight recorder)
--   statue_mishkin.luau rules 24-26 (Mishkin at the gates)
-- The intro is staged here (mission.json "level_intro": false; the start pad, items, outfit and watch stay setup data):
-- the three intro cameras, then the swirl into Bond's view with his intro animation (Cinematics, Intro).

local guard_ai = require("guard_ai")
guard_ai.boot()

local lib = require("campaign_lib")
local guards = require("statue_guards")
local janus = require("statue_janus")
local heli = require("statue_heli")
local mishkin = require("statue_mishkin")

local show = lib.bottom
local STEP = 0.25

-- ===================================================================== level states
local S = {
    meeting = "before",       -- Valentin's meeting: "before" | "on" | "over"
    failed = false,           -- the mission-failure state (rules 4, 5)
    hostile = false,          -- Janus' side opens fire (rules 6, 8, 10-15)
    janus_spawned = false,
    janus = nil :: Target?,
    janus_gone = false,       -- set when Janus starts to leave (rule 15)
    janus_gone_at = 0,
    natalya = nil :: Target?,
    natalya_reached = false,  -- rule 17
    natalya_at_gates = false, -- rule 19
    blown = false,            -- the helicopter exploded (rule 23)
    mishkin = nil :: Target?,
    mishkin_arrived = false,  -- rule 24
    mishkin_hostile = false,  -- rule 25
    exiting = false,          -- rule 27: the exit sequence has begun
    intro = true,             -- the intro (Cinematics) is playing
    set_hostile = function() end,
}
function S.set_hostile()
    S.hostile = true
end

-- The mission-failure state: every objective fails.
local function fail_state()
    if not S.failed then
        S.failed = true
        objectives.fail_all()
    end
end

-- ===================================================================== objectives
objectives.add("a", text("8C39") or "", 0)   -- contact Valentin
objectives.add("b", text("8C3A") or "", 0)   -- confront and unmask Janus
objectives.add("c", text("8C3B") or "", 0)   -- locate the helicopter
objectives.add("d", text("8C3C") or "", 0)   -- rescue Natalya
objectives.add("e", text("8C3D") or "", 0)   -- find the flight recorder

-- ===================================================================== objects and world
local heli_obj = tag(0)
-- TODO(spec): set_rotor takes the setup's own rotor units; the spec gives "62 degrees per tick" (full speed at once),
-- read here as that setup value.
heli_obj:set_rotor(62, 0)
heli_obj:anim(2, { from = 2, to = 2, hold = true })   -- the helicopter's model holds its animation 2 at frame 2
heli_obj:move_to(22)                       -- parked out of play until rule 16
tag(1):set_collectable(false)              -- the flight recorder: not collectable until rule 23
for t = 11, 16 do
    tag(t):set_invincible(true)            -- the charges: indestructible until rule 23
end

-- ===================================================================== characters
-- Park guards chr 0x06-0x09 (their KF7 and hat are setup items): hunting guards from the start (rule 22).
for n = 0x06, 0x09 do
    local g: Target = chr(n)
    if g:exists() then
        guard_ai.stop(g)
        spawn(function() guards.hunter(S, g, "park") end)
    end
end

-- ===================================================================== Valentin (rules 1-5)
spawn(function()
    local v: Target = chr(0x01)
    if not v:exists() then
        return
    end
    guard_ai.stop(v)
    v:set_flag("no_autoaim", true)

    -- rule 5: Valentin dies before or during the meeting (after rule 3 it has no effect)
    local died: Connection? = nil
    died = on("guard_died", function(_ev: MissionEvent)
        if died then
            died:off()
            died = nil
        end
        if S.meeting ~= "over" then
            show("8C30")
            S.meeting = "over"
            fail_state()
        end
    end, { chr = 0x01 })

    -- rule 1: a player comes into his line of sight (after the intro): he faces Bond and the meeting starts
    lib.wait_until(function() return S.meeting == "over" or (not S.intro and v:sees_player()) end)
    if S.meeting == "over" then
        return
    end
    v:face_player()
    S.meeting = "on"

    -- rule 2: the talk, on a clock that runs only while the nearest player is within 8 m
    local LINES: { { at: number, id: string } } = { { at = 5, id = "8C09" }, { at = 10, id = "8C0A" },
        { at = 15, id = "8C0B" }, { at = 20, id = "8C0C" }, { at = 25, id = "8C0D" } }
    local t = 0
    local i = 1
    local away = false
    local last = clock()
    while S.meeting == "on" do
        local now = clock()
        local d = v:distance()   -- the nearest living player (co-op note)
        if d > 2000 then
            -- rule 4: more than 20 m away during the meeting: contact failed
            show("8C0F")
            S.meeting = "over"
            fail_state()
            v:fade_out()
            return
        elseif d <= 800 then
            away = false
            t += now - last
            if i <= #LINES and t >= LINES[i].at then
                show(LINES[i].id)
                i += 1
            end
            if t >= 30 then
                break
            end
        elseif not away then
            away = true
            show("8C0E")   -- once per step away
        end
        last = now
        wait(STEP)
    end
    if S.meeting ~= "on" then
        return
    end

    -- rule 3: the talk is over: objective a; he walks to pad 22 and fades out once the player is beyond 30 m
    objectives.complete("a")
    S.meeting = "over"
    v:run_to(22, "walk")
    while v:exists() do
        if v:distance() > 3000 then
            v:fade_out()
            return
        end
        wait(STEP)
    end
end)

-- ===================================================================== intro (Cinematics, Intro)
-- Played by the shared intro module from statue_intro_data (one of the three fixed cameras with 8C3E, the swirl,
-- the body's animation 97 to frame 175), alongside the rest of the level as before; S.intro ends at the hand-over.
spawn(function()
    require("intro").play(require("statue_intro_data"))
    S.intro = false
end)

-- ===================================================================== the other beats
janus.start(S)     -- rules 6-8, 11-15
heli.start(S)      -- rules 16-20, 23
mishkin.start(S)   -- rules 24-26

-- ===================================================================== reinforcements (rule 21)
local SPAWN_PADS = {
    { 196, 129, 36, 0 },
    { 163, 89, 58, 52 },
    { 162, 203, 54, 192 },
    { 214, 152, 107, 42 },
}
local PAD_WEIGHTS = { 24, 23, 23, 30 }

-- One random roll picks the first pad; a failed spawn falls through to the next one.
local function spawn_at(n: number, pads: { number }, body: number): Target?
    for k = lib.roll(PAD_WEIGHTS), #pads do
        local g = spawn_guard({ chr = n, pad = pads[k], body = body })
        if g then
            return g
        end
    end
    return nil
end

spawn(function()
    while true do
        if not S.exiting then
            local park = S.meeting == "before" or (S.meeting == "over" and not S.janus_spawned)
            local escorts = (S.hostile or S.janus_gone) and not objectives.is_complete("e")
            for k = 1, 4 do
                if park then
                    local n = 0x05 + k   -- chr 0x06-0x09, park guards (body 3)
                    if not lib.present(chr(n)) then
                        local g = spawn_at(n, SPAWN_PADS[k], 3)
                        if g then
                            spawn(function() guards.park(S, g) end)   -- loadout, then rule 22
                        end
                    end
                elseif escorts then
                    local n = 0x01 + k   -- chr 0x02-0x05, escorts (body 4)
                    if not lib.present(chr(n)) then
                        local g = spawn_at(n, SPAWN_PADS[k], 4)
                        if g then
                            spawn(function() guards.escort(S, g) end)   -- rule 9 loadout, hence rule 22
                        end
                    end
                end
            end
        end
        wait(2)
    end
end)

-- ===================================================================== exit (rule 27) and outro
local function outro(p: number)
    -- 1. every character hidden; any button press now fades out and ends the level; Bond's weapon hidden
    cinema.hide_all_chrs(true)
    local ending = false
    local function finish_win()
        if ending then
            return
        end
        ending = true
        lib.fade_out(nil, { settle = lib.STEP })
        mission.win()
    end
    spawn(function()
        while not ending do
            if cinema.skipped() then
                finish_win()
            end
            wait(STEP)
        end
    end)
    for _, n in lib.living() do
        player(n):hide_weapon(true)
    end
    -- 2. cutscene camera tag 2, turning to keep Bond in view; fade in; the secondary track; the gates snap open
    local body = bond_body(p)
    body:show(true)
    camera.fixed(tag(2), { look_at = body })
    screen.fade_in()
    music.play_extra(0, { total = 255, stop_after = 0 })   -- 255 s, no linger
    for _, t in { 3, 4 } do
        tag(t):unlock(true)
        tag(t):open_now()   -- without sound
    end
    -- 3. Bond at pad 16 (0x6B); Mishkin at pad 17 (0x52); chr 0x0B at pad 20 (0x28); chr 0x0C at pad 19 kneeling,
    --    aiming at Bond, standing still 4.5 s later
    local t3 = clock()
    body:teleport(16)
    body:anim(0x6B)
    local m = S.mishkin
    if m ~= nil and m:exists() then
        m:set_hidden(false)
        m:teleport(17)
        m:anim(0x52)
    end
    local s1: Target = chr(0x0B)
    if s1:exists() then
        s1:set_hidden(false)
        s1:teleport(20)
        s1:anim(0x28)
    end
    local s2: Target = chr(0x0C)
    if s2:exists() then
        s2:set_hidden(false)
        s2:teleport(19)
        s2:fire_at(player(p), { stance = "kneel", aim_only = true })
        spawn(function()
            wait(4.5)
            if s2:exists() then
                s2:halt()
            end
        end)
    end
    -- 4. 5 s after step 3: fade to black, then the level ends (a win)
    wait(math.max(0, 5 - (clock() - t3)))
    finish_win()
end

spawn(function()
    -- rule 27: the first living player in the room of pad 22 triggers the exit for everyone
    local p: number? = nil
    while p == nil do
        p = lib.player_in_room(22)
        if p == nil then
            wait(STEP)
        end
    end
    S.exiting = true
    -- every player invulnerable, no pickups, HUD hidden, controls locked
    -- TODO(api): no verb stops the mission clock; cinema.begin() is taken to cover it.
    cinema.begin()
    lib.fade_out(nil, { settle = lib.STEP })
    local m = S.mishkin
    if m ~= nil and lib.present(m) and not S.mishkin_hostile and objectives.all_complete() then
        outro(p :: number)
    else
        mission.finish()   -- the usual end-of-mission objective check decides
    end
end)
