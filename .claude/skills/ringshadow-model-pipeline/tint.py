# /// script
# dependencies = ["numpy", "pillow", "scipy"]
# ///
"""Tint the clay faces of the nets with each part's material colour.

    uv run tint.py <unit dir>

For every nets.N.png / nets.N.painted.png pair in <unit dir>/nets, writes
nets.N.tinted.png: the painted faces as they are, and every clay face shaded
as before but in the colour of the material its parts.txt look names first.
That colour is the median over the nets of that material of each net's
painted pixels (5 px in from their edges; lights and the gold visor left out
of all but glow). One colour per material, not per net: a net's own median
follows whatever fills its painted face (the helmet front's is the visor gold).
Grok keeps the colours it is given far better than the colours a legend names
(nets v2: parts named dark gunmetal came out as bright as the silver ones).
"""
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image
from scipy.ndimage import binary_erosion

from legend import names
from netscheck import CLAY_GREY, material, painted_mask

CLAY = CLAY_GREY
MIN_PAINT = 150  # painted pixels a net needs to give its own colour


def main():
    unit = Path(sys.argv[1])
    nets = json.loads((unit / "nets" / "nets.json").read_text())
    known = names(unit / "parts.txt")
    sheets = {}
    for n in range(1, nets["sheets"] + 1):
        blank = np.asarray(Image.open(unit / "nets" / f"nets.{n}.png").convert("RGB")).astype(np.float32)
        painted = np.asarray(Image.open(unit / "nets" / f"nets.{n}.painted.png").convert("RGB")).astype(np.float32)
        shape = blank.mean(2) < 245
        paint = painted_mask(blank, painted, shape)
        sheets[n - 1] = (blank, painted, shape, paint, binary_erosion(paint, iterations=5))

    own, by_mat = {}, {}
    for i, net in enumerate(nets["nets"]):
        _, painted, _, _, core = sheets[net["sheet"]]
        x, y, w, h = net["rect"]
        px = painted[y:y + h, x:x + w][core[y:y + h, x:x + w]]
        mat = material(next((known[p] for p in net["paths"] if p in known), ""))
        if len(px) >= MIN_PAINT:
            # Glow is the lit strip, not the dark housing round it; any other
            # material leaves out the lights and the gold visor on its faces.
            if mat == "glow":
                px = px[px.mean(1) >= np.percentile(px.mean(1), 70)]
            elif mat != "blue paint":
                px = px[px.max(1) - px.min(1) < 40]
            if len(px) >= MIN_PAINT:
                own[i] = np.median(px, 0)
                by_mat.setdefault(mat, []).append(own[i])
    palette = {m: np.median(v, 0) for m, v in by_mat.items()}
    if "glow" not in palette:
        # The thin light strips rarely hold enough paint of their own: take
        # the lit cyan-blue anywhere on the painted faces.
        px = np.concatenate([painted[paint] for _, painted, _, paint, _ in sheets.values()])
        lit = px[(px[:, 2] > px[:, 0] + 60) & (px.mean(1) > 140)]
        if len(lit):
            palette["glow"] = np.median(lit, 0)
            by_mat["glow"] = [palette["glow"]]
    grey = np.array([CLAY] * 3)

    for s, (blank, painted, shape, paint, _) in sheets.items():
        out = painted.copy()
        for i, net in enumerate(nets["nets"]):
            if net["sheet"] != s:
                continue
            mat = material(next((known[p] for p in net["paths"] if p in known), ""))
            tint = palette.get(mat, grey)
            x, y, w, h = net["rect"]
            clay = (shape & ~paint)[y:y + h, x:x + w]
            g = blank[y:y + h, x:x + w].mean(2, keepdims=True)
            region = out[y:y + h, x:x + w]
            region[clay] = np.clip(tint * g[clay] / CLAY, 0, 255)
        dest = unit / "nets" / f"nets.{s + 1}.tinted.png"
        Image.fromarray(out.astype(np.uint8)).save(dest)
        print(f"wrote {dest}")
    for m, c in sorted(palette.items()):
        print(f"  {m:14} {c.round().astype(int).tolist()} ({len(by_mat[m])} nets)")
    print(f"  colours from {len(own)} of {len(nets['nets'])} nets")


if __name__ == "__main__":
    main()
