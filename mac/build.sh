#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
APP="$ROOT/dist/Reset Countdown.app"
mkdir -p "$APP/Contents/MacOS"
cp "$ROOT/Info.plist" "$APP/Contents/Info.plist"
swiftc -module-cache-path "$ROOT/dist/module-cache" "$ROOT/ResetCountdown.swift" -o "$APP/Contents/MacOS/ResetCountdown" -framework Cocoa
codesign --force --deep --sign - "$APP"
