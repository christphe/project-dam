#!/usr/bin/env python3
"""campaign_lint.py -- the Luau campaign ships no game text (#165).

Every text the campaign shows is the game's own, referenced by id: `text("2C04")` in Luau, `"text:2C04"` in
mission.json. So a string literal in campaign/ may only be:

  - a 4-hex-digit text id ("2C04"), or "text:" + one in mission.json;
  - a lowercase identifier: event and field names, animation and speed names, module names
    ([a-z0-9_.:-], no space, no capital).

mission.json / campaign.json keys whose value is shown to the player ("name", "description", "briefing",
objectives' "text") must hold a text reference or be empty. Anything else is flagged.

Usage: campaign_lint.py <repo>          Exit status 1 when something is flagged.
"""
import json
import os
import re
import sys

ID = re.compile(r'^[0-9A-F]{4}$')
IDENT = re.compile(r'^[a-z0-9_.:\-]*$')
TEXTREF = re.compile(r'^text:[0-9A-F]{4}$')
SHOWN = {'name', 'description', 'briefing', 'text', 'author'}


def lua_strings(src):
    """(line, string) for every short string literal; comments skipped. Long strings [[...]] are flagged whole."""
    out = []
    i, line, n = 0, 1, len(src)
    while i < n:
        c = src[i]
        if c == '\n':
            line += 1
            i += 1
        elif src.startswith('--', i):
            m = re.match(r'--\[(=*)\[', src[i:])
            if m:
                end = src.find(']' + m.group(1) + ']', i)
                end = n if end < 0 else end
                line += src.count('\n', i, end)
                i = end + 2 + len(m.group(1))
            else:
                j = src.find('\n', i)
                i = n if j < 0 else j
        elif c == '[' and re.match(r'\[(=*)\[', src[i:]):
            m = re.match(r'\[(=*)\[', src[i:])
            end = src.find(']' + m.group(1) + ']', i)
            end = n if end < 0 else end
            out.append((line, src[i + len(m.group(0)):end]))
            line += src.count('\n', i, end)
            i = end + 2 + len(m.group(1))
        elif c in '"\'`':
            j, buf = i + 1, []
            while j < n and src[j] != c and src[j] != '\n':
                if src[j] == '\\' and j + 1 < n:
                    buf.append(src[j:j + 2])
                    j += 2
                else:
                    buf.append(src[j])
                    j += 1
            out.append((line, ''.join(buf)))
            i = j + 1
        else:
            i += 1
    return out


def check_lua(path):
    bad = []
    for line, s in lua_strings(open(path, encoding='utf-8').read()):
        if not (ID.match(s) or IDENT.match(s)):
            bad.append((line, 'string literal %r: reference game text with text("XXXX")' % s[:40]))
    return bad


def check_json(path):
    bad = []

    def walk(v, key):
        if isinstance(v, dict):
            for k, x in v.items():
                walk(x, k)
        elif isinstance(v, list):
            for x in v:
                walk(x, key)
        elif isinstance(v, str) and key in SHOWN and v and not TEXTREF.match(v):
            bad.append((0, '"%s": %r: use "text:XXXX"' % (key, v[:40])))

    walk(json.load(open(path, encoding='utf-8')), None)
    return bad


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    root = os.path.join(sys.argv[1], 'campaign')
    total = files = 0
    for d, _, names in os.walk(root):
        for f in sorted(names):
            p = os.path.join(d, f)
            if f.endswith('.luau'):
                bad = check_lua(p)
            elif f in ('mission.json',):
                bad = check_json(p)
            else:
                continue
            files += 1
            for line, why in bad:
                print('%s:%d: %s' % (os.path.relpath(p, sys.argv[1]), line, why))
                total += 1
    print('%d file(s), %d problem(s)' % (files, total))
    return 1 if total else 0


if __name__ == '__main__':
    sys.exit(main())
