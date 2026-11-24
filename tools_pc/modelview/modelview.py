#!/usr/bin/env python
"""modelview: compare mesh subdivision methods on a model, outside the game (PC only).

  python tools_pc/modelview/modelview.py pc [viewer options]   build and open the viewer (detached)
        viewer options go through: --shot FILE.png (renders one frame, writes it, exits; waits for it) --w N --h N
        --rom FILE.z64 --model CgreyguardZ --tier 0|1 --method FLAT|PN|PHONG|BUTTERFLY|LOOP|HEAD --passes 1|2 --crease DEG
        --clamp F --splitjoint 0|1 --view side|split|sub|orig --wire 0|1 --edges 0|1 --checker 0|1 --normals 0|1
        --light 0|1 --smooth 0|1
  python tools_pc/modelview/modelview.py selftest              texture formats and animation maths, no ROM needed
  python tools_pc/modelview/modelview.py shots --out DIR [--anim NAME --frame N] [--clean] [--models A,B,...] [--methods FLAT,PN,...]
        one screenshot per model and method (side by side, wireframe on) plus DIR/index.html

The Windows g++ comes from MODELVIEW_GXX (environment or <repo>/.env), else SEAVIEW_GXX. The characters are read
from your own ROM at run time: --rom, else MODELVIEW_ROM, else N64_ROM, else a game data/ folder (build-win/data/...). Settings are
saved in tools_pc/modelview/data/modelview.ini (git ignores data/ and build/). Nothing from the ROM is written into the repo.
"""
import argparse, html, os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
DATA = os.path.join(HERE, "data")
sys.path.insert(0, os.path.join(REPO, "tools"))
import devenv   # tools/devenv.py: .env (the main checkout's from a worktree), ROM and g++ lookup
DEFAULT_MODELS = ["CgreyguardZ", "CtrevelyanZ", "CheadbrosnanZ", "CheadmartinZ"]
DEFAULT_METHODS = ["GAME", "PN", "FLAT", "PHONG", "BUTTERFLY", "HEAD"]


def load_env():
    return devenv.load_env()


def find_gxx(env):
    gxx = devenv.find_gxx(env, ("MODELVIEW_GXX",))
    if not gxx:
        sys.exit(f"modelview: set MODELVIEW_GXX (or SEAVIEW_GXX) in {devenv.env_files()[-1]}: a Windows g++ (MinGW-w64)")
    return gxx


def find_rom(env, extra):
    """--rom, MODELVIEW_ROM, N64_ROM, else a game data/ folder (devenv.find_rom); None = the viewer runs on the mannequin only"""
    if "--rom" in extra:
        return None   # the user gave one: pass it through untouched
    return devenv.find_rom(env, ("MODELVIEW_ROM",))


def build(env):
    """compile the viewer; returns the exe path (build/modelview_new.exe when a running viewer locks modelview.exe)"""
    gxx = find_gxx(env)
    gcc = os.path.join(os.path.dirname(gxx), "gcc.exe" if os.name == "nt" else "gcc")
    pc = os.path.join(HERE, "pc")
    conv = os.path.join(REPO, "port", "convert")
    imgui = os.path.join(REPO, "port", "third_party", "imgui")   # vendored, declared in NOTICE; no backends: pc/mv_imgui.cpp
    bdir = os.path.join(HERE, "build"); odir = os.path.join(bdir, "obj")
    os.makedirs(odir, exist_ok=True); os.makedirs(DATA, exist_ok=True)
    e = dict(os.environ, PATH=os.path.dirname(gxx) + os.pathsep + os.environ.get("PATH", ""))
    objs = []

    def obj(cc, src, flags):   # cached by modification time
        o = os.path.join(odir, os.path.basename(src) + ".o")
        objs.append(o)
        if not os.path.exists(o) or os.path.getmtime(o) < os.path.getmtime(src):
            r = subprocess.run([cc, "-O2", "-c", "-o", o, src] + flags, env=e)
            if r.returncode != 0:
                sys.exit(f"modelview: {os.path.basename(src)} did not compile")

    # the converter's own code (C), linked unchanged; its zlib calls go to the inflate shim in pc/
    for f in ("conv_common.c", "conv_subdiv.c", "convert_data.c"):
        obj(gcc, os.path.join(conv, f), ["-I", os.path.join(pc, "shim"), "-I", conv, "-w"])
    for f in ("imgui.cpp", "imgui_draw.cpp", "imgui_tables.cpp", "imgui_widgets.cpp"):
        obj(gxx, os.path.join(imgui, f), ["-std=c++17", "-I", imgui, "-w"])

    exe = os.path.join(bdir, "modelview.exe")
    if os.path.exists(exe):
        try:
            os.remove(exe)
        except OSError:   # a running viewer locks it: build next to it
            exe = os.path.join(bdir, "modelview_new.exe")
            print("modelview: modelview.exe is running, building", exe)
    srcs = [os.path.join(pc, f) for f in ("modelview.cpp", "mv_imgui.cpp", "mv_subdiv.cpp", "mv_mannequin.cpp", "mv_rom.cpp", "mv_zlib.cpp", "mv_tex.cpp", "mv_anim.cpp", "mv_head.cpp")]
    cmd = [gxx, "-O2", "-std=c++17", "-Wall", "-I", imgui, "-I", conv, "-o", exe] + srcs + objs + ["-lopengl32", "-lgdi32", "-luser32", "-limm32", "-static"]
    if subprocess.run(cmd, env=e).returncode != 0:
        sys.exit("modelview: the viewer did not build")
    return exe


def cmd_pc(env, a):
    exe = build(env)
    extra = list(a.extra)
    rom = find_rom(env, extra)
    if rom:
        extra += ["--rom", rom]
    extra += ["--repo", REPO]
    for i, x in enumerate(extra[:-1]):   # the viewer runs in data/: a path given relative to here must be made absolute
        if x in ("--shot", "--rom"):
            extra[i + 1] = os.path.abspath(extra[i + 1])
    if "--shot" in extra:   # unattended screenshot: wait for it
        sys.exit(subprocess.run([exe] + extra, cwd=DATA).returncode)
    detached = 0x00000008 | 0x00000200 if os.name == "nt" else 0   # DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP: outlive this script
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
    sys.exit(subprocess.run([exe, "--selftest"], cwd=DATA).returncode)


def cmd_shots(env, a):
    out = os.path.abspath(a.out)
    if inside(out, REPO) and not inside(out, DATA):
        sys.exit("modelview: --out must be outside the repo or inside tools_pc/modelview/data/ (the images are ROM-derived)")
    os.makedirs(out, exist_ok=True)
    models = [m for m in a.models.split(",") if m] if a.models else DEFAULT_MODELS
    methods = [m for m in a.methods.split(",") if m] if a.methods else DEFAULT_METHODS
    exe = build(env)
    rom = a.rom or find_rom(env, [])
    if not rom:
        sys.exit("modelview: no ROM: pass --rom, or set N64_ROM in .env")
    ok = {}
    for m in models:
        for me in methods:
            png = os.path.join(out, f"{m}_{me}.png")
            r = subprocess.run([exe, "--rom", os.path.abspath(rom), "--model", m, "--method", me, "--view", "side", "--wire", "1", "--edges", "1",
                                "--w", str(a.w), "--h", str(a.h), "--repo", REPO] + (["--anim", a.anim] if a.anim else []) + (["--frame", str(a.frame)] if a.frame is not None else []) + (["--clean", "1"] if a.clean else []) + ["--shot", png], cwd=DATA, capture_output=True, text=True)
            ok[(m, me)] = r.returncode == 0 and os.path.exists(png)
            print(("ok   " if ok[(m, me)] else "FAIL ") + f"{m} {me}")
    rows = []
    for m in models:
        cells = "".join(f'<td><div>{html.escape(me)}</div>' + (f'<a href="{m}_{me}.png"><img src="{m}_{me}.png" width="480"></a>' if ok[(m, me)] else "failed") + "</td>" for me in methods)
        rows.append(f"<tr><th>{html.escape(m)}</th>{cells}</tr>")
    with open(os.path.join(out, "index.html"), "w", encoding="utf-8") as f:
        f.write("<!doctype html><meta charset=utf-8><title>modelview shots</title>"
                "<style>body{background:#222;color:#ddd;font:14px sans-serif}td,th{padding:4px;vertical-align:top}img{display:block}</style>"
                "<h3>original | subdivided, wireframe: red boundary, green joint, yellow crease, magenta UV seam</h3>"
                "<table>" + "".join(rows) + "</table>")
    print("wrote", os.path.join(out, "index.html"))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("pc", allow_abbrev=False); p.set_defaults(f=cmd_pc)
    p = sub.add_parser("selftest"); p.set_defaults(f=cmd_selftest)
    p = sub.add_parser("shots", allow_abbrev=False)
    p.add_argument("--out", required=True); p.add_argument("--models"); p.add_argument("--methods"); p.add_argument("--rom")
    p.add_argument("--anim"); p.add_argument("--frame", type=float); p.add_argument("--clean", action="store_true", help="no overlays: texture only")
    p.add_argument("--w", type=int, default=1280); p.add_argument("--h", type=int, default=720); p.set_defaults(f=cmd_shots)
    a, extra = ap.parse_known_args()
    a.extra = extra
    a.f(load_env(), a)


if __name__ == "__main__":
    main()
