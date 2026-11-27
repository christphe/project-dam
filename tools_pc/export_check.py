#!/usr/bin/env python3
"""Validate a ge007-export output directory (issue #150). Stdlib only.

usage: export_check.py <out_dir>

For every <out_dir>/<level>/<level>.gltf: the JSON parses; every bufferView and
accessor fits the .bin; index values are below the vertex count; POSITION
min/max match the data; every image file exists and is a valid PNG; every
referenced material / texture / sampler / image exists. Prints rooms /
primitives / triangles / textures per level; exits non-zero on any error.
"""
import json
import os
import struct
import sys
import zlib

CT = {5120: ("b", 1), 5121: ("B", 1), 5122: ("h", 2), 5123: ("H", 2), 5125: ("I", 4), 5126: ("f", 4)}
NC = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}


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
    stats = (len(g.get("nodes", [])), nprim, ntri, nimg, nmat)
    return errs, stats


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


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    root = sys.argv[1]
    levels = sorted(n for n in os.listdir(root) if os.path.isdir(os.path.join(root, n)) and n != "models")
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
        for e in errs:
            print("    ERROR:", e)
        if errs:
            bad += 1
    print("%d levels checked, %d with errors; rooms %d primitives %d triangles %d textures %d"
          % (len(levels), bad, tot[0], tot[1], tot[2], tot[3]))
    mbad = check_models(os.path.join(root, "models")) if os.path.isdir(os.path.join(root, "models")) else 0
    return 1 if bad or mbad or not levels else 0


if __name__ == "__main__":
    sys.exit(main())
