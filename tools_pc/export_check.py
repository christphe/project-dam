#!/usr/bin/env python3
"""Validate a ge007-export output directory (issue #150). Stdlib only.

usage: export_check.py <out_dir>

<out_dir>/models and <out_dir>/characters are checked as their own indexes (models.json, characters.json).
For every <out_dir>/<level>/<level>.gltf: the JSON parses; every bufferView and
accessor fits the .bin; index values are below the vertex count; POSITION
min/max match the data; every image file exists and is a valid PNG; every
referenced material / texture / sampler / image exists. Prints rooms /
primitives / triangles / textures per level; exits non-zero on any error.

For every <out_dir>/<level>/setup.json and setup_mp.json (docs/specs/setup.md): the
schema (format, version, header, pads, props, ...), every record's `raw` has the size
of its type, every link / pad / waypoint / tag reference resolves, and every AI list's
byte range lies inside the file.
"""
import json
import os
import struct
import sys
import zlib

CT = {5120: ("b", 1), 5121: ("B", 1), 5122: ("h", 2), 5123: ("H", 2), 5125: ("I", 4), 5126: ("f", 4)}
NC = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}


def check_png(path):
    with open(path, "rb") as f:
        d = f.read()
    if d[:8] != b"\x89PNG\r\n\x1a\n":
        return "bad signature"
    p = 8
    idat = b""
    ihdr = None
    seen_end = False
    while p + 8 <= len(d):
        n, typ = struct.unpack(">I4s", d[p:p + 8])
        body = d[p + 8:p + 8 + n]
        crc = struct.unpack(">I", d[p + 8 + n:p + 12 + n])[0]
        if zlib.crc32(typ + body) & 0xFFFFFFFF != crc:
            return "bad crc in " + typ.decode("latin1")
        if typ == b"IHDR":
            ihdr = struct.unpack(">IIBBBBB", body)
        elif typ == b"IDAT":
            idat += body
        elif typ == b"IEND":
            seen_end = True
            break
        p += 12 + n
    if not ihdr or not seen_end:
        return "missing IHDR/IEND"
    w, h, bd, ct = ihdr[0], ihdr[1], ihdr[2], ihdr[3]
    if w <= 0 or h <= 0:
        return "zero size"
    try:
        raw = zlib.decompress(idat)
    except zlib.error as e:
        return "idat: %s" % e
    if bd == 8 and ct == 6 and len(raw) != h * (1 + 4 * w):
        return "idat size %d != %d" % (len(raw), h * (1 + 4 * w))
    return None


COLL = {}   # level -> (tiles, rooms, triangles) of the collision floor, for the summary


def check_collision(g, accs, views, binb, name):
    """The collision floor (docs/specs/stan.md): a hidden `collision` node (scene 1 only) with one child per room."""
    errs = []
    ce = g.get("asset", {}).get("extras", {}).get("collision")
    if ce is None:
        return errs   # a level without a stan file is allowed (the report says why)
    scale = g["asset"]["extras"].get("level_scale", 1.0)
    scenes = g.get("scenes", [])
    nodes = g.get("nodes", [])
    if g["asset"]["extras"].get("collision_scene") != 1 or len(scenes) < 2:
        errs.append("collision: asset.extras.collision_scene must be 1 and scenes[1] must exist")
        return errs
    roots = [i for i, n in enumerate(nodes) if n.get("name") == "collision"]
    if len(roots) != 1:
        errs.append("collision: expected one node named collision, found %d" % len(roots))
        return errs
    if scenes[1].get("nodes") != roots:
        errs.append("collision: scenes[1] must contain exactly the collision node")
    if roots[0] in scenes[0].get("nodes", []):
        errs.append("collision: the collision node is in the default scene")
    tiles = {}
    tri_total = 0
    mats = g.get("materials", [])
    for ci in nodes[roots[0]].get("children", []):
        node = nodes[ci]
        room = (node.get("extras") or {}).get("room")
        if room is None or "mesh" not in node or node.get("name") != "collision_room_%d" % room:
            errs.append("collision: bad child node %d" % ci)
            continue
        prims = g["meshes"][node["mesh"]]["primitives"]
        if len(prims) != 1:
            errs.append("collision room %d: expected one primitive" % room)
            continue
        p = prims[0]
        if list(p["attributes"].keys()) != ["POSITION"]:
            errs.append("collision room %d: only POSITION expected" % room)
        if p.get("material", -1) >= len(mats) or mats[p.get("material", 0)].get("name") != "collision":
            errs.append("collision room %d: material is not the collision material" % room)
        pa = accs[p["attributes"]["POSITION"]]
        ia = accs[p["indices"]]
        fmt, sz = CT[ia["componentType"]]
        v = views[ia["bufferView"]]
        off = v["byteOffset"] + ia.get("byteOffset", 0)
        idx = struct.unpack("<%d%s" % (ia["count"], fmt), binb[off:off + ia["count"] * sz])
        if ia["count"] % 3 or (idx and max(idx) >= pa["count"]):
            errs.append("collision room %d: bad indices" % room)
        vv = views[pa["bufferView"]]
        po = vv["byteOffset"] + pa.get("byteOffset", 0)
        pos = struct.unpack("<%df" % (pa["count"] * 3), binb[po:po + pa["count"] * 12])
        tri_total += ia["count"] // 3
        tl = p.get("extras", {}).get("tiles", [])
        vi = 0
        tri = 0
        for t in tl:
            n = t["point_count"]
            if not (len(t["points"]) == len(t["links"]) == len(t["neighbour"]) == n):
                errs.append("collision tile %s: array lengths" % t.get("index"))
                continue
            if t["room"] != room:
                errs.append("collision tile %s: room %d in room %d's mesh" % (t["index"], t["room"], room))
            if t["index"] in tiles:
                errs.append("collision tile %s: duplicate index" % t["index"])
            tiles[t["index"]] = t
            if t["first_triangle"] != tri:
                errs.append("collision tile %d: first_triangle %d != %d" % (t["index"], t["first_triangle"], tri))
            if n >= 3:
                for j, pt in enumerate(t["points"]):
                    for c in range(3):
                        if abs(pos[(vi + j) * 3 + c] - pt[c] / scale) > 1e-4 * max(1.0, abs(pt[c] / scale)):
                            errs.append("collision tile %d: point %d does not match POSITION" % (t["index"], j))
                            break
                    else:
                        continue
                    break
                if idx[tri * 3:tri * 3 + 3 * (n - 2)] != tuple(x for j in range(1, n - 1) for x in (vi, vi + j, vi + j + 1)):
                    errs.append("collision tile %d: not a fan from point 0" % t["index"])
                vi += n
                tri += n - 2
        if vi != pa["count"] or tri * 3 != ia["count"]:
            errs.append("collision room %d: tiles do not account for all vertices / triangles" % room)
    if sorted(tiles) != list(range(len(tiles))) or len(tiles) != ce["tiles"]:
        errs.append("collision: tile indices are not 0..%d (asset says %d)" % (len(tiles) - 1, ce["tiles"]))
    nb_bad = 0
    for t in tiles.values():
        for L, nb in zip(t["links"], t["neighbour"]):
            if (L >> 4 == 0) != (nb == -1) or nb >= len(tiles):
                nb_bad += 1
    if nb_bad:
        errs.append("collision: %d edges whose link and neighbour disagree (or point outside the tile list)" % nb_bad)
    if ce.get("unresolved_links"):
        errs.append("collision: %d links do not land on a tile" % ce["unresolved_links"])
    if ce.get("rooms") != len(nodes[roots[0]].get("children", [])):
        errs.append("collision: asset room count != children")
    COLL[name] = (len(tiles), len(nodes[roots[0]].get("children", [])), tri_total)
    return errs


def check_level(root, name, report=True):
    errs = []
    d = os.path.join(root, name)
    gp = os.path.join(d, name + ".gltf")
    try:
        with open(gp, encoding="utf-8") as f:
            g = json.load(f)
    except Exception as e:  # noqa: BLE001
        return ["gltf: %s" % e], None
    if g.get("asset", {}).get("version") != "2.0":
        errs.append("asset.version")
    bp = os.path.join(d, name + ".bin")
    try:
        with open(bp, "rb") as f:
            binb = f.read()
    except OSError as e:
        return ["bin: %s" % e], None
    bufs = g.get("buffers", [])
    if g.get("bufferViews") and (len(bufs) != 1 or bufs[0].get("byteLength") != len(binb)):
        errs.append("buffer byteLength != .bin size")
    views = g.get("bufferViews", [])
    for i, v in enumerate(views):
        if v["byteOffset"] + v["byteLength"] > len(binb):
            errs.append("bufferView %d outside .bin" % i)
    accs = g.get("accessors", [])
    nverts = {}
    for i, a in enumerate(accs):
        ct = a["componentType"]
        if ct not in CT or a["type"] not in NC:
            errs.append("accessor %d: type" % i)
            continue
        fmt, sz = CT[ct]
        n = NC[a["type"]]
        v = views[a["bufferView"]]
        off = v["byteOffset"] + a.get("byteOffset", 0)
        if off % sz:
            errs.append("accessor %d: misaligned" % i)
        if a.get("byteOffset", 0) + a["count"] * n * sz > v["byteLength"]:
            errs.append("accessor %d does not fit its view" % i)
            continue
        nverts[i] = a["count"]
    nmat = len(g.get("materials", []))
    ntex = len(g.get("textures", []))
    nimg = len(g.get("images", []))
    nsmp = len(g.get("samplers", []))
    ntri = 0
    nprim = 0
    for m in g.get("meshes", []):
        if m.get("name", "").startswith("collision_room_"):
            continue   # the collision floor: check_collision below
        for p in m["primitives"]:
            nprim += 1
            pa = p["attributes"]
            pos = accs[pa["POSITION"]]
            cnt = pos["count"]
            for k, ai in pa.items():
                if accs[ai]["count"] != cnt:
                    errs.append("%s: attribute %s count mismatch" % (m.get("name"), k))
            if p.get("material", 0) >= nmat or "material" not in p:
                errs.append("%s: bad material" % m.get("name"))
            ca = accs[pa["COLOR_0"]]
            if ca["componentType"] != 5123 or not ca.get("normalized") or ca["type"] != "VEC4":
                errs.append("%s: COLOR_0 must be normalized UNSIGNED_SHORT VEC4" % m.get("name"))
            ia = accs[p["indices"]]
            fmt, sz = CT[ia["componentType"]]
            v = views[ia["bufferView"]]
            off = v["byteOffset"] + ia.get("byteOffset", 0)
            idx = struct.unpack("<%d%s" % (ia["count"], fmt), binb[off:off + ia["count"] * sz])
            if idx and max(idx) >= cnt:
                errs.append("%s: index %d >= %d vertices" % (m.get("name"), max(idx), cnt))
            if ia["count"] % 3:
                errs.append("%s: index count not a multiple of 3" % m.get("name"))
            ntri += ia["count"] // 3
            vv = views[pos["bufferView"]]
            po = vv["byteOffset"] + pos.get("byteOffset", 0)
            vals = struct.unpack("<%df" % (cnt * 3), binb[po:po + cnt * 12])
            for k in range(3):
                col = vals[k::3]
                if col and (abs(min(col) - pos["min"][k]) > 1e-4 * max(1, abs(min(col)))
                            or abs(max(col) - pos["max"][k]) > 1e-4 * max(1, abs(max(col)))):
                    errs.append("%s: POSITION min/max wrong on axis %d" % (m.get("name"), k))
    for i, t in enumerate(g.get("textures", [])):
        if t["source"] >= nimg or t.get("sampler", 0) >= nsmp:
            errs.append("texture %d: bad source/sampler" % i)
    for i, mt in enumerate(g.get("materials", [])):
        bc = mt.get("pbrMetallicRoughness", {}).get("baseColorTexture")
        if bc and bc["index"] >= ntex:
            errs.append("material %d: bad texture" % i)
    for node in g.get("nodes", []):
        if "mesh" in node and node["mesh"] >= len(g.get("meshes", [])):
            errs.append("node %s: bad mesh" % node.get("name"))
    for im in g.get("images", []):
        ip = os.path.join(d, im["uri"])
        if not os.path.isfile(ip):
            errs.append("image missing: %s" % im["uri"])
        else:
            e = check_png(ip)
            if e:
                errs.append("png %s: %s" % (im["uri"], e))
    if report and not os.path.isfile(os.path.join(d, "report.txt")):
        errs.append("report.txt missing")
    errs += check_collision(g, accs, views, binb, name)
    stats = (len([n for n in g.get("nodes", []) if not (n.get("extras") or {}).get("collision")]), nprim, ntri, nimg, nmat)
    return errs, stats


# record size by propDef type (docs/specs/setup.md); END (48) has no entry in props
SETUP_SIZES = {1: 256, 2: 8, 3: 128, 4: 132, 5: 128, 6: 236, 7: 132, 8: 136, 9: 28, 10: 256, 11: 596, 12: 128, 13: 216,
               14: 12, 17: 128, 18: 12, 19: 16, 20: 180, 21: 136, 22: 16, 23: 16, 24: 4, 25: 8, 26: 8, 27: 8, 28: 8,
               29: 8, 30: 16, 31: 4, 32: 16, 33: 20, 34: 12, 35: 16, 36: 128, 37: 40, 38: 16, 39: 176, 40: 180,
               42: 128, 43: 128, 44: 20, 45: 224, 46: 28, 47: 148}
SETUP_OBJ = {1, 3, 4, 5, 6, 7, 8, 10, 11, 12, 13, 17, 20, 21, 36, 39, 40, 42, 43, 45, 47}
SETUP_INTRO = {0: 12, 1: 16, 2: 16, 3: 32, 4: 8, 5: 8, 6: 40, 7: 12, 8: 8, 9: 4}
SETUP_COUNT = {}   # file path -> (pads, bound pads, props, ailists) for the summary


def check_setup(path):
    """Validates one setup.json; returns a list of error strings."""
    errs = []
    try:
        with open(path, encoding="utf-8") as f:
            j = json.load(f)
    except Exception as e:  # noqa: BLE001
        return ["setup: %s" % e]

    def err(m):
        if len(errs) < 40:
            errs.append(m)

    for k in ("format", "version", "level", "stage", "mode", "setup", "region", "file", "level_scale", "scale", "header",
              "pads", "bound_pads", "waypoint_names", "group_names", "props", "objectives", "briefing", "ailists",
              "waypoints", "waypoint_groups", "patrol_paths", "intro", "credits"):
        if k not in j:
            err("missing key %s" % k)
    if errs:
        return errs
    if j["format"] != "ge007-setup" or j["version"] != 1:
        err("format/version")
    if j["mode"] not in ("solo", "multi"):
        err("mode")
    fsize = j["file"]["size"]
    if len(j["header"]) != 10 or any(not (0 <= h <= fsize) for h in j["header"]):
        err("header words outside the file")
    if abs(j["scale"] * j["level_scale"] - 1.0) > 1e-5:
        err("scale * level_scale != 1")
    if len(j["file"]["sha1"]) != 40:
        err("file.sha1")
    npads, nb = len(j["pads"]), len(j["bound_pads"])
    for i, p in enumerate(j["pads"]):
        if p["num"] != i:
            err("pad %d: num %d" % (i, p["num"]))
    for i, p in enumerate(j["bound_pads"]):
        if p["num"] != 10000 + i or "bbox" not in p or len(p["file_bbox"]) != 6:
            err("bound pad %d: num/bbox" % i)
    for p in j["pads"] + j["bound_pads"]:
        for k in ("pos", "file_pos", "up", "look"):
            if len(p[k]) != 3:
                err("pad %d: %s" % (p["num"], k))
        if p["stan_id"] is not None and not (0 <= p["stan_id"] <= 0xFFFFFF):
            err("pad %d: stan_id" % p["num"])
        if (p["tile"] == "") != (p["stan_id"] is None):
            err("pad %d: tile/stan_id" % p["num"])
    for k in ("waypoint_names", "group_names"):
        if j[k] is not None and not all(isinstance(x, str) for x in j[k]):
            err(k)
    if j["waypoint_names"] is not None and len(j["waypoint_names"]) != len(j["waypoints"]):
        err("waypoint_names length != waypoints")
    if j["group_names"] is not None and len(j["group_names"]) != len(j["waypoint_groups"]):
        err("group_names length != waypoint_groups")

    def pad_ok(v, plain_only=False):
        if v < 10000:
            return 0 <= v < npads
        return not plain_only and 10000 <= v < 10000 + nb

    props = j["props"]
    n = len(props)

    def ref_ok(v):
        return v is None or (isinstance(v, int) and 0 <= v < n)

    guards = set()
    lastend = 0
    for i, r in enumerate(props):
        t = r["type"]
        if r["prop"] != i:
            err("prop %d: index %d" % (i, r["prop"]))
        if t not in SETUP_SIZES:
            err("prop %d: type %d" % (i, t))
            continue
        if len(r["raw"]) != 2 * SETUP_SIZES[t] or r["raw"][6:8] != "%02x" % t:
            err("prop %d: raw size / type byte" % i)
        if r["offset"] < lastend or r["offset"] + SETUP_SIZES[t] > fsize:
            err("prop %d: offset %d" % (i, r["offset"]))
        lastend = r["offset"] + SETUP_SIZES[t]
        if "fields" not in r:
            err("prop %d: fields" % i)
        for rk, rv in (r.get("refs") or {}).items():
            if not ref_ok(rv):
                err("prop %d: refs.%s = %r" % (i, rk, rv))
        if t == 9:
            guards.add(r["chr"])
            if not pad_ok(r["pad"], True):
                err("prop %d: guard pad %d" % (i, r["pad"]))
            if r["ai"] != r["ailist"]:
                err("prop %d: ai != ailist" % i)
        elif t in SETUP_OBJ:
            for k in ("model", "pad", "pad_raw", "load_on"):
                if k not in r:
                    err("prop %d: %s" % (i, k))
            if r.get("pad") is None:
                if "inside" not in (r.get("refs") or {}) and "carrier" not in r:
                    err("prop %d: no pad, no inside, no carrier" % i)
            elif t == 1 and not pad_ok(r["pad"]):
                err("prop %d: door pad %d" % (i, r["pad"]))
            if set(r.get("load_on", {})) != {"agent", "secret", "00", "007", "multi", "2p", "3p", "4p"}:
                err("prop %d: load_on" % i)
    for i, r in enumerate(props):   # carriers are chr numbers (some belong to guards the AI creates: not in the file)
        if r.get("carrier") is not None and not (0 <= r["carrier"] < 0x8000):
            err("prop %d: carrier %d" % (i, r["carrier"]))
    for o in j["objectives"]:
        if not (0 <= o["prop"] < n) or props[o["prop"]]["type"] != 23:
            err("objective at prop %d" % o["prop"])
        for c in o["conditions"]:
            if not (0 <= c["prop"] < n) or props[c["prop"]]["type"] != c["type"]:
                err("objective condition at prop %d" % c["prop"])
            if "object" in c and not ref_ok(c["object"]):
                err("objective condition %d: object" % c["prop"])
    for b in j["briefing"]:
        if not (0 <= b["prop"] < n) or props[b["prop"]]["type"] != 35:
            err("briefing at prop %d" % b["prop"])
    for a in j["ailists"]:
        if a["offset"] + a["size"] > fsize or len(a["raw"]) != 2 * a["size"] or not a["raw"].endswith("04"):
            err("ailist %d: byte range" % a["id"])
        if a["class"] not in ("global", "chr", "bg"):
            err("ailist %d: class" % a["id"])
    nwp, ngr = len(j["waypoints"]), len(j["waypoint_groups"])
    for i, w in enumerate(j["waypoints"]):
        if not (0 <= w["pad"] < npads) or not (0 <= w["group"] < max(ngr, 1)) \
                or any(not (0 <= x < nwp) for x in w["neighbours"]):
            err("waypoint %d: pad/group/neighbours" % i)
    for i, g in enumerate(j["waypoint_groups"]):
        if any(not (0 <= x < nwp) for x in g["waypoints"]) or any(not (0 <= x < ngr) for x in g["neighbours"]):
            err("waypoint group %d" % i)
    for i, p in enumerate(j["patrol_paths"]):
        if any(not (0 <= x < nwp) for x in p["waypoints"]) or len(p["pads"]) != len(p["waypoints"]) \
                or any(p["pads"][k] != j["waypoints"][x]["pad"] for k, x in enumerate(p["waypoints"])):
            err("patrol path %d" % i)
    if not j["intro"] or j["intro"][-1]["type"] != 9:
        err("intro does not end with END")
    for r in j["intro"]:
        if len(r["raw"]) != 2 * SETUP_INTRO[r["type"]]:
            err("intro record at %d: raw size" % r["offset"])
        if r["type"] == 0 and not (0 <= r["pad"] < npads):
            err("intro spawn pad %d" % r["pad"])
    has8 = any(r["type"] == 8 for r in j["intro"])
    if has8 != (j["credits"] is not None):
        err("credits block vs intro credits record")
    SETUP_COUNT[path] = (npads, nb, n, len(j["ailists"]))
    return errs


def check_models(mroot):
    """Models export (issue #119): <mroot>/<model>/<model>.gltf for every entry of models.json."""
    try:
        with open(os.path.join(mroot, "models.json"), encoding="utf-8") as f:
            idx = json.load(f)
    except Exception as e:  # noqa: BLE001
        print("models: models.json: %s" % e)
        return 1
    bad, tot, ntri_all = 0, [0, 0, 0, 0], 0
    ents = idx.get("models", [])
    nums = [e.get("model") for e in ents]
    if nums != list(range(len(ents))):
        print("models: ERROR: model numbers are not 0..%d in order" % (len(ents) - 1))
        bad += 1
    for e in ents:
        if e.get("status") == "failed":
            print("model %d %s: FAILED %s" % (e["model"], e.get("file"), e.get("notes")))
            bad += 1
            continue
        name = os.path.dirname(e["gltf"])
        errs, st = check_level(mroot, name, report=False)
        if st is None:
            errs = errs or ["no stats"]
        else:
            for i in range(4):
                tot[i] += st[i + 0] if i < 3 else st[3]
            try:
                with open(os.path.join(mroot, e["gltf"]), encoding="utf-8") as f:
                    g = json.load(f)
                sc = g["scenes"]
                nn = len(g["nodes"])
                for si in sc:
                    for ni in si["nodes"]:
                        if ni >= nn:
                            errs.append("scene node %d out of range" % ni)
                for nd in g["nodes"]:
                    for c in nd.get("children", []):
                        if c >= nn:
                            errs.append("child %d out of range" % c)
                if g["asset"]["extras"].get("model") != e["model"]:
                    errs.append("asset.extras.model != models.json")
                ntri_all += st[2]
                if st[2] != e["triangles_all"]:
                    errs.append("triangles_all %d != gltf %d" % (e["triangles_all"], st[2]))
            except Exception as ex:  # noqa: BLE001
                errs.append("gltf: %s" % ex)
        for er in errs:
            print("model %d %s: ERROR: %s" % (e["model"], e.get("file"), er))
        if errs:
            bad += 1
    print("%d models checked, %d with errors; nodes %d primitives %d triangles %d textures %d"
          % (len(ents), bad, tot[0], tot[1], tot[2], tot[3]))
    return 1 if bad or not ents else 0


# ---- text export (docs/specs/text.md §Verification): strings/slots per bank, columns US E, US J, EU E, EU J, EU P, JP E, JP J ----
TXT_COLS = {("ntsc-final", "E"): 0, ("ntsc-final", "J"): 1, ("pal-final", "E"): 2, ("pal-final", "J"): 3,
            ("pal-final", "P"): 4, ("jpn-final", "E"): 5, ("jpn-final", "J"): 6}
TXT_TOTAL = [1785, 1741, 1858, 1897, 1811, 1811, 1850]
TXT_SHA1 = ["d96c68685336dcf802af4d8aaab465d6b681f2ee", "6dc32d754c0b48b9917993bf342dbfe71aea61f6",
            "456f969513944726d02ea4686b15cb85d1406ac1", "78dd1128df28d425815b09e714f6a4f29f1bb19e",
            "1824d661bfe5b0447f7d5160271f29b76f85dc0f", "0601e5fdb4effab8081a4fcf5aa6dd19afa1e293",
            "593afa7a783da5bfd40cf4b5823fe08a45fcc31c"]


def _same(x):
    return " ".join([x] * 7)


TXT_ROWS = {
    2: "62/64 63/64 62/64 63/64 62/64 62/64 63/64", 3: "71/72 72/72 71/72 72/72 71/72 71/72 72/72",
    5: "36/44 44/44 36/44 44/44 36/44 36/44 44/44", 7: _same("41/44"), 8: _same("45/48"),
    9: _same("35/36"), 10: _same("15/16"), 11: _same("34/36"),
    12: "30/36 35/36 30/36 35/36 30/36 30/36 35/36", 13: _same("31/32"), 18: _same("42/44"),
    20: "130/140 55/56 140/148 148/148 140/148 140/148 148/148", 25: _same("29/32"),
    28: "26/32 30/32 26/32 30/32 26/32 26/32 30/32", 29: "104/108 108/108 104/108 108/108 104/108 104/108 108/108",
    30: _same("47/48"), 31: "38/44 42/44 38/44 42/44 38/44 38/44 42/44",
    32: "31/36 35/36 31/36 35/36 31/36 31/36 35/36", 34: _same("71/72"), 35: _same("68/68"),
    36: _same("45/48"), 37: "0/8 0/8 47/48 47/48 0/8 0/8 0/8", 38: _same("223/224"),
    39: "287/288 287/288 302/304 302/304 302/304 302/304 302/304", 40: _same("36/36"),
    41: _same("65/68"), 42: _same("14/16"), 43: _same("61/64"),
    44: "68/68 68/68 69/72 69/72 69/72 69/72 69/72",
}
for _b in (1, 4, 6, 14, 15, 16, 17, 19, 21, 22, 23, 24, 26, 27, 33):
    TXT_ROWS[_b] = _same("0/8")
HEX4 = set("0123456789ABCDEF")
# the spec's own examples (US E)
TXT_EXAMPLES = {"9840": "Hunting Knife\n", "9856": "PP7 Special Issue\n", "986B": "Klobb\n", "9842": "KF7 Soviet\n",
                "9866": "Sniper Rifle\n", "9C79": "Dam"}


def check_text(troot):
    """Text export (docs/specs/text.md): text_<lang>.json per language set and names.json."""
    errs = []
    files = sorted(n for n in os.listdir(troot) if n.startswith("text_") and n.endswith(".json"))
    if not files:
        errs.append("no text_<lang>.json")
    region = None
    ngame = 0
    for fn in files:
        try:
            with open(os.path.join(troot, fn), encoding="utf-8") as f:
                t = json.load(f)
        except Exception as e:  # noqa: BLE001
            errs.append("%s: %s" % (fn, e))
            continue
        region = t.get("region")
        suf = t.get("suffix")
        tag = "%s %s" % (region, suf)
        if t.get("format") != "ge007-text/1":
            errs.append("%s: format" % tag)
        col = TXT_COLS.get((region, suf))
        if col is None:
            errs.append("%s: unexpected set" % tag)
            continue
        if t.get("game_language"):
            ngame += 1
        strs = t.get("strings", {})
        banks = t.get("banks", [])
        if [b["bank"] for b in banks] != list(range(1, 45)):
            errs.append("%s: banks not 1..44" % tag)
            continue
        per = {}
        for k, v in strs.items():
            if len(k) != 4 or set(k) - HEX4:
                errs.append("%s: bad id %r" % (tag, k))
                continue
            i = int(k, 16)
            if (i >> 10) < 1 or (i >> 10) > 44 or v.get("index") != (i & 0x3FF) or v.get("bank") != banks[(i >> 10) - 1]["name"]:
                errs.append("%s: id %s does not match its bank/index" % (tag, k))
                continue
            per[i >> 10] = per.get(i >> 10, 0) + 1
            if suf != "J" and "{" in v["text"]:
                errs.append("%s: escape in English text %s" % (tag, k))
            if suf == "J" and not v.get("raw") and v["text"] != "":
                errs.append("%s: %s has no raw" % (tag, k))
        for b in banks:
            exp = TXT_ROWS[b["bank"]].split()[col]
            got = "%d/%d" % (b["strings"], b["slots"])
            if got != exp:
                errs.append("%s: bank %d %s: %s, spec says %s" % (tag, b["bank"], b["name"], got, exp))
            if per.get(b["bank"], 0) != b["strings"]:
                errs.append("%s: bank %d lists %d strings, header says %d" % (tag, b["bank"], per.get(b["bank"], 0), b["strings"]))
        if len(strs) != TXT_TOTAL[col] or t.get("strings_total") != TXT_TOTAL[col]:
            errs.append("%s: %d strings, spec says %d" % (tag, len(strs), TXT_TOTAL[col]))
        if t.get("sha1") != TXT_SHA1[col]:
            errs.append("%s: sha1 %s, spec says %s" % (tag, t.get("sha1"), TXT_SHA1[col]))
        if col == 0:
            for k, want in TXT_EXAMPLES.items():
                if strs.get(k, {}).get("text") != want:
                    errs.append("%s: example id %s differs from the spec" % (tag, k))
        print("text %-14s %4d strings%s" % (tag, len(strs), " (game language)" if t.get("game_language") else ""))
    if files and ngame != 1:
        errs.append("%d sets marked as the game language" % ngame)
    try:
        with open(os.path.join(troot, "names.json"), encoding="utf-8") as f:
            nm = json.load(f)
        for key, n in (("items", 89), ("folders", 29), ("briefings", 20), ("mp_stages", 12), ("mp_characters", 64)):
            if len(nm.get(key, [])) != n:
                errs.append("names.json: %s has %d entries, spec says %d" % (key, len(nm.get(key, [])), n))
        if [i["item"] for i in nm["items"]] != list(range(89)):
            errs.append("names.json: item numbers")
        nobj = sum(len(b["objectives"]) for b in nm["briefings"])
        if nobj != 80:
            errs.append("names.json: %d objectives, spec says 80" % nobj)
        if sum(1 for f_ in nm["folders"] if f_["header"]) != 9:
            errs.append("names.json: mission headers")

        def ids(o):
            if isinstance(o, dict):
                for k, v in o.items():
                    if k in ("long", "short", "name", "select", "id"):
                        yield v
                    yield from ids(v)
            elif isinstance(o, list):
                for v in o:
                    yield from ids(v)
        for v in ids(nm):
            if v is not None and (not isinstance(v, str) or len(v) != 4 or set(v) - HEX4):
                errs.append("names.json: bad id %r" % (v,))
                break
        print("names: %d items, %d folders, %d briefings (%d objectives), %d stages, %d characters"
              % (len(nm["items"]), len(nm["folders"]), len(nm["briefings"]), nobj, len(nm["mp_stages"]), len(nm["mp_characters"])))
        if region and nm.get("region") != region:
            errs.append("names.json region differs from text")
    except Exception as e:  # noqa: BLE001
        errs.append("names.json: %s" % e)
    for er in errs:
        print("text: ERROR: %s" % er)
    return 1 if errs else 0
def check_characters(croot):
    """Characters export (docs/specs/characters.md): <croot>/<name>/<name>.gltf for every entry of characters.json."""
    try:
        with open(os.path.join(croot, "characters.json"), encoding="utf-8") as f:
            idx = json.load(f)
    except Exception as e:  # noqa: BLE001
        print("characters: characters.json: %s" % e)
        return 1
    ents = idx.get("characters", [])
    bad, nb, nh, nhand, tri_def, tri_all = 0, 0, 0, 0, 0, 0
    if [e.get("id") for e in ents] != list(range(len(ents))) or len(ents) != 80:
        print("characters: ERROR: ids are not 0..79 in order")
        bad += 1
    for e in ents:
        if e.get("kind") == "hand":
            nhand += 1
            if e.get("gltf") is not None:
                print("character %d: ERROR: hand must not have a gltf" % e["id"])
                bad += 1
            continue
        if e.get("kind") not in ("body", "head"):
            print("character %d %s: FAILED %s" % (e["id"], e.get("file"), e.get("notes")))
            bad += 1
            continue
        name = os.path.dirname(e["gltf"])
        errs, st = check_level(croot, name, report=False)
        if st is not None:
            try:
                with open(os.path.join(croot, e["gltf"]), encoding="utf-8") as f:
                    g = json.load(f)
                with open(os.path.join(croot, name, name + ".bin"), "rb") as f:
                    binb = f.read()
                accs, nodes = g["accessors"], g["nodes"]
                body = e["kind"] == "body"
                nb += body
                nh += not body
                if st[2] != e["triangles_all"]:
                    errs.append("triangles_all %d != gltf %d" % (e["triangles_all"], st[2]))
                tdef = 0
                for m in g.get("meshes", []):
                    for p in m["primitives"]:
                        x = p.get("extras", {})
                        if x.get("lod_far") or x.get("sunglasses"):
                            continue
                        tdef += accs[p["indices"]]["count"] // 3
                if body:
                    tri_def += tdef
                    if tdef != e["triangles"]:
                        errs.append("triangles %d != gltf %d" % (e["triangles"], tdef))
                tri_all += st[2]
                for si in g["scenes"]:
                    for ni in si["nodes"]:
                        if ni >= len(nodes):
                            errs.append("scene node %d out of range" % ni)
                for nd in nodes:
                    for c in nd.get("children", []):
                        if c >= len(nodes):
                            errs.append("child %d out of range" % c)
                if g["asset"]["extras"].get("character") != e["id"]:
                    errs.append("asset.extras.character != characters.json")
                if body:
                    errs += check_body_rig(g, binb, e)
                else:
                    for m in g.get("meshes", []):
                        for p in m["primitives"]:
                            if "JOINTS_0" in p["attributes"]:
                                errs.append("head with a skin attribute")
                    if [n["name"] for n in nodes if n.get("name") == "neck"] != ["neck"]:
                        errs.append("head: no neck root node")
            except Exception as ex:  # noqa: BLE001
                errs.append("gltf: %s" % ex)
        for er in errs:
            print("character %d %s: ERROR: %s" % (e["id"], e.get("file"), er))
        if errs:
            bad += 1
    print("%d characters checked (%d bodies, %d heads, %d hand skipped), %d with errors; body triangles (scene 0) %d, all %d"
          % (len(ents), nb, nh, nhand, bad, tri_def, tri_all))
    bad += check_shared_animations(croot, idx)
    return 1 if bad or not ents else 0


def check_shared_animations(croot, idx):
    """animations/<name>.gltf: every used animation of characters.json, one animation on the guard rig per file."""
    if not idx.get("animations_dir"):
        print("characters: ERROR: characters.json names no animations_dir")
        return 1
    bad, n = 0, 0
    for an in idx.get("animations", []):
        errs = []
        try:
            if not an.get("gltf"):
                raise ValueError("no gltf")
            gp = os.path.join(croot, an["gltf"])
            with open(gp, encoding="utf-8") as f:
                g = json.load(f)
            with open(os.path.join(os.path.dirname(gp), g["buffers"][0]["uri"]), "rb") as f:
                binb = f.read()
            nodes = g["nodes"]
            for j in range(16):
                if not any(nd.get("name", "").startswith("j%d_" % j) and "_half" not in nd["name"] for nd in nodes):
                    errs.append("joint %d node missing" % j)
            if any(nd.get("mesh") is not None for nd in nodes):
                errs.append("the rig carries a mesh")
            anims = g.get("animations", [])
            if [a.get("name") for a in anims] != [an["name"]] or anims[0].get("extras", {}).get("number") != an["number"]:
                errs.append("not exactly the animation %s (%d)" % (an["name"], an["number"]))
            for a in anims:
                for c in a["channels"]:
                    if c["target"]["node"] >= len(nodes):
                        errs.append("node out of range")
                        break
                    sm = a["samplers"][c["sampler"]]
                    ti, _ = acc_vals(g, binb, sm["input"])
                    vo, _ = acc_vals(g, binb, sm["output"])
                    if len(ti) != an["frames"] or len(vo) != len(ti) * (3 if c["target"]["path"] == "translation" else 4):
                        errs.append("sampler sizes")
                        break
        except Exception as ex:  # noqa: BLE001
            errs.append("%s" % ex)
        for er in errs:
            print("animation %d %s: ERROR: %s" % (an.get("number", -1), an.get("name"), er))
        bad += bool(errs)
        n += 1
    print("animations: %d files checked on the %s rig, %d with errors" % (n, idx.get("animations_rig"), bad))
    return 1 if bad or not n else 0


def acc_vals(g, binb, ai):
    a = g["accessors"][ai]
    fmt, sz = CT[a["componentType"]]
    n = 16 if a["type"] == "MAT4" else NC[a["type"]]
    v = g["bufferViews"][a["bufferView"]]
    off = v["byteOffset"] + a.get("byteOffset", 0)
    return struct.unpack("<%d%s" % (a["count"] * n, fmt), binb[off:off + a["count"] * n * sz]), n


def check_body_rig(g, binb, e):
    """The skin (joints, inverse bind matrices, one joint of weight 1 per vertex) and the animations of a body."""
    errs = []
    nodes = g["nodes"]
    sk = g.get("skins", [])
    if len(sk) != 1:
        return ["expected one skin, found %d" % len(sk)]
    sk = sk[0]
    nj = len(sk["joints"])
    if nj != e["joints"]:
        errs.append("skin has %d joints, characters.json %d" % (nj, e["joints"]))
    if any(j >= len(nodes) for j in sk["joints"]) or nodes[sk["skeleton"]].get("name") != "j0_root":
        errs.append("skin joints / skeleton node")
    for j in range(16):
        if not any(n.get("name", "").startswith("j%d_" % j) and "_half" not in n["name"] for n in nodes):
            errs.append("joint %d node missing" % j)
    ibm, _ = acc_vals(g, binb, sk["inverseBindMatrices"])
    if len(ibm) != nj * 16:
        errs.append("inverseBindMatrices count")
    for m in g["meshes"]:
        for p in m["primitives"]:
            a = p["attributes"]
            if "JOINTS_0" not in a or "WEIGHTS_0" not in a:
                errs.append("%s: body primitive without JOINTS_0/WEIGHTS_0" % m.get("name"))
                continue
            jv, _ = acc_vals(g, binb, a["JOINTS_0"])
            wv, _ = acc_vals(g, binb, a["WEIGHTS_0"])
            if jv and max(jv) >= nj:
                errs.append("%s: joint index %d >= %d" % (m.get("name"), max(jv), nj))
            if any(jv[i] for i in range(len(jv)) if i % 4) or any(
                    wv[i * 4] != 1.0 or wv[i * 4 + 1] or wv[i * 4 + 2] or wv[i * 4 + 3] for i in range(len(wv) // 4)):
                errs.append("%s: weights are not (1, 0, 0, 0) on joint slot 0" % m.get("name"))
    want = set(e.get("animations", []))
    have = set(a.get("name") for a in g.get("animations", []))
    if want != have:
        errs.append("animations %s != characters.json %s" % (sorted(have), sorted(want)))
    for a in g.get("animations", []):
        for c in a["channels"]:
            if c["target"]["node"] >= len(nodes):
                errs.append("animation %s: node out of range" % a["name"])
            s = a["samplers"][c["sampler"]]
            ti, _ = acc_vals(g, binb, s["input"])
            vo, _ = acc_vals(g, binb, s["output"])
            if len(vo) != len(ti) * (3 if c["target"]["path"] == "translation" else 4) or any(b <= a2 for a2, b in zip(ti, ti[1:])):
                errs.append("animation %s: sampler sizes / times" % a["name"])
            if c["target"]["path"] == "rotation":
                for k in range(len(ti)):
                    q = vo[k * 4:k * 4 + 4]
                    if abs(sum(v * v for v in q) - 1.0) > 1e-4:
                        errs.append("animation %s: non-unit quaternion" % a["name"])
                        break
    return errs


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    root = sys.argv[1]
    levels = sorted(n for n in os.listdir(root) if os.path.isdir(os.path.join(root, n)) and n not in ("models", "text", "characters"))
    bad = 0
    tot = [0, 0, 0, 0]
    for n in levels:
        errs, st = check_level(root, n)
        if st:
            print("%-10s rooms %3d  primitives %5d  triangles %7d  textures %3d  materials %4d"
                  % ((n,) + st))
            for i in range(4):
                tot[i] += st[i]
        else:
            print("%-10s ERRORS" % n)
        if n in COLL:
            print("%-10s collision: %d tiles in %d rooms, %d triangles (hidden scene 1)" % ((n,) + COLL[n]))
        for e in errs:
            print("    ERROR:", e)
        for sf in ("setup.json", "setup_mp.json"):
            sp = os.path.join(root, n, sf)
            if not os.path.isfile(sp):
                continue
            serrs = check_setup(sp)
            if sp in SETUP_COUNT:
                print("%-10s %-13s pads %3d bound pads %3d propDefs %4d AI lists %2d" % ((n, sf) + SETUP_COUNT[sp]))
            for e in serrs:
                print("    ERROR:", sf, e)
            errs = errs + serrs
        if errs:
            bad += 1
    print("%d levels checked, %d with errors; rooms %d primitives %d triangles %d textures %d"
          % (len(levels), bad, tot[0], tot[1], tot[2], tot[3]))
    mbad = check_models(os.path.join(root, "models")) if os.path.isdir(os.path.join(root, "models")) else 0
    tbad = check_text(os.path.join(root, "text")) if os.path.isdir(os.path.join(root, "text")) else 0
    cbad = check_characters(os.path.join(root, "characters")) if os.path.isdir(os.path.join(root, "characters")) else 0
    return 1 if bad or mbad or tbad or cbad or not levels else 0


if __name__ == "__main__":
    sys.exit(main())
