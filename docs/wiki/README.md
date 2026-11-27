# The wiki: how this fork works today

A small wiki of pages that each explain one subject as it stands on `main`: what it is,
where the code lives, how to change it. It is written and kept up to date mostly by LLM
agents, from the code and the merged PRs, and read by people and agents alike.

Start at [index.md](index.md). What changed and when: [log.md](log.md).

## Where things go

| Kind of knowledge | Home |
|---|---|
| How a subsystem works now, how to change it | **this wiki** |
| A trap: a root cause someone would otherwise rediscover | `docs/dev/findings/` (one file each) |
| A recurring N64 -> PC bug class | `docs/porting-notes.md` |
| Open work | the GitHub issues |
| The history of a feature, its measurements | the PR |
| The original port's architecture (§1–§10) | `docs/internals.md` |

A wiki page links to findings and PRs; it does not copy them. When a page and the code
disagree, the code wins and the page gets fixed.

## Page format

```markdown
---
title: Options menu
sources:                 # the files the page describes; the lint checks them
  - port/src/menu/menu.json
  - port/src/optionsoverlay.c
verified: 7d15f029       # the main commit the page was last checked against
---

# Options menu

One paragraph: what it is and when you need this page.
...
```

- One subject per page, file name in kebab case. A page grows sections, not siblings; split it
  when two halves are read by different people.
- Present tense, describing `main`. No "we will", no history beyond one line pointing at the PR.
- Link source files and other pages with relative links. Every claim about behaviour should
  be traceable to a file in `sources`.
- Tree hygiene applies (AGENTS.md): no machine paths, IP addresses or usernames; use the
  placeholders (`<repo>`, `<games>`…).
- English, short sentences, tables for reference material.

## Operations

**Ingest** (after a PR merges, or when a page is asked for): read the PR and the code it
touches, update every page whose `sources` it changed (or add a page), set `verified` to the
new `main` commit, add the page to `index.md` if new, and append one line to `log.md`.

**Query:** read `index.md`, then only the pages it points to. If the answer is not in the wiki
and took real digging, that is a page (or a section) to write.

**Lint:** `python tools_pc/wiki_lint.py` lists pages whose sources are gone, and pages whose
sources changed since their `verified` commit. A listed page is re-read against the code and
either fixed or re-verified.
