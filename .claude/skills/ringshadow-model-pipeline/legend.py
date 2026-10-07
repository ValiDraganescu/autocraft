"""Name the parts on one nets sheet, row by row, for the nets prompt.

    uv run legend.py <unit dir> <sheet>      (sheet counts from 1, as in nets.<sheet>.png)

Reads <unit dir>/nets/nets.json and <unit dir>/parts.txt (tree path, spaces,
name, colon, surface); the legend gives each net's surface only. The nets are shelf-packed, so a row is the nets that share a top edge;
the legend reads them top to bottom, left to right, as Grok would count them.
"""
import json
import sys
from pathlib import Path

ROWS = ["Top row", "Second row", "Third row", "Fourth row", "Fifth row", "Sixth row", "Seventh row"]


def names(path: Path) -> dict[str, str]:
    out = {}
    for line in path.read_text().splitlines():
        if not line.strip() or line.startswith("#"):
            continue
        key, _, name = line.partition(" ")
        out[key] = name.strip()
    return out


def size_word(w: int, h: int, sheet_w: int) -> str:
    big = max(w, h) / sheet_w
    return "tiny " if big < 0.05 else "small " if big < 0.1 else "large " if big > 0.25 else ""


def legend(unit: Path, sheet: int) -> str:
    nets = json.loads((unit / "nets" / "nets.json").read_text())
    known = names(unit / "parts.txt")
    here = [n for n in nets["nets"] if n["sheet"] == sheet - 1]
    rows: dict[int, list] = {}
    for n in here:
        rows.setdefault(n["rect"][1], []).append(n)
    lines = []
    for r, top in enumerate(sorted(rows)):
        row = sorted(rows[top], key=lambda n: n["rect"][0])
        label = ROWS[r] if r < len(ROWS) - 1 or r == len(rows) - 1 else f"Row {r + 1}"
        if r == len(rows) - 1 and r > 0:
            label = "Bottom row"
        items = []
        for k, n in enumerate(row, 1):
            name = next((known[p] for p in n["paths"] if p in known), None)
            if name is None:
                sys.exit(f"parts.txt has no name for any of {n['paths']}")
            _, _, w, h = n["rect"]
            # Only the surface: an object's name makes Grok draw the object.
            surface = name.partition(":")[2].strip() or name
            items.append(f"{k}) {size_word(w, h, nets['size'][0])}{surface}")
        lines.append(f"- {label}, {len(row)} from left to right: " + "; ".join(items) + ".")
    # The count first: with a blank sheet Grok drew parts of another sheet's
    # legend into the empty space (nets v2).
    head = f"The sheet holds exactly {len(here)} nets. Their surfaces, row by row from the top:"
    return "\n".join([head] + lines)


if __name__ == "__main__":
    print(legend(Path(sys.argv[1]), int(sys.argv[2])))
