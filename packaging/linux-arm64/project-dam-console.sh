#!/bin/sh
# Project Dam, Linux arm64: console mode, for a box on a TV played with pads
# (a Raspberry Pi): no keyboard or mouse, fullscreen, the pad menu.
# Plain ./ge007.aarch64 is the desktop mode; project-dam-handheld.sh is for a
# handheld's small screen. Arguments are passed through to the game.
cd "$(dirname "$0")" || exit 1
GE_CONSOLE=1 exec ./ge007.aarch64 "$@"
