# /// script
# requires-python = ">=3.11"
# dependencies = ["numpy", "pillow", "scipy"]
# ///
"""Unwarp flat, designed faces (a shield's face, a banner) from the painting
into their own texture, lined up with the part's outline.

    model.sh decal <unit>   (or: uv run decal.py art/models/<unit>)

The skin projects each triangle into the view that sees it best. That is
fine for armour, but a flat face seen at an angle takes its paint from two or
three views plus nearby pixels, and wherever the image AI drew the face's
design off its outline (the ranger's shield ring sat off centre, the fist hid
a corner) the design shows up shifted or cut. A decal instead reads the whole
face from one view, in the part's own texture coordinates, then moves the
painted design onto the outline: the bounding box of its glowing ring is
stretched and shifted to a fixed inset from the edge.

<dir>/decals.json lists the decals, by name:
  path      the part's tree path (as in stencil.skin.json)
  view      the view to read (front, right, back, left, top)
  origin    the part-local point at texture (0, 0), the top left
  u, v      part-local vectors across the texture, left to right, top to bottom
  size      texture width, height in pixels
  outline   the face's outline in texture space (x right, y up, 0...1), as in the model
  ring      the glowing ring's bounding box inset from the outline's, as a
            fraction of width and height ([0.1, 0.05]); omit to keep the
            painted placement
  centre    true: put the emblem inside the ring (the largest bright,
            unsaturated blob: a white star) on the centre line, each half of
            the ring stretched to its own side
  mirror    for a left-right symmetric face: true, a texel the view does not
            see takes its mirror image's paint first; "left" or "right", that
            half of the texture (after the ring fit) is used for both, when
            the other half picked up a neighbour or a hand
  texture   the file name the game loads (Resources/Textures/<texture>.jpg)

Reads paint-pose.json (the painted frames and framing), views/<view>.png and
.mask.png (from split), stencil.skin.json and stencil.parts.png (which pixels
show the part). Writes <dir>/decals/<texture>.jpg and <texture>_glow.jpg and a
check image <dir>/decals/<name>.check.png: the painted view with the face's
outline drawn in, the raw unwarp, and the fitted texture.
"""
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw
from scipy import ndimage

sys.path.insert(0, str(Path(__file__).parent))
from split import glow  # noqa: E402

VIEWS = ["front", "right", "back", "left", "top"]


def id_color(i):
    """Stencil.idColor: the flat colour part i has in stencil.parts.png."""
    h = ((i + 1) * 2654435761) & 0xFFFFFFFF
    return np.array([20 + (h >> s) % 216 for s in (0, 9, 18)], np.float32)


def sample(img, x, y):
    """Bilinear sample of an HxW(xC) array at float pixel coordinates."""
    h, w = img.shape[:2]
    x = np.clip(x - 0.5, 0, w - 1.001)
    y = np.clip(y - 0.5, 0, h - 1.001)
    x0, y0 = np.floor(x).astype(int), np.floor(y).astype(int)
    fx, fy = x - x0, y - y0
    if img.ndim == 3:
        fx, fy = fx[..., None], fy[..., None]
    a, b = img[y0, x0], img[y0, x0 + 1]
    c, d = img[y0 + 1, x0], img[y0 + 1, x0 + 1]
    return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy


def inside(outline, w, h):
    """The outline (x right, y up, 0...1) as a mask in texture pixels."""
    m = Image.new("L", (w, h), 0)
    ImageDraw.Draw(m).polygon([(x * w, (1 - y) * h) for x, y in outline], fill=255)
    return np.asarray(m) > 0


def emblem_x(raw, seen, bx, by, w, h):
    """The x (0...1) of the emblem inside the ring: the largest bright,
    unsaturated blob (a white star), or None if there is none."""
    a = raw / 255
    v, lo = a.max(axis=2), a.min(axis=2)
    sat = (v - lo) / np.maximum(v, 1e-6)
    m = seen & (v > 0.7) & (sat < 0.25)
    box = np.zeros_like(m)
    box[int(by[0] * h):int(by[1] * h), int(bx[0] * w):int(bx[1] * w)] = True
    labels, n = ndimage.label(m & box)
    if n == 0:
        return None
    sizes = ndimage.sum(np.ones_like(labels), labels, range(1, n + 1))
    biggest = 1 + int(np.argmax(sizes))
    if sizes[biggest - 1] < 0.002 * w * h:
        return None
    return float(np.nonzero(labels == biggest)[1].mean() + 0.5) / w


def decal(d, name, spec, pose, plan, parts_sheet):
    view = VIEWS.index(spec["view"])
    panel = pose["panels"][view]
    by_path = {p["path"]: p for p in pose["parts"] if p.get("path")}
    if spec["path"] not in by_path:
        sys.exit(f"{name}: no part {spec['path']} in paint-pose.json")
    frame = np.array(by_path[spec["path"]]["frame"], np.float32).reshape(4, 4).T
    w, h = spec["size"]
    rgb = np.asarray(Image.open(d / "views" / f"{spec['view']}.png").convert("RGB")).astype(np.float32)
    painted = np.asarray(Image.open(d / "views" / f"{spec['view']}.mask.png").convert("L")) > 127

    # Which of the panel's pixels show this part (its own flat colour).
    index = [p.get("path") for p in plan["parts"]].index(spec["path"])
    x0, y0, pw, ph = (int(v) for v in panel["rect"])
    ids = parts_sheet[y0:y0 + ph, x0:x0 + pw].astype(np.float32)
    own = np.abs(ids - id_color(index)).max(axis=2) < 12
    own = ndimage.binary_erosion(own, iterations=1)   # its edge is antialiased into neighbours

    # Every texel's point on the face, in the stencil's frame, then in the panel.
    u, v = np.meshgrid((np.arange(w) + 0.5) / w, (np.arange(h) + 0.5) / h)
    local = (np.array(spec["origin"], np.float32)[:, None, None]
             + np.array(spec["u"], np.float32)[:, None, None] * u
             + np.array(spec["v"], np.float32)[:, None, None] * v)
    p = np.einsum("ij,jhw->ihw", frame[:3, :3], local) + frame[:3, 3][:, None, None]
    rel = p - np.array(panel["center"], np.float32)[:, None, None]
    ppu = panel["pixelsPerUnit"]
    col = pw / 2 + np.einsum("i,ihw->hw", np.array(panel["right"], np.float32), rel) * ppu
    row = ph / 2 - np.einsum("i,ihw->hw", np.array(panel["up"], np.float32), rel) * ppu
    face = inside(spec["outline"], w, h)
    raw = sample(rgb, col, row)
    seen = face & (sample(own.astype(np.float32), col, row) > 0.5) & (sample(painted.astype(np.float32), col, row) > 0.5)
    print(f"{name}: {spec['view']} view, the face spans {np.ptp(col[face]):.0f} x {np.ptp(row[face]):.0f} px of the painting "
          f"for a {w} x {h} texture; {seen.sum() / face.sum():.0%} of it seen")

    # Move the painted ring onto its place on the outline.
    tex, ok = raw, seen
    if spec.get("ring"):
        g = glow(raw.astype(np.uint8)).max(axis=2) > 60
        g &= seen
        ys, xs = np.nonzero(g)
        if len(xs) < 50:
            print(f"{name}: no glowing ring found; kept the painted placement")
        else:
            bx = np.percentile(xs, [0.5, 99.5]) / w
            by = np.percentile(ys, [0.5, 99.5]) / h
            iu, iv = spec["ring"]
            tx, ty = np.array([iu, 1 - iu]), np.array([iv, 1 - iv])
            sx, sy = (bx[1] - bx[0]) / (tx[1] - tx[0]), (by[1] - by[0]) / (ty[1] - ty[0])
            # target texel -> painted texel
            su = bx[0] + (u - tx[0]) * sx
            sv = by[0] + (v - ty[0]) * sy
            centre = emblem_x(raw, seen, bx, by, w, h) if spec.get("centre") else None
            if centre is not None:
                # The image AI drew one half of the design narrower (as if in
                # perspective): each half of the ring goes to its own side of
                # the centre line, the emblem on it.
                left = bx[0] + (u - tx[0]) / (0.5 - tx[0]) * (centre - bx[0])
                right = centre + (u - 0.5) / (tx[1] - 0.5) * (bx[1] - centre)
                su = np.where(u < 0.5, left, right)
                print(f"{name}: emblem at x {centre:.3f}, {(centre - bx[0]) / (bx[1] - bx[0]):.0%} across the painted ring; "
                      f"put on the centre line")
            tex = sample(raw, su * w, sv * h)
            ok = face & (sample(seen.astype(np.float32), su * w, sv * h) > 0.5)
            print(f"{name}: painted ring at x {bx[0]:.3f}-{bx[1]:.3f}, y {by[0]:.3f}-{by[1]:.3f} of the face; "
                  f"moved to x {tx[0]:.2f}-{tx[1]:.2f}, y {ty[0]:.2f}-{ty[1]:.2f} (scale {1 / sx:.3f} x {1 / sy:.3f})")

    # Fill what the view does not show: the mirror image first, then the nearest seen texel.
    tex = tex.copy()
    keep = spec.get("mirror")
    if keep in ("left", "right"):
        # The design is symmetric and one half came out clean: that half is
        # the whole design, mirrored onto the other.
        half = np.arange(w) < w / 2 if keep == "right" else np.arange(w) >= w / 2
        tex[:, half] = tex[:, ::-1][:, half]
        ok = ok.copy()
        ok[:, half] = ok[:, ::-1][:, half]
        print(f"{name}: the {keep} half mirrored onto the other")
    elif keep:
        flip, flip_ok = tex[:, ::-1], ok[:, ::-1]
        take = face & ~ok & flip_ok
        tex[take] = flip[take]
        ok = ok | take
        print(f"{name}: {take.sum() / face.sum():.0%} of the face from its mirror image")
    if not ok.all():
        _, (iy, ix) = ndimage.distance_transform_edt(~ok, return_indices=True)
        print(f"{name}: {(face & ~ok).sum() / face.sum():.0%} of the face from the nearest painted texel")
        tex = tex[iy, ix]
    tex = np.clip(tex, 0, 255).astype(np.uint8)

    out = d / "decals"
    out.mkdir(exist_ok=True)
    Image.fromarray(tex).save(out / f"{spec['texture']}.jpg", quality=92)
    Image.fromarray(glow(tex)).save(out / f"{spec['texture']}_glow.jpg", quality=92)

    # The check: the painted view with the face's outline, the raw unwarp (grey where unseen), the texture.
    view_img = Image.fromarray(rgb.astype(np.uint8))
    dr = ImageDraw.Draw(view_img)
    edge = [(float(col[min(int((1 - y) * h), h - 1), min(int(x * w), w - 1)]),
             float(row[min(int((1 - y) * h), h - 1), min(int(x * w), w - 1)])) for x, y in spec["outline"]]
    dr.polygon(edge, outline=(255, 0, 255))
    xs_, ys_ = [e[0] for e in edge], [e[1] for e in edge]
    pad = 20
    crop = view_img.crop((int(min(xs_)) - pad, int(min(ys_)) - pad, int(max(xs_)) + pad, int(max(ys_)) + pad))
    crop = crop.resize((int(crop.width * h / crop.height), h), Image.LANCZOS)
    shown = np.where(seen[..., None], raw, 90).astype(np.uint8)
    sheet = Image.new("RGB", (crop.width + 2 * w + 40, h), (255, 255, 255))
    sheet.paste(crop, (0, 0))
    sheet.paste(Image.fromarray(shown), (crop.width + 20, 0))
    sheet.paste(Image.fromarray(tex), (crop.width + w + 40, 0))
    sheet.save(out / f"{name}.check.png")
    print(f"{name}: wrote {out / (spec['texture'] + '.jpg')}, _glow.jpg and {out / (name + '.check.png')}")


def main():
    d = Path(sys.argv[1]).resolve()
    specs = json.loads((d / "decals.json").read_text())
    pose = json.loads((d / "paint-pose.json").read_text())
    plan = json.loads((d / "stencil.skin.json").read_text())
    parts_sheet = np.asarray(Image.open(d / "stencil.parts.png").convert("RGB"))
    for name, spec in specs.items():
        if not name.startswith("_"):
            decal(d, name, spec, pose, plan, parts_sheet)


if __name__ == "__main__":
    main()
