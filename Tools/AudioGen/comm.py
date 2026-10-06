# /// script
# requires-python = ">=3.11"
# dependencies = ["numpy"]
# ///
"""A voice through a suit's helmet comm: thin, bright and a little electronic.

    uv run Tools/AudioGen/comm.py <in.wav> <out.wav>

Band-limited to a radio's 320 Hz - 6 kHz, a presence lift at 3 kHz so
consonants bite, a short metal comb (the helmet), a faint ring modulation
(the electronic edge), soft clipping, and a squelch click in and out. Mono
16-bit WAV at the input's rate. Deterministic: the same input gives the same
file.
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


def shape(x, rate):
    """Band limit and presence lift, in the frequency domain."""
    n = len(x)
    f = np.fft.rfftfreq(n, 1 / rate)
    X = np.fft.rfft(x)
    hp = 1 / np.sqrt(1 + (320 / np.maximum(f, 1)) ** 8)
    lp = 1 / np.sqrt(1 + (f / 6000) ** 8)
    presence = 1 + 0.9 * np.exp(-0.5 * ((f - 3000) / 900) ** 2)
    return np.fft.irfft(X * hp * lp * presence, n)


def comb(x, rate, ms=2.3, gain=0.25):
    """A short feedback echo: the ring of a small metal space."""
    d = max(1, int(rate * ms / 1000))
    y = x.copy()
    for i in range(d, len(y)):
        y[i] += gain * y[i - d]
    return y


def squelch(rate, seed, ms=45):
    """A burst of band-limited static: the key going down or up."""
    n = int(rate * ms / 1000)
    noise = np.random.default_rng(seed).standard_normal(n)
    env = np.exp(-np.linspace(0, 6, n))
    return shape(noise * env, rate) * 0.25


def comm(x, rate):
    y = shape(x, rate)
    y = comb(y, rate)
    t = np.arange(len(y)) / rate
    y = y * (1 - 0.1 + 0.1 * np.sin(2 * np.pi * 57 * t))
    y = np.tanh(2.2 * y / (np.abs(y).max() + 1e-9)) / np.tanh(2.2)
    # The key goes down a beat before the first word: static right against
    # it masks the first consonant ("Torch" heard as "George").
    lead, tail = np.zeros(int(rate * 0.12)), np.zeros(int(rate * 0.04))
    return np.concatenate([squelch(rate, 1, ms=35), lead, y, tail, squelch(rate, 2, ms=30)])


def main():
    x, rate = read(sys.argv[1])
    y = comm(x, rate)
    y = y / (np.abs(y).max() + 1e-9) * 0.89
    with wave.open(sys.argv[2], "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes((y * 32767).astype(np.int16).tobytes())


if __name__ == "__main__":
    main()
