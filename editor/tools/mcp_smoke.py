#!/usr/bin/env python3
"""Smoke test of project-dam-editor's MCP server, headless (--mcp-stdio) and over HTTP.

  mcp_smoke.py EDITOR_EXE EXPORT_DIR WORK_DIR

EXPORT_DIR holds level.rec + level.json (the synthetic one from make_test_rec is enough; its data/ folder
holds the synthetic setup and is picked up through GE_EDITOR_DATA);
WORK_DIR receives the saved mission. Exits 0 when every check passes.
"""
import json
import os
import subprocess
import sys
import urllib.error
import urllib.request

failures = []


def editor_env(export):
    """The editor reads the level setup from the converted game data: the synthetic one from make_test_rec sits in
    <export>/data (a real export uses the data folder next to the game, so leave the variable alone there)."""
    env = dict(os.environ)
    data = os.path.join(export, "data")
    if os.path.isdir(data):
        env["GE_EDITOR_DATA"] = data
    return env


def check(cond, what):
    print(("ok   " if cond else "FAIL ") + what)
    if not cond:
        failures.append(what)


class Stdio:
    def __init__(self, exe, export):
        self.p = subprocess.Popen([exe, "--mcp-stdio", "--export", export], stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True, env=editor_env(export))
        self.n = 0

    def call(self, method, params=None):
        self.n += 1
        msg = {"jsonrpc": "2.0", "id": self.n, "method": method}
        if params is not None:
            msg["params"] = params
        self.p.stdin.write(json.dumps(msg) + "\n")
        self.p.stdin.flush()
        return json.loads(self.p.stdout.readline())

    def notify(self, method):
        self.p.stdin.write(json.dumps({"jsonrpc": "2.0", "method": method}) + "\n")
        self.p.stdin.flush()

    def tool(self, tool_name, **args):
        r = self.call("tools/call", {"name": tool_name, "arguments": args})["result"]
        text = r["content"][0]["text"]
        try:
            return r["isError"], json.loads(text)
        except ValueError:
            return r["isError"], text

    def close(self):
        self.p.stdin.close()
        self.p.wait(timeout=10)


def near(a, b, eps=0.01):
    return all(abs(x - y) < eps for x, y in zip(a, b))


def transform_checks(exe, export, work):
    """set_transform / move with a position: grid + floor snap, axis lock, yaw, undo."""
    mpath = os.path.join(work, "smoke_transform.json")
    if os.path.exists(mpath):
        os.remove(mpath)
    s = Stdio(exe, export)
    s.call("initialize", {"protocolVersion": "2025-06-18"})
    s.tool("mission_open", new=True, level="bunker1", name="Transform test")
    names = [t["name"] for t in s.call("tools/list")["result"]["tools"]]
    check("set_transform" in names and "capture_thumb" in names, "tools/list has set_transform and capture_thumb")

    # an original guard moved to a free spot: gets a mission pad there, snapped to a 25 grid and the floor
    err, d = s.tool("set_transform", prop=0, pos=[123.4, 17.0, 301.2], snap={"grid": 25, "floor": True})
    check(not err and near(d["pos"], [125, 0, 300]), "set_transform snaps to the grid (x/z) and the floor (y): " + json.dumps(d))
    err, d = s.tool("move", prop=0, pos=[510, 99, 99], axis="x", snap={"grid": 25, "floor": True})
    check(not err and near(d["pos"], [500, 0, 300]), "move with axis x only changes x: " + json.dumps(d))
    err, d = s.tool("set_transform", prop=0, yaw=90)
    check(not err and abs(d["yaw"] - 90) < 0.1, "set_transform yaw")
    err, d = s.tool("set_transform", mission_pad="p1", pos=[60, 0, 60], snap={"grid": 25})
    check(not err and near(d["pos"], [50, 0, 50]), "set_transform on a mission pad: " + json.dumps(d))
    err, d = s.tool("set_transform", prop=1, pos=[1305, 0, 205], snap={"pads": True, "grid": 10})
    check(not err and near(d["pos"], [1300, 0, 200]), "set_transform snaps to the existing pad 3: " + json.dumps(d))
    err, d = s.tool("set_transform", prop=99, pos=[0, 0, 0])
    check(err, "set_transform of a missing prop is an error")
    err, d = s.tool("undo")
    check(not err, "undo the last transform")
    err, d = s.tool("mission_save", path=mpath)
    saved = json.load(open(mpath))
    pads = {p["id"]: p for p in saved["pads"]}
    mv = [g for g in saved["guards"] if g["op"] == "move" and g["prop"] == 0]
    check(len(mv) == 1 and mv[0]["pad"] in pads, "saved: one move op for prop 0 on a mission pad")
    if mv and mv[0]["pad"] in pads:
        p = pads[mv[0]["pad"]]
        check(near(p["pos"], [50, 0, 50]), "saved pad position is the snapped one: " + json.dumps(p["pos"]))
        check(abs(p["look"][0] - 1) < 0.01 and abs(p["look"][2]) < 0.01, "saved pad look follows the 90 degree yaw")
    check(not any(g["op"] == "move" and g["prop"] == 1 for g in saved["guards"]), "undone move of prop 1 is gone")
    s.close()


def duplicate_checks(exe, export, work):
    """duplicate: a copy of an original guard / object is a new add op with all its fields and its facing."""
    mpath = os.path.join(work, "smoke_duplicate.json")
    if os.path.exists(mpath):
        os.remove(mpath)
    s = Stdio(exe, export)
    s.call("initialize", {"protocolVersion": "2025-06-18"})
    s.tool("mission_open", new=True, level="bunker1", name="Duplicate test")
    err, d = s.tool("duplicate", prop=0)
    check(not err and d["added"] == {"kind": "guard", "op": 0}, "duplicate an original guard: " + json.dumps(d))
    err, d = s.tool("duplicate", prop=3, pos=[600, 0, 300])
    check(not err and d["added"]["kind"] == "object" and "new_pad" in d, "duplicate an original object to a point: " + json.dumps(d))
    err, d = s.tool("duplicate", kind="guard", op=0, pad=4)
    check(not err and d["added"]["op"] == 1, "duplicate an added guard onto a pad")
    err, d = s.tool("duplicate", prop=999)
    check(err, "duplicate of a missing prop is an error")
    err, d = s.tool("mission_save", path=mpath)
    saved = json.load(open(mpath))
    g = [x for x in saved["guards"] if x["op"] == "add"]
    check(len(g) == 2 and g[0]["model"] == 3 and g[0]["ai"] == 1025 and g[0]["health"] == 100 and g[0]["head"] == 0,
          "the copied guard carries the original's fields: " + json.dumps(g[0] if g else {}))
    o = [x for x in saved["objects"] if x["op"] == "add"]
    check(len(o) == 1 and o[0]["type"] == 3 and o[0]["model"] == 120 and o[0]["damage"] == 1000, "the copied object carries the original's fields")
    err, d = s.tool("undo")
    s.close()


def item_of(d, kind, key, idx):
    for it in d["items"]:
        if it["kind"] == kind and it.get(key) == idx:
            return it
    return None


def validator_checks(exe, export, work):
    """Validator (#103): pads off the floor, unreachable mission pads, scripts whose target / key / objective is gone."""
    s = Stdio(exe, export)
    s.call("initialize", {"protocolVersion": "2025-06-18"})
    s.tool("mission_open", new=True, level="bunker1", name="Validator test")
    err, d = s.tool("validate")
    check(not err and d["valid"] and d["issues"] == [], "validator: an empty mission is valid")
    # inside a room, and 20 units outside its wall (inside the tolerance): fine
    s.tool("place_guard", pos=[300, 0, 300], model=3)
    s.tool("place_guard", pos=[-20, 0, 400], model=3)
    err, d = s.tool("validate")
    check(d["valid"], "validator: pads on the floor and at its edge are fine: " + json.dumps(d["errors"]))
    # far outside every room: a guard there does not spawn
    err, d = s.tool("place_guard", pos=[5000, 0, 5000], model=3)
    pad = d["new_pad"]
    err, d = s.tool("validate")
    guard_err = [i for i in d["issues"] if "off the walkable floor" in i["text"] and i["severity"] == "error" and "will not spawn" in i["text"]]
    check(len(guard_err) == 1 and guard_err[0].get("item") == {"kind": "guard", "op": 2},
          "validator: a guard on a pad off the floor is an error naming the guard: " + json.dumps(d["issues"]))
    unreach = [i for i in d["issues"] if "unreachable" in i["text"]]
    check(len(unreach) == 1 and unreach[0].get("mission_pad") == pad, "validator: the mission pad off the floor is unreachable, selectable: " + json.dumps(unreach))
    err, d = s.tool("place_object", type=3, model=120, pad=pad)
    err, d = s.tool("validate")
    obj = [i for i in d["issues"] if "object op" in i["text"] and i["severity"] == "warning"]
    check(len(obj) == 1 and obj[0].get("item", {}).get("kind") == "object", "validator: an object off the floor is a warning: " + json.dumps(obj))
    # an original guard moved off the floor
    err, d = s.tool("move", prop=0, pad=pad)
    err, d = s.tool("validate")
    check(any("guard op" in i["text"] and i.get("item") == {"kind": "guard", "prop": 0} for i in d["issues"]),
          "validator: a moved original guard is named by its prop")
    s.close()

    # scripts: target / key / objective
    mpath = os.path.join(work, "smoke_scripts.json")
    mission = {
        "format": 1, "name": "Scripts", "level": "bunker1",
        "objectives": [{"id": "o1", "text": "x", "difficulty": 0}],
        "guards": [{"op": "remove", "prop": 0}],
        "scripts": [
            {"use": "key_door", "target": {"kind": "prop", "prop": 3}, "props": {"key": 99}},
            {"use": "key_door", "target": {"kind": "prop", "prop": 4}, "props": {"key": 33}},
            {"use": "exit_unlock", "target": {"kind": "prop", "prop": 0}, "props": {}},
            {"use": "exit_unlock", "target": {"kind": "prop", "prop": 77}, "props": {}},
            {"use": "key_door", "target": {"kind": "prop", "prop": 3}, "props": {"key": 33, "objective": "zzz"}},
            {"use": "give_item", "target": {"kind": "prop", "prop": 3}, "props": {"item": 99}},
        ],
    }
    json.dump(mission, open(mpath, "w"))
    s = Stdio(exe, export)
    s.call("initialize", {"protocolVersion": "2025-06-18"})
    err, d = s.tool("mission_open", path=mpath)
    check(not err, "validator: the scripts mission opens: " + str(d)[:80])
    err, d = s.tool("validate")
    texts = [i["text"] for i in d["issues"]]
    check(any("script 2" in t and "removed by this mission" in t for t in texts), "validator: a script on a removed prop: " + json.dumps(texts))
    check(any("script 3" in t and "does not exist" in t for t in texts), "validator: a script on a missing prop")
    check(not any("key item 99" in t for t in texts), "validator: give_item providing the key silences the key warning (script 0)")
    check(not any("script 1" in t and "key item" in t for t in texts), "validator: the key the level provides (33) is fine")
    check(any("script 4" in t and "objective 'zzz'" in t for t in texts), "validator: a script naming an objective the mission lacks")
    rem = [i for i in d["issues"] if "removed by this mission" in i["text"]]
    check(len(rem) == 1 and rem[0].get("item") == {"kind": "guard", "prop": 0}, "validator: the removed target is selectable")
    err, d = s.tool("remove", prop=5)       # the level's key goes: key 33 is provided by nothing now
    err, d = s.tool("validate")
    check(any("script 1" in i["text"] and "key item 33" in i["text"] for i in d["issues"]), "validator: a key whose only giver was removed")
    s.close()


def behavior_checks(exe, export, work):
    """set_guard_behavior (#102): an original guard gets a move op carrying ai, an added guard its own ai; emptied start."""
    mpath = os.path.join(work, "smoke_behavior.json")
    if os.path.exists(mpath):
        os.remove(mpath)
    s = Stdio(exe, export)
    s.call("initialize", {"protocolVersion": "2025-06-18"})
    names = [t["name"] for t in s.call("tools/list")["result"]["tools"]]
    check("set_guard_behavior" in names, "tools/list has set_guard_behavior")
    s.tool("mission_open", new=True, level="bunker1", name="Behavior test")
    err, d = s.tool("set_guard_behavior", prop=1, ai=1027)
    check(not err and d["guard"] == {"kind": "guard", "prop": 1} and d["ai"] == 1027 and d["level_own_list"],
          "set_guard_behavior on an original guard: " + json.dumps(d))
    err, d = s.tool("level_describe", detail="props")
    g = item_of(d, "guard", "prop", 1)
    check(g and g["ai"] == 1027 and not g["moved"], "the guard shows its new ai and has not moved: " + json.dumps(g))
    err, d = s.tool("set_guard_behavior", prop=1, ai="0x401")
    check(not err and d["ai"] == 1025, "ai given as a hex string")
    err, d = s.tool("set_guard_behavior", prop=1, ai=70000)
    check(err, "an AI list id out of range is an error")
    err, d = s.tool("set_guard_behavior", prop=3, ai=1025)
    check(err, "an object is not a guard")
    err, d = s.tool("set_guard_behavior", prop=1)
    check(err, "ai is required")
    err, d = s.tool("set_guard_behavior", prop=99, ai=1025)
    check(err, "a missing prop is an error")
    err, d = s.tool("set_guard_behavior", prop=0, ai=2000)
    check(not err and d["level_own_list"] is False and "note" in d, "a list the level does not own is accepted with a note")
    err, d = s.tool("place_guard", pad=4, model=3, ai=1025)
    op = d["added"]["op"]
    err, d = s.tool("set_guard_behavior", kind="guard", op=op, ai=1026)
    check(not err and d["guard"] == {"kind": "guard", "op": op}, "set_guard_behavior on an added guard")
    err, d = s.tool("undo")
    err, d = s.tool("level_describe", detail="props")
    check(item_of(d, "guard", "op", op)["ai"] == 1025, "undo restores the added guard's ai")
    err, d = s.tool("set_guard_behavior", kind="guard", op=op, ai=1026)
    err, d = s.tool("remove", prop=2)
    err, d = s.tool("set_guard_behavior", prop=2, ai=1025)
    check(err, "a removed guard cannot be given a behaviour")
    err, d = s.tool("mission_save", path=mpath)
    saved = json.load(open(mpath))
    mv = [x for x in saved["guards"] if x["op"] == "move" and x["prop"] == 1]
    check(len(mv) == 1 and mv[0]["ai"] == 1025 and mv[0]["pad"] == 3, "saved: one move op for prop 1, same pad, ai 0x401: " + json.dumps(mv))
    check(any(x["op"] == "add" and x["ai"] == 1026 for x in saved["guards"]), "saved: the added guard's ai")
    s.close()
    s = Stdio(exe, export)
    s.call("initialize", {"protocolVersion": "2025-06-18"})
    err, d = s.tool("mission_open", path=mpath)
    err, d = s.tool("level_describe", detail="props")
    g = item_of(d, "guard", "prop", 1)
    check(g and g["ai"] == 1025 and g["pad"] == 3, "reopened: the guard keeps its pad and the new ai")
    s.close()

    # an emptied start: every original guard and pickup removed, doors / scenery (type 3 props) kept
    epath = os.path.join(work, "smoke_emptied.json")
    s = Stdio(exe, export)
    s.call("initialize", {"protocolVersion": "2025-06-18"})
    err, d = s.tool("mission_open", new=True, level="bunker1", name="Emptied", emptied=True)
    check(not err, "mission_open emptied")
    err, d = s.tool("mission_save", path=epath)
    saved = json.load(open(epath))
    check(sorted(x["prop"] for x in saved["guards"] if x["op"] == "remove") == [0, 1, 2], "emptied: the three original guards are removed")
    check(sorted(x["prop"] for x in saved["objects"] if x["op"] == "remove") == [5],
          "emptied: the key pickup is removed, the scenery (type 3) is kept: " + json.dumps(saved["objects"]))
    s.close()


def presence_checks(exe, export, work):
    """set_presence: per-difficulty presence of original and added guards / objects."""
    mpath = os.path.join(work, "smoke_presence.json")
    if os.path.exists(mpath):
        os.remove(mpath)
    s = Stdio(exe, export)
    s.call("initialize", {"protocolVersion": "2025-06-18"})
    names = [t["name"] for t in s.call("tools/list")["result"]["tools"]]
    check("set_presence" in names, "tools/list has set_presence")
    s.tool("mission_open", new=True, level="bunker1", name="Presence test")
    err, d = s.tool("set_presence", prop=1, difficulties=[2, 3])
    check(not err and d["difficulties"] == [2, 3], "set_presence on an original guard: " + json.dumps(d))
    err, d = s.tool("level_describe", detail="props")
    g = item_of(d, "guard", "prop", 1)
    check(g and not g["moved"] and g["pad"] == 3, "a presence edit does not move the guard: " + json.dumps(g))
    err, d = s.tool("set_presence", prop=3, difficulties=[0])
    check(not err, "set_presence on an original object")
    err, d = s.tool("place_guard", pad=4, model=3)
    op = d["added"]["op"]
    err, d = s.tool("set_presence", kind="guard", op=op, difficulties=[1])
    check(not err, "set_presence on an added guard")
    err, d = s.tool("set_presence", prop=1, difficulties=[])
    check(err, "no difficulty at all is an error")
    err, d = s.tool("set_presence", prop=1, difficulties=[7])
    check(err, "a difficulty out of range is an error")
    err, d = s.tool("validate")
    check(d["valid"], "a presence-only move op validates: " + json.dumps(d["errors"]))
    err, d = s.tool("mission_save", path=mpath)
    saved = json.load(open(mpath))
    mv = [x for x in saved["guards"] if x["op"] == "move" and x["prop"] == 1]
    check(len(mv) == 1 and mv[0]["difficulties"] == [2, 3] and "pad" not in mv[0], "saved: a move op with difficulties and no pad: " + json.dumps(mv))
    ob = [x for x in saved["objects"] if x["op"] == "move" and x["prop"] == 3]
    check(len(ob) == 1 and ob[0]["difficulties"] == [0], "saved: the object's presence")
    check(any(x["op"] == "add" and x["difficulties"] == [1] for x in saved["guards"]), "saved: the added guard's presence")
    err, d = s.tool("set_presence", prop=1, difficulties=[0, 1, 2, 3])
    err, d = s.tool("mission_save", path=mpath)
    saved = json.load(open(mpath))
    check(not any("difficulties" in x for x in saved["guards"] if x["op"] == "move"), "all four difficulties = the key is gone")
    err, d = s.tool("undo")
    err, d = s.tool("undo")
    s.close()


def mod_checks(exe, export, work):
    """mod (campaign): create, two missions, reorder, save, reload; environment block."""
    import shutil
    moddir = os.path.join(work, "mods", "smokemod")
    shutil.rmtree(moddir, ignore_errors=True)
    s = Stdio(exe, export)
    s.call("initialize", {"protocolVersion": "2025-06-18"})
    names = [t["name"] for t in s.call("tools/list")["result"]["tools"]]
    for want in ("mod_open", "mod_save", "mod_add_mission", "mod_reorder", "set_environment"):
        check(want in names, "tools/list has " + want)
    err, d = s.tool("mod_open", dir=moddir, name="Smoke mod", author="tester", description="a test", affects=["story", "multiplayer"],
                    unlock="open")
    check(not err and d["name"] == "Smoke mod" and d["missions"] == [], "mod_open creates the mod folder")
    import re
    uuid_re = re.compile(r"^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$")
    mod_id = d["id"]
    check(uuid_re.match(mod_id) is not None and d["id_valid"], "a new mod gets a UUID v4: " + mod_id)
    check(json.load(open(os.path.join(moddir, "campaign.json")))["id"] == mod_id, "the id is in campaign.json")
    err, d = s.tool("mod_add_mission", name="First", level="bunker1")
    check(not err and d["folder"] == "first" and os.path.exists(os.path.join(moddir, "first", "mission.json")), "mod_add_mission 1")
    err, d = s.tool("mod_add_mission", name="Second", level="silo")
    check(not err and d["folder"] == "second" and d["index"] == 1, "mod_add_mission 2")
    err, d = s.tool("mod_reorder", order=["second", "first"])
    check(not err and d["missions"] == ["second", "first"], "mod_reorder by order")
    err, d = s.tool("mod_reorder", mission="second", to=1)
    check(not err and d["missions"] == ["first", "second"], "mod_reorder by mission + to")
    err, d = s.tool("mod_reorder", order=["second", "first"])
    err, d = s.tool("mod_save")
    check(not err and not d["warnings"], "mod_save without warnings: " + json.dumps(d.get("warnings")))
    camp = json.load(open(os.path.join(moddir, "campaign.json")))
    check(camp["format"] == 1 and camp["name"] == "Smoke mod" and camp["author"] == "tester", "campaign.json header")
    check(camp["missions"] == ["second", "first"], "campaign.json mission order")
    check(sorted(camp["affects"]) == ["multiplayer", "story"] and camp["unlock"] == "open", "campaign.json affects and unlock")
    err, d = s.tool("mod_add_mission", name="Third")
    err, d = s.tool("mod_open", dir=moddir)
    check(d["missions"] == ["second", "first", "third"] or d["missions"] == ["second", "first"], "mod_open reloads the order: " + json.dumps(d["missions"]))
    s.close()

    s = Stdio(exe, export)
    s.call("initialize", {"protocolVersion": "2025-06-18"})
    err, d = s.tool("mod_open", dir=moddir, mission="first")
    check(not err and d["missions"][0] == "second", "reloaded in a new process")
    check(d["id"] == mod_id, "the id survives save and reload")
    err, d = s.tool("level_describe")
    check(d["mission"]["open"] and d["mission"]["name"] == "First", "mod_open mission=first opens that mission")

    # a campaign.json without an id: warned on open, generated on the next save
    cpath = os.path.join(moddir, "campaign.json")
    camp = json.load(open(cpath))
    del camp["id"]
    json.dump(camp, open(cpath, "w"))
    err, d = s.tool("mod_open", dir=moddir)
    check(not err and not d["id_valid"] and "warning" in d, "mod without an id: warned on open")
    err, d = s.tool("mod_save")
    check(not err and uuid_re.match(d["id"]) is not None and any("id" in w for w in d["warnings"]), "mod_save generates the missing id")
    check(uuid_re.match(json.load(open(cpath))["id"]) is not None, "the generated id is in campaign.json")
    err, d = s.tool("mod_open", dir=moddir, mission="first")
    err, d = s.tool("set_environment", mood="dusk", sun={"azimuth": 120, "elevation": 12, "color": [1, 0.8, 0.6], "glow": 0.6},
                    sky=[0.5, 0.6, 0.7], cloud_speed=2, sea={"enabled": True, "height": -120, "swell": 1.5})
    check(not err and d["environment"]["mood"] == "dusk", "set_environment: " + json.dumps(d)[:120])
    err, d = s.tool("mission_save")
    saved = json.load(open(os.path.join(moddir, "first", "mission.json")))
    env = saved.get("environment", {})
    check(env.get("mood") == "dusk" and env["sun"]["azimuth"] == 120 and env["sun"]["elevation"] == 12, "saved environment: mood and sun")
    check(abs(env["sun"]["color"][1] - 0.8) < 1e-3 and abs(env["sun"]["glow"] - 0.6) < 1e-3, "saved sun colour and glow")
    check(env["sky"] == [0.5, 0.6, 0.7] or all(abs(x - y) < 1e-3 for x, y in zip(env["sky"], [0.5, 0.6, 0.7])), "saved sky colour")
    check(env["cloud_speed"] == 2 and env["sea"]["enabled"] is True and env["sea"]["height"] == -120 and env["sea"]["swell"] == 1.5,
          "saved cloud speed and sea block")
    check("clouds" not in env and "chop" not in env["sea"], "fields not overridden stay absent")
    err, d = s.tool("set_environment", clear=["sun", "sea"])
    err, d = s.tool("mission_save")
    env = json.load(open(os.path.join(moddir, "first", "mission.json"))).get("environment", {})
    check("sun" not in env and "sea" not in env and env.get("mood") == "dusk", "clear removes sun and sea, keeps mood")
    err, d = s.tool("undo")
    err, d = s.tool("set_environment", clear="all")
    err, d = s.tool("mission_save")
    check("environment" not in json.load(open(os.path.join(moddir, "first", "mission.json"))), "clear all drops the environment block")
    s.close()


def gui_capture_check(exe, export, work):
    """capture_thumb needs the window: start the GUI (a display is required) and call it over HTTP."""
    import time
    port = 18766
    url = "http://127.0.0.1:%d/mcp" % port
    out = os.path.join(work, "mod", "mission1", "thumb.png")
    if os.path.exists(out):
        os.remove(out)
    p = subprocess.Popen([exe, "--export", export, "--port", str(port)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, env=editor_env(export))

    def post(body):
        req = urllib.request.Request(url, data=json.dumps(body).encode(), headers={"Content-Type": "application/json"}, method="POST")
        with urllib.request.urlopen(req, timeout=20) as r:
            return json.loads(r.read().decode())

    try:
        up = False
        for _ in range(60):
            try:
                post({"jsonrpc": "2.0", "id": 1, "method": "ping"})
                up = True
                break
            except Exception:
                time.sleep(0.5)
        check(up, "GUI editor answers on HTTP")
        if up:
            time.sleep(1.0)   # a few frames, so the 3D view rectangle is known
            r = post({"jsonrpc": "2.0", "id": 2, "method": "tools/call",
                      "params": {"name": "capture_thumb", "arguments": {"path": out}}})["result"]
            check(not r["isError"], "capture_thumb ok: " + r["content"][0]["text"])
            ok = os.path.exists(out)
            check(ok, "thumb.png written (parent directories created)")
            if ok:
                data = open(out, "rb").read()
                w, h = int.from_bytes(data[16:20], "big"), int.from_bytes(data[20:24], "big")
                check(data[:8] == b"\x89PNG\r\n\x1a\n" and (w, h) == (320, 240), "thumb is a 320x240 PNG (%dx%d)" % (w, h))
            # with a mod open the default path is mods/<mod>/<mission>/thumb.png
            import shutil
            moddir = os.path.join(work, "mods", "guimod")
            shutil.rmtree(moddir, ignore_errors=True)

            def call(i, n, a):
                return post({"jsonrpc": "2.0", "id": i, "method": "tools/call", "params": {"name": n, "arguments": a}})["result"]

            # two captures of the same view: both 320x240 and identical (a plain crop, deterministic)
            def dims(path):
                b = open(path, "rb").read()
                return int.from_bytes(b[16:20], "big"), int.from_bytes(b[20:24], "big"), b
            p1, p1b = [os.path.join(work, n) for n in ("cap1.png", "cap2.png")]
            r1 = call(7, "capture_thumb", {"path": p1})
            r2 = call(8, "capture_thumb", {"path": p1b})
            check(not (r1["isError"] or r2["isError"]), "capture_thumb twice")
            w1, h1, b1 = dims(p1); _, _, b2 = dims(p1b)
            check((w1, h1) == (320, 240), "the capture is 320x240")
            check(b1 == b2, "the capture is deterministic (same view, same file)")
            call(3, "mod_open", {"dir": moddir, "name": "GUI mod"})
            call(4, "mod_add_mission", {"name": "Thumbed", "level": "bunker1"})
            r = call(5, "capture_thumb", {})
            tp = os.path.join(moddir, "thumbed", "thumb.png")
            check(not r["isError"] and os.path.exists(tp), "capture_thumb without a path writes mods/<mod>/<mission>/thumb.png")
    finally:
        p.kill()
        p.wait(timeout=10)


def main():
    exe, export, work = [os.path.abspath(p) for p in sys.argv[1:4]]
    os.makedirs(work, exist_ok=True)
    mpath = os.path.join(work, "smoke_mission.json")
    if os.path.exists(mpath):
        os.remove(mpath)

    s = Stdio(exe, export)
    r = s.call("initialize", {"protocolVersion": "2025-06-18", "capabilities": {},
                              "clientInfo": {"name": "smoke", "version": "0"}})
    check(r["result"]["serverInfo"]["name"] == "project-dam-editor", "initialize names the server")
    check(r["result"]["protocolVersion"] == "2025-06-18", "initialize echoes the protocol version")
    s.notify("notifications/initialized")

    names = [t["name"] for t in s.call("tools/list")["result"]["tools"]]
    for want in ("level_describe", "mission_open", "mission_save", "place_guard", "place_object", "move", "remove",
                 "validate", "test_run"):
        check(want in names, "tools/list has " + want)

    err, d = s.tool("level_describe")
    check(not err and d["level"] == "bunker1" and d["room_count"] == 3, "level_describe: 3 rooms of bunker1")
    check(d["pad_count"] == 7 and d["guard_count"] == 3, "level_describe: pads and guards counted")

    err, d = s.tool("mission_open", new=True, level="bunker1", name="Smoke test")
    check(not err and d["mission"]["open"], "mission_open new bunker1 mission")

    err, d = s.tool("place_guard", pad=4, model=3, ai=0x401, weapon=17)
    check(not err and d["added"]["kind"] == "guard", "place_guard at an original pad")
    err, d = s.tool("place_guard", pos=[300, 0, 300])
    check(not err and d.get("new_pad") == "p1", "place_guard at a point creates pad p1")
    err, d = s.tool("place_object", type=3, model=120, pad="p1")
    check(not err, "place_object on the new pad")
    err, d = s.tool("move", prop=0, pad=2)
    check(not err, "move original guard prop 0")
    err, d = s.tool("remove", prop=4)
    check(not err, "remove original object prop 4")
    err, d = s.tool("move", prop=0, pad=0xdead)
    check(err, "move to an unknown pad is an error")
    err, d = s.tool("undo")
    check(not err, "undo")
    err, d = s.tool("validate")
    check(not err and d["valid"], "validate: mission is valid " + json.dumps(d))
    err, d = s.tool("mission_save", path=mpath)
    check(not err and os.path.exists(mpath), "mission_save wrote the file")

    saved = json.load(open(mpath))
    check(saved["format"] == 1 and saved["level"] == "bunker1" and saved["name"] == "Smoke test", "saved mission header")
    adds = [g for g in saved["guards"] if g["op"] == "add"]
    check(len(adds) == 2 and adds[0]["pad"] == 4 and adds[0]["ai"] == 0x401 and adds[0]["weapons"][0]["item"] == 17,
          "saved guard add op with pad/ai/weapon")
    check(adds[1]["pad"] == "p1" and saved["pads"][0]["id"] == "p1", "saved guard on the new pad p1")
    check(any(g["op"] == "move" and g["prop"] == 0 for g in saved["guards"]), "saved guard move op")
    check(any(o["op"] == "remove" and o["prop"] == 4 for o in saved["objects"]) is False, "undone remove is gone")

    s.close()

    # reopen the saved file in a fresh headless editor
    s = Stdio(exe, export)
    s.call("initialize", {"protocolVersion": "2025-03-26"})
    err, d = s.tool("mission_open", path=mpath)
    check(not err and d["mission"]["guard_ops"] == 3, "reopened mission has its guard ops")
    r = s.call("nope/nothing")
    check(r["error"]["code"] == -32601, "unknown method is a JSON-RPC error")
    s.close()

    # HTTP transport (headless --mcp-http)
    p = subprocess.Popen([exe, "--mcp-http", "--port", "18765", "--export", export], stdout=subprocess.PIPE,
                         stderr=subprocess.DEVNULL, text=True, env=editor_env(export))
    try:
        line = p.stdout.readline().strip()
        check(line.startswith("MCP http://127.0.0.1:"), "http server announces its url: " + line)
        url = line[4:]

        def post(body, headers=None):
            h = {"Content-Type": "application/json", "Accept": "application/json, text/event-stream"}
            h.update(headers or {})
            req = urllib.request.Request(url, data=json.dumps(body).encode(), headers=h, method="POST")
            try:
                with urllib.request.urlopen(req, timeout=10) as r:
                    return r.status, r.read().decode()
            except urllib.error.HTTPError as e:
                return e.code, e.read().decode()

        st, body = post({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {"protocolVersion": "2025-03-26"}})
        check(st == 200 and json.loads(body)["result"]["serverInfo"]["name"] == "project-dam-editor", "http initialize")
        st, body = post({"jsonrpc": "2.0", "method": "notifications/initialized"})
        check(st == 202 and body == "", "http notification is 202 with no body")
        st, body = post({"jsonrpc": "2.0", "id": 2, "method": "tools/list"})
        check(st == 200 and len(json.loads(body)["result"]["tools"]) >= 9, "http tools/list")
        st, body = post({"jsonrpc": "2.0", "id": 3, "method": "tools/call", "params": {"name": "level_describe", "arguments": {}}})
        check(st == 200 and "bunker1" in body, "http tools/call level_describe")
        st, body = post({"jsonrpc": "2.0", "id": 4, "method": "ping"}, {"Origin": "https://evil.example"})
        check(st == 403, "http rejects a foreign Origin")
        st, body = post({"jsonrpc": "2.0", "id": 5, "method": "ping"}, {"Origin": "http://localhost:3000"})
        check(st == 200, "http accepts a localhost Origin")
    finally:
        p.kill()
        p.wait(timeout=10)

    transform_checks(exe, export, work)
    duplicate_checks(exe, export, work)
    mod_checks(exe, export, work)
    validator_checks(exe, export, work)
    behavior_checks(exe, export, work)
    presence_checks(exe, export, work)
    if "--gui" in sys.argv:
        gui_capture_check(exe, export, work)

    print("%d failure(s)" % len(failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
