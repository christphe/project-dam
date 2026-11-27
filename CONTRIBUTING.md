# Contributing

Thanks for your interest. This is a small hobby project; issues and pull
requests are welcome, but please read the ground rules first — they are what
keep the port faithful to the original game.

## Ground rules

1. **There is no N64 build to keep.** The goal is a modern engine that runs
   the GoldenEye campaign; the N64 `Makefile`, `rsp/`, `ld/` and the N64
   build's tools are not maintained.

2. **`src/` may be changed.** The decompiled code is being moved out and
   removed, so it is edited or deleted like any other code. Trace a bug to
   its exact cause before fixing it, and state any change in behaviour in
   the PR (what it did before, what it does now).

3. **Region macros belong to the build.** `CMakeLists.txt` `REGION_DEFS` no
   longer has to follow the N64 `Makefile`.

4. **Verify before you push.** A build-affecting change needs, at minimum, a
   clean configure + build for `ntsc-final` and a crash-free run of one level
   (`./build-pc/ge007.x86_64 -level_09`).

5. **No game data, ever.** Never commit, upload or attach a ROM or anything
   extracted from one (textures, audio, models, level data, in-game text,
   the generated `data/pcmodels-*` / `data/pccg-*` folders) in a pull
   request or an issue. A small cropped screenshot of a glitch is fine.

## Bug reports

Open an [issue](https://github.com/christphe/project-dam/issues/new)
with:

- `ge007.crash.log` if the game crashed (in the folder you launched from),
- the output of `ge007.x86_64 --version` (or the release version you
  downloaded), your OS, and your GPU and driver version,
- your ROM's SHA-1 (Windows: `certutil -hashfile <rom> SHA1`, Linux:
  `sha1sum <rom>`),
- what you did, what happened, and what you expected.

**Never attach the ROM itself.**

## Style

- C/C++ formatting follows `.clang-format` and `.editorconfig` in the repo
  root. Match the surrounding code.
- Port-layer code is plain C11 / C++17, SDL2 + OpenGL, no extra dependencies.
- Keep commits focused and describe *why*, not just *what*.

## Forks and derivative works

Fork away — the code is MIT and the `port/` layer exists to be extended.
Rebranding, localizing and extending are all welcome, and none of them need
permission. If you carry the work forward under your own name, please keep
two things:

- **Attribution.** A clear statement, in your README and about page, that the
  project derives from this one — with a link. The engine prints its origin at
  startup and in its `--version` output; keeping that intact is part of the
  ask.
- **The record.** `docs/dev/` (the finding log, porting notes, per-level
  status) is the engineering record behind every fix. Derivative projects are
  asked to preserve or link to it rather than fork-and-forget.

Stripping the attribution is the one thing that turns a fork into something
we no longer recognize as a descendant of this project.

## Where to look

- `docs/internals.md` — how the port is structured.
- `docs/porting-notes.md` — the recurring bug classes. Check here before
  debugging a crash; you are likely looking at a known pattern.
- `docs/dev/` — the full finding log and per-level status.
