// The gas giant's plan: see AcGasGiant.h. The look is in M_AcSky.
//
// Stills (headless, with -AcShot; all optional):
//   -AcGasGiantShot            a camera 2 m over the map middle aimed at the planet
//   -AcGasGiantFov=F           field of view (default 70; the ring is 2.5 radii wide)
//   -AcGasGiantAim=center|storm|impact   where to aim (default center)
//   -AcGasGiantHeight=CM       the camera's height over the ground (default 200)
//   -AcGasGiantStorm           turn the storm to face the viewer
//   -AcGasGiantT=SECONDS       freeze the clock there (planet, winds, events)
//   -AcGasGiantEvent=comet|meteor  force an event (and no other):
//   -AcGasGiantAge=S           seconds into it (comet: 3.6 s of flight, then
//                              the impact; meteor: 0.8 s long)
//   -AcGasGiantSeed=N          the seed of the schedule
#include "AcGasGiant.h"

#include "AcDaylight.h"
#include "AcLog.h"
#include "AcSkyFx.h"
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
	TAutoConsoleVariable<int32> CVarGasGiant(
		TEXT("ac.GasGiant"), 1, TEXT("The gas giant in the sky (0 off, 1 on)."));
	TAutoConsoleVariable<int32> CVarGasGiantEvents(
		TEXT("ac.GasGiant.Events"), 1, TEXT("Meteors and comets on the gas giant (0 off, 1 on)."));
	TAutoConsoleVariable<float> CVarGasGiantGain(
		TEXT("ac.GasGiant.Gain"), 1.0f, TEXT("Brightness trim of the gas giant."));

	// The planet: how it hangs. Angular radius, spin (radians a second: one
	// turn in 50 minutes), the pole's tilt on the screen and toward the eye.
	constexpr double AngularRadius = 27.0;
	constexpr double SpinRate = 0.0021;
	constexpr double TiltScreen = 24.0, TiltEye = 14.0;
		// The storm rides this latitude (radians); the winds are the material's
	// `zonal` (keep the two the same).
	constexpr double StormLat = 0.34;
	constexpr double TwoPi = 2 * UE_DOUBLE_PI;

	constexpr double Sunrise = AAcDaylight::Sunrise;
	constexpr double CometPeriod = 240, CometJitter = 100, CometFlight = 3.6;
	constexpr double MeteorLife = 0.8;
	constexpr double MeteorPeriods[3] = {37, 53, 71};

	double Zonal(const double Lat)
	{
		return 0.0018 * (0.6 * FMath::Cos(Lat * 4.0) + 0.4 * FMath::Cos(Lat * 9.0 + 1.0));
	}

	uint32 HashU(uint32 A, uint32 B = 0, uint32 C = 0, uint32 D = 0)
	{
		uint32 H = 2166136261u;
		for (const uint32 V : {A, B, C, D})
		{
			H = (H ^ V) * 16777619u;
			H ^= H >> 15;
			H *= 0x2c1b3c6du;
			H ^= H >> 12;
		}
		return H;
	}
	/// 0 to 1.
	double Rand(const uint32 Seed, const uint32 A, const uint32 B = 0, const uint32 C = 0)
	{
		return double(HashU(Seed, A, B, C) & 0xFFFFFF) / double(0x1000000);
	}
	double Smooth(const double A, const double B, const double X)
	{
		const double T = FMath::Clamp((X - A) / (B - A), 0.0, 1.0);
		return T * T * (3 - 2 * T);
	}

	uint32 RunSeed()
	{
		static const uint32 Seed = [] {
			int32 S = 0;
			if (FParse::Value(FCommandLine::Get(), TEXT("AcGasGiantSeed="), S)) return uint32(S);
			return uint32(FPlatformTime::Cycles64() ^ (FPlatformTime::Cycles64() >> 20));
		}();
		return Seed;
	}
}

double FAcGasGiant::AngularRadiusDeg() { return AngularRadius; }

// 0 with the sun clear of the planet, 1 with it well behind the disc.
double FAcGasGiant::Eclipse(const FVector& SunDir)
{
	const double Ang = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(SunDir, Direction()), -1.0, 1.0)));
	return Smooth(AngularRadius * 0.95, AngularRadius * 0.45, Ang);
}

// Near the moon's rising point, a little out of the sun's plane: the sun and
// the moon (and so the day) sweep one great circle, and the moon is the
// antipode of the sun, so a planet there is full by night, a crescent at
// dawn and a gibbous disc at noon, and never in the sun's way.
FVector FAcGasGiant::Direction()
{
	static const FVector Dir = [] {
		// The moon at 9° up on the rising side: the sun's antipode when it is 9° down, in the evening.
		double Best = 0, BestErr = 1e9;
		for (double H = 18.5; H < 24.0; H += 0.01)
		{
			const double El = FMath::RadiansToDegrees(FMath::Asin(AAcDaylight::SunToward(H).Z));
			if (FMath::Abs(El + 9.0) < BestErr) { BestErr = FMath::Abs(El + 9.0); Best = H; }
		}
		const FVector Moon = -AAcDaylight::SunToward(Best);
		const FVector Plane = FVector::CrossProduct(AAcDaylight::SunToward(12.0), AAcDaylight::SunToward(Sunrise)).GetSafeNormal();
		const double B = FMath::DegreesToRadians(9.0);
		FVector Out = Moon * FMath::Cos(B) + Plane * FMath::Sin(B);
		FVector Out2 = Moon * FMath::Cos(B) - Plane * FMath::Sin(B);
		if (FMath::Abs(Out2.Z - 0.25) < FMath::Abs(Out.Z - 0.25)) Out = Out2;
		return Out.GetSafeNormal();
	}();
	return Dir;
}

bool FAcGasGiant::Surface(const FFrame& F, const double X, const double Y, const double Spin, double& Lat, double& Lon) const
{
	const double R2 = X * X + Y * Y;
	if (R2 >= 1) return false;
	const FVector N(X, Y, FMath::Sqrt(1 - R2));
	const FVector& A = F.AxisView;
	const FVector B = FVector::CrossProduct(A, FVector(0, 0, 1)).GetSafeNormal();
	const FVector C = FVector::CrossProduct(B, A);
	Lat = FMath::Asin(FMath::Clamp(FVector::DotProduct(N, A), -1.0, 1.0));
	Lon = FMath::Atan2(FVector::DotProduct(N, B), FVector::DotProduct(N, C)) - Spin;
	return true;
}

void FAcGasGiant::Update(UWorld* World, AActor* Owner, UMaterialInstanceDynamic* Mat, const FVector& SunDir, const double Dark)
{
	if (!World || !Mat) return;
	const bool bOn = CVarGasGiant.GetValueOnGameThread() != 0;
	if (!bOn)
	{
		Mat->SetVectorParameterValue(TEXT("PlanetMisc"), FLinearColor(0, 0, 0, 0));
		Mat->SetVectorParameterValue(TEXT("PlanetDir"), FLinearColor(0, 0, 1, 0));
		return;
	}
	const TCHAR* Cmd = FCommandLine::Get();
	float Frozen = 0;
	const bool bFrozen = FParse::Value(Cmd, TEXT("AcGasGiantT="), Frozen);
	const double Now = bFrozen ? double(Frozen) : World->GetTimeSeconds() + 5.0;
	const uint32 Seed = RunSeed();

	// The frame: P toward the planet; E1 right, E2 up on the screen.
	FFrame F;
	F.P = Direction();
	F.E1 = FVector::CrossProduct(FVector::UpVector, F.P).GetSafeNormal();
	F.E2 = FVector::CrossProduct(F.P, F.E1);
	F.Radius = FMath::Tan(FMath::DegreesToRadians(AngularRadius));
	// The planet's centre in radii from the eye (a real perspective sphere in the material).
	const double CentreDist = 1.0 / FMath::Sin(FMath::DegreesToRadians(AngularRadius));
	const double Ts = FMath::DegreesToRadians(TiltScreen), Te = FMath::DegreesToRadians(TiltEye);
	// The pole in the view frame (x right, y up, z toward the eye).
	F.AxisView = FVector(FMath::Sin(Ts) * FMath::Cos(Te), FMath::Cos(Ts) * FMath::Cos(Te), FMath::Sin(Te));
	const FVector AxisWorld = F.E1 * F.AxisView.X + F.E2 * F.AxisView.Y - F.P * F.AxisView.Z;
	const double Spin = FMath::Fmod(SpinRate * Now, TwoPi);

	// Brightness. The sky's own light is high by day and tiny at night; the
	// planet is seen through the day's haze (Fade).
	const double Gain = FMath::Lerp(1.0, 1.4, 1 - Dark) * CVarGasGiantGain.GetValueOnGameThread();
	const double Fade = FMath::Lerp(1.0, 0.7, 1 - Dark);

	// The storm.
	double StormLon0 = 0.8;
	if (FParse::Param(Cmd, TEXT("AcGasGiantStorm")))
	{
		// The meridian that faces the eye at the storm's latitude.
		double Best = -2, BestLon = 0;
		const FVector& A = F.AxisView;
		const FVector B = FVector::CrossProduct(A, FVector(0, 0, 1)).GetSafeNormal();
		const FVector C = FVector::CrossProduct(B, A);
		for (int32 I = 0; I < 720; ++I)
		{
			const double L = TwoPi * I / 720;
			const FVector N = A * FMath::Sin(StormLat) + (C * FMath::Cos(L) + B * FMath::Sin(L)) * FMath::Cos(StormLat);
			if (N.Z > Best) { Best = N.Z; BestLon = L; }
		}
		StormLon0 = BestLon - Spin - Zonal(StormLat) * Now - 0.12 * FMath::Sin(Now * 0.0023);
	}

	// Events.
	FLinearColor CometA(0, 0, 0, 0), CometB(0, 0, 0, 0);
	FLinearColor Scar[2] = {FLinearColor(0, 0, -1, 0), FLinearColor(0, 0, -1, 0)};
	FLinearColor MetA[3], MetB[3];
	for (int32 I = 0; I < 3; ++I) { MetA[I] = FLinearColor(0, 0, 0, 0); MetB[I] = FLinearColor(0, 0, 0, 0); }
	FString Force;
	FParse::Value(Cmd, TEXT("AcGasGiantEvent="), Force);
	float ForceAge = 0;
	FParse::Value(Cmd, TEXT("AcGasGiantAge="), ForceAge);
	const bool bEvents = CVarGasGiantEvents.GetValueOnGameThread() != 0;

	if (bEvents && (Force.IsEmpty() || Force == TEXT("comet")))
	{
		// The last two comets that have begun.
		const int64 K = int64(FMath::FloorToDouble(Now / CometPeriod));
		int32 Slot = 0;
		for (int64 Back = 0; Back < 4 && Slot < 2; ++Back)
		{
			const int64 Ki = K - Back;
			if (Ki < 0) break;
			double Start = double(Ki) * CometPeriod + CometJitter * Rand(Seed, uint32(Ki), 1);
			if (!Force.IsEmpty())
			{
				if (Back > 0) break;
				Start = Now - ForceAge;
			}
			if (Now < Start) continue;
			const double Age = Now - Start;
			// Where it lands: on the face toward us, between the middle and 0.72 of the radius.
			const double Ang = TwoPi * Rand(Seed, uint32(Ki), 2);
			const double Rad = 0.18 + 0.54 * Rand(Seed, uint32(Ki), 3);
			const double Ix = Rad * FMath::Cos(Ang), Iy = Rad * FMath::Sin(Ang);
			if (Slot == 0) LastImpact = FVector2D(Ix, Iy);
			if (Age < CometFlight)
			{
				// It comes from 1.55 radii out (the sky ends at 1.9: the planet is big), along the sun's anti-direction
				// a little: any direction, tail behind.
				const double From = TwoPi * Rand(Seed, uint32(Ki), 4);
				const FVector2D Far(1.55 * FMath::Cos(From), 1.55 * FMath::Sin(From));
				const FVector2D To(Ix, Iy);
				const double S = FMath::Pow(Age / CometFlight, 1.5);
				const FVector2D Head = Far + (To - Far) * S;
				const FVector2D Dir = (To - Far).GetSafeNormal();
				const double Tail = FMath::Min(0.9, (Head - Far).Size());
				const FVector2D Back2 = Head - Dir * Tail;
				CometA = FLinearColor(float(Head.X), float(Head.Y), float(Back2.X), float(Back2.Y));
				CometB = FLinearColor(float(Smooth(0, 0.4, Age) * 1.0), 0, 0, 0);
			}
			// The scar: from the impact on, in the planet's own coordinates.
			double Lat = 0, Lon = 0;
			const double SpinAtImpact = FMath::Fmod(SpinRate * (Start + CometFlight), TwoPi);
			if (Surface(F, Ix, Iy, SpinAtImpact, Lat, Lon))
			{
				Scar[Slot] = FLinearColor(float(Lat), float(Lon), float(Age - CometFlight), 1.0f);
				++Slot;
			}
		}
	}
	if (bEvents && (Force.IsEmpty() || Force == TEXT("meteor")))
	{
		for (int32 S = 0; S < 3; ++S)
		{
			const double Period = MeteorPeriods[S];
			const int64 K = int64(FMath::FloorToDouble(Now / Period));
			double Start = double(K) * Period + (Period - 1.0) * Rand(Seed, uint32(K), uint32(S), 11);
			if (!Force.IsEmpty()) Start = Now - ForceAge;
			const double U = (Now - Start) / MeteorLife;
			if (U < 0 || U > 1) continue;
			const double Th = TwoPi * Rand(Seed, uint32(K), uint32(S), 12);
			const double Rho = 0.82 + 0.22 * Rand(Seed, uint32(K), uint32(S), 13);
			const double Len = 0.16 + 0.2 * Rand(Seed, uint32(K), uint32(S), 14);
			const double Side = Rand(Seed, uint32(K), uint32(S), 15) < 0.5 ? -1.0 : 1.0;
			const FVector2D A(Rho * FMath::Cos(Th), Rho * FMath::Sin(Th));
			const FVector2D Tangent(-FMath::Sin(Th) * Side, FMath::Cos(Th) * Side);
			const FVector2D Dir = (Tangent - A.GetSafeNormal() * 0.3).GetSafeNormal();
			const FVector2D Head = A + Dir * Len * U;
			const FVector2D Tail = A + Dir * Len * FMath::Max(0.0, U - 0.7);
			MetA[S] = FLinearColor(float(Head.X), float(Head.Y), float(Tail.X), float(Tail.Y));
			MetB[S] = FLinearColor(float(FMath::Pow(FMath::Sin(UE_DOUBLE_PI * U), 0.6) * (0.7 + 0.6 * Rand(Seed, uint32(K), uint32(S), 16))), 0, 0, 0);
		}
	}

	// The comet's streak and the limb meteors never show by day.
	const float NightVis = float(FAcSkyFx::Night(Dark));
	CometB.R *= NightVis;
	for (int32 I = 0; I < 3; ++I) MetB[I].R *= NightVis;
	auto V = [&](const TCHAR* Name, const FLinearColor& Value) { Mat->SetVectorParameterValue(Name, Value); };
	V(TEXT("PlanetDir"), FLinearColor(float(F.P.X), float(F.P.Y), float(F.P.Z), float(CentreDist)));
	V(TEXT("PlanetAxis"), FLinearColor(float(AxisWorld.X), float(AxisWorld.Y), float(AxisWorld.Z), float(Spin)));
	V(TEXT("PlanetSun"), FLinearColor(float(SunDir.X), float(SunDir.Y), float(SunDir.Z), float(Gain)));
	V(TEXT("PlanetMisc"), FLinearColor(float(FMath::Fmod(Now, 86400.0)), float(Fade), float(StormLon0), 1.0f));
	V(TEXT("CometA"), CometA);
	V(TEXT("CometB"), CometB);
	V(TEXT("Scar0"), Scar[0]);
	V(TEXT("Scar1"), Scar[1]);
	V(TEXT("Met0A"), MetA[0]); V(TEXT("Met0B"), MetB[0]);
	V(TEXT("Met1A"), MetA[1]); V(TEXT("Met1B"), MetB[1]);
	V(TEXT("Met2A"), MetA[2]); V(TEXT("Met2B"), MetB[2]);

	if (!bLogged)
	{
		bLogged = true;
		UE_LOG(LogAutocraft, Log, TEXT("gasgiant: toward %s (azimuth %.0f°, elevation %.0f°), radius %.1f°, seed %u"), *F.P.ToString(), FMath::RadiansToDegrees(FMath::Atan2(F.P.Y, F.P.X)), FMath::RadiansToDegrees(FMath::Asin(F.P.Z)), AngularRadius, Seed);
	}
	if (FParse::Param(Cmd, TEXT("AcGasGiantShot"))) StageShot(World, Owner, F, Spin, Now);
}

// A camera over the middle of the map, aimed at the planet (or the storm, or
// the impact), for the stills.
void FAcGasGiant::StageShot(UWorld* World, AActor* Owner, const FFrame& F, const double Spin, const double Now)
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
		float Height = 200;
		FParse::Value(Cmd, TEXT("AcGasGiantHeight="), Height);
		At.Z += Height;
		FString Aim = TEXT("center");
		FParse::Value(Cmd, TEXT("AcGasGiantAim="), Aim);
		FVector2D Off(0, 0);
		if (Aim == TEXT("storm"))
		{
			// Where the storm is on the disc: the central-meridian point at its latitude.
			const FVector& A = F.AxisView;
			const FVector B = FVector::CrossProduct(A, FVector(0, 0, 1)).GetSafeNormal();
			const FVector C = FVector::CrossProduct(B, A);
			double Best = -2;
			for (int32 I = 0; I < 720; ++I)
			{
				const double L = TwoPi * I / 720;
				const FVector N = A * FMath::Sin(StormLat) + (C * FMath::Cos(L) + B * FMath::Sin(L)) * FMath::Cos(StormLat);
				if (N.Z > Best) { Best = N.Z; Off = FVector2D(N.X, N.Y); }
			}
		}
		else if (Aim == TEXT("impact"))
		{
			Off = LastImpact;
		}
		const FVector Toward = (F.P + F.Radius * (F.E1 * Off.X + F.E2 * Off.Y)).GetSafeNormal();
		float Fov = 70;
		FParse::Value(Cmd, TEXT("AcGasGiantFov="), Fov);
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ACameraActor* Cam = World->SpawnActor<ACameraActor>(At, Toward.Rotation(), Params);
		Cam->GetCameraComponent()->SetConstraintAspectRatio(false);
		Cam->GetCameraComponent()->SetFieldOfView(Fov);
		Camera = Cam;
		UE_LOG(LogAutocraft, Log, TEXT("gasgiant: shot camera at %s toward %s, fov %.0f"), *At.ToString(), *Toward.ToString(), Fov);
	}
	if (APlayerController* PC = World->GetFirstPlayerController(); PC && PC->GetViewTarget() != Camera.Get())
	{
		PC->SetViewTarget(Camera.Get());
	}
	(void)Spin;
}
