#!/usr/bin/env python3
"""campaign_spec_lint.py -- keep the campaign behaviour specs clean-room safe (#165).

A spec (docs/campaign/specs/*.md) describes what a level does for the player. It must not carry the
original AI scripts' form: no AI command names, no label numbers, no quoted game text. This lint flags:

  - any identifier that is an AI command macro of src/bondaicommands.h or src/aicommands2.h
    (for example a snake_case command name, or a CamelCase macro of the Archives setup);
  - "label" followed by a number (the scripts' jump targets);
  - a quoted string of four words or more (game text goes in as a 4-hex-digit text id instead);
  - a dialogue gist that paraphrases the line, "(Natalya: ...)": a line is its speaker and, at most, its
    function ("(Natalya line)", "(Natalya line: asks for time)");
  - five consecutive words shared with a game text of the decompiled English banks (assets/obseg/text/*E.c),
    when they are there. Generic phrases can trip it; reword them anyway. Only line numbers are printed.

Usage: campaign_spec_lint.py <repo> [spec.md ...]     (no spec: every docs/campaign/specs/*.md)
Exit status 1 when something is flagged.
"""
import os
import re
import sys


def command_names(repo):
    names = set()
    for rel in ('src/bondaicommands.h', 'src/aicommands2.h'):
        path = os.path.join(repo, rel)
        if not os.path.exists(path):
            continue
        for m in re.finditer(r'^#define\s+([A-Za-z_]\w*)', open(path, encoding='utf-8', errors='replace').read(), re.M):
            n = re.sub(r'_(ID|LENGTH)$', '', m.group(1))
            # Short or generic macro names (THIS, DO, LOOP) would flag ordinary prose: keep the distinctive ones.
            if len(n) >= 8 and ('_' in n or re.search(r'[a-z][A-Z]', n)):
                names.add(n)
    return names


SPEAKER = re.compile(r'\((?:Bond|Natalya|Valentin|Mishkin|Trevelyan|Xenia|Ourumov|Boris|Jaws|Baron|Samedi|Doak|'
                     r'Moneypenny|[Jj]ailer|[Gg]uard|Kalinin|Dimitri|Janus|Q|M): ')


def words(s):
    return re.sub(r'[^a-z0-9 ]+', ' ', s.lower()).split()


def text_grams(repo):
    grams = set()
    d = os.path.join(repo, 'assets/obseg/text')
    if not os.path.isdir(d):
        return grams
    for f in os.listdir(d):
        if not f.endswith('E.c'):
            continue
        for s in re.findall(r'"((?:[^"\\]|\\.)*)"', open(os.path.join(d, f), encoding='utf-8', errors='replace').read()):
            w = words(s.replace('\\n', ' '))
            for i in range(len(w) - 4):
                grams.add(tuple(w[i:i + 5]))
    return grams


ALLOWED_GRAMS = {('at', 'the', 'bottom', 'of', 'the')}   # generic phrases the check cannot tell from game text


def lint(path, names, grams=frozenset()):
    bad = []
    in_code = False
    for no, line in enumerate(open(path, encoding='utf-8'), 1):
        if line.lstrip().startswith('```'):
            in_code = not in_code
        if SPEAKER.search(line):
            bad.append((no, 'dialogue gist paraphrases the line: keep the speaker only ("(Natalya line)")'))
        w = words(line)
        if any(tuple(w[i:i + 5]) in grams and tuple(w[i:i + 5]) not in ALLOWED_GRAMS for i in range(len(w) - 4)):
            bad.append((no, 'five words in a row shared with a game text: reword'))
        for tok in set(re.findall(r'[A-Za-z_]\w{7,}', line)):
            if tok in names:
                bad.append((no, 'AI command name: %s' % tok))
        if re.search(r'\blabel\s*\(?\s*(0x)?[0-9a-fA-F]+', line, re.I):
            bad.append((no, 'label number'))
        for q in re.findall(r'["“«]([^"”»]+)["”»]', line):
            if len(q.split()) >= 4:
                bad.append((no, 'quoted text of %d words (use a text id)' % len(q.split())))
    return bad


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    repo = sys.argv[1]
    specs = sys.argv[2:]
    if not specs:
        d = os.path.join(repo, 'docs/campaign/specs')
        specs = sorted(os.path.join(d, f) for f in os.listdir(d) if f.endswith('.md') and f != 'README.md')
    names = command_names(repo)
    grams = text_grams(repo)
    total = 0
    for s in specs:
        for no, why in lint(s, names, grams):
            print('%s:%d: %s' % (s, no, why))
            total += 1
    print('%d spec(s), %d problem(s)' % (len(specs), total))
    return 1 if total else 0


if __name__ == '__main__':
    sys.exit(main())
