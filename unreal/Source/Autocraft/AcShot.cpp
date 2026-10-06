#include "AcShot.h"

#include "AcAudioDirector.h"
#include "AcLog.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

UAcShotSubsystem* UAcShotSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	return World ? World->GetSubsystem<UAcShotSubsystem>() : nullptr;
}

bool UAcShotSubsystem::IsShotRun()
{
	FString Value;
	return FParse::Value(FCommandLine::Get(), TEXT("AcShot="), Value);
}

bool UAcShotSubsystem::IsRecordRun()
{
	float Seconds = 0;
	return IsShotRun() && FParse::Value(FCommandLine::Get(), TEXT("AcShotRecord="), Seconds) && Seconds > 0;
}

bool UAcShotSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game && IsShotRun();
}

void UAcShotSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	const TCHAR* Cmd = FCommandLine::Get();
	FParse::Value(Cmd, TEXT("AcShot="), Path);
	if (FPaths::IsRelative(Path)) Path = FPaths::ConvertRelativePathToFull(FPaths::LaunchDir(), Path);
	if (!Path.EndsWith(TEXT(".png"), ESearchCase::IgnoreCase)) Path += TEXT(".png");
	FParse::Value(Cmd, TEXT("AcShotFrames="), WarmFrames);
	bShowUI = FParse::Param(Cmd, TEXT("AcShotUI"));
	float RecordSeconds = 0;
	if (FParse::Value(Cmd, TEXT("AcShotRecord="), RecordSeconds) && RecordSeconds > 0)
	{
		const double Step = FApp::UseFixedTimeStep() && FApp::GetFixedDeltaTime() > 0 ? FApp::GetFixedDeltaTime() : 1.0 / 30;
		if (!FApp::UseFixedTimeStep())
		{
			UE_LOG(LogAutocraft, Warning, TEXT("shot: recording without -UseFixedTimeStep -FPS=30; the clip's speed follows the render speed"));
		}
		RecordFrames = FMath::Max(1, FMath::RoundToInt32(RecordSeconds / Step));
	}
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
	// A file left from an earlier run must not pass for this one's.
	IFileManager::Get().Delete(*Path, false, true, true);
	for (int32 I = 0; I < RecordFrames; ++I) IFileManager::Get().Delete(*FramePath(I), false, true, true);
	if (RecordFrames > 0)
	{
		UE_LOG(LogAutocraft, Log, TEXT("shot: recording %d frames to %s after %d frames"), RecordFrames, *FramePath(0), WarmFrames);
	}
	else
	{
		UE_LOG(LogAutocraft, Log, TEXT("shot: %s after %d frames"), *Path, WarmFrames);
	}
}

FString UAcShotSubsystem::FramePath(const int32 Index) const
{
	return FString::Printf(TEXT("%s_%05d.png"), *FPaths::ChangeExtension(Path, TEXT("")), Index);
}

TStatId UAcShotSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UAcShotSubsystem, STATGROUP_Tickables);
}

void UAcShotSubsystem::Tick(const float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (bDone) return;
	if (RecordFrames > 0)
	{
		UAcAudioDirector* Audio = GetWorld()->GetSubsystem<UAcAudioDirector>();
		const bool bSound = Audio && Audio->IsRecordingRequested();
		if (Recorded == 0)
		{
			if (++Frames < WarmFrames || Holds > 0) return;
			// -AcAudioRecord (with a far -AcAudioRecordAt): start the sound
			// now and the frames with it (it waits for its bank), or after
			// 300 frames without it.
			if (bSound && SoundWait == 0) Audio->RecordFromNow();
			if (bSound && !Audio->HasRecordingStarted() && ++SoundWait < 300) return;
			if (bSound && !Audio->HasRecordingStarted())
			{
				UE_LOG(LogAutocraft, Warning, TEXT("shot: the sound recording has not started; recording without it"));
			}
			OnBeforeShot.Broadcast();
			RecordStart = GetWorld()->GetTimeSeconds();
			UE_LOG(LogAutocraft, Log, TEXT("shot: recording starts at world %.4f s"), RecordStart.GetValue());
		}
		if (Recorded < RecordFrames)
		{
			FScreenshotRequest::RequestScreenshot(FramePath(Recorded), bShowUI, /*bAddFilenameSuffix*/ false);
			++Recorded;
			return;
		}
		// Done once the last frame's size holds still (the writer is async)
		// and the sound, if any, is written.
		++Waited;
		const int64 Size = IFileManager::Get().FileSize(*FramePath(RecordFrames - 1));
		const bool bSoundDone = !bSound || !Audio->HasRecordingStarted() || Audio->IsRecordingDone() || Waited > 600;
		if (Size > 0 && Size == LastSize && bSoundDone)
		{
			bDone = true;
			UE_LOG(LogAutocraft, Display, TEXT("shot: recorded %d frames to %s"), RecordFrames, *FramePath(0));
			FPlatformMisc::RequestExit(false, TEXT("AcShot"));
			return;
		}
		LastSize = Size;
		if (Waited > 600)
		{
			bDone = true;
			UE_LOG(LogAutocraft, Error, TEXT("shot: the last frame %s was never written"), *FramePath(RecordFrames - 1));
			FPlatformMisc::RequestExitWithStatus(false, 1, TEXT("AcShot"));
		}
		return;
	}
	if (Waited < 0)
	{
		++Frames;
		if (Frames < WarmFrames || Holds > 0) return;
		OnBeforeShot.Broadcast();
		// The viewport writes it at the end of its next draw.
		FScreenshotRequest::RequestScreenshot(Path, bShowUI, /*bAddFilenameSuffix*/ false);
		Waited = 0;
		return;
	}
	++Waited;
	// Written once its size holds still for a frame (the image writer
	// works on its own thread).
	const int64 Size = IFileManager::Get().FileSize(*Path);
	if (Size > 0 && Size == LastSize)
	{
		bDone = true;
		UE_LOG(LogAutocraft, Display, TEXT("shot: saved %s (%lld bytes, %d frames)"), *Path, (long long)Size, Frames);
		FPlatformMisc::RequestExit(false, TEXT("AcShot"));
		return;
	}
	LastSize = Size;
	if (Waited > 600)
	{
		bDone = true;
		UE_LOG(LogAutocraft, Error, TEXT("shot: %s was never written (is anything rendering? -nullrhi cannot take shots)"), *Path);
		FPlatformMisc::RequestExitWithStatus(false, 1, TEXT("AcShot"));
	}
}
