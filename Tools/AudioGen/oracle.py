# /// script
# requires-python = ">=3.11"
# dependencies = ["numpy"]
# ///
"""The base computer's voice: clean and wide, with a faint synthetic sheen.

    uv run Tools/AudioGen/oracle.py <in.wav> <out.wav>

Not a radio (that is comm.py, the units' helmets): the Oracle speaks
from the base itself, so the band stays wide (140 Hz - 9 kHz), with no
squelch. A very short comb (a thin metallic resonance) and a light ring
modulation make it sound built rather than spoken; soft clipping evens
it out. Mono 16-bit WAV at the input's rate. Deterministic.
"""
import sys
import wave

import numpy as np


def read(path):
    with wave.open(path) as w:
        rate, ch = w.getframerate(), w.getnchannels()
        x = np.frombuffer(w.readframes(w.getnframes()), np.int16).astype(np.float64) / 32768
    if ch > 1:
        x = x.reshape(-1, ch).mean(axis=1)
    return x, rate


def band(x, rate, low=140, high=9000):
    n = len(x)
    f = np.fft.rfftfreq(n, 1 / rate)
    hp = 1 / np.sqrt(1 + (low / np.maximum(f, 1)) ** 4)
    lp = 1 / np.sqrt(1 + (f / high) ** 8)
    return np.fft.irfft(np.fft.rfft(x) * hp * lp, n)


def comb(x, rate, ms=0.9, gain=0.3):
    d = max(1, int(rate * ms / 1000))
    y = x.copy()
    for i in range(d, len(y)):
        y[i] += gain * y[i - d]
    return y


def oracle(x, rate):
    y = comb(band(x, rate), rate)
    t = np.arange(len(y)) / rate
    y = y * (1 - 0.06 + 0.06 * np.sin(2 * np.pi * 110 * t))
    y = np.tanh(1.6 * y / (np.abs(y).max() + 1e-9)) / np.tanh(1.6)
    return np.concatenate([np.zeros(int(rate * 0.05)), y, np.zeros(int(rate * 0.05))])


def main():
    x, rate = read(sys.argv[1])
    y = oracle(x, rate)
    y = y / (np.abs(y).max() + 1e-9) * 0.89
    with wave.open(sys.argv[2], "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes((y * 32767).astype(np.int16).tobytes())


if __name__ == "__main__":
    main()
