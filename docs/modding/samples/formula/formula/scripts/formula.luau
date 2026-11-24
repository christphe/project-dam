-- formula: the whole story of the "Formula" mission, one script.
-- target: the exit zone (the script reads `target` as the place the mission is won).
--
-- Yuri Kalinin, a programmer, hides in a washroom cubicle (hideDoor) on Agent: opening that door finds him. From Secret
-- Agent on he has been caught: a guard (captor) holds him at gunpoint, hands up, and shoots him if he sees a player for
-- too long or a shot misses him, as the hostage takers of Frigate do. Kill the captor first and Kalinin is free. The
-- Facility's own security terminals were removed; the mission adds monitors of its own at the same places. Those have
-- no link to the doors: a player uses a terminal to call Kalinin over, and he overrides it.
--
-- Difficulty (mission.difficulty(): 0 Agent, 1 Secret Agent, 2 00 Agent, 3 007):
--   Agent         keep Kalinin alive and leave.
--   Secret Agent+ Kalinin is a hostage (kalininHostage, captor: present on those difficulties only).
--   Secret Agent  + download the formulas at the fourth computer.
--   00 Agent, 007 + the computer refuses until a player carries today's password list (an item pickup in the laboratory,
--                   only present on those difficulties).
-- The password list is ITEM_DOSSIERRED (66) and the formulas ITEM_MICROFILM (75): neither is carried by any prop of the
-- Facility's own setup (its pickups are ITEM_AK47 and ITEM_MP5K only), so having one can only mean this mission gave it.

export type Props = {
    kalinin: Guard,                        -- the ally, hiding in the washrooms (Agent)
    hideDoor: Door?,                       -- the cubicle door he hides behind: opening it finds him (optional)
    kalininHostage: Guard?,                -- the ally held hostage (Secret Agent and up; optional)
    captor: Guard?,                        -- the guard who holds him
    hostageDoor: Door?,                    -- the door into the room where he is held: opening it, he calls out
    terminal1: Prop, door1: Door,          -- laboratory
    terminal2: Prop, door2: Door,          -- east wing, first door
    terminal3: Prop, door3: Door,          -- east wing, second door
    computer: Prop,                        -- the fourth computer: gives the formulas
    passwords: Item,                       -- the password list
    formulas: Item,                        -- what the computer gives
    objFind: Objective, objLab: Objective, objEast: Objective,
    objPasswords: Objective, objFormulas: Objective, objAlive: Objective,
}
local P = props :: Props

local STEP = 0.25            -- seconds per poll
local MEET_RANGE = 120       -- a player this close to Kalinin finds him (the door was shot away, or there is no hideDoor)
local ARRIVED = 90           -- Kalinin is at the terminal (or has stopped moving)
local WALK_TIMEOUT = 10      -- seconds: he types where he stands if he has not arrived by then
local TYPING = 3             -- seconds of keyboard_right_hand_tapping (frames 0-89)
local NEXT_DELAY = 2.5       -- seconds before he starts another terminal (two can be side by side: lines would pile up)
local LAG_RANGE = 1500       -- Kalinin calls out when the nearest player is farther than this
local LAG_EVERY = 20         -- ... at most this often (seconds)
local CHATTER_MIN = 25       -- seconds between two of his nervous remarks while he follows ...
local CHATTER_MAX = 50       -- ... picked at random in this range

-- Left behind (one at random, never the same twice in a row).
local WAIT_LINES = {
    "Wait for me!",
    "Wait! Please, not so fast!",
    "Don't leave me here, Englishman!",
    "I can't run like you, wait!",
    "Where are you? Don't leave me!",
    "Please, wait! My legs...",
    "Slow down! I'm not a soldier!",
    "Englishman! Wait for me!",
}

-- Nervous remarks while he follows (one at random, never the same twice in a row).
local CHATTER_LINES = {
    "If they catch me, it won't be Siberia. It will be a wall.",
    "My mother wanted me to be a dentist.",
    "I can't stop shaking. Is that normal?",
    "Do you always shoot this much?",
    "I left my coat in the office. My good coat.",
    "Breathe, Yuri. In, out. In, out.",
    "They will put my name on a list. I hate lists.",
    "Was that a guard? That sounded like a guard.",
    "Please don't get us killed.",
    "I only wanted a quiet job and a pension.",
    "Every time a door opens, I age a year.",
    "I am not brave. I want that understood.",
}

local lastPick: { [any]: number } = {}
local function pick(list: { string }): string
    local i = math.random(1, #list)
    if #list > 1 and i == lastPick[list] then
        i = i % #list + 1
    end
    lastPick[list] = i
    return list[i]
end

local diff = mission.difficulty()
if diff < 0 then
    diff = 3
end

-- From Secret Agent on, the hostage version of Kalinin is the one present (the mission sinks the other).
local hostage = diff >= 1 and P.kalininHostage ~= nil and P.captor ~= nil
local kalinin = guard(if hostage and P.kalininHostage then P.kalininHostage else P.kalinin)
kalinin:keep_body()   -- if he dies, his body stays: the players see what happened
local captor = if hostage and P.captor then guard(P.captor) else nil
-- Frigate's hostage takers fire after seeing Bond this long: 3 s on Agent, 2.5 s on Secret Agent, 2 s above.
local EXECUTE_AFTER = if diff >= 2 then 2.0 else 2.5
local seenFor = 0         -- how long the captor has seen a player
local executing = false   -- the captor is shooting Kalinin
local aimed = false       -- the captor was told to cover him (once: re-ordering every poll makes him twitch)
local nextShot = 0        -- clock time of the captor's next shot while executing
local SAFE_RANGE = 1200   -- once the captor is dead, Kalinin waits until no guard is this close ...
local SAFE_FOR = 2        -- ... for this long, before he lowers his hands and thanks the player
local safeSince = -1      -- clock time from which no guard has been near (-1: one is)
local RECOVER = 0.75      -- a hit captor executes Kalinin this long after the hit (his pain reaction)
local hitAt = -1          -- clock time the captor was hit (-1: not yet)
local captorHits = -1     -- the captor's hit count when he started covering Kalinin
local pleaded = false     -- the hostage has called out to the player
local hostageDoor = if hostage and P.hostageDoor then door(P.hostageDoor) else nil

-- Overheard through the door as a player comes near it (in the corridor, not on another floor): the captor
-- questioning Kalinin, so the player knows what waits inside. Paused while no player is near, it goes on from
-- where it stopped; it ends when the door opens. One short line each, slow enough to read during a fight.
local INTERROGATION_RANGE = 500   -- around the door, measured flat ...
local INTERROGATION_HEIGHT = 150  -- ... and at most this far above or below it
local INTERROGATION_EVERY = 7     -- seconds between two lines
local INTERROGATION = {
    { "Guard", "Sit, Comrade Kalinin. Hands on the table." },
    { "Kalinin", "Yes, comrade officer. Of course." },
    { "Guard", "Your pass. The east doors. Every night." },
    { "Kalinin", "I work late. The program is behind." },
    { "Guard", "The program. And who else do you work for?" },
    { "Kalinin", "For the State! For the Motherland!" },
    { "Guard", "Someone in this plant talks to the West." },
    { "Kalinin", "Not me! I barely talk to anyone!" },
    { "Guard", "True. Nobody here will speak for you." },
    { "Guard", "Your own supervisor signed your file." },
    { "Kalinin", "Please... my mother is alone in Murmansk." },
    { "Guard", "Then give me the names of the others." },
    { "Kalinin", "There are no others! I know nothing!" },
    { "Guard", "Everyone knows something, comrade." },
    { "Kalinin", "My hands... they won't stop shaking." },
    { "Guard", "Move from that chair and I shoot you." },
    { "Kalinin", "I won't move. I won't move." },
    { "Guard", "Good. We have until morning." },
}
local heard = 0           -- lines of the interrogation said so far
local nextLine = 0        -- clock time of the next one
local hideDoor = if P.hideDoor then door(P.hideDoor) else nil
if hideDoor then
    hideDoor:unlock(true)   -- nothing may keep a player from opening it
end

type Terminal = {
    term: any, door: any?,
    start: string, done: string,
    gate: (() -> boolean)?,  -- false: Kalinin cannot do it yet
    gateText: string?,
    finish: () -> (),
    state: string,           -- "idle" | "walking" | "facing" | "typing" | "done"
    since: number,
    faced: boolean?,         -- on his spot, facing the terminal
    askedAside: boolean?,    -- he asked the player to step aside
}

local function terminal(prop: number, doorProp: number?, start: string, done: string, finish: () -> ()): Terminal
    return {
        term = door(prop), door = if doorProp then door(doorProp) else nil,
        start = start, done = done, finish = finish, state = "idle", since = 0,
    }
end

local clock = 0
local met = false
local dead = false
local nextChatter = 30   -- the first remark, then every CHATTER_MIN..CHATTER_MAX seconds
local lastLag = -LAG_EVERY
local passwordsDone = false

local lab = terminal(P.terminal1, P.door1, "Laboratory access... one moment.", "Done. The door is open.", function()
    objectives.complete(P.objLab)
end)
-- The two east wing terminals can be done in either order: their lines are picked by progress (see eastLine).
local east1 = terminal(P.terminal2, P.door2, "", "", function() end)
local east2 = terminal(P.terminal3, P.door3, "", "", function() end)

-- The line Kalinin says at an east terminal, by how many of the two are already done (not by which one this is).
local function eastLine(t: Terminal, done: boolean): string
    local other = if t == east1 then east2 else east1
    if other.state ~= "done" then
        return if done then "Done. One more." else "These codes change every shift. Lucky for us, I wrote them down."
    end
    return if done then "Done. The east wing is open." else "Last one. If they catch me after this, I'm a dead man."
end

local function line(t: Terminal, done: boolean): string
    if t == east1 or t == east2 then
        return eastLine(t, done)
    end
    return if done then t.done else t.start
end
local computer = terminal(P.computer, nil, "The formulas are on this machine. Give me a moment.", "Downloading... done. Now we leave.", function()
    give(P.formulas)
    objectives.complete(P.objFormulas)
end)
-- On 00 Agent and 007 the computer is encrypted: it needs today's password list.
if diff >= 2 then
    computer.gate = function() return has(P.passwords) end
    computer.gateText = "Encrypted. Without today's password list I can't get in."
end
local terminals = { lab, east1, east2, computer }

for _, t in terminals do
    t.term:make_usable()   -- a player activates a terminal (B / use) to call Kalinin to it
    if t.door then
        t.door:lock()
    end
end

local busy: Terminal? = nil   -- the terminal Kalinin is working on (one at a time)
local freeAt = 0               -- clock time from which he takes another terminal

local function work(t: Terminal)
    if t.state == "done" then
        return
    end
    if t.state == "idle" then
        if not t.term:was_used() then
            -- nothing until a player activates the terminal
        elseif not met then
            message("The terminal needs a security override.")
        elseif busy or clock < freeAt then
            say("Kalinin", "One at a time!")
        elseif t.gate and not t.gate() then
            say("Kalinin", t.gateText or "I can't.")
        else
            -- He stops following, walks up to the terminal, faces it and types; then follows again.
            busy = t
            kalinin:stop()
            say("Kalinin", line(t, false))
            if kalinin:go_to(t.term, "run") then
                t.state = "walking"
            else
                t.state = "facing"   -- no route: he types where he stands
            end
            t.since = clock
        end
    elseif t.state == "walking" then
        if not kalinin:is_moving() or kalinin:distance_to(t.term) <= ARRIVED or clock - t.since >= WALK_TIMEOUT then
            t.state = "facing"
            t.since = clock
        end
    elseif t.state == "facing" then
        if not t.faced then
            -- The spot in front of the terminal is often where the player stands after using it: he waits for it.
            if kalinin:face(t.term) then
                t.faced = true
                t.since = clock
            elseif not t.askedAside then
                say("Kalinin", "Excuse me, I need to get to the terminal.")
                t.askedAside = true
            end
        elseif clock - t.since >= 0.5 then   -- time to turn
            kalinin:play_anim("keyboard_right_hand_tapping")
            t.state = "typing"
            t.since = clock
        end
    elseif clock - t.since >= TYPING then
        if t.door then
            t.door:unlock(true)
            t.door:open()
        end
        say("Kalinin", line(t, true))
        t.state = "done"
        busy = nil
        freeAt = clock + NEXT_DELAY
        kalinin:follow()
        t.finish()
        if east1.state == "done" and east2.state == "done" then
            objectives.complete(P.objEast)
        end
    end
end

-- The objectives that must be done before the exit counts on this difficulty (see mission.json).
local function othersDone(): boolean
    if not objectives.is_complete(P.objFind) then
        return false
    end
    if diff >= 1 and not (objectives.is_complete(P.objLab) and objectives.is_complete(P.objEast)
        and objectives.is_complete(P.objFormulas)) then
        return false
    end
    if diff >= 2 and not objectives.is_complete(P.objPasswords) then
        return false
    end
    return true
end

while true do
    wait(STEP)
    clock += STEP

    -- His death fails his objective; the mission goes on (the exit then ends it as failed).
    if not dead and kalinin:is_dead() then
        dead = true
        busy = nil
        objectives.fail(P.objAlive)
        if captor and not captor:is_dead() then
            captor:set_ai(2)   -- GAILIST_STANDARD_GUARD: from now on he fights like any guard
        end
    end

    if dead then
        -- nothing more from him
    elseif not met and captor then
        -- Held at gunpoint: hands up, the captor covering him, until the captor dies or shoots.
        if captor:is_dead() and kalinin:guards_near(SAFE_RANGE) > 0 then
            safeSince = -1       -- other guards still around: he keeps his hands up
        elseif captor:is_dead() and safeSince < 0 then
            safeSince = clock
        elseif captor:is_dead() and clock - safeSince >= SAFE_FOR then
            met = true
            kalinin:stand()      -- turns to face the player
            say("Kalinin", "Thank you... you saved my life, Englishman. I owe you. Get me out of here and I'll open every door for you.")
            objectives.complete(P.objFind)
            wait(4)
            say("Kalinin", "Stay close. I'm right behind you.")
            wait(1)
            kalinin:follow()
        elseif not captor:is_dead() then
            if clock < 1 then
                kalinin:surrender()
            end
            local seen = captor:sees_player()
            if seen then
                seenFor += STEP
            end
            local doorOpen = hostageDoor ~= nil and hostageDoor:is_open()
            if not doorOpen and not pleaded and not executing and heard < #INTERROGATION
                and clock >= nextLine and hostageDoor ~= nil and hostageDoor:near(INTERROGATION_RANGE, INTERROGATION_HEIGHT) then
                heard += 1
                say(INTERROGATION[heard][1], INTERROGATION[heard][2])
                nextLine = clock + INTERROGATION_EVERY
            end
            -- He calls out when a player opens the door to his room (or, without that door, when the captor sees one).
            if not pleaded and not executing and (doorOpen or (not hostageDoor and seen)) then
                pleaded = true
                say("Kalinin", "Please! Don't let him shoot me!")
            end
            -- Hit but not killed: he executes Kalinin as soon as he recovers.
            if captorHits < 0 then
                captorHits = captor:hits()
            elseif hitAt < 0 and captor:hits() > captorHits then
                hitAt = clock
            end
            if not executing and (seenFor >= EXECUTE_AFTER or captor:was_shot_at()
                or (hitAt >= 0 and clock - hitAt >= RECOVER)) then
                executing = true   -- the captor's own voice lines are the game's
            end
            if executing then
                if clock >= nextShot then
                    captor:shoot(kalinin)
                    nextShot = clock + 0.75
                end
            elseif not aimed then
                aimed = captor:aim_at(kalinin)
            end
        end
    elseif not met then
        if (hideDoor and hideDoor:is_open()) or kalinin:near(MEET_RANGE) then
            met = true
            kalinin:stand()      -- turns to face the player
            say("Kalinin", "Don't shoot! You're the Englishman? Thank God. Get me out of here, I'll open your doors.")
            wait(0.5)
            kalinin:surrender()
            objectives.complete(P.objFind)
            wait(4)
            kalinin:stand()
            say("Kalinin", "Stay close. I'm right behind you.")
            wait(1)
            kalinin:follow()
        end
    else
        if kalinin:distance() > LAG_RANGE and clock - lastLag >= LAG_EVERY then
            say("Kalinin", pick(WAIT_LINES))
            lastLag = clock
        elseif not busy and clock >= nextChatter then
            say("Kalinin", pick(CHATTER_LINES))
            nextChatter = clock + math.random(CHATTER_MIN, CHATTER_MAX)
        end
    end

    if not dead then
        for _, t in terminals do
            work(t)
        end
    else
        -- Without him the terminals are useless: say so when a player tries one.
        for _, t in terminals do
            if t.state ~= "done" and t.term:was_used() then
                if t == computer then
                    message("Kalinin is dead. You cannot get the formulas without him.")
                else
                    message("Kalinin is dead. Nobody can override this terminal.")
                end
            end
        end
    end

    if not passwordsDone and has(P.passwords) then
        passwordsDone = true
        objectives.complete(P.objPasswords)
        if met then
            say("Kalinin", "Today's passwords! That's what I need for the main computer.")
        else
            message("Today's password list. The programmer will need it.")
        end
    end

    if dead and target:near() then
        mission.fail()   -- Kalinin's objective is failed: leaving ends the mission as failed
        return
    end

    if met and not dead and target:near() and othersDone() then
        objectives.complete(P.objAlive)
        say("Kalinin", "I never want to see this place again.")
        wait(3)
        mission.win()
        return
    end
end
