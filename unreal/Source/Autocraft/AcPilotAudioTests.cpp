// The pilot's 3D hearing (AcPilotAudio.h, chunk E7) against the Swift stage:
// `reverbBlend`, `soundBlocked`, `sightPoint`, and the HRTF levels measured
// offline from AVAudioEnvironmentNode (Tools/Audio/avstage_grid.swift). Run:
//   UnrealEditor Autocraft.uproject -ExecCmds="Automation RunTests Autocraft.Audio.Pilot; Quit"
//     -unattended -nullrhi -nosplash -nosound -log
#include "AcPilotAudio.h"
#include "AcSettings.h"

#include "Misc/AutomationTest.h"

#include "Rules.h"
#include "Types.h"

#include <cmath>

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags Flags =
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter;

	float Db(float V) { return 20.f * std::log10(V); }

	/// Ears at 2 m over the origin, facing UE +X, up +Z (right is +Y).
	FAcEars Ears()
	{
		FAcEars E;
		E.At = FVector(0, 0, 200);
		E.Forward = FVector(1, 0, 0);
		E.Up = FVector(0, 0, 1);
		return E;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcPilotAudioBlendTest, "Autocraft.Audio.Pilot.ReverbBlend", Flags)
bool FAcPilotAudioBlendTest::RunTest(const FString&)
{
	// min(0.45, 0.06 + 0.4 · d / max(range, 1)) (Audio.swift:340).
	TestEqual(TEXT("at the ears"), AcPilotAudio::ReverbBlend(0, 16), 0.06f, 1e-6f);
	TestEqual(TEXT("half the range"), AcPilotAudio::ReverbBlend(8, 16), 0.26f, 1e-6f);
	TestEqual(TEXT("past 97.5%"), AcPilotAudio::ReverbBlend(16, 16), 0.45f, 1e-6f);
	TestEqual(TEXT("range under 1"), AcPilotAudio::ReverbBlend(0.5, 0.2), 0.26f, 1e-6f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcPilotAudioHrtfTest, "Autocraft.Audio.Pilot.Hrtf", Flags)
bool FAcPilotAudioHrtfTest::RunTest(const FString&)
{
	float L = 0, R = 0;
	// Measured grid points come back as measured.
	AcPilotAudio::Hrtf(0, 0, L, R);
	TestEqual(TEXT("ahead L"), Db(L), -0.94f, 0.01f);
	TestEqual(TEXT("ahead R"), Db(R), -0.42f, 0.01f);
	AcPilotAudio::Hrtf(90, 0, L, R);
	TestEqual(TEXT("right L"), Db(L), -14.04f, 0.01f);
	TestEqual(TEXT("right R"), Db(R), 0.00f, 0.01f);
	AcPilotAudio::Hrtf(-90, 0, L, R);
	TestEqual(TEXT("left L"), Db(L), 0.66f, 0.01f);
	TestEqual(TEXT("left R"), Db(R), -14.02f, 0.01f);
	AcPilotAudio::Hrtf(180, 0, L, R);
	TestEqual(TEXT("behind L"), Db(L), -5.64f, 0.01f);
	AcPilotAudio::Hrtf(37, -90, L, R);
	TestEqual(TEXT("below, any azimuth"), Db(L), -12.92f, 0.01f);
	// Between grid points: between its neighbours' levels.
	AcPilotAudio::Hrtf(7.5, -7.5, L, R);
	TestTrue(TEXT("interpolated"), Db(R) > -1.4f && Db(R) < 1.6f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcPilotAudioPlaceTest, "Autocraft.Audio.Pilot.Place", Flags)
bool FAcPilotAudioPlaceTest::RunTest(const FString&)
{
	const FAcEars E = Ears();
	// 6 cells to the right, level with the ears: UE's equal-power panner at
	// `Pan` with the volume's level gives Swift's two ears.
	FAcVoice3D V = AcPilotAudio::Place(E, FVector(0, 600, 200), 16, false);
	TestEqual(TEXT("distance"), V.Distance, 6.0, 1e-6);
	TestEqual(TEXT("azimuth"), V.Azimuth, 90.0, 1e-6);
	TestEqual(TEXT("elevation"), V.Elevation, 0.0, 1e-6);
	const float Level = std::sqrt(V.Left * V.Left + V.Right * V.Right);
	TestEqual(TEXT("left ear"), Db(Level * std::cos(V.Pan * UE_HALF_PI)), -14.04f, 0.01f);
	TestEqual(TEXT("right ear"), Db(Level * std::sin(V.Pan * UE_HALF_PI)), 0.0f, 0.01f);
	const float Blend = 0.06f + 0.4f * 6.f / 16.f;
	TestEqual(TEXT("blend"), V.Blend, Blend, 1e-6f);
	TestEqual(TEXT("gain = dry share × level"), V.Gain, std::sqrt(1.f - Blend) * Level, 1e-5f);
	TestEqual(TEXT("send = wet share / gain (scaled)"), V.Send, AcPilotAudio::SendScale * std::sqrt(Blend) / V.Gain, 1e-5f);
	TestEqual(TEXT("open"), V.LowPassHz, 0.f);
	// Blocked: −1.75 dB and the 3 kHz low-pass on the direct path; the
	// hall's share unchanged (its send rises by as much).
	const FAcVoice3D B = AcPilotAudio::Place(E, FVector(0, 600, 200), 16, true);
	TestEqual(TEXT("obstructed gain"), B.Gain, V.Gain * AcPilotAudio::ObstructedGain, 1e-5f);
	TestEqual(TEXT("low-pass"), B.LowPassHz, 3000.f);
	TestEqual(TEXT("reverb kept"), B.Send * B.Gain, V.Send * V.Gain, 1e-5f);
	// Behind, and below.
	V = AcPilotAudio::Place(E, FVector(-500, 0, 200), 16, false);
	TestEqual(TEXT("behind"), FMath::Abs(V.Azimuth), 180.0, 1e-6);
	V = AcPilotAudio::Place(E, FVector(300, -300, 200 - 300 * std::sqrt(2.0)), 16, false);
	TestEqual(TEXT("left-ahead azimuth"), V.Azimuth, -45.0, 1e-6);
	TestEqual(TEXT("below"), V.Elevation, -45.0, 1e-6);
	// The driven unit's own sound: 0.4 cells ahead, not in the head.
	V = AcPilotAudio::Place(E, FVector(10, 5, 190), 16, false);
	TestEqual(TEXT("own sound ahead"), V.At, FVector(40, 0, 200));
	TestEqual(TEXT("own sound straight ahead"), V.Azimuth, 0.0, 1e-6);
	// The virtual point pans as UE's stereo channels sit (270 left, 90 right).
	const FVector P = AcPilotAudio::VirtualPoint(FVector::ZeroVector, FVector(1, 0, 0), FVector(0, 1, 0), 1.f);
	TestTrue(TEXT("hard right is +Y"), P.Equals(FVector(0, 100, 0), 1e-3));
	const FVector C = AcPilotAudio::VirtualPoint(FVector::ZeroVector, FVector(1, 0, 0), FVector(0, 1, 0), 0.5f);
	TestTrue(TEXT("centre ahead"), C.Equals(FVector(100, 0, 0), 1e-3));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcPilotAudioBlockedTest, "Autocraft.Audio.Pilot.SoundBlocked", Flags)
bool FAcPilotAudioBlockedTest::RunTest(const FString&)
{
	std::vector<ac::Structure> None;
	auto Flat = [](ac::Vec2) { return 0.0; };
	// Ears 1.5 over flat ground, a sound 0.7 over it 10 cells off: clear.
	TestFalse(TEXT("flat"), AcPilotAudio::SoundBlocked(FVector(0, 0, 150), FVector(1000, 0, 70), Flat, None));
	// A 3-cell ridge half way: blocked.
	auto Ridge = [](ac::Vec2 P) { return std::abs(P.x - 5) < 1 ? 3.0 : 0.0; };
	TestTrue(TEXT("ridge"), AcPilotAudio::SoundBlocked(FVector(0, 0, 150), FVector(1000, 0, 70), Ridge, None));
	// Up to 1.2 cells along the ground nothing counts.
	TestFalse(TEXT("close"), AcPilotAudio::SoundBlocked(FVector(0, 0, 150), FVector(110, 0, 70), Ridge, None));
	// A Citadel in between, below its roof: blocked; the Citadel a sound
	// comes from does not count.
	ac::Structure Citadel;
	Citadel.kind = ac::StructureKind::citadel;
	Citadel.position = ac::Vec2(5, 0);
	std::vector<ac::Structure> One{Citadel};
	TestTrue(TEXT("through a building"), AcPilotAudio::SoundBlocked(FVector(0, 0, 150), FVector(1000, 0, 70), Flat, One));
	TestFalse(TEXT("from the building"), AcPilotAudio::SoundBlocked(FVector(-500, 0, 150), FVector(500, 0, 70), Flat, One));
	// High over the roof: clear.
	const double Roof = std::max(1.2, ac::Rules::radius(ac::StructureKind::citadel) * 1.5);
	TestFalse(TEXT("over the roof"),
		AcPilotAudio::SoundBlocked(FVector(0, 0, (Roof + 1) * 100), FVector(1000, 0, (Roof + 1) * 100), Flat, One));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcPilotAudioSightTest, "Autocraft.Audio.Pilot.SightPoint", Flags)
bool FAcPilotAudioSightTest::RunTest(const FString&)
{
	// First person: from the camera toward the crosshair, at most the reach.
	FVector P = AcPilotAudio::SightPoint(FVector(0, 0, 200), FVector(1, 0, 0), {}, FVector(2000, 0, 0), 500);
	TestTrue(TEXT("reach-limited"), P.Equals(FVector(0, 0, 200) + (FVector(2000, 0, -200)).GetSafeNormal() * 500, 1e-3));
	P = AcPilotAudio::SightPoint(FVector(0, 0, 200), FVector(1, 0, 0), {}, FVector(300, 0, 0), 500);
	TestTrue(TEXT("on the crosshair"), P.Equals(FVector(300, 0, 0), 1e-3));
	// Third person: from the unit's eye, not the camera behind it.
	P = AcPilotAudio::SightPoint(FVector(-400, 0, 300), FVector(1, 0, 0), FVector(0, 0, 150), FVector(2000, 0, 150), 500);
	TestTrue(TEXT("from the chase eye"), P.Equals(FVector(500, 0, 150), 1e-3));
	// Nothing under the sight: the reach straight ahead of the camera.
	P = AcPilotAudio::SightPoint(FVector(0, 0, 200), FVector(1, 0, 0), {}, {}, 500);
	TestTrue(TEXT("no crosshair"), P.Equals(FVector(500, 0, 200), 1e-3));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcAudioSettingsGainTest, "Autocraft.Audio.Pilot.SettingsVolumes", Flags)
bool FAcAudioSettingsGainTest::RunTest(const FString&)
{
	// `UAcSettings` scales Swift's defaults (volume 0.6, music 0.5), live.
	UAcSettings& S = UAcSettings::Get();
	const float Master = S.MasterVolume, Music = S.MusicVolume;
	const bool bMusic = S.bMusic;
	S.MasterVolume = 1.f;
	S.MusicVolume = 1.f;
	S.bMusic = true;
	TestEqual(TEXT("master at full"), AcSound::MasterGain(), AcSound::Volume, 1e-6f);
	TestEqual(TEXT("music at full"), AcSound::MusicGain(), AcSound::MusicVolume, 1e-6f);
	S.MasterVolume = 0.5f;
	S.MusicVolume = 0.25f;
	TestEqual(TEXT("master half"), AcSound::MasterGain(), AcSound::Volume * 0.5f, 1e-6f);
	TestEqual(TEXT("music quarter"), AcSound::MusicGain(), AcSound::MusicVolume * 0.25f, 1e-6f);
	S.MasterVolume = 3.f;
	TestEqual(TEXT("clamped"), AcSound::MasterGain(), AcSound::Volume, 1e-6f);
	S.bMusic = false;
	TestEqual(TEXT("music off"), AcSound::MusicGain(), 0.f);
	S.MasterVolume = Master;
	S.MusicVolume = Music;
	S.bMusic = bMusic;
	return true;
}

#endif
