#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = []
# ///
"""Vertical cuts of a 16:9 clip or explainer for the phone feed.

    uv run .claude/skills/autocraft-video/scripts/vertical.py video/clips/fight-12-dive.mp4
    uv run .claude/skills/autocraft-video/scripts/vertical.py IN.mp4 --aspect 9:16 --mode crop --focus 0.4
    uv run .claude/skills/autocraft-video/scripts/vertical.py IN.mp4 --kicker "Micro simulation" --title "Two armies|of twelve meet"

Modes:
  fit   (default) the whole 16:9 picture across the width, over a blurred,
        darkened fill of itself; --zoom 1.3 makes it larger and crops the
        sides; --kicker (cyan) and --title (ice; "|" breaks the line) go
        above it in the HUD font. Keeps the HUD and the whole frame.
  crop  a full-height slice of the picture; --focus 0..1 picks where
        (0 left, 0.5 centre, 1 right). Fills the screen; loses the sides.

--aspect: 4:5 (1080x1350, the X feed; default), 9:16 (1080x1920), 1:1.
Out: NAME-<aspect>-<mode>.mp4 next to the input (or in --out). The sound is
kept. FFmpeg: the video workspace's, else one from nixpkgs.
"""
from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

SKILL = Path(__file__).resolve().parent.parent
REPO = SKILL.parent.parent.parent
FONT = REPO / "unreal/Content-src/fonts/BarlowCondensed-Bold.ttf"
WORKSPACE_FFMPEG = REPO / "video/.tools/ffmpeg-bin/bin/ffmpeg"
SIZES = {"4:5": (1080, 1350), "9:16": (1080, 1920), "1:1": (1080, 1080)}
# The kit's tokens (kit/tokens.css): --text and --accent.
TEXT, ACCENT = "0xEBF7FF", "0x59D9FF"


def ffmpeg() -> str:
    if WORKSPACE_FFMPEG.exists():
        return str(WORKSPACE_FFMPEG)
    if found := shutil.which("ffmpeg"):
        return found
    out = subprocess.run(
        ["nix", "--extra-experimental-features", "nix-command flakes", "build", "nixpkgs#ffmpeg.bin", "--no-link", "--print-out-paths"],
        check=True, capture_output=True, text=True,
    ).stdout.strip().splitlines()[-1]
    return f"{out}/bin/ffmpeg"


def text_filter(tmp: Path, name: str, text: str, size: int, color: str, y: int) -> str:
    # textfile= keeps quotes, colons and commas out of the filter's escaping.
    f = tmp / f"{name}.txt"
    f.write_text(text)
    return f"drawtext=fontfile='{FONT}':textfile='{f}':fontsize={size}:fontcolor={color}:x=(w-text_w)/2:y={y}"


def graph(a: argparse.Namespace, w: int, h: int, tmp: Path) -> str:
    if a.mode == "crop":
        return f"[0:v]scale=-2:{h},crop={w}:{h}:(iw-{w})*{a.focus}:0,setsar=1[v]"
    fg_w = round(w * a.zoom / 2) * 2
    fg_h = round(fg_w * 9 / 16 / 2) * 2
    lines = [line for line in (a.title or "").split("|") if line]
    title_size, kicker_size, gap = 76, 40, 14
    block = (kicker_size + gap if a.kicker else 0) + len(lines) * (title_size + gap)
    # The picture a little below the middle when there is text above it.
    top = (h - fg_h) // 2 + (block // 2 if block else 0)
    parts = [
        f"[0:v]split[a][b]",
        # The fill from the top 70% (the scene, not the dark HUD console).
        f"[a]crop=iw:ih*0.7:0:0,scale={w}:{h}:force_original_aspect_ratio=increase,crop={w}:{h},gblur=sigma=40,eq=brightness=-0.22:saturation=0.85[bg]",
        f"[b]scale={fg_w}:{fg_h},crop={min(fg_w, w)}:{fg_h}[fg]",
    ]
    chain = f"[bg][fg]overlay=(W-w)/2:{top}"
    y = top - block - 40
    if a.kicker:
        chain += "," + text_filter(tmp, "kicker", a.kicker.upper(), kicker_size, ACCENT, y)
        y += kicker_size + gap
    for i, line in enumerate(lines):
        chain += "," + text_filter(tmp, f"title{i}", line, title_size, TEXT, y)
        y += title_size + gap
    parts.append(chain + ",setsar=1[v]")
    return ";".join(parts)


def cut(src: Path, a: argparse.Namespace) -> Path:
    w, h = SIZES[a.aspect]
    out_dir = a.out or src.parent
    out_dir.mkdir(parents=True, exist_ok=True)
    out = out_dir / f"{src.stem}-{a.aspect.replace(':', 'x')}-{a.mode}.mp4"
    with tempfile.TemporaryDirectory() as t:
        cmd = [ffmpeg(), "-y", "-loglevel", "error", "-i", str(src), "-filter_complex", graph(a, w, h, Path(t)),
               "-map", "[v]", "-map", "0:a?", "-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "17",
               "-c:a", "aac", "-b:a", "192k", "-movflags", "+faststart", str(out)]
        subprocess.run(cmd, check=True)
    print(f"vertical: {out} ({w}x{h}, {a.mode}, {out.stat().st_size / 1e6:.1f} MB)")
    return out


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("inputs", nargs="+", type=Path)
    ap.add_argument("--aspect", choices=SIZES, default="4:5")
    ap.add_argument("--mode", choices=["fit", "crop"], default="fit")
    ap.add_argument("--focus", type=float, default=0.5, help="crop: 0 left .. 1 right")
    ap.add_argument("--zoom", type=float, default=1.0, help="fit: the picture's width over the frame's (1.3 crops the sides)")
    ap.add_argument("--kicker", help="fit: a short cyan line above the title")
    ap.add_argument("--title", help="fit: the title above the picture; | breaks the line")
    ap.add_argument("--out", type=Path)
    a = ap.parse_args()
    if not 0 <= a.focus <= 1 or a.zoom < 1:
        sys.exit("vertical: --focus is 0..1 and --zoom at least 1")
    for src in a.inputs:
        if not src.exists():
            sys.exit(f"vertical: no {src}")
        cut(src, a)


if __name__ == "__main__":
    main()
