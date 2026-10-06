#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = ["numpy", "pillow"]
# ///
"""Ground textures for the Unreal terrain (`M_Terrain`, chunk A2).

Copies the five ground textures (unreal/Resources/Textures, from the
Swift game) into unreal/Content-src/terrain/ and bakes a tangent-space normal
map for each, offline, with the Sobel filter the Swift game runs at load
(`normalMap(from:strength:)`, Materials.swift:319). The Swift game only makes
normals for rock and plating; the ground ones are for Unreal's richer look.

Unreal wants DirectX-style normals (green = -dV), so green is the negative of
Swift's (OpenGL-style) green. Rerunnable: it overwrites its outputs.

    uv run unreal/Tools/terrain_textures.py
then, in the editor: unreal/Tools/Editor/make_terrain_material.py
"""
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / "unreal/Resources/Textures"
OUT = ROOT / "unreal/Content-src/terrain"

# name in the Swift game -> (name in Unreal, Sobel strength). Rock and
# plating use the Swift game's own strengths; the ground ones are gentler.
TEXTURES = {
    "ground_dirt": ("T_GroundDirt", 1.6),
    "ground_grass": ("T_GroundGrass", 1.2),
    "ground_highland": ("T_GroundHighland", 1.8),
    "cliff_rock": ("T_CliffRock", 3.0),
    "base_plating": ("T_BasePlating", 2.0),
}


def normal_map(rgb: np.ndarray, strength: float) -> np.ndarray:
    lum = (0.3 * rgb[..., 0] + 0.59 * rgb[..., 1] + 0.11 * rgb[..., 2]) / 255.0

    def L(dx: int, dy: int) -> np.ndarray:  # wraps, as the Swift code does
        return np.roll(np.roll(lum, -dy, axis=0), -dx, axis=1)

    gx = (L(1, -1) + 2 * L(1, 0) + L(1, 1)) - (L(-1, -1) + 2 * L(-1, 0) + L(-1, 1))
    gy = (L(-1, 1) + 2 * L(0, 1) + L(1, 1)) - (L(-1, -1) + 2 * L(0, -1) + L(1, -1))
    # Swift: (-gx·s, gy·s, 1) with image rows going down; DirectX green flips it.
    n = np.stack([-gx * strength, -gy * strength, np.ones_like(gx)], axis=-1)
    n /= np.linalg.norm(n, axis=-1, keepdims=True)
    return np.clip((n * 0.5 + 0.5) * 255.0 + 0.5, 0, 255).astype(np.uint8)


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    for src, (name, strength) in TEXTURES.items():
        img = Image.open(SRC / f"{src}.jpg").convert("RGB")
        img.save(OUT / f"{name}.png")
        normal = normal_map(np.asarray(img, dtype=np.float32), strength)
        Image.fromarray(normal, "RGB").save(OUT / f"{name}_N.png")
        print(f"{name}: {img.size[0]}x{img.size[1]}, normal strength {strength}")


if __name__ == "__main__":
    main()
