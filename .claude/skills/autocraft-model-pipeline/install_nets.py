# /// script
# dependencies = ["numpy", "pillow", "scipy"]
# ///
"""Put a unit's painted nets in the game.

    uv run install_nets.py <unit dir> <pattern> <textures dir>

<pattern> names each sheet's painted image with {N} for its number, relative
to <unit dir>/nets (grok/v9/nets.{N}.recoloured.png): recolour.py's output,
on the sheet's pixels. Writes <unit>_nets_N.jpg (the paint), <unit>_nets_N_glow.jpg
(its lit strips, for emission) and <unit>_nets.json (the nets plan) into
<textures dir>, and removes sheets a previous install left beyond the count.

The game samples the sheets with mipmaps, so the white round the nets would
bleed into their edges at a distance: every pixel off the nets (and the drawn
outline on their edge) takes the colour of the nearest pixel inside a net.
"""
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image
from scipy.ndimage import binary_erosion, distance_transform_edt

from netscheck import grey, inside

OUTLINE = 2  # px: the drawn outline round each net, replaced like the background


def glow(a: np.ndarray) -> np.ndarray:
    """The lit strips: bright and clearly blue (as `Autocraft netspreview`)."""
    r, g, b = a[..., 0].astype(int), a[..., 1].astype(int), a[..., 2].astype(int)
    lit = (b - r > 60) & (np.maximum(g, b) > 150)
    return np.where(lit[..., None], a, 0).astype(np.uint8)


def main():
    unit, pattern, tex = Path(sys.argv[1]), sys.argv[2], Path(sys.argv[3])
    name = unit.name
    nets = json.loads((unit / "nets" / "nets.json").read_text())
    size = nets["size"]
    for n in range(1, nets["sheets"] + 1):
        src = unit / "nets" / pattern.replace("{N}", str(n))
        img = Image.open(src).convert("RGB")
        if list(img.size) != [int(v) for v in size]:
            sys.exit(f"{src} is {img.size[0]}×{img.size[1]}, the sheet {size[0]:.0f}×{size[1]:.0f}: recolour.py fits Grok's image first")
        a = np.asarray(img).copy()
        blank = np.asarray(Image.open(unit / "nets" / f"nets.{n}.png").convert("RGB")).astype(np.float32)
        keep = binary_erosion((grey(blank) < 245) & inside(size), iterations=OUTLINE)
        _, (iy, ix) = distance_transform_edt(~keep, return_indices=True)
        a = a[iy, ix]
        Image.fromarray(a).save(tex / f"{name}_nets_{n}.jpg", quality=92)
        Image.fromarray(glow(a)).save(tex / f"{name}_nets_{n}_glow.jpg", quality=92)
        print(f"installed {name}_nets_{n}.jpg and {name}_nets_{n}_glow.jpg from {src.relative_to(unit)}")
    (tex / f"{name}_nets.json").write_text(json.dumps(nets, separators=(",", ":")))
    print(f"installed {name}_nets.json ({len(nets['nets'])} nets on {nets['sheets']} sheets)")
    k = nets["sheets"] + 1
    while (tex / f"{name}_nets_{k}.jpg").exists():
        for f in (tex / f"{name}_nets_{k}.jpg", tex / f"{name}_nets_{k}_glow.jpg"):
            if f.exists():
                f.unlink()
                print(f"removed {f.name} (left from a bigger install)")
        k += 1


if __name__ == "__main__":
    main()
