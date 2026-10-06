#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = ["pillow>=10", "numpy>=1.26"]
# ///
"""F2 reference shots: render every scene of `scenes.json` in the Swift game
(`autocraft windowshot`) and in Unreal (`-AcScene=NAME`), then write a
side-by-side PNG and a difference score per scene.

    uv run unreal/Tools/shots/compare.py                   # every scene
    uv run unreal/Tools/shots/compare.py top orders pilot-ranger
    uv run unreal/Tools/shots/compare.py --group pilot --jobs 2
    uv run unreal/Tools/shots/compare.py --only-score      # re-score PNGs already there
    uv run unreal/Tools/shots/compare.py --list

Out (default `unreal/Saved/Shots/`, or --out DIR):
    swift/NAME.png, ue/NAME.png, logs/NAME.{swift,ue}.log,
    side/NAME.png  (Swift | Unreal, the difference map below),
    report.json, report.md (the table), sheet.png (every pair, small).

Scores (both images at the Swift size, then 1/4 scale, a light blur):
    diff   mean |RGB difference| in % of full scale (0 = identical),
    view   the same over the 3-D view only (above the console's 234 pt),
    hud    the same over the console strip only,
    edges  correlation of the two gradient maps (1 = same shapes in the
           same places; layout and framing, blind to colour),
    grade  A (edges >= .80 and diff <= 6), B (>= .65, <= 10), C (>= .45),
           else D; "-" when a side is missing.

Rules it keeps (AGENTS.md, the agent brief): `make` must succeed before the
Swift binary runs; every Unreal run has -nosound; nothing here opens a
window, plays sound or relaunches the game; it kills only processes it
started.
"""
from __future__ import annotations

import argparse
import concurrent.futures as cf
import json
import os
import subprocess
import sys
import time
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = Path(__file__).resolve().parent
UNREAL = HERE.parent.parent
REPO = UNREAL.parent
TABLE = HERE / "scenes.json"
SWIFT_BIN = REPO / ".build/release/Autocraft"
UE_BIN = Path("/Users/Shared/Epic Games/UE_5.8/Engine/Binaries/Mac/UnrealEditorBG.app/Contents/MacOS/UnrealEditor")  # Dock-less copy (LSUIElement); see Tools/README.md
UPROJECT = UNREAL / "Autocraft.uproject"
CONSOLE_PT = 234  # Console.cover at every width (D2), in points = pixels at 1x


def load_table() -> tuple[dict, list[dict]]:
    doc = json.loads(TABLE.read_text())
    return doc["defaults"], doc["scenes"]


def field(scene: dict, defaults: dict, name: str):
    return scene[name] if name in scene else defaults.get(name)


def swift_command(scene: dict, defaults: dict, out: Path) -> tuple[list[str], dict]:
    style, size = field(scene, defaults, "map").split("-")[:2]
    cmd = [str(SWIFT_BIN), "windowshot", str(out),
           "--width", str(field(scene, defaults, "width")), "--height", str(field(scene, defaults, "height")),
           "--warm", str(field(scene, defaults, "warm"))]
    if not scene.get("playground"):
        cmd += ["--map", style, "--size", size]
    zoom, at = field(scene, defaults, "zoom"), field(scene, defaults, "at")
    if zoom is not None:
        cmd += ["--zoom", str(zoom)]
    if at is not None:
        cmd += ["--at", f"{at[0]},{at[1]}"]
    cmd += scene.get("swift", [])
    env = dict(os.environ)
    env.update(scene.get("env", {}))
    return cmd, env


def ue_command(scene: dict, defaults: dict, out: Path, log: Path) -> list[str]:
    w, h = field(scene, defaults, "width"), field(scene, defaults, "height")
    return [str(UE_BIN), str(UPROJECT), "/Game/Maps/Battlefield", "-game", "-RenderOffscreen", "-windowed",
            "-ForceRes", f"-ResX={w}", f"-ResY={h}", "-unattended", "-nosplash", "-nosound", "-ini:Input:[/Script/Engine.InputSettings]:bCaptureMouseOnLaunch=False",
            f"-abslog={log}", f"-AcScene={scene['name']}", f"-AcShot={out}"]


def run(cmd: list[str], log: Path, env: dict | None = None, timeout: float = 420) -> int:
    with open(log, "w") as f:
        p = subprocess.Popen(cmd, stdout=f, stderr=subprocess.STDOUT, env=env, cwd=REPO)
        try:
            return p.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            p.kill()  # only the process this script started, by its own handle
            p.wait()
            f.write(f"\n[compare.py] killed after {timeout} s\n")
            return -9


def render_swift(scene, defaults, dirs) -> str:
    out = dirs["swift"] / f"{scene['name']}.png"
    cmd, env = swift_command(scene, defaults, out)
    log = dirs["logs"] / f"{scene['name']}.swift.log"
    if out.exists():
        out.unlink()
    code = run(cmd, log, env, timeout=180)
    return "ok" if out.exists() else f"failed ({code}, {log.name})"


def render_ue(scene, defaults, dirs) -> str:
    if scene.get("ue") is None:
        return "not staged"
    out = dirs["ue"] / f"{scene['name']}.png"
    log = dirs["logs"] / f"{scene['name']}.ue.log"
    for attempt in range(3):
        if out.exists():
            out.unlink()
        code = run(ue_command(scene, defaults, out, log), dirs["logs"] / f"{scene['name']}.ue.stdout", timeout=420)
        if out.exists():
            return "ok"
        text = log.read_text(errors="replace") if log.exists() else ""
        # Another agent relinking the module: wait and retry.
        if "module Autocraft" in text or "Autocraft could not be loaded" in text or code == -9:
            time.sleep(20)
            continue
        break
    return f"failed ({code}, {log.name})"


# --- scoring ---------------------------------------------------------------

def rgb(path: Path, size: tuple[int, int] | None = None) -> Image.Image:
    im = Image.open(path).convert("RGB")
    if size and im.size != size:
        im = im.resize(size, Image.LANCZOS)
    return im


def small(im: Image.Image) -> np.ndarray:
    w, h = im.size
    s = im.resize((max(1, w // 4), max(1, h // 4)), Image.BOX).filter(ImageFilter.GaussianBlur(1))
    return np.asarray(s, dtype=np.float32) / 255.0


def gradient(a: np.ndarray) -> np.ndarray:
    g = a.mean(axis=2)
    gx = np.zeros_like(g)
    gy = np.zeros_like(g)
    gx[:, 1:-1] = g[:, 2:] - g[:, :-2]
    gy[1:-1, :] = g[2:, :] - g[:-2, :]
    return np.hypot(gx, gy)


def score(swift: Path, ue: Path) -> dict:
    a_im = rgb(swift)
    b_im = rgb(ue, a_im.size)
    a, b = small(a_im), small(b_im)
    d = np.abs(a - b).mean(axis=2)
    h = d.shape[0]
    cut = max(1, h - round(CONSOLE_PT / 4))  # 1x: points = pixels; the arrays are 1/4 scale
    ga, gb = gradient(a).ravel(), gradient(b).ravel()
    edges = float(np.corrcoef(ga, gb)[0, 1]) if ga.std() > 0 and gb.std() > 0 else 0.0
    r = {"diff": float(d.mean() * 100), "view": float(d[:cut].mean() * 100), "hud": float(d[cut:].mean() * 100),
         "edges": edges, "size": list(a_im.size)}
    r["grade"] = grade(r)
    return r


def grade(r: dict) -> str:
    e, d = r["edges"], r["diff"]
    if e >= 0.80 and d <= 6:
        return "A"
    if e >= 0.65 and d <= 10:
        return "B"
    if e >= 0.45:
        return "C"
    return "D"


def font(size: int):
    for p in ["/System/Library/Fonts/Supplemental/Arial Bold.ttf", "/System/Library/Fonts/Helvetica.ttc"]:
        try:
            return ImageFont.truetype(p, size)
        except OSError:
            pass
    return ImageFont.load_default()


def side_by_side(name: str, swift: Path | None, ue: Path | None, out: Path, r: dict | None, status: str):
    ref = rgb(swift) if swift and swift.exists() else None
    size = ref.size if ref else (1600, 1000)
    other = rgb(ue, size) if ue and ue.exists() else None
    blank = Image.new("RGB", size, (40, 20, 20))
    a, b = ref or blank, other or blank
    w, h = size
    bar = 44
    canvas = Image.new("RGB", (w * 2, h + bar + h // 2), (18, 18, 22))
    canvas.paste(a, (0, bar))
    canvas.paste(b, (w, bar))
    if ref and other:
        d = np.abs(np.asarray(a, np.float32) - np.asarray(b, np.float32)).mean(axis=2)
        d = np.clip(d * 3, 0, 255).astype(np.uint8)
        heat = Image.fromarray(d).resize((w // 2, h // 2), Image.BOX)
        heat = Image.merge("RGB", (heat, heat.point(lambda v: v // 2), Image.new("L", heat.size, 0)))
        canvas.paste(heat, (0, bar + h))
        blend = Image.blend(a, b, 0.5).resize((w // 2, h // 2), Image.BOX)
        canvas.paste(blend, (w // 2, bar + h))
    dr = ImageDraw.Draw(canvas)
    f = font(26)
    dr.text((12, 8), f"{name} · Swift", fill=(235, 235, 235), font=f)
    label = f"Unreal ({status})" if status != "ok" else "Unreal"
    dr.text((w + 12, 8), label, fill=(235, 235, 235), font=f)
    if r:
        dr.text((w + 12, bar + h + 12),
                f"grade {r['grade']}   diff {r['diff']:.1f}%   view {r['view']:.1f}%   hud {r['hud']:.1f}%   edges {r['edges']:.2f}",
                fill=(255, 220, 120), font=f)
        dr.text((12, bar + h + h // 2 - 34), "difference ×3", fill=(200, 200, 200), font=font(20))
        dr.text((w // 2 + 12, bar + h + h // 2 - 34), "50 % blend", fill=(200, 200, 200), font=font(20))
    canvas.save(out)


def contact_sheet(rows: list[dict], dirs: dict, out: Path):
    tiles = [p for p in (dirs["side"] / f"{r['name']}.png" for r in rows) if p.exists()]
    if not tiles:
        return
    tw = 640
    ims = []
    for p in tiles:
        im = Image.open(p).convert("RGB")
        ims.append(im.resize((tw, round(im.size[1] * tw / im.size[0])), Image.BOX))
    th = max(i.size[1] for i in ims)
    cols = 4
    rows_n = (len(ims) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * tw, rows_n * th), (10, 10, 12))
    for k, im in enumerate(ims):
        sheet.paste(im, ((k % cols) * tw, (k // cols) * th))
    sheet.save(out)


def report(rows: list[dict], dirs: dict, out_dir: Path):
    (out_dir / "report.json").write_text(json.dumps(rows, indent=1) + "\n")
    lines = ["| Scene | Group | Swift | Unreal | Grade | Diff % | View % | HUD % | Edges | Owners |",
             "|---|---|---|---|---|---|---|---|---|---|"]
    for r in rows:
        s = r.get("score") or {}
        num = (lambda k, fmt: format(s[k], fmt) if k in s else "-")
        lines.append(f"| {r['name']} | {r['group']} | {r['swift']} | {r['ue']} | {s.get('grade', '-')} | "
                     f"{num('diff', '.1f')} | {num('view', '.1f')} | {num('hud', '.1f')} | {num('edges', '.2f')} | "
                     f"{', '.join(r['owners'])} |")
    (out_dir / "report.md").write_text("\n".join(lines) + "\n")
    print("\n".join(lines))


def make_ok() -> bool:
    print("make (repo root) ...", flush=True)
    p = subprocess.run(["make"], cwd=REPO, capture_output=True, text=True)
    if p.returncode != 0:
        print(p.stdout[-2000:], p.stderr[-2000:], sep="\n")
    return p.returncode == 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("scenes", nargs="*", help="scene names (default: all)")
    ap.add_argument("--group", help="only scenes of this group (top-down, command-map, leveling, pilot, third, playground)")
    ap.add_argument("--out", default=str(UNREAL / "Saved/Shots"))
    ap.add_argument("--jobs", type=int, default=2, help="Unreal runs at once (default 2)")
    ap.add_argument("--skip-swift", action="store_true", help="keep the Swift PNGs already there")
    ap.add_argument("--skip-ue", action="store_true", help="keep the Unreal PNGs already there")
    ap.add_argument("--only-score", action="store_true", help="render nothing; score what is there")
    ap.add_argument("--no-make", action="store_true", help="trust that `make` just succeeded")
    ap.add_argument("--list", action="store_true")
    a = ap.parse_args()

    defaults, scenes = load_table()
    if a.list:
        for s in scenes:
            print(f"{s['name']:24} {s['group']:12} swift: {' '.join(s['swift']) or '-':40} ue: "
                  f"{'NOT STAGED' if s['ue'] is None else ' '.join(s['ue']) or '-'}")
        return
    if a.scenes:
        names = set(a.scenes)
        unknown = names - {s["name"] for s in scenes}
        if unknown:
            sys.exit(f"unknown scenes: {', '.join(sorted(unknown))} (--list)")
        scenes = [s for s in scenes if s["name"] in names]
    if a.group:
        scenes = [s for s in scenes if s["group"] == a.group]

    out_dir = Path(a.out).resolve()
    dirs = {k: out_dir / k for k in ("swift", "ue", "side", "logs")}
    for d in dirs.values():
        d.mkdir(parents=True, exist_ok=True)

    status: dict[str, dict] = {s["name"]: {"swift": "kept", "ue": "kept"} for s in scenes}
    if not a.only_score:
        if not a.skip_swift:
            if not a.no_make and not make_ok():
                sys.exit("make failed: the Swift binary is not used (AGENTS.md). Retry, or --skip-swift.")
            for s in scenes:
                status[s["name"]]["swift"] = render_swift(s, defaults, dirs)
                print(f"swift  {s['name']:24} {status[s['name']]['swift']}", flush=True)
        if not a.skip_ue:
            with cf.ThreadPoolExecutor(max_workers=max(1, a.jobs)) as pool:
                futures = {pool.submit(render_ue, s, defaults, dirs): s for s in scenes}
                for fu in cf.as_completed(futures):
                    s = futures[fu]
                    status[s["name"]]["ue"] = fu.result()
                    print(f"unreal {s['name']:24} {status[s['name']]['ue']}", flush=True)

    rows = []
    for s in scenes:
        n = s["name"]
        sw, ue = dirs["swift"] / f"{n}.png", dirs["ue"] / f"{n}.png"
        st = status[n]
        if st["swift"] == "kept":
            st["swift"] = "ok" if sw.exists() else "missing"
        if st["ue"] == "kept":
            st["ue"] = "not staged" if s.get("ue") is None else ("ok" if ue.exists() else "missing")
        r = score(sw, ue) if st["swift"] == "ok" and st["ue"] == "ok" else None
        side_by_side(n, sw if sw.exists() else None, ue if st["ue"] == "ok" else None, dirs["side"] / f"{n}.png", r, st["ue"])
        rows.append({"name": n, "group": s["group"], "owners": s["owners"], "swift": st["swift"], "ue": st["ue"],
                     "score": r, "notes": s.get("notes", "")})
    report(rows, dirs, out_dir)
    contact_sheet(rows, dirs, out_dir / "sheet.png")
    print(f"\n{out_dir}/report.md, side/*.png, sheet.png")


if __name__ == "__main__":
    main()
