#!/usr/bin/env bash
# Autocraft's own sounds from ElevenLabs, made once and shipped in the repo
# (unreal/Resources/Sounds). The game never calls ElevenLabs.
#
#   elevenlabs.sh voices                              voice_id | name | labels
#   elevenlabs.sh say <voice_id> <out.wav> "<text>" [model]   default eleven_v4
#   elevenlabs.sh sfx <out.wav> "<prompt>" [seconds 0.5-30] [loop true|false] [influence 0-1]
#   elevenlabs.sh music <out.wav|.mp3> "<prompt>" [length_ms 3000-600000] [model]  default music_v2_5, instrumental
#   elevenlabs.sh manifest <file.json> <out_dir>      every entry whose <id>.wav is missing
#
# A manifest entry with "fx": "comm" is made into raw/<id>.wav beside the
# manifest (once; that is the paid part), then put through comm.py (the
# helmet comm) into <out_dir> on every run, so the effect can be retuned
# for free. Words after the script name are its arguments: "fx": "trim 0.28"
# runs trim.py raw final 0.28.
#
# Output is 16-bit WAV, made here from raw PCM (the API's own WAV is
# Pro tier only, and mp3's encoder padding puts a gap in a loop): 44.1 kHz,
# or 24 kHz where the plan refuses 44.1 kHz PCM. An .mp3 path gets the API's
# own mp3 instead (44.1 kHz, 192 kbps, else 128): for long music beds.
# The key is $ELEVENLABS_API_KEY, else ELEVENLABS_API_KEY= in a .env here or
# in a parent directory. It is never printed.
set -euo pipefail

API=https://api.elevenlabs.io

key() {
    [[ -n "${ELEVENLABS_API_KEY:-}" ]] && return
    local d=$PWD
    while [[ "$d" != / ]]; do
        if [[ -f "$d/.env" ]] && grep -q '^ELEVENLABS_API_KEY=' "$d/.env"; then
            ELEVENLABS_API_KEY=$(grep '^ELEVENLABS_API_KEY=' "$d/.env" | head -1 | cut -d= -f2- | tr -d '"'"'"' ')
            export ELEVENLABS_API_KEY
            return
        fi
        d=$(dirname "$d")
    done
    echo "no ELEVENLABS_API_KEY in the environment or a .env" >&2
    exit 1
}

# post <path> <json body> <out.wav|.mp3> <channels>: the audio as WAV or mp3, or the error
# (key masked). Speech comes back mono; sound effects and music stereo.
post() {
    local tmp code fmt formats sep
    tmp=$(mktemp)
    [[ "$1" == *\?* ]] && sep='&' || sep='?'
    [[ "$3" == *.mp3 ]] && formats="mp3_44100_192 mp3_44100_128" || formats="pcm_44100 pcm_24000"
    for fmt in $formats; do
        code=$(curl -sS -o "$tmp" -w '%{http_code}' -X POST "$API$1${sep}output_format=$fmt" \
            -H "xi-api-key: $ELEVENLABS_API_KEY" -H 'Content-Type: application/json' -d "$2")
        [[ "$code" == 403 ]] && grep -q output_format_not_allowed "$tmp" && continue
        break
    done
    if [[ "$code" != 200 ]]; then
        echo "HTTP $code: $(sed "s/$ELEVENLABS_API_KEY/***/g" "$tmp" | head -c 600)" >&2
        rm -f "$tmp"
        return 1
    fi
    mkdir -p "$(dirname "$3")"
    if [[ "$fmt" == mp3_* ]]; then
        cat "$tmp" >"$3"
    else
        python3 -c '
import sys, wave
raw = open(sys.argv[1], "rb").read()
with wave.open(sys.argv[2], "wb") as w:
    w.setnchannels(int(sys.argv[4])); w.setsampwidth(2); w.setframerate(int(sys.argv[3])); w.writeframes(raw)' "$tmp" "$3" "${fmt#pcm_}" "$4"
    fi
    rm -f "$tmp"
    echo "$3 ($fmt, $(wc -c <"$3" | tr -d ' ') bytes)"
}

say() {
    local model=${4:-eleven_v4}
    post "/v1/text-to-speech/$1" \
        "$(python3 -c 'import json,sys; print(json.dumps({"text": sys.argv[1], "model_id": sys.argv[2]}))' "$3" "$model")" "$2" 1
}

sfx() {
    local body
    body=$(python3 -c '
import json, sys
text, dur, loop, infl = sys.argv[1:5]
b = {"text": text, "model_id": "eleven_text_to_sound_v2", "loop": loop == "true", "prompt_influence": float(infl)}
if dur: b["duration_seconds"] = float(dur)
print(json.dumps(b))' "$2" "${3:-}" "${4:-false}" "${5:-0.3}")
    post "/v1/sound-generation" "$body" "$1" 2
}

music() {
    local body
    body=$(python3 -c '
import json, sys
prompt, ms, model = sys.argv[1:4]
b = {"prompt": prompt, "model_id": model, "force_instrumental": True}
if ms: b["music_length_ms"] = int(ms)
print(json.dumps(b))' "$2" "${3:-}" "${4:-music_v2_5}")
    post "/v1/music" "$body" "$1" 2
}

voices() {
    local token=""
    while :; do
        local page
        page=$(curl -sS "$API/v2/voices?page_size=100${token:+&next_page_token=$token}" -H "xi-api-key: $ELEVENLABS_API_KEY")
        python3 -c '
import json, sys
d = json.loads(sys.argv[1])
for v in d.get("voices", []):
    l = v.get("labels") or {}
    print(v["voice_id"], v["name"], ", ".join(f"{k}={x}" for k, x in l.items()), sep="\t")' "$page"
        token=$(python3 -c 'import json,sys; d=json.loads(sys.argv[1]); print(d.get("next_page_token") or "" if d.get("has_more") else "")' "$page")
        [[ -z "$token" ]] && break
    done
}

# Manifest: {"voices": {"<role>": "<voice_id>"}, "entries": [
#   {"id": "vprospector_1", "kind": "say", "voice": "<role>", "text": "...", "model": "eleven_v4", "fx": "comm"},
#   {"id": "drill_1", "kind": "sfx", "prompt": "...", "seconds": 2, "loop": true, "influence": 0.5},
#   {"id": "theme_1", "kind": "music", "prompt": "...", "ms": 60000}]}
manifest() {
    local file=$1 out=$2
    python3 -c '
import json, sys
m = json.load(open(sys.argv[1]))
for e in m["entries"]:
    k = e["kind"]
    if k == "say":
        a = [m["voices"][e["voice"]], e["text"], e.get("model", "eleven_v4")]
    elif k == "sfx":
        a = [e["prompt"], str(e.get("seconds", "")), "true" if e.get("loop") else "false", str(e.get("influence", 0.3))]
    else:
        a = [e["prompt"], str(e.get("ms", "")), e.get("model", "music_v2_5")]
    print("\x1f".join([k, e["id"], e.get("fx", "-")] + a))' "$file" |
    while IFS=$'\x1f' read -r kind id fx a b c d; do
        local dest="$out/$id.wav" final=""
        if [[ "$fx" != - ]]; then
            final=$dest
            dest="$(dirname "$file")/raw/$id.wav"
        fi
        if [[ -s "$dest" ]]; then
            echo "skip $id (exists)"
        else
            case $kind in
                say) say "$a" "$dest" "$b" "$c" ;;
                sfx) sfx "$dest" "$a" "$b" "$c" "$d" ;;
                music) music "$dest" "$a" "$b" "$c" ;;
            esac || { echo "FAILED $id" >&2; continue; }
        fi
        if [[ -n "$final" ]]; then
            mkdir -p "$(dirname "$final")"
            read -r -a fxargs <<< "$fx"
            uv run --quiet "$(dirname "$0")/${fxargs[0]}.py" "$dest" "$final" "${fxargs[@]:1}" </dev/null && echo "$final ($fx)"
        fi
    done
}

cmd=${1:-}
[[ -z "$cmd" ]] && { sed -n '2,12p' "$0"; exit 0; }
shift
key
case $cmd in
    voices) voices ;;
    say) say "$@" ;;
    sfx) sfx "$@" ;;
    music) music "$@" ;;
    manifest) manifest "$@" ;;
    *) sed -n '2,12p' "$0"; exit 1 ;;
esac
