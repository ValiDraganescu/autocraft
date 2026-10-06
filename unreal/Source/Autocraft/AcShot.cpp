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
#include "Engine/GameViewportClient.h"
#include "FrameGrabber.h"
#include "Slate/SceneViewport.h"
#include "Widgets/SViewport.h"

#include <cstdio>

namespace
{
	/// The recorded frame's number, carried through the grabber (the
	/// flushing frames after the last have none to write).
	struct FAcRawFrame : IFramePayload
	{
		int32 Index = 0;
	};
}

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
		FParse::Value(Cmd, TEXT("AcShotRaw="), RawPath);
	}
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
	// A file left from an earlier run must not pass for this one's.
	IFileManager::Get().Delete(*Path, false, true, true);
	for (int32 I = 0; I < RecordFrames; ++I) IFileManager::Get().Delete(*FramePath(I), false, true, true);
	if (RecordFrames > 0 && !RawPath.IsEmpty())
	{
		UE_LOG(LogAutocraft, Log, TEXT("shot: recording %d raw frames to %s after %d frames"), RecordFrames, *RawPath, WarmFrames);
	}
	else if (RecordFrames > 0)
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
			if (!RawPath.IsEmpty() && !StartRaw())
			{
				bDone = true;
				FPlatformMisc::RequestExitWithStatus(false, 1, TEXT("AcShot"));
				return;
			}
			RecordStart = GetWorld()->GetTimeSeconds();
			UE_LOG(LogAutocraft, Log, TEXT("shot: recording starts at world %.4f s"), RecordStart.GetValue());
		}
		if (Grabber)
		{
			// Every frame read back is written; a few more are captured after
			// the last so the grabber reads that one back too.
			ApplyUI();
			if (!DrainRaw())
			{
				bDone = true;
				StopRaw();
				FPlatformMisc::RequestExitWithStatus(false, 1, TEXT("AcShot"));
				return;
			}
			if (Recorded < RecordFrames || (Written < RecordFrames && Flushes < 8))
			{
				TSharedRef<FAcRawFrame, ESPMode::ThreadSafe> Frame = MakeShared<FAcRawFrame, ESPMode::ThreadSafe>();
				Frame->Index = Recorded < RecordFrames ? Recorded : -1;
				Grabber->CaptureThisFrame(Frame);
				if (Recorded < RecordFrames) ++Recorded;
				else ++Flushes;
				return;
			}
			++Waited;
			const bool bSoundDone = !bSound || !Audio->HasRecordingStarted() || Audio->IsRecordingDone() || Waited > 600;
			if (Written >= RecordFrames && bSoundDone)
			{
				bDone = true;
				StopRaw();
				UE_LOG(LogAutocraft, Display, TEXT("shot: recorded %d raw frames to %s"), Written, *RawPath);
				FPlatformMisc::RequestExit(false, TEXT("AcShot"));
			}
			else if (Waited > 600)
			{
				bDone = true;
				StopRaw();
				UE_LOG(LogAutocraft, Error, TEXT("shot: only %d of %d raw frames were read back"), Written, RecordFrames);
				FPlatformMisc::RequestExitWithStatus(false, 1, TEXT("AcShot"));
			}
			return;
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

// MARK: - -AcShotRaw

bool UAcShotSubsystem::StartRaw()
{
	UGameViewportClient* Client = GetWorld()->GetGameViewport();
	const TSharedPtr<SViewport> Widget = Client ? Client->GetGameViewportWidget() : nullptr;
	const TSharedPtr<FSceneViewport> Viewport = Widget ? StaticCastSharedPtr<FSceneViewport>(Widget->GetViewportInterface().Pin()) : nullptr;
	if (!Viewport)
	{
		UE_LOG(LogAutocraft, Error, TEXT("shot: -AcShotRaw: no game viewport to read back"));
		return false;
	}
	// The size the encoder is told (-ResX/-ResY), else the viewport's.
	RawSize = Viewport->GetSizeXY();
	FParse::Value(FCommandLine::Get(), TEXT("ResX="), RawSize.X);
	FParse::Value(FCommandLine::Get(), TEXT("ResY="), RawSize.Y);
	// Opening a pipe waits for its reader.
	Raw = std::fopen(TCHAR_TO_UTF8(*RawPath), "wb");
	if (!Raw)
	{
		UE_LOG(LogAutocraft, Error, TEXT("shot: -AcShotRaw: cannot open %s"), *RawPath);
		return false;
	}
	std::setvbuf(Raw, nullptr, _IOFBF, 4 << 20);
	Grabber = MakeShared<FFrameGrabber>(Viewport.ToSharedRef(), RawSize, PF_B8G8R8A8, 4);
	Grabber->StartCapturingFrames();
	ApplyUI();
	UE_LOG(LogAutocraft, Log, TEXT("shot: raw %dx%d BGRA from a %dx%d viewport"), RawSize.X, RawSize.Y, Viewport->GetSizeXY().X, Viewport->GetSizeXY().Y);
	return true;
}

void UAcShotSubsystem::ApplyUI()
{
	UGameViewportClient* Client = GetWorld() ? GetWorld()->GetGameViewport() : nullptr;
	const TSharedPtr<SViewport> Widget = Client ? Client->GetGameViewportWidget() : nullptr;
	const TSharedPtr<SWidget> Content = Widget ? Widget->GetContent() : nullptr;
	if (!Content) return;
	if (!bShowUI && !UIWas)
	{
		UIWas = Content->GetVisibility();
		Content->SetVisibility(EVisibility::Collapsed);
	}
	else if (bShowUI && UIWas)
	{
		Content->SetVisibility(*UIWas);
		UIWas.Reset();
	}
}

bool UAcShotSubsystem::DrainRaw()
{
	if (!Grabber || !Raw) return true;
	for (FCapturedFrameData& F : Grabber->GetCapturedFrames())
	{
		const FAcRawFrame* Frame = F.GetPayload<FAcRawFrame>();
		if (!Frame || Frame->Index < 0 || Written >= RecordFrames) continue;
		if (Frame->Index != Written)
		{
			UE_LOG(LogAutocraft, Warning, TEXT("shot: raw frame %d arrived as number %d"), Frame->Index, Written);
		}
		if (F.BufferSize != RawSize || F.ColorBuffer.Num() != RawSize.X * RawSize.Y)
		{
			UE_LOG(LogAutocraft, Error, TEXT("shot: raw frame %d is %dx%d, not %dx%d"), Written, F.BufferSize.X, F.BufferSize.Y, RawSize.X, RawSize.Y);
			return false;
		}
		const size_t Bytes = size_t(F.ColorBuffer.Num()) * sizeof(FColor);
		if (std::fwrite(F.ColorBuffer.GetData(), 1, Bytes, Raw) != Bytes)
		{
			UE_LOG(LogAutocraft, Error, TEXT("shot: writing raw frame %d to %s failed (did the encoder stop?)"), Written, *RawPath);
			return false;
		}
		++Written;
	}
	return true;
}

void UAcShotSubsystem::StopRaw()
{
	if (Grabber)
	{
		Grabber->StopCapturingFrames();
		Grabber->Shutdown();
		Grabber.Reset();
	}
	if (Raw)
	{
		std::fclose(Raw);
		Raw = nullptr;
	}
}

void UAcShotSubsystem::Deinitialize()
{
	StopRaw();
	Super::Deinitialize();
}
