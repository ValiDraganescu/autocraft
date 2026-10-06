# /// script
# requires-python = ">=3.10"
# dependencies = ["numpy", "pillow"]
# ///
"""Bakes the gas giant's look once (GAME-LAYER.md, "Gas giant" rows).

  uv run unreal/Tools/Editor/bake_gas_giant.py [--small]

Writes (rerunnable, deterministic):
  unreal/Content-src/sky/gasgiant_bands.png   4096 x 2048 lat-long map of the
      cloud bands (sRGB-encoded linear albedo; u = longitude / 2 pi, v = 0.5 -
      latitude / pi). The two-layer flow of the old per-pixel shader is the
      same map read at two lags: the material scrolls longitude per latitude.
  unreal/Content-src/sky/gasgiant_ring.png    1024 x 4 strip over the ring's
      radius 1.2..2.5: R density, G ripple, B density without the fine
      detail (for the planet's own shadow term). Linear data.

The noise is the SkyFx shader's (PLANET_HLSL in make_sky_material.py)
ported to numpy: bands warped by a first turbulence, then flow-stretched
detail read through the warp, then filaments.
make_sky_material.py imports both PNGs.
"""
import os
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "..", "Content-src", "sky")
f32 = np.float32


def frac(x):
    return x - np.floor(x)


def smoothstep(a, b, x):
    t = np.clip((x - a) / (b - a), 0.0, 1.0)
    return t * t * (3 - 2 * t)


def hash3(x, y, z):
    x = frac(x * f32(0.1031))
    y = frac(y * f32(0.1030))
    z = frac(z * f32(0.0973))
    d = x * (y + f32(33.33)) + y * (z + f32(33.33)) + z * (x + f32(33.33))
    x = x + d
    y = y + d
    z = z + d
    return frac((x + y) * z)


def noise3(x, y, z):
    ix, iy, iz = np.floor(x), np.floor(y), np.floor(z)
    fx, fy, fz = x - ix, y - iy, z - iz
    fx = fx * fx * (3 - 2 * fx)
    fy = fy * fy * (3 - 2 * fy)
    fz = fz * fz * (3 - 2 * fz)

    def h(dx, dy, dz):
        return hash3(ix + dx, iy + dy, iz + dz)

    def lerp(a, b, t):
        return a + (b - a) * t

    return lerp(
        lerp(lerp(h(0, 0, 0), h(1, 0, 0), fx), lerp(h(0, 1, 0), h(1, 1, 0), fx), fy),
        lerp(lerp(h(0, 0, 1), h(1, 0, 1), fx), lerp(h(0, 1, 1), h(1, 1, 1), fx), fy), fz)


def fbm(x, y, z, octaves):
    s = np.zeros_like(x)
    a, nrm = f32(0.5), f32(0.0)
    for _ in range(octaves):
        s = s + a * noise3(x, y, z)
        nrm += a
        x, y, z = x * f32(2.03) + f32(17.1), y * f32(2.03) + f32(5.3), z * f32(2.03) + f32(9.7)
        a *= f32(0.5)
    return s / nrm


def band_col(lat, tu):
    b = 0.5 + 0.28 * np.sin(lat * 11.0 + 1.2 * np.sin(lat * 3.3)) + 0.17 * np.sin(lat * 23.0 + 0.7) + 0.09 * np.sin(lat * 41.0 + 2.0)
    b = np.clip(b + tu, 0, 1)
    dk, md, lt = np.array([0.30, 0.22, 0.17]), np.array([0.68, 0.53, 0.37]), np.array([0.96, 0.90, 0.76])
    lo = dk + (md - dk) * (b * 2.0)[:, None]
    hi = md + (lt - md) * ((b - 0.5) * 2.0)[:, None]
    c = np.where((b < 0.5)[:, None], lo, hi)
    pole = smoothstep(0.95, 1.4, np.abs(lat))
    pc = np.array([0.50, 0.56, 0.64]) * (0.75 + 0.35 * b)[:, None]
    return c + (pc - c) * (pole * 0.75)[:, None]


def layer(lat, lon):
    cl = np.cos(lat)
    qx = (cl * np.cos(lon) * 3.0).astype(f32)
    qy = (np.sin(lat) * 15.0).astype(f32)
    qz = (cl * np.sin(lon) * 3.0).astype(f32)
    t = fbm(qx, qy, qz, 5) - 0.5
    wx, wy, wz = qx + t * 0.9, qy + t * 0.15, qz - t * 0.9
    t2 = fbm(wx * 2.3 + 7.3, wy * 1.5 + 7.3, wz * 2.3 + 7.3, 4) - 0.5
    t3 = fbm(wx * 7.0 + 3.1, wy * 3.2 + 3.1, wz * 7.0 + 3.1, 3) - 0.5
    c = band_col(lat + 0.055 * t + 0.012 * t3, 0.3 * t2 + 0.16 * t3)
    fil = 0.5 + 0.5 * np.sin(lat * 130.0 + 9.0 * t2 + 4.0 * t3)
    return c * ((0.93 + 0.14 * np.sin(lat * 70.0 + 5.0 * t)) * (1.0 - 0.1 * fil * smoothstep(0.1, 0.4, np.abs(t2))))[:, None]


def srgb(c):
    c = np.clip(c, 0, 1)
    return np.where(c <= 0.0031308, c * 12.92, 1.055 * np.power(c, 1 / 2.4) - 0.055)


def bake_bands(w, h):
    img = np.zeros((h, w, 3), np.uint8)
    xs = ((np.arange(w) + 0.5) / w * 2 * np.pi).astype(f32)
    for y0 in range(0, h, 16):
        rows = np.arange(y0, min(y0 + 16, h))
        lat = (np.pi / 2 - (rows + 0.5) / h * np.pi).astype(f32)
        LAT = np.repeat(lat, w)
        LON = np.tile(xs, len(rows))
        c = layer(LAT, LON).reshape(len(rows), w, 3)
        img[rows] = (srgb(c) * 255 + 0.5).astype(np.uint8)
        print(f"bands {y0 + len(rows)}/{h}", file=sys.stderr, end="\r")
    print(file=sys.stderr)
    return img


def ring_density(rho, detail):
    d = smoothstep(1.26, 1.34, rho) * smoothstep(2.46, 2.38, rho)
    d *= 0.4 + 0.6 * smoothstep(1.52, 1.58, rho)
    d *= 1.0 - 0.93 * smoothstep(1.96, 2.0, rho) * smoothstep(2.1, 2.05, rho)
    d *= 1.0 - 0.75 * np.exp(-((rho - 2.29) * 55.0) ** 2)
    d *= 1.0 - detail * 0.28 * (0.5 - 0.5 * np.sin(rho * 97.0 + 2.0 * np.sin(rho * 23.0))) * 2.0 * 0.5 - (1.0 - detail) * 0.1
    return np.clip(d, 0, 1)


def bake_ring(w):
    rho = 1.2 + (np.arange(w) + 0.5) / w * 1.3
    rip = 0.5 + 0.5 * np.sin(rho * 61.0 + 3.0 * np.sin(rho * 17.0))
    rip2 = 0.5 + 0.5 * np.sin(rho * 231.0 + 4.0 * np.sin(rho * 71.0))
    rip = rip + (rip2 - rip) * 0.35
    row = np.stack([ring_density(rho, 1.0), rip, ring_density(rho, 0.0)], axis=-1)
    return (np.clip(row, 0, 1) * 255 + 0.5).astype(np.uint8)


def main():
    os.makedirs(OUT, exist_ok=True)
    small = "--small" in sys.argv
    w, h = (1024, 512) if small else (4096, 2048)
    Image.fromarray(bake_bands(w, h), "RGB").save(os.path.join(OUT, "gasgiant_bands.png"))
    ring = bake_ring(1024)
    Image.fromarray(np.repeat(ring[None], 4, axis=0), "RGB").save(os.path.join(OUT, "gasgiant_ring.png"))
    print("baked", OUT)


main()
