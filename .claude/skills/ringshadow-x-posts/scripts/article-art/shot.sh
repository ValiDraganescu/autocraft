#!/bin/bash
# shot.sh PAGE.html W H: renders PAGE.html to PAGE.png beside it with headless Chrome, at W×H.
# Copies base.css and the HUD fonts (unreal/Content-src/fonts) beside the page first.
set -e
here="$(cd "$(dirname "$0")" && pwd)"
fonts="$here/../../../../../unreal/Content-src/fonts"
dir="$(cd "$(dirname "$1")" && pwd)"; name="$(basename "$1" .html)"
[ -f "$dir/base.css" ] || cp "$here/base.css" "$dir/"
for f in BarlowCondensed-Medium BarlowCondensed-SemiBold BarlowCondensed-Bold BarlowCondensed-ExtraBold Exo2-Variable; do
  [ -f "$dir/$f.ttf" ] || cp "$fonts/$f.ttf" "$dir/"
done
"/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" --headless=new --disable-gpu --hide-scrollbars \
  --user-data-dir="$dir/.chrome" --force-device-scale-factor=1 --allow-file-access-from-files \
  --window-size=$2,$3 --screenshot="$dir/$name.png" "file://$dir/$name.html" 2>/dev/null
ls -l "$dir/$name.png" | awk '{print $5, $9}'
