# The render space is scaled by the level's visibility scale

**Trap.** GE does not render in world units. `bondview2.c` builds the render lookat from `(camera - player) x D_800364CC`, and `D_800364CC` is the level's visibility scale (`bg.c bgGetLevelVisibilityScale`, 0.2 on Dam). The camera matrix GE hands out for gameplay (`camGetWorldToScreenMtxf`, `field_64`) is in world units, so an eye-space position built with it is 1/scale too far from the camera compared with the eye positions the shaders rebuild from the pixels.

**Symptom.** Dynamic lights placed with `camGetWorldToScreenMtxf` light the wrong spot on levels whose scale is not 1: on Dam an explosion's light sat five times too far out and reached five times too far. A sun shadow map built from world-unit matrices covered a fifth of what its constants said, so shadows only showed near the camera.

**What works.** Multiply an eye-space position and a radius by the scale before handing them to the shaders (`port/src/dynlights.c renderScale`). A matrix that takes the shaders' eye positions into a world-unit space needs its first three rows divided by it. Anything drawn and looked up through the same matrix (a shadow map's casters and receivers) is consistent either way: only its coverage changes.
