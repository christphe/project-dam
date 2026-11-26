#!/usr/bin/env python
"""fxview: GoldenEye's combat-effect sprites up close, ORIGINAL vs EFFECT SHADER (fxShade), outside the game (PC only).

  python tools_pc/fxview/fxview.py pc [viewer options]   build and open the viewer (detached)
        viewer options go through: --shot FILE.png (renders one image, writes it, exits; waits for it) --w N --h N
        --rom FILE.z64 --kind tracer|laser|watchlaser|wallpuff|bloodpuff|scatter|impact --impact 0..19 --age TICKS
        --angle RAD --view side|split|orig|fx --backdrop grey|dark|checker|wall --bgtex N --zoom F --fc N --nearest 0|1
        --tracerlen F --shader FILE (default: <repo>/port/shaders/fx.glsl)
  python tools_pc/fxview/fxview.py selftest              texture formats, the FXSHADE block, the puff frame stepping; no ROM
  python tools_pc/fxview/fxview.py shots --out DIR [--backdrop grey|dark|checker|wall] [--bgtex N] [--fc N]
        one side-by-side PNG per effect (puffs: several animation frames) plus DIR/index.html
  python tools_pc/fxview/fxview.py decalshots --out DIR [--holes N] [--yaw DEG] [--pitch DEG]
        the port's bullet holes (fxDecalBlend) on a real wall texture per material, game vs port, plus DIR/index.html
        (decal mode in the viewer: pc --mode decal [--mat stone|tile|default|wood|metal|metalobj|dirt|mud|snow|glass ...])
  the viewer's tabs: pc --mode decal|muzzle|tracer|burst|explosion|sprite [--t SECONDS --tshow game|port|side ...]
        (muzzle flashes, tracers, blood / smoke / debris bursts and explosions, game vs port FX: see README.md)

fxShade is read from port/shaders/fx.glsl (between "// FXSHADE BEGIN" and "// FXSHADE END") and hot-reloaded when the file
changes: tuning = editing that file with the viewer open. The Windows g++ comes from FXVIEW_GXX, MODELVIEW_GXX or
SEAVIEW_GXX (environment or <repo>/.env). The sprites are decoded from your own ROM at run time: --rom, else FXVIEW_ROM,
else MODELVIEW_ROM, else N64_ROM, else a game data/ folder (build-win/data/...). Settings: tools_pc/fxview/data/fxview.ini (git
ignores data/ and build/). Nothing from the ROM is written into the repo: shots refuse an output dir inside it.
"""
import argparse, html, os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
DATA = os.path.join(HERE, "data")
MV = os.path.join(REPO, "tools_pc", "modelview", "pc")   # the texture decoder, inflate, ImGui renderer and PNG writer are modelview's
sys.path.insert(0, os.path.join(REPO, "tools"))
import devenv   # tools/devenv.py: .env (the main checkout's from a worktree), ROM and g++ lookup

# (file stem, viewer args, caption) for `shots`
SHOTS = [
    ("tracer", ["--kind", "tracer"], "gun tracer"),
    ("laser", ["--kind", "laser"], "laser tracer"),
    ("watchlaser", ["--kind", "watchlaser"], "watch laser"),
] + [(f"wallpuff_age{a:02d}", ["--kind", "wallpuff", "--age", str(a), "--zoom", "2.5"], f"wall puff, age {a}") for a in (0, 2, 4, 6, 8, 10)] \
  + [(f"bloodpuff_age{a:02d}", ["--kind", "bloodpuff", "--age", str(a), "--zoom", "2.5"], f"blood puff, age {a}") for a in (0, 2, 4, 6, 8, 10)] \
  + [(f"scatter_age{a:02d}", ["--kind", "scatter", "--age", str(a), "--zoom", "2.5"], f"scatter puff, age {a}") for a in (0, 4, 8)] \
  + [(f"impact_{t:02d}", ["--kind", "impact", "--impact", str(t)], f"bullet-hole decal type {t}") for t in (0, 1, 2, 3, 7, 8, 9, 13, 16)]

# `decalshots`: the port's bullet holes (fxDecalBlend) per material, (file stem, viewer args, caption)
DECAL_MATS = [("stone", "stone"), ("tile", "tile"), ("default", "default (untagged)"), ("wood", "wood"), ("metal", "metal"),
              ("dirt", "dirt"), ("snow", "snow"), ("glass", "glass")]
SUN_UL = ["--sun", "1", "--sunaz", "135", "--sunel", "35"]           # the sun from the upper left
FLASH = ["--pt", "1", "--ptx", "40", "--pty", "-20", "--pth", "90"]   # a muzzle flash in front of the wall, beside the holes (closer: the wall saturates)
DECAL_SHOTS = [(f"decal_{m}_sun_flash", ["--mat", m] + SUN_UL + FLASH, f"{cap}: sun from the upper left + muzzle flash") for m, cap in DECAL_MATS] \
    + [(f"decal_{m}_flash", ["--mat", m, "--sun", "0"] + FLASH, f"{cap}: muzzle flash only (no sun: the in-game case today)") for m, cap in DECAL_MATS] \
    + [("decal_stone_sun_lowright", ["--mat", "stone", "--sun", "1", "--sunaz", "-30", "--sunel", "20", "--pt", "0"], "stone: low sun from the lower right, no flash (relief check)"),
       ("decal_stone_sun_upleft", ["--mat", "stone"] + SUN_UL + ["--pt", "0"], "stone: sun from the upper left, no flash (relief check)"),
       ("decal_stone_grazing", ["--mat", "stone", "--yaw", "68", "--pitch", "8"] + SUN_UL + FLASH, "stone: grazing view"),
       ("decal_wood_type7_overlap", ["--mat", "wood", "--dtype", "7", "--holes", "6"] + SUN_UL + FLASH, "wood, impact type 7, six overlapping holes")]


def load_env():
    return devenv.load_env()


def find_gxx(env):
    gxx = devenv.find_gxx(env, ("FXVIEW_GXX", "MODELVIEW_GXX"))
    if not gxx:
        sys.exit(f"fxview: set FXVIEW_GXX (or MODELVIEW_GXX / SEAVIEW_GXX) in {devenv.env_files()[-1]}: a Windows g++ (MinGW-w64)")
    return gxx


def find_rom(env):
    return devenv.find_rom(env, ("FXVIEW_ROM", "MODELVIEW_ROM"))


def build(env):
    """compile the viewer; returns the exe path (build/fxview_new.exe when a running viewer locks fxview.exe)"""
    gxx = find_gxx(env)
    pc = os.path.join(HERE, "pc")
    imgui = os.path.join(REPO, "port", "third_party", "imgui")   # vendored, declared in NOTICE
    bdir = os.path.join(HERE, "build"); odir = os.path.join(bdir, "obj")
    os.makedirs(odir, exist_ok=True); os.makedirs(DATA, exist_ok=True)
    e = dict(os.environ, PATH=os.path.dirname(gxx) + os.pathsep + os.environ.get("PATH", ""))
    objs = []

    def obj(src, flags):   # cached by modification time
        o = os.path.join(odir, os.path.basename(src) + ".o")
        objs.append(o)
        if not os.path.exists(o) or os.path.getmtime(o) < os.path.getmtime(src):
            if subprocess.run([gxx, "-O2", "-std=c++17", "-c", "-o", o, src] + flags, env=e).returncode != 0:
                sys.exit(f"fxview: {os.path.basename(src)} did not compile")

    for f in ("imgui.cpp", "imgui_draw.cpp", "imgui_tables.cpp", "imgui_widgets.cpp"):
        obj(os.path.join(imgui, f), ["-I", imgui, "-w"])
    for f in ("mv_tex.cpp", "mv_zlib.cpp", "mv_imgui.cpp"):
        obj(os.path.join(MV, f), ["-I", imgui, "-I", MV, "-Wall"])

    exe = os.path.join(bdir, "fxview.exe")
    if os.path.exists(exe):
        try:
            os.remove(exe)
        except OSError:   # a running viewer locks it: build next to it
            exe = os.path.join(bdir, "fxview_new.exe")
            print("fxview: fxview.exe is running, building", exe)
    cmd = [gxx, "-O2", "-std=c++17", "-Wall", "-I", imgui, "-I", MV, "-o", exe, os.path.join(pc, "fxview.cpp"), os.path.join(pc, "fx_tex.cpp")] + objs \
        + ["-lopengl32", "-lgdi32", "-luser32", "-limm32", "-static"]
    if subprocess.run(cmd, env=e).returncode != 0:
        sys.exit("fxview: the viewer did not build")
    return exe


def cmd_pc(env, a):
    exe = build(env)
    extra = list(a.extra)
    if "--rom" not in extra:
        rom = find_rom(env)
        if rom:
            extra += ["--rom", rom]
        else:
            print("fxview: no ROM (pass --rom or set N64_ROM in .env): the sprites show as untextured quads")
    extra += ["--repo", REPO]
    for i, x in enumerate(extra[:-1]):   # the viewer runs in data/: a path given relative to here must be made absolute
        if x in ("--shot", "--rom", "--shader"):
            extra[i + 1] = os.path.abspath(extra[i + 1])
    if "--shot" in extra:   # unattended screenshot: wait for it
        sys.exit(subprocess.run([exe] + extra, cwd=DATA).returncode)
    detached = 0x00000008 | 0x00000200 if os.name == "nt" else 0   # DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP
    subprocess.Popen([exe] + extra, cwd=DATA, creationflags=detached)
    print("viewer open; its settings are saved in", DATA)


def inside(path, root):
    path, root = os.path.abspath(path), os.path.abspath(root)
    try:
        return os.path.commonpath([path, root]) == root
    except ValueError:   # another drive
        return False


def cmd_selftest(env, a):
    exe = build(env)
    sys.exit(subprocess.run([exe, "--selftest", "--repo", REPO], cwd=DATA).returncode)


def cmd_shots(env, a):
    out = os.path.abspath(a.out)
    if inside(out, REPO) and not inside(out, DATA):
        sys.exit("fxview: --out must be outside the repo or inside tools_pc/fxview/data/ (the images are ROM-derived)")
    os.makedirs(out, exist_ok=True)
    exe = build(env)
    rom = a.rom or find_rom(env)
    if not rom:
        sys.exit("fxview: no ROM: pass --rom, or set N64_ROM in .env")
    common = ["--rom", os.path.abspath(rom), "--repo", REPO, "--view", "side", "--backdrop", a.backdrop, "--fc", str(a.fc),
              "--w", str(a.w), "--h", str(a.h)] + (["--bgtex", str(a.bgtex)] if a.bgtex is not None else [])
    ok = {}
    for stem, args, cap in SHOTS:
        png = os.path.join(out, stem + ".png")
        r = subprocess.run([exe] + common + args + ["--shot", png], cwd=DATA, capture_output=True, text=True)
        ok[stem] = r.returncode == 0 and os.path.exists(png)
        print(("ok   " if ok[stem] else "FAIL ") + stem + ("" if ok[stem] else "\n" + r.stderr.strip()))
    cells = "".join(f'<figure><figcaption>{html.escape(cap)}</figcaption>'
                    + (f'<a href="{stem}.png"><img src="{stem}.png" width="640"></a>' if ok[stem] else "failed") + "</figure>"
                    for stem, _, cap in SHOTS)
    with open(os.path.join(out, "index.html"), "w", encoding="utf-8") as f:
        f.write("<!doctype html><meta charset=utf-8><title>fxview shots</title>"
                "<style>body{background:#222;color:#ddd;font:14px sans-serif}figure{display:inline-block;margin:6px}img{display:block}</style>"
                f"<h3>left: original (texture x vertex colour, alpha over) | right: + fxShade, frame_count {a.fc}, backdrop {html.escape(a.backdrop)}</h3>"
                + cells)
    print("wrote", os.path.join(out, "index.html"))
    if not all(ok.values()):
        sys.exit(1)


def cmd_decalshots(env, a):
    out = os.path.abspath(a.out)
    if inside(out, REPO) and not inside(out, DATA):
        sys.exit("fxview: --out must be outside the repo or inside tools_pc/fxview/data/ (the images are ROM-derived)")
    os.makedirs(out, exist_ok=True)
    exe = build(env)
    rom = a.rom or find_rom(env)
    if not rom:
        sys.exit("fxview: no ROM: pass --rom, or set N64_ROM in .env")
    common = ["--rom", os.path.abspath(rom), "--repo", REPO, "--mode", "decal", "--view", "side", "--holes", str(a.holes),
              "--yaw", str(a.yaw), "--pitch", str(a.pitch), "--w", str(a.w), "--h", str(a.h)]
    ok = {}
    for stem, args, cap in DECAL_SHOTS:
        png = os.path.join(out, stem + ".png")
        r = subprocess.run([exe] + common + args + ["--shot", png], cwd=DATA, capture_output=True, text=True)
        ok[stem] = r.returncode == 0 and os.path.exists(png)
        print(("ok   " if ok[stem] else "FAIL ") + stem + ("" if ok[stem] else "\n" + r.stderr.strip()))
    cells = "".join(f'<figure><figcaption>{html.escape(cap)}</figcaption>'
                    + (f'<a href="{stem}.png"><img src="{stem}.png" width="900"></a>' if ok[stem] else "failed") + "</figure>"
                    for stem, _, cap in DECAL_SHOTS)
    with open(os.path.join(out, "index.html"), "w", encoding="utf-8") as f:
        f.write("<!doctype html><meta charset=utf-8><title>fxview decal shots</title>"
                "<style>body{background:#222;color:#ddd;font:14px sans-serif}figure{display:inline-block;margin:6px}img{display:block}</style>"
                "<h3>left: the game's hole (texture x vertex colour, alpha over) | right: the port's (fxDecalBlend, 2x multiply over the wall)."
                f" Camera yaw {a.yaw}, pitch {a.pitch}; {a.holes} holes; the wall is a real level texture of the material</h3>" + cells)
    print("wrote", os.path.join(out, "index.html"))
    if not all(ok.values()):
        sys.exit(1)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("pc", allow_abbrev=False); p.set_defaults(f=cmd_pc)
    p = sub.add_parser("selftest"); p.set_defaults(f=cmd_selftest)
    p = sub.add_parser("shots", allow_abbrev=False)
    p.add_argument("--out", required=True); p.add_argument("--rom")
    p.add_argument("--backdrop", default="grey", choices=["grey", "dark", "checker", "wall"]); p.add_argument("--bgtex", type=int)
    p.add_argument("--fc", type=int, default=120, help="frame_count given to fxShade")
    p.add_argument("--w", type=int, default=1200); p.add_argument("--h", type=int, default=600); p.set_defaults(f=cmd_shots)
    p = sub.add_parser("decalshots", allow_abbrev=False)
    p.add_argument("--out", required=True); p.add_argument("--rom")
    p.add_argument("--holes", type=int, default=3); p.add_argument("--yaw", type=float, default=25); p.add_argument("--pitch", type=float, default=12)
    p.add_argument("--w", type=int, default=1400); p.add_argument("--h", type=int, default=600); p.set_defaults(f=cmd_decalshots)
    a, extra = ap.parse_known_args()
    a.extra = extra
    a.f(load_env(), a)


if __name__ == "__main__":
    main()
