---
title: Releases
sources:
  - CHANGELOG.md
  - .github/workflows/ci.yml
  - tools/switch/package_release.sh
  - docs/dev/RELEASE-GATE.md
verified: 7d15f029
---

# Releases

How a Project Dam release is cut and what CI publishes. History rewrites, PII and legal checks
before a release: `docs/dev/RELEASE-GATE.md`.

## The changelog

`CHANGELOG.md` keeps one `## [Unreleased] - <level>` section (`patch` / `minor` / `major`) with
`Added` / `Changed` / `Fixed` entries, written for players. The level of the section sets the next
version number.

- A PR that changes what a player sees adds an entry.
- A tweak to a feature that is not released yet changes that feature's entry; it does not add one.

## Cutting a release

1. In a **clean** worktree from `origin/main`, create `release/X.Y.Z`. (A worktree with
   uncommitted changes once had them swept into the release commit.)
2. Promote the changelog: `## [Unreleased] - <level>` becomes `## [X.Y.Z] - <date>`. The
   maintainer's `frelease` tool does this and prints the version; do not edit the heading by hand.
3. Commit as `switch: release X.Y.Z` and test that commit's NRO on the console.
4. Push an annotated tag `project-dam-vX.Y.Z` on that commit, and fast-forward `main` to it.

## What CI does with the tag

`.github/workflows/ci.yml` runs on `project-dam-v*` tags: it builds the NRO in the pinned
devkitA64 image and the Windows exe, packages them (`tools/switch/package_release.sh <version> dist`
gives `project-dam-<version>-switch.zip`, `package_release_win.sh` the `-windows-x64` zip) and creates
the GitHub release with both zips, `project-dam.nro` and the changelog section as notes. The in-game
update (#76) looks for these releases.

`ci.yml` replaced upstream's workflow of the same name (and `selfhosted.yml` is gone). GitHub keeps
a workflow's disabled state by path, and upstream's `ci.yml` was disabled here: if CI does not run
on `main`, `gh workflow enable CI --repo christphe/project-dam-nx`.
