# /// script
# requires-python = ">=3.11"
# dependencies = ["numpy"]
# ///
"""A voice on KSTR 88.7 Stardust: a polished radio ad, heard over the air.

    uv run Tools/AudioGen/radio.py <in.wav> <out.wav> [--boom <boom.wav>]

The station's ads and idents (docs/music.md). Silence trimmed off both
ends; the broadcast band (110 Hz - 7.5 kHz) with a presence lift at
2.5 kHz; a station compressor that evens the read; soft saturation; and
the level set to the music's (-16.5 dBFS RMS over the louder half of
0.4 s blocks, the Suno tracks sit at -14.5 to -18), so an ad sits with
the songs. With --boom, a distant explosion (low-passed, quiet) goes into
the longest pause in the second half, widened to fit. Mono 16-bit WAV at
the input's rate. Deterministic: the same input gives the same file.
"""
import sys
import wave

import numpy as np

TARGET = -16.5


def read(path):
    with wave.open(path) as w:
        rate, ch = w.getframerate(), w.getnchannels()
        x = np.frombuffer(w.readframes(w.getnframes()), np.int16).astype(np.float64) / 32768
    if ch > 1:
        x = x.reshape(-1, ch).mean(axis=1)
    return x, rate


def write(path, x, rate):
    y = (np.clip(x, -1, 1) * 32767).astype(np.int16)
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(y.tobytes())


def frames(x, rate, ms=20):
    n = max(1, int(rate * ms / 1000))
    k = len(x) // n
    return np.sqrt((x[: k * n].reshape(k, n) ** 2).mean(axis=1) + 1e-12), n


def trim(x, rate, pad=0.12):
    level, n = frames(x, rate)
    loud = np.nonzero(level > 10 ** (-45 / 20))[0]
    if len(loud) == 0:
        return x
    a = max(0, loud[0] * n - int(pad * rate))
    b = min(len(x), (loud[-1] + 1) * n + int(pad * rate))
    return x[a:b]


def shape(x, rate, f, gain):
    """Multiply the spectrum by gain(f)."""
    n = len(x)
    freqs = np.fft.rfftfreq(n, 1 / rate)
    return np.fft.irfft(np.fft.rfft(x) * gain(freqs), n)


def band(x, rate):
    def gain(f):
        hp = 1 / np.sqrt(1 + (110 / np.maximum(f, 1)) ** 4)
        lp = 1 / np.sqrt(1 + (f / 7500) ** 8)
        presence = 1 + 0.35 * np.exp(-((np.log2(np.maximum(f, 1) / 2500)) ** 2) / 0.5)
        return hp * lp * presence
    return shape(x, rate, None, gain)


def compress(x, rate, threshold=-24, ratio=3.0):
    """A station compressor: 5 ms attack, 120 ms release, on the RMS."""
    env = np.zeros_like(x)
    a, r = np.exp(-1 / (0.005 * rate)), np.exp(-1 / (0.12 * rate))
    e = 0.0
    sq = x * x
    for i in range(len(x)):
        c = a if sq[i] > e else r
        e = c * e + (1 - c) * sq[i]
        env[i] = e
    level = 10 * np.log10(env + 1e-12)
    over = np.maximum(level - threshold, 0)
    return x * 10 ** (-(over - over / ratio) / 20)


def loudness(x, rate):
    n = int(rate * 0.4)
    k = len(x) // n
    if k == 0:
        return 20 * np.log10(np.sqrt((x * x).mean()) + 1e-12)
    blocks = np.sort((x[: k * n].reshape(k, n) ** 2).mean(axis=1))
    return 10 * np.log10(blocks[k // 2:].mean() + 1e-12)


def boom(x, rate, path):
    """A far-off blast in the longest pause of the second half."""
    b, brate = read(path)
    t = np.arange(int(len(b) * rate / brate)) * brate / rate
    b = np.interp(t, np.arange(len(b)), b)
    b = shape(b, rate, None, lambda f: 1 / np.sqrt(1 + (f / 900) ** 4)) * 0.4
    level, n = frames(x, rate)
    quiet = level < 10 ** (-40 / 20)
    best, run, start = (0, 0), 0, 0
    for i, q in enumerate(quiet):
        if q:
            if run == 0:
                start = i
            run += 1
            if i * n > len(x) / 2 and run > best[1]:
                best = (start, run)
        else:
            run = 0
    at = (best[0] * n if best[1] else len(x) // 2) + int(0.1 * rate)
    # Widen the pause so the blast's loud part fits, then lay it in.
    need = int(1.3 * rate) - best[1] * n
    if need > 0:
        x = np.concatenate([x[:at], np.zeros(need), x[at:]])
    y = np.zeros(max(len(x), at + len(b)))
    y[: len(x)] = x
    y[at: at + len(b)] += b
    return y


def main():
    args = sys.argv[1:]
    blast = None
    if "--boom" in args:
        i = args.index("--boom")
        blast = args[i + 1]
        args = args[:i] + args[i + 2:]
    src, dst = args
    x, rate = read(src)
    x = trim(x, rate)
    if blast:
        x = boom(x, rate, blast)
    x = band(x, rate)
    x = compress(x, rate)
    x = np.tanh(1.4 * x) / np.tanh(1.4)
    # The level, then the peaks rounded off under -1 dBFS; twice, as the
    # limiting takes a little of the level.
    for _ in range(2):
        x *= 10 ** ((TARGET - loudness(x, rate)) / 20)
        x = limit(x)
    write(dst, x, rate)


def limit(x, knee=0.6, ceiling=0.89):
    """Untouched below `knee`; above it, rounded off toward `ceiling`."""
    a = np.abs(x)
    over = a > knee
    y = x.copy()
    y[over] = np.sign(x[over]) * (knee + (ceiling - knee) * np.tanh((a[over] - knee) / (ceiling - knee)))
    return y


if __name__ == "__main__":
    main()
