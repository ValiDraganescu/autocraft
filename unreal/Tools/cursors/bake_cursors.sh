#!/bin/sh
# Bake the four game cursors from swift/GameCursor.swift (the Swift game's) (see
# main.swift). Rerunnable:  sh unreal/Tools/cursors/bake_cursors.sh
set -e
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../../.." && pwd)
build=${TMPDIR:-/tmp}/autocraft-bake-cursors
mkdir -p "$build"
swiftc -O -module-name BakeCursors -o "$build/bake_cursors" \
    "$here/swift/GameCursor.swift" \
    "$here/main.swift"
"$build/bake_cursors" "$repo/unreal/Content/UI/Cursors" "$repo/unreal/Content-src/cursors"
