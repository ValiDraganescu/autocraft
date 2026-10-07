// Ringshadow space ↔ Unreal space: the one place that knows the axes, the
// units and the yaw sign (GAME-LAYER.md §1). Every chunk converts through
// these functions and nothing else.
//
// - The sim (`ac::Vec2(x, y)`, heights from `TerrainField`) works in cells:
//   1 world unit = 1 cell, and SceneKit draws it Y up, right-handed, with
//   the sim's (x, y) on SceneKit's ground (x, z).
// - Unreal is Z up, left-handed, in centimetres. **One cell is 1 m = 100 cm**
//   (`CmPerCell`), so a Ranger (about 1 cell tall) is a person-sized 1 m
//   and the physics, audio and light defaults behave at sane scales.
// - Ground: sim (x, y) → UE (X, Y) = (x, y)·100; height h → UE Z = h·100.
// - SceneKit points (x, y, z) → UE (x, z, y)·100: swapping Y and Z is a
//   reflection, and that reflection is exactly what turns right-handed into
//   left-handed, so a model exported from SceneKit and placed with these
//   functions looks the same (not mirrored).
// - Facing: `Unit.heading` is radians in the sim plane, 0 = +x, π/2 = +y
//   (SceneKit sets `eulerAngles.y = -heading`). In Unreal a unit's yaw is
//   **+heading in degrees** (yaw 90° turns +X toward +Y). Models face +X in
//   both. Buildings face SceneKit +Z, which is UE +Y.
// - SceneKit rotations: a quaternion (x, y, z, w) becomes (-x, -z, -y, w)
//   (the axis is a pseudovector under the reflection); a rotation about
//   SceneKit Y by a is UE yaw −a degrees.
//
// The round trips and signs are checked by the automation tests
// `Autocraft.Space.*` (AcSpaceTests.cpp).
#pragma once

#include "CoreMinimal.h"
#include "SimdMath.h"

namespace AcSpace
{
	/// Centimetres per cell (per sim and SceneKit world unit).
	inline constexpr double CmPerCell = 100.0;
	inline constexpr double CellsPerCm = 1.0 / CmPerCell;

	/// A sim ground point and a height (cells) → an Unreal world location.
	inline FVector ToWorld(const ac::Vec2 P, const double Height = 0.0)
	{
		return FVector(P.x * CmPerCell, P.y * CmPerCell, Height * CmPerCell);
	}

	/// An Unreal world location → the sim ground point under it.
	inline ac::Vec2 ToSim(const FVector& W)
	{
		return ac::Vec2(W.X * CellsPerCm, W.Y * CellsPerCm);
	}

	/// An Unreal world location's height in cells.
	inline double HeightToSim(const FVector& W)
	{
		return W.Z * CellsPerCm;
	}

	/// A length in cells → centimetres, and back.
	inline double ToCm(const double Cells) { return Cells * CmPerCell; }
	inline double ToCells(const double Cm) { return Cm * CellsPerCm; }

	/// A SceneKit point (x, y, z), Y up, in cells → an Unreal location.
	inline FVector FromSceneKit(const double X, const double Y, const double Z)
	{
		return FVector(X * CmPerCell, Z * CmPerCell, Y * CmPerCell);
	}

	/// An Unreal location → a SceneKit point (x, y, z) in cells.
	inline FVector ToSceneKit(const FVector& W)
	{
		return FVector(W.X * CellsPerCm, W.Z * CellsPerCm, W.Y * CellsPerCm);
	}

	/// A SceneKit direction or scale (no units): (x, y, z) → (x, z, y).
	inline FVector AxesFromSceneKit(const double X, const double Y, const double Z)
	{
		return FVector(X, Z, Y);
	}

	/// A SceneKit quaternion (x, y, z, w) → the same rotation in Unreal.
	inline FQuat QuatFromSceneKit(const double X, const double Y, const double Z, const double W)
	{
		return FQuat(-X, -Z, -Y, W);
	}

	/// An Unreal quaternion → SceneKit (x, y, z, w), as an FQuat holding them.
	inline FQuat QuatToSceneKit(const FQuat& Q)
	{
		return FQuat(-Q.X, -Q.Z, -Q.Y, Q.W);
	}

	/// A SceneKit node transform (position in cells, quaternion, scale) →
	/// an Unreal transform.
	inline FTransform TransformFromSceneKit(const FVector& SkPosition, const FQuat& SkRotation, const FVector& SkScale)
	{
		return FTransform(
			QuatFromSceneKit(SkRotation.X, SkRotation.Y, SkRotation.Z, SkRotation.W),
			FromSceneKit(SkPosition.X, SkPosition.Y, SkPosition.Z),
			AxesFromSceneKit(SkScale.X, SkScale.Y, SkScale.Z));
	}

	/// `Unit.heading` (radians, 0 = +x, π/2 = +y) → Unreal yaw in degrees.
	inline double YawFromHeading(const double Heading)
	{
		return FMath::RadiansToDegrees(Heading);
	}

	/// Unreal yaw in degrees → a sim heading in radians.
	inline double HeadingFromYaw(const double Yaw)
	{
		return FMath::DegreesToRadians(Yaw);
	}

	/// The rotation that turns a model facing +X to `Heading`.
	inline FQuat QuatFromHeading(const double Heading)
	{
		return FQuat(FVector::UpVector, Heading);
	}

	inline FRotator RotatorFromHeading(const double Heading)
	{
		return FRotator(0.0, YawFromHeading(Heading), 0.0);
	}

	/// A sim direction (a unit vector on the ground) → an Unreal direction.
	inline FVector DirectionToWorld(const ac::Vec2 D)
	{
		return FVector(D.x, D.y, 0.0);
	}
}
