// `FAcPilotChase`: the third-person camera of a driven unit (GAME-LAYER.md
// §2.12, chunk E3), the port of `PilotCamera.chase` and `reach(of:)`
// (Sources/Autocraft/PilotCamera.swift).
//
// As shooters do it: the camera on a boom that swings round a pivot over the
// unit's right shoulder, turned by the mouse alone (`ChaseYaw` across the
// ground, `Pitch` up at the sky or down at its feet). The pivot and the
// shoulder offset grow with how far the model reaches (`Reach`), the boom is
// `(2.6 + 3.3·reach) × FAcPilotCamera::Boom(kind).length` long. Two of E1's
// `Blocked` rays shorten it (pivot → camera, then camera → the unit's body),
// and the camera stays 0.3 cells over the ground. The sight stays in the
// middle of the view; the unit faces what that falls on
// (`AcPilotAim::Facing`), and its shots start at `Eye()` (the unit's own eye
// on its model), not at the camera.
//
// `AAcPilotPawn` owns one: V toggles `FAcPilotCamera::bThirdPerson` (kept
// for the next ride), and `Follow` puts the camera here instead of in the
// cockpit. No `USpringArmComponent`: its probe would need collision on the
// instanced units.
#pragma once

#include "CoreMinimal.h"

#include "Types.h"

struct FAcModelInfo;
class UWorld;

class AUTOCRAFT_API FAcPilotChase
{
public:
	/// The farthest a model's parts reach from its centre across the ground,
	/// in cells, its root's scale counted (`PilotCamera.reach(of:)`), from
	/// the pieces of the parts shown, posed as `Local` and `Visible` (empty:
	/// the rest pose); measured once per model, the first time.
	static float Reach(const FAcModelInfo& Model, TConstArrayView<bool> Visible = {}, TConstArrayView<FTransform> Local = {});
	/// `Reach` of the unit's model posed at ease (as Swift first meets it).
	static float AtEase(UWorld* World, const ac::Unit& U, const FAcModelInfo& Model);

	/// A new ride or a switch of view: nothing eased yet.
	void Reset();

	/// The camera behind the unit (`chase`). `Root`: the model root's world
	/// (UE cm); `Model`: its model (null: a reach of 1); `Pitch`, `Yaw`: the
	/// view (radians; yaw as `heading`); `RealNow` real seconds. Out: the
	/// camera's location and rotation (UE: looking along +X).
	void Follow(UWorld* World, const FTransform& Root, const ac::Unit& U, const FAcModelInfo* Model, double Pitch, double Yaw,
		double RealNow, FVector& OutLocation, FQuat& OutRotation);

	/// The unit's eye in the world (cm) as of the last `Follow`: where its
	/// line of fire starts (`chaseEye`). Unset before the first.
	const TOptional<FVector>& Eye() const { return ChaseEye; }

private:
	TOptional<TPair<double, double>> PivotZ;
	TOptional<FVector> ChaseEye;
};
