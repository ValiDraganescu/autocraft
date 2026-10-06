# /// script
# dependencies = ["numpy", "pillow", "scipy"]
# ///
"""Put the prefilled faces back and move Grok's paint to each part's material colour.

    uv run recolour.py <unit dir> <sheet> <grok image>

Grok paints grey clay in full detail but all in one light grey (nets v2), and
leaves tinted clay nearly as given (v4, v5). So Grok paints the grey sheet and
this sets the colour: on every net's faces that were clay, the brightness is
scaled so its mean matches the part's material colour from
nets.<sheet>.tinted.png (tint.py) and the hue is that colour's; relative
contrast, so the detail, stays. Glow parts are left as Grok returned them, and
the faces the sheet came with mostly painted (PASTE_MIN) are the prefill's again.
A coloured part that Grok already painted in its colour keeps Grok's colours
(CHROMA_MIN, HUE_MAX). A part whose look names no paint colour loses the team
blue the prefill brought (TEAM_HUE): the turnaround can paint a gunmetal part
blue in one view (the juggernaut's TOP painted its hump blue). Where the
two meet, Grok's paint takes on the prefill's tone near the edge (the
difference measured along it, fading over SEAM_REACH px into Grok's paint)
and the last SEAM_BLEND px cross-fade, so the pasted faces do not show as
patches. Writes <grok image stem>.recoloured.png on the sheet's pixels
(fitted as netscheck.py fits).
"""
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image
from scipy.ndimage import distance_transform_edt, gaussian_filter

from legend import names
from netscheck import CLAY_GREY, fit, grey, inside, material, painted_mask

SEAM_RING = 4     # px either side of the edge whose tones are compared
SEAM_REACH = 14   # px into Grok's paint over which that match fades out
SEAM_BLEND = 3    # px either side of the edge that cross-fade
PASTE_MIN = 0.5   # a face's prefill goes back only if it covers this much of the face
CHROMA_MIN = 8    # Grok's median colour this far from grey (max - min channel) counts as coloured
HUE_MAX = 40      # degrees between Grok's hue and the named paint's for Grok's colour to stay
# Paint colours a parts.txt look can name, with their hue in degrees.
PAINT_HUES = [("deep blue", 210), ("hazard", 45), ("yellow", 45), ("red", 0)]
TEAM_HUE = 210    # the team blue: prefill this close to it (HUE_MAX) is wrong on an unpainted part


def hue_chroma(c):
    """Hue in degrees and chroma (max - min channel) of an RGB colour."""
    import colorsys
    r, g, b = (float(v) / 255 for v in c)
    return colorsys.rgb_to_hsv(r, g, b)[0] * 360, (max(c) - min(c))


def blend_seams(res, grok, prefill, paint, shape, rect):
    """Inside one net: ease Grok's paint into the prefilled faces beside it."""
    x, y, w, h = rect
    sl = np.s_[y:y + h, x:x + w]
    sh, pm = shape[sl], paint[sl] & shape[sl]
    clay = sh & ~pm
    if pm.sum() < 30 or clay.sum() < 30:
        return
    d_in = distance_transform_edt(pm)     # into the prefill, from its edge
    d_out = distance_transform_edt(~pm)   # into Grok's paint
    inner = pm & (d_in <= SEAM_RING)
    outer = clay & (d_out <= SEAM_RING)
    # Each side's tone along the edge, spread smoothly over the net
    # (normalised convolution, so empty areas do not pull it to zero); the
    # difference is how far Grok's paint is off at each point of the edge.
    def spread(img, where):
        wsum = gaussian_filter(where.astype(np.float32), SEAM_REACH / 2)
        val = np.stack([gaussian_filter(img[..., c] * where, SEAM_REACH / 2) for c in range(3)], -1)
        return val / np.maximum(wsum, 1e-3)[..., None], wsum
    near_prefill, w_in = spread(prefill[sl], inner)
    near_grok, w_out = spread(grok[sl], outer)
    field = near_prefill - near_grok
    fade = np.clip(1 - d_out / SEAM_REACH, 0, 1) * ((w_in > 1e-3) & (w_out > 1e-3))
    region = res[sl]
    region[clay] = np.clip(grok[sl][clay] + field[clay] * fade[clay][:, None], 0, 255)
    # Cross-fade the last few pixels: prefill weight 1 at SEAM_BLEND inside,
    # 0 at SEAM_BLEND outside.
    signed = np.where(pm, d_in, -d_out)
    a = np.clip((signed + SEAM_BLEND) / (2 * SEAM_BLEND), 0, 1)[..., None]
    band = sh & (np.abs(signed) < SEAM_BLEND)
    mixed = a * prefill[sl] + (1 - a) * region
    region[band] = mixed[band]


def main():
    unit, sheet, path = Path(sys.argv[1]), int(sys.argv[2]), Path(sys.argv[3])
    nets = json.loads((unit / "nets" / "nets.json").read_text())
    size = nets["size"]
    rgb = lambda name: np.asarray(Image.open(unit / "nets" / name).convert("RGB")).astype(np.float32)
    blank, prefill, tinted = rgb(f"nets.{sheet}.png"), rgb(f"nets.{sheet}.painted.png"), rgb(f"nets.{sheet}.tinted.png")
    shape = (grey(blank) < 245) & inside(size)
    out = fit(Image.open(path).convert("RGB"), shape, size)
    paint = painted_mask(blank, prefill, shape)
    # A face the turnaround saw only in part carries a ragged patch of
    # prefill (the stencil marks paint per triangle; 134 of the Ranger's 230
    # prefilled faces are under 60% covered). Grok's copy of such a face is
    # whole, so it stays; only mostly painted faces get the prefill back.
    for n in nets["nets"]:
        if n["sheet"] != sheet - 1:
            continue
        for t in n["tiles"]:
            x, y, w, h = [int(round(v)) for v in t["rect"]]
            s, pm = shape[y:y + h, x:x + w], paint[y:y + h, x:x + w]
            if s.sum() and (pm & s).sum() < PASTE_MIN * s.sum():
                pm[s] = False
    clay = shape & ~paint
    known = names(unit / "parts.txt")
    res = out.copy()
    # The prefilled faces are the installed skin, already right: put them
    # back. Grok's copy of them drifts (v7 erased the stars on the shield,
    # chest and pads; v6 added stars to the pads).
    res[paint] = prefill[paint]
    # Team blue in the prefill of a part whose look names no paint colour:
    # to grey of the same brightness. The turnaround can paint a gunmetal
    # part blue in one view (the Juggernaut's TOP painted its hump blue, its
    # FRONT the pads' gunmetal rims). The seams below match Grok's paint to
    # the prefill as it is now.
    neutral = []
    for n in nets["nets"]:
        look = next((known[p] for p in n["paths"] if p in known), "")
        if n["sheet"] != sheet - 1 or material(look) == "glow" or any(w in look.partition(":")[2] for w, _ in PAINT_HUES):
            continue
        x, y, w, h = n["rect"]
        region, pm = res[y:y + h, x:x + w], paint[y:y + h, x:x + w] & shape[y:y + h, x:x + w]
        px = region[pm]
        r, g, b = px[:, 0], px[:, 1], px[:, 2]
        hi, lo = px.max(1), px.min(1)
        d = np.maximum(hi - lo, 1e-3)
        hue = np.where(hi == r, (g - b) / d % 6, np.where(hi == g, (b - r) / d + 2, (r - g) / d + 4)) * 60
        blue = (hi - lo >= CHROMA_MIN) & (np.minimum(abs(hue - TEAM_HUE), 360 - abs(hue - TEAM_HUE)) <= HUE_MAX)
        if blue.sum() > 0.05 * max(pm.sum(), 1):
            px[blue] = px[blue].mean(1, keepdims=True)
            region[pm] = px
            neutral.append(look.split(":")[0])
    prefill = np.where(paint[..., None], res, prefill)
    kept = []
    for n in nets["nets"]:
        if n["sheet"] != sheet - 1:
            continue
        look = next((known[p] for p in n["paths"] if p in known), "")
        if material(look) == "glow":
            continue
        x, y, w, h = n["rect"]
        want = next((hue for word, hue in PAINT_HUES if word in look.partition(":")[2]), None)
        m = clay[y:y + h, x:x + w] & (grey(blank[y:y + h, x:x + w]) > 60)
        if m.sum() < 30:
            continue
        # The material colour, unshaded: the tint over the clay's shading.
        shade = grey(blank[y:y + h, x:x + w])[m][:, None] / CLAY_GREY
        colour = np.median(tinted[y:y + h, x:x + w][m] / np.maximum(shade, 1e-3), 0)
        # A part whose look names a paint colour (the Prospector's deep blue bell,
        # its hazard stripes) that Grok already painted in that colour keeps
        # Grok's colours, stripes and all. Recolouring pulled every pixel to
        # the one median colour: the stripes' black and yellow average to
        # grey, and the bell went blue-grey (Prospector nets v1).
        gh, gc = hue_chroma(np.median(out[y:y + h, x:x + w][m], 0))
        if "--debug" in sys.argv:
            print(n["paths"][0], look.split(":")[0], "grok hue", round(gh), "chroma", round(gc), "wants", want)
        if want is not None and gc >= CHROMA_MIN and min(abs(gh - want), 360 - abs(gh - want)) <= HUE_MAX:
            res[y:y + h, x:x + w][m] = out[y:y + h, x:x + w][m]
            kept.append(look.split(":")[0])
            continue
        lum = grey(out[y:y + h, x:x + w])[m]
        k = colour.mean() / max(lum.mean(), 1)
        region = res[y:y + h, x:x + w]
        region[m] = np.clip((lum * k)[:, None] * (colour / colour.mean())[None, :], 0, 255)
    # Grok's paint, recoloured, beside the prefill: match it at the edges.
    grok = res.copy()
    for n in nets["nets"]:
        if n["sheet"] == sheet - 1:
            blend_seams(res, grok, prefill, paint, shape, [int(round(v)) for v in n["rect"]])
    dest = path.with_name(path.stem + ".recoloured.png")
    Image.fromarray(res.astype(np.uint8)).save(dest)
    print(f"wrote {dest}")
    if kept:
        print(f"kept Grok's own colour on {len(kept)} nets: {', '.join(kept)}")
    if neutral:
        print(f"took the team blue off the prefill of {len(neutral)} unpainted nets: {', '.join(neutral)}")


if __name__ == "__main__":
    main()
