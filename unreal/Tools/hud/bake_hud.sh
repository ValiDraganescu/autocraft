#!/bin/sh
# Bake the HUD art (plates, resource icons) from the Swift game's drawing
# code (copied into swift/ when the Swift game left the repo) into unreal/Content-src/hud/ (see main.swift). Rerunnable.
#   sh unreal/Tools/hud/bake_hud.sh
# Then import in the editor: py "<repo>/unreal/Tools/Editor/import_hud.py"
set -e
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../../.." && pwd)
build=${TMPDIR:-/tmp}/autocraft-bake-hud
mkdir -p "$build"
swiftc -O -module-name BakeHud -o "$build/bake_hud" \
    "$here/swift/HUDChrome.swift" \
    "$here/swift/ResourceArt.swift" \
    "$here/main.swift"
"$build/bake_hud" "$repo/unreal/Content-src/hud"
