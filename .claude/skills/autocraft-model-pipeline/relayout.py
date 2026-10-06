# /// script
# dependencies = ["numpy", "pillow"]
# ///
"""Carry painted nets over to a new nets layout.

    uv run relayout.py <unit dir> <old dir> <new dir> [--fresh P]

<old dir> holds the nets.json the paint was made on and its sheets,
nets.N.recoloured.png (a grok/vK folder with a copy of that nets.json).
After the model changes in one place (the comet's head rebuilt from plates),
`model.sh nets --group P --fresh P` lays the rest out first and the changed
parts on sheets of their own; this puts the approved paint of every net whose
paths are unchanged into its new rect, scaled, so only the new sheets go to
Grok. Writes <new dir>/nets.N.recoloured.png for every sheet with a carried
net (the rest of such a sheet is the new prefill, nets.N.painted.png) and
prints the nets it could not carry: they need painting. --fresh P (the path
given to `model.sh nets --fresh`) carries nothing under P: rebuilt parts can
reuse an old part's path (the Comet's -X helmet plate took the neck guard's
0/0/10/0/2).
"""
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image


def main():
    unit, old_dir, new_dir = (Path(a) for a in sys.argv[1:4])
    fresh = sys.argv[sys.argv.index("--fresh") + 1] if "--fresh" in sys.argv else None
    under = lambda paths: fresh is not None and any(p == fresh or p.startswith(fresh + "/") for p in paths)
    old = json.loads((old_dir / "nets.json").read_text())
    new = json.loads((unit / "nets" / "nets.json").read_text())
    by_paths = {tuple(n["paths"]): n for n in old["nets"]}
    sheets, carried, missing = {}, 0, []
    for n in new["nets"]:
        o = None if under(n["paths"]) else by_paths.get(tuple(n["paths"]))
        if o is None:
            missing.append(n)
            continue
        s = n["sheet"]
        if s not in sheets:
            sheets[s] = Image.open(unit / "nets" / f"nets.{s + 1}.painted.png").convert("RGB")
            if sheets[s].size != tuple(new["size"]):
                sheets[s] = sheets[s].resize(tuple(new["size"]))
        src = Image.open(old_dir / f"nets.{o['sheet'] + 1}.recoloured.png").convert("RGB")
        ox, oy, ow, oh = o["rect"]
        x, y, w, h = n["rect"]
        # The same net at another zoom: same aspect, so one scale.
        piece = src.crop((round(ox), round(oy), round(ox + ow), round(oy + oh))).resize((round(w), round(h)), Image.LANCZOS)
        blank = np.asarray(Image.open(unit / "nets" / f"nets.{s + 1}.png").convert("L"))[round(y):round(y) + piece.height, round(x):round(x) + piece.width]
        # Only the net's own faces: the blank sheet is white outside them.
        mask = Image.fromarray(((blank < 245) * 255).astype(np.uint8))
        sheets[s].paste(piece, (round(x), round(y)), mask.resize(piece.size))
        carried += 1
    new_dir.mkdir(parents=True, exist_ok=True)
    for s, im in sorted(sheets.items()):
        dest = new_dir / f"nets.{s + 1}.recoloured.png"
        im.save(dest)
        print(f"wrote {dest}")
    print(f"carried {carried} nets")
    for n in missing:
        print(f"to paint: sheet {n['sheet'] + 1} {', '.join(n['paths'])}")


if __name__ == "__main__":
    main()
