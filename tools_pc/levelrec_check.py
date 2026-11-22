#!/usr/bin/env python3
"""Check a level export (`ge007 -level_XX --export-level DIR`, issue #102).

    python tools_pc/levelrec_check.py DIR [--png OUT.png] [--size 1024]

Reads DIR/level.rec (format: port/levelrec/levelrec.h) and DIR/level.json, prints
the counts and renders a top-down PNG of every recorded triangle (world x/z, lower
geometry first, shaded by height), the stan floor tiles (green), the portals (blue)
and the pads (red). Pure Python, standard library only. The export is ROM-derived:
keep it, and the PNG, out of the repository.
"""
import argparse
import json
import os
import struct
import sys
import zlib

HDR = struct.Struct("<II64s32sIII")
TEX = struct.Struct("<QHH")
OWNER = struct.Struct("<Ii32s")
RUN = struct.Struct("<IQI2i" "2B2B2B2B" "4B" "H" "2B" "2B" "4f" "2f" "32f32f32f" "2B" "3I")
MAGIC = 0x524C4547
OWNER_KIND = {0: "room", 1: "model"}


def parse_rec(path):
    data = open(path, "rb").read()
    magic, version, build, level, ntex, nown, nruns = HDR.unpack_from(data, 0)
    if magic != MAGIC or version != 1:
        raise SystemExit(f"{path}: not a version 1 level.rec (magic {magic:#x}, version {version})")
    off = HDR.size
    textures = []
    for _ in range(ntex):
        h, w, ht = TEX.unpack_from(data, off)
        off += TEX.size
        textures.append((h, w, ht))
        off += w * ht * 4
    owners = []
    for _ in range(nown):
        kind, ident, name = OWNER.unpack_from(data, off)
        off += OWNER.size
        owners.append((kind, ident, name.split(b"\0")[0].decode("ascii", "replace")))
    runs = []
    for _ in range(nruns):
        f = RUN.unpack_from(data, off)
        off += RUN.size
        owner, nverts, nmtx, nfloats = f[0], f[-3], f[-2], f[-1]
        mtx = struct.unpack_from(f"<{nmtx * 16}f", data, off)
        off += nmtx * 64
        verts = struct.unpack_from(f"<{nfloats}f", data, off)
        off += nfloats * 4
        runs.append((owner, f, nverts, mtx, verts))
    if off != len(data):
        raise SystemExit(f"{path}: {len(data) - off} trailing bytes")
    return dict(build=build.split(b"\0")[0].decode(), level=level.split(b"\0")[0].decode(),
                textures=textures, owners=owners, runs=runs)


def world_triangles(rec):
    """(owner, [(x, y, z) * 3]) for every triangle of every run."""
    for owner, f, nverts, mtx, verts in rec["runs"]:
        if nverts == 0:
            continue
        stride = len(verts) // nverts
        pts = []
        for v in range(nverts):
            b = v * stride
            x, y, z, slot = verts[b], verts[b + 1], verts[b + 2], int(verts[b + 3])
            m = mtx[slot * 16:slot * 16 + 16]
            pts.append((x * m[0] + y * m[4] + z * m[8] + m[12],
                        x * m[1] + y * m[5] + z * m[9] + m[13],
                        x * m[2] + y * m[6] + z * m[10] + m[14]))
        for t in range(0, nverts - 2, 3):
            yield owner, pts[t:t + 3]


class Canvas:
    def __init__(self, w, h):
        self.w, self.h = w, h
        self.px = bytearray(bytes((16, 16, 20)) * (w * h))

    def put(self, x, y, c):
        if 0 <= x < self.w and 0 <= y < self.h:
            i = (y * self.w + x) * 3
            self.px[i:i + 3] = bytes(c)

    def line(self, x0, y0, x1, y1, c):
        x0, y0, x1, y1 = int(x0), int(y0), int(x1), int(y1)
        dx, dy = abs(x1 - x0), -abs(y1 - y0)
        sx, sy = (1 if x0 < x1 else -1), (1 if y0 < y1 else -1)
        err = dx + dy
        for _ in range(4096):
            self.put(x0, y0, c)
            if x0 == x1 and y0 == y1:
                break
            e2 = 2 * err
            if e2 >= dy:
                err += dy
                x0 += sx
            if e2 <= dx:
                err += dx
                y0 += sy

    def tri(self, p, c):
        (x0, y0), (x1, y1), (x2, y2) = p
        ymin, ymax = max(0, int(min(y0, y1, y2))), min(self.h - 1, int(max(y0, y1, y2)))
        edges = [((x0, y0), (x1, y1)), ((x1, y1), (x2, y2)), ((x2, y2), (x0, y0))]
        col = bytes(c)
        for y in range(ymin, ymax + 1):
            xs = []
            yc = y + 0.5
            for (ax, ay), (bx, by) in edges:
                if (ay <= yc < by) or (by <= yc < ay):
                    xs.append(ax + (yc - ay) * (bx - ax) / (by - ay))
            if len(xs) >= 2:
                a, b = max(0, int(min(xs))), min(self.w - 1, int(max(xs)))
                if b >= a:
                    i = (y * self.w + a) * 3
                    self.px[i:i + (b - a + 1) * 3] = col * (b - a + 1)

    def png(self, path):
        raw = b"".join(b"\0" + bytes(self.px[y * self.w * 3:(y + 1) * self.w * 3]) for y in range(self.h))

        def chunk(t, d):
            c = struct.pack(">I", len(d)) + t + d
            return c + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
        with open(path, "wb") as f:
            f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", self.w, self.h, 8, 2, 0, 0, 0))
                    + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dir")
    ap.add_argument("--png", help="output PNG (default DIR/levelrec_topdown.png)")
    ap.add_argument("--size", type=int, default=1024)
    a = ap.parse_args()

    rec = parse_rec(os.path.join(a.dir, "level.rec"))
    js = json.load(open(os.path.join(a.dir, "level.json"), encoding="utf-8"))
    tris = list(world_triangles(rec))
    room_owners = {o for o, (k, _, _) in enumerate(rec["owners"]) if k == 0}
    rooms_with_geo = {rec["owners"][o][1] for o, _ in tris if o in room_owners}
    json_rooms = [r["index"] for r in js["rooms"]]
    missing = [r for r in json_rooms if r not in rooms_with_geo]
    refs = {t for _, f, _, _, _ in rec["runs"] for t in f[3:5] if t >= 0}
    print(f"level      {rec['level']}  (build {rec['build']})")
    print(f"textures   {len(rec['textures'])}   referenced by runs: {len(refs)}")
    print(f"owners     {len(rec['owners'])}   rooms {sum(1 for k, _, _ in rec['owners'] if k == 0)}, "
          f"models {sum(1 for k, _, _ in rec['owners'] if k == 1)}")
    print(f"runs       {len(rec['runs'])}   triangles {len(tris)}")
    print(f"rooms      {len(json_rooms)} in level.json, {len(rooms_with_geo)} with geometry; no geometry: {missing}")
    print(f"stan       {len(js['stan'])} tiles   portals {len(js['portals'])}")
    print(f"pads       {len(js['pads'])}   props {len(js['props'])}   ailists {len(js['ailists'])}")
    owned = sum(1 for p in js["props"] if p["owner"] >= 0)
    print(f"props with a model owner: {owned}")

    if not tris and not js["stan"]:
        return
    xs = [p[0] for _, t in tris for p in t] + [v[0] for s in js["stan"] for v in s["v"]]
    zs = [p[2] for _, t in tris for p in t] + [v[2] for s in js["stan"] for v in s["v"]]
    x0, x1, z0, z1 = min(xs), max(xs), min(zs), max(zs)
    n = a.size
    sc = (n - 8) / max(x1 - x0, z1 - z0, 1.0)

    def pt(x, z):
        return (4 + (x - x0) * sc, n - 4 - (z - z0) * sc)

    cv = Canvas(n, n)
    ys = [(sum(p[1] for p in t) / 3, o, t) for o, t in tris if o in room_owners]
    if ys:
        ylo, yhi = min(y for y, _, _ in ys), max(y for y, _, _ in ys)
        for y, o, t in sorted(ys, key=lambda e: e[0]):
            g = 40 + int(170 * (y - ylo) / max(yhi - ylo, 1e-6))
            cv.tri([pt(p[0], p[2]) for p in t], (g, g, g))
    for s in js["stan"]:
        v = [pt(p[0], p[2]) for p in s["v"]]
        for i in range(len(v)):
            cv.line(*v[i], *v[(i + 1) % len(v)], (60, 200, 90))
    for p in js["portals"]:
        v = [pt(q[0], q[2]) for q in p["v"]]
        for i in range(len(v)):
            cv.line(*v[i], *v[(i + 1) % len(v)], (70, 130, 255))
    for p in js["pads"]:
        x, y = pt(p["pos"][0], p["pos"][2])
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                cv.put(int(x) + dx, int(y) + dy, (255, 60, 60))
    out = a.png or os.path.join(a.dir, "levelrec_topdown.png")
    cv.png(out)
    print("top-down PNG:", out)


if __name__ == "__main__":
    main()
