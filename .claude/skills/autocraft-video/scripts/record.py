#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = []
# ///
"""Record micro simulations of `sims.json` as MP4 clips.

    uv run .claude/skills/autocraft-video/scripts/record.py ranger-fire
    uv run .claude/skills/autocraft-video/scripts/record.py ranger-fire comet-jump --out video/projects/NAME/sources
    uv run .claude/skills/autocraft-video/scripts/record.py --list

One hidden Unreal run per sim (UnrealEditorBG.app, -RenderOffscreen, no
-log): the game stages the sim from its flags, steps at a fixed 1/FPS,
streams every frame into a pipe as raw BGRA (`-AcShotRecord -AcShotRaw`,
AcShot.h: read back from the GPU without stalling the render), and quits;
FFmpeg encodes from the pipe while the game runs. `--png` saves every frame
as a PNG instead (the old, several times slower way). With sound (the
default) the same run renders the game's mix offline to a WAV
(-deterministicaudio, -AcAudioRecord: the non-realtime mixer, nothing reaches
the speakers; AcAudioDirector.h); without, the run has -nosound. FFmpeg then
encodes the frames to H.264 and lines the WAV up with them from the two
start times the log records. Out, per sim: NAME.mp4, NAME-contact.png (four
frames), NAME.log (the Unreal log). The frames are deleted once the MP4 is
checked.

Needs the game module built with -AcShotRecord, and FFmpeg: the video
workspace's (`scripts/setup.sh`), else one from nixpkgs.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

SKILL = Path(__file__).resolve().parent.parent
REPO = SKILL.parent.parent.parent
TABLE = SKILL / "sims.json"
UE_BIN = Path("/Users/Shared/Epic Games/UE_5.8/Engine/Binaries/Mac/UnrealEditorBG.app/Contents/MacOS/UnrealEditor")  # Dock-less copy; unreal/Tools/README.md
UPROJECT = REPO / "unreal/Autocraft.uproject"
WORKSPACE_FFMPEG = REPO / "video/.tools/ffmpeg-bin/bin/ffmpeg"


def ffmpeg() -> str:
    if WORKSPACE_FFMPEG.exists():
        return str(WORKSPACE_FFMPEG)
    if found := shutil.which("ffmpeg"):
        return found
    out = subprocess.run(
        ["nix", "--extra-experimental-features", "nix-command flakes", "build", "nixpkgs#ffmpeg.bin", "--no-link", "--print-out-paths"],
        check=True, capture_output=True, text=True,
    ).stdout.strip().splitlines()[-1]
    return f"{out}/bin/ffmpeg"


def load() -> tuple[dict, dict[str, dict]]:
    doc = json.loads(TABLE.read_text())
    return doc["defaults"], {s["name"]: s for s in doc["sims"]}


def command(sim: dict, d: dict, frames: Path, log: Path) -> list[str]:
    w, h, fps = sim.get("width", d["width"]), sim.get("height", d["height"]), sim.get("fps", d["fps"])
    cmd = [
        str(UE_BIN), str(UPROJECT), "/Game/Maps/Battlefield", "-game",
        "-RenderOffscreen", "-windowed", "-ForceRes", f"-ResX={w}", f"-ResY={h}",
        "-unattended", "-nosplash",
        "-ini:Input:[/Script/Engine.InputSettings]:bCaptureMouseOnLaunch=False",
        f"-abslog={log}", "-UseFixedTimeStep", f"-FPS={fps}",
        f"-AcShot={frames / 'f.png'}", f"-AcShotRecord={sim.get('seconds', d['seconds'])}",
        f"-AcShotFrames={sim.get('warm', d['warm'])}",
    ]
    if sim.get("sound", d.get("sound", True)):
        # Offline only: the non-realtime mixer renders to the WAV, never to the speakers.
        # The shot starts the sound (-AcAudioRecordAt far away) and its frames with it.
        cmd += ["-deterministicaudio", f"-AcAudioRecord={frames / 'sound.wav'}", "-AcAudioRecordAt=100000",
                f"-AcAudioRecordSeconds={sim.get('seconds', d['seconds']) + 0.5}"]
    else:
        cmd.append("-nosound")
    if not sim.get("music", d.get("music", True)):
        cmd.append("-AcNoMusic")
    if path := sim.get("camera"):
        cmd.append(f"-AcCamPath={path}")
    # Typed flags come first: they win where a chunk reads the first match.
    cmd += d.get("flags", []) + sim.get("flags", [])
    if scene := sim.get("scene"):
        cmd.append(f"-AcScene={scene}")
    return cmd


def frame_count(ff: str, video: Path) -> int:
    probe = str(Path(ff).with_name("ffprobe"))
    if not Path(probe).exists():
        probe = shutil.which("ffprobe") or "ffprobe"
    out = subprocess.run([probe, "-v", "error", "-select_streams", "v:0", "-count_packets", "-show_entries",
                          "stream=nb_read_packets", "-of", "csv=p=0", str(video)], capture_output=True, text=True)
    return int(out.stdout.strip() or 0)


def record(name: str, sim: dict, d: dict, out: Path, png: bool = False) -> Path:
    out.mkdir(parents=True, exist_ok=True)
    frames = REPO / "video/.frames" / name
    if frames.exists():
        shutil.rmtree(frames)
    frames.mkdir(parents=True)
    log = out / f"{name}.log"
    fps = sim.get("fps", d["fps"])
    t0 = time.time()
    ff = ffmpeg()
    w, h = sim.get("width", d["width"]), sim.get("height", d["height"])
    expected = round(sim.get("seconds", d["seconds"]) * fps)
    cmd = command(sim, d, frames, log)
    encoder = None
    if not png:
        # The game writes raw frames into a pipe; FFmpeg encodes them as they come.
        pipe = frames / "f.raw"
        os.mkfifo(pipe)
        encoder = subprocess.Popen([ff, "-y", "-loglevel", "error", "-f", "rawvideo", "-pix_fmt", "bgra", "-s", f"{w}x{h}",
                                    "-framerate", str(fps), "-i", str(pipe), "-c:v", "libx264", "-pix_fmt", "yuv420p",
                                    "-crf", "16", str(frames / "video.mp4")])
        cmd.append(f"-AcShotRaw={pipe}")
    run = subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=1800)
    if encoder:
        try:
            encoder.wait(timeout=120 if run.returncode == 0 else 5)
        except subprocess.TimeoutExpired:
            # The game never opened the pipe (it failed before recording).
            encoder.kill()
            encoder.wait()
        count = frame_count(ff, frames / "video.mp4") if (frames / "video.mp4").exists() else 0
        video = ["-i", str(frames / "video.mp4")]
    else:
        count = len(sorted(frames.glob("f_*.png")))
        video = ["-framerate", str(fps), "-i", str(frames / "f_%05d.png")]
    if run.returncode != 0 or count < expected:
        sys.exit(f"record: {name}: exit {run.returncode}, {count} of {expected} frames; read {log}")
    mp4 = out / f"{name}.mp4"
    sound, note = [], "silent"
    wav = frames / "sound.wav"
    if wav.exists() and wav.stat().st_size > 44:
        text = log.read_text(errors="replace")
        v = re.search(r"shot: recording starts at world ([\d.]+) s", text)
        a = re.search(r"audio: recording .* \(world ([\d.]+) s\)", text)
        peak = re.search(r"audio: recorded .*peak ([\d.]+), rms ([-\d.]+) dBFS", text)
        offset = max(0.0, float(v.group(1)) - float(a.group(1))) if v and a else 0.0
        sound = ["-ss", f"{offset:.4f}", "-i", str(wav), "-map", "0:v", "-map", "1:a", "-c:a", "aac", "-b:a", "192k",
                 "-t", f"{count / fps:.4f}"]
        note = f"sound offset {offset:.3f} s" + (f", peak {peak.group(1)}, rms {peak.group(2)} dBFS" if peak else "")
    elif sim.get("sound", d.get("sound", True)):
        print(f"record: {name}: warning: no sound was written; the clip is silent (read {log})")
    vcodec = ["-c:v", "copy"] if encoder else ["-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "16"]
    if encoder and not sound:
        sound = ["-map", "0:v"]
    subprocess.run([ff, "-y", "-loglevel", "error", *video, *sound,
                    *vcodec, "-movflags", "+faststart", str(mp4)], check=True)
    step = max(1, count // 4)
    subprocess.run([ff, "-y", "-loglevel", "error", "-i", str(mp4), "-vf",
                    f"select='not(mod(n\\,{step}))',scale=800:-1,tile=2x2", "-frames:v", "1", str(out / f"{name}-contact.png")], check=True)
    if mp4.stat().st_size > 0:
        shutil.rmtree(frames)
    print(f"record: {name}: {mp4} ({count} frames, {count / fps:.1f} s, {note}, {mp4.stat().st_size / 1e6:.1f} MB, {time.time() - t0:.0f} s)")
    return mp4


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("sims", nargs="*")
    ap.add_argument("--out", type=Path, default=REPO / "video/clips")
    ap.add_argument("--seconds", type=float, help="override every sim's length")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--png", action="store_true", help="save PNG frames and encode after the run (the old, slower way)")
    a = ap.parse_args()
    d, sims = load()
    if a.list or not a.sims:
        for s in sims.values():
            print(f"{s['name']:24} {s.get('seconds', d['seconds']):>4} s  {s.get('notes', '')}")
        return
    for name in a.sims:
        if name not in sims:
            sys.exit(f"record: no sim {name!r} in {TABLE} (--list)")
        sim = dict(sims[name])
        if a.seconds:
            sim["seconds"] = a.seconds
        record(name, sim, d, a.out.resolve(), a.png)


if __name__ == "__main__":
    main()
