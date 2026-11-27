#!/usr/bin/env python3
"""Compare a static glTF level export (ge007-export, issue #150) with the
game's own capture of the same level (`ge007 -level_NN --export-level DIR`,
level.rec, issue #102).

  python tools_pc/export_vs_levelrec.py <gltf_level_dir> <levelrec_dir>

Checks:
  * textures: every texture the glTF uses is found among the level.rec
    textures with identical RGBA pixels (by the level.rec FNV-1a hash), or
    the closest same-size texture is reported with its max channel difference;
  * geometry: triangles per room, glTF node "room_<n>" vs the level.rec runs
    owned by room n.

  python tools_pc/export_vs_levelrec.py --models <models_dir> <levelrec_dir>

compares every object model of the level.rec (owners of kind 1, "object <n>")
with <models_dir>/models.json (ge007-export, issue #119): triangle count of the
default scene and the set of textures (matched by pixels, padded copies
included).

Both inputs are ROM-derived and stay in the user's data folders. Stdlib only.
"""
import json
import os
import struct
import sys
import zlib

FNV_OFF, FNV_PRIME, MASK = 1469598103934665603, 1099511628211, (1 << 64) - 1

# Textures the game rewrites while the level runs (docs/specs/textures.md): water.
RUNTIME_ALTERED = {1511}


def fnv(rgba, w, h):
    x = FNV_OFF
    for b in rgba:
        x = ((x ^ b) * FNV_PRIME) & MASK
    x = ((x ^ w) * FNV_PRIME) & MASK
    return ((x ^ h) * FNV_PRIME) & MASK


def read_png(path):
    data = open(path, "rb").read()
    assert data[:8] == b"\x89PNG\r\n\x1a\n", path
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


def compare_models(mdir, rdir):
    idx = json.load(open(os.path.join(mdir, "models.json")))["models"]
    tex, rec = read_levelrec_models(os.path.join(rdir, "level.rec"))
    bad = 0
    print("model file                      tris(ours/capture)  textures(ours/capture)  match")
    for n in sorted(rec):
        e = idx[n]
        e.setdefault("triangles", -1)
        ctris, ctex = rec[n]
        ours = []   # (texnum, hash, w, h, rgba)
        for tn in e.get("textures", []):
            w, h, rgba = read_png(os.path.join(mdir, os.path.dirname(e["gltf"]), "textures", "%d.png" % tn))
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


def main():
    if sys.argv[1] == "--models":
        return compare_models(sys.argv[2], sys.argv[3])
    gdir, rdir = sys.argv[1], sys.argv[2]
    level = os.path.basename(os.path.normpath(gdir))
    g = json.load(open(os.path.join(gdir, level + ".gltf")))
    tex, rec_tris = read_levelrec(os.path.join(rdir, "level.rec"))
    rec_by_hash = {t[0]: t for t in tex}

    used = sorted({m["extras"]["texnum"] for m in g.get("materials", [])
                   if m.get("extras", {}).get("texnum", -1) >= 0})
    exact, padded, runtime, close, missing = 0, 0, [], [], []
    for n in used:
        w, h, rgba = read_png(os.path.join(gdir, "textures", "%d.png" % n))
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
