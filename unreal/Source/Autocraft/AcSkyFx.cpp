// The sky's life: see AcSkyFx.h. The look is in M_AcSky.
#include "AcSkyFx.h"

#include "AcGasGiant.h"
#include "AcLog.h"
#include "AcSimSubsystem.h"
#include "AcSpace.h"
#include "AcTerrain.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace
{
	TAutoConsoleVariable<int32> CVarSkyShips(TEXT("ac.Sky.Ships"), 1, TEXT("Ships to and from the gas giant (0 off, 1 on)."));
	TAutoConsoleVariable<int32> CVarSkyComet(TEXT("ac.Sky.Comet"), 1, TEXT("The comet in the sky (0 off, 1 on); never by day."));
	TAutoConsoleVariable<int32> CVarSkyMeteors(TEXT("ac.Sky.Meteors"), 1, TEXT("The meteor shower (0 off, 1 on); never by day."));

	constexpr double SfTwoPi = 2 * UE_DOUBLE_PI;
	constexpr double ShipSlot = 120, ShipJitter = 60;
	constexpr double BurstSlot = 300, BurstLength = 25;
	constexpr double FireSlot = 480, FireLength = 2.4;
	constexpr double BaseRate = 0.33;

	uint32 SfHash(uint32 A, uint32 B = 0, uint32 C = 0)
	{
		uint32 H = 2166136261u;
		for (const uint32 V : {A, B, C})
		{
			H = (H ^ V) * 16777619u;
			H ^= H >> 15;
			H *= 0x2c1b3c6du;
			H ^= H >> 12;
		}
		return H;
	}
	double SfRand(const uint32 Seed, const uint32 A, const uint32 B = 0)
	{
		return double(SfHash(Seed ^ 0x5bd1e995u, A, B) & 0xFFFFFF) / double(0x1000000);
	}
	double SfSmooth(const double A, const double B, const double X)
	{
		const double T = FMath::Clamp((X - A) / (B - A), 0.0, 1.0);
		return T * T * (3 - 2 * T);
	}
	uint32 SfSeed()
	{
		static const uint32 Seed = [] {
			int32 S = 0;
			if (FParse::Value(FCommandLine::Get(), TEXT("AcGasGiantSeed="), S)) return uint32(S);
			return uint32(FPlatformTime::Cycles64() ^ (FPlatformTime::Cycles64() >> 17));
		}();
		return Seed;
	}
	FVector FromAzEl(const double Az, const double El)
	{
		return FVector(FMath::Cos(El) * FMath::Cos(Az), FMath::Cos(El) * FMath::Sin(Az), FMath::Sin(El));
	}
	FVector SfSlerp(const FVector& A, const FVector& B, const double T)
	{
		const double Om = FMath::Acos(FMath::Clamp(FVector::DotProduct(A, B), -1.0, 1.0));
		if (Om < 1e-4) return A;
		return (A * FMath::Sin((1 - T) * Om) + B * FMath::Sin(T * Om)) / FMath::Sin(Om);
	}
	// Directions go to the material as v * 0.5 + 0.5 (a vector parameter is clamped at 0).
	FLinearColor Enc(const FVector& V, const double W) { return FLinearColor(float(V.X * 0.5 + 0.5), float(V.Y * 0.5 + 0.5), float(V.Z * 0.5 + 0.5), float(W)); }
	double PlanetAz() { const FVector P = FAcGasGiant::Direction(); return FMath::Atan2(P.Y, P.X); }
}

double FAcSkyFx::Night(const double Dark)
{
	return SfSmooth(0.15, 0.85, Dark);
}

// The comet's place: over the side of the sky away from the planet, ~34 deg up,
// drifting 0.5 deg a minute round and breathing a little in height.
FVector FAcSkyFx::CometAt(const double Now) const
{
	const double Az = PlanetAz() + UE_DOUBLE_PI * 0.78 + FMath::DegreesToRadians(0.5) * Now / 60.0;
	const double El = FMath::DegreesToRadians(34.0 + 5.0 * FMath::Sin(Now / 1500.0));
	return FromAzEl(Az, El);
}

FAcSkyFx::FShip FAcSkyFx::ShipAt(const double Now, const uint32 Seed) const
{
	FShip Out;
	const TCHAR* Cmd = FCommandLine::Get();
	FString Force;
	FParse::Value(Cmd, TEXT("AcSkyShip="), Force);
	float At = 0.5f;
	FParse::Value(Cmd, TEXT("AcSkyShipAt="), At);

	const int64 K = int64(FMath::FloorToDouble(Now / ShipSlot));
	const uint32 Ki = uint32(K);
	const double Start = double(K) * ShipSlot + ShipJitter * SfRand(Seed, Ki, 1);
	const double Dur = 20 + 20 * SfRand(Seed, Ki, 2);
	double U = (Now - Start) / Dur;
	bool bTakeOff = SfRand(Seed, Ki, 3) < 0.5;
	if (!Force.IsEmpty())
	{
		U = At;
		bTakeOff = Force != TEXT("landing");
	}
	else if (U < 0 || U > 1) return Out;

	const FVector P = FAcGasGiant::Direction();
	const double Az = PlanetAz() + UE_DOUBLE_PI + (SfRand(Seed, Ki, 4) - 0.5) * 2.0;
	const FVector Q = FromAzEl(Az, FMath::DegreesToRadians(-3.5));
	// The planet end: on the disc, toward the limb on the way out.
	const FVector E1 = FVector::CrossProduct(FVector::UpVector, P).GetSafeNormal();
	const FVector E2 = FVector::CrossProduct(P, E1);
	const double R = FMath::Tan(FMath::DegreesToRadians(FAcGasGiant::AngularRadiusDeg()));
	const FVector ToQ = (Q - P * FVector::DotProduct(Q, P)).GetSafeNormal();
	const FVector Start0 = (P + R * 0.62 * (E1 * FVector::DotProduct(ToQ, E1) + E2 * FVector::DotProduct(ToQ, E2))).GetSafeNormal();
	// Ps 0 at the planet, 1 below the horizon.
	auto Path = [&](const double Ps) {
		const double T = FMath::Clamp(Ps, 0.0, 1.0);
		return (SfSlerp(Start0, Q, T) + FVector::UpVector * 0.24 * FMath::Sin(UE_DOUBLE_PI * T)).GetSafeNormal();
	};
	const double Ps = bTakeOff ? FMath::Pow(U, 1.6) : FMath::Pow(1 - U, 1.6);
	const double Dir = bTakeOff ? 1.0 : -1.0;
	Out.bOn = true;
	Out.Pos = Path(Ps);
	Out.Tail = Path(Ps - Dir * 0.045);
	// Nearest overhead, smaller toward both ends.
	Out.Size = 0.0018 * (0.5 + 0.9 * FMath::Pow(FMath::Sin(UE_DOUBLE_PI * FMath::Clamp(Ps, 0.02, 0.98)), 0.6));
	Out.Fade = SfSmooth(0.0, 0.05, U) * SfSmooth(1.0, 0.94, U);
	// Take-off: the engine burns hard at first. Landing: it flares at the end.
	Out.Engine = bTakeOff ? 1.0 - 0.55 * SfSmooth(0.15, 0.6, U) : 0.45 + 0.55 * SfSmooth(0.65, 0.95, U);
	return Out;
}

void FAcSkyFx::Update(UWorld* World, AActor* Owner, UMaterialInstanceDynamic* Mat, const FVector& SunDir, const double Dark)
{
	if (!World || !Mat) return;
	const TCHAR* Cmd = FCommandLine::Get();
	float Frozen = 0;
	const bool bFrozen = FParse::Value(Cmd, TEXT("AcSkyT="), Frozen);
	const double Now = bFrozen ? double(Frozen) : World->GetTimeSeconds() + 5.0;
	const uint32 Seed = SfSeed();
	const double Night = FAcSkyFx::Night(Dark);
	auto V = [&](const TCHAR* Name, const FLinearColor& Value) { Mat->SetVectorParameterValue(Name, Value); };

	// Ships.
	FShip Ship;
	if (CVarSkyShips.GetValueOnGameThread() != 0) Ship = ShipAt(Now, Seed);
	const FVector Zero = FVector::ZeroVector;
	if (Ship.bOn)
	{
		V(TEXT("ShipA"), Enc(Ship.Pos, Ship.Size));
		V(TEXT("ShipB"), Enc(Ship.Tail, Ship.Engine));
		// By day the sky is bright: a stronger craft, a fainter engine and trail.
		V(TEXT("ShipC"), FLinearColor(float(Ship.Fade), float(FMath::Lerp(0.4, 1.0, Night)), float(FMath::Lerp(2.2, 1.0, Night)), 0));
	}
	else
	{
		V(TEXT("ShipA"), Enc(FVector(0, 0, 1), 0));
		V(TEXT("ShipC"), FLinearColor(0, 0, 0, 0));
	}

	// The comet: its tail away from the sun along the sky.
	const FVector Comet = CometAt(Now);
	{
		// The tangent at the comet that points away from the sun.
		FVector Away = -(SunDir - Comet * FVector::DotProduct(SunDir, Comet));
		if (Away.SizeSquared() < 1e-4) Away = FVector::CrossProduct(Comet, FVector::UpVector);
		Away = Away.GetSafeNormal();
		const double On = CVarSkyComet.GetValueOnGameThread() != 0 ? Night : 0.0;
		V(TEXT("CometA2"), Enc(Comet, On));
		V(TEXT("CometB2"), Enc(Away, 0));
	}

	// Meteors: the rate, the burst, the fireball.
	double Burst = 0;
	{
		const int64 K = int64(FMath::FloorToDouble(Now / BurstSlot));
		const double Start = double(K) * BurstSlot + (BurstSlot - BurstLength - 5) * SfRand(Seed, uint32(K), 21);
		const double A = Now - Start;
		if (A > 0 && A < BurstLength) Burst = SfSmooth(0, 4, A) * SfSmooth(BurstLength, BurstLength - 8, A);
		FString Force;
		FParse::Value(Cmd, TEXT("AcSkyMeteors="), Force);
		if (Force == TEXT("burst")) Burst = 1;
	}
	double FireU = -1, FireB = 0, FirePhi = 0, FireT0 = 0, FireT1 = 0;
	{
		const int64 K = int64(FMath::FloorToDouble(Now / FireSlot));
		const uint32 Ki = uint32(K);
		const double Start = double(K) * FireSlot + (FireSlot - 30) * SfRand(Seed, Ki, 31);
		double Age = Now - Start;
		bool bOn = SfRand(Seed, Ki, 32) < 0.75;
		float FU = 0;
		if (FParse::Param(Cmd, TEXT("AcSkyFireball"))) { bOn = true; Age = 0.7; }
		if (FParse::Value(Cmd, TEXT("AcSkyFireball="), FU)) { bOn = true; Age = FU * FireLength; }
		if (bOn && Age > 0 && Age < FireLength * 1.65)
		{
			FireU = Age / FireLength;
			FireB = SfSmooth(0, 0.08, FireU) * SfSmooth(1.65, 0.9, FireU);
			FirePhi = SfTwoPi * SfRand(Seed, Ki, 33);
			FireT0 = FMath::DegreesToRadians(6.0 + 14.0 * SfRand(Seed, Ki, 34));
			FireT1 = FireT0 + FMath::DegreesToRadians(45.0 + 25.0 * SfRand(Seed, Ki, 35));
		}
	}
	const double Vis = CVarSkyMeteors.GetValueOnGameThread() != 0 ? Night : 0.0;
	V(TEXT("SkyFxMisc"), FLinearColor(float(FMath::Fmod(Now, 20000.0)), float(Vis), float(BaseRate), float(Burst)));
	V(TEXT("FireA"), FLinearColor(float(FirePhi), float(FireT0), float(FireT1), float(FireU)));
	V(TEXT("FireB"), FLinearColor(float(FireB), 0, 0, 0));
	(void)Zero;

	if (!bLogged)
	{
		bLogged = true;
		UE_LOG(LogAutocraft, Log, TEXT("skyfx: comet toward %s, seed %u, night %.2f dark %.2f"), *Comet.ToString(), Seed, Night, Dark);
	}
	if (FParse::Param(Cmd, TEXT("AcSkyShot"))) StageShot(World, Owner, Ship.Pos, Comet);
}

void FAcSkyFx::StageShot(UWorld* World, AActor* Owner, const FVector& ShipPos, const FVector& CometPos)
{
	if (ShotFrames > 400) return;
	++ShotFrames;
	const TCHAR* Cmd = FCommandLine::Get();
	if (!Camera.IsValid())
	{
		FVector At = FVector::ZeroVector;
		if (const UAcSimSubsystem* Sim = UAcSimSubsystem::Get(Owner); Sim && Sim->IsRunning())
		{
			const ac::GroundRect& B = Sim->Map().bounds;
			At = AcSpace::ToWorld(ac::Vec2((B.minX + B.maxX) / 2, (B.minZ + B.maxZ) / 2), 0);
		}
		if (const AAcTerrain* Terrain = AAcTerrain::Find(World); Terrain && Terrain->IsBuilt()) At.Z = Terrain->GroundZ(At);
		At.Z += 200;
		FString Aim = TEXT("ship");
		FParse::Value(Cmd, TEXT("AcSkyAim="), Aim);
		FVector Toward = ShipPos.IsNearlyZero() ? FAcGasGiant::Direction() : ShipPos;
		float Fov = 60;
		if (Aim == TEXT("comet")) { Toward = CometPos; Fov = 55; }
		else if (Aim == TEXT("zenith")) { Toward = FVector(0.0001, 0, 1); Fov = 110; }
		else if (Aim == TEXT("planet")) { Toward = FAcGasGiant::Direction(); Fov = 50; }
		else if (Aim == TEXT("horizon")) { Toward = FVector(ShipPos.X, ShipPos.Y, 0).GetSafeNormal() + FVector::UpVector * 0.08; Fov = 60; }
		FParse::Value(Cmd, TEXT("AcSkyFov="), Fov);
		FRotator Rot = Toward.Rotation();
		float V = 0;
		if (FParse::Value(Cmd, TEXT("AcSkyPitch="), V)) Rot.Pitch = V;
		if (FParse::Value(Cmd, TEXT("AcSkyYaw="), V)) Rot.Yaw = V;
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ACameraActor* Cam = World->SpawnActor<ACameraActor>(At, Rot, Params);
		Cam->GetCameraComponent()->SetConstraintAspectRatio(false);
		Cam->GetCameraComponent()->SetFieldOfView(Fov);
		Camera = Cam;
		UE_LOG(LogAutocraft, Log, TEXT("skyfx: shot camera at %s, yaw %.0f pitch %.0f, fov %.0f"), *At.ToString(), Rot.Yaw, Rot.Pitch, Fov);
	}
	if (APlayerController* PC = World->GetFirstPlayerController(); PC && PC->GetViewTarget() != Camera.Get())
	{
		PC->SetViewTarget(Camera.Get());
	}
}
