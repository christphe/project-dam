#!/bin/sh
# Cross-build ge007-audio-export and midiplay for Windows inside the windows image and
# put the runtime DLLs next to the executables:
#
#   docker compose run --rm windows sh tools_pc/midiplay/docker-build-win.sh
#
# Output: build-audio-win/ge007-audio-export.exe, build-midiplay-win/midiplay.exe
set -eu
SYSROOT=/usr/x86_64-w64-mingw32/sys-root/mingw
export SDL2DIR="${SDL2DIR:-${SYSROOT}}"
TOOLCHAIN="${TOOLCHAIN:-/usr/share/mingw/toolchain-mingw64.cmake}"

cmake -S port/export/audio -B build-audio-win -G Ninja -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN}"
cmake --build build-audio-win
cmake -S tools_pc/midiplay -B build-midiplay-win -G Ninja -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN}"
cmake --build build-midiplay-win

cp -f "${SYSROOT}/bin/zlib1.dll" build-audio-win/
cp -f "${SYSROOT}/bin/SDL2.dll" build-midiplay-win/
