-- mission.d.luau: the definition file of the mission script API (a Luau "declare" file).
-- It is a reference for modders and the source the editor reads for its pickers; it is NOT a runnable script
-- (port/mission/mission.cmake embeds it as g_missionApiDefs, not as a library script). The runtime is
-- port/mission/mission_lua.cpp; the full prose is the API comment in port/mission/mission_lua.h.
--
-- Editor-facing aliases: a script declares the properties it reads as a type at its top,
--     export type Props = { key: Item, message: string?, objective: Objective? }
-- and the editor offers the right picker for each alias (an item list, the mission's objectives, ...).
-- At run time they are all plain numbers or strings: type annotations are erased by the compiler.
--
-- Dialect: Luau 0.746 (the vendored Analysis the editor checks scripts with): `declare extern type X [extends Y] with ... end`,
-- `export type` for the aliases (a plain `type` is not visible to scripts), and overloads as an intersection of function types.

export type Item = number            -- an ITEM_IDS value (the editor shows the name)
export type Objective = string       -- the id of one of the mission's objectives
export type Door = number            -- a setup prop number of a door; in mission.json an original's number or an added door's uuid
export type Guard = number           -- a setup prop number of a guard; in mission.json an original's number or an added guard's uuid
export type Prop = number            -- a setup prop number of any object; in mission.json an original's number or an added object's uuid
export type Zone = string            -- the id of one of the mission's zones
export type Pad = number | string    -- an original pad number, or the id of one of the mission's pads

-- What wait(event(...)) returns, and the fields an event filter can match.
export type MissionEvent = {
    name: string,             -- "start" | "end" | "pickup" | "death" | "door" | "zone" | "objective"
    win: number?,             -- "end": 1 if the mission was won
    item: number?,            -- "pickup": the ITEM_IDS value
    player: number?,          -- "pickup", "zone": 0-based player
    prop: number?,            -- "death", "door": the setup prop number
    state: string?,           -- "door": "open" | "closed" | "moving"
    id: string?,              -- "zone": the zone id; "objective": the objective id
}

export type EventFilter = { [string]: number | string | boolean }
export type EventDescriptor = { __event: string, filter: EventFilter }

-- A target: a setup prop (door, guard, object), a pad, a zone, or the mission itself.
declare extern type Target with
    kind: string              -- "prop" | "pad" | "zone" | "mission"
    prop: number?             -- kind "prop": the setup prop number
    zone: string?             -- kind "zone": the zone id
    pad: (number | string)?   -- kind "pad": the pad number or id

    -- doors
    function lock(self)                       -- the door stays shut until unlock()
    function unlock(self, force: boolean?)    -- force: also remove the door's native lock (decoder / key) for the rest of the level
    function open(self)
    function close(self)
    function is_open(self): boolean
    -- guards
    function is_dead(self): boolean           -- true once the guard's chr is gone or dead
    function set_ai(self, list: number)       -- an AI list id
    function follow(self, player: number?)    -- walk to the nearest living player (or only `player`, 0-based) whenever
                                              -- farther than 200 units, run beyond 350, sprint beyond 1200. The guard
                                              -- gets a passive AI list so it neither flees nor attacks.
    function stop(self)                       -- stop issuing follow moves
    function surrender(self)                  -- hands up (an armed guard drops its weapons)
    function stand(self)                      -- stand still facing the nearest player, hands down
    function go_to(self, target: Target, speed: string?): boolean   -- to a prop's pad: "walk" | "run" (default) | "sprint";
                                              -- false: no route. :stop() a follow first or it takes the guard away
    function is_moving(self): boolean         -- still on its way after go_to / follow
    function face(self, target: Target): boolean   -- face a prop square on, set exactly on the spot go_to aims for when
                                              -- close; false: someone stands on that spot (call again once it is free)
    function keep_body(self)                  -- once dead, the body stays where it fell (the game fades bodies out)
    function aim_at(self, other: Target): boolean   -- an armed guard keeps another guard covered; false: unarmed
    function shoot(self, other: Target)       -- fires at the other guard, which takes a hostage taker's hit
    function sees_player(self): boolean       -- the guard sees a living player
    function was_shot_at(self): boolean       -- a player's shot just missed it
    function guards_near(self, radius: number): number   -- living guards within radius (the target itself left out)
    function hits(self): number               -- how many times the guard has been hit
    -- objects
    function make_usable(self)                -- players can activate it (B / use), like the game's own terminals
    function was_used(self): boolean          -- true once per activation by a player since the last call
    function play_anim(self, name: string): boolean   -- once: "keyboard_right_hand_tapping" | "keyboard_left_hand" | "keyboard_right_hand2"
    -- distances and presence (world units)
    function near(self, radius: number?, height: number?): boolean   -- a player within `radius` (a zone's own radius when
                                              -- omitted); with `height`: radius measured flat, and at most `height`
                                              -- above or below (a floor above or below does not count)
    function distance(self): number                     -- to the nearest living player
    function distance_to(self, other: Target): number   -- between two targets; 1e30 when one has no position
end

-- The type of `target` per attachment kind (the editor binds the one that matches the entry; fields of that kind are not optional).
declare extern type MissionTarget extends Target with
end
declare extern type PropTarget extends Target with
    prop: number
end
declare extern type PadTarget extends Target with
    pad: number | string
end
declare extern type ZoneTarget extends Target with
    zone: string
end

-- Globals of every script.
declare target: Target        -- the script's own target (mission.json "target")
declare props: { [string]: any }   -- the script's values (mission.json "props"); type them with `export type Props`

declare function door(prop: number): Target
declare function guard(prop: number): Target

declare function give(item: number, player: number?)    -- player: 0-based; omitted = every player
declare function take(item: number, player: number?)
declare function has(item: number, player: number?): boolean   -- omitted = any player

declare function message(text: string)                  -- on-screen text, bottom of the screen
declare function say(who: string, text: string)         -- dialogue line at the top of the screen: "who: text"

declare wait: ((seconds: number) -> ()) & ((ev: EventDescriptor) -> MissionEvent)   -- wait(seconds) sleeps; wait(event(...)) sleeps until it happens
declare function event(name: string, filter: EventFilter?): EventDescriptor

declare function print(...: any)                        -- to the game log

declare objectives: {
    add: (id: string, text: string, difficulty: number?) -> (),
    complete: (id: Objective) -> (),
    is_complete: (id: Objective) -> boolean,
    fail: (id: Objective) -> (),                        -- "Objective a: failed" on screen, shown as failed; no completing it
    is_failed: (id: Objective) -> boolean,
    all_complete: () -> boolean,                        -- of the difficulty being played
}

declare alarm: {
    trigger: () -> (),
}

declare mission: {
    win: () -> (),                                      -- end the level as a success
    fail: () -> (),                                     -- end the level as a failure
    difficulty: () -> number,                           -- 0 Agent, 1 Secret Agent, 2 00 Agent, 3 007; -1 without one
}
