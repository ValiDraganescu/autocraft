#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
  echo "usage: tts.sh <output.wav> <text>" >&2
  exit 2
fi

OUT="$1"
TEXT="$2"
PYTHON="${VIDEO_TTS_PYTHON:-$HOME/git/dev/mlx-audio/.venv/bin/python}"
MODEL="${VIDEO_TTS_MODEL:-mlx-community/Kokoro-82M-bf16}"
VOICE="${VIDEO_TTS_VOICE:-af_heart}"
SPEED="${VIDEO_TTS_SPEED:-1.0}"
LANG_CODE="${VIDEO_TTS_LANG:-a}"

if [[ ! -x "$PYTHON" ]]; then
  echo "tts: the mlx-audio python is missing at $PYTHON (set VIDEO_TTS_PYTHON)" >&2
  exit 3
fi

DIR="$(cd "$(dirname "$OUT")" && pwd)"
PREFIX="$(basename "$OUT" .wav)"
LOG="$DIR/$PREFIX.tts.log"

if ! "$PYTHON" -m mlx_audio.tts.generate \
  --model "$MODEL" \
  --voice "$VOICE" \
  --speed "$SPEED" \
  --lang_code "$LANG_CODE" \
  --text "$TEXT" \
  --output_path "$DIR" \
  --file_prefix "$PREFIX" \
  --audio_format wav \
  --join_audio >"$LOG" 2>&1; then
  echo "tts: synthesis failed, see $LOG" >&2
  exit 4
fi

if [[ ! -s "$DIR/$PREFIX.wav" ]]; then
  echo "tts: no audio written at $DIR/$PREFIX.wav, see $LOG" >&2
  exit 4
fi
rm -f "$LOG"
