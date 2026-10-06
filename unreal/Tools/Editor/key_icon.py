# /// script
# requires-python = ">=3.11"
# dependencies = ["pillow", "numpy", "scipy"]
# ///
"""Make a 192x192 RGBA icon from a model-row shot on black.

    uv run unreal/Tools/Editor/key_icon.py <shot.png> <out.png> [fill=0.97] [target_luma=0]

The renderer has no alpha: the model row's background is flat black. The
background is the near-black region connected to the image border (dark parts
of the model inside it stay opaque); the edge gets a soft alpha from how bright
the pixel is, un-premultiplied against the black. The model is cropped to its
bounding box and scaled so its longer side is `fill` of the 192 px frame, centred
across and sitting a little low (as the Swift icons do). `target_luma` (the Swift
icons' mean is 120-160; the model row's lighting is darker): a gamma on the colour
(not the alpha) that brings the opaque pixels' mean to it; 0 leaves it alone.
Hidden shot: unreal/SHOTS.md, "-AcModelFocus=<model>_blue -AcModelDistance=0.4".
"""
import sys

import numpy as np
from PIL import Image
from scipy import ndimage

SIZE = 192


def main():
    src, dst = sys.argv[1], sys.argv[2]
    fill = float(sys.argv[3]) if len(sys.argv) > 3 else 0.97
    target = float(sys.argv[4]) if len(sys.argv) > 4 else 0.0
    im = np.asarray(Image.open(src).convert("RGB")).astype(np.float64)
    mx = im.max(axis=2)
    dark = mx <= 6
    lab, n = ndimage.label(dark)
    border = set(np.unique(np.concatenate([lab[0], lab[-1], lab[:, 0], lab[:, -1]]))) - {0}
    bg = np.isin(lab, list(border))
    # Opaque inside the silhouette; a soft alpha only on the pixels next to the background.
    near = ndimage.binary_dilation(bg, iterations=2) & ~bg
    alpha = np.where(bg, 0.0, 1.0)
    alpha = np.where(near, np.clip(mx / 60.0, 0.0, 1.0), alpha)
    a = np.clip(alpha, 1e-3, 1)[..., None]
    rgb = np.where(near[..., None], np.clip(im / a, 0, 255), im)
    if target > 0:
        inside = alpha > 0.9
        lo, hi = 0.1, 3.0  # the exponent: below 1 brightens
        for _ in range(30):
            g = (lo + hi) / 2
            mean = (255 * np.clip(rgb[inside] / 255, 0, 1) ** g).mean()
            lo, hi = (g, hi) if mean > target else (lo, g)
        rgb = 255 * np.clip(rgb / 255, 0, 1) ** g
        print("gamma exponent %.2f" % g)
    ys, xs = np.where(alpha > 0.08)
    x0, x1, y0, y1 = xs.min(), xs.max() + 1, ys.min(), ys.max() + 1
    crop = np.dstack([rgb, alpha * 255])[y0:y1, x0:x1]
    h, w = crop.shape[:2]
    s = SIZE * fill / max(h, w)
    big = Image.fromarray(crop.astype(np.uint8), "RGBA").resize((max(1, round(w * s)), max(1, round(h * s))), Image.LANCZOS)
    out = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    ox = (SIZE - big.width) // 2
    oy = min(SIZE - big.height, (SIZE - big.height) // 2 + 6)
    out.paste(big, (ox, oy))
    out.save(dst)
    print(dst, "bbox in shot", x0, y0, x1, y1, "scaled", big.size)


main()
