# /// script
# requires-python = ">=3.11"
# dependencies = ["numpy"]
# ///
"""Make a take into a seamless loop: drop its fade in and out, then
cross-fade its tail into its head.

    uv run Tools/AudioGen/loop.py <in.wav> <out.wav> [cut_s=0.35] [xfade_s=0.3]

`cut_s` comes off each end (where a sound-effect take swells in and dies
away); the last `xfade_s` of what is left is laid over its first `xfade_s`
with an equal-power cross-fade (for noise-like sounds: a roar, a hum), so
the end runs into the start with no click and no dip. Out: mono 16-bit at
the input's rate. Deterministic.
"""
import sys
import wave

import numpy as np


def main():
    src, dst = sys.argv[1], sys.argv[2]
    cut = float(sys.argv[3]) if len(sys.argv) > 3 else 0.35
    xfade = float(sys.argv[4]) if len(sys.argv) > 4 else 0.3
    with wave.open(src) as w:
        rate, ch, width = w.getframerate(), w.getnchannels(), w.getsampwidth()
        assert width == 2, "16-bit WAV only"
        x = np.frombuffer(w.readframes(w.getnframes()), np.int16).reshape(-1, ch).astype(np.float64).mean(1)
    c, n = int(rate * cut), int(rate * xfade)
    x = x[c: len(x) - c]
    assert len(x) > 2 * n, "too short for that cut and cross-fade"
    t = np.linspace(0, np.pi / 2, n)
    head = x[:n] * np.sin(t) + x[-n:] * np.cos(t)
    y = np.concatenate([head, x[n: len(x) - n]])
    y = np.clip(y, -32768, 32767).astype(np.int16)
    with wave.open(dst, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(y.tobytes())
    print(f"{dst}: {len(y) / rate:.2f} s loop, seam jump {abs(int(y[-1]) - int(y[0])) / 32768:.3f}")


if __name__ == "__main__":
    main()
