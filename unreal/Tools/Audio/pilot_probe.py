# E7 (AcPilotAudio.h): checks the master recording of an -AcPilotAudioProbe run against Swift's measured stage.
# uv run --with numpy python pilot_probe.py RUN.wav RUN.log   (the run: see the header of AcPilotAudio.h / GAME-LAYER.md E7)
# Check E7's offline probe WAV against Swift's measured stage (avmeasure.txt).
import sys, re, wave, math
import numpy as np
wav, log = sys.argv[1], sys.argv[2]
L = open(log, errors='ignore').read()
start = int(re.search(r'\]\[\s*(\d+)\]LogAutocraft: audio: recording [0-9]', L).group(1))
probes = {}
for m in re.finditer(r'probe (\d+) (\w+) [^\n]*? at frame (\d+)[^\n]*\n[^\n]*pilot audio: \w+ d ([\d.]+) az (-?\d+) el (-?\d+) L (-?[\d.]+) R (-?[\d.]+) dB, blocked (\d), blend ([\d.]+), gain ([\d.]+), pan ([\d.]+), lpf (\d+), send ([\d.]+), volume ([\d.]+)', L):
    k = int(m.group(1)); probes[k] = dict(frame=int(m.group(3)), gain=float(m.group(11)), blend=float(m.group(10)), send=float(m.group(14)), vol=float(m.group(15)), az=m.group(5), el=m.group(6))
w = wave.open(wav); sr = w.getframerate()
x = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).astype(np.float64).reshape(-1, 2) / 32767
f0 = None
def t(k): return (probes[k]['frame'] - start) / 60.0
def seg(a, b): return x[max(0, int(a * sr)):int(b * sr)]
def pw(s): return float(np.mean(s ** 2)) if len(s) else 0.0
def en(s): return float(np.sum(s ** 2))
db = lambda v: 10 * math.log10(max(v, 1e-30))
print(f"recording from frame {start}; probes at", {k: round(t(k), 3) for k in probes})
noise = pw(seg(t(0) - 0.6, t(0) - 0.05)); print(f"floor before the probes: {db(noise):.1f} dBFS (game hushed)")
print("\n1. The wind noise round the ears (flat offline: the 3D gain without the pan)")
ref = None
for k in range(5):
    s = seg(t(k) + 0.15, t(k) + 0.7); p = pw(s[:, 0]) + pw(s[:, 1])
    if ref is None: ref = (p, probes[0]['gain'])
    exp = 20 * math.log10(probes[k]['gain'] / ref[1])
    print(f"  {k} az {probes[k]['az']:>4} el {probes[k]['el']:>3}: measured {db(p) - db(ref[0]):+6.2f} dB vs ahead, expected {exp:+6.2f} (gain {probes[k]['gain']:.3f})")
print("\n2. Obstruction: blocked / clear drone by band (Swift's -14 dB obstruction measured with sines)")
swift = {63: -1.75, 125: -1.75, 250: -1.77, 500: -1.82, 1000: -2.05, 2000: -2.87, 4000: -5.50, 8000: -11.26, 12000: -15.53, 16000: -18.19}
a = seg(t(0) + 0.15, t(0) + 0.7).mean(axis=1); b = seg(t(4) + 0.15, t(4) + 0.7).mean(axis=1)
n = min(len(a), len(b)); A = np.abs(np.fft.rfft(a[:n] * np.hanning(n))) ** 2; B = np.abs(np.fft.rfft(b[:n] * np.hanning(n))) ** 2; f = np.fft.rfftfreq(n, 1 / sr)
gfix = 20 * math.log10(probes[4]['gain'] / probes[0]['gain'])
for c, sv in swift.items():
    m = (f > c / 2 ** (1 / 6)) & (f < c * 2 ** (1 / 6))
    if A[m].sum() <= 0: continue
    print(f"  {c:6d} Hz: UE {db(B[m].sum()) - db(A[m].sum()):+6.2f} dB   Swift {sv:+6.2f}   (source level {db(A[m].sum()) - db(A.sum()):6.1f} dB)")
print("\n3. The hall: a hit mark ahead, dry (send 0) vs with its send; tail 0.35-1.9 s after")
def tail(k): return en(seg(t(k) + 0.35, t(k) + 1.9))
def direct(k): return en(seg(t(k), t(k) + 0.3))
E5, T5, T7, T9 = direct(5), tail(5), tail(7), tail(9)
bl, g5 = probes[7]['blend'], probes[5]['gain']
target = 0.0485 * 2 * bl * E5 / g5 ** 2  # Swift: tail = -13.15 dB x b of the source (both ears), dry = gain^2 x source
print(f"  dry energy {db(E5):.1f} dB; dry's own tail {db(T5):.1f}; with the hall {db(T7):.1f}; Swift's tail for blend {bl:.3f}: {db(target):.1f} dB"
      f"  -> hall {db(T7 - T5) - db(target):+.2f} dB off")
print(f"  blocked tail {db(T9):.1f} dB ({db(T9) - db(T7):+.2f} vs clear; Swift: 0.00)")
sl = []
for a0 in np.arange(0.3, 1.9, 0.1): sl.append(db(en(seg(t(7) + a0, t(7) + a0 + 0.1))) - db(E5))
ok = [(i * 0.1, v) for i, v in enumerate(sl) if v > -100]
d = np.polyfit([a for a, _ in ok], [v for _, v in ok], 1)[0] if len(ok) > 2 else float('nan')
print(f"  decay {d:.1f} dB/s (Swift's mediumHall: -31.5 dB/s); 0.1 s slices re the dry: " + " ".join(f"{v:.0f}" for v in sl))
