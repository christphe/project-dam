#!/usr/bin/env python
"""menuview: the game's menu on its own, to iterate on its look and feel.

  python tools_pc/menuview/menuview.py            build (if a source changed) and open it
  python tools_pc/menuview/menuview.py --build    build only
  python tools_pc/menuview/menuview.py --w 1920 --h 1080   window size

The menu is port/src/menu/menu_ui.cpp, the same file the game compiles. Windows only: it uses the g++ named
SEAVIEW_GXX in <repo>/.env (see .env.example), like seaview. Keys: arrows / Enter / PageUp / PageDown, mouse, any
XInput pad; Shift+Esc quits.
"""
import argparse, os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
EXE = os.path.join(HERE, "menuview.exe")
IMGUI = os.path.join(REPO, "port", "third_party", "imgui")
MENU = os.path.join(REPO, "port", "src", "menu")


def load_env():
    env = {}
    for root in (REPO, os.path.abspath(os.path.join(REPO, "..", "..", ".."))):   # a worktree reads the main checkout's .env
        p = os.path.join(root, ".env")
        if os.path.exists(p):
            for line in open(p, encoding="utf-8"):
                line = line.strip()
                if line and not line.startswith("#") and "=" in line:
                    k, v = line.split("=", 1)
                    env.setdefault(k.strip(), v.strip().strip('"'))
    if os.environ.get("SEAVIEW_GXX"):
        env["SEAVIEW_GXX"] = os.environ["SEAVIEW_GXX"]
    return env


def sources():
    core = [os.path.join(IMGUI, f) for f in ("imgui.cpp", "imgui_draw.cpp", "imgui_tables.cpp", "imgui_widgets.cpp")]
    back = [os.path.join(HERE, "backends", f) for f in ("imgui_impl_win32.cpp", "imgui_impl_opengl3.cpp")]
    mine = [os.path.join(HERE, "menuview.cpp")] + [os.path.join(MENU, f) for f in ("menu_ui.cpp", "menu_model.cpp", "menu_json.cpp")]
    mine += [os.path.join(MENU, "menu_pad.cpp")]
    mine += [os.path.join(MENU, "..", f) for f in ("defaults.c", "defaults_data.c")]   # compiled as C++ here, without the game's config
    return mine + core + back


def build():
    gxx = load_env().get("SEAVIEW_GXX")
    if not gxx or not os.path.exists(gxx):
        sys.exit("set SEAVIEW_GXX in <repo>/.env to a Windows g++ (see .env.example)")
    srcs = sources()
    deps = srcs + [os.path.join(MENU, f) for f in ("menu_ui.h", "menu_model.h", "menu_json.h")]
    if os.path.exists(EXE) and os.path.getmtime(EXE) >= max(os.path.getmtime(p) for p in deps):
        return
    cmd = [gxx, "-O2", "-std=gnu++17", "-DDEFAULTS_NO_CONFIG", "-I", IMGUI, "-I", HERE, "-I", MENU, "-o", EXE] + srcs + \
          ["-lopengl32", "-lgdi32", "-luser32", "-ldwmapi", "-limm32", "-static"]
    print("building menuview...")
    if subprocess.run(cmd).returncode != 0:
        sys.exit("build failed")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--build", action="store_true", help="build only")
    ap.add_argument("--lang", help="a translation file (flat JSON: English text -> translated text)")
    ap.add_argument("--w", type=int, default=1280)
    ap.add_argument("--h", type=int, default=720)
    a = ap.parse_args()
    build()
    if a.build:
        return
    cmd = [EXE, "--fonts", os.path.join(REPO, "port", "third_party", "fonts"), "--menu", os.path.join(MENU, "menu.json"),
           "--w", str(a.w), "--h", str(a.h)]
    if a.lang:
        cmd += ["--lang", a.lang]
    subprocess.Popen(cmd)


if __name__ == "__main__":
    main()
