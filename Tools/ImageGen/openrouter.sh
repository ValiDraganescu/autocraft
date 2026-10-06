#!/bin/zsh
# Build-time image generation through OpenRouter. The app never calls it.
#
#   openrouter.sh gen <out.png> "<prompt>" [model]
#   openrouter.sh manifest <manifest.json> <out_dir>    (skips ids whose .png exists)
#
# The key is OPENROUTER_API_KEY from the environment or the nearest .env in
# this directory or a parent. It is never printed.
set -e
DEFAULT_MODEL="google/gemini-3.1-flash-image-preview"

load_key() {
  [[ -n "$OPENROUTER_API_KEY" ]] && return
  local d="$PWD"
  while [[ "$d" != "/" ]]; do
    if [[ -f "$d/.env" ]] && grep -q '^OPENROUTER_API_KEY=' "$d/.env"; then
      export OPENROUTER_API_KEY="$(grep '^OPENROUTER_API_KEY=' "$d/.env" | head -1 | cut -d= -f2- | tr -d '"'"'")"
      return
    fi
    d="$(dirname "$d")"
  done
  echo "OPENROUTER_API_KEY not found in env or any parent .env" >&2; exit 1
}

gen() {
  local out="$1" prompt="$2" model="${3:-$DEFAULT_MODEL}"
  local body resp code
  body=$(jq -n --arg m "$model" --arg p "$prompt" \
    '{model:$m, modalities:["image","text"], messages:[{role:"user",content:$p}],
      image_config:{aspect_ratio:"1:1", image_size:"2K"}}')
  resp=$(mktemp)
  code=$(curl -sS -o "$resp" -w '%{http_code}' https://openrouter.ai/api/v1/chat/completions \
    -H "Authorization: Bearer $OPENROUTER_API_KEY" -H 'Content-Type: application/json' -d "$body")
  if [[ "$code" != "200" ]]; then
    echo "HTTP $code for $out: $(head -c 400 "$resp" | sed "s/$OPENROUTER_API_KEY/***/g")" >&2; rm -f "$resp"; return 1
  fi
  jq -r '.choices[0].message.images[0].image_url.url' "$resp" | sed 's/^data:image\/[a-z]*;base64,//' | base64 -d > "$out"
  rm -f "$resp"
  local size=$(stat -f%z "$out")
  (( size < 1024 )) && echo "warning: $out is only $size bytes" >&2
  echo "wrote $out ($size bytes)"
}

load_key
case "$1" in
  gen) gen "$2" "$3" "$4" ;;
  manifest)
    mkdir -p "$3"
    jq -c '.[]' "$2" | while read -r e; do
      id=$(jq -r .id <<<"$e"); p=$(jq -r .prompt <<<"$e"); m=$(jq -r '.model_id // empty' <<<"$e")
      [[ -f "$3/$id.png" ]] && { echo "skip $id"; continue; }
      gen "$3/$id.png" "$p" "$m" &
    done
    wait ;;
  *) echo "usage: $0 gen|manifest ..." >&2; exit 2 ;;
esac
