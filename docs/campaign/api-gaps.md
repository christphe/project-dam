# Campaign API: gaps the level scripts hit (#165)

Collected from the clean-room script writers' TODO(api) markers. Each becomes a declaration in
`port/mission/lib/mission.d.luau` and an implementation in its domain, then the TODOs are resolved in the scripts.

| Gap | Wanted by | Domain | Proposed |
|---|---|---|---|
| Stop / resume the mission clock (the end-of-level time) | surface1, surface2, bunker2 | world | `mission.clock_running(on)` |
| A turret / drone gun on or off | surface1 | world | `tag(n):set_active(on)` |
| An object only explosions can damage | surface1 | world | `tag(n):set_damage("explosions")` |
| The moment the intro swirl starts | surface1, surface2 | cinema | event `"intro_swirl"` |
| Body flags in a scene: culled by its hit box, height locked | surface1 | cinema | `bond_body():set_flags{...}` |
| Put away / drop the body's hand item | surface2 | cinema | `bond_body():drop_item(hand)` |
| A prop by its setup number for any type (not only doors / guards) | surface2 | world | `prop(n)` |
| `fire_at` a body target (a scene) | surface2 | guard | accept kind "body" |
| Explosions around the players (the bunker's destruction) | bunker2 | world | `explosions_around_players()` |
| A raw chr flag (unknown meaning, kept as data) | bunker2 | guard | `set_flag_raw(bits, on)` |
| A door characters can use | bunker2 | world | `door:set_usable_by_guards(on)` |
| Whether a guard spawned during `hide_all_chrs` shows after `set_hidden(false)` | surface2, bunker2 | cinema | define it: yes |
| Music options: what `stop_after` / `total` / `fade` mean exactly | surface1 | world | document |
| Hearing and vision ranges, the clone-sender flag as stats | surface1, surface2, bunker2 | guard | `GuardStats.hearing`, `.vision`; flag `clone_sender` exists |
| One-update guard signals as events (near miss, witnessed hit / death, spotted, heard) instead of latched queries | guard_ai, caverns (samples for 0.5 s), train | guard | events `guard_near_miss`, `guard_witnessed`, `guard_spotted` (on the not-seen to seen edge), `guard_heard`; proposed to the maintainer |
| The number of players | train, dam, runway | world | `mission.players()` |
| Start inventory, ammo, watch time | train, dam, runway | world | left to the setup for now; `give()` exists |
| Intro shots at raw positions, the swirl spline | train, dam, runway | cinema | the engine's own intro plays them; `camera.path` for others |
| An aircraft / vehicle model animation | runway, cradle, frigate | cinema | accept `anim()` on an object |
| No gravity on the body (held in the air) | dam, cradle | cinema | `bond_body():set_gravity(on)` |
| The Dam truck has no tag or prop number in the spec | dam | spec | find its setup prop and add it to dam.md |
| Drop a live grenade | cradle | guard | `drop_grenade()` |
| Ignore animation translation on the body | cradle | cinema | `AnimOpts.move = false` exists: also on bodies |
| Knock-back / hit-box culling on the body | cradle, surface1 | cinema | `bond_body():set_flags{...}` |
| "Killed in action" end, kill one player in co-op | cradle | world | `mission.killed_in_action()`, `player(n):kill()` |
| The item ids that count as a real weapon | caverns, frigate | spec | add the list to the specs |
| A player's health (the existing health() is a guard's) | control, depot, egypt | world | `player(n):health()` 0..1 |
| A door passable by one character only | control | world | `door:set_passable(on, {only = target})` |
| The end of a surrender (hands back down) | control | guard | `anim_done()` after `surrender()`, or an event |
| The body of the spawned reinforcements | control | spec | add the body numbers to control.md |
| The body part of a scripted hit | depot | spec | add the number to depot.md |
| The body aims, fires, kneels, walks in a scene | depot, egypt | cinema | `bond_body():aim_at / fire_at / kneel / walk_to(pad)` |
| A flinch rating (0..100), not a boolean | egypt | guard | `GuardStats.flinch: number` |
| `picked_up.tag` and the #165 event fields in MissionEvent | egypt | API file | add the fields to MissionEvent |
| The game's own key-copy and photograph objectives (state / events) | bunker1 | world | events `key_copied {tag}`, `photographed {tag}` |
| Add armour (+n), not only set it | bunker1, statue, aztec | guard | `GuardStats.add_armour` |
| Hit recovery as a stat | aztec | guard | `GuardStats.recovery` |
| A shuttle / rocket lift-off (object animation) | aztec | cinema | `anim()` on an object |
| `set_rotor` speed unit | statue | world | document (game units per tick) |
| `activated` for the setup's own terminals without make_usable() | aztec, dam | world | define it: yes |
| How guard_ai hands a guard back after a mode with fallback "inert" | bunker1, facility | guard_ai | event or `guard_ai.on_done(g, fn)` |
| Every death, not only player kills (civilians, hostages) | facility, silo, frigate | guard | event `death` exists for setup guards; add `chr` and spawned guards |
| A guard holds a specific object (the decoder in Doak's hand) | facility | guard | `give_object(tag)` |
| `give()` without the pickup message | facility | world | `give(item, player, {silent = true})` |
| A gas-leak event (the bottling room) | facility | world | event `gas_leak` |
| Is an item lying in a given room (a planted charge) | silo | world | `item_in_room(item, pad)` |
| Drop one object, not everything held | silo | guard | `drop(tag)` |
| An orbit centre between two pads | cuba | cinema | `camera.orbit{centre = {pad(a), pad(b)}}` |
| Units of camera.orbit (degrees, look_height offset) | cuba | cinema | document |
| bond_body: set_invincible, aim | jungle | cinema | `bond_body():set_invincible / aim_at` |
| Vision range as a stat | jungle, archives | guard | `GuardStats.vision` |
| Two characters in the same room | jungle | world | `a:same_room_as(b)` |
| A spot near the player (not cover) | jungle, streets | guard | `guard:spot_near_player()` |
| A guard's held item | jungle | guard | `guard:held_item()` |
| Give a tagged object (an inventory key) to a player | archives | world | `give_object(tag, player)`; blocks archives objective c on SA+ |
| Reinforcement bodies | jungle | spec | add the body numbers to jungle.md |
| Does countdown.set() keep a running clock running | streets | world | define it: yes |

## Shared helpers to move to `campaign/scripts/`

- The level-specific hunter and the small helpers are copied in `surface1_lib`, `surface2_lib`, `bunker2_lib`
  (a mission's require() sees its own folder and the mod's `scripts/` only, and the level writers may not write
  there). Merge them into one `campaign/scripts/` module at integration.
