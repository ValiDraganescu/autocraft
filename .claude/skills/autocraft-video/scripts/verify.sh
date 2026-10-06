#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
  echo "usage: verify.sh <project dir> <video.mp4>" >&2
  exit 2
fi

PROJECT="$(cd "$1" && pwd)"
VIDEO="$2"
FFMPEG="${HYPERFRAMES_FFMPEG_PATH:?source video/env.sh first}"
FFPROBE="${HYPERFRAMES_FFPROBE_PATH:?source video/env.sh first}"
TIMELINE="$PROJECT/.build/timeline.json"
FAILED=0

[[ -s "$VIDEO" ]] || { echo "verify: FAIL no video at $VIDEO"; exit 1; }
[[ -s "$TIMELINE" ]] || { echo "verify: FAIL no $TIMELINE; run build.mjs first"; exit 1; }

check() {
  if [[ "$1" == "ok" ]]; then echo "verify: ok   $2"; else echo "verify: FAIL $2"; FAILED=1; fi
}

VCODEC="$("$FFPROBE" -v error -select_streams v:0 -show_entries stream=codec_name -of csv=p=0 "$VIDEO" | head -1)"
ACODEC="$("$FFPROBE" -v error -select_streams a:0 -show_entries stream=codec_name -of csv=p=0 "$VIDEO" | head -1)"
SIZE="$("$FFPROBE" -v error -select_streams v:0 -show_entries stream=width,height -of csv=p=0:s=x "$VIDEO" | head -1)"
DURATION="$("$FFPROBE" -v error -show_entries format=duration -of csv=p=0 "$VIDEO")"
EXPECTED="$(node -e "console.log(JSON.parse(require('fs').readFileSync(process.argv[1],'utf8')).total)" "$TIMELINE")"
MAX_VOLUME="$("$FFMPEG" -hide_banner -nostats -i "$VIDEO" -map 0:a:0 -af volumedetect -f null - 2>&1 | sed -n 's/.*max_volume: \(-*[0-9.]*\) dB.*/\1/p')"

[[ -n "$VCODEC" ]] && check ok "video track ($VCODEC, $SIZE)" || check fail "no video track"
[[ -n "$ACODEC" ]] && check ok "audio track ($ACODEC)" || check fail "no audio track"
node -e "process.exit(Math.abs(Number(process.argv[1]) - Number(process.argv[2])) <= 0.5 ? 0 : 1)" -- "$DURATION" "$EXPECTED" \
  && check ok "duration ${DURATION}s (timeline ${EXPECTED}s)" \
  || check fail "duration ${DURATION}s does not match the timeline ${EXPECTED}s"
node -e "process.exit(Number(process.argv[1]) > -30 ? 0 : 1)" -- "${MAX_VOLUME:--100}" \
  && check ok "narration is audible (max volume ${MAX_VOLUME} dB)" \
  || check fail "audio is silent or near silent (max volume ${MAX_VOLUME:-none} dB)"

SHEET_DIR="$PROJECT/.build/contact"
mkdir -p "$SHEET_DIR"
find "$SHEET_DIR" -name 'frame-*.png' -type f -delete
INDEX=0
while IFS=$'\t' read -r _ AT; do
  INDEX=$((INDEX + 1))
  "$FFMPEG" -v error -y -ss "$AT" -i "$VIDEO" -frames:v 1 -vf "scale=640:-2" "$SHEET_DIR/frame-$(printf '%03d' "$INDEX").png"
done < <(node -e "for (const s of JSON.parse(require('fs').readFileSync(process.argv[1],'utf8')).scenes) console.log(s.id + '\t' + (s.start + s.duration * 0.6).toFixed(2))" "$TIMELINE")

COLUMNS=3
ROWS=$(( (INDEX + COLUMNS - 1) / COLUMNS ))
SHEET="$(dirname "$VIDEO")/$(basename "$VIDEO" .mp4)-contact.png"
"$FFMPEG" -v error -y -framerate 1 -pattern_type glob -i "$SHEET_DIR/frame-*.png" -vf "tile=${COLUMNS}x${ROWS}:padding=8:color=black" -frames:v 1 "$SHEET"
[[ -s "$SHEET" ]] && check ok "contact sheet $SHEET ($INDEX scenes)" || check fail "no contact sheet"

exit "$FAILED"
