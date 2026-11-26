# The uber shader drawing an effect hangs the Switch GPU

**Symptom.** Shooting a wall with the Enhanced effects on freezes the game after a few seconds of fire: the image
stops, the sound goes on ("D204: audio queue full"), the kernel heartbeat fires ("no frame rendered"). On Ryubing the
same run ends with `VulkanException: Unexpected API error "ErrorDeviceLost"` in Ryubing's own log
(`<ryubing>/Logs/`), after `Shader translator: Shader instruction Pret is not implemented` when firing starts.
`GE_FX=0` makes it go away.

**Cause.** An effect combiner (an id with a PORTFX kind, `SHADER_OPT_FX_MASK`) missing from the shader pack was drawn by
the uber shader (`DKSH miss` in the log). The uber shader compiled by uam for the console runs the effect code through
subroutine calls (the `Pret` Ryubing does not implement) and the submission that draws it never completes: the
fence waits forever (`dkQueueAcquireImage` blocks, then every thread behind it). The draw that hangs is not where it
looks: the pending submission at the freeze is whatever was in flight (end-of-frame resolve + FXAA, for instance), not
the culprit.

**Ruled out.** Ring or descriptor exhaustion in the deko3d backend (the rings grow, the pools are maps), a CPU deadlock
(the GPU fence is the one not signalled), FXAA (off: still hangs), any single fxparticles path (each off alone: still
hangs), the effect draws themselves (skipping every PORTFX-kind draw at the backend still hung: the uber draws of the
same frames remained).

**How it was found.** A stage probe in the heartbeat (last stage per thread + the oldest pending fence) showed a GPU
hang, not a CPU one; logging the first use of each program per draw site, with and without `GE_FX`, left 4 programs,
all uber, first drawn a few frames before the freeze; skipping only the uber draws (`p->uber`) removed it.

**Rule.** The uber shader is the fallback for the game's combiners only. Every effect combiner has its own shader in
the packs (`port/shaders/fx/combiners.txt`, code in `port/shaders/fx/`, `tools_pc/dkshgen`); an effect missing from the
pack is not drawn and logs an error (`tools_pc/cc_census_merge.py` files it). Do not route an effect back to the uber
shader to "make it show".
