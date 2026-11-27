---
title: Development Process
description: The investigation workflow behind the port; task budgets, file partitioning, and the finding-log discipline used across sessions and agents.
---

## How this port is developed

Most of this port's work is not writing code; it is **diagnosis**: figuring
out which N64 hardware or ABI behavior a piece of unmodified game code is
silently relying on, and satisfying it in the `port/` layer without touching
the game's logic. A lot of that diagnosis is done with LLM coding agents, and
the workflow below is what makes that productive rather than chaotic.

For the project context (the two-model split, the timeline, and an honest
assessment of what did and did not work), see
[`dev/agentic-development.md`](dev/agentic-development.md).

It is adapted from Chris Lewis's write-up
[*"Decompiling a Nintendo 64 Game in 84 Days"*](https://blog.chrislewis.au/decompiling-a-nintendo-64-game-in-84-days/)
(Snowboard Kids, 2026), an AI-assisted decompilation that ran several times
faster than its predecessor. That project bit-matches compiler output;
this one matches runtime behavior, so the mechanics differ; but the
project-structure lessons carry over directly.

### `src/game` may be changed (non-negotiable #2 withdrawn 2026-11-27)

The decompiled code is being moved out of the project and removed, so
`src/game` is no longer frozen: it is edited or deleted like any other
code, and there is no sign-off procedure any more. Two habits stay:

- **Diagnose to the exact cause first.** Trace a bug as deep into game
  state as the evidence leads and name the struct, field or logic
  responsible before choosing where to fix it. The 32→64-bit layout
  traps (`docs/porting-notes.md` §A1) are still the most common cause of
  "game state looks wrong".
- **State behaviour changes.** A fix that changes what the game does is
  said so in its PR: what it did before, what it does now.

### Clean-room porting (AGENTS.md rule 7)

The decompiled code is being replaced, not copied. Porting a piece of game
logic or a data format follows two steps with two different agents:

1. **Spec.** An agent that may read the decomp writes what the code does as
   facts in prose, in a page of `docs/specs/` (byte layouts, fields, rules,
   constants, measured values, edge cases), following
   `docs/specs/README.md`. No code is pasted or paraphrased line by line, and
   decomp function or variable names are not needed to read the page. The lead
   reviews the page before step 2.
2. **Implementation.** A different agent, whose brief says MAY NOT READ:
   `src/`, `include/`, `rsp/`, `PR/gbi.h`, libultra, `port/fast3d/` and any
   tool derived from them, writes the code from the spec pages only. When the
   spec is silent or ambiguous, it stops and reports the question; the spec
   agent answers by amending the page. The implementation agent never looks
   the answer up in the decomp, and the lead never passes decomp code or names
   in a brief.

Verification compares the new code's output with the old code's (a capture,
an export check, a pixel diff), never its source. `port/export/` (issue #150)
is the worked example: specs in `docs/specs/`, code written from them.

## 1. Every investigation task gets a visible budget

An open-ended tool; for them the permuter, for us "rebuild and run the game
and stare at a frame dump"; wrecks throughput when an agent will grind it
indefinitely. Giving the agent a **deadline it can see** lets it trade
tool-time against thinking and decide when to give up.

So every investigation brief states:

- a **budget**; "≤ N build → run → inspect cycles" or "~M minutes"; and
- the **fallback on expiry**: stop, revert any temporary probes, and write up
  what was found with an explicit confidence rating. A good write-up of a
  half-solved bug is a deliverable, not a failure.

The budget scales with the project's maturity. Early bugs are cheap (one-line
truncations, off-by-ones) and get a tight cap; once those are gone the
survivors are structural (matrix handedness, serialization formats) and get
more room.

## 2. A shared, append-only learnings file

Lewis's agents recorded generalisable IDO quirks in a learnings file; later
agents read it and did better. Our equivalent is
[`porting-notes.md`](porting-notes.md); the recurring N64→PC bug classes,
each entry a terse index into the full
[`dev/findings.md`](https://github.com/jkdansereau/goldeneye-pc-port/blob/main/docs/dev/findings.md)
log. Every investigation brief links it, and every investigation ends by
appending any new generalisable quirk.

The classes it currently tracks:

- pointer-width struct growth (32→64) in ROM-serialized or pun-allocated
  structs; the dominant class;
- 16-byte host `Gfx`/`Vtx` vs 8-byte N64; reservation and copy sizing;
- big-endian rodata read on a little-endian host: `f32` word-pairs, header
  offset tables, packed bitfields;
- N64 hardware idioms the software RSP does not emulate (fill-rect Z clear,
  LOD/detail tiles, segment-address folds);
- whole serialized formats are converted by an **offline sidecar** rather than
  patched at runtime.

**Two process lessons from the sibling GEVR VR port** (same decomp lineage,
reference-only survey in `scratchpad/GEVR-TRIAGE.md` item D — their corpus
hit both repeatedly enough to name them): (1) **a grep standing in for a
read** — a site list built by grepping one helper name or spelling misses
siblings spelled differently or living in an adjacent file (their own
missed-smoke-scissor-site example); treat a grep-built site list as a draft,
not a census, until it's been read against. (2) **write the falsifier before
the run** — state up front what result would kill the hypothesis ("if X
survives, hypothesis Y is dead"), not just what would confirm it; this fits
the existing budget/findings discipline directly and costs one sentence per
brief.

## 3. Parallelise by file, consolidate often

Lewis ran multiple git worktrees and found the real cost was
*synchronisation*. For parallel investigation agents the same rule applies:

- **Partition tracks by the files they will touch**, so patches never collide.
  A task that can't be cleanly file-partitioned shouldn't be parallelised.
- Each agent reverts its own temporary probes before reporting, leaving only
  committed, env-gated, capped diagnostics; and never touches another track's
  uncommitted edits.
- One integrator merges each track with its write-up, then the next round
  starts from a clean tree. Investigation branches are not allowed to drift.

## 4. Spend the cheap automated pass before spending agent time

Before dispatching an investigation agent:

- confirm the build is green and the link is clean;
- run the existing env-gated probes for the area and attach their output;
- capture a baseline frame dump;
- grep the finding log for a prior instance of the same bug class;
- check the [Perfect Dark port](https://github.com/fgsfdsfgs/perfect_dark) for
  the analogous code; same engine family.

Then hand the agent the *findings*, not just the problem.

## 5. What does not transfer from the decomp workflow

- **The match-percentage loop.** This port doesn't bit-match. "Done" is: no
  fault, and visually correct against N64 reference footage.
- **"Understanding the function is the easy part."** True when decompiling;
  inverted here; the hard part is identifying the implicit hardware/ABI
  contract the already-readable C depends on.

## 6. Keep PR/git-history noise low

A session produces a lot of small, independently-verifiable units of work;
one Dxx row, one docs pass, one mechanical fix. Left unmanaged that turns
into a pile of small open PRs that's harder to review than the work
justifies. Rules:

- **Squash-merge single-purpose and docs-only PRs.** The repo has squash
  merge enabled; use it (`gh pr merge --squash --delete-branch`) for anything
  whose in-branch commit history (iteration, probe-add, probe-strip) isn't
  itself useful to keep; which is most PRs. Reserve a plain merge commit for
  a PR whose individual commits are each a distinct, reviewable unit someone
  might want to `git revert` independently (e.g. this session's PR #50: a
  fix commit + a separate analysis-only commit).
- **Batch same-session, same-flavor docs updates into one PR/branch**
  instead of opening a new PR per finding row. A runtime re-check that
  updates one finding file (like #48) doesn't need its own PR if
  another docs-only branch is already open in the same session; fold it in.
  Only split into separate PRs when the pieces have genuinely different
  review/merge timing (e.g. one needs a human playtest, the other doesn't).
- **A branch takes only the change it's named for.** Don't let unrelated
  housekeeping (a stray gitignore fix, a config tweak) ride along on an
  active feature/fix branch just because it's the one checked out; cut a
  separate branch, even for a one-line change.
- **Stacked PRs declare the dependency in the title/body** ("step 2 of N,
  step 1 is #N") and get rebased/merged in order promptly; don't let a
  later step sit open so long the earlier step's content drifts under it.
- **Triage the open-PR list at least once a session**, ordered by
  time-to-close (docs-only and non-draft first, human-playtest-gated last).
  A PR idle long enough that `main` has moved past the finding rows it
  touches is **stale, not just old**; check for real textual conflict
  (`git merge-tree`), not just calendar age. If `main` already has more
  current information than the PR (a bug the PR still lists `OPEN` that's
  since been fixed and verified), **close it rather than force a merge or
  spend a cycle rebasing it**; re-derive only whatever part of its content
  is still true as a fresh, small PR against current `main`. Merging stale
  content back over newer information is a regression, not a save.
- **Commit/tree hygiene is a standing rule, not a per-session judgment** —
  see AGENTS.md §Tree hygiene & release gate. In short: no local absolute
  paths or usernames in tracked files (use the placeholder convention), no
  contributor emails in prose (name + PR number), every vendored third-party
  license declared in `NOTICE`, no bulk ROM-derived captures in the tree
  (capture output dirs are gitignored — keep them on disk, untracked), and
  one batched commit per session rather than one per approved fix.

## 7. (withdrawn) The rule-2 sign-off procedure

Withdrawn on 2026-11-27 with non-negotiable #2: `src/game` may be
changed (see the top of this file). Existing `RULE-2-SIGNOFF` findings
stay as history.

## 8. LLM session discipline (model tiers, data-before-edit, context resets)

Standing rules for LLM-agent sessions, fixed 2026-10-09 after the session
forensics on the #150 cull hunt (pi session `01a11ec6`, 2026-10-09): a
27B 4-bit-quantized default model spent an evening firing unvalidated
render fixes at the maintainer for A/B (four consecutive failures), and
the mid-session compaction summary promoted an unvalidated hypothesis to
"diagnosed" — the post-compaction turns then built three more fix
candidates on that false premise. The loss was model + discipline, not
retrieval; the rules below are the zero- and low-cost countermeasures.

- **Model tiers.** Planning, diagnosis, and anything that touches a
  contested claim run on the strongest model available — the frontier
  `openrouter/deepseek/deepseek-v4-pro` (no local contention) or, with
  the local backend switched, `strata-coder-hard`. Only ONE local backend
  can run at a time (ninfer ↔ strata), so the **standing backend is ninfer** and the
  session default is its model (`qwen3.8-27b-nvfp4full`, the
  `ninfer-coding.bat` 4-bit daily driver). The strong *local* tier is
  the same backend under `ninfer-quality.bat` (groupwise-int
  `qwen3.8-27b`, 200k ctx) — a launcher restart, not a backend
  switch; the strata 125B remains the top local option at the cost of a
  full switch. Mechanical labor — builds, captures,
  log triage, golden sweeps, doc sync — goes to the ninfer models via
  the model-pinned subagent roles in the `pi/` package (see
  `pi/README.md`): 4-bit for build/capture/triage/doc-sync, and
  `pairing` pinned to `inherit` so the reasoning step follows the
  parent session’s tier. 4-bit is never a planning or diagnosis model.
- **Data before edit.** If a probe log for the current hypothesis
  already exists on disk, the next action is *analyzing it*, not building
  another fix candidate. The maintainer gates only the final candidate;
  intermediate variants are judged by the golden sweep (regressions are
  machine-detected), not by maintainer A/B — one A/B is the gate, not
  the test harness.
- **Context resets.** After a second failed maintainer A/B, or after a
  compaction whose state is contested, do not continue the session: write
  a HANDOFF brief and start a fresh session from it. Compaction summaries
  must carry an **evidence ledger** — one line per live hypothesis tagged
  `refuted` / `unvalidated` / `validated (<artifact: log, golden frame,
  maintainer A/B, commit>)`, and `"diagnosed"` is forbidden for any fix
  lacking a maintainer A/B or a passing golden sweep. The
  `pi/extensions/compaction-ledger/` extension encodes this prompt (and,
  with `GE_COMPACT_MODEL`, summarizes on a stronger model than the
  session's labor model).
- **Regression triage first.** Before any probe or theory on a bug,
  ask when it first appeared. If a release window is known, list the
  commits in `git log <good>..<bad>` that touch the subsystem and run the
  off switch each of them shipped with, one run per switch. The #150
  wall-hug see-through (D579) spent days on cull/ucode/guard-band probes; a
  single `GE_NEARCLIP=0` run named the v0.5.0 feature (D543) and the fix
  was one line. Corollaries: when an A/B refutes a layer, leave that layer;
  when a probe shows the code behaving correctly, pivot instead of probing
  deeper; a delegated decode is a cross-check, never the lead.
- **Briefs are mandatory** (§1): no investigation kickoff without a
  BUDGET line and a filled brief template — including the MODEL line
  below, so the tier choice is visible in the brief itself.

## Investigation-brief template

```
TASK: <one bug, one subsystem>.
READ FIRST: docs/porting-notes.md; docs/dev/findings/<the specific files>.
FILES YOU MAY TOUCH: <disjoint from any other in-flight work>.
KNOWN-GOOD / RULED OUT: <list; do not re-investigate>.
PRE-FLIGHT ATTACHED: <probe output, baseline capture, PD-port pointer>.
MODEL: <tier + model id, per §8 — planning/diagnosis on the strong
  model; mechanical labor delegated to the pi/ subagent roles>.
BUDGET: <N build->run cycles / ~M min>. On expiry: revert probes, write up with confidence.
CONSTRAINTS: <files, scope>. Name the exact cause (struct/field/logic) before
  fixing; check docs/porting-notes.md §A1 for 32→64-bit layout traps. State
  any change in game behaviour in the report (before / after).
VERIFY: <the exact tools_pc/verify.sh (or probe) invocation that proves this done; or, if runtime verification is impossible in this environment, say so and name what a human must run>.
REPORT: (a) root cause + file:line evidence  (b) fix diff, or why not
        (c) probes left in tree  (d) confidence.
        Append any generalisable quirk to docs/porting-notes.md.
```
