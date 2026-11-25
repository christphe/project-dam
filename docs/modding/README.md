# Modding: custom missions and scripts

A **mod** is a campaign: an ordered list of missions, each a small delta over one of the original solo levels
(guards moved, added or removed, objects added, objectives, zones, Luau scripts). A mod holds numbers and text only,
never game data, so it is safe to share. The format is documented in `port/mission/mission.h`.

## Installing a mod

Put the mod folder next to the game's `data/` folder, under `mods/`:

```
<game>/mods/formula/campaign.json
<game>/mods/formula/formula/mission.json
<game>/mods/formula/formula/scripts/formula.luau
```

(On the Switch: `sdmc:/switch/project-dam/mods/`.) The mod then shows on the game's Mods screen and its missions fill the
mission select. The folder name is free; progress is kept per mod by the `id` in `campaign.json`.

## The Formula sample

`docs/modding/samples/formula/` is a complete mod you can copy into `mods/` and play, or open in the editor
(**project-dam-editor**: open the mod folder from its Mod tab, see `editor/README.md`; the editor shows the Facility from a
level export it makes from your own game data). It is a Facility mission with an ally to protect:

- **Agent:** Yuri Kalinin, a programmer, hides behind a washroom cubicle door (`hideDoor`, setup door 32). Opening it
  finds him: he turns to the player, hands up, then follows. He is added on the mission pad `p3` at (-663, -45, -546),
  present on Agent only (`"difficulties": [0]`). A door's pad in the setup is a *bound* pad (index without the +10000 of
  the pad numbers objects use), which is why a door is not found by looking for pads near a position.
- **Secret Agent and up:** he has been caught. A second Kalinin (`kalininHostage`) stands hands up in a room past door 56
  (`hostageDoor`), a guard (`captor`) behind him. Near that door the player overhears the captor questioning him; opening
  it, Kalinin begs for his life. The captor shoots him, as Frigate's hostage takers do, when he has seen a player for 2.5 s
  (2 s from 00 Agent), when a shot misses him, or as soon as he recovers from a hit. Kill the captor first: once no guard
  is left near Kalinin he thanks the player and follows.
- The Facility's own security terminals are removed and replaced by terminals of the mission's own that are linked to
  nothing. A player uses a terminal (the action button) to call Kalinin over: he stops following, walks to the spot in
  front of it, turns to it and types (`keyboard_right_hand_tapping`, 3 s), then the door opens and he follows again. The
  script uses `door:unlock(true)` so the door's native lock goes too and it can be used again after it closes.
- A fourth computer (on `p1`) gives the formulas: Kalinin downloads them and the script `give`s the item.
- The exit zone sits on the level's own exit (room 69), past the computer: getting the formulas does not end the mission,
  reaching the exit with Kalinin alive does, once the objectives of the difficulty are done. If he dies, "Keep Kalinin
  alive" fails (the game's own "objective failed" line), his body stays, the terminals tell the player they are useless,
  and reaching the exit ends the mission as failed. The last door before the exit (102) stays the game's own: on Secret
  Agent and up it needs Dr. Doak's decoder.

Difficulty (`mission.difficulty()`): **Agent** keep Kalinin alive and leave. **Secret Agent** also download the formulas
(and override the lab and east doors). **00 Agent / 007** also find today's password list, a pickup in the laboratory
(object on `p4`, present only on those difficulties through the object op's `"difficulties": [2, 3]`); the computer
refuses until a player has it. Objectives carry their lowest difficulty in `mission.json`.

How it is built:

| Part | Where |
|---|---|
| Removed terminals (props 64, 70, 78) and Trevelyan (guard 382) | `objects` / `guards` `remove` ops in `mission.json` |
| New terminals, the computer, the password list | `objects` `add` ops (the terminals are type 10 monitors, model 337, on the removed ones' pads) |
| Kalinin | `guards` `add` ops with a body (33), a head (50), sunglasses (`bitflags` 1) and a passive AI list: one per difficulty set |
| The captor | a `guards` `add` op (body 2, the level's guard weapon), Secret Agent and up |
| Pictures | `cover.png` (the mod on the Mods screen) and `formula/thumb.png` (the mission), 320x240 |
| The story | `formula/scripts/formula.luau`, run on the zone `exit` |

**Props a mission adds have a uuid.** Every guard or object `add` op carries a `"uuid"` (required: lowercase RFC 4122 text, 36
characters, unique in the mission; the editor makes a random v4 one) and an optional `"name"` (display only). A script
references a prop the mission adds by that uuid and never by a number: the setup number of an added prop is only
the level's prop count plus its position in the add list, so adding one guard would silently shift every later reference.
A `Guard` / `Door` / `Prop` property of a script is an **original level prop's number** (door1 = 63, hideDoor = 32) **or the
uuid string of an added prop**, and a script target is `{ "kind": "prop", "prop": 63 }` or `{ "kind": "prop", "uuid": "..." }`.
A number at or past the level's own prop count is an error (the editor reports it, the game logs it and treats the prop as
missing). When the game builds the level's setup it numbers the added records (the Facility has 523 props, so the first added guard present
on the difficulty played becomes 523, and so on: the numbers differ between difficulties, the uuids do not) and resolves every uuid to that number, logging one `missions:` line per
resolved uuid; the scripts still see plain numbers (`Guard = number`). A prop that is absent on the chosen difficulty
(an add op with `"difficulties"`) resolves to -1: scripts treat it as a missing prop. In the sample, `kalinin`,
`terminal1..3` and `computer` are uuids and `door1..3`, `hideDoor` stay numbers (original doors).

## Pictures

A mission's `thumb.png` is its image in the mission grid; the mod's `cover.png` is its picture on the Mods screen (without
it, the first mission's thumb). Both are 320x240 RGB PNGs, made in the editor (Mission tab > Thumbnail > Capture or Import
picture; Mod tab > Choose picture). The editor writes them compressed (per-row PNG filters, zlib level 9): a capture is
a few tens of KB.

## Scripts

A script is a Luau file (`<mission>/scripts/<name>.luau`, `<mod>/scripts/<name>.luau`, or a built-in library script).
`mission.json` lists the scripts of a mission; each entry names the script (`use`), a `target` (a door or guard prop, a
pad, a zone, or the mission itself) and `props`, the values the script reads.

The whole API (globals, target methods, event names) is declared in **`port/mission/lib/mission.d.luau`**, a Luau
definition file. It is the reference; the prose version is the comment at the top of `port/mission/mission_lua.h`. In
short: `give` / `take` / `has` for items, `message` and `say` for text, `wait(seconds)` and `wait(event(...))`,
`objectives.*` (`complete`, `fail`), `alarm.trigger()`, `mission.win()` / `mission.fail()`, and targets with `:lock()`,
`:unlock([force])`, `:open()`, `:near([radius] [, height])`, `:distance()`, `:distance_to(other)`, `:is_dead()`, `:set_ai()`,
and `:say(who, text [, range])`: the line of `say`, with a marker over the target (above a guard's head) in every
player's view (`player(n):say(...)` makes a player speak: the others see the marker over it), pinned to the edge of the view when the speaker is out of it, fainter and smaller the further away; with
a range, players farther away see neither the line nor the marker.
Guards also have `:follow()` / `:stop()`, `:go_to(target)` / `:is_moving()`, `:face(target)`, `:play_anim(name)`,
`:surrender()` / `:stand()`, `:aim_at(other)` / `:shoot(other)`, `:sees_player()`, `:was_shot_at()`, `:hits()`,
`:guards_near(radius)` and `:keep_body()`; objects have `:make_usable()` / `:was_used()` (a terminal a player activates).

Each script runs as a coroutine in a sandbox (no `io`, `os`, `require`; a time budget per resume), so a script waits
instead of looping: use `wait(0.25)` in a polling loop or `wait(event(...))`.

### Typed props

A script declares what it reads at its top, as a Luau type:

```lua
export type Props = { key: Item, message: string?, objective: Objective? }
```

The aliases `Item`, `Objective`, `Door`, `Guard`, `Prop`, `Zone` and `Pad` are defined in `mission.d.luau`; they are plain
numbers and strings at run time (the compiler erases type annotations), and tell the editor which picker to show for
each property. Read the values through the `props` global as before; `local P = props :: Props` gives a typed view.

### Library scripts (`port/mission/lib/`)

| Script | Does | Props |
|---|---|---|
| `key_door` | a door stays locked until a player has the key item | `key: Item`, `message: string?`, `objective: Objective?` |
| `give_item` | gives an item when a player reaches the target | `item: Item`, `message: string?`, `radius: number?` |
| `exit_unlock` | once every objective is complete, unlocks a door or wins at a zone | `message: string?` |
| `pickup_objective` | completes an objective when a player picks an item up | `item: Item`, `objective: Objective`, `message: string?` |

Your own scripts can use the same names as library scripts; the mission's folder wins, then the mod's `scripts/`, then
the library.
