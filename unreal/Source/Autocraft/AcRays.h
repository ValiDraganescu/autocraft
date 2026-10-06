// `AcRays`: the ray math of `Sources/Autocraft/GameScene+Rays.swift` (chunk
// E1, GAME-LAYER.md §2.9), plain C++ with no engine types. The Unreal side
// (`UAcRaysSubsystem`, AcRaysWorld.h) feeds it the ground, the parts' world
// transforms and the shapes; tests in AcRaysTests.cpp check it against the
// Swift code's own results (core-tests/golden/rays.json).
//
// Everything here is in SceneKit space, as in Swift, so the code ports line
// by line: cells, Y up, points `F3{x, height, z}` (sim ground (x, y) is
// (x, z)), matrices column-major (simd). AcRaysWorld converts.
//
// - `FAcRayBox`: one solid piece (a `RayBox`): a geometry's bounds and, for
//   a round primitive, its true sphere, cylinder or capsule inside them.
//   `Hit` is where a segment first enters it. A segment starting inside
//   passes (the bounds are looser than the part).
// - `FAcRayShape`: a model as the rays see it (a `RayShape`): every piece,
//   the part it moves with, and how far the parts reach from the root, to
//   pass it by cheaply. A hidden part is not met: a shot between a
//   Ranger's legs misses.
// - `AcRays::Cast`: what a segment meets first: the ground
//   (`FAcHeightGrid::crossing`), then each target, then the scenery.
#pragma once

#include "AcHeightGrid.h"

#include <cstdint>
#include <optional>
#include <vector>

using FAcF3 = FAcHeightGrid::F3;

/// A 4×4 float matrix as simd holds it: column-major, `M[c * 4 + r]`.
struct FAcMat4
{
	float M[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

	static FAcMat4 Identity() { return FAcMat4(); }
	/// From 16 floats in simd's order (the manifest's `transform`, `localTransform`).
	static FAcMat4 FromColumns(const float* V);

	float& operator()(int R, int C) { return M[C * 4 + R]; }
	float operator()(int R, int C) const { return M[C * 4 + R]; }
	FAcMat4 operator*(const FAcMat4& B) const;
	/// The general inverse (simd's `inverse`); the identity's for a singular one.
	FAcMat4 Inverse() const;
	FAcF3 Point(FAcF3 P) const;
	FAcF3 Vector(FAcF3 V) const;
	FAcF3 Translation() const { return {M[12], M[13], M[14]}; }
	/// The longest of the three axis columns (`max(length(columns.k))`).
	float MaxScale() const;
};

/// One solid piece: Swift's `RayBox`.
struct FAcRayBox
{
	enum class ERound : uint8_t
	{
		None,
		Sphere,
		Cylinder,
		Capsule
	};

	FAcF3 Lo, Hi;
	ERound Round = ERound::None;
	/// The round primitive about the node's origin, along its y: radius, and
	/// half height (a capsule's: from its middle to a cap's centre).
	float Radius = 0, Half = 0;

	/// False for empty bounds.
	bool Known() const;
	/// Where along `A` + (`B` − `A`)·t, t in 0…1, the piece under
	/// `Transform` (its node's world) is first met; nullopt: never.
	std::optional<float> Hit(const FAcMat4& Transform, FAcF3 A, FAcF3 B) const;
};

/// A model as the rays see it: Swift's `RayShape`.
struct FAcRayShape
{
	struct FPiece
	{
		/// The part it moves with (index into the model's parts).
		int32_t Part = 0;
		/// The piece's node in the part's frame.
		FAcMat4 At;
		FAcRayBox Box;
	};

	std::vector<FPiece> Pieces;
	/// Each part's parent (−1: the root); parents come first.
	std::vector<int32_t> Parent;
	/// The root part (the model root: placement × its rest is its world).
	int32_t Root = 0;
	/// How far the pieces reach from the root's origin, in the root's own
	/// space, with room for them to swing (×1.25); 0 with no pieces.
	float Reach = 0;

	bool Empty() const { return Pieces.empty(); }

	/// `Reach` from the parts' rest transforms (relative to their parents;
	/// the root's own is left out, as Swift measures in the root's space).
	void Measure(const std::vector<FAcMat4>& RestLocal);

	/// Could the segment come within reach of the root (world `RootWorld`)?
	bool Near(const FAcMat4& RootWorld, FAcF3 A, FAcF3 B) const;

	/// Where along the segment the first visible piece is met, if before
	/// `Limit`. `PartWorld[p]`: each part's world matrix; `Visible[p]`
	/// (null: all): each part shown, its parents' visibility already folded
	/// in. Does not test `Near`: `Hit` does both.
	std::optional<float> HitPieces(const FAcMat4* PartWorld, const uint8_t* Visible, FAcF3 A, FAcF3 B, float Limit) const;
	std::optional<float> Hit(const FAcMat4* PartWorld, const uint8_t* Visible, FAcF3 A, FAcF3 B, float Limit) const;
};

namespace AcRays
{
	/// What a segment met: `T` along it and the unit or building's id
	/// (nullopt: the ground or the scenery).
	struct FHit
	{
		float T = 0;
		std::optional<int64_t> Id;
	};

	/// One unit, building or piece of scenery at this moment.
	struct FTarget
	{
		/// Nullopt: scenery (ore, a well, a doodad).
		std::optional<int64_t> Id;
		const FAcRayShape* Shape = nullptr;
		/// Each part's world (index = part); `Visible` as in `HitPieces`.
		const FAcMat4* PartWorld = nullptr;
		const uint8_t* Visible = nullptr;
	};

	/// `GameScene.cast`: the ground first, then each target (units and
	/// buildings; scenery last), the nearest kept. `Skip` lets one id through.
	std::optional<FHit> Cast(const FAcHeightGrid* Ground, const std::vector<FTarget>& Targets, FAcF3 A, FAcF3 B,
		std::optional<int64_t> Skip = std::nullopt);
}
