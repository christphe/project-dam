#!/usr/bin/env python
"""fxview: GoldenEye's combat effects up close, ORIGINAL vs EFFECT shader, drawn through ge_render (issue #144).

  python tools_pc/fxview/fxview.py win [viewer options]        build (Docker, GE_TOOLS=ON) and open the viewer on PC (Vulkan; --backend gl|vk)
  python tools_pc/fxview/fxview.py ryujinx [viewer options]    build the NRO, put it on Ryujinx's SD card and start Ryujinx (the options go to fxview.args)
  python tools_pc/fxview/fxview.py nx [viewer options]         build the NRO and run it on the console over nxlink (the options are its argv)
  python tools_pc/fxview/fxview.py shots --out DIR [--backdrop grey|dark|checker|wall] [--bgtex N] [--fc N] [--backend vk|gl]
        one side-by-side PNG per effect (puffs: several animation frames) plus DIR/index.html
  python tools_pc/fxview/fxview.py decalshots --out DIR [--holes N] [--yaw DEG] [--pitch DEG] [--backend vk|gl]
        the port's bullet-hole clusters on a real wall texture per material, game vs port, plus DIR/index.html
  python tools_pc/fxview/fxview.py tabshots --out DIR [--backend vk|gl]
        the Muzzle, Tracers, Blood/smoke/debris and Explosions tabs (game vs port) at fixed times, plus the decal texture sheets, plus DIR/index.html
  python tools_pc/fxview/fxview.py selftest                    texture codecs, puff frame stepping, effect files; no ROM, no window

Viewer options (the same on PC and Switch, see ge/fxview.cpp): --mode sprite|decal|cluster|muzzle|tracer|burst|explosion, --kind tracer|laser|watchlaser|wallpuff|
bloodpuff|scatter|impact, --impact 0..19, --age TICKS, --angle RAD, --view side|split|orig|fx, --split F, --backdrop grey|dark|checker|wall,
--bgtex N, --zoom F, --fc N, --nearest 0|1, --tracerlen F, --seed N; decal: --mat stone|tile|default|wood|metal|metalobj|dirt|mud|snow|glass,
--holes N, --yaw DEG, --pitch DEG, --sun/--pt lights; cluster: --dist N, --diag coverage|veye|uv|pom|pomalpha|deriv; --probe X,Y (pixel readback
to the log), --shot FILE.png, --w N --h N, --rom FILE.z64; tabs: --t SECONDS --tshow game|port|side --gun NAME --tritem gun|laser|watchlaser --burst
metal|wood|glass|water|snow|dirt|stone|blood --explo 0..20 (and --mrate --mscale --mvis --trrange --trperiod --bperiod --etail --tyaw --tpitch --tzoom --twall --twalltex --tseed);
decal --view sheet [--page N]. Settings are saved to <out>/fxview.ini (loaded unless --shot/--probe/--dump; arguments win).

The ROM: --rom, else FXVIEW_ROM / MODELVIEW_ROM / N64_ROM (environment or <repo>/.env, the main checkout's in a worktree), else the game's
data/ folder (build-win/data, the Ryujinx SD's switch/project-dam/data). Nothing ROM-derived is written into the repo: shots and
decalshots refuse an output dir inside it (except tools_pc/fxview/data/, git-ignored).
The effect shaders are port/shaders/fx/*.glsl: the packs (deko3d, Vulkan) are built from them, so a change rebuilds the pack on the next
`win` / `ryujinx` / `nx` (cmake tracks the files); see README.md for the hot reload.
"""
import argparse, html, os, shutil, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
DATA = os.path.join(HERE, "data")
sys.path.insert(0, os.path.join(REPO, "tools"))
import devenv   # tools/devenv.py: .env (the main checkout's from a worktree) and the ROM lookup

# (file stem, viewer args, caption) for `shots`
SHOTS = [
    ("tracer", ["--kind", "tracer"], "gun tracer"),
    ("laser", ["--kind", "laser"], "laser tracer"),
    ("watchlaser", ["--kind", "watchlaser"], "watch laser"),
] + [(f"wallpuff_age{a:02d}", ["--kind", "wallpuff", "--age", str(a), "--zoom", "2.5"], f"wall puff, age {a}") for a in (0, 2, 4, 6, 8, 10)] \
  + [(f"bloodpuff_age{a:02d}", ["--kind", "bloodpuff", "--age", str(a), "--zoom", "2.5"], f"blood puff, age {a}") for a in (0, 2, 4, 6, 8, 10)] \
  + [(f"scatter_age{a:02d}", ["--kind", "scatter", "--age", str(a), "--zoom", "2.5"], f"scatter puff, age {a}") for a in (0, 4, 8)] \
  + [(f"impact_{t:02d}", ["--kind", "impact", "--impact", str(t)], f"bullet-hole decal type {t}") for t in (0, 1, 2, 3, 7, 8, 9, 13, 16)]

# `decalshots`: the port's bullet holes per material, (file stem, viewer args, caption)
DECAL_MATS = [("stone", "stone"), ("tile", "tile"), ("default", "default (untagged)"), ("wood", "wood"), ("metal", "metal"),
              ("dirt", "dirt"), ("snow", "snow"), ("glass", "glass")]
SUN_UL = ["--sun", "1", "--sunaz", "135", "--sunel", "35"]           # the sun from the upper left
FLASH = ["--pt", "1", "--ptx", "40", "--pty", "-20", "--pth", "90"]   # a muzzle flash in front of the wall, beside the holes
DECAL_SHOTS = [(f"decal_{m}_sun_flash", ["--mat", m] + SUN_UL + FLASH, f"{cap}: sun from the upper left + muzzle flash") for m, cap in DECAL_MATS] \
    + [(f"decal_{m}_flash", ["--mat", m, "--sun", "0"] + FLASH, f"{cap}: muzzle flash only (no sun: the in-game case today)") for m, cap in DECAL_MATS] \
    + [("decal_stone_sun_lowright", ["--mat", "stone", "--sun", "1", "--sunaz", "-30", "--sunel", "20", "--pt", "0"], "stone: low sun from the lower right, no flash (relief check)"),
       ("decal_stone_sun_upleft", ["--mat", "stone"] + SUN_UL + ["--pt", "0"], "stone: sun from the upper left, no flash (relief check)"),
       ("decal_stone_grazing", ["--mat", "stone", "--yaw", "68", "--pitch", "8"] + SUN_UL + FLASH, "stone: grazing view"),
       ("decal_wood_type7_overlap", ["--mat", "wood", "--dtype", "7", "--holes", "6"] + SUN_UL + FLASH, "wood, impact type 7, six overlapping holes")]

# `tabshots`: the Muzzle, Tracers, Blood/smoke/debris and Explosions tabs (game vs port), at a fixed time (--t SECONDS freezes the tab's clock)
TAB_SHOTS = [("muzzle", ["--mode", "muzzle", "--t", "0.02"], "muzzle flash (Pchrautoshot GUNFIRE), 0.02 s"),
             ("tracer_gun", ["--mode", "tracer", "--t", "0.1"], "gun tracer, 0.1 s"),
             ("tracer_laser", ["--mode", "tracer", "--tritem", "laser", "--t", "0.1"], "laser tracer, 0.1 s")]     + [(f"burst_{m}_t{int(t * 100):03d}", ["--mode", "burst", "--burst", m, "--t", str(t)], f"burst {m}, {t} s")
       for m, t in (("stone", 0.05), ("stone", 0.15), ("metal", 0.1), ("wood", 0.1), ("glass", 0.1), ("water", 0.1), ("snow", 0.1), ("dirt", 0.1), ("blood", 0.1), ("blood", 0.3))]     + [(f"explosion_{n}_t{int(t * 100):03d}", ["--mode", "explosion", "--explo", str(n), "--t", str(t)], f"explosion type {n}, {t} s")
       for n, t in ((13, 0.3), (13, 1.2), (13, 2.4), (4, 1.0), (10, 1.0), (19, 1.0))]
SHEET_SHOTS = [("sheet_" + m, ["--mode", "decal", "--view", "sheet", "--mat", m], f"texture sheet: {m}") for m in ("stone", "wood", "metal", "glass")]


def load_env():
    return devenv.load_env()


def find_rom(env):
    return devenv.find_rom(env, ("FXVIEW_ROM", "MODELVIEW_ROM"))


def inside(path, root):
    path, root = os.path.abspath(path), os.path.abspath(root)
    try:
        return os.path.commonpath([path, root]) == root
    except ValueError:   # another drive
        return False


# ---- fxview on ge_render (issue #144): the CMake target `fxview` (GE_TOOLS=ON), built in Docker, run on PC, in Ryujinx or on the console
IMAGE = "ghcr.io/christphe/project-dam-build@sha256:7ba869fa0ea8b7195b7dcab3b23f87ae2e3065fac1001bd89ca817771dcc7baf"   # the published build image (tools/check_build_image.py keeps it in sync)
IMAGE = os.environ.get("DOCKER_IMAGE") or devenv.load_env().get("DOCKER_IMAGE") or IMAGE   # as tools/dev.py: DOCKER_IMAGE (environment or .env) wins
GE_BUILDS = {   # kind -> (build dir, cmake configure extras, output)
    "win": ("build-win", None, "build-win/tools_pc/fxview/fxview.exe"),
    "linux": ("build-linux", [], "build-linux/tools_pc/fxview/fxview"),
    "switch": ("build-switch", ["-DCMAKE_TOOLCHAIN_FILE=/opt/devkitpro/cmake/Switch.cmake"], "build-switch/tools_pc/fxview/fxview.nro"),
}


def ge_build(kind, lowcost=False):
    """build fxview for `kind` in Docker with GE_TOOLS=ON; returns the output path. A changed port/shaders file rebuilds the packs (cmake dependency)."""
    d, extra, out = GE_BUILDS[kind]
    envv = dict(os.environ, MSYS_NO_PATHCONV="1")
    if kind == "win":   # the compose service (the offline MinGW image) and its script, with the tools on
        cmd = ["docker", "compose", "run", "--rm", "-e", "CMAKE_EXTRA=-DGE_TOOLS=ON", "windows", "sh", "-c",
               "SDL2DIR=/usr/x86_64-w64-mingw32/sys-root/mingw cmake -S . -B build-win -G Ninja -DROMID=ntsc-final -DCMAKE_BUILD_TYPE=RelWithDebInfo "
               "-DCMAKE_TOOLCHAIN_FILE=/usr/share/mingw/toolchain-mingw64.cmake -DGE_TOOLS=ON -DGE_FX_LOWCOST=" + ("ON" if lowcost else "OFF") + " && cmake --build build-win --target fxview"]
        r = subprocess.run(cmd, cwd=REPO, env=envv)
    else:
        sh = (f"cmake -S . -B {d} -G Ninja -DROMID=ntsc-final {' '.join(extra)} -DGE_TOOLS=ON && cmake --build {d} --target {'fxview_nro' if kind == 'switch' else 'fxview'}")
        r = subprocess.run(["docker", "run", "--rm", "-v", f"{REPO}:/src", "-v", "project-dam-ccache:/ccache", "-w", "/src", IMAGE, "sh", "-c", sh],
                           env=envv)
    if r.returncode:
        sys.exit("fxview: build failed")
    return os.path.join(REPO, out)


def win_exe(a):
    """the Windows fxview.exe next to the DLLs (build-win), built in Docker unless --nobuild"""
    exe = os.path.join(REPO, "build-win", "fxview.exe")
    if not getattr(a, "nobuild", False) or not os.path.exists(exe):
        shutil.copy(ge_build("win", "--lowcost" in a.extra), exe)   # --lowcost: the Switch's cheaper effects (FX_LOWCOST) in the Vulkan pack
    return exe


def cmd_win(env, a):
    exe = win_exe(a)
    os.makedirs(DATA, exist_ok=True)
    extra = [x for x in a.extra if x != "--lowcost"]
    if "--rom" not in extra:
        rom = find_rom(env)
        if rom:
            extra += ["--rom", rom]
    if "--shaders" not in extra:
        extra += ["--shaders", os.path.join(REPO, "port", "shaders", "fx")]
    subprocess.Popen([exe, "--out", DATA + os.sep] + extra, cwd=os.path.join(REPO, "build-win"))


def cmd_selftest(env, a):
    exe = win_exe(a)
    sys.exit(subprocess.run([exe, "--selftest", "--shaders", os.path.join(REPO, "port", "shaders", "fx")], cwd=os.path.join(REPO, "build-win")).returncode)


def _batch(env, a, name, shots, common_extra, caption, width):
    out = os.path.abspath(a.out)
    if inside(out, REPO) and not inside(out, DATA):
        sys.exit("fxview: --out must be outside the repo or inside tools_pc/fxview/data/ (the images are ROM-derived)")
    os.makedirs(out, exist_ok=True)
    exe = win_exe(a)
    rom = a.rom or find_rom(env)
    if not rom:
        sys.exit("fxview: no ROM: pass --rom, or set N64_ROM in .env")
    common = ["--rom", os.path.abspath(rom), "--shaders", os.path.join(REPO, "port", "shaders", "fx"), "--out", out + os.sep, "--backend", a.backend, "--ui", "0", "--w", str(a.w), "--h", str(a.h)] + common_extra
    ok = {}
    for stem, args, cap in shots:
        png = os.path.join(out, stem + ".png")
        if os.path.exists(png):
            os.remove(png)
        r = subprocess.run([exe] + common + args + ["--shot", png], cwd=os.path.join(REPO, "build-win"), capture_output=True, text=True)
        ok[stem] = r.returncode == 0 and os.path.exists(png)
        print(("ok   " if ok[stem] else "FAIL ") + stem + ("" if ok[stem] else "\n" + (r.stdout + r.stderr).strip()[-600:]))
    cells = "".join(f'<figure><figcaption>{html.escape(cap)}</figcaption>'
                    + (f'<a href="{stem}.png"><img src="{stem}.png" width="{width}"></a>' if ok[stem] else "failed") + "</figure>"
                    for stem, _, cap in shots)
    with open(os.path.join(out, "index.html"), "w", encoding="utf-8") as f:
        f.write(f"<!doctype html><meta charset=utf-8><title>fxview {name}</title>"
                "<style>body{background:#222;color:#ddd;font:14px sans-serif}figure{display:inline-block;margin:6px}img{display:block}</style>"
                f"<h3>{caption}</h3>" + cells)
    print("wrote", os.path.join(out, "index.html"))
    if not all(ok.values()):
        sys.exit(1)


def cmd_shots(env, a):
    _batch(env, a, "shots", SHOTS, ["--view", "side", "--backdrop", a.backdrop, "--fc", str(a.fc)] + (["--bgtex", str(a.bgtex)] if a.bgtex is not None else []),
           f"left: original (texture x vertex colour, alpha over) | right: the pack's effect shader, frame_count {a.fc}, backdrop {html.escape(a.backdrop)}, backend {a.backend}", 640)


def cmd_tabshots(env, a):
    _batch(env, a, "tabshots", TAB_SHOTS + SHEET_SHOTS, ["--tshow", "side"],
           f"left: the game's own look (F9 off) | right: the port (effect shaders, the Lights block); backend {a.backend}. The Lights block exists on Vulkan and deko3d only: "
           "on GL the lit wall and the explosion's light are missing", 900)


def cmd_decalshots(env, a):
    _batch(env, a, "decalshots", DECAL_SHOTS, ["--mode", "decal", "--view", "side", "--holes", str(a.holes), "--yaw", str(a.yaw), "--pitch", str(a.pitch)],
           "left: the game's hole (texture x vertex colour, alpha over) | right: the port's cluster (fxClusterPom) on a lit real wall texture. "
           f"Camera yaw {a.yaw}, pitch {a.pitch}; {a.holes} holes; backend {a.backend}", 900)


def _switch_args_file(sd, extra):
    os.makedirs(sd, exist_ok=True)
    with open(os.path.join(sd, "fxview.args"), "w", encoding="utf-8") as f:
        f.write(" ".join(extra) + chr(10))


def cmd_ryujinx(env, a):
    c = devenv.load_env()
    exe = c.get("SWITCH_EMU_EXE")
    sdroot = c.get("SWITCH_EMU_SD") or os.path.join(os.environ.get("APPDATA", ""), "Ryujinx", "sdcard")
    if not exe:
        sys.exit("fxview: set SWITCH_EMU_EXE in .env")
    nro = ge_build("switch")
    sd = os.path.join(sdroot, "switch", "project-dam")
    os.makedirs(os.path.join(sd, "fxview"), exist_ok=True)
    shutil.copy(nro, os.path.join(sd, "fxview.nro"))
    _switch_args_file(os.path.join(sd, "fxview"), a.extra)
    print("args file:", os.path.join(sd, "fxview", "fxview.args")); print("log:", os.path.join(sd, "fxview", "fxview.log"))
    subprocess.Popen([exe, os.path.join(sd, "fxview.nro")], cwd=os.path.dirname(exe))


def cmd_nx(env, a):
    c = devenv.load_env()
    ip = c.get("SWITCH_IP")
    if not ip:
        sys.exit("fxview: set SWITCH_IP in .env")
    nro = ge_build("switch")
    log = os.path.join(REPO, "build-switch", "fxview-nx.log")
    cmd = [sys.executable, os.path.join(REPO, "tools", "switch", "nxlink.py"), ip, nro, "project-dam/fxview.nro", log] + a.extra   # argv reaches fxview over nxlink
    print("log:", log)
    subprocess.run(cmd, env=dict(os.environ, NXLINK_RETRIES=os.environ.get("NXLINK_RETRIES", "20")))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    for nm, fn in (("win", cmd_win), ("ryujinx", cmd_ryujinx), ("nx", cmd_nx)):   # remaining args go to the viewer
        p = sub.add_parser(nm, allow_abbrev=False); p.set_defaults(f=fn)
        if nm == "win":
            p.add_argument("--nobuild", action="store_true", help="run the existing build-win/fxview.exe")
    p = sub.add_parser("selftest"); p.add_argument("--nobuild", action="store_true"); p.set_defaults(f=cmd_selftest)
    p = sub.add_parser("shots", allow_abbrev=False)
    p.add_argument("--out", required=True); p.add_argument("--rom"); p.add_argument("--nobuild", action="store_true"); p.add_argument("--backend", default="vk", choices=["vk", "gl"])
    p.add_argument("--backdrop", default="grey", choices=["grey", "dark", "checker", "wall"]); p.add_argument("--bgtex", type=int)
    p.add_argument("--fc", type=int, default=120, help="frame_count given to the effect shaders")
    p.add_argument("--w", type=int, default=1200); p.add_argument("--h", type=int, default=600); p.set_defaults(f=cmd_shots)
    p = sub.add_parser("tabshots", allow_abbrev=False)
    p.add_argument("--out", required=True); p.add_argument("--rom"); p.add_argument("--nobuild", action="store_true"); p.add_argument("--backend", default="vk", choices=["vk", "gl"])
    p.add_argument("--w", type=int, default=1200); p.add_argument("--h", type=int, default=600); p.set_defaults(f=cmd_tabshots)
    p = sub.add_parser("decalshots", allow_abbrev=False)
    p.add_argument("--out", required=True); p.add_argument("--rom"); p.add_argument("--nobuild", action="store_true"); p.add_argument("--backend", default="vk", choices=["vk", "gl"])
    p.add_argument("--holes", type=int, default=3); p.add_argument("--yaw", type=float, default=25); p.add_argument("--pitch", type=float, default=12)
    p.add_argument("--w", type=int, default=1400); p.add_argument("--h", type=int, default=600); p.set_defaults(f=cmd_decalshots)
    a, extra = ap.parse_known_args()
    a.extra = extra
    a.f(load_env(), a)


if __name__ == "__main__":
    main()
