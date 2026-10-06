# /// script
# requires-python = ">=3.11"
# dependencies = ["numpy"]
# ///
"""Cut a sound short: its first <seconds>, the last <fade> ms faded out.

    uv run Tools/AudioGen/trim.py <in.wav> <out.wav> <seconds> [fade_ms=60]

For sounds the game plays in quick pairs (a comet's jump takes off and
lands within about half a second), so the first is gone before the second
starts. Channels, rate and sample width stay as they were. Deterministic.
"""
import sys
import wave

import numpy as np


def main():
    src, dst, seconds = sys.argv[1], sys.argv[2], float(sys.argv[3])
    fade_ms = float(sys.argv[4]) if len(sys.argv) > 4 else 60
    with wave.open(src) as w:
        rate, ch, width = w.getframerate(), w.getnchannels(), w.getsampwidth()
        x = np.frombuffer(w.readframes(w.getnframes()), np.int16).reshape(-1, ch).astype(np.float64)
    x = x[: int(rate * seconds)]
    n = min(len(x), int(rate * fade_ms / 1000))
    # A cosine fade, so the cut makes no click.
    x[len(x) - n:] *= (0.5 + 0.5 * np.cos(np.linspace(0, np.pi, n)))[:, None]
    with wave.open(dst, "wb") as w:
        w.setnchannels(ch)
        w.setsampwidth(width)
        w.setframerate(rate)
        w.writeframes(np.round(x).astype(np.int16).tobytes())


if __name__ == "__main__":
    main()
