# /// script
# requires-python = ">=3.11"
# dependencies = ["numpy", "pillow", "scipy"]
# ///
"""Compose a unit's stencil sheet from the hidden-run view shots (pipeline step 2).

    uv run stencil.py <shots dir> <out dir> [model name]

<shots dir> holds, for each of front right back left top 3q, the shots `<view>-clay.png` and
`<view>-parts.png` (512 x 512, from `-AcModelView=<view> -AcModelPaint=clay|parts`, see model.sh stencil) and
the camera numbers `<view>-clay.json` that AAcModelRow wrote next to them. Writes to <out dir>:

  stencil.png        1536 x 1024, six 512 px panels (FRONT, RIGHT SIDE, BACK, LEFT SIDE / TOP, 3/4): grey clay,
                     dark outlines on the silhouette and between parts, the panel name, and in the four side
                     views the ground line, the top line and the dashed quarter-height guides; 8 px black
                     borders between the panels, on white. split.py ignores the labels and guides.
  stencil.parts.png  the same sheet, one flat colour per part, on white, nothing else.
  stencil.json       size, lineWidth, bounds, model and per panel view, rect, right, up, toCamera, center,
                     pixelsPerUnit, groundRow, orthographic: the format of art/models/kestrel/stencil.json.

The model is where the shot is not black. Parts are found in the parts shot by colour (flat colour per part,
tone-mapped, so the colours are clustered, not looked up).
"""
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont
from scipy import ndimage

VIEWS = [("front", "FRONT"), ("right", "RIGHT SIDE"), ("back", "BACK"), ("left", "LEFT SIDE"), ("top", "TOP"),
         ("3q", "3/4")]
JSON_NAME = {"3q": "threeQuarter"}
PANEL, LINE = 512, 8
OUTLINE, GUIDE, GROUND, LABEL = (58, 58, 58), (170, 170, 170), (120, 120, 120), (128, 128, 128)


def model_mask(rgb):
    return rgb.max(axis=2) > 14


def label_parts(parts, mask):
    """(labels, centres): each model pixel's part, by clustering the flat colours."""
    q = (parts.astype(np.int32) // 12)
    key = q[..., 0] * 4096 + q[..., 1] * 64 + q[..., 2]
    vals, counts = np.unique(key[mask], return_counts=True)
    order = np.argsort(-counts)
    centres = []
    for i in order:
        if counts[i] < 60:
            break
        pix = parts[mask & (key == vals[i])].astype(np.float32)
        c = pix.mean(axis=0)
        if all(np.abs(c - o).sum() > 60 for o in centres):
            centres.append(c)
    if not centres:
        return np.zeros(mask.shape, np.int32), np.zeros((1, 3))
    cs = np.array(centres)
    d = ((parts[..., None, :].astype(np.float32) - cs[None, None]) ** 2).sum(axis=3)
    lab = d.argmin(axis=2).astype(np.int32)
    lab[~mask] = -1
    # one lone pixel of another part is antialiasing: take the majority of the 3x3 neighbourhood
    return lab, cs


def edges(lab):
    e = np.zeros(lab.shape, bool)
    for dy, dx in ((0, 1), (1, 0)):
        a = lab[: lab.shape[0] - dy, : lab.shape[1] - dx]
        b = lab[dy:, dx:]
        diff = (a != b) & (a >= 0) & (b >= 0)
        e[: lab.shape[0] - dy, : lab.shape[1] - dx] |= diff
    return e


def font(size):
    try:
        return ImageFont.load_default(size=size)
    except TypeError:
        return ImageFont.load_default()


def dashed(draw, y, x0, x1, color):
    x = x0
    while x < x1:
        draw.line([(x, y), (min(x + 8, x1), y)], fill=color, width=1)
        x += 14


def main():
    shots, out = Path(sys.argv[1]), Path(sys.argv[2])
    out.mkdir(parents=True, exist_ok=True)
    sheet = Image.new("RGB", (3 * PANEL, 2 * PANEL), "white")
    psheet = Image.new("RGB", (3 * PANEL, 2 * PANEL), "white")
    draw, f = ImageDraw.Draw(sheet), font(15)
    panels, model, bounds = [], sys.argv[3] if len(sys.argv) > 3 else None, None
    for i, (view, title) in enumerate(VIEWS):
        px, py = (i % 3) * PANEL, (i // 3) * PANEL
        clay = np.asarray(Image.open(shots / f"{view}-clay.png").convert("RGB"))
        parts = np.asarray(Image.open(shots / f"{view}-parts.png").convert("RGB"))
        cam = json.loads((shots / f"{view}-clay.json").read_text())
        if clay.shape[:2] != (PANEL, PANEL):
            sys.exit(f"{view}: the shot is {clay.shape[1]}x{clay.shape[0]}, want {PANEL}x{PANEL} (-ResX=512 -ResY=512)")
        mask = model_mask(parts) | model_mask(clay)
        lab, centres = label_parts(parts, mask)
        panel = np.full((PANEL, PANEL, 3), 255, np.uint8)
        # grey clay: the shot's own shading, as grey
        grey = clay.astype(np.float32).mean(axis=2)
        panel[mask] = np.repeat(np.clip(grey[mask] * 1.0, 40, 235)[:, None], 3, axis=1).astype(np.uint8)
        # outlines: the silhouette, and the borders between parts
        rim = mask & ~ndimage.binary_erosion(mask, iterations=2)
        seam = ndimage.binary_dilation(edges(lab), iterations=1) & mask
        panel[rim | seam] = OUTLINE
        img = Image.fromarray(panel)
        d = ImageDraw.Draw(img)
        ppu, gr = cam["pixelsPerUnit"], cam["groundRow"]
        if cam["orthographic"] and view != "top":
            top = gr - (cam["boundsMax"][1] - cam["boundsMin"][1]) * ppu
            for k in (1, 2, 3):
                dashed(d, round(gr + (top - gr) * k / 4), 0, PANEL, GUIDE)
            d.line([(0, round(top)), (PANEL, round(top))], fill=GUIDE, width=1)
            d.line([(0, round(gr)), (PANEL, round(gr))], fill=GROUND, width=2)
            # the guides and ground sit behind the model: redraw it over them
            keep = np.asarray(img).copy()
            keep[mask] = panel[mask]
            img = Image.fromarray(keep)
            d = ImageDraw.Draw(img)
        d.text((18, 18), title, fill=LABEL, font=f)
        sheet.paste(img, (px, py))
        pp = np.full((PANEL, PANEL, 3), 255, np.uint8)
        for k, c in enumerate(centres):
            pp[lab == k] = np.clip(c, 0, 255).astype(np.uint8)
        psheet.paste(Image.fromarray(pp), (px, py))
        entry = {"view": JSON_NAME.get(view, view), "rect": [px, py, PANEL, PANEL], "right": cam["right"], "up": cam["up"],
                 "toCamera": cam["toCamera"], "center": cam["center"], "pixelsPerUnit": ppu, "orthographic": cam["orthographic"]}
        if cam["orthographic"] and view != "top":
            entry["groundRow"] = gr
        panels.append(entry)
        if view == "front":
            model, bounds = model or cam["model"], {"min": cam["boundsMin"], "max": cam["boundsMax"]}
        print(f"{view:6} {int(mask.sum()):6} model pixels, {len(centres):2} parts found")
    draw = ImageDraw.Draw(sheet)
    for x in (0, PANEL - LINE // 2, 2 * PANEL - LINE // 2, 3 * PANEL - LINE):
        draw.rectangle([x, 0, x + LINE - 1, 2 * PANEL - 1], fill="black")
    for y in (0, PANEL - LINE // 2, 2 * PANEL - LINE):
        draw.rectangle([0, y, 3 * PANEL - 1, y + LINE - 1], fill="black")
    sheet.save(out / "stencil.png")
    psheet.save(out / "stencil.parts.png")
    meta = {"bounds": bounds, "lineWidth": LINE, "model": model, "size": [3 * PANEL, 2 * PANEL], "panels": panels,
            "note": "rect is x, y, w, h in pixels from the top left; a model point p lands at column w/2 + dot(p - center, right) * pixelsPerUnit, row h/2 - dot(p - center, up) * pixelsPerUnit"}
    (out / "stencil.json").write_text(json.dumps(meta, indent=2, sort_keys=True))
    print(f"wrote stencil.png, stencil.parts.png, stencil.json to {out}")


main()
