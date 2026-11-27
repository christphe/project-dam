#!/usr/bin/env python3
"""Keep the pinned build-image reference identical everywhere.

The image built from Dockerfile.build is published to GHCR by
.github/workflows/build-image.yml. ci.yml, compose.yaml, tools/dev.py and
tools_pc/fxview/fxview.py each carry its digest-pinned reference; this checks
that all four have one and that they agree.

  python tools/check_build_image.py            # exit 1 on a missing or differing reference
  python tools/check_build_image.py --print    # print the reference
  python tools/check_build_image.py --set REF  # rewrite all four files to REF
"""
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
FILES = [".github/workflows/ci.yml", "compose.yaml", "tools/dev.py", "tools_pc/fxview/fxview.py"]
REF = r"ghcr\.io/christphe/project-dam-build@sha256:[0-9a-f]{64}"


def refs(path):
    with open(REPO / path, newline="") as f:
        return re.findall(REF, f.read())


def main(argv):
    if len(argv) == 3 and argv[1] == "--set":
        if not re.fullmatch(REF, argv[2]):
            sys.exit("--set expects ghcr.io/christphe/project-dam-build@sha256:<64 hex>")
        for p in FILES:
            with open(REPO / p, newline="") as f:
                s = f.read()
            n = re.sub(REF, argv[2], s)
            with open(REPO / p, "w", newline="") as f:
                f.write(n)
            print(f"{p}: {len(re.findall(REF, n))} reference(s) set")
        return 0
    found = {p: set(refs(p)) for p in FILES}
    bad = False
    for p, r in found.items():
        if not r:
            print(f"{p}: no build image reference")
            bad = True
    allrefs = set().union(*found.values())
    if len(allrefs) > 1:
        print("build image references differ:")
        for p, r in found.items():
            print(f"  {p}: {', '.join(sorted(r)) or '-'}")
        bad = True
    if bad:
        return 1
    ref = next(iter(allrefs))
    print(ref if "--print" in argv else f"ok: {ref}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
