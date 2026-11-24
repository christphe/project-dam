RULE-2-SIGNOFF

# Render-only effect markers in `src/game` (muzzle flash, tracers, puffs, blood, impact decals)

**Status:** signed off 2026-11-23; markers implemented and verified on the Windows build (the shaders that consume them come later).

## Request as stated (`docs/dev-process.md` §7)

1. **Change.** Under `#ifdef PORT`, the game brackets its effect draws with a tagged `G_NOOP` (`gDPNoOpTag`, `port/include/portfx.h`). It changes nothing else.
   - `src/game/glass2.c` `bullet_spark_render`: one span per puff. The puff is tagged `PORTFX_BLOOD` when its colour is `g_BulletSparkColors[2]` (the Game.BloodOnHit body puff), and `PORTFX_SPARK` otherwise.
   - `src/game/explosion.c` `explosionRenderBulletImpactOnProp`: one `PORTFX_IMPACT` span around the decal loop.
   - `src/game/gunfire.c`: `PORTFX_TRACER` around both 1P `sub_GAME_7F061E18` beam calls. Sets `g_PortFxMuzzleNode` (the hand model's `Switches[1]`, the node gunfire already uses for `flashvisptr`) around the hand-weapon `subdraw`.
   - `src/game/lv.c`: `PORTFX_TRACER` around `weaponRenderTracers` (guards and autoguns).
   - `src/game/model.c`: in `dogfnegx` (chr GUNFIRE node), a `PORTFX_MUZZLE` span around the flash quad. In `subdraw`, the span opens on `g_PortFxMuzzleNode` and closes when the walk leaves its subtree.
2. **Proof of divergence.** None. This does not fix a divergence from the N64. It is a render-only enhancement for the Switch remake, where fidelity is not binding. The markers do not change game state or control flow. The N64 RSP ignores `G_NOOP`, and the game never emits it otherwise (finding B1).
3. **Why not `port/` alone.** fast3d sees only RDP state and triangles. The effects share generic combiner and render modes, and the 1P flash is an ordinary DL inside the gun model, so the renderer cannot identify them reliably without a hint from the emitting code. **Why not the ABI exception.** Nothing here is pointer-width related.
4. **Fix scope.** These are additive commands only, one span per effect batch or puff. The `#else` (N64) build is byte-identical.

## Grant

2026-11-23, in-thread: "ok option 2, go". This was the maintainer's reply to the option-2 plan (tagged `G_NOOP` markers that fast3d reads, with dedicated shaders per effect kind).

## Verification (Windows build, Dam, KF7 autofire, `GE_FXLOG=1`)

- With no firing, zero tagged triangles. The 1P muzzle span still opens each frame with 0 triangles, because the switch node is walked even when hidden.
- When firing, muzzle, tracer, spark and impact spans each carry triangles. There are more than 60 muzzle spans a second, which means the guards' `dogfnegx` flashes are being tagged too.
- Blood was not exercised at run time, because autofire hit no guard. The classification reads the spark colour copied verbatim from `g_BulletSparkColors` in `bullet_sparks_init`.

## Extension: event hooks and render replacement (2026-11-23)

**Request.** Add one-line `GE_EV_*` / `GE_FX_*` hook macros in `src/game`. They follow the co-op `GE_*` convention in `src/game/player.h` (D960): under `PORT` each macro calls the port event bus (`port/src/portevents.c`), and the N64 arm is empty or the original expression. They fire at:
- the creation of bullet impacts, impact puffs, guard hits and shots/tracers (action events, before/after);
- the game's render calls for puffs and tracers (a "replace" hook: when a port subscriber handles the event, the game's own draw of that effect is skipped).

Replacement is limited to rendering. A subscriber that replaced game logic would need its own rule-2 request.

**Grant (2026-11-23, in-thread).** "fais 1 et 2". Step 1 = the bus (actions, filters, replace) with a log subscriber and unit tests. Step 2 = effects as the first user (event macros, render replacement, particles per material). Converting the co-op hooks to the bus was explicitly deferred. So was wrapping a hook around a pointer to the original action ("on verra ... un jour").
