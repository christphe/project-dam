#!/usr/bin/env python
"""Dev launcher: build, nxlink to the console, Switch emulator, N64 emulator, native Linux/Windows.

    python tools/dev.py build  {switch|args|link|linux|windows}
    python tools/dev.py nx     [--build link|args|switch|none] [--name N] [--level N] [--hard N] [--secs S] [ARGS...]
    python tools/dev.py switch [--build args|switch|none] [--level N] [--hard N] [ARGS...]
    python tools/dev.py n64
    python tools/dev.py win    [--build] [ARGS...]     # build-win/ge007.x86_64.exe
    python tools/dev.py linux  [--build] [ARGS...]     # build-linux/ge007.x86_64 (WSL if on Windows)
    python tools/dev.py config                         # show the resolved config

Machine-specific paths are NOT in this file: they come from <repo>/.env, or the main checkout's when run from a worktree (gitignored):
SWITCH_IP, SWITCH_EMU_EXE, SWITCH_EMU_SD (default %APPDATA%/Ryujinx/sdcard),
N64_EMU_EXE, N64_ROM (the .z64 to open), DOCKER_IMAGE (optional).
"""
import argparse, json, os, shutil, subprocess, sys, time
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
HERE = Path(__file__).resolve().parent
KEYS = {"console_ip": "SWITCH_IP", "ryujinx_exe": "SWITCH_EMU_EXE", "ryujinx_sd": "SWITCH_EMU_SD",
        "ares_exe": "N64_EMU_EXE", "ares_rom": "N64_ROM", "image": "DOCKER_IMAGE"}
IMAGE = cfg_image = "local/project-dam-build:latest"
SD_DIR = "switch/project-dam"          # game folder on the SD card
NX_DIR = "project-dam"                 # the same folder for nxlink: the netloader writes under sdmc:/switch/
NRO = "ge007.aarch64.nro"

# build kind -> (build dir, cmake configure extras, output)
BUILDS = {
    "switch": ("build-switch", []),
    "args":   ("build-switch-args", ["-DNX_ARGS=ON"]),
    # NX_ARGS too: the NAME=value tokens (--secs, env) are only read with it
    "link":   ("build-switch-link", ["-DNX_LINKSTDIO=ON", "-DNX_ARGS=ON"]),
}
SWITCH_TC = "-DCMAKE_TOOLCHAIN_FILE=/opt/devkitpro/cmake/Switch.cmake"


# solo levels, same name:number pairs as port/src/main.c (kSoloLevels)
LEVELS = {"dam": 33, "facility": 34, "runway": 35, "surface1": 36, "bunker1": 9, "silo": 20, "frigate": 26,
          "surface2": 43, "bunker2": 27, "statue": 22, "archives": 24, "streets": 29, "depot": 30, "train": 25,
          "jungle": 37, "control": 23, "caverns": 39, "cradle": 41, "aztec": 28, "egypt": 32, "cuba": 54}
LEVELS_HELP = ", ".join(f"{n.capitalize()}={v}" for n, v in LEVELS.items())


def level_arg(text):
    """--level takes a number or a level name (case-insensitive)."""
    t = text.strip().lower()
    if t in LEVELS:
        return LEVELS[t]
    if t.isdigit():
        return int(t)
    raise argparse.ArgumentTypeError(f"unknown level '{text}' (names: {', '.join(LEVELS)})")


def cfg():
    """Config from <repo>/.env, or the main checkout's when run from a worktree (gitignored) overridden by the real environment."""
    env = {}
    # a worktree has no .env of its own: fall back to the main checkout's (the git common dir's parent)
    common = subprocess.run(["git", "rev-parse", "--path-format=absolute", "--git-common-dir"], cwd=REPO,
                            capture_output=True, text=True).stdout.strip()
    f = next((p for p in (REPO / ".env", Path(common).parent / ".env" if common else REPO / ".env") if p.exists()),
             REPO / ".env")
    if f.exists():
        for line in f.read_text(encoding="utf-8").splitlines():
            line = line.strip()
            if line and not line.startswith("#") and "=" in line:
                k, v = line.split("=", 1)
                env[k.strip()] = v.strip().strip("'\"")
    env.update({k: v for k, v in os.environ.items() if k in KEYS})
    c = {name: env[var] for name, var in KEYS.items() if env.get(var)}
    if "ryujinx_sd" not in c and os.environ.get("APPDATA"):
        c["ryujinx_sd"] = str(Path(os.environ["APPDATA"]) / "Ryujinx" / "sdcard")
    return c


def need(c, key):
    if not c.get(key):
        sys.exit(f"missing '{key}': set the matching variable in <repo>/.env (SWITCH_IP, SWITCH_EMU_EXE, SWITCH_EMU_SD, N64_EMU_EXE, N64_ROM)")
    return c[key]


def run(cmd, **kw):
    print("+", " ".join(map(str, cmd)), flush=True)
    r = subprocess.run(cmd, **kw)
    if r.returncode:
        sys.exit(f"command failed ({r.returncode})")


def docker(shell_cmd):
    env = dict(os.environ, MSYS_NO_PATHCONV="1")
    run(["docker", "run", "--rm", "-v", f"{REPO}:/src", "-v", "project-dam-ccache:/ccache",
         "-w", "/src", cfg().get("image", IMAGE), "sh", "-c", shell_cmd], env=env)


def build(kind):
    if kind in BUILDS:
        d, extra = BUILDS[kind]
        conf = ""
        # configure once: the source GLOBs are CONFIGURE_DEPENDS, so ninja itself re-runs cmake when a file
        # is added or removed. Re-running cmake on every build rewrote generated files and cost ~4 s.
        if not (REPO / d / "build.ninja").exists():
            conf = (f"cmake -S . -B {d} -G Ninja -DROMID=ntsc-final {SWITCH_TC} "
                    + " ".join(extra) + " && ")
        docker(conf + f"cmake --build {d}")
        return REPO / d / NRO
    if kind == "windows":
        run(["docker", "compose", "run", "--rm", "windows"], cwd=REPO)
        return REPO / "build-win" / "ge007.x86_64.exe"
    if kind == "linux":
        run(["docker", "compose", "run", "--rm", "linux"], cwd=REPO)
        return REPO / "build-linux" / "ge007.x86_64"
    sys.exit(f"unknown build '{kind}'")


def game_args(a):
    out = []
    if a.level is not None:
        out.append(f"-level_{a.level:02d}")
    if a.hard is not None:
        out.append(f"-hard{a.hard}")
    if getattr(a, "secs", None):
        out.append(f"NX_TEST_SECS={a.secs}")
    return out + list(a.args)


def cmd_build(a):
    p = build(a.kind)
    print("->", p)


def cmd_nx(a):
    c = cfg()
    ip = need(c, "console_ip")
    kind = a.build
    nro = REPO / BUILDS[kind][0] / NRO if kind != "none" else REPO / BUILDS["link"][0] / NRO
    if kind != "none":
        build(kind)
    if a.name:                                             # identifiable copy, like <feature>-test.nro
        named = nro.with_name(f"{a.name}-test.nro")
        shutil.copyfile(nro, named)
        nro = named
    log = Path(a.log) if a.log else REPO / BUILDS.get(kind, BUILDS["link"])[0] / "nx.log"
    log.write_text("", encoding="utf-8")
    dest = "project-dam.nro" if a.installed else "project-dam-test.nro"   # --installed: the in-game update's target
    cmd = [sys.executable, str(HERE / "switch" / "nxlink.py"), ip, str(nro), f"{NX_DIR}/{dest}",
           str(log)] + game_args(a)
    print(f"log: {log}")
    env = dict(os.environ, NXLINK_RETRIES=os.environ.get("NXLINK_RETRIES", "20"))
    # the client waits for the game's stdio after the upload; a TimeoutError then is normal for non-link builds
    subprocess.run(cmd, env=env)


def cmd_switch(a):
    c = cfg()
    exe = need(c, "ryujinx_exe")
    sd = Path(need(c, "ryujinx_sd")) / SD_DIR
    if a.build != "none":
        nro = build(a.build)
    else:
        nro = REPO / BUILDS["args"][0] / NRO
    sd.mkdir(parents=True, exist_ok=True)
    (sd / "data").mkdir(exist_ok=True)
    shutil.copyfile(nro, sd / "project-dam.nro")
    args = game_args(a)
    if args:
        args_file = sd / "data" / "ge007.args"
        if args_file.exists() and not (args_file.with_suffix(".args.bak")).exists():
            shutil.copyfile(args_file, args_file.with_suffix(".args.bak"))
        args_file.write_text(" ".join(args) + "\n", encoding="utf-8")
        print("args:", " ".join(args))
    print("log:", sd / "data" / "ge007.log")
    subprocess.Popen([exe, str(sd / "project-dam.nro")], cwd=str(Path(exe).parent))


def cmd_n64(a):
    c = cfg()
    exe, rom = need(c, "ares_exe"), need(c, "ares_rom")
    subprocess.Popen([exe, rom], cwd=str(Path(exe).parent))   # ROM path only: --system opens an Error window


def ensure_native_data(exe):
    """The PC build reads data/ next to the exe (ROM, ge007.ini, log). A fresh worktree has none:
    create it and copy the ROM from N64_ROM under the name the game looks for."""
    data = Path(exe).parent / "data"
    data.mkdir(exist_ok=True)
    names = ("ge007.ntsc-final.z64", "baserom.u.z64")
    if not any((data / n).exists() or (Path(exe).parent / n).exists() for n in names):
        c = cfg()
        src = Path(c["ares_rom"]) if c.get("ares_rom") else None
        if src is None and c.get("ryujinx_sd"):             # same ROM the Switch emulator already has
            src = Path(c["ryujinx_sd"]) / SD_DIR / "data" / names[0]
        if src is None or not src.exists():
            sys.exit(f"no ROM for the native build: set N64_ROM in .env (or put {names[0]} in {data})")
        print(f"copying the ROM into {data / names[0]}")
        shutil.copyfile(src, data / names[0])
    return data


def cmd_native(a, kind):
    if a.build:
        exe = build(kind)
    else:
        exe = REPO / ("build-win/ge007.x86_64.exe" if kind == "windows" else "build-linux/ge007.x86_64")
    if not exe.exists():
        sys.exit(f"{exe} does not exist: run with --build")
    data = ensure_native_data(exe)
    # Only the Switch NX_ARGS build turns NAME=value tokens into environment variables; the PC game would take
    # them as plain arguments and ignore them (GE_QUITFRAME=300 never quit). Set them in its environment here.
    args, env = [], dict(os.environ)
    for t in game_args(a):
        k, eq, v = t.partition("=")
        if eq and k and not t.startswith("-"):
            env[k] = v
        else:
            args.append(t)
    if kind == "linux" and os.name == "nt":
        wsl = subprocess.run(["wsl", "wslpath", "-a", exe.as_posix()], capture_output=True, text=True).stdout.strip()
        if not wsl:                                         # backslashes would be eaten by the WSL shell
            sys.exit("WSL could not translate " + str(exe))
        names = [k for k in env if k not in os.environ or env[k] != os.environ[k]]
        if names:                                           # WSLENV carries Windows variables into the Linux side
            env["WSLENV"] = ":".join(filter(None, [os.environ.get("WSLENV", "")] + names))
        p = subprocess.Popen(["wsl", wsl] + args, cwd=str(exe.parent), env=env)
    else:
        p = subprocess.Popen([str(exe)] + args, cwd=str(exe.parent), env=env)
    time.sleep(3)
    if p.poll() is not None:                               # a game that dies at once would otherwise be silent
        sys.exit(f"the game exited right away (code {p.returncode}); see {data / 'ge007.log'}")
    print("running; log:", data / "ge007.log")


def cmd_config(a):
    for k, v in cfg().items():
        print(f"{k} = {v}")


TARGETS_HELP = ("Targets:\n"
    "  switch   console build (build-switch), what ships\n"
    "  args     Switch build reading data/ge007.args (build-switch-args), for the emulator\n"
    "  link     Switch build with nxlink stdio + crash dump (build-switch-link), for the console\n"
    "  linux    native Linux build (build-linux)\n"
    "  windows  native Windows build (build-win)")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    def game_opts(p):
        p.add_argument("--level", type=level_arg, metavar="LEVEL", help="boot into a solo level: a number or a name. " + LEVELS_HELP)
        p.add_argument("--hard", type=int, help="-hardN (0..3)")
        p.add_argument("args", nargs="*", help="extra game args / NAME=value env tokens")

    p = sub.add_parser("build", help="build one target in Docker", formatter_class=argparse.RawDescriptionHelpFormatter,
                       description=TARGETS_HELP)
    p.add_argument("kind", choices=["switch", "args", "link", "linux", "windows"], metavar="{switch,args,link,linux,windows}")
    p.set_defaults(fn=cmd_build)

    p = sub.add_parser("nx", help="build + send to the console over nxlink")
    p.add_argument("--build", choices=["link", "args", "switch", "none"], default="link",
                   help="which build to send (default link: log + crash dump come back); none = send the last one without rebuilding")
    p.add_argument("--name", help="copy the NRO to <name>-test.nro before sending")
    p.add_argument("--installed", action="store_true",
                   help="send over the installed project-dam.nro instead of project-dam-test.nro (tests the in-game update)")
    p.add_argument("--secs", type=int, help="NX_TEST_SECS=N")
    p.add_argument("--log", help="stdio log file (default: <build dir>/nx.log)")
    game_opts(p); p.set_defaults(fn=cmd_nx)

    p = sub.add_parser("switch", help="build + run in the Switch emulator")
    p.add_argument("--build", choices=["args", "switch", "none"], default="args",
                   help="which build to run (default args: reads the level/args this command writes); none = no rebuild")
    game_opts(p); p.set_defaults(fn=cmd_switch)

    p = sub.add_parser("n64", help="launch the N64 emulator (ares) on the ROM"); p.set_defaults(fn=cmd_n64)

    for name, kind in (("win", "windows"), ("linux", "linux")):
        p = sub.add_parser(name, help=f"run the native {kind} build")
        p.add_argument("--build", action="store_true", help="rebuild first (Docker)")
        game_opts(p); p.set_defaults(fn=lambda a, k=kind: cmd_native(a, k))

    p = sub.add_parser("config"); p.set_defaults(fn=cmd_config)
    a = ap.parse_args()
    a.fn(a)


if __name__ == "__main__":
    main()
