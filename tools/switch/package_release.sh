#!/bin/sh
# project-dam: assemble the release archive from a finished Switch build.
#
#   tools/switch/package_release.sh <version> [out_dir]
#
# Run from the repo root after the NRO is built (build-switch/). Produces
# <out_dir>/project-dam-<version>-switch.zip with:
#
#   switch/project-dam/project-dam.nro   copy switch/ to the root of the SD card
#   switch/project-dam/mods/campaign.pmod  the solo campaign in Luau (#165), a mod like any other
#   README.txt, LICENSE, NOTICE
#
# No game content: players supply their own ROM; the game converts it on
# first launch.
set -eu

VERSION="${1:?usage: package_release.sh <version> [out_dir]}"
OUT="${2:-dist}"
NRO=build-switch/ge007.aarch64.nro
[ -f "$NRO" ] || { echo "error: $NRO missing; build the NRO first" >&2; exit 1; }

NAME="project-dam-$VERSION-switch"
STAGE="$OUT/$NAME"
rm -rf "$STAGE"
mkdir -p "$STAGE/switch/project-dam/data" "$STAGE/switch/project-dam/mods"

cp "$NRO" "$STAGE/switch/project-dam/project-dam.nro"
# The in-game update downloads this file by name; the release carries it next
# to the zip and GitHub lists its sha256.
cp "$NRO" "$OUT/project-dam.nro"

# The Luau campaign (#165): campaign/ packed as a .pmod (a plain ustar tar of the mod folder, docs/modding).
sh tools/pack_mod.sh campaign "$STAGE/switch/project-dam/mods/campaign.pmod"

cp LICENSE NOTICE "$STAGE/"
sed "s/@VERSION@/$VERSION/g" tools/switch/README-release.txt > "$STAGE/README.txt"

(cd "$OUT" && rm -f "$NAME.zip" && zip -qr "$NAME.zip" "$NAME")
echo "$OUT/$NAME.zip"
