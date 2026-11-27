#!/usr/bin/env python
"""modelview: compare mesh subdivision methods on a character, outside the game (PC only).

  python tools_pc/modelview/modelview.py pc [viewer options]   build and open the viewer (detached)
        viewer options go through: --shot FILE.png (renders one frame, writes it, exits; waits for it) --w N --h N
        --export DIR --body CgreyguardZ --head CheadkarlZ --specs 0|1 --hair 0|1 --bodyscale 0|1 --model FILE|BODY+HEAD
        --tier 0|1 --method GAME|FLAT|PN|PHONG|BUTTERFLY|LOOP|HEAD --passes 1|2 --crease DEG --clamp F --splitjoint 0|1
        --view side|split|sub|orig --wire 0|1 --edges 0|1 --checker 0|1 --normals 0|1 --light 0|1 --smooth 0|1
  python tools_pc/modelview/modelview.py export [--rom FILE] [--all-animations]
        write the character export into tools_pc/modelview/data/export (ge007-export, built in Docker)
  python tools_pc/modelview/modelview.py selftest              PNG decoding, pose maths and the HEAD method, no export needed
  python tools_pc/modelview/modelview.py shots --out DIR [--anim NAME --frame N] [--clean] [--models A,B+C,...] [--methods FLAT,PN,...]
        one screenshot per model and method plus DIR/index.html (a model is a body, a head, or BODY+HEAD)

The characters come from the static export (characters/characters.json + one glTF per body and head,
docs/specs/characters.md), made from your own ROM: --export DIR, else MODELVIEW_EXPORT, else
tools_pc/modelview/data/export (`modelview.py export` writes it). The Windows g++ comes from MODELVIEW_GXX
(environment or <repo>/.env), else SEAVIEW_GXX. Settings are saved in tools_pc/modelview/data/modelview.ini
(git ignores data/ and build/). Nothing from the ROM is written into the repo outside data/.
"""
import argparse, html, json, os, re, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
DATA = os.path.join(HERE, "data")
OWN_EXPORT = os.path.join(DATA, "export")
sys.path.insert(0, os.path.join(REPO, "tools"))
import devenv   # tools/devenv.py: .env (the main checkout's from a worktree), ROM and g++ lookup
DEFAULT_MODELS = ["CgreyguardZ+CheadkarlZ", "CtrevelyanZ", "CheadbrosnanZ", "CheadmartinZ"]
DEFAULT_METHODS = ["GAME", "PN", "FLAT", "PHONG", "BUTTERFLY", "HEAD"]


def load_env():
    return devenv.load_env()


def find_gxx(env):
    gxx = devenv.find_gxx(env, ("MODELVIEW_GXX",))
    if not gxx:
        sys.exit(f"modelview: set MODELVIEW_GXX (or SEAVIEW_GXX) in {devenv.env_files()[-1]}: a Windows g++ (MinGW-w64)")
    return gxx


def exporter_version():
    """EX_EXPORTER_VERSION of this checkout (port/export/export.h), None when it cannot be read"""
    try:
        m = re.search(r"#define\s+EX_EXPORTER_VERSION\s+(\d+)", open(os.path.join(REPO, "port", "export", "export.h"), encoding="utf-8").read())
        return int(m.group(1)) if m else None
    except OSError:
        return None


def export_state(d):
    """(characters dir, its exporter version) for an export dir or its characters/ folder; (None, None) when absent"""
    for c in (d, os.path.join(d, "characters")):
        j = os.path.join(c, "characters.json")
        if os.path.isfile(j):
            try:
                return os.path.abspath(c), json.load(open(j, encoding="utf-8")).get("exporter_version")
            except (OSError, ValueError):
                return os.path.abspath(c), None
    return None, None


def find_export(env, extra):
    """--export (passed through untouched), MODELVIEW_EXPORT, then data/export; None = the viewer shows the mannequin only"""
    if "--export" in extra:
        return None
    for d in ([env["MODELVIEW_EXPORT"]] if env.get("MODELVIEW_EXPORT") else []) + [OWN_EXPORT]:
        c, ver = export_state(d)
        if c:
            want = exporter_version()
            if want is not None and ver != want:
                print(f"modelview: {c} was written by exporter version {ver}, this checkout writes {want}: "
                      "run `modelview.py export` to refresh it")
            return c
    print("modelview: no character export: run `python tools_pc/modelview/modelview.py export` (or pass --export DIR)")
    return None


def build(env):
    """compile the viewer; returns the exe path (build/modelview_new.exe when a running viewer locks modelview.exe)"""
    gxx = find_gxx(env)
    gcc = os.path.join(os.path.dirname(gxx), "gcc.exe" if os.name == "nt" else "gcc")
    pc = os.path.join(HERE, "pc")
    conv = os.path.join(REPO, "port", "convert")
    third = os.path.join(REPO, "port", "third_party")   # vendored, declared in NOTICE
    imgui = os.path.join(third, "imgui")               # no backends: pc/mv_imgui.cpp
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

    # the converter's own subdivision (C), linked unchanged (PN, GAME); its zlib calls go to the inflate shim in pc/
    for f in ("conv_common.c", "conv_subdiv.c", "convert_data.c"):
        obj(gcc, os.path.join(conv, f), ["-I", os.path.join(pc, "shim"), "-I", conv, "-w"])
    for f in ("imgui.cpp", "imgui_draw.cpp", "imgui_tables.cpp", "imgui_widgets.cpp"):
        obj(gxx, os.path.join(imgui, f), ["-std=c++17", "-I", imgui, "-w"])
    obj(gcc, os.path.join(third, "cjson", "cJSON.c"), ["-w"])

    exe = os.path.join(bdir, "modelview.exe")
    if os.path.exists(exe):
        try:
            os.remove(exe)
        except OSError:   # a running viewer locks it: build next to it
            exe = os.path.join(bdir, "modelview_new.exe")
            print("modelview: modelview.exe is running, building", exe)
    srcs = [os.path.join(pc, f) for f in ("modelview.cpp", "mv_imgui.cpp", "mv_subdiv.cpp", "mv_mannequin.cpp", "mv_export.cpp", "mv_zlib.cpp", "mv_head.cpp")]
    cmd = [gxx, "-O2", "-std=c++17", "-Wall", "-I", imgui, "-I", conv, "-I", os.path.join(third, "cgltf"), "-I", os.path.join(third, "cjson"),
           "-o", exe] + srcs + objs + ["-lopengl32", "-lgdi32", "-luser32", "-limm32", "-static"]
    if subprocess.run(cmd, env=e).returncode != 0:
        sys.exit("modelview: the viewer did not build")
    return exe


def absolutise(extra):
    for i, x in enumerate(extra[:-1]):   # the viewer runs in data/: a path given relative to here must be made absolute
        if x in ("--shot", "--export"):
            extra[i + 1] = os.path.abspath(extra[i + 1])
    return extra


def cmd_pc(env, a):
    exe = build(env)
    extra = list(a.extra)
    exp = find_export(env, extra)
    if exp:
        extra += ["--export", exp]
    extra = absolutise(extra)
    if "--shot" in extra:   # unattended screenshot: wait for it
        sys.exit(subprocess.run([exe] + extra, cwd=DATA).returncode)
    detached = 0x00000008 | 0x00000200 if os.name == "nt" else 0   # DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP: outlive this script
    subprocess.Popen([exe] + extra, cwd=DATA, creationflags=detached)
    print("viewer open; its settings are saved in", DATA)


def cmd_export(env, a):
    """ge007-export from this checkout, built and run in the pinned build image; the ROM is mounted read-only"""
    rom = os.path.abspath(a.rom) if a.rom else devenv.find_rom(env, ("MODELVIEW_ROM",))
    if not rom or not os.path.isfile(rom):
        sys.exit("modelview: no ROM: pass --rom, or set N64_ROM in .env")
    import dev   # tools/dev.py: the pinned build image
    image = dev.cfg().get("image", dev.IMAGE)
    os.makedirs(OWN_EXPORT, exist_ok=True)
    run = ("[ -f build-linux/build.ninja ] || cmake -S . -B build-linux -G Ninja -DROMID=ntsc-final >/dev/null && "
           "cmake --build build-linux --target ge007-export && "
           f"./build-linux/port/export/ge007-export '/rom/{os.path.basename(rom)}' /out")
    cmd = ["docker", "run", "--rm", "-v", f"{REPO}:/src", "-v", "project-dam-ccache:/ccache", "-v", f"{os.path.dirname(rom)}:/rom:ro",
           "-v", f"{OWN_EXPORT}:/out", "-w", "/src"] + (["-e", "GE007_EXPORT_ALL_ANIMATIONS=1"] if a.all_animations else []) + [image, "sh", "-c", run]
    print("+ ge007-export in", image.split("@")[0], "->", OWN_EXPORT, flush=True)
    r = subprocess.run(cmd, env=dict(os.environ, MSYS_NO_PATHCONV="1"))
    if r.returncode:
        sys.exit(f"modelview: the export failed ({r.returncode})")
    c, ver = export_state(OWN_EXPORT)
    print("export ready:", c, f"(exporter version {ver})")


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
    exp = os.path.abspath(a.export) if a.export else find_export(env, [])
    if not exp:
        sys.exit("modelview: no character export (run `modelview.py export`, or pass --export DIR)")
    ok = {}
    for m in models:
        for me in methods:
            stem = m.replace("+", "_")
            png = os.path.join(out, f"{stem}_{me}.png")
            r = subprocess.run([exe, "--export", exp, "--model", m, "--method", me, "--view", "side", "--wire", "1", "--edges", "1",
                                "--w", str(a.w), "--h", str(a.h)] + (["--anim", a.anim] if a.anim else []) + (["--frame", str(a.frame)] if a.frame is not None else []) + (["--clean", "1"] if a.clean else []) + ["--shot", png], cwd=DATA, capture_output=True, text=True)
            ok[(m, me)] = r.returncode == 0 and os.path.exists(png)
            print(("ok   " if ok[(m, me)] else "FAIL ") + f"{m} {me}")
    rows = []
    for m in models:
        stem = m.replace("+", "_")
        cells = "".join(f'<td><div>{html.escape(me)}</div>' + (f'<a href="{stem}_{me}.png"><img src="{stem}_{me}.png" width="480"></a>' if ok[(m, me)] else "failed") + "</td>" for me in methods)
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
    p = sub.add_parser("export"); p.add_argument("--rom"); p.add_argument("--all-animations", action="store_true", help="also put all 170 animations into every body file (they are always in characters/animations/)")
    p.set_defaults(f=cmd_export)
    p = sub.add_parser("selftest"); p.set_defaults(f=cmd_selftest)
    p = sub.add_parser("shots", allow_abbrev=False)
    p.add_argument("--out", required=True); p.add_argument("--models"); p.add_argument("--methods"); p.add_argument("--export")
    p.add_argument("--anim"); p.add_argument("--frame", type=float); p.add_argument("--clean", action="store_true", help="no overlays: texture only")
    p.add_argument("--w", type=int, default=1280); p.add_argument("--h", type=int, default=720); p.set_defaults(f=cmd_shots)
    a, extra = ap.parse_known_args()
    a.extra = extra
    a.f(load_env(), a)


if __name__ == "__main__":
    main()
