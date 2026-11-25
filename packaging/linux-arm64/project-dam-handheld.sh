#!/bin/sh
# Project Dam, Linux arm64: handheld mode, for a device with its own small
# screen and built-in pad: console mode (no keyboard or mouse, fullscreen) plus
# the small-screen settings (the aim zoom). Arguments are passed through.
cd "$(dirname "$0")" || exit 1
GE_CONSOLE=1 GE_HANDHELD=1 exec ./ge007.aarch64 "$@"
