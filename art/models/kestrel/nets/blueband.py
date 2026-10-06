# /// script
# dependencies = ["numpy", "pillow"]
# ///
"""Paint the Kestrel's deep blue flank bands (and cyan strips) back onto its recoloured nets.

    uv run blueband.py <in dir> <out dir>

Reads <in dir>/nets.{1,2,3}.recoloured.png and ../nets.json, writes the same names into <out dir>.

Why: recolour.py keeps Grok's colours on a net only when the net's median is the named paint hue.
The hull's and nose's medians are silver, so the few blue wedges Grok painted there went grey and the
c1-gunship concept's blue flanks were lost. This puts them back by rule, in model space: every pixel of
a listed net's tile is mapped to the point of the part it shows (the tile's centre, right, up and
pixels per unit, as `ViewSkin.netGeometry` projects), and where the rule says "blue" the pixel takes the
deep blue Grok painted on the tailplane, scaled by the pixel's own brightness against the band's mean so
panel lines, rivets and chips stay. Cyan strips are bright and clearly blue, so install_nets.py puts
them on the glow sheet and they light up.
"""
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image

BLUE = np.array([42.0, 63.0, 101.0])  # median of Grok's deep blue on sheet 3 (tailplane, stabilisers)
CYAN = np.array([90.0, 205.0, 255.0])


def hull_top(x):
    """Top edge of the hull slab (0/0/0) and the nose (0/0/1) along x, as Models+Kestrel builds them."""
    return np.where(x < 0.4, 0.1 + (x + 0.62) / 1.02 * 0.05, 0.15 - (x - 0.4) / 0.68 * 0.15)


def top_half_width(x):
    return np.where(x < 0.4, 0.14 + (x + 0.62) / 1.02 * 0.07, 0.21 - (x - 0.4) / 0.68 * 0.16)


def rule(path, axis, p):
    """0 silver (unchanged), 1 blue, 2 cyan strip, for points p (N×3) on face `axis` of part `path`."""
    x, y, z = p[:, 0], p[:, 1], p[:, 2]
    out = np.zeros(len(p), dtype=int)
    if axis in ("+Z", "-Z"):
        # Flanks: blue below a line 0.075 under the top edge, a cyan strip in it.
        band = y < hull_top(x) - 0.075
        out[band] = 1
        strip = np.abs(y - (hull_top(x) - 0.1)) < 0.007
        if path == "0/0/0":
            strip &= ((x > -0.4) & (x < -0.18)) | ((x > -0.05) & (x < 0.25))
        else:
            strip &= (x > 0.5) & (x < 0.78)
        out[strip] = 2
    elif axis == "+Y":
        # Top: blue along both edges, silver down the middle (the nose's silver ridge).
        frac = 0.62 if path == "0/0/1" else 0.8
        out[np.abs(z) > frac * top_half_width(x)] = 1
    return out


def main():
    src, dst = Path(sys.argv[1]), Path(sys.argv[2])
    dst.mkdir(parents=True, exist_ok=True)
    plan = json.loads((src / "nets.json").read_text()) if (src / "nets.json").exists() else \
        json.loads((Path(__file__).parent / "nets.json").read_text())
    sheets = {n: np.asarray(Image.open(src / f"nets.{n}.recoloured.png").convert("RGB")).astype(float) for n in (1, 2, 3)}
    for net in plan["nets"]:
        path = net["paths"][0]
        if path not in ("0/0/0", "0/0/1"):
            continue
        a = sheets[net["sheet"] + 1]
        for t in net["tiles"]:
            right, up, c = np.array(t["right"], float), np.array(t["up"], float), np.array(t["center"], float)
            normal = np.cross(right, up)
            axis = ("+" if normal.max() > 0.5 else "-") + "XYZ"[int(np.argmax(np.abs(normal)))]
            x0, y0, w, h = t["rect"]
            ppu = t["pixelsPerUnit"]
            cols, rows = np.meshgrid(np.arange(w), np.arange(h))
            u = (cols + 0.5 - w / 2) / ppu
            v = (h / 2 - rows - 0.5) / ppu
            p = c + u.reshape(-1, 1) * right + v.reshape(-1, 1) * up
            r = rule(path, axis, p).reshape(h, w)
            tile = a[y0:y0 + h, x0:x0 + w]
            # Only the net's own pixels (not the white round it).
            on = tile.mean(axis=2) < 235
            blue = (r == 1) & on
            if blue.any():
                lum = tile.mean(axis=2)
                k = np.clip(lum / max(lum[blue].mean(), 1), 0.35, 1.6)
                tile[blue] = BLUE * k[blue][:, None]
            cyan = (r == 2) & on
            tile[cyan] = CYAN
            print(f"{path} {axis}: {blue.sum()} px blue, {cyan.sum()} px cyan")
    for n, a in sheets.items():
        Image.fromarray(np.clip(a, 0, 255).astype(np.uint8)).save(dst / f"nets.{n}.recoloured.png")


if __name__ == "__main__":
    main()
