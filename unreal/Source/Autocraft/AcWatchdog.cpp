#include "AcWatchdog.h"

#include "AcLog.h"

#include "HAL/IConsoleManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformStackWalk.h"
#include "HAL/PlatformTime.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"

#include <atomic>

#if PLATFORM_MAC
#include <mach/mach.h>
#include <pthread.h>
#endif

static TAutoConsoleVariable<int32> CVarAcStallMs(
	TEXT("ac.StallMs"), 250,
	TEXT("The watchdog logs a game-thread stall longer than this, ms (Log.swift's 250)."));
static TAutoConsoleVariable<int32> CVarAcStallStackMs(
	TEXT("ac.StallStackMs"), 500,
	TEXT("A stall this long (ms) gets the game thread's stack logged once. 0: never."));

namespace
{
	std::atomic<const TCHAR*> GActivity{TEXT("idle")};

	/// The game thread as the watchdog walks it.
	struct FGameThreadInfo
	{
#if PLATFORM_MAC
		thread_act_t Port = MACH_PORT_NULL;
		uint64 StackLow = 0;
		uint64 StackHigh = 0;
#endif
		bool bValid = false;
	};

	class FAcWatchdogThread final : public FRunnable
	{
	public:
		explicit FAcWatchdogThread(const FGameThreadInfo& InGame) : Game(InGame), LastBeat(FPlatformTime::Seconds()) {}

		virtual uint32 Run() override
		{
			while (!bStop.load())
			{
				FPlatformProcess::SleepNoStats(0.05f);
				const double Now = FPlatformTime::Seconds();
				const double StallAfter = FMath::Max(1, CVarAcStallMs.GetValueOnAnyThread()) / 1000.0;
				const int32 StackMs = CVarAcStallStackMs.GetValueOnAnyThread();
				bool bTakeStack = false;
				double Age = 0;
				const TCHAR* During = nullptr;
				{
					FScopeLock Lock(&Mutex);
					Age = Now - LastBeat;
					if (Age > StallAfter && !bStalled)
					{
						bStalled = true;
						StalledSince = LastBeat;
						StallActivity = GActivity.load();
						bStackTaken = false;
					}
					if (bStalled && !bStackTaken && StackMs > 0 && Age * 1000.0 >= StackMs)
					{
						bStackTaken = true;
						bTakeStack = true;
						During = StallActivity;
					}
				}
				if (bTakeStack) LogStack(Age, During);
			}
			return 0;
		}

		virtual void Stop() override { bStop.store(true); }

		void Beat()
		{
			const double Now = FPlatformTime::Seconds();
			bool bWas = false;
			double Since = 0;
			const TCHAR* What = nullptr;
			{
				FScopeLock Lock(&Mutex);
				bWas = bStalled;
				Since = StalledSince;
				What = StallActivity;
				bStalled = false;
				LastBeat = Now;
			}
			if (bWas)
			{
				Stalls.fetch_add(1);
				UE_LOG(LogAutocraft, Log, TEXT("game thread stalled %.0f ms (during: %s; now: %s)"), (Now - Since) * 1000.0,
					What ? What : TEXT("?"), GActivity.load());
			}
		}

		std::atomic<int32> Stalls{0};

	private:
		/// Suspend the game thread, read its frame-pointer chain, resume it,
		/// then name the frames and log them. Nothing that allocates or locks
		/// runs while it is suspended (it may hold the allocator's lock).
		void LogStack(const double Age, const TCHAR* During)
		{
			uint64 Frames[MaxFrames];
			const int32 Count = Capture(Frames);
			if (Count <= 0)
			{
				UE_LOG(LogAutocraft, Log, TEXT("stall: game thread stack at %.0f ms (during: %s): not available on this platform"),
					Age * 1000.0, During ? During : TEXT("?"));
				return;
			}
			FString Text = FString::Printf(TEXT("stall: game thread stack at %.0f ms (during: %s):"), Age * 1000.0,
				During ? During : TEXT("?"));
			for (int32 I = 0; I < Count; ++I)
			{
				FProgramCounterSymbolInfo Info;
				FPlatformStackWalk::ProgramCounterToSymbolInfo(Frames[I], Info);
				const FString Function = Info.FunctionName[0] ? FString(ANSI_TO_TCHAR(Info.FunctionName))
				                                              : FString::Printf(TEXT("0x%llx"), (unsigned long long)Frames[I]);
				FString Module = FPaths::GetCleanFilename(FString(ANSI_TO_TCHAR(Info.ModuleName)));
				Text += FString::Printf(TEXT("\n    #%d %s (%s)"), I, *Function, *Module);
				if (Info.Filename[0] && Info.LineNumber > 0)
				{
					Text += FString::Printf(TEXT(" %s:%d"), *FPaths::GetCleanFilename(FString(ANSI_TO_TCHAR(Info.Filename))), Info.LineNumber);
				}
			}
			UE_LOG(LogAutocraft, Log, TEXT("%s"), *Text);
		}

		static constexpr int32 MaxFrames = 48;

		int32 Capture(uint64 (&Out)[MaxFrames]) const
		{
#if PLATFORM_MAC
			if (!Game.bValid) return 0;
			if (thread_suspend(Game.Port) != KERN_SUCCESS) return 0;
			int32 N = 0;
			uint64 Pc = 0, Fp = 0;
#if PLATFORM_CPU_ARM_FAMILY
			arm_thread_state64_t State;
			mach_msg_type_number_t StateCount = ARM_THREAD_STATE64_COUNT;
			if (thread_get_state(Game.Port, ARM_THREAD_STATE64, (thread_state_t)&State, &StateCount) == KERN_SUCCESS)
			{
				Pc = (uint64)arm_thread_state64_get_pc(State);
				Fp = (uint64)arm_thread_state64_get_fp(State);
			}
#else
			x86_thread_state64_t State;
			mach_msg_type_number_t StateCount = x86_THREAD_STATE64_COUNT;
			if (thread_get_state(Game.Port, x86_THREAD_STATE64, (thread_state_t)&State, &StateCount) == KERN_SUCCESS)
			{
				Pc = State.__rip;
				Fp = State.__rbp;
			}
#endif
			// Return addresses may carry a pointer signature (arm64e): keep
			// the address bits only.
			constexpr uint64 AddressBits = 0x0000'7FFF'FFFF'FFFFull;
			if (Pc) Out[N++] = Pc & AddressBits;
			while (N < MaxFrames && Fp >= Game.StackLow && Fp + 16 <= Game.StackHigh && (Fp & 7) == 0)
			{
				const uint64* Record = reinterpret_cast<const uint64*>(Fp);
				const uint64 Next = Record[0];
				const uint64 Return = Record[1] & AddressBits;
				if (!Return) break;
				Out[N++] = Return;
				if (Next <= Fp) break;
				Fp = Next;
			}
			thread_resume(Game.Port);
			return N;
#else
			return 0;
#endif
		}

		FGameThreadInfo Game;
		FCriticalSection Mutex;
		double LastBeat = 0;
		double StalledSince = 0;
		const TCHAR* StallActivity = nullptr;
		bool bStalled = false;
		bool bStackTaken = false;
		std::atomic<bool> bStop{false};
	};

	FAcWatchdogThread* GWatchdog = nullptr;
	FRunnableThread* GThread = nullptr;
	FDelegateHandle GBeginFrame, GEndFrame;

	FAutoConsoleCommand GStallCommand(
		TEXT("ac.Stall"),
		TEXT("Sleep the game thread for N ms (default 800), to see the watchdog log the stall and its stack."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			const int32 Ms = Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 800;
			UE_LOG(LogAutocraft, Log, TEXT("ac.Stall: sleeping the game thread %d ms"), Ms);
			FAcActivity Label(TEXT("ac.Stall"));
			FPlatformProcess::SleepNoStats(Ms / 1000.0f);
		}));
}

void AcWatchdog::Start()
{
	check(IsInGameThread());
	if (GThread || IsRunningCommandlet() || (GIsEditor && !IsRunningGame())) return;
	FGameThreadInfo Game;
#if PLATFORM_MAC
	const pthread_t Self = pthread_self();
	Game.Port = pthread_mach_thread_np(Self);
	Game.StackHigh = (uint64)pthread_get_stackaddr_np(Self);
	Game.StackLow = Game.StackHigh - (uint64)pthread_get_stacksize_np(Self);
	Game.bValid = Game.Port != MACH_PORT_NULL;
#endif
	GWatchdog = new FAcWatchdogThread(Game);
	GThread = FRunnableThread::Create(GWatchdog, TEXT("AcWatchdog"), 64 * 1024, TPri_AboveNormal);
	if (!GThread)
	{
		delete GWatchdog;
		GWatchdog = nullptr;
		return;
	}
	GBeginFrame = FCoreDelegates::OnBeginFrame.AddStatic(&AcWatchdog::Beat);
	GEndFrame = FCoreDelegates::OnEndFrame.AddStatic(&AcWatchdog::Beat);
	FCoreDelegates::OnEnginePreExit.AddStatic(&AcWatchdog::Stop);
	UE_LOG(LogAutocraft, Log, TEXT("watchdog: on (stalls over %d ms, stack at %d ms)"), CVarAcStallMs.GetValueOnGameThread(),
		CVarAcStallStackMs.GetValueOnGameThread());
}

void AcWatchdog::Stop()
{
	if (!GThread) return;
	FCoreDelegates::OnBeginFrame.Remove(GBeginFrame);
	FCoreDelegates::OnEndFrame.Remove(GEndFrame);
	GThread->Kill(true);
	delete GThread;
	GThread = nullptr;
	delete GWatchdog;
	GWatchdog = nullptr;
}

void AcWatchdog::Beat()
{
	if (GWatchdog) GWatchdog->Beat();
}

bool AcWatchdog::IsRunning()
{
	return GThread != nullptr;
}

int32 AcWatchdog::StallCount()
{
	return GWatchdog ? GWatchdog->Stalls.load() : 0;
}

FAcActivity::FAcActivity(const TCHAR* Label)
	: Previous(nullptr)
{
	if (!IsInGameThread()) return;
	Previous = GActivity.exchange(Label);
}

FAcActivity::~FAcActivity()
{
	if (Previous) GActivity.store(Previous);
}

const TCHAR* FAcActivity::Current()
{
	return GActivity.load();
}
