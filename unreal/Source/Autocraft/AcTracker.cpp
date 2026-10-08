#include "AcTracker.h"

#include "AcLog.h"

#include "Async/Async.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/DateTime.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"

namespace
{
	std::string Utf8(const FString& S)
	{
		return std::string(TCHAR_TO_UTF8(*S));
	}

	/// The app binary's date: the game module's file (a rebuild changes it,
	/// the editor's own binary does not), else the executable's.
	FString BuildDate()
	{
#if IS_MONOLITHIC
		FString File;
#else
		FString File = FModuleManager::Get().GetModuleFilename(TEXT("Autocraft"));
#endif
		if (File.IsEmpty() || !IFileManager::Get().FileExists(*File)) File = FPlatformProcess::ExecutablePath();
		const FDateTime Stamp = IFileManager::Get().GetTimeStamp(*File);
		return Stamp.ToString(TEXT("%Y-%m-%dT%H:%M:%SZ"));
	}
}

FAcTracker::FAcTracker(FString InPath)
	: DatabasePath(MoveTemp(InPath))
	, BuildText(Utf8(BuildDate()))
	, OSText(Utf8(FString::Printf(TEXT("macOS %s"), *FPlatformMisc::GetOSVersion())))
{
}

FAcTracker::~FAcTracker()
{
	Finish();
}

void FAcTracker::Write(ac::TrackedGame Game)
{
	{
		FScopeLock Guard(&Lock);
		if (bBroken) return;
		Queue.push_back(std::move(Game));
		if (bRunning) return;
		bRunning = true;
	}
	Async(EAsyncExecution::ThreadPool, [this] { Drain(); });
}

void FAcTracker::Drain()
{
	for (;;)
	{
		ac::TrackedGame Game;
		{
			FScopeLock Guard(&Lock);
			if (Queue.empty())
			{
				bRunning = false;
				return;
			}
			Game = std::move(Queue.front());
			Queue.pop_front();
		}
		const double T0 = FPlatformTime::Seconds();
		std::string Error;
		bool bOk = true;
		if (!Store)
		{
			IFileManager::Get().MakeDirectory(*FPaths::GetPath(DatabasePath), true);
			Store = ac::TrackingStore::open(Utf8(DatabasePath), &Error);
			bOk = Store != nullptr;
			if (!bOk)
			{
				// Not opened: this run goes untracked, said once.
				UE_LOG(LogAutocraft, Warning, TEXT("tracking: %s won't open (%s); games go untracked"), *DatabasePath, UTF8_TO_TCHAR(Error.c_str()));
				FScopeLock Guard(&Lock);
				bBroken = true;
				Queue.clear();
				bRunning = false;
				return;
			}
		}
		bOk = Store->write(Game, &Error);
		const double Ms = (FPlatformTime::Seconds() - T0) * 1000.0;
		if (!bOk)
		{
			const FString Message = UTF8_TO_TCHAR(Error.c_str());
			if (!Logged.Contains(Message))
			{
				Logged.Add(Message);
				UE_LOG(LogAutocraft, Warning, TEXT("tracking: write failed: %s"), *Message);
			}
		}
		FScopeLock Guard(&Lock);
		Counted.Writes += 1;
		Counted.Failures += bOk ? 0 : 1;
		Counted.LastMs = Ms;
		Counted.MaxMs = FMath::Max(Counted.MaxMs, Ms);
		Counted.TotalMs += Ms;
	}
}

void FAcTracker::Finish()
{
	for (;;)
	{
		{
			FScopeLock Guard(&Lock);
			if (!bRunning && Queue.empty()) return;
		}
		FPlatformProcess::Sleep(0.001f);
	}
}

FAcTracker::FStats FAcTracker::Stats() const
{
	FScopeLock Guard(&Lock);
	return Counted;
}
