#include "AcPoseVehicles.h"

#include "AcModelCatalog.h"
#include "AcPilotAim.h"
#include "AcPose.h"
#include "AcSpace.h"

#include "Misc/ScopeRWLock.h"

#include <cmath>

namespace
{
	constexpr double Pi = UE_DOUBLE_PI;

	double Smoothstep(const double A, const double B, const double X)
	{
		const double T = FMath::Clamp((X - A) / (B - A), 0.0, 1.0);
		return T * T * (3.0 - 2.0 * T);
	}

	double Rem2Pi(const double A) { return std::remainder(A, 2.0 * Pi); }

	/// SceneKit `eulerAngles` → the node's quaternion in SceneKit space
	/// (R = Rz·Ry·Rx, checked against the export's matrices).
	FQuat SkEuler(const FVector& E)
	{
		return FQuat(FVector(0, 0, 1), E.Z) * FQuat(FVector(0, 1, 0), E.Y) * FQuat(FVector(1, 0, 0), E.X);
	}

	/// The export merged nodes without handles into their parents, so a
	/// part's rest (relative to its exported parent) can be its own
	/// SceneKit transform under a few vanished nodes (the beetle Comet's
	/// `upper`, a Firefly's rear wheel mount): Rest = Own × Chain. The
	/// chain, per part, once per model.
	struct FChains
	{
		TArray<FMatrix> Chain;
		TArray<bool> bIdentity;
	};

	const FChains& ChainsFor(const FAcModelInfo& M)
	{
		static FRWLock Lock;
		static TMap<const FAcModelInfo*, TUniquePtr<FChains>> Map;
		{
			FReadScopeLock Read(Lock);
			if (const TUniquePtr<FChains>* Found = Map.Find(&M)) return **Found;
		}
		FWriteScopeLock Write(Lock);
		if (const TUniquePtr<FChains>* Found = Map.Find(&M)) return **Found;
		TUniquePtr<FChains> C = MakeUnique<FChains>();
		for (const FAcModelPart& Part : M.Parts)
		{
			const FTransform Own = AcSpace::TransformFromSceneKit(Part.SkPosition, SkEuler(Part.SkEuler), Part.SkScale);
			const FMatrix Chain = Own.ToMatrixWithScale().Inverse() * Part.Rest.ToMatrixWithScale();
			C->Chain.Add(Chain);
			C->bIdentity.Add(Chain.Equals(FMatrix::Identity, 1e-3));
		}
		const FChains& Out = *C;
		Map.Add(&M, MoveTemp(C));
		return Out;
	}

	/// A SceneKit node's own values (position in cells, eulerAngles or an
	/// orientation, scale), starting from the part's rest, as the Swift pose
	/// code sets them.
	struct FSkNode
	{
		FVector Pos = FVector::ZeroVector;
		FVector Euler = FVector::ZeroVector;
		FVector Scale = FVector::OneVector;
		TOptional<FQuat> Orientation;

		FQuat Rotation() const { return Orientation ? *Orientation : SkEuler(Euler); }
	};

	FSkNode Rest(const FAcModelInfo& M, const int32 I)
	{
		const FAcModelPart& P = M.Parts[I];
		return {P.SkPosition, P.SkEuler, P.SkScale, {}};
	}

	/// The part's own transform in Unreal space → its local (through the chain).
	void ApplyOwn(const FAcModelInfo& M, FAcPose& Pose, const int32 I, const FTransform& Own)
	{
		const FChains& C = ChainsFor(M);
		Pose.Local[I] = C.bIdentity[I] ? Own : FTransform(Own.ToMatrixWithScale() * C.Chain[I]);
	}

	void Apply(const FAcModelInfo& M, FAcPose& Pose, const int32 I, const FSkNode& N)
	{
		if (I == INDEX_NONE) return;
		ApplyOwn(M, Pose, I, AcSpace::TransformFromSceneKit(N.Pos, N.Rotation(), N.Scale));
	}

	/// The part's local transform relative to the model root (the root's own
	/// transform left out: `convertPosition(_, to: root)`).
	FTransform ToRoot(const FAcModelInfo& M, const FAcPose& Pose, int32 I)
	{
		FTransform T = Pose.Local[I];
		for (I = M.Parts[I].Parent; I != INDEX_NONE && M.Parts[I].Parent != INDEX_NONE; I = M.Parts[I].Parent)
		{
			T = T * Pose.Local[I];
		}
		return T;
	}

	void SetVisible(FAcPose& P, const int32 I, const bool bVisible)
	{
		if (I != INDEX_NONE) P.Visible[I] = bVisible;
	}

	void SetEmission(FAcPose& P, const int32 I, const double E)
	{
		if (I != INDEX_NONE) P.Emission[I] = float(E);
	}

	void SetMeshEmission(FAcPose& P, const int32 I, const FName Material, const double E)
	{
		if (I != INDEX_NONE) P.MeshEmission.Add({I, Material, float(E)});
	}

	struct FVehicleState : FAcPoseState
	{
		FAcVehicleFx Fx;
	};

	struct FFireflyState : FVehicleState
	{
		/// `FireflyRoll`: the wheels' drawn roll, carried frame to frame.
		double Front = 0.0, Rear = 0.0, Smear = 0.0;
		TOptional<double> Stride;
		TOptional<ac::Vec2> Position;
		/// The shot `FlameLength` was last measured for.
		TOptional<double> SeenShot;
	};

	/// Aiming as `GameScene.sync` decides it (GameScene.swift:665): the
	/// driven unit for 1.5 s after a shot, the rest while they stand and
	/// fight. Driven with a crosshair, its guns are held up on it (`held`).
	bool Aiming(const FAcPoseContext& C)
	{
		const ac::Unit& U = *C.Unit;
		if (C.bDriven) return C.PilotAim || (C.Memory.LastShot.IsSet() && C.Time - *C.Memory.LastShot < 1.5);
		return U.task == ac::Unit::Task::attacking && !U.walking();
	}

	// MARK: Comet ---------------------------------------------------------

	void PoseComet(const FAcPoseContext& C, FAcPose& P)
	{
		static const FAcPartIndex Body(TEXT("body")), Head(TEXT("head"));
		static const FAcPartIndex Legs[2] = {FAcPartIndex(TEXT("legs_0")), FAcPartIndex(TEXT("legs_1"))};
		static const FAcPartIndex Knees[2] = {FAcPartIndex(TEXT("knees_0")), FAcPartIndex(TEXT("knees_1"))};
		static const FAcPartIndex Guns[2] = {FAcPartIndex(TEXT("guns_0")), FAcPartIndex(TEXT("guns_1"))};
		static const FAcPartIndex Flashes[2] = {FAcPartIndex(TEXT("flashes_0")), FAcPartIndex(TEXT("flashes_1"))};
		static const FAcPartIndex Flames[2] = {FAcPartIndex(TEXT("flames_0")), FAcPartIndex(TEXT("flames_1"))};
		static const FAcPartIndex Halos[2] = {FAcPartIndex(TEXT("halos_0")), FAcPartIndex(TEXT("halos_1"))};
		static const FAcPartIndex Wings[2] = {FAcPartIndex(TEXT("wings_0")), FAcPartIndex(TEXT("wings_1"))};
		static const FName JetGlow(TEXT("jetGlow")), FlameGlow(TEXT("flameGlow")), EyeGlow(TEXT("eyeGlow"));

		const FAcModelInfo& M = C.Model;
		const ac::Unit& U = *C.Unit;
		FVehicleState& S = C.Memory.State<FVehicleState>();
		P.bAnimated = true;

		const double Time = C.Time;
		const bool bWalking = U.walking();
		const bool bAiming = Aiming(C);
		const TOptional<double> Shot = C.Memory.LastShot;

		const double T = Time + double(U.id) * 1.37;
		const double Seed = double((U.id * 7919) % 97) / 97.0;
		const double Phase = U.stride * 4.6;
		// Jump envelope: 0 on the ground, 1 through the flight.
		TOptional<double> J;
		if (const std::optional<double> Jump = U.jump()) J = FMath::Clamp(*Jump, 0.0, 1.0);
		const double Tuck = J ? Smoothstep(0, 0.14, *J) * (1 - Smoothstep(0.82, 1, *J)) : 0.0;
		const double Lift = J ? 1.6 * std::sin(Pi * *J) : 0.0;
		const double Run = bWalking && !J ? 1.0 : 0.0;
		const double Bob = Run * std::abs(std::sin(U.stride * 4.6)) * 0.035;  // Models.Comet.bob
		// The placement is already at `flightY` (AcPose::RestUnit).
		P.Placement.AddToTranslation(FVector(0, 0, AcSpace::ToCm(Lift + Bob)));

		// Legs: a long running stride with the knee folding on the swing;
		// in flight both tuck up under the body.
		for (int32 I = 0; I < 2; ++I)
		{
			const double Ph = Phase + double(I) * Pi;
			const double Swing = Run * std::sin(Ph) * 0.8;
			const double KneeRun = Run * (0.2 + 0.85 * FMath::Max(0.0, std::cos(Ph)));
			const double IdleKnee = (1 - Run) * 0.08;
			if (const int32 Hip = Legs[I].Get(M); Hip != INDEX_NONE)
			{
				FSkNode N = Rest(M, Hip);
				N.Euler.Z = Swing * (1 - Tuck) + Tuck * (0.95 + 0.1 * double(I));
				Apply(M, P, Hip, N);
			}
			if (const int32 Knee = Knees[I].Get(M); Knee != INDEX_NONE)
			{
				FSkNode N = Rest(M, Knee);
				N.Euler.Z = -(KneeRun + IdleKnee) * (1 - Tuck) - Tuck * 1.55;
				Apply(M, P, Knee, N);
			}
		}

		// Body: leans into the run, breathes at ease, pitches forward in flight.
		const double Lean = -0.3 * Run * (bAiming ? 0.5 : 1.0) - 0.4 * Tuck;
		const int32 BodyI = Body.Get(M);
		if (BodyI != INDEX_NONE)
		{
			FSkNode N = Rest(M, BodyI);
			N.Euler.Z = Lean + (1 - Run) * (1 - Tuck) * 0.02 * std::sin(T * 1.7);
			N.Euler.X = Run * std::sin(Phase) * 0.06;
			N.Pos.Y = 0.47 - Run * 0.025 + (1 - Run) * 0.005 * std::sin(T * 1.9);  // Models.Comet.hipY
			Apply(M, P, BodyI, N);
		}
		if (const int32 HeadI = Head.Get(M); HeadI != INDEX_NONE)
		{
			FSkNode N = Rest(M, HeadI);
			N.Euler.Y = bWalking || bAiming || J ? 0.0 : 0.45 * std::sin(T * 0.37 + Seed * 6);
			N.Euler.Z = -Lean * 0.6;
			Apply(M, P, HeadI, N);
			// The glowing eyes of the mask (the beetle's: 1.5 ± 0.3).
			SetMeshEmission(P, HeadI, EyeGlow, (1.5 + 0.3 * std::sin(T * 0.8 + Seed * 5)) / 1.6);
		}

		// Pistols: hanging loose at ease, forward on the run, level to aim,
		// out wide for balance in flight. Each kicks up after its shot.
		const double Since = Shot ? Time - *Shot : 10.0;
		for (int32 I = 0; I < 2; ++I)
		{
			const double Side = I == 0 ? 1.0 : -1.0;
			const double Fire = Since - double(I) * 0.06;
			const double Kick = Fire >= 0 ? FMath::Max(0.0, 1 - Fire / 0.14) : 0.0;
			const double Idle = 0.22 + 0.04 * std::sin(T * 1.3 + double(I));
			const double Running = 0.75 - 0.25 * std::sin(Phase + double(I) * Pi);
			double Pitch = bAiming ? Pi / 2 - Lean : Run > 0 ? Running : Idle;
			Pitch = Pitch * (1 - Tuck) + Tuck * 0.9;
			if (const int32 Gun = Guns[I].Get(M); Gun != INDEX_NONE)
			{
				FSkNode N = Rest(M, Gun);
				N.Euler.Z = Pitch + Kick * 0.35;
				N.Euler.X = -Side * (Tuck * 0.5 + (bAiming ? -0.05 : 0.12));
				// Driven, both pistols on what the crosshair is on (E4, `aimParts`).
				if (!J)
				{
					const FQuat Q = N.Rotation();
					N.Orientation = AcPilotAim::Held(C, P, Gun, N.Pos, Q, N.Scale, Flashes[I].Get(M), FVector(0, -1, 0)) * Q;
				}
				Apply(M, P, Gun, N);
			}
			if (const int32 Flash = Flashes[I].Get(M); Flash != INDEX_NONE)
			{
				SetVisible(P, Flash, Fire >= 0 && Fire < 0.05);
				FSkNode N = Rest(M, Flash);
				N.Euler.Y = Fire * 97;
				Apply(M, P, Flash, N);
			}
		}

		// Jetpack: a flickering pilot at ease, a stronger idle on the run,
		// a full blast through the jump (hardest at take-off and landing).
		const double Flicker = 0.5 + 0.5 * std::sin(T * 23) * std::sin(T * 7.3 + Seed * 9);
		double Thrust = 0.08 + 0.06 * Flicker + Run * 0.1;
		if (J) Thrust = 0.75 + 0.25 * std::abs(std::cos(Pi * *J)) + 0.08 * Flicker;
		SetMeshEmission(P, BodyI, JetGlow, (0.5 + 2.6 * Thrust) / 0.8);
		for (int32 I = 0; I < 2; ++I)
		{
			S.Fx.JetRate[I] = float(J ? 260 * Thrust : 6 + 18 * Flicker + Run * 16);
			const double Len = J ? Thrust * (0.85 + 0.3 * std::sin(T * 31 + double(I) * 2)) : 0.18 + 0.12 * Flicker + Run * 0.15;
			if (const int32 F = Flames[I].Get(M); F != INDEX_NONE)
			{
				SetVisible(P, F, Len >= 0.05);
				FSkNode N = Rest(M, F);
				N.Scale = FVector(1, Len, 1);
				Apply(M, P, F, N);
				SetMeshEmission(P, F, FlameGlow, (1.4 + 1.2 * Flicker) / 2.2);
			}
			if (const int32 H = Halos[I].Get(M); H != INDEX_NONE)
			{
				// A soft camera-facing glow (M_AcSprite faces the camera);
				// additive, so its opacity is its emission.
				SetVisible(P, H, Len >= 0.05);
				SetEmission(P, H, FMath::Clamp((Len - 0.12) * 1.4, 0.0, 1.0));
				const double Hs = 0.45 + 0.8 * Len;
				FSkNode N = Rest(M, H);
				N.Scale = FVector(Hs);
				Apply(M, P, H, N);
			}
		}
		S.Fx.JetVelocity = J ? 3.2f : 1.2f;

		// Wing cases: closed on the ground, lifted a little on the run; they
		// snap open sideways at take-off, buzz in flight, and fold for the
		// landing.
		const double Open = J ? Smoothstep(0, 0.1, *J) * (1 - Smoothstep(0.8, 0.97, *J)) : 0.0;
		const double Spread = Open * (1 + 0.04 * std::sin(T * 47)) + Run * 0.06;
		for (int32 I = 0; I < 2; ++I)
		{
			const int32 W = Wings[I].Get(M);
			if (W == INDEX_NONE) continue;
			const double Side = I == 0 ? 1.0 : -1.0;
			FSkNode N = Rest(M, W);
			N.Orientation = FQuat(FVector(1, 0, 0), -Side * 1.25 * Spread) * FQuat(FVector(0, 0, 1), 0.3 * Spread)
				* SkEuler(M.Parts[W].SkEuler);
			Apply(M, P, W, N);
		}
	}

	// MARK: Juggernaut ----------------------------------------------------

	void PoseJuggernaut(const FAcPoseContext& C, FAcPose& P)
	{
		static const FAcPartIndex Body(TEXT("body")), Head(TEXT("head"));
		static const FAcPartIndex Legs[2] = {FAcPartIndex(TEXT("legs_0")), FAcPartIndex(TEXT("legs_1"))};
		static const FAcPartIndex Shins[2] = {FAcPartIndex(TEXT("shin")), FAcPartIndex(TEXT("shin_2"))};
		static const FAcPartIndex Launchers[2] = {FAcPartIndex(TEXT("launchers_0")), FAcPartIndex(TEXT("launchers_1"))};
		static const FAcPartIndex Flashes[2] = {FAcPartIndex(TEXT("flashes_0")), FAcPartIndex(TEXT("flashes_1"))};
		// The gorilla's (Models+JuggernautGorilla.swift:220): elbow, hang, and
		// a helmet set into the chest that turns with the torso.
		const FVector Elbow(0.1, 0.64, 0.53);
		constexpr double Hang = -0.85;
		constexpr double HeadTurn = 0.0;

		const FAcModelInfo& M = C.Model;
		const ac::Unit& U = *C.Unit;
		FVehicleState& S = C.Memory.State<FVehicleState>();
		P.bAnimated = true;

		const double Time = C.Time;
		const bool bWalking = U.walking();
		const bool bAiming = Aiming(C);

		// The torso turns to what it shoots at (the unit may fire still a
		// little off it) or last shot at. Driven, it shoots where it faces.
		double Twist = 0.0;
		if (!C.bDriven)
		{
			TOptional<ac::Vec2> AimAt;
			// The frame a volley leaves (the renderer stamps the shot before
			// the poses, the effects fire after them): brought up on the
			// shot's point this instant, as `GameScene.fire` re-poses it, so
			// the grenades leave the bores.
			if (C.Memory.LastShot && *C.Memory.LastShot >= Time && C.Memory.LastShotAt) AimAt = *C.Memory.LastShotAt;
			else if (U.target)
			{
				if (const std::optional<ac::Unit> V = C.State.unit(*U.target)) AimAt = V->position;
				else if (const std::optional<ac::Structure> B = C.State.structure(*U.target)) AimAt = B->position;
			}
			// `lastAim`: where it last fired (set by the effects, GameScene.fire);
			// the shot's point until they have.
			if (!AimAt && C.Memory.LastAim) AimAt = *C.Memory.LastAim;
			if (!AimAt && C.Memory.LastShotAt) AimAt = *C.Memory.LastShotAt;
			if (AimAt)
			{
				Twist = Rem2Pi(std::atan2(AimAt->y - U.position.y, AimAt->x - U.position.x) - U.heading);
			}
		}

		const double T = Time + double(U.id) * 1.37;
		// Stride phase: one full cycle is two steps.
		const double Ph = U.stride * 2.7;
		const double Since = C.Memory.LastShot ? Time - *C.Memory.LastShot : 10.0;
		const bool bFresh = Since >= 0 && Since < 1.07;
		// Recoil: a hard kick that settles over a quarter second.
		const double Kick = bFresh ? std::exp(-Since * 11) : 0.0;
		const double Settle = bFresh ? std::exp(-Since * 5) * std::sin(Since * 20) : 0.0;

		// Heavy stomp: highest mid-stance, a sharp drop onto each foot.
		const double Stance = std::abs(std::cos(Ph));
		const double Bob = bWalking ? 0.05 * Stance - 0.012 * std::exp(-8 * (1 - Stance)) : 0.0;
		P.Placement.AddToTranslation(FVector(0, 0, AcSpace::ToCm(Bob)));

		for (int32 I = 0; I < 2; ++I)
		{
			const double Q = Ph + double(I) * Pi;
			const double Swing = bWalking ? std::sin(Q) * 0.5 : 0.0;
			if (const int32 Hip = Legs[I].Get(M); Hip != INDEX_NONE)
			{
				FSkNode N = Rest(M, Hip);
				N.Euler.Z = Swing;
				Apply(M, P, Hip, N);
			}
			// The knee folds while the leg swings forward, then plants.
			const double LiftK = bWalking ? FMath::Max(0.0, std::cos(Q)) : 0.0;
			if (const int32 Shin = Shins[I].Get(M); Shin != INDEX_NONE)
			{
				FSkNode N = Rest(M, Shin);
				N.Euler.Z = -0.75 * LiftK * LiftK - Swing * 0.35;
				Apply(M, P, Shin, N);
			}
		}

		const double Breathe = std::sin(T * 1.6);
		// Up to aim when standing to shoot, and for a moment after any
		// volley (it can fire on the move), so grenades leave level bores.
		const double Aim = (bAiming && !bWalking) || (Since >= 0 && Since < 0.6) ? 1.0 : 0.0;
		const double Turn = Aim > 0 ? FMath::Clamp(Twist, -1.0, 1.0) : 0.0;
		const double Look = bWalking || bAiming ? 0.0 : 0.45 * std::sin(T * 0.29) * Smoothstep(0.2, 0.8, std::sin(T * 0.11));
		if (const int32 B = Body.Get(M); B != INDEX_NONE)
		{
			// Torso: rolls over the planted leg when walking; leans in to
			// aim; kicks back with the volley.
			FSkNode N = Rest(M, B);
			N.Pos.Y = bWalking ? 0.0 : 0.008 * Breathe - 0.02 * Aim;
			N.Pos.X = -0.035 * Kick;
			N.Euler.X = bWalking ? std::sin(Ph) * 0.06 : 0.015 * std::sin(T * 0.37);
			N.Euler.Y = (bWalking ? std::sin(Ph) * 0.07 : (1 - HeadTurn) * 0.35 * Look) - Turn;
			N.Euler.Z = -0.06 * Aim + (bWalking ? -0.05 : 0.0) + 0.12 * Kick + 0.02 * Settle;
			Apply(M, P, B, N);
		}
		if (const int32 H = Head.Get(M); H != INDEX_NONE)
		{
			FSkNode N = Rest(M, H);
			N.Euler.Y = HeadTurn * Look;
			N.Euler.Z = -0.08 * Kick * HeadTurn;
			Apply(M, P, H, N);
		}

		// Launchers: hanging and slightly apart at ease, swinging against
		// the legs when walking, level when aiming; the volley throws them up.
		for (int32 I = 0; I < 2; ++I)
		{
			const int32 Gun = Launchers[I].Get(M);
			if (Gun == INDEX_NONE) continue;
			const double Sz = I == 0 ? 1.0 : -1.0;
			const double ArmSwing = bWalking ? -std::sin(Ph + double(I) * Pi) * 0.22 : 0.0;
			const double RestPitch = Hang + 0.03 * Breathe + 0.02 * std::sin(T * 0.7 + double(I));
			const double Pitch = Aim > 0 ? 0.04 : bWalking ? Hang + 0.3 + ArmSwing : RestPitch;
			FSkNode N = Rest(M, Gun);
			N.Euler.Z = Pitch + 0.5 * Kick + 0.04 * Settle;
			N.Euler.X = Aim > 0 ? 0.0 : 0.06 * Sz;
			N.Pos.X = Elbow.X - 0.07 * Kick;
			N.Pos.Y = Elbow.Y + (bWalking ? 0.0 : 0.006 * Breathe);
			// Driven, both launchers on what the crosshair is on (E4, `aimParts`).
			{
				static const FAcPartIndex Muzzles[2] = {FAcPartIndex(TEXT("muzzles_0")), FAcPartIndex(TEXT("muzzles_1"))};
				const FQuat Q = N.Rotation();
				N.Orientation = AcPilotAim::Held(C, P, Gun, N.Pos, Q, N.Scale, Muzzles[I].Get(M), FVector(1, 0, 0)) * Q;
			}
			Apply(M, P, Gun, N);
		}

		const bool bFlash = Since >= 0 && Since < 0.08;
		for (int32 I = 0; I < 2; ++I)
		{
			const int32 F = Flashes[I].Get(M);
			if (F == INDEX_NONE) continue;
			SetVisible(P, F, bFlash);
			FSkNode N = Rest(M, F);
			N.Scale = FVector(0.8 + Since * 8);
			N.Euler.X = Since * 97;
			Apply(M, P, F, N);
		}
		S.Fx.MuzzleSmokeRate = Since >= 0 && Since < 0.14 ? 110.f : 0.f;
	}

	// MARK: Firefly -------------------------------------------------------

	void PoseFirefly(const FAcPoseContext& C, FAcPose& P)
	{
		static const FAcPartIndex Body(TEXT("body")), Turret(TEXT("turret")), Nozzle(TEXT("nozzle"));
		static const FAcPartIndex Pilot(TEXT("pilot")), Flame(TEXT("flame")), FlameEnd(TEXT("flameEnd"));
		static const FAcPartIndex Flare(TEXT("flare")), FlameLight(TEXT("flameLightNode"));
		static const FAcPartIndex Layers[3] = {
			FAcPartIndex(TEXT("flameLayers_0")), FAcPartIndex(TEXT("flameLayers_1")), FAcPartIndex(TEXT("flameLayers_2"))};
		static const FAcPartIndex Wheels[4] = {FAcPartIndex(TEXT("wheels_0")), FAcPartIndex(TEXT("wheels_1")),
			FAcPartIndex(TEXT("wheels_2")), FAcPartIndex(TEXT("wheels_3"))};
		static const FAcPartIndex Smears[4] = {FAcPartIndex(TEXT("smears_0")), FAcPartIndex(TEXT("smears_1")),
			FAcPartIndex(TEXT("smears_2")), FAcPartIndex(TEXT("smears_3"))};
		static const FAcPartIndex Steers[2] = {FAcPartIndex(TEXT("steers_0")), FAcPartIndex(TEXT("steers_1"))};
		static const FAcPartIndex Tail[6] = {FAcPartIndex(TEXT("tail_0")), FAcPartIndex(TEXT("tail_1")),
			FAcPartIndex(TEXT("tail_2")), FAcPartIndex(TEXT("tail_3")), FAcPartIndex(TEXT("tail_4")),
			FAcPartIndex(TEXT("tail_5"))};
		static const FName ExhaustGlow(TEXT("exhaustGlow")), HeatGlow(TEXT("heatGlow"));
		static const FName BallHot(TEXT("flameMaterials_3")), BallOuter(TEXT("flameMaterials_4"));
		// The scorpion's tail root (`spine[0]`), SceneKit.
		const FVector TailPivot(-0.36, 0.84, 0.0);
		constexpr double Burn = AcPoseVehicles::FireflyBurn;

		const FAcModelInfo& M = C.Model;
		const ac::Unit& U = *C.Unit;
		FFireflyState& S = C.Memory.State<FFireflyState>();
		P.bAnimated = true;

		const double Time = C.Time;
		const bool bMoving = U.walking();
		// Steers and leans with how fast it turns (up to ~4 rad/s).
		const double TurnIn = AcPose::Turning(C) / 4;
		// The tail turns on the hull to its aim, like a tank's turret.
		const double AimA = Rem2Pi(U.look() - U.heading);
		const TOptional<double> Firing = C.Memory.LastShot;

		// `GameScene.fire`: how far the jet reaches, measured once a shot, on
		// the frame it fires (the renderer stamps the shot before the poses;
		// the jet's head is still in the nozzle then). The effects (C1) then
		// measure it again with the simulation's weapon range (boosts of the
		// driven unit included) and their value holds from the next frame.
		if (Firing && !(S.SeenShot == Firing))
		{
			S.SeenShot = Firing;
			if (C.Memory.LastShotAt)
			{
				const double Reach = U.stats().range * (C.bDriven ? ac::Rules::heroRange : 1.0);
				C.Memory.FlameLength = FMath::Min(ac::distance(U.position, *C.Memory.LastShotAt),
					Reach + ac::Rules::radius(ac::UnitKind::firefly));
			}
		}
		const double FlameLength = C.Memory.FlameLength;

		const double Seed = double(U.id % 97) * 1.618;
		const double T = Time + Seed;

		// Wheels roll forward (top toward +X), back while it reverses. The
		// drawn turn is held to 0.35 of a lug a frame (16 alike lugs: more
		// and the eye pairs each with the wrong one and the wheel crawls or
		// runs backwards); a smear over the tyres shows the rest. The front
		// pair steers.
		const double Steer = FMath::Clamp(TurnIn, -1.0, 1.0);
		double Moved = S.Stride ? U.stride - *S.Stride : 0.0;
		if (S.Position && ac::dot(U.position - *S.Position, ac::Vec2(std::cos(U.heading), std::sin(U.heading))) < -1e-4)
		{
			Moved = -Moved;
		}
		S.Stride = U.stride;
		S.Position = U.position;
		const double Lug = 0.35 * 2 * Pi / double(AcPoseVehicles::FireflyLugs);
		auto Held = [Lug](const double A) { return FMath::Clamp(A, -Lug, Lug); };
		S.Front = Rem2Pi(S.Front - Held(Moved / AcPoseVehicles::FireflyFrontRadius));
		S.Rear = Rem2Pi(S.Rear - Held(Moved / AcPoseVehicles::FireflyRearRadius));
		for (int32 I = 0; I < 4; ++I)
		{
			if (const int32 W = Wheels[I].Get(M); W != INDEX_NONE)
			{
				FSkNode N = Rest(M, W);
				N.Euler.Z = I < 2 ? S.Front : S.Rear;
				Apply(M, P, W, N);
			}
		}
		S.Smear += (0.55 * Smoothstep(1, 3, std::abs(Moved / AcPoseVehicles::FireflyFrontRadius) / Lug) - S.Smear) * 0.3;
		for (int32 I = 0; I < 4; ++I)
		{
			// M_AcSmear reads custom data 1 as its opacity.
			const int32 Sm = Smears[I].Get(M);
			SetVisible(P, Sm, S.Smear >= 0.02);
			SetEmission(P, Sm, S.Smear);
		}
		for (int32 I = 0; I < 2; ++I)
		{
			if (const int32 St = Steers[I].Get(M); St != INDEX_NONE)
			{
				FSkNode N = Rest(M, St);
				N.Euler.Y = -Steer * 0.45;
				Apply(M, P, St, N);
			}
		}

		const double Since = Firing ? Time - *Firing : TNumericLimits<double>::Max();
		const bool bFinite = Firing.IsSet();
		const bool bBurning = Since >= 0 && Since < Burn;
		// Kick as the jet lights, easing off over the burn.
		const double Kick = bBurning ? Smoothstep(0, 0.05, Since) * (1 - Smoothstep(0.1, Burn, Since)) : 0.0;

		// Suspension: bumps from the road, an idle shiver, a roll into the
		// turn (leaning out), nose up against the flame's thrust.
		const double Bounce = bMoving
			? 0.016 * std::sin(U.stride * 4.3 + Seed) + 0.009 * std::sin(U.stride * 10.7 + Seed * 2) + 0.012
			: 0.003 * std::sin(T * 1.9);
		const double Shiver = (bMoving ? 0.002 : 0.0035) * std::sin(T * 57) + (bBurning ? 0.004 * std::sin(T * 83) : 0.0);
		const int32 BodyI = Body.Get(M);
		if (BodyI != INDEX_NONE)
		{
			FSkNode N = Rest(M, BodyI);
			N.Pos.Y = Bounce + Shiver - 0.012 * Kick;
			N.Euler.X = -Steer * (bMoving ? 0.075 : 0.02) + (bMoving ? 0.01 * std::sin(U.stride * 3.1 + Seed) : 0.0);
			N.Euler.Z = (bMoving ? 0.012 * std::sin(U.stride * 2.3 + Seed * 3) - 0.015 : 0.0) + 0.03 * Kick;
			Apply(M, P, BodyI, N);
		}
		// The tail and the stinger on it turn about the tail's root, a turn
		// of −aim about +Y; the flame's kick pushes the stinger back along it.
		const FVector PivotUe = AcSpace::FromSceneKit(TailPivot.X, TailPivot.Y, TailPivot.Z);
		const FQuat SkTurn(FVector(0, 1, 0), -AimA);
		const FTransform About = FTransform(-PivotUe) * FTransform(AcSpace::QuatFromSceneKit(SkTurn.X, SkTurn.Y, SkTurn.Z, SkTurn.W))
			* FTransform(PivotUe);
		for (int32 I = 0; I < 6; ++I)
		{
			const int32 K = Tail[I].Get(M);
			if (K == INDEX_NONE) continue;
			const FSkNode R = Rest(M, K);
			ApplyOwn(M, P, K, AcSpace::TransformFromSceneKit(R.Pos, R.Rotation(), R.Scale) * About);
		}
		const int32 TurretI = Turret.Get(M);
		if (TurretI != INDEX_NONE)
		{
			FSkNode R = Rest(M, TurretI);
			R.Pos.X -= 0.025 * Kick;
			ApplyOwn(M, P, TurretI, AcSpace::TransformFromSceneKit(R.Pos, R.Rotation(), R.Scale) * About);
		}

		// Exhaust: a steady stream driving, the odd puff at idle.
		S.Fx.ExhaustRate = bMoving ? 36.f : 3.f;
		SetMeshEmission(P, BodyI,
			ExhaustGlow, (bMoving ? 2.2 + 0.6 * std::sin(T * 23) : 0.9 + 0.2 * std::sin(T * 3)) / 1.8);

		// The nozzle heats with each burst and cools over the cooldown
		// (the lip's material is glow orange at intensity 1).
		const double Heat = bFinite && Since >= 0 ? FMath::Max(0.0, 1 - FMath::Max(0.0, Since - 0.1) / 1.5) : 0.0;
		SetMeshEmission(P, TurretI, HeatGlow, 2.6 * Heat * Heat);

		const int32 PilotI = Pilot.Get(M);
		SetVisible(P, PilotI, !bBurning);
		SetVisible(P, Flame.Get(M), bBurning);
		if (!bBurning)
		{
			const double Flick = 1 + 0.18 * std::sin(T * 31) * std::sin(T * 17 + 1);
			if (PilotI != INDEX_NONE)
			{
				FSkNode N = Rest(M, PilotI);
				N.Scale = FVector(Flick, 1 + 0.08 * std::sin(T * 23), 1 + 0.08 * std::sin(T * 23));
				Apply(M, P, PilotI, N);
				SetEmission(P, PilotI, (2.2 + 0.5 * std::sin(T * 13) * std::sin(T * 7)) / 2.4);
			}
			S.Fx.FireRate = S.Fx.EmberRate = S.Fx.SmokeRate = 0.f;
			S.Fx.FlameLight = 0.f;
			return;
		}

		// The jet: its head races out in 0.08 s, its tail leaves the nozzle
		// in the last 0.1 s, and it dims as it goes.
		const int32 NozzleI = Nozzle.Get(M);
		const FVector Nz = NozzleI != INDEX_NONE ? AcSpace::ToSceneKit(ToRoot(M, P, NozzleI).GetTranslation()) : FVector(0.12, 1.0, 0);
		const double Ahead = Nz.X * std::cos(AimA) + Nz.Z * std::sin(AimA);
		const double ReachTotal = FMath::Max(0.4, FlameLength - Ahead);
		const double Drop = FMath::Max(0.0, Nz.Y - 0.35);
		const double Pitch = std::atan2(Drop, ReachTotal);
		const double Full = std::sqrt(ReachTotal * ReachTotal + Drop * Drop);
		if (const int32 F = Flame.Get(M); F != INDEX_NONE)
		{
			FSkNode N = Rest(M, F);
			N.Euler.Z = -Pitch;
			Apply(M, P, F, N);
		}
		const double Head = Full * Smoothstep(0, 0.08, Since);
		const double TailX = Full * Smoothstep(Burn - 0.11, Burn, Since);
		const double Len = FMath::Max(0.01, Head - TailX);
		const double Fade = 1 - 0.7 * Smoothstep(Burn - 0.12, Burn, Since);
		const double Widen = 0.8 + 0.12 * FMath::Min(Full, 5.5);

		static constexpr double Spin[3] = {4.1, -2.7, 1.9};
		for (int32 I = 0; I < 3; ++I)
		{
			const int32 L = Layers[I].Get(M);
			if (L == INDEX_NONE) continue;
			const double Fl = 1 + 0.14 * std::sin(T * (41 + double(I) * 13)) * std::sin(T * (23 + double(I) * 7) + double(I));
			const double W = Widen * Fl * (1 + 0.25 * (1 - Smoothstep(0, 0.12, Since)));
			FSkNode N = Rest(M, L);
			N.Pos.X = TailX;
			N.Scale = FVector(Len, W, W * (1 + 0.1 * std::sin(T * 29 + double(I))));
			N.Euler.X = T * Spin[I];
			Apply(M, P, L, N);
			// The licks' scroll is the material's (M_AcFlame, by time).
			SetEmission(P, L, Fade * (0.9 + 0.2 * std::sin(T * 67 + double(I) * 2)));
		}
		// Splash ball at the head, swelling as the jet lands.
		const double Land = Smoothstep(0.05, 0.14, Since);
		const double Ball = (0.6 + 0.4 * Land) * Widen * (1 + 0.15 * std::sin(T * 37)) * Fade;
		const int32 EndI = FlameEnd.Get(M);
		if (EndI != INDEX_NONE)
		{
			FSkNode N = Rest(M, EndI);
			N.Pos = FVector(Head, 0, 0);
			N.Scale = FVector(Ball);
			N.Euler.X = T * 6;
			Apply(M, P, EndI, N);
			SetMeshEmission(P, EndI, BallHot, Fade * Land * (0.85 + 0.3 * std::sin(T * 53)));
			SetMeshEmission(P, EndI, BallOuter, Fade * Land * (0.85 + 0.3 * std::sin(T * 53 + 1)));
		}
		if (const int32 Fr = Flare.Get(M); Fr != INDEX_NONE)
		{
			SetVisible(P, Fr, !(Since > Burn - 0.1));
			const double Fl = 1 + 0.3 * std::sin(T * 71) * std::sin(T * 43);
			FSkNode N = Rest(M, Fr);
			N.Scale = FVector(1.4 * Fl, Fl, Fl);
			Apply(M, P, Fr, N);
		}

		// Particles speed out to the head, no farther; smoke rises off the
		// splash. The flame lights the ground around it.
		S.Fx.FireRate = Since < Burn - 0.1 ? 260.f : 0.f;
		S.Fx.FireVelocity = float(Full / 0.38);
		S.Fx.EmberRate = Since < Burn - 0.1 ? 70.f : 0.f;
		S.Fx.EmberVelocity = float(Full / 0.8);
		S.Fx.SmokeRate = Land > 0.5 ? 30.f : 0.f;
		S.Fx.FlameLight = float(70 * Fade * (0.8 + 0.25 * std::sin(T * 61) * std::sin(T * 19)));
		S.Fx.FlameLightRange = float(2 + 0.4 * Full);
		if (const int32 Li = FlameLight.Get(M); Li != INDEX_NONE)
		{
			FSkNode N = Rest(M, Li);
			N.Pos = FVector(Head * 0.6, 0.15, 0);
			Apply(M, P, Li, N);
		}
	}

	FAcPoseRegistration CometPose(ac::UnitKind::comet, &PoseComet);
	FAcPoseRegistration JuggernautPose(ac::UnitKind::juggernaut, &PoseJuggernaut);
	FAcPoseRegistration FireflyPose(ac::UnitKind::firefly, &PoseFirefly);
}

namespace AcPoseVehicles
{
	const FAcVehicleFx* Fx(const ac::UnitKind Kind, const FAcUnitMemory& Memory)
	{
		if (Kind != ac::UnitKind::comet && Kind != ac::UnitKind::juggernaut && Kind != ac::UnitKind::firefly) return nullptr;
		if (!Memory.KindState) return nullptr;
		return &static_cast<const FVehicleState&>(*Memory.KindState).Fx;
	}
}
