#!/bin/sh
# Pack a mod source folder into a .pmod: a plain, uncompressed ustar tar of the folder's contents (docs/modding/README.md),
# without dotfiles and without a top-level dist/ (the editor's Publish does the same).
#
#   tools/pack_mod.sh <mod folder> <out.pmod>
set -eu
SRC="${1:?usage: pack_mod.sh <mod folder> <out.pmod>}"
OUT="${2:?usage: pack_mod.sh <mod folder> <out.pmod>}"
[ -f "$SRC/campaign.json" ] || { echo "error: $SRC has no campaign.json" >&2; exit 1; }
mkdir -p "$(dirname "$OUT")"
rm -f "$OUT"
LIST="$(pwd)/.pack_mod.list"   # absolute: tar -C changes directory before it reads -T
(cd "$SRC" && find . -path ./dist -prune -o -name '.?*' -prune -o -type f -print | sed 's|^\./||' | LC_ALL=C sort > "$LIST")
LOCAL=""
tar --version 2>/dev/null | grep -q GNU && LOCAL="--force-local"   # GNU tar reads "C:..." as a remote host otherwise
tar $LOCAL --format=ustar -cf "$OUT" -C "$SRC" -T "$LIST"
rm -f "$LIST"
echo "$OUT ($(wc -c < "$OUT") bytes)"
