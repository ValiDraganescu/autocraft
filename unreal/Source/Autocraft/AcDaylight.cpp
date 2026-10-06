#include "AcDaylight.h"

#include "AcGasGiant.h"
#include "AcSkyFx.h"
#include "AcLog.h"
#include "AcSimSubsystem.h"
#include "AcSpace.h"
#include "AcTerrain.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/PlayerController.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace
{
	TAutoConsoleVariable<float> CVarHour(
		TEXT("ac.Hour"), -1.0f,
		TEXT("Hold the day clock at this hour (0-24), for shots; negative: the game's own clock. ")
		TEXT("Also -AcHour=H on the command line (AUTOCRAFT_HOUR in the Swift game)."));
	TAutoConsoleVariable<float> CVarDayLength(
		TEXT("ac.DayLength"), 600.0f,
		TEXT("Seconds of game time in one day and night (Daylight.length)."));
	TAutoConsoleVariable<int32> CVarClouds(
		TEXT("ac.Daylight.Clouds"), 1,
		TEXT("Volumetric clouds and their shadows over the map (1) or none (0)."));
	TAutoConsoleVariable<int32> CVarVolumetricFog(
		TEXT("ac.Daylight.VolumetricFog"), 1,
		TEXT("Volumetric fog in the haze (1) or plain height fog (0)."));
	TAutoConsoleVariable<float> CVarHazeGain(
		TEXT("ac.Daylight.HazeGain"), 1.0f,
		TEXT("Brightness of Swift's haze rising from the horizon on the sky dome and the pilot's veil."));
	TAutoConsoleVariable<float> CVarFog(
		TEXT("ac.Daylight.Fog"), 1.0f,
		TEXT("Scale on the height fog's density (0: no haze)."));
	TAutoConsoleVariable<float> CVarToneCurve(
		TEXT("ac.Daylight.ToneCurve"), 0.6f,
		TEXT("How much of Unreal's filmic tone curve the picture gets (0: SceneKit-like clip, 1: all)."));
	TAutoConsoleVariable<int32> CVarIsolate(
		TEXT("ac.Daylight.Isolate"), 0,
		TEXT("Calibration: light with one part only. 0 all, 1 sun and moon, 2 fill, 3 sky light."));

	// Calibration against the Swift renders (`windowshot` with AUTOCRAFT_HOUR,
	// side by side with `-AcShot` at the same hours). SceneKit's light
	// intensity 1000 is 1.0; these map it to lux at manual exposure EV 0.
	constexpr double SunLux = 2.9 / 1000.0;
	constexpr double MoonLux = 3.2 / 1000.0;
	constexpr double FillLux = 1.9 / 1000.0;
	/// The sky light's intensity for Swift's ambient 1.0 (its environment
	/// map at 0.75 by day, 0.12 at night).
	constexpr double SkyLightScale = 1.3;
	/// C6 (the A2-fix finding): shade and cliff faces read darker and the
	/// ground redder than Swift, whose studio ambient is bluer. A cvar lift
	/// on the sky light and its tint, tuned against Swift `bench --shot base`
	/// with the fog off at noon: cliffs, shade, plateau, lowland and grass
	/// within ~5% (they were 15-25% dark and short of blue).
	TAutoConsoleVariable<float> CVarAmbient(
		TEXT("ac.Daylight.Ambient"), 3.0f,
		TEXT("Scale on the sky light over the Swift ambient tone (C6 calibration)."));
	TAutoConsoleVariable<float> CVarAmbientBlue(
		TEXT("ac.Daylight.AmbientBlue"), 1.6f,
		TEXT("Blue tint of the sky light (1: neutral; green between 1 and this)."));
	/// The captured night sky is far darker than Swift's studio map at 0.12,
	/// so unlit night ground read ~60% of Swift's: the sky light is lifted by
	/// this much with the sun 18° or more below the horizon (none above -8°).
	TAutoConsoleVariable<float> CVarNightAmbient(
		TEXT("ac.Daylight.NightAmbient"), 3.5f,
		TEXT("Lift on the sky light at full dark (1: none); unlit night ground against Swift."));
	/// How much the atmosphere's low sun or moon is lifted at the horizon,
	/// fading out by 25°.
	constexpr double LowLightLift = 3.0;
	/// The atmosphere's sun and moon (they light the sky, not the ground).
	constexpr double SkySunLux = 6.0;
	constexpr double SkyMoonLux = 2.0;
	constexpr float FogDensity = 0.012f;

	const TCHAR* SkyMaterialPath = TEXT("/Game/Sky/M_AcSky.M_AcSky");
	const TCHAR* DomeMeshPath = TEXT("/Engine/BasicShapes/Sphere.Sphere");
	const TCHAR* CloudMaterialPath = TEXT("/Engine/EngineSky/VolumetricClouds/m_SimpleVolumetricCloud_Inst.m_SimpleVolumetricCloud_Inst");
	/// The engine's clouds thinning out toward the horizon as Swift's
	/// (`Tools/Editor/make_cloud_material.py`); the engine's if missing.
	const TCHAR* AcCloudMaterialPath = TEXT("/Game/Sky/MI_AcCloud.MI_AcCloud");

	double Smoothstep(double A, double B, double X)
	{
		const double T = FMath::Clamp((X - A) / (B - A), 0.0, 1.0);
		return T * T * (3 - 2 * T);
	}

	FLinearColor Mix(const FLinearColor& A, const FLinearColor& B, double T)
	{
		return A * float(1 - T) + B * float(T);
	}

	FLinearColor Rgb(double R, double G, double B) { return FLinearColor(float(R), float(G), float(B), 1.0f); }

	/// A Swift colour (sRGB, as `NSColor(calibratedRed:...)` takes it) in
	/// linear light, as Unreal's light colours want it.
	FLinearColor Lin(const FLinearColor& C) { return FLinearColor::FromSRGBColor(C.ToFColor(false)); }
}

double AAcDaylight::DayLength() { return FMath::Max(1.0, double(CVarDayLength.GetValueOnGameThread())); }

double AAcDaylight::HeldHour() { return CVarHour.GetValueOnGameThread(); }

double AAcDaylight::HourAt(const double Time)
{
	const double Held = HeldHour();
	if (Held >= 0) return FMath::Fmod(Held, 24.0);
	return FMath::Fmod(StartHour + Time / DayLength() * 24.0, 24.0);
}

FVector AAcDaylight::SunSceneKit(const double H)
{
	const FVector Noon = FVector(-60, 110, -55).GetSafeNormal();
	const FVector West = FVector(-Noon.Z, 0, Noon.X).GetSafeNormal();
	const double Day = Sunset - Sunrise;
	const double A = H >= Sunrise && H <= Sunset
		? (H - Sunrise) / Day * PI - PI / 2
		: FMath::Fmod(H - Sunset + 24, 24.0) / (24 - Day) * PI + PI / 2;
	return FMath::Sin(A) * West + FMath::Cos(A) * Noon;
}

FVector AAcDaylight::SunToward(const double H)
{
	const FVector S = SunSceneKit(H);
	return AcSpace::AxesFromSceneKit(S.X, S.Y, S.Z).GetSafeNormal();
}

AAcDaylight::AAcDaylight()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
	Root->SetMobility(EComponentMobility::Movable);

	auto MakeLight = [this](const TCHAR* Name) {
		UDirectionalLightComponent* L = CreateDefaultSubobject<UDirectionalLightComponent>(Name);
		L->SetupAttachment(Root);
		L->SetMobility(EComponentMobility::Movable);
		return L;
	};
	// The sun and the moon on the ground: the shadows (virtual shadow maps).
	Sun = MakeLight(TEXT("Sun"));
	Sun->bAtmosphereSunLight = false;
	Sun->LightSourceAngle = 0.5357f;
	Sun->CastShadows = true;
	Sun->VolumetricScatteringIntensity = 0.0f;
	Moon = MakeLight(TEXT("Moon"));
	Moon->bAtmosphereSunLight = false;
	Moon->LightSourceAngle = 0.52f;
	Moon->CastShadows = false;
	Moon->VolumetricScatteringIntensity = 0.0f;
	// The same in the sky: atmosphere lights 0 and 1, lighting no geometry
	// (no lighting channel), but the sky, the fog's light shafts and the
	// clouds.
	auto MakeSkyLight = [&](const TCHAR* Name, int32 Index) {
		UDirectionalLightComponent* L = MakeLight(Name);
		L->bAtmosphereSunLight = true;
		L->AtmosphereSunLightIndex = Index;
		L->CastShadows = false;
		L->LightingChannels.bChannel0 = false;
		L->LightingChannels.bChannel1 = false;
		L->LightingChannels.bChannel2 = false;
		L->bAffectTranslucentLighting = false;
		return L;
	};
	SkySun = MakeSkyLight(TEXT("SkySun"), 0);
	SkySun->LightSourceAngle = 0.5357f;
	SkyMoon = MakeSkyLight(TEXT("SkyMoon"), 1);
	SkyMoon->LightSourceAngle = 0.52f;
	SkyMoon->AtmosphereSunDiskColorScale = FLinearColor(0.9f, 0.92f, 1.0f);
	// The fill: from the side opposite the noon sun, no shadows, lights
	// neither the sky nor the fog.
	Fill = MakeLight(TEXT("Fill"));
	Fill->bAtmosphereSunLight = false;
	Fill->CastShadows = false;
	Fill->VolumetricScatteringIntensity = 0.0f;
	Fill->bAffectTranslucentLighting = true;

	SkyLight = CreateDefaultSubobject<USkyLightComponent>(TEXT("SkyLight"));
	SkyLight->SetupAttachment(Root);
	SkyLight->SetMobility(EComponentMobility::Movable);
	SkyLight->bRealTimeCapture = true;
	SkyLight->SourceType = SLS_CapturedScene;
	// What the capture cannot see below the horizon: the ground's own
	// bounce is Lumen's; keep the lower hemisphere dim, not black.
	SkyLight->bLowerHemisphereIsBlack = false;
	SkyLight->LowerHemisphereColor = FLinearColor(0.05f, 0.045f, 0.04f);

	Atmosphere = CreateDefaultSubobject<USkyAtmosphereComponent>(TEXT("Atmosphere"));
	Atmosphere->SetupAttachment(Root);

	Fog = CreateDefaultSubobject<UExponentialHeightFogComponent>(TEXT("Fog"));
	Fog->SetupAttachment(Root);
	Fog->SetMobility(EComponentMobility::Movable);
	// A light ground haze: the map stays readable from the top-down camera
	// (about 50 m up), the far border and the pilot's horizon melt into it.
	Fog->FogDensity = FogDensity;
	Fog->FogHeightFalloff = 0.35f;
	Fog->StartDistance = 2500.0f;
	Fog->FogMaxOpacity = 0.85f;
	// Not on the sky dome (25 km): the pilot's veil (E4) paints the far
	// ground with the bare atmosphere, so a fogged dome showed the far
	// mountains as a paler outline against it (B9).
	Fog->FogCutoffDistance = 2.0e6f;
	Fog->bEnableVolumetricFog = true;
	Fog->VolumetricFogScatteringDistribution = 0.35f;
	Fog->VolumetricFogExtinctionScale = 0.6f;
	Fog->VolumetricFogDistance = 12000.0f;
	Fog->SkyAtmosphereAmbientContributionColorScale = FLinearColor(0.35f, 0.35f, 0.35f);

	Clouds = CreateDefaultSubobject<UVolumetricCloudComponent>(TEXT("Clouds"));
	Clouds->SetupAttachment(Root);

	Dome = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Dome"));
	Dome->SetupAttachment(Root);
	Dome->SetMobility(EComponentMobility::Movable);
	Dome->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Dome->SetCastShadow(false);
	Dome->bAffectDistanceFieldLighting = false;
	Dome->bAffectDynamicIndirectLighting = false;
	Dome->bVisibleInRayTracing = false;
	Dome->bNeverDistanceCull = true;
	Dome->SetGenerateOverlapEvents(false);
	// The basic sphere is 50 cm across its radius: 5 · 10^4 times that is a
	// 25 km dome, past everything but the clouds' far edge.
	Dome->SetRelativeScale3D(FVector(5.0e4));

	Exposure = CreateDefaultSubobject<UPostProcessComponent>(TEXT("Exposure"));
	Exposure->SetupAttachment(Root);
	Exposure->bUnbound = true;
	Exposure->Priority = -10.0f;
	FPostProcessSettings& P = Exposure->Settings;
	P.bOverride_AutoExposureMethod = true;
	P.AutoExposureMethod = AEM_Manual;
	P.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
	P.AutoExposureApplyPhysicalCameraExposure = false;
	P.bOverride_AutoExposureBias = true;
	P.AutoExposureBias = 0.0f;
	// The Swift camera's grading (`CameraRig.makeCamera`): no exposure
	// adaptation, saturation 1.12, contrast +0.12, gentle bloom over 0.95.
	// SceneKit has no filmic curve (it clips): part of Unreal's is kept so
	// highlights roll off, but less of it, so the ground keeps its colour.
	P.bOverride_ColorSaturation = true;
	P.ColorSaturation = FVector4(1.12, 1.12, 1.12, 1.0);
	// Keep bright cores (explosions, flames) white instead of crushing their blue (C3).
	P.bOverride_ColorSaturationHighlights = true;
	P.ColorSaturationHighlights = FVector4(0.85, 0.85, 0.85, 1.0);
	P.bOverride_ColorContrast = true;
	P.ColorContrast = FVector4(1.12, 1.12, 1.12, 1.0);
	P.bOverride_ToneCurveAmount = true;
	P.ToneCurveAmount = 0.6f;
	P.bOverride_BloomIntensity = true;
	P.BloomIntensity = 0.5f;
	P.bOverride_BloomThreshold = true;
	P.BloomThreshold = 0.95f;
	P.bOverride_VignetteIntensity = true;
	P.VignetteIntensity = 0.0f;
	P.bOverride_LocalExposureHighlightContrastScale = true;
	P.LocalExposureHighlightContrastScale = 1.0f;
	P.bOverride_LocalExposureShadowContrastScale = true;
	P.LocalExposureShadowContrastScale = 1.0f;
}

FLinearColor AAcDaylight::GetHazeRise() const
{
	FLinearColor H = FLinearColor::FromSRGBColor(Tones.Haze.ToFColor(false)) * CVarHazeGain.GetValueOnGameThread();
	H.A = float(Tones.Rise);
	return H;
}

AAcDaylight* AAcDaylight::Find(const UWorld* World)
{
	if (!World) return nullptr;
	TActorIterator<AAcDaylight> It(const_cast<UWorld*>(World));
	return It ? *It : nullptr;
}

AAcDaylight* AAcDaylight::SpawnFor(UWorld* World)
{
	if (!World) return nullptr;
	if (AAcDaylight* Existing = Find(World)) return Existing;
	float Held = -1;
	if (FParse::Value(FCommandLine::Get(), TEXT("AcHour="), Held))
	{
		CVarHour->Set(Held, ECVF_SetByCommandline);
	}
	FActorSpawnParameters Params;
	Params.Name = TEXT("AcDaylight");
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AAcDaylight* Daylight = World->SpawnActor<AAcDaylight>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
	return Daylight;
}

void AAcDaylight::BeginPlay()
{
	Super::BeginPlay();
	UMaterialInterface* Cloud = LoadObject<UMaterialInterface>(nullptr, AcCloudMaterialPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!Cloud) Cloud = LoadObject<UMaterialInterface>(nullptr, CloudMaterialPath);
	if (Cloud)
	{
		Clouds->SetMaterial(Cloud);
	}
	UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, DomeMeshPath);
	UMaterialInterface* SkyMaterial = LoadObject<UMaterialInterface>(nullptr, SkyMaterialPath);
	if (Sphere && SkyMaterial)
	{
		Dome->SetStaticMesh(Sphere);
		DomeMaterial = UMaterialInstanceDynamic::Create(SkyMaterial, this);
		Dome->SetMaterial(0, DomeMaterial);
	}
	else
	{
		UE_LOG(LogAutocraft, Warning, TEXT("daylight: no sky dome (%s missing: run Tools/Editor/make_sky_material.py)"), SkyMaterialPath);
		Dome->SetVisibility(false);
	}
	const UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this);
	Update(Sim ? Sim->GameTime() : 0.0);
	UE_LOG(LogAutocraft, Log, TEXT("daylight: hour %.2f%s, sun %.1f°, dark %.2f"), Hour,
	       HeldHour() >= 0 ? TEXT(" (held)") : TEXT(""), Elevation, Dark);
}

void AAcDaylight::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (StandInChecks < 120)
	{
		++StandInChecks;
		RemoveStandIns();
	}
	StageSkyView();
	const UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this);
	Update(Sim ? Sim->GameTime() : 0.0);
}

// Other chunks put up a stand-in sun and sky for their renders when the
// level has none (`-AcTerrainView`, AcTerrain.cpp): this actor is the sky,
// so those go.
void AAcDaylight::RemoveStandIns()
{
	UWorld* World = GetWorld();
	if (!World) return;
	TArray<AActor*> Doomed;
	for (TActorIterator<ADirectionalLight> It(World); It; ++It) Doomed.Add(*It);
	for (TActorIterator<ASkyLight> It(World); It; ++It) Doomed.Add(*It);
	for (TActorIterator<ASkyAtmosphere> It(World); It; ++It) Doomed.Add(*It);
	for (TActorIterator<AExponentialHeightFog> It(World); It; ++It) Doomed.Add(*It);
	for (AActor* A : Doomed)
	{
		UE_LOG(LogAutocraft, Log, TEXT("daylight: removing stand-in %s"), *A->GetName());
		A->Destroy();
	}
}

// -AcSkyView[=YAW] (with -AcShot): a camera 2 m over the middle of the map
// looking 12° up toward YAW (degrees, Unreal; default: the sun's azimuth at
// the hour, or the moon's at night), to show the sky, the clouds and the
// stars, which the top-down view never sees.
void AAcDaylight::StageSkyView()
{
	const TCHAR* Cmd = FCommandLine::Get();
	float Yaw = 0;
	const bool bYaw = FParse::Value(Cmd, TEXT("AcSkyView="), Yaw);
	if (!bYaw && !FParse::Param(Cmd, TEXT("AcSkyView"))) return;
	UWorld* World = GetWorld();
	if (!World || SkyViewFrames > 200) return;
	++SkyViewFrames;
	if (!SkyCamera)
	{
		FVector At = FVector::ZeroVector;
		if (const UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this); Sim && Sim->IsRunning())
		{
			const ac::GroundRect& B = Sim->Map().bounds;
			At = AcSpace::ToWorld(ac::Vec2((B.minX + B.maxX) / 2, (B.minZ + B.maxZ) / 2), 0);
		}
		if (const AAcTerrain* Terrain = AAcTerrain::Find(World); Terrain && Terrain->IsBuilt())
		{
			At.Z = Terrain->GroundZ(At);
		}
		At.Z += 200;
		const FVector Toward = Elevation > 0 ? SunToward(Hour) : -SunToward(Hour);
		const double Look = bYaw ? Yaw : Toward.Rotation().Yaw;
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ACameraActor* Camera = World->SpawnActor<ACameraActor>(At, FRotator(12, Look, 0), Params);
		Camera->GetCameraComponent()->SetConstraintAspectRatio(false);
		Camera->GetCameraComponent()->SetFieldOfView(80);
		SkyCamera = Camera;
		UE_LOG(LogAutocraft, Log, TEXT("daylight: sky view from %s, yaw %.0f"), *At.ToString(), Look);
	}
	if (APlayerController* PC = World->GetFirstPlayerController(); PC && PC->GetViewTarget() != SkyCamera)
	{
		PC->SetViewTarget(SkyCamera);
	}
}

void AAcDaylight::Update(const double Time)
{
	Hour = HourAt(Time);
	const FVector SunDir = SunToward(Hour);
	const double Deg = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(SunDir.Z, -1.0, 1.0)));
	Elevation = Deg;
	const double Abs = FMath::Abs(Deg);
	// 1 by day, 0 by night, through the twilight; the dusk glow peaks with
	// the sun on the horizon.
	const double Light = Smoothstep(-9, 8, Deg);
	const double Glow = Smoothstep(0, 1, 1 - FMath::Abs(Deg - 2) / 11);
	Dark = 1 - Smoothstep(-6, 10, Deg);
	auto Tone = [&](const FLinearColor& Day, const FLinearColor& Night, const FLinearColor& Dusk) {
		return Mix(Mix(Night, Day, Light), Dusk, Glow * 0.85);
	};
	auto ToneV = [&](double Day, double Night, double Dusk) {
		return Tone(Rgb(Day, Day, Day), Rgb(Night, Night, Night), Rgb(Dusk, Dusk, Dusk)).R;
	};

	// The sun, warmer as it sinks; the moon opposite. Shadows fade as the
	// light nears the horizon (they grow very long and sweep fast there).
	const double Fade = Smoothstep(0, 12, Abs);
	Sun->SetWorldRotation((-SunDir).Rotation());
	Moon->SetWorldRotation(SunDir.Rotation());
	SkySun->SetWorldRotation((-SunDir).Rotation());
	SkyMoon->SetWorldRotation(SunDir.Rotation());
	const int32 Isolate = CVarIsolate.GetValueOnGameThread();
	const double KeyGain = (Isolate == 0 || Isolate == 1) ? 1.0 : 0.0;
	const double FillGain = (Isolate == 0 || Isolate == 2) ? 1.0 : 0.0;
	const double SkyGain = (Isolate == 0 || Isolate == 3) ? 1.0 : 0.0;
	// On the ground: the Swift lights, colours and strengths as they are.
	// The gas giant between us and the sun (it sits where the sun rises): dim, not dark.
	const double Eclipsed = 1.0 - 0.55 * FAcGasGiant::Eclipse(SunDir);
	const double SunFull = Deg > 0 ? Eclipsed * 1900 * FMath::Pow(Smoothstep(0, 9, Abs), 0.6) : 0.0;
	const double MoonFull = Deg < 0 ? 650 * FMath::Pow(Smoothstep(0, 10, Abs), 0.6) : 0.0;
	Sun->SetIntensity(float(SunFull * SunLux * KeyGain));
	Sun->SetLightColor(Lin(Mix(Rgb(1, 0.5, 0.28), Rgb(1, 0.93, 0.82), Smoothstep(0, 32, Deg > 0 ? Abs : 0))));
	Moon->SetIntensity(float(MoonFull * MoonLux * KeyGain));
	Moon->SetLightColor(Lin(Rgb(0.5, 0.64, 1)));
	// In the sky: the atmosphere dims a low sun or moon a great deal, and the
	// sky the sky light captures with it; lift it near the horizon so the
	// ambient follows Swift's tone instead. The sun still lights the sky a
	// while after it has set (the twilight).
	auto Lift = [](double E) { return 1.0 + LowLightLift * (1.0 - Smoothstep(0, 25, E)); };
	SkySun->SetIntensity(float(Eclipsed * SkySunLux * Lift(FMath::Max(Deg, 0.0)) * Smoothstep(-12, 0, Deg)));
	SkyMoon->SetIntensity(float(SkyMoonLux * Lift(FMath::Max(-Deg, 0.0)) * Smoothstep(-12, 0, -Deg)));
	Sun->SetShadowAmount(float(0.85 * Fade));
	Moon->SetShadowAmount(float(0.75 * Fade));
	const bool bDay = Deg > 0;
	if (bDay != bSunShadows)
	{
		bSunShadows = bDay;
		Sun->SetCastShadows(bDay);
		Moon->SetCastShadows(!bDay);
	}
	// The one directional light forward-shaded translucency, water and
	// volumetric fog use: the sun by day, the moon by night, never the sky
	// pair or the fill (equal priorities fall back to the brightest, with an
	// on-screen warning). The setters do nothing while the value holds.
	Sun->SetForwardShadingPriority(bDay ? 2 : 1);
	Moon->SetForwardShadingPriority(bDay ? 1 : 2);

	// The cool fill from the opposite side keeps shadowed faces readable.
	Fill->SetWorldRotation((-AcSpace::AxesFromSceneKit(50, 40, 60).GetSafeNormal()).Rotation());
	Fill->SetLightColor(Lin(Tone(Rgb(0.55, 0.65, 0.9), Rgb(0.32, 0.44, 1), Rgb(0.9, 0.55, 0.62))));
	Fill->SetIntensity(float(ToneV(450, 210, 360) * FillLux * FillGain));

	// The ambient: the captured sky, at Swift's ambient tone (relative to day).
	// Only once the sun is well down: at dusk the captured sky is still lit.
	const double NightLift = 1.0 + (CVarNightAmbient.GetValueOnGameThread() - 1.0) * (1.0 - Smoothstep(-18, -8, Deg));
	SkyLight->SetIntensity(float(ToneV(0.75, 0.12, 0.4) / 0.75 * SkyLightScale * CVarAmbient.GetValueOnGameThread() * NightLift * SkyGain));
	// At dawn and dusk the captured sky is red, and the sun on the ground is
	// already the Swift orange: cool the ambient so shade is not red too.
	const double Blue = CVarAmbientBlue.GetValueOnGameThread();
	SkyLight->SetLightColor(Mix(Rgb(1, (1 + Blue) / 2, Blue), Rgb(0.8, 1.0, 1.3), Glow * 0.8));

	// The haze: the background, the fog, the terrain's border. Top down as in
	// Swift; the first-person haze is in the tones.
	Haze = Tone(Rgb(0.27, 0.22, 0.19), Rgb(0.035, 0.045, 0.085), Rgb(0.3, 0.16, 0.12));
	const FLinearColor HazeLinear = FLinearColor::FromSRGBColor(Haze.ToFColor(false));
	Fog->SetFogInscatteringColor(HazeLinear);
	Fog->SetVolumetricFogAlbedo(Tone(Rgb(0.95, 0.9, 0.85), Rgb(0.6, 0.7, 1.0), Rgb(1.0, 0.8, 0.7)).ToFColor(false));
	Fog->SetFogDensity(FogDensity * CVarFog.GetValueOnGameThread());
	Fog->SetVolumetricFog(CVarVolumetricFog.GetValueOnGameThread() != 0);
	const bool bClouds = CVarClouds.GetValueOnGameThread() != 0;
	if (Clouds->IsVisible() != bClouds) Clouds->SetVisibility(bClouds);

	if (const UWorld* World = GetWorld())
	{
		if (AAcTerrain* Terrain = AAcTerrain::Find(World); Terrain && Terrain->GetMaterial())
		{
			const UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this);
			const double Reach = Sim && Sim->IsRunning() ? AAcTerrain::BorderWidth(Sim->Map().bounds) : 90.0;
			const FLinearColor Want(Haze.R, Haze.G, Haze.B, float(Reach));
			if (!Want.Equals(TerrainHaze, 1.0f / 512))
			{
				TerrainHaze = Want;
				Terrain->GetMaterial()->SetVectorParameterValue(TEXT("Haze"), Want);
			}
		}
	}

	// The first-person sky (Swift `Daylight.Sky`), for the pilot's veil.
	Tones.Top = Tone(Rgb(0.07, 0.15, 0.34), Rgb(0.008, 0.012, 0.035), Rgb(0.08, 0.1, 0.26));
	Tones.High = Tone(Rgb(0.2, 0.34, 0.6), Rgb(0.015, 0.025, 0.06), Rgb(0.3, 0.28, 0.45));
	Tones.Horizon = Tone(Rgb(0.52, 0.6, 0.72), Rgb(0.05, 0.06, 0.11), Rgb(0.95, 0.5, 0.28));
	Tones.Haze = Tone(Rgb(0.62, 0.6, 0.58), Rgb(0.06, 0.07, 0.12), Rgb(0.62, 0.4, 0.3));
	Tones.Glow = Rgb(0.9, 0.38, 0.16) * float(Glow);
	Tones.Cloud = Tone(Rgb(0.96, 0.95, 0.93), Rgb(0.07, 0.08, 0.12), Rgb(1, 0.66, 0.5));
	Tones.Shade = Tone(Rgb(0.66, 0.69, 0.76), Rgb(0.035, 0.04, 0.065), Rgb(0.5, 0.36, 0.42));
	Tones.Sun = SunDir;
	Tones.Stars = 1 - Smoothstep(-14, -3, Deg);
	Tones.Rise = 24 + (12 - 24) * Light;
	Tones.Reach = 40 + (110 - 40) * Light;

	Exposure->Settings.ToneCurveAmount = CVarToneCurve.GetValueOnGameThread();
	if (DomeMaterial)
	{
		DomeMaterial->SetVectorParameterValue(TEXT("Haze"), GetHazeRise());
		DomeMaterial->SetScalarParameterValue(TEXT("Stars"), float(Tones.Stars));
		DomeMaterial->SetScalarParameterValue(TEXT("Time"), float(FMath::Fmod(Time, 86400.0)));
		if (!GasGiant) GasGiant = MakeShared<FAcGasGiant>();
		GasGiant->Update(GetWorld(), this, DomeMaterial, SunDir, Dark);
		if (!SkyFx) SkyFx = MakeShared<FAcSkyFx>();
		SkyFx->Update(GetWorld(), this, DomeMaterial, SunDir, Dark);
	}
}
