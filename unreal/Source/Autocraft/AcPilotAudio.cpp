// See AcPilotAudio.h. Swift: Sources/Autocraft/Audio.swift (`start` :183,
// `hearFrom` :307, `place` :330), GameController+Ears.swift,
// GameController+Pilot.swift (`followPilot` :262, `leavePilot` :172,
// `sightPoint` :505).
#include "AcPilotAudio.h"

#include "AcAudioDirector.h"
#include "AcLog.h"
#include "AcPilotPawn.h"
#include "AcRtsPawn.h"
#include "AcSimSubsystem.h"
#include "AcSpace.h"
#include "AcTerrain.h"

#include "AudioDevice.h"
#include "Components/AudioComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Sound/AudioSettings.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundSubmix.h"
#include "SubmixEffects/AudioMixerSubmixEffectReverb.h"

#include "Rules.h"
#include "Simulation.h"

#include <algorithm>
#include <cmath>

static TAutoConsoleVariable<int32> CVarAcPilotHrtf(TEXT("ac.PilotHrtf"), 0,
	TEXT("Driving: 0 places each sound so UE's stereo panner gives Swift's measured HRTF ear levels (default); ")
	TEXT("1 plays it at its true place with the engine's HRTF algorithm (a spatialisation plugin, else plain panning)."));
static TAutoConsoleVariable<int32> CVarAcPilotAudioLog(TEXT("ac.PilotAudioLog"), 0,
	TEXT("1: log every placed world sound while driving, each frame (distance, direction, ear levels, blocked, reverb)."));
static TAutoConsoleVariable<float> CVarAcPilotHallDecay(TEXT("ac.PilotHallDecay"), 1.9f,
	TEXT("The pilot hall's DecayTime (UE's plate reverb), read when the world starts. Swift's mediumHall falls 31.5 dB/s. NOT calibrated: ")
	TEXT("the offline mixer does not run submix effects (AcPilotAudio.h)."));
static TAutoConsoleVariable<float> CVarAcPilotHallWet(TEXT("ac.PilotHallWet"), 1.25f,
	TEXT("The pilot hall's wet level, read when the world starts: the tail should hold -13.2 dB x blend of the source (Swift). ")
	TEXT("NOT calibrated (an estimate: a plate's tail near its input's energy at 1)."));

namespace
{
	/// Swift's `.HRTFHQ` levels at each ear (dB of a mono source), measured
	/// offline from `AVAudioEnvironmentNode` (unreal/Tools/Audio/avstage_grid.swift):
	/// [elevation][azimuth], azimuth 0…345° in 15° steps (+ = right),
	/// elevations below. Listener facing −Z, y up, 5 cells out, rolloff 0.
	constexpr double Elevations[9] = {-90, -60, -30, -15, 0, 15, 30, 60, 90};
	constexpr float LeftDb[9][24] = {
		{-12.92f, -12.92f, -12.92f, -12.92f, -12.92f, -12.92f, -12.92f, -12.92f, -12.92f, -12.92f, -12.92f, -12.92f, -12.92f, -12.92f, -12.92f, -12.92f, -12.92f, -12.92f, -12.92f, -12.92f, -12.92f, -12.92f, -12.92f, -12.92f},
		{-5.53f, -9.78f, -11.14f, -15.50f, -16.23f, -17.26f, -17.73f, -16.58f, -17.06f, -15.63f, -13.56f, -11.26f, -9.91f, -8.34f, -5.96f, -5.11f, -4.42f, -1.70f, -3.66f, -3.97f, -2.60f, -1.90f, -6.12f, -7.25f},
		{-1.78f, -5.39f, -8.03f, -11.18f, -14.10f, -16.63f, -16.38f, -17.20f, -16.32f, -13.87f, -10.25f, -6.53f, -5.05f, -3.34f, -0.76f, 0.69f, 1.47f, 1.60f, 2.22f, 0.95f, 0.13f, 0.16f, 1.05f, 0.14f},
		{-1.45f, -5.37f, -9.05f, -12.12f, -13.88f, -15.02f, -15.43f, -16.87f, -15.54f, -12.62f, -9.30f, -6.62f, -5.50f, -4.06f, -1.72f, -0.32f, 0.45f, 0.55f, 1.08f, 0.48f, 0.08f, 0.05f, 1.46f, 1.47f},
		{-0.94f, -5.30f, -9.57f, -13.19f, -14.31f, -15.29f, -14.04f, -15.02f, -15.04f, -12.62f, -9.77f, -7.09f, -5.64f, -4.21f, -1.68f, -0.28f, 0.44f, 0.45f, 0.66f, 0.71f, 1.29f, 0.01f, 1.31f, 2.00f},
		{-1.97f, -6.60f, -10.18f, -12.88f, -14.71f, -15.47f, -13.14f, -15.27f, -15.35f, -13.01f, -9.89f, -6.89f, -5.30f, -4.00f, -1.44f, 0.21f, 0.62f, 0.43f, 0.75f, 0.36f, 0.74f, 0.28f, 0.70f, 1.12f},
		{-2.82f, -6.26f, -9.93f, -12.75f, -14.20f, -15.37f, -12.54f, -12.97f, -12.88f, -11.33f, -8.69f, -5.64f, -3.76f, -3.04f, -1.87f, -0.42f, 0.07f, 0.19f, 0.74f, 0.68f, 1.03f, 1.28f, 1.56f, 0.56f},
		{-2.66f, -4.70f, -6.68f, -8.09f, -8.89f, -8.89f, -8.98f, -9.70f, -10.22f, -10.11f, -8.76f, -6.78f, -5.36f, -4.23f, -3.42f, -2.77f, -2.40f, -2.19f, -1.60f, -0.72f, 0.14f, 0.38f, 0.00f, -0.96f},
		{-3.12f, -3.12f, -3.12f, -3.12f, -3.12f, -3.12f, -3.12f, -3.12f, -3.12f, -3.12f, -3.12f, -3.12f, -3.12f, -3.12f, -3.12f, -3.12f, -3.12f, -3.12f, -3.12f, -3.12f, -3.12f, -3.12f, -3.12f, -3.12f},
	};
	constexpr float RightDb[9][24] = {
		{-12.56f, -12.56f, -12.56f, -12.56f, -12.56f, -12.56f, -12.56f, -12.56f, -12.56f, -12.56f, -12.56f, -12.56f, -12.56f, -12.56f, -12.56f, -12.56f, -12.56f, -12.56f, -12.56f, -12.56f, -12.56f, -12.56f, -12.56f, -12.56f},
		{-7.80f, -4.44f, -3.31f, -0.49f, -2.03f, -3.42f, -2.91f, -2.62f, -4.14f, -5.13f, -6.04f, -7.51f, -8.78f, -10.93f, -13.00f, -15.07f, -16.59f, -16.57f, -17.33f, -17.82f, -16.76f, -15.22f, -11.95f, -7.95f},
		{-2.04f, 0.04f, 0.92f, 1.39f, 1.06f, 0.76f, 0.80f, 1.49f, 0.77f, -0.12f, -1.64f, -3.74f, -6.06f, -7.81f, -10.80f, -14.16f, -16.72f, -17.60f, -16.77f, -16.95f, -15.31f, -11.46f, -8.91f, -5.09f},
		{-1.34f, 0.92f, 1.69f, 1.31f, 0.98f, 0.31f, 0.15f, 0.79f, 0.24f, -0.89f, -1.92f, -4.09f, -6.55f, -7.73f, -10.27f, -13.54f, -16.09f, -17.46f, -16.01f, -15.53f, -13.28f, -11.59f, -9.11f, -4.86f},
		{-0.42f, 1.51f, 2.21f, 1.22f, 1.47f, 0.37f, 0.00f, 1.02f, 0.65f, -0.53f, -1.79f, -4.54f, -6.30f, -7.67f, -10.32f, -13.38f, -15.02f, -15.83f, -14.02f, -15.28f, -14.77f, -12.84f, -8.88f, -4.28f},
		{-1.13f, 1.35f, 1.95f, 1.30f, 1.21f, 0.25f, -0.02f, 0.67f, 0.48f, -0.65f, -2.28f, -4.78f, -5.88f, -7.42f, -10.13f, -13.31f, -15.57f, -15.64f, -13.39f, -14.98f, -14.91f, -12.94f, -9.63f, -5.57f},
		{-2.10f, 0.97f, 2.25f, 1.96f, 1.65f, 0.80f, 0.36f, 0.40f, -0.00f, -1.31f, -2.63f, -3.93f, -4.97f, -6.99f, -9.70f, -12.31f, -14.19f, -14.55f, -11.94f, -14.91f, -13.74f, -12.62f, -10.34f, -5.90f},
		{-3.40f, -1.33f, -0.04f, 0.85f, 0.71f, 0.08f, -0.96f, -2.10f, -2.59f, -3.34f, -3.83f, -4.55f, -6.06f, -7.68f, -8.94f, -10.27f, -11.05f, -10.30f, -9.14f, -9.15f, -9.32f, -9.13f, -7.83f, -5.50f},
		{-2.67f, -2.67f, -2.67f, -2.67f, -2.67f, -2.67f, -2.67f, -2.67f, -2.67f, -2.67f, -2.67f, -2.67f, -2.67f, -2.67f, -2.67f, -2.67f, -2.67f, -2.67f, -2.67f, -2.67f, -2.67f, -2.67f, -2.67f, -2.67f},
	};

	float Lookup(const float (&Table)[9][24], double Az, double El)
	{
		Az = std::fmod(std::fmod(Az, 360.0) + 360.0, 360.0);
		El = FMath::Clamp(El, -90.0, 90.0);
		const double AzF = Az / 15.0;
		const int32 A0 = FMath::Clamp(int32(std::floor(AzF)), 0, 23);
		const int32 A1 = (A0 + 1) % 24;
		const double TA = AzF - A0;
		int32 E0 = 0;
		while (E0 < 7 && El > Elevations[E0 + 1]) ++E0;
		const double TE = (El - Elevations[E0]) / (Elevations[E0 + 1] - Elevations[E0]);
		// Interpolate the levels as power, not dB (a null between two
		// measured directions must not get deeper than either).
		auto P = [](float Db) { return std::pow(10.0, Db / 10.0); };
		const double Lo = P(Table[E0][A0]) * (1 - TA) + P(Table[E0][A1]) * TA;
		const double Hi = P(Table[E0 + 1][A0]) * (1 - TA) + P(Table[E0 + 1][A1]) * TA;
		return static_cast<float>(std::sqrt(Lo * (1 - TE) + Hi * TE));
	}
}

// MARK: - The math

float AcPilotAudio::ReverbBlend(const double DistanceCells, const double RangeCells)
{
	return static_cast<float>(std::min(0.45, 0.06 + 0.4 * DistanceCells / std::max(RangeCells, 1.0)));
}

void AcPilotAudio::Hrtf(const double AzimuthDeg, const double ElevationDeg, float& OutLeft, float& OutRight)
{
	OutLeft = Lookup(LeftDb, AzimuthDeg, ElevationDeg);
	OutRight = Lookup(RightDb, AzimuthDeg, ElevationDeg);
}

FAcVoice3D AcPilotAudio::Place(const FAcEars& Ears, const FVector& At, const double RangeCells, const bool bBlocked, const bool bTrueHrtf)
{
	FAcVoice3D V;
	V.At = At;
	// The driven unit's own sounds: just ahead, not inside the head.
	if (FVector::Dist(V.At, Ears.At) < AcSpace::ToCm(Ahead)) V.At = Ears.At + Ears.Forward * AcSpace::ToCm(Ahead);
	const FVector D = V.At - Ears.At;
	V.Distance = AcSpace::ToCells(D.Size());
	// Direction in the ears' frame (UE is left-handed: right = up × forward).
	const FVector F = Ears.Forward.GetSafeNormal();
	const FVector U = (Ears.Up - F * FVector::DotProduct(Ears.Up, F)).GetSafeNormal();
	const FVector R = FVector::CrossProduct(U, F);
	const FVector N = D.GetSafeNormal();
	V.Azimuth = FMath::RadiansToDegrees(std::atan2(FVector::DotProduct(N, R), FVector::DotProduct(N, F)));
	V.Elevation = FMath::RadiansToDegrees(std::asin(FMath::Clamp(FVector::DotProduct(N, U), -1.0, 1.0)));
	V.bBlocked = bBlocked;
	V.Blend = ReverbBlend(V.Distance, RangeCells);
	const float Dry = std::sqrt(1.f - V.Blend);
	const float Wet = std::sqrt(V.Blend);
	const float Obstruction = bBlocked ? ObstructedGain : 1.f;
	V.LowPassHz = bBlocked ? ObstructedLowPassHz : 0.f;
	float Level = 1.f;
	if (bTrueHrtf)
	{
		V.PlayAt = V.At;
		V.Left = V.Right = 1.f;
		V.Pan = 0.5f;
	}
	else
	{
		Hrtf(V.Azimuth, V.Elevation, V.Left, V.Right);
		// UE's equal-power panner gives cos and sin of pan·π/2: with the
		// volume carrying √(L² + R²) the two ears get exactly L and R.
		Level = std::sqrt(V.Left * V.Left + V.Right * V.Right);
		V.Pan = static_cast<float>(std::atan2(V.Right, V.Left) / UE_HALF_PI);
	}
	V.Gain = Obstruction * Dry * Level;
	// The hall's send rides on the voice's volume: take the dry factors back
	// out, so the reverb is the blend's share alone (obstruction filters the
	// direct path only).
	V.Send = V.Gain > 1e-4f ? FMath::Min(1.f, SendScale * Wet / V.Gain) : 0.f;
	return V;
}

FVector AcPilotAudio::VirtualPoint(const FVector& Loc, const FVector& Front, const FVector& Right, const float Pan)
{
	// UE's stereo channels sit at azimuth 270 (left) and 90 (right): pan p
	// is azimuth (p − ½)·180° in front.
	const double A = (FMath::Clamp(Pan, 0.f, 1.f) - 0.5) * UE_PI;
	return Loc + (Front * std::cos(A) + Right * std::sin(A)) * 100.0;
}

bool AcPilotAudio::SoundBlocked(const FVector& A, const FVector& B, const std::function<double(ac::Vec2)>& Ground,
	const std::vector<ac::Structure>& Structures)
{
	// In cells, SceneKit-style: ground (x, z), height y.
	const ac::Vec3 a(AcSpace::ToCells(A.X), AcSpace::ToCells(A.Z), AcSpace::ToCells(A.Y));
	const ac::Vec3 b(AcSpace::ToCells(B.X), AcSpace::ToCells(B.Z), AcSpace::ToCells(B.Y));
	const ac::Vec3 d = b - a;
	const double Len = std::sqrt(d.x * d.x + d.z * d.z);
	if (!(Len > 1.2)) return false;
	const ac::Vec2 Src(b.x, b.z);
	const ac::Vec2 Lo(std::min(a.x, b.x), std::min(a.z, b.z)), Hi(std::max(a.x, b.x), std::max(a.z, b.z));
	std::vector<const ac::Structure*> Near;
	for (const ac::Structure& S : Structures)
	{
		const double r = ac::Rules::radius(S.kind);
		if (S.position.x + r > Lo.x && S.position.x - r < Hi.x && S.position.y + r > Lo.y && S.position.y - r < Hi.y
			&& !(std::abs(Src.x - S.position.x) < r + 0.3 && std::abs(Src.y - S.position.y) < r + 0.3))
		{
			Near.push_back(&S);
		}
	}
	// Every half cell, leaving out the ends (the ears' and the sound's own ground).
	const int32 Steps = static_cast<int32>(Len / 0.5);
	if (Steps <= 2) return false;
	for (int32 I = 1; I < Steps; ++I)
	{
		const double T = double(I) / Steps;
		const ac::Vec3 p(a.x + d.x * T, a.y + d.y * T, a.z + d.z * T);
		const ac::Vec2 g(p.x, p.z);
		if (Ground(g) > p.y + 0.1) return true;
		for (const ac::Structure* S : Near)
		{
			const double r = ac::Rules::radius(S->kind);
			if (std::abs(g.x - S->position.x) < r && std::abs(g.y - S->position.y) < r
				&& p.y < Ground(S->position) + std::max(1.2, r * 1.5))
			{
				return true;
			}
		}
	}
	return false;
}

FVector AcPilotAudio::SightPoint(const FVector& Camera, const FVector& Forward, const TOptional<FVector>& ChaseEye,
	const TOptional<FVector>& Crosshair, const double ReachCm)
{
	const FVector From = ChaseEye ? *ChaseEye : Camera;
	const FVector Aim = Crosshair ? *Crosshair : Camera + Forward * ReachCm;
	const FVector D = Aim - From;
	const double Len = D.Size();
	return From + D / std::max(Len, 1e-2) * std::min(Len, ReachCm);
}

// MARK: - The subsystem

UAcPilotAudio* UAcPilotAudio::Get(const UObject* WorldContext)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	return World ? World->GetSubsystem<UAcPilotAudio>() : nullptr;
}

bool UAcPilotAudio::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UAcPilotAudio::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UAcSimSubsystem>();
	Collection.InitializeDependency<UAcAudioDirector>();
	bLogFlag = FParse::Param(FCommandLine::Get(), TEXT("AcPilotAudioLog"));
	bProbe = FParse::Param(FCommandLine::Get(), TEXT("AcPilotAudioProbe"));
}

void UAcPilotAudio::Deinitialize()
{
	if (UAcSimSubsystem* Sim = GetWorld() ? GetWorld()->GetSubsystem<UAcSimSubsystem>() : nullptr)
	{
		Sim->RemoveFrameListener(EAcFrameStage::Other, FrameHandle);
	}
	Super::Deinitialize();
}

void UAcPilotAudio::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	// Placed round the ears; no distance model of its own (`Listener.gain`
	// does that: rolloff 0), no engine reverb, occlusion or focus.
	Spatial = NewObject<USoundAttenuation>(this);
	FSoundAttenuationSettings& A = Spatial->Attenuation;
	A.bAttenuate = false;
	A.bSpatialize = true;
	A.SpatializationAlgorithm = CVarAcPilotHrtf.GetValueOnGameThread() == 1 ? ESoundSpatializationAlgorithm::SPATIALIZATION_HRTF
		: ESoundSpatializationAlgorithm::SPATIALIZATION_Default;
	A.NonSpatializedRadiusStart = 0.f;
	A.NonSpatializedRadiusEnd = 0.f;
	A.bAttenuateWithLPF = false;
	A.bEnableOcclusion = false;
	A.bEnableReverbSend = false;
	A.bEnablePriorityAttenuation = false;
	A.bEnableListenerFocus = false;
	MakeHall();
	if (UAcSimSubsystem* Sim = InWorld.GetSubsystem<UAcSimSubsystem>())
	{
		FrameHandle = Sim->AddFrameListener(EAcFrameStage::Other, FAcFrameEvent::FDelegate::CreateUObject(this, &UAcPilotAudio::OnFrame));
	}
}

void UAcPilotAudio::MakeHall()
{
	// Swift's `mediumHall` at −4 dB: wet only, ~1.9 s to fall 60 dB; the
	// tail should hold Swift's share (`HallTailDb` × blend) of the source,
	// which arrives here at `SendScale`. The decay and wet level are
	// estimates (ac.PilotHallDecay, ac.PilotHallWet): the offline mixer runs
	// no submix effects, so they could not be measured (AcPilotAudio.h).
	HallReverb = NewObject<USubmixEffectReverbPreset>(this);
	FSubmixEffectReverbSettings R;
	float Decay = CVarAcPilotHallDecay.GetValueOnGameThread(), Wet = CVarAcPilotHallWet.GetValueOnGameThread();
	// Calibration runs: -AcPilotHallDecay=S -AcPilotHallWet=W (the cvars are read before -ExecCmds runs).
	FParse::Value(FCommandLine::Get(), TEXT("AcPilotHallDecay="), Decay);
	FParse::Value(FCommandLine::Get(), TEXT("AcPilotHallWet="), Wet);
	R.DecayTime = Decay;
	R.Density = 0.85f;
	R.Diffusion = 0.85f;
	R.ReflectionsDelay = 0.02f;
	R.ReflectionsGain = 0.3f;
	R.LateDelay = 0.03f;
	R.LateGain = 1.26f;
	// The late reverb's own gain (0 dB): the struct's default 0 silences it.
	R.Gain = 1.f;
	R.GainHF = 0.8f;
	R.DecayHFRatio = 0.8f;
	R.WetLevel = FMath::Clamp(Wet, 0.f, 10.f);
	R.DryLevel = 0.f;
	HallReverb->SetSettings(R);
	UE_LOG(LogAutocraft, Log, TEXT("pilot audio: hall decay %.2f, wet %.2f"), R.DecayTime, R.WetLevel);
	Hall = NewObject<USoundSubmix>(this, TEXT("AcPilotHall"));
	Hall->SubmixEffectChain.Add(HallReverb);
	// A reverb's tail is quiet: never let the mixer cut it as "silent".
	Hall->bAutoDisable = false;
	// Into the main mix: a submix made at run time with no parent is wired
	// to the "base default" one, which this project has not got (offline its
	// output went nowhere).
	if (USoundSubmixBase* Main = Cast<USoundSubmixBase>(GetDefault<UAudioSettings>()->MasterSubmix.TryLoad()))
	{
		Hall->ParentSubmix = Main;
	}
	if (FAudioDevice* Device = GetWorld() ? GetWorld()->GetAudioDeviceRaw() : nullptr)
	{
		Device->RegisterSoundSubmix(Hall, true);
	}
}

FAcVoice3D UAcPilotAudio::Voice(const FAcEars& Ears, const FVector& At, const bool bBlocked) const
{
	double Range = AcPilotAudio::DefaultRange;
	if (const UAcSimSubsystem* Sim = GetWorld() ? GetWorld()->GetSubsystem<UAcSimSubsystem>() : nullptr; Sim && Sim->IsRunning())
	{
		if (const std::optional<ac::Listener>& L = Sim->Session().listener) Range = L->range;
	}
	const bool bTrue = CVarAcPilotHrtf.GetValueOnGameThread() == 1;
	FAcVoice3D V = AcPilotAudio::Place(Ears, At, Range, bBlocked, bTrue);
	if (!bTrue)
	{
		FVector Loc = Ears.At, Front = Ears.Forward, Right = FVector::CrossProduct(Ears.Up, Ears.Forward).GetSafeNormal();
		if (const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
		{
			PC->GetAudioListenerPosition(Loc, Front, Right);
		}
		V.PlayAt = AcPilotAudio::VirtualPoint(Loc, Front, Right, V.Pan);
	}
	return V;
}

void UAcPilotAudio::Apply(UAudioComponent* C, const FAcVoice3D* V) const
{
	if (!C) return;
	const float Hz = V ? V->LowPassHz : 0.f;
	C->SetLowPassFilterEnabled(Hz > 0.f);
	if (Hz > 0.f) C->SetLowPassFilterFrequency(Hz);
	if (Hall) C->SetSubmixSend(Hall, V ? V->Send : 0.f);
}

bool UAcPilotAudio::Logging() const
{
	return bLogFlag || CVarAcPilotAudioLog.GetValueOnGameThread() != 0;
}

void UAcPilotAudio::Log(const FName Sound, const FAcVoice3D& V, const float Volume) const
{
	if (!Logging()) return;
	UE_LOG(LogAutocraft, Log,
		TEXT("pilot audio: %s d %.2f az %.0f el %.0f L %.1f R %.1f dB, blocked %d, blend %.3f, gain %.3f, pan %.3f, lpf %.0f, send %.3f, volume %.4f"),
		*Sound.ToString(), V.Distance, V.Azimuth, V.Elevation, 20.f * std::log10(std::max(V.Left, 1e-6f)),
		20.f * std::log10(std::max(V.Right, 1e-6f)), V.bBlocked ? 1 : 0, V.Blend, V.Gain, V.Pan, V.LowPassHz, V.Send, Volume);
}

bool UAcPilotAudio::Blocked(const FVector& Ears, const FVector& Sound) const
{
	const UAcSimSubsystem* Sim = GetWorld() ? GetWorld()->GetSubsystem<UAcSimSubsystem>() : nullptr;
	if (!Sim || !Sim->IsRunning()) return false;
	const AAcTerrain* Terrain = AAcTerrain::Find(GetWorld());
	return AcPilotAudio::SoundBlocked(Ears, Sound, [Terrain](ac::Vec2 P) { return Terrain ? Terrain->FieldHeight(P) : 0.0; },
		Sim->State().structures);
}

void UAcPilotAudio::OnFrame(const FAcFrame& Frame)
{
	UAcSimSubsystem* Sim = GetWorld() ? GetWorld()->GetSubsystem<UAcSimSubsystem>() : nullptr;
	UAcAudioDirector* Audio = UAcAudioDirector::Get(this);
	if (!Sim || !Sim->IsRunning() || !Audio) return;
	const AAcPilotPawn* Pawn = AAcPilotPawn::Find(this);
	const ac::Simulation& S = Sim->Simulation();
	const std::optional<ac::Unit> U = Pawn && Pawn->Driving() ? S.state.unit(Pawn->Driven()) : std::nullopt;
	std::optional<ac::Listener>& L = Sim->Session().listener;
	if (U)
	{
		// `moveEars(to: u.position, facing: u.look)`: the reach of every
		// sound is from the driven unit (the window's listener, range 24, if
		// there is none yet: GameController.swift:186).
		if (!L)
		{
			L = ac::Listener{};
			L->range = AcPilotAudio::WindowRange;
		}
		L->position = U->position;
		L->facing = U->look();
		// Where its misses land (`pilotMissed`): the sound with the dust.
		const double Reach = S.weapon(*U).range + ac::Rules::radius(U->kind);
		const FVector Spent = AcPilotAudio::SightPoint(Pawn->GetActorLocation(), Pawn->GetActorRotation().Vector(), Pawn->ChaseEye(),
			Pawn->Crosshair() ? TOptional<FVector>(Pawn->Crosshair()->Point) : TOptional<FVector>(), AcSpace::ToCm(Reach));
		Audio->Rules().PilotSpent = AcSpace::ToSim(Spent);
		bWasDriving = true;
		if (bProbe) Probe(Frame);
	}
	else if (bWasDriving)
	{
		// `leavePilot`: back to the camera's centre, facing up the screen.
		bWasDriving = false;
		Audio->Rules().PilotSpent.reset();
		if (L)
		{
			if (const AAcRtsPawn* Rts = AAcRtsPawn::Get(this); Rts && Rts->View()) L->position = Rts->View()->target;
			L->facing.reset();
		}
	}
}

void UAcPilotAudio::Probe(const FAcFrame& Frame)
{
	// The offline check (AcPilotAudio.h): the game's own sounds hushed, one
	// probe a second round the ears, 6 cells out on the ground, each a
	// single fixed variant so their levels compare:
	//   0-4  the wind (one variant, steady, broadband) 0.8 s ahead, right, behind,
	//        left, then ahead with the obstruction forced on;
	//   5    a hit mark (0.22 s) ahead, its hall send forced off (the dry);
	//   7    the same with its send (the hall's tail after it, 2 s);
	//   9    the same blocked (the tail must stay).
	UAcAudioDirector* Audio = UAcAudioDirector::Get(this);
	const AAcPilotPawn* Pawn = AAcPilotPawn::Find(this);
	UWorld* World = GetWorld();
	if (!Audio || !Pawn || !World) return;
	Audio->SetMuted(true);
	ProbeClock += Frame.RealDelta;
	for (int32 I = ProbeStops.Num() - 1; I >= 0; --I)
	{
		if (ProbeStops[I].Value > ProbeClock) continue;
		if (UAudioComponent* C = ProbeStops[I].Key.Get()) C->Stop();
		ProbeStops.RemoveAtSwap(I);
	}
	// From 3 s in: the take-over's report-in line has played out.
	const int32 Step = static_cast<int32>(std::floor(ProbeClock - 3.0));
	if (Step == ProbeStep || Step < 0 || Step > 9 || Step == 6 || Step == 8) return;
	ProbeStep = Step;
	const int32 Slot = Step < 4 ? Step : 0;
	FAcEars Ears;
	Ears.At = Pawn->GetActorLocation();
	Ears.Forward = Pawn->GetActorRotation().Vector();
	Ears.Up = FRotationMatrix(Pawn->GetActorRotation()).GetUnitAxis(EAxis::Z);
	const FVector F = Ears.Forward.GetSafeNormal2D();
	const FVector R = FVector::CrossProduct(FVector::UpVector, F);
	const FVector Dirs[4] = {F, R, -F, -R};
	const ac::Vec2 Ground = AcSpace::ToSim(Ears.At + Dirs[Slot] * AcSpace::ToCm(6.0));
	const AAcTerrain* Terrain = AAcTerrain::Find(World);
	const FVector At = AcSpace::ToWorld(Ground, (Terrain ? Terrain->FieldHeight(Ground) : 0.0) + 0.7);
	const FName Name = Step < 5 ? FName("wind") : FName("hitmark");
	USoundWave* Wave = Audio->Sound(Name, 0);
	if (!Wave) return;
	FAcVoice3D V = Voice(Ears, At, Step == 4 || Step == 9);
	if (Step == 5) V.Send = 0.f;
	const float Volume = 0.5f * V.Gain * AcSound::Volume;
	const bool bFlat = Audio->Flat();
	UAudioComponent* C = bFlat ? UGameplayStatics::SpawnSound2D(World, Wave, Volume, 1.f, 0.f, nullptr, false, true)
		: UGameplayStatics::SpawnSoundAtLocation(World, Wave, V.PlayAt, FRotator::ZeroRotator, Volume, 1.f, 0.f, Spatial, nullptr, true);
	Apply(C, &V);
	if (C && Step < 5) ProbeStops.Add({C, ProbeClock + 0.8});
	static const TCHAR* Names[10] = {TEXT("ahead"), TEXT("right"), TEXT("behind"), TEXT("left"), TEXT("ahead, blocked"),
		TEXT("ahead, dry only"), TEXT(""), TEXT("ahead, with the hall"), TEXT(""), TEXT("ahead, blocked, with the hall")};
	UE_LOG(LogAutocraft, Log, TEXT("pilot audio: probe %d %s %s at frame %llu (%s)"), Step, *Name.ToString(), Names[Step], GFrameCounter,
		bFlat ? TEXT("flat") : TEXT("placed"));
	bLogFlag = true;
	Log(Name, V, Volume);
	bLogFlag = FParse::Param(FCommandLine::Get(), TEXT("AcPilotAudioLog"));
}
