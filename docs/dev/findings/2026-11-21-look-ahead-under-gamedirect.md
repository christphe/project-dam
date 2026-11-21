# Look ahead under GameDirect: disableLookAhead does not stop a running centre

**Trap.** In `bondview2.c`, `moveData.disableLookAhead = 1` only clears `automovecentre` (~5961). A centre that has already started (`docentreupdown`) keeps running until it reaches the target (~6002-6038). On the N64 the only thing that stops it is a manual pitch through `speedVertaUp/Down` (C-up / C-down in HONEY, ~5953), which also sets `movecentrerelease` while walking (~6075-6086): no new centre until the walk stick goes back under 40. Under GameDirect (#78) the look stick drives `analogPitch` (natural pitch), which that check never sees, so letting the game's look-ahead run there without more makes it fight the look stick.

**Second trap.** The SOLITARE / GOODNIGHT `disableLookAhead = 1` (~5535) is set AFTER the GameDirect hook (~5343), at the end of the 1.x branch. Clearing it inside the hook does nothing.

**What #85 does.** A `#ifdef PORT` block right after that line asks `portLookAheadMode` (input.c). For Original, it clears `disableLookAhead`, and while the look stick is deflected it does what C-up / C-down does: `docentreupdown = automovecentre = FALSE`, `movecentrerelease = TRUE`. For Low / Medium / High, it keeps the game's centre off (`docentreupdown = FALSE`) and `nxLookAheadStep` (nxgame.c) eases toward the exported `targetPitch` (without its -4 base, so flat ground stays level, D957).

**Also.** `analogWalk > 60` is not "analog movement only": on the N64 it means "the stick walks, decidedly", and the styles where the stick is the view turn the look-ahead off. Supersedes the D957 premise that the digital walk was the only blocker.
