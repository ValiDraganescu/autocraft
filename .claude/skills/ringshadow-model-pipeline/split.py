# /// script
# requires-python = ">=3.11"
# dependencies = ["numpy", "pillow", "scipy"]
# ///
"""Split a painted turnaround sheet into its views and compare it with the stencil.

    model.sh split <unit> [painting]   (or: uv run split.py art/models/<unit> [painting])

Reads <dir>/stencil.json, stencil.parts.png (from `Autocraft stencil`) and the
painting (default: the first painted.* in <dir>). Finds the black dividing
lines in both, maps each painted panel onto the stencil panel's pixels (so the
stencil's pixels-per-unit holds for it), and cuts the model out of the white
background. Writes <dir>/views/<view>.png (painting), <view>.mask.png, and
<dir>/compare.png: per panel, where the painting leaves the stencil (magenta:
painted only, cyan: stencil only). Each view is first fitted to the stencil
(the stretch and shift that match their bounding boxes), which is reported as
the figure's drift; the saved views and the comparison are the fitted ones,
so what remains is drift in shape. Prints per view the overlap (IoU) before
and after fitting, then how well the painted views agree with each other on
width, depth and height. Also writes the texture for `model.sh install`:
<dir>/skin.jpg (the fitted orthographic views on the stencil's sheet, each
carried out past its silhouette, plus a swatch) and skin_glow.jpg.
"""
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw
from scipy import ndimage

VIEWS_PER_ROW = 3


def runs(flags):
    """(start, end) of each run of True, end exclusive."""
    out, start = [], None
    for i, f in enumerate(flags):
        if f and start is None:
            start = i
        elif not f and start is not None:
            out.append((start, i))
            start = None
    if start is not None:
        out.append((start, len(flags)))
    return out


def dividers(gray, axis, want):
    """The black lines across the sheet along one axis: `want` runs including
    the frame; a missing frame line counts as the image edge."""
    dark = (gray < 70).mean(axis=axis)
    # A dividing line runs the whole sheet; a dark model can fill over half
    # of a column (the Prospector's back plate above its 3/4 view: 0.5 found two
    # lines there).
    lines = [r for r in runs(dark > 0.8) if r[1] - r[0] >= 2]
    size = gray.shape[1 - axis]
    if not lines or lines[0][0] > size * 0.05:
        lines.insert(0, (0, 0))
    if lines[-1][1] < size * 0.95:
        lines.append((size, size))
    if len(lines) != want:
        sys.exit(f"found {len(lines)} dividing lines along axis {axis}, expected {want}: {lines}")
    return lines


def panels(img):
    """Interior rects (x0, y0, x1, y1) of the panels, reading order."""
    gray = np.asarray(img.convert("L"), dtype=np.int16)
    cols = dividers(gray, 0, VIEWS_PER_ROW + 1)
    rows = dividers(gray, 1, 3)
    return [(cols[c][1], rows[r][1], cols[c + 1][0], rows[r + 1][0]) for r in range(2) for c in range(VIEWS_PER_ROW)]


def painted_mask(rgb, inner, near=None, thin=None):
    """The model: everything that is not the near-white, grey background,
    holes filled and specks dropped. Only inside `inner` (x0, y0, x1, y1),
    kept clear of the dividing lines' soft edges, which would otherwise
    close a ring round the panel for the hole fill to flood.

    An image AI sometimes keeps the stencil's panel label, header rule,
    dashed height guides and ground line (the ranger's v3 sheet did, and
    the fit read them as figure): thin horizontal lines are opened away,
    and with `near` (the stencil's silhouette, grown) only the pieces that
    touch it are kept."""
    a = rgb.astype(np.int16)
    lo, hi = a.min(axis=2), a.max(axis=2)
    x0, y0, x1, y1 = inner
    # The background is near white, but an image AI paints it anywhere from
    # white to a light grey (the Atlas's v1 sheets: 223 to 226), so each
    # panel's own background level comes from a ring just inside its edge.
    ring = np.concatenate([lo[y0:y0 + 6, x0:x1].ravel(), lo[y1 - 6:y1, x0:x1].ravel(),
                           lo[y0:y1, x0:x0 + 6].ravel(), lo[y0:y1, x1 - 6:x1].ravel()])
    level = min(225, int(np.median(ring)) - 12) if ring.size else 225
    m = ~((lo > level) & (hi - lo < 25))
    # A soft drop shadow or glow round the figure (the Scorpion's v1 sheet)
    # is colourless and only a little darker than the background; the
    # model's grey metal is far darker, and inner pixels come back with the
    # hole fill below.
    if ring.size:
        m &= ~((lo > int(np.median(ring)) - 40) & (hi - lo < 10))
    keep = np.zeros_like(m)
    keep[y0:y1, x0:x1] = True
    m &= keep
    # `thin` (the stencil's own silhouette, grown a few pixels) keeps what
    # the opening would take there: a level wing seen head-on is itself a
    # thin horizontal line (the Peregrine's, 4 cm thick).
    opened = ndimage.binary_opening(m, structure=np.ones((5, 1), bool))
    m = opened | (m & thin) if thin is not None else opened
    if near is not None:
        lab, k = ndimage.label(m)
        if k:
            touching = np.unique(lab[near & (lab > 0)])
            m = np.isin(lab, touching)
    m = ndimage.binary_closing(m, iterations=2)
    m = ndimage.binary_fill_holes(m)
    m = ndimage.binary_opening(m, iterations=2)
    lab, k = ndimage.label(m)
    if k:
        sizes = ndimage.sum(m, lab, range(1, k + 1))
        m = np.isin(lab, [i + 1 for i, s in enumerate(sizes) if s >= 150])
    return m


# Where triangles no view sees, of parts no view shows, take their colour
# (x, y and half size in sheet pixels; the stencil's plan points at 12, 12),
# and the colour: dark gunmetal.
SWATCH = (12, 12, 12)
SWATCH_COLOR = (48, 50, 56)


def carry_out(rgb, mask):
    """The painting inside the mask, carried outward to the panel's edges
    (each outside pixel takes the nearest painted one), so a triangle at the
    silhouette never samples the white background. The mask is eroded a
    little first: its rim is the painting's outline and antialiasing."""
    inner = ndimage.binary_erosion(mask, iterations=1)
    if not inner.any():
        return rgb
    _, (iy, ix) = ndimage.distance_transform_edt(~inner, return_indices=True)
    return rgb[iy, ix]


def glow(rgb):
    """The glowing paint: bright, saturated pixels (the blue strips, the
    visor), kept at their colour; everything else black."""
    a = rgb.astype(np.float32) / 255
    v, lo = a.max(axis=2), a.min(axis=2)
    sat = np.where(v > 0, (v - lo) / np.maximum(v, 1e-6), 0)
    w = np.clip((sat - 0.35) / 0.25, 0, 1) * np.clip((v - 0.7) / 0.2, 0, 1)
    w = ndimage.gaussian_filter(w, 1.0)
    return (a * w[..., None] * 255).astype(np.uint8)


def best_overlap(pm, sm, ax, bx, ay, by):
    """Tune the fit (stencil pixel x -> painted pixel ax * x + bx, and the
    same for y) for the most overlap between the painted mask and the
    stencil's: coordinate steps in scale and shift, coarse to fine."""
    ys, xs = np.mgrid[0:sm.shape[0], 0:sm.shape[1]]

    def iou(p):
        warped = ndimage.map_coordinates(pm.astype(np.uint8), [p[3] + p[2] * ys, p[1] + p[0] * xs], order=0, cval=0) > 0
        return (warped & sm).sum() / max(1, (warped | sm).sum())

    p, best = [ax, bx, ay, by], None
    best = iou(p)
    for scale, shift in ((0.04, 8), (0.02, 4), (0.01, 2), (0.005, 1)):
        moved = True
        while moved:
            moved = False
            for i, step in ((0, scale), (1, shift), (2, scale), (3, shift)):
                for sign in (1, -1):
                    q = list(p)
                    if i in (0, 2):
                        # Scale about the stencil figure's centre, not the corner.
                        c = np.nonzero(sm.any(axis=0 if i == 0 else 1))[0].mean()
                        q[i] = p[i] * (1 + sign * step)
                        q[i + 1] = p[i + 1] + (p[i] - q[i]) * c
                    else:
                        q[i] = p[i] + sign * step
                    v = iou(q)
                    if v > best + 1e-4:
                        p, best, moved = q, v, True
    return p


def bbox(m):
    ys, xs = np.nonzero(m)
    return (xs.min(), ys.min(), xs.max() + 1, ys.max() + 1) if len(xs) else None


def main():
    d = Path(sys.argv[1])
    meta = json.loads((d / "stencil.json").read_text())
    painting = Path(sys.argv[2]) if len(sys.argv) > 2 else next(iter(sorted(d.glob("painted.*"))), None)
    if painting is None:
        sys.exit(f"no painted.* in {d}")
    stencil = Image.open(d / "stencil.png").convert("RGB")
    parts = np.asarray(Image.open(d / "stencil.parts.png").convert("RGB"))
    paint = Image.open(painting).convert("RGB")
    s_rects, p_rects = panels(stencil), panels(paint)
    out = d / "views"
    out.mkdir(exist_ok=True)
    W, H = meta["size"]
    sheet = Image.new("RGB", (W, H), "white")
    # The texture the game paints the model with: the fitted orthographic
    # views where the stencil has them, and the swatch.
    skin = np.full((H, W, 3), 255, np.uint8)
    draw = ImageDraw.Draw(sheet)
    extents = {}  # view -> (horizontal, vertical) in model units, painting and stencil
    print(f"{painting.name}: {paint.width}x{paint.height}, stencil {W}x{H}")
    print("IoU: overlap with the stencil as painted, then with the whole figure's drift undone;")
    print("drift: the painting's size against the stencil's (wide, tall) and its offset in model units (right, up)")
    print(f"{'view':13} {'IoU':>5} {'fitted':>7}   {'wide':>6} {'tall':>6}  offset")
    for info, s, p in zip(meta["panels"], s_rects, p_rects):
        view, (px, py, pw, ph) = info["view"], info["rect"]
        ppu = info["pixelsPerUnit"]
        # The painted panel, stretched onto the stencil panel's interior; the
        # rest of the full panel is white, so its pixels map to model units
        # exactly as the stencil's do.
        full = Image.new("RGB", (pw, ph), "white")
        sx0, sy0, sx1, sy1 = s
        full.paste(paint.crop(p).resize((sx1 - sx0, sy1 - sy0), Image.LANCZOS), (sx0 - px, sy0 - py))
        m = 6
        sp = parts[py:py + ph, px:px + pw]
        sm = ~((sp > 245).all(axis=2))
        # The painting drifts up to about a tenth of the panel.
        near = ndimage.binary_dilation(sm, iterations=max(8, pw // 12))
        thin = ndimage.binary_dilation(sm, iterations=6)
        pm = painted_mask(np.asarray(full), (sx0 - px + m, sy0 - py + m, sx1 - px - m, sy1 - py - m), near, thin)
        raw = (pm & sm).sum() / max(1, (pm | sm).sum())
        pb, sb = bbox(pm), bbox(sm)
        if not (pb and sb):
            print(f"{view:13} no model found")
            continue
        extents[view] = ((pb[2] - pb[0]) / ppu, (pb[3] - pb[1]) / ppu, (sb[2] - sb[0]) / ppu, (sb[3] - sb[1]) / ppu)
        # Drift of the whole figure: the stretch and shift that put the
        # painting's bounding box on the stencil's. Undo it, so what is left
        # is drift in shape, and the saved view lines up with the stencil.
        # The boxes only start it: one part painted wider (the Ranger's
        # edge-on shield painted face-on, v4) would drag the whole body, so
        # the stretch and shift are then tuned for the most overlap.
        ax, ay = (pb[2] - pb[0]) / (sb[2] - sb[0]), (pb[3] - pb[1]) / (sb[3] - sb[1])
        ax, bx, ay, by = best_overlap(pm, sm, ax, pb[0] - sb[0] * ax, ay, pb[1] - sb[1] * ay)
        kx, ky = 1 / ax, 1 / ay
        cx, cy = (sb[0] + sb[2]) / 2, (sb[1] + sb[3]) / 2
        dx = (ax * cx + bx - cx) / ppu
        dy = -(ay * cy + by - cy) / ppu
        fit = (ax, 0, bx, 0, ay, by)
        full = full.transform(full.size, Image.AFFINE, fit, resample=Image.BICUBIC, fillcolor="white")
        pm = np.asarray(Image.fromarray(pm).transform(full.size, Image.AFFINE, fit, resample=Image.NEAREST)) > 0
        rgb = np.asarray(full)
        iou = (pm & sm).sum() / max(1, (pm | sm).sum())
        full.save(out / f"{view}.png")
        Image.fromarray((pm * 255).astype(np.uint8)).save(out / f"{view}.mask.png")
        if info["orthographic"]:
            skin[py:py + ph, px:px + pw] = carry_out(rgb, pm)
        print(f"{view:13} {raw:5.2f} {iou:7.2f}   {(1 / kx - 1) * 100:+5.1f}% {(1 / ky - 1) * 100:+5.1f}%  {dx:+.3f} {dy:+.3f}")
        # The comparison panel: the painting faded, painted-only magenta,
        # stencil-only cyan.
        fade = (rgb.astype(np.float32) * 0.45 + 255 * 0.55).astype(np.uint8)
        fade[pm & ~sm] = (230, 40, 200)
        fade[sm & ~pm] = (30, 190, 230)
        sheet.paste(Image.fromarray(fade), (px, py))
        draw.text((px + 14, py + 12), f"{view}  IoU {iou:.2f} after fitting (raw {raw:.2f})", fill=(0, 0, 0))
    for x in range(1, VIEWS_PER_ROW):
        draw.line([(x * W // VIEWS_PER_ROW, 0), (x * W // VIEWS_PER_ROW, H)], fill="black", width=meta["lineWidth"])
    draw.line([(0, H // 2), (W, H // 2)], fill="black", width=meta["lineWidth"])
    sheet.save(d / "compare.png")
    x, y, k = SWATCH
    skin[y - k:y + k, x - k:x + k] = SWATCH_COLOR
    Image.fromarray(skin).save(d / "skin.jpg", quality=92)
    Image.fromarray(glow(skin)).save(d / "skin_glow.jpg", quality=92)

    # The painted views against each other: each size of the model appears in
    # several views, which should agree. Stencil numbers in brackets.
    measures = {
        "width (side to side)": [("front", 0), ("back", 0), ("top", 0)],
        "depth (front to back)": [("right", 0), ("left", 0), ("top", 1)],
        "height": [("front", 1), ("right", 1), ("back", 1), ("left", 1)],
    }
    print("\nviews against each other, model units (stencil in brackets):")
    for name, uses in measures.items():
        vals = [(v, extents[v][i], extents[v][i + 2]) for v, i in uses if v in extents]
        if not vals:
            continue
        ps = [p for _, p, _ in vals]
        spread = (max(ps) - min(ps)) / (sum(ps) / len(ps)) * 100
        cells = "  ".join(f"{v} {p:.3f} ({s:.3f})" for v, p, s in vals)
        print(f"  {name:22} {cells}   spread {spread:.0f}%")
    print(f"\nwrote {out}/<view>.png, <view>.mask.png, {d / 'compare.png'} and the texture: skin.jpg, skin_glow.jpg")


if __name__ == "__main__":
    main()
