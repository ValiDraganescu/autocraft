// `-AcShot=PATH`: render some frames, save the game viewport as a PNG at
// PATH, then quit. Every chunk uses it to show its work headless:
//
//   UnrealEditor Autocraft.uproject /Game/Maps/Battlefield -game
//     -RenderOffscreen -ResX=1920 -ResY=1080 -unattended -nosplash
//     -AcMap=badlands-large -AcShot=/abs/out.png [-AcShotFrames=60] [-AcShotUI]
//
// -AcShotFrames=N  frames rendered before the shot (default 60: lets TSR,
//                  Lumen and the auto exposure settle).
// -AcShotUI        include the Slate UI (HUD) in the picture.
// -AcShotRecord=S  record S seconds instead of one picture: after the warm
//                  frames and the holds, every frame is saved as
//                  PATH_00000.png, PATH_00001.png... (a scripted drive does
//                  not hold a recording: the recording shows the drive).
//                  With -AcAudioRecord (AcAudioDirector.h; give it a far
//                  -AcAudioRecordAt) the recording starts the sound, starts
//                  with it, and waits for its WAV; both log
//                  their start in world seconds, to line them up. Run it with
//                  -UseFixedTimeStep -FPS=30 so each frame is 1/30 s of game
//                  time; the autocraft-video skill encodes the frames.
// -AcShotRaw=FILE  with -AcShotRecord: no PNGs; the frames are read back
//                  from the GPU without stalling the render (FFrameGrabber,
//                  a frame or two behind) and written one after another to
//                  FILE as raw BGRA, -ResX x -ResY each. FILE can be a pipe
//                  an encoder reads (the autocraft-video skill's record.py
//                  makes one). The UI is whatever the window shows: without
//                  -AcShotUI (or after SetShowUI(false)) the viewport's
//                  widgets are collapsed.
// A relative PATH is relative to the directory the process was launched in.
// Code that needs more time before the picture (a scene being staged, a
// mesh streaming in) holds the shot with `Hold()` and `Release()`.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

#include "AcShot.generated.h"

DECLARE_MULTICAST_DELEGATE(FAcBeforeShot);

UCLASS()
class AUTOCRAFT_API UAcShotSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	static UAcShotSubsystem* Get(const UObject* WorldContext);
	/// Whether this process was started to take a shot.
	static bool IsShotRun();
	/// Whether it records frames (-AcShotRecord) rather than one picture.
	static bool IsRecordRun();
	/// The world time of the first recorded frame, once recording.
	TOptional<double> GetRecordStart() const { return RecordStart; }

	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/// Keep the shot from being taken until as many `Release()`s.
	void Hold() { ++Holds; }
	void Release() { Holds = FMath::Max(0, Holds - 1); }

	/// Fired on the frame the picture is requested (last chance to pose).
	FAcBeforeShot OnBeforeShot;

	const FString& GetPath() const { return Path; }
	/// Whether the frames keep the UI (-AcShotUI); a recording can drop it
	/// midway (the pilot's pull-out).
	void SetShowUI(const bool bShow) { bShowUI = bShow; }

private:
	FString Path;
	int32 WarmFrames = 60;
	bool bShowUI = false;
	int32 Frames = 0;
	int32 Holds = 0;
	/// Frames since the request (-1: not requested yet).
	int32 Waited = -1;
	int64 LastSize = -1;
	bool bDone = false;
	/// -AcShotRecord: frames to record (0: one picture), frames requested.
	int32 RecordFrames = 0;
	int32 Recorded = 0;
	int32 SoundWait = 0;
	TOptional<double> RecordStart;
	FString FramePath(int32 Index) const;

	/// -AcShotRaw: the grabber, the file, the size, frames written.
	TSharedPtr<class FFrameGrabber> Grabber;
	FString RawPath;
	FILE* Raw = nullptr;
	FIntPoint RawSize = FIntPoint::ZeroValue;
	int32 Written = 0;
	int32 Flushes = 0;
	/// The viewport's widgets' visibility before -AcShotRaw collapsed them.
	TOptional<EVisibility> UIWas;
	bool StartRaw();
	void ApplyUI();
	/// Writes the frames read back so far; false on a write error.
	bool DrainRaw();
	void StopRaw();
	virtual void Deinitialize() override;
};
