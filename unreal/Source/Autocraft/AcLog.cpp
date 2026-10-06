#include "AcLog.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/OutputDevice.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"

DEFINE_LOG_CATEGORY(LogAutocraft);

namespace
{
	FString LogDirectory()
	{
		return FPaths::Combine(FString(FPlatformProcess::UserHomeDir()), TEXT("Library/Logs/Autocraft"));
	}

	class FAcLogFile final : public FOutputDevice
	{
	public:
		bool Open()
		{
			const FString Dir = LogDirectory();
			IFileManager& Files = IFileManager::Get();
			Files.MakeDirectory(*Dir, true);
			const FString Path = AcLog::FilePath();
			if (Files.FileSize(*Path) > 5'000'000)
			{
				const FString Old = Path + TEXT(".1");
				Files.Delete(*Old, false, true, true);
				Files.Move(*Old, *Path, true, true);
			}
			Archive.Reset(Files.CreateFileWriter(*Path, FILEWRITE_Append | FILEWRITE_AllowRead));
			return Archive.IsValid();
		}

		virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category) override
		{
			static const FName Ours(TEXT("LogAutocraft"));
			const ELogVerbosity::Type Level = ELogVerbosity::Type(Verbosity & ELogVerbosity::VerbosityMask);
			if (Category != Ours && Level > ELogVerbosity::Warning)
			{
				return;
			}
			const FDateTime Now = FDateTime::Now();
			FString Line = FString::Printf(TEXT("%04d-%02d-%02d %02d:%02d:%02d.%03d "), Now.GetYear(), Now.GetMonth(), Now.GetDay(),
				Now.GetHour(), Now.GetMinute(), Now.GetSecond(), Now.GetMillisecond());
			if (Category != Ours)
			{
				Line += Category.ToString() + TEXT(": ");
			}
			Line += V;
			Line += TEXT("\n");
			const FTCHARToUTF8 Utf8(*Line);
			FScopeLock Lock(&Mutex);
			if (Archive)
			{
				Archive->Serialize(const_cast<ANSICHAR*>(Utf8.Get()), Utf8.Length());
				Archive->Flush();
			}
		}

		virtual bool CanBeUsedOnAnyThread() const override { return true; }
		virtual bool CanBeUsedOnMultipleThreads() const override { return true; }

		virtual void Flush() override
		{
			FScopeLock Lock(&Mutex);
			if (Archive) Archive->Flush();
		}

		virtual void TearDown() override
		{
			FScopeLock Lock(&Mutex);
			if (Archive) Archive->Close();
			Archive.Reset();
		}

	private:
		FCriticalSection Mutex;
		TUniquePtr<FArchive> Archive;
	};

	FAcLogFile* GFile = nullptr;
}

FString AcLog::FilePath()
{
	return FPaths::Combine(LogDirectory(), TEXT("unreal.log"));
}

void AcLog::Start()
{
	// Only the game (`-game` or a packaged build), not the editor or a
	// commandlet: the file is the game's diary, as in Swift.
	if (GFile || !GLog || IsRunningCommandlet() || (GIsEditor && !IsRunningGame())) return;
	GFile = new FAcLogFile();
	if (!GFile->Open())
	{
		delete GFile;
		GFile = nullptr;
		return;
	}
	GLog->AddOutputDevice(GFile);
	UE_LOG(LogAutocraft, Log, TEXT("---- launch pid %u, args: %s"), FPlatformProcess::GetCurrentProcessId(), FCommandLine::Get());
}

void AcLog::Stop()
{
	if (!GFile) return;
	if (GLog) GLog->RemoveOutputDevice(GFile);
	GFile->TearDown();
	delete GFile;
	GFile = nullptr;
}
