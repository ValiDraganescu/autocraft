# /// script
# dependencies = ["numpy", "pillow", "scipy"]
# ///
"""Measure a Grok-painted nets sheet against the sheet it was given.

    uv run netscheck.py <unit dir> <sheet> <grok image> [--given painted|tinted|blank]

Compares with the sheet that was attached: <unit dir>/nets/nets.<sheet>.<given>.png
(painted, the default: the prefill; tinted: the prefill with the clay in each
part's material colour, tint.py; blank: nets.<sheet>.png). Grok returns the sheet at its own
size (1728x1152 for 1536x1024) and sometimes a few pixels off, so its image is
first fitted to the sheet: scale and shift from the shapes' bounding box, then
the shift that gives the best overlap. Then it prints:
- layout: IoU of the non-white shapes, the whole sheet and the worst nets;
- kept: how much the faces that were already painted changed (prefill only);
- detail: local contrast (std in 7x7 windows) inside the faces, 5 px in from
  every outline and painted edge, so outlines do not count: Grok's fill of the
  clay, the prefill's painted faces, and the clay itself;
- tone: mean brightness inside each net, grouped by the material its
  parts.txt look names first, against the same material on the prefill (the
  installed skin, from the turnaround).
Writes <grok image stem>.check.png: given sheet | Grok's, fitted | the change, stacked.
"""
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image
from scipy.ndimage import binary_erosion, binary_fill_holes, label, uniform_filter

from legend import names

MATERIALS = [("glowing", "glow"), ("dark gunmetal", "dark gunmetal"), ("deep blue", "blue paint"),
             ("bright steel", "bright steel"), ("gunmetal", "gunmetal"), ("silver", "silver"),
             ("dark", "dark")]
FRAME = 12
CLAY_GREY = 187.0  # the blank sheet's flat clay grey
HOLE = 1500  # px: paint the colour of clay (the shield's white star) is still paint
HOLE_DIFF = 3.0  # grey levels: a hole closer than this to the blank, on average, is clay the turnaround missed


def painted_mask(blank, painted, shape):
    """Pixels the prefill painted: those that differ from the blank sheet,
    plus small holes in them (painted the clay's own grey, like a white star).
    A clay face framed by a painted rim is a big hole and stays clay. So does a
    small hole that is the blank itself (HOLE_DIFF): a triangle the turnaround
    did not see, inside a face it did. Painted holes differ by 4.5-10 grey
    levels on average and missed triangles by 0-2 (ranger, prospector, comet); the
    comet's forearm took a clay triangle back as paint before this."""
    diff = np.abs(painted - blank).max(2)
    paint = shape & (diff > 12)
    holes = binary_fill_holes(paint) & ~paint
    lab, n = label(holes)
    sizes = np.bincount(lab.ravel())
    means = np.bincount(lab.ravel(), weights=diff.ravel()) / np.maximum(sizes, 1)
    small = (sizes < HOLE) & (means >= HOLE_DIFF)
    small[0] = False
    return paint | (small[lab] & shape)


def load(p: Path) -> Image.Image:
    return Image.open(p).convert("RGB")


def grey(a: np.ndarray) -> np.ndarray:
    return a.astype(np.float32).mean(2)


def bbox(mask: np.ndarray):
    ys, xs = np.nonzero(mask)
    return xs.min(), ys.min(), xs.max() + 1, ys.max() + 1


def inside(size) -> np.ndarray:
    m = np.zeros((size[1], size[0]), bool)
    m[FRAME:-FRAME, FRAME:-FRAME] = True
    return m


def fit(grok: Image.Image, shape: np.ndarray, size) -> np.ndarray:
    """Grok's image on the sheet's pixels."""
    g = np.asarray(grok)
    gin = np.zeros(g.shape[:2], bool)
    f = round(FRAME * grok.size[0] / size[0])
    gin[f:-f, f:-f] = True
    x0, y0, x1, y1 = bbox((grey(g) < 235) & gin)
    X0, Y0, X1, Y1 = bbox(shape)
    sx, sy = (x1 - x0) / (X1 - X0), (y1 - y0) / (Y1 - Y0)
    best = None
    if grok.size == tuple(size):
        # Already on the sheet's pixels (recolour.py's output): the frame can
        # sit inside the margin and throw the bounding box, so try it as is.
        im = np.asarray(grok)
        got = (grey(im) < 235) & inside(size)
        best = ((shape & got).sum() / (shape | got).sum(), im)
    for dx in range(-6, 7):
        for dy in range(-6, 7):
            # Sheet pixel (X, Y) comes from Grok's (x0 + (X - X0 + dx) * sx, ...).
            a = (sx, 0, x0 - (X0 - dx) * sx, 0, sy, y0 - (Y0 - dy) * sy)
            im = np.asarray(grok.transform(tuple(size), Image.AFFINE, a, Image.BILINEAR, fillcolor=(255, 255, 255)))
            got = (grey(im) < 235) & inside(size)
            iou = (shape & got).sum() / (shape | got).sum()
            if best is None or iou > best[0]:
                best = (iou, im)
    return best[1].astype(np.float32)


def local_std(g: np.ndarray) -> np.ndarray:
    m = uniform_filter(g, 7)
    return np.sqrt(np.maximum(uniform_filter(g * g, 7) - m * m, 0))


def material(name: str) -> str:
    look = name.partition(":")[2] or name
    hits = [(look.find(key), -len(key), label) for key, label in MATERIALS if key in look]
    return min(hits)[2] if hits else "other"


def main():
    unit, sheet, path = Path(sys.argv[1]), int(sys.argv[2]), Path(sys.argv[3])
    kind = sys.argv[sys.argv.index("--given") + 1] if "--given" in sys.argv else "painted"
    blank_given = kind == "blank"
    nets = json.loads((unit / "nets" / "nets.json").read_text())
    size = nets["size"]
    blank = np.asarray(load(unit / "nets" / f"nets.{sheet}.png")).astype(np.float32)
    prefill = np.asarray(load(unit / "nets" / f"nets.{sheet}.painted.png")).astype(np.float32)
    given = blank if blank_given else np.asarray(load(unit / "nets" / f"nets.{sheet}.{kind}.png")).astype(np.float32)
    shape = (grey(blank) < 245) & inside(size)
    out = fit(load(path), shape, size)
    g_out, g_pre = grey(out), grey(prefill)

    got = (g_out < 235) & inside(size)
    iou = (shape & got).sum() / (shape | got).sum()
    painted = painted_mask(blank, prefill, shape)
    core = binary_erosion(shape, iterations=5)
    painted_core = binary_erosion(painted, iterations=5)
    clay_core = core & binary_erosion(~painted, iterations=5)
    std_out, std_pre = local_std(g_out), local_std(g_pre)

    print(f"sheet {sheet}: {path.name} against nets.{sheet}.{'png (blank)' if blank_given else kind + '.png'}")
    print(f"layout: outline IoU {iou:.2f}")
    if not blank_given:
        d = np.abs(out - given).max(2)[painted_core]
        print(f"kept: painted faces changed by {d.mean():.0f}/255 on average, {100 * (d > 40).mean():.0f}% of pixels by more than 40")
    filled = core if blank_given else clay_core
    # Did Grok paint the unpainted faces at all? Against the tinted sheet a
    # face left as given changes little (Grok's re-encode alone moves ~5).
    moved = np.abs(out - given).max(2)[filled]
    print(f"painted over: the unpainted faces changed by {moved.mean():.0f}/255 on average, "
          f"{100 * (moved > 30).mean():.0f}% of their pixels by more than 30")
    print(f"detail (local contrast inside faces): Grok's fill {std_out[filled].mean():.1f}; "
          f"turnaround's painted faces {std_pre[painted_core].mean():.1f}; clay {local_std(grey(blank))[clay_core].mean():.1f}")

    known = names(unit / "parts.txt")
    rows, by_mat, ref_mat = [], {}, {}
    for i, n in enumerate(nets["nets"]):
        if n["sheet"] != sheet - 1:
            continue
        x, y, w, h = n["rect"]
        box = np.zeros_like(shape)
        box[y:y + h, x:x + w] = True
        whole = shape & box
        n_iou = (whole & got).sum() / max((whole | (got & box)).sum(), 1)
        name = next((known[p] for p in n["paths"] if p in known), "?")
        mat = material(name)
        mask = filled & box
        lum = g_out[mask].mean() if mask.sum() > 30 else float("nan")
        if lum == lum:
            by_mat.setdefault(mat, []).append(lum)
        ref = painted_core & box
        if ref.sum() > 30:
            ref_mat.setdefault(mat, []).append(g_pre[ref].mean())
        ch = (np.abs(out - given).max(2)[mask] > 30).mean() * 100 if mask.any() else float("nan")
        rows.append((i, name.split(":")[0], mat, n_iou, lum, std_out[mask].mean() if mask.any() else float("nan"), ch))
    worst = sorted(rows, key=lambda r: r[3])[:5]
    print("worst layout: " + "; ".join(f"{r[1]} {r[3]:.2f}" for r in worst))
    print("tone inside the faces by the material parts.txt names (mean brightness 0-255):")
    for mat, v in sorted(by_mat.items(), key=lambda kv: np.mean(kv[1])):
        r = ref_mat.get(mat)
        ref = f"turnaround {np.mean(r):3.0f}" if r else "turnaround   -"
        print(f"  {mat:14} Grok {np.mean(v):3.0f}  {ref}  ({len(v)} nets, {min(v):.0f}-{max(v):.0f})")
    print("per net: name, material, layout IoU, brightness, local contrast, % of unpainted pixels changed by more than 30")
    for r in rows:
        print(f"  {r[0]:2} {r[1][:34]:34} {r[2]:13} {r[3]:4.2f} {r[4]:5.0f} {r[5]:5.1f} {r[6]:4.0f}%")

    change = np.clip(np.abs(out - given).max(2) * 2, 0, 255).astype(np.uint8)
    stack = np.concatenate([given.astype(np.uint8), out.astype(np.uint8), np.stack([change] * 3, 2)], 0)
    dest = path.with_name(path.stem + ".check.png")
    Image.fromarray(stack).resize((size[0] // 2, size[1] * 3 // 2)).save(dest)
    print(f"wrote {dest}")


if __name__ == "__main__":
    main()
