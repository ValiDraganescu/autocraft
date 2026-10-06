// Headless tests for the runtime MP3 loader. Run:
//   UnrealEditor Autocraft.uproject -ExecCmds="Automation RunTests Autocraft.Audio.Mp3; Quit"
//     -unattended -nullrhi -nosplash -nosound -log
#include "AcMp3Loader.h"

#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Sound/SoundGenerator.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/** The game's own soundtrack (Resources/Sounds/music). */
	FString SoundtrackFile(const TCHAR* Name)
	{
		return FPaths::ConvertRelativePathToFull(
			FPaths::Combine(FPaths::ProjectDir(), TEXT("Resources/Sounds/music"), Name));
	}

	/** Pull `Seconds` of audio out of a generator the way the mixer does. */
	TArray<float> Pull(ISoundGenerator& Gen, int32 Channels, int32 Rate, float Seconds)
	{
		TArray<float> Out;
		TArray<float> Block;
		Block.SetNumUninitialized(1024 * Channels);
		const int64 Want = static_cast<int64>(Seconds * Rate) * Channels;
		while (Out.Num() < Want && !Gen.IsFinished())
		{
			const int32 N = Gen.GetNextBuffer(Block.GetData(), Block.Num());
			Out.Append(Block.GetData(), N);
		}
		return Out;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcMp3LoaderTest, "Autocraft.Audio.Mp3.Soundtrack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FAcMp3LoaderTest::RunTest(const FString& Parameters)
{
	// aftermath_1.mp3: afinfo says 2 ch, 48000 Hz, 194.424 s.
	const FString Path = SoundtrackFile(TEXT("aftermath_1.mp3"));
	TestTrue(TEXT("soundtrack file exists"), FPaths::FileExists(Path));

	FAcMp3Info Info;
	if (!TestTrue(TEXT("probe"), UAcMp3Loader::Probe(Path, Info)))
	{
		return false;
	}
	AddInfo(FString::Printf(TEXT("%s: %d Hz, %d ch, %lld frames, %.3f s"), *Path, Info.SampleRate, Info.NumChannels, Info.NumFrames, Info.Duration));
	TestEqual(TEXT("sample rate"), Info.SampleRate, 48000);
	TestEqual(TEXT("channels"), Info.NumChannels, 2);
	TestTrue(FString::Printf(TEXT("duration %.3f s within 0.1 s of 194.424"), Info.Duration), FMath::Abs(Info.Duration - 194.424f) < 0.1f);

	// The streaming wave.
	UAcMp3Wave* Wave = UAcMp3Loader::LoadMp3(Path);
	if (!TestNotNull(TEXT("wave"), Wave))
	{
		return false;
	}
	TestEqual(TEXT("wave rate"), static_cast<int32>(Wave->GetSampleRateForCurrentPlatform()), 48000);
	TestEqual(TEXT("wave channels"), Wave->NumChannels, 2);
	TestTrue(TEXT("wave is procedural"), static_cast<bool>(Wave->bProcedural));
	TestNearlyEqual(TEXT("wave duration"), Wave->GetDuration(), Info.Duration, 0.01f);

	// Decode 10 s from the start through the generator the mixer uses.
	FSoundGeneratorInitParams Params;
	Params.SampleRate = 48000.f;
	Params.NumChannels = 2;
	Params.NumFramesPerCallback = 1024;
	ISoundGeneratorPtr Gen = Wave->CreateSoundGenerator(Params);
	if (!TestTrue(TEXT("generator"), Gen.IsValid()))
	{
		return false;
	}
	const TArray<float> Head = Pull(*Gen, 2, 48000, 10.f);
	TestTrue(TEXT("10 s decoded"), Head.Num() >= 10 * 48000 * 2);
	float Peak = 0.f;
	double Energy = 0.0;
	bool bFinite = true;
	for (const float S : Head)
	{
		bFinite &= FMath::IsFinite(S);
		Peak = FMath::Max(Peak, FMath::Abs(S));
		Energy += static_cast<double>(S) * S;
	}
	const float Rms = static_cast<float>(FMath::Sqrt(Energy / FMath::Max(Head.Num(), 1)));
	AddInfo(FString::Printf(TEXT("first 10 s: peak %.3f, rms %.4f"), Peak, Rms));
	TestTrue(TEXT("samples finite"), bFinite);
	TestTrue(TEXT("not silent"), Rms > 0.005f);
	TestTrue(TEXT("in range"), Peak <= 1.5f);
	TestFalse(TEXT("not finished after 10 s"), Gen->IsFinished());

	// Start half a second before the end: it plays out and reports finished.
	Params.StartTime = Info.Duration - 0.5f;
	ISoundGeneratorPtr Tail = Wave->CreateSoundGenerator(Params);
	const TArray<float> End = Pull(*Tail, 2, 48000, 5.f);
	AddInfo(FString::Printf(TEXT("tail: %.3f s before finished"), End.Num() / 2.f / 48000.f));
	TestTrue(TEXT("tail finishes"), Tail->IsFinished());
	TestTrue(TEXT("tail about 0.5 s"), End.Num() / 2.f / 48000.f < 0.6f);

	// Whole-file decode agrees with the probe.
	TArray<float> All;
	FAcMp3Info AllInfo;
	TestTrue(TEXT("decode all"), UAcMp3Loader::DecodeAll(Path, All, AllInfo));
	TestEqual(TEXT("decode all length"), static_cast<int64>(All.Num()), Info.NumFrames * 2);

	// Not an MP3, or missing: refused, no wave.
	FAcMp3Info Bad;
	TestFalse(TEXT("missing file"), UAcMp3Loader::Probe(SoundtrackFile(TEXT("no_such_song.mp3")), Bad));
	TestNull(TEXT("missing file wave"), UAcMp3Loader::LoadMp3(SoundtrackFile(TEXT("no_such_song.mp3"))));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcMp3FolderTest, "Autocraft.Audio.Mp3.Folder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FAcMp3FolderTest::RunTest(const FString& Parameters)
{
	// Every shipped song probes, in name order.
	const FString Dir = FPaths::GetPath(SoundtrackFile(TEXT("x.mp3")));
	const TArray<FString> Files = UAcMp3Loader::FindMp3s(Dir);
	TestEqual(TEXT("14 soundtrack files"), Files.Num(), 14);
	for (const FString& F : Files)
	{
		FAcMp3Info Info;
		TestTrue(*FString::Printf(TEXT("probe %s"), *FPaths::GetCleanFilename(F)), UAcMp3Loader::Probe(F, Info));
		AddInfo(FString::Printf(TEXT("%s: %d Hz, %d ch, %.3f s"), *FPaths::GetCleanFilename(F), Info.SampleRate, Info.NumChannels, Info.Duration));
	}
	if (Files.Num() > 0)
	{
		TestTrue(TEXT("sorted"), FPaths::GetCleanFilename(Files[0]) == TEXT("aftermath_1.mp3"));
	}
	TestTrue(TEXT("user folder path"), UAcMp3Loader::UserMusicFolder().EndsWith(TEXT("Autocraft/Audio/music")));
	return true;
}

#endif
