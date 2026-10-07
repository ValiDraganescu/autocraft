#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 || ! "$1" =~ ^[a-z0-9-]+$ ]]; then
  echo "usage: finish.sh <video>   (kebab-case)" >&2
  exit 2
fi

NAME="$1"
SKILL_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REPO_ROOT="$(cd "$SKILL_DIR/../../.." && pwd)"
WORKSPACE="$REPO_ROOT/video"
PROJECT="$WORKSPACE/projects/$NAME"
VIDEO="$PROJECT/renders/$NAME.mp4"
SHEET="$PROJECT/renders/$NAME-contact.png"
MAIN_CHECKOUT="$(dirname "$(git -C "$REPO_ROOT" rev-parse --path-format=absolute --git-common-dir)")"
OUTPUT="${VIDEO_OUTPUT_DIR:-$MAIN_CHECKOUT/video/renders}"

[[ -d "$PROJECT" ]] || { echo "finish: no project at $PROJECT" >&2; exit 1; }
source "$WORKSPACE/env.sh"
bash "$SKILL_DIR/scripts/verify.sh" "$PROJECT" "$VIDEO"

mkdir -p "$OUTPUT"
cp "$VIDEO" "$OUTPUT/$NAME.mp4"
cp "$SHEET" "$OUTPUT/$NAME-contact.png"
cmp -s "$VIDEO" "$OUTPUT/$NAME.mp4" || { echo "finish: the copy of the video differs; the project stays" >&2; exit 1; }

if git -C "$OUTPUT" rev-parse --is-inside-work-tree >/dev/null 2>&1 && ! git -C "$OUTPUT" check-ignore -q "$OUTPUT/$NAME.mp4"; then
  echo "finish: warning: git does not ignore $OUTPUT; that checkout does not have the video lines of .gitignore yet" >&2
fi

rm -rf "$PROJECT"
echo "finish: video   $OUTPUT/$NAME.mp4 ($(du -h "$OUTPUT/$NAME.mp4" | cut -f1 | tr -d ' '))"
echo "finish: contact $OUTPUT/$NAME-contact.png"
echo "finish: project $PROJECT deleted"
