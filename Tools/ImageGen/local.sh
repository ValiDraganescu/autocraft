#!/bin/zsh
# Generate the texture manifest with local Qwen-Image-2.1 (qimg, $0) instead
# of OpenRouter. Skips ids whose .png already exists.
#   Tools/ImageGen/local.sh [manifest] [out_dir]
set -e
cd "$(dirname "$0")/../.."
manifest="${1:-Tools/ImageGen/textures.manifest.json}"
out="${2:-art/raw}"
mkdir -p "$out"
jq -c '.[]' "$manifest" | while read -r e; do
  id=$(jq -r .id <<<"$e"); p=$(jq -r .prompt <<<"$e")
  [[ -f "$out/$id.png" ]] && { echo "skip $id"; continue; }
  (cd "$out" && qimg "$p" -W 1024 -H 1024 --seed 11 -o "$id.png")
done
