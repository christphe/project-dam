-- control: the Control level (specs/control.md). Natalya waits in the lift, hacks the security console (blast
-- door), then the satellite guidance console while reinforcements pour in; Boris surrenders and flees,
-- Trevelyan taunts and shuts a door; the exit room plays the outro. Rules 1-14 (Natalya), 15-17 (Boris),
-- 18 (Trevelyan), 19 (exit + outro), 20-24 (reinforcements, control_reinforce.luau), 25 (ambushers),
-- 26 (typing sound), 27 (doors), 28 (music).

local guard_ai = require("guard_ai")
guard_ai.boot()

local U = require("campaign_lib")
local R = require("control_reinforce")
local STEP: number = U.STEP
local show: (id: string) -> () = U.top

-- ---------------------------------------------------------------- objectives
objectives.add("a", text("2029") :: string, 0)   -- protect Natalya
objectives.add("b", text("202A") :: string, 0)   -- disable the satellite
objectives.add("c", text("202B") :: string, 0)   -- destroy the armoured mainframes

-- ---------------------------------------------------------------- characters
-- Start (pad 223, PP7, ammo, outfit, watch) is the level's own start data: nothing to script.
local nat: Target = chr(0x00)
local trev: Target = chr(0x01)
local boris: Target = chr(0x02)

local function take_over(g: Target)
    guard_ai.stop(g)
    g:halt()
end

take_over(nat)
nat:set_stats({ armour = 100, health = 100 })
nat:set_flag("no_autoaim", true)
take_over(trev)
trev:set_stats({ armour = 5000, speed = 100, accuracy = 100 })
trev:set_flag("no_autoaim", true)
take_over(boris)
boris:set_flag("no_autoaim", true)

-- Standard generic guards: 0x03, 0x30, 0x04-0x16, 0x19, 0x1C-0x21, 0x26-0x2C (boot() already started the ones
-- whose setup list is generic; the others are started here), with the Characters table's hearing / vision.
local function standard(n: number, stats: GuardStats)
    local g: Target = chr(n)
    if g:exists() then
        g:set_stats(stats)
        if guard_ai.mode(g) == nil then
            guard_ai.start(g, "standard")
        end
    end
end
standard(0x03, { hearing = 0.8, vision = 25 })
standard(0x30, { hearing = 0.8, vision = 20 })
for n = 0x04, 0x16 do standard(n, { hearing = 0.01 }) end
standard(0x19, { hearing = 0.01 })
for n = 0x1C, 0x21 do standard(n, { hearing = 0.01 }) end
for n = 0x26, 0x2C do standard(n, { hearing = 0 }) end   -- deaf

-- ---------------------------------------------------------------- doors (rule 27)
local BLAST = 0x11
local GUIDANCE_DOOR = 0x12
local SIDE_DOORS = { 0x0A, 0x0D, 0x08, 0x09 }
for _, t in { 0x11, 0x12, 0x0D, 0x08, 0x09, 0x0A } do
    tag(t):set_passable(false)    -- rule 27: shut against everyone at the start
end

local function open_door(t: number)
    local d = tag(t)
    d:set_passable(true)
    d:unlock()
    d:open()
end

-- rule 27: once the blast door is open, it closes and locks behind the player in the room of pad 210
local function blast_door_opened()
    open_door(BLAST)
    spawn(function()
        U.wait_until(function(): boolean
            return U.player_in_rooms({ 210 }) ~= nil
        end)
        tag(BLAST):close()
        tag(BLAST):lock()
    end)
end

-- ---------------------------------------------------------------- music (rule 28)
-- Started by the intro (below, its swirl hook) when its swirl begins.
local function music_loop()
    local playing = false
    while true do
        local inside = U.player_in_rooms({ 223 }) ~= nil   -- rule 28: any player
        if inside and not playing then
            music.play_extra(1)
            playing = true
        elseif not inside and playing then
            music.stop_extra(1)
            playing = false
        end
        wait(STEP)
    end
end

-- ---------------------------------------------------------------- Natalya
-- state: "lift" | "to_security" | "security" | "to_guidance" | "guidance" | "giving_up" | "leaving" | "gone"
local state = "lift"
local boris_escaped = false
local hack_done = false          -- rule 10: the clock reached 135 s (objective b complete)
local nat_task: Task? = nil
local typing_task: Task? = nil

-- Starts fn as Natalya's behaviour, cancelling the one she runs.
local function natalya_do(fn: () -> ())
    local old, old_typing = nat_task, typing_task
    typing_task = nil
    nat_task = spawn(fn)
    if old_typing then old_typing:cancel() end
    if old then old:cancel() end
end

-- rule 26: the typing sound, 1 s later, 78% of the time, from her console
local function typing_sound(console: number)
    spawn(function()
        wait(1)
        if math.random() < 0.78 then
            sound.play(184, { from = tag(console) })
        end
    end)
end

-- rule 4 / rule 8: one bout of typing (one animation at random)
local BOUTS = {
    { 20, { 163, 0, 140 } },
    { 20, { 172, 0, 69 } },
    { 20, { 173, 0, 74 } },
    { 20, { 174, 0, 79 } },
    { 21, { 175, 0, 89 } },
}
local function bout(console: number)
    local a = U.pick(BOUTS)
    nat:anim(a[1], { from = a[2], to = a[3] })
    if a[1] ~= 163 then
        typing_sound(console)    -- rule 4: every bout that is not 163 asks for the sound (rule 26)
    end
    U.wait_until(function(): boolean
        return nat:anim_done() or nat:is_dying() or not nat:exists()
    end)
end

-- rules 11 / 6 / 16: she gives up; 6 s later the lift line, objective b fails, she runs to the lift; the level ends
-- when a player joins her there.
local function give_up()
    state = "giving_up"
    R.natalya_left()                      -- rule 11: the shooters stop targeting her (rule 22)
    wait(6)
    show("201F")                          -- rule 11: going to the lift
    objectives.fail("b")
    nat:run_to(224, "run")
    U.wait_until(function(): boolean
        return nat:distance_to(pad(224)) <= 250 or not nat:is_moving()
    end)
    -- rule 11: she has reached the lift; a player entering its room ends the level (any player, co-op notes)
    U.wait_until(function(): boolean
        return U.player_in_rooms({ 224 }) ~= nil
    end)
    cinema.begin()                        -- HUD hidden, players frozen
    screen.fade_out()
    U.wait_fade({ settle = 0 })
    mission.finish()                      -- rule 11: the objectives decide the outcome
end

-- rule 12: the hack is done; she leaves for the lift and fades out there
local function leave_success()
    state = "leaving"
    show("2013")
    R.natalya_left()                      -- rule 12: shooters stop targeting her (rule 22)
    R.slow_down()                         -- rule 12: the reinforcement pace slows (rule 24)
    U.run_near(nat, 224, 250, "run")
    objectives.complete("a")              -- rule 12
    nat:fade_out()
    state = "gone"
end

-- rules 8-10, 12: the guidance console
local function guidance()
    state = "to_guidance"
    show("2010")                          -- rule 8
    tag(GUIDANCE_DOOR):set_passable(true) -- rule 8 / 27
    -- TODO(api): set_passable cannot single out one character; the door stops being shut against everyone.
    U.run_near(nat, 112, 250, "run")
    U.walk_to(nat, 227)
    nat:face_pad(228)
    state = "guidance"
    local arrived = clock()
    wait(1)
    U.anim(nat, 163, 0, 140)
    show("200E")                          -- rule 8: the weapon target is revealed

    -- rule 8: typing bouts, put back on pad 227 between bouts while out of view
    typing_task = spawn(function()
        while true do
            bout(0x26)
            if not nat:on_screen() then
                nat:teleport(227)
            end
        end
    end)

    -- rule 9: 15 s at this console starts the override
    spawn(function()
        wait(math.max(0, 15 - (clock() - arrived)))
        if state ~= "guidance" then return end
        show("200F")
        alarm.trigger()
        for _, t in SIDE_DOORS do
            local d = tag(t)
            d:set_passable(true)          -- rule 27: the side doors unlock
            d:unlock()
        end
        R.start()                         -- rules 20-24
    end)

    -- rule 10: the hack clock (counts from her arrival at pad 227)
    local LINES: { { at: number, id: string } } = {
        { at = 40, id = "2015" }, { at = 60, id = "2017" }, { at = 80, id = "2014" },
        { at = 135, id = "2011" }, { at = 140, id = "2012" },
    }
    local next_line = 1
    local hack = 0
    local strayed = false
    while hack < 145 do
        wait(STEP)
        local near = nat:distance() <= 1500                 -- the nearest player (co-op notes)
        if hack_done or near then
            hack += STEP
            if near then strayed = false end
        elseif not strayed then
            strayed = true
            show("2016")                                     -- once per time the player strays
        end
        while next_line <= #LINES and hack >= LINES[next_line].at do
            show(LINES[next_line].id)
            if LINES[next_line].at == 135 then
                hack_done = true
                objectives.complete("b")
            end
            next_line += 1
        end
    end
    if typing_task then typing_task:cancel(); typing_task = nil end
    nat:stop_anim()
    leave_success()                       -- rule 12 (clock 145 s)
end

-- rule 16: Boris was killed; she will not do the guidance hack
local function boris_killed()
    state = "giving_up"
    open_door(GUIDANCE_DOOR)
    U.wait_until(function(): boolean return nat:sees_player() end)
    show("2020")
    nat:face_player()
    wait(5)
    show("2021")
    give_up()
end

-- rules 3-5: the security console
local function security()
    state = "to_security"
    U.run_near(nat, 220, 250, "run")
    U.walk_to(nat, 225)
    nat:face_pad(226)
    state = "security"
    wait(1)
    local started = clock()
    U.anim(nat, 163, 0, 140)
    show("2009")                          -- rule 3: blast doors locked
    local opened = false
    while true do
        bout(0x24)                        -- rule 4
        if nat:on_screen() then
            -- rule 5: checked between bouts while she is in view; once only
            if not opened and clock() - started >= 15 then
                opened = true
                show("200A")
                blast_door_opened()       -- rule 27
            end
        else
            nat:teleport(225)             -- rule 4: back on pad 225 while out of view
        end
    end
end

-- rules 1-2: the lift
-- rule 2: the player she spotted last (the "guard_spotted" edge), for a player she already sees
local nat_spotted: number? = nil
on("guard_spotted", function(ev: MissionEvent)
    nat_spotted = ev.player
end, { chr = 0x00 })

local function lift()
    show("2008")                          -- rule 1 (called once the intro is over)
    U.wait_until(function(): boolean
        for _, t in { 0x02, 0x03, 0x04 } do
            if not tag(t):is_destroyed() then return false end
        end
        for _, n in { 0x1C, 0x1D, 0x1E, 0x1F, 0x26, 0x27 } do
            if chr(n):exists() then return false end
        end
        return true
    end)
    -- rule 2: when she sees a player (the first she spots, co-op notes): a comment on his health
    local seen: number? = if nat:sees_player() then nat_spotted else nil
    if seen == nil then
        seen = wait(event("guard_spotted", { chr = 0x00 })).player
    end
    local h = U.health(seen or 0)
    if h < 0.3 then
        show("200D")
    elseif h < 0.7 then
        show("200C")
    else
        show("200B")
    end
    security()                            -- rule 3
end

R.set_natalya(nat)

-- rule 6 / rule 11: a console she needs is destroyed
on("destroyed", function(ev: MissionEvent)
    local t = (ev :: any).tag
    if (t == 0x23 or t == 0x24) and (state == "to_security" or state == "security") then
        show("201D")                      -- rule 6
        natalya_do(give_up)
    elseif (t == 0x25 or t == 0x26) and (state == "to_guidance" or state == "guidance") and not hack_done then
        show("201E")                      -- rule 11
        natalya_do(give_up)
    end
end)

-- rule 7: a player enters the control room (pad 214) while she is at the security console
spawn(function()
    U.wait_until(function(): boolean
        return state == "security" and U.player_in_rooms({ 214 }) ~= nil
    end)
    if not boris_escaped and not boris:exists() then
        natalya_do(boris_killed)          -- rule 16
    else
        natalya_do(guidance)              -- rule 8
    end
end)

-- rule 13: she dies during the guidance hack
on("guard_died", function()
    if state == "to_guidance" or state == "guidance" then
        show("2018")
    end
end, { chr = 0x00 })

-- rule 14: Natalya no longer exists and objective a is not complete
spawn(function()
    U.wait_until(function(): boolean return not nat:exists() end)
    if typing_task then typing_task:cancel() end
    if nat_task then nat_task:cancel() end
    R.natalya_left()
    if not objectives.is_complete("a") then
        show("2023")
        objectives.fail("a")
    end
end)

-- objective c: the six mainframes
spawn(function()
    U.wait_until(function(): boolean
        for t = 0x15, 0x1A do
            if not tag(t):is_destroyed() then return false end
        end
        return true
    end)
    objectives.complete("c")
end)


-- ---------------------------------------------------------------- Boris (rules 15, 17)
-- rule 17: he flees to pad 229 and fades out there (escaped)
local function boris_flee()
    if boris:is_dying() or not boris:exists() then return end
    show("201A")
    boris:run_to(229, "sprint")
    U.wait_until(function(): boolean
        if not boris:exists() or boris:is_dying() then return true end
        if not boris:is_moving() then boris:run_to(229, "sprint") end
        return boris:distance_to(pad(229)) <= 300
    end)
    if boris:exists() and not boris:is_dying() then
        boris_escaped = true
        boris:fade_out()
    end
end

spawn(function()
    -- rule 15
    U.wait_until(function(): boolean return boris:sees_player() or not boris:exists() end)
    if not boris:exists() then return end
    boris:face_player()
    U.wait_until(function(): boolean return boris:distance() <= 500 or not boris:exists() end)   -- nearest player
    if not boris:exists() then return end
    show("2022")
    wait(4)
    if boris:exists() and not boris:is_dying() and boris:on_screen() then
        U.anim(boris, 66, 0, 20, true)
        boris:give_weapon(4, { model = 191 })
        U.anim(boris, 65, 21, 40, true)
        boris:drop_items()
        show("201C")
        wait(0.5)
        show("2019")
        boris:surrender()
        -- TODO(api): no "surrender animation ended" query; is_busy() going false stands for it
        wait(STEP)
        U.wait_until(function(): boolean
            return not boris:exists() or not boris:is_busy() or not boris:on_screen()
        end)
    end
    boris_flee()                          -- rule 17
end)

-- ---------------------------------------------------------------- Trevelyan (rule 18)
spawn(function()
    trev:teleport(38)
    local missed = false
    local miss_conn = on("guard_near_miss", function() missed = true end, { chr = 0x01 })
    local firing = false
    while trev:exists() do
        if missed or trev:distance() < (if firing then 1000 else 500) then
            break
        end
        if trev:sees_player() and not trev:is_busy() then
            trev:fire_at(nil)             -- bursts at the player he sees
            firing = true
        elseif firing and not trev:is_busy() and not trev:sees_player() then
            firing = false
        end
        wait(STEP)
    end
    miss_conn:off()
    tag(0x1E):close()
    tag(0x1E):lock()
    tag(0x1F):lock()
    show("201B")
    trev:halt()                           -- he does nothing more
end)

-- ---------------------------------------------------------------- ambushers (rule 25)
local AMBUSH_KNEEL_FIRE = 1 - 0.98 ^ (60 * STEP)   -- 2% per frame, over one poll step
local function ambusher(n: number)
    local g: Target = chr(n)
    if not g:exists() then return end
    take_over(g)
    g:set_stats({ armour = 20, speed = 80, accuracy = 80 })
    -- a shot just missing it, or a guard it saw shot or killed: latched from the guard events
    local disturbed = false
    local function disturb() disturbed = true end
    on("guard_near_miss", disturb, { chr = n })
    on("guard_witnessed", disturb, { chr = n })
    spawn(function()
        while g:exists() and not g:is_dying() do
            g:fire_at(nil, { stance = "stand", aim_only = true })   -- aims at the player, never leaves its spot
            U.wait_until(function(): boolean
                if not g:exists() or g:is_dying() then return true end
                if disturbed then
                    disturbed = false
                    return true
                end
                return g:sees_player()
            end)
            if not g:exists() or g:is_dying() then break end
            local r = math.random() * 99
            if r < 21 then
                g:throw_grenade()
                U.wait_until(function(): boolean return not g:is_busy() end, 3)
                g:kneel()
            elseif r < 60 then
                g:kneel()
            else
                g:fire_at(nil, { stance = "stand" })
                U.wait_until(function(): boolean return not g:is_busy() end, 5)
            end
            if r < 60 then
                -- kneeling 3 s, 2% chance per frame to fire a burst
                local t0 = clock()
                while clock() - t0 < 3 and g:exists() do
                    if math.random() < AMBUSH_KNEEL_FIRE and not g:is_busy() then
                        g:fire_at(nil, { stance = "kneel" })
                    end
                    wait(STEP)
                end
            end
        end
    end)
end
for _, n in { 0x17, 0x18, 0x1A, 0x1B } do
    ambusher(n)
end

-- ---------------------------------------------------------------- exit and outro (rule 19)
local function outro()
    -- 1. hide every character; any button from now on fades out and ends the level
    cinema.hide_all_chrs(true)
    spawn(function()
        U.wait_until(function(): boolean return cinema.skipped() end)
        screen.fade_out()
        U.wait_fade({ settle = 0 })
        mission.win()
    end)
    -- 2. exit doors snap open; Bond's body holds no item (item 0 empties a hand)
    tag(0x1C):open_now()
    tag(0x1D):open_now()
    local bond = bond_body(0)             -- co-op notes: one Bond model, player 0
    bond:show(true)
    bond:hold_item(0, "right")
    bond:hold_item(0, "left")
    -- 3. the fixed camera-position record tag 0x20, looking at Bond
    camera.fixed(tag(0x20), { look_at = bond })
    -- 4. fade in, the extra track in slot 0
    screen.fade_in()
    music.play_extra(0)
    local t0 = clock()
    -- 5. Bond on pad 168; after 1 s animation 101 frames 8-109, held
    bond:teleport(168)
    wait(1)
    bond:anim(101, { from = 8, to = 109, hold = true })
    -- 6. at 5 s his held items are removed; door 0x1C closes
    wait(math.max(0, 5 - (clock() - t0)))
    bond:hold_item(0, "right")
    bond:hold_item(0, "left")
    tag(0x1C):close()
    -- 7. at 7 s fade to black, the level ends (won)
    wait(math.max(0, 7 - (clock() - t0)))
    screen.fade_out()
    U.wait_fade({ settle = 0 })
    mission.win()
end

spawn(function()
    -- rule 19: the first living player in the room of pad 168 (co-op notes); every player frozen
    U.wait_until(function(): boolean
        return U.player_in_rooms({ 168 }) ~= nil
    end)
    R.exiting()
    cinema.begin()                        -- invulnerable, no pickups, HUD hidden, frozen (every player)
    screen.fade_out()
    U.wait_fade({ settle = 0 })
    if not objectives.all_complete() then
        mission.fail()
        return
    end
    outro()
end)

-- ---------------------------------------------------------------- intro (Cinematics, intro)
-- mission.json "level_intro": false: one of the two fixed cameras with 202C, the swirl and the body's table pose
-- are played by the shared intro module from control_intro_data; rule 28's music starts with the swirl (its swirl
-- hook), and rule 1 follows the hand-over (lift()).
spawn(function()
    require("intro").play(require("control_intro_data"), { swirl = music_loop })   -- rule 28
    natalya_do(lift)                      -- rule 1 follows the intro
end)
