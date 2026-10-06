# /// script
# requires-python = ">=3.9"
# dependencies = []
# ///
"""Import the Swift game's sounds into Unreal (GAME-LAYER.md §2.10, chunk C7).

Two stages, both rerunnable:

1. prepare (plain Python, no Unreal): every WAV under
   unreal/Resources/Sounds/{sfx,ads} becomes a mono 16-bit WAV in
   unreal/Content-src/sounds/{sfx,ads}/, downmixed and peak-normalised to 0.8
   as the Swift game does at load (AudioDirector.loadMono + Synth.normalize).
   The sample rate is kept (24/44.1/48 kHz; Unreal resamples). A format the
   `wave` module cannot read is converted with afconvert first. Files whose
   source has not changed are skipped (stamps in .prepared.json).
2. import (inside the editor): each prepared file becomes a SoundWave at
   /Game/Audio/Sfx/<name>/SW_<name>_<n> (variants grouped per folder) or
   /Game/Audio/Ads/SW_<id>, and the table Sounds.json is written to
   unreal/Content-src/sounds/ and unreal/Content/Audio/ (read at runtime).

Run:
    uv run unreal/Tools/Editor/import_sounds.py          # prepare + table only
    in the editor:  py "<repo>/unreal/Tools/Editor/import_sounds.py"
                    (or the MCP editor's Python tool)      # prepare + import + table

A sound's variants are the files named <name>_<n>.wav. Swift matched by
prefix (`hasPrefix(name)`), so its `flak` bank also picked up the flakhit
files; the table groups exactly, which is what the names mean.
"""

import array
import hashlib
import json
import os
import re
import subprocess
import sys
import tempfile
import wave

try:
    import unreal  # only inside the editor
except ImportError:
    unreal = None

HERE = os.path.dirname(os.path.abspath(__file__))
UNREAL_DIR = os.path.normpath(os.path.join(HERE, "..", ".."))
REPO = os.path.dirname(UNREAL_DIR)
# The Swift game's sound tables (Audio.swift, SoundBoard.swift), copied here
# when the Swift game left the repo.
SWIFT = os.path.join(HERE, "swift")
SOUNDS = os.path.join(UNREAL_DIR, "Resources", "Sounds")
OUT = os.path.join(UNREAL_DIR, "Content-src", "sounds")
RUNTIME_TABLE = os.path.join(UNREAL_DIR, "Content", "Audio", "Sounds.json")
STAMPS = os.path.join(OUT, ".prepared.json")

PEAK = 0.8  # Synth.normalize(peak: 0.8)
SCRIPT = "Tools/Editor/import_sounds.py"
VARIANT = re.compile(r"^([a-z]+)_(\d+)\.wav$")


def log(msg):
    if unreal:
        unreal.log("import_sounds: " + msg)
    else:
        print(msg)


# ------------------------------------------------------------------ prepare

def read_pcm16(path):
    """(rate, channels, int16 array) or None if `wave` cannot read it."""
    try:
        with wave.open(path, "rb") as w:
            if w.getsampwidth() != 2 or w.getcomptype() != "NONE":
                return None
            rate, ch, n = w.getframerate(), w.getnchannels(), w.getnframes()
            data = array.array("h")
            data.frombytes(w.readframes(n))
    except (wave.Error, EOFError):
        return None
    if sys.byteorder == "big":
        data.byteswap()
    return rate, ch, data


def read_any(path):
    got = read_pcm16(path)
    if got:
        return got
    # Anything else (24-bit, float, AIFF, CAF...): let CoreAudio convert it.
    with tempfile.TemporaryDirectory() as tmp:
        conv = os.path.join(tmp, "x.wav")
        subprocess.run(["afconvert", "-f", "WAVE", "-d", "LEI16", path, conv], check=True)
        got = read_pcm16(conv)
    if not got:
        raise RuntimeError("cannot read " + path)
    return got


def normalised_mono(rate, ch, data):
    """Mono (channel average), peak at PEAK, as int16."""
    if ch == 1:
        mono = data
    else:
        mono = array.array("f", (sum(data[i:i + ch]) / ch for i in range(0, len(data), ch)))
    peak = max((abs(s) for s in mono), default=0)
    if peak == 0:
        return array.array("h", (int(s) for s in mono))
    g = PEAK * 32767.0 / peak
    return array.array("h", (int(round(s * g)) for s in mono))


def write_wav(path, rate, samples):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    out = samples
    if sys.byteorder == "big":
        out = array.array("h", samples)
        out.byteswap()
    tmp = path + ".tmp"
    with wave.open(tmp, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(out.tobytes())
    os.replace(tmp, path)


def file_hash(path):
    h = hashlib.sha1()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def prepare():
    """Normalise every source WAV that changed. Returns {group: [entries]}."""
    stamps = {}
    if os.path.exists(STAMPS):
        with open(STAMPS) as f:
            stamps = json.load(f)
    done, made = {}, 0
    for group in ("sfx", "ads"):
        src_dir = os.path.join(SOUNDS, group)
        entries = []
        for name in sorted(os.listdir(src_dir)):
            if not name.lower().endswith(".wav"):
                continue
            src = os.path.join(src_dir, name)
            dst = os.path.join(OUT, group, name)
            key = group + "/" + name
            digest = file_hash(src)
            stamp = stamps.get(key)
            if not (stamp and stamp["source"] == digest and os.path.exists(dst)):
                rate, ch, data = read_any(src)
                mono = normalised_mono(rate, ch, data)
                write_wav(dst, rate, mono)
                stamp = {"source": digest, "rate": rate, "sourceChannels": ch,
                         "seconds": round(len(mono) / rate, 3)}
                stamps[key] = stamp
                made += 1
            entries.append(dict(stamp, file=name, prepared=os.path.relpath(dst, UNREAL_DIR)))
        done[group] = entries
    # Forget files that left the source folders.
    live = {g + "/" + e["file"] for g in done for e in done[g]}
    stamps = {k: v for k, v in stamps.items() if k in live}
    os.makedirs(OUT, exist_ok=True)
    with open(STAMPS, "w") as f:
        json.dump(stamps, f, indent=1, sort_keys=True)
    log("prepared %d of %d files into %s" % (made, len(live), OUT))
    return done


# ------------------------------------------------------------------ Swift uses

def swift_gains():
    """AudioDirector's `static let fooGain: Float = 0.25` constants."""
    text = open(os.path.join(SWIFT, "Audio.swift")).read()
    return {m.group(1): float(m.group(2))
            for m in re.finditer(r"static let (\w+Gain): Float = ([\d.]+)", text)}


def swift_uses(names):
    """Per sound name: the SoundBoard entries (id, title, gain, loop, driven)
    and the Audio.swift lines that name it."""
    gains = swift_gains()
    board = open(os.path.join(SWIFT, "SoundBoard.swift")).read()
    uses = {n: {"board": [], "audioSwiftLines": []} for n in names}
    seen = set()
    pat = re.compile(r'Sound\(id: "(\w+)", name: "(\w+)", title: "([^"]*)", gain: ([\w.]+)([^)]*)\)')
    for m in pat.finditer(board):
        sid, name, title, gain, rest = m.groups()
        g = gains.get(gain.split(".")[-1]) if not re.match(r"^[\d.]+$", gain) else float(gain)
        entry = {"id": sid, "title": title, "gain": g, "gainSwift": gain,
                 "loop": "loop: true" in rest, "driven": "driven: true" in rest}
        k = (name, sid, title, gain)
        if name in uses and k not in seen:
            seen.add(k)
            uses[name]["board"].append(entry)
    audio = open(os.path.join(SWIFT, "Audio.swift")).read().splitlines()
    for i, line in enumerate(audio, 1):
        for n in re.findall(r'"([a-z]+)"', line):
            if n in uses and i not in uses[n]["audioSwiftLines"]:
                uses[n]["audioSwiftLines"].append(i)
    return uses


# ------------------------------------------------------------------ table

def sfx_asset(name, n):
    return "/Game/Audio/Sfx/%s/SW_%s_%s" % (name, name, n)


def ad_asset(ad_id):
    return "/Game/Audio/Ads/SW_%s" % ad_id


def build_table(prepared):
    sounds = {}
    for e in prepared["sfx"]:
        m = VARIANT.match(e["file"])
        if not m:
            log("skipping %s: not <name>_<n>.wav" % e["file"])
            continue
        name, n = m.groups()
        path = sfx_asset(name, n)
        s = sounds.setdefault(name, {"variants": []})
        s["variants"].append({"asset": path + "." + path.rsplit("/", 1)[1], "source": "sfx/" + e["file"],
                              "prepared": e["prepared"], "seconds": e["seconds"], "rate": e["rate"]})
    for s in sounds.values():
        s["variants"].sort(key=lambda v: int(re.search(r"_(\d+)\.", v["source"]).group(1)))
    uses = swift_uses(sounds.keys())
    for name, s in sounds.items():
        s.update(uses[name])
    titles = json.load(open(os.path.join(SOUNDS, "ads", "titles.json")))
    ads = {}
    for e in prepared["ads"]:
        ad_id = e["file"][:-4]
        path = ad_asset(ad_id)
        ads[ad_id] = {"asset": path + "." + path.rsplit("/", 1)[1], "title": titles.get(ad_id, ad_id),
                      "source": "ads/" + e["file"], "prepared": e["prepared"], "seconds": e["seconds"]}
    unused = sorted(n for n, s in sounds.items() if not s["board"] and not s["audioSwiftLines"])
    return {
        "madeBy": SCRIPT,
        "notes": "Variants of a sound are picked at random. Files are mono, peak-normalised to %.1f; "
                 "the gains are AudioDirector's (Audio.swift) and SoundBoard.swift's. Swift matched "
                 "variants by prefix, so its `flak` also played the flakhit files; this table does not." % PEAK,
        "peak": PEAK,
        "sounds": dict(sorted(sounds.items())),
        "ads": dict(sorted(ads.items())),
        "unusedInSwift": unused,
    }


def write_table(table):
    for path in (os.path.join(OUT, "Sounds.json"), RUNTIME_TABLE):
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w") as f:
            json.dump(table, f, indent=1)
            f.write("\n")
    log("table: %d sounds, %d variants, %d ads -> %s" % (
        len(table["sounds"]), sum(len(s["variants"]) for s in table["sounds"].values()),
        len(table["ads"]), RUNTIME_TABLE))


# ------------------------------------------------------------------ import

def import_all(table):
    eal = unreal.EditorAssetLibrary
    jobs = []  # (prepared file, folder, asset name, is_ad)
    for name, s in table["sounds"].items():
        for v in s["variants"]:
            pkg = v["asset"].split(".")[0]
            jobs.append((v["prepared"], pkg.rsplit("/", 1)[0], pkg.rsplit("/", 1)[1], v["source"]))
    for ad_id, a in table["ads"].items():
        pkg = a["asset"].split(".")[0]
        jobs.append((a["prepared"], pkg.rsplit("/", 1)[0], pkg.rsplit("/", 1)[1], a["source"]))

    tasks, todo = [], []
    for prepared, folder, asset, source in jobs:
        full = os.path.join(UNREAL_DIR, prepared)
        digest = file_hash(full)
        path = folder + "/" + asset
        if eal.does_asset_exist(path):
            old = eal.load_asset(path)
            if old and eal.get_metadata_tag(old, "AcSourceHash") == digest:
                continue
        t = unreal.AssetImportTask()
        t.filename = full
        t.destination_path = folder
        t.destination_name = asset
        t.replace_existing = True
        t.automated = True
        t.save = False
        tasks.append(t)
        todo.append((path, source, digest))
    if tasks:
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
    failed = []
    for path, source, digest in todo:
        a = eal.load_asset(path)
        if not a:
            failed.append(path)
            continue
        eal.set_metadata_tag(a, "MadeBy", SCRIPT)
        eal.set_metadata_tag(a, "AcSource", "unreal/Resources/Sounds/" + source)
        eal.set_metadata_tag(a, "AcSourceHash", digest)
        eal.save_asset(path, only_if_is_dirty=False)
    log("imported %d of %d sound waves (%d unchanged)%s" % (
        len(todo) - len(failed), len(jobs), len(jobs) - len(todo),
        "; FAILED: " + ", ".join(failed) if failed else ""))
    return failed


def main():
    prepared = prepare()
    table = build_table(prepared)
    write_table(table)
    if unreal:
        failed = import_all(table)
        if failed:
            raise RuntimeError("import_sounds: %d imports failed" % len(failed))
    else:
        log("not in the editor: run it there to import the sound waves")


main()
