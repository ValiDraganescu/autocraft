#!/usr/bin/env bash
set -euo pipefail

SKILL_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REPO_ROOT="$(cd "$SKILL_DIR/../../.." && pwd)"
WORKSPACE="$REPO_ROOT/video"

if ! git -C "$REPO_ROOT" check-ignore -q "$WORKSPACE"; then
  echo "setup: git does not ignore $WORKSPACE; check the video line of .gitignore" >&2
  exit 1
fi

mkdir -p "$WORKSPACE"
cp "$SKILL_DIR/workspace/package.json" "$SKILL_DIR/workspace/pnpm-lock.yaml" "$SKILL_DIR/workspace/env.sh" "$WORKSPACE/"
printf 'allowBuilds:\n  esbuild: true\n' > "$WORKSPACE/pnpm-workspace.yaml"
(cd "$WORKSPACE" && pnpm install --frozen-lockfile --silent)

mkdir -p "$WORKSPACE/.tools"
if [[ ! -x "$WORKSPACE/.tools/ffmpeg-bin/bin/ffmpeg" ]]; then
  nix --extra-experimental-features 'nix-command flakes' build nixpkgs#ffmpeg.bin --out-link "$WORKSPACE/.tools/ffmpeg"
fi

VERSION="$(node -e "console.log(require(process.argv[1]).dependencies.hyperframes)" "$WORKSPACE/package.json")"
SKILLS="$WORKSPACE/skills"
if [[ "$(cat "$SKILLS/.version" 2>/dev/null)" != "$VERSION" ]]; then
  DOWNLOAD="$(mktemp -d)"
  trap 'rm -rf "$DOWNLOAD"' EXIT
  curl -fsSL "https://codeload.github.com/heygen-com/hyperframes/tar.gz/refs/tags/v$VERSION" | tar -xz -C "$DOWNLOAD"
  [[ -d "$DOWNLOAD/hyperframes-$VERSION/skills/hyperframes-core" ]] || { echo "setup: the HyperFrames v$VERSION archive has no skills folder" >&2; exit 1; }
  rm -rf "$SKILLS"
  mv "$DOWNLOAD/hyperframes-$VERSION/skills" "$SKILLS"
  echo "$VERSION" > "$SKILLS/.version"
fi

source "$WORKSPACE/env.sh"
hyperframes doctor 2>&1 | grep -E "FFmpeg|FFprobe|Chrome" || true
echo "setup: workspace ready at $WORKSPACE"
