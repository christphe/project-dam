#!/usr/bin/env python3
"""Merge combiner ids from game logs into the shader-pack lists (D973).

Usage: python -I tools_pc/cc_census_merge.py <combiners.txt> <log> [<log> ...]

Reads the `CCID <id0 hex16> <id1 hex8>` lines a run prints with GE_CCLOG=1
(port/fast3d/gfx_frame.cpp), the `DKSH miss <id0> <id1>` lines the deko3d
backend prints for a combiner missing from the shipped shader pack
(port/fast3d/gfx_deko3d.cpp) and the `SPIR-V miss` lines of the Vulkan one.
Each id goes to the list it belongs in:
  - combiners.txt (the argument): the game's combiners;
  - fx/combiners.txt next to it: the port's effects, an id whose id1 carries a
    PORTFX_* kind (bits 28..31, SHADER_OPT_FX_MASK). They get their effect code
    from port/shaders/fx/ (tools_pc/dkshgen); the uber shader never draws them.
Each list keeps one `0x<16 hex> 0x<8 hex>` pair per line, sorted and unique;
`#` comment lines at the top of a file are kept. Prints how many pairs were added.
"""
import os
import re
import sys

PAT = re.compile(r"(?:CCID|DKSH miss|SPIR-V miss)\s+(?:0x)?([0-9a-fA-F]{1,16})\s+(?:0x)?([0-9a-fA-F]{1,8})\b")
LINE = re.compile(r"^\s*0x([0-9a-fA-F]{1,16})\s+0x([0-9a-fA-F]{1,8})\s*(?:#.*)?$")
FX_MASK = 0xF0000000   # SHADER_OPT_FX_MASK (port/fast3d/gfx_cc.h)

HEADERS = {
    "game": ["# deko3d / Vulkan shader packs: the game's combiner ids (D973). One pair per line: 0x<id0> 0x<id1>.",
             "# Merged from GE_CCLOG=1 / DKSH miss log lines by tools_pc/cc_census_merge.py."],
    "fx": ["# The port's effect combiners (ids with a PORTFX_* kind, SHADER_OPT_FX_MASK), for the deko3d and Vulkan shader packs.",
           "# tools_pc/dkshgen gives each its own shader: fx/common.glsl, then its family's file (bullets, bursts, impacts).",
           "# Merged from GE_CCLOG=1 / DKSH miss log lines by tools_pc/cc_census_merge.py."],
}


def load(path, kind):
    header, pairs = [], set()
    try:
        with open(path, "r", encoding="utf-8") as f:
            for raw in f:
                line = raw.rstrip("\n")
                m = LINE.match(line)
                if m:
                    pairs.add((int(m.group(1), 16), int(m.group(2), 16)))
                elif line.strip().startswith("#") or not line.strip():
                    if not pairs:
                        header.append(line)
                else:
                    print("warning: %s: ignoring line %r" % (path, line), file=sys.stderr)
    except FileNotFoundError:
        header = list(HEADERS[kind])
    while header and not header[-1].strip():
        header.pop()
    return header, pairs


def save(path, header, pairs):
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        for h in header:
            f.write(h + "\n")
        for id0, id1 in sorted(pairs):
            f.write("0x%016x 0x%08x\n" % (id0, id1))


def main(argv):
    if len(argv) < 3:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    paths = {"game": argv[1], "fx": os.path.join(os.path.dirname(os.path.abspath(argv[1])), "fx", "combiners.txt")}
    lists = {k: load(p, k) for k, p in paths.items()}
    # an id filed in the wrong list moves to the right one
    for k, other in (("game", "fx"), ("fx", "game")):
        wrong = {p for p in lists[k][1] if bool(p[1] & FX_MASK) != (k == "fx")}
        lists[k][1].difference_update(wrong)
        lists[other][1].update(wrong)
    before = {k: len(v[1]) for k, v in lists.items()}
    for log in argv[2:]:
        with open(log, "r", encoding="utf-8", errors="replace") as f:
            for line in f:
                for m in PAT.finditer(line):
                    pair = (int(m.group(1), 16), int(m.group(2), 16))
                    lists["fx" if pair[1] & FX_MASK else "game"][1].add(pair)
    for k, (header, pairs) in lists.items():
        if k == "fx" and not pairs and not os.path.exists(paths[k]):
            continue
        save(paths[k], header, pairs)
        print("%s: %d pairs (+%d)" % (paths[k], len(pairs), len(pairs) - before[k]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
