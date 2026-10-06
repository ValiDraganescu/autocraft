#!/usr/bin/env bash
set -euo pipefail

SKILL_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REPO_ROOT="$(cd "$SKILL_DIR/../../.." && pwd)"
WORKSPACE="$REPO_ROOT/video"
PROJECT="$WORKSPACE/projects/kit-gallery"

source "$WORKSPACE/env.sh"
mkdir -p "$PROJECT"
rm -rf "$PROJECT/snapshots"
cp "$SKILL_DIR/kit/gallery/"* "$PROJECT/"
# gallery.sh [landscape|portrait|story]: the same scenes on that canvas.
if [ -n "${1:-}" ]; then
  node -e 'const f=process.argv[1],p=process.argv[2],fs=require("fs");const s=JSON.parse(fs.readFileSync(p,"utf8"));s.format=f;fs.writeFileSync(p,JSON.stringify(s,null,2))' "$1" "$PROJECT/scenes.json"
fi
node "$SKILL_DIR/scripts/build.mjs" kit-gallery
cd "$PROJECT"
hyperframes check
AT="$(node -e "console.log(JSON.parse(require('fs').readFileSync('.build/timeline.json','utf8')).scenes.map(s=>(s.start+s.duration*0.6).toFixed(2)).join(','))")"
hyperframes snapshot --at "$AT"
echo "gallery: snapshots in $PROJECT/snapshots"
