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
    check(not err and d["added"]["kind"] == "guard" and d["added"]["op"] == 0 and len(d["uuid"]) == 36 and d["uuid"] == d["added"]["uuid"],
          "duplicate an original guard (it gets a fresh uuid): " + json.dumps(d))
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


def noid(ref):
    """An item ref without its uuid / name (an added item carries both)."""
    return {k: v for k, v in (ref or {}).items() if k not in ("uuid", "name")}


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
    check(len(guard_err) == 1 and noid(guard_err[0].get("item")) == {"kind": "guard", "op": 2},
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
    check(any("script 3" in t and ("does not exist" in t or "not a prop of the level" in t) for t in texts), "validator: a script on a missing prop")
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
    check(not err and noid(d["guard"]) == {"kind": "guard", "op": op}, "set_guard_behavior on an added guard")
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


def script_checks(exe, export, work):
    """Scripts and zones (#105): the entries, the Props schema, the type checker, mission scripts on disk, zones."""
    mpath = os.path.join(work, "smoke_scripts", "mission.json")
    if os.path.isdir(os.path.dirname(mpath)):
        import shutil
        shutil.rmtree(os.path.dirname(mpath))
    s = Stdio(exe, export)
    s.call("initialize", {"protocolVersion": "2025-06-18"})
    names = [t["name"] for t in s.call("tools/list")["result"]["tools"]]
    for want in ("script_list", "script_read", "script_write", "script_copy", "script_attach", "set_props", "script_remove",
                 "zone_add", "zone_set", "zone_remove"):
        check(want in names, "tools/list has " + want)
    s.tool("mission_open", new=True, level="bunker1", name="Script test")
    err, d = s.tool("mission_save", path=mpath)
    check(not err, "scripts: mission saved")

    # the library and its Props schemas (parsed with the Luau parser)
    err, d = s.tool("script_list")
    lib = {x["name"]: x["props_schema"] for x in d["library"]}
    kd = lib.get("key_door", {}).get("fields", [])
    check([(f["name"], f["type"], f["optional"]) for f in kd] ==
          [("key", "Item", False), ("message", "string", True), ("objective", "Objective", True)],
          "script_list: key_door's Props: " + json.dumps(kd))

    # zones: add, set, undo, remove
    err, d = s.tool("zone_add", pos=[300, 20, 300], radius=100)
    check(not err and d["id"] == "z1" and d["radius"] == 100 and d["room"] >= 0, "zone_add: z1 with the room at its centre: " + json.dumps(d))
    err, d = s.tool("zone_add", pos=[100, 20, 100])
    check(not err and d["id"] == "z2" and d["radius"] == 150, "zone_add: the next id is z2, radius defaults to 150")
    err, d = s.tool("zone_set", id="z1", radius=220, pos=[310, 20, 300])
    check(not err and d["radius"] == 220 and d["pos"][0] == 310, "zone_set: radius and pos")
    err, d = s.tool("zone_set", id="nope", radius=5)
    check(err, "zone_set: an unknown zone is an error")
    err, d = s.tool("zone_add", pos=[0, 0, 0], radius=-1)
    check(err, "zone_add: a radius must be positive")
    s.tool("zone_remove", id="z2")
    err, d = s.tool("zone_remove", id="z2")
    check(err, "zone_remove: removing twice is an error")

    # an entry: a library script on a prop target, typed props
    err, d = s.tool("script_attach", use="key_door", target={"kind": "prop", "prop": 0}, props={"key": 33, "message": "Locked"})
    check(not err and d["index"] == 0 and d["props"] == {"key": 33, "message": "Locked"} and d["origin"] == "library", "script_attach: key_door on prop 0: " + json.dumps(d))
    err, d = s.tool("script_attach", use="nope", target={"kind": "mission"})
    check(err, "script_attach: an unknown script is an error")
    err, d = s.tool("script_attach", use="give_item", target={"kind": "zone", "zone": "z1"}, props={"item": 8})
    check(not err and d["index"] == 1, "script_attach: give_item on zone z1")
    err, d = s.tool("set_props", index=0, props={"objective": "o1"}, unset=["message"])
    check(not err and d["props"] == {"key": 33, "objective": "o1"}, "set_props: merge + unset: " + json.dumps(d))
    err, d = s.tool("validate")
    texts = " | ".join(i["text"] for i in d["issues"])
    check("objective 'o1' is not in the mission's objectives" in texts, "validate: an objective that does not exist is reported: " + texts)
    s.tool("set_props", index=0, unset=["objective"])
    err, d = s.tool("validate")
    check(not any(i.get("script") for i in d["issues"] if i["severity"] == "error"), "validate: the library scripts type-check clean: " + json.dumps(d["issues"]))
    # a required property that is missing
    err, d = s.tool("set_props", index=0, unset=["key"])
    err, d = s.tool("validate")
    check(any("property 'key' (Item) is not set" in i["text"] and i["severity"] == "error" for i in d["issues"]), "validate: a required property left empty is an error")
    s.tool("set_props", index=0, props={"key": 33})

    # a mission script: source, type errors on their line, saved next to mission.json
    good = "export type Props = { item: Item, message: string? }\nlocal n: number = props.item\ngive(n)\nif props.message then\n    message(props.message)\nend\n"
    err, d = s.tool("script_write", use="giver", text=good)
    check(not err and d["diagnostics"] == [] and [f["name"] for f in d["props_schema"]["fields"]] == ["item", "message"],
          "script_write: a clean script has no diagnostics: " + json.dumps(d))
    bad = good.replace("give(n)", 'give("x")')
    err, d = s.tool("script_write", use="giver", text=bad)
    check(not err and len(d["diagnostics"]) == 1 and d["diagnostics"][0]["line"] == 3 and d["diagnostics"][0]["severity"] == "error",
          "script_write: give(\"x\") is an error on line 3: " + json.dumps(d["diagnostics"]))
    err, d = s.tool("script_attach", use="giver", target={"kind": "mission"}, props={"item": 8})
    check(not err, "script_attach: the mission's own script")
    err, d = s.tool("validate")
    mine = [i for i in d["issues"] if i.get("script") == "giver"]
    check(len(mine) == 1 and mine[0]["line"] == 3 and mine[0]["severity"] == "error" and not d["valid"],
          "validate: the type error carries the script name and line and makes the mission invalid: " + json.dumps(mine))
    err, d = s.tool("script_write", use="giver", text=bad.replace('give("x")', "give(n)"))
    err, d = s.tool("validate")
    check(d["valid"], "validate: reverted, valid again: " + json.dumps(d["issues"]))
    err, d = s.tool("script_write", use="key_door", text="x")
    check(err, "script_write: a library script is read-only")
    err, d = s.tool("script_copy", use="key_door")
    check(not err and d["use"] == "key_door", "script_copy: key_door -> the mission's own copy")
    err, d = s.tool("script_read", use="key_door")
    check(not err and d["origin"] == "mission" and "export type Props" in d["text"], "script_read: the copy is the mission's own now")
    err, d = s.tool("script_write", use="bad name!", text="x")
    check(err, "script_write: a script name has no spaces or punctuation")
    err, d = s.tool("mission_save")
    check(not err, "scripts: mission_save")
    on_disk = os.path.join(os.path.dirname(mpath), "scripts", "giver.luau")
    check(os.path.isfile(on_disk) and "give(n)" in open(on_disk).read(), "scripts: giver.luau is saved next to mission.json")
    check(os.path.isfile(os.path.join(os.path.dirname(mpath), "scripts", "key_door.luau")), "scripts: the copy of key_door is saved too")
    with open(mpath) as f:
        mj = json.load(f)
    check(len(mj["scripts"]) == 3 and mj["scripts"][0]["target"] == {"kind": "prop", "prop": 0} and mj["zones"][0]["id"] == "z1" and mj["zones"][0]["radius"] == 220,
          "scripts: mission.json holds the entries and zones: " + json.dumps(mj["scripts"]))

    # undo walks back through the entries
    err, d = s.tool("script_remove", index=2)
    check(not err and d["remaining"] == 2, "script_remove")
    err, d = s.tool("undo")
    err, d = s.tool("script_list")
    check(len(d["entries"]) == 3, "undo restores a removed entry")
    s.close()


def uuid_checks(exe, export, work):
    """Props a mission adds are referenced by a stable uuid (scripts: Door / Guard / Prop properties, a prop target)."""
    mpath = os.path.join(work, "smoke_uuid", "mission.json")
    if os.path.isdir(os.path.dirname(mpath)):
        import shutil
        shutil.rmtree(os.path.dirname(mpath))
    s = Stdio(exe, export)
    s.call("initialize", {"protocolVersion": "2025-06-18"})
    s.tool("mission_open", new=True, level="bunker1", name="Uuid test")
    err, d = s.tool("mission_save", path=mpath)
    check(not err, "uuid: mission saved")

    err, d = s.tool("place_guard", pad=4, model=3)
    guard_uuid = d.get("uuid", "")
    check(not err and len(guard_uuid) == 36 and d["added"]["uuid"] == guard_uuid and guard_uuid[14] == "4",
          "place_guard without a uuid: the editor makes a v4 one and returns it: " + json.dumps(d))
    fixed = "6f1c2a9e-3b7d-4e55-9a10-5d2c8b7e4f01"
    err, d = s.tool("place_object", type=3, model=120, pad=4, uuid=fixed, name="Crate")
    check(not err and d["uuid"] == fixed and d["added"].get("name") == "Crate", "place_object with a uuid keeps it (and the name): " + json.dumps(d))
    err, d = s.tool("place_object", type=3, model=120, pad=4, uuid=fixed)
    check(err, "place_object: a uuid already in use is refused")
    err, d = s.tool("place_object", type=3, model=120, pad=4, uuid="not-a-uuid")
    check(err, "place_object: a malformed uuid is refused")
    err, d = s.tool("duplicate", from_uuid=fixed, name="Crate 2")
    check(not err and d["uuid"] != fixed and len(d["uuid"]) == 36 and d["added"]["name"] == "Crate 2", "duplicate: a new uuid and the given name: " + json.dumps(d))
    err, d = s.tool("move", uuid=fixed, pad=5)
    check(not err and d["moved"]["uuid"] == fixed, "move: an added item is addressed by its uuid")

    # item listing and script_list show them
    err, d = s.tool("level_describe", detail="items")
    mine = [i for i in d["items"] if i.get("added")]
    check(len(mine) == 3 and all(len(i["uuid"]) == 36 for i in mine) and any(i.get("name") == "Crate" for i in mine), "level_describe items: uuid + name of added items")
    err, d = s.tool("script_list")
    check(any(x["uuid"] == fixed and x["name"] == "Crate" and x["kind"] == "object" for x in d["added_props"]) and len(d["added_props"]) == 3,
          "script_list: added_props lists uuid and name: " + json.dumps(d["added_props"]))

    # a script entry whose target and properties name added props by uuid
    err, d = s.tool("script_attach", use="key_door", target={"kind": "prop", "uuid": guard_uuid}, props={"key": 33})
    check(not err and d["target"] == {"kind": "prop", "uuid": guard_uuid}, "script_attach: a prop target by uuid: " + json.dumps(d))
    s.tool("script_write", use="needs_props", text=(
        "export type Props = { door: Door, who: Guard, thing: Prop }\n"
        "local p = props :: Props\n"
        "door(p.door):open()\n"
        "guard(p.who):follow()\n"
        "wait(event('start'))\n"))
    err, d = s.tool("script_attach", use="needs_props", target={"kind": "mission"}, props={"door": 5, "who": guard_uuid, "thing": fixed})
    idx = d["index"]
    check(not err and d["props"]["who"] == guard_uuid, "script_attach: Guard / Prop properties take a uuid string")
    err, d = s.tool("validate")
    texts = " | ".join(i["text"] for i in d["issues"] if i["severity"] == "error")
    check(not any("uuid" in i["text"] or "not a prop of the level" in i["text"] for i in d["issues"]), "validate: props by uuid are accepted: " + texts)

    # a stale uuid, the wrong kind, and a number that designates an added prop are reported
    s.tool("set_props", index=idx, props={"who": "11111111-1111-4111-8111-111111111111"})
    err, d = s.tool("validate")
    texts = " | ".join(i["text"] for i in d["issues"] if i["severity"] == "error")
    check(not d["valid"] and "no prop the mission adds has uuid '11111111-1111-4111-8111-111111111111'" in texts, "validate: a stale uuid is an error: " + texts)
    s.tool("set_props", index=idx, props={"who": fixed})
    err, d = s.tool("validate")
    texts = " | ".join(i["text"] for i in d["issues"] if i["severity"] == "error")
    check("is not a guard" in texts, "validate: a Guard property naming an object's uuid is an error: " + texts)
    s.tool("set_props", index=idx, props={"who": guard_uuid, "thing": guard_uuid})
    err, d = s.tool("validate")
    texts = " | ".join(i["text"] for i in d["issues"] if i["severity"] == "error")
    check("is a guard, not an object" in texts, "validate: a Prop property naming a guard's uuid is an error: " + texts)
    s.tool("set_props", index=idx, props={"thing": fixed, "door": 100000})
    err, d = s.tool("validate")
    texts = " | ".join(i["text"] for i in d["issues"] if i["severity"] == "error")
    check("not a prop of the level" in texts and "referenced by its uuid" in texts, "validate: a number past the level's props is an error: " + texts)
    s.tool("set_props", index=idx, props={"door": 5})

    # remove the object: its uuid goes stale in the script
    err, d = s.tool("remove", uuid=fixed)
    check(not err, "remove: an added item by uuid")
    err, d = s.tool("validate")
    texts = " | ".join(i["text"] for i in d["issues"] if i["severity"] == "error")
    check("no prop the mission adds has uuid '%s'" % fixed in texts, "validate: the uuid of a deleted prop is stale: " + texts)
    err, d = s.tool("script_attach", use="key_door", target={"kind": "prop", "uuid": "11111111-1111-4111-8111-111111111111"}, props={"key": 1})
    check(err, "script_attach: a target uuid that is no added prop is refused")

    # on disk: add ops carry uuid + name; the script entries carry the uuids
    s.tool("undo")
    err, d = s.tool("mission_save")
    with open(mpath) as fh:
        mj = json.load(fh)
    adds = [g for g in mj["guards"] + mj["objects"] if g["op"] == "add"]
    check(len(adds) == 3 and all(len(a["uuid"]) == 36 for a in adds) and any(a.get("name") == "Crate" for a in adds), "uuid: mission.json has uuid + name on every add op: " + json.dumps(adds)[:200])
    check(mj["scripts"][0]["target"] == {"kind": "prop", "uuid": guard_uuid} and mj["scripts"][1]["props"]["who"] == guard_uuid, "uuid: the script entries hold the uuids")
    s.close()

    # the Formula sample validates with no error through the editor (stdio MCP): needs the facility level export
    # (the game exports it into its export cache; skipped when there is no game / ROM here)
    sample = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), "docs", "modding", "samples", "formula")
    if os.path.isdir(sample):
        s = Stdio(exe, export)
        s.call("initialize", {"protocolVersion": "2025-06-18"})
        err, d = s.tool("level_open", level="facility")
        if not err:
            err, d = s.tool("mod_open", dir=sample, mission=0)
        if err:
            print("  skip  formula sample validation: " + str(d)[:140])
        else:
            err, d = s.tool("validate")
            errs = [i["text"] for i in d["issues"] if i["severity"] == "error"]
            check(not errs, "the Formula sample validates with 0 errors: " + json.dumps(errs))
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
    # .pmod packages: a new mod has a .gitignore with dist/; mod_publish / mod_deploy write a ustar tar of the folder
    check(open(os.path.join(moddir, ".gitignore")).read().strip() == "dist/", "a new mod gets .gitignore with dist/")
    import tarfile
    err, d = s.tool("mod_publish")
    check(not err and d["path"].replace("\\", "/").endswith("/dist/smokemod.pmod") and os.path.isfile(d["path"]) and d["bytes"] == os.path.getsize(d["path"]),
          "mod_publish writes <mod>/dist/<mod>.pmod: " + json.dumps(d))
    if not err:
        with tarfile.open(d["path"]) as tf:
            names = sorted(m.name for m in tf.getmembers())
            check("campaign.json" in names and "first/mission.json" in names and "second/mission.json" in names, "the package lists the mod's files: " + ",".join(names))
            check(not any(n.startswith("dist") or n.startswith(".") for n in names), "the package leaves out dist/ and dotfiles")
            check(all(m.isfile() for m in tf.getmembers()), "the package holds regular files only")
            check(tf.extractfile("campaign.json").read() == open(os.path.join(moddir, "campaign.json"), "rb").read(), "campaign.json is byte-identical in the package")
    out2 = os.path.join(work, "pubout")
    err, d = s.tool("mod_publish", out_dir=out2)
    check(not err and os.path.isfile(os.path.join(out2, "smokemod.pmod")), "mod_publish out_dir")
    err, d = s.tool("mod_deploy")
    check(not err and d["path"].replace("\\", "/").endswith("/mods/smokemod.pmod") and os.path.isfile(d["path"]), "mod_deploy writes <mods>/<mod>.pmod: " + json.dumps(d))
    if not err:
        os.remove(d["path"])
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


def authoring_checks(exe, export, work):
    """Whole-mission authoring: metadata, objectives, record fields on place_*, links / names, script_check, test_run args."""
    mpath = os.path.join(work, "smoke_authoring.json")
    if os.path.exists(mpath):
        os.remove(mpath)
    s = Stdio(exe, export)
    s.call("initialize", {"protocolVersion": "2025-06-18"})
    tools = s.call("tools/list")["result"]["tools"]
    names = [t["name"] for t in tools]
    for want in ("mission_set", "objective_add", "objective_set", "objective_remove", "script_check"):
        check(want in names, "tools/list has " + want)
    tr = [t for t in tools if t["name"] == "test_run"][0]
    check("wait_seconds" in tr["inputSchema"]["properties"] and "stop" in tr["inputSchema"]["properties"], "test_run takes wait_seconds and stop")
    s.tool("mission_open", new=True, level="bunker1", name="Authoring test")

    # metadata
    err, d = s.tool("mission_set", name="Formula", briefing="Win the race.", description="A test")
    check(not err and d["name"] == "Formula" and d["briefing"] == "Win the race.", "mission_set: " + json.dumps(d))
    err, d = s.tool("mission_set")
    check(err, "mission_set with nothing is an error")
    err, d = s.tool("mission_set", name="x" * 600)
    check(err, "mission_set: a name that does not fit is refused")

    # objectives
    err, d = s.tool("objective_add", id="finish", text="Cross the line", difficulty=1)
    check(not err and d["objectives"][0]["id"] == "finish" and d["objectives"][0]["difficulty"] == 1, "objective_add: " + json.dumps(d))
    err, d = s.tool("objective_add", id="finish", text="again")
    check(err, "objective_add refuses a duplicate id")
    err, d = s.tool("objective_add", id="bad id", text="x")
    check(err, "objective_add refuses a bad id")
    s.tool("objective_add", id="second", text="Second")
    s.tool("objective_add", id="third", text="Third")
    err, d = s.tool("objective_add", id="d", text="x", difficulty=3)
    check(err, "objective_add: difficulty must be 0..2")
    err, d = s.tool("objective_set", id="third", index=0, text="First now", difficulty=2)
    check(not err and [o["id"] for o in d["objectives"]] == ["third", "finish", "second"] and d["objectives"][0]["text"] == "First now"
          and d["objectives"][0]["difficulty"] == 2, "objective_set: text, difficulty and reorder: " + json.dumps(d))
    err, d = s.tool("objective_set", id="second", new_id="finish")
    check(err, "objective_set refuses a duplicate new_id")
    err, d = s.tool("objective_set", id="second", new_id="renamed")
    check(not err and d["objectives"][2]["id"] == "renamed", "objective_set: new_id")
    err, d = s.tool("objective_set", id="nope", text="x")
    check(err, "objective_set: unknown id")
    err, d = s.tool("objective_set", id="third", index=9)
    check(err, "objective_set: index out of range")
    s.tool("undo")   # the rename
    err, d = s.tool("level_describe", detail="mission")
    check([o["id"] for o in d["mission"]["json"]["objectives"]] == ["third", "finish", "second"], "objective_set is one undo step")
    err, d = s.tool("objective_remove", id="second")
    check(not err and [o["id"] for o in d["objectives"]] == ["third", "finish"], "objective_remove")
    err, d = s.tool("objective_remove", id="second")
    check(err, "objective_remove: unknown id")

    # record fields on place_*
    err, d = s.tool("place_guard", pad=4, model=3, head=50, health=120, reaction_time=40, bitflags=3, difficulties=[1, 2],
                    weapons=[{"item": 17}, {"item": 4, "flags": 16384}])
    check(not err, "place_guard with record fields, weapons and difficulties: " + json.dumps(d))
    op = d["added"]["op"] if not err else 0
    err, d = s.tool("place_guard", pad=4, model=3, healthh=5)
    check(err and "health" in d and "reaction_time" in d, "place_guard: an unknown field is refused with the valid names: " + str(d)[:160])
    err, d = s.tool("place_guard", pad=4, model=3, health=70000)
    check(err, "place_guard: a value out of range is refused")
    err, d = s.tool("place_guard", pad=4, model=3, weapon=4, weapons=[{"item": 4}])
    check(err, "place_guard: weapon and weapons together are refused")
    err, d = s.tool("place_object", type=3, model=120, pad=4, flags=16, maxdamage=5.5)
    check(not err, "place_object with header fields: " + json.dumps(d))
    err, d = s.tool("place_object", type=3, model=120, pad=4, nonsense=1)
    check(err and "flags" in d, "place_object: an unknown field is refused with the valid names")
    err, d = s.tool("place_object", type=4, model=120, pad=4, key={"keyflags": 1, "bogus": 2})
    check(err and "key.bogus" in d, "place_object: an unknown sub-object field is refused")
    err, d = s.tool("level_describe", detail="props")
    g = item_of(d, "guard", "op", op)
    check(g and g["fields"]["head"] == 50 and g["fields"]["health"] == 120 and g["fields"]["bitflags"] == 3
          and len(g["weapons"]) == 2 and g["weapons"][1]["item"] == 4, "the placed guard carries its fields and weapons: " + json.dumps(g))
    check(g and g.get("head_name") == "CheadbZ" and g.get("body_name"), "props names the guard's body and head: " + json.dumps(g))
    err, d = s.tool("mission_save", path=mpath)
    saved = json.load(open(mpath))
    ad = [x for x in saved["guards"] if x["op"] == "add"][0]
    check(ad["head"] == 50 and ad["health"] == 120 and ad["difficulties"] == [1, 2] and ad["weapons"][1]["flags"] == 16384,
          "saved: the add op has the fields, difficulties and weapons: " + json.dumps(ad))
    ob = [x for x in saved["objects"] if x["op"] == "add"][0]
    check(ob["flags"] == 16, "saved: the object's header field")

    # undo takes a placed guard with its fields in one step
    n = len(saved["guards"])
    s.tool("place_guard", pad=4, model=3, health=10)
    s.tool("undo")
    s.tool("mission_save", path=mpath)
    check(len(json.load(open(mpath))["guards"]) == n, "undo takes a placed guard with its fields in one step")

    # links and names
    err, d = s.tool("level_describe", detail="links")
    check(not err and isinstance(d["links"], list) and isinstance(d["tags"], list) and isinstance(d["doors"], list), "level_describe links: " + str(d)[:200])
    err, d = s.tool("level_describe", detail="names")
    bodies = {b["id"]: b["file"] for b in d["bodies"]}
    heads = {h["id"]: h["file"] for h in d["heads"]}
    check(not err and bodies.get(33) == "CcardimanZ" and heads.get(50) == "CheadbZ" and any(i["name"] == "KNIFE" for i in d["items"]),
          "level_describe names: 33 = CcardimanZ, 50 = CheadbZ, item names")

    # script_check on a draft: nothing is written
    err, d = s.tool("script_check", text="local x: number = 'a'\n")
    check(not err and d["errors"] >= 1 and d["diagnostics"][0]["line"] == 1, "script_check reports a type error in a draft: " + json.dumps(d)[:200])
    err, d = s.tool("script_check", text="local x = 1\n", use="draft", target_kind="zone")
    check(not err and d["errors"] == 0 and d["target_kind"] == "zone", "script_check: a clean draft: " + json.dumps(d)[:200])
    err, d = s.tool("script_check", text="x", target_kind="bogus")
    check(err, "script_check: a bad target_kind is refused")
    err, d = s.tool("script_list")
    check(not d["mission_scripts"], "script_check wrote no script")

    # test_run with wait_seconds opens the real game window for a second: only with GE_SMOKE_RUN_GAME=1, else skipped
    if not os.environ.get("GE_SMOKE_RUN_GAME"):
        print("skip  test_run wait_seconds (set GE_SMOKE_RUN_GAME=1 to start the game)")
    else:
        err, d = s.tool("test_run", wait_seconds=1, stop=True)
        if err:
            print("skip  test_run wait_seconds (no game to run here): " + str(d)[:100])
        else:
            check("running" in d and "missions" in d, "test_run with wait_seconds returns running and the missions log lines: " + json.dumps(d)[:200])
    s.close()


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
    script_checks(exe, export, work)
    uuid_checks(exe, export, work)
    authoring_checks(exe, export, work)
    if "--gui" in sys.argv:
        gui_capture_check(exe, export, work)

    print("%d failure(s)" % len(failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
