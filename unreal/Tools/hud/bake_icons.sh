#!/bin/sh
# Export the console's icons from the Swift game (GAME-LAYER.md §2.4 "Icons",
# chunk D2) into unreal/Content-src/icons/<name>.png, 192 px with alpha:
#   - the 20 model icons (`Icons.models`), rendered by the Swift game's own
#     SceneKit pass (`AUTOCRAFT_ICON_DUMP`, written while a headless
#     windowshot warms the icons);
#   - the drawn emblems (`autocraft icon up.<upgrade>|act.<action>`).
# Needs `make` in the repo root first (it runs .build/release/Autocraft).
# Rerunnable. Then import: py "<repo>/unreal/Tools/Editor/import_icons.py"
set -e
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../../.." && pwd)
bin="$repo/.build/release/Autocraft"
out="$repo/unreal/Content-src/icons"
[ -x "$bin" ] || { echo "no $bin: run make in $repo first" >&2; exit 1; }
mkdir -p "$out"
tmp=${TMPDIR:-/tmp}/autocraft-bake-icons
mkdir -p "$tmp"
AUTOCRAFT_ICON_DUMP="$out" "$bin" windowshot "$tmp/warm.png" --width 1280 --height 800 --warm 0 >/dev/null
for name in up.minigun up.aegisShield up.novaIgniters up.lifelineReactor act.build act.repair act.strike act.back act.heal; do
    "$bin" icon "$name" "$out/$name.png" >/dev/null
done
ls "$out" | wc -l | xargs echo "icons:"
