#include "AcMp3Loader.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/Paths.h"
#include "Sound/SoundGenerator.h"
#include "UObject/Package.h"

// dr_mp3, compiled here only. Every function is static so its symbols can
// never clash with another copy of dr_mp3/minimp3 linked into the engine.
THIRD_PARTY_INCLUDES_START
#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_STDIO
#define DRMP3_API static
#include "ThirdParty/dr_mp3.h"
THIRD_PARTY_INCLUDES_END

DEFINE_LOG_CATEGORY_STATIC(LogAcMp3, Log, All);

// ---------------------------------------------------------------- stream

struct FAcMp3Stream::FImpl
{
	TUniquePtr<IFileHandle> File;
	drmp3 Mp3;
	bool bOpen = false;

	~FImpl()
	{
		if (bOpen)
		{
			drmp3_uninit(&Mp3);
		}
	}

	static size_t OnRead(void* User, void* Out, size_t Bytes)
	{
		IFileHandle* F = static_cast<FImpl*>(User)->File.Get();
		const int64 Left = F->Size() - F->Tell();
		const int64 N = FMath::Clamp<int64>(Left, 0, static_cast<int64>(Bytes));
		if (N <= 0 || !F->Read(static_cast<uint8*>(Out), N))
		{
			return 0;
		}
		return static_cast<size_t>(N);
	}

	static drmp3_bool32 OnSeek(void* User, int Offset, drmp3_seek_origin Origin)
	{
		IFileHandle* F = static_cast<FImpl*>(User)->File.Get();
		int64 To = Offset;
		if (Origin == DRMP3_SEEK_CUR)
		{
			To += F->Tell();
		}
		else if (Origin == DRMP3_SEEK_END)
		{
			To += F->Size();
		}
		if (To < 0 || To > F->Size())
		{
			return DRMP3_FALSE;
		}
		return F->Seek(To) ? DRMP3_TRUE : DRMP3_FALSE;
	}

	static drmp3_bool32 OnTell(void* User, drmp3_int64* Cursor)
	{
		*Cursor = static_cast<FImpl*>(User)->File->Tell();
		return DRMP3_TRUE;
	}
};

FAcMp3Stream::FAcMp3Stream() : Impl(MakeUnique<FImpl>()) {}
FAcMp3Stream::~FAcMp3Stream() = default;

bool FAcMp3Stream::Open(const FString& Path)
{
	Impl = MakeUnique<FImpl>();
	Impl->File.Reset(FPlatformFileManager::Get().GetPlatformFile().OpenRead(*Path));
	if (!Impl->File)
	{
		return false;
	}
	Impl->bOpen = drmp3_init(&Impl->Mp3, &FImpl::OnRead, &FImpl::OnSeek, &FImpl::OnTell, nullptr, Impl.Get(), nullptr) == DRMP3_TRUE;
	if (Impl->bOpen && (Impl->Mp3.sampleRate == 0 || Impl->Mp3.channels == 0))
	{
		drmp3_uninit(&Impl->Mp3);
		Impl->bOpen = false;
	}
	return Impl->bOpen;
}

bool FAcMp3Stream::IsOpen() const { return Impl->bOpen; }
int32 FAcMp3Stream::SampleRate() const { return Impl->bOpen ? static_cast<int32>(Impl->Mp3.sampleRate) : 0; }
int32 FAcMp3Stream::NumChannels() const { return Impl->bOpen ? static_cast<int32>(Impl->Mp3.channels) : 0; }

int64 FAcMp3Stream::CountFrames()
{
	return Impl->bOpen ? static_cast<int64>(drmp3_get_pcm_frame_count(&Impl->Mp3)) : 0;
}

int64 FAcMp3Stream::Read(float* Out, int64 Frames)
{
	if (!Impl->bOpen || Frames <= 0)
	{
		return 0;
	}
	return static_cast<int64>(drmp3_read_pcm_frames_f32(&Impl->Mp3, static_cast<drmp3_uint64>(Frames), Out));
}

bool FAcMp3Stream::Seek(int64 Frame)
{
	return Impl->bOpen && drmp3_seek_to_pcm_frame(&Impl->Mp3, static_cast<drmp3_uint64>(FMath::Max<int64>(Frame, 0))) == DRMP3_TRUE;
}

// ---------------------------------------------------------------- generator

namespace
{
	/** Decodes on the audio render thread, one decoder per playing sound. */
	class FAcMp3Generator final : public ISoundGenerator
	{
	public:
		FAcMp3Generator(const FString& Path, int32 InChannels, float StartTime)
			: Channels(FMath::Max(InChannels, 1))
		{
			if (Stream.Open(Path) && Stream.NumChannels() == Channels)
			{
				if (StartTime > 0.f)
				{
					Stream.Seek(static_cast<int64>(StartTime * Stream.SampleRate()));
				}
			}
			else
			{
				UE_LOG(LogAcMp3, Warning, TEXT("cannot stream %s"), *Path);
				bDone = true;
			}
		}

		virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override
		{
			const int64 Frames = NumSamples / Channels;
			const int64 Got = bDone ? 0 : Stream.Read(OutAudio, Frames);
			const int32 Written = static_cast<int32>(Got * Channels);
			if (Written < NumSamples)
			{
				FMemory::Memzero(OutAudio + Written, (NumSamples - Written) * sizeof(float));
			}
			if (Got < Frames)
			{
				bDone = true;
			}
			return NumSamples;
		}

		virtual int32 GetDesiredNumSamplesToRenderPerCallback() const override { return 1024 * Channels; }

		virtual bool IsFinished() const override { return bDone; }
		virtual int32 GetNumChannels() const override { return Channels; }

	private:
		FAcMp3Stream Stream;
		int32 Channels;
		bool bDone = false;
	};
}

// ---------------------------------------------------------------- wave

void UAcMp3Wave::Init(const FString& Path, const FAcMp3Info& InInfo)
{
	FilePath = Path;
	Info = InInfo;
	SetSampleRate(InInfo.SampleRate);
	NumChannels = InInfo.NumChannels;
	Duration = InInfo.Duration;
	SoundGroup = SOUNDGROUP_Music;
	bLooping = false;
}

ISoundGeneratorPtr UAcMp3Wave::CreateSoundGenerator(const FSoundGeneratorInitParams& InParams)
{
	return MakeShared<FAcMp3Generator, ESPMode::ThreadSafe>(FilePath, NumChannels, InParams.StartTime);
}

Audio::EAudioMixerStreamDataFormat::Type UAcMp3Wave::GetGeneratedPCMDataFormat() const
{
	return Audio::EAudioMixerStreamDataFormat::Float;
}

// ---------------------------------------------------------------- loader

bool UAcMp3Loader::Probe(const FString& Path, FAcMp3Info& OutInfo)
{
	OutInfo = FAcMp3Info();
	FAcMp3Stream Stream;
	if (!Stream.Open(Path))
	{
		return false;
	}
	OutInfo.SampleRate = Stream.SampleRate();
	OutInfo.NumChannels = Stream.NumChannels();
	OutInfo.NumFrames = Stream.CountFrames();
	OutInfo.Duration = OutInfo.SampleRate > 0 ? static_cast<float>(static_cast<double>(OutInfo.NumFrames) / OutInfo.SampleRate) : 0.f;
	return OutInfo.NumFrames > 0;
}

UAcMp3Wave* UAcMp3Loader::LoadMp3(const FString& Path, UObject* Outer)
{
	FAcMp3Info Info;
	if (!Probe(Path, Info))
	{
		UE_LOG(LogAcMp3, Warning, TEXT("not an MP3 we can read: %s"), *Path);
		return nullptr;
	}
	UAcMp3Wave* Wave = NewObject<UAcMp3Wave>(Outer ? Outer : GetTransientPackage());
	Wave->Init(Path, Info);
	return Wave;
}

bool UAcMp3Loader::DecodeAll(const FString& Path, TArray<float>& OutSamples, FAcMp3Info& OutInfo)
{
	OutSamples.Reset();
	if (!Probe(Path, OutInfo))
	{
		return false;
	}
	FAcMp3Stream Stream;
	if (!Stream.Open(Path))
	{
		return false;
	}
	OutSamples.SetNumUninitialized(OutInfo.NumFrames * OutInfo.NumChannels);
	const int64 Got = Stream.Read(OutSamples.GetData(), OutInfo.NumFrames);
	OutSamples.SetNum(Got * OutInfo.NumChannels);
	return Got > 0;
}

FString UAcMp3Loader::UserMusicFolder()
{
	// UserSettingsDir is ~/Library/Application Support on the Mac.
	return FPaths::Combine(FPlatformProcess::UserSettingsDir(), TEXT("Autocraft"), TEXT("Audio"), TEXT("music"));
}

TArray<FString> UAcMp3Loader::FindMp3s(const FString& Folder)
{
	TArray<FString> Names;
	IFileManager::Get().FindFiles(Names, *FPaths::Combine(Folder, TEXT("*.mp3")), true, false);
	Names.Sort();
	TArray<FString> Paths;
	for (const FString& Name : Names)
	{
		Paths.Add(FPaths::Combine(Folder, Name));
	}
	return Paths;
}
