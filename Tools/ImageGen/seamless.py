# /// script
# requires-python = ">=3.11"
# dependencies = ["pillow", "numpy"]
# ///
"""Make generated textures tile: blend each image with a copy rolled by half
its size, using the rolled copy near the edges (where it wraps cleanly) and the
original in the middle. Writes JPEGs into the app's texture resources.

  uv run Tools/ImageGen/seamless.py art/raw unreal/Resources/Textures
"""
import sys
from pathlib import Path
import numpy as np
from PIL import Image

src, dst = Path(sys.argv[1]), Path(sys.argv[2])
dst.mkdir(parents=True, exist_ok=True)
for f in sorted(src.glob("*.png")):
    a = np.asarray(Image.open(f).convert("RGB").resize((1024, 1024), Image.LANCZOS)).astype(np.float32)
    h, w, _ = a.shape
    # One axis at a time: the copy rolled along x has its only seam in the
    # middle column, where the original is used, and wraps cleanly at the
    # edges, where it is used. Then the same along y.
    def ramp(n):
        t = np.linspace(0, 1, n)
        m = np.clip((np.minimum(t, 1 - t) - 0.06) / 0.2, 0, 1)
        return m * m * (3 - 2 * m)
    mx = ramp(w)[None, :, None]
    b = a * mx + np.roll(a, w // 2, axis=1) * (1 - mx)
    my = ramp(h)[:, None, None]
    out = b * my + np.roll(b, h // 2, axis=0) * (1 - my)
    Image.fromarray(out.clip(0, 255).astype(np.uint8)).save(dst / (f.stem + ".jpg"), quality=92)
    print("wrote", dst / (f.stem + ".jpg"))
