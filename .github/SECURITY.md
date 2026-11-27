# Security Policy

Project Dam is a non-commercial, fan-made port. It has no server component
and no telemetry, and no networking unless you turn on the opt-in update
check (`Game.CheckUpdates`, off by default), which makes one HTTPS request to
GitHub's releases API per launch (GitHub sees that request and your IP, as
with any web request):

- **Switch:** libcurl over the console's sockets, with peer verification
  against the CA bundle built into the NRO. If you accept an update, the game
  downloads `project-dam.nro` from this repository's latest release, checks
  its SHA-256 against the digest GitHub lists for that asset, and only then
  replaces the running NRO.
- **Windows:** the system `winhttp.dll`, loaded from System32 only. It never
  downloads or installs anything.

Each release ships the Switch NRO and a Windows zip, but never a ROM or any
game asset: you supply those yourself from a copy you legally own. The
realistic security surface is:

- the game parsing your own ROM and the files derived from it at load time
  (a malformed ROM or asset could in principle crash it or worse),
- the update download on the Switch, and
- the build scripts and the CI workflow.

### Why the binaries are unsigned

The Windows `.exe` is **not code-signed**: this is a free hobby project with
no code-signing budget, not a sign of anything wrong with the binary.
**Windows SmartScreen** may show "Windows protected your PC" on first run.
Click "More info" → "Run anyway."

Every release is built by this repository's CI (`.github/workflows/ci.yml`)
from the tagged commit, so a from-source build of the same tag is the check
available.

## Reporting a vulnerability

Please **do not** open a public issue for a security problem.

Use GitHub's private vulnerability reporting:
**Security → Report a vulnerability** on this repository
(<https://github.com/christphe/project-dam/security/advisories/new>).

Please include:

- the release you downloaded, or the commit hash,
- the platform (Switch console, emulator, Windows),
- a minimal reproduction, and
- the crash log if there is one (`data/ge007-crash.log` on the Switch,
  `ge007.crash.log` on Windows).

## Scope

In scope: memory-safety bugs in the `port/` layer and the tooling, the
update download, issues in the CI workflow or build scripts, and dependency
problems we can act on.

Out of scope: bugs inherited unchanged from the
[GoldenEye 007 decompilation](https://github.com/n64decomp/007) that are not
made worse by the port, missing-asset or wrong-ROM errors, and anything
requiring a ROM or assets we do not distribute.

## Supported versions

Only the tip of the default branch and the
[latest release](https://github.com/christphe/project-dam/releases/latest)
are supported.
