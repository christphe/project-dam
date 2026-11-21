#!/usr/bin/env python3
"""Lint docs/wiki: report pages whose sources are gone or changed since the page was verified.

  python tools_pc/wiki_lint.py          exit 1 if a page needs a look

Each wiki page starts with a front matter listing `sources:` (repo-relative paths) and `verified:`
(the commit the page was last checked against). See docs/wiki/README.md."""
import os, subprocess, sys

REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
WIKI = os.path.join(REPO, "docs", "wiki")
SKIP = {"README.md", "index.md", "log.md"}


def front_matter(path):
    lines = open(path, encoding="utf-8").read().split("\n")
    if not lines or lines[0] != "---":
        return None
    sources, verified, in_src = [], None, False
    for line in lines[1:]:
        if line == "---":
            return sources, verified
        if line.startswith("sources:"):
            in_src = True
        elif in_src and line.startswith("  - "):
            sources.append(line[4:].split("#")[0].strip())
        else:
            in_src = False
            if line.startswith("verified:"):
                verified = line.split(":", 1)[1].split("#")[0].strip()
    return None


def git(*args):
    return subprocess.run(["git", *args], cwd=REPO, capture_output=True, text=True)


def main():
    bad = 0
    for name in sorted(os.listdir(WIKI)):
        if not name.endswith(".md") or name in SKIP:
            continue
        fm = front_matter(os.path.join(WIKI, name))
        if fm is None:
            print(f"{name}: no front matter (sources / verified)")
            bad += 1
            continue
        sources, verified = fm
        if not verified or git("cat-file", "-e", verified + "^{commit}").returncode:
            print(f"{name}: verified commit '{verified}' not found")
            bad += 1
            continue
        for s in sources:
            if not os.path.exists(os.path.join(REPO, s)):
                print(f"{name}: source gone: {s}")
                bad += 1
        changed = [s for s in sources if os.path.exists(os.path.join(REPO, s))
                   and git("diff", "--quiet", verified, "HEAD", "--", s).returncode]
        if changed:
            print(f"{name}: changed since {verified}: {', '.join(changed)}")
            bad += 1
    print("wiki: ok" if not bad else f"wiki: {bad} item(s) to check")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
