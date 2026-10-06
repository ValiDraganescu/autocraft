# /// script
# requires-python = ">=3.11"
# dependencies = ["requests"]
# ///
"""Voice Design: three previews of a described voice, and saving one.

    uv run Tools/AudioGen/design_voice.py design <name> "<description>" "<sample text>" <out_dir>
        writes <out_dir>/<name>_1.mp3 .. _3.mp3 and prints each generated_voice_id
    uv run Tools/AudioGen/design_voice.py save <voice_name> "<description>" <generated_voice_id>
        saves the chosen preview as a voice (needs a free custom voice slot); prints its voice_id

The key is ELEVENLABS_API_KEY from the environment or the nearest .env; never printed.
"""
import base64
import os
import sys

import requests

API = "https://api.elevenlabs.io"


def key():
    k = os.environ.get("ELEVENLABS_API_KEY")
    d = os.getcwd()
    while not k and d != "/":
        p = os.path.join(d, ".env")
        if os.path.exists(p):
            for line in open(p):
                if line.startswith("ELEVENLABS_API_KEY="):
                    k = line.split("=", 1)[1].strip().strip("\"' ")
        d = os.path.dirname(d)
    if not k:
        sys.exit("no ELEVENLABS_API_KEY")
    return k


def post(path, body, k):
    r = requests.post(API + path, json=body, headers={"xi-api-key": k}, timeout=180)
    if r.status_code != 200:
        sys.exit(f"HTTP {r.status_code}: {r.text.replace(k, '***')[:600]}")
    return r.json()


def main():
    k = key()
    if sys.argv[1] == "design":
        name, desc, text, out = sys.argv[2:6]
        d = post("/v1/text-to-voice/design", {"voice_description": desc, "model_id": "eleven_ttv_v3", "text": text, "guidance_scale": 5}, k)
        os.makedirs(out, exist_ok=True)
        for i, p in enumerate(d["previews"], 1):
            path = os.path.join(out, f"{name}_{i}.mp3")
            open(path, "wb").write(base64.b64decode(p["audio_base_64"]))
            print(path, p["generated_voice_id"])
    elif sys.argv[1] == "save":
        name, desc, gid = sys.argv[2:5]
        d = post("/v1/text-to-voice", {"voice_name": name, "voice_description": desc, "generated_voice_id": gid}, k)
        print(d["voice_id"])


main()
