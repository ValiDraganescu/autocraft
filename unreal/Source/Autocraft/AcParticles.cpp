#include "AcParticles.h"
#include "AcInstancedMesh.h"

#include "AcLog.h"
#include "AcSpace.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"

namespace
{
	const TCHAR* const QuadPath = TEXT("/Game/Effects/SM_AcParticleQuad.SM_AcParticleQuad");
	const TCHAR* const AddPath = TEXT("/Game/Effects/M_AcParticleAdd.M_AcParticleAdd");
	const TCHAR* const SmokePath = TEXT("/Game/Effects/M_AcParticleSmoke.M_AcParticleSmoke");
	const TCHAR* const ClockPath = TEXT("/Game/Effects/MPC_AcParticles.MPC_AcParticles");
	const TCHAR* const SmokeTex = TEXT("/Game/Models/Textures/T_smoke.T_smoke");
	const TCHAR* const SparkTex = TEXT("/Game/Models/Textures/T_spark.T_spark");
	constexpr int32 Floats = 11;
	constexpr double Cm = AcSpace::CmPerCell;
	constexpr double Gravity = 9.8;  // SCNPhysicsWorld's default, cells/s²

	TAutoConsoleVariable<float> CVarParticleRate(TEXT("ac.ParticleRate"), 1.f,
		TEXT("Scales every particle birth rate (1 = Swift's). 0 turns the particles off."));

	// --- Swift colours → linear ---------------------------------------------

	/// NSColor(calibratedRed:green:blue:alpha:): γ 1.8 (as the export reads it).
	FLinearColor Cal(double R, double G, double B, double A = 1)
	{
		return FLinearColor(float(FMath::Pow(R, 1.8)), float(FMath::Pow(G, 1.8)), float(FMath::Pow(B, 1.8)), float(A));
	}

	/// NSColor(white:alpha:): grey in sRGB.
	FLinearColor White(double W, double A = 1)
	{
		FLinearColor C = FLinearColor::FromSRGBColor(FColor(uint8(FMath::RoundToInt(W * 255)), uint8(FMath::RoundToInt(W * 255)),
			uint8(FMath::RoundToInt(W * 255))));
		C.A = float(A);
		return C;
	}

	/// NSColor(calibratedHue:saturation:brightness:alpha:) → calibrated RGB components.
	FVector HsbToRgb(double H, double S, double V)
	{
		H = H - FMath::FloorToDouble(H);
		const double I = FMath::FloorToDouble(H * 6);
		const double F = H * 6 - I;
		const double P = V * (1 - S), Q = V * (1 - F * S), T = V * (1 - (1 - F) * S);
		switch (int32(I) % 6)
		{
		case 0: return FVector(V, T, P);
		case 1: return FVector(Q, V, P);
		case 2: return FVector(P, V, T);
		case 3: return FVector(P, Q, V);
		case 4: return FVector(T, P, V);
		default: return FVector(V, P, Q);
		}
	}

	FVector RgbToHsb(const FVector& C)
	{
		const double Mx = C.GetMax(), Mn = C.GetMin(), D = Mx - Mn;
		double H = 0;
		if (D > 1e-9)
		{
			if (Mx == C.X) H = FMath::Fmod((C.Y - C.Z) / D, 6.0);
			else if (Mx == C.Y) H = (C.Z - C.X) / D + 2;
			else H = (C.X - C.Y) / D + 4;
			H /= 6;
			if (H < 0) H += 1;
		}
		return FVector(H, Mx > 0 ? D / Mx : 0, Mx);
	}

	enum class EShape : uint8
	{
		Point,
		Sphere,    // A = radius
		Cylinder,  // A = radius, B = height (SceneKit Y)
		Box        // A = width (x), B = height (y), C = length (z)
	};

	struct FRampKey
	{
		float At = 0;
		float Value = 1;
	};
	struct FColourKey
	{
		float At = 0;
		FLinearColor Value = FLinearColor::White;  // linear rgb multiplier, alpha
	};
}

/// One SCNParticleSystem's values (cells, seconds, degrees, SceneKit axes).
struct FAcParticles::FSpec
{
	const TCHAR* Name = TEXT("");
	/// SceneKit's `blendMode`. Every Swift system keeps the default,
	/// `.additive` (measured: a grey 0.12 smoke over the ground only
	/// lightens it a little, it never darkens it), so the smoke is pale as
	/// in Swift; `false` (translucent, M_AcParticleSmoke) is there for a
	/// system that sets `.alpha`.
	bool bAdditive = true;
	bool bSpark = false;  // lib.spark, else lib.smoke
	double Rate = 0;      // birthRate
	double Life = 1, LifeVariation = 0;
	double Speed = 0, SpeedVariation = 0;
	double Spread = 0;  // spreadingAngle: the cone's half-angle
	FVector Direction = FVector(0, 1, 0);
	EShape Shape = EShape::Point;
	double A = 0, B = 0, C = 0;
	double Size = 0.1, SizeVariation = 0;
	/// particleColor (calibrated components unless `bWhite`), and the HSBA variation.
	FVector Rgb = FVector(1, 1, 1);
	double Alpha = 1;
	bool bWhite = false;
	bool bHsb = false;  // Rgb holds hue, saturation, brightness
	FVector4 Variation = FVector4(0, 0, 0, 0);
	double Intensity = 1;  // particleIntensity (additive)
	double Damping = 0;
	FVector Accel = FVector::ZeroVector;
	bool bGravity = false;
	double Stretch = 0;
	TArray<FRampKey> SizeKeys;          // .size controller (× size)
	TArray<FColourKey> ColourKeys;  // .color controller (replaces the colour) or .opacity (alpha only)
	bool bColourReplaces = false;
	int32 Capacity = 1024;
	/// Smoke's soft edge where it meets the ground, cm.
	double SoftCm = 40;
};

namespace
{
	using FSpec = FAcParticles::FSpec;

	TArray<FColourKey> Opacity(std::initializer_list<FRampKey> Keys)
	{
		TArray<FColourKey> Out;
		for (const FRampKey& K : Keys) Out.Add({K.At, FLinearColor(1, 1, 1, K.Value)});
		return Out;
	}

	/// The table: the Swift systems, value for value.
	const TArray<FSpec>& Specs()
	{
		static const TArray<FSpec> Table = []
		{
			TArray<FSpec> T;
			T.SetNum(int32(EAcParticle::Count));
			auto Set = [&T](EAcParticle K) -> FSpec& { return T[int32(K)]; };
			{
				FSpec& S = Set(EAcParticle::ExplosionFire);  // Effects.swift:633
				S.Name = TEXT("ExplosionFire");
				S.Rate = 260; S.Life = 0.55; S.LifeVariation = 0.25; S.Speed = 2.2; S.SpeedVariation = 1.4;
				S.Spread = 180; S.Shape = EShape::Sphere; S.A = 0.25; S.Size = 0.28; S.SizeVariation = 0.12;
				S.Rgb = FVector(1, 0.55, 0.18); S.Variation = FVector4(0.05, 0.1, 0, 0); S.Damping = 3; S.Intensity = 2.2;
				S.SizeKeys = {{0, 0.6}, {0.4, 1.4}, {1, 1.8}};
				S.Capacity = 6144;
			}
			{
				FSpec& S = Set(EAcParticle::ExplosionSmoke);  // :657
				S.Name = TEXT("ExplosionSmoke");
				S.Rate = 40; S.Life = 2.4; S.LifeVariation = 0.8; S.Speed = 0.9; S.SpeedVariation = 0.5;
				S.Spread = 50; S.Shape = EShape::Sphere; S.A = 0.3; S.Size = 0.35;
				S.bWhite = true; S.Rgb = FVector(0.12); S.Alpha = 0.55; S.Damping = 0.8;
				S.SizeKeys = {{0, 0.7}, {1, 2.2}};
				S.ColourKeys = Opacity({{0, 0}, {0.15, 1}, {1, 0}});
				S.Capacity = 6144; S.SoftCm = 60;
			}
			{
				FSpec& S = Set(EAcParticle::ExplosionSparks);  // :679
				S.Name = TEXT("ExplosionSparks");
				S.bSpark = true;
				S.Rate = 300; S.Life = 0.9; S.LifeVariation = 0.4; S.Speed = 4.5; S.SpeedVariation = 2;
				S.Spread = 75; S.Size = 0.05; S.Rgb = FVector(1, 0.7, 0.3); S.bGravity = true;
				S.Accel = FVector(0, -9, 0); S.Stretch = 0.06; S.Intensity = 3;
				S.Capacity = 6144;
			}
			{
				FSpec& S = Set(EAcParticle::BlastFire);  // :900 fireBurst
				S.Name = TEXT("BlastFire");
				S.Rate = 500; S.Life = 0.35; S.LifeVariation = 0.12; S.Speed = 2; S.SpeedVariation = 1;
				S.Spread = 180; S.Shape = EShape::Sphere; S.A = 0.15; S.Size = 0.2; S.SizeVariation = 0.07;
				S.Rgb = FVector(1, 0.62, 0.25); S.Variation = FVector4(0.04, 0.1, 0, 0); S.Damping = 4; S.Intensity = 2.4;
				S.SizeKeys = {{0, 0.6}, {0.4, 1.5}, {1, 1.2}};
				S.Capacity = 4096;
			}
			{
				FSpec& S = Set(EAcParticle::BlastSmoke);  // :925 smokeBurst
				S.Name = TEXT("BlastSmoke");
				S.Rate = 120; S.Life = 1.3; S.LifeVariation = 0.4; S.Speed = 0.8; S.SpeedVariation = 0.4;
				S.Spread = 60; S.Shape = EShape::Sphere; S.A = 0.15; S.Size = 0.25;
				S.bWhite = true; S.Rgb = FVector(0.2); S.Alpha = 0.5; S.Damping = 1;
				S.SizeKeys = {{0, 0.6}, {1, 2.2}};
				S.ColourKeys = Opacity({{0, 0}, {0.15, 1}, {1, 0}});
				S.Capacity = 2048;
			}
			{
				FSpec& S = Set(EAcParticle::GroundFire);  // :948
				S.Name = TEXT("GroundFire");
				S.Rate = 110; S.Life = 0.55; S.LifeVariation = 0.2; S.Speed = 1.1; S.SpeedVariation = 0.5;
				S.Spread = 18; S.Shape = EShape::Cylinder; S.A = 0.3; S.B = 0.05; S.Size = 0.22; S.SizeVariation = 0.08;
				S.Rgb = FVector(1, 0.5, 0.15); S.Variation = FVector4(0.03, 0.1, 0, 0); S.Intensity = 2;
				S.SizeKeys = {{0, 1.2}, {1, 0.3}};
				S.Capacity = 4096;
			}
			{
				FSpec& S = Set(EAcParticle::Embers);  // :972
				S.Name = TEXT("Embers");
				S.bSpark = true;
				S.Rate = 40; S.Life = 0.9; S.LifeVariation = 0.4; S.Speed = 1.6; S.SpeedVariation = 0.8;
				S.Spread = 35; S.Shape = EShape::Cylinder; S.A = 0.3; S.B = 0.05; S.Size = 0.035;
				S.Rgb = FVector(1, 0.7, 0.3); S.Intensity = 2.5; S.Damping = 1.5;
				S.Capacity = 2048;
			}
			{
				FSpec& S = Set(EAcParticle::Dust);  // :1045
				S.Name = TEXT("Dust");
				S.Rate = 260; S.Life = 1.6; S.LifeVariation = 0.5; S.Speed = 3.2; S.SpeedVariation = 1.2;
				S.Spread = 82; S.Shape = EShape::Cylinder; S.A = 0.4; S.B = 0.05; S.Size = 0.32; S.SizeVariation = 0.1;
				S.Rgb = FVector(0.55, 0.44, 0.32); S.Alpha = 0.5; S.Damping = 3.5; S.Accel = FVector(0, -0.6, 0);
				S.SizeKeys = {{0, 0.6}, {1, 2.4}};
				S.ColourKeys = Opacity({{0, 0}, {0.1, 1}, {1, 0}});
				S.Capacity = 4096; S.SoftCm = 50;
			}
			{
				FSpec& S = Set(EAcParticle::Chips);  // :993
				S.Name = TEXT("Chips");
				S.bSpark = true;
				S.Rate = 500; S.Life = 0.7; S.LifeVariation = 0.25; S.Speed = 1.5; S.SpeedVariation = 0.8;
				S.Spread = 70; S.Shape = EShape::Sphere; S.A = 0.06; S.Size = 0.035; S.SizeVariation = 0.015;
				// Opal dust: teal, violet and gold glints.
				S.bHsb = true; S.Rgb = FVector(0.5, 0.55, 1); S.Variation = FVector4(1, 0.2, 0, 0);
				S.Accel = FVector(0, -6, 0);
				S.ColourKeys = Opacity({{0, 1}, {0.6, 1}, {1, 0}});
				S.Capacity = 4096;
			}
			{
				FSpec& S = Set(EAcParticle::Ricochet);  // :1020
				S.Name = TEXT("Ricochet");
				S.bSpark = true;
				S.Rate = 520; S.Life = 0.28; S.LifeVariation = 0.12; S.Speed = 5; S.SpeedVariation = 2.5;
				S.Spread = 32; S.Shape = EShape::Sphere; S.A = 0.03; S.Size = 0.022; S.SizeVariation = 0.008;
				S.Stretch = 0.06; S.Rgb = FVector(1, 0.78, 0.4); S.Variation = FVector4(0, 0.1, 0.1, 0); S.Intensity = 2.2;
				S.Accel = FVector(0, -9, 0);
				S.ColourKeys = Opacity({{0, 1}, {0.5, 1}, {1, 0}});
				S.Capacity = 8192;
			}
			{
				FSpec& S = Set(EAcParticle::DamageSmoke);  // :1075 (radius r: box r × 0.1 × r)
				S.Name = TEXT("DamageSmoke");
				S.Life = 2.2; S.LifeVariation = 0.6; S.Speed = 1.1; S.SpeedVariation = 0.4;
				S.Spread = 12; S.Direction = FVector(0.2, 1, 0); S.Shape = EShape::Box; S.A = 1; S.B = 0.1; S.C = 1;
				S.Size = 0.3; S.bWhite = true; S.Rgb = FVector(0.1); S.Alpha = 0.45;
				S.SizeKeys = {{0, 0.6}, {1, 2.4}};
				S.Capacity = 8192; S.SoftCm = 60;
			}
			{
				FSpec& S = Set(EAcParticle::DamageFire);  // :1091 (box 0.7r × 0.1 × 0.7r)
				S.Name = TEXT("DamageFire");
				S.Life = 0.5; S.LifeVariation = 0.2; S.Speed = 1.2; S.SpeedVariation = 0.5;
				S.Spread = 15; S.Shape = EShape::Box; S.A = 0.7; S.B = 0.1; S.C = 0.7; S.Size = 0.2; S.SizeVariation = 0.08;
				S.Rgb = FVector(1, 0.5, 0.15); S.Intensity = 1.8;
				S.SizeKeys = {{0, 1}, {1, 0.3}};
				S.Capacity = 8192;
			}
			{
				FSpec& S = Set(EAcParticle::CometJet);  // Models+Comet.swift:292
				S.Name = TEXT("CometJet");
				S.Life = 0.28; S.LifeVariation = 0.1; S.Speed = 2.2; S.SpeedVariation = 0.6; S.Spread = 12;
				S.Direction = FVector(0, -1, 0); S.Size = 0.06; S.SizeVariation = 0.02; S.Intensity = 2;
				S.Variation = FVector4(0.04, 0.12, 0, 0);
				S.SizeKeys = {{0, 0.6}, {0.4, 1.4}, {1, 2.4}};
				S.bColourReplaces = true;
				S.ColourKeys = {{0, Cal(1, 0.8, 0.45, 1)}, {0.4, Cal(1, 0.4, 0.1, 0.8)}, {1, Cal(0.3, 0.1, 0.05, 0)}};
				S.Capacity = 8192;
			}
			{
				FSpec& S = Set(EAcParticle::MuzzleSmoke);  // Models+Juggernaut.swift:237
				S.Name = TEXT("MuzzleSmoke");
				S.Life = 0.9; S.LifeVariation = 0.3; S.Speed = 0.9; S.SpeedVariation = 0.4; S.Spread = 25;
				S.Direction = FVector(1, 0, 0); S.Size = 0.07; S.bWhite = true; S.Rgb = FVector(0.7); S.Alpha = 0.5;
				S.Accel = FVector(0, 0.5, 0); S.Damping = 3;
				S.SizeKeys = {{0, 0.5}, {0.4, 1.8}, {1, 3}};
				S.ColourKeys = Opacity({{0, 0}, {0.15, 0.6}, {1, 0}});
				S.Capacity = 2048; S.SoftCm = 20;
			}
			{
				FSpec& S = Set(EAcParticle::FireflyFire);  // Models+Firefly.swift:593
				S.Name = TEXT("FireflyFire");
				S.Life = 0.3; S.LifeVariation = 0.08; S.Speed = 10; S.SpeedVariation = 2; S.Spread = 6;
				S.Direction = FVector(1, 0, 0); S.Size = 0.07; S.SizeVariation = 0.03; S.Intensity = 2.2; S.Damping = 1.2;
				S.SizeKeys = {{0, 0.5}, {0.5, 2.2}, {1, 3.6}};
				S.bColourReplaces = true;
				S.ColourKeys = {{0, Cal(1, 0.95, 0.75, 1)}, {0.3, Cal(1, 0.6, 0.15, 0.9)}, {0.7, Cal(0.85, 0.18, 0.03, 0.6)},
					{1, Cal(0.2, 0.03, 0, 0)}};
				S.Capacity = 8192;
			}
			{
				FSpec& S = Set(EAcParticle::FireflyEmbers);  // :620
				S.Name = TEXT("FireflyEmbers");
				S.bSpark = true;
				S.Life = 0.55; S.LifeVariation = 0.25; S.Speed = 8; S.SpeedVariation = 3; S.Spread = 11;
				S.Direction = FVector(1, 0.1, 0); S.Size = 0.022; S.SizeVariation = 0.012; S.Rgb = FVector(1, 0.7, 0.3);
				S.bGravity = true; S.Accel = FVector(0, -3, 0); S.Stretch = 0.04; S.Intensity = 3;
				S.Capacity = 4096;
			}
			{
				FSpec& S = Set(EAcParticle::FireflySmoke);  // :644
				S.Name = TEXT("FireflySmoke");
				S.Life = 1.1; S.LifeVariation = 0.3; S.Speed = 0.7; S.SpeedVariation = 0.3; S.Spread = 35;
				S.Direction = FVector(0.3, 1, 0); S.Shape = EShape::Sphere; S.A = 0.2; S.Size = 0.18;
				S.Rgb = FVector(0.09, 0.07, 0.06); S.Alpha = 0.55;
				S.SizeKeys = {{0, 0.5}, {0.4, 1.6}, {1, 2.8}};
				S.ColourKeys = Opacity({{0, 0}, {0.2, 0.6}, {1, 0}});
				S.Capacity = 4096;
			}
			{
				FSpec& S = Set(EAcParticle::FireflyExhaust);  // :665
				S.Name = TEXT("FireflyExhaust");
				S.Life = 0.8; S.LifeVariation = 0.2; S.Speed = 0.6; S.SpeedVariation = 0.2; S.Spread = 18;
				S.Direction = FVector(-0.6, 1, 0); S.Shape = EShape::Box; S.A = 0.03; S.B = 0.03; S.C = 0.42; S.Size = 0.06;
				S.bWhite = true; S.Rgb = FVector(0.35); S.Alpha = 0.35;
				S.SizeKeys = {{0, 0.6}, {0.4, 1.8}, {1, 3}};
				S.ColourKeys = Opacity({{0, 0}, {0.2, 0.5}, {1, 0}});
				S.Capacity = 4096; S.SoftCm = 20;
			}
			{
				FSpec& S = Set(EAcParticle::Plume);  // Models+Dropship.swift:328
				S.Name = TEXT("Plume");
				S.Rate = 30; S.Life = 0.3; S.LifeVariation = 0.1; S.Speed = 1.6; S.SpeedVariation = 0.4; S.Spread = 8;
				S.Direction = FVector(-1, 0, 0); S.Shape = EShape::Sphere; S.A = 0.06; S.Size = 0.16;
				S.Rgb = FVector(1, 0.6, 0.25); S.Alpha = 0.8; S.Intensity = 1.4;
				S.SizeKeys = {{0, 1}, {0.5, 0.7}, {1, 0.2}};
				S.ColourKeys = Opacity({{0, 0.9}, {0.4, 0.5}, {1, 0}});
				S.Capacity = 16384;
			}
			{
				FSpec& S = Set(EAcParticle::WeldSparks);  // Models.swift:214
				S.Name = TEXT("WeldSparks");
				S.bSpark = true;
				S.Life = 0.35; S.LifeVariation = 0.2; S.Speed = 2.4; S.SpeedVariation = 1.2; S.Spread = 70;
				S.Size = 0.035; S.SizeVariation = 0.02; S.Rgb = FVector(1, 0.75, 0.35);
				S.bGravity = true; S.Accel = FVector(0, -6, 0); S.Stretch = 0.05; S.Intensity = 3;
				S.Capacity = 4096;
			}
			{
				FSpec& S = Set(EAcParticle::GrenadeTrail);  // Effects.swift:825
				S.Name = TEXT("GrenadeTrail");
				S.Rate = 120; S.Life = 0.4; S.LifeVariation = 0.1; S.Speed = 0.1; S.Spread = 180; S.Size = 0.06;
				S.bWhite = true; S.Rgb = FVector(0.85); S.Alpha = 0.35;
				S.SizeKeys = {{0, 1}, {1, 2.4}};
				S.ColourKeys = Opacity({{0, 1}, {1, 0}});
				S.Capacity = 4096; S.SoftCm = 10;
			}
			{
				FSpec& S = Set(EAcParticle::ShellFire);  // :866
				S.Name = TEXT("ShellFire");
				S.Rate = 160; S.Life = 0.3; S.LifeVariation = 0.08; S.Speed = 0.3; S.Spread = 180; S.Size = 0.24;
				S.Rgb = FVector(0.35, 0.65, 1); S.Intensity = 2;
				S.SizeKeys = {{0, 1}, {1, 0.3}};
				S.Capacity = 2048;
			}
			{
				FSpec& S = Set(EAcParticle::ShellSmoke);  // :880
				S.Name = TEXT("ShellSmoke");
				S.Rate = 60; S.Life = 1.1; S.LifeVariation = 0.3; S.Speed = 0.2; S.Spread = 180; S.Size = 0.12;
				S.Rgb = FVector(0.55, 0.65, 0.8); S.Alpha = 0.35;
				S.SizeKeys = {{0, 1}, {1, 3.2}};
				S.ColourKeys = Opacity({{0, 1}, {1, 0}});
				S.Capacity = 2048; S.SoftCm = 10;
			}
			return T;
		}();
		return Table;
	}

	/// Four keys (time, value) for the shaders: unused keys repeat the last.
	void Pack4(const TArray<FRampKey>& Keys, FLinearColor& Values, FLinearColor& Times)
	{
		float V[4] = {1, 1, 1, 1}, Tm[4] = {0, 1, 1, 1};
		const int32 N = FMath::Min(Keys.Num(), 4);
		for (int32 I = 0; I < 4; ++I)
		{
			const FRampKey& K = N > 0 ? Keys[FMath::Min(I, N - 1)] : FRampKey{float(I > 0 ? 1 : 0), 1.f};
			V[I] = K.Value;
			Tm[I] = I < N ? K.At : 1.f;
		}
		Values = FLinearColor(V[0], V[1], V[2], V[3]);
		Times = FLinearColor(Tm[0], Tm[1], Tm[2], Tm[3]);
	}

	/// A uniformly random direction within `HalfAngle` (radians) of `Axis`.
	FVector InCone(FRandomStream& R, const FVector& Axis, double HalfAngle)
	{
		const double CosMax = FMath::Cos(FMath::Min(HalfAngle, PI));
		const double Z = 1 - R.GetFraction() * (1 - CosMax);
		const double Phi = R.GetFraction() * 2 * PI;
		const double S = FMath::Sqrt(FMath::Max(0.0, 1 - Z * Z));
		const FVector Local(S * FMath::Cos(Phi), S * FMath::Sin(Phi), Z);
		return FQuat::FindBetweenNormals(FVector::UpVector, Axis).RotateVector(Local);
	}

	/// A point on the emitter shape's surface (SceneKit's default
	/// `birthLocation`), SceneKit local, cells.
	FVector OnShape(FRandomStream& R, const FSpec& S, double Scale)
	{
		const double A = S.A * Scale, B = S.B * Scale, C = S.C * Scale;
		switch (S.Shape)
		{
		case EShape::Sphere:
			return R.GetUnitVector() * A;
		case EShape::Cylinder:
		{
			const double Side = 2 * PI * A * B, Cap = PI * A * A;
			const double Pick = R.GetFraction() * (Side + 2 * Cap);
			const double Phi = R.GetFraction() * 2 * PI;
			if (Pick < Side) return FVector(A * FMath::Cos(Phi), (R.GetFraction() - 0.5) * B, A * FMath::Sin(Phi));
			const double Rr = A * FMath::Sqrt(R.GetFraction());
			return FVector(Rr * FMath::Cos(Phi), Pick < Side + Cap ? B * 0.5 : -B * 0.5, Rr * FMath::Sin(Phi));
		}
		case EShape::Box:
		{
			const double Ax = B * C, Ay = A * C, Az = A * B;
			const double Pick = R.GetFraction() * (Ax + Ay + Az);
			const double Sign = R.GetFraction() < 0.5 ? -0.5 : 0.5;
			const double U = R.GetFraction() - 0.5, V = R.GetFraction() - 0.5;
			if (Pick < Ax) return FVector(Sign * A, U * B, V * C);
			if (Pick < Ax + Ay) return FVector(U * A, Sign * B, V * C);
			return FVector(U * A, V * B, Sign * C);
		}
		default:
			return FVector::ZeroVector;
		}
	}

	/// ± half the variation (SceneKit's `…Variation` is the whole range).
	double Vary(FRandomStream& R, double Base, double Variation)
	{
		return Base + (R.GetFraction() - 0.5) * Variation;
	}
}

/// One kind's instances: a ring of `Capacity` quads.
struct FAcParticles::FBatch
{
	UInstancedStaticMeshComponent* Component = nullptr;
	const FSpec* Spec = nullptr;
	FLinearColor BaseLinear = FLinearColor::White;
	TArray<FTransform> Xf;
	TArray<float> Data;
	TArray<double> DiesAt;
	int32 Cursor = 0;
	/// Slots written this frame: from `FirstDirty`, `Dirty` of them (ring).
	int32 FirstDirty = 0, Dirty = 0;
};

FAcEmit FAcEmit::Scaled(const EAcParticle Kind, const double K, const double InDuration)
{
	const FSpec& S = Specs()[int32(Kind)];
	FAcEmit E;
	E.Rate = S.Rate * K;
	E.Duration = InDuration;
	E.Size = S.Size * K;
	E.Speed = S.Speed * FMath::Sqrt(FMath::Max(K, 0.0));
	return E;
}

FAcParticles::FAcParticles() = default;
FAcParticles::~FAcParticles() = default;

bool FAcParticles::Init(AActor* Owner)
{
	bReady = false;
	Batches.Reset();
	if (!Owner) return false;
	UStaticMesh* Quad = LoadObject<UStaticMesh>(nullptr, QuadPath);
	UMaterialInterface* AddM = LoadObject<UMaterialInterface>(nullptr, AddPath);
	UMaterialInterface* SmokeM = LoadObject<UMaterialInterface>(nullptr, SmokePath);
	UMaterialParameterCollection* Mpc = LoadObject<UMaterialParameterCollection>(nullptr, ClockPath);
	UTexture* Smoke = LoadObject<UTexture>(nullptr, SmokeTex);
	UTexture* Spark = LoadObject<UTexture>(nullptr, SparkTex);
	if (!Quad || !AddM || !SmokeM || !Mpc)
	{
		UE_LOG(LogAutocraft, Error, TEXT("particles: missing /Game/Effects assets (run Tools/Editor/make_particle_materials.py): no particles"));
		return false;
	}
	Clock.Reset(Mpc);
	const TArray<FSpec>& Table = Specs();
	for (int32 K = 0; K < Table.Num(); ++K)
	{
		const FSpec& S = Table[K];
		TUniquePtr<FBatch>& B = Batches.Add_GetRef(MakeUnique<FBatch>());
		B->Spec = &S;
		UInstancedStaticMeshComponent* C = NewObject<UAcInstancedMesh>(Owner,  // cheap bounds (P2)
			FName(*FString::Printf(TEXT("AcParticles_%s"), S.Name)));
		C->SetMobility(EComponentMobility::Movable);
		C->SetStaticMesh(Quad);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCanEverAffectNavigation(false);
		C->SetCastShadow(false);
		C->bAffectDistanceFieldLighting = false;
		C->bAffectDynamicIndirectLighting = false;
		C->SetReceivesDecals(false);
		C->bUseAsOccluder = false;
		C->SetNumCustomDataFloats(Floats);
		UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(S.bAdditive ? AddM : SmokeM, C);
		// The kind's physics and ramps (see make_particle_materials.py).
		M->SetScalarParameterValue(TEXT("Damping"), float(S.Damping));
		const FVector Acc = AcSpace::AxesFromSceneKit(S.Accel.X, S.Accel.Y - (S.bGravity ? Gravity : 0.0), S.Accel.Z) * Cm;
		M->SetVectorParameterValue(TEXT("Accel"), FLinearColor(float(Acc.X), float(Acc.Y), float(Acc.Z), 0.f));
		FLinearColor SizeK, SizeT;
		Pack4(S.SizeKeys, SizeK, SizeT);
		M->SetVectorParameterValue(TEXT("SizeKeys"), SizeK);
		M->SetVectorParameterValue(TEXT("SizeTimes"), SizeT);
		{
			FLinearColor Col[4], Times(0, 1, 1, 1);
			const int32 N = FMath::Min(S.ColourKeys.Num(), 4);
			float Tm[4] = {0, 1, 1, 1};
			for (int32 I = 0; I < 4; ++I)
			{
				Col[I] = N > 0 ? S.ColourKeys[FMath::Min(I, N - 1)].Value : FLinearColor::White;
				Tm[I] = I < N ? S.ColourKeys[I].At : 1.f;
			}
			Times = FLinearColor(Tm[0], Tm[1], Tm[2], Tm[3]);
			for (int32 I = 0; I < 4; ++I) M->SetVectorParameterValue(FName(*FString::Printf(TEXT("Col%d"), I)), Col[I]);
			M->SetVectorParameterValue(TEXT("ColTimes"), Times);
		}
		M->SetScalarParameterValue(TEXT("Stretch"), float(S.Stretch));
		if (UTexture* Tex = S.bSpark ? Spark : Smoke) M->SetTextureParameterValue(TEXT("SpriteTex"), Tex);
		M->SetScalarParameterValue(TEXT("SoftCm"), float(S.bAdditive ? FMath::Min(S.SoftCm, S.Size * Cm) : S.SoftCm));
		C->SetMaterial(0, M);
		C->SetupAttachment(Owner->GetRootComponent());
		C->RegisterComponent();
		Owner->AddInstanceComponent(C);

		// The ring, all collapsed (birth far in the past).
		const int32 N = FMath::Max(16, S.Capacity);
		B->Xf.Init(FTransform(FQuat::Identity, FVector::ZeroVector, FVector(0.01)), N);
		B->Data.Init(0.f, N * Floats);
		for (int32 I = 0; I < N; ++I)
		{
			B->Data[I * Floats + 0] = -1e6f;
			B->Data[I * Floats + 1] = 1.f;
		}
		B->DiesAt.Init(-1e9, N);
		C->AddInstances(B->Xf, false, true, false);
		C->SetCustomData(0, N - 1, B->Data, false);
		C->MarkRenderInstancesDirty();
		B->Component = C;
		// Colour (calibrated → linear), for kinds without variation.
		B->BaseLinear = S.bWhite ? White(S.Rgb.X, S.Alpha) : Cal(S.Rgb.X, S.Rgb.Y, S.Rgb.Z, S.Alpha);
	}
	bReady = true;
	UE_LOG(LogAutocraft, Log, TEXT("particles: %d kinds ready"), Batches.Num());
	return true;
}

void FAcParticles::Burst(const EAcParticle Kind, const FTransform& Frame, const double Time, const FAcEmit& Emit)
{
	if (!bReady) return;
	const FSpec& S = *Batches[int32(Kind)]->Spec;
	const double Rate = (Emit.Rate >= 0 ? Emit.Rate : S.Rate) * CVarParticleRate.GetValueOnGameThread();
	const double Want = Rate * FMath::Max(Emit.Duration, 0.0);
	int32 N = FMath::FloorToInt(Want);
	if (Random.GetFraction() < Want - N) ++N;
	if (N > 0) Spawn(Kind, Frame, Frame, Time, Time + FMath::Max(Emit.Duration, 0.0), N, Emit);
}

void FAcParticles::Stream(const EAcParticle Kind, const uint64 Key, const FTransform& Frame, const double Rate,
	const double Time, const double Dt, const FAcEmit& Emit)
{
	if (!bReady || Dt <= 0) return;
	const double Want = FMath::Max(Rate, 0.0) * Dt * CVarParticleRate.GetValueOnGameThread();
	FTransform From = Frame;
	double Have = Want;
	if (Key != 0)
	{
		const uint64 K = Key ^ (uint64(Kind) << 56);
		FTrack& T = Tracks.FindOrAdd(K);
		// An emitter seen again after a gap starts afresh where it is.
		if (T.SeenAt > 0 && Time - T.SeenAt <= 0.25) From = T.Last;
		Have = T.Carry + Want;
		T.Last = Frame;
		T.SeenAt = Time;
		const int32 N = FMath::FloorToInt(Have);
		T.Carry = Have - N;
		if (N > 0) Spawn(Kind, From, Frame, Time - Dt, Time, N, Emit);
		return;
	}
	int32 N = FMath::FloorToInt(Have);
	if (Random.GetFraction() < Have - N) ++N;
	if (N > 0) Spawn(Kind, From, Frame, Time - Dt, Time, N, Emit);
}

void FAcParticles::Spawn(const EAcParticle Kind, const FTransform& From, const FTransform& To, const double T0,
	const double T1, int32 Count, const FAcEmit& Emit)
{
	FBatch& B = *Batches[int32(Kind)];
	const FSpec& S = *B.Spec;
	const int32 Cap = B.Xf.Num();
	Count = FMath::Min(Count, Cap);
	const double Speed = Emit.Speed >= 0 ? Emit.Speed : S.Speed;
	const double SpeedVar = Emit.SpeedVariation >= 0 ? Emit.SpeedVariation : S.SpeedVariation;
	const double Size = Emit.Size >= 0 ? Emit.Size : S.Size;
	const double SizeVar = Emit.SizeVariation >= 0 ? Emit.SizeVariation : S.SizeVariation;
	const FVector DirLocal = AcSpace::AxesFromSceneKit(S.Direction.X, S.Direction.Y, S.Direction.Z).GetSafeNormal();
	const double Half = FMath::DegreesToRadians(S.Spread);
	const bool bVary = S.bHsb || S.Variation.X != 0 || S.Variation.Y != 0 || S.Variation.Z != 0 || S.Variation.W != 0;
	const FVector BaseHsb = S.bHsb ? S.Rgb : RgbToHsb(S.Rgb);
	double Intensity = S.Intensity;
	FLinearColor Base = S.bColourReplaces ? FLinearColor(1, 1, 1, 1) : B.BaseLinear;
	// Travel bound for culling: the farthest a particle can fly, cm.
	const double MaxSpeed = (Speed + SpeedVar * 0.5) * Cm;
	const double MaxLife = S.Life + S.LifeVariation * 0.5;
	const FVector AccCm = AcSpace::AxesFromSceneKit(S.Accel.X, S.Accel.Y - (S.bGravity ? Gravity : 0.0), S.Accel.Z) * Cm;
	double Reach = S.Damping > 1e-3 ? MaxSpeed / S.Damping + AccCm.Size() * MaxLife / S.Damping
	                                : MaxSpeed * MaxLife + 0.5 * AccCm.Size() * MaxLife * MaxLife;
	double MaxKey = 1;
	for (const FRampKey& K : S.SizeKeys) MaxKey = FMath::Max(MaxKey, double(K.Value));
	Reach += (Size + SizeVar) * Cm * MaxKey * 2 + 2 * S.Stretch * MaxSpeed + S.A * Emit.ShapeScale * Cm;
	const FVector Extent(FMath::Max(Reach, 10.0) / 50.0);

	if (B.Dirty == 0) B.FirstDirty = B.Cursor;
	for (int32 I = 0; I < Count; ++I)
	{
		const double U = Count > 1 ? (double(I) + Random.GetFraction()) / Count : Random.GetFraction();
		const double Birth = FMath::Lerp(T0, T1, U);
		FTransform At;
		At.Blend(From, To, float(U));
		const FVector P = At.TransformPosition(AcSpace::AxesFromSceneKit(0, 0, 0))
			+ At.GetRotation().RotateVector(
				[&] { const FVector L = OnShape(Random, S, Emit.ShapeScale); return AcSpace::AxesFromSceneKit(L.X, L.Y, L.Z) * Cm; }());
		const FVector Dir = InCone(Random, At.GetRotation().RotateVector(DirLocal), Half);
		const double V = FMath::Max(0.0, Vary(Random, Speed, SpeedVar)) * Cm;
		const double Life = FMath::Max(0.02, Vary(Random, S.Life, S.LifeVariation));
		const double Sz = FMath::Max(0.0, Vary(Random, Size, SizeVar)) * Cm;
		FLinearColor Col = Base;
		if (bVary && !S.bColourReplaces)
		{
			const FVector Hsb(BaseHsb.X + (Random.GetFraction() - 0.5) * S.Variation.X,
				FMath::Clamp(BaseHsb.Y + (Random.GetFraction() - 0.5) * S.Variation.Y, 0.0, 1.0),
				FMath::Clamp(BaseHsb.Z + (Random.GetFraction() - 0.5) * S.Variation.Z, 0.0, 1.0));
			const FVector Rgb = HsbToRgb(Hsb.X, Hsb.Y, Hsb.Z);
			Col = Cal(Rgb.X, Rgb.Y, Rgb.Z, FMath::Clamp(S.Alpha + (Random.GetFraction() - 0.5) * S.Variation.W, 0.0, 1.0));
		}
		const int32 Slot = B.Cursor;
		B.Cursor = (B.Cursor + 1) % Cap;
		B.Xf[Slot] = FTransform(FQuat::Identity, P, Extent);
		float* D = &B.Data[Slot * Floats];
		D[0] = float(Birth);
		D[1] = float(Life);
		const FVector Vel = Dir * V;
		D[2] = float(Vel.X);
		D[3] = float(Vel.Y);
		D[4] = float(Vel.Z);
		D[5] = float(Sz);
		D[6] = float(Col.R * Intensity);
		D[7] = float(Col.G * Intensity);
		D[8] = float(Col.B * Intensity);
		D[9] = Col.A;
		D[10] = Random.GetFraction();
		B.DiesAt[Slot] = Birth + Life;
	}
	B.Dirty = FMath::Min(B.Dirty + Count, Cap);
	Stats.Born += Count;
}

void FAcParticles::Update(UWorld* World, const double DisplayTime)
{
	if (!bReady) return;
	if (World && Clock.IsValid())
	{
		if (UMaterialParameterCollectionInstance* I = World->GetParameterCollectionInstance(Clock.Get()))
		{
			I->SetScalarParameterValue(TEXT("Time"), float(DisplayTime));
		}
	}
	Stats.Uploaded = 0;
	for (TUniquePtr<FBatch>& P : Batches)
	{
		FBatch& B = *P;
		if (B.Dirty == 0 || !B.Component) continue;
		const int32 Cap = B.Xf.Num();
		auto Send = [&B](const int32 Start, const int32 N)
		{
			B.Component->BatchUpdateInstancesTransforms(Start, TArrayView<const FTransform>(B.Xf.GetData() + Start, N), true, false, true);
			B.Component->SetCustomData(Start, Start + N - 1,
				TConstArrayView<float>(B.Data.GetData() + Start * Floats, N * Floats), false);
		};
		const int32 First = FMath::Min(B.Dirty, Cap - B.FirstDirty);
		Send(B.FirstDirty, First);
		if (B.Dirty > First) Send(0, B.Dirty - First);
		B.Component->MarkRenderInstancesDirty();
		Stats.Uploaded += B.Dirty;
		B.Dirty = 0;
	}
	// Forget emitters not seen for a while.
	if (DisplayTime >= PruneAt)
	{
		PruneAt = DisplayTime + 2.0;
		for (auto It = Tracks.CreateIterator(); It; ++It)
		{
			if (DisplayTime - It.Value().SeenAt > 1.0) It.RemoveCurrent();
		}
		Stats.Emitters = Tracks.Num();
	}
}

void FAcParticles::Clear()
{
	Tracks.Reset();
	for (TUniquePtr<FBatch>& P : Batches)
	{
		FBatch& B = *P;
		for (int32 I = 0; I < B.DiesAt.Num(); ++I)
		{
			B.Data[I * Floats + 0] = -1e6f;
			B.DiesAt[I] = -1e9;
		}
		B.FirstDirty = 0;
		B.Dirty = B.Xf.Num();
		B.Cursor = 0;
	}
}

int32 FAcParticles::Alive(const double Time) const
{
	int32 N = 0;
	for (const TUniquePtr<FBatch>& P : Batches)
		for (const double D : P->DiesAt) N += D > Time ? 1 : 0;
	return N;
}
