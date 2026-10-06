#include "AcPoseAir.h"

#include "AcModelCatalog.h"
#include "AcSpace.h"

#include <cmath>

namespace
{
	double Smoothstep(const double A, const double B, const double X)
	{
		const double T = FMath::Clamp((X - A) / (B - A), 0.0, 1.0);
		return T * T * (3.0 - 2.0 * T);
	}

	/// Swift's `fmod` for a time that is never negative here.
	double Fract(const double X) { return X - std::floor(X); }

	/// The emission intensities the export baked into each material
	/// (manifest.json `emissiveIntensity`): custom data 1 is the scale on
	/// top of them, so a Swift `emission.intensity` v is v / exported.
	constexpr double DropshipGlowExported = 1.8;    // glow_ff6b14, the engine nozzles
	constexpr double BeaconExported = 2.5;          // glow_ff0d03 (Dropship), beacon (Kestrel)
	constexpr double BayExported = 0.15;            // glow_ffa956
	constexpr double LensExported = 0.7;            // glow_52f665
	constexpr double BeamCoreExported = 2.2;        // glow_bcfbbe (the halo, glow_2cdf49, never changes)
	constexpr double KestrelGlowExported = 1.8;     // glow_ff5707

	/// The red beacons blink twice every 1/k seconds (`ph < 0.06 || 0.14 < ph < 0.2`).
	double BeaconIntensity(const double T, const double K)
	{
		const double Ph = Fract(T * K);
		return (Ph < 0.06 || (Ph > 0.14 && Ph < 0.2)) ? 3.2 : 0.5;
	}

	struct FPart
	{
		const TCHAR* Name;
		FAcPartIndex Index;
		explicit FPart(const TCHAR* InName) : Name(InName), Index(FName(InName)) {}
		int32 Get(const FAcModelInfo& M) const { return Index.Get(M); }
	};

	/// A part's local transform with a new SceneKit position (cells) and
	/// euler angles, the rest scale kept.
	void SetSk(FAcPose& P, const FAcModelInfo& M, const int32 I, const FVector& SkPos, const FVector& SkEuler)
	{
		if (I == INDEX_NONE) return;
		P.Local[I] = FTransform(AcPoseAir::QuatFromSceneKitEuler(SkEuler.X, SkEuler.Y, SkEuler.Z),
			AcSpace::FromSceneKit(SkPos.X, SkPos.Y, SkPos.Z), M.Parts[I].Rest.GetScale3D());
	}

	void SetRotation(FAcPose& P, const int32 I, const FVector& SkEuler)
	{
		if (I == INDEX_NONE) return;
		P.Local[I].SetRotation(AcPoseAir::QuatFromSceneKitEuler(SkEuler.X, SkEuler.Y, SkEuler.Z));
	}

	void SetEmission(FAcPose& P, const int32 I, const double Value)
	{
		if (I != INDEX_NONE) P.Emission[I] = float(Value);
	}

	/// Scale a ground shadow (its rest scale × k).
	void ScaleShadow(FAcPose& P, const FAcModelInfo& M, const int32 I, const double K)
	{
		if (I == INDEX_NONE) return;
		P.Local[I].SetScale3D(M.Parts[I].Rest.GetScale3D() * K);
	}

	/// The part's transform relative to the model root (part 0), from `Local`.
	FTransform ToRoot(const FAcPose& P, const FAcModelInfo& M, int32 I)
	{
		FTransform T = FTransform::Identity;
		while (I > 0)
		{
			T = T * P.Local[I];
			I = M.Parts[I].Parent;
		}
		return T;
	}
}

namespace AcPoseAir
{
	FQuat QuatFromSceneKitEuler(const double X, const double Y, const double Z)
	{
		// SceneKit: q = qz · qy · qx (checked against SCNNode.simdOrientation).
		const FQuat Sk = FQuat(FVector(0, 0, 1), Z) * FQuat(FVector(0, 1, 0), Y) * FQuat(FVector(1, 0, 0), X);
		return AcSpace::QuatFromSceneKit(Sk.X, Sk.Y, Sk.Z, Sk.W);
	}

	TOptional<double> LaunchDrop(const double Age, const double Hover)
	{
		// Models.Spacedock.spawnPoint.y.
		constexpr double SpawnY = 1.59;
		const double A = Age / 1.4;
		if (A >= 1.0) return {};
		return (Hover - SpawnY) * (1.0 - Smoothstep(0.0, 1.0, A));
	}

	TOptional<double> RampOpening(const double Age)
	{
		const double A = Age / 1.5;
		if (A >= 1.0) return {};
		return Smoothstep(0.0, 0.2, A) * (1.0 - Smoothstep(0.7, 1.0, A));
	}

	void PoseDropship(const FAcModelInfo& M, FAcPose& P, const FDropship& In)
	{
		static const FPart Body(TEXT("body")), Origin(TEXT("beamOrigin")), Beacon(TEXT("beacon")),
			Beacon2(TEXT("beacon_2")), Ramp(TEXT("ramp")), Bay(TEXT("bay")), Shadow(TEXT("shadow")),
			Beam(TEXT("beam")), Shaft(TEXT("shaft")), Splash(TEXT("splash")),
			Pulse0(TEXT("pulse0")), Pulse1(TEXT("pulse1")), Pulse2(TEXT("pulse2"));
		static const FPart Engines[2] = {FPart(TEXT("engines_0")), FPart(TEXT("engines_1"))};
		static const FPart Fans[2] = {FPart(TEXT("fan")), FPart(TEXT("fan_2"))};
		static const FPart Glows[2] = {FPart(TEXT("glow")), FPart(TEXT("glow_2"))};
		static const FName BeaconMaterial(TEXT("glow_ff0d03")), CoreMaterial(TEXT("glow_bcfbbe"));

		const double Time = In.Time;
		const double T = Time + double(In.Id) * 2.37;
		const double B = FMath::Clamp(In.Bank, -1.0, 1.0);
		const double Open = FMath::Clamp(In.Unloading.Get(0.0), 0.0, 1.0);
		const bool bMoving = In.bMoving;
		P.bAnimated = true;

		// The body: hover with a slow bob, sink a little to unload; roll into
		// the turn (bank > 0 turns right, toward +Z), drift at the hover,
		// nose down in flight.
		const double Bob = 0.07 * std::sin(T * 1.3) + 0.025 * std::sin(T * 2.7 + 1.0);
		const int32 BodyI = Body.Get(M);
		SetSk(P, M, BodyI, FVector(0.0, DropshipHover + Bob - 0.35 * Smoothstep(0.0, 0.4, Open), 0.0),
			FVector(0.4 * B + (bMoving ? 0.0 : 0.035 * std::sin(T * 0.9)),
				bMoving ? 0.0 : 0.03 * std::sin(T * 0.43),
				(bMoving ? -0.1 : 0.02 * std::sin(T * 0.7)) + 0.012 * std::sin(T * 1.9)));

		// Engines: level at the hover, tilted forward in flight, the outer
		// one a touch more in a turn; fans spin; nozzles flicker.
		for (int32 I = 0; I < 2; ++I)
		{
			const double Side = I == 0 ? -1.0 : 1.0;
			const double Tilt = bMoving ? -0.42 - 0.12 * B * Side : 0.04 * std::sin(T * 1.1 + Side);
			SetRotation(P, Engines[I].Get(M), FVector(0.0, 0.0, Tilt));
			SetRotation(P, Fans[I].Get(M), FVector(Time * (bMoving ? 32.0 : 20.0) + Side, 0.0, 0.0));
			const double Flicker = 0.12 * std::sin(Time * 37.0 + Side * 3.0) * std::sin(Time * 13.0);
			SetEmission(P, Glows[I].Get(M), ((bMoving ? 3.2 : 1.7) + Flicker) / DropshipGlowExported);
		}

		// Beacons (one red copy per ship: the nose pair and the cross and
		// claw lamps merged into the body) blink twice every ~2 s.
		const double Beacons = BeaconIntensity(T, 0.55) / BeaconExported;
		SetEmission(P, Beacon.Get(M), Beacons);
		SetEmission(P, Beacon2.Get(M), Beacons);
		if (BodyI != INDEX_NONE) P.MeshEmission.Add({BodyI, BeaconMaterial, float(Beacons)});

		// Ramp and bay light.
		SetRotation(P, Ramp.Get(M), FVector(0.0, 0.0, RampOpen * Smoothstep(0.0, 1.0, Open)));
		SetEmission(P, Bay.Get(M), (0.15 + 1.6 * Open) / BayExported);

		// Ground shadow: smaller as the ship rises (its opacity, 0.4 + 0.1
		// open in Swift, is left as exported).
		ScaleShadow(P, M, Shadow.Get(M), 1.0 - 0.12 * Bob / 0.1 + 0.1 * Open);

		// Heal beam, in the root's space: from the lens under the chin to
		// the patient's chest.
		const int32 OriginI = Origin.Get(M), BeamI = Beam.Get(M);
		auto NoBeam = [&]
		{
			if (BeamI != INDEX_NONE) P.Visible[BeamI] = false;
			SetEmission(P, OriginI, 0.8 / LensExported);
		};
		if (!In.Healing || OriginI == INDEX_NONE || BeamI == INDEX_NONE)
		{
			NoBeam();
			return;
		}
		const FVector Org = ToRoot(P, M, OriginI).GetLocation();
		const FVector Tgt = (P.Local[0] * P.Placement).InverseTransformPosition(*In.Healing);
		const FVector D = Tgt - Org;
		const double Len = AcSpace::ToCells(D.Size());
		if (Len <= 0.05)
		{
			NoBeam();
			return;
		}
		// The beam is built along SceneKit +Y (Unreal +Z) from its origin.
		P.Visible[BeamI] = true;
		P.Local[BeamI] = FTransform(FQuat::FindBetweenNormals(FVector::UpVector, D.GetSafeNormal()), Org);
		const double Pulse = 0.5 + 0.5 * std::sin(Time * 9.0 + double(In.Id));
		const double W = 0.85 + 0.3 * Pulse;
		const double Core = (1.6 + 1.2 * Pulse) / BeamCoreExported;
		if (const int32 I = Shaft.Get(M); I != INDEX_NONE)
		{
			P.Local[I].SetScale3D(AcSpace::AxesFromSceneKit(W, Len, W));
			P.MeshEmission.Add({I, CoreMaterial, float(Core)});
		}
		const FPart* Pulses[3] = {&Pulse0, &Pulse1, &Pulse2};
		for (int32 K = 0; K < 3; ++K)
		{
			const int32 I = Pulses[K]->Get(M);
			if (I == INDEX_NONE) continue;
			const double F = Fract(Time * 1.4 + double(K) / 3.0);
			const double S = std::sin(F * UE_DOUBLE_PI);
			P.Local[I] = FTransform(FQuat::Identity, AcSpace::FromSceneKit(0.0, F * Len, 0.0),
				AcSpace::AxesFromSceneKit(S, 1.8 * S, S));
			P.Emission[I] = float(Core);
		}
		if (const int32 I = Splash.Get(M); I != INDEX_NONE)
		{
			const double S = 0.8 + 0.35 * Pulse;
			P.Local[I] = FTransform(QuatFromSceneKitEuler(0.0, Time * 2.0, 0.0), AcSpace::FromSceneKit(0.0, Len, 0.0),
				FVector(S));
			P.MeshEmission.Add({I, CoreMaterial, float(Core)});
		}
		SetEmission(P, OriginI, (2.5 + 1.5 * Pulse) / LensExported);
	}

	void PoseKestrel(const FAcModelInfo& M, FAcPose& P, const FKestrel& In)
	{
		static const FPart Body(TEXT("body")), Shadow(TEXT("shadow"));
		static const FPart Engines[2] = {FPart(TEXT("engines_0")), FPart(TEXT("engines_1"))};
		static const FPart Glows[2] = {FPart(TEXT("glow")), FPart(TEXT("glow_2"))};
		static const FPart Pods[2] = {FPart(TEXT("pods_0")), FPart(TEXT("pods_1"))};
		static const FPart Flashes[2] = {FPart(TEXT("flashes_0")), FPart(TEXT("flashes_1"))};
		static const FName BeaconMaterial(TEXT("beacon"));

		const double Time = In.Time;
		const double T = Time + double(In.Id) * 1.91;
		const double B = FMath::Clamp(In.Bank, -1.0, 1.0);
		const bool bMoving = In.bMoving;
		P.bAnimated = true;

		// A quick bob; roll into turns, nose down in flight.
		const double Bob = 0.05 * std::sin(T * 1.7) + 0.02 * std::sin(T * 3.1 + 1.0);
		const int32 BodyI = Body.Get(M);
		SetSk(P, M, BodyI, FVector(0.0, KestrelHover + Bob, 0.0),
			FVector(0.5 * B + (bMoving ? 0.0 : 0.03 * std::sin(T * 1.1)), 0.0,
				(bMoving ? -0.14 : 0.02 * std::sin(T * 0.8)) + 0.01 * std::sin(T * 2.3)));
		for (int32 I = 0; I < 2; ++I)
		{
			const double Side = I == 0 ? -1.0 : 1.0;
			SetRotation(P, Engines[I].Get(M),
				FVector(0.0, 0.0, bMoving ? -0.3 - 0.1 * B * Side : 0.05 * std::sin(T * 1.3 + Side)));
			SetEmission(P, Glows[I].Get(M),
				((bMoving ? 3.0 : 1.6) + 0.15 * std::sin(Time * 41.0 + Side * 3.0) * std::sin(Time * 11.0)) / KestrelGlowExported);
		}
		// The wing-tip beacons (merged into the body) blink.
		if (BodyI != INDEX_NONE) P.MeshEmission.Add({BodyI, BeaconMaterial, float(BeaconIntensity(T, 0.6) / BeaconExported)});

		// The pods fire one after the other: the left at once, the right a
		// beat later; each kicks back on its rail and flashes.
		const double Since = In.Shot ? Time - *In.Shot : 10.0;
		for (int32 K = 0; K < 2; ++K)
		{
			const double S = Since - double(K) * 0.08;
			const double Kick = (S >= 0.0 && S < 0.15) ? 1.0 - S / 0.15 : 0.0;
			if (const int32 I = Pods[K].Get(M); I != INDEX_NONE)
			{
				const FVector& Rest = M.Parts[I].SkPosition;
				P.Local[I].SetTranslation(AcSpace::FromSceneKit(PodX - 0.07 * Kick, Rest.Y, Rest.Z));
			}
			if (const int32 I = Flashes[K].Get(M); I != INDEX_NONE) P.Visible[I] = S >= 0.0 && S < 0.06;
		}
		ScaleShadow(P, M, Shadow.Get(M), 1.0 - 0.12 * Bob / 0.07);
	}
}

namespace
{
	/// How far below its hover a flyer just out of its Spacedock still is
	/// (`GameScene.liftOff`); forgets `BornAt` once it is up.
	double LiftOff(const FAcPoseContext& C, const double Hover)
	{
		if (!C.Memory.BornAt) return 0.0;
		if (const TOptional<double> Drop = AcPoseAir::LaunchDrop(C.Time - *C.Memory.BornAt, Hover)) return *Drop;
		C.Memory.BornAt.Reset();
		return 0.0;
	}

	const ac::Unit* FindUnit(const ac::GameState& S, const int64 Id)
	{
		for (const ac::Unit& U : S.units)
		{
			if (U.id == Id) return &U;
		}
		return nullptr;
	}

	/// GameScene.poseDropship (:832). The placement is already over
	/// `flightBase` (AcPose::RestUnit).
	void PoseDropshipUnit(const FAcPoseContext& C, FAcPose& P)
	{
		const ac::Unit& U = *C.Unit;
		const double Lift = LiftOff(C, AcPoseAir::DropshipHover);
		P.Placement.AddToTranslation(FVector(0.0, 0.0, -AcSpace::ToCm(Lift)));

		AcPoseAir::FDropship In;
		In.Id = U.id;
		In.Time = C.Time;
		In.bMoving = U.walking();
		In.Bank = AcPose::Turning(C) / 2.5;
		// Healing: the beam at its patient's chest (`chest`, :1175).
		if (U.task == ac::Unit::Task::attacking && U.target)
		{
			if (const ac::Unit* V = FindUnit(C.State, *U.target))
			{
				const double Chest = V->kind == ac::UnitKind::juggernaut ? 0.85 : 0.62;
				In.Healing = AcSpace::ToWorld(V->position, C.Ground(V->position) + Chest);
			}
		}
		// The ramp opens after it sets its load down.
		const int32 N = U.cargo ? (int32)U.cargo->size() : 0;
		if (C.Memory.CargoCount && N < *C.Memory.CargoCount) C.Memory.UnloadedAt = C.Time;
		C.Memory.CargoCount = N;
		if (C.Memory.UnloadedAt)
		{
			In.Unloading = AcPoseAir::RampOpening(C.Time - *C.Memory.UnloadedAt);
			if (!In.Unloading) C.Memory.UnloadedAt.Reset();
		}
		AcPoseAir::PoseDropship(C.Model, P, In);
	}

	/// GameScene.sync's Kestrel case (:718).
	void PoseKestrelUnit(const FAcPoseContext& C, FAcPose& P)
	{
		const ac::Unit& U = *C.Unit;
		const double Lift = LiftOff(C, AcPoseAir::KestrelHover);
		P.Placement.AddToTranslation(FVector(0.0, 0.0, -AcSpace::ToCm(Lift)));

		AcPoseAir::FKestrel In;
		In.Id = U.id;
		In.Time = C.Time;
		In.bMoving = U.walking();
		In.Bank = AcPose::Turning(C) / 2.5;
		In.Shot = C.Memory.LastShot;
		AcPoseAir::PoseKestrel(C.Model, P, In);
	}

	FAcPoseRegistration DropshipReg(ac::UnitKind::dropship, &PoseDropshipUnit);
	FAcPoseRegistration KestrelReg(ac::UnitKind::kestrel, &PoseKestrelUnit);
}
