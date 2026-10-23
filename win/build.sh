#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
: "${CXX:=x86_64-w64-mingw32-clang++}"
mkdir -p "$ROOT/dist"
"$CXX" -std=c++17 -Os -ffunction-sections -fdata-sections -static -municode -mwindows \
  -Wl,--gc-sections -s "$ROOT/src/main.cpp" -o "$ROOT/dist/TiboReset.exe" \
  -ladvapi32 -ld2d1 -ldwrite -lole32 -luuid -lcomctl32 -lshell32 -lgdi32 -luser32
