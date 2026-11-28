#!/bin/sh
# project-dam: assemble the Windows release archive from a finished
# cross-compiled build (tools_pc/docker-build-win.sh, Dockerfile.windows).
#
#   tools/switch/package_release_win.sh <version> [out_dir] [build_dir]
#
# Produces <out_dir>/project-dam-<version>-windows-x64.zip with the exe, the
# runtime DLLs docker-build-win.sh copied next to it, README.txt, licenses/ and
# an empty data/ (where the ROM goes).
# The "-windows" in the name is what the PC update check looks for
# (port/include/updatecheck_parse.h hasPcAsset). No game content.
set -eu

VERSION="${1:?usage: package_release_win.sh <version> [out_dir] [build_dir]}"
OUT="${2:-dist}"
BUILD="${3:-build-win}"
EXE="$BUILD/ge007.x86_64.exe"
[ -f "$EXE" ] || { echo "error: $EXE missing; build it first" >&2; exit 1; }

NAME="project-dam-$VERSION-windows-x64"
STAGE="$OUT/$NAME"
rm -rf "$STAGE"
mkdir -p "$STAGE/licenses" "$STAGE/data" "$STAGE/mods"
# The Luau campaign (#165): campaign/ packed as a .pmod next to the exe (mods/).
sh tools/pack_mod.sh campaign "$STAGE/mods/campaign.pmod"

cp "$EXE" "$STAGE/"
for dll in "$BUILD"/*.dll; do
    [ -f "$dll" ] && cp "$dll" "$STAGE/" && echo "bundled $(basename "$dll")"
done

sed "s/@VERSION@/$VERSION/g" tools/switch/README-windows.txt > "$STAGE/README.txt"
cp LICENSE "$STAGE/licenses/LICENSE"
cp NOTICE "$STAGE/licenses/NOTICE"
[ -f port/fast3d/LICENSE.txt ] && cp port/fast3d/LICENSE.txt "$STAGE/licenses/LICENSE-fast3d.txt"
# Bundled runtime: SDL2 (zlib license), libgcc/libstdc++ (GPL-3.0 with the GCC
# Runtime Library Exception); zlib and winpthread are listed in NOTICE.
cp tools_pc/dist/SDL2-LICENSE.txt "$STAGE/licenses/SDL2-LICENSE.txt"
cp tools_pc/dist/GPL-3.0.txt "$STAGE/licenses/gcc-COPYING3"
cp tools_pc/dist/GCC-RUNTIME-EXCEPTION.txt "$STAGE/licenses/gcc-COPYING.RUNTIME"

if find "$STAGE" -type f \( -iname '*.z64' -o -iname '*.n64' -o -iname '*.v64' \) | grep -q .; then
    echo "error: the archive would contain a ROM image" >&2; exit 1
fi

(cd "$OUT" && rm -f "$NAME.zip" && zip -qr "$NAME.zip" "$NAME")
echo "$OUT/$NAME.zip"
