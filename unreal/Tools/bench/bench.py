#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# ///
"""F3 bench: Swift's `autocraft bench` in Unreal (`-AcBench=SHOT`, AcBench.h),
shot by shot, one process a shot, and a table in the shape of Swift's.

    uv run unreal/Tools/bench/bench.py                       # every shot, both sizes
    uv run unreal/Tools/bench/bench.py --size 1920x1080@1 --shot fight,army
    uv run unreal/Tools/bench/bench.py --swift --rounds 2    # Swift's bench too, interleaved
    uv run unreal/Tools/bench/bench.py --png                 # each shot's last frame
    uv run unreal/Tools/bench/bench.py --gpu --shot pilot \
        --variant base= --variant c32=-ForceDPCVars=r.VolumetricCloud.ViewRaySampleMaxCount=32
                                   # `stat gpu` per pass, A/B variants interleaved

Shots (Bench.shots): fight fight-night base wide pilot pilot-night third
army army-night, `late` (fixture `unreal/bench/late.json`: zoomed fully out
on Blue's soldiers) and `late8` (Unreal only: a new 8-player 4v4 game on
badlands-large-8 with B1's `-AcStage=180 -AcStageBuildings`, ~1,500 units,
saved nowhere).

Sizes are WxH@SCALE: SCALE is Swift's points-to-pixels (MSAA 2x at 2, 4x
at 1); Unreal gets `-AcHudScale` and `-AcBenchScale` so the HUD and the view
span the same points and cells. Default: 1920x1080@1 and 5120x1378@2.

Every frame is `tick(1/30)` in both (`-UseFixedTimeStep -FPS=30`); 20
warm-up frames step the game before the 90 timed ones, so the timed moment
is the same game time. Unreal first renders `--settle` frames with the game
paused (its own warm-up: shaders, streaming, Lumen, exposure).

Out (default `unreal/Saved/Bench/<time>/`): `report.md` (the tables, the
load during each run), `results.json`, logs, PNGs with --png.

Rules kept (AGENTS.md, the agent brief): `make` must succeed before the
Swift binary runs; every Unreal run has -nosound; the Swift bench never
plays sound (no audio director headless) and writes no tracking database
(`--db none`); only the processes this script started are killed.
"""
from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path

HERE = Path(__file__).resolve().parent
UNREAL = HERE.parent.parent
REPO = UNREAL.parent
SWIFT_BIN = REPO / ".build/release/Autocraft"
UE_BIN = Path("/Users/Shared/Epic Games/UE_5.8/Engine/Binaries/Mac/UnrealEditorBG.app/Contents/MacOS/UnrealEditor")  # Dock-less copy (LSUIElement); see Tools/README.md
UPROJECT = UNREAL / "Autocraft.uproject"
FIXTURE = "bench/badlands-large.json"
LATE = UNREAL / "bench/late.json"

SHOTS = ["fight", "fight-night", "base", "wide", "pilot", "pilot-night", "third", "army", "army-night", "late", "late8", "late8-base"]
HOURS = {"fight-night": 23, "pilot-night": 23, "army-night": 23}
PARTS = ["sim", "events", "scene", "hud", "audio", "save"]


def load_now() -> float:
    return os.getloadavg()[0]


def busiest(n: int = 4) -> str:
    """The processes using the most CPU now, `name 123%`."""
    try:
        out = subprocess.run(["ps", "-Ao", "pcpu=,comm="], capture_output=True, text=True, timeout=10).stdout
    except Exception:
        return ""
    rows = []
    for line in out.splitlines():
        parts = line.strip().split(None, 1)
        if len(parts) == 2:
            try:
                rows.append((float(parts[0]), Path(parts[1]).name))
            except ValueError:
                pass
    rows.sort(reverse=True)
    return ", ".join(f"{name} {cpu:.0f}%" for cpu, name in rows[:n])


def run(cmd: list[str], log: Path, timeout: float) -> int:
    with open(log, "w") as f:
        p = subprocess.Popen(cmd, stdout=f, stderr=subprocess.STDOUT, cwd=REPO)
        try:
            return p.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            p.kill()  # only the process this script started
            p.wait()
            f.write(f"\n[bench.py] killed after {timeout} s\n")
            return -9


def result_in(text: str) -> dict | None:
    for line in text.splitlines():
        at = line.find("bench-result ")
        if at >= 0:
            try:
                return json.loads(line[at + len("bench-result "):])
            except json.JSONDecodeError:
                pass
    return None


def ue_shot(shot: str, w: int, h: int, scale: int, a: argparse.Namespace, out: Path,
            variant: tuple[str, list[str]] = ("", [])) -> dict | None:
    tag = f"{shot}{'.' + variant[0] if variant[0] else ''}"
    log = out / "logs" / f"ue-{tag}-{w}x{h}.log"
    cmd = [str(UE_BIN), str(UPROJECT), "/Game/Maps/Battlefield", "-game", "-RenderOffscreen", "-windowed", "-ForceRes",
           f"-ResX={w}", f"-ResY={h}", "-unattended", "-nosplash", "-nosound", "-ini:Input:[/Script/Engine.InputSettings]:bCaptureMouseOnLaunch=False", f"-abslog={log}",
           "-UseFixedTimeStep", "-FPS=30", "-AcNoPerfPanel", f"-AcHudScale={scale}", f"-AcBenchScale={scale}",
           f"-AcBench={shot}", f"-AcHour={HOURS.get(shot, 12)}", f"-AcBenchFrames={a.frames}",
           f"-AcBenchWarmup={a.warmup}", f"-AcBenchSettle={a.settle}"]
    if shot.startswith("late8"):
        cmd += ["-AcMap=badlands-large-8", "-AcTeams=4v4", "-AcNew", "-AcNoSave", f"-AcSaveDir={out / 'saves'}",
                f"-AcStage={a.late8}", "-AcStageBuildings"]
    else:
        cmd += [f"-AcSession={LATE if shot == 'late' else FIXTURE}"]
    if shot == "third":
        cmd += ["-AcPilotThird"]
    if a.png:
        cmd += [f"-AcBenchPng={out / 'png' / f'ue-{w}x{h}' / (variant[0] or 'base')}"]
    if a.gpu:
        cmd += ["-AcBenchGpu"]
    cmd += variant[1]
    for attempt in range(3):
        code = run(cmd, out / "logs" / f"ue-{tag}-{w}x{h}.stdout", timeout=600)
        text = log.read_text(errors="replace") if log.exists() else ""
        r = result_in(text)
        if r:
            return r
        stdout = (out / "logs" / f"ue-{tag}-{w}x{h}.stdout").read_text(errors="replace")
        # Another agent relinking or building the module: wait and retry.
        if ("module Autocraft" in text or "module 'Autocraft'" in text or "could not be loaded" in text or code == -9 or not text
                or "UnrealBuildTool_Mutex" in stdout):
            time.sleep(20)
            continue
        print(f"  ue {shot}: failed (exit {code}), see {log}", file=sys.stderr)
        return None
    return None


def swift_shot(shot: str, w: int, h: int, scale: int, a: argparse.Namespace, out: Path) -> dict | None:
    if shot.startswith("late8"):
        return None
    log = out / "logs" / f"swift-{shot}-{w}x{h}.log"
    cmd = [str(SWIFT_BIN), "bench", "--child", shot, "--fixture", str(LATE) if shot == "late" else FIXTURE,
           "--width", str(w), "--height", str(h), "--scale", str(scale), "--frames", str(a.frames),
           "--warmup", str(a.warmup)]
    if a.png:
        cmd += ["--png", str(out / "png" / f"swift-{w}x{h}")]
    code = run(cmd, log, timeout=600)
    r = result_in(log.read_text(errors="replace"))
    if not r:
        print(f"  swift {shot}: failed (exit {code}), see {log}", file=sys.stderr)
    return r


def make_swift() -> bool:
    for attempt in range(3):
        p = subprocess.run(["make"], cwd=REPO, capture_output=True, text=True)
        if p.returncode == 0:
            return True
        print(f"make failed (attempt {attempt + 1}); retrying", file=sys.stderr)
        time.sleep(15)
    print(p.stdout[-2000:] + p.stderr[-2000:], file=sys.stderr)
    return False


# --- tables -----------------------------------------------------------------

def swift_row(name: str, r: dict) -> list[str]:
    return [
        f"{name:<12}{r['gpuBusy']:8.1f} {r['gpuWallP50']:8.1f} {r['gpuWallP95']:4.0f} {r['gpuWallMax']:4.0f}"
        f" {r['tickP95']:8.1f} {r['renderP95']:10.1f} {r['hudP95']:5.1f} {r['footprintMB']:6.0f} {r['graphicsMB']:8.0f}"
        f" {r['draws']:5d} {r['triangles'] / 1e6:8.1f}M",
        "            tick ms (mean/worst) " + " ".join(f"{p['name'] if 'name' in p else PARTS[i]} {p['ms']:.1f}/{p['max']:.0f}"
                                                      for i, p in enumerate(r["parts"]))
        + f" | lights {r['lights']}, {r['litLights']} lit",
    ]


def ue_row(name: str, r: dict) -> list[str]:
    # Metal's RHI leaves its draw and primitive counters at 0: the mesh
    # draw commands (`stat scenerendering`) stand in for draws.
    draws = r["draws"] if r["draws"] > 0 else r.get("meshDraws", -1)
    tri = f"{r['triangles'] / 1e6:8.1f}M" if r["triangles"] > 0 else "        -"
    return [
        f"{name:<12}{r['gpuBusy']:8.1f} {r['gpuWallP50']:8.1f} {r['gpuWallP95']:4.0f} {r['gpuWallMax']:4.0f}"
        f" {r['tickP95']:8.1f} {r['renderP95']:10.1f} {r['rhiP95']:5.1f} {r['gameP95']:6.1f} {r['frameP50']:6.1f} {r['fps']:5.0f}"
        f" {r['footprintMB']:6.0f} {r['graphicsMB']:8.0f} {draws:5d} {tri} {r['units']:5d}",
        "            tick ms (mean/worst) " + " ".join(f"{p['name']} {p['ms']:.1f}/{p['max']:.0f}" for p in r["parts"])
        + f" | lights {r['lights']}, {r['litLights']} lit | {r['objects']} objects, {r['instances']} instances",
    ]


SWIFT_HEAD = ("shot        gpu busy wall p50  p95  max tick p95 render p95 (hud) memory graphics draws triangles")
UE_HEAD = ("shot        gpu busy  gpu p50  p95  max tick p95 render p95   rhi  game  frame   fps memory graphics draws triangles units")


def report(runs: list[dict], machine: str, a: argparse.Namespace) -> str:
    out: list[str] = []
    out.append(f"# Bench, {datetime.now():%Y-%m-%d %H:%M}\n")
    out.append(f"{machine}\n")
    out.append(f"{a.frames} timed frames a shot after {a.warmup} to warm up (Unreal: {a.settle} paused frames before); "
               f"fixture {FIXTURE}; times in ms a frame, memory in MB.\n")
    out.append("Unreal: `gpu busy` = the driver's GPU time for the process over the timed frames, a frame's share "
               "(as Swift's); `gpu p50/p95/max` = Unreal's GPU frame time (`RHIGetGPUFrameCycles`, Swift's `wall`); "
               "`tick` = `UAcSimSubsystem::Tick` and its listeners (Swift's `GameController.tick`); `render` = the render "
               "thread, `rhi` the RHI thread, `game` the whole game thread (engine included); `frame` = wall time "
               "between frames (p50) and `fps` its mean. Draws: Unreal's mesh draw commands (`stat scenerendering` after the timed frames; Metal leaves the RHI's draw and triangle counters at 0); Swift's are its shown geometry elements before culling.\n")
    for (w, h, scale) in sorted({(r["w"], r["h"], r["scale"]) for r in runs}):
        here = [r for r in runs if (r["w"], r["h"]) == (w, h)]
        out.append(f"\n## {w}x{h} at {scale}x\n")
        for engine, head, row in (("swift", SWIFT_HEAD, swift_row), ("ue", UE_HEAD, ue_row)):
            rows = [r for r in here if r["engine"] == engine and r["result"]]
            if not rows:
                continue
            out.append(f"\n### {'Swift (SceneKit)' if engine == 'swift' else 'Unreal 5.8'}\n\n```")
            out.append(head + "  | load")
            for r in rows:
                lines = row(r["shot"] + (f" #{r['round']}" if a.rounds > 1 else ""), r["result"])
                out.append(lines[0] + f"  | {r['load_before']:.1f}->{r['load_after']:.1f}")
                out.append(lines[1])
            out.append("```")
        # Side by side: the headline numbers.
        out.append("\n### Side by side (Swift | Unreal; mean of rounds)\n")
        out.append("| shot | gpu busy S | gpu busy U | gpu p50 S | gpu p50 U | tick p95 S | tick p95 U | render p95 S | render p95 U"
                   " | U game p95 | U frame p50 (fps) | memory S | memory U | load S | load U |")
        out.append("|" + "---|" * 15)

        def mean(engine: str, shot: str, key: str) -> float | None:
            v = [r["result"][key] for r in here if r["engine"] == engine and r["shot"] == shot and r["result"]]
            return sum(v) / len(v) if v else None

        def mload(engine: str, shot: str) -> str:
            v = [(r["load_before"] + r["load_after"]) / 2 for r in here if r["engine"] == engine and r["shot"] == shot]
            return f"{sum(v) / len(v):.1f}" if v else "-"

        def f(v: float | None, d: int = 1) -> str:
            return "-" if v is None else f"{v:.{d}f}"

        for shot in dict.fromkeys(r["shot"] for r in here):
            fp, fps = mean("ue", shot, "frameP50"), mean("ue", shot, "fps")
            out.append(f"| {shot} | {f(mean('swift', shot, 'gpuBusy'))} | {f(mean('ue', shot, 'gpuBusy'))}"
                       f" | {f(mean('swift', shot, 'gpuWallP50'))} | {f(mean('ue', shot, 'gpuWallP50'))}"
                       f" | {f(mean('swift', shot, 'tickP95'))} | {f(mean('ue', shot, 'tickP95'))}"
                       f" | {f(mean('swift', shot, 'renderP95'))} | {f(mean('ue', shot, 'renderP95'))}"
                       f" | {f(mean('ue', shot, 'gameP95'))} | {f(fp)} ({f(fps, 0)})"
                       f" | {f(mean('swift', shot, 'footprintMB'), 0)} | {f(mean('ue', shot, 'footprintMB'), 0)}"
                       f" | {mload('swift', shot)} | {mload('ue', shot)} |")
    passes = [r for r in runs if r["result"] and r["result"].get("gpuPasses")]
    if passes:
        out.append("\n## GPU passes (Unreal `stat gpu`, busy ms a frame, mean of rounds)\n")
        for (w, h) in sorted({(r["w"], r["h"]) for r in passes}):
            here = [r for r in passes if (r["w"], r["h"]) == (w, h)]
            labels = list(dict.fromkeys(r["shot"] for r in here))
            names: dict[str, float] = {}
            for r in here:
                for k, v in r["result"]["gpuPasses"].items():
                    names[k] = max(names.get(k, 0.0), v)
            keep = [k for k, v in sorted(names.items(), key=lambda kv: -kv[1]) if v >= 0.05]
            out.append(f"\n### {w}x{h}\n")
            out.append("| pass | " + " | ".join(labels) + " |")
            out.append("|---|" + "---|" * len(labels))
            for k in keep:
                cells = []
                for lab in labels:
                    v = [r["result"]["gpuPasses"].get(k, 0.0) for r in here if r["shot"] == lab]
                    cells.append(f"{sum(v) / len(v):.2f}" if v else "-")
                out.append(f"| {k} | " + " | ".join(cells) + " |")
    out.append("\n## Load\n")
    for r in runs:
        out.append(f"- {r['engine']} {r['shot']} {r['w']}x{r['h']} #{r['round']}: load {r['load_before']:.1f} -> "
                   f"{r['load_after']:.1f}; busiest before: {r['busiest']}")
    return "\n".join(out) + "\n"


def machine() -> str:
    try:
        chip = subprocess.run(["sysctl", "-n", "machdep.cpu.brand_string"], capture_output=True, text=True).stdout.strip()
        mem = int(subprocess.run(["sysctl", "-n", "hw.memsize"], capture_output=True, text=True).stdout) / 2**30
        cores = os.cpu_count()
        return f"{chip}, {cores} cores, {mem:.0f} GB"
    except Exception:
        return "unknown machine"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--shot", default=",".join(SHOTS), help="comma list (default: all, late8 included)")
    ap.add_argument("--size", action="append", help="WxH@SCALE, repeatable (default 1920x1080@1 and 5120x1378@2)")
    ap.add_argument("--frames", type=int, default=90)
    ap.add_argument("--warmup", type=int, default=20)
    ap.add_argument("--settle", type=int, default=120, help="Unreal: paused frames before the warm-up")
    ap.add_argument("--late8", type=int, default=180, help="late8: units staged for each of the 8 players")
    ap.add_argument("--rounds", type=int, default=1, help="repeat every shot, interleaved")
    ap.add_argument("--swift", action="store_true", help="also Swift's bench (after `make`), interleaved shot by shot")
    ap.add_argument("--no-ue", action="store_true", help="Swift only")
    ap.add_argument("--png", action="store_true", help="save each shot's last frame")
    ap.add_argument("--out", type=Path)
    ap.add_argument("--gpu", action="store_true", help="Unreal: `stat gpu` per pass after the timed frames (-AcBenchGpu)")
    ap.add_argument("--variant", action="append", default=[],
                    help="Unreal: NAME=ARGS, extra command-line args (e.g. '-dpcvars=r.ScreenPercentage=67'); "
                         "repeatable, run interleaved shot by shot; `base=` for none")
    a = ap.parse_args()

    shots = [s for s in a.shot.split(",") if s]
    unknown = [s for s in shots if s not in SHOTS]
    if unknown:
        ap.error(f"no such shot: {', '.join(unknown)}; shots: {', '.join(SHOTS)}")
    sizes = []
    for s in a.size or ["1920x1080@1", "5120x1378@2"]:
        dims, _, scale = s.partition("@")
        w, h = (int(v) for v in dims.lower().split("x"))
        sizes.append((w, h, int(scale or 1)))
    out = a.out or UNREAL / "Saved/Bench" / datetime.now().strftime("%Y%m%d-%H%M%S")
    (out / "logs").mkdir(parents=True, exist_ok=True)
    if a.swift and not make_swift():
        print("make failed: no Swift bench", file=sys.stderr)
        a.swift = False

    m = machine()
    print(f"bench: {m}; out {out}")
    engines = (["swift"] if a.swift else []) + ([] if a.no_ue else ["ue"])
    variants = [(v.partition("=")[0], v.partition("=")[2].split()) for v in a.variant] or [("", [])]
    runs: list[dict] = []
    for rnd in range(1, a.rounds + 1):
        for (w, h, scale) in sizes:
            for shot in shots:
                for engine, variant in [(e, v) for e in engines for v in (variants if e == "ue" else [("", [])])]:
                    if engine == "swift" and shot.startswith("late8"):
                        continue
                    before, who = load_now(), busiest()
                    t0 = time.time()
                    r = (swift_shot(shot, w, h, scale, a, out) if engine == "swift"
                         else ue_shot(shot, w, h, scale, a, out, variant))
                    after = load_now()
                    label = f"{shot}/{variant[0]}" if variant[0] else shot
                    runs.append({"engine": engine, "shot": label, "w": w, "h": h, "scale": scale, "round": rnd,
                                 "load_before": before, "load_after": after, "busiest": who, "result": r,
                                 "seconds": time.time() - t0})
                    tag = "ok" if r else "FAILED"
                    extra = (f"gpu busy {r['gpuBusy']:.1f}, tick p95 {r['tickP95']:.1f}, render p95 {r['renderP95']:.1f}"
                             if r else "")
                    print(f"  {engine:5} {label:12} {w}x{h} #{rnd}: {tag} {extra} (load {before:.1f}->{after:.1f}, "
                          f"{time.time() - t0:.0f} s)", flush=True)
                    (out / "results.json").write_text(json.dumps({"machine": m, "runs": runs}, indent=1))
    text = report(runs, m, a)
    (out / "report.md").write_text(text)
    print()
    print(text)
    return 0 if all(r["result"] for r in runs) else 1


if __name__ == "__main__":
    sys.exit(main())
