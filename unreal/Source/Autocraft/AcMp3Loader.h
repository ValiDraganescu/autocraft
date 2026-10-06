// Runtime MP3s for the user's music folder (GAME-LAYER.md §2.11, chunk C8).
//
// Unreal cannot import MP3 at runtime, and the user keeps dropping MP3s into
// ~/Library/Application Support/Autocraft/Audio/music (Swift: MusicPlayer,
// AudioDirector.directory). This decodes them with dr_mp3
// (ThirdParty/dr_mp3.h, v0.7.3, public domain / MIT-0) and plays them as a
// procedural sound wave that streams: each play opens the file and decodes
// on the audio render thread, so a song never sits in memory as PCM.
//
// The music player (UAcMusicPlayer, a later chunk) does:
//     UAcMp3Wave* Wave = UAcMp3Loader::LoadMp3(Path, this);
//     AudioComponent->SetSound(Wave); AudioComponent->Play();
// and gets OnAudioFinished when the song plays out (the generator reports
// IsFinished at the end of the file).

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Sound/SoundWaveProcedural.h"
#include "AcMp3Loader.generated.h"

/** What an MP3 holds, read without decoding the audio. */
USTRUCT(BlueprintType)
struct FAcMp3Info
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Autocraft|Audio")
	int32 SampleRate = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Autocraft|Audio")
	int32 NumChannels = 0;

	/** PCM frames (samples per channel), encoder delay and padding removed. */
	UPROPERTY(BlueprintReadOnly, Category = "Autocraft|Audio")
	int64 NumFrames = 0;

	/** Seconds. */
	UPROPERTY(BlueprintReadOnly, Category = "Autocraft|Audio")
	float Duration = 0.f;
};

/** An MP3 decoder on a file (or nothing, if the file is not an MP3). Plain
 *  C++, one thread at a time. */
class AUTOCRAFT_API FAcMp3Stream
{
public:
	FAcMp3Stream();
	~FAcMp3Stream();
	FAcMp3Stream(const FAcMp3Stream&) = delete;
	FAcMp3Stream& operator=(const FAcMp3Stream&) = delete;

	/** Open `Path`. False if it is missing or not an MP3. */
	bool Open(const FString& Path);
	bool IsOpen() const;
	int32 SampleRate() const;
	int32 NumChannels() const;
	/** Scans the whole file (frame headers only, a few ms), then rewinds. */
	int64 CountFrames();
	/** Decode up to `Frames` frames of interleaved float into `Out`;
	 *  returns the frames written (fewer only at the end of the file). */
	int64 Read(float* Out, int64 Frames);
	bool Seek(int64 Frame);

private:
	struct FImpl;
	TUniquePtr<FImpl> Impl;
};

/** A procedural sound wave that streams one MP3 file from disk. */
UCLASS()
class AUTOCRAFT_API UAcMp3Wave : public USoundWaveProcedural
{
	GENERATED_BODY()

public:
	/** The file it plays. */
	UPROPERTY(BlueprintReadOnly, Category = "Autocraft|Audio")
	FString FilePath;

	UPROPERTY(BlueprintReadOnly, Category = "Autocraft|Audio")
	FAcMp3Info Info;

	/** Point the wave at `Path` with the facts `Probe` read from it. */
	void Init(const FString& Path, const FAcMp3Info& InInfo);

	virtual ISoundGeneratorPtr CreateSoundGenerator(const FSoundGeneratorInitParams& InParams) override;
	virtual Audio::EAudioMixerStreamDataFormat::Type GetGeneratedPCMDataFormat() const override;
};

UCLASS()
class AUTOCRAFT_API UAcMp3Loader : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Sample rate, channels and length of an MP3. False if unreadable. */
	UFUNCTION(BlueprintCallable, Category = "Autocraft|Audio")
	static bool Probe(const FString& Path, FAcMp3Info& OutInfo);

	/** A streaming wave for an MP3 (nullptr if unreadable). Outer defaults to
	 *  the transient package; keep a UPROPERTY reference while it plays. */
	UFUNCTION(BlueprintCallable, Category = "Autocraft|Audio")
	static UAcMp3Wave* LoadMp3(const FString& Path, UObject* Outer = nullptr);

	/** The whole file as interleaved float PCM (for short clips and tests;
	 *  songs should stream through LoadMp3). */
	static bool DecodeAll(const FString& Path, TArray<float>& OutSamples, FAcMp3Info& OutInfo);

	/** ~/Library/Application Support/Autocraft/Audio/music, as in Swift
	 *  (AudioDirector.directory + "music"). */
	UFUNCTION(BlueprintPure, Category = "Autocraft|Audio")
	static FString UserMusicFolder();

	/** The MP3s in `Folder` (none if it is missing), full paths sorted by file
	 *  name, as Swift's AudioDirector.audioFiles sorts them. */
	UFUNCTION(BlueprintCallable, Category = "Autocraft|Audio")
	static TArray<FString> FindMp3s(const FString& Folder);
};
