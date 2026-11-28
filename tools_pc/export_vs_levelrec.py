#!/usr/bin/env python3
"""Compare the static export (the base asset pack ge007.passets, ge007-export, issue #150) with the
game's own capture of a level (`ge007 -level_NN --export-level DIR`, level.rec, issue #102).

<pack> is ge007.passets (an uncompressed ustar tar, or a folder made with --folder), or a folder holding it; it is read in
place (tarfile), nothing is unpacked. The level is the capture's own (<levelrec_dir>/level.json "level"), or LEVEL when given.

  python tools_pc/export_vs_levelrec.py <pack> <levelrec_dir> [LEVEL]

Checks:
  * textures: every texture the glTF uses is found among the level.rec
    textures with identical RGBA pixels (by the level.rec FNV-1a hash), or
    the closest same-size texture is reported with its max channel difference;
  * geometry: triangles per room, glTF node "room_<n>" vs the level.rec runs
    owned by room n.

  python tools_pc/export_vs_levelrec.py --models <pack> <levelrec_dir>

compares every object model of the level.rec (owners of kind 1, "object <n>")
with the pack's models/models.json (ge007-export, issue #119): triangle count of the
default scene and the set of textures (matched by pixels, padded copies
included).

  python tools_pc/export_vs_levelrec.py --setup <pack> <levelrec_dir> [setup.json|setup_mp.json]

compares the level's levels/<level>/setup.json of the pack (docs/specs/setup.md) with the capture's level.json: `scale`,
the set of object model numbers (plus ammo box slot models with a quantity) against
the `models[]` entries with guard false, the guard `body` values against the
guard true entries; and, when that level.json has `pads`, every pad within float32
precision.
  python tools_pc/export_vs_levelrec.py --characters <pack> <levelrec_dir>

compares every `guard <body id>` owner of the level.rec with the pack's characters/characters.json
(docs/specs/characters.md): (1) every near-LOD triangle of the ROM body (raw positions) occurs in the capture;
(2) the capture's joint matrices equal S(model scale) x G of some (animation, frame) of the export (accepted at
1e-3; a blended live pose is reported, not failed); (3) the heads whose every vertex occurs in the capture.

Both inputs are ROM-derived and stay in the user's data folders. Stdlib only.
"""
import json
import os
import posixpath
import struct
import sys
import zlib

import export_check   # the pack reader (tar or folder), same folder
from export_check import Pack

FNV_OFF, FNV_PRIME, MASK = 1469598103934665603, 1099511628211, (1 << 64) - 1

# Textures the game rewrites while the level runs (docs/specs/textures.md): water.
RUNTIME_ALTERED = {1511}


def fnv(rgba, w, h):
    x = FNV_OFF
    for b in rgba:
        x = ((x ^ b) * FNV_PRIME) & MASK
    x = ((x ^ w) * FNV_PRIME) & MASK
    return ((x ^ h) * FNV_PRIME) & MASK



PK = None   # the open pack


def rdjson(rel):
    return json.loads(PK.read(rel).decode("utf-8"))


def texture_png(n):
    """The bytes of the picture of texture n: textures/<n>.png of the pack (the glTFs point at it with ../../textures/<n>.png)."""
    return PK.read("textures/%d.png" % n)


def read_png(data):
    assert data[:8] == b"\x89PNG\r\n\x1a\n", "not a PNG"
    pos, idat, w, h = 8, b"", 0, 0
    while pos < len(data):
        n, typ = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        if typ == b"IHDR":
            w, h, depth, ctype = struct.unpack(">IIBB", body[:10])
            assert depth == 8 and ctype == 6, "RGBA8 only"
        elif typ == b"IDAT":
            idat += body
        pos += 12 + n
    raw = zlib.decompress(idat)
    stride = w * 4
    out = bytearray()
    prev = bytearray(stride)
    for y in range(h):
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - 4] if i >= 4 else 0
            b = prev[i]
            c = prev[i - 4] if i >= 4 else 0
            if f == 1:
                line[i] = (line[i] + a) & 255
            elif f == 2:
                line[i] = (line[i] + b) & 255
            elif f == 3:
                line[i] = (line[i] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        out += line
        prev = line
    return w, h, bytes(out)


def read_levelrec(path):
    d = open(path, "rb").read()
    magic, ver = struct.unpack_from("<II", d, 0)
    assert magic == 0x524C4547 and ver == 1, "not a level.rec v1"
    ntex, nown, nruns = struct.unpack_from("<III", d, 8 + 64 + 32)
    off = 8 + 64 + 32 + 12
    tex = []
    for _ in range(ntex):
        hsh, w, h = struct.unpack_from("<QHH", d, off)
        off += 12
        tex.append((hsh, w, h, d[off:off + w * h * 4]))
        off += w * h * 4
    owners = []
    for _ in range(nown):
        kind, oid = struct.unpack_from("<Ii", d, off)
        owners.append((kind, oid))
        off += 8 + 32
    # LevelRecRun (packed): owner u32, shader_id0 u64, shader_id1 u32, tex[2] i32,
    # 8 u8 sampler bytes, 4 u8 depth bytes, zmode u16, 4 u8, fog 6 f32,
    # 3 x 8 x 4 f32, tpf0 tpf1 u8, nverts nmtx nfloats u32
    run_fmt = "<IQI2i8B4BH4B6f96f2B3I"
    run_size = struct.calcsize(run_fmt)
    tris_by_room = {}
    for _ in range(nruns):
        r = struct.unpack_from(run_fmt, d, off)
        owner = r[0]
        nverts, nmtx, nfloats = r[-3:]
        off += run_size + nmtx * 64 + nfloats * 4
        kind, oid = owners[owner]
        if kind == 0:
            tris_by_room[oid] = tris_by_room.get(oid, 0) + nverts // 3
    assert off == len(d), "level.rec parse did not end at EOF (%d / %d)" % (off, len(d))
    return tex, tris_by_room


def read_levelrec_models(path):
    """-> (textures, {model: [triangles, set of texture indices]}) for the kind-1 owners "object <n>"."""
    d = open(path, "rb").read()
    ntex, nown, nruns = struct.unpack_from("<III", d, 8 + 64 + 32)
    off = 8 + 64 + 32 + 12
    tex = []
    for _ in range(ntex):
        hsh, w, h = struct.unpack_from("<QHH", d, off)
        off += 12
        tex.append((hsh, w, h, d[off:off + w * h * 4]))
        off += w * h * 4
    owners = []
    for _ in range(nown):
        kind, oid = struct.unpack_from("<Ii", d, off)
        name = d[off + 8:off + 40].split(b"\0")[0].decode("latin1")
        owners.append((kind, oid, name))
        off += 8 + 32
    run_fmt = "<IQI2i8B4BH4B6f96f2B3I"
    run_size = struct.calcsize(run_fmt)
    models = {}
    for _ in range(nruns):
        r = struct.unpack_from(run_fmt, d, off)
        nverts, nmtx, nfloats = r[-3:]
        off += run_size + nmtx * 64 + nfloats * 4
        kind, oid, name = owners[r[0]]
        if kind != 1:
            continue
        n = int(name.split()[-1]) if name.startswith("object ") else oid
        m = models.setdefault(n, [0, set()])
        m[0] += nverts // 3
        for t in (r[3], r[4]):
            if t >= 0:
                m[1].add(t)
    assert off == len(d), "level.rec parse did not end at EOF"
    return tex, models


def compare_models(rdir):
    idx = rdjson("models/models.json")["models"]
    tex, rec = read_levelrec_models(os.path.join(rdir, "level.rec"))
    bad = 0
    print("model file                      tris(ours/capture)  textures(ours/capture)  match")
    for n in sorted(rec):
        e = idx[n]
        e.setdefault("triangles", -1)
        ctris, ctex = rec[n]
        ours = []   # (texnum, hash, w, h, rgba)
        for tn in e.get("textures", []):
            w, h, rgba = read_png(texture_png(tn))
            ours.append((tn, fnv(rgba, w, h), w, h, rgba))
        matched, unmatched = set(), []
        for tn, hs, w, h, rgba in ours:
            hit = None
            for i in ctex:
                _, th, tw2, trgba = tex[i][0], tex[i][2], tex[i][1], tex[i][3]
                if tex[i][0] == hs:
                    hit = i
                    break
                tw, thh = tex[i][1], tex[i][2]
                if tw >= w and thh >= h and all(tex[i][3][y * tw * 4:y * tw * 4 + w * 4] == rgba[y * w * 4:(y + 1) * w * 4]
                                                for y in range(h)):
                    hit = i
                    break
            if hit is None:
                unmatched.append(tn)
            else:
                matched.add(hit)
        extra_cap = sorted(set(ctex) - matched)
        ok_t = not unmatched and not extra_cap
        ok_g = e["triangles"] == ctris
        print("%-4d %-26s %5d / %-5d %s  %2d / %-2d %s  %s" % (
            n, e["file"], e["triangles"], ctris, "ok " if ok_g else "BAD", len(ours), len(ctex), "ok " if ok_t else "DIFF",
            ("ours not in capture %s " % unmatched if unmatched else "") +
            ("capture-only %d texture(s)" % len(extra_cap) if extra_cap else "")))
        if not ok_g:
            bad += 1
    print("%d object models in the capture, %d with a different triangle count" % (len(rec), bad))
    return 1 if bad else 0


def compare_stan(g, rdir):
    """The collision floor of the glTF (asset.extras.collision, primitives' extras.tiles) against level.json "stan":
    same tile count per room, same points tile by tile (level.json has 4 decimals), every link resolves."""
    ce = g.get("asset", {}).get("extras", {}).get("collision")
    jp = os.path.join(rdir, "level.json")
    if ce is None or not os.path.isfile(jp):
        print("stan: %s" % ("no collision in the glTF" if ce is None else "no level.json"))
        return 0 if ce is None else 1
    ref = json.load(open(jp)).get("stan", [])
    scale = g["asset"]["extras"]["level_scale"]
    tiles = {}
    for n in g["nodes"]:
        if not (n.get("extras") or {}).get("collision") or "mesh" not in n:
            continue
        for p in g["meshes"][n["mesh"]]["primitives"]:
            for t in p.get("extras", {}).get("tiles", []):
                tiles[t["index"]] = t
    mine = [tiles[i] for i in sorted(tiles)]
    per_room_a, per_room_b = {}, {}
    for t in mine:
        per_room_a[t["room"]] = per_room_a.get(t["room"], 0) + 1
    for t in ref:
        per_room_b[t["room"]] = per_room_b.get(t["room"], 0) + 1
    room_bad = [r for r in sorted(set(per_room_a) | set(per_room_b)) if per_room_a.get(r, 0) != per_room_b.get(r, 0)]
    pt_bad = 0
    worst = 0.0
    for a, b in zip(mine, ref):
        if a["room"] != b["room"] or len(a["points"]) != len(b["v"]):
            pt_bad += 1
            continue
        # level.json has 4 decimals and float32 values: the allowed difference is 2e-4 plus 2.4e-7 of the value
        d = max(abs(p[c] / scale - q[c]) - 2.4e-7 * abs(q[c]) for p, q in zip(a["points"], b["v"]) for c in range(3))
        worst = max(worst, d)
        if d > 2e-4:
            pt_bad += 1
    unresolved = ce.get("unresolved_links", 0)
    bad_nb = sum(1 for t in mine for L, nb in zip(t["links"], t["neighbour"]) if (L >> 4 != 0) and nb < 0)
    print("stan: glTF %d tiles in %d rooms, level.json %d tiles in %d rooms; %d rooms differ in tile count, "
          "%d tiles differ in room/points (max point difference %.5f), %d unresolved links"
          % (len(mine), len(per_room_a), len(ref), len(per_room_b), len(room_bad), pt_bad, worst, unresolved + bad_nb))
    for r in room_bad[:20]:
        print("  room %d: glTF %d tiles, level.json %d" % (r, per_room_a.get(r, 0), per_room_b.get(r, 0)))
    return 1 if (room_bad or pt_bad or len(mine) != len(ref) or unresolved or bad_nb) else 0


def compare_setup(rdir, name="setup.json"):
    """The pack's setup.json of the capture's level against level.json: scale, object model set, guard bodies (and pads when present)."""
    s = rdjson("levels/%s/%s" % (capture_level(rdir), name))
    ref = json.load(open(os.path.join(rdir, "level.json")))
    bad = 0
    sc = abs(s["scale"] - ref["scale"]) <= 1e-6 * abs(ref["scale"]) + 1e-6
    print("setup scale: setup.json %.7g, level.json %.7g  %s" % (s["scale"], ref["scale"], "ok" if sc else "DIFF"))
    bad += not sc
    models, bodies = set(), set()
    for r in s["props"]:
        if r["type"] == 9:
            if r["body"] != 0xFFFF:
                bodies.add(r["body"])
        elif "model" in r:
            models.add(r["model"])
            if r["type"] == 20:
                g = r["fields"]["ammo"]
                for k in range(13):
                    if g["slot%d_model" % k] != 0xFFFF and g["slot%d_qty" % k]:
                        models.add(g["slot%d_model" % k])
    rm = {m["model"] for m in ref["models"] if not m["guard"]}
    rb = {m["model"] for m in ref["models"] if m["guard"]}
    print("setup object models: %d in setup.json, %d in level.json; only setup %s, only level.json %s"
          % (len(models), len(rm), sorted(models - rm), sorted(rm - models)))
    print("setup guard bodies: setup.json %s, level.json %s" % (sorted(bodies), sorted(rb)))
    bad += (models != rm) + (bodies != rb)
    if "pads" in ref:
        worst, nbad = 0.0, 0
        mine = {p["num"]: p for p in s["pads"] + s["bound_pads"]}
        for p in ref["pads"]:
            m = mine.get(p.get("num"))
            if m is None:
                nbad += 1
                continue
            d = max(abs(a - b) - 2.4e-7 * abs(b) for a, b in zip(m["pos"], p["pos"]))
            worst = max(worst, d)
            nbad += d > 1e-3
        print("setup pads: %d compared, %d differ (max difference %.6f)" % (len(ref["pads"]), nbad, worst))
        bad += nbad > 0
    else:
        print("setup pads: this level.json has no pads (the editor reads them from the setup): not compared")
    return 1 if bad else 0


def read_levelrec_runs(path):
    """-> {owner name: [(mtx rows (list of 16-float tuples), nverts, vertex floats)]} for the kind-1 owners."""
    d = open(path, "rb").read()
    ntex, nown, nruns = struct.unpack_from("<III", d, 8 + 64 + 32)
    off = 8 + 64 + 32 + 12
    for _ in range(ntex):
        hsh, w, h = struct.unpack_from("<QHH", d, off)
        off += 12 + w * h * 4
    owners = []
    for _ in range(nown):
        kind, oid = struct.unpack_from("<Ii", d, off)
        name = d[off + 8:off + 40].split(b"\0")[0].decode("latin1")
        owners.append((kind, oid, name))
        off += 8 + 32
    run_fmt = "<IQI2i8B4BH4B6f96f2B3I"
    run_size = struct.calcsize(run_fmt)
    out = {}
    for _ in range(nruns):
        r = struct.unpack_from(run_fmt, d, off)
        nverts, nmtx, nfloats = r[-3:]
        off += run_size
        mt = struct.unpack_from("<%df" % (nmtx * 16), d, off)
        off += nmtx * 64
        vf = struct.unpack_from("<%df" % nfloats, d, off)
        off += nfloats * 4
        kind, oid, name = owners[r[0]]
        if kind == 1:
            out.setdefault(name, []).append(([mt[i * 16:i * 16 + 16] for i in range(nmtx)], nverts, vf))
    return out


def m4mul(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(4)) for j in range(4)] for i in range(4)]


def m4trs(t, q):
    x, y, z, w = q
    return [[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w), t[0]],
            [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w), t[1]],
            [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y), t[2]],
            [0.0, 0.0, 0.0, 1.0]]


def load_body(cdir, e):
    """The ROM body of a characters.json entry: stored-position triangles per list, the skin, per-frame joint matrices."""
    gp = posixpath.join(cdir, e["gltf"])
    g = rdjson(gp)
    binb = PK.read(gp[:-5] + ".bin")

    def acc(ai):
        a = g["accessors"][ai]
        fmt, sz = {5121: ("B", 1), 5123: ("H", 2), 5125: ("I", 4), 5126: ("f", 4)}[a["componentType"]]
        n = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}[a["type"]]
        v = g["bufferViews"][a["bufferView"]]
        o = v["byteOffset"] + a.get("byteOffset", 0)
        return struct.unpack("<%d%s" % (a["count"] * n, fmt), binb[o:o + a["count"] * n * sz]), n

    nodes = g["nodes"]
    parent = {}
    for i, n in enumerate(nodes):
        for c in n.get("children", []):
            parent[c] = i
    skin = g["skins"][0]["joints"]

    def joint_of(ni):
        return int(nodes[ni]["name"].split("_")[0][1:])

    def bind_sum(ni):   # sum of the origins from joint 1 down to the node's joint (the half node adds its own origin)
        s = [0.0, 0.0, 0.0]
        while ni in parent and joint_of(ni) >= 1:
            t = nodes[ni]["translation"]
            s = [s[k] + t[k] for k in range(3)]
            ni = parent[ni]
        return s

    sums = {ni: bind_sum(ni) for ni in skin}
    tris = []   # (scene0, [(stored pos int tuple, matrix) x 3])
    for mesh in g["meshes"]:
        for p in mesh["primitives"]:
            ex = p.get("extras", {})
            near = not ex.get("lod_far")
            pos, _ = acc(p["attributes"]["POSITION"])
            jt, _ = acc(p["attributes"]["JOINTS_0"])
            idx, _ = acc(p["indices"])
            for k in range(0, len(idx), 3):
                vs = []
                for ii in idx[k:k + 3]:
                    m = jt[ii * 4]
                    s = sums[skin[m]] if m < len(skin) else [0, 0, 0]
                    vs.append((tuple(int(round(pos[ii * 3 + c] - s[c])) for c in range(3)), m))
                tris.append((near, vs))
    # joint matrices per (animation, frame)
    anims = {}
    for a in g.get("animations", []):
        ch = {}
        for c in a["channels"]:
            s = a["samplers"][c["sampler"]]
            vals, n = acc(s["output"])
            ch[(c["target"]["node"], c["target"]["path"])] = [vals[i:i + n] for i in range(0, len(vals), n)]
        nf = a["extras"]["frames"]
        frames = []
        for f in range(nf):
            glob = {}

            def G(ni):
                if ni in glob:
                    return glob[ni]
                nd = nodes[ni]
                t = nd.get("translation", [0, 0, 0])
                q = nd.get("rotation", [0, 0, 0, 1])
                if (ni, "rotation") in ch:
                    q = ch[(ni, "rotation")][f]
                if joint_of(ni) == 0:
                    t = [0.0, 0.0, 0.0]   # the capture zeroes the root's position and heading
                    q = [0.0, 0.0, 0.0, 1.0]
                elif (ni, "translation") in ch:
                    t = ch[(ni, "translation")][f]
                loc = m4trs(t, q)
                glob[ni] = m4mul(G(parent[ni]), loc) if ni in parent else loc
                return glob[ni]

            frames.append([G(ni) for ni in skin])
        anims[a["name"]] = frames
    return tris, anims, skin


def compare_characters(rdir):
    cdir = "characters"
    cj = rdjson(posixpath.join(cdir, "characters.json"))
    ents = {e["id"]: e for e in cj["characters"]}
    runs = read_levelrec_runs(os.path.join(rdir, "level.rec"))
    bad = 0
    heads = [e for e in cj["characters"] if e.get("kind") == "head"]
    for name in sorted(runs):
        if not name.startswith("guard "):
            continue
        bid = int(name.split()[-1])
        e = ents.get(bid)
        if not e or e.get("kind") != "body":
            print("%s: not a body of characters.json" % name)
            bad += 1
            continue
        scale = e["model_scale"]
        cap = {}   # sorted triangle of int positions -> (run, [(pos, idx)])
        capverts = set()
        for ri, (mt, nv, vf) in enumerate(runs[name]):
            stride = len(vf) // nv
            for t in range(nv // 3):
                vs = []
                for k in range(3):
                    o = (t * 3 + k) * stride
                    vs.append((tuple(int(round(vf[o + c])) for c in range(3)), int(round(vf[o + 3]))))
                    capverts.add(vs[-1][0])
                cap.setdefault(tuple(sorted(v[0] for v in vs)), (ri, vs))
        tris, anims, skin = load_body(cdir, e)
        near = [t for t in tris if t[0]]
        found = 0
        pairs = {}   # rom matrix -> (run, capture matrix idx)
        for _, vs in near:
            key = tuple(sorted(v[0] for v in vs))
            hit = cap.get(key)
            if not hit:
                continue
            found += 1
            for pos, m in vs:
                for cpos, ci in hit[1]:
                    if cpos == pos:
                        pairs.setdefault(m, (hit[0], ci))
        body_ok = found == len(near)
        # the pose: S(model scale) x G(joint) of some (animation, frame) for every matrix, capture matrices transposed
        best = None
        for an, frames in anims.items():
            for f, Gs in enumerate(frames):
                err = 0.0
                for m, (ri, ci) in pairs.items():
                    cm = runs[name][ri][0][ci]
                    for i in range(4):
                        for j in range(4):
                            want = Gs[m][i][j] * (scale if i < 3 else 1.0)
                            err = max(err, abs(cm[j * 4 + i] - want))
                if best is None or err < best[0]:
                    best = (err, an, f)
        ok_pose = best is not None and best[0] <= 1e-3
        capt = sum(nv // 3 for _, nv, _ in runs[name])
        print("%-9s body %-14s (%s): ROM near-LOD triangles %d, in capture %d (capture total %d); pose: %s"
              % (name, e["file"], e.get("label"), len(near), found, capt,
                 ("%s frame %d, max matrix error %.2g" % (best[1], best[2], best[0])) if best and ok_pose else
                 ("no exported frame within 1e-3 (closest %s frame %d, %.3g): a blended or non-exported pose" % (best[1], best[2], best[0]))
                 if best else "no animation"))
        if not body_ok and not e.get("has_head"):
            bad += 1
        # heads: every vertex of the ROM head (sunglasses left out) occurs in the capture
        hits = []
        for h in heads:
            try:
                hg = rdjson(posixpath.join(cdir, h["gltf"]))
                hb = PK.read(posixpath.join(cdir, h["gltf"])[:-5] + ".bin")
            except OSError:
                continue
            tot = ok = 0
            for mesh in hg["meshes"]:
                for p in mesh["primitives"]:
                    if p.get("extras", {}).get("sunglasses") or p.get("extras", {}).get("lod_far"):
                        continue
                    a = hg["accessors"][p["attributes"]["POSITION"]]
                    v = hg["bufferViews"][a["bufferView"]]
                    vals = struct.unpack_from("<%df" % (a["count"] * 3), hb, v["byteOffset"] + a.get("byteOffset", 0))
                    for i in range(a["count"]):
                        tot += 1
                        ok += tuple(int(round(c)) for c in vals[i * 3:i * 3 + 3]) in capverts
            if tot and ok == tot:
                hits.append("%d %s (%d tris)" % (h["id"], h["name"], h["triangles"]))
        print("          heads whose every vertex occurs in the capture: %s" % (", ".join(hits) if hits else "none"))
    print("%d guard owners; %d failures" % (sum(1 for n in runs if n.startswith("guard ")), bad))
    return 1 if bad else 0


def capture_level(rdir):
    return json.load(open(os.path.join(rdir, "level.json")))["level"]


def open_pack(arg):
    """arg: ge007.passets (tar or folder), or a folder holding it"""
    if os.path.isfile(arg) or (os.path.isdir(arg) and os.path.isfile(os.path.join(arg, "pack.json"))):
        return Pack(arg)
    cand = os.path.join(arg, "ge007.passets")
    if os.path.exists(cand):
        return Pack(cand)
    raise OSError("no asset pack at %s (pass ge007.passets, or the folder holding it)" % arg)


def main():
    global PK
    args = sys.argv[1:]
    mode = args.pop(0) if args and args[0] in ("--setup", "--characters", "--models") else None
    if len(args) < 2:
        print(__doc__)
        return 2
    try:
        PK = open_pack(args[0])
    except OSError as e:
        print("export_vs_levelrec: %s" % e)
        return 2
    export_check.PK = PK
    rdir = args[1]
    if mode == "--setup":
        return compare_setup(rdir, args[2] if len(args) > 2 else "setup.json")
    if mode == "--characters":
        return compare_characters(rdir)
    if mode == "--models":
        return compare_models(rdir)
    level = args[2] if len(args) > 2 else capture_level(rdir)
    g = rdjson("levels/%s/%s.gltf" % (level, level))
    tex, rec_tris = read_levelrec(os.path.join(rdir, "level.rec"))
    rec_by_hash = {t[0]: t for t in tex}

    used = sorted({m["extras"]["texnum"] for m in g.get("materials", [])
                   if m.get("extras", {}).get("texnum", -1) >= 0})
    exact, padded, runtime, close, missing = 0, 0, [], [], []
    for n in used:
        w, h, rgba = read_png(texture_png(n))
        if fnv(rgba, w, h) in rec_by_hash:
            exact += 1
            continue
        if n in RUNTIME_ALTERED:
            runtime.append(n)
            continue
        # The renderer stores some textures padded (54x54 as 56x54, 64x1 as 64x2):
        # identical when the capture's top-left corner equals the decoded texture.
        if any(tw >= w and th >= h and (tw, th) != (w, h)
               and all(trgba[y * tw * 4:y * tw * 4 + w * 4] == rgba[y * w * 4:(y + 1) * w * 4] for y in range(h))
               for (_, tw, th, trgba) in tex):
            padded += 1
            continue
        best = None
        for (_, tw, th, trgba) in tex:
            if (tw, th) != (w, h):
                continue
            diff = max(abs(a - b) for a, b in zip(rgba, trgba))
            if best is None or diff < best:
                best = diff
        (close if best is not None else missing).append((n, w, h, best))
    print("%s textures: %d used, %d identical to level.rec, %d identical in a padded copy, "
          "%d altered by the game at run time %s, %d differing, %d without a match"
          % (level, len(used), exact, padded, len(runtime), runtime, len(close), len(missing)))
    for n, w, h, diff in close[:20]:
        print("  tex %d %dx%d: closest level.rec texture differs by up to %d per channel" % (n, w, h, diff))
    for n, w, h, _ in missing[:20]:
        print("  tex %d %dx%d: no level.rec texture of that size" % (n, w, h))

    mesh_tris = {}
    for node in g["nodes"]:
        room = node.get("extras", {}).get("room")
        if room is None or "mesh" not in node or node.get("extras", {}).get("collision"):
            continue
        t = 0
        for p in g["meshes"][node["mesh"]]["primitives"]:
            t += g["accessors"][p["indices"]]["count"] // 3
        mesh_tris[room] = t
    rooms = sorted(set(mesh_tris) | set(rec_tris))
    bad = [(r, mesh_tris.get(r, 0), rec_tris.get(r, 0)) for r in rooms if mesh_tris.get(r, 0) != rec_tris.get(r, 0)]
    print("%s geometry: %d rooms, glTF %d triangles, level.rec %d triangles, %d rooms differ"
          % (level, len(rooms), sum(mesh_tris.values()), sum(rec_tris.values()), len(bad)))
    for r, a, b in bad[:20]:
        print("  room %d: glTF %d, level.rec %d" % (r, a, b))
    stan_bad = compare_stan(g, rdir)
    return 1 if (missing or bad or close or stan_bad) else 0


if __name__ == "__main__":
    sys.exit(main())
