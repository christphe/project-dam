# Campaign behaviour specs (#165)

One file per solo level (`<level>.md`) plus `global.md` for the generic guard. A spec says what a level does
for the player, precisely enough that someone who has never seen the original scripts can rebuild it on the
mission API (`port/mission/lib/mission.d.luau`). The Luau under `campaign/` is written from these files only.

## Clean-room rules

The spec writer has read the original data. The Luau writer has not, and must not need to. So a spec carries
**behaviour and facts, never the form of the original scripts**:

- **No AI command names, no label numbers, no script order.** Write "when the player enters the room of pad
  0x2A, the two guards of the control room run to the alarm", not the commands that do it. One original
  script can become several rules, and several scripts one rule. `tools_pc/campaign_spec_lint.py` checks this.
- **No game text.** Every text is its 4-hex-digit id (the key of `text_<lang>.json`, `docs/specs/text.md`),
  with a gist of six words at most in your own words that says what the text is FOR, never what it says:
  `2C04 (alarm warning)`, `2C05 (objective: disable the dish)`. A dialogue line is its speaker and at most
  its function: `8C16 (Bond line)`, `9016 (Natalya line: asks for time)`, never a paraphrase of the line.
  Never quote the string. The lint flags "(Name: ..." gists and five words in a row shared with a game text.
- **Intent, not mechanism.** Keep what changes what the player sees or can do: triggers, conditions,
  difficulty differences, timings that set the pace, who appears where and when, cinematics shot by shot.
  Leave out polling loops, sleep slicing, flags passed between scripts, retries, debug output and dead code.
  A timer that sets the pace ("the reinforcements arrive 30 s after the alarm") stays; a 1-tick wait does not.
- **Facts are fine.** Native ids are data the game files give and the API takes: chr numbers, pad numbers
  (bound pads with their +10000 when that is how the setup refers to them), object tags, path ids, camera /
  animation ids, item ids, objective letters, text ids. Times in seconds (the scripts count 1/60 s).
  Probabilities as percentages. Rooms as "the room of pad N".

## The template

```markdown
# <Level name>

Setup file `<file>`, text bank <n> (ids 0xNN00..). <One sentence on the mission.>

## Characters
| chr | Who | Difficulties | Starts as | Notes |
|---|---|---|---|---|
| 0x05 | guard at the dam gate | all | patrols path 2 | generic guard (global.md) after the patrol |
(Only gameplay-relevant stats and items: health, accuracy, armour, a held item, "drops key 0x..".)

## Objectives
| Letter | Text | Difficulties | Completed when | Failed when |
|---|---|---|---|---|

## Rules
Numbered, each one self-contained: **when** <trigger>, **if** <condition>, **then** <effect>; difficulty
differences inline. Group them by area or by story beat. Cover every behaviour of the level here.

## Objects and world
Doors that lock or open on an event, destructible objects that matter, TV screens, vehicles and aircraft
(their path and behaviour), fog changes, music and sound cues, the alarm.

## Cinematics
Intro and outro, shot by shot: the camera (id or position, how it moves, how long), who is shown and what
they do (animation id, walk to pad N), fades, text ids, when control returns to the player. Also any
in-level scripted scene.

## Co-op notes
Anything that refers to "the player" in a way that needs a choice with two players (the nearest, any, all).

## API needed
Every verb, query or event the rules use, in plain words with arguments, marked **existing** (named as in
mission.d.luau) or **new**: `new: guard:attack(target)` the guard fights the target with the generic combat
behaviour until it dies or loses it.
```

## Coverage files

The spec writer also fills `docs/campaign/coverage/<level>.md`: every AI list id of the level, and the rule
numbers that cover it, or `data` (it only sets up a character: the Characters table holds it) or `dead`
(never reached, or debug only), with a reason. This is how the lead checks that nothing was dropped. The
coverage files are **not** given to the Luau writers.
